#include <string.h>

#include "vr/vr_gfx.h"
#include "gfx_internal.h"

/* ---- Formats ---------------------------------------------------------------- */

int vr_format_is_compressed(vr_format format)
{
    return format == VR_FMT_DXT1 || format == VR_FMT_DXT3 || format == VR_FMT_DXT5;
}

static size_t bytes_per_pixel(vr_format format)
{
    switch (format) {
    case VR_FMT_RGBA8:
    case VR_FMT_BGRA8:
    case VR_FMT_BGRX8:
        return 4;
    case VR_FMT_R5G6B5:
    case VR_FMT_A1R5G5B5:
    case VR_FMT_X1R5G5B5:
    case VR_FMT_A4R4G4B4:
    case VR_FMT_A8L8:
        return 2;
    case VR_FMT_L8:
    case VR_FMT_A8:
        return 1;
    default:
        return 0;
    }
}

size_t vr_format_row_bytes(vr_format format, int width)
{
    if (width <= 0 || (unsigned)format >= VR_FMT_COUNT)
        return 0;
    if (vr_format_is_compressed(format))
        return (size_t)((width + 3) / 4) * (format == VR_FMT_DXT1 ? 8u : 16u);
    return (size_t)width * bytes_per_pixel(format);
}

size_t vr_format_level_bytes(vr_format format, int width, int height)
{
    if (height <= 0)
        return 0;
    if (vr_format_is_compressed(format))
        height = (height + 3) / 4;
    return vr_format_row_bytes(format, width) * (size_t)height;
}

int vr__mip_count(int width, int height)
{
    int n = 1;

    while (width > 1 || height > 1) {
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
        n++;
    }
    return n;
}

int vr__mip_dim(int size, int level)
{
    size >>= level;
    return size > 0 ? size : 1;
}

/* ---- Texture decoding --------------------------------------------------------- */

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint8_t expand(unsigned v, int bits)
{
    switch (bits) {
    case 1: return (uint8_t)(v ? 255 : 0);
    case 4: return (uint8_t)(v * 17);
    case 5: return (uint8_t)((v << 3) | (v >> 2));
    case 6: return (uint8_t)((v << 2) | (v >> 4));
    default: return (uint8_t)v;
    }
}

static void rgb565(uint16_t c, uint8_t *out)
{
    out[0] = expand((c >> 11) & 31u, 5);
    out[1] = expand((c >> 5) & 63u, 6);
    out[2] = expand(c & 31u, 5);
}

/* Decodes the colour half of a DXT block into 16 RGBA texels. */
static void dxt_color_block(const uint8_t *blk, int allow_1bit_alpha, uint8_t out[16][4])
{
    uint16_t c0 = rd16(blk), c1 = rd16(blk + 2);
    uint32_t bits = (uint32_t)blk[4] | ((uint32_t)blk[5] << 8) |
                    ((uint32_t)blk[6] << 16) | ((uint32_t)blk[7] << 24);
    uint8_t pal[4][4];
    int i, k;

    rgb565(c0, pal[0]);
    rgb565(c1, pal[1]);
    pal[0][3] = pal[1][3] = 255;
    if (c0 > c1 || !allow_1bit_alpha) {
        for (k = 0; k < 3; k++) {
            pal[2][k] = (uint8_t)((2 * pal[0][k] + pal[1][k] + 1) / 3);
            pal[3][k] = (uint8_t)((pal[0][k] + 2 * pal[1][k] + 1) / 3);
        }
        pal[2][3] = pal[3][3] = 255;
    } else {
        for (k = 0; k < 3; k++)
            pal[2][k] = (uint8_t)((pal[0][k] + pal[1][k]) / 2);
        pal[2][3] = 255;
        pal[3][0] = pal[3][1] = pal[3][2] = pal[3][3] = 0;
    }
    for (i = 0; i < 16; i++)
        memcpy(out[i], pal[(bits >> (2 * i)) & 3u], 4);
}

static void dxt5_alpha_block(const uint8_t *blk, uint8_t out[16][4])
{
    unsigned a0 = blk[0], a1 = blk[1];
    uint64_t bits = 0;
    uint8_t pal[8];
    int i;

    for (i = 0; i < 6; i++)
        bits |= (uint64_t)blk[2 + i] << (8 * i);
    pal[0] = (uint8_t)a0;
    pal[1] = (uint8_t)a1;
    if (a0 > a1) {
        for (i = 1; i < 7; i++)
            pal[i + 1] = (uint8_t)(((7 - i) * a0 + i * a1 + 3) / 7);
    } else {
        for (i = 1; i < 5; i++)
            pal[i + 1] = (uint8_t)(((5 - i) * a0 + i * a1 + 2) / 5);
        pal[6] = 0;
        pal[7] = 255;
    }
    for (i = 0; i < 16; i++)
        out[i][3] = pal[(bits >> (3 * i)) & 7u];
}

