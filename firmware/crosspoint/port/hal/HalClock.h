/* WODLE-PORT: HalClock stub — no external RTC wired yet (SF32 has an on-chip
 * RTC; wiring it = future task). isAvailable()=false keeps the status bar
 * clockless, exactly like an X4 without NTP. */
#pragma once

#include <Arduino.h>

class HalClock
{
public:
    void begin() {}
    bool isAvailable() const { return false; }
    bool getTime(uint8_t &, uint8_t &) const { return false; }
    bool formatTime(char *, size_t, uint8_t = 48, bool = false) const { return false; }
    bool syncFromNTP() { return false; }
    bool writeTimeToRTC(uint8_t, uint8_t, uint8_t) { return false; }
};

extern HalClock halClock;
