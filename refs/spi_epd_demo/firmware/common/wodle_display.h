#ifndef WODLE_DISPLAY_H
#define WODLE_DISPLAY_H

#include "wodle_epd.h"

typedef enum
{
    WODLE_COLOR_WHITE = 0,
    WODLE_COLOR_BLACK = 1,
} wodle_color_t;

typedef struct
{
    uint8_t *frame;
    rt_size_t frame_bytes;
    int width;
    int height;
} wodle_display_t;

rt_err_t wodle_display_init(wodle_display_t *display, uint8_t *frame, rt_size_t frame_bytes);
rt_err_t wodle_display_refresh(wodle_display_t *display);
rt_err_t wodle_display_refresh_fast(wodle_display_t *display);
rt_err_t wodle_display_refresh_partial_fast(wodle_display_t *display, int x, int y, int width, int height);
void wodle_display_clear(wodle_display_t *display, wodle_color_t color);
void wodle_display_draw_pixel(wodle_display_t *display, int x, int y, wodle_color_t color);
void wodle_display_draw_hline(wodle_display_t *display, int x, int y, int width, wodle_color_t color);
void wodle_display_draw_vline(wodle_display_t *display, int x, int y, int height, wodle_color_t color);
void wodle_display_draw_line(wodle_display_t *display, int x0, int y0, int x1, int y1, wodle_color_t color);
void wodle_display_draw_rect(wodle_display_t *display, int x, int y, int width, int height, wodle_color_t color);
void wodle_display_fill_rect(wodle_display_t *display, int x, int y, int width, int height, wodle_color_t color);
void wodle_display_draw_checker(wodle_display_t *display, int cell_size);
void wodle_display_draw_text(wodle_display_t *display, int x, int y, const char *text, int scale, wodle_color_t color);
int wodle_display_text_width(const char *text, int scale);
int wodle_display_text_height(int scale);

#endif
