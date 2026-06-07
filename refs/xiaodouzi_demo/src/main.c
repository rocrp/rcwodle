#include "rtthread.h"
#include "rtdevice.h"
#include <stdio.h>
#include <string.h>
#include "bf0_hal.h"
#include "backlight.h"
#include "power.h"
#include "epd_uc8179c.h"
#include "drv_touch.h"
#include "drv_io.h"
#include "rtc_config.h"
#include "bq27220.h"
#include "aw32001e.h"
#include "u8g2_port.h"
#include "sd_spi.h"
#include <dfs_fs.h>
#include <dfs_posix.h>

#define PWR_EN 10
#define KEY2   43
#define KEY3   44
#define PWRKEY 34

#define TOP_BAR_H      44
#define ROW_START_Y    44
#define ROW_H          176
#define ROW_TOP(r)     (ROW_START_Y + (r) * ROW_H)
#define ROW_BOT(r)     (ROW_TOP(r) + ROW_H)
#define GRID_COLS       3
#define GRID_ROWS       4
#define COL_W          (528 / GRID_COLS)
#define COL_CENTER(c)  ((c) * COL_W + COL_W / 2)
#define BOT_BAR_Y      (ROW_BOT(GRID_ROWS - 1))
#define BOT_BAR_H      44
#define ITEMS_PER_PAGE (GRID_COLS * GRID_ROWS)

static const char *page_labels[][ITEMS_PER_PAGE] = {
    {
        "Settings", "Music",    "Weather",
        "Notes",    "SD Test",  "Bluetooth",
        "Timer",    "Calendar", "Calculator",
        "Battery",  "Gray Test","Tools"
    },
    {
        "Compass",  "Alarm",    "Stopwatch",
        "Flashlight","QRCode",  "Radio",
        "Pedometer","Recorder", "Barometer",
        "WorldClock","Memo",    "Vibration"
    },
};
static int page_count = sizeof(page_labels) / sizeof(page_labels[0]);
static int current_page = 0;

static int brightness_level = 1;
static const int brightness_table[] = {0, 30, 35, 40, 45, 50, 55, 60, 65, 70};
#define BRIGHTNESS_NUM (sizeof(brightness_table) / sizeof(brightness_table[0]))

static volatile int need_refresh = 0;
static volatile int sleep_requested = 0;
static struct rt_mutex epd_lock;
static volatile int touch_x = -1, touch_y = -1;
static volatile int touch_active = 0;
static int touch_start_x, touch_start_y;
static int min_x = 20, max_x = 526, min_y = 5, max_y = 791;
static uint8_t fb[EPD_FRAME_BYTES];
static u8g2_t g_u8g2;

static char time_str[8] = "--:--";
static char batt_str[8] = "---%";
static int highlight_cell = -1;
static int app_mode = 0;
static volatile int sdf_busy = 0;
static int sd_ready = 0;
static int sd_line_count = 0;
static char sd_lines[20][36];
static int bat_chg_stat;
static int16_t bat_current_ma;
static uint16_t bat_voltage_mv;
static uint16_t bat_soc_pct;
static uint16_t bat_full_mah;
static uint16_t bat_soh_pct;
static uint32_t last_tap_tick;

static uint8_t gs_bp0[EPD_FRAME_BYTES] __attribute__((section(".psram_bss")));
static uint8_t gs_bp1[EPD_FRAME_BYTES] __attribute__((section(".psram_bss")));

static void gs_set_pixel(uint8_t *bp0, uint8_t *bp1, int x, int y, int level)
{
    rt_size_t off = (rt_size_t)x * EPD_ROW_BYTES + (rt_size_t)y / 8;
    uint8_t mask = 0x80 >> (y % 8);
    if (!(level & 2)) bp0[off] |= mask; else bp0[off] &= ~mask;
    if (!(level & 1)) bp1[off] |= mask; else bp1[off] &= ~mask;
}

static void gs_fill_rect(uint8_t *bp0, uint8_t *bp1, int x, int y, int w, int h, int level)
{
    for (int i = 0; i < h; i++)
        for (int j = 0; j < w; j++)
            gs_set_pixel(bp0, bp1, x + j, y + i, level);
}

