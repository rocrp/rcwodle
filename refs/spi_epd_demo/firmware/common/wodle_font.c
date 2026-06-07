#include "wodle_font.h"

const wodle_font_glyph_t *wodle_font_fallback_glyph(const wodle_font_t *font)
{
    if (!font || !font->glyphs || font->glyph_count == 0)
    {
        return RT_NULL;
    }

    return &font->glyphs[0];
}

const wodle_font_glyph_t *wodle_font_find_glyph(const wodle_font_t *font, uint32_t codepoint)
{
    if (!font || !font->glyphs)
    {
        return RT_NULL;
    }

    for (uint16_t i = 0; i < font->glyph_count; i++)
    {
        if (font->glyphs[i].codepoint == codepoint)
        {
            return &font->glyphs[i];
        }
    }

    return wodle_font_fallback_glyph(font);
}

uint8_t wodle_font_glyph_alpha(const wodle_font_t *font, const wodle_font_glyph_t *glyph, int x, int y)
{
    uint32_t pixel;
    uint8_t packed;
    int shift;

    if (!font || !glyph || !font->bitmap ||
        x < 0 || x >= glyph->width ||
        y < 0 || y >= glyph->height)
    {
        return 0;
    }

    pixel = (uint32_t)y * glyph->width + (uint32_t)x;
    if (glyph->offset + pixel / 4 >= font->bitmap_len)
    {
        return 0;
    }

    packed = font->bitmap[glyph->offset + pixel / 4];
    shift = 6 - (int)((pixel & 3) * 2);
    return (packed >> shift) & 0x03;
}
