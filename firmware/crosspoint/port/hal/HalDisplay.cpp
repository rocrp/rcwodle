/* WODLE-PORT: UC8179C backend for HalDisplay.
 *
 * Transport (spi_epd_demo, the vendor-quality reference): LCDC1 SPI DCX —
 * commands via HAL_LCDC_WriteU8Reg, bulk data via direct LCD_IF single-write
 * registers (4 bytes per transaction). Build with -DWODLE_EPD_BITBANG to fall
 * back to the HIL-proven GPIO bit-bang path (same wiring, GPIO-muxed).
 *
 * Wiring (PAxx = pad index, refs/schematic/README.md):
 *   RST=PA0  BUSY=PA2(TE, active-low)  CS=PA3  CLK=PA4  SDA/DIO0=PA5  DC/DIO1=PA6
 *
 * VRES quirk: resolution register must declare 600 gate lines (panel bonds
 * 528 physical gates from both ends of a 600-line scan); declaring 528 leaves
 * a dead band mid-screen. Vendor ref: "3F要写600=58".
 *
 * Framebuffer: 1bpp, bit set = white (matches upstream + 0x13 polarity),
 * row-major over 528 gate rows x 99 bytes.
 *
 * Refresh modes:
 *   GC   full quality (FULL/HALF, auto-promoted every Nth fast refresh)
 *   DU   fast differential (FAST_REFRESH page turns)
 *   DU partial window (refreshWindow — spi_epd_demo recipe: 0x91/0x90 enter,
 *        column-span data per gate row, 0x12, 0x10 sync, 0x92 exit)
 *   GRAY4 absolute 4-gray full refresh (AA pass — planes composed from the
 *        displayed BW page + GfxRenderer's MSB/LSB flag planes; replaces the
 *        earlier blind differential-overlay LUT rearrangement) */

#include "HalDisplay.h"

#include <rtdevice.h>
#include <rtthread.h>

#include "FrameBlit.h"
#include "WodlePsram.h"
#include "bf0_hal.h"

#ifndef WODLE_EPD_BITBANG
#include "bf0_hal_lcdc.h"
#endif

#define PIN_EPD_RST 0
#define PIN_EPD_BUSY 2
#define PIN_EPD_CS 3
#define PIN_EPD_CLK 4
#define PIN_EPD_SDA 5
#define PIN_EPD_DC 6

/* GC settle margin between BUSY release and the 0x10 old-frame sync. The
 * spi_epd_demo lab default is a conservative 3000ms — a HIL comparison point,
 * not a UX default. */
#define EPD_GC_SETTLE_MS 500

HalDisplay display;

static uint8_t s_frameBuffer[HalDisplay::BUFFER_SIZE];
static bool s_panelInitialized = false;
/* True while the controller RAMs hold 4-gray planes (after an AA pass): not a
 * valid DU diff base, and partial refresh must be refused until the next full
 * refresh rewrites both RAMs (codex-flagged interaction). */
static bool s_ramsHoldGrayPlanes = false;

/* GC (full-refresh) waveform LUT — 5 banks x 49 bytes, vendor reference
 * (byte-identical to spi_epd_demo's s_lut_gc — good cross-validation). */
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

/* 4-gray waveform LUT, vendor reference — used UNMODIFIED as the absolute
 * full-frame 4-gray waveform, exactly like spi_epd_demo (which describes it
 * as "the verified UC8279 gray reference"). Plane semantics: 0x10 RAM bit =
 * gray bit1, 0x13 RAM bit = gray bit0; 00=black 01=dark 10=light 11=white.
 *
 * HIL knob: refs/xiaodouzi_demo runs the byte-identical LUT with the INVERSE
 * convention — 0x50=0x00 (DDX flipped) before the gray banks and (0,0)=white
 * /(1,1)=black planes. Both claim panel-verified; the DDX bits in 0x50 invert
 * data interpretation, so they're plausibly equivalent. If HIL item 11b shows
 * inverted/garbled grays, switch to that pair (CDI write + plane inversion). */
