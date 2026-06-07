#include "wodle_text.h"

#include <string.h>

#define WODLE_TEXT_MAX_LINE_GLYPHS 96

typedef struct
{
    uint32_t codepoint;
    const wodle_font_glyph_t *glyph;
    int advance;
    rt_bool_t missing;
} text_item_t;

typedef struct
{
    const char *start;
    const char *end;
    const char *next;
    rt_bool_t explicit_newline;
    int width;
    int count;
    text_item_t items[WODLE_TEXT_MAX_LINE_GLYPHS];
} text_line_t;

static uint32_t clamp_u32(uint32_t value, uint32_t low, uint32_t high)
{
    if (value < low)
    {
        return low;
    }

    return value > high ? high : value;
}

static int max_int(int a, int b)
{
    return a > b ? a : b;
}

static int min_int(int a, int b)
{
    return a < b ? a : b;
}

static rt_bool_t utf8_next(const char **cursor, uint32_t *codepoint)
{
    const unsigned char *p;

    if (!cursor || !*cursor || !**cursor || !codepoint)
    {
        return RT_FALSE;
    }

    p = (const unsigned char *)*cursor;
    if (p[0] < 0x80)
    {
        *codepoint = p[0];
        *cursor += 1;
        return RT_TRUE;
    }

    if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80)
    {
        *codepoint = ((uint32_t)(p[0] & 0x1F) << 6) |
                     (uint32_t)(p[1] & 0x3F);
        *cursor += 2;
        return RT_TRUE;
    }

    if ((p[0] & 0xF0) == 0xE0 &&
        (p[1] & 0xC0) == 0x80 &&
        (p[2] & 0xC0) == 0x80)
    {
        *codepoint = ((uint32_t)(p[0] & 0x0F) << 12) |
                     ((uint32_t)(p[1] & 0x3F) << 6) |
                     (uint32_t)(p[2] & 0x3F);
        *cursor += 3;
        return RT_TRUE;
    }

    if ((p[0] & 0xF8) == 0xF0 &&
        (p[1] & 0xC0) == 0x80 &&
        (p[2] & 0xC0) == 0x80 &&
        (p[3] & 0xC0) == 0x80)
    {
        *codepoint = ((uint32_t)(p[0] & 0x07) << 18) |
                     ((uint32_t)(p[1] & 0x3F) << 12) |
                     ((uint32_t)(p[2] & 0x3F) << 6) |
                     (uint32_t)(p[3] & 0x3F);
        *cursor += 4;
        return RT_TRUE;
    }

    *codepoint = '?';
    *cursor += 1;
    return RT_TRUE;
}

static rt_bool_t is_space_codepoint(uint32_t codepoint)
{
    return codepoint == ' ' || codepoint == '\t';
}

static rt_bool_t is_ascii_word_break(uint32_t codepoint)
{
    return codepoint == ' ' || codepoint == '\t' || codepoint == '-' || codepoint == '/';
}

static int style_line_height(const wodle_text_style_t *style)
{
    if (style->line_height_px > 0)
    {
        return style->line_height_px;
    }

    return style->font ? style->font->line_height_px : 0;
}

static wodle_gray4_t blend_gray(wodle_gray4_t foreground, wodle_gray4_t background, uint8_t alpha)
{
    int fg = (int)foreground;
    int bg = (int)background;
    int mixed;

    alpha = (uint8_t)clamp_u32(alpha, 0, 3);
    mixed = (fg * alpha + bg * (3 - alpha) + 1) / 3;
    return (wodle_gray4_t)clamp_u32((uint32_t)mixed, WODLE_GRAY4_BLACK, WODLE_GRAY4_WHITE);
}

static void result_reset(wodle_text_result_t *result, wodle_rect_t rect)
{
    if (!result)
    {
        return;
    }

    result->bounds.x = rect.x + rect.width;
    result->bounds.y = rect.y + rect.height;
    result->bounds.width = 0;
    result->bounds.height = 0;
    result->lines = 0;
    result->glyphs = 0;
    result->missing_glyphs = 0;
    result->clipped = RT_FALSE;
}

static void result_add_pixel(wodle_text_result_t *result, int x, int y)
{
    int x0;
    int y0;
    int x1;
    int y1;

    if (!result)
    {
        return;
    }

    if (result->bounds.width == 0 || result->bounds.height == 0)
    {
        result->bounds.x = x;
        result->bounds.y = y;
        result->bounds.width = 1;
        result->bounds.height = 1;
        return;
    }

    x0 = min_int(result->bounds.x, x);
    y0 = min_int(result->bounds.y, y);
    x1 = max_int(result->bounds.x + result->bounds.width - 1, x);
    y1 = max_int(result->bounds.y + result->bounds.height - 1, y);
    result->bounds.x = x0;
    result->bounds.y = y0;
    result->bounds.width = x1 - x0 + 1;
    result->bounds.height = y1 - y0 + 1;
}

static void result_finish(wodle_text_result_t *result, wodle_rect_t rect)
{
    if (!result)
    {
        return;
    }

    if (result->bounds.width == 0 || result->bounds.height == 0)
    {
        result->bounds.x = rect.x;
        result->bounds.y = rect.y;
    }
}

static int glyph_advance(const wodle_text_style_t *style, const wodle_font_glyph_t *glyph)
{
    if (!style || !glyph)
    {
        return 0;
    }

    return glyph->advance + style->letter_spacing_px;
}

