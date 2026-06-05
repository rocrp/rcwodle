/* WODLE-PORT: esp_mac shim. Upstream uses the eFuse MAC as a device-unique
 * obfuscation key. SF32 exposes a chip UID via eFuse; until that read is
 * wired, derive a stable pseudo-unique key from the flash-resident factory
 * data address content. TODO(HIL): replace with real SF32 UID read. */
#pragma once

#include <cstdint>
#include <cstring>

typedef int esp_err_t;
#define ESP_OK 0

static inline esp_err_t esp_efuse_mac_get_default(uint8_t *mac)
{
    /* stable per-device-ish: mix bytes from the vendor bootloader region
     * (device-flashed image) — constant across boots, not across devices
     * with identical firmware. Good enough for settings obfuscation. */
    const uint8_t *src = (const uint8_t *)0x12208000;
    uint8_t acc[6] = {0x77, 0x6F, 0x64, 0x6C, 0x65, 0x21}; /* "wodle!" */
    for (int i = 0; i < 64; i++)
        acc[i % 6] ^= src[i];
    memcpy(mac, acc, 6);
    return ESP_OK;
}
