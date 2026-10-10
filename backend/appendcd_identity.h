/* appendcd local optical identity guard, 2026-10-10. */
#ifndef APPENDCD_IDENTITY_H
#define APPENDCD_IDENTITY_H
#include <stdint.h>

static int appendcd_registry_matches(const char *expected, uint64_t actual) {
    uint64_t value = 0;
    unsigned digit;
    if (!expected || !*expected || !actual) return 0;
    while (*expected) {
        if (*expected < '0' || *expected > '9') return 0;
        digit = (unsigned)(*expected++ - '0');
        if (value > (UINT64_MAX - digit) / 10) return 0;
        value = value * 10 + digit;
    }
    return value == actual;
}
#endif
