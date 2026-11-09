/* Shader cache file format: round trip, and rejection of anything that is
 * not exactly what was written for this key. */
#include <stdio.h>
#include <string.h>

#include "shader_cache.h"

static int g_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

int main(void)
{
    const char payload[] = "compiled program bytes";
    const uint64_t key = 0x1122334455667788ull;
    uint8_t file[128], copy[128];
    size_t n, got = 0;
    const void *p;

    /* FNV-1a 64 reference values. */
    CHECK(vr__fnv1a64("", 0, VR__FNV_SEED) == 0xcbf29ce484222325ull);
    CHECK(vr__fnv1a64("a", 1, VR__FNV_SEED) == 0xaf63dc4c8601ec8cull);
    /* Hashing in pieces equals hashing the whole. */
    CHECK(vr__fnv1a64("bc", 2, vr__fnv1a64("a", 1, VR__FNV_SEED)) ==
          vr__fnv1a64("abc", 3, VR__FNV_SEED));

    n = vr__cache_pack(key, payload, sizeof(payload), file, sizeof(file));
    CHECK(n == VR__CACHE_HEADER_SIZE + sizeof(payload));
    p = vr__cache_unpack(key, file, n, &got);
    CHECK(p && got == sizeof(payload) && !memcmp(p, payload, got));

    /* Too small an output buffer. */
    CHECK(vr__cache_pack(key, payload, sizeof(payload), file, VR__CACHE_HEADER_SIZE + 3) == 0);

    /* Wrong key (sources or compiler changed). */
    CHECK(!vr__cache_unpack(key + 1, file, n, &got));
    /* Truncated, or with trailing bytes. */
    CHECK(!vr__cache_unpack(key, file, n - 1, &got));
    CHECK(!vr__cache_unpack(key, file, VR__CACHE_HEADER_SIZE - 1, &got));
    memcpy(copy, file, n);
    copy[n] = 0;
    CHECK(!vr__cache_unpack(key, copy, n + 1, &got));
    /* Any flipped byte, in the header or the payload. */
    for (size_t i = 0; i < n; i++) {
        memcpy(copy, file, n);
        copy[i] ^= 0x40;
        if (vr__cache_unpack(key, copy, n, &got)) {
            fprintf(stderr, "corruption at byte %zu was accepted\n", i);
            g_failures++;
        }
    }
    /* Empty payloads are never valid. */
    n = vr__cache_pack(key, NULL, 0, file, sizeof(file));
    CHECK(n == VR__CACHE_HEADER_SIZE && !vr__cache_unpack(key, file, n, &got));
    CHECK(!vr__cache_unpack(key, NULL, 0, &got));

    if (g_failures) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all shader cache tests passed\n");
    return 0;
}
