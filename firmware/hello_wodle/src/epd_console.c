/* EPD line console: 2x-scaled 8x16 font on the 528x792 panel.
 *
 * Coordinate model (empirically calibrated, see docs):
 *   device x (0=left, <528)  -> controller gate row g = x
 *   device y (0=top,  <792)  -> controller source bit s = y   (flip here if
 *                               text ever renders upside-down)
 * Framebuffer bit 1 = white, 0 = black (matches 0x13 data polarity). */

#include <stdarg.h>
#include <string.h>
#include "rtthread.h"
#include "epd.h"
#include "epd_console.h"
#include "font_8x16.h"

#define SCALE     2
#define CELL_W    (FONT_W * SCALE)  /* 16 */
#define CELL_H    (FONT_H * SCALE)  /* 32 */
#define MARGIN_X  8
#define MARGIN_Y  12
#define CON_COLS  ((EPD_GATE - 2 * MARGIN_X) / CELL_W) /* 32  */
#define CON_LINES ((EPD_SRC - 2 * MARGIN_Y) / CELL_H)  /* 24  */

static uint8_t fb[EPD_FRAME_BYTES];
static char    con_lines[CON_LINES][CON_COLS + 1];
static int     con_used;

static void set_black(int x, int y)
{
    if ((unsigned)x >= EPD_GATE || (unsigned)y >= EPD_SRC)
        return;
    fb[x * EPD_ROW_BYTES + y / 8] &= ~(0x80 >> (y % 8));
}

static void draw_glyph(int x0, int y0, char ch)
{
    if (ch < FONT_FIRST || ch > FONT_LAST)
        ch = '?';
    const uint8_t *rows = font_8x16[ch - FONT_FIRST];
    for (int gy = 0; gy < FONT_H; gy++)
    {
        uint8_t row = rows[gy];
        for (int gx = 0; gx < FONT_W; gx++)
        {
            if (!(row & (0x80 >> gx)))
                continue;
            for (int dy = 0; dy < SCALE; dy++)
                for (int dx = 0; dx < SCALE; dx++)
                    set_black(x0 + gx * SCALE + dx, y0 + gy * SCALE + dy);
        }
    }
}

void epd_console_init(void)
{
    memset(fb, 0xFF, sizeof(fb));
    con_used = 0;
}

void epd_console_printf(const char *fmt, ...)
{
    char buf[CON_COLS + 1];
    va_list args;
    va_start(args, fmt);
    rt_vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (con_used == CON_LINES) /* scroll */
    {
        memmove(con_lines[0], con_lines[1], sizeof(con_lines) - sizeof(con_lines[0]));
        con_used--;
    }
    strcpy(con_lines[con_used++], buf);
}

void epd_console_flush(void)
{
    memset(fb, 0xFF, sizeof(fb));
    for (int l = 0; l < con_used; l++)
    {
        const char *s = con_lines[l];
        for (int c = 0; s[c]; c++)
            draw_glyph(MARGIN_X + c * CELL_W, MARGIN_Y + l * CELL_H, s[c]);
    }
    epd_render(fb);
}
