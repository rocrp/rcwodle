/* Host tests for the hand-written (blind) port shims: MD5, base64, String.
 * These are exactly the pieces with known-answer vectors — if they're wrong,
 * settings obfuscation and KOReader document ids silently corrupt. */

#include <gtest/gtest.h>

#include <vector>

#include "MD5Builder.h"
#include "WString.h"
#include "base64.h"
#include "mbedtls/base64.h"

/* ------------------------------------------------------------------- MD5 */
static std::string md5Hex(const char *input)
{
    MD5Builder md5;
    md5.begin();
    md5.add(input);
    md5.calculate();
    return md5.toString().c_str();
}

TEST(Md5, Rfc1321Vectors)
{
    EXPECT_EQ(md5Hex(""), "d41d8cd98f00b204e9800998ecf8427e");
    EXPECT_EQ(md5Hex("a"), "0cc175b9c0f1b6a831c399e269772661");
    EXPECT_EQ(md5Hex("abc"), "900150983cd24fb0d6963f7d28e17f72");
    EXPECT_EQ(md5Hex("message digest"), "f96b697d7cb7938d525a2f31aaf161d0");
    EXPECT_EQ(md5Hex("abcdefghijklmnopqrstuvwxyz"), "c3fcd3d76192e4007dfb496cca67e13b");
    EXPECT_EQ(md5Hex("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"),
              "d174ab98d277d9f5a5611c2c9f419d9f");
    EXPECT_EQ(md5Hex("12345678901234567890123456789012345678901234567890123456789012345678901234567890"),
              "57edf4a22be3c955ac49da2e2107b67a");
}

TEST(Md5, ChunkedAddMatchesOneShot)
{
    MD5Builder a;
    a.begin();
    a.add("hello ");
    a.add("world");
    a.calculate();

    MD5Builder b;
    b.begin();
    b.add("hello world");
    b.calculate();

    EXPECT_EQ(std::string(a.toString().c_str()), std::string(b.toString().c_str()));
    EXPECT_EQ(std::string(a.toString().c_str()), "5eb63bbbe01eeed093cb22bb8f5acdc3");
}

TEST(Md5, CrossesBlockBoundary)
{
    /* 55/56/64/65 bytes straddle the padding edge cases */
    for (size_t n : {55u, 56u, 63u, 64u, 65u, 119u, 120u, 128u})
    {
        std::string input(n, 'x');
        MD5Builder whole, split;
        whole.begin();
        whole.add((const uint8_t *)input.data(), input.size());
        whole.calculate();
        split.begin();
        split.add((const uint8_t *)input.data(), 1);
        split.add((const uint8_t *)input.data() + 1, input.size() - 1);
        split.calculate();
        EXPECT_EQ(std::string(whole.toString().c_str()), std::string(split.toString().c_str()))
            << "length " << n;
    }
}

/* ----------------------------------------------------------------- base64 */
TEST(Base64, EncodeVectors)
{
    /* RFC 4648 vectors */
    EXPECT_EQ(std::string(base64::encode(String("")).c_str()), "");
    EXPECT_EQ(std::string(base64::encode(String("f")).c_str()), "Zg==");
    EXPECT_EQ(std::string(base64::encode(String("fo")).c_str()), "Zm8=");
    EXPECT_EQ(std::string(base64::encode(String("foo")).c_str()), "Zm9v");
    EXPECT_EQ(std::string(base64::encode(String("foob")).c_str()), "Zm9vYg==");
    EXPECT_EQ(std::string(base64::encode(String("fooba")).c_str()), "Zm9vYmE=");
    EXPECT_EQ(std::string(base64::encode(String("foobar")).c_str()), "Zm9vYmFy");
}

TEST(Base64, MbedtlsDecodeVectors)
{
    auto decode = [](const char *in) {
        unsigned char out[64];
        size_t olen = 0;
        int rc = mbedtls_base64_decode(out, sizeof(out), &olen,
                                       (const unsigned char *)in, strlen(in));
        EXPECT_EQ(rc, 0);
        return std::string((char *)out, olen);
    };
    EXPECT_EQ(decode("Zg=="), "f");
    EXPECT_EQ(decode("Zm8="), "fo");
    EXPECT_EQ(decode("Zm9v"), "foo");
    EXPECT_EQ(decode("Zm9vYmFy"), "foobar");
}

