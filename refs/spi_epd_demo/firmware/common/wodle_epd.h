#ifndef WODLE_EPD_H
#define WODLE_EPD_H

#include "rtthread.h"
#include <stdint.h>

#define WODLE_EPD_DEVICE_WIDTH 528
#define WODLE_EPD_DEVICE_HEIGHT 792
#define WODLE_EPD_SRC_PIXELS 792
#define WODLE_EPD_GATE_LINES 528
#define WODLE_EPD_ROW_BYTES (WODLE_EPD_SRC_PIXELS / 8)
#define WODLE_EPD_FRAME_BYTES (WODLE_EPD_SRC_PIXELS * WODLE_EPD_GATE_LINES / 8)

typedef struct
{
    rt_uint32_t reset_wait_ms;
    rt_uint32_t init_wait_ms;
    rt_uint32_t power_on_wait_ms;
    rt_uint32_t refresh_wait_ms;
    rt_uint32_t power_off_wait_ms;
    rt_uint32_t lut_write_ms;
    rt_uint32_t new_frame_write_ms;
    rt_uint32_t old_frame_write_ms;
    rt_uint32_t refresh_cmd_ms;
    rt_uint32_t settle_wait_ms;
    rt_uint32_t refresh_total_ms;
    rt_uint32_t refresh_bytes;
    int refresh_x;
    int refresh_y;
    int refresh_w;
    int refresh_h;
    rt_bool_t last_refresh_partial;
    rt_bool_t last_wait_timed_out;
} wodle_epd_stats_t;

typedef enum
{
    WODLE_EPD_LAB_LUT_KEEP = 0,
    WODLE_EPD_LAB_LUT_GC,
    WODLE_EPD_LAB_LUT_DU,
    WODLE_EPD_LAB_LUT_GRAY4,
    WODLE_EPD_LAB_LUT_ZERO,
} wodle_epd_lab_lut_t;

#define WODLE_EPD_LAB_PSR_KEEP 0xFF
#define WODLE_EPD_LAB_OPT_KEEP 0xFF

typedef struct
{
    uint8_t vcom_and_data_interval;
    uint8_t kw_lut_option[3];
    uint8_t partial_scan;
} wodle_epd_lab_options_t;

const char *wodle_epd_bus_name(void);
void wodle_epd_set_gc_settle_ms(rt_uint32_t settle_ms);
rt_uint32_t wodle_epd_get_gc_settle_ms(void);
rt_err_t wodle_epd_init(void);
rt_err_t wodle_epd_refresh_full(const uint8_t *frame);
rt_err_t wodle_epd_refresh_fast(const uint8_t *frame);
rt_err_t wodle_epd_refresh_partial_fast(const uint8_t *frame, int x, int y, int width, int height);
rt_err_t wodle_epd_refresh_gray4_full(const uint8_t *plane10, const uint8_t *plane13);
rt_err_t wodle_epd_refresh_gray4_partial(const uint8_t *plane10,
                                         const uint8_t *plane13,
                                         int x,
                                         int y,
                                         int width,
                                         int height);
rt_err_t wodle_epd_sleep(void);
void wodle_epd_get_stats(wodle_epd_stats_t *stats);
rt_err_t wodle_epd_lab_set_panel_setting(uint8_t psr0, uint8_t psr1);
rt_err_t wodle_epd_lab_refresh_planes(const uint8_t *plane10,
                                      const uint8_t *plane13,
                                      int x,
                                      int y,
                                      int width,
                                      int height,
                                      rt_bool_t partial,
                                      wodle_epd_lab_lut_t lut,
                                      uint8_t psr0,
                                      uint8_t psr1);
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
                                         const wodle_epd_lab_options_t *options);

#endif
