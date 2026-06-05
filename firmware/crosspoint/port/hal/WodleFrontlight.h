/* WODLE-PORT: frontlight control — wodle-specific (no upstream equivalent;
 * X4/X3 have no frontlight). PA1 -> GPTIM1_CH4 hardware PWM @100kHz, driven
 * via direct HAL (the rt_pwm framework's set() fails through the ROM-linked
 * RT layer). HIL-PROVEN 2026-06-06 — note the boost is VBAT-fed: no battery,
 * no light, even on USB power.
 *
 * Brightness UX: vertical swipes on the touch panel step the level (see
 * WodleTouch); the level persists to /.crosspoint/frontlight. */
#pragma once

#include <cstdint>

namespace WodleFrontlight
{
void init();               /* prepare GPTIM1 ch4 (pad stays on GPTIM1_CH4 mux) */
void set(uint8_t percent); /* 0 = off, 1..100 = duty (no persistence) */
void pulse(int ms);        /* blocking on/off pulse (boot proof-of-life) */

int level();                    /* current percent */
void setPersisted(uint8_t pct); /* set + persist (Settings UI) */
void stepUp();                  /* +1 step, persists */
void stepDown();                /* -1 step, persists */
void restorePersisted();        /* call once Storage is ready */
} // namespace WodleFrontlight
