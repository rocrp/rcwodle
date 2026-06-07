#include "wodle_epd.h"

#include "bf0_hal.h"
#include "bf0_hal_lcdc.h"
#include "rtdevice.h"
#include <string.h>

#define PIN_EPD_RST 0
#define PIN_EPD_BUSY 2

#define EPD_WAIT_RESET_MS 1000
#define EPD_WAIT_INIT_MS 1000
#define EPD_WAIT_POWER_ON_MS 2000
#define EPD_WAIT_REFRESH_MS 8000
#define EPD_WAIT_POWER_OFF_MS 2000
#define EPD_DEFAULT_GC_SETTLE_MS 3000
#define EPD_SETTLE_AFTER_FAST_MS 0
#define EPD_LCDC_SPI_FREQ 12000000
#define EPD_LCDC_WAIT_BUSY_MS 1000
#define EPD_REPEAT_STACK_CHUNK 256
#define EPD_LCDC_SINGLE_WR_DATA ((1UL << LCD_IF_LCD_SINGLE_TYPE_Pos) | LCD_IF_LCD_SINGLE_WR_TRIG)

typedef enum
{
    EPD_LUT_UNKNOWN = 0,
    EPD_LUT_GC,
    EPD_LUT_DU,
    EPD_LUT_GRAY4,
    EPD_LUT_LAB_ZERO,
} epd_lut_t;

static wodle_epd_stats_t s_stats;
static rt_bool_t s_initialized;
static rt_err_t s_bus_error;
static LCDC_HandleTypeDef s_lcdc;
static rt_bool_t s_lcdc_initialized;
static epd_lut_t s_lut = EPD_LUT_UNKNOWN;
static rt_uint32_t s_gc_settle_ms = EPD_DEFAULT_GC_SETTLE_MS;

static rt_err_t epd_bus_write_reg(uint8_t command, const uint8_t *data, rt_size_t len);
static rt_err_t epd_bus_write_data_buf(const uint8_t *data, rt_size_t len);

/* GC full-refresh waveform: first 49 bytes of each bank from the vendor UC8179C reference. */
static const uint8_t s_lut_gc[245] = {
    0x01,0x18,0x04,0x0E,0x0A,0x01,0x01, 0x01,0x0A,0x00,0x00,0x00,0x01,0x01,
    0x01,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x01,0x58,0x04,0x8E,0x8A,0x01,0x01, 0x01,0x0A,0x00,0x00,0x00,0x01,0x01,
    0x01,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x01,0x18,0x04,0x8E,0x8A,0x01,0x01, 0x01,0x0A,0x00,0x00,0x00,0x01,0x01,
    0x01,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x01,0x18,0x04,0x4E,0x0A,0x01,0x01, 0x01,0x4A,0x00,0x00,0x00,0x01,0x01,
    0x01,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x01,0x98,0x04,0x4E,0x0A,0x01,0x01, 0x01,0x4A,0x00,0x00,0x00,0x01,0x01,
    0x01,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};

/* DU fast-refresh waveform: first 49 bytes of each bank from the vendor UC8179C reference. */
static const uint8_t s_lut_du[245] = {
    0x01,0x06,0x01,0x06,0x06,0x01,0x01, 0x01,0x04,0x01,0x01,0x00,0x01,0x01,
    0x01,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x01,0x06,0x81,0x06,0x06,0x01,0x01, 0x01,0x04,0x01,0x01,0x00,0x01,0x01,
    0x01,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x01,0x86,0x81,0x86,0x86,0x01,0x01, 0x01,0x84,0x81,0x01,0x00,0x01,0x01,
    0x01,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x01,0x46,0x41,0x46,0x46,0x01,0x01, 0x01,0x44,0x41,0x01,0x00,0x01,0x01,
    0x01,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x01,0x06,0x01,0x06,0x06,0x01,0x01, 0x01,0x04,0x01,0x41,0x00,0x01,0x01,
    0x01,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};

/* 4-gray waveform: five 49-byte LUT banks from the verified UC8279 gray reference. */
static const uint8_t s_lut_gray4[245] = {
    0x01,0x08,0x02,0x08,0x03,0x01,0x01, 0x01,0x09,0x03,0x04,0x03,0x01,0x01,
    0x01,0x0A,0x02,0x01,0x01,0x01,0x01, 0x01,0x02,0x02,0x00,0x00,0x01,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x01,0x08,0x02,0x08,0x03,0x01,0x01, 0x01,0x49,0x43,0x44,0x03,0x01,0x01,
    0x01,0x8A,0x82,0x81,0x81,0x01,0x01, 0x01,0x82,0x02,0x00,0x00,0x01,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x01,0x88,0x82,0x08,0x03,0x01,0x01, 0x01,0x49,0x43,0x04,0x03,0x01,0x01,
    0x01,0x0A,0x82,0x01,0x01,0x01,0x01, 0x01,0x02,0x02,0x00,0x00,0x01,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x01,0x88,0x02,0x08,0x03,0x01,0x01, 0x01,0x49,0x43,0x04,0x03,0x01,0x01,
    0x01,0x0A,0x82,0x81,0x81,0x01,0x01, 0x01,0x02,0x02,0x00,0x00,0x01,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x01,0x88,0x82,0x88,0x03,0x01,0x01, 0x01,0x49,0x43,0x44,0x03,0x01,0x01,
    0x01,0x0A,0x02,0x01,0x01,0x01,0x01, 0x01,0x02,0x42,0x00,0x00,0x01,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};

