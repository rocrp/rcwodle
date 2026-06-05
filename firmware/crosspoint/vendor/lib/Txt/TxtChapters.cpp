// WODLE-PORT addition (not upstream). See TxtChapters.h.
#include "TxtChapters.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

#include <algorithm>
#include <cstdlib>

namespace {
constexpr uint32_t MAGIC = 0x54585443;  // "TXTC"
// Bump when TxtChapterScanner patterns change — cached lists are stale then.
constexpr uint8_t VERSION = 1;
constexpr size_t SCAN_CHUNK = 8 * 1024;

std::string cacheFile(const Txt& txt) { return txt.getCachePath() + "/chapters.bin"; }

bool loadCache(const Txt& txt, std::vector<TxtChapter>& out) {
  HalFile f;
  if (!Storage.openFileForRead("TXTC", cacheFile(txt), f)) return false;

  uint32_t magic = 0, fileSize = 0, count = 0;
  uint8_t version = 0;
  serialization::readPod(f, magic);
  serialization::readPod(f, version);
  serialization::readPod(f, fileSize);
  serialization::readPod(f, count);
  if (magic != MAGIC || version != VERSION || fileSize != static_cast<uint32_t>(txt.getFileSize()) ||
      count > TxtChapterScanner::MAX_CHAPTERS) {
    f.close();
    return false;
  }

  out.clear();
  out.reserve(count);
  for (uint32_t i = 0; i < count; i++) {
    TxtChapter ch;
    uint8_t titleLen = 0;
    serialization::readPod(f, ch.offset);
    serialization::readPod(f, titleLen);
    char buf[TxtChapterScanner::MAX_TITLE_BYTES] = {};
    if (titleLen > sizeof(buf) || f.read(reinterpret_cast<uint8_t*>(buf), titleLen) != titleLen) {
      f.close();
      return false;
    }
    ch.title.assign(buf, titleLen);
    out.push_back(std::move(ch));
  }
  f.close();
  return true;
}

void saveCache(const Txt& txt, const std::vector<TxtChapter>& chapters) {
  HalFile f;
  if (!Storage.openFileForWrite("TXTC", cacheFile(txt), f)) return;
  serialization::writePod(f, MAGIC);
  serialization::writePod(f, VERSION);
  serialization::writePod(f, static_cast<uint32_t>(txt.getFileSize()));
  serialization::writePod(f, static_cast<uint32_t>(chapters.size()));
  for (const auto& ch : chapters) {
    serialization::writePod(f, ch.offset);
    const auto titleLen = static_cast<uint8_t>(ch.title.size());
    serialization::writePod(f, titleLen);
    f.write(reinterpret_cast<const uint8_t*>(ch.title.data()), titleLen);
  }
  f.close();
}

}  // namespace

std::vector<TxtChapter> TxtChapters::loadOrScan(const Txt& txt) {
  std::vector<TxtChapter> chapters;
  if (loadCache(txt, chapters)) {
    LOG_DBG("TXTC", "Loaded %zu chapters from cache", chapters.size());
    return chapters;
  }

  TxtChapterScanner scanner;
  const size_t fileSize = txt.getFileSize();
  auto* buffer = static_cast<uint8_t*>(malloc(SCAN_CHUNK));
  if (!buffer) {
    LOG_ERR("TXTC", "Failed to allocate scan buffer");
    return chapters;
  }
  for (size_t offset = 0; offset < fileSize && !scanner.full(); offset += SCAN_CHUNK) {
    const size_t want = std::min(SCAN_CHUNK, fileSize - offset);
    if (!txt.readContent(buffer, offset, want)) break;
    scanner.feed(buffer, want);
  }
  free(buffer);
  scanner.finish();

  chapters = scanner.chapters();
  LOG_DBG("TXTC", "Scanned %zu chapters", chapters.size());
  txt.setupCacheDir();
  saveCache(txt, chapters);
  return chapters;
}