static const uint8_t LUT_GRAY4[245] = {
    /* VCOM */
    0x01,0x08,0x02,0x08,0x03,0x01,0x01, 0x01,0x09,0x03,0x04,0x03,0x01,0x01,
    0x01,0x0A,0x02,0x01,0x01,0x01,0x01, 0x01,0x02,0x02,0x00,0x00,0x01,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* (1,1) -> white */
    0x01,0x08,0x02,0x08,0x03,0x01,0x01, 0x01,0x49,0x43,0x44,0x03,0x01,0x01,
    0x01,0x8A,0x82,0x81,0x81,0x01,0x01, 0x01,0x82,0x02,0x00,0x00,0x01,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* (0,1) -> dark grey */
    0x01,0x88,0x82,0x08,0x03,0x01,0x01, 0x01,0x49,0x43,0x04,0x03,0x01,0x01,
    0x01,0x0A,0x82,0x01,0x01,0x01,0x01, 0x01,0x02,0x02,0x00,0x00,0x01,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* (1,0) -> light grey */
    0x01,0x88,0x02,0x08,0x03,0x01,0x01, 0x01,0x49,0x43,0x04,0x03,0x01,0x01,
    0x01,0x0A,0x82,0x81,0x81,0x01,0x01, 0x01,0x02,0x02,0x00,0x00,0x01,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* (0,0) -> black */
    0x01,0x88,0x82,0x88,0x03,0x01,0x01, 0x01,0x49,0x43,0x44,0x03,0x01,0x01,
    0x01,0x0A,0x02,0x01,0x01,0x01,0x01, 0x01,0x02,0x42,0x00,0x00,0x01,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};

/* ------------------------------------------------------------- transport */

#ifndef WODLE_EPD_BITBANG

/* LCDC1 SPI DCX path, ported from spi_epd_demo/firmware/common/wodle_epd.c:
 * commands through HAL_LCDC_WriteU8Reg, bulk data through the LCD_IF
 * single-write registers (4 bytes per transaction, CS auto). */
#define EPD_LCDC_SPI_FREQ 12000000
#define EPD_LCDC_WAIT_BUSY_MS 1000
#define EPD_LCDC_SINGLE_WR_DATA ((1UL << LCD_IF_LCD_SINGLE_TYPE_Pos) | LCD_IF_LCD_SINGLE_WR_TRIG)

static LCDC_HandleTypeDef s_lcdc;
static bool s_lcdcInitialized = false;

static void epdBusPinmux()
{
    HAL_PIN_Set(PAD_PA00, GPIO_A0, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA02, GPIO_A2, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA03, LCDC1_SPI_CS, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA04, LCDC1_SPI_CLK, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA05, LCDC1_SPI_DIO0, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA06, LCDC1_SPI_DIO1, PIN_NOPULL, 1);

    rt_pin_mode(PIN_EPD_RST, PIN_MODE_OUTPUT);
    rt_pin_mode(PIN_EPD_BUSY, PIN_MODE_INPUT_PULLUP);
    rt_pin_write(PIN_EPD_RST, PIN_HIGH);
}

static bool epdBusInit()
{
    epdBusPinmux();
    if (s_lcdcInitialized) return true;

    LCDC_InitTypeDef cfg;
    memset(&s_lcdc, 0, sizeof(s_lcdc));
    memset(&cfg, 0, sizeof(cfg));
    cfg.lcd_itf = LCDC_INTF_SPI_DCX_1DATA;
    cfg.freq = EPD_LCDC_SPI_FREQ;
    cfg.color_mode = LCDC_PIXEL_FORMAT_RGB565;
    cfg.cfg.spi.dummy_clock = 0;
    cfg.cfg.spi.syn_mode = HAL_LCDC_SYNC_DISABLE;
    cfg.cfg.spi.cs_polarity = 0;
    cfg.cfg.spi.clk_polarity = 0;
    cfg.cfg.spi.clk_phase = 0;
    cfg.cfg.spi.vsyn_polarity = 1;
    cfg.cfg.spi.vsyn_delay_us = 0;
    cfg.cfg.spi.hsyn_num = 0;
    cfg.cfg.spi.bytes_gap_us = 0;
    cfg.cfg.spi.readback_from_Dx = 0;

    s_lcdc.Instance = hwp_lcdc1;
    memcpy(&s_lcdc.Init, &cfg, sizeof(cfg));
    if (HAL_LCDC_Init(&s_lcdc) != HAL_OK)
    {
        rt_kprintf("[HalDisplay] LCDC init FAILED\n");
        return false;
    }
    s_lcdcInitialized = true;
    return true;
}

