// Aht20Codec host tests — CRC8 + 20-bit raw decode math.
//
// CRC ground truth: the AHT20 uses CRC-8/NRSC-5 (poly 0x31, init 0xFF, no
// reflect, no final XOR) — the same variant as Sensirion SHT3x, whose
// datasheet publishes 0xBEEF -> 0x92 as the reference vector.

#include <Aht20Codec.h>
#include <gtest/gtest.h>

#include <cstdint>

namespace {

// Independent table-driven CRC implementation for cross-checking.
uint8_t referenceCrc8(const uint8_t* data, int len) {
  static uint8_t table[256];
  static bool built = false;
  if (!built) {
    for (int i = 0; i < 256; i++) {
      uint8_t crc = static_cast<uint8_t>(i);
      for (int bit = 0; bit < 8; bit++) crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31) : static_cast<uint8_t>(crc << 1);
      table[i] = crc;
    }
    built = true;
  }
  uint8_t crc = 0xFF;
  for (int i = 0; i < len; i++) crc = table[crc ^ data[i]];
  return crc;
}

// Build a measurement frame from raw 20-bit values, with a valid CRC.
void buildFrame(uint32_t rawHum, uint32_t rawTemp, uint8_t frame[Aht20Codec::FRAME_LEN],
                uint8_t status = Aht20Codec::STATUS_CALIBRATED) {
  frame[0] = status;
  frame[1] = static_cast<uint8_t>(rawHum >> 12);
  frame[2] = static_cast<uint8_t>(rawHum >> 4);
  frame[3] = static_cast<uint8_t>(((rawHum & 0x0F) << 4) | ((rawTemp >> 16) & 0x0F));
  frame[4] = static_cast<uint8_t>(rawTemp >> 8);
  frame[5] = static_cast<uint8_t>(rawTemp);
  frame[6] = referenceCrc8(frame, 6);
}

TEST(Aht20Crc, PublishedVector) {
  // Sensirion-documented vector for this CRC variant: 0xBEEF -> 0x92.
  const uint8_t data[2] = {0xBE, 0xEF};
  EXPECT_EQ(Aht20Codec::crc8(data, 2), 0x92);
}

TEST(Aht20Crc, EmptyIsInit) { EXPECT_EQ(Aht20Codec::crc8(nullptr, 0), 0xFF); }

TEST(Aht20Crc, MatchesTableImplementation) {
  // Cross-check the bitwise implementation against the table-driven one
  // over a deterministic byte spread.
  uint8_t buf[32];
  for (int i = 0; i < 32; i++) buf[i] = static_cast<uint8_t>(i * 37 + 11);
  for (int len = 1; len <= 32; len++) {
    EXPECT_EQ(Aht20Codec::crc8(buf, len), referenceCrc8(buf, len)) << "len=" << len;
  }
}

TEST(Aht20Decode, MidScale) {
  // raw 0x80000 = half scale: 50% RH, 200*0.5-50 = 50°C — both exact.
  uint8_t frame[Aht20Codec::FRAME_LEN];
  buildFrame(0x80000, 0x80000, frame);
  Aht20Codec::Reading r{};
  ASSERT_TRUE(Aht20Codec::decode(frame, r));
  EXPECT_FLOAT_EQ(r.humidityPct, 50.0f);
  EXPECT_FLOAT_EQ(r.temperatureC, 50.0f);
}

TEST(Aht20Decode, RoomConditions) {
  // raw temp 0x60000 = 0.375 scale -> exactly 25.0°C; raw hum 0x40000 -> 25%.
  uint8_t frame[Aht20Codec::FRAME_LEN];
  buildFrame(0x40000, 0x60000, frame);
  Aht20Codec::Reading r{};
  ASSERT_TRUE(Aht20Codec::decode(frame, r));
  EXPECT_FLOAT_EQ(r.humidityPct, 25.0f);
  EXPECT_FLOAT_EQ(r.temperatureC, 25.0f);
}

TEST(Aht20Decode, Extremes) {
  uint8_t frame[Aht20Codec::FRAME_LEN];
  buildFrame(0, 0, frame);
  Aht20Codec::Reading r{};
  ASSERT_TRUE(Aht20Codec::decode(frame, r));
  EXPECT_FLOAT_EQ(r.humidityPct, 0.0f);
  EXPECT_FLOAT_EQ(r.temperatureC, -50.0f);

  buildFrame(0xFFFFF, 0xFFFFF, frame);
  ASSERT_TRUE(Aht20Codec::decode(frame, r));
  EXPECT_NEAR(r.humidityPct, 100.0f, 0.001f);
  EXPECT_NEAR(r.temperatureC, 150.0f, 0.001f);
}

TEST(Aht20Decode, RejectsBusyFrame) {
  uint8_t frame[Aht20Codec::FRAME_LEN];
  buildFrame(0x80000, 0x80000, frame, Aht20Codec::STATUS_CALIBRATED | Aht20Codec::STATUS_BUSY);
  Aht20Codec::Reading r{};
  EXPECT_FALSE(Aht20Codec::decode(frame, r));
}

TEST(Aht20Decode, RejectsCorruptedFrame) {
  uint8_t frame[Aht20Codec::FRAME_LEN];
  buildFrame(0x80000, 0x80000, frame);
  frame[4] ^= 0x01;  // single bit flip -> CRC mismatch
  Aht20Codec::Reading r{};
  EXPECT_FALSE(Aht20Codec::decode(frame, r));
}

TEST(Aht20Convert, Fahrenheit) {
  EXPECT_FLOAT_EQ(Aht20Codec::toFahrenheit(0.0f), 32.0f);
  EXPECT_FLOAT_EQ(Aht20Codec::toFahrenheit(100.0f), 212.0f);
  EXPECT_FLOAT_EQ(Aht20Codec::toFahrenheit(25.0f), 77.0f);
  EXPECT_FLOAT_EQ(Aht20Codec::toFahrenheit(-40.0f), -40.0f);
}

}  // namespace
