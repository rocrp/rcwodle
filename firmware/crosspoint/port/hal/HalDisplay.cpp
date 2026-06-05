/* WODLE-PORT: UC8179C backend for HalDisplay — bit-banged GPIO SPI, ported
 * from the HIL-proven hello_wodle driver (firmware/hello_wodle/src/epd.c).
 *
 * Wiring (PAxx = pad index, refs/schematic/README.md):
 *   RST=PA0  BUSY=PA2(TE, active-low)  CS=PA3  CLK=PA4  SDA=PA5  DC=PA6
 *
 * Pinmux note: the linked sf32lb52-lcd_base bsp muxes PA02-PA08 to LCDC1_*;
 * rt_pin_* only touches the GPIO block, so HAL_PIN_Set(GPIO_*) is mandatory.
 *
 * VRES quirk: resolution register must declare 600 gate lines (panel bonds
 * 528 physical gates from both ends of a 600-line scan); declaring 528 leaves
 * a dead band mid-screen. Vendor ref: "3F要写600=58".
 *
 * Framebuffer: 1bpp, bit set = white (matches upstream + 0x13 polarity),
 * row-major over 528 gate rows x 99 bytes. */

#include "HalDisplay.h"

#include <rtdevice.h>
#include <rtthread.h>

#include "bf0_hal.h"

#define PIN_EPD_RST 0
#define PIN_EPD_BUSY 2
#define PIN_EPD_CS 3
#define PIN_EPD_CLK 4
#define PIN_EPD_SDA 5
#define PIN_EPD_DC 6

HalDisplay display;

static uint8_t s_frameBuffer[HalDisplay::BUFFER_SIZE];
static bool s_panelInitialized = false;

/* GC (full-refresh) waveform LUT — 5 banks x 49 bytes, vendor reference. */
static const uint8_t LUT_GC[245] = {
    /* VCOM */
    0x01,0x18,0x04,0x0E,0x0A,0x01,0x01, 0x01,0x0A,0x00,0x00,0x00,0x01,0x01,
    0x01,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* WW */
    0x01,0x58,0x04,0x8E,0x8A,0x01,0x01, 0x01,0x0A,0x00,0x00,0x00,0x01,0x01,
    0x01,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* BW */
    0x01,0x18,0x04,0x8E,0x8A,0x01,0x01, 0x01,0x0A,0x00,0x00,0x00,0x01,0x01,
    0x01,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* WB */
    0x01,0x18,0x04,0x4E,0x0A,0x01,0x01, 0x01,0x4A,0x00,0x00,0x00,0x01,0x01,
    0x01,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* BB */
    0x01,0x98,0x04,0x4E,0x0A,0x01,0x01, 0x01,0x4A,0x00,0x00,0x00,0x01,0x01,
    0x01,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};

/* Direct DOSR/DOCR stores: the 52KB frame write runs ~52K x 8 clock edges;
 * rt_pin_write's device-framework overhead makes that seconds. All EPD pins
 * live in GPIO1 bank 0 (pads 0..6), so single-store set/clear is safe. */
#define EPD_MASK(pin) (1u << (pin))
static inline void pinHigh(int pin) { hwp_gpio1->DOSR = EPD_MASK(pin); }
static inline void pinLow(int pin) { hwp_gpio1->DOCR = EPD_MASK(pin); }

/* DU (fast/differential) waveform LUT — vendor reference. Guidance from the
 * same file: ~every 10 DU refreshes run 1 GC; use GC for big tonal areas. */
static const uint8_t LUT_DU[245] = {
    /* VCOM */
    0x01,0x06,0x01,0x06,0x06,0x01,0x01, 0x01,0x04,0x01,0x01,0x00,0x01,0x01,
    0x01,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* WW */
    0x01,0x06,0x81,0x06,0x06,0x01,0x01, 0x01,0x04,0x01,0x01,0x00,0x01,0x01,
    0x01,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* BW */
    0x01,0x86,0x81,0x86,0x86,0x01,0x01, 0x01,0x84,0x81,0x01,0x00,0x01,0x01,
    0x01,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* WB */
    0x01,0x46,0x41,0x46,0x46,0x01,0x01, 0x01,0x44,0x41,0x01,0x00,0x01,0x01,
    0x01,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* BB */
    0x01,0x06,0x01,0x06,0x06,0x01,0x01, 0x01,0x04,0x01,0x41,0x00,0x01,0x01,
    0x01,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};

static void epdSpiByte(uint8_t b)
{
    for (int i = 0; i < 8; i++)
    {
        pinLow(PIN_EPD_CLK);
        if (b & 0x80)
            pinHigh(PIN_EPD_SDA);
        else
            pinLow(PIN_EPD_SDA);
        b <<= 1;
        pinHigh(PIN_EPD_CLK); /* sample on rising edge */
    }
    pinLow(PIN_EPD_CLK);
}

