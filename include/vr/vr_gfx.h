#ifndef VR_GFX_H
#define VR_GFX_H

#include <stddef.h>
#include <stdint.h>

#include "vr_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed-function style 3D API.
 *
 * Conventions (they match Direct3D, so a D3D-style renderer needs no
 * conversion beyond what is noted):
 * - Matrices are 16 floats, row-major, used with row vectors: v' = v * M.
 *   Translation lives in elements 12..14.
 * - Clip space depth is 0..1 (z/w). Triangles are clipped against the near
 *   plane; fragments outside 0..1 depth are discarded.
 * - Screen space has y pointing down. Winding is judged in screen space.
 * - Fragments are sampled at pixel centres (x + 0.5, y + 0.5) with a
 *   top-left fill rule. Direct3D 8/9 put pixel centres at integers; pass
 *   screen_offset = { 0.5, 0.5 } (times any screen_scale) to reproduce that,
 *   so a D3D coordinate of 0 lands on the centre of pixel 0 (the D3D shim
 *   does this).
 * - Colours are 0..1 floats in the API (RGBA order). */

typedef struct vr_buffer  vr_buffer;
typedef struct vr_texture vr_texture;

/* ---- Device and frames ---------------------------------------------------- */

typedef struct {
    int width;  /* back buffer size; 0 selects the backend default (960x544) */
    int height;
    int vsync;  /* nonzero waits for vertical blank on present */
} vr_gfx_desc;

vr_result vr_gfx_init(const vr_gfx_desc *desc);
void      vr_gfx_shutdown(void);
void      vr_gfx_get_size(int *width, int *height);

void vr_gfx_begin_frame(void);
void vr_gfx_end_frame(void); /* presents the back buffer */

enum {
    VR_CLEAR_COLOR = 1u << 0,
    VR_CLEAR_DEPTH = 1u << 1
};

/* Clears the current viewport rectangle. */
void vr_gfx_clear(unsigned flags, const float rgba[4], float depth);

/* min_z/max_z remap clip depth 0..1, like D3DVIEWPORT. */
void vr_gfx_set_viewport(int x, int y, int width, int height, float min_z, float max_z);

/* Copies back-buffer pixels as tightly packed RGBA8, top row first.
 * Backends without readback return VR_ERR_UNSUPPORTED. */
vr_result vr_gfx_read_pixels(int x, int y, int width, int height, void *rgba8);

/* ---- Buffers -------------------------------------------------------------- */

typedef enum { VR_BUFFER_VERTEX, VR_BUFFER_INDEX } vr_buffer_type;

/* data may be NULL (contents undefined until updated). */
vr_result vr_buffer_create(vr_buffer_type type, size_t size, const void *data, vr_buffer **out);
vr_result vr_buffer_update(vr_buffer *buf, size_t offset, const void *data, size_t size);
void      vr_buffer_destroy(vr_buffer *buf);

/* ---- Textures ------------------------------------------------------------- */

/* 8888 formats are byte orders. 16-bit formats are little-endian uint16
 * values with fields listed from the most significant bit (R5G6B5: red in
 * bits 15..11). DXT formats are standard S3TC/BC1-3 4x4 blocks. */
typedef enum {
    VR_FMT_RGBA8,
    VR_FMT_BGRA8,    /* = D3DFMT_A8R8G8B8 in memory */
    VR_FMT_BGRX8,    /* = D3DFMT_X8R8G8B8; alpha reads as 1 */
    VR_FMT_R5G6B5,
    VR_FMT_A1R5G5B5,
    VR_FMT_X1R5G5B5,
    VR_FMT_A4R4G4B4,
    VR_FMT_L8,       /* luminance, alpha 1 */
    VR_FMT_A8,       /* alpha, colour 0 */
    VR_FMT_A8L8,     /* little-endian uint16: alpha high byte */
    VR_FMT_DXT1,
    VR_FMT_DXT3,
    VR_FMT_DXT5,
    VR_FMT_COUNT
} vr_format;

typedef struct {
    int       width, height;
    int       levels; /* mip levels; 0 means a full chain down to 1x1 */
    vr_format format;
} vr_texture_desc;

vr_result vr_texture_create(const vr_texture_desc *desc, vr_texture **out);
/* Replaces one mip level. data is tightly packed: rows of
 * vr_format_row_bytes(format, level_width) bytes (block rows for DXT). */