static void epd_lcdc_pinmux(void)
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

static rt_err_t epd_lcdc_bus_init(void)
{
    LCDC_InitTypeDef cfg;
    HAL_StatusTypeDef hal_ret;

    epd_lcdc_pinmux();

    if (s_lcdc_initialized)
    {
        return RT_EOK;
    }

    rt_memset(&s_lcdc, 0, sizeof(s_lcdc));
    rt_memset(&cfg, 0, sizeof(cfg));
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
    rt_memcpy(&s_lcdc.Init, &cfg, sizeof(cfg));
    hal_ret = HAL_LCDC_Init(&s_lcdc);
    if (hal_ret != HAL_OK)
    {
        return -RT_ERROR;
    }

    s_lcdc_initialized = RT_TRUE;
    return RT_EOK;
}

static rt_err_t epd_lcdc_write_reg(uint8_t command, const uint8_t *data, rt_size_t len)
{
    HAL_StatusTypeDef hal_ret;

    if (!s_lcdc_initialized)
    {
        rt_err_t err = epd_lcdc_bus_init();
        if (err != RT_EOK)
        {
            return err;
        }
    }

    hal_ret = HAL_LCDC_WriteU8Reg(&s_lcdc, command, (uint8_t *)data, (uint32_t)len);
    return (hal_ret == HAL_OK) ? RT_EOK : -RT_ERROR;
}

static rt_err_t epd_lcdc_wait_idle(void)
{
    rt_uint32_t start = rt_tick_get();
    rt_uint32_t timeout_ticks = EPD_LCDC_WAIT_BUSY_MS * RT_TICK_PER_SECOND / 1000;

    while ((s_lcdc.Instance->STATUS & LCD_IF_STATUS_LCD_BUSY) ||
           (s_lcdc.Instance->LCD_SINGLE & LCD_IF_LCD_SINGLE_LCD_BUSY))
    {
        if (rt_tick_get() - start > timeout_ticks)
        {
            return -RT_ETIMEOUT;
        }
        rt_thread_yield();
    }

    return RT_EOK;
}

static rt_err_t epd_lcdc_write_data_buf(const uint8_t *data, rt_size_t len)
{
    if (!data && len > 0)
    {
        return -RT_EINVAL;
    }

    if (!s_lcdc_initialized)
    {
        rt_err_t err = epd_lcdc_bus_init();
        if (err != RT_EOK)
        {
            return err;
        }
    }

    while (len > 0)
    {
        uint32_t value = 0;
        rt_size_t count = len > 4 ? 4 : len;

        for (rt_size_t i = 0; i < count; i++)
        {
            value = (value << 8) | data[i];
        }

        rt_err_t err = epd_lcdc_wait_idle();
        if (err != RT_EOK)
        {
            return err;
        }

        uint32_t config = s_lcdc.Instance->SPI_IF_CONF;
        config &= ~(LCD_IF_SPI_IF_CONF_RD_LEN_Msk |
                    LCD_IF_SPI_IF_CONF_SPI_RD_MODE_Msk |
                    LCD_IF_SPI_IF_CONF_WR_LEN_Msk);
        config |= ((uint32_t)(count - 1) << LCD_IF_SPI_IF_CONF_WR_LEN_Pos);
        config |= LCD_IF_SPI_IF_CONF_SPI_CS_AUTO_DIS;
        s_lcdc.Instance->SPI_IF_CONF = config;
        s_lcdc.Instance->LCD_WR = value;
        s_lcdc.Instance->LCD_SINGLE = EPD_LCDC_SINGLE_WR_DATA;

        data += count;
        len -= count;
    }

    return epd_lcdc_wait_idle();
}

static rt_err_t epd_bus_init(void)
{
    s_bus_error = RT_EOK;
    return epd_lcdc_bus_init();
}

static rt_err_t epd_bus_write_reg(uint8_t command, const uint8_t *data, rt_size_t len)
{
    return epd_lcdc_write_reg(command, data, len);
}

static rt_err_t epd_bus_write_data_buf(const uint8_t *data, rt_size_t len)
{
    return epd_lcdc_write_data_buf(data, len);
}

static void epd_record_bus_error(rt_err_t err)
{
    if (err != RT_EOK && s_bus_error == RT_EOK)
    {
        s_bus_error = err;
    }
}

static rt_err_t epd_current_bus_error(void)
{
    return s_bus_error == RT_EOK ? RT_EOK : s_bus_error;
}

static void epd_write_command(uint8_t command)
{
    epd_record_bus_error(epd_bus_write_reg(command, RT_NULL, 0));
}

