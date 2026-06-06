/* WODLE-PORT: HalClock on the SF32 on-chip RTC (BSP_USING_ONCHIP_RTC — the
 * board template compiles the driver; LXT 32.768kHz is enabled by the board
 * init). The RTC keeps running through our hibernate (it's a PMU wake
 * source) and across warm reboots (RTC_BACKUP_INITIALIZED skips re-init);
 * only pulling the battery resets it.
 *
 * Convention mirrors upstream's X3/DS3231 path: the RTC stores UTC, display
 * applies the user's quarter-hour offset. There is no NTP (no WiFi) — time
 * is set manually via TimeSetActivity (the clock-sync slot in Status Bar
 * settings). Until the user sets it once, the stored year is implausible
 * and getTime/formatTime return false, so the status bar stays clockless —
 * but isAvailable() is true (the hardware exists), which is what reveals
 * the clock rows in settings. */
#pragma once

#include <Arduino.h>

class HalClock
{
public:
    void begin() {}
    /* The on-chip RTC is always present. */
    bool isAvailable() const { return true; }
    /* Current UTC hour/minute; false until the time has been set once. */
    bool getTime(uint8_t &hour, uint8_t &minute) const;
    /* Local time string per the biased quarter-hour offset; false until set. */
    bool formatTime(char *buf, size_t bufSize, uint8_t utcOffsetQuarterHoursBiased = 48,
                    bool use12Hour = false) const;
    bool syncFromNTP() { return false; } /* no WiFi */
    /* Write UTC h:m:s. Anchors the date to 2026-01-01 when implausible. */
    bool writeTimeToRTC(uint8_t hour, uint8_t minute, uint8_t second);
};

extern HalClock halClock;