static void decode_dxt(vr_format format, int w, int h, const uint8_t *src, uint8_t *dst)
{
    int bx, by, i;
    size_t block_size = format == VR_FMT_DXT1 ? 8 : 16;

    for (by = 0; by < (h + 3) / 4; by++) {
        for (bx = 0; bx < (w + 3) / 4; bx++) {
            uint8_t texels[16][4];

            if (format == VR_FMT_DXT1) {
                dxt_color_block(src, 1, texels);
            } else {
                dxt_color_block(src + 8, 0, texels);
                if (format == VR_FMT_DXT3) {
                    for (i = 0; i < 16; i++)
                        texels[i][3] = expand((src[i / 2] >> (4 * (i & 1))) & 15u, 4);
                } else {
                    dxt5_alpha_block(src, texels);
                }
            }
            for (i = 0; i < 16; i++) {
                int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x < w && y < h)
                    memcpy(dst + ((size_t)y * (size_t)w + (size_t)x) * 4, texels[i], 4);
            }
            src += block_size;
        }
    }
}

void vr__decode_rgba8(vr_format format, int w, int h, const void *data, uint8_t *dst)
{
    const uint8_t *src = data;
    size_t i, n = (size_t)w * (size_t)h;

    if (vr_format_is_compressed(format)) {
        decode_dxt(format, w, h, src, dst);
        return;
    }
    for (i = 0; i < n; i++, dst += 4) {
        uint16_t v;

        switch (format) {
        case VR_FMT_RGBA8:
            memcpy(dst, src + i * 4, 4);
            break;
        case VR_FMT_BGRA8:
        case VR_FMT_BGRX8:
            dst[0] = src[i * 4 + 2];
            dst[1] = src[i * 4 + 1];
            dst[2] = src[i * 4 + 0];
            dst[3] = format == VR_FMT_BGRA8 ? src[i * 4 + 3] : 255;
            break;
        case VR_FMT_R5G6B5:
            rgb565(rd16(src + i * 2), dst);
            dst[3] = 255;
            break;
        case VR_FMT_A1R5G5B5:
        case VR_FMT_X1R5G5B5:
            v = rd16(src + i * 2);
            dst[0] = expand((v >> 10) & 31u, 5);
            dst[1] = expand((v >> 5) & 31u, 5);
            dst[2] = expand(v & 31u, 5);
            dst[3] = format == VR_FMT_A1R5G5B5 ? expand(v >> 15, 1) : 255;
            break;
        case VR_FMT_A4R4G4B4:
            v = rd16(src + i * 2);
            dst[0] = expand((v >> 8) & 15u, 4);
            dst[1] = expand((v >> 4) & 15u, 4);
            dst[2] = expand(v & 15u, 4);
            dst[3] = expand(v >> 12, 4);
            break;
        case VR_FMT_L8:
            dst[0] = dst[1] = dst[2] = src[i];
            dst[3] = 255;
            break;
        case VR_FMT_A8:
            dst[0] = dst[1] = dst[2] = 0;
            dst[3] = src[i];
            break;
        case VR_FMT_A8L8:
            dst[0] = dst[1] = dst[2] = src[i * 2];
            dst[3] = src[i * 2 + 1];
            break;
        default:
            memset(dst, 0, 4);
            break;
        }
    }
}

/* ---- Render state ------------------------------------------------------------- */

void vr_render_state_default(vr_render_state *s)
{
    int i;

    if (!s)
        return;
    memset(s, 0, sizeof(*s));
    s->blend_src = VR_BLEND_ONE;
    s->blend_dst = VR_BLEND_ZERO;
    s->blend_op = VR_BLEND_OP_ADD;
    s->alpha_func = VR_CMP_ALWAYS;
    s->depth_test_enable = 1;
    s->depth_write_enable = 1;
    s->depth_func = VR_CMP_LEQUAL;
    s->cull = VR_CULL_CCW;
    s->color_write_mask = 0xF;
    s->fog_end = 1.0f;
    for (i = 0; i < 4; i++)
        s->texture_factor[i] = 1.0f;
    for (i = 0; i < VR_MAX_TEXTURE_STAGES; i++) {
        vr_texture_stage *st = &s->stages[i];
        st->texcoord = i;
        st->min_filter = st->mag_filter = VR_FILTER_NEAREST;
        st->mip_filter = VR_MIP_NONE;
        st->address_u = st->address_v = VR_ADDRESS_WRAP;
        st->color_op = i == 0 ? VR_TOP_MODULATE : VR_TOP_DISABLE;
        st->alpha_op = i == 0 ? VR_TOP_SELECT_ARG1 : VR_TOP_DISABLE;
        st->color_arg1 = st->alpha_arg1 = VR_TARG_TEXTURE;
        st->color_arg2 = st->alpha_arg2 = VR_TARG_CURRENT;
    }
}

/* ---- Vertex fetch ----------------------------------------------------------- */

