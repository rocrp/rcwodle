/* WODLE-PORT: CST836U touch (Hynitron CST8xx family, CST816-compatible
 * registers) on I2C1 (SCL=PA7 SDA=PA8), INT=PA42, addr 0x15 — wodle-specific;
 * upstream X4/X3 have no touch panel.
 *
 * Taps + swipes + stationary long-press, synthesized into logical buttons
 * by screen zone (portrait 528x792):
 *   top strip (y < 96)        -> BACK
 *   left third                -> UP   (previous page)
 *   right third               -> DOWN (next page)
 *   center                    -> CONFIRM
 * A stationary hold reports the zone's button as continuously pressed
 * (backdated to touch-down), which drives the reader's hold gestures:
 * hold-center = bookmark, hold-top = go home, hold-page-zone = chapter
 * skip. Orientation of the raw coordinates is a HIL checkpoint — adjust
 * the TOUCH_* flags in the .cpp if zones land wrong. */
#pragma once

#include <cstdint>

namespace WodleTouch
{
void init();
bool available();

/* One input frame's worth of synthesized touch state. */
struct Frame
{
    int tapButton = -1;            /* completed tap/swipe -> one-frame press+release */
    int holdButton = -1;           /* stationary hold in progress -> held this frame */
    bool holdReleased = false;     /* the hold ended this frame */
    unsigned long holdStartMs = 0; /* touch-down time of the active hold */
    /* WODLE-PORT: raw tap/hold coordinates alongside the synthesized button,
     * the foundation for direct tap-to-select. -1 = no coordinate this frame.
     * These are LOGICAL PORTRAIT coordinates (x in [0,SCREEN_W), y in
     * [0,SCREEN_H)) — already through readTouch's SWAP/MIRROR macros, same
     * space TapClassifier zones in. Non-portrait orientation mapping (the
     * reader can rotate) is a later concern for the consumer, not this seam. */
    int tapX = -1;  /* x of a completed tap (set with tapButton on a Tap) */
    int tapY = -1;  /* y of a completed tap */
    int holdX = -1; /* x of touch-down for an active stationary hold */
    int holdY = -1; /* y of touch-down for an active stationary hold */
};

/* Poll once per input frame. */
Frame poll();
} // namespace WodleTouch
