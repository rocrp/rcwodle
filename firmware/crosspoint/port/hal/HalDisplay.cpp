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

#include "FrameBlit.h"
#include "WodlePsram.h"
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

/* 4-gray waveform LUT, vendor reference (refs/epd/UC8279_4gray_reference.c).
 * Vendor scheme is ABSOLUTE 4-level: cell (old,new) selects the target shade
 * via bank — WW(1,1)=white, BB(0,0)=black, BW(0,1)=dark grey, WB(1,0)=light
 * grey. We repurpose the panel-tuned grey drives for a DIFFERENTIAL OVERLAY
 * (upstream X3 architecture): see s_lutGreyOverlay construction below. */
static const uint8_t LUT_GREY_VENDOR[245] = {
    /* VCOM */
    0x01,0x08,0x02,0x08,0x03,0x01,0x01, 0x01,0x09,0x03,0x04,0x03,0x01,0x01,
    0x01,0x0A,0x02,0x01,0x01,0x01,0x01, 0x01,0x02,0x02,0x00,0x00,0x01,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* WW -> white */
    0x01,0x08,0x02,0x08,0x03,0x01,0x01, 0x01,0x49,0x43,0x44,0x03,0x01,0x01,
    0x01,0x8A,0x82,0x81,0x81,0x01,0x01, 0x01,0x82,0x02,0x00,0x00,0x01,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* BW -> dark grey */
    0x01,0x88,0x82,0x08,0x03,0x01,0x01, 0x01,0x49,0x43,0x04,0x03,0x01,0x01,
    0x01,0x0A,0x82,0x01,0x01,0x01,0x01, 0x01,0x02,0x02,0x00,0x00,0x01,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* WB -> light grey */
    0x01,0x88,0x02,0x08,0x03,0x01,0x01, 0x01,0x49,0x43,0x04,0x03,0x01,0x01,
    0x01,0x0A,0x82,0x81,0x81,0x01,0x01, 0x01,0x02,0x02,0x00,0x00,0x01,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* BB -> black */
    0x01,0x88,0x82,0x88,0x03,0x01,0x01, 0x01,0x49,0x43,0x44,0x03,0x01,0x01,
    0x01,0x0A,0x02,0x01,0x01,0x01,0x01, 0x01,0x02,0x42,0x00,0x00,0x01,0x00,
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

enum class Lut : uint8_t { None, GC, DU, GREY };
static Lut s_lutLoaded = Lut::None;
static int s_fastSinceGc = 0;
#define FAST_REFRESHES_PER_GC 10 /* vendor guidance */

/* Differential-overlay grey LUT, constructed from LUT_GREY_VENDOR:
 * GfxRenderer's AA pass writes flag planes (MSB plane -> 0x10 "old", LSB
 * plane -> 0x13 "new"), so cells mean: (1,1)=dark grey -> WW slot gets the
 * vendor drive-to-dark-grey rows; (1,0)=light grey -> WB slot keeps the
 * vendor drive-to-light-grey rows; (0,0)=untouched pixel -> BB slot must be
 * a NO-OP, as must the never-generated (0,1) BW slot. No-op rows reuse the
 * VCOM bank's phase timings with the level byte zeroed (pixel held at GND
 * through the same frame envelope). HIL tuning knob: if untouched pixels
 * shift, try all-zero no-op banks instead. */
static uint8_t s_lutGreyOverlay[245];
static void buildGreyOverlayLut()
{
    static bool built = false;
    if (built) return;
    memset(s_lutGreyOverlay, 0, sizeof(s_lutGreyOverlay));
    /* VCOM verbatim */
    memcpy(&s_lutGreyOverlay[0], &LUT_GREY_VENDOR[0], 49);
    /* WW slot (cell 1,1 = dark grey) <- vendor BW bank (drive to dark grey) */
    memcpy(&s_lutGreyOverlay[49], &LUT_GREY_VENDOR[98], 49);
    /* BW slot (cell 0,1, never generated) + BB slot (cell 0,0, untouched):
     * VCOM timings with level byte zeroed per 7-byte row. */
    for (int bank = 2; bank <= 4; bank += 2)
    {
        for (int row = 0; row < 7; row++)
        {
            const uint8_t *src = &LUT_GREY_VENDOR[row * 7];
            uint8_t *dst = &s_lutGreyOverlay[bank * 49 + row * 7];
            memcpy(dst, src, 7);
            dst[0] = 0x00; /* levels = GND for every phase */
        }
    }
    /* WB slot (cell 1,0 = light grey) <- vendor WB bank, in place */
    memcpy(&s_lutGreyOverlay[147], &LUT_GREY_VENDOR[147], 49);
    built = true;
}

static void epdLoadLut(Lut which)
{
    if (s_lutLoaded == which) return;
    const uint8_t *lut = LUT_GC;
    uint8_t cdi = 0x97;
    if (which == Lut::DU)
    {
        lut = LUT_DU;
        cdi = 0xD7;
    }
    else if (which == Lut::GREY)
    {
        buildGreyOverlayLut();
        lut = s_lutGreyOverlay;
        cdi = 0x97;
    }
    epdCmd(0x50); epdData(cdi); /* VCOM/data interval */
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
    FrameBlit::fill(s_frameBuffer, color);
}

uint8_t *HalDisplay::getFrameBuffer() const { return s_frameBuffer; }

void HalDisplay::drawImage(const uint8_t *imageData, uint16_t x, uint16_t y, uint16_t w,
                           uint16_t h, bool) const
{
    FrameBlit::blit(s_frameBuffer, imageData, x, y, w, h);
}

void HalDisplay::drawImageTransparent(const uint8_t *imageData, uint16_t x, uint16_t y,
                                      uint16_t w, uint16_t h, bool) const
{
    FrameBlit::blitTransparent(s_frameBuffer, imageData, x, y, w, h);
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

    /* Timing instrumentation: write vs refresh cost is THE input for the
     * deferred LCDC/hw-SPI decision (todo.md) — every HIL page turn logs it. */
    const unsigned long t0 = rt_tick_get_millisecond();
    epdCmd(0x13);
    for (uint32_t i = 0; i < BUFFER_SIZE; i++) epdData(s_frameBuffer[i]);
    const unsigned long t1 = rt_tick_get_millisecond();
    epdCmd(0x12);
    epdWaitBusy(8000);
    if (!wantFast)
        rt_thread_mdelay(500); /* GC settle margin (BUSY already waited) */
    const unsigned long t2 = rt_tick_get_millisecond();
    epdCmd(0x10); /* sync old RAM = differential base for the next DU */
    for (uint32_t i = 0; i < BUFFER_SIZE; i++) epdData(s_frameBuffer[i]);
    rt_kprintf("[HalDisplay] %s write=%lums refresh=%lums sync=%lums\n", wantFast ? "DU" : "GC",
               t1 - t0, t2 - t1, rt_tick_get_millisecond() - t2);
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

/* ------------------------------------------------------------- 4-gray pass
 * Differential overlay, upstream-X3 style: the BW page is already displayed;
 * GfxRenderer re-renders the page twice into flag planes (MSB = any-gray,
 * LSB = dark-gray-only, bit 1 = flagged) which we stage in heap buffers.
 * displayGrayBuffer() writes MSB->0x10 / LSB->0x13, runs the overlay grey
 * LUT, and only flagged cells get driven: (1,1)->dark grey, (1,0)->light
 * grey, (0,0)->no-op. Afterwards both controller RAMs hold flag planes, so
 * the reader calls cleanupGrayscaleBuffers(bw) to restore the differential
 * base (it does this after restoring its BW framebuffer). Plane backing
 * comes from the otherwise-unused 8MB PSRAM when the boot probe passed
 * (permanent carve-out, reused across passes) and falls back to 2x52KB of
 * SRAM heap only while a pass is in flight; allocation failure degrades to
 * a plain BW page (AA pass skipped). ZERO HIL yet — waveform quality is
 * HIL checklist material. */
static uint8_t *s_grayMsb = nullptr; /* staged-plane pointer (null = not staged) */
static uint8_t *s_grayLsb = nullptr;
static uint8_t *s_grayMsbPsram = nullptr; /* permanent PSRAM backing, alloc'd once */
static uint8_t *s_grayLsbPsram = nullptr;

static void freeGrayPlanes()
{
    /* PSRAM backings persist (carve-outs are never freed); only heap
     * fallbacks are returned. Clearing the staged pointers is what marks
     * the planes as consumed either way. */
    if (s_grayMsb && s_grayMsb != s_grayMsbPsram) free(s_grayMsb);
    if (s_grayLsb && s_grayLsb != s_grayLsbPsram) free(s_grayLsb);
    s_grayMsb = nullptr;
    s_grayLsb = nullptr;
}

static bool stageGrayPlane(uint8_t *&slot, uint8_t *&psramBacking, const uint8_t *plane)
{
    if (!plane) return false;
    if (!slot)
    {
        if (!psramBacking) psramBacking = static_cast<uint8_t *>(WodlePsram::alloc(HalDisplay::BUFFER_SIZE));
        slot = psramBacking ? psramBacking : static_cast<uint8_t *>(malloc(HalDisplay::BUFFER_SIZE));
    }
    if (!slot)
    {
        rt_kprintf("[HalDisplay] gray plane alloc failed, skipping AA pass\n");
        return false;
    }
    memcpy(slot, plane, HalDisplay::BUFFER_SIZE);
    return true;
}

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t *lsbBuffer)
{
    if (!stageGrayPlane(s_grayLsb, s_grayLsbPsram, lsbBuffer)) freeGrayPlanes();
}

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t *msbBuffer)
{
    if (!stageGrayPlane(s_grayMsb, s_grayMsbPsram, msbBuffer)) freeGrayPlanes();
}

