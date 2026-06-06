#pragma once
#include <HalStorage.h>

#include <iostream>

namespace serialization {
template <typename T>
void writePod(std::ostream& os, const T& value) {
  os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
void writePod(HalFile& file, const T& value) {
  file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

template <typename T>
void readPod(std::istream& is, T& value) {
  is.read(reinterpret_cast<char*>(&value), sizeof(T));
}

template <typename T>
void readPod(HalFile& file, T& value) {
  file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T));
}

inline void writeString(std::ostream& os, const std::string& s) {
  const uint32_t len = s.size();
  writePod(os, len);
  os.write(s.data(), len);
}

inline void writeString(HalFile& file, const std::string& s) {
  const uint32_t len = s.size();
  writePod(file, len);
  file.write(reinterpret_cast<const uint8_t*>(s.data()), len);
}

// WODLE-PORT: a corrupt/truncated cache file (power loss mid-write) must not
// drive a multi-gigabyte resize() — that's a bad_alloc panic and a crash loop
// on the book. No legitimate cached string approaches this bound; oversized
// lengths read as empty, short reads keep only the bytes present.
constexpr uint32_t MAX_STRING_LEN = 1024 * 1024;

inline void readString(std::istream& is, std::string& s) {
  uint32_t len = 0;
  readPod(is, len);
  if (len > MAX_STRING_LEN) {
    s.clear();
    return;
  }
  s.resize(len);
  is.read(&s[0], len);
  if (static_cast<uint32_t>(is.gcount()) != len) {
    s.resize(is.gcount() > 0 ? static_cast<size_t>(is.gcount()) : 0);
  }
}

inline void readString(HalFile& file, std::string& s) {
  uint32_t len = 0;
  readPod(file, len);
  if (len > MAX_STRING_LEN) {
    s.clear();
    return;
  }
  s.resize(len);
  const int got = file.read(&s[0], len);
  if (got < 0 || static_cast<uint32_t>(got) != len) {
    s.resize(got > 0 ? static_cast<size_t>(got) : 0);
  }
}
}  // namespace serialization
