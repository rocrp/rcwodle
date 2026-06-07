#include "wodle_gray4_display.h"

#include <string.h>

static rt_size_t pixel_offset(int x, int y)
{
    return (rt_size_t)x * WODLE_EPD_ROW_BYTES + (rt_size_t)y / 8;
}

static uint8_t pixel_mask(int y)
{
    return (uint8_t)(0x80 >> (y & 7));
}

static rt_err_t decode_rle_plane(const uint8_t *rle, rt_uint32_t rle_len, uint8_t *plane, rt_size_t plane_bytes)
{
    rt_uint32_t in = 0;
    rt_uint32_t out = 0;

    if (!rle || !plane || plane_bytes < WODLE_EPD_FRAME_BYTES)
    {
        return -RT_EINVAL;
    }

    while (in + 3 <= rle_len)
    {
        rt_uint32_t count = ((rt_uint32_t)rle[in] << 8) | rle[in + 1];
        uint8_t value = rle[in + 2];

        if (count == 0 || out + count > WODLE_EPD_FRAME_BYTES)
        {
            return -RT_ERROR;
        }

        rt_memset(plane + out, value, count);
        out += count;
        in += 3;
    }

    if (in != rle_len || out != WODLE_EPD_FRAME_BYTES)
    {
        return -RT_ERROR;
    }

    return RT_EOK;
}

rt_err_t wodle_gray4_display_init(wodle_gray4_display_t *display,
                                  uint8_t *plane10,
                                  uint8_t *plane13,
                                  rt_size_t plane_bytes)
{
    if (!display || !plane10 || !plane13 || plane_bytes < WODLE_EPD_FRAME_BYTES)
    {
        return -RT_EINVAL;
    }

    display->plane10 = plane10;
    display->plane13 = plane13;
    display->plane_bytes = plane_bytes;
    display->width = WODLE_EPD_DEVICE_WIDTH;
    display->height = WODLE_EPD_DEVICE_HEIGHT;
    wodle_gray4_display_clear(display, WODLE_GRAY4_WHITE);
    return RT_EOK;
}

rt_err_t wodle_gray4_display_refresh(wodle_gray4_display_t *display)
{
    if (!display || !display->plane10 || !display->plane13)
    {
        return -RT_EINVAL;
    }

    return wodle_epd_refresh_gray4_full(display->plane10, display->plane13);
}

rt_err_t wodle_gray4_display_refresh_partial(wodle_gray4_display_t *display, int x, int y, int width, int height)
{
    if (!display || !display->plane10 || !display->plane13)
    {
        return -RT_EINVAL;
    }

    return wodle_epd_refresh_gray4_partial(display->plane10, display->plane13, x, y, width, height);
}

rt_err_t wodle_gray4_display_load_rle(wodle_gray4_display_t *display,
                                      const uint8_t *plane10_rle,
                                      rt_uint32_t plane10_rle_len,
                                      const uint8_t *plane13_rle,
                                      rt_uint32_t plane13_rle_len)
{
    rt_err_t err;

    if (!display || !display->plane10 || !display->plane13)
    {
        return -RT_EINVAL;
    }

    err = decode_rle_plane(plane10_rle, plane10_rle_len, display->plane10, display->plane_bytes);
    if (err != RT_EOK)
    {
        return err;
    }

    return decode_rle_plane(plane13_rle, plane13_rle_len, display->plane13, display->plane_bytes);
}

void wodle_gray4_display_clear(wodle_gray4_display_t *display, wodle_gray4_t color)
{
    uint8_t value = (uint8_t)color & 0x03;

    if (!display || !display->plane10 || !display->plane13)
    {
        return;
    }

    rt_memset(display->plane10, (value & 0x02) ? 0xFF : 0x00, display->plane_bytes);
    rt_memset(display->plane13, (value & 0x01) ? 0xFF : 0x00, display->plane_bytes);
}

void wodle_gray4_display_draw_pixel(wodle_gray4_display_t *display, int x, int y, wodle_gray4_t color)
{
    rt_size_t offset;
    uint8_t mask;
    uint8_t value = (uint8_t)color & 0x03;

    if (!display || !display->plane10 || !display->plane13 ||
        x < 0 || x >= display->width ||
        y < 0 || y >= display->height)
    {
        return;
    }

    offset = pixel_offset(x, y);
    mask = pixel_mask(y);

    if (value & 0x02)
    {
        display->plane10[offset] |= mask;
    }
    else
    {
        display->plane10[offset] &= (uint8_t)~mask;
    }

    if (value & 0x01)
    {
        display->plane13[offset] |= mask;
    }
    else
    {
        display->plane13[offset] &= (uint8_t)~mask;
    }
}

void wodle_gray4_display_draw_hline(wodle_gray4_display_t *display, int x, int y, int width, wodle_gray4_t color)
{
    if (width < 0)
    {
        x += width + 1;
        width = -width;
    }

    for (int i = 0; i < width; i++)
    {
        wodle_gray4_display_draw_pixel(display, x + i, y, color);
    }
}

void wodle_gray4_display_draw_vline(wodle_gray4_display_t *display, int x, int y, int height, wodle_gray4_t color)
{
    if (height < 0)
    {
        y += height + 1;
        height = -height;
    }

    for (int i = 0; i < height; i++)
    {
        wodle_gray4_display_draw_pixel(display, x, y + i, color);
    }
}

void wodle_gray4_display_draw_rect(wodle_gray4_display_t *display, int x, int y, int width, int height, wodle_gray4_t color)
{
    if (width <= 0 || height <= 0)
    {
        return;
    }

    wodle_gray4_display_draw_hline(display, x, y, width, color);
    wodle_gray4_display_draw_hline(display, x, y + height - 1, width, color);
    wodle_gray4_display_draw_vline(display, x, y, height, color);
    wodle_gray4_display_draw_vline(display, x + width - 1, y, height, color);
}

void wodle_gray4_display_fill_rect(wodle_gray4_display_t *display, int x, int y, int width, int height, wodle_gray4_t color)
{
    if (width <= 0 || height <= 0)
    {
        return;
    }

    for (int yy = 0; yy < height; yy++)
    {
        wodle_gray4_display_draw_hline(display, x, y + yy, width, color);
    }
}
