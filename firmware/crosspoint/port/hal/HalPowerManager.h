/* WODLE-PORT: HalPowerManager — battery via the BQ27220 fuel gauge
 * (WodleBattery, I2C2); falls back to 100% if the gauge doesn't respond.
 * CPU-frequency scaling is a no-op (SF32 LPM is a separate workstream). */
#pragma once

#include <Arduino.h>

#include "HalGPIO.h"
#include "WodleBattery.h"

class HalPowerManager;
extern HalPowerManager powerManager;

class HalPowerManager
{
public:
    static constexpr int LOW_POWER_FREQ = 10;
    static constexpr unsigned long IDLE_POWER_SAVING_MS = 3000;
    static constexpr unsigned long BATTERY_POLL_MS = 1500;

    void begin() { WodleBattery::init(); }
    void setPowerSaving(bool) {}
    void startDeepSleep(HalGPIO &g) const { g.startDeepSleep(); }
    uint16_t getBatteryPercentage() const
    {
        int p = WodleBattery::percent();
        return p < 0 ? 100 : (uint16_t)p;
    }

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