static void epdCmd(uint8_t c)
{
    pinLow(PIN_EPD_DC);
    pinLow(PIN_EPD_CS);
    epdSpiByte(c);
    pinHigh(PIN_EPD_CS);
}

static void epdData(uint8_t d)
{
    pinHigh(PIN_EPD_DC);
    pinLow(PIN_EPD_CS);
    epdSpiByte(d);
    pinHigh(PIN_EPD_CS);
}

static void epdWaitBusy(int maxMs)
{
    while (rt_pin_read(PIN_EPD_BUSY) == PIN_LOW && maxMs-- > 0)
        rt_thread_mdelay(1);
}

static void epdGpioInit()
{
    HAL_PIN_Set(PAD_PA00, GPIO_A0, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA02, GPIO_A2, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA03, GPIO_A3, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA04, GPIO_A4, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA05, GPIO_A5, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA06, GPIO_A6, PIN_NOPULL, 1);

    const int outs[] = {PIN_EPD_RST, PIN_EPD_CS, PIN_EPD_CLK, PIN_EPD_SDA, PIN_EPD_DC};
    for (unsigned i = 0; i < sizeof(outs) / sizeof(outs[0]); i++)
        rt_pin_mode(outs[i], PIN_MODE_OUTPUT);
    rt_pin_mode(PIN_EPD_BUSY, PIN_MODE_INPUT_PULLUP);
    rt_pin_write(PIN_EPD_CS, PIN_HIGH);
    rt_pin_write(PIN_EPD_CLK, PIN_LOW);
    rt_pin_write(PIN_EPD_RST, PIN_HIGH);
}

static void epdReset()
{
    rt_pin_write(PIN_EPD_RST, PIN_LOW);
    rt_thread_mdelay(20);
    rt_pin_write(PIN_EPD_RST, PIN_HIGH);
    rt_thread_mdelay(100);
    epdWaitBusy(1000);
}

static void epdPanelInit()
{
    epdCmd(0x00); epdData(0x3F); epdData(0x4A);
    epdCmd(0x03); epdData(0x10);
    epdCmd(0x01); epdData(0x03); epdData(0x00);
    epdData(0x78); epdData(0x78); epdData(0x17);
    epdCmd(0x06); epdData(0x25); epdData(0x25); epdData(0x3C);
    epdCmd(0x82); epdData(0x24);
    epdCmd(0x30); epdData(0x0F);
    epdCmd(0x61); epdData(0x03); epdData(0x18); /* HRES=792 */
    epdData(0x02); epdData(0x58);               /* VRES=600 (NOT 528 — see header) */
    epdWaitBusy(1000);
    epdCmd(0x65); epdData(0x00); epdData(0x00); epdData(0x00); epdData(0x00);
    epdCmd(0xE1); epdData(0x02);
    epdCmd(0x10); /* old RAM = white */
    for (uint32_t i = 0; i < HalDisplay::BUFFER_SIZE; i++) epdData(0xFF);
    epdCmd(0x04); /* power on */
    epdWaitBusy(2000);
    rt_thread_mdelay(50);
}

enum class Lut : uint8_t { None, GC, DU };
static Lut s_lutLoaded = Lut::None;
static int s_fastSinceGc = 0;
#define FAST_REFRESHES_PER_GC 10 /* vendor guidance */

static void epdLoadLut(Lut which)
{
    if (s_lutLoaded == which) return;
    const uint8_t *lut = (which == Lut::DU) ? LUT_DU : LUT_GC;
    epdCmd(0x50); epdData(which == Lut::DU ? 0xD7 : 0x97); /* VCOM/data interval */
    static const uint8_t bankCmd[5] = {0x20, 0x21, 0x22, 0x23, 0x24};
    for (int bank = 0; bank < 5; bank++)
    {
        epdCmd(bankCmd[bank]);
        for (int i = 0; i < 49; i++) epdData(lut[bank * 49 + i]);
    }
    s_lutLoaded = which;
}

static void epdLoadLutGc() { epdLoadLut(Lut::GC); }

/* ----------------------------------------------------------------- HalDisplay */
HalDisplay::HalDisplay() = default;
HalDisplay::~HalDisplay() = default;

void HalDisplay::begin(bool seamless)
{
    (void)seamless;
    if (s_panelInitialized) return;
    epdGpioInit();
    epdReset();
    epdPanelInit();
    epdLoadLutGc();
    memset(s_frameBuffer, 0xFF, sizeof(s_frameBuffer));
    s_panelInitialized = true;
}

