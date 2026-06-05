/* WODLE-PORT: HalDisplay for the wodle UC8179C 792x528 panel.
 * API mirrors upstream lib/hal/HalDisplay.h (X3/X4 EInkDisplay wrapper) so
 * GfxRenderer + activities compile unchanged. Backend = bit-banged GPIO SPI
 * driver proven in hello_wodle (GC full refresh; FAST/HALF currently alias to
 * GC — X3-style fast LUTs are a planned upgrade). Grayscale paths are stubs:
 * supportsStripGrayscale()=false and GfxRenderer gates on it. */
#pragma once

#include <Arduino.h>

class HalDisplay
{
public:
    HalDisplay();
    ~HalDisplay();

    enum RefreshMode
    {
        FULL_REFRESH,
        HALF_REFRESH,
        FAST_REFRESH
    };

    void begin(bool seamless = false);

    /* Controller-space geometry: width = 792 sources, height = 528 gates.
     * Identical buffer layout to upstream X3 (99 B/row, row = gate line). */
    static constexpr uint16_t DISPLAY_WIDTH = 792;
    static constexpr uint16_t DISPLAY_HEIGHT = 528;
    static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
    static constexpr uint32_t BUFFER_SIZE = (uint32_t)DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

    void clearScreen(uint8_t color = 0xFF) const;
    void drawImage(const uint8_t *imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                   bool fromProgmem = false) const;
    void drawImageTransparent(const uint8_t *imageData, uint16_t x, uint16_t y, uint16_t w,
                              uint16_t h, bool fromProgmem = false) const;

    void displayBuffer(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false);
    void refreshDisplay(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false);

    void deepSleep();

    uint8_t *getFrameBuffer() const;

    void copyGrayscaleBuffers(const uint8_t *lsbBuffer, const uint8_t *msbBuffer);
    void copyGrayscaleLsbBuffers(const uint8_t *lsbBuffer);
    void copyGrayscaleMsbBuffers(const uint8_t *msbBuffer);
    void cleanupGrayscaleBuffers(const uint8_t *bwBuffer);
    void displayGrayBuffer(bool turnOffScreen = false);
    void writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t *rows, uint16_t yStart,
                                  uint16_t numRows);
    bool supportsStripGrayscale() const;

    uint16_t getDisplayWidth() const { return DISPLAY_WIDTH; }
    uint16_t getDisplayHeight() const { return DISPLAY_HEIGHT; }
    uint16_t getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
    uint32_t getBufferSize() const { return BUFFER_SIZE; }
};

extern HalDisplay display;