static void gs_clear(uint8_t *bp0, uint8_t *bp1, int level)
{
    uint8_t b0 = (level & 2) ? 0x00 : 0xFF;
    uint8_t b1 = (level & 1) ? 0x00 : 0xFF;
    memset(bp0, b0, EPD_FRAME_BYTES);
    memset(bp1, b1, EPD_FRAME_BYTES);
}

static void gs_from_fb(uint8_t *bp0, uint8_t *bp1, const uint8_t *fb)
{
    for (int i = 0; i < EPD_FRAME_BYTES; i++) {
        uint8_t inv = ~fb[i];
        bp0[i] = inv;
        bp1[i] = inv;
    }
}

static void battery_gs_render(void)
{
    u8g2_t *u = &g_u8g2;

    epd_clear();

    u8g2_ClearBuffer(u);
    u8g2_SetFont(u, u8g2_font_helvR14_tf);
    u8g2_DrawStr(u, 10, 30, "Battery");
    u8g2_SetFont(u, u8g2_font_helvR10_tf);
    char buf[40];
    int y = 65;
    const char *chg_labels[] = {"Not Charging", "Pre-charge", "Charging", "Full"};
    int chg_idx = (bat_chg_stat >= 0 && bat_chg_stat <= 3) ? bat_chg_stat : 0;
    snprintf(buf, sizeof(buf), "Status: %s", chg_labels[chg_idx]);
    u8g2_DrawStr(u, 10, y, buf); y += 24;
    snprintf(buf, sizeof(buf), "Voltage: %umV", bat_voltage_mv);
    u8g2_DrawStr(u, 10, y, buf); y += 24;
    snprintf(buf, sizeof(buf), "Current: %+dmA", bat_current_ma);
    u8g2_DrawStr(u, 10, y, buf); y += 24;
    snprintf(buf, sizeof(buf), "SOC: %u%% (%u/%umAh)", bat_soc_pct,
             bat_soc_pct * bat_full_mah / 100, bat_full_mah);
    u8g2_DrawStr(u, 10, y, buf); y += 24;
    snprintf(buf, sizeof(buf), "Health: %u%%", bat_soh_pct);
    u8g2_DrawStr(u, 10, y, buf); y += 24;
    if (bat_chg_stat == AW32001E_CHG_STAT_FAST && bat_current_ma > 0 && bat_full_mah > 0) {
        uint16_t rem = bat_full_mah - (bat_soc_pct * bat_full_mah / 100);
        uint16_t ttf_min = rem * 60 / bat_current_ma;
        snprintf(buf, sizeof(buf), "TTF: ~%umin", ttf_min);
        u8g2_DrawStr(u, 10, y, buf); y += 24;
    }
    u8g2_DrawStr(u, 10, 780, "Tap to go back");

    int bx = 400, by = 200, bw = 80, bh = 280, nub_w = 16, nub_h = 50;
    int nub_x = bx + bw, nub_y = by + (bh - nub_h) / 2;
    int m = 4;
    u8g2_SetDrawColor(u, 1);
    u8g2_DrawBox(u, nub_x, nub_y, nub_w, nub_h);
    u8g2_DrawFrame(u, bx, by, bw, bh);
    int fill_h = (bh - 2 * m) * bat_soc_pct / 100;
    int fill_y = by + bh - m - fill_h;
    int dith = (bat_soc_pct > 50) ? 1 : 0;
    for (int r = 0; r < fill_h; r++) {
        int yy = fill_y + r;
        for (int c = 0; c < bw - 2 * m; c += 2) {
            if (dith || (r + c) % 3 == 0)
                u8g2_DrawPixel(u, bx + m + c, yy);
        }
    }

    u8g2_port_flush(u, fb);
    epd_render_partial(fb);
}