void HalDisplay::clearScreen(uint8_t color) const
{
    memset(s_frameBuffer, color, sizeof(s_frameBuffer));
}

uint8_t *HalDisplay::getFrameBuffer() const { return s_frameBuffer; }

void HalDisplay::drawImage(const uint8_t *imageData, uint16_t x, uint16_t y, uint16_t w,
                           uint16_t h, bool) const
{
    /* byte-aligned 1bpp blit (x and w in pixels; x must be byte-aligned for
     * the fast path, else fall back to bitwise) */
    const uint16_t wBytes = w / 8;
    for (uint16_t row = 0; row < h; row++)
    {
        uint32_t dy = y + row;
        if (dy >= DISPLAY_HEIGHT) break;
        if ((x % 8) == 0)
        {
            uint32_t dst = dy * DISPLAY_WIDTH_BYTES + x / 8;
            uint32_t n = wBytes;
            if (x / 8 + n > DISPLAY_WIDTH_BYTES) n = DISPLAY_WIDTH_BYTES - x / 8;
            memcpy(&s_frameBuffer[dst], &imageData[(uint32_t)row * wBytes], n);
        }
        else
        {
            for (uint16_t col = 0; col < w; col++)
            {
                uint32_t dx = x + col;
                if (dx >= DISPLAY_WIDTH) break;
                bool white = imageData[(uint32_t)row * wBytes + col / 8] & (0x80 >> (col % 8));
                uint32_t idx = dy * DISPLAY_WIDTH_BYTES + dx / 8;
                uint8_t mask = 0x80 >> (dx % 8);
                if (white) s_frameBuffer[idx] |= mask;
                else s_frameBuffer[idx] &= ~mask;
            }
        }
    }
}

void HalDisplay::drawImageTransparent(const uint8_t *imageData, uint16_t x, uint16_t y,
                                      uint16_t w, uint16_t h, bool) const
{
    /* black pixels only (white = transparent) */
    const uint16_t wBytes = w / 8;
    for (uint16_t row = 0; row < h; row++)
    {
        uint32_t dy = y + row;
        if (dy >= DISPLAY_HEIGHT) break;
        for (uint16_t col = 0; col < w; col++)
        {
            uint32_t dx = x + col;
            if (dx >= DISPLAY_WIDTH) break;
            bool white = imageData[(uint32_t)row * wBytes + col / 8] & (0x80 >> (col % 8));
            if (!white)
                s_frameBuffer[dy * DISPLAY_WIDTH_BYTES + dx / 8] &= ~(0x80 >> (dx % 8));
        }
    }
}

void HalDisplay::displayBuffer(RefreshMode mode, bool turnOffScreen)
{
    refreshDisplay(mode, turnOffScreen);
}

void HalDisplay::refreshDisplay(RefreshMode mode, bool)
{
    /* FAST -> DU differential (vs old RAM); FULL/HALF -> GC. Auto-promote to
     * GC every FAST_REFRESHES_PER_GC fast updates (ghosting management). */
    bool wantFast = (mode == FAST_REFRESH) && (s_fastSinceGc < FAST_REFRESHES_PER_GC);
    if (wantFast)
    {
        epdLoadLut(Lut::DU);
        s_fastSinceGc++;
    }
    else
    {
        epdLoadLut(Lut::GC);
        s_fastSinceGc = 0;
    }

    epdCmd(0x13);
    for (uint32_t i = 0; i < BUFFER_SIZE; i++) epdData(s_frameBuffer[i]);
    epdCmd(0x12);
    epdWaitBusy(8000);
    if (!wantFast)
        rt_thread_mdelay(500); /* GC settle margin (BUSY already waited) */
    epdCmd(0x10); /* sync old RAM = differential base for the next DU */
    for (uint32_t i = 0; i < BUFFER_SIZE; i++) epdData(s_frameBuffer[i]);
}

void HalDisplay::deepSleep()
{
    epdCmd(0x02); /* power off */
    epdWaitBusy(5000);
    epdCmd(0x07);
    epdData(0xA5); /* deep sleep */
    s_panelInitialized = false;
    s_lutLoaded = Lut::None;
}

/* Grayscale: not supported yet on the wodle backend. */
void HalDisplay::copyGrayscaleBuffers(const uint8_t *, const uint8_t *) {}
void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t *) {}
void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t *) {}
void HalDisplay::cleanupGrayscaleBuffers(const uint8_t *) {}
void HalDisplay::displayGrayBuffer(bool turnOffScreen) { displayBuffer(FULL_REFRESH, turnOffScreen); }
void HalDisplay::writeGrayscalePlaneStrip(bool, const uint8_t *, uint16_t, uint16_t) {}
bool HalDisplay::supportsStripGrayscale() const { return false; }