vr_result vr_texture_update(vr_texture *tex, int level, const void *data);
void      vr_texture_destroy(vr_texture *tex);

/* Size helpers, usable without a device. */
size_t vr_format_row_bytes(vr_format format, int width);
size_t vr_format_level_bytes(vr_format format, int width, int height);
int    vr_format_is_compressed(vr_format format);

/* ---- Vertex layout -------------------------------------------------------- */

typedef enum {
    VR_ATTR_POSITION,   /* object space, transformed by vr_draw_desc.transform */
    VR_ATTR_POSITION_T, /* screen space x, y (pixels), z (0..1), rhw = 1/w */
    VR_ATTR_COLOR0,     /* diffuse */
    VR_ATTR_COLOR1,     /* specular (rgb added after texturing when enabled) */
    VR_ATTR_TEXCOORD0,
    VR_ATTR_TEXCOORD1,
    VR_ATTR_SEMANTIC_COUNT
} vr_attr_semantic;

typedef enum {
    VR_ATTR_FLOAT1,
    VR_ATTR_FLOAT2,
    VR_ATTR_FLOAT3,
    VR_ATTR_FLOAT4,
    VR_ATTR_UBYTE4_RGBA, /* normalized, bytes R,G,B,A */
    VR_ATTR_UBYTE4_BGRA  /* normalized, bytes B,G,R,A (= D3DCOLOR) */
} vr_attr_type;

#define VR_MAX_ATTRS 8

typedef struct {
    vr_attr_semantic semantic;
    vr_attr_type     type;
    uint32_t         offset; /* bytes from the start of the vertex */
} vr_vertex_attr;

typedef struct {
    int            count;
    uint32_t       stride;
    vr_vertex_attr attrs[VR_MAX_ATTRS];
} vr_vertex_layout;

/* ---- Render state ------------------------------------------------------- */

typedef enum {
    VR_CMP_NEVER, VR_CMP_LESS, VR_CMP_EQUAL, VR_CMP_LEQUAL,
    VR_CMP_GREATER, VR_CMP_NOTEQUAL, VR_CMP_GEQUAL, VR_CMP_ALWAYS
} vr_compare;

typedef enum {
    VR_BLEND_ZERO, VR_BLEND_ONE,
    VR_BLEND_SRC_COLOR, VR_BLEND_INV_SRC_COLOR,
    VR_BLEND_SRC_ALPHA, VR_BLEND_INV_SRC_ALPHA,
    VR_BLEND_DST_ALPHA, VR_BLEND_INV_DST_ALPHA,
    VR_BLEND_DST_COLOR, VR_BLEND_INV_DST_COLOR,
    VR_BLEND_SRC_ALPHA_SAT,
    VR_BLEND_CONSTANT, VR_BLEND_INV_CONSTANT
} vr_blend_factor;

typedef enum {
    VR_BLEND_OP_ADD, VR_BLEND_OP_SUBTRACT, VR_BLEND_OP_REV_SUBTRACT,
    VR_BLEND_OP_MIN, VR_BLEND_OP_MAX
} vr_blend_op;

typedef enum { VR_CULL_NONE, VR_CULL_CW, VR_CULL_CCW } vr_cull;

typedef enum { VR_FILTER_NEAREST, VR_FILTER_LINEAR } vr_filter;
typedef enum { VR_MIP_NONE, VR_MIP_NEAREST, VR_MIP_LINEAR } vr_mip_filter;
typedef enum { VR_ADDRESS_WRAP, VR_ADDRESS_CLAMP, VR_ADDRESS_MIRROR } vr_address;

/* Texture combiner, modelled on D3D texture stage states. */
typedef enum {
    VR_TOP_DISABLE,       /* this and later stages are skipped */
    VR_TOP_SELECT_ARG1,
    VR_TOP_SELECT_ARG2,
    VR_TOP_MODULATE,
    VR_TOP_MODULATE2X,
    VR_TOP_MODULATE4X,
    VR_TOP_ADD,
    VR_TOP_ADD_SIGNED,
    VR_TOP_ADD_SIGNED2X,
    VR_TOP_SUBTRACT,
    VR_TOP_ADD_SMOOTH,
    VR_TOP_BLEND_DIFFUSE_ALPHA,
    VR_TOP_BLEND_TEXTURE_ALPHA,
    VR_TOP_BLEND_FACTOR_ALPHA,
    VR_TOP_BLEND_CURRENT_ALPHA,
    VR_TOP_DOTPRODUCT3
} vr_texture_op;