static void epd_write_command_data(uint8_t command, const uint8_t *data, rt_size_t len)
{
    epd_record_bus_error(epd_bus_write_reg(command, data, len));
}

static void epd_write_command_byte(uint8_t command, uint8_t data)
{
    epd_write_command_data(command, &data, 1);
}

static rt_uint32_t elapsed_ms(rt_uint32_t start)
{
    return (rt_tick_get() - start) * 1000 / RT_TICK_PER_SECOND;
}

static void reset_refresh_stats(rt_bool_t partial, int x, int y, int width, int height)
{
    s_bus_error = RT_EOK;
    s_stats.refresh_wait_ms = 0;
    s_stats.lut_write_ms = 0;
    s_stats.new_frame_write_ms = 0;
    s_stats.old_frame_write_ms = 0;
    s_stats.refresh_cmd_ms = 0;
    s_stats.settle_wait_ms = 0;
    s_stats.refresh_total_ms = 0;
    s_stats.refresh_bytes = 0;
    s_stats.refresh_x = x;
    s_stats.refresh_y = y;
    s_stats.refresh_w = width;
    s_stats.refresh_h = height;
    s_stats.last_refresh_partial = partial;
    s_stats.last_wait_timed_out = RT_FALSE;
}

static rt_err_t normalize_partial_rect(int *x, int *y, int *width, int *height)
{
    int x0;
    int y0;
    int x1;
    int y1;

    if (!x || !y || !width || !height || *width <= 0 || *height <= 0)
    {
        return -RT_EINVAL;
    }

    x0 = *x;
    y0 = *y;
    x1 = *x + *width - 1;
    y1 = *y + *height - 1;

    if (x0 < 0)
    {
        x0 = 0;
    }
    if (y0 < 0)
    {
        y0 = 0;
    }
    if (x1 >= WODLE_EPD_DEVICE_WIDTH)
    {
        x1 = WODLE_EPD_DEVICE_WIDTH - 1;
    }
    if (y1 >= WODLE_EPD_DEVICE_HEIGHT)
    {
        y1 = WODLE_EPD_DEVICE_HEIGHT - 1;
    }

    if (x0 > x1 || y0 > y1)
    {
        return -RT_EINVAL;
    }

    y0 = (y0 / 8) * 8;
    y1 = ((y1 + 8) / 8) * 8 - 1;
    if (y1 >= WODLE_EPD_DEVICE_HEIGHT)
    {
        y1 = WODLE_EPD_DEVICE_HEIGHT - 1;
    }

    *x = x0;
    *y = y0;
    *width = x1 - x0 + 1;
    *height = y1 - y0 + 1;
    return RT_EOK;
}

static rt_uint32_t epd_wait_busy(rt_uint32_t max_ms, rt_bool_t *timed_out)
{
    rt_uint32_t waited_ms = 0;

    while (rt_pin_read(PIN_EPD_BUSY) == PIN_LOW && waited_ms < max_ms)
    {
        rt_thread_mdelay(1);
        waited_ms++;
    }

    if (timed_out)
    {
        *timed_out = (rt_pin_read(PIN_EPD_BUSY) == PIN_LOW);
    }

    s_stats.last_wait_timed_out = timed_out ? *timed_out : RT_FALSE;
    return waited_ms;
}

static void epd_reset(void)
{
    rt_bool_t timed_out;

    rt_pin_write(PIN_EPD_RST, PIN_LOW);
    rt_thread_mdelay(20);
    rt_pin_write(PIN_EPD_RST, PIN_HIGH);
    rt_thread_mdelay(100);
    s_stats.reset_wait_ms = epd_wait_busy(EPD_WAIT_RESET_MS, &timed_out);
}

static void epd_write_repeated(uint8_t command, uint8_t data, rt_size_t len)
{
    uint8_t chunk[EPD_REPEAT_STACK_CHUNK];

    rt_memset(chunk, data, sizeof(chunk));
    epd_write_command(command);
    while (len > 0)
    {
        rt_size_t count = len > sizeof(chunk) ? sizeof(chunk) : len;

        epd_record_bus_error(epd_bus_write_data_buf(chunk, count));
        len -= count;
    }
}

static void epd_load_lut(epd_lut_t lut, const uint8_t *data, uint8_t vcom)
{
    static const uint8_t bank_cmd[5] = {0x20, 0x21, 0x22, 0x23, 0x24};
    rt_uint32_t start;

    if (s_lut == lut)
    {
        return;
    }

    start = rt_tick_get();

    epd_write_command_byte(0x50, vcom);

    for (int bank = 0; bank < 5; bank++)
    {
        epd_write_command_data(bank_cmd[bank], &data[bank * 49], 49);
    }

    if (epd_current_bus_error() == RT_EOK)
    {
        s_lut = lut;
    }

    s_stats.lut_write_ms += elapsed_ms(start);
}

static void epd_load_lut_gc(void)
{
    epd_load_lut(EPD_LUT_GC, s_lut_gc, 0x97);
}

