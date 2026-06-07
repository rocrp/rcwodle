#include "rtthread.h"
#include "rtdevice.h"
#include "bf0_hal.h"
#include "usb_log.h"
#include "wodle_display.h"
#include "wodle_epd.h"
#include "wodle_frontlight.h"
#include "wodle_gray4_display.h"

#define LOG_PREFIX "spi_epd_demo"
#define PIN_PWR_EN 10
#define PIN_KEY_UP 44
#define PIN_KEY_DOWN 43
#define KEY_POLL_MS 20
#define HEARTBEAT_MS 3000
#define FRONTLIGHT_STEP 10

typedef struct
{
    const char *name;
    rt_base_t pin;
    int last_raw;
} key_state_t;

static uint8_t s_bw_frame[WODLE_EPD_FRAME_BYTES];
static uint8_t s_gray_plane10[WODLE_EPD_FRAME_BYTES];
static uint8_t s_gray_plane13[WODLE_EPD_FRAME_BYTES];
static wodle_display_t s_bw_display;
static wodle_gray4_display_t s_gray_display;
static rt_uint32_t s_refresh_count;
static rt_err_t s_last_refresh = -RT_ERROR;
static rt_bool_t s_partial_page_ready;
static key_state_t s_key_up = {"up_pa44", PIN_KEY_UP, -1};
static key_state_t s_key_down = {"down_pa43", PIN_KEY_DOWN, -1};

static void hold_power(void)
{
    HAL_PIN_Set(PAD_PA10, GPIO_A10, PIN_NOPULL, 1);
    rt_pin_mode(PIN_PWR_EN, PIN_MODE_OUTPUT);
    rt_pin_write(PIN_PWR_EN, PIN_HIGH);
}

static void setup_key_pins(void)
{
    HAL_PIN_Set(PAD_PA43, GPIO_A43, PIN_PULLDOWN, 1);
    HAL_PIN_Set(PAD_PA44, GPIO_A44, PIN_PULLDOWN, 1);
    rt_pin_mode(PIN_KEY_DOWN, PIN_MODE_INPUT_PULLDOWN);
    rt_pin_mode(PIN_KEY_UP, PIN_MODE_INPUT_PULLDOWN);
}

static int parse_int(const char *text, int *value)
{
    int sign = 1;
    int result = 0;
    int saw_digit = 0;

    while (*text == ' ')
    {
        text++;
    }

    if (*text == '-')
    {
        sign = -1;
        text++;
    }

    while (*text >= '0' && *text <= '9')
    {
        result = result * 10 + (*text - '0');
        saw_digit = 1;
        text++;
    }

    if (!saw_digit)
    {
        return 0;
    }

    *value = result * sign;
    return 1;
}

static rt_uint32_t elapsed_ms(rt_uint32_t start)
{
    return (rt_tick_get() - start) * 1000 / RT_TICK_PER_SECOND;
}

static void log_epd_stats(const char *tag)
{
    wodle_epd_stats_t stats;

    wodle_epd_get_stats(&stats);
    usb_log_printf("[%s] stats tag=%s reset=%lums init=%lums power_on=%lums busy=%lums timeout=%d total=%lums lut=%lums new=%lums cmd_busy=%lums settle=%lums old=%lums bytes=%lu partial=%d rect=%d,%d,%d,%d bus=%s\r\n",
                   LOG_PREFIX,
                   tag,
                   (unsigned long)stats.reset_wait_ms,
                   (unsigned long)stats.init_wait_ms,
                   (unsigned long)stats.power_on_wait_ms,
                   (unsigned long)stats.refresh_wait_ms,
                   stats.last_wait_timed_out,
                   (unsigned long)stats.refresh_total_ms,
                   (unsigned long)stats.lut_write_ms,
                   (unsigned long)stats.new_frame_write_ms,
                   (unsigned long)stats.refresh_cmd_ms,
                   (unsigned long)stats.settle_wait_ms,
                   (unsigned long)stats.old_frame_write_ms,
                   (unsigned long)stats.refresh_bytes,
                   stats.last_refresh_partial,
                   stats.refresh_x,
                   stats.refresh_y,
                   stats.refresh_w,
                   stats.refresh_h,
                   wodle_epd_bus_name());
}