typedef enum {
    VR_TARG_CURRENT,  /* output of the previous stage (diffuse for stage 0) */
    VR_TARG_DIFFUSE,
    VR_TARG_SPECULAR,
    VR_TARG_TEXTURE,
    VR_TARG_TFACTOR,
    VR_TARG_COMPLEMENT = 0x10,     /* modifier: 1 - x */
    VR_TARG_ALPHA_REPLICATE = 0x20 /* modifier: x.aaaa */
} vr_texture_arg;

typedef struct {
    vr_texture   *texture;    /* NULL samples opaque white */
    int           texcoord;   /* which TEXCOORDn to use */
    vr_filter     min_filter, mag_filter;
    vr_mip_filter mip_filter;
    vr_address    address_u, address_v;
    vr_texture_op color_op, alpha_op;
    int           color_arg1, color_arg2; /* vr_texture_arg | modifiers */
    int           alpha_arg1, alpha_arg2;
} vr_texture_stage;

#define VR_MAX_TEXTURE_STAGES 2

typedef struct {
    int             blend_enable;
    vr_blend_factor blend_src, blend_dst;
    vr_blend_op     blend_op;
    float           blend_constant[4];

    int        alpha_test_enable;
    vr_compare alpha_func;
    float      alpha_ref; /* 0..1 */

    int        depth_test_enable;
    int        depth_write_enable;
    vr_compare depth_func;
    float      depth_bias; /* added to window depth */

    vr_cull cull;
    unsigned color_write_mask; /* bits 0..3 = R,G,B,A */

    int   fog_enable; /* linear fog on clip w (eye depth for usual projections) */
    float fog_color[4];
    float fog_start, fog_end;

    int   specular_enable;
    float texture_factor[4]; /* VR_TARG_TFACTOR */

    vr_texture_stage stages[VR_MAX_TEXTURE_STAGES];
} vr_render_state;

/* Defaults match Direct3D: depth test LEQUAL with writes, CCW culling, no
 * blending, stage 0 = MODULATE(texture, diffuse) / SELECT_ARG1 alpha
 * (texture, current), stage 1 disabled. */
void vr_render_state_default(vr_render_state *state);

/* ---- Drawing ---------------------------------------------------------------- */

typedef enum {
    VR_PRIM_POINT_LIST,
    VR_PRIM_LINE_LIST,
    VR_PRIM_LINE_STRIP,
    VR_PRIM_TRIANGLE_LIST,
    VR_PRIM_TRIANGLE_STRIP,
    VR_PRIM_TRIANGLE_FAN
} vr_primitive;

typedef enum { VR_INDEX_16, VR_INDEX_32 } vr_index_type;

typedef struct {
    vr_primitive            primitive;
    const vr_vertex_layout *layout;

    /* Vertices come from vertex_buffer + vertex_offset, or from vertex_data
     * when vertex_buffer is NULL. vertex_count is the number of vertices
     * that may be referenced (from base_vertex). */
    const vr_buffer *vertex_buffer;
    size_t           vertex_offset;
    const void      *vertex_data;
    uint32_t         vertex_count;
    int32_t          base_vertex; /* added to every index */

    /* Indexed when index_count > 0: from index_buffer + index_offset, or
     * index_data when index_buffer is NULL. Otherwise vertices are drawn
     * in order and vertex_count gives the number used. */
    const vr_buffer *index_buffer;
    size_t           index_offset;
    const void      *index_data;
    vr_index_type    index_type;
    uint32_t         index_count;

    const float           *transform; /* world*view*proj; NULL = identity */
    const vr_render_state *state;     /* NULL = defaults */
    /* POSITION_T x/y are multiplied by screen_scale (0 means 1), then
     * screen_offset (pixels) is added to every projected vertex. */
    float screen_scale[2];
    float screen_offset[2];
} vr_draw_desc;

vr_result vr_gfx_draw(const vr_draw_desc *desc);

#ifdef __cplusplus
}
#endif

#endif
