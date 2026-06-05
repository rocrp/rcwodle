/* WODLE-PORT: pure tap-classification + zone-mapping logic, extracted from
 * WodleTouch so the host test suite can exercise it (no RT-Thread deps). */
#pragma once

namespace TapClassifier
{
constexpr int SCREEN_W = 528;
constexpr int SCREEN_H = 792;
constexpr int TOP_STRIP_PX = 96;
constexpr unsigned long TAP_MAX_MS = 400;
constexpr int TAP_MAX_MOVE = 40;

/* logical button ids — mirror HalGPIO::BTN_* (kept numeric so this header
 * stays dependency-free; static_asserted at the WodleTouch.cpp seam) */
constexpr int BTN_BACK = 0;
constexpr int BTN_CONFIRM = 1;
constexpr int BTN_UP = 4;
constexpr int BTN_DOWN = 5;

inline bool isTap(unsigned long heldMs, int dx, int dy)
{
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return heldMs <= TAP_MAX_MS && dx <= TAP_MAX_MOVE && dy <= TAP_MAX_MOVE;
}

inline int zoneButton(int x, int y)
{
    if (y < TOP_STRIP_PX) return BTN_BACK;
    if (x < SCREEN_W / 3) return BTN_UP;
    if (x > 2 * SCREEN_W / 3) return BTN_DOWN;
    return BTN_CONFIRM;
}
} // namespace TapClassifier
