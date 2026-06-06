// WODLE-PORT (rocrp fork f407e5a): see TextSanitizer.h
#include "TextSanitizer.h"

#include <cstdint>

namespace TextSanitizer {

bool isAsciiWhitespaceByte(const unsigned char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }

bool isSanitizedAsciiControlByte(const unsigned char c) {
  return (c < 0x20 && !isAsciiWhitespaceByte(c)) || c == 0x7F;
}

Action classifyTextBytes(const char* s, const int len, const int index) {
  if (s == nullptr || index < 0 || index >= len) {
    return {ActionKind::Skip, 0};
  }

  const auto c = static_cast<uint8_t>(s[index]);

  if (isAsciiWhitespaceByte(c) || isSanitizedAsciiControlByte(c)) {
    return {ActionKind::WordBoundary, 1};
  }

  // U+00A0 no-break space (0xC2 0xA0)
  if (c == 0xC2 && index + 1 < len && static_cast<uint8_t>(s[index + 1]) == 0xA0) {
    return {ActionKind::NonBreakingSpace, 2};
  }

  // U+202F narrow no-break space (0xE2 0x80 0xAF)
  if (c == 0xE2 && index + 2 < len && static_cast<uint8_t>(s[index + 1]) == 0x80 &&
      static_cast<uint8_t>(s[index + 2]) == 0xAF) {
    return {ActionKind::NonBreakingSpace, 3};
  }

  // U+FEFF zero-width no-break space / BOM (0xEF 0xBB 0xBF)
  if (c == 0xEF && index + 2 < len && static_cast<uint8_t>(s[index + 1]) == 0xBB &&
      static_cast<uint8_t>(s[index + 2]) == 0xBF) {
    return {ActionKind::Skip, 3};
  }

  return {ActionKind::CopyByte, 1};
}

}  // namespace TextSanitizer
