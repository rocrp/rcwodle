/* UC8179C e-paper driver — bit-banged GPIO SPI (clock-robust; GPIO+mdelay are
 * proven on this board). Command set + GC LUT ported from the vendor
 * reference refs/epd/UC8179C_3.68in_528x792_reference.c.
 *
 * Wiring (PAxx = pad index, from refs/schematic/README.md):
 *   RST=PA0  BUSY=PA2(TE, active-low)  CS=PA3  CLK=PA4  SDA=PA5  DC=PA6
 *
 * CRITICAL: the linked sf32lb52-lcd_base bsp_pinmux muxes PA02-PA08 to
 * LCDC1_* — rt_pin_mode/rt_pin_write only talk to the GPIO block and do NOT
 * change the pad mux, so HAL_PIN_Set(GPIO_*) here is what physically connects
 * these writes to the pads. */

#include "rtthread.h"
#include "rtdevice.h"
#include "bf0_hal.h"
#include "epd.h"

#define PIN_EPD_RST   0
#define PIN_EPD_BUSY  2
#define PIN_EPD_CS    3
#define PIN_EPD_CLK   4
#define PIN_EPD_SDA   5
#define PIN_EPD_DC    6

/* GC (full-refresh) waveform LUT — 5 banks x 49 bytes, from vendor reference. */
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

static void epd_spi_byte(uint8_t b)
{
    for (int i = 0; i < 8; i++)
    {
        rt_pin_write(PIN_EPD_CLK, PIN_LOW);
        rt_pin_write(PIN_EPD_SDA, (b & 0x80) ? PIN_HIGH : PIN_LOW);
        b <<= 1;
        rt_pin_write(PIN_EPD_CLK, PIN_HIGH); /* sample on rising edge */
    }
    rt_pin_write(PIN_EPD_CLK, PIN_LOW);
}

static void epd_cmd(uint8_t c)
{
    rt_pin_write(PIN_EPD_DC, PIN_LOW);
    rt_pin_write(PIN_EPD_CS, PIN_LOW);
    epd_spi_byte(c);
    rt_pin_write(PIN_EPD_CS, PIN_HIGH);
}

static void epd_data(uint8_t d)
{
    rt_pin_write(PIN_EPD_DC, PIN_HIGH);
    rt_pin_write(PIN_EPD_CS, PIN_LOW);
    epd_spi_byte(d);
    rt_pin_write(PIN_EPD_CS, PIN_HIGH);
}

/* BUSY is active-low (low = busy). Guarded by a timeout in case PA2 isn't BUSY. */
static void epd_wait_busy(int max_ms)
{
    while (rt_pin_read(PIN_EPD_BUSY) == PIN_LOW && max_ms-- > 0)
        rt_thread_mdelay(1);
}

static void epd_gpio_init(void)
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

static void epd_reset(void)
{
    rt_pin_write(PIN_EPD_RST, PIN_LOW);
    rt_thread_mdelay(20);
    rt_pin_write(PIN_EPD_RST, PIN_HIGH);
    rt_thread_mdelay(100);
    epd_wait_busy(1000);
}

static void epd_panel_init(void)
{
    epd_cmd(0x00); epd_data(0x3F); epd_data(0x4A);          /* panel setting (LUT from reg) */
    epd_cmd(0x03); epd_data(0x10);
    epd_cmd(0x01); epd_data(0x03); epd_data(0x00);          /* power setting */
    epd_data(0x78); epd_data(0x78); epd_data(0x17);
    epd_cmd(0x06); epd_data(0x25); epd_data(0x25); epd_data(0x3C); /* booster */
    epd_cmd(0x82); epd_data(0x24);                          /* VCOM_DC */
    epd_cmd(0x30); epd_data(0x0F);                          /* PLL 80Hz */
    epd_cmd(0x61); epd_data(0x03); epd_data(0x18);          /* TRES: HRES=792 */
    epd_data(0x02); epd_data(0x58);                         /* VRES=600 (NOT 528!
                       vendor ref: "3F要写600=58" — panel has 528 physical gates
                       bonded from both ends of a 600-line scan; declaring 528
                       leaves a dead band mid-screen) */
    epd_wait_busy(1000);
    epd_cmd(0x65); epd_data(0x00); epd_data(0x00); epd_data(0x00); epd_data(0x00);
    epd_cmd(0xE1); epd_data(0x02);                          /* gate scan mode */
    epd_cmd(0x10);                                          /* old RAM = white */
    for (int i = 0; i < EPD_FRAME_BYTES; i++) epd_data(0xFF);
    epd_cmd(0x04);                                          /* power on */
    epd_wait_busy(2000);
    rt_thread_mdelay(50);
}

static void epd_load_lut_gc(void)
{
    epd_cmd(0x50); epd_data(0x97);
    static const uint8_t bank_cmd[5] = {0x20, 0x21, 0x22, 0x23, 0x24};
    for (int bank = 0; bank < 5; bank++)
    {
        epd_cmd(bank_cmd[bank]);
        for (int i = 0; i < 49; i++) epd_data(LUT_GC[bank * 49 + i]);
    }
}

void epd_hw_init(void)
{
    epd_gpio_init();
    epd_reset();
    epd_panel_init();
    epd_load_lut_gc();
}

void epd_render(const uint8_t *fb)
{
    epd_cmd(0x13);                                          /* new frame */
    for (int i = 0; i < EPD_FRAME_BYTES; i++) epd_data(fb[i]);
    epd_cmd(0x12);                                          /* GC refresh */
    epd_wait_busy(8000);
    rt_thread_mdelay(3000); /* covers a wrong BUSY pin */
    epd_cmd(0x10);                                          /* sync old RAM */
    for (int i = 0; i < EPD_FRAME_BYTES; i++) epd_data(fb[i]);
}