static void collect_line(const wodle_text_style_t *style, const char *cursor, wodle_rect_t rect, text_line_t *line)
{
    const char *p = cursor;
    const char *last_break_next = RT_NULL;
    int last_break_count = 0;
    int last_break_width = 0;
    int width = 0;

    rt_memset(line, 0, sizeof(*line));
    line->start = cursor;
    line->end = cursor;
    line->next = cursor;

    while (*p && line->count < WODLE_TEXT_MAX_LINE_GLYPHS)
    {
        const char *before = p;
        uint32_t cp;
        const wodle_font_glyph_t *glyph;
        int advance;
        rt_bool_t missing;

        if (!utf8_next(&p, &cp))
        {
            break;
        }

        if (cp == '\n')
        {
            line->end = before;
            line->next = p;
            line->explicit_newline = RT_TRUE;
            return;
        }

        glyph = wodle_font_find_glyph(style->font, cp);
        missing = glyph && glyph->codepoint != cp;
        advance = glyph_advance(style, glyph);

        if (line->count > 0 && width + advance > rect.width)
        {
            if (last_break_next && last_break_count > 0)
            {
                line->count = last_break_count;
                line->width = last_break_width;
                line->end = last_break_next;
                line->next = last_break_next;
                while (*line->next == ' ' || *line->next == '\t')
                {
                    line->next++;
                }
                return;
            }

            line->end = before;
            line->next = before;
            return;
        }

        line->items[line->count].codepoint = cp;
        line->items[line->count].glyph = glyph;
        line->items[line->count].advance = advance;
        line->items[line->count].missing = missing;
        line->count++;
        width += advance;
        line->width = width;
        line->end = p;
        line->next = p;

        if (is_ascii_word_break(cp))
        {
            last_break_next = p;
            last_break_count = line->count;
            last_break_width = width;
        }
    }
}

static void draw_glyph(wodle_gray4_display_t *display,
                       const wodle_text_style_t *style,
                       const wodle_font_glyph_t *glyph,
                       int x,
                       int y,
                       wodle_rect_t clip,
                       wodle_text_result_t *result)
{
    if (!glyph)
    {
        return;
    }

    for (int gy = 0; gy < glyph->height; gy++)
    {
        int py = y + glyph->y_offset + gy;
        if (py < clip.y || py >= clip.y + clip.height)
        {
            if (result)
            {
                result->clipped = RT_TRUE;
            }
            continue;
        }

        for (int gx = 0; gx < glyph->width; gx++)
        {
            int px = x + glyph->x_offset + gx;
            uint8_t alpha;
            wodle_gray4_t color;

            if (px < clip.x || px >= clip.x + clip.width)
            {
                if (result)
                {
                    result->clipped = RT_TRUE;
                }
                continue;
            }

            alpha = wodle_font_glyph_alpha(style->font, glyph, gx, gy);
            if (alpha == 0)
            {
                continue;
            }

            color = blend_gray(style->foreground, style->background, alpha);
            wodle_gray4_display_draw_pixel(display, px, py, color);
            result_add_pixel(result, px, py);
        }
    }
}

static void draw_line(wodle_gray4_display_t *display,
                      const wodle_text_style_t *style,
                      const text_line_t *line,
                      wodle_rect_t rect,
                      int y,
                      wodle_text_result_t *result)
{
    int x = rect.x;

    if (style->align == WODLE_TEXT_ALIGN_CENTER)
    {
        x += max_int(0, (rect.width - line->width) / 2);
    }
    else if (style->align == WODLE_TEXT_ALIGN_RIGHT)
    {
        x += max_int(0, rect.width - line->width);
    }

    for (int i = 0; i < line->count; i++)
    {
        const text_item_t *item = &line->items[i];

        if (!is_space_codepoint(item->codepoint))
        {
            draw_glyph(display, style, item->glyph, x, y, rect, result);
            if (result)
            {
                result->glyphs++;
                if (item->missing)
                {
                    result->missing_glyphs++;
                }
            }
        }

        x += item->advance;
    }
}

rt_err_t wodle_text_draw_box(wodle_gray4_display_t *display,
                             wodle_rect_t rect,
                             const wodle_text_style_t *style,
                             const char *utf8,
                             wodle_text_result_t *result)
{
    const char *cursor;
    int y;
    int line_height;

    if (!display || !style || !style->font || !utf8 ||
        rect.width <= 0 || rect.height <= 0)
    {
        return -RT_EINVAL;
    }

    result_reset(result, rect);

    if (style->fill_background)
    {
        wodle_gray4_display_fill_rect(display, rect.x, rect.y, rect.width, rect.height, style->background);
        result_add_pixel(result, rect.x, rect.y);
        result_add_pixel(result, rect.x + rect.width - 1, rect.y + rect.height - 1);
    }

    cursor = utf8;
    y = rect.y;
    line_height = style_line_height(style);
    while (*cursor && y + line_height <= rect.y + rect.height)
    {
        text_line_t line;

        collect_line(style, cursor, rect, &line);
        if (line.next == cursor && line.count == 0)
        {
            break;
        }

        draw_line(display, style, &line, rect, y, result);
        if (result)
        {
            result->lines++;
        }

        cursor = line.next;
        y += line_height;
    }

    if (*cursor && result)
    {
        result->clipped = RT_TRUE;
    }

    result_finish(result, rect);
    return RT_EOK;
}

int wodle_text_measure_line(const wodle_text_style_t *style, const char *utf8)
{
    const char *cursor = utf8;
    int width = 0;

    if (!style || !style->font || !utf8)
    {
        return 0;
    }

    while (*cursor)
    {
        uint32_t cp;
        const wodle_font_glyph_t *glyph;

        if (!utf8_next(&cursor, &cp) || cp == '\n')
        {
            break;
        }

        glyph = wodle_font_find_glyph(style->font, cp);
        width += glyph_advance(style, glyph);
    }

    return width;
}
