#include "Hyphenator.h"

#include <Utf8.h>

#include <algorithm>
#include <cassert>
#include <vector>

#include "HyphenationCommon.h"
#include "LanguageHyphenator.h"
#include "LanguageRegistry.h"

const LanguageHyphenator* Hyphenator::cachedHyphenator_ = nullptr;

namespace {

// Normalize ISO 639-2 (three-letter) codes to ISO 639-1 (two-letter) codes used by the
// hyphenation registry.  EPUBs may use either form in their dc:language metadata (e.g.
// "eng" instead of "en").  Both the bibliographic ("fre"/"ger") and terminological
// ("fra"/"deu") ISO 639-2 variants are mapped.
struct Iso639Mapping {
  const char* iso639_2;
  const char* iso639_1;
};
static constexpr Iso639Mapping kIso639Mappings[] = {{"eng", "en"}, {"fra", "fr"}, {"fre", "fr"}, {"deu", "de"},
                                                    {"ger", "de"}, {"rus", "ru"}, {"spa", "es"}, {"ita", "it"},
                                                    {"ukr", "uk"}, {"swe", "sv"}};

// Maps a BCP-47 or ISO 639-2 language tag to a language-specific hyphenator.
const LanguageHyphenator* hyphenatorForLanguage(const std::string& langTag) {
  if (langTag.empty()) return nullptr;

  // Extract primary subtag and normalize to lowercase (e.g., "en-US" -> "en", "ENG" -> "en").
  std::string primary;
  primary.reserve(langTag.size());
  for (char c : langTag) {
    if (c == '-' || c == '_') break;
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    primary.push_back(c);
  }
  if (primary.empty()) return nullptr;

  // Normalize ISO 639-2 three-letter codes to two-letter equivalents.
  for (const auto& mapping : kIso639Mappings) {
    if (primary == mapping.iso639_2) {
      primary = mapping.iso639_1;
      break;
    }
  }

  return getLanguageHyphenatorForPrimaryTag(primary);
}

// Maps a codepoint index back to its byte offset inside the source word.
size_t byteOffsetForIndex(const std::vector<CodepointInfo>& cps, const size_t index) {
  return (index < cps.size()) ? cps[index].byteOffset : (cps.empty() ? 0 : cps.back().byteOffset);
}

// Builds a vector of break information from explicit hyphen markers in the given codepoints.
// Only hyphens that appear between two alphabetic characters are considered valid breaks.
//
// Example: "US-Satellitensystems" (cps: U, S, -, S, a, t, ...)
//   -> finds '-' at index 2 with alphabetic neighbors 'S' and 'S'
//   -> returns one BreakInfo at the byte offset of 'S' (the char after '-'),
//      with requiresInsertedHyphen=false because '-' is already visible.
//
// Example: "Satel\u00ADliten" (soft-hyphen between 'l' and 'l')
//   -> returns one BreakInfo with requiresInsertedHyphen=true (soft-hyphen
//      is invisible and needs a visible '-' when the break is used).
std::vector<Hyphenator::BreakInfo> buildExplicitBreakInfos(const std::vector<CodepointInfo>& cps) {
  std::vector<Hyphenator::BreakInfo> breaks;

  for (size_t i = 1; i + 1 < cps.size(); ++i) {
    const uint32_t cp = cps[i].value;
    if (!isExplicitHyphen(cp) || !isAlphabetic(cps[i - 1].value) || !isAlphabetic(cps[i + 1].value)) {
      continue;
    }
    // Offset points to the next codepoint so rendering starts after the hyphen marker.
    breaks.push_back({cps[i + 1].byteOffset, isSoftHyphen(cp)});
  }

  return breaks;
}

bool isSegmentSeparator(const uint32_t cp) { return isExplicitHyphen(cp) || isApostrophe(cp); }

void appendSegmentPatternBreaks(const std::vector<CodepointInfo>& cps, const LanguageHyphenator& hyphenator,
                                const bool includeFallback, std::vector<Hyphenator::BreakInfo>& outBreaks) {
  size_t segStart = 0;

  for (size_t i = 0; i <= cps.size(); ++i) {
    const bool atEnd = i == cps.size();
    const bool atSeparator = !atEnd && isSegmentSeparator(cps[i].value);
    if (!atEnd && !atSeparator) {
      continue;
    }

    if (i > segStart) {
      std::vector<CodepointInfo> segment(cps.begin() + segStart, cps.begin() + i);
      auto segIndexes = hyphenator.breakIndexes(segment);

      if (includeFallback && segIndexes.empty()) {
        const size_t minPrefix = hyphenator.minPrefix();
        const size_t minSuffix = hyphenator.minSuffix();
        for (size_t idx = minPrefix; idx + minSuffix <= segment.size(); ++idx) {
          segIndexes.push_back(idx);
        }
      }

      for (const size_t idx : segIndexes) {
        assert(idx > 0 && idx < segment.size());
        if (idx == 0 || idx >= segment.size()) continue;
        const size_t cpIdx = segStart + idx;
        if (cpIdx < cps.size()) {
          outBreaks.push_back({cps[cpIdx].byteOffset, true});
        }
      }
    }

    segStart = i + 1;
  }
}

void appendApostropheContractionBreaks(const std::vector<CodepointInfo>& cps,
                                       std::vector<Hyphenator::BreakInfo>& outBreaks) {
  constexpr size_t kMinLeftSegmentLen = 3;
  constexpr size_t kMinRightSegmentLen = 3;
  size_t segmentStart = 0;

  for (size_t i = 0; i < cps.size(); ++i) {
    if (isSegmentSeparator(cps[i].value)) {
      if (isApostrophe(cps[i].value) && i > 0 && i + 1 < cps.size() && isAlphabetic(cps[i - 1].value) &&
          isAlphabetic(cps[i + 1].value)) {
        size_t leftPrefixLen = 0;
        for (size_t j = segmentStart; j < i; ++j) {
          if (isAlphabetic(cps[j].value)) {
            ++leftPrefixLen;
          }
        }

        size_t rightSuffixLen = 0;
        for (size_t j = i + 1; j < cps.size() && !isSegmentSeparator(cps[j].value); ++j) {
          if (isAlphabetic(cps[j].value)) {
            ++rightSuffixLen;
          }
        }

        // Avoid stranding short clitics like "l'"/"d'" or contraction tails like "'ve"/"'re"/"'ll".
        if (leftPrefixLen >= kMinLeftSegmentLen && rightSuffixLen >= kMinRightSegmentLen) {
          outBreaks.push_back({cps[i + 1].byteOffset, false});
        }
      }
      segmentStart = i + 1;
    }
  }
}

void sortAndDedupeBreakInfos(std::vector<Hyphenator::BreakInfo>& infos) {
  std::sort(infos.begin(), infos.end(), [](const Hyphenator::BreakInfo& a, const Hyphenator::BreakInfo& b) {
    if (a.byteOffset != b.byteOffset) {
      return a.byteOffset < b.byteOffset;
    }
    return a.requiresInsertedHyphen < b.requiresInsertedHyphen;
  });

  infos.erase(std::unique(infos.begin(), infos.end(),
                          [](const Hyphenator::BreakInfo& a, const Hyphenator::BreakInfo& b) {
                            return a.byteOffset == b.byteOffset;
                          }),
              infos.end());
}

// WODLE-PORT: CJK boundaries are always legal break points (no hyphen),
// regardless of which break path produced the word's other breaks. Without
// this, a word like "Wi-Fi信号不太好" takes the explicit-hyphen path, which
// only runs Liang patterns on alphabetic segments — leaving the CJK tail
// unbreakable. sortAndDedupeBreakInfos keeps the no-hyphen variant on
// collisions (false sorts first).
void appendCjkBoundaryBreaks(const std::vector<CodepointInfo>& cps, std::vector<Hyphenator::BreakInfo>& breaks) {
  for (size_t idx = 1; idx < cps.size(); idx++) {
    if (utf8IsCjkBreakable(cps[idx].value) || utf8IsCjkBreakable(cps[idx - 1].value)) {
      breaks.push_back({cps[idx].byteOffset, false});
    }
  }
}

// WODLE-PORT: post-filter for ALL break paths (sorted input):
//  - drop breaks that violate kinsoku (line starting with closing punct or
//    ending with opening punct);
//  - CJK-adjacent breaks render no hyphen (CJK scripts don't hyphenate) —
//    previously only the plain-indexes path did this, so e.g. "Wi-Fi信号"
//    could render a hyphen after a hanzi.
void applyCjkTypography(const std::vector<CodepointInfo>& cps, std::vector<Hyphenator::BreakInfo>& breaks) {
  if (breaks.empty()) {
    return;
  }
  std::vector<Hyphenator::BreakInfo> kept;
  kept.reserve(breaks.size());
  size_t idx = 0;
  for (auto b : breaks) {
    while (idx < cps.size() && cps[idx].byteOffset < b.byteOffset) {
      idx++;
    }
    if (idx >= cps.size() || cps[idx].byteOffset != b.byteOffset) {
      kept.push_back(b);  // break not at a codepoint boundary we know — keep untouched
      continue;
    }
    if (utf8IsCjkClosingPunct(cps[idx].value)) {
      continue;
    }
    if (idx > 0 && utf8IsCjkOpeningPunct(cps[idx - 1].value)) {
      continue;
    }
    if (utf8IsCjkBreakable(cps[idx].value) || (idx > 0 && utf8IsCjkBreakable(cps[idx - 1].value))) {
      b.requiresInsertedHyphen = false;
    }
    kept.push_back(b);
  }
  breaks.swap(kept);
}

}  // namespace

