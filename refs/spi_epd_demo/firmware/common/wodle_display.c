#include "wodle_display.h"

#include "font_8x16.h"
#include <string.h>

static int abs_int(int value)
{
    return value < 0 ? -value : value;
}

static int normalize_scale(int scale)
{
    return scale <= 0 ? 1 : scale;
}

rt_err_t wodle_display_init(wodle_display_t *display, uint8_t *frame, rt_size_t frame_bytes)
{
    if (!display || !frame || frame_bytes < WODLE_EPD_FRAME_BYTES)
    {
        return -RT_EINVAL;
    }

    display->frame = frame;
    display->frame_bytes = frame_bytes;
    display->width = WODLE_EPD_DEVICE_WIDTH;
    display->height = WODLE_EPD_DEVICE_HEIGHT;
    wodle_display_clear(display, WODLE_COLOR_WHITE);
    return RT_EOK;
}

rt_err_t wodle_display_refresh(wodle_display_t *display)
{
    if (!display || !display->frame)
    {
        return -RT_EINVAL;
    }

    return wodle_epd_refresh_full(display->frame);
}

rt_err_t wodle_display_refresh_fast(wodle_display_t *display)
{
    if (!display || !display->frame)
    {
        return -RT_EINVAL;
    }

    return wodle_epd_refresh_fast(display->frame);
}

rt_err_t wodle_display_refresh_partial_fast(wodle_display_t *display, int x, int y, int width, int height)
{
    if (!display || !display->frame)
    {
        return -RT_EINVAL;
    }

    return wodle_epd_refresh_partial_fast(display->frame, x, y, width, height);
}

void wodle_display_clear(wodle_display_t *display, wodle_color_t color)
{
    if (!display || !display->frame)
    {
        return;
    }

    rt_memset(display->frame,
              color == WODLE_COLOR_BLACK ? 0x00 : 0xFF,
              display->frame_bytes);
}

void wodle_display_draw_pixel(wodle_display_t *display, int x, int y, wodle_color_t color)
{
    rt_size_t offset;
    uint8_t mask;

    if (!display || !display->frame ||
        x < 0 || x >= display->width ||
        y < 0 || y >= display->height)
    {
        return;
    }

    offset = (rt_size_t)x * WODLE_EPD_ROW_BYTES + (rt_size_t)y / 8;
    mask = (uint8_t)(0x80 >> (y % 8));

    if (color == WODLE_COLOR_BLACK)
    {
        display->frame[offset] &= (uint8_t)~mask;
    }
    else
    {
        display->frame[offset] |= mask;
    }
}

void wodle_display_draw_hline(wodle_display_t *display, int x, int y, int width, wodle_color_t color)
{
    if (width < 0)
    {
        x += width + 1;
        width = -width;
    }

    for (int i = 0; i < width; i++)
    {
        wodle_display_draw_pixel(display, x + i, y, color);
    }
}

void wodle_display_draw_vline(wodle_display_t *display, int x, int y, int height, wodle_color_t color)
{
    if (height < 0)
    {
        y += height + 1;
        height = -height;
    }

    for (int i = 0; i < height; i++)
    {
        wodle_display_draw_pixel(display, x, y + i, color);
    }
}

void wodle_display_draw_line(wodle_display_t *display, int x0, int y0, int x1, int y1, wodle_color_t color)
{
    int dx = abs_int(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs_int(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (1)
    {
        wodle_display_draw_pixel(display, x0, y0, color);

        if (x0 == x1 && y0 == y1)
        {
            break;
        }

        int e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }

        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

void wodle_display_draw_rect(wodle_display_t *display, int x, int y, int width, int height, wodle_color_t color)
{
    if (width <= 0 || height <= 0)
    {
        return;
    }

    wodle_display_draw_hline(display, x, y, width, color);
    wodle_display_draw_hline(display, x, y + height - 1, width, color);
    wodle_display_draw_vline(display, x, y, height, color);
    wodle_display_draw_vline(display, x + width - 1, y, height, color);
}

void wodle_display_fill_rect(wodle_display_t *display, int x, int y, int width, int height, wodle_color_t color)
{
    if (width <= 0 || height <= 0)
    {
        return;
    }

    for (int yy = 0; yy < height; yy++)
    {
        wodle_display_draw_hline(display, x, y + yy, width, color);
    }
}

void wodle_display_draw_checker(wodle_display_t *display, int cell_size)
{
    if (!display || cell_size <= 0)
    {
        cell_size = 24;
    }

    if (!display)
    {
        return;
    }

    for (int x = 0; x < display->width; x++)
    {
        for (int y = 0; y < display->height; y++)
        {
            wodle_color_t color = (((x / cell_size) + (y / cell_size)) & 1)
                                      ? WODLE_COLOR_BLACK
                                      : WODLE_COLOR_WHITE;
            wodle_display_draw_pixel(display, x, y, color);
        }
    }
}

static void draw_glyph(wodle_display_t *display, int x0, int y0, char ch, int scale, wodle_color_t color)
{
    if (ch < FONT_FIRST || ch > FONT_LAST)
    {
        ch = '?';
    }

    scale = normalize_scale(scale);

    const uint8_t *rows = font_8x16[ch - FONT_FIRST];
    for (int gy = 0; gy < FONT_H; gy++)
    {
        uint8_t row = rows[gy];
        for (int gx = 0; gx < FONT_W; gx++)
        {
            if (!(row & (0x80 >> gx)))
            {
                continue;
            }

            wodle_display_fill_rect(display,
                                    x0 + gx * scale,
                                    y0 + gy * scale,
                                    scale,
                                    scale,
                                    color);
        }
    }
}

void wodle_display_draw_text(wodle_display_t *display, int x, int y, const char *text, int scale, wodle_color_t color)
{
    int cursor_x = x;
    int cursor_y = y;
    int cell_w;
    int cell_h;

    if (!display || !text)
    {
        return;
    }

    scale = normalize_scale(scale);
    cell_w = FONT_W * scale;
    cell_h = FONT_H * scale;

    for (const char *p = text; *p; p++)
    {
        if (*p == '\n')
        {
            cursor_x = x;
            cursor_y += cell_h + scale;
            continue;
        }

        draw_glyph(display, cursor_x, cursor_y, *p, scale, color);
        cursor_x += cell_w;

        if (cursor_x + cell_w >= display->width)
        {
            cursor_x = x;
            cursor_y += cell_h + scale;
        }
    }
}

int wodle_display_text_width(const char *text, int scale)
{
    int width = 0;
    int line_width = 0;

    if (!text)
    {
        return 0;
    }

    scale = normalize_scale(scale);

    for (const char *p = text; *p; p++)
    {
        if (*p == '\n')
        {
            if (line_width > width)
            {
                width = line_width;
            }
            line_width = 0;
            continue;
        }

        line_width += FONT_W * scale;
    }

    return line_width > width ? line_width : width;
}

int wodle_display_text_height(int scale)
{
    scale = normalize_scale(scale);
    return FONT_H * scale;
}