void HalDisplay::copyGrayscaleBuffers(const uint8_t *lsbBuffer, const uint8_t *msbBuffer)
{
    copyGrayscaleLsbBuffers(lsbBuffer);
    copyGrayscaleMsbBuffers(msbBuffer);
}

void HalDisplay::displayGrayBuffer(bool)
{
    if (!s_grayMsb || !s_grayLsb)
    {
        /* Planes never staged (alloc failure) — page is already correct BW. */
        freeGrayPlanes();
        return;
    }

    epdLoadLut(Lut::GREY);
    epdCmd(0x10); /* "old" RAM <- MSB (any-gray) flags */
    for (uint32_t i = 0; i < BUFFER_SIZE; i++) epdData(s_grayMsb[i]);
    epdCmd(0x13); /* "new" RAM <- LSB (dark-gray) flags */
    for (uint32_t i = 0; i < BUFFER_SIZE; i++) epdData(s_grayLsb[i]);
    epdCmd(0x12);
    epdWaitBusy(8000);

    freeGrayPlanes();

    /* The reader's full-buffer AA path never calls cleanupGrayscaleBuffers —
     * both controller RAMs now hold flag planes, useless as a DU diff base.
     * Force the next refresh onto the GC branch: GC settles every cell to the
     * NEW frame regardless of stale old data, then re-syncs 0x10. */
    s_fastSinceGc = FAST_REFRESHES_PER_GC;
}

/* Re-sync both controller RAMs from the restored BW frame so the next DU/GC
 * refresh diffs against real content (the gray pass left flag planes there). */
void HalDisplay::cleanupGrayscaleBuffers(const uint8_t *bwBuffer)
{
    freeGrayPlanes();
    if (!bwBuffer) return;
    epdCmd(0x13);
    for (uint32_t i = 0; i < BUFFER_SIZE; i++) epdData(bwBuffer[i]);
    epdCmd(0x10);
    for (uint32_t i = 0; i < BUFFER_SIZE; i++) epdData(bwBuffer[i]);
}

void HalDisplay::writeGrayscalePlaneStrip(bool, const uint8_t *, uint16_t, uint16_t) {}
bool HalDisplay::supportsStripGrayscale() const { return false; }
