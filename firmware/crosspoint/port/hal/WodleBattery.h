/* WODLE-PORT: battery subsystem on I2C2 (SCL=PA31 SDA=PA32) — two chips:
 *
 * BQ27220 fuel gauge @0x55. Register map from stock-firmware RE:
 * StateOfCharge()=0x2C (%), Voltage()=0x08 (mV) — same codes upstream uses
 * for the X3's BQ27220 (lib/hal/HalGPIO.h).
 *
 * AW32001 charger @0x49. Datasheet refs/datasheets/AW32001ECSR.pdf: chip-ID
 * reg 0x0A (=0x49), system-status reg 0x08 with PG_STAT bit1 (VIN power
 * good = USB present) + CHG_STAT bits4:3. Drives the battery-icon charging
 * bolt via HalGPIO::isUsbConnected().
 *
 * Graceful: probe failure -> unavailable, callers fall back to 100% / no
 * bolt. */
#pragma once

#include <cstdint>

namespace WodleBattery
{
void init();                /* mux pads, find i2c2, probe gauge + charger */
bool available();
int percent();              /* 0..100, -1 if unavailable */
int millivolts();           /* -1 if unavailable */

/* True when the AW32001 answered its chip-ID probe at init. */
bool chargerAvailable();
/* AW32001 PG_STAT, cached and refreshed at most once a second (the main
 * loop polls usbStateChanged() every tick). False when the charger probe
 * failed. */
bool usbPowered();
/* Edge detect over usbPowered(); true once per plug/unplug transition. */
bool usbStateChanged();
} // namespace WodleBattery
