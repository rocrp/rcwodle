#include "FlashFontSystem.h"

#include <algorithm>
#include <cstring>

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "FlashFontParse.h"

namespace {
constexpr uint32_t kFnvPrime = 16777619u;

uint32_t fnv1a(const uint8_t* data, size_t len, uint32_t hash = 2166136261u) {
  for (size_t i = 0; i < len; i++) {
    hash ^= data[i];
    hash *= kFnvPrime;
  }
  return hash;
}

// Deterministic, collision-resistant font ID — mirrors SdCardFontManager's scheme
// (FNV-1a continuation over content hash + family + size). Seeding over the .cpfont
// header+TOC (rather than the full SD-style content hash) keeps flash IDs distinct
// from SD IDs even for an identical font, and distinct per size within a family.
int makeFontId(uint32_t contentHash, const char* familyName, uint8_t pointSize) {
  uint32_t hash = contentHash;
  for (const char* p = familyName; *p; ++p) {
    hash ^= static_cast<uint8_t>(*p);
    hash *= kFnvPrime;
  }
  hash ^= pointSize;
  hash *= kFnvPrime;
  const int id = static_cast<int>(hash);
  return id != 0 ? id : 1;  // 0 is the "not found" sentinel
}
}  // namespace

void FlashFontSystem::begin(GfxRenderer& renderer, const uint8_t* base, uint32_t size) {
  uint32_t count = 0;
  if (!flashfont::readDirectory(base, size, count)) {
    LOG_DBG("FFS", "no flash fonts (no valid WFFD directory)");
    return;
  }

  // reserve() so EpdFontData/EpdFont addresses stay stable (the renderer keeps
  // EpdFontFamily copies that point back into these vectors).
  datas_.reserve(count);
  fonts_.reserve(count);
  registered_.reserve(count);

  for (uint32_t i = 0; i < count; i++) {
    const flashfont::DirEntry entry = flashfont::readEntry(base, i);
    if (entry.offset < flashfont::kDirHeaderSize || static_cast<uint64_t>(entry.offset) + entry.length > size) {
      LOG_ERR("FFS", "entry %u (%s) out of range (off=%u len=%u)", i, entry.name, entry.offset, entry.length);
      continue;
    }
    const uint8_t* blob = base + entry.offset;

    EpdFontData data;
    if (!flashfont::parseCpfontRegular(blob, entry.length, data)) {
      LOG_ERR("FFS", "entry %u (%s sz=%u): invalid .cpfont", i, entry.name, entry.pointSize);
      continue;
    }

    // Hash the .cpfont header + style TOC (32 B + styleCount × 32 B) for the seed.
    const uint32_t hdrTocLen =
        flashfont::kCpHeaderSize + static_cast<uint32_t>(blob[12]) * flashfont::kCpTocEntrySize;
    const uint32_t contentHash = fnv1a(blob, hdrTocLen < entry.length ? hdrTocLen : entry.length);
    const int fontId = makeFontId(contentHash, entry.name, entry.pointSize);
    if (renderer.getFontMap().count(fontId) != 0) {
      LOG_ERR("FFS", "font ID %d collides, skipping %s sz=%u", fontId, entry.name, entry.pointSize);
      continue;
    }

    datas_.push_back(data);
    fonts_.emplace_back(&datas_.back());
    EpdFontFamily family(&fonts_.back());
    renderer.insertFont(fontId, family);
    registered_.push_back({entry.name, entry.pointSize, fontId});
    if (std::find(families_.begin(), families_.end(), entry.name) == families_.end()) {
      families_.emplace_back(entry.name);
    }
    LOG_DBG("FFS", "registered %s sz=%u id=%d (XIP @ 0x%08X)", entry.name, entry.pointSize, fontId,
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(blob)));
  }

  // Install the resolver + publish family names for the settings menu.
  SETTINGS.flashFontIdResolver = [](void* ctx, const char* familyName, uint8_t fontSizeEnum) -> int {
    return static_cast<FlashFontSystem*>(ctx)->resolveFontId(familyName, fontSizeEnum);
  };
  SETTINGS.flashFontResolverCtx = this;
  SETTINGS.flashFontFamilyCount = 0;
  for (const auto& f : families_) {
    if (SETTINGS.flashFontFamilyCount >= CrossPointSettings::MAX_FLASH_FAMILIES) break;
    char* slot = SETTINGS.flashFontFamilies[SETTINGS.flashFontFamilyCount++];
    strncpy(slot, f.c_str(), 31);
    slot[31] = '\0';
  }
  available_ = !registered_.empty();
  LOG_INF("FFS", "ready: %u fonts, %u families", static_cast<unsigned>(registered_.size()),
          static_cast<unsigned>(families_.size()));
}

int FlashFontSystem::resolveFontId(const char* familyName, uint8_t fontSizeEnum) const {
  // Gather this family's registered sizes in ascending point-size order, then map
  // the size enum (SMALL=0 .. EXTRA_LARGE=3) to a slot (clamped to what's present).
  const Registered* slots[8];
  int n = 0;
  for (const auto& r : registered_) {
    if (r.family == familyName && n < 8) slots[n++] = &r;
  }
  if (n == 0) return 0;
  std::sort(slots, slots + n, [](const Registered* a, const Registered* b) { return a->pointSize < b->pointSize; });
  int idx = fontSizeEnum;
  if (idx >= n) idx = n - 1;
  return slots[idx]->fontId;
}