static void epd_load_lut_du(void)
{
    epd_load_lut(EPD_LUT_DU, s_lut_du, 0xD7);
}

static void epd_load_lut_gray4(void)
{
    static const uint8_t bank_cmd[5] = {0x20, 0x21, 0x22, 0x23, 0x24};
    rt_uint32_t start;

    if (s_lut == EPD_LUT_GRAY4)
    {
        return;
    }

    start = rt_tick_get();
    for (int bank = 0; bank < 5; bank++)
    {
        epd_write_command_data(bank_cmd[bank], &s_lut_gray4[bank * 49], 49);
    }

    if (epd_current_bus_error() == RT_EOK)
    {
        s_lut = EPD_LUT_GRAY4;
    }

    s_stats.lut_write_ms += elapsed_ms(start);
}

static void epd_load_lut_zero(void)
{
    static const uint8_t zero_lut[245] = {0};
    static const uint8_t bank_cmd[5] = {0x20, 0x21, 0x22, 0x23, 0x24};
    rt_uint32_t start;

    if (s_lut == EPD_LUT_LAB_ZERO)
    {
        return;
    }

    start = rt_tick_get();
    epd_write_command_byte(0x50, 0x00);
    for (int bank = 0; bank < 5; bank++)
    {
        epd_write_command_data(bank_cmd[bank], &zero_lut[bank * 49], 49);
    }

    if (epd_current_bus_error() == RT_EOK)
    {
        s_lut = EPD_LUT_LAB_ZERO;
    }

    s_stats.lut_write_ms += elapsed_ms(start);
}

static void epd_set_partial_window_ex(int x, int y, int width, int height, uint8_t partial_scan)
{
    uint8_t payload[9];
    int x0;
    int x1;
    int y0;
    int y1;

    x0 = y;
    x1 = y + height - 1;
    y0 = x;
    y1 = x + width - 1;

    payload[0] = (uint8_t)(x0 >> 8);
    payload[1] = (uint8_t)x0;
    payload[2] = (uint8_t)(x1 >> 8);
    payload[3] = (uint8_t)x1;
    payload[4] = (uint8_t)(y0 >> 8);
    payload[5] = (uint8_t)y0;
    payload[6] = (uint8_t)(y1 >> 8);
    payload[7] = (uint8_t)y1;
    payload[8] = partial_scan ? 0x01 : 0x00;

    epd_write_command(0x91);
    epd_write_command_data(0x90, payload, sizeof(payload));
}

static void epd_set_partial_window(int x, int y, int width, int height)
{
    epd_set_partial_window_ex(x, y, width, height, 0x01);
}

static void epd_exit_partial_window(void)
{
    epd_write_command(0x92);
}

static void epd_write_partial_plane_data(uint8_t command, const uint8_t *frame, int x, int y, int width, int height)
{
    int row_byte = y / 8;
    int bytes_per_gate = height / 8;

    epd_write_command(command);
    for (int gate = x; gate < x + width; gate++)
    {
        const uint8_t *line = &frame[(rt_size_t)gate * WODLE_EPD_ROW_BYTES + row_byte];
        epd_record_bus_error(epd_bus_write_data_buf(line, (rt_size_t)bytes_per_gate));
    }
}

static void epd_write_partial_frame_data(const uint8_t *frame, int x, int y, int width, int height)
{
    epd_write_partial_plane_data(0x13, frame, x, y, width, height);
}

static void epd_sync_partial_old_frame(const uint8_t *frame, int x, int y, int width, int height)
{
    epd_write_partial_plane_data(0x10, frame, x, y, width, height);
}

static void epd_panel_init_sequence(void)
{
    rt_bool_t timed_out;
    static const uint8_t panel_setting[] = {0x3F, 0x4A};
    static const uint8_t power_setting[] = {0x03, 0x00, 0x78, 0x78, 0x17};
    static const uint8_t booster_soft_start[] = {0x25, 0x25, 0x3C};
    static const uint8_t resolution_setting[] = {0x03, 0x18, 0x02, 0x58};
    static const uint8_t flash_mode[] = {0x00, 0x00, 0x00, 0x00};

    epd_write_command_data(0x00, panel_setting, sizeof(panel_setting));
    epd_write_command_byte(0x03, 0x10);
    epd_write_command_data(0x01, power_setting, sizeof(power_setting));
    epd_write_command_data(0x06, booster_soft_start, sizeof(booster_soft_start));
    epd_write_command_byte(0x82, 0x24);
    epd_write_command_byte(0x30, 0x0F);
    epd_write_command_data(0x61, resolution_setting, sizeof(resolution_setting));
    s_stats.init_wait_ms = epd_wait_busy(EPD_WAIT_INIT_MS, &timed_out);

    epd_write_command_data(0x65, flash_mode, sizeof(flash_mode));
    epd_write_command_byte(0xE1, 0x02);

    epd_write_repeated(0x10, 0xFF, WODLE_EPD_FRAME_BYTES);

    epd_write_command(0x04);
    s_stats.power_on_wait_ms = epd_wait_busy(EPD_WAIT_POWER_ON_MS, &timed_out);
    rt_thread_mdelay(50);
}

