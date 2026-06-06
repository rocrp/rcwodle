/* WODLE-PORT: pure reading-statistics accumulator — no clock/storage deps so
 * the host suite covers it. Day-keyed ring of buckets + lifetime totals.
 *
 * Day keys are local-day indices (localEpochSeconds / 86400). With the RTC
 * date anchored to 2026-01-01 the calendar LABELS may be wrong, but day
 * BOUNDARIES are real — "today" and "last N days" stay meaningful. Day key
 * 0 = "clock never set"; those samples still count toward lifetime and live
 * in one catch-all bucket. */
#pragma once

#include <stdint.h>

namespace ReadingStats
{

constexpr int MAX_DAYS = 30;

struct DayBucket
{
    int32_t day = -1; /* local day index, -1 = empty slot */
    uint32_t seconds = 0;
    uint32_t pages = 0;
};

class Core
{
public:
    /* Record a reading sample: pages turned (>=0) and actively-read seconds
     * (0 when the interval was rejected as a pause). */
    void record(int32_t day, uint32_t pages, uint32_t seconds)
    {
        lifetimePages_ += pages;
        lifetimeSeconds_ += seconds;
        DayBucket &b = bucketFor(day);
        b.pages += pages;
        b.seconds += seconds;
    }

    uint64_t lifetimeSeconds() const { return lifetimeSeconds_; }
    uint64_t lifetimePages() const { return lifetimePages_; }

    /* Totals for one specific day (0s when absent). */
    void dayTotals(int32_t day, uint32_t &seconds, uint32_t &pages) const
    {
        seconds = 0;
        pages = 0;
        for (const DayBucket &b : buckets_)
        {
            if (b.day == day)
            {
                seconds = b.seconds;
                pages = b.pages;
                return;
            }
        }
    }

    /* Sum over days in (today-n, today] — i.e. the last n days incl. today. */
    void lastDaysTotals(int32_t today, int n, uint32_t &seconds, uint32_t &pages) const
    {
        seconds = 0;
        pages = 0;
        for (const DayBucket &b : buckets_)
        {
            if (b.day >= 0 && b.day <= today && b.day > today - n)
            {
                seconds += b.seconds;
                pages += b.pages;
            }
        }
    }

    /* Raw access for (de)serialization. */
    int bucketCount() const
    {
        int n = 0;
        for (const DayBucket &b : buckets_)
            if (b.day >= 0) n++;
        return n;
    }
    const DayBucket *buckets() const { return buckets_; }
    void restore(uint64_t lifetimeSeconds, uint64_t lifetimePages) /* then re-record buckets */
    {
        lifetimeSeconds_ = lifetimeSeconds;
        lifetimePages_ = lifetimePages;
    }
    void restoreBucket(int32_t day, uint32_t seconds, uint32_t pages)
    {
        DayBucket &b = bucketFor(day);
        b.seconds = seconds;
        b.pages = pages;
    }

private:
    /* Find or create the bucket for a day, evicting the oldest when full. */
    DayBucket &bucketFor(int32_t day)
    {
        DayBucket *empty = nullptr;
        DayBucket *oldest = &buckets_[0];
        for (DayBucket &b : buckets_)
        {
            if (b.day == day) return b;
            if (b.day < 0 && !empty) empty = &b;
            if (b.day < oldest->day) oldest = &b;
        }
        DayBucket &victim = empty ? *empty : *oldest;
        victim.day = day;
        victim.seconds = 0;
        victim.pages = 0;
        return victim;
    }

    DayBucket buckets_[MAX_DAYS];
    uint64_t lifetimeSeconds_ = 0;
    uint64_t lifetimePages_ = 0;
};

} // namespace ReadingStats