static void grayscale_test_fn(void)
{
    epd_clear();

    gs_clear(gs_bp0, gs_bp1, 3);

    u8g2_t *u = &g_u8g2;
    u8g2_ClearBuffer(u);
    u8g2_SetFont(u, u8g2_font_helvR14_tf);
    u8g2_DrawStr(u, 10, 30, "Gray Test");
    u8g2_SetFont(u, u8g2_font_helvR10_tf);
    int lx[] = {10, 140, 270, 400};
    const char *labels[] = {"Black", "D.Gray", "L.Gray", "White"};
    char buf[40];
    for (int i = 0; i < 4; i++) {
        snprintf(buf, sizeof(buf), "%d-%s", i, labels[i]);
        u8g2_DrawStr(u, lx[i], 55, buf);
    }
    u8g2_DrawStr(u, 10, 780, "Tap to go back");

    u8g2_port_flush(u, fb);
    gs_from_fb(gs_bp0, gs_bp1, fb);

    int rw = 120, rh = 300, ry = 80;
    for (int i = 0; i < 4; i++)
        gs_fill_rect(gs_bp0, gs_bp1, lx[i], ry, rw, rh, i);

    epd_render_grayscale4(gs_bp0, gs_bp1);
}

static int key_pressed(uint32_t pin)
{
    return HAL_GPIO_ReadPin(hwp_gpio1, pin);
}

static struct rt_device sd_blk_dev;

static rt_err_t sd_blk_init(rt_device_t dev) { return RT_EOK; }
static rt_err_t sd_blk_open(rt_device_t dev, rt_uint16_t oflag) { return RT_EOK; }
static rt_err_t sd_blk_close(rt_device_t dev) { return RT_EOK; }

static rt_size_t sd_blk_read(rt_device_t dev, rt_off_t pos, void *buf, rt_size_t size)
{
    for (rt_size_t i = 0; i < size; i++) {
        uint32_t sector = sd_is_hc() ? pos + i : (pos + i) * 512;
        if (sd_read_sector(sector, (uint8_t *)buf + i * 512) < 0)
            return i;
    }
    return size;
}

static rt_size_t sd_blk_write(rt_device_t dev, rt_off_t pos, const void *buf, rt_size_t size)
{
    for (rt_size_t i = 0; i < size; i++) {
        uint32_t sector = sd_is_hc() ? pos + i : (pos + i) * 512;
        if (sd_write_sector(sector, (const uint8_t *)buf + i * 512) < 0)
            return i;
    }
    return size;
}

static rt_err_t sd_blk_control(rt_device_t dev, int cmd, void *args)
{
    if (cmd == RT_DEVICE_CTRL_BLK_GETGEOME) {
        struct rt_device_blk_geometry *geo = (struct rt_device_blk_geometry *)args;
        geo->bytes_per_sector = 512;
        geo->block_size = 512;
        geo->sector_count = sd_sector_count();
        return RT_EOK;
    }
    return RT_EIO;
}

#ifdef RT_USING_DEVICE_OPS
static const struct rt_device_ops sd_blk_ops = {
    sd_blk_init, sd_blk_open, sd_blk_close,
    sd_blk_read, sd_blk_write, sd_blk_control
};
#endif

static int register_sd_block_device(void)
{
    sd_blk_dev.type = RT_Device_Class_Block;
#ifdef RT_USING_DEVICE_OPS
    sd_blk_dev.ops = &sd_blk_ops;
#else
    sd_blk_dev.init = sd_blk_init;
    sd_blk_dev.open = sd_blk_open;
    sd_blk_dev.close = sd_blk_close;
    sd_blk_dev.read = sd_blk_read;
    sd_blk_dev.write = sd_blk_write;
    sd_blk_dev.control = sd_blk_control;
#endif
    return rt_device_register(&sd_blk_dev, "sd0",
                              RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_REMOVABLE);
}

