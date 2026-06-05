// Host tests for TxtChapterScanner (WODLE-PORT addition): zh/en heading
// patterns, chunked feeding with line carry, offsets, title capping — plus
// the TxtChapters cache over a real GBK fixture routed through Txt's
// transcode path (the scan must see UTF-8 offsets, not GBK ones).

#include <Txt.h>
#include <TxtChapterScanner.h>
#include <TxtChapters.h>
#include <Utf8.h>
#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

bool heading(const char* s) { return TxtChapterScanner::isHeading(s, std::strlen(s)); }

TEST(IsHeading, ChineseChapterPatterns) {
  EXPECT_TRUE(heading("第一章"));
  EXPECT_TRUE(heading("第1章 初见"));
  EXPECT_TRUE(heading("第１２章　风云再起"));   // fullwidth digits + full-width space
  EXPECT_TRUE(heading("第一千零一回"));
  EXPECT_TRUE(heading("  第三节 试探"));        // leading spaces
  EXPECT_TRUE(heading("\t第二卷 江湖"));        // leading tab
  EXPECT_TRUE(heading("　第四部"));             // leading full-width space
  EXPECT_TRUE(heading("第五篇：归途"));
  EXPECT_TRUE(heading("第6集"));
  EXPECT_TRUE(heading("第两百话"));
}

TEST(IsHeading, SpecialHeadings) {
  EXPECT_TRUE(heading("楔子"));
  EXPECT_TRUE(heading("序章"));
  EXPECT_TRUE(heading("番外一"));
  EXPECT_TRUE(heading("番外 重逢"));
  EXPECT_TRUE(heading("尾声"));
  EXPECT_TRUE(heading("后记"));
  EXPECT_TRUE(heading("大结局"));
}

TEST(IsHeading, EnglishChapters) {
  EXPECT_TRUE(heading("Chapter 1"));
  EXPECT_TRUE(heading("Chapter 42: The Answer"));
  EXPECT_TRUE(heading("CHAPTER IV"));
  EXPECT_TRUE(heading("chapter 7"));
}

TEST(IsHeading, Negatives) {
  EXPECT_FALSE(heading(""));
  EXPECT_FALSE(heading("我刚读完第一章，写得不错。"));  // 第 not at line start
  EXPECT_FALSE(heading("第x章"));                       // not a numeral
  EXPECT_FALSE(heading("第章"));                        // no numeral at all
  EXPECT_FALSE(heading("楔子的故事是这样展开的，那一年冬天特别冷。"));  // 楔子 + prose
  EXPECT_FALSE(heading("Chapters are great"));
  EXPECT_FALSE(heading("Chapter one"));  // spelled-out not supported
  // Long body line that *starts* like a heading must be rejected by length.
  std::string longLine = "第一章 ";
  for (int i = 0; i < 40; i++) longLine += "废话";
  EXPECT_FALSE(TxtChapterScanner::isHeading(longLine.c_str(), longLine.size()));
}

TEST(Scanner, FindsChaptersWithOffsets) {
  const std::string text =
      "书名：测试\n"
      "\n"
      "第一章 开端\n"
      "正文第一段。\n"
      "第二章 发展\r\n"     // CRLF
      "更多正文。\n"
      "尾声\n"
      "完。\n";

  TxtChapterScanner scanner;
  scanner.feed(reinterpret_cast<const uint8_t*>(text.data()), text.size());
  scanner.finish();

  const auto& ch = scanner.chapters();
  ASSERT_EQ(ch.size(), 3u);
  EXPECT_EQ(ch[0].title, "第一章 开端");
  EXPECT_EQ(ch[1].title, "第二章 发展");
  EXPECT_EQ(ch[2].title, "尾声");
  // Offsets point at the heading line start; verify against the source.
  EXPECT_EQ(text.compare(ch[0].offset, ch[0].title.size(), ch[0].title), 0);
  EXPECT_EQ(text.compare(ch[1].offset, ch[1].title.size(), ch[1].title), 0);
  EXPECT_EQ(text.compare(ch[2].offset, ch[2].title.size(), ch[2].title), 0);
}