static void log_status(void)
{
    usb_log_printf("[%s] status refresh_count=%lu last_refresh=%d light=%d duty=%d bus=%s partial_page=%d bw_bytes=%lu gray_bytes=%lu\r\n",
                   LOG_PREFIX,
                   (unsigned long)s_refresh_count,
                   s_last_refresh,
                   wodle_frontlight_get_level(),
                   wodle_frontlight_get_duty(),
                   wodle_epd_bus_name(),
                   s_partial_page_ready,
                   (unsigned long)WODLE_EPD_FRAME_BYTES,
                   (unsigned long)(WODLE_EPD_FRAME_BYTES * 2));
    log_epd_stats("status");
}

static rt_err_t refresh_gc(const char *tag)
{
    rt_uint32_t start = rt_tick_get();

    usb_log_printf("[%s] refresh begin tag=%s api=wodle_epd_refresh_full count=%lu\r\n",
                   LOG_PREFIX,
                   tag,
                   (unsigned long)(s_refresh_count + 1));
    s_last_refresh = wodle_display_refresh(&s_bw_display);
    s_refresh_count++;
    usb_log_printf("[%s] refresh end tag=%s ret=%d elapsed=%lums count=%lu\r\n",
                   LOG_PREFIX,
                   tag,
                   s_last_refresh,
                   (unsigned long)elapsed_ms(start),
                   (unsigned long)s_refresh_count);
    log_epd_stats(tag);
    return s_last_refresh;
}

static rt_err_t refresh_du(const char *tag)
{
    rt_uint32_t start = rt_tick_get();

    usb_log_printf("[%s] refresh begin tag=%s api=wodle_epd_refresh_fast count=%lu\r\n",
                   LOG_PREFIX,
                   tag,
                   (unsigned long)(s_refresh_count + 1));
    s_last_refresh = wodle_display_refresh_fast(&s_bw_display);
    s_refresh_count++;
    usb_log_printf("[%s] refresh end tag=%s ret=%d elapsed=%lums count=%lu\r\n",
                   LOG_PREFIX,
                   tag,
                   s_last_refresh,
                   (unsigned long)elapsed_ms(start),
                   (unsigned long)s_refresh_count);
    log_epd_stats(tag);
    return s_last_refresh;
}

static rt_err_t refresh_partial_du(const char *tag, int x, int y, int width, int height)
{
    rt_uint32_t start = rt_tick_get();

    usb_log_printf("[%s] refresh begin tag=%s api=wodle_epd_refresh_partial_fast rect=%d,%d,%d,%d count=%lu\r\n",
                   LOG_PREFIX,
                   tag,
                   x,
                   y,
                   width,
                   height,
                   (unsigned long)(s_refresh_count + 1));
    s_last_refresh = wodle_display_refresh_partial_fast(&s_bw_display, x, y, width, height);
    s_refresh_count++;
    usb_log_printf("[%s] refresh end tag=%s ret=%d elapsed=%lums count=%lu\r\n",
                   LOG_PREFIX,
                   tag,
                   s_last_refresh,
                   (unsigned long)elapsed_ms(start),
                   (unsigned long)s_refresh_count);
    log_epd_stats(tag);
    return s_last_refresh;
}

