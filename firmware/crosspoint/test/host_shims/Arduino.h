/* host shim: Arduino core over std::chrono — no RT-Thread. */
#pragma once

#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "HardwareSerial.h"
#include "Print.h"
#include "Stream.h"
#include "WString.h"

inline unsigned long millis()
{
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return (unsigned long)duration_cast<milliseconds>(steady_clock::now() - t0).count();
}
inline unsigned long micros() { return millis() * 1000UL; }
inline void delay(unsigned long ms) { usleep((useconds_t)ms * 1000); }
inline void delayMicroseconds(unsigned int us) { usleep(us); }
inline void yield() {}

#define F(x) (x)
#define PSTR(x) (x)
#define PROGMEM
#define RTC_NOINIT_ATTR
#define RTC_DATA_ATTR
typedef bool boolean;
typedef uint8_t byte;

#ifndef CROSSPOINT_VERSION
#define CROSSPOINT_VERSION "host-test"
#endif

class EspClass
{
public:
    uint32_t getFreeHeap() { return 1 << 20; }
    uint32_t getHeapSize() { return 1 << 21; }
    uint32_t getMinFreeHeap() { return 1 << 19; }
    uint32_t getMaxAllocHeap() { return 1 << 19; }
    void restart() { abort(); }
};
extern EspClass ESP;
