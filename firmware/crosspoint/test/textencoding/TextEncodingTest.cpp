// Host tests for TextEncoding (WODLE-PORT addition): encoding detection +
// GBK/UTF-16/BOM → UTF-8 transcoding. Fixtures under test/fixtures/sample_*
// were generated with Python's codecs (ground truth, see commit); the GBK and
// UTF-16 fixtures must transcode byte-identically to sample_utf8.txt.
// Also covers the Txt::load() integration (read path swap + cache reuse).

#include <TextEncoding.h>
#include <Txt.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> readAll(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  EXPECT_TRUE(in.good()) << "cannot read " << path;
  std::vector<char> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  return {bytes.begin(), bytes.end()};
}

std::string fixture(const char* name) { return std::string(FIXTURE_DIR) + "/" + name; }

TextFileEncoding detectFile(const char* name) {
  const auto bytes = readAll(fixture(name));
  // Sniff window mirrors Txt::ensureUtf8ReadPath().
  return TextEncoding::detect(bytes.data(), std::min<size_t>(bytes.size(), 4096));
}

TEST(TextEncodingDetect, RecognizesAllFixtureEncodings) {
  EXPECT_EQ(detectFile("sample_utf8.txt"), TextFileEncoding::Utf8);
  EXPECT_EQ(detectFile("sample_utf8_bom.txt"), TextFileEncoding::Utf8Bom);
  EXPECT_EQ(detectFile("sample_gbk.txt"), TextFileEncoding::Gbk);
  EXPECT_EQ(detectFile("sample_utf16le.txt"), TextFileEncoding::Utf16Le);
  EXPECT_EQ(detectFile("sample_utf16be.txt"), TextFileEncoding::Utf16Be);
}

TEST(TextEncodingDetect, AsciiIsUtf8) {
  const char* ascii = "plain ascii text\nwith lines\n";
  EXPECT_EQ(TextEncoding::detect(reinterpret_cast<const uint8_t*>(ascii), strlen(ascii)),
            TextFileEncoding::Utf8);
}

TEST(TextEncodingDetect, TruncatedUtf8SequenceTolerated) {
  // 你 = E4 BD A0; cut after two bytes — still UTF-8 (sniff window artifact).
  const uint8_t buf[] = {'a', 0xE4, 0xBD};
  EXPECT_EQ(TextEncoding::detect(buf, sizeof(buf)), TextFileEncoding::Utf8);
}

TEST(TextEncodingDetect, Latin1IsNotGbk) {
  // 0xE9 ' ' — valid GBK lead but 0x20 is not a GBK trail → Unknown, not Gbk.
  const uint8_t buf[] = {'c', 'a', 'f', 0xE9, ' ', 'b', 'a', 'r'};
  EXPECT_EQ(TextEncoding::detect(buf, sizeof(buf)), TextFileEncoding::Unknown);
}

TEST(TextEncodingGbk, MapsCommonHanzi) {
  // Spot values cross-checked against Python: '你'.encode('gbk') = C4 E3 etc.
  EXPECT_EQ(TextEncoding::gbkToUnicode(0xC4, 0xE3), 0x4F60u);  // 你
  EXPECT_EQ(TextEncoding::gbkToUnicode(0xBA, 0xC3), 0x597Du);  // 好
  EXPECT_EQ(TextEncoding::gbkToUnicode(0xA1, 0xA3), 0x3002u);  // 。
  EXPECT_EQ(TextEncoding::gbkToUnicode(0x20, 0x20), 0xFFFDu);  // not a pair
}

class TranscodeFixture : public ::testing::Test {
 protected:
  void SetUp() override { utf8_ = readAll(fixture("sample_utf8.txt")); }

  void expectTranscodesToUtf8(const char* name, const TextFileEncoding enc) {
    const std::string dst = std::string(::testing::TempDir()) + "transcoded_" + name;
    ASSERT_TRUE(TextEncoding::transcodeToUtf8(fixture(name).c_str(), dst.c_str(), enc));
    EXPECT_EQ(readAll(dst), utf8_) << name << " did not round-trip to UTF-8";
  }

  std::vector<uint8_t> utf8_;
};

TEST_F(TranscodeFixture, GbkRoundTrips) { expectTranscodesToUtf8("sample_gbk.txt", TextFileEncoding::Gbk); }

TEST_F(TranscodeFixture, Utf8BomStripped) {
  expectTranscodesToUtf8("sample_utf8_bom.txt", TextFileEncoding::Utf8Bom);
}

TEST_F(TranscodeFixture, Utf16LeRoundTrips) {
  expectTranscodesToUtf8("sample_utf16le.txt", TextFileEncoding::Utf16Le);
}

TEST_F(TranscodeFixture, Utf16BeRoundTrips) {
  expectTranscodesToUtf8("sample_utf16be.txt", TextFileEncoding::Utf16Be);
}

// Txt::load() integration: GBK source → readContent serves UTF-8 bytes,
// getFileSize() reports the transcoded size, getPath() stays the original.
TEST(TxtIntegration, GbkSourceServesUtf8) {
  const std::string cacheBase = std::string(::testing::TempDir()) + "txt_cache";
  std::filesystem::remove_all(cacheBase);
  std::filesystem::create_directories(cacheBase);

  const std::string src = fixture("sample_gbk.txt");
  const auto utf8 = readAll(fixture("sample_utf8.txt"));

  Txt txt(src, cacheBase);
  ASSERT_TRUE(txt.load());
  EXPECT_EQ(txt.getPath(), src);
  EXPECT_EQ(txt.getFileSize(), utf8.size());

  std::vector<uint8_t> buf(utf8.size());
  ASSERT_TRUE(txt.readContent(buf.data(), 0, buf.size()));
  EXPECT_EQ(buf, utf8);

  // Second load must reuse the cached transcode (meta sidecar present).
  const std::string utf8Cache = txt.getCachePath() + "/utf8.txt";
  EXPECT_TRUE(std::filesystem::exists(utf8Cache));
  const auto mtime = std::filesystem::last_write_time(utf8Cache);
  Txt again(src, cacheBase);
  ASSERT_TRUE(again.load());
  EXPECT_EQ(std::filesystem::last_write_time(utf8Cache), mtime) << "cache not reused";
}

TEST(TxtIntegration, Utf8SourceReadDirectly) {
  const std::string cacheBase = std::string(::testing::TempDir()) + "txt_cache_utf8";
  std::filesystem::remove_all(cacheBase);
  std::filesystem::create_directories(cacheBase);

  const std::string src = fixture("sample_utf8.txt");
  Txt txt(src, cacheBase);
  ASSERT_TRUE(txt.load());
  EXPECT_FALSE(std::filesystem::exists(txt.getCachePath() + "/utf8.txt"))
      << "UTF-8 source must not be transcoded";
}

}  // namespace
