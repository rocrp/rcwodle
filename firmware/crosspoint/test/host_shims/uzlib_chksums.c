/* host shim: uzlib checksum impls — upstream vendored uzlib without
 * adler32.c/crc32.c (the chksum path is dead code on target and gets
 * gc-sectioned away; the host linker still wants the symbols). Real
 * implementations, not stubs, in case a test ever exercises the path. */

#include <stdint.h>
#include <stddef.h>

unsigned int uzlib_adler32(const void *data, unsigned int length, unsigned int prev_sum)
{
    const unsigned char *buf = (const unsigned char *)data;
    unsigned int s1 = prev_sum & 0xFFFF;
    unsigned int s2 = (prev_sum >> 16) & 0xFFFF;
    while (length--)
    {
        s1 = (s1 + *buf++) % 65521;
        s2 = (s2 + s1) % 65521;
    }
    return (s2 << 16) | s1;
}

unsigned int uzlib_crc32(const void *data, unsigned int length, unsigned int crc)
{
    const unsigned char *buf = (const unsigned char *)data;
    crc = ~crc;
    while (length--)
    {
        crc ^= *buf++;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1) + 1));
    }
    return ~crc;
}