const char *wodle_epd_bus_name(void)
{
    return "lcdc1_spi_dcx";
}

void wodle_epd_set_gc_settle_ms(rt_uint32_t settle_ms)
{
    s_gc_settle_ms = settle_ms;
}

rt_uint32_t wodle_epd_get_gc_settle_ms(void)
{
    return s_gc_settle_ms;
}

rt_err_t wodle_epd_init(void)
{
    rt_err_t err;

    rt_memset(&s_stats, 0, sizeof(s_stats));
    err = epd_bus_init();
    if (err != RT_EOK)
    {
        s_initialized = RT_FALSE;
        return err;
    }

    s_lut = EPD_LUT_UNKNOWN;
    epd_reset();
    epd_panel_init_sequence();
    if (epd_current_bus_error() != RT_EOK)
    {
        s_initialized = RT_FALSE;
        return epd_current_bus_error();
    }

    epd_load_lut_gc();
    if (epd_current_bus_error() != RT_EOK)
    {
        s_initialized = RT_FALSE;
        return epd_current_bus_error();
    }

    s_initialized = RT_TRUE;
    return RT_EOK;
}

rt_err_t wodle_epd_refresh_full(const uint8_t *frame)
{
    rt_uint32_t start;
    rt_uint32_t step;
    rt_bool_t timed_out;

    if (!frame)
    {
        return -RT_EINVAL;
    }

    if (!s_initialized)
    {
        rt_err_t err = wodle_epd_init();
        if (err != RT_EOK)
        {
            return err;
        }
    }

    reset_refresh_stats(RT_FALSE, 0, 0, WODLE_EPD_DEVICE_WIDTH, WODLE_EPD_DEVICE_HEIGHT);
    start = rt_tick_get();
    epd_load_lut_gc();
    if (epd_current_bus_error() != RT_EOK)
    {
        return epd_current_bus_error();
    }

    step = rt_tick_get();
    epd_write_command_data(0x13, frame, WODLE_EPD_FRAME_BYTES);
    s_stats.new_frame_write_ms = elapsed_ms(step);
    s_stats.refresh_bytes = WODLE_EPD_FRAME_BYTES;
    if (epd_current_bus_error() != RT_EOK)
    {
        return epd_current_bus_error();
    }

    step = rt_tick_get();
    epd_write_command(0x12);
    if (epd_current_bus_error() != RT_EOK)
    {
        return epd_current_bus_error();
    }

    s_stats.refresh_wait_ms = epd_wait_busy(EPD_WAIT_REFRESH_MS, &timed_out);
    s_stats.refresh_cmd_ms = elapsed_ms(step);

    if (s_gc_settle_ms > 0)
    {
        step = rt_tick_get();
        rt_thread_mdelay(s_gc_settle_ms);
        s_stats.settle_wait_ms = elapsed_ms(step);
    }

    step = rt_tick_get();
    epd_write_command_data(0x10, frame, WODLE_EPD_FRAME_BYTES);
    s_stats.old_frame_write_ms = elapsed_ms(step);
    if (epd_current_bus_error() != RT_EOK)
    {
        return epd_current_bus_error();
    }

    s_stats.refresh_total_ms = elapsed_ms(start);
    return timed_out ? -RT_ETIMEOUT : RT_EOK;
}

rt_err_t wodle_epd_refresh_fast(const uint8_t *frame)
{
    rt_uint32_t start;
    rt_uint32_t step;
    rt_bool_t timed_out;

    if (!frame)
    {
        return -RT_EINVAL;
    }

    if (!s_initialized)
    {
        rt_err_t err = wodle_epd_init();
        if (err != RT_EOK)
        {
            return err;
        }
    }

    reset_refresh_stats(RT_FALSE, 0, 0, WODLE_EPD_DEVICE_WIDTH, WODLE_EPD_DEVICE_HEIGHT);
    start = rt_tick_get();
    epd_load_lut_du();
    if (epd_current_bus_error() != RT_EOK)
    {
        return epd_current_bus_error();
    }

    step = rt_tick_get();
    epd_write_command_data(0x13, frame, WODLE_EPD_FRAME_BYTES);
    s_stats.new_frame_write_ms = elapsed_ms(step);
    s_stats.refresh_bytes = WODLE_EPD_FRAME_BYTES;
    if (epd_current_bus_error() != RT_EOK)
    {
        return epd_current_bus_error();
    }

    step = rt_tick_get();
    epd_write_command(0x12);
    if (epd_current_bus_error() != RT_EOK)
    {
        return epd_current_bus_error();
    }

    s_stats.refresh_wait_ms = epd_wait_busy(EPD_WAIT_REFRESH_MS, &timed_out);
    s_stats.refresh_cmd_ms = elapsed_ms(step);

    if (EPD_SETTLE_AFTER_FAST_MS > 0)
    {
        step = rt_tick_get();
        rt_thread_mdelay(EPD_SETTLE_AFTER_FAST_MS);
        s_stats.settle_wait_ms = elapsed_ms(step);
    }

    step = rt_tick_get();
    epd_write_command_data(0x10, frame, WODLE_EPD_FRAME_BYTES);
    s_stats.old_frame_write_ms = elapsed_ms(step);
    if (epd_current_bus_error() != RT_EOK)
    {
        return epd_current_bus_error();
    }

    s_stats.refresh_total_ms = elapsed_ms(start);
    return timed_out ? -RT_ETIMEOUT : RT_EOK;
}

