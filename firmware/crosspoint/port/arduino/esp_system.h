/* WODLE-PORT: esp_system.h shim — only what the vendored sources touch. */
#pragma once

#include <cstdint>
#include <cstdlib>

typedef int esp_err_t;

static inline uint32_t esp_random(void) { return (uint32_t)rand(); }