TEST(Scanner, ChunkedFeedMatchesSingleFeed) {
  std::string text;
  for (int i = 1; i <= 30; i++) {
    text += "第" + std::to_string(i) + "章 标题" + std::to_string(i) + "\n";
    text += "这是正文，毫无意义的填充内容，足够长以模拟真实段落。\n\n";
  }

  TxtChapterScanner whole;
  whole.feed(reinterpret_cast<const uint8_t*>(text.data()), text.size());
  whole.finish();

  for (const size_t chunk : {1u, 7u, 64u, 4096u}) {
    TxtChapterScanner chunked;
    for (size_t pos = 0; pos < text.size(); pos += chunk) {
      const size_t n = std::min(chunk, text.size() - pos);
      chunked.feed(reinterpret_cast<const uint8_t*>(text.data()) + pos, n);
    }
    chunked.finish();

    ASSERT_EQ(chunked.chapters().size(), whole.chapters().size()) << "chunk=" << chunk;
    for (size_t i = 0; i < whole.chapters().size(); i++) {
      EXPECT_EQ(chunked.chapters()[i].offset, whole.chapters()[i].offset) << "chunk=" << chunk;
      EXPECT_EQ(chunked.chapters()[i].title, whole.chapters()[i].title) << "chunk=" << chunk;
    }
  }
}

TEST(Scanner, UnterminatedFinalHeading) {
  const char* text = "正文。\n第一章 没有换行结尾";
  TxtChapterScanner scanner;
  scanner.feed(reinterpret_cast<const uint8_t*>(text), std::strlen(text));
  scanner.finish();
  ASSERT_EQ(scanner.chapters().size(), 1u);
  EXPECT_EQ(scanner.chapters()[0].title, "第一章 没有换行结尾");
}

TEST(Scanner, TitleCappedAtUtf8Boundary) {
  std::string line = "第一章 ";
  while (line.size() < TxtChapterScanner::MAX_TITLE_BYTES + 20) line += "长";
  line.resize(TxtChapterScanner::MAX_HEADING_BYTES);  // stay heading-eligible
  while (!line.empty() && (static_cast<unsigned char>(line.back()) & 0xC0) == 0x80) line.pop_back();
  const std::string text = line + "\n";

  TxtChapterScanner scanner;
  scanner.feed(reinterpret_cast<const uint8_t*>(text.data()), text.size());
  scanner.finish();
  ASSERT_EQ(scanner.chapters().size(), 1u);
  const std::string& title = scanner.chapters()[0].title;
  EXPECT_LE(title.size(), TxtChapterScanner::MAX_TITLE_BYTES);
  // The cap must not split a UTF-8 sequence: decoding the whole title yields
  // no replacement characters.
  const auto* p = reinterpret_cast<const unsigned char*>(title.c_str());
  while (*p) {
    EXPECT_NE(utf8NextCodepoint(&p), 0xFFFDu) << "title cap split a UTF-8 char";
  }
}

// GBK fixture → Txt transcodes to UTF-8 → chapter scan sees UTF-8 offsets.
// sample_gbk.txt repeats a 4-line block 40× and each block opens with
// 第一章　风雪山神庙.
TEST(TxtChaptersIntegration, ScansGbkBookAndCaches) {
  const std::string cacheBase = std::string(::testing::TempDir()) + "txtch_cache";
  std::filesystem::remove_all(cacheBase);
  std::filesystem::create_directories(cacheBase);

  Txt txt(std::string(FIXTURE_DIR) + "/sample_gbk.txt", cacheBase);
  ASSERT_TRUE(txt.load());

  const auto chapters = TxtChapters::loadOrScan(txt);
  ASSERT_EQ(chapters.size(), 40u);
  for (const auto& ch : chapters) {
    EXPECT_EQ(ch.title, "第一章　风雪山神庙");
  }
  // Offsets must be valid positions in the *transcoded* (UTF-8) content.
  std::vector<uint8_t> buf(chapters[5].title.size());
  ASSERT_TRUE(txt.readContent(buf.data(), chapters[5].offset, buf.size()));
  EXPECT_EQ(std::memcmp(buf.data(), chapters[5].title.data(), buf.size()), 0);

  // Second call loads the cache (same result).
  const auto again = TxtChapters::loadOrScan(txt);
  ASSERT_EQ(again.size(), chapters.size());
  EXPECT_EQ(again[39].offset, chapters[39].offset);
  EXPECT_TRUE(std::filesystem::exists(txt.getCachePath() + "/chapters.bin"));
}

}  // namespace