rt_err_t wodle_epd_refresh_partial_fast(const uint8_t *frame, int x, int y, int width, int height)
{
    rt_uint32_t start;
    rt_uint32_t step;
    rt_bool_t timed_out;
    rt_err_t err;

    if (!frame)
    {
        return -RT_EINVAL;
    }

    err = normalize_partial_rect(&x, &y, &width, &height);
    if (err != RT_EOK)
    {
        return err;
    }

    if (!s_initialized)
    {
        err = wodle_epd_init();
        if (err != RT_EOK)
        {
            return err;
        }
    }

    reset_refresh_stats(RT_TRUE, x, y, width, height);
    start = rt_tick_get();
    epd_load_lut_du();
    if (epd_current_bus_error() != RT_EOK)
    {
        return epd_current_bus_error();
    }

    epd_set_partial_window(x, y, width, height);
    if (epd_current_bus_error() != RT_EOK)
    {
        return epd_current_bus_error();
    }

    step = rt_tick_get();
    epd_write_partial_frame_data(frame, x, y, width, height);
    s_stats.new_frame_write_ms = elapsed_ms(step);
    s_stats.refresh_bytes = (rt_uint32_t)(width * (height / 8));
    if (epd_current_bus_error() != RT_EOK)
    {
        epd_exit_partial_window();
        return epd_current_bus_error();
    }

    step = rt_tick_get();
    epd_write_command(0x12);
    if (epd_current_bus_error() != RT_EOK)
    {
        epd_exit_partial_window();
        return epd_current_bus_error();
    }

    s_stats.refresh_wait_ms = epd_wait_busy(EPD_WAIT_REFRESH_MS, &timed_out);
    s_stats.refresh_cmd_ms = elapsed_ms(step);

    if (EPD_SETTLE_AFTER_FAST_MS > 0)
    {
        step = rt_tick_get();
        rt_thread_mdelay(EPD_SETTLE_AFTER_FAST_MS);
        s_stats.settle_wait_ms = elapsed_ms(step);
    }

    step = rt_tick_get();
    epd_sync_partial_old_frame(frame, x, y, width, height);
    s_stats.old_frame_write_ms = elapsed_ms(step);
    epd_exit_partial_window();
    if (epd_current_bus_error() != RT_EOK)
    {
        return epd_current_bus_error();
    }

    s_stats.refresh_total_ms = elapsed_ms(start);
    return timed_out ? -RT_ETIMEOUT : RT_EOK;
}

rt_err_t wodle_epd_refresh_gray4_full(const uint8_t *plane10, const uint8_t *plane13)
{
    rt_uint32_t start;
    rt_uint32_t step;
    rt_bool_t timed_out;

    if (!plane10 || !plane13)
    {
        return -RT_EINVAL;
    }

    if (!s_initialized)
    {
        rt_err_t err = wodle_epd_init();
        if (err != RT_EOK)
        {
            return err;
        }
    }

    reset_refresh_stats(RT_FALSE, 0, 0, WODLE_EPD_DEVICE_WIDTH, WODLE_EPD_DEVICE_HEIGHT);
    start = rt_tick_get();

    step = rt_tick_get();
    epd_write_command_data(0x10, plane10, WODLE_EPD_FRAME_BYTES);
    s_stats.old_frame_write_ms = elapsed_ms(step);
    if (epd_current_bus_error() != RT_EOK)
    {
        return epd_current_bus_error();
    }

    step = rt_tick_get();
    epd_write_command_data(0x13, plane13, WODLE_EPD_FRAME_BYTES);
    s_stats.new_frame_write_ms = elapsed_ms(step);
    s_stats.refresh_bytes = WODLE_EPD_FRAME_BYTES * 2;
    if (epd_current_bus_error() != RT_EOK)
    {
        return epd_current_bus_error();
    }

    epd_load_lut_gray4();
    if (epd_current_bus_error() != RT_EOK)
    {
        return epd_current_bus_error();
    }

    step = rt_tick_get();
    epd_write_command(0x12);
    if (epd_current_bus_error() != RT_EOK)
    {
        return epd_current_bus_error();
    }

    s_stats.refresh_wait_ms = epd_wait_busy(EPD_WAIT_REFRESH_MS, &timed_out);
    s_stats.refresh_cmd_ms = elapsed_ms(step);
    s_stats.refresh_total_ms = elapsed_ms(start);
    return timed_out ? -RT_ETIMEOUT : RT_EOK;
}

