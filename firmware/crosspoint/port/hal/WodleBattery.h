/* WODLE-PORT: BQ27220 fuel gauge on I2C2 (SCL=PA31 SDA=PA32, addr 0x55).
 * Register map from stock-firmware RE: StateOfCharge()=0x2C (%), Voltage()=
 * 0x08 (mV), Current()=0x0C (signed mA) — same codes upstream uses for the
 * X3's BQ27220 (lib/hal/HalGPIO.h). Graceful: probe failure -> unavailable,
 * callers fall back to 100%. */
#pragma once

#include <cstdint>

namespace WodleBattery
{
void init();                /* mux pads, find i2c2, probe the gauge */
bool available();
int percent();              /* 0..100, -1 if unavailable */
int millivolts();           /* -1 if unavailable */
} // namespace WodleBattery