static rt_err_t refresh_gray4_full(const char *tag)
{
    rt_uint32_t start = rt_tick_get();

    usb_log_printf("[%s] refresh begin tag=%s api=wodle_epd_refresh_gray4_full count=%lu\r\n",
                   LOG_PREFIX,
                   tag,
                   (unsigned long)(s_refresh_count + 1));
    s_last_refresh = wodle_gray4_display_refresh(&s_gray_display);
    s_refresh_count++;
    usb_log_printf("[%s] refresh end tag=%s ret=%d elapsed=%lums count=%lu\r\n",
                   LOG_PREFIX,
                   tag,
                   s_last_refresh,
                   (unsigned long)elapsed_ms(start),
                   (unsigned long)s_refresh_count);
    log_epd_stats(tag);
    return s_last_refresh;
}

static void draw_boot_page(void)
{
    s_partial_page_ready = RT_FALSE;
    wodle_display_clear(&s_bw_display, WODLE_COLOR_WHITE);
    wodle_display_draw_rect(&s_bw_display, 0, 0, WODLE_EPD_DEVICE_WIDTH, WODLE_EPD_DEVICE_HEIGHT, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 22, "SPI EPD demo", 3, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 92, "bus: LCDC1 SPI DCX", 2, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 138, "RST PA00  BUSY PA02", 1, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 162, "CS PA03 CLK PA04", 1, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 186, "MOSI PA05 DCX PA06", 1, WODLE_COLOR_BLACK);
    wodle_display_draw_hline(&s_bw_display, 18, 226, WODLE_EPD_DEVICE_WIDTH - 36, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 250, "commands:", 2, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 298, "gc du partial gray4", 2, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 344, "white black checker text", 1, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 368, "status init sleep light N", 1, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 424, "1bpp frame: 52272 bytes", 1, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 448, "1=white, 0=black", 1, WODLE_COLOR_BLACK);
    wodle_display_fill_rect(&s_bw_display, 22, 510, 112, 56, WODLE_COLOR_BLACK);
    wodle_display_draw_rect(&s_bw_display, 154, 510, 112, 56, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 612, "USB CDC logs include BUSY", 1, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 636, "wait, bytes, LUT, rect.", 1, WODLE_COLOR_BLACK);
}

static void draw_text_page(void)
{
    s_partial_page_ready = RT_FALSE;
    wodle_display_clear(&s_bw_display, WODLE_COLOR_WHITE);
    wodle_display_draw_rect(&s_bw_display, 0, 0, WODLE_EPD_DEVICE_WIDTH, WODLE_EPD_DEVICE_HEIGHT, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 22, "GC full refresh", 2, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 72, "API: wodle_epd_refresh_full", 1, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 116, "Write 0x13 new frame", 1, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 140, "Trigger 0x12 refresh", 1, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 164, "Wait BUSY release", 1, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 188, "Sync 0x10 old frame", 1, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 256, "Use for clean pages.", 2, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 316, "0123456789 ABC xyz", 2, WODLE_COLOR_BLACK);
}

static void draw_du_page(void)
{
    s_partial_page_ready = RT_FALSE;
    wodle_display_clear(&s_bw_display, WODLE_COLOR_WHITE);
    wodle_display_draw_rect(&s_bw_display, 0, 0, WODLE_EPD_DEVICE_WIDTH, WODLE_EPD_DEVICE_HEIGHT, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 22, "DU fast refresh", 2, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 72, "API: wodle_epd_refresh_fast", 1, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 116, "Same full frame size", 1, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 140, "DU LUT for speed", 1, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 164, "Use GC sometimes", 1, WODLE_COLOR_BLACK);
    for (int i = 0; i < 8; i++)
    {
        wodle_color_t color = (i & 1) ? WODLE_COLOR_WHITE : WODLE_COLOR_BLACK;
        wodle_display_fill_rect(&s_bw_display, 34 + i * 58, 260, 44, 180, color);
        wodle_display_draw_rect(&s_bw_display, 34 + i * 58, 260, 44, 180, WODLE_COLOR_BLACK);
    }
}