TEST(Base64, MbedtlsSizeQueryContract)
{
    /* upstream calls with dst=nullptr first to learn the needed size */
    size_t olen = 0;
    int rc = mbedtls_base64_decode(nullptr, 0, &olen, (const unsigned char *)"Zm9vYmFy", 8);
    EXPECT_EQ(rc, MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL);
    EXPECT_EQ(olen, 6u);
}

TEST(Base64, MbedtlsRejectsInvalidChars)
{
    unsigned char out[16];
    size_t olen = 0;
    int rc = mbedtls_base64_decode(out, sizeof(out), &olen, (const unsigned char *)"Zm!v", 4);
    EXPECT_EQ(rc, MBEDTLS_ERR_BASE64_INVALID_CHARACTER);
}

TEST(Base64, RoundTripBinary)
{
    uint8_t blob[97];
    for (size_t i = 0; i < sizeof(blob); i++) blob[i] = (uint8_t)(i * 37 + 5);
    String enc = base64::encode(blob, sizeof(blob));

    unsigned char dec[128];
    size_t olen = 0;
    ASSERT_EQ(mbedtls_base64_decode(dec, sizeof(dec), &olen,
                                    (const unsigned char *)enc.c_str(), enc.length()),
              0);
    ASSERT_EQ(olen, sizeof(blob));
    EXPECT_EQ(memcmp(dec, blob, sizeof(blob)), 0);
}

/* ------------------------------------------------------------------ String */
TEST(WString, BasicsAndSearch)
{
    String s("Hello World");
    EXPECT_EQ(s.length(), 11u);
    EXPECT_TRUE(s.startsWith(String("Hello")));
    EXPECT_TRUE(s.endsWith(String("World")));
    EXPECT_EQ(s.indexOf('o'), 4);
    EXPECT_EQ(s.indexOf('o', 5), 7);
    EXPECT_EQ(s.lastIndexOf('o'), 7);
    EXPECT_EQ(s.indexOf(String("World")), 6);
    EXPECT_EQ(s.indexOf(String("nope")), -1);
}

TEST(WString, SubstringSliceSemantics)
{
    String s("abcdef");
    EXPECT_EQ(std::string(s.substring(2).c_str()), "cdef");
    EXPECT_EQ(std::string(s.substring(1, 4).c_str()), "bcd"); /* [begin, end) */
    EXPECT_EQ(std::string(s.substring(4, 2).c_str()), "");
    EXPECT_EQ(std::string(s.substring(99).c_str()), "");
}

TEST(WString, MutatingOps)
{
    String s("  Mixed Case  ");
    s.trim();
    EXPECT_EQ(std::string(s.c_str()), "Mixed Case");
    s.toLowerCase();
    EXPECT_EQ(std::string(s.c_str()), "mixed case");
    s.toUpperCase();
    EXPECT_EQ(std::string(s.c_str()), "MIXED CASE");
    s.replace(String("CASE"), String("BAG"));
    EXPECT_EQ(std::string(s.c_str()), "MIXED BAG");
    s.replace('M', 'F');
    EXPECT_EQ(std::string(s.c_str()), "FIXED BAG");
    s.remove(5);
    EXPECT_EQ(std::string(s.c_str()), "FIXED");
}

TEST(WString, NumericCtorsAndConversions)
{
    EXPECT_EQ(std::string(String(42).c_str()), "42");
    EXPECT_EQ(std::string(String(-7).c_str()), "-7");
    EXPECT_EQ(std::string(String(255, 16).c_str()), "ff");
    EXPECT_EQ(std::string(String(3.14159, 2).c_str()), "3.14");
    EXPECT_EQ(String("123").toInt(), 123);
    EXPECT_NEAR(String("2.5").toFloat(), 2.5f, 1e-6);
}

