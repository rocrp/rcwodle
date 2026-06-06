/* WODLE-PORT: the wodle's 8MB OPI PSRAM (MPI1, mapped at 0x60000000) is
 * initialized by the SDK at every boot (HAL_PreInit -> rt_psram_init, gated
 * on BSP_USING_PSRAM which our build sets) but nothing is linked into it —
 * 8MB of dead memory. This exposes it as a permanent carve-out allocator
 * for large buffers, relieving SRAM heap pressure (first tenant: the 2x52KB
 * grayscale AA planes in HalDisplay).
 *
 * Deliberately NOT an rt_memheap: the 52x RT-Thread kernel is partly
 * ROM-linked and flipping RT_USING_MEMHEAP_AS_HEAP blind risks config skew
 * with the ROM build. A bump allocator with no free() covers the actual use
 * case (boot-time/static tenants) with zero kernel surface. */
#pragma once

#include <cstdint>

namespace WodlePsram
{

/* Probe the die with a strided write/readback pattern (beats the cache by
 * spanning well past it) and arm the allocator. Call once at boot. */
void init();

bool available();

/* Permanent carve-out, 32-byte aligned, never freed. nullptr when PSRAM is
 * absent or exhausted — callers must keep their SRAM fallback. */
void *alloc(uint32_t size);

/* Bytes remaining (0 when unavailable) — diagnostics. */
uint32_t remaining();

} // namespace WodlePsram
