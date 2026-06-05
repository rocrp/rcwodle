/* WODLE-PORT: frontlight control — wodle-specific (no upstream equivalent;
 * X4/X3 have no frontlight). PA1 -> GPTIM1_CH4 hardware PWM @100kHz, driven
 * via direct HAL (the rt_pwm framework's set() fails through the ROM-linked
 * RT layer). HIL-PROVEN 2026-06-06 — note the boost is VBAT-fed: no battery,
 * no light, even on USB power. */
#pragma once

#include <cstdint>

namespace WodleFrontlight
{
void init();                  /* prepare GPTIM1 ch4 (pad stays on GPTIM1_CH4 mux) */
void set(uint8_t percent);    /* 0 = off, 1..100 = duty */
void pulse(int ms);           /* blocking on/off pulse (boot proof-of-life) */
} // namespace WodleFrontlight
