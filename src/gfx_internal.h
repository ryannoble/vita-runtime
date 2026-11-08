#ifndef VR_GFX_INTERNAL_H
#define VR_GFX_INTERNAL_H

/* Backend-independent graphics helpers; not part of the public API. */

#include <stddef.h>
#include <stdint.h>

#include "vr/vr_gfx.h"

int  vr__mip_count(int width, int height);
int  vr__mip_dim(int size, int level);

/* Decodes one level of any vr_format to tightly packed RGBA8. */
void vr__decode_rgba8(vr_format format, int w, int h, const void *data, uint8_t *dst);

/* Reads one attribute as 4 floats; missing components are (0, 0, 0, 1). */
void   vr__fetch_attr(const uint8_t *vtx, const vr_vertex_attr *a, float out[4]);
size_t vr__attr_size(vr_attr_type type);

/* Checks a draw description; buffer sizes are 0 when not used. */
vr_result vr__validate_draw(const vr_draw_desc *d, size_t vb_size, size_t ib_size);
uint32_t  vr__index_at(const vr_draw_desc *d, const uint8_t *indices, uint32_t i);
uint32_t  vr__prim_count(vr_primitive prim, uint32_t vertex_or_index_count);
void      vr__prim_vertices(vr_primitive prim, uint32_t p, uint32_t out[3]);

#endif