TEST(WString, ConcatAndCompare)
{
    String a("foo");
    a += "bar";
    a += String("baz");
    a += '!';
    EXPECT_EQ(std::string(a.c_str()), "foobarbaz!");
    EXPECT_TRUE(String("abc") == "abc");
    EXPECT_TRUE(String("abc") != "abd");
    EXPECT_TRUE(String("ABC").equalsIgnoreCase(String("abc")));
    String sum = String("a") + "b" + String("c");
    EXPECT_EQ(std::string(sum.c_str()), "abc");
}

TEST(WString, WriteAppendsForArduinoJson)
{
    String s;
    s.write((uint8_t)'h');
    s.write((const uint8_t *)"ey", 2);
    EXPECT_EQ(std::string(s.c_str()), "hey");
}

/* -------------------------------------------------------- ReadingStatsCore */
#include "../../port/hal/ReadingStatsCore.h"

TEST(ReadingStatsCore, AccumulatesAndQueries)
{
    ReadingStats::Core c;
    c.record(100, 1, 60);
    c.record(100, 1, 90);
    c.record(101, 2, 120);

    uint32_t sec, pages;
    c.dayTotals(100, sec, pages);
    EXPECT_EQ(sec, 150u);
    EXPECT_EQ(pages, 2u);
    c.dayTotals(101, sec, pages);
    EXPECT_EQ(sec, 120u);
    EXPECT_EQ(pages, 2u);
    c.dayTotals(999, sec, pages); /* absent day */
    EXPECT_EQ(sec, 0u);
    EXPECT_EQ(pages, 0u);

    EXPECT_EQ(c.lifetimeSeconds(), 270u);
    EXPECT_EQ(c.lifetimePages(), 4u);

    /* last-7-days window includes today, excludes older */
    c.record(94, 1, 30); /* exactly 7 days before 101 -> outside (101-7=94 excluded) */
    c.record(95, 1, 40); /* inside */
    uint32_t wSec, wPages;
    c.lastDaysTotals(101, 7, wSec, wPages);
    EXPECT_EQ(wSec, 150u + 120u + 40u);
    EXPECT_EQ(wPages, 5u);
}

TEST(ReadingStatsCore, EvictsOldestWhenFull)
{
    ReadingStats::Core c;
    for (int d = 1; d <= ReadingStats::MAX_DAYS + 5; d++) c.record(d, 1, 10);
    EXPECT_EQ(c.bucketCount(), ReadingStats::MAX_DAYS);
    uint32_t sec, pages;
    c.dayTotals(1, sec, pages); /* oldest five evicted */
    EXPECT_EQ(pages, 0u);
    c.dayTotals(ReadingStats::MAX_DAYS + 5, sec, pages);
    EXPECT_EQ(pages, 1u);
    /* lifetime survives eviction */
    EXPECT_EQ(c.lifetimePages(), (uint64_t)(ReadingStats::MAX_DAYS + 5));
}

TEST(ReadingStatsCore, RestoreRoundTrip)
{
    ReadingStats::Core a;
    a.record(10, 3, 300);
    a.record(11, 2, 200);

    ReadingStats::Core b;
    b.restore(a.lifetimeSeconds(), a.lifetimePages());
    for (int i = 0; i < ReadingStats::MAX_DAYS; i++)
    {
        const ReadingStats::DayBucket &bk = a.buckets()[i];
        if (bk.day >= 0) b.restoreBucket(bk.day, bk.seconds, bk.pages);
    }
    uint32_t sec, pages;
    b.dayTotals(10, sec, pages);
    EXPECT_EQ(sec, 300u);
    EXPECT_EQ(pages, 3u);
    EXPECT_EQ(b.lifetimeSeconds(), a.lifetimeSeconds());
}

/* ------------------------------------------------------------- ClockFormat */
#include "../../port/hal/ClockFormat.h"