static bool epdLcdcWaitIdle()
{
    const rt_tick_t start = rt_tick_get();
    const rt_tick_t timeout = EPD_LCDC_WAIT_BUSY_MS * RT_TICK_PER_SECOND / 1000;
    while ((s_lcdc.Instance->STATUS & LCD_IF_STATUS_LCD_BUSY) ||
           (s_lcdc.Instance->LCD_SINGLE & LCD_IF_LCD_SINGLE_LCD_BUSY))
    {
        if (rt_tick_get() - start > timeout) return false;
        rt_thread_yield();
    }
    return true;
}

/* Bus-error latch (demo pattern): helpers record the first failure; refresh
 * entry points check-and-clear it and ABORT before mutating software state —
 * a stalled LCDC must not leave us believing controller RAM is current. */
static bool s_busError = false;
static void epdNoteBusError(const char *what)
{
    if (!s_busError) rt_kprintf("[HalDisplay] BUS ERROR: %s\n", what);
    s_busError = true;
}

static void epdCmd(uint8_t c)
{
    if (HAL_LCDC_WriteU8Reg(&s_lcdc, c, RT_NULL, 0) != HAL_OK) epdNoteBusError("cmd");
}

/* Command + short parameter block in ONE WriteU8Reg transaction — exactly the
 * demo's epd_write_command_data (CS held across cmd+params). Used for panel
 * init, CDI, LUT banks, the partial-window payload and deep-sleep key; bulk
 * frame/plane streams keep the raw data path below (the demo splits those the
 * same way). Codex-review finding: the previous cmd-then-raw-data split was
 * not demo-equivalent for short params. */
static void epdCmdData(uint8_t c, const uint8_t *data, uint32_t len)
{
    if (HAL_LCDC_WriteU8Reg(&s_lcdc, c, (uint8_t *)data, len) != HAL_OK) epdNoteBusError("cmd+data");
}

static void epdWriteBuf(const uint8_t *data, uint32_t len)
{
    while (len > 0)
    {
        uint32_t value = 0;
        const uint32_t count = len > 4 ? 4 : len;
        for (uint32_t i = 0; i < count; i++) value = (value << 8) | data[i];

        if (!epdLcdcWaitIdle())
        {
            epdNoteBusError("data timeout");
            return;
        }

        uint32_t config = s_lcdc.Instance->SPI_IF_CONF;
        config &= ~(LCD_IF_SPI_IF_CONF_RD_LEN_Msk | LCD_IF_SPI_IF_CONF_SPI_RD_MODE_Msk |
                    LCD_IF_SPI_IF_CONF_WR_LEN_Msk);
        config |= ((count - 1) << LCD_IF_SPI_IF_CONF_WR_LEN_Pos);
        config |= LCD_IF_SPI_IF_CONF_SPI_CS_AUTO_DIS;
        s_lcdc.Instance->SPI_IF_CONF = config;
        s_lcdc.Instance->LCD_WR = value;
        s_lcdc.Instance->LCD_SINGLE = EPD_LCDC_SINGLE_WR_DATA;

        data += count;
        len -= count;
    }
    if (!epdLcdcWaitIdle()) epdNoteBusError("data drain timeout");
}

#else /* WODLE_EPD_BITBANG */

/* GPIO bit-bang fallback (the original HIL-proven hello_wodle transport).
 * Direct DOSR/DOCR stores: all EPD pins live in GPIO1 bank 0 (pads 0..6). */
#define EPD_MASK(pin) (1u << (pin))
static inline void pinHigh(int pin) { hwp_gpio1->DOSR = EPD_MASK(pin); }
static inline void pinLow(int pin) { hwp_gpio1->DOCR = EPD_MASK(pin); }

static bool epdBusInit()
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
    return true;
}

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

static void epdWriteBuf(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) epdData(data[i]);
}

/* Bit-bang has no failure source; per-byte CS framing is the HIL-proven
 * semantics, so cmd+params just chain the proven primitives. */
static bool s_busError = false;
static void epdCmdData(uint8_t c, const uint8_t *data, uint32_t len)
{
    epdCmd(c);
    epdWriteBuf(data, len);
}

