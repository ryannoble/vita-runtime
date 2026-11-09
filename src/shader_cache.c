#include <string.h>

#include "shader_cache.h"

uint64_t vr__fnv1a64(const void *data, size_t size, uint64_t seed)
{
    const uint8_t *p = data;
    uint64_t h = seed;
    size_t i;

    for (i = 0; i < size; i++) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

static void put_u32(uint8_t *p, uint32_t v)
{
    int i;
    for (i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

static void put_u64(uint8_t *p, uint64_t v)
{
    int i;
    for (i = 0; i < 8; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t get_u64(const uint8_t *p)
{
    return (uint64_t)get_u32(p) | ((uint64_t)get_u32(p + 4) << 32);
}

size_t vr__cache_pack(uint64_t key, const void *payload, size_t size, void *out, size_t cap)
{
    uint8_t *o = out;

    if (!out || (!payload && size) || cap < VR__CACHE_HEADER_SIZE ||
        size > cap - VR__CACHE_HEADER_SIZE)
        return 0;
    memcpy(o, "VRSC", 4);
    put_u32(o + 4, VR__CACHE_VERSION);
    put_u64(o + 8, key);
    put_u64(o + 16, (uint64_t)size);
    put_u64(o + 24, vr__fnv1a64(payload, size, VR__FNV_SEED));
    if (size)
        memcpy(o + VR__CACHE_HEADER_SIZE, payload, size);
    return VR__CACHE_HEADER_SIZE + size;
}

const void *vr__cache_unpack(uint64_t key, const void *file, size_t size, size_t *payload_size)
{
    const uint8_t *f = file;
    uint64_t n;

    if (!file || size < VR__CACHE_HEADER_SIZE || memcmp(f, "VRSC", 4) ||
        get_u32(f + 4) != VR__CACHE_VERSION || get_u64(f + 8) != key)
        return NULL;
    n = get_u64(f + 16);
    if (n == 0 || n != (uint64_t)(size - VR__CACHE_HEADER_SIZE))
        return NULL;
    if (get_u64(f + 24) != vr__fnv1a64(f + VR__CACHE_HEADER_SIZE, (size_t)n, VR__FNV_SEED))
        return NULL;
    if (payload_size)
        *payload_size = (size_t)n;
    return f + VR__CACHE_HEADER_SIZE;
}
