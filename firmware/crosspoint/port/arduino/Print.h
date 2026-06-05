/* WODLE-PORT: Arduino Print/Printable shim. */
#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "WString.h"

class Print;

class Printable
{
public:
    virtual ~Printable() = default;
    virtual size_t printTo(Print &p) const = 0;
};

class Print
{
public:
    virtual ~Print() = default;

    virtual size_t write(uint8_t b) = 0;
    virtual size_t write(const uint8_t *buf, size_t size)
    {
        size_t n = 0;
        while (n < size && write(buf[n])) n++;
        return n;
    }
    size_t write(const char *s) { return s ? write((const uint8_t *)s, strlen(s)) : 0; }
    size_t write(const char *buf, size_t size) { return write((const uint8_t *)buf, size); }

    size_t print(const char *s) { return write(s); }
    size_t print(const String &s) { return write((const uint8_t *)s.c_str(), s.length()); }
    size_t print(char c) { return write((uint8_t)c); }
    size_t print(int v) { return print(String(v)); }
    size_t print(unsigned int v) { return print(String(v)); }
    size_t print(long v) { return print(String(v)); }
    size_t print(unsigned long v) { return print(String(v)); }
    size_t print(double v, int decimals = 2) { return print(String(v, decimals)); }
    size_t print(const Printable &p) { return p.printTo(*this); }

    size_t println() { return write("\r\n"); }
    template <typename T>
    size_t println(const T &v)
    {
        size_t n = print(v);
        return n + println();
    }

    size_t printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)))
    {
        char buf[256];
        va_list args;
        va_start(args, fmt);
        int n = vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        if (n <= 0) return 0;
        return write((const uint8_t *)buf, (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
    }

    virtual void flush() {}
};
