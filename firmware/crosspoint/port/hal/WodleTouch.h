/* WODLE-PORT: CST836U touch (Hynitron CST8xx family, CST816-compatible
 * registers) on I2C1 (SCL=PA7 SDA=PA8), INT=PA42, addr 0x15 — wodle-specific;
 * upstream X4/X3 have no touch panel.
 *
 * v1 model: taps only, synthesized into logical buttons by screen zone
 * (portrait 528x792):
 *   top strip (y < 96)        -> BACK
 *   left third                -> UP   (previous page)
 *   right third               -> DOWN (next page)
 *   center                    -> CONFIRM
 * Orientation of the raw coordinates is a HIL checkpoint — adjust the
 * TOUCH_* flags below if zones land wrong. */
#pragma once

#include <cstdint>

namespace WodleTouch
{
void init();
bool available();

/* Poll once per input frame. Returns a HalGPIO::BTN_* index when a tap
 * completed since the last poll, or -1. */
int pollTapButton();
} // namespace WodleTouch
