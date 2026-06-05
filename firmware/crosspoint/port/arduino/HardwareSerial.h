/* WODLE-PORT: Serial → rt_kprintf. The upstream type for USB-CDC serial on
 * ESP32-C3 Arduino is HWCDC; Logging.h binds `HWCDC& logSerial = Serial`, so
 * we provide the same name. */
#pragma once

#include <rtthread.h>

#include "Stream.h"

class HWCDC : public Stream
{
public:
    void begin(unsigned long = 0) {}
    void end() {}

    size_t write(uint8_t b) override
    {
        char c = (char)b;
        rt_kputs_n(&c, 1);
        return 1;
    }
    size_t write(const uint8_t *buf, size_t size) override
    {
        rt_kputs_n((const char *)buf, size);
        return size;
    }

    int available() override { return 0; } /* no console input path */
    int read() override { return -1; }
    int peek() override { return -1; }

    operator bool() const { return true; } /* upstream does `return Serial;` */

private:
    /* rt_kputs needs NUL-terminated; chunk through a small buffer. */
    static void rt_kputs_n(const char *s, size_t n)
    {
        char buf[128];
        while (n)
        {
            size_t c = n < sizeof(buf) - 1 ? n : sizeof(buf) - 1;
            rt_memcpy(buf, s, c);
            buf[c] = '\0';
            rt_kputs(buf);
            s += c;
            n -= c;
        }
    }
};

extern HWCDC Serial;