static void sd_test_fn(void)
{
    sdf_busy = 1;
    sd_line_count = 0;
    u8g2_t *u = &g_u8g2;

    if (!sd_ready) {
        u8g2_ClearBuffer(u);
        u8g2_SetFont(u, u8g2_font_helvR14_tf);
        u8g2_DrawStr(u, 10, 30, "SD Test");
        u8g2_SetFont(u, u8g2_font_helvR10_tf);
        snprintf(sd_lines[sd_line_count], 36, "Init...");
        u8g2_DrawStr(u, 10, 60, sd_lines[sd_line_count]);
        sd_line_count++;
        u8g2_port_flush(u, fb);
        epd_render_partial(fb);

        int err = sd_init();
        if (err) {
            snprintf(sd_lines[sd_line_count++], 36, "Init FAIL: %d", err);
            goto done;
        }
        snprintf(sd_lines[sd_line_count++], 36, "Init OK");

        u8g2_ClearBuffer(u);
        u8g2_SetFont(u, u8g2_font_helvR14_tf);
        u8g2_DrawStr(u, 10, 30, "SD Test");
        u8g2_SetFont(u, u8g2_font_helvR10_tf);
        int dy = 60;
        for (int i = 0; i < sd_line_count; i++, dy += 22)
            u8g2_DrawStr(u, 10, dy, sd_lines[i]);
        snprintf(sd_lines[sd_line_count], 36, "Mount...");
        u8g2_DrawStr(u, 10, dy, sd_lines[sd_line_count]);
        u8g2_port_flush(u, fb);
        epd_render_partial(fb);

        if (register_sd_block_device() != RT_EOK) {
            snprintf(sd_lines[++sd_line_count], 36, "Reg FAIL");
            goto done;
        }
        snprintf(sd_lines[++sd_line_count], 36, "Reg OK");

        if (dfs_mount("sd0", "/", "elm", 0, 0) != 0) {
            snprintf(sd_lines[++sd_line_count], 36, "Mount FAIL");
            goto done;
        }
        snprintf(sd_lines[++sd_line_count], 36, "Mount OK");
        sd_ready = 1;
    }

    DIR *dir = opendir("/");
    if (!dir) {
        snprintf(sd_lines[++sd_line_count], 36, "opendir FAIL");
        goto done;
    }
    snprintf(sd_lines[++sd_line_count], 36, "Files:");
    struct dirent *de;
    while ((de = readdir(dir)) != NULL && sd_line_count < 18) {
        char t = (de->d_type == DT_DIR) ? 'D' : 'F';
        snprintf(sd_lines[sd_line_count], 36, " %c %s", t, de->d_name);
        sd_line_count++;
    }
    closedir(dir);

done:
    u8g2_ClearBuffer(u);
    u8g2_SetFont(u, u8g2_font_helvR14_tf);
    u8g2_DrawStr(u, 10, 30, "SD Test");
    u8g2_SetFont(u, u8g2_font_helvR10_tf);
    int yy = 60;
    for (int i = 0; i < sd_line_count; i++, yy += 22)
        u8g2_DrawStr(u, 10, yy, sd_lines[i]);
    u8g2_DrawStr(u, 10, 780, "Tap to go back");
    u8g2_port_flush(u, fb);
    epd_render_partial(fb);

    sdf_busy = 0;
}

static void battery_detail_fn(void)
{
    int16_t ma = 0;
    uint16_t mv = 0, soc = 0, full = 0, soh = 0;
    uint8_t chg_stat = 0;

    bq27220_read_voltage(&mv);
    bq27220_read_current(&ma);
    bq27220_read_soc_percent(&soc);
    bq27220_read_full(&full);
    bq27220_read_soh(&soh);
    aw32001e_read_status(&chg_stat, NULL, NULL);

    bat_voltage_mv = mv;
    bat_current_ma = ma;
    bat_soc_pct = soc;
    bat_full_mah = full;
    bat_soh_pct = soh;
    bat_chg_stat = (chg_stat == AW32001E_CHG_STAT_NOT) ? 0 : chg_stat;
}

static int debounce_tap(void)
{
    uint32_t now = rt_tick_get();
    if (now - last_tap_tick < RT_TICK_PER_SECOND / 3)
        return 0;
    last_tap_tick = now;
    return 1;
}

static void adjust_brightness(int delta)
{
    brightness_level += delta;
    if (brightness_level < 0) brightness_level = 0;
    if (brightness_level >= BRIGHTNESS_NUM) brightness_level = BRIGHTNESS_NUM - 1;
    backlight_set(brightness_table[brightness_level]);
    need_refresh = 1;
    rtc_config_save(brightness_level);
}