#endif /* WODLE_EPD_BITBANG */

/* ------------------------------------------------------- shared panel ops */

static void epdWaitBusy(int maxMs)
{
    while (rt_pin_read(PIN_EPD_BUSY) == PIN_LOW && maxMs-- > 0)
        rt_thread_mdelay(1);
}

static void epdReset()
{
    rt_pin_write(PIN_EPD_RST, PIN_LOW);
    rt_thread_mdelay(20);
    rt_pin_write(PIN_EPD_RST, PIN_HIGH);
    rt_thread_mdelay(100);
    epdWaitBusy(1000);
}

static void epdWriteRepeated(uint8_t cmd, uint8_t value, uint32_t len)
{
    uint8_t chunk[256];
    memset(chunk, value, sizeof(chunk));
    epdCmd(cmd);
    while (len > 0)
    {
        const uint32_t count = len > sizeof(chunk) ? sizeof(chunk) : len;
        epdWriteBuf(chunk, count);
        len -= count;
    }
}

static void epdPanelInit()
{
    /* Same values as before, but each command's parameters ride in ONE
     * transaction (demo epd_write_command_data — codex-review finding). */
    static const uint8_t panelSetting[] = {0x3F, 0x4A};
    static const uint8_t pfs[] = {0x10};
    static const uint8_t powerSetting[] = {0x03, 0x00, 0x78, 0x78, 0x17};
    static const uint8_t boosterSoftStart[] = {0x25, 0x25, 0x3C};
    static const uint8_t vcomDc[] = {0x24};
    static const uint8_t pll[] = {0x0F};
    static const uint8_t resolution[] = {0x03, 0x18, 0x02, 0x58}; /* 792 x 600 (NOT 528 — see header) */
    static const uint8_t flashMode[] = {0x00, 0x00, 0x00, 0x00};
    static const uint8_t powerSaving[] = {0x02};

    epdCmdData(0x00, panelSetting, sizeof(panelSetting));
    epdCmdData(0x03, pfs, sizeof(pfs));
    epdCmdData(0x01, powerSetting, sizeof(powerSetting));
    epdCmdData(0x06, boosterSoftStart, sizeof(boosterSoftStart));
    epdCmdData(0x82, vcomDc, sizeof(vcomDc));
    epdCmdData(0x30, pll, sizeof(pll));
    epdCmdData(0x61, resolution, sizeof(resolution));
    epdWaitBusy(1000);
    epdCmdData(0x65, flashMode, sizeof(flashMode));
    epdCmdData(0xE1, powerSaving, sizeof(powerSaving));
    epdWriteRepeated(0x10, 0xFF, HalDisplay::BUFFER_SIZE); /* old RAM = white */
    epdCmd(0x04); /* power on */
    epdWaitBusy(2000);
    rt_thread_mdelay(50);
}

enum class Lut : uint8_t { None, GC, DU, GRAY4 };
static Lut s_lutLoaded = Lut::None;
static int s_fastSinceGc = 0;
#define FAST_REFRESHES_PER_GC 10 /* vendor guidance */
/* Bound LOCAL ghosting from clock-style partial updates: promote every Nth
 * partial window to the GC waveform (a brief localized flash). */
#define PARTIALS_PER_GC 20
static int s_partialsSinceGc = 0;

static void epdLoadLut(Lut which)
{
    if (s_lutLoaded == which) return;
    static const uint8_t bankCmd[5] = {0x20, 0x21, 0x22, 0x23, 0x24};

    const uint8_t *lut = LUT_GC;
    if (which == Lut::DU)
        lut = LUT_DU;
    else if (which == Lut::GRAY4)
        lut = LUT_GRAY4;

    /* CDI (0x50) for GC/DU only — spi_epd_demo deliberately does NOT touch
     * 0x50 when loading the gray LUT (codex: match it; "harmless" unproven). */
    if (which == Lut::GC)
    {
        const uint8_t cdi = 0x97;
        epdCmdData(0x50, &cdi, 1);
    }
    else if (which == Lut::DU)
    {
        const uint8_t cdi = 0xD7;
        epdCmdData(0x50, &cdi, 1);
    }

    for (int bank = 0; bank < 5; bank++)
    {
        epdCmdData(bankCmd[bank], &lut[bank * 49], 49);
    }
    /* a failed load must not be remembered as loaded */
    if (!s_busError) s_lutLoaded = which;
}

