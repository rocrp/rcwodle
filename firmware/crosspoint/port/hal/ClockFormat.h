/* WODLE-PORT: pure clock math for the SF32 on-chip RTC HalClock — no
 * RT-Thread deps so the host suite covers it. Convention mirrors upstream's
 * DS3231 path: the RTC keeps UTC; display applies the user's quarter-hour
 * UTC offset (biased by 48: 0 = UTC-12:00, 48 = UTC+0, 104 = UTC+14:00). */
#pragma once

#include <stddef.h>
#include <stdio.h>

namespace ClockFormat
{

constexpr int BIAS_Q = 48;

inline int offsetMinutes(unsigned char offsetQ)
{
    if (offsetQ > 104) offsetQ = BIAS_Q;
    return (static_cast<int>(offsetQ) - BIAS_Q) * 15;
}

/* UTC epoch seconds -> local hour/minute under the biased offset. */
inline void localHM(long long epochUtc, unsigned char offsetQ, int &hour, int &minute)
{
    long long mins = (epochUtc / 60 + offsetMinutes(offsetQ)) % 1440;
    if (mins < 0) mins += 1440;
    hour = static_cast<int>(mins / 60);
    minute = static_cast<int>(mins % 60);
}

/* Local hour/minute -> UTC hour/minute (for writing the RTC). */
inline void localToUtcHM(int hour, int minute, unsigned char offsetQ, int &utcHour, int &utcMinute)
{
    int mins = (hour * 60 + minute - offsetMinutes(offsetQ)) % 1440;
    if (mins < 0) mins += 1440;
    utcHour = mins / 60;
    utcMinute = mins % 60;
}

/* "HH:MM" (24h, needs >=6 bytes) or "H:MM AM"/"12:MM PM" (12h, needs >=9).
 * Matches upstream HalClock::formatTime output. */
inline bool format(char *buf, size_t bufSize, int hour, int minute, bool use12Hour)
{
    if (!buf || hour < 0 || hour > 23 || minute < 0 || minute > 59) return false;
    if (!use12Hour)
    {
        if (bufSize < 6) return false;
        snprintf(buf, bufSize, "%02d:%02d", hour, minute);
        return true;
    }
    if (bufSize < 9) return false;
    const bool pm = hour >= 12;
    int h12 = hour % 12;
    if (h12 == 0) h12 = 12;
    snprintf(buf, bufSize, "%d:%02d %s", h12, minute, pm ? "PM" : "AM");
    return true;
}

} // namespace ClockFormat
