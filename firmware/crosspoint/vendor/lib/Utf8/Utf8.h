#pragma once

#include <cstdint>
#include <string>
#define REPLACEMENT_GLYPH 0xFFFD

uint32_t utf8NextCodepoint(const unsigned char** string);
// Appends a Unicode codepoint to a std::string in UTF-8 encoding.
void utf8AppendCodepoint(uint32_t cp, std::string& out);
// Remove the last UTF-8 codepoint from a std::string and return the new size.
size_t utf8RemoveLastChar(std::string& str);
// Truncate string by removing N UTF-8 codepoints from the end.
void utf8TruncateChars(std::string& str, size_t numChars);

// Truncate a raw char buffer to the last complete UTF-8 codepoint boundary.
// Returns the new length (<= len). If the buffer ends mid-sequence, the
// incomplete trailing bytes are excluded.
int utf8SafeTruncateBuffer(const char* buf, int len);

// Returns true for CJK characters that allow line breaks on either side without hyphenation.
// Covers CJK Unified Ideographs, Hiragana, Katakana, Hangul Syllables, CJK punctuation,
// and fullwidth forms — the ranges where word boundaries are implicit per character.
inline bool utf8IsCjkBreakable(const uint32_t cp) {
  return (cp >= 0x3000 && cp <= 0x303F)        // CJK Symbols and Punctuation
         || (cp >= 0x3040 && cp <= 0x309F)     // Hiragana
         || (cp >= 0x30A0 && cp <= 0x30FF)     // Katakana
         || (cp >= 0x3400 && cp <= 0x4DBF)     // CJK Extension A
         || (cp >= 0x4E00 && cp <= 0x9FFF)     // CJK Unified Ideographs
         || (cp >= 0xAC00 && cp <= 0xD7AF)     // Hangul Syllables
         || (cp >= 0xF900 && cp <= 0xFAFF)     // CJK Compatibility Ideographs
         || (cp >= 0xFE30 && cp <= 0xFE4F)     // CJK Compatibility Forms
         || (cp >= 0xFF01 && cp <= 0xFF60)     // Fullwidth Latin / Punctuation
         || (cp >= 0xFF65 && cp <= 0xFFEF)     // Halfwidth Katakana / Hangul
         || (cp >= 0x20000 && cp <= 0x2A6DF)   // CJK Extension B
         || (cp >= 0x2A700 && cp <= 0x2B73F);  // CJK Extension C
}

// Returns true for Unicode combining diacritical marks that should not advance the cursor.
inline bool utf8IsCombiningMark(const uint32_t cp) {
  return (cp >= 0x0300 && cp <= 0x036F)      // Combining Diacritical Marks
         || (cp >= 0x1DC0 && cp <= 0x1DFF)   // Combining Diacritical Marks Supplement
         || (cp >= 0x20D0 && cp <= 0x20FF)   // Combining Diacritical Marks for Symbols
         || (cp >= 0xFE20 && cp <= 0xFE2F);  // Combining Half Marks
}

// WODLE-PORT: CJK kinsoku (行首禁则) — closing punctuation that must not start
// a line. U+2019 ’ is deliberately absent: it doubles as the Latin apostrophe.
inline bool utf8IsCjkClosingPunct(const uint32_t cp) {
  switch (cp) {
    case 0x3001:  // 、
    case 0x3002:  // 。
    case 0xFF0C:  // ，
    case 0xFF01:  // ！
    case 0xFF1F:  // ？
    case 0xFF1B:  // ；
    case 0xFF1A:  // ：
    case 0xFF09:  // ）
    case 0x300B:  // 》
    case 0x300D:  // 」
    case 0x300F:  // 』
    case 0x3011:  // 】
    case 0x3009:  // 〉
    case 0x2026:  // …
    case 0x201D:  // ”
    case 0xFF0E:  // ．
    case 0xFF05:  // ％
      return true;
    default:
      return false;
  }
}

// WODLE-PORT: CJK kinsoku (行尾禁则) — opening punctuation that must not end a line.
inline bool utf8IsCjkOpeningPunct(const uint32_t cp) {
  switch (cp) {
    case 0xFF08:  // （
    case 0x300A:  // 《
    case 0x300C:  // 「
    case 0x300E:  // 『
    case 0x3010:  // 【
    case 0x3008:  // 〈
    case 0x201C:  // “
      return true;
    default:
      return false;
  }
}
