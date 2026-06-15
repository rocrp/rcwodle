#pragma once

// WODLE-PORT: flash-resident XIP font system.
//
// Inspired by the sibling "crosslink" firmware's Flash Font Storage, but built on
// crosspoint's own .cpfont v4 format + renderer rather than a foreign glyph format.
// A `fonts.bin` blob (built by tools/build_flash_fonts.py) is flashed into the
// device's unused NOR-flash partitions and rendered straight from memory-mapped
// flash — the whole font lives XIP, costing ~zero glyph-bitmap RAM (vs SD fonts,
// which copy glyphs into RAM on demand).
//
// fonts.bin layout (offsets are FONT-area-relative; see build_flash_fonts.py):
//   Header (16 B):  magic "WFFD" | u32 version=1 | u32 count | u32 reserved
//   Entry  (40 B) x count:  char name[24] | u8 size | u8 style | u8 pad[2]
//                           u32 offset | u32 length | u32 reserved
//   -> 32 B align, then each .cpfont blob (32 B aligned)

#include <cstdint>
#include <string>
#include <vector>

#include "EpdFont.h"
#include "EpdFontData.h"

class GfxRenderer;

class FlashFontSystem {
 public:
  // The flash-font area = the firmware-unused EZIP_REGION (6.5 MB @ 0x12580000) +
  // FONT_REGION (4 MB @ 0x12C00000), claimed as one contiguous 10.5 MB XIP region.
  // The stock bootloader/ftab only own the HCPU app; these data partitions are read
  // by direct memory address, so merging them needs no partition-table change.
  // Mirrored by FONT_REGION_BASE/SIZE in tools/build_flash_fonts.py.
  static constexpr uint32_t kBase = 0x12580000;
  static constexpr uint32_t kSize = 0x00A80000;  // 10.5 MB

  // Parse the WFFD directory, register every font with the renderer, and install
  // the flash-font ID resolver into SETTINGS. `base` is overridable for host tests.
  void begin(GfxRenderer& renderer, const uint8_t* base = reinterpret_cast<const uint8_t*>(kBase),
             uint32_t size = kSize);

  bool available() const { return available_; }

  // Resolve (familyName, fontSizeEnum SMALL..EXTRA_LARGE) -> renderer font ID,
  // or 0 if this family is not flash-resident. Picks the size whose ordinal
  // matches the enum (clamped to the available sizes).
  int resolveFontId(const char* familyName, uint8_t fontSizeEnum) const;

  // Distinct flash family names (for the font-family settings menu).
  const std::vector<std::string>& families() const { return families_; }

 private:
  struct Registered {
    std::string family;
    uint8_t pointSize;
    int fontId;
  };

  // Stable backing storage: EpdFontFamily (copied into the renderer) holds EpdFont*,
  // which hold EpdFontData*. Both vectors are reserve()'d so their addresses never move.
  std::vector<EpdFontData> datas_;
  std::vector<EpdFont> fonts_;
  std::vector<Registered> registered_;
  std::vector<std::string> families_;
  bool available_ = false;
};