TEST(ClockFormat, OffsetMinutes)
{
    EXPECT_EQ(ClockFormat::offsetMinutes(48), 0);    /* UTC+0 */
    EXPECT_EQ(ClockFormat::offsetMinutes(80), 480);  /* UTC+8 */
    EXPECT_EQ(ClockFormat::offsetMinutes(0), -720);  /* UTC-12 */
    EXPECT_EQ(ClockFormat::offsetMinutes(104), 840); /* UTC+14 */
    EXPECT_EQ(ClockFormat::offsetMinutes(71), 345);  /* Nepal +5:45 */
    EXPECT_EQ(ClockFormat::offsetMinutes(200), 0);   /* corrupt -> UTC+0 */
}

TEST(ClockFormat, LocalHMAndWrap)
{
    int h, m;
    /* 2025-01-01 00:00:00 UTC = epoch 1735689600 */
    ClockFormat::localHM(1735689600LL, 48, h, m);
    EXPECT_EQ(h, 0);
    EXPECT_EQ(m, 0);
    ClockFormat::localHM(1735689600LL, 80, h, m); /* UTC+8 -> 08:00 */
    EXPECT_EQ(h, 8);
    EXPECT_EQ(m, 0);
    ClockFormat::localHM(1735689600LL, 0, h, m); /* UTC-12 -> 12:00 previous day */
    EXPECT_EQ(h, 12);
    EXPECT_EQ(m, 0);
    /* 23:30 UTC at +1 wraps to 00:30 */
    ClockFormat::localHM(1735689600LL + 23 * 3600 + 30 * 60, 52, h, m);
    EXPECT_EQ(h, 0);
    EXPECT_EQ(m, 30);
}

TEST(ClockFormat, LocalToUtcRoundTrip)
{
    for (uint8_t q : {(uint8_t)0, (uint8_t)48, (uint8_t)71, (uint8_t)80, (uint8_t)104})
    {
        int uh, um, lh, lm;
        ClockFormat::localToUtcHM(8, 30, q, uh, um);
        ClockFormat::localHM((long long)uh * 3600 + (long long)um * 60, q, lh, lm);
        EXPECT_EQ(lh, 8) << "offsetQ=" << (int)q;
        EXPECT_EQ(lm, 30) << "offsetQ=" << (int)q;
    }
}

TEST(ClockFormat, Format24And12)
{
    char buf[9];
    ASSERT_TRUE(ClockFormat::format(buf, sizeof(buf), 9, 5, false));
    EXPECT_STREQ(buf, "09:05");
    ASSERT_TRUE(ClockFormat::format(buf, sizeof(buf), 0, 0, true));
    EXPECT_STREQ(buf, "12:00 AM"); /* midnight */
    ASSERT_TRUE(ClockFormat::format(buf, sizeof(buf), 12, 0, true));
    EXPECT_STREQ(buf, "12:00 PM"); /* noon */
    ASSERT_TRUE(ClockFormat::format(buf, sizeof(buf), 23, 59, true));
    EXPECT_STREQ(buf, "11:59 PM");
    EXPECT_FALSE(ClockFormat::format(buf, 5, 9, 5, false));  /* buffer too small */
    EXPECT_FALSE(ClockFormat::format(buf, sizeof(buf), 24, 0, false)); /* bad hour */
}

/* ------------------------------------------------------ ReadingSpeedEstimator */
#include "../../port/hal/ReadingSpeedEstimator.h"

TEST(ReadingSpeed, NotReadyUntilMinSamples)
{
    ReadingSpeed::Estimator e;
    unsigned long t = 1000;
    e.observe(1, t);
    EXPECT_FALSE(e.ready());
    EXPECT_EQ(e.minutesLeft(10), -1);
    for (int p = 2; p <= ReadingSpeed::MIN_SAMPLES; p++)
    {
        t += 30000;
        e.observe(p, t);
    }
    EXPECT_FALSE(e.ready()); /* MIN_SAMPLES turns need MIN_SAMPLES+1 pages */
    t += 30000;
    e.observe(ReadingSpeed::MIN_SAMPLES + 1, t);
    EXPECT_TRUE(e.ready());
}