rt_err_t wodle_epd_refresh_gray4_partial(const uint8_t *plane10,
                                         const uint8_t *plane13,
                                         int x,
                                         int y,
                                         int width,
                                         int height)
{
    rt_uint32_t start;
    rt_uint32_t step;
    rt_bool_t timed_out;
    rt_err_t err;

    if (!plane10 || !plane13)
    {
        return -RT_EINVAL;
    }

    err = normalize_partial_rect(&x, &y, &width, &height);
    if (err != RT_EOK)
    {
        return err;
    }

    if (!s_initialized)
    {
        err = wodle_epd_init();
        if (err != RT_EOK)
        {
            return err;
        }
    }

    reset_refresh_stats(RT_TRUE, x, y, width, height);
    start = rt_tick_get();

    epd_load_lut_gray4();
    if (epd_current_bus_error() != RT_EOK)
    {
        return epd_current_bus_error();
    }

    epd_set_partial_window(x, y, width, height);
    if (epd_current_bus_error() != RT_EOK)
    {
        return epd_current_bus_error();
    }

    step = rt_tick_get();
    epd_write_partial_plane_data(0x10, plane10, x, y, width, height);
    s_stats.old_frame_write_ms = elapsed_ms(step);
    if (epd_current_bus_error() != RT_EOK)
    {
        epd_exit_partial_window();
        return epd_current_bus_error();
    }

    step = rt_tick_get();
    epd_write_partial_plane_data(0x13, plane13, x, y, width, height);
    s_stats.new_frame_write_ms = elapsed_ms(step);
    s_stats.refresh_bytes = (rt_uint32_t)(width * (height / 8) * 2);
    if (epd_current_bus_error() != RT_EOK)
    {
        epd_exit_partial_window();
        return epd_current_bus_error();
    }

    step = rt_tick_get();
    epd_write_command(0x12);
    if (epd_current_bus_error() != RT_EOK)
    {
        epd_exit_partial_window();
        return epd_current_bus_error();
    }

    s_stats.refresh_wait_ms = epd_wait_busy(EPD_WAIT_REFRESH_MS, &timed_out);
    s_stats.refresh_cmd_ms = elapsed_ms(step);
    epd_exit_partial_window();
    if (epd_current_bus_error() != RT_EOK)
    {
        return epd_current_bus_error();
    }

    s_stats.refresh_total_ms = elapsed_ms(start);
    return timed_out ? -RT_ETIMEOUT : RT_EOK;
}

rt_err_t wodle_epd_sleep(void)
{
    rt_bool_t timed_out;

    if (!s_initialized)
    {
        return RT_EOK;
    }

    epd_write_command(0x02);
    if (epd_current_bus_error() != RT_EOK)
    {
        return epd_current_bus_error();
    }

    s_stats.power_off_wait_ms = epd_wait_busy(EPD_WAIT_POWER_OFF_MS, &timed_out);
    epd_write_command_byte(0x07, 0xA5);
    s_initialized = RT_FALSE;
    s_lut = EPD_LUT_UNKNOWN;

    if (epd_current_bus_error() != RT_EOK)
    {
        return epd_current_bus_error();
    }

    return timed_out ? -RT_ETIMEOUT : RT_EOK;
}

void wodle_epd_get_stats(wodle_epd_stats_t *stats)
{
    if (stats)
    {
        *stats = s_stats;
    }
}

rt_err_t wodle_epd_lab_set_panel_setting(uint8_t psr0, uint8_t psr1)
{
    uint8_t payload[2] = {psr0, psr1};

    if (!s_initialized)
    {
        rt_err_t err = wodle_epd_init();
        if (err != RT_EOK)
        {
            return err;
        }
    }

    s_bus_error = RT_EOK;
    epd_write_command_data(0x00, payload, sizeof(payload));
    s_lut = EPD_LUT_UNKNOWN;
    return epd_current_bus_error();
}

static rt_err_t epd_lab_load_lut(wodle_epd_lab_lut_t lut)
{
    switch (lut)
    {
    case WODLE_EPD_LAB_LUT_KEEP:
        return RT_EOK;
    case WODLE_EPD_LAB_LUT_GC:
        epd_load_lut_gc();
        break;
    case WODLE_EPD_LAB_LUT_DU:
        epd_load_lut_du();
        break;
    case WODLE_EPD_LAB_LUT_GRAY4:
        epd_load_lut_gray4();
        break;
    case WODLE_EPD_LAB_LUT_ZERO:
        epd_load_lut_zero();
        break;
    default:
        return -RT_EINVAL;
    }

    return epd_current_bus_error();
}

rt_err_t wodle_epd_lab_refresh_planes(const uint8_t *plane10,
                                      const uint8_t *plane13,
                                      int x,
                                      int y,
                                      int width,
                                      int height,
                                      rt_bool_t partial,
                                      wodle_epd_lab_lut_t lut,
                                      uint8_t psr0,
                                      uint8_t psr1)
{
    return wodle_epd_lab_refresh_planes_ex(plane10,
                                           plane13,
                                           x,
                                           y,
                                           width,
                                           height,
                                           partial,
                                           lut,
                                           psr0,
                                           psr1,
                                           RT_NULL);
}

