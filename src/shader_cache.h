#ifndef VR_SHADER_CACHE_H
#define VR_SHADER_CACHE_H

/* File format for cached compiled shader programs. Backend independent and
 * testable on the host; the Vita backend does the file I/O.
 *
 *   offset  size  field
 *   0       4     magic "VRSC"
 *   4       4     format version (VR__CACHE_VERSION), little endian
 *   8       8     key (identifies the sources and the compiler)
 *   16      8     payload length
 *   24      8     FNV-1a 64 checksum of the payload
 *   32      n     payload (opaque program binary)
 *
 * A file is used only if every field matches, so stale, truncated or
 * corrupted files are rejected instead of being handed to the GPU driver. */

#include <stddef.h>
#include <stdint.h>

#define VR__CACHE_VERSION     1u
#define VR__CACHE_HEADER_SIZE 32u

/* FNV-1a 64; pass the previous result as seed to hash several pieces
 * (start with VR__FNV_SEED). */
#define VR__FNV_SEED 0xcbf29ce484222325ull
uint64_t vr__fnv1a64(const void *data, size_t size, uint64_t seed);

/* Writes header + payload to out. Returns the bytes written, or 0 if cap is
 * too small. */
size_t vr__cache_pack(uint64_t key, const void *payload, size_t size, void *out, size_t cap);

/* Validates a whole file against key. Returns a pointer to the payload inside
 * file and its size, or NULL if anything does not match. */
const void *vr__cache_unpack(uint64_t key, const void *file, size_t size, size_t *payload_size);

#endif
