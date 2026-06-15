#pragma once

// WODLE-PORT: parsing for the flash-resident XIP font blob (fonts.bin).
//
// Header-only and dependency-free (EpdFontData only) so it is unit-testable
// without the renderer/settings. The blob is produced by tools/build_flash_fonts.py
// and read straight from memory-mapped NOR flash by FlashFontSystem.
//
//   Directory (16 B):  magic "WFFD" | u32 version=1 | u32 count | u32 reserved
//   Entry     (40 B) x count:  char name[24] | u8 size | u8 style | u8 pad[2]
//                              u32 offset (FONT-area-relative) | u32 length | u32 reserved
//   -> 32 B align, then each .cpfont v4 blob (32 B aligned)

#include <cstdint>
#include <cstring>

#include "EpdFontData.h"

namespace flashfont {

constexpr uint32_t kDirHeaderSize = 16;
constexpr uint32_t kDirEntrySize = 40;
constexpr uint32_t kDirVersion = 1;

// .cpfont v4 sizes (see fontconvert_sdcard.py / SdCardFont.cpp).
constexpr uint32_t kCpHeaderSize = 32;
constexpr uint32_t kCpTocEntrySize = 32;

// Little-endian, alignment-safe reads (directory + TOC fields are byte-packed).
inline uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
inline int16_t rdi16(const uint8_t* p) { return static_cast<int16_t>(rd16(p)); }
inline uint32_t rd32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

struct DirEntry {
  char name[25];      // NUL-terminated copy of the 24-byte name field
  uint8_t pointSize;  // point size (150 DPI)
  uint8_t style;      // 0 = regular
  uint32_t offset;    // FONT-area-relative offset of the .cpfont blob
  uint32_t length;    // .cpfont blob length
};

// Validate the WFFD directory header at `base`. On success, sets `outCount` and
// returns true. `size` is the available region length.
inline bool readDirectory(const uint8_t* base, uint32_t size, uint32_t& outCount) {
  if (size < kDirHeaderSize || memcmp(base, "WFFD", 4) != 0) return false;
  if (rd32(base + 4) != kDirVersion) return false;
  const uint32_t count = rd32(base + 8);
  if (count == 0 || count > 64) return false;
  if (kDirHeaderSize + static_cast<uint64_t>(count) * kDirEntrySize > size) return false;
  outCount = count;
  return true;
}

// Read directory entry `i` (caller guarantees i < count).
inline DirEntry readEntry(const uint8_t* base, uint32_t i) {
  const uint8_t* e = base + kDirHeaderSize + i * kDirEntrySize;
  DirEntry out{};
  memcpy(out.name, e, 24);
  out.name[24] = '\0';
  out.pointSize = e[24];
  out.style = e[25];
  out.offset = rd32(e + 28);
  out.length = rd32(e + 32);
  return out;
}

// Build an EpdFontData whose pointers aim into the XIP .cpfont blob at `blob`
// (regular style / styleId 0 only — flash blobs carry one weight). Returns false
// on bad magic/version, no regular style, or a truncated blob.
inline bool parseCpfontRegular(const uint8_t* blob, uint32_t blobSize, EpdFontData& out) {
  if (blobSize < kCpHeaderSize) return false;
  static constexpr char kMagic[8] = {'C', 'P', 'F', 'O', 'N', 'T', '\0', '\0'};
  if (memcmp(blob, kMagic, 8) != 0) return false;
  if (rd16(blob + 8) != 4 /* CPFONT_VERSION */) return false;

  const bool is2Bit = (rd16(blob + 10) & 1) != 0;
  const uint8_t styleCount = blob[12];
  if (styleCount == 0 || styleCount > 4) return false;

  const uint8_t* toc = nullptr;
  for (uint8_t i = 0; i < styleCount; i++) {
    const uint8_t* e = blob + kCpHeaderSize + i * kCpTocEntrySize;
    if (e[0] == 0) {  // styleId 0 == regular
      toc = e;
      break;
    }
  }
  if (!toc) return false;

  const uint32_t intervalCount = rd32(toc + 4);
  const uint32_t glyphCount = rd32(toc + 8);
  const uint8_t advanceY = toc[12];
  const int16_t ascender = rdi16(toc + 13);
  const int16_t descender = rdi16(toc + 15);
  const uint16_t kernLeftEntryCount = rd16(toc + 17);
  const uint16_t kernRightEntryCount = rd16(toc + 19);
  const uint8_t kernLeftClassCount = toc[21];
  const uint8_t kernRightClassCount = toc[22];
  const uint8_t ligaturePairCount = toc[23];
  const uint32_t dataOffset = rd32(toc + 24);

  // Section offsets within the blob (identical math to SdCardFont::computeStyleFileOffsets).
  const uint32_t intervalsOff = dataOffset;
  const uint32_t glyphsOff = intervalsOff + intervalCount * sizeof(EpdUnicodeInterval);
  const uint32_t kernLeftOff = glyphsOff + glyphCount * sizeof(EpdGlyph);
  const uint32_t kernRightOff = kernLeftOff + kernLeftEntryCount * sizeof(EpdKernClassEntry);
  const uint32_t kernMatrixOff = kernRightOff + kernRightEntryCount * sizeof(EpdKernClassEntry);
  const uint32_t ligOff = kernMatrixOff + static_cast<uint32_t>(kernLeftClassCount) * kernRightClassCount;
  const uint32_t bitmapOff = ligOff + ligaturePairCount * sizeof(EpdLigaturePair);
  if (bitmapOff > blobSize) return false;  // truncated/corrupt

  out = EpdFontData{};
  out.bitmap = blob + bitmapOff;
  out.glyph = reinterpret_cast<const EpdGlyph*>(blob + glyphsOff);
  out.intervals = reinterpret_cast<const EpdUnicodeInterval*>(blob + intervalsOff);
  out.intervalCount = intervalCount;
  out.advanceY = advanceY;
  out.ascender = ascender;
  out.descender = descender;
  out.is2Bit = is2Bit;
  out.groups = nullptr;
  out.groupCount = 0;
  out.glyphToGroup = nullptr;
  if (kernLeftEntryCount && kernRightEntryCount && kernLeftClassCount && kernRightClassCount) {
    out.kernLeftClasses = reinterpret_cast<const EpdKernClassEntry*>(blob + kernLeftOff);
    out.kernRightClasses = reinterpret_cast<const EpdKernClassEntry*>(blob + kernRightOff);
    out.kernMatrix = reinterpret_cast<const int8_t*>(blob + kernMatrixOff);
    out.kernLeftEntryCount = kernLeftEntryCount;
    out.kernRightEntryCount = kernRightEntryCount;
    out.kernLeftClassCount = kernLeftClassCount;
    out.kernRightClassCount = kernRightClassCount;
  }
  if (ligaturePairCount) {
    out.ligaturePairs = reinterpret_cast<const EpdLigaturePair*>(blob + ligOff);
    out.ligaturePairCount = ligaturePairCount;
  }
  out.glyphMissHandler = nullptr;  // all glyphs resident XIP
  out.glyphMissCtx = nullptr;
  return true;
}

}  // namespace flashfont
