/* host shim: Serial -> stdout, no RT-Thread */
#pragma once

#include <cstdio>

#include "Print.h"
#include "Stream.h"

class HWCDC : public Stream
{
public:
    void begin(unsigned long = 0) {}
    void setTxTimeoutMs(unsigned) {}
    size_t write(uint8_t b) override
    {
        fputc(b, stdout);
        return 1;
    }
    size_t write(const uint8_t *buf, size_t size) override
    {
        fwrite(buf, 1, size, stdout);
        return size;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    operator bool() const { return true; }
};

extern HWCDC Serial;