static void draw_partial_base_page(void)
{
    s_partial_page_ready = RT_TRUE;
    wodle_display_clear(&s_bw_display, WODLE_COLOR_WHITE);
    wodle_display_draw_rect(&s_bw_display, 0, 0, WODLE_EPD_DEVICE_WIDTH, WODLE_EPD_DEVICE_HEIGHT, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 22, "Partial DU refresh", 2, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 72, "API: refresh_partial_fast", 1, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 18, 116, "Only update the box.", 2, WODLE_COLOR_BLACK);
    wodle_display_draw_rect(&s_bw_display, 42, 214, 444, 268, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, 70, 516, "Run partial repeatedly.", 1, WODLE_COLOR_BLACK);
}

static void draw_partial_patch(int index)
{
    int x = 72 + (index % 3) * 136;
    int y = 246 + (index / 3) * 96;

    wodle_display_fill_rect(&s_bw_display, 54, 226, 416, 232, WODLE_COLOR_WHITE);
    wodle_display_draw_rect(&s_bw_display, 42, 214, 444, 268, WODLE_COLOR_BLACK);
    wodle_display_fill_rect(&s_bw_display, x, y, 96, 56, WODLE_COLOR_BLACK);
    wodle_display_draw_rect(&s_bw_display, x, y, 96, 56, WODLE_COLOR_BLACK);
    wodle_display_draw_text(&s_bw_display, x + 12, y + 20, "DU", 1, WODLE_COLOR_WHITE);
    wodle_display_draw_text(&s_bw_display, 72, 420, "rect 54,226,416,232", 1, WODLE_COLOR_BLACK);
}

static void draw_gray4_page(void)
{
    s_partial_page_ready = RT_FALSE;
    wodle_gray4_display_clear(&s_gray_display, WODLE_GRAY4_WHITE);
    wodle_gray4_display_draw_rect(&s_gray_display, 0, 0, WODLE_EPD_DEVICE_WIDTH, WODLE_EPD_DEVICE_HEIGHT, WODLE_GRAY4_BLACK);
    wodle_gray4_display_fill_rect(&s_gray_display, 40, 92, 100, 420, WODLE_GRAY4_BLACK);
    wodle_gray4_display_fill_rect(&s_gray_display, 150, 92, 100, 420, WODLE_GRAY4_DARK);
    wodle_gray4_display_fill_rect(&s_gray_display, 260, 92, 100, 420, WODLE_GRAY4_LIGHT);
    wodle_gray4_display_fill_rect(&s_gray_display, 370, 92, 100, 420, WODLE_GRAY4_WHITE);
    wodle_gray4_display_draw_rect(&s_gray_display, 370, 92, 100, 420, WODLE_GRAY4_BLACK);
    wodle_gray4_display_draw_rect(&s_gray_display, 40, 92, 470, 420, WODLE_GRAY4_BLACK);
    wodle_gray4_display_draw_rect(&s_gray_display, 36, 86, 478, 432, WODLE_GRAY4_DARK);
    for (int i = 0; i < 12; i++)
    {
        wodle_gray4_t color = (wodle_gray4_t)(i % 4);
        wodle_gray4_display_draw_hline(&s_gray_display, 54, 570 + i * 8, 420, color);
    }
}

