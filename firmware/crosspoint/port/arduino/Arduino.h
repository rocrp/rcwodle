/* WODLE-PORT: Arduino core shim over RT-Thread / SiFli HAL. */
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <rthw.h> /* rt_hw_us_delay */
#include <rtthread.h>

#include "HardwareSerial.h"
#include "Print.h"
#include "Stream.h"
#include "WString.h"
#include "pgmspace.h"

/* ---- timing -------------------------------------------------------------- */
inline unsigned long millis() { return (unsigned long)rt_tick_get_millisecond(); }
inline unsigned long micros() { return millis() * 1000UL; } /* coarse; refine if needed */
inline void delay(unsigned long ms) { rt_thread_mdelay((rt_int32_t)ms); }
inline void delayMicroseconds(unsigned int us) { rt_hw_us_delay(us); }
inline void yield() { rt_thread_yield(); }

/* ---- arithmetic helpers --------------------------------------------------- */
#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif
using std::max;
using std::min;

/* ---- misc Arduino-isms ----------------------------------------------------- */
#define F(x) (x)
#define PSTR(x) (x)
typedef bool boolean;
typedef uint8_t byte;

/* ESP32 RTC-noinit data survives deep sleep/reboot on ESP32; the wodle port
 * has no .noinit segment wired up, so these become ordinary statics (crash
 * ring buffer won't survive a reboot — acceptable for now). */
#define RTC_NOINIT_ATTR
#define RTC_DATA_ATTR

/* ---- ESP class subset ------------------------------------------------------ */
class EspClass
{
public:
    uint32_t getFreeHeap()
    {
        rt_size_t total = 0, used = 0, max_used = 0;
        rt_memory_info(&total, &used, &max_used);
        return (uint32_t)(total - used);
    }
    void restart();
};
extern EspClass ESP;