static const char *detect_gesture(int x1, int y1, int x2, int y2)
{
    int dx = x2 - x1;
    int dy = y2 - y1;
    int adx = dx > 0 ? dx : -dx;
    int ady = dy > 0 ? dy : -dy;
    int rx = max_x - min_x;
    int ry = max_y - min_y;
    int range = (rx > ry ? rx : ry);
    int tap_thr = range / 20;
    int swp_thr = range / 6;

    if (adx < tap_thr && ady < tap_thr)
        return "TAP";
    if (adx < swp_thr && ady < swp_thr)
        return "?";

    int pct_y = ry > 0 ? ((y1 - min_y) * 100) / ry : 50;

    if (pct_y > 85 && dy < -swp_thr)
        return "HOME";

    return adx > ady ? (dx > 0 ? "R" : "L") : (dy > 0 ? "D" : "U");
}

static int grid_hit_test(int tx, int ty)
{
    for (int row = 0; row < GRID_ROWS; row++) {
        if (ty >= ROW_TOP(row) && ty < ROW_BOT(row)) {
            int col = tx / COL_W;
            if (col < 0) col = 0;
            if (col >= GRID_COLS) col = GRID_COLS - 1;
            return row * GRID_COLS + col;
        }
    }
    return -1;
}

static void draw_screen(void)
{
    u8g2_t *u = &g_u8g2;
    u8g2_ClearBuffer(u);

    if (app_mode == 1) {
        u8g2_SetFont(u, u8g2_font_helvR14_tf);
        u8g2_DrawStr(u, 10, 30, "SD Test");
        u8g2_SetFont(u, u8g2_font_helvR10_tf);
        int yy = 60;
        for (int i = 0; i < sd_line_count; i++, yy += 22)
            u8g2_DrawStr(u, 10, yy, sd_lines[i]);
        u8g2_DrawStr(u, 10, 780, "Tap to go back");
        u8g2_port_flush(u, fb);
        return;
    }

    if (app_mode == 2 || app_mode == 3)
        return;

    u8g2_SetFont(u, u8g2_font_helvR18_tf);
    u8g2_DrawStr(u, 10, TOP_BAR_H / 2 + 6, time_str);

    int bw = u8g2_GetStrWidth(u, batt_str);
    u8g2_DrawStr(u, 527 - bw, TOP_BAR_H / 2 + 6, batt_str);

    u8g2_DrawLine(u, 0, TOP_BAR_H, 527, TOP_BAR_H);

    u8g2_SetFont(u, u8g2_font_helvR10_tf);
    const char **labels = page_labels[current_page];
    for (int i = 0; i < ITEMS_PER_PAGE; i++) {
        int row = i / GRID_COLS;
        int col = i % GRID_COLS;
        int cx = COL_CENTER(col);
        int ty = ROW_TOP(row);
        int sw = u8g2_GetStrWidth(u, labels[i]);

        if (i == highlight_cell) {
            u8g2_SetDrawColor(u, 2);
            u8g2_DrawBox(u, col * COL_W, ty, COL_W, ROW_H);
            u8g2_SetDrawColor(u, 1);
        }
        u8g2_DrawStr(u, cx - sw / 2, ty + ROW_H / 2 + 4, labels[i]);

        if (row < GRID_ROWS - 1 && col == 0)
            u8g2_DrawLine(u, 0, ROW_BOT(row), 527, ROW_BOT(row));
    }

    for (int c = 1; c < GRID_COLS; c++)
        u8g2_DrawLine(u, c * COL_W, TOP_BAR_H, c * COL_W, BOT_BAR_Y);

    u8g2_DrawLine(u, 0, BOT_BAR_Y, 527, BOT_BAR_Y);
    u8g2_SetFont(u, u8g2_font_helvR10_tf);
    char bl[16];
    snprintf(bl, sizeof(bl), "BL:%d%%", brightness_table[brightness_level]);
    u8g2_DrawStr(u, 10, BOT_BAR_Y + BOT_BAR_H / 2 + 4, bl);

    char page_str[8];
    snprintf(page_str, sizeof(page_str), "%d/%d", current_page + 1, page_count);
    int pw = u8g2_GetStrWidth(u, page_str);
    u8g2_DrawStr(u, 527 - pw, BOT_BAR_Y + BOT_BAR_H / 2 + 4, page_str);

    u8g2_port_flush(u, fb);
}

