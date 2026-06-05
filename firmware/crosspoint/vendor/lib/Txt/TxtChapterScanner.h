// WODLE-PORT addition (not upstream): chapter-heading detection for plain-text
// books. Chinese .txt novels carry their structure in heading lines (第X章 /
// 楔子 / 番外…), English ones in "Chapter N" — the scanner finds them so the
// TXT reader can offer chapter navigation like EPUB/XTC.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct TxtChapter {
  uint32_t offset = 0;  // byte offset of the heading line start (in the UTF-8 file)
  std::string title;    // trimmed heading line, capped at MAX_TITLE_BYTES
};

// Incremental line scanner: feed() file bytes in arbitrary chunks (headings
// split across chunk boundaries are handled), then finish() for the last
// unterminated line. Pure — no storage/heap surprises beyond the result.
class TxtChapterScanner {
 public:
  static constexpr size_t MAX_CHAPTERS = 2000;
  static constexpr size_t MAX_TITLE_BYTES = 64;
  // Longest line (bytes, CR/LF excluded) still considered a heading: real
  // headings are short; body paragraphs blow way past this.
  static constexpr size_t MAX_HEADING_BYTES = 100;

  void feed(const uint8_t* data, size_t len);
  void finish();

  [[nodiscard]] const std::vector<TxtChapter>& chapters() const { return chapters_; }
  [[nodiscard]] bool full() const { return chapters_.size() >= MAX_CHAPTERS; }

  // True if a single line (no trailing newline) is a chapter heading.
  static bool isHeading(const char* line, size_t len);

 private:
  void endLine();

  std::string pending_;          // current line so far (stops growing on overflow)
  bool pendingOverflow_ = false;
  uint32_t lineStart_ = 0;       // file offset of the current line's first byte
  uint32_t consumed_ = 0;        // total bytes fed
  std::vector<TxtChapter> chapters_;
};