static void run_command(const char *line)
{
    int value;
    static int partial_index;

    usb_log_printf("[%s] cmd=%s\r\n", LOG_PREFIX, line);

    if (rt_strcmp(line, "help") == 0 || rt_strcmp(line, "?") == 0)
    {
        usb_log_printf("[%s] commands: status init white black checker text gc du partial gray4 sleep light N up down\r\n", LOG_PREFIX);
        usb_log_printf("[%s] public refresh APIs: full fast partial_fast gray4_full\r\n", LOG_PREFIX);
        return;
    }

    if (rt_strcmp(line, "status") == 0)
    {
        log_status();
        return;
    }

    if (rt_strcmp(line, "init") == 0)
    {
        rt_uint32_t start = rt_tick_get();
        rt_err_t err = wodle_epd_init();
        usb_log_printf("[%s] init ret=%d elapsed=%lums bus=%s\r\n",
                       LOG_PREFIX,
                       err,
                       (unsigned long)elapsed_ms(start),
                       wodle_epd_bus_name());
        log_epd_stats("init");
        return;
    }

    if (rt_strcmp(line, "white") == 0)
    {
        s_partial_page_ready = RT_FALSE;
        wodle_display_clear(&s_bw_display, WODLE_COLOR_WHITE);
        refresh_gc("white-gc");
        return;
    }

    if (rt_strcmp(line, "black") == 0)
    {
        s_partial_page_ready = RT_FALSE;
        wodle_display_clear(&s_bw_display, WODLE_COLOR_BLACK);
        refresh_gc("black-gc");
        return;
    }

    if (rt_strcmp(line, "checker") == 0)
    {
        s_partial_page_ready = RT_FALSE;
        wodle_display_draw_checker(&s_bw_display, 32);
        wodle_display_draw_rect(&s_bw_display, 0, 0, WODLE_EPD_DEVICE_WIDTH, WODLE_EPD_DEVICE_HEIGHT, WODLE_COLOR_BLACK);
        refresh_gc("checker-gc");
        return;
    }

    if (rt_strcmp(line, "text") == 0 || rt_strcmp(line, "gc") == 0)
    {
        draw_text_page();
        refresh_gc("gc-full");
        return;
    }

    if (rt_strcmp(line, "du") == 0)
    {
        draw_du_page();
        refresh_du("du-full");
        return;
    }

    if (rt_strcmp(line, "partial") == 0)
    {
        if (!s_partial_page_ready)
        {
            draw_partial_base_page();
            refresh_gc("partial-base-gc");
        }

        draw_partial_patch(partial_index);
        partial_index = (partial_index + 1) % 6;
        refresh_partial_du("partial-du", 54, 226, 416, 232);
        return;
    }

    if (rt_strcmp(line, "gray4") == 0)
    {
        draw_gray4_page();
        refresh_gray4_full("gray4-full");
        return;
    }

    if (rt_strcmp(line, "sleep") == 0)
    {
        rt_err_t err = wodle_epd_sleep();
        usb_log_printf("[%s] sleep ret=%d; use init before next refresh if needed\r\n",
                       LOG_PREFIX,
                       err);
        log_epd_stats("sleep");
        return;
    }

    if (rt_strcmp(line, "up") == 0)
    {
        rt_err_t err = wodle_frontlight_set_level(wodle_frontlight_get_level() + FRONTLIGHT_STEP);
        usb_log_printf("[%s] frontlight up ret=%d level=%d duty=%d\r\n",
                       LOG_PREFIX,
                       err,
                       wodle_frontlight_get_level(),
                       wodle_frontlight_get_duty());
        return;
    }

    if (rt_strcmp(line, "down") == 0)
    {
        rt_err_t err = wodle_frontlight_set_level(wodle_frontlight_get_level() - FRONTLIGHT_STEP);
        usb_log_printf("[%s] frontlight down ret=%d level=%d duty=%d\r\n",
                       LOG_PREFIX,
                       err,
                       wodle_frontlight_get_level(),
                       wodle_frontlight_get_duty());
        return;
    }

    if (rt_strncmp(line, "light ", 6) == 0)
    {
        if (parse_int(line + 6, &value))
        {
            rt_err_t err = wodle_frontlight_set_level(value);
            usb_log_printf("[%s] frontlight set ret=%d level=%d duty=%d\r\n",
                           LOG_PREFIX,
                           err,
                           wodle_frontlight_get_level(),
                           wodle_frontlight_get_duty());
        }
        else
        {
            usb_log_printf("[%s] ERROR usage: light 0..100\r\n", LOG_PREFIX);
        }
        return;
    }

    usb_log_printf("[%s] unknown command, type help\r\n", LOG_PREFIX);
}

static int read_key_raw(const key_state_t *key)
{
    return rt_pin_read(key->pin) ? 1 : 0;
}

