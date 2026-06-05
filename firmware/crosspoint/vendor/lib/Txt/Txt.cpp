#include "Txt.h"

#include <FsHelpers.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>
#include <Serialization.h>
#include <TextEncoding.h>  // WODLE-PORT: GBK/UTF-16 → UTF-8 transcoding

Txt::Txt(std::string path, std::string cacheBasePath)
    : filepath(std::move(path)), cacheBasePath(std::move(cacheBasePath)) {
  // Generate cache path from file path hash
  const size_t hash = std::hash<std::string>{}(filepath);
  cachePath = this->cacheBasePath + "/txt_" + std::to_string(hash);
}

bool Txt::load() {
  if (loaded) {
    return true;
  }

  if (!Storage.exists(filepath.c_str())) {
    LOG_ERR("TXT", "File does not exist: %s", filepath.c_str());
    return false;
  }

  // WODLE-PORT: the pipeline is strictly UTF-8 — Chinese .txt is commonly
  // GBK or UTF-16. Detect and (once) transcode into the cache dir; all reads
  // then go through readPath.
  if (!ensureUtf8ReadPath()) {
    return false;
  }

  HalFile file;
  if (!Storage.openFileForRead("TXT", readPath, file)) {
    LOG_ERR("TXT", "Failed to open file: %s", readPath.c_str());
    return false;
  }

  fileSize = file.size();
  file.close();

  loaded = true;
  LOG_DBG("TXT", "Loaded TXT file: %s (%zu bytes)", readPath.c_str(), fileSize);
  return true;
}

// WODLE-PORT: sniff the source encoding; for non-UTF-8, transcode once to
// <cachePath>/utf8.txt (with a sidecar recording source size + head hash for
// invalidation) and point readPath at the copy.
bool Txt::ensureUtf8ReadPath() {
  readPath = filepath;

  HalFile src;
  if (!Storage.openFileForRead("TXT", filepath, src)) {
    LOG_ERR("TXT", "Failed to open file: %s", filepath.c_str());
    return false;
  }
  const size_t srcSize = src.size();
  uint8_t sniff[4096];
  const size_t sniffLen = src.read(sniff, sizeof(sniff));
  src.close();

  const TextFileEncoding enc = TextEncoding::detect(sniff, sniffLen);
  if (!TextEncoding::needsTranscode(enc)) {
    if (enc == TextFileEncoding::Unknown) {
      LOG_ERR("TXT", "Unknown encoding, reading as UTF-8 (lossy): %s", filepath.c_str());
    }
    return true;
  }

  setupCacheDir();
  const std::string utf8Path = cachePath + "/utf8.txt";
  const std::string metaPath = cachePath + "/utf8.meta";

  // FNV-1a over the sniff window: cheap content fingerprint for invalidation.
  uint32_t headHash = 2166136261u;
  for (size_t i = 0; i < sniffLen; i++) {
    headHash = (headHash ^ sniff[i]) * 16777619u;
  }

  HalFile meta;
  if (Storage.exists(utf8Path.c_str()) && Storage.openFileForRead("TXT", metaPath, meta)) {
    uint32_t storedSize = 0, storedHash = 0;
    if (meta.size() >= sizeof(storedSize) + sizeof(storedHash)) {
      serialization::readPod(meta, storedSize);
      serialization::readPod(meta, storedHash);
    }
    meta.close();
    if (storedSize == static_cast<uint32_t>(srcSize) && storedHash == headHash) {
      readPath = utf8Path;
      return true;
    }
  }

  LOG_DBG("TXT", "Transcoding %s (%s) to UTF-8 cache", filepath.c_str(), TextEncoding::name(enc));
  if (!TextEncoding::transcodeToUtf8(filepath.c_str(), utf8Path.c_str(), enc)) {
    LOG_ERR("TXT", "Transcode failed, reading original (mojibake likely)");
    return true;  // degrade gracefully rather than refusing to open
  }

  HalFile metaOut;
  if (Storage.openFileForWrite("TXT", metaPath, metaOut)) {
    serialization::writePod(metaOut, static_cast<uint32_t>(srcSize));
    serialization::writePod(metaOut, headHash);
    metaOut.close();
  }

  readPath = utf8Path;
  return true;
}

std::string Txt::getTitle() const {
  // Extract filename without path and extension
  size_t lastSlash = filepath.find_last_of('/');
  std::string filename = (lastSlash != std::string::npos) ? filepath.substr(lastSlash + 1) : filepath;

  // Remove .txt extension
  if (FsHelpers::hasTxtExtension(filename)) {
    filename = filename.substr(0, filename.length() - 4);
  }

  return filename;
}

