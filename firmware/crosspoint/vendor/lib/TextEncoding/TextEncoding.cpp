// WODLE-PORT addition (not upstream). See TextEncoding.h.
#include "TextEncoding.h"

#include <HalStorage.h>
#include <Logging.h>

#include "GbkTable.h"

namespace {

constexpr uint32_t REPLACEMENT = 0xFFFD;

// Strict UTF-8 validation. Tolerates one truncated sequence at the buffer end
// (the sniff window may cut a multibyte char in half). Returns true if every
// complete sequence is well-formed.
bool isValidUtf8(const uint8_t* buf, const size_t len) {
  size_t i = 0;
  while (i < len) {
    const uint8_t b = buf[i];
    size_t need;
    uint32_t cp;
    if (b < 0x80) {
      i++;
      continue;
    } else if ((b & 0xE0) == 0xC0) {
      need = 1;
      cp = b & 0x1F;
    } else if ((b & 0xF0) == 0xE0) {
      need = 2;
      cp = b & 0x0F;
    } else if ((b & 0xF8) == 0xF0) {
      need = 3;
      cp = b & 0x07;
    } else {
      return false;  // continuation or invalid lead byte
    }
    if (i + need >= len) {
      return true;  // truncated final sequence — tolerated
    }
    for (size_t k = 1; k <= need; k++) {
      if ((buf[i + k] & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (buf[i + k] & 0x3F);
    }
    // Overlongs, surrogates, out of range.
    static constexpr uint32_t kMin[4] = {0, 0x80, 0x800, 0x10000};
    if (cp < kMin[need] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    i += need + 1;
  }
  return true;
}

bool isGbkLead(const uint8_t b) { return b >= 0x81 && b <= 0xFE; }
bool isGbkTrail(const uint8_t b) { return b >= 0x40 && b <= 0xFE && b != 0x7F; }

// Plausible-GBK scan: every high byte must start a valid 2-byte pair.
// Returns true when there was at least one pair and zero invalid sequences
// (a truncated final pair is tolerated).
bool looksLikeGbk(const uint8_t* buf, const size_t len) {
  size_t pairs = 0;
  size_t i = 0;
  while (i < len) {
    const uint8_t b = buf[i];
    if (b < 0x80) {
      i++;
      continue;
    }
    if (!isGbkLead(b)) return false;
    if (i + 1 >= len) break;  // truncated final pair
    if (!isGbkTrail(buf[i + 1])) return false;
    pairs++;
    i += 2;
  }
  return pairs > 0;
}

class Utf8Writer {
 public:
  Utf8Writer(HalFile& file) : file_(file) {}
  bool put(const uint32_t cp) {
    char tmp[4];
    const int n = TextEncoding::encodeUtf8(cp, tmp);
    for (int i = 0; i < n; i++) {
      buf_[fill_++] = tmp[i];
      if (fill_ == sizeof(buf_) && !flushBuf()) return false;
    }
    return true;
  }
  bool flushBuf() {
    if (fill_ > 0 && file_.write(reinterpret_cast<uint8_t*>(buf_), fill_) != fill_) return false;
    fill_ = 0;
    return true;
  }

 private:
  HalFile& file_;
  char buf_[2048];
  size_t fill_ = 0;
};

}  // namespace

namespace TextEncoding {

uint32_t gbkToUnicode(const uint8_t lead, const uint8_t trail) {
  if (!isGbkLead(lead) || !isGbkTrail(trail)) return REPLACEMENT;
  const size_t trailIdx = (trail < 0x7F) ? (trail - 0x40) : (trail - 0x41);
  return GBK_TABLE[(lead - 0x81) * 190 + trailIdx];
}

int encodeUtf8(uint32_t cp, char* out) {
  if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = REPLACEMENT;
  if (cp < 0x80) {
    out[0] = static_cast<char>(cp);
    return 1;
  }
  if (cp < 0x800) {
    out[0] = static_cast<char>(0xC0 | (cp >> 6));
    out[1] = static_cast<char>(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = static_cast<char>(0xE0 | (cp >> 12));
    out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[2] = static_cast<char>(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = static_cast<char>(0xF0 | (cp >> 18));
  out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
  out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  out[3] = static_cast<char>(0x80 | (cp & 0x3F));
  return 4;
}

TextFileEncoding detect(const uint8_t* buf, const size_t len) {
  if (len >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) return TextFileEncoding::Utf8Bom;
  if (len >= 2 && buf[0] == 0xFF && buf[1] == 0xFE) return TextFileEncoding::Utf16Le;
  if (len >= 2 && buf[0] == 0xFE && buf[1] == 0xFF) return TextFileEncoding::Utf16Be;
  if (isValidUtf8(buf, len)) return TextFileEncoding::Utf8;
  if (looksLikeGbk(buf, len)) return TextFileEncoding::Gbk;
  return TextFileEncoding::Unknown;
}

const char* name(const TextFileEncoding e) {
  switch (e) {
    case TextFileEncoding::Utf8:
      return "UTF-8";
    case TextFileEncoding::Utf8Bom:
      return "UTF-8 BOM";
    case TextFileEncoding::Utf16Le:
      return "UTF-16LE";
    case TextFileEncoding::Utf16Be:
      return "UTF-16BE";
    case TextFileEncoding::Gbk:
      return "GBK";
    default:
      return "unknown";
  }
}

bool transcodeToUtf8(const char* srcPath, const char* dstPath, const TextFileEncoding enc) {
  HalFile src, dst;
  if (!Storage.openFileForRead("TENC", srcPath, src)) return false;
  if (!Storage.openFileForWrite("TENC", dstPath, dst)) {
    src.close();
    return false;
  }

  Utf8Writer out(dst);
  uint8_t buf[2048];
  bool ok = true;

  if (enc == TextFileEncoding::Utf8Bom) {
    // Strip the 3-byte BOM, raw-copy the rest (already UTF-8).
    uint8_t bom[3];
    src.read(bom, 3);
    size_t n;
    while (ok && (n = src.read(buf, sizeof(buf))) > 0) {
      ok = dst.write(buf, n) == n;
    }
  } else if (enc == TextFileEncoding::Utf16Le || enc == TextFileEncoding::Utf16Be) {
    const bool be = enc == TextFileEncoding::Utf16Be;
    uint32_t pendingHigh = 0;  // pending high surrogate
    bool first = true;
    size_t n, carry = 0;
    while (ok && (n = src.read(buf + carry, sizeof(buf) - carry) + carry) > carry) {
      size_t i = 0;
      for (; i + 1 < n; i += 2) {
        const uint32_t unit = be ? (buf[i] << 8 | buf[i + 1]) : (buf[i + 1] << 8 | buf[i]);
        if (first) {
          first = false;
          if (unit == 0xFEFF) continue;  // BOM
        }
        if (pendingHigh) {
          if (unit >= 0xDC00 && unit <= 0xDFFF) {
            ok = out.put(0x10000 + ((pendingHigh - 0xD800) << 10) + (unit - 0xDC00));
          } else {
            ok = out.put(REPLACEMENT) && out.put(unit);
          }
          pendingHigh = 0;
        } else if (unit >= 0xD800 && unit <= 0xDBFF) {
          pendingHigh = unit;
        } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
          ok = out.put(REPLACEMENT);  // stray low surrogate
        } else {
          ok = out.put(unit);
        }
        if (!ok) break;
      }
      carry = n - i;
      if (carry) buf[0] = buf[i];  // odd byte → next round
    }
    if (ok && pendingHigh) ok = out.put(REPLACEMENT);
    if (ok && carry) ok = out.put(REPLACEMENT);  // stray trailing byte
  } else {  // Gbk (and Unknown treated as GBK best-effort by callers if ever passed)
    size_t n, carry = 0;
    while (ok && (n = src.read(buf + carry, sizeof(buf) - carry) + carry) > carry) {
      size_t i = 0;
      while (i < n) {
        const uint8_t b = buf[i];
        if (b < 0x80) {
          ok = out.put(b);
          i++;
        } else if (isGbkLead(b)) {
          if (i + 1 >= n) break;  // split pair → carry
          ok = out.put(gbkToUnicode(b, buf[i + 1]));
          i += 2;
        } else {
          ok = out.put(REPLACEMENT);
          i++;
        }
        if (!ok) break;
      }
      carry = n - i;
      if (carry) buf[0] = buf[i];  // lead byte waiting for its trail
    }
    if (ok && carry) ok = out.put(REPLACEMENT);  // truncated final pair
  }

  ok = ok && out.flushBuf();
  src.close();
  dst.close();
  if (!ok) {
    LOG_ERR("TENC", "Transcode %s failed (%s)", name(enc), srcPath);
    Storage.remove(dstPath);
  }
  return ok;
}

}  // namespace TextEncoding