std::vector<Hyphenator::BreakInfo> Hyphenator::breakOffsets(const std::string& word, const bool includeFallback) {
  if (word.empty()) {
    return {};
  }

  // Convert to codepoints and normalize word boundaries.
  auto cps = collectCodepoints(word);
  trimSurroundingPunctuationAndFootnote(cps);
  const auto* hyphenator = cachedHyphenator_;

  // Detect apostrophe-like separators early; used by both branches below.
  bool hasApostropheLikeSeparator = false;
  for (const auto& cp : cps) {
    if (isApostrophe(cp.value)) {
      hasApostropheLikeSeparator = true;
      break;
    }
  }

  // Explicit hyphen markers (soft or hard) take precedence over language breaks.
  auto explicitBreakInfos = buildExplicitBreakInfos(cps);
  if (!explicitBreakInfos.empty()) {
    // When a word contains explicit hyphens we also run Liang patterns on each alphabetic
    // segment between them. Without this, "US-Satellitensystems" would only offer one split
    // point (after "US-"), making it impossible to break mid-"Satellitensystems" even when
    // "US-Satelliten-" would fit on the line.
    //
    // Example: "US-Satellitensystems"
    //   Segments: ["US", "Satellitensystems"]
    //   Explicit break: after "US-"           -> @3  (no inserted hyphen)
    //   Pattern breaks on "Satellitensystems" -> @5  Sa|tel  (+hyphen)
    //                                            @8  Satel|li  (+hyphen)
    //                                            @10 Satelli|ten  (+hyphen)
    //                                            @13 Satelliten|sys  (+hyphen)
    //                                            @16 Satellitensys|tems  (+hyphen)
    //   Result: 6 sorted break points; the line-breaker picks the widest prefix that fits.
    if (hyphenator) {
      appendSegmentPatternBreaks(cps, *hyphenator, /*includeFallback=*/false, explicitBreakInfos);
    }
    // Also add apostrophe contraction breaks when present (e.g. "l'état-major"
    // has both an explicit hyphen and an apostrophe that can independently break).
    if (hasApostropheLikeSeparator) {
      appendApostropheContractionBreaks(cps, explicitBreakInfos);
    }
    // Merge all break points into ascending byte-offset order.
    appendCjkBoundaryBreaks(cps, explicitBreakInfos);  // WODLE-PORT
    sortAndDedupeBreakInfos(explicitBreakInfos);
    applyCjkTypography(cps, explicitBreakInfos);  // WODLE-PORT
    return explicitBreakInfos;
  }

  // Apostrophe-like separators split compounds into alphabetic segments; run Liang on each segment.
  // This allows words like "all'improvviso" to hyphenate within "improvviso" instead of becoming
  // completely unsplittable due to the apostrophe punctuation. Apostrophe contraction breaks are
  // applied regardless of whether a language hyphenator is available.
  if (hasApostropheLikeSeparator) {
    std::vector<BreakInfo> segmentedBreaks;
    if (hyphenator) {
      appendSegmentPatternBreaks(cps, *hyphenator, includeFallback, segmentedBreaks);
    }
    appendApostropheContractionBreaks(cps, segmentedBreaks);
    appendCjkBoundaryBreaks(cps, segmentedBreaks);  // WODLE-PORT
    sortAndDedupeBreakInfos(segmentedBreaks);
    applyCjkTypography(cps, segmentedBreaks);  // WODLE-PORT
    return segmentedBreaks;
  }

  // Ask language hyphenator for legal break points.
  std::vector<size_t> indexes;
  if (hyphenator) {
    indexes = hyphenator->breakIndexes(cps);
  }

  // Only add fallback breaks if needed
  if (includeFallback && indexes.empty()) {
    const size_t minPrefix = hyphenator ? hyphenator->minPrefix() : LiangWordConfig::kDefaultMinPrefix;
    const size_t minSuffix = hyphenator ? hyphenator->minSuffix() : LiangWordConfig::kDefaultMinSuffix;
    for (size_t idx = minPrefix; idx + minSuffix <= cps.size(); ++idx) {
      indexes.push_back(idx);
    }
  }

  std::vector<Hyphenator::BreakInfo> breaks;
  breaks.reserve(indexes.size());
  for (const size_t idx : indexes) {
    // Hyphen need + CJK/kinsoku handling is centralized in applyCjkTypography
    // (WODLE-PORT) so all break paths agree.
    breaks.push_back({byteOffsetForIndex(cps, idx), true});
  }

  // WODLE-PORT: CJK boundaries break without patterns or fallback — append
  // them even when `indexes` is empty (the old early-return made unspaced
  // Chinese text unbreakable unless the fallback kicked in).
  appendCjkBoundaryBreaks(cps, breaks);
  if (breaks.empty()) {
    return {};
  }
  sortAndDedupeBreakInfos(breaks);
  applyCjkTypography(cps, breaks);  // WODLE-PORT
  return breaks;
}

void Hyphenator::setPreferredLanguage(const std::string& lang) { cachedHyphenator_ = hyphenatorForLanguage(lang); }
