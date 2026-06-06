// Host tests for the pure logic behind the `wodle` MSH debug commands.
// The injection tracker must be hardware-faithful: the edges it emits are
// indistinguishable from physical key handling in HalGPIO::update().
#include "WodleDebugCmdCore.h"

#include <gtest/gtest.h>

namespace {

using namespace WodleDebugCmdCore;

// Mirrors HalGPIO.cpp's PWR_SHORT_MAX_MS.
constexpr uint16_t SHORT_MAX = 800;

TEST(ParseButtonName, KnownNames) {
  EXPECT_EQ(parseButtonName("up"), CORE_BTN_UP);
  EXPECT_EQ(parseButtonName("down"), CORE_BTN_DOWN);
  EXPECT_EQ(parseButtonName("left"), CORE_BTN_LEFT);
  EXPECT_EQ(parseButtonName("right"), CORE_BTN_RIGHT);
  EXPECT_EQ(parseButtonName("confirm"), CORE_BTN_CONFIRM);
  EXPECT_EQ(parseButtonName("back"), CORE_BTN_BACK);
  EXPECT_EQ(parseButtonName("power"), CORE_BTN_POWER);
}

TEST(ParseButtonName, RejectsUnknown) {
  EXPECT_EQ(parseButtonName(nullptr), BTN_INVALID);
  EXPECT_EQ(parseButtonName(""), BTN_INVALID);
  EXPECT_EQ(parseButtonName("UP"), BTN_INVALID);  // lowercase only, documented
  EXPECT_EQ(parseButtonName("sleep"), BTN_INVALID);
}

TEST(KeyQueue, FifoOrder) {
  KeyQueue q;
  EXPECT_TRUE(q.push({CORE_BTN_UP, 0}));
  EXPECT_TRUE(q.push({CORE_BTN_DOWN, 50}));
  KeyInject e;
  ASSERT_TRUE(q.pop(e));
  EXPECT_EQ(e.btn, CORE_BTN_UP);
  ASSERT_TRUE(q.pop(e));
  EXPECT_EQ(e.btn, CORE_BTN_DOWN);
  EXPECT_EQ(e.holdMs, 50);
  EXPECT_FALSE(q.pop(e));
}

TEST(KeyQueue, BoundedAndFailsLoudly) {
  KeyQueue q;
  for (int i = 0; i < KeyQueue::CAPACITY; i++) {
    EXPECT_TRUE(q.push({CORE_BTN_UP, 0})) << "push " << i;
  }
  EXPECT_FALSE(q.push({CORE_BTN_UP, 0}));  // full -> rejected, not silently dropped
  EXPECT_EQ(q.size(), KeyQueue::CAPACITY);

  // Drain and wrap around the ring a second time.
  KeyInject e;
  for (int i = 0; i < KeyQueue::CAPACITY; i++) ASSERT_TRUE(q.pop(e));
  EXPECT_TRUE(q.push({CORE_BTN_BACK, 7}));
  ASSERT_TRUE(q.pop(e));
  EXPECT_EQ(e.btn, CORE_BTN_BACK);
  EXPECT_EQ(e.holdMs, 7);
}

TEST(InjectionTracker, TapEmitsPressAndReleaseTogether) {
  InjectionTracker t;
  EXPECT_FALSE(t.active());
  t.begin({CORE_BTN_DOWN, 0}, 1000);
  ASSERT_TRUE(t.active());

  const auto e = t.tick(1000, SHORT_MAX, CORE_BTN_POWER);
  EXPECT_EQ(e.pressEdge, CORE_BTN_DOWN);
  EXPECT_EQ(e.releaseEdge, CORE_BTN_DOWN);
  EXPECT_FALSE(e.synthesizeConfirm);  // only power synthesizes CONFIRM
  EXPECT_EQ(e.heldBtn, BTN_INVALID);
  EXPECT_FALSE(t.active());  // one-shot

  // Subsequent ticks are inert.
  const auto e2 = t.tick(1010, SHORT_MAX, CORE_BTN_POWER);
  EXPECT_EQ(e2.pressEdge, BTN_INVALID);
  EXPECT_EQ(e2.releaseEdge, BTN_INVALID);
}

TEST(InjectionTracker, HoldSpansFrames) {
  InjectionTracker t;
  t.begin({CORE_BTN_UP, 100}, 0);

  auto e = t.tick(0, SHORT_MAX, CORE_BTN_POWER);
  EXPECT_EQ(e.pressEdge, CORE_BTN_UP);  // press edge on first frame only
  EXPECT_EQ(e.heldBtn, CORE_BTN_UP);
  EXPECT_EQ(e.releaseEdge, BTN_INVALID);

  e = t.tick(50, SHORT_MAX, CORE_BTN_POWER);
  EXPECT_EQ(e.pressEdge, BTN_INVALID);  // no repeated press edge
  EXPECT_EQ(e.heldBtn, CORE_BTN_UP);

  e = t.tick(100, SHORT_MAX, CORE_BTN_POWER);
  EXPECT_EQ(e.releaseEdge, CORE_BTN_UP);
  EXPECT_EQ(e.heldBtn, BTN_INVALID);
  EXPECT_FALSE(t.active());
}

TEST(InjectionTracker, ShortPowerPressSynthesizesConfirm) {
  // Mirrors the physical pin: PWR released before the short-press cutoff
  // emits a CONFIRM edge (that's how short-press-confirm works on hardware).
  InjectionTracker t;
  t.begin({CORE_BTN_POWER, 100}, 5000);
  auto e = t.tick(5000, SHORT_MAX, CORE_BTN_POWER);
  EXPECT_EQ(e.pressEdge, CORE_BTN_POWER);
  EXPECT_FALSE(e.synthesizeConfirm);

  e = t.tick(5100, SHORT_MAX, CORE_BTN_POWER);
  EXPECT_EQ(e.releaseEdge, CORE_BTN_POWER);
  EXPECT_TRUE(e.synthesizeConfirm);
}

TEST(InjectionTracker, LongPowerHoldDoesNotConfirm) {
  InjectionTracker t;
  t.begin({CORE_BTN_POWER, 1500}, 0);
  auto e = t.tick(0, SHORT_MAX, CORE_BTN_POWER);
  EXPECT_EQ(e.pressEdge, CORE_BTN_POWER);
  e = t.tick(800, SHORT_MAX, CORE_BTN_POWER);
  EXPECT_EQ(e.heldBtn, CORE_BTN_POWER);
  e = t.tick(1500, SHORT_MAX, CORE_BTN_POWER);
  EXPECT_EQ(e.releaseEdge, CORE_BTN_POWER);
  EXPECT_FALSE(e.synthesizeConfirm);  // >= cutoff: a sleep press, not a confirm
}

TEST(InjectionTracker, NonPowerHoldNeverConfirms) {
  InjectionTracker t;
  t.begin({CORE_BTN_BACK, 50}, 0);
  (void)t.tick(0, SHORT_MAX, CORE_BTN_POWER);
  const auto e = t.tick(60, SHORT_MAX, CORE_BTN_POWER);
  EXPECT_EQ(e.releaseEdge, CORE_BTN_BACK);
  EXPECT_FALSE(e.synthesizeConfirm);
}

TEST(FrameDump, Crc32KnownVectors) {
  // Standard CRC-32 (IEEE): the canonical "123456789" check value.
  const char* check = "123456789";
  EXPECT_EQ(crc32(reinterpret_cast<const uint8_t*>(check), 9), 0xCBF43926u);
  EXPECT_EQ(crc32(nullptr, 0), 0x00000000u);
  const uint8_t zero[4] = {0, 0, 0, 0};
  EXPECT_EQ(crc32(zero, 4), 0x2144DF1Cu);
}

TEST(FrameDump, Base64KnownVectors) {
  char out[80];
  // RFC 4648 test vectors.
  EXPECT_EQ(encodeBase64Line(reinterpret_cast<const uint8_t*>("Man"), 3, out), 4);
  EXPECT_STREQ(out, "TWFu");
  encodeBase64Line(reinterpret_cast<const uint8_t*>("Ma"), 2, out);
  EXPECT_STREQ(out, "TWE=");
  encodeBase64Line(reinterpret_cast<const uint8_t*>("M"), 1, out);
  EXPECT_STREQ(out, "TQ==");
  encodeBase64Line(reinterpret_cast<const uint8_t*>("foobar"), 6, out);
  EXPECT_STREQ(out, "Zm9vYmFy");
}

TEST(FrameDump, FullLineIs76Chars) {
  uint8_t buf[BASE64_LINE_BYTES];
  for (int i = 0; i < BASE64_LINE_BYTES; i++) buf[i] = static_cast<uint8_t>(i);
  char out[80];
  EXPECT_EQ(encodeBase64Line(buf, BASE64_LINE_BYTES, out), 76);  // 57 bytes -> 76 chars, no padding
  EXPECT_EQ(strchr(out, '='), nullptr);
}

TEST(InjectionTracker, StartMsExposedForHeldTimeBackdating) {
  InjectionTracker t;
  t.begin({CORE_BTN_POWER, 2000}, 12345);
  EXPECT_EQ(t.startMs(), 12345UL);
  EXPECT_EQ(t.activeButton(), CORE_BTN_POWER);
  (void)t.tick(12345, SHORT_MAX, CORE_BTN_POWER);
  EXPECT_EQ(t.activeButton(), CORE_BTN_POWER);  // still active mid-hold
}

}  // namespace
