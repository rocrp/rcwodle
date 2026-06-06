// WODLE-PORT: see WodleDebugCmds.h. MSH handlers (tshell thread) + mailbox.

#include "WodleDebugCmds.h"

#include <rtthread.h>

#include <cstdlib>
#include <cstring>

#include "HalClock.h"
#include "HalGPIO.h"
#include "WodleAht20.h"
#include "WodleBattery.h"
#include "WodleFrontlight.h"

using WodleDebugCmdCore::KeyInject;
using WodleDebugCmdCore::KeyQueue;

/* The pure core mirrors HalGPIO's logical indices — keep them honest. */
static_assert(WodleDebugCmdCore::CORE_BTN_BACK == HalGPIO::BTN_BACK, "core/HalGPIO drift");
static_assert(WodleDebugCmdCore::CORE_BTN_CONFIRM == HalGPIO::BTN_CONFIRM, "core/HalGPIO drift");
static_assert(WodleDebugCmdCore::CORE_BTN_LEFT == HalGPIO::BTN_LEFT, "core/HalGPIO drift");
static_assert(WodleDebugCmdCore::CORE_BTN_RIGHT == HalGPIO::BTN_RIGHT, "core/HalGPIO drift");
static_assert(WodleDebugCmdCore::CORE_BTN_UP == HalGPIO::BTN_UP, "core/HalGPIO drift");
static_assert(WodleDebugCmdCore::CORE_BTN_DOWN == HalGPIO::BTN_DOWN, "core/HalGPIO drift");
static_assert(WodleDebugCmdCore::CORE_BTN_POWER == HalGPIO::BTN_POWER, "core/HalGPIO drift");

namespace
{
/* mailbox — every access wrapped in a critical section */
KeyQueue s_keyQueue;
char s_pendingOpen[256] = {0}; /* empty = none */
bool s_pendingShot = false;
bool s_pendingDump = false;
bool s_debugSession = false; /* latched by any command except `stat` */
bool s_noSleepForced = false;

struct CriticalSection
{
    rt_base_t level;
    CriticalSection() : level(rt_hw_interrupt_disable()) {}
    ~CriticalSection() { rt_hw_interrupt_enable(level); }
};

void markDebugSession()
{
    CriticalSection cs;
    s_debugSession = true;
}

bool sleepInhibitedInternal()
{
    return s_debugSession || s_noSleepForced;
}

int cmdKey(int argc, char **argv)
{
    if (argc < 1)
    {
        rt_kprintf("usage: wodle key <up|down|left|right|confirm|back|power> [holdMs]\n");
        return -1;
    }
    const int btn = WodleDebugCmdCore::parseButtonName(argv[0]);
    if (btn == WodleDebugCmdCore::BTN_INVALID)
    {
        rt_kprintf("wodle: unknown key '%s'\n", argv[0]);
        return -1;
    }
    long hold = 0;
    if (argc >= 2)
    {
        hold = std::strtol(argv[1], nullptr, 10);
        if (hold < 0 || hold > 60000)
        {
            rt_kprintf("wodle: holdMs out of range (0..60000)\n");
            return -1;
        }
    }
    /* A short power press needs press/release on separate frames so the
     * held-time machinery sees it; default it to a plausible human tap. */
    if (btn == HalGPIO::BTN_POWER && hold == 0) hold = 100;

    markDebugSession();
    bool ok;
    {
        CriticalSection cs;
        ok = s_keyQueue.push(KeyInject{static_cast<uint8_t>(btn), static_cast<uint16_t>(hold)});
    }
    if (!ok)
    {
        rt_kprintf("wodle: key queue full (%d pending) — command DROPPED\n", KeyQueue::CAPACITY);
        return -1;
    }
    rt_kprintf("wodle: key %s hold=%ldms queued\n", argv[0], hold);
    return 0;
}

int cmdOpen(int argc, char **argv)
{
    if (argc < 1)
    {
        rt_kprintf("usage: wodle open </books/foo.epub>\n");
        return -1;
    }
    markDebugSession();
    CriticalSection cs;
    if (s_pendingOpen[0] != '\0')
    {
        rt_kprintf("wodle: an open is already pending — command DROPPED\n");
        return -1;
    }
    if (std::strlen(argv[0]) >= sizeof(s_pendingOpen))
    {
        rt_kprintf("wodle: path too long\n");
        return -1;
    }
    std::strcpy(s_pendingOpen, argv[0]);
    rt_kprintf("wodle: open '%s' queued (main thread validates)\n", argv[0]);
    return 0;
}

int cmdShot()
{
    markDebugSession();
    {
        CriticalSection cs;
        s_pendingShot = true;
    }
    rt_kprintf("wodle: screenshot queued (lands as BMP on the SD card)\n");
    return 0;
}

int cmdDump()
{
    markDebugSession();
    {
        CriticalSection cs;
        s_pendingDump = true;
    }
    /* the dump itself prints from the main loop within ~1 frame */
    return 0;
}

int cmdStat()
{
    /* Reads only cached/thread-safe sources — never touches the renderer
     * or activity state from this thread. Clock is UTC (the status bar
     * applies the user's offset; keeping this raw avoids SETTINGS access). */
    float tC = 0, rh = 0;
    const bool aht = WodleAht20::read(tC, rh);
    char clock[16];
    if (!halClock.formatTime(clock, sizeof(clock)))
    {
        std::strcpy(clock, "unset");
    }
    rt_uint32_t total = 0, used = 0, maxUsed = 0;
    rt_memory_info(&total, &used, &maxUsed);
    int keysPending;
    {
        CriticalSection cs;
        keysPending = s_keyQueue.size();
    }

    rt_kprintf("wodle: bat=%d%% mv=%d usb=%d heap_free=%u heap_min_free=%u uptime_ms=%u ", WodleBattery::percent(),
               WodleBattery::millivolts(), WodleBattery::usbPowered() ? 1 : 0, (unsigned)(total - used),
               (unsigned)(total - maxUsed), (unsigned)rt_tick_get_millisecond());
    if (aht)
    {
        rt_kprintf("temp_c=%d.%d rh=%d ", (int)tC, ((int)(tC * 10) % 10 + 10) % 10, (int)rh);
    }
    rt_kprintf("fl=%d%% clock_utc=%s keys_pending=%d nosleep=%d\n", WodleFrontlight::level(), clock, keysPending,
               sleepInhibitedInternal() ? 1 : 0);
    return 0;
}

int cmdNoSleep(int argc, char **argv)
{
    if (argc < 1 || (std::strcmp(argv[0], "on") != 0 && std::strcmp(argv[0], "off") != 0))
    {
        rt_kprintf("usage: wodle nosleep <on|off>\n");
        return -1;
    }
    const bool on = std::strcmp(argv[0], "on") == 0;
    {
        CriticalSection cs;
        s_noSleepForced = on;
        if (!on) s_debugSession = false; /* off also ends the implicit inhibit */
    }
    rt_kprintf("wodle: auto-sleep inhibit %s\n", on ? "ON" : "OFF (debug latch cleared)");
    return 0;
}

} // namespace

