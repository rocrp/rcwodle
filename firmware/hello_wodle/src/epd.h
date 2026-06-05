#pragma once
#include <stdint.h>

/* UC8179C 3.68" e-paper, 528 gates x 792 sources.
 * Controller buffer is row-major over GATE lines; one gate row = 792 source
 * bits = 99 bytes. Gate axis = DEVICE HORIZONTAL (proven empirically: gate
 * row 0 renders at the LEFT edge of the hand-held portrait device). */
#define EPD_SRC          792
#define EPD_GATE         528
#define EPD_ROW_BYTES    (EPD_SRC / 8)              /* 99    */
#define EPD_FRAME_BYTES  (EPD_SRC * EPD_GATE / 8)   /* 52272 */

/* One-time bring-up: pinmux + GPIO + panel reset + init + GC LUT. */
void epd_hw_init(void);

/* Send a full frame (1 bit/px, 0 = black) and run a GC refresh (~3 s). */
void epd_render(const uint8_t *fb);
