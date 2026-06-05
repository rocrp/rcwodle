// Host tests for the Hyphenator — pins the line-breaking behavior the Chinese
// reading pipeline depends on. Chinese text has no spaces, so a whole clause
// reaches the layout as one "word" and must break between hanzi without
// inserted hyphens; line-start punctuation (行首禁则: a line must not begin
// with ，。！？etc.) is enforced by the WODLE-PORT kinsoku filter.

#include <Hyphenator.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

std::vector<Hyphenator::BreakInfo> breaksFor(const std::string& word, bool includeFallback = true) {
  return Hyphenator::breakOffsets(word, includeFallback);
}

// Codepoint starting at byteOffset of a UTF-8 string (enough for BMP).
uint32_t cpAt(const std::string& s, size_t off) {
  const auto b0 = static_cast<unsigned char>(s[off]);
  if (b0 < 0x80) return b0;
  if ((b0 & 0xE0) == 0xC0) return ((b0 & 0x1Fu) << 6) | (s[off + 1] & 0x3F);
  return ((b0 & 0x0Fu) << 12) | ((s[off + 1] & 0x3Fu) << 6) | (s[off + 2] & 0x3F);
}

TEST(Hyphenator, ChineseClauseBreaksBetweenHanziWithoutHyphen) {
  Hyphenator::setPreferredLanguage("zh");
  const std::string word = "今天天气很好我们去爬山";  // 11 hanzi, no spaces
  const auto breaks = breaksFor(word);
  ASSERT_FALSE(breaks.empty()) << "an unspaced Chinese clause must be breakable";
  for (const auto& b : breaks) {
    EXPECT_FALSE(b.requiresInsertedHyphen) << "CJK breaks must not render a hyphen (offset " << b.byteOffset << ")";
    EXPECT_EQ(b.byteOffset % 3, 0u) << "break must sit on a hanzi boundary";
  }
  // Breaks should be dense — at least one per few characters, or long lines
  // could only break at word edges.
  EXPECT_GE(breaks.size(), 6u);
}

TEST(Hyphenator, ChineseBreaksWithoutFallback) {
  // CJK boundaries are legal breaks on their own — no every-N fallback needed.
  Hyphenator::setPreferredLanguage("zh");
  const auto breaks = breaksFor("风雪山神庙", /*includeFallback=*/false);
  ASSERT_EQ(breaks.size(), 4u);  // between each of the 5 hanzi
  for (const auto& b : breaks) {
    EXPECT_FALSE(b.requiresInsertedHyphen);
  }
}

TEST(Hyphenator, EnglishStillHyphenates) {
  Hyphenator::setPreferredLanguage("en");
  const auto breaks = breaksFor("hyphenation", /*includeFallback=*/false);
  ASSERT_FALSE(breaks.empty());
  for (const auto& b : breaks) {
    EXPECT_TRUE(b.requiresInsertedHyphen);
  }
}

TEST(Hyphenator, KinsokuNoBreakBeforeClosingPunct) {
  Hyphenator::setPreferredLanguage("zh");
  const std::string word = "你好，世界。再见！都好？嗯、了；说：哦）了》此」呢』";
  const auto breaks = breaksFor(word);
  ASSERT_FALSE(breaks.empty());
  for (const auto& b : breaks) {
    const uint32_t next = cpAt(word, b.byteOffset);
    EXPECT_FALSE(next == 0xFF0C || next == 0x3002 || next == 0xFF01 || next == 0xFF1F || next == 0x3001 ||
                 next == 0xFF1B || next == 0xFF1A || next == 0xFF09 || next == 0x300B || next == 0x300D ||
                 next == 0x300F)
        << "line must not start with closing punctuation U+" << std::hex << next;
  }
}

TEST(Hyphenator, KinsokuNoBreakAfterOpeningPunct) {
  Hyphenator::setPreferredLanguage("zh");
  const std::string word = "他说（你好）和《书名》以及「引用」了";
  const auto breaks = breaksFor(word);
  ASSERT_FALSE(breaks.empty());
  for (const auto& b : breaks) {
    ASSERT_GE(b.byteOffset, 3u);
    const uint32_t prev = cpAt(word, b.byteOffset - 3);  // all cps here are 3-byte
    EXPECT_FALSE(prev == 0xFF08 || prev == 0x300A || prev == 0x300C)
        << "line must not end with opening punctuation U+" << std::hex << prev;
  }
}

TEST(Hyphenator, MixedLatinCjkBreaksAtCjkBoundaries) {
  Hyphenator::setPreferredLanguage("zh");
  const std::string word = "Wi-Fi信号不太好";
  const auto breaks = breaksFor(word);
  ASSERT_FALSE(breaks.empty());
  bool sawCjkBreak = false;
  for (const auto& b : breaks) {
    if (b.byteOffset >= 5) {  // inside the CJK tail
      sawCjkBreak = true;
      EXPECT_FALSE(b.requiresInsertedHyphen);
    }
  }
  EXPECT_TRUE(sawCjkBreak);
}

}  // namespace
