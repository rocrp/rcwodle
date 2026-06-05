/* WODLE-PORT: AVR progmem shim — flash is memory-mapped (XIP) on SF32, so
 * everything is a plain pointer read. */
#pragma once

#include <cstdint>
#include <cstring>

#define PROGMEM
#define PGM_P const char *
#define PGM_VOID_P const void *

#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#define pgm_read_word(addr) (*(const uint16_t *)(addr))
#define pgm_read_dword(addr) (*(const uint32_t *)(addr))
#define pgm_read_ptr(addr) (*(void *const *)(addr))

#define memcpy_P memcpy
#define strcpy_P strcpy
#define strncpy_P strncpy
#define strcmp_P strcmp
#define strncmp_P strncmp
#define strlen_P strlen
#define snprintf_P snprintf
#define vsnprintf_P vsnprintf
