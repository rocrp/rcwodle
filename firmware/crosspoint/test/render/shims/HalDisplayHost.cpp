/* host shim impl — mirrors port/hal/HalDisplay.cpp's FrameBlit usage. */
#include <HalDisplay.h>

#include <cstring>

#include "FrameBlit.h"

static uint8_t s_frameBuffer[HalDisplay::BUFFER_SIZE];

void HalDisplay::begin(bool) { memset(s_frameBuffer, 0xFF, sizeof(s_frameBuffer)); }

void HalDisplay::clearScreen(uint8_t color) const { FrameBlit::fill(s_frameBuffer, color); }

uint8_t *HalDisplay::getFrameBuffer() const { return s_frameBuffer; }

void HalDisplay::drawImage(const uint8_t *imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool) const
{
    FrameBlit::blit(s_frameBuffer, imageData, x, y, w, h);
}

void HalDisplay::drawImageTransparent(const uint8_t *imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                      bool) const
{
    FrameBlit::blitTransparent(s_frameBuffer, imageData, x, y, w, h);
}