static void poll_key(key_state_t *key, int delta)
{
    int raw = read_key_raw(key);

    if (raw == key->last_raw)
    {
        return;
    }

    key->last_raw = raw;
    usb_log_printf("[%s] key name=%s raw=%d tick=%lu\r\n",
                   LOG_PREFIX,
                   key->name,
                   raw,
                   (unsigned long)rt_tick_get());

    if (raw)
    {
        wodle_frontlight_set_level(wodle_frontlight_get_level() + delta);
        usb_log_printf("[%s] frontlight key level=%d duty=%d\r\n",
                       LOG_PREFIX,
                       wodle_frontlight_get_level(),
                       wodle_frontlight_get_duty());
    }
}

int main(void)
{
    hold_power();
    setup_key_pins();

    rt_kprintf("\r\n[%s] boot %s %s\r\n", LOG_PREFIX, __DATE__, __TIME__);

    rt_err_t usb_ret = usb_log_init(run_command);
    rt_kprintf("[%s] usb_log_init=%d\r\n", LOG_PREFIX, usb_ret);

    usb_log_printf("[%s] boot\r\n", LOG_PREFIX);
    usb_log_printf("[%s] LCDC1 SPI DCX EPD demo; bus=%s\r\n", LOG_PREFIX, wodle_epd_bus_name());
    usb_log_printf("[%s] type help; history replays early logs\r\n", LOG_PREFIX);

    s_key_up.last_raw = read_key_raw(&s_key_up);
    s_key_down.last_raw = read_key_raw(&s_key_down);

    rt_err_t fl_ret = wodle_frontlight_init();
    wodle_frontlight_set_level(20);
    usb_log_printf("[%s] frontlight init=%d level=%d duty=%d\r\n",
                   LOG_PREFIX,
                   fl_ret,
                   wodle_frontlight_get_level(),
                   wodle_frontlight_get_duty());

    rt_err_t bw_ret = wodle_display_init(&s_bw_display, s_bw_frame, sizeof(s_bw_frame));
    rt_err_t gray_ret = wodle_gray4_display_init(&s_gray_display,
                                                 s_gray_plane10,
                                                 s_gray_plane13,
                                                 sizeof(s_gray_plane10));
    usb_log_printf("[%s] display init bw=%d gray4=%d width=%d height=%d frame_bytes=%lu\r\n",
                   LOG_PREFIX,
                   bw_ret,
                   gray_ret,
                   WODLE_EPD_DEVICE_WIDTH,
                   WODLE_EPD_DEVICE_HEIGHT,
                   (unsigned long)WODLE_EPD_FRAME_BYTES);

    rt_uint32_t init_start = rt_tick_get();
    rt_err_t epd_ret = wodle_epd_init();
    usb_log_printf("[%s] epd init=%d elapsed=%lums\r\n",
                   LOG_PREFIX,
                   epd_ret,
                   (unsigned long)elapsed_ms(init_start));
    log_epd_stats("boot-init");

    draw_boot_page();
    refresh_gc("boot-gc");

    rt_uint32_t last_heartbeat = rt_tick_get();
    while (1)
    {
        poll_key(&s_key_up, FRONTLIGHT_STEP);
        poll_key(&s_key_down, -FRONTLIGHT_STEP);

        rt_uint32_t now = rt_tick_get();
        if ((now - last_heartbeat) >= rt_tick_from_millisecond(HEARTBEAT_MS))
        {
            last_heartbeat = now;
            usb_log_printf("[%s] heartbeat refresh_count=%lu light=%d duty=%d tick=%lu bus=%s\r\n",
                           LOG_PREFIX,
                           (unsigned long)s_refresh_count,
                           wodle_frontlight_get_level(),
                           wodle_frontlight_get_duty(),
                           (unsigned long)now,
                           wodle_epd_bus_name());
        }

        rt_thread_mdelay(KEY_POLL_MS);
    }

    return 0;
}
