#ifndef WODLE_GRAY4_DISPLAY_H
#define WODLE_GRAY4_DISPLAY_H

#include "wodle_epd.h"

typedef enum
{
    WODLE_GRAY4_BLACK = 0,
    WODLE_GRAY4_DARK = 1,
    WODLE_GRAY4_LIGHT = 2,
    WODLE_GRAY4_WHITE = 3,
} wodle_gray4_t;

typedef struct
{
    uint8_t *plane10;
    uint8_t *plane13;
    rt_size_t plane_bytes;
    int width;
    int height;
} wodle_gray4_display_t;

rt_err_t wodle_gray4_display_init(wodle_gray4_display_t *display,
                                  uint8_t *plane10,
                                  uint8_t *plane13,
                                  rt_size_t plane_bytes);
rt_err_t wodle_gray4_display_refresh(wodle_gray4_display_t *display);
rt_err_t wodle_gray4_display_refresh_partial(wodle_gray4_display_t *display, int x, int y, int width, int height);
rt_err_t wodle_gray4_display_load_rle(wodle_gray4_display_t *display,
                                      const uint8_t *plane10_rle,
                                      rt_uint32_t plane10_rle_len,
                                      const uint8_t *plane13_rle,
                                      rt_uint32_t plane13_rle_len);
void wodle_gray4_display_clear(wodle_gray4_display_t *display, wodle_gray4_t color);
void wodle_gray4_display_draw_pixel(wodle_gray4_display_t *display, int x, int y, wodle_gray4_t color);
void wodle_gray4_display_draw_hline(wodle_gray4_display_t *display, int x, int y, int width, wodle_gray4_t color);
void wodle_gray4_display_draw_vline(wodle_gray4_display_t *display, int x, int y, int height, wodle_gray4_t color);
void wodle_gray4_display_draw_rect(wodle_gray4_display_t *display, int x, int y, int width, int height, wodle_gray4_t color);
void wodle_gray4_display_fill_rect(wodle_gray4_display_t *display, int x, int y, int width, int height, wodle_gray4_t color);

#endif