static void key_thread(void *param)
{
    GPIO_InitTypeDef c = {0};
    c.Mode = GPIO_MODE_INPUT;
    c.Pull = GPIO_PULLDOWN;
    c.Pin = KEY2; HAL_GPIO_Init(hwp_gpio1, &c);
    c.Pin = KEY3; HAL_GPIO_Init(hwp_gpio1, &c);
    c.Pin = PWRKEY; HAL_GPIO_Init(hwp_gpio1, &c);

    while (1) {
        if (key_pressed(PWRKEY)) {
            rt_thread_mdelay(50);
            if (key_pressed(PWRKEY)) {
                while (key_pressed(PWRKEY)) rt_thread_mdelay(10);
                sleep_requested = 1;
            }
        }
        if (key_pressed(KEY3)) {
            adjust_brightness(1);
            while (key_pressed(KEY3)) rt_thread_mdelay(10);
        }
        if (key_pressed(KEY2)) {
            adjust_brightness(-1);
            while (key_pressed(KEY2)) rt_thread_mdelay(10);
        }
        rt_thread_mdelay(20);
    }
}

static void touch_thread(void *param)
{
    rt_device_t touch_dev = rt_device_find("touch");
    if (!touch_dev) {
        while (1) { rt_thread_mdelay(2000); }
    }
    rt_device_open(touch_dev, 0);

    while (1) {
        struct touch_message msg;
        if (rt_device_read(touch_dev, 0, &msg, 1) == 1) {
            if (msg.event == TOUCH_EVENT_DOWN) {
                if (!touch_active) {
                    touch_start_x = msg.x;
                    touch_start_y = msg.y;
                }
                touch_x = msg.x;
                touch_y = msg.y;
                touch_active = 1;
                if (msg.x < min_x) min_x = msg.x;
                if (msg.x > max_x) max_x = msg.x;
                if (msg.y < min_y) min_y = msg.y;
                if (msg.y > max_y) max_y = msg.y;
            } else if (msg.event == TOUCH_EVENT_UP && touch_active) {
                touch_active = 0;
                const char *gesture = detect_gesture(touch_start_x, touch_start_y, touch_x, touch_y);

                if (strcmp(gesture, "TAP") == 0) {
                    if (!debounce_tap()) continue;
                    if (app_mode == 1 || app_mode == 2 || app_mode == 3) {
                        if (app_mode == 3) {
                            sdf_busy = 1;
                            rt_mutex_take(&epd_lock, RT_WAITING_FOREVER);
                            epd_clear();
                            rt_mutex_release(&epd_lock);
                            sdf_busy = 0;
                        }
                        app_mode = 0;
                        highlight_cell = -1;
                        need_refresh = 1;
                    } else {
                        int idx = grid_hit_test(touch_x, touch_y);
                        if (idx >= 0) {
                            const char *label = page_labels[current_page][idx];
                            if (strcmp(label, "SD Test") == 0) {
                                app_mode = 1;
                                rt_mutex_take(&epd_lock, RT_WAITING_FOREVER);
                                sd_test_fn();
                                rt_mutex_release(&epd_lock);
                                need_refresh = 1;
                            } else if (strcmp(label, "Battery") == 0) {
                                app_mode = 2;
                                battery_detail_fn();
                                sdf_busy = 1;
                                rt_mutex_take(&epd_lock, RT_WAITING_FOREVER);
                                battery_gs_render();
                                rt_mutex_release(&epd_lock);
                                sdf_busy = 0;
                                need_refresh = 0;
                            } else if (strcmp(label, "Gray Test") == 0) {
                                app_mode = 3;
                                sdf_busy = 1;
                                rt_mutex_take(&epd_lock, RT_WAITING_FOREVER);
                                grayscale_test_fn();
                                rt_mutex_release(&epd_lock);
                                sdf_busy = 0;
                                need_refresh = 0;
                            } else {
                                highlight_cell = idx;
                                need_refresh = 1;
                            }
                        }
                    }
                } else if (strcmp(gesture, "L") == 0) {
                    if (current_page < page_count - 1) {
                        current_page++;
                        highlight_cell = -1;
                        need_refresh = 1;
                    }
                } else if (strcmp(gesture, "R") == 0) {
                    if (current_page > 0) {
                        current_page--;
                        highlight_cell = -1;
                        need_refresh = 1;
                    }
                }
            }
        }
        rt_thread_mdelay(20);
    }
}

