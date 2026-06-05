/* WODLE-PORT: pure 1bpp framebuffer blit helpers (host-testable), used by
 * HalDisplay. Buffer layout: row-major, MSB = leftmost pixel, bit 1 = white.
 * Image data uses the same convention (matches upstream EInkDisplay). */
#pragma once

#include <cstdint>
#include <cstring>

namespace FrameBlit
{
constexpr int FB_W = 792; /* sources (bits per row) */
constexpr int FB_H = 528; /* gate rows */
constexpr int FB_ROW_BYTES = FB_W / 8;

inline void fill(uint8_t *fb, uint8_t color)
{
    memset(fb, color, (size_t)FB_ROW_BYTES * FB_H);
}

inline bool getPixel(const uint8_t *fb, int x, int y)
{
    return fb[(size_t)y * FB_ROW_BYTES + x / 8] & (0x80 >> (x % 8));
}

inline void setPixel(uint8_t *fb, int x, int y, bool white)
{
    if ((unsigned)x >= FB_W || (unsigned)y >= FB_H) return;
    uint8_t mask = 0x80 >> (x % 8);
    size_t idx = (size_t)y * FB_ROW_BYTES + x / 8;
    if (white)
        fb[idx] |= mask;
    else
        fb[idx] &= ~mask;
}

/* Copy blit: image overwrites the destination rect (clipped to fb bounds).
 * Fast path when x is byte-aligned; bitwise otherwise. */
inline void blit(uint8_t *fb, const uint8_t *img, int x, int y, int w, int h)
{
    const int wBytes = (w + 7) / 8; /* image rows are byte-padded */
    for (int row = 0; row < h; row++)
    {
        int dy = y + row;
        if (dy < 0) continue;
        if (dy >= FB_H) break;
        if ((x % 8) == 0 && (w % 8) == 0 && x >= 0)
        {
            int n = wBytes;
            if (x / 8 + n > FB_ROW_BYTES) n = FB_ROW_BYTES - x / 8;
            if (n > 0)
                memcpy(&fb[(size_t)dy * FB_ROW_BYTES + x / 8], &img[(size_t)row * wBytes], (size_t)n);
        }
        else
        {
            for (int col = 0; col < w; col++)
            {
                int dx = x + col;
                if (dx < 0) continue;
                if (dx >= FB_W) break;
                bool white = img[(size_t)row * wBytes + col / 8] & (0x80 >> (col % 8));
                setPixel(fb, dx, dy, white);
            }
        }
    }
}

/* Transparent blit: only black image pixels land (white = transparent). */
inline void blitTransparent(uint8_t *fb, const uint8_t *img, int x, int y, int w, int h)
{
    const int wBytes = (w + 7) / 8; /* image rows are byte-padded */
    for (int row = 0; row < h; row++)
    {
        int dy = y + row;
        if (dy < 0) continue;
        if (dy >= FB_H) break;
        for (int col = 0; col < w; col++)
        {
            int dx = x + col;
            if (dx < 0) continue;
            if (dx >= FB_W) break;
            bool white = img[(size_t)row * wBytes + col / 8] & (0x80 >> (col % 8));
            if (!white) setPixel(fb, dx, dy, false);
        }
    }
}
} // namespace FrameBlit
