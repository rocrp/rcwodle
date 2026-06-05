// WODLE-PORT addition (not upstream). See TxtChapterScanner.h.
#include "TxtChapterScanner.h"

#include <Utf8.h>

#include <cstring>

namespace {

// Codepoint classes used by the heading patterns. Hand-rolled — std::regex
// costs ~100KB of flash and heap churn; this is a handful of comparisons.

bool isAsciiDigit(const uint32_t cp) { return cp >= '0' && cp <= '9'; }
bool isFullwidthDigit(const uint32_t cp) { return cp >= 0xFF10 && cp <= 0xFF19; }  // ０-９

bool isCjkNumeral(const uint32_t cp) {
  switch (cp) {
    case 0x4E00:  // 一
    case 0x4E8C:  // 二
    case 0x4E09:  // 三
    case 0x56DB:  // 四
    case 0x4E94:  // 五
    case 0x516D:  // 六
    case 0x4E03:  // 七
    case 0x516B:  // 八
    case 0x4E5D:  // 九
    case 0x5341:  // 十
    case 0x767E:  // 百
    case 0x5343:  // 千
    case 0x4E07:  // 万
    case 0x4EBF:  // 亿
    case 0x96F6:  // 零
    case 0x3007:  // 〇
    case 0x4E24:  // 两
      return true;
    default:
      return false;
  }
}

bool isNumeral(const uint32_t cp) { return isAsciiDigit(cp) || isFullwidthDigit(cp) || isCjkNumeral(cp); }

bool isChapterUnit(const uint32_t cp) {
  switch (cp) {
    case 0x7AE0:  // 章
    case 0x8282:  // 节
    case 0x5377:  // 卷
    case 0x56DE:  // 回
    case 0x90E8:  // 部
    case 0x7BC7:  // 篇
    case 0x96C6:  // 集
    case 0x8BDD:  // 话
    case 0x5E55:  // 幕
      return true;
    default:
      return false;
  }
}

bool isSpaceCp(const uint32_t cp) {
  return cp == ' ' || cp == '\t' || cp == 0x3000 /* full-width space */ || cp == 0x00A0;
}

// Standalone headings that open/close books without a 第X章 pattern.
// UTF-8 literals; matched as a line prefix.
const char* const kSpecialHeadings[] = {
    "序章",  // 序章
    "序言",  // 序言
    "自序",  // 自序
    "前言",  // 前言
    "引子",  // 引子
    "楔子",  // 楔子
    "尾声",  // 尾声
    "后记",  // 后记
    "番外",  // 番外
    "终章",  // 终章
    "大结局",  // 大结局
};

uint32_t nextCp(const unsigned char*& p, const unsigned char* end) {
  if (p >= end) return 0;
  return utf8NextCodepoint(&p);
}

bool isRomanNumeral(const char c) {
  switch (c) {
    case 'I':
    case 'V':
    case 'X':
    case 'L':
    case 'C':
    case 'i':
    case 'v':
    case 'x':
    case 'l':
    case 'c':
      return true;
    default:
      return false;
  }
}

}  // namespace

bool TxtChapterScanner::isHeading(const char* line, const size_t len) {
  if (len == 0 || len > MAX_HEADING_BYTES) return false;

  const auto* p = reinterpret_cast<const unsigned char*>(line);
  const unsigned char* end = p + len;

  // Skip leading whitespace (incl. full-width space).
  const unsigned char* cur = p;
  while (cur < end) {
    const unsigned char* probe = cur;
    const uint32_t cp = nextCp(probe, end);
    if (!isSpaceCp(cp)) break;
    cur = probe;
  }
  if (cur >= end) return false;
  const size_t remaining = static_cast<size_t>(end - cur);

  // Pattern 1: 第 <numerals×1..8> <unit> ...
  {
    const unsigned char* probe = cur;
    if (nextCp(probe, end) == 0x7B2C) {  // 第
      int numerals = 0;
      uint32_t cp = 0;
      while (probe < end && numerals <= 8) {
        const unsigned char* save = probe;
        cp = nextCp(probe, end);
        if (!isNumeral(cp)) {
          probe = save;
          break;
        }
        numerals++;
        cp = 0;
      }
      if (numerals >= 1 && numerals <= 8 && probe < end && isChapterUnit(nextCp(probe, end))) {
        return true;  // rest is the chapter name — line length is already capped
      }
    }
  }

  // Pattern 2: special standalone headings (序章/楔子/番外…), optionally
  // followed by whitespace, separator or a numeral (番外一 / 楔子：缘起).
  for (const char* special : kSpecialHeadings) {
    const size_t slen = std::strlen(special);
    if (remaining < slen || std::memcmp(cur, special, slen) != 0) continue;
    const unsigned char* probe = cur + slen;
    if (probe >= end) return true;
    const uint32_t cp = nextCp(probe, end);
    if (isSpaceCp(cp) || isNumeral(cp) || cp == 0xFF1A /*：*/ || cp == ':' || cp == 0x3001 /*、*/ ||
        cp == 0x00B7 /*·*/ || cp == '-' || cp == 0x2014 /*—*/ || cp == '.') {
      return true;
    }
    return false;
  }

  // Pattern 3: Chapter N / CHAPTER IV (ASCII).
  if (remaining >= 9 && (std::memcmp(cur, "Chapter ", 8) == 0 || std::memcmp(cur, "CHAPTER ", 8) == 0 ||
                         std::memcmp(cur, "chapter ", 8) == 0)) {
    const char c = static_cast<char>(cur[8]);
    if ((c >= '0' && c <= '9') || isRomanNumeral(c)) return true;
  }

  return false;
}

void TxtChapterScanner::endLine() {
  // Strip trailing CR.
  size_t len = pending_.size();
  if (len > 0 && pending_[len - 1] == '\r') len--;

  if (!pendingOverflow_ && len > 0 && !full() && isHeading(pending_.data(), len)) {
    TxtChapter ch;
    ch.offset = lineStart_;
    // Cap title at a UTF-8 boundary.
    size_t titleLen = len < MAX_TITLE_BYTES ? len : MAX_TITLE_BYTES;
    while (titleLen > 0 && (static_cast<unsigned char>(pending_[titleLen]) & 0xC0) == 0x80) titleLen--;
    ch.title.assign(pending_.data(), titleLen);
    chapters_.push_back(std::move(ch));
  }

  pending_.clear();
  pendingOverflow_ = false;
}

void TxtChapterScanner::feed(const uint8_t* data, const size_t len) {
  for (size_t i = 0; i < len; i++) {
    const uint8_t b = data[i];
    if (b == '\n') {
      endLine();
      lineStart_ = consumed_ + static_cast<uint32_t>(i) + 1;
    } else if (!pendingOverflow_) {
      if (pending_.size() >= MAX_HEADING_BYTES + 1) {
        pendingOverflow_ = true;  // too long to be a heading; stop buffering
      } else {
        pending_.push_back(static_cast<char>(b));
      }
    }
  }
  consumed_ += static_cast<uint32_t>(len);
}

void TxtChapterScanner::finish() {
  if (!pending_.empty()) endLine();
}