static void epdLoadLutGc() { epdLoadLut(Lut::GC); }

/* ----------------------------------------------------------------- HalDisplay */
HalDisplay::HalDisplay() = default;
HalDisplay::~HalDisplay() = default;

void HalDisplay::begin(bool seamless)
{
    (void)seamless;
    if (s_panelInitialized) return;
    s_busError = false;
    if (!epdBusInit())
    {
        rt_kprintf("[HalDisplay] bus init FAILED — display dead, continuing blind\n");
    }
    epdReset();
    epdPanelInit();
    epdLoadLutGc();
    if (s_busError) rt_kprintf("[HalDisplay] panel init saw bus errors — expect a dead display\n");
    memset(s_frameBuffer, 0xFF, sizeof(s_frameBuffer));
    s_panelInitialized = true;
    /* WODLE-PORT: force the first on-screen refresh to a full GC (like the
     * demo's boot-gc). Otherwise the boot splash paints DU over whatever was on
     * the panel (recovery/demo page) and its ghost is never cleared. */
    s_fastSinceGc = FAST_REFRESHES_PER_GC;
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

/* BW shadow of the last fully-refreshed frame. Two consumers: the absolute
 * 4-gray composition (the AA flow overwrites the live framebuffer with flag
 * planes before displayGrayBuffer runs — see EpubReaderActivity's fallback
 * path) — and it survives in PSRAM when available. */
static uint8_t *s_bwShadow = nullptr;
static uint8_t *s_bwShadowPsram = nullptr;

static void captureBwShadowFromFb()
{
    if (!s_bwShadow)
    {
        if (!s_bwShadowPsram)
            s_bwShadowPsram = static_cast<uint8_t *>(WodlePsram::alloc(HalDisplay::BUFFER_SIZE));
        s_bwShadow = s_bwShadowPsram ? s_bwShadowPsram
                                     : static_cast<uint8_t *>(malloc(HalDisplay::BUFFER_SIZE));
        if (!s_bwShadow) return; /* AA pass will degrade gracefully */
    }
    memcpy(s_bwShadow, s_frameBuffer, HalDisplay::BUFFER_SIZE);
}

/* WODLE-PORT: public hook so the reader's AA path can stage the BW shadow that
 * displayGrayBuffer() composes its absolute planes from, without first doing a
 * visible BW refresh (which is the only other place the shadow is captured). */
void HalDisplay::captureBwShadow() { captureBwShadowFromFb(); }

void HalDisplay::refreshDisplay(RefreshMode mode, bool)
{
    s_busError = false;
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

    /* Timing instrumentation: write vs refresh cost decides whether the LCDC
     * transport pays off vs bit-bang — every HIL page turn logs it. */
    const unsigned long t0 = rt_tick_get_millisecond();
    epdCmd(0x13);
    epdWriteBuf(s_frameBuffer, BUFFER_SIZE);
    const unsigned long t1 = rt_tick_get_millisecond();
    if (s_busError)
    {
        /* Controller RAM is in an unknown state — do NOT refresh against it
           and do NOT mark the DU base/shadow current (codex finding #2). */
        rt_kprintf("[HalDisplay] refresh ABORTED on bus error (frame write)\n");
        s_lutLoaded = Lut::None;
        s_fastSinceGc = FAST_REFRESHES_PER_GC; /* force GC once the bus recovers */
        return;
    }
    epdCmd(0x12);
    epdWaitBusy(8000);
    if (!wantFast)
        rt_thread_mdelay(EPD_GC_SETTLE_MS); /* GC settle margin (BUSY already waited) */
    const unsigned long t2 = rt_tick_get_millisecond();
    epdCmd(0x10); /* sync old RAM = differential base for the next DU */
    epdWriteBuf(s_frameBuffer, BUFFER_SIZE);
    rt_kprintf("[HalDisplay] %s write=%lums refresh=%lums sync=%lums\n", wantFast ? "DU" : "GC",
               t1 - t0, t2 - t1, rt_tick_get_millisecond() - t2);

    /* Both RAMs hold the BW frame again. */
    s_ramsHoldGrayPlanes = false;
    s_partialsSinceGc = 0;
    captureBwShadowFromFb();
}

/* DU partial-window refresh — spi_epd_demo recipe. x = source axis (0..791,
 * byte-aligned internally), y = gate row (0..527). The 0x90 payload's
 * controller-x IS the source axis (HRES=792), controller-y the gate axis. */
bool HalDisplay::refreshWindow(int x, int y, int w, int h)
{
    if (!s_panelInitialized || w <= 0 || h <= 0) return false;
    if (s_ramsHoldGrayPlanes) return false; /* gray planes are not a DU base */

    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w - 1;
    int y1 = y + h - 1;
    if (x1 >= DISPLAY_WIDTH) x1 = DISPLAY_WIDTH - 1;
    if (y1 >= DISPLAY_HEIGHT) y1 = DISPLAY_HEIGHT - 1;
    if (x0 > x1 || y0 > y1) return false;

    /* Byte-align the source axis (8 pixels per byte). */
    x0 = (x0 / 8) * 8;
    x1 = ((x1 + 8) / 8) * 8 - 1;
    if (x1 >= DISPLAY_WIDTH) x1 = DISPLAY_WIDTH - 1;

    s_busError = false;
    /* Promote every Nth partial to GC so clock-style updates can't accumulate
     * unbounded local ghosting between page turns. */
    const bool promoteGc = (++s_partialsSinceGc >= PARTIALS_PER_GC);
    if (promoteGc) s_partialsSinceGc = 0;
    epdLoadLut(promoteGc ? Lut::GC : Lut::DU);

    /* Enter partial mode + window (0x91, then the 9-byte 0x90 payload). */
    uint8_t payload[9];
    payload[0] = (uint8_t)(x0 >> 8);
    payload[1] = (uint8_t)x0;
    payload[2] = (uint8_t)(x1 >> 8);
    payload[3] = (uint8_t)x1;
    payload[4] = (uint8_t)(y0 >> 8);
    payload[5] = (uint8_t)y0;
    payload[6] = (uint8_t)(y1 >> 8);
    payload[7] = (uint8_t)y1;
    payload[8] = 0x01; /* partial scan */
    epdCmd(0x91);
    epdCmdData(0x90, payload, sizeof(payload));

    const int firstByte = x0 / 8;
    const int spanBytes = (x1 - x0 + 1) / 8;

    const unsigned long t0 = rt_tick_get_millisecond();
    epdCmd(0x13);
    for (int row = y0; row <= y1; row++)
        epdWriteBuf(&s_frameBuffer[(uint32_t)row * DISPLAY_WIDTH_BYTES + firstByte], spanBytes);
    epdCmd(0x12);
    epdWaitBusy(8000);
    const unsigned long t1 = rt_tick_get_millisecond();
    epdCmd(0x10); /* sync the window in old RAM */
    for (int row = y0; row <= y1; row++)
        epdWriteBuf(&s_frameBuffer[(uint32_t)row * DISPLAY_WIDTH_BYTES + firstByte], spanBytes);
    epdCmd(0x92); /* exit partial mode */

    if (s_busError)
    {
        rt_kprintf("[HalDisplay] partial ABORTED on bus error\n");
        s_lutLoaded = Lut::None;
        s_fastSinceGc = FAST_REFRESHES_PER_GC;
        return false;
    }

    /* Keep the BW shadow current for the next AA pass. */
    if (s_bwShadow)
        for (int row = y0; row <= y1; row++)
            memcpy(&s_bwShadow[(uint32_t)row * DISPLAY_WIDTH_BYTES + firstByte],
                   &s_frameBuffer[(uint32_t)row * DISPLAY_WIDTH_BYTES + firstByte], spanBytes);

    rt_kprintf("[HalDisplay] partial%s rect=%d,%d %dx%d refresh=%lums total=%lums\n",
               promoteGc ? "(GC)" : "", x0, y0, x1 - x0 + 1, y1 - y0 + 1, t1 - t0,
               rt_tick_get_millisecond() - t0);
    return true;
}

void HalDisplay::deepSleep()
{
    epdCmd(0x02); /* power off */
    epdWaitBusy(5000);
    static const uint8_t sleepKey = 0xA5;
    epdCmdData(0x07, &sleepKey, 1); /* deep sleep */
    s_panelInitialized = false;
    s_lutLoaded = Lut::None;
}

/* ------------------------------------------------------------- 4-gray pass
 * ABSOLUTE full-frame 4-gray, the panel's designed gray mode (spi_epd_demo;
 * replaces the earlier blind differential-overlay LUT rearrangement).
 *
 * GfxRenderer's AA flow stages two flag planes here (MSB = any-gray, LSB =
 * dark-gray-only, bit 1 = flagged) while the BW page is already displayed.
 * displayGrayBuffer() composes the absolute planes from the BW shadow +
 * flags (algebra codex-checked):
 *     plane10 = (bw & ~msb) | (msb & ~lsb)   -- gray bit1 (0x10 RAM)
 *     plane13 = (bw & ~msb) | (msb &  lsb)   -- gray bit0 (0x13 RAM)
 * giving 00=black 01=dark 10=light 11=white per pixel, then runs one gray4
 * full refresh: planes -> LUT (no 0x50 touch, like the demo) -> 0x12.
 *
 * Afterwards the controller RAMs hold gray planes: not a DU diff base, and
 * partial refresh is refused until the next full refresh (the reader's AA
 * path never calls cleanupGrayscaleBuffers, so the next refresh is forced
 * onto the GC branch). Plane backing comes from PSRAM when the boot probe
 * passed; SRAM heap fallback only while a pass is in flight. */
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
    if (!s_grayMsb || !s_grayLsb || !s_bwShadow)
    {
        /* Planes never staged (alloc failure) or no BW reference — the page
         * is already correct in BW; skip the AA pass. */
        freeGrayPlanes();
        return;
    }

    /* Compose absolute planes in place over the staged flag planes. */
    for (uint32_t i = 0; i < BUFFER_SIZE; i++)
    {
        const uint8_t bw = s_bwShadow[i];
        const uint8_t msb = s_grayMsb[i];
        const uint8_t lsb = s_grayLsb[i];
        s_grayMsb[i] = (uint8_t)((bw & ~msb) | (msb & ~lsb)); /* plane10 */
        s_grayLsb[i] = (uint8_t)((bw & ~msb) | (msb & lsb));  /* plane13 */
    }

    /* Demo sequence: planes first, then the gray LUT, then refresh. */
    s_busError = false;
    const unsigned long t0 = rt_tick_get_millisecond();
    epdCmd(0x10);
    epdWriteBuf(s_grayMsb, BUFFER_SIZE);
    epdCmd(0x13);
    epdWriteBuf(s_grayLsb, BUFFER_SIZE);
    epdLoadLut(Lut::GRAY4);
    if (s_busError)
    {
        /* RAM state unknown — the post-pass bookkeeping below (forced GC +
           partial refusal) is already the conservative recovery; skip only
           the refresh trigger. */
        rt_kprintf("[HalDisplay] GRAY4 ABORTED on bus error\n");
        s_lutLoaded = Lut::None;
    }
    else
    {
        epdCmd(0x12);
        epdWaitBusy(8000);
        rt_kprintf("[HalDisplay] GRAY4 total=%lums\n", rt_tick_get_millisecond() - t0);
    }

    freeGrayPlanes();

    /* Controller RAMs now hold gray planes — force the next refresh onto the
     * GC branch (settles every cell regardless) and refuse partial windows
     * until then. */
    s_fastSinceGc = FAST_REFRESHES_PER_GC;
    s_ramsHoldGrayPlanes = true;
}

/* Re-sync both controller RAMs from the restored BW frame so the next DU/GC
 * refresh diffs against real content (the gray pass left planes there). */
void HalDisplay::cleanupGrayscaleBuffers(const uint8_t *bwBuffer)
{
    freeGrayPlanes();
    if (!bwBuffer) return;
    s_busError = false;
    epdCmd(0x13);
    epdWriteBuf(bwBuffer, BUFFER_SIZE);
    epdCmd(0x10);
    epdWriteBuf(bwBuffer, BUFFER_SIZE);
    if (!s_busError) s_ramsHoldGrayPlanes = false;
}

void HalDisplay::writeGrayscalePlaneStrip(bool, const uint8_t *, uint16_t, uint16_t) {}
bool HalDisplay::supportsStripGrayscale() const { return false; }
