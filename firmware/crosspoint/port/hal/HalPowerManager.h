/* WODLE-PORT: HalPowerManager — battery via BQ27220 fuel gauge on I2C2 is a
 * planned upgrade (addresses known from RE: gauge 0x55, charger 0x49); until
 * then a fixed 100% with the upstream API. CPU-frequency scaling is a no-op
 * (SF32 LPM is a separate workstream). */
#pragma once

#include <Arduino.h>

#include "HalGPIO.h"

class HalPowerManager;
extern HalPowerManager powerManager;

class HalPowerManager
{
public:
    static constexpr int LOW_POWER_FREQ = 10;
    static constexpr unsigned long IDLE_POWER_SAVING_MS = 3000;
    static constexpr unsigned long BATTERY_POLL_MS = 1500;

    void begin() {}
    void setPowerSaving(bool) {}
    void startDeepSleep(HalGPIO &g) const { g.startDeepSleep(); }
    uint16_t getBatteryPercentage() const { return 100; } /* TODO(HIL): BQ27220 */

    class Lock
    {
    public:
        explicit Lock() = default;
        ~Lock() = default;
        Lock(const Lock &) = delete;
        Lock &operator=(const Lock &) = delete;
        Lock(Lock &&) = delete;
        Lock &operator=(Lock &&) = delete;
    };
};
