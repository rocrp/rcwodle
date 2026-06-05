// WODLE-PORT addition (not upstream): encoding detection + one-shot
// transcoding for plain-text books. Chinese .txt novels are commonly GBK (or
// UTF-16 from Windows Notepad); the reading pipeline is strictly UTF-8, so
// Txt::load() detects foreign encodings and transcodes the file once into the
// book cache, then reads the UTF-8 copy. Candidate for upstreaming.
#pragma once

#include <cstddef>
#include <cstdint>

enum class TextFileEncoding : uint8_t {
  Utf8,     // plain UTF-8 / ASCII — read directly, no transcode
  Utf8Bom,  // UTF-8 with BOM — transcode = strip BOM
  Utf16Le,
  Utf16Be,
  Gbk,      // also covers GB2312; GB18030 4-byte sequences map to U+FFFD
  Unknown,  // not valid UTF-8 and not plausible GBK — read as-is (lossy)
};

namespace TextEncoding {

// Map a GBK 2-byte pair to a Unicode codepoint (U+FFFD if unmapped/invalid).
uint32_t gbkToUnicode(uint8_t lead, uint8_t trail);

// Encode cp as UTF-8 into out (>= 4 bytes); returns byte count (1..4).
int encodeUtf8(uint32_t cp, char* out);

// Sniff the encoding from the first `len` bytes of a file (BOMs, strict UTF-8
// scan, then GBK validation). Truncated trailing sequences are tolerated.
TextFileEncoding detect(const uint8_t* buf, size_t len);

inline bool needsTranscode(const TextFileEncoding e) {
  return e == TextFileEncoding::Utf8Bom || e == TextFileEncoding::Utf16Le ||
         e == TextFileEncoding::Utf16Be || e == TextFileEncoding::Gbk;
}

const char* name(TextFileEncoding e);

// Stream-transcode srcPath → dstPath as plain UTF-8 (BOM stripped, invalid
// input → U+FFFD). Returns false on IO error (dst is removed on failure).
bool transcodeToUtf8(const char* srcPath, const char* dstPath, TextFileEncoding enc);

}  // namespace TextEncoding
