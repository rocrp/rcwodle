#pragma once
#include <stdint.h>

#define EPD_DEV_WIDTH   528
#define EPD_DEV_HEIGHT  792
#define EPD_ROW_BYTES   (EPD_DEV_HEIGHT / 8)
#define EPD_FRAME_BYTES (EPD_DEV_WIDTH * EPD_DEV_HEIGHT / 8)

void epd_hw_init(void);
void epd_draw_pixel(uint8_t *fb, int x, int y, int black);
void epd_fill(uint8_t *fb, int black);
void epd_render(const uint8_t *fb);
void epd_render_grayscale4(const uint8_t *bp0, const uint8_t *bp1);
void epd_render_partial(const uint8_t *fb);
void epd_render_partial_start(const uint8_t *fb);
int  epd_render_partial_poll(void);
void epd_clear(void);
void epd_sleep(void);
int  epd_is_ready(void);