TEST(ReadingSpeed, AverageAndMinutesLeft)
{
    ReadingSpeed::Estimator e;
    unsigned long t = 0;
    e.observe(1, t);
    for (int p = 2; p <= 5; p++)
    {
        t += 60000; /* exactly one minute per page */
        e.observe(p, t);
    }
    EXPECT_EQ(e.avgMsPerPage(), 60000UL);
    EXPECT_EQ(e.minutesLeft(10), 10);
    EXPECT_EQ(e.minutesLeft(0), 0);
    /* rounding up: 90s/page x 1 page = 2 min display */
    ReadingSpeed::Estimator e2;
    t = 0;
    e2.observe(1, t);
    for (int p = 2; p <= 5; p++)
    {
        t += 90000;
        e2.observe(p, t);
    }
    EXPECT_EQ(e2.minutesLeft(1), 2);
}

TEST(ReadingSpeed, RejectsOutliersAndJumps)
{
    ReadingSpeed::Estimator e;
    unsigned long t = 0;
    e.observe(1, t);
    t += ReadingSpeed::MIN_TURN_MS - 1; /* flipping: too fast */
    e.observe(2, t);
    t += ReadingSpeed::MAX_TURN_MS + 1; /* walked away: too slow */
    e.observe(3, t);
    t += 30000;
    e.observe(10, t); /* chapter jump: no sample */
    EXPECT_FALSE(e.ready());
    EXPECT_EQ(e.avgMsPerPage(), 0UL);

    /* repaints of the same page must not move the anchor */
    ReadingSpeed::Estimator e2;
    e2.observe(1, 0);
    e2.observe(1, 25000); /* status bar redraw (USB plug etc.) */
    e2.observe(2, 30000); /* interval = 30s from page-1 arrival, not 5s */
    e2.observe(3, 60000);
    e2.observe(4, 90000);
    EXPECT_TRUE(e2.ready());
    EXPECT_EQ(e2.avgMsPerPage(), 30000UL);
}

TEST(ReadingSpeed, BackwardTurnsCountAndWindowSlides)
{
    ReadingSpeed::Estimator e;
    unsigned long t = 0;
    e.observe(5, t);
    t += 30000;
    e.observe(4, t); /* re-reading backwards is still reading */
    t += 30000;
    e.observe(5, t);
    t += 30000;
    e.observe(6, t);
    EXPECT_TRUE(e.ready());
    EXPECT_EQ(e.avgMsPerPage(), 30000UL);

    /* window slides: WINDOW fast pages push out old slow ones */
    for (int i = 0; i < ReadingSpeed::WINDOW; i++)
    {
        t += 10000;
        e.observe(7 + i, t);
    }
    EXPECT_EQ(e.avgMsPerPage(), 10000UL);
}

/* ------------------------------------------------------------ TapClassifier */
#include "../../port/hal/TapClassifier.h"

TEST(TapClassifier, TapVsSwipeVsLongPress)
{
    using namespace TapClassifier;
    EXPECT_TRUE(isTap(100, 5, -5));
    EXPECT_TRUE(isTap(TAP_MAX_MS, TAP_MAX_MOVE, TAP_MAX_MOVE));
    EXPECT_FALSE(isTap(TAP_MAX_MS + 1, 0, 0));   /* long press */
    EXPECT_FALSE(isTap(100, TAP_MAX_MOVE + 1, 0)); /* horizontal swipe */
    EXPECT_FALSE(isTap(100, 0, -(TAP_MAX_MOVE + 1))); /* vertical swipe */
}

TEST(TapClassifier, StationaryHold)
{
    using namespace TapClassifier;
    /* hold starts exactly one tick past the tap window */
    EXPECT_FALSE(isHold(TAP_MAX_MS, 0, 0)); /* still a potential tap */
    EXPECT_TRUE(isHold(TAP_MAX_MS + 1, 0, 0));
    EXPECT_TRUE(isHold(2000, TAP_MAX_MOVE, -TAP_MAX_MOVE)); /* jitter within budget */
    /* moved out of the tap budget -> drag/swipe in progress, not a hold */
    EXPECT_FALSE(isHold(TAP_MAX_MS + 1, TAP_MAX_MOVE + 1, 0));
    EXPECT_FALSE(isHold(2000, 0, -(TAP_MAX_MOVE + 1)));
    /* a released stationary hold classifies as None either way — the
     * release-time suppression in WodleTouch has classifier backup */
    EXPECT_EQ(classify(TAP_MAX_MS + 50, 5, 5), Gesture::None);  /* < SWIPE_MAX_MS */
    EXPECT_EQ(classify(SWIPE_MAX_MS + 50, 5, 5), Gesture::None); /* > SWIPE_MAX_MS */
}