static void battery_thread(void *param)
{
    rt_thread_mdelay(3000);
    uint8_t chipid;
    int chg_ok = (aw32001e_read_chipid(&chipid) == 0);
    int bat_ok = 0;

    while (1) {
        uint16_t mv = 0, soc = 0;
        uint8_t chg_str[8] = "";

        bat_ok = (bq27220_read_voltage(&mv) == 0);
        if (bat_ok && mv > 2500 && mv < 5000) {
            uint16_t dc;
            if (bq27220_read_design_capacity(&dc) == 0 && dc != 850) {
                bq27220_unseal();
                bq27220_write_design_capacity(850);
                rt_thread_mdelay(100);
            }
            if (bq27220_read_soc_percent(&soc) == 0) {
                snprintf(batt_str, sizeof(batt_str), "%d%%", soc);
            }
        }

        if (chg_ok) {
            uint8_t stat;
            if (aw32001e_read_status(&stat, NULL, NULL) == 0 && stat != AW32001E_CHG_STAT_NOT)
                snprintf((char *)chg_str, sizeof(chg_str), "CHG");
        }

        uint8_t h, m, s;
        rtc_read_time(&h, &m, &s);
        if (h < 24 && m < 60)
            snprintf(time_str, sizeof(time_str), "%02d:%02d", h, m);

        need_refresh = 1;
        rt_thread_mdelay(5000);
    }
}

int main(void)
{
    if (power_wakeup_reason() == POWER_ON_KEY)
        rt_thread_mdelay(100);

    rtc_config_init();
    uint8_t saved = rtc_config_load();
    if (saved < BRIGHTNESS_NUM)
        brightness_level = saved;

    backlight_init();
    backlight_set(brightness_table[brightness_level]);

    rt_thread_t tid = rt_thread_create("key", key_thread, RT_NULL, 512, 10, 20);
    if (tid) rt_thread_startup(tid);

    HAL_PIN_Set(PAD_PA10, GPIO_A10, PIN_NOPULL, 1);
    rt_pin_mode(PWR_EN, PIN_MODE_OUTPUT);
    rt_pin_write(PWR_EN, PIN_HIGH);

    HAL_PIN_Set(PAD_PA31, I2C3_SCL, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA32, I2C3_SDA, PIN_PULLUP, 1);

    epd_hw_init();
    u8g2_port_init(&g_u8g2);
    draw_screen();
    rt_mutex_init(&epd_lock, "epd", RT_IPC_FLAG_PRIO);
    epd_render(fb);

    rt_thread_t t = rt_thread_create("touch", touch_thread, RT_NULL, 1024, 20, 20);
    if (t) rt_thread_startup(t);

    rt_thread_t bat = rt_thread_create("bat", battery_thread, RT_NULL, 1024, 12, 20);
    if (bat) rt_thread_startup(bat);

    int refr_busy = 0;
    int fb_ready = 0;
    while (1) {
        if (sleep_requested) {
            sleep_requested = 0;
            need_refresh = 0;
            rtc_config_save(brightness_level);
            uint32_t slp_wait = HAL_GetTick();
            while (refr_busy && HAL_GetTick() - slp_wait < 500)
                rt_thread_mdelay(10);
            rt_mutex_take(&epd_lock, RT_WAITING_FOREVER);
            epd_fill(fb, 0);
            epd_render_partial_start(fb);
            while (!epd_render_partial_poll()) rt_thread_mdelay(10);
            rt_mutex_release(&epd_lock);
            power_sleep();
        }
        if (need_refresh && !refr_busy && app_mode == 0) {
            need_refresh = 0;
            draw_screen();
            fb_ready = 1;
        }
        if (fb_ready && !refr_busy) {
            fb_ready = 0;
            rt_mutex_take(&epd_lock, RT_WAITING_FOREVER);
            epd_render_partial_start(fb);
            refr_busy = 1;
        }
        if (refr_busy) {
            if (epd_render_partial_poll()) {
                refr_busy = 0;
                rt_mutex_release(&epd_lock);
            }
        }
        rt_thread_mdelay(2);
    }
}