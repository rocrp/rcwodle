#ifndef WODLE_TEXT_H
#define WODLE_TEXT_H

#include "wodle_font.h"
#include "wodle_gray4_display.h"

typedef enum
{
    WODLE_TEXT_ALIGN_LEFT = 0,
    WODLE_TEXT_ALIGN_CENTER,
    WODLE_TEXT_ALIGN_RIGHT,
} wodle_text_align_t;

typedef struct
{
    int x;
    int y;
    int width;
    int height;
} wodle_rect_t;

typedef struct
{
    const wodle_font_t *font;
    int line_height_px;
    int letter_spacing_px;
    wodle_gray4_t foreground;
    wodle_gray4_t background;
    rt_bool_t fill_background;
    wodle_text_align_t align;
} wodle_text_style_t;

typedef struct
{
    wodle_rect_t bounds;
    int lines;
    int glyphs;
    int missing_glyphs;
    rt_bool_t clipped;
} wodle_text_result_t;

rt_err_t wodle_text_draw_box(wodle_gray4_display_t *display,
                             wodle_rect_t rect,
                             const wodle_text_style_t *style,
                             const char *utf8,
                             wodle_text_result_t *result);
int wodle_text_measure_line(const wodle_text_style_t *style, const char *utf8);

#endif