rt_err_t wodle_epd_lab_refresh_planes_ex(const uint8_t *plane10,
                                         const uint8_t *plane13,
                                         int x,
                                         int y,
                                         int width,
                                         int height,
                                         rt_bool_t partial,
                                         wodle_epd_lab_lut_t lut,
                                         uint8_t psr0,
                                         uint8_t psr1,
                                         const wodle_epd_lab_options_t *options)
{
    rt_uint32_t start;
    rt_uint32_t step;
    rt_bool_t timed_out;
    rt_err_t err;
    uint8_t partial_scan = 0x01;

    if (!plane10 || !plane13)
    {
        return -RT_EINVAL;
    }

    if (partial)
    {
        err = normalize_partial_rect(&x, &y, &width, &height);
        if (err != RT_EOK)
        {
            return err;
        }
    }
    else
    {
        x = 0;
        y = 0;
        width = WODLE_EPD_DEVICE_WIDTH;
        height = WODLE_EPD_DEVICE_HEIGHT;
    }

    if (!s_initialized)
    {
        err = wodle_epd_init();
        if (err != RT_EOK)
        {
            return err;
        }
    }

    reset_refresh_stats(partial, x, y, width, height);
    start = rt_tick_get();

    if (psr0 != WODLE_EPD_LAB_PSR_KEEP)
    {
        uint8_t psr_payload[2] = {psr0, psr1};

        epd_write_command_data(0x00, psr_payload, sizeof(psr_payload));
        s_lut = EPD_LUT_UNKNOWN;
        if (epd_current_bus_error() != RT_EOK)
        {
            return epd_current_bus_error();
        }
    }

    if (options && options->partial_scan != WODLE_EPD_LAB_OPT_KEEP)
    {
        partial_scan = options->partial_scan ? 0x01 : 0x00;
    }

    err = epd_lab_load_lut(lut);
    if (err != RT_EOK)
    {
        return err;
    }

    if (options && options->vcom_and_data_interval != WODLE_EPD_LAB_OPT_KEEP)
    {
        epd_write_command_byte(0x50, options->vcom_and_data_interval);
        if (epd_current_bus_error() != RT_EOK)
        {
            return epd_current_bus_error();
        }
    }

    if (options && options->kw_lut_option[0] != WODLE_EPD_LAB_OPT_KEEP)
    {
        epd_write_command_data(0x2B, options->kw_lut_option, sizeof(options->kw_lut_option));
        if (epd_current_bus_error() != RT_EOK)
        {
            return epd_current_bus_error();
        }
    }

    if (partial)
    {
        epd_set_partial_window_ex(x, y, width, height, partial_scan);
        if (epd_current_bus_error() != RT_EOK)
        {
            return epd_current_bus_error();
        }

        step = rt_tick_get();
        epd_write_partial_plane_data(0x10, plane10, x, y, width, height);
        s_stats.old_frame_write_ms = elapsed_ms(step);
        if (epd_current_bus_error() != RT_EOK)
        {
            epd_exit_partial_window();
            return epd_current_bus_error();
        }

        step = rt_tick_get();
        epd_write_partial_plane_data(0x13, plane13, x, y, width, height);
        s_stats.new_frame_write_ms = elapsed_ms(step);
        s_stats.refresh_bytes = (rt_uint32_t)(width * (height / 8) * 2);
        if (epd_current_bus_error() != RT_EOK)
        {
            epd_exit_partial_window();
            return epd_current_bus_error();
        }
    }
    else
    {
        step = rt_tick_get();
        epd_write_command_data(0x10, plane10, WODLE_EPD_FRAME_BYTES);
        s_stats.old_frame_write_ms = elapsed_ms(step);
        if (epd_current_bus_error() != RT_EOK)
        {
            return epd_current_bus_error();
        }

        step = rt_tick_get();
        epd_write_command_data(0x13, plane13, WODLE_EPD_FRAME_BYTES);
        s_stats.new_frame_write_ms = elapsed_ms(step);
        s_stats.refresh_bytes = WODLE_EPD_FRAME_BYTES * 2;
        if (epd_current_bus_error() != RT_EOK)
        {
            return epd_current_bus_error();
        }
    }

    step = rt_tick_get();
    epd_write_command(0x12);
    if (epd_current_bus_error() != RT_EOK)
    {
        if (partial)
        {
            epd_exit_partial_window();
        }
        return epd_current_bus_error();
    }

    s_stats.refresh_wait_ms = epd_wait_busy(EPD_WAIT_REFRESH_MS, &timed_out);
    s_stats.refresh_cmd_ms = elapsed_ms(step);

    if (partial)
    {
        epd_exit_partial_window();
        if (epd_current_bus_error() != RT_EOK)
        {
            return epd_current_bus_error();
        }
    }

    s_stats.refresh_total_ms = elapsed_ms(start);
    return timed_out ? -RT_ETIMEOUT : RT_EOK;
}
