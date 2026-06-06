/* WODLE-PORT: see HalClock.h. */
#include "HalClock.h"

#include <rtthread.h>
#include <sys/time.h>
#include <time.h>

#include "ClockFormat.h"

extern "C" rt_err_t set_date(rt_uint32_t year, rt_uint32_t month, rt_uint32_t day);
extern "C" rt_err_t set_time(rt_uint32_t hour, rt_uint32_t minute, rt_uint32_t second);

namespace
{
/* A freshly-initialized RTC sits near the epoch; any set time lands in the
 * 2020s. 2025-01-01 UTC is a safe plausibility floor. */
constexpr long long PLAUSIBLE_EPOCH = 1735689600LL;

bool plausibleNow(time_t &out)
{
    out = time(nullptr);
    return (long long)out >= PLAUSIBLE_EPOCH;
}
} // namespace

bool HalClock::getTime(uint8_t &hour, uint8_t &minute) const
{
    time_t now;
    if (!plausibleNow(now)) return false;
    struct tm utc;
    gmtime_r(&now, &utc);
    hour = (uint8_t)utc.tm_hour;
    minute = (uint8_t)utc.tm_min;
    return true;
}

bool HalClock::formatTime(char *buf, size_t bufSize, uint8_t utcOffsetQuarterHoursBiased,
                          bool use12Hour) const
{
    time_t now;
    if (!plausibleNow(now)) return false;
    int h = 0, m = 0;
    ClockFormat::localHM((long long)now, utcOffsetQuarterHoursBiased, h, m);
    return ClockFormat::format(buf, bufSize, h, m, use12Hour);
}

bool HalClock::writeTimeToRTC(uint8_t hour, uint8_t minute, uint8_t second)
{
    time_t now;
    if (!plausibleNow(now))
    {
        /* Anchor the date; we only ever display hours/minutes. */
        if (set_date(2026, 1, 1) != RT_EOK)
        {
            rt_kprintf("[HalClock] set_date failed\n");
            return false;
        }
    }
    if (set_time(hour, minute, second) != RT_EOK)
    {
        rt_kprintf("[HalClock] set_time failed\n");
        return false;
    }
    rt_kprintf("[HalClock] RTC set to %02u:%02u:%02u UTC\n", hour, minute, second);
    return true;
}
