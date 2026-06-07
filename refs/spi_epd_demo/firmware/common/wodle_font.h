#ifndef WODLE_FONT_H
#define WODLE_FONT_H

#include "rtthread.h"
#include <stdint.h>

typedef struct
{
    uint32_t codepoint;
    uint32_t offset;
    uint8_t width;
    uint8_t height;
    int8_t x_offset;
    int8_t y_offset;
    uint8_t advance;
} wodle_font_glyph_t;

typedef struct
{
    const char *name;
    uint8_t size_px;
    uint8_t line_height_px;
    uint16_t glyph_count;
    const wodle_font_glyph_t *glyphs;
    const uint8_t *bitmap;
    uint32_t bitmap_len;
} wodle_font_t;

const wodle_font_glyph_t *wodle_font_find_glyph(const wodle_font_t *font, uint32_t codepoint);
const wodle_font_glyph_t *wodle_font_fallback_glyph(const wodle_font_t *font);
uint8_t wodle_font_glyph_alpha(const wodle_font_t *font, const wodle_font_glyph_t *glyph, int x, int y);

#endif
