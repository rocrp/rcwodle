#include "rtthread.h"
#include "rtdevice.h"
#include "bf0_hal.h"

/* wodle EPD wiring (PAxx == pad index), from refs/schematic/README.md.
 * Panel = UltraChip UC8179C, 528x792, driven here by bit-banged GPIO SPI
 * (clock-robust: GPIO + mdelay are already proven working). Reference command
 * set + LUT_GC are ported from refs/epd/UC8179C_3.68in_528x792_reference.c. */
#define PIN_EPD_RST   0    /* PA0  LCDC1_SPI_RSTB */
#define PIN_BL        1    /* PA1  frontlight (PWM-boost — software-PWM here) */
#define PIN_EPD_BUSY  2    /* PA2  TE / BUSY (inferred; active-low) */
#define PIN_EPD_CS    3    /* PA3  LCDC1_SPI_CS */
#define PIN_EPD_CLK   4    /* PA4  LCDC1_SPI_CLK */
#define PIN_EPD_SDA   5    /* PA5  LCDC1_SPI_DIO0 */
#define PIN_EPD_DC    6    /* PA6  LCDC1_SPI_DC */
#define PIN_PWR_EN    10   /* PA10 system power-enable latch */

#define EPD_W  792         /* source / horizontal */
#define EPD_H  528         /* gate / vertical */
#define EPD_ROW_BYTES (EPD_W / 8)          /* 99 */
#define EPD_FRAME_BYTES (EPD_W * EPD_H / 8) /* 52272 */

/* GC (full-refresh) waveform LUT — 5 banks x 49 bytes, from the vendor reference. */
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
    const int outs[] = {PIN_EPD_RST, PIN_EPD_CS, PIN_EPD_CLK, PIN_EPD_SDA, PIN_EPD_DC, PIN_BL};
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

static void epd_init(void)
{
    epd_cmd(0x00); epd_data(0x3F); epd_data(0x4A);          /* panel setting (LUT from reg) */
    epd_cmd(0x03); epd_data(0x10);
    epd_cmd(0x01); epd_data(0x03); epd_data(0x00);          /* power setting */
    epd_data(0x78); epd_data(0x78); epd_data(0x17);
    epd_cmd(0x06); epd_data(0x25); epd_data(0x25); epd_data(0x3C); /* booster */
    epd_cmd(0x82); epd_data(0x24);                          /* VCOM_DC */
    epd_cmd(0x30); epd_data(0x0F);                          /* PLL 80Hz */
    epd_cmd(0x61); epd_data(0x03); epd_data(0x18);          /* TRES: HRES=792 */
    epd_data(0x02); epd_data(0x10);                         /*       VRES=528 */
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

/* Paint top half white (0xFF), bottom half black (0x00) — an unmistakable change. */
static void epd_draw_test_pattern(void)
{
    epd_cmd(0x13);
    for (int row = 0; row < EPD_H; row++)
    {
        uint8_t v = (row < EPD_H / 2) ? 0xFF : 0x00;
        for (int b = 0; b < EPD_ROW_BYTES; b++) epd_data(v);
    }
}

static void epd_refresh(void)
{
    epd_cmd(0x12);
    epd_wait_busy(8000);
    rt_thread_mdelay(3000); /* GC full refresh ~2-3s; covers a wrong BUSY pin */
}

int main(void)
{
    rt_kprintf("\n[hello_wodle] EPD test boot: %s %s\n", __DATE__, __TIME__);

    rt_pin_mode(PIN_PWR_EN, PIN_MODE_OUTPUT);
    rt_pin_write(PIN_PWR_EN, PIN_HIGH);

    epd_gpio_init();
    epd_reset();
    epd_init();
    epd_load_lut_gc();
    epd_draw_test_pattern();
    epd_refresh();
    rt_kprintf("[hello_wodle] EPD refresh done\n");

    /* Backlight: software PWM (the boost driver needs switching, not a DC level). */
    while (1)
    {
        rt_pin_write(PIN_BL, PIN_HIGH);
        for (volatile int d = 0; d < 80; d++) { __NOP(); }
        rt_pin_write(PIN_BL, PIN_LOW);
        for (volatile int d = 0; d < 80; d++) { __NOP(); }
    }
    return 0;
}
