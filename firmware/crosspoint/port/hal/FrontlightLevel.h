/* WODLE-PORT: pure frontlight-level stepping (host-testable).
 * Levels are percent 0..100 in fixed steps; swipe-up brightens. 0 = off. */
#pragma once

namespace FrontlightLevel
{
constexpr int STEP = 20;
constexpr int MIN = 0;
constexpr int MAX = 100;
constexpr int DEFAULT_ON = 60; /* first brighten from 0 lands here-ish */

inline int clamp(int level)
{
    if (level < MIN) return MIN;
    if (level > MAX) return MAX;
    /* snap to step grid so repeated swipes cycle stable values */
    return (level / STEP) * STEP;
}

inline int up(int level) { return clamp(clamp(level) + STEP); }
inline int down(int level) { return clamp(clamp(level) - STEP); }
} // namespace FrontlightLevel