TEST(TapClassifier, ZoneMap)
{
    using namespace TapClassifier;
    /* top strip wins over horizontal thirds */
    EXPECT_EQ(zoneButton(10, 10), BTN_BACK);
    EXPECT_EQ(zoneButton(SCREEN_W - 10, TOP_STRIP_PX - 1), BTN_BACK);
    /* thirds below the strip */
    EXPECT_EQ(zoneButton(0, TOP_STRIP_PX), BTN_UP);
    EXPECT_EQ(zoneButton(SCREEN_W / 3 - 1, 400), BTN_UP);
    EXPECT_EQ(zoneButton(SCREEN_W / 3, 400), BTN_CONFIRM);
    EXPECT_EQ(zoneButton(2 * SCREEN_W / 3, 400), BTN_CONFIRM);
    EXPECT_EQ(zoneButton(2 * SCREEN_W / 3 + 1, 400), BTN_DOWN);
    EXPECT_EQ(zoneButton(SCREEN_W - 1, SCREEN_H - 1), BTN_DOWN);
}

TEST(TapClassifier, GestureClassification)
{
    using namespace TapClassifier;
    /* taps */
    EXPECT_EQ(classify(100, 5, -5), Gesture::Tap);
    /* clean swipes */
    EXPECT_EQ(classify(300, -200, 10), Gesture::SwipeLeft);
    EXPECT_EQ(classify(300, 200, -30), Gesture::SwipeRight);
    EXPECT_EQ(classify(300, 10, -200), Gesture::SwipeUp);
    EXPECT_EQ(classify(300, -30, 200), Gesture::SwipeDown);
    /* boundary: exactly minimum travel, dominant enough */
    EXPECT_EQ(classify(300, -SWIPE_MIN_TRAVEL, SWIPE_MIN_TRAVEL / SWIPE_AXIS_RATIO),
              Gesture::SwipeLeft);
    /* too slow -> none */
    EXPECT_EQ(classify(SWIPE_MAX_MS + 1, -200, 0), Gesture::None);
    /* diagonal (no dominant axis) -> none */
    EXPECT_EQ(classify(300, 150, 140), Gesture::None);
    /* movement too small for swipe, too big for tap -> none */
    EXPECT_EQ(classify(300, 80, 0), Gesture::None);
}

TEST(TapClassifier, SwipeButtonsFollowReadingFlow)
{
    using namespace TapClassifier;
    EXPECT_EQ(swipeButton(Gesture::SwipeLeft), BTN_DOWN); /* flip forward */
    EXPECT_EQ(swipeButton(Gesture::SwipeRight), BTN_UP);  /* flip back */
    EXPECT_EQ(swipeButton(Gesture::SwipeUp), -1);
    EXPECT_EQ(swipeButton(Gesture::Tap), -1);
}

/* --------------------------------------------------------- FrontlightLevel */
#include "../../port/hal/FrontlightLevel.h"

TEST(FrontlightLevel, StepUpDownClampAndGrid)
{
    using namespace FrontlightLevel;
    EXPECT_EQ(up(0), STEP);
    EXPECT_EQ(up(80), 100);
    EXPECT_EQ(up(100), 100);    /* ceiling */
    EXPECT_EQ(down(100), 80);
    EXPECT_EQ(down(STEP), 0);
    EXPECT_EQ(down(0), 0);      /* floor */
    /* off-grid values snap to the step grid */
    EXPECT_EQ(clamp(47), 40);
    EXPECT_EQ(clamp(-5), 0);
    EXPECT_EQ(clamp(150), 100);
    EXPECT_EQ(up(47), 60);
    EXPECT_EQ(down(47), 20);
}

