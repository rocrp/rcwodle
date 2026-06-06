/* WODLE-PORT: AHT20 temperature/humidity sensor on the wodle sensor bus
 * (I2C2, PA31/PA32 — shared with the AW32001 charger and BQ27220 gauge).
 * Non-blocking: read() never sleeps. A measurement is triggered on demand and
 * collected on a later call once the 80ms conversion window has passed, so
 * the render path only ever pays two short I2C transactions. Values are
 * cached and refreshed at most every REFRESH_MS. */
#pragma once

namespace WodleAht20
{

/* Probe the sensor (I2C2 must already be pinmuxed by WodleBattery::init) and
 * kick off the first measurement. Call once at boot, after powerManager. */
void init();

/* True when the boot-time probe found a responding AHT20. */
bool available();

/* Latest cached reading. Returns false until the first measurement lands
 * (~80ms after init) or when the sensor is absent. Triggers a refresh in the
 * background when the cache has gone stale. */
bool read(float &temperatureC, float &humidityPct);

} // namespace WodleAht20
