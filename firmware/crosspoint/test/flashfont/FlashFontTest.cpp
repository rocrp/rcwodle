// Host-side tests for the flash-resident XIP font path (FlashFontParse), over the
// same real .cpfont fixture the SD-font suite uses. Proves the firmware's flash
// parser and the converter agree on the v4 layout, and that the WFFD directory
// round-trips — all without the device, renderer, or settings.

#include <EpdFont.h>
#include <FlashFontParse.h>
#include <HardwareSerial.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

HWCDC Serial;  // host Logging sink (mirrors HalStorageHost.cpp)

namespace {

// Codepoints present in TestCJK_14.cpfont (ascii + 、。书世你好界，), regular+bold.
constexpr uint32_t kCpNi = 0x4F60;    // 你
constexpr uint32_t kCpHao = 0x597D;   // 好
constexpr uint32_t kCpShi = 0x4E16;   // 世
constexpr uint32_t kCpJie = 0x754C;   // 界
constexpr uint32_t kCpShu = 0x4E66;   // 书
constexpr uint32_t kCpZhong = 0x4E2D; // 中 — deliberately NOT in the fixture

std::vector<uint8_t> readAll(const char* path) {
  std::ifstream in(path, std::ios::binary);
  EXPECT_TRUE(in.good()) << "cannot read " << path;
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void wr32(uint8_t* p, uint32_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
  p[2] = (v >> 16) & 0xFF;
  p[3] = (v >> 24) & 0xFF;
}

// Wrap a .cpfont blob in a one-entry WFFD directory, mirroring build_flash_fonts.py.
std::vector<uint8_t> wrapWffd(const std::vector<uint8_t>& cpfont, const char* name, uint8_t size, uint32_t& blobOff) {
  blobOff = 64;  // align_up(16 header + 40 entry, 32)
  std::vector<uint8_t> out(blobOff + cpfont.size(), 0);
  memcpy(out.data(), "WFFD", 4);
  wr32(out.data() + 4, 1);  // version
  wr32(out.data() + 8, 1);  // count
  uint8_t* e = out.data() + 16;
  strncpy(reinterpret_cast<char*>(e), name, 24);
  e[24] = size;
  e[25] = 0;  // style
  wr32(e + 28, blobOff);
  wr32(e + 32, static_cast<uint32_t>(cpfont.size()));
  memcpy(out.data() + blobOff, cpfont.data(), cpfont.size());
  return out;
}

class FlashFontTest : public ::testing::Test {
 protected:
  void SetUp() override {
    blob_ = readAll(FIXTURE_CPFONT);
    ASSERT_GT(blob_.size(), flashfont::kCpHeaderSize);
  }
  std::vector<uint8_t> blob_;
};

TEST_F(FlashFontTest, ParsesRegularStyleFromMultiStyleCpfont) {
  EpdFontData data;
  ASSERT_TRUE(flashfont::parseCpfontRegular(blob_.data(), blob_.size(), data));

  EXPECT_TRUE(data.is2Bit);
  EXPECT_GT(data.intervalCount, 0u);
  EXPECT_GT(data.advanceY, 0);
  EXPECT_EQ(data.groups, nullptr);
  EXPECT_EQ(data.glyphMissHandler, nullptr);

  // All section pointers must land inside the blob.
  const uint8_t* lo = blob_.data();
  const uint8_t* hi = blob_.data() + blob_.size();
  EXPECT_GE(reinterpret_cast<const uint8_t*>(data.intervals), lo);
  EXPECT_LT(reinterpret_cast<const uint8_t*>(data.glyph), hi);
  EXPECT_GE(data.bitmap, lo);
  EXPECT_LE(data.bitmap, hi);
}

TEST_F(FlashFontTest, RendererCanLookUpPresentGlyphsXip) {
  EpdFontData data;
  ASSERT_TRUE(flashfont::parseCpfontRegular(blob_.data(), blob_.size(), data));
  EpdFont font(&data);

  for (uint32_t cp : {static_cast<uint32_t>('H'), kCpNi, kCpHao, kCpShi, kCpJie, kCpShu}) {
    const EpdGlyph* g = font.getGlyph(cp);
    ASSERT_NE(g, nullptr) << "missing glyph U+" << std::hex << cp;
    EXPECT_GT(g->advanceX, 0) << "zero advance U+" << std::hex << cp;
    EXPECT_GT(g->width, 0) << "empty bitmap U+" << std::hex << cp;
  }

  // A CJK ideograph the fixture excludes falls back to the replacement glyph,
  // i.e. it does not resolve to a real 中 glyph.
  const EpdGlyph* miss = font.getGlyph(kCpZhong);
  const EpdGlyph* ni = font.getGlyph(kCpNi);
  EXPECT_NE(miss, ni);
}

TEST_F(FlashFontTest, WffdDirectoryRoundTrips) {
  uint32_t blobOff = 0;
  std::vector<uint8_t> region = wrapWffd(blob_, "TestCJK", 14, blobOff);

  uint32_t count = 0;
  ASSERT_TRUE(flashfont::readDirectory(region.data(), region.size(), count));
  EXPECT_EQ(count, 1u);

  flashfont::DirEntry e = flashfont::readEntry(region.data(), 0);
  EXPECT_STREQ(e.name, "TestCJK");
  EXPECT_EQ(e.pointSize, 14);
  EXPECT_EQ(e.style, 0);
  EXPECT_EQ(e.offset, blobOff);
  EXPECT_EQ(e.length, blob_.size());

  // The blob the directory points at parses as a valid font.
  EpdFontData data;
  EXPECT_TRUE(flashfont::parseCpfontRegular(region.data() + e.offset, e.length, data));
}

TEST_F(FlashFontTest, RejectsGarbageAndTruncation) {
  EpdFontData data;
  std::vector<uint8_t> garbage(64, 0xAB);
  EXPECT_FALSE(flashfont::parseCpfontRegular(garbage.data(), garbage.size(), data));

  // Valid magic but truncated mid-blob must be rejected, not parsed into OOB pointers.
  std::vector<uint8_t> truncated(blob_.begin(), blob_.begin() + flashfont::kCpHeaderSize + 16);
  EXPECT_FALSE(flashfont::parseCpfontRegular(truncated.data(), truncated.size(), data));

  uint32_t count = 0;
  EXPECT_FALSE(flashfont::readDirectory(garbage.data(), garbage.size(), count));
}

}  // namespace
