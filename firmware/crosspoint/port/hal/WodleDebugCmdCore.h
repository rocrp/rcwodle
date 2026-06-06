/* WODLE-PORT: pure logic behind the `wodle` MSH debug commands — no
 * RT-Thread dependencies so the host suite (test/debugcmds) can cover it.
 * Threading is the caller's job: WodleDebugCmds wraps queue access in
 * critical sections; HalGPIO::update() consumes on the main thread.
 *
 * Design notes (after discussion with codex):
 * - Injection happens at HalGPIO edge level so remapping and reader
 *   behavior stay real (same path as physical keys / touch taps).
 * - The tracker mirrors the PHYSICAL power-key semantics: a synthetic
 *   power press released before the short-press cutoff synthesizes a
 *   CONFIRM edge, exactly like HalGPIO does for the real pin; a long
 *   synthetic hold drives the genuine hold-to-sleep path.
 * - The queue is bounded and push reports failure so the MSH handler can
 *   fail loudly instead of silently dropping input. */
#pragma once

#include <cstdint>
#include <cstring>

namespace WodleDebugCmdCore {

constexpr int BTN_INVALID = -1;

/* Mirrors upstream HalGPIO logical button indices (can't include the real
 * header here — it drags in Arduino.h). Static-asserted in WodleDebugCmds.cpp. */
constexpr uint8_t CORE_BTN_BACK = 0;
constexpr uint8_t CORE_BTN_CONFIRM = 1;
constexpr uint8_t CORE_BTN_LEFT = 2;
constexpr uint8_t CORE_BTN_RIGHT = 3;
constexpr uint8_t CORE_BTN_UP = 4;
constexpr uint8_t CORE_BTN_DOWN = 5;
constexpr uint8_t CORE_BTN_POWER = 6;

/* Lowercase logical names only; returns BTN_INVALID on anything else. */
inline int parseButtonName(const char* name) {
  if (name == nullptr) return BTN_INVALID;
  if (std::strcmp(name, "up") == 0) return CORE_BTN_UP;
  if (std::strcmp(name, "down") == 0) return CORE_BTN_DOWN;
  if (std::strcmp(name, "left") == 0) return CORE_BTN_LEFT;
  if (std::strcmp(name, "right") == 0) return CORE_BTN_RIGHT;
  if (std::strcmp(name, "confirm") == 0) return CORE_BTN_CONFIRM;
  if (std::strcmp(name, "back") == 0) return CORE_BTN_BACK;
  if (std::strcmp(name, "power") == 0) return CORE_BTN_POWER;
  return BTN_INVALID;
}

struct KeyInject {
  uint8_t btn = 0;
  uint16_t holdMs = 0;  // 0 = one-frame tap (press+release together)
};

/* Fixed-capacity FIFO mailbox; push fails when full. Caller locks. */
class KeyQueue {
 public:
  static constexpr int CAPACITY = 8;

  bool push(const KeyInject e) {
    if (count_ >= CAPACITY) return false;
    items_[(head_ + count_) % CAPACITY] = e;
    count_++;
    return true;
  }

  bool pop(KeyInject& out) {
    if (count_ == 0) return false;
    out = items_[head_];
    head_ = (head_ + 1) % CAPACITY;
    count_--;
    return true;
  }

  int size() const { return count_; }

 private:
  KeyInject items_[CAPACITY] = {};
  int head_ = 0;
  int count_ = 0;
};

/* One active synthetic press, emitting hardware-faithful edges over time.
 * Lifecycle per main-loop frame:
 *   if (!active()) queue.pop -> begin(e, now)
 *   edges = tick(now, ...)   // apply to wasPressed/wasReleased/isPressed
 */
class InjectionTracker {
 public:
  struct Edges {
    int pressEdge = BTN_INVALID;     // latch wasPressed[pressEdge] this frame
    int releaseEdge = BTN_INVALID;   // latch wasReleased[releaseEdge] this frame
    bool synthesizeConfirm = false;  // short power release -> CONFIRM edge
    int heldBtn = BTN_INVALID;       // keep isPressed[heldBtn] asserted
  };

  bool active() const { return activeBtn_ != BTN_INVALID; }
  int activeButton() const { return activeBtn_; }
  unsigned long startMs() const { return startMs_; }

  void begin(const KeyInject e, const unsigned long nowMs) {
    activeBtn_ = e.btn;
    holdMs_ = e.holdMs;
    startMs_ = nowMs;
    pressPending_ = true;
  }

  Edges tick(const unsigned long nowMs, const uint16_t powerShortMaxMs, const uint8_t btnPower) {
    Edges edges;
    if (!active()) return edges;

    if (pressPending_) {
      pressPending_ = false;
      edges.pressEdge = activeBtn_;
    }

    const unsigned long held = nowMs - startMs_;
    if (held >= holdMs_) {
      edges.releaseEdge = activeBtn_;
      /* Mirror the physical pin: short power press -> CONFIRM on release. */
      if (activeBtn_ == btnPower && held < powerShortMaxMs) {
        edges.synthesizeConfirm = true;
      }
      activeBtn_ = BTN_INVALID;
    } else {
      edges.heldBtn = activeBtn_;
    }
    return edges;
  }

 private:
  int activeBtn_ = BTN_INVALID;
  uint16_t holdMs_ = 0;
  unsigned long startMs_ = 0;
  bool pressPending_ = false;
};

/* --- framebuffer dump helpers (`wodle dump`) -------------------------------
 * The 1-bit panel buffer is streamed over the console as base64 lines between
 * WODLE_DUMP_BEGIN/END markers with a CRC32 so the host driver
 * (tools/wodle_console.py) can verify a noisy UART capture. Pure code so the
 * host suite pins the exact encoding the Python decoder expects. */

/* Standard CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320), bitwise — no
 * table; ~52KB frame = ~420k iterations, negligible at MCU clock. */
inline uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc = 0xFFFFFFFFu) {
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

/* Bytes of input consumed per base64 output line (76 chars). */
constexpr int BASE64_LINE_BYTES = 57;

/* Encode up to BASE64_LINE_BYTES bytes; out must hold 4*ceil(n/3)+1 chars.
 * Returns the number of chars written (NUL appended, not counted). */
inline int encodeBase64Line(const uint8_t* in, int n, char* out) {
  static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int o = 0;
  for (int i = 0; i < n; i += 3) {
    const uint32_t b0 = in[i];
    const uint32_t b1 = (i + 1 < n) ? in[i + 1] : 0;
    const uint32_t b2 = (i + 2 < n) ? in[i + 2] : 0;
    const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
    out[o++] = kAlphabet[(triple >> 18) & 0x3F];
    out[o++] = kAlphabet[(triple >> 12) & 0x3F];
    out[o++] = (i + 1 < n) ? kAlphabet[(triple >> 6) & 0x3F] : '=';
    out[o++] = (i + 2 < n) ? kAlphabet[triple & 0x3F] : '=';
  }
  out[o] = '\0';
  return o;
}

}  // namespace WodleDebugCmdCore
