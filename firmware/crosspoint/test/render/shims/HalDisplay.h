/* host shim: HalDisplay over a plain in-memory framebuffer — same geometry
 * and FrameBlit-backed draw paths as the wodle port (port/hal/HalDisplay.h),
 * refresh/sleep are no-ops. Lets GfxRenderer + fonts render on host so UI
 * output can be asserted on and dumped to image files (blind-dev "screen"). */
#pragma once

#include <Arduino.h>

class HalDisplay
{
public:
    HalDisplay() = default;
    ~HalDisplay() = default;

    enum RefreshMode
    {
        FULL_REFRESH,
        HALF_REFRESH,
        FAST_REFRESH
    };

    void begin(bool seamless = false);

    static constexpr uint16_t DISPLAY_WIDTH = 792;
    static constexpr uint16_t DISPLAY_HEIGHT = 528;
    static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
    static constexpr uint32_t BUFFER_SIZE = (uint32_t)DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

    void clearScreen(uint8_t color = 0xFF) const;
    void drawImage(const uint8_t *imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                   bool fromProgmem = false) const;
    void drawImageTransparent(const uint8_t *imageData, uint16_t x, uint16_t y, uint16_t w,
                              uint16_t h, bool fromProgmem = false) const;

    void displayBuffer(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false) {}
    void refreshDisplay(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false) {}
    /* WODLE-PORT: partial refresh no-op; records the rect so tests can pin
     * the logical->physical window mapping in GfxRenderer::displayWindow. */
    struct WindowRect
    {
        int x = -1, y = -1, w = -1, h = -1;
    };
    mutable WindowRect lastWindow;
    bool refreshWindow(int x, int y, int w, int h)
    {
        lastWindow = {x, y, w, h};
        return true;
    }
    void deepSleep() {}

    uint8_t *getFrameBuffer() const;

    /* Grayscale paths unsupported, like the wodle port. */
    void copyGrayscaleBuffers(const uint8_t *, const uint8_t *) {}
    void copyGrayscaleLsbBuffers(const uint8_t *) {}
    void copyGrayscaleMsbBuffers(const uint8_t *) {}
    void cleanupGrayscaleBuffers(const uint8_t *) {}
    void displayGrayBuffer(bool = false) {}
    void writeGrayscalePlaneStrip(bool, const uint8_t *, uint16_t, uint16_t) {}
    bool supportsStripGrayscale() const { return false; }

    uint16_t getDisplayWidth() const { return DISPLAY_WIDTH; }
    uint16_t getDisplayHeight() const { return DISPLAY_HEIGHT; }
    uint16_t getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
    uint32_t getBufferSize() const { return BUFFER_SIZE; }
};
