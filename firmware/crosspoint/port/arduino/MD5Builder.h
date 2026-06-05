/* WODLE-PORT: Arduino MD5Builder shim (self-contained MD5, no mbedtls). */
#pragma once

#include <cstdint>
#include <cstring>

#include "WString.h"

class MD5Builder
{
public:
    void begin();
    void add(const uint8_t *data, size_t len);
    void add(const char *s) { add((const uint8_t *)s, strlen(s)); }
    void add(const String &s) { add((const uint8_t *)s.c_str(), s.length()); }
    void calculate();
    String toString() const;
    void getBytes(uint8_t *out) const { memcpy(out, _digest, 16); }

private:
    void transform(const uint8_t block[64]);

    uint32_t _state[4];
    uint64_t _bits;
    uint8_t _buf[64];
    size_t _bufLen;
    uint8_t _digest[16];
};
