#pragma once

#include <HalStorage.h>

#include <memory>
#include <string>

class Txt {
  std::string filepath;
  std::string cacheBasePath;
  std::string cachePath;
  // WODLE-PORT: file actually read by readContent(). Differs from filepath
  // when load() transcoded a non-UTF-8 source (GBK/UTF-16/BOM) into the cache.
  std::string readPath;
  bool loaded = false;
  size_t fileSize = 0;

  bool ensureUtf8ReadPath();  // WODLE-PORT: detect + one-shot transcode

 public:
  explicit Txt(std::string path, std::string cacheBasePath);

  bool load();
  [[nodiscard]] const std::string& getPath() const { return filepath; }
  [[nodiscard]] const std::string& getCachePath() const { return cachePath; }
  [[nodiscard]] std::string getTitle() const;
  // WODLE-PORT: size of the file readContent() serves (the UTF-8 copy when
  // the source was transcoded) — pagination offsets/caches key on this.
  [[nodiscard]] size_t getFileSize() const { return fileSize; }

  void setupCacheDir() const;
  bool clearCache() const;

  // Cover image support - looks for cover.bmp/jpg/jpeg/png in same folder as txt file
  [[nodiscard]] std::string getCoverBmpPath() const;
  [[nodiscard]] bool generateCoverBmp() const;
  [[nodiscard]] std::string findCoverImage() const;

  // Read content from file
  [[nodiscard]] bool readContent(uint8_t* buffer, size_t offset, size_t length) const;
};