void vr__fetch_attr(const uint8_t *vtx, const vr_vertex_attr *a, float out[4])
{
    const uint8_t *p = vtx + a->offset;
    int n = 0, i;

    out[0] = out[1] = out[2] = 0.0f;
    out[3] = 1.0f;
    switch (a->type) {
    case VR_ATTR_FLOAT1: n = 1; break;
    case VR_ATTR_FLOAT2: n = 2; break;
    case VR_ATTR_FLOAT3: n = 3; break;
    case VR_ATTR_FLOAT4: n = 4; break;
    case VR_ATTR_UBYTE4_RGBA:
        for (i = 0; i < 4; i++)
            out[i] = p[i] / 255.0f;
        return;
    case VR_ATTR_UBYTE4_BGRA:
        out[0] = p[2] / 255.0f;
        out[1] = p[1] / 255.0f;
        out[2] = p[0] / 255.0f;
        out[3] = p[3] / 255.0f;
        return;
    }
    for (i = 0; i < n; i++)
        memcpy(&out[i], p + 4 * i, 4);
}

size_t vr__attr_size(vr_attr_type type)
{
    switch (type) {
    case VR_ATTR_FLOAT1: return 4;
    case VR_ATTR_FLOAT2: return 8;
    case VR_ATTR_FLOAT3: return 12;
    case VR_ATTR_FLOAT4: return 16;
    default: return 4;
    }
}

/* ---- Primitive assembly -------------------------------------------------------- */

vr_result vr__validate_draw(const vr_draw_desc *d, size_t vb_size, size_t ib_size)
{
    int i;
    size_t isz;

    if (!d || !d->layout || d->layout->count < 0 || d->layout->count > VR_MAX_ATTRS)
        return VR_ERR_INVALID_ARG;
    if ((unsigned)d->primitive > VR_PRIM_TRIANGLE_FAN)
        return VR_ERR_INVALID_ARG;
    if (!d->vertex_buffer && !d->vertex_data && d->vertex_count)
        return VR_ERR_INVALID_ARG;
    for (i = 0; i < d->layout->count; i++) {
        const vr_vertex_attr *a = &d->layout->attrs[i];
        if ((unsigned)a->semantic >= VR_ATTR_SEMANTIC_COUNT ||
            (unsigned)a->type > VR_ATTR_UBYTE4_BGRA ||
            a->offset + vr__attr_size(a->type) > d->layout->stride)
            return VR_ERR_INVALID_ARG;
    }
    if (d->base_vertex < 0)
        return VR_ERR_INVALID_ARG;
    if (d->vertex_buffer) {
        uint64_t need = (uint64_t)d->vertex_offset +
                        ((uint64_t)d->base_vertex + d->vertex_count) * d->layout->stride;
        if (need > vb_size)
            return VR_ERR_INVALID_ARG;
    }
    if (d->index_count) {
        isz = d->index_type == VR_INDEX_32 ? 4 : 2;
        if (!d->index_buffer && !d->index_data)
            return VR_ERR_INVALID_ARG;
        if (d->index_buffer &&
            (uint64_t)d->index_offset + (uint64_t)d->index_count * isz > ib_size)
            return VR_ERR_INVALID_ARG;
    }
    return VR_OK;
}

uint32_t vr__index_at(const vr_draw_desc *d, const uint8_t *indices, uint32_t i)
{
    if (!d->index_count)
        return i;
    if (d->index_type == VR_INDEX_32) {
        uint32_t v;
        memcpy(&v, indices + 4 * (size_t)i, 4);
        return v;
    }
    return rd16(indices + 2 * (size_t)i);
}

uint32_t vr__prim_count(vr_primitive prim, uint32_t n)
{
    switch (prim) {
    case VR_PRIM_POINT_LIST: return n;
    case VR_PRIM_LINE_LIST: return n / 2;
    case VR_PRIM_LINE_STRIP: return n >= 2 ? n - 1 : 0;
    case VR_PRIM_TRIANGLE_LIST: return n / 3;
    case VR_PRIM_TRIANGLE_STRIP:
    case VR_PRIM_TRIANGLE_FAN: return n >= 3 ? n - 2 : 0;
    }
    return 0;
}

/* Returns the stream positions of primitive p's vertices, keeping a
 * consistent winding for strips (odd triangles are swapped like D3D). */
void vr__prim_vertices(vr_primitive prim, uint32_t p, uint32_t out[3])
{
    switch (prim) {
    case VR_PRIM_POINT_LIST:
        out[0] = out[1] = out[2] = p;
        break;
    case VR_PRIM_LINE_LIST:
        out[0] = 2 * p;
        out[1] = out[2] = 2 * p + 1;
        break;
    case VR_PRIM_LINE_STRIP:
        out[0] = p;
        out[1] = out[2] = p + 1;
        break;
    case VR_PRIM_TRIANGLE_LIST:
        out[0] = 3 * p;
        out[1] = 3 * p + 1;
        out[2] = 3 * p + 2;
        break;
    case VR_PRIM_TRIANGLE_STRIP:
        if (p & 1) {
            out[0] = p + 1;
            out[1] = p;
        } else {
            out[0] = p;
            out[1] = p + 1;
        }
        out[2] = p + 2;
        break;
    case VR_PRIM_TRIANGLE_FAN:
        out[0] = 0;
        out[1] = p + 1;
        out[2] = p + 2;
        break;
    }
}
