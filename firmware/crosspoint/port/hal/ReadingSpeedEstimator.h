/* WODLE-PORT: reading-speed estimator for the "time left in chapter" status
 * bar element. Pure logic, no RT-Thread deps — host-tested.
 *
 * Feed it the page number every time the status bar draws; it detects
 * single-page turns itself (|delta| == 1) and ignores jumps (chapter nav,
 * book switches, the settings preview), so callers need no reset wiring.
 * Intervals outside [MIN_TURN_MS, MAX_TURN_MS] are dropped: shorter is
 * flipping, longer means the reader walked away. The estimate is the mean
 * of the last WINDOW accepted intervals, available once MIN_SAMPLES landed. */
#pragma once

namespace ReadingSpeed
{

constexpr unsigned long MIN_TURN_MS = 2000;        /* faster = flipping, not reading */
constexpr unsigned long MAX_TURN_MS = 5 * 60000UL; /* slower = away from the book */
constexpr int WINDOW = 8;
constexpr int MIN_SAMPLES = 3;

class Estimator
{
public:
    /* What one observe() call concluded — consumed by the stats tracker.
     * turned: a single-page reading turn happened (jumps don't count);
     * intervalMs: the accepted reading interval, 0 when rejected as a
     * pause/flip. */
    struct Sample
    {
        bool turned = false;
        unsigned long intervalMs = 0;
    };

    /* Call with the currently displayed page whenever the reader paints. */
    Sample observe(int page, unsigned long nowMs)
    {
        Sample sample;
        if (!hasLast_)
        {
            hasLast_ = true;
            lastPage_ = page;
            lastChangeMs_ = nowMs;
            return sample;
        }
        if (page == lastPage_) return sample; /* repaint of the same page: keep the anchor */

        const int delta = page - lastPage_;
        if (delta == 1 || delta == -1)
        {
            sample.turned = true;
            const unsigned long interval = nowMs - lastChangeMs_;
            if (interval >= MIN_TURN_MS && interval <= MAX_TURN_MS)
            {
                record(interval);
                sample.intervalMs = interval;
            }
        }
        /* jumps (chapter nav / book switch / settings preview): no sample */
        lastPage_ = page;
        lastChangeMs_ = nowMs;
        return sample;
    }

    bool ready() const { return count_ >= MIN_SAMPLES; }

    unsigned long avgMsPerPage() const
    {
        if (count_ == 0) return 0;
        unsigned long long sum = 0;
        for (int i = 0; i < count_; i++) sum += samples_[i];
        return (unsigned long)(sum / count_);
    }

    /* Minutes to finish pagesLeft more pages, rounded up. -1 until ready. */
    int minutesLeft(int pagesLeft) const
    {
        if (!ready() || pagesLeft < 0) return -1;
        const unsigned long long totalMs = (unsigned long long)avgMsPerPage() * pagesLeft;
        return (int)((totalMs + 59999) / 60000);
    }

private:
    void record(unsigned long interval)
    {
        samples_[next_] = interval;
        next_ = (next_ + 1) % WINDOW;
        if (count_ < WINDOW) count_++;
    }

    unsigned long samples_[WINDOW] = {};
    int next_ = 0;
    int count_ = 0;
    bool hasLast_ = false;
    int lastPage_ = 0;
    unsigned long lastChangeMs_ = 0;
};

} // namespace ReadingSpeed
