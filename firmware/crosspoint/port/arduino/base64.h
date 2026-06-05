/* WODLE-PORT: ESP32-Arduino base64 class shim. */
#pragma once

#include <cstddef>
#include <cstdint>

#include "WString.h"

class base64
{
public:
    static String encode(const uint8_t *data, size_t len);
    static String encode(const String &text)
    {
        return encode((const uint8_t *)text.c_str(), text.length());
    }
};
