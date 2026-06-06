/* WODLE-PORT (rocrp fork f407e5a): byte classifier for EPUB character data.
 * Centralizes whitespace / control-char / no-break-space / BOM handling so
 * stray C0 controls (and DEL) in real-world books become word boundaries
 * instead of corrupting words or rendering as tofu. */
#pragma once

#include <cstdint>

namespace TextSanitizer {

enum class ActionKind : uint8_t { CopyByte, WordBoundary, NonBreakingSpace, Skip };

struct Action {
  ActionKind kind;
  int bytesConsumed;
};

bool isAsciiWhitespaceByte(unsigned char c);
bool isSanitizedAsciiControlByte(unsigned char c);
Action classifyTextBytes(const char* s, int len, int index);

}  // namespace TextSanitizer
