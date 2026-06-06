// TextSanitizer host tests (ported from rocrp fork f407e5a, gtest-ified).
// canonicalize() mirrors the characterData() dispatch in
// ChapterHtmlSlimParser: boundaries collapse to single spaces, Skip bytes
// vanish, everything else is copied.
#include "TextSanitizer.h"

#include <gtest/gtest.h>

#include <string>

namespace {

std::string canonicalize(const std::string& input) {
  std::string out;
  bool pendingBoundary = false;  // boundaries separate words; emitted lazily

  for (int i = 0; i < static_cast<int>(input.size()); i++) {
    const auto action = TextSanitizer::classifyTextBytes(input.data(), static_cast<int>(input.size()), i);

    switch (action.kind) {
      case TextSanitizer::ActionKind::CopyByte:
        if (pendingBoundary && !out.empty()) {
          out.push_back(' ');
        }
        pendingBoundary = false;
        out.push_back(input[i]);
        break;
      case TextSanitizer::ActionKind::WordBoundary:
      case TextSanitizer::ActionKind::NonBreakingSpace:
        pendingBoundary = true;
        i += action.bytesConsumed - 1;
        break;
      case TextSanitizer::ActionKind::Skip:
        i += action.bytesConsumed - 1;
        break;
    }
  }

  return out;
}

TEST(TextSanitizer, ControlBytesAreWordBoundaries) {
  EXPECT_EQ(canonicalize(std::string("gambler's") + '\x7F' + "fallacy"), "gambler's fallacy");
  EXPECT_EQ(canonicalize(std::string("alpha") + '\x01' + '\x1F' + "beta"), "alpha beta");
  EXPECT_EQ(canonicalize(std::string("\x7F\x7F") + "alpha"), "alpha");
}

TEST(TextSanitizer, PlainWhitespaceStillCollapses) {
  EXPECT_EQ(canonicalize("alpha \r\n\t beta"), "alpha beta");
  EXPECT_EQ(canonicalize("  leading and trailing  "), "leading and trailing");
}

TEST(TextSanitizer, NoBreakSpacesBecomeSpaces) {
  EXPECT_EQ(canonicalize(std::string("200") + "\xC2\xA0" + "Quadratkilometer"), "200 Quadratkilometer");
  EXPECT_EQ(canonicalize(std::string("200") + "\xE2\x80\xAF" + "Quadratkilometer"), "200 Quadratkilometer");
}

TEST(TextSanitizer, BomIsDropped) {
  EXPECT_EQ(canonicalize(std::string("\xEF\xBB\xBF") + "alpha"), "alpha");
  // BOM glued mid-word must not split the word.
  EXPECT_EQ(canonicalize(std::string("al") + "\xEF\xBB\xBF" + "pha"), "alpha");
}

TEST(TextSanitizer, MultiByteUtf8PassesThrough) {
  // CJK + accented latin must come through byte-identical.
  EXPECT_EQ(canonicalize("\xE4\xBD\xA0\xE5\xA5\xBD"), "\xE4\xBD\xA0\xE5\xA5\xBD");
  EXPECT_EQ(canonicalize("caf\xC3\xA9"), "caf\xC3\xA9");
  // 0xC2 / 0xE2 / 0xEF lead bytes NOT forming NBSP/202F/BOM are copied.
  EXPECT_EQ(canonicalize("\xC2\xA1Hola!"), "\xC2\xA1Hola!");     // U+00A1 ¡
  EXPECT_EQ(canonicalize("\xE2\x80\x94"), "\xE2\x80\x94");       // U+2014 em dash
  EXPECT_EQ(canonicalize("\xEF\xBC\x8C"), "\xEF\xBC\x8C");       // U+FF0C fullwidth comma
}

TEST(TextSanitizer, Classification) {
  const std::string nbsp = std::string("200") + "\xC2\xA0" + "Q";
  auto a = TextSanitizer::classifyTextBytes(nbsp.data(), static_cast<int>(nbsp.size()), 3);
  EXPECT_EQ(a.kind, TextSanitizer::ActionKind::NonBreakingSpace);
  EXPECT_EQ(a.bytesConsumed, 2);

  const std::string narrow = std::string("200") + "\xE2\x80\xAF" + "Q";
  a = TextSanitizer::classifyTextBytes(narrow.data(), static_cast<int>(narrow.size()), 3);
  EXPECT_EQ(a.kind, TextSanitizer::ActionKind::NonBreakingSpace);
  EXPECT_EQ(a.bytesConsumed, 3);

  const std::string bom = "\xEF\xBB\xBF" + std::string("a");
  a = TextSanitizer::classifyTextBytes(bom.data(), static_cast<int>(bom.size()), 0);
  EXPECT_EQ(a.kind, TextSanitizer::ActionKind::Skip);
  EXPECT_EQ(a.bytesConsumed, 3);
}

TEST(TextSanitizer, TruncatedSequencesAtBufferEndAreCopied) {
  // A lead byte whose continuation got cut off by the buffer boundary must
  // not be misclassified (expat hands us whole UTF-8 sequences in practice,
  // but the classifier must stay in-bounds regardless).
  const std::string cut = "\xC2";
  const auto a = TextSanitizer::classifyTextBytes(cut.data(), 1, 0);
  EXPECT_EQ(a.kind, TextSanitizer::ActionKind::CopyByte);
  EXPECT_EQ(a.bytesConsumed, 1);
}

TEST(TextSanitizer, OutOfRangeIsSafe) {
  const auto a = TextSanitizer::classifyTextBytes(nullptr, 0, 0);
  EXPECT_EQ(a.kind, TextSanitizer::ActionKind::Skip);
  EXPECT_EQ(a.bytesConsumed, 0);
}

TEST(TextSanitizer, Predicates) {
  EXPECT_TRUE(TextSanitizer::isAsciiWhitespaceByte(' '));
  EXPECT_TRUE(TextSanitizer::isAsciiWhitespaceByte('\n'));
  EXPECT_FALSE(TextSanitizer::isAsciiWhitespaceByte('a'));

  EXPECT_TRUE(TextSanitizer::isSanitizedAsciiControlByte('\x01'));
  EXPECT_TRUE(TextSanitizer::isSanitizedAsciiControlByte('\x7F'));
  EXPECT_FALSE(TextSanitizer::isSanitizedAsciiControlByte('\t'));  // whitespace, not sanitized
  EXPECT_FALSE(TextSanitizer::isSanitizedAsciiControlByte('A'));
  EXPECT_FALSE(TextSanitizer::isSanitizedAsciiControlByte(0xA0));  // high bytes untouched
}

}  // namespace
