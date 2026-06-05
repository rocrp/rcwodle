/* WODLE-PORT: Arduino Stream shim. */
#pragma once

#include "Print.h"

class Stream : public Print
{
public:
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() = 0;

    void setTimeout(unsigned long ms) { _timeoutMs = ms; }
    unsigned long getTimeout() const { return _timeoutMs; }

    size_t readBytes(uint8_t *buf, size_t length)
    {
        size_t n = 0;
        while (n < length)
        {
            int c = read();
            if (c < 0) break; /* no blocking-with-timeout semantics needed yet */
            buf[n++] = (uint8_t)c;
        }
        return n;
    }
    size_t readBytes(char *buf, size_t length) { return readBytes((uint8_t *)buf, length); }

protected:
    unsigned long _timeoutMs = 1000;
};
