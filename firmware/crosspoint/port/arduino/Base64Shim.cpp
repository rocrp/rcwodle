/* WODLE-PORT: base64 codec backing both the Arduino `base64` class and the
 * mbedtls_base64_* API. */

#include "base64.h"
#include "mbedtls/base64.h"

static const char ENC[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int decVal(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

extern "C" int mbedtls_base64_encode(unsigned char *dst, size_t dlen, size_t *olen,
                                     const unsigned char *src, size_t slen)
{
    size_t need = ((slen + 2) / 3) * 4;
    *olen = need;
    if (!dst || dlen < need + 1) return MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;

    size_t o = 0;
    for (size_t i = 0; i < slen; i += 3)
    {
        unsigned v = src[i] << 16;
        if (i + 1 < slen) v |= src[i + 1] << 8;
        if (i + 2 < slen) v |= src[i + 2];
        dst[o++] = ENC[(v >> 18) & 63];
        dst[o++] = ENC[(v >> 12) & 63];
        dst[o++] = (i + 1 < slen) ? ENC[(v >> 6) & 63] : '=';
        dst[o++] = (i + 2 < slen) ? ENC[v & 63] : '=';
    }
    dst[o] = '\0';
    *olen = o;
    return 0;
}

extern "C" int mbedtls_base64_decode(unsigned char *dst, size_t dlen, size_t *olen,
                                     const unsigned char *src, size_t slen)
{
    /* count significant chars */
    size_t sig = 0;
    for (size_t i = 0; i < slen; i++)
    {
        unsigned char c = src[i];
        if (c == '\r' || c == '\n' || c == ' ' || c == '=') continue;
        if (decVal(c) < 0) return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;
        sig++;
    }
    size_t need = (sig / 4) * 3 + (sig % 4 ? sig % 4 - 1 : 0);
    *olen = need;
    if (!dst || dlen < need) return MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;

    unsigned acc = 0;
    int bits = 0;
    size_t o = 0;
    for (size_t i = 0; i < slen; i++)
    {
        int v = decVal(src[i]);
        if (v < 0) continue; /* skip ws and '=' */
        acc = (acc << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            dst[o++] = (unsigned char)((acc >> bits) & 0xFF);
        }
    }
    *olen = o;
    return 0;
}

String base64::encode(const uint8_t *data, size_t len)
{
    size_t need = ((len + 2) / 3) * 4 + 1;
    std::string out(need, '\0');
    size_t olen = 0;
    if (mbedtls_base64_encode((unsigned char *)out.data(), need, &olen, data, len) != 0)
        return String();
    out.resize(olen);
    return String(std::move(out));
}
