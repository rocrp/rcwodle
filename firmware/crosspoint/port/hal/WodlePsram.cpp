/* WODLE-PORT: see WodlePsram.h. */
#include "WodlePsram.h"

#include <rtthread.h>

#define PSRAM_BASE_ADDR 0x60000000u
#define PSRAM_TOTAL (8u * 1024u * 1024u)
#define ALIGNMENT 32u

namespace
{
bool s_available;
uint32_t s_brk; /* next free offset */

/* Strided write/readback probe. 16 words spaced 64KB apart span 1MB —
 * far past any cache, so readback hits the die. Original words are
 * restored (harmless either way: nothing else uses PSRAM). If the die
 * were absent the MPI returns junk, not a bus fault, since the
 * controller itself is initialized by HAL_PreInit. */
bool probe()
{
    constexpr int POINTS = 16;
    constexpr uint32_t STRIDE = 64u * 1024u;
    uint32_t saved[POINTS];

    for (int i = 0; i < POINTS; i++)
    {
        volatile uint32_t *p = reinterpret_cast<volatile uint32_t *>(PSRAM_BASE_ADDR + i * STRIDE);
        saved[i] = *p;
        *p = 0xA5C30000u | (uint32_t)i;
    }
    bool ok = true;
    for (int i = 0; i < POINTS; i++)
    {
        volatile uint32_t *p = reinterpret_cast<volatile uint32_t *>(PSRAM_BASE_ADDR + i * STRIDE);
        if (*p != (0xA5C30000u | (uint32_t)i)) ok = false;
        *p = saved[i];
    }
    return ok;
}
} // namespace

namespace WodlePsram
{

void init()
{
    s_available = probe();
    s_brk = 0;
    rt_kprintf("[WodlePsram] %s (8MB @0x60000000)\n", s_available ? "OK" : "probe FAILED");
}

bool available() { return s_available; }

void *alloc(uint32_t size)
{
    if (!s_available || size == 0) return nullptr;
    const uint32_t aligned = (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
    if (aligned > PSRAM_TOTAL - s_brk) return nullptr;
    void *p = reinterpret_cast<void *>(PSRAM_BASE_ADDR + s_brk);
    s_brk += aligned;
    return p;
}

uint32_t remaining() { return s_available ? PSRAM_TOTAL - s_brk : 0; }

} // namespace WodlePsram