void Txt::setupCacheDir() const {
  if (!Storage.exists(cacheBasePath.c_str())) {
    Storage.mkdir(cacheBasePath.c_str());
  }
  if (!Storage.exists(cachePath.c_str())) {
    Storage.mkdir(cachePath.c_str());
  }
}

std::string Txt::findCoverImage() const {
  // Get the folder containing the txt file
  size_t lastSlash = filepath.find_last_of('/');
  std::string folder = (lastSlash != std::string::npos) ? filepath.substr(0, lastSlash) : "";
  if (folder.empty()) {
    folder = "/";
  }

  // Get the base filename without extension (e.g., "mybook" from "/books/mybook.txt")
  std::string baseName = getTitle();

  // Image extensions to try
  const char* extensions[] = {".bmp", ".jpg", ".jpeg", ".png", ".BMP", ".JPG", ".JPEG", ".PNG"};

  // First priority: look for image with same name as txt file (e.g., mybook.jpg)
  for (const auto& ext : extensions) {
    std::string coverPath = folder + "/" + baseName + ext;
    if (Storage.exists(coverPath.c_str())) {
      LOG_DBG("TXT", "Found matching cover image: %s", coverPath.c_str());
      return coverPath;
    }
  }

  // Fallback: look for cover image files
  const char* coverNames[] = {"cover", "Cover", "COVER"};
  for (const auto& name : coverNames) {
    for (const auto& ext : extensions) {
      std::string coverPath = folder + "/" + std::string(name) + ext;
      if (Storage.exists(coverPath.c_str())) {
        LOG_DBG("TXT", "Found fallback cover image: %s", coverPath.c_str());
        return coverPath;
      }
    }
  }

  return "";
}

std::string Txt::getCoverBmpPath() const { return cachePath + "/cover.bmp"; }

bool Txt::generateCoverBmp() const {
  // Already generated, return true
  if (Storage.exists(getCoverBmpPath().c_str())) {
    return true;
  }

  std::string coverImagePath = findCoverImage();
  if (coverImagePath.empty()) {
    LOG_DBG("TXT", "No cover image found for TXT file");
    return false;
  }

  // Setup cache directory
  setupCacheDir();

  if (FsHelpers::hasBmpExtension(coverImagePath)) {
    // Copy BMP file to cache
    LOG_DBG("TXT", "Copying BMP cover image to cache");
    HalFile src, dst;
    if (!Storage.openFileForRead("TXT", coverImagePath, src)) {
      return false;
    }
    if (!Storage.openFileForWrite("TXT", getCoverBmpPath(), dst)) {
      return false;
    }
    uint8_t buffer[1024];
    while (src.available()) {
      size_t bytesRead = src.read(buffer, sizeof(buffer));
      dst.write(buffer, bytesRead);
    }
    LOG_DBG("TXT", "Copied BMP cover to cache");
    return true;
  } else if (FsHelpers::hasJpgExtension(coverImagePath)) {
    // Convert JPG/JPEG to BMP (same approach as Epub)
    LOG_DBG("TXT", "Generating BMP from JPG cover image");
    HalFile coverJpg, coverBmp;
    if (!Storage.openFileForRead("TXT", coverImagePath, coverJpg)) {
      return false;
    }
    if (!Storage.openFileForWrite("TXT", getCoverBmpPath(), coverBmp)) {
      return false;
    }
    const bool success = JpegToBmpConverter::jpegFileToBmpStream(coverJpg, coverBmp);

    if (!success) {
      LOG_ERR("TXT", "Failed to generate BMP from JPG cover image");
      Storage.remove(getCoverBmpPath().c_str());
    } else {
      LOG_DBG("TXT", "Generated BMP from JPG cover image");
    }
    return success;
  }

  // PNG files are not supported (would need a PNG decoder)
  LOG_ERR("TXT", "Cover image format not supported (only BMP/JPG/JPEG)");
  return false;
}

bool Txt::clearCache() const {
  if (!Storage.exists(cachePath.c_str())) {
    LOG_DBG("TXT", "Cache does not exist, no action needed");
    return true;
  }

  if (!Storage.removeDir(cachePath.c_str())) {
    LOG_ERR("TXT", "Failed to clear cache");
    return false;
  }

  LOG_DBG("TXT", "Cache cleared successfully");
  return true;
}

bool Txt::readContent(uint8_t* buffer, size_t offset, size_t length) const {
  if (!loaded) {
    return false;
  }

  HalFile file;
  // WODLE-PORT: read the (possibly transcoded) UTF-8 copy.
  if (!Storage.openFileForRead("TXT", readPath, file)) {
    return false;
  }

  if (!file.seek(offset)) {
    return false;
  }

  size_t bytesRead = file.read(buffer, length);
  return bytesRead > 0;
}