/* --------------------------------------------------------------- FrameBlit */
#include "../../port/hal/FrameBlit.h"

namespace
{
struct Fb
{
    std::vector<uint8_t> buf;
    Fb() : buf((size_t)FrameBlit::FB_ROW_BYTES * FrameBlit::FB_H, 0xFF) {}
    uint8_t *data() { return buf.data(); }
    bool px(int x, int y) { return FrameBlit::getPixel(buf.data(), x, y); }
};
} // namespace

TEST(FrameBlit, AlignedBlitCopiesExactRect)
{
    Fb fb;
    /* 16x2 image: all black */
    uint8_t img[4] = {0x00, 0x00, 0x00, 0x00};
    FrameBlit::blit(fb.data(), img, 8, 10, 16, 2);
    EXPECT_FALSE(fb.px(8, 10));
    EXPECT_FALSE(fb.px(23, 11));
    EXPECT_TRUE(fb.px(7, 10));  /* left neighbor untouched */
    EXPECT_TRUE(fb.px(24, 10)); /* right neighbor untouched */
    EXPECT_TRUE(fb.px(8, 9));   /* above untouched */
    EXPECT_TRUE(fb.px(8, 12));  /* below untouched */
}

TEST(FrameBlit, UnalignedBlitMatchesAligned)
{
    /* checker pattern 16x4 */
    uint8_t img[8];
    for (int i = 0; i < 8; i++) img[i] = (i % 2) ? 0xAA : 0x55;

    Fb a, b;
    FrameBlit::blit(a.data(), img, 8, 0, 16, 4);  /* aligned   */
    FrameBlit::blit(b.data(), img, 11, 0, 16, 4); /* unaligned (+3) */
    for (int y = 0; y < 4; y++)
        for (int col = 0; col < 16; col++)
            EXPECT_EQ(a.px(8 + col, y), b.px(11 + col, y)) << "col " << col << " y " << y;
}

TEST(FrameBlit, OddWidthUsesBytePaddedStride)
{
    /* 9px-wide image, 2 rows: row0 all black (9 bits), row1 all white.
     * With a (w+7)/8=2-byte stride, row1 starts at img[2]. */
    uint8_t img[4] = {0x00, 0x00, 0xFF, 0xFF};
    Fb fb;
    FrameBlit::blit(fb.data(), img, 0, 0, 9, 2);
    EXPECT_FALSE(fb.px(0, 0));
    EXPECT_FALSE(fb.px(8, 0));
    EXPECT_TRUE(fb.px(9, 0)); /* beyond width: untouched */
    EXPECT_TRUE(fb.px(0, 1)); /* row 1 white */
}

TEST(FrameBlit, ClipsAtEdges)
{
    Fb fb;
    uint8_t img[2] = {0x00, 0x00}; /* 16x1 black */
    /* off right edge */
    FrameBlit::blit(fb.data(), img, FrameBlit::FB_W - 8, 0, 16, 1);
    EXPECT_FALSE(fb.px(FrameBlit::FB_W - 1, 0));
    /* off bottom edge: must not crash or write row 0 */
    FrameBlit::blit(fb.data(), img, 0, FrameBlit::FB_H - 1, 16, 4);
    EXPECT_FALSE(fb.px(0, FrameBlit::FB_H - 1));
    EXPECT_TRUE(fb.px(20, 0));
}

TEST(FrameBlit, TransparentOnlyDarkens)
{
    Fb fb;
    /* paint a black region first */
    uint8_t black[2] = {0x00, 0x00};
    FrameBlit::blit(fb.data(), black, 0, 0, 16, 1);
    /* transparent-blit an all-white image over it: nothing changes */
    uint8_t white[2] = {0xFF, 0xFF};
    FrameBlit::blitTransparent(fb.data(), white, 0, 0, 16, 1);
    EXPECT_FALSE(fb.px(0, 0));
    /* black pixels in the image DO land on white background */
    FrameBlit::blitTransparent(fb.data(), black, 32, 0, 16, 1);
    EXPECT_FALSE(fb.px(32, 0));
}