/* file scope (not anonymous-namespaced) so MSH_CMD_EXPORT's section entry
 * binds cleanly; msh strips the __cmd_ prefix -> console command `wodle` */
static int wodle(int argc, char **argv)
{
    if (argc < 2)
    {
        rt_kprintf("wodle <key|open|shot|stat|nosleep> — CrossPoint debug driver\n");
        return -1;
    }
    if (std::strcmp(argv[1], "key") == 0) return cmdKey(argc - 2, argv + 2);
    if (std::strcmp(argv[1], "open") == 0) return cmdOpen(argc - 2, argv + 2);
    if (std::strcmp(argv[1], "shot") == 0) return cmdShot();
    if (std::strcmp(argv[1], "dump") == 0) return cmdDump();
    if (std::strcmp(argv[1], "stat") == 0) return cmdStat();
    if (std::strcmp(argv[1], "nosleep") == 0) return cmdNoSleep(argc - 2, argv + 2);
    rt_kprintf("wodle: unknown subcommand '%s'\n", argv[1]);
    return -1;
}
MSH_CMD_EXPORT(wodle, CrossPoint debug driver - key/open/shot/dump/stat/nosleep);

namespace WodleDebugCmds
{

bool dequeueKey(KeyInject &out)
{
    CriticalSection cs;
    return s_keyQueue.pop(out);
}

bool consumePendingOpen(std::string &path)
{
    CriticalSection cs;
    if (s_pendingOpen[0] == '\0') return false;
    path = s_pendingOpen;
    s_pendingOpen[0] = '\0';
    return true;
}

bool consumePendingShot()
{
    CriticalSection cs;
    if (!s_pendingShot) return false;
    s_pendingShot = false;
    return true;
}

bool consumePendingDump()
{
    CriticalSection cs;
    if (!s_pendingDump) return false;
    s_pendingDump = false;
    return true;
}

void emitFrameDump(const uint8_t *fb, const uint32_t size, const int width, const int height)
{
    using WodleDebugCmdCore::BASE64_LINE_BYTES;
    const uint32_t crc = WodleDebugCmdCore::crc32(fb, size);
    rt_kprintf("WODLE_DUMP_BEGIN w=%d h=%d bytes=%u crc32=%08X\n", width, height, (unsigned)size, (unsigned)crc);
    char line[(BASE64_LINE_BYTES + 2) / 3 * 4 + 1];
    for (uint32_t off = 0; off < size; off += BASE64_LINE_BYTES)
    {
        const int n = (size - off < (uint32_t)BASE64_LINE_BYTES) ? (int)(size - off) : BASE64_LINE_BYTES;
        WodleDebugCmdCore::encodeBase64Line(fb + off, n, line);
        rt_kprintf("%s\n", line);
    }
    rt_kprintf("WODLE_DUMP_END\n");
}

bool sleepInhibited()
{
    return sleepInhibitedInternal();
}

} // namespace WodleDebugCmds
