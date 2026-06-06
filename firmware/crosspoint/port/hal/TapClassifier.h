/* WODLE-PORT: pure touch-gesture classification + zone mapping, extracted
 * from WodleTouch so the host test suite can exercise it (no RT-Thread deps).
 *
 * Gestures: tap (short, little movement) -> zone button; directional swipes
 * (dominant axis, enough travel) -> page turns (horizontal) and frontlight
 * (vertical); stationary long-press (isHold) -> the zone's button reported
 * as HELD while the finger stays down, feeding the reader's hold gestures
 * (hold-center = bookmark, hold-top = go home, hold-page-zone = chapter
 * skip). Anything else (diagonal wobble, slow drag) -> None. */
#pragma once

namespace TapClassifier
{
constexpr int SCREEN_W = 528;
constexpr int SCREEN_H = 792;
constexpr int TOP_STRIP_PX = 96;
constexpr unsigned long TAP_MAX_MS = 400;
constexpr int TAP_MAX_MOVE = 40;
constexpr unsigned long SWIPE_MAX_MS = 700;
constexpr int SWIPE_MIN_TRAVEL = 120;
constexpr int SWIPE_AXIS_RATIO = 2; /* dominant axis must be 2x the other */

/* logical button ids — mirror HalGPIO::BTN_* (kept numeric so this header
 * stays dependency-free; static_asserted at the WodleTouch.cpp seam) */
constexpr int BTN_BACK = 0;
constexpr int BTN_CONFIRM = 1;
constexpr int BTN_UP = 4;
constexpr int BTN_DOWN = 5;

enum class Gesture
{
    None,
    Tap,
    SwipeLeft,
    SwipeRight,
    SwipeUp,
    SwipeDown
};

inline bool isTap(unsigned long heldMs, int dx, int dy)
{
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return heldMs <= TAP_MAX_MS && dx <= TAP_MAX_MOVE && dy <= TAP_MAX_MOVE;
}

/* Stationary long-press: finger down past the tap window without leaving
 * the tap movement budget. Declared mid-touch (not on release); once a
 * touch becomes a hold it stays one until the finger lifts. */
inline bool isHold(unsigned long heldMs, int dx, int dy)
{
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return heldMs > TAP_MAX_MS && dx <= TAP_MAX_MOVE && dy <= TAP_MAX_MOVE;
}

inline Gesture classify(unsigned long heldMs, int dx, int dy)
{
    if (isTap(heldMs, dx, dy)) return Gesture::Tap;
    if (heldMs > SWIPE_MAX_MS) return Gesture::None; /* slow drag / long press */

    int ax = dx < 0 ? -dx : dx;
    int ay = dy < 0 ? -dy : dy;
    if (ax >= SWIPE_MIN_TRAVEL && ax >= ay * SWIPE_AXIS_RATIO)
        return dx < 0 ? Gesture::SwipeLeft : Gesture::SwipeRight;
    if (ay >= SWIPE_MIN_TRAVEL && ay >= ax * SWIPE_AXIS_RATIO)
        return dy < 0 ? Gesture::SwipeUp : Gesture::SwipeDown;
    return Gesture::None;
}

inline int zoneButton(int x, int y)
{
    if (y < TOP_STRIP_PX) return BTN_BACK;
    if (x < SCREEN_W / 3) return BTN_UP;
    if (x > 2 * SCREEN_W / 3) return BTN_DOWN;
    return BTN_CONFIRM;
}

/* Reading-flow convention: swipe left (like flipping a page away) = next
 * page = BTN_DOWN; swipe right = previous = BTN_UP. */
inline int swipeButton(Gesture g)
{
    if (g == Gesture::SwipeLeft) return BTN_DOWN;
    if (g == Gesture::SwipeRight) return BTN_UP;
    return -1;
}
} // namespace TapClassifier
