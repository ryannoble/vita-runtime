/* Pixel-exact tests for vr_gfx, run against the host reference rasterizer. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vr/vr_gfx.h"
#include "vr/vr_platform.h"
#include "gfx_internal.h"

static int g_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

#define W 16
#define H 16

typedef struct {
    float x, y, z, rhw;
    uint8_t bgra[4];
    float u, v;
} tl_vertex; /* like D3DFVF_XYZRHW | DIFFUSE | TEX1 */

static const vr_vertex_layout tl_layout = {
    3, sizeof(tl_vertex),
    { { VR_ATTR_POSITION_T, VR_ATTR_FLOAT4, 0 },
      { VR_ATTR_COLOR0, VR_ATTR_UBYTE4_BGRA, 16 },
      { VR_ATTR_TEXCOORD0, VR_ATTR_FLOAT2, 20 } }
};

static uint8_t g_px[W * H * 4];

static void read_all(void)
{
    CHECK(vr_gfx_read_pixels(0, 0, W, H, g_px) == VR_OK);
}

static const uint8_t *px(int x, int y)
{
    return g_px + ((size_t)y * W + (size_t)x) * 4;
}

static int px_is(int x, int y, int r, int g, int b, int a)
{
    const uint8_t *p = px(x, y);
    return abs(p[0] - r) <= 1 && abs(p[1] - g) <= 1 && abs(p[2] - b) <= 1 && abs(p[3] - a) <= 1;
}

static void clear(float r, float g, float b, float a)
{
    const float c[4] = { r, g, b, a };
    vr_gfx_clear(VR_CLEAR_COLOR | VR_CLEAR_DEPTH, c, 1.0f);
}

static tl_vertex tv(float x, float y, float z, uint32_t argb, float u, float v)
{
    tl_vertex t;
    t.x = x;
    t.y = y;
    t.z = z;
    t.rhw = 1.0f;
    t.bgra[0] = (uint8_t)argb;
    t.bgra[1] = (uint8_t)(argb >> 8);
    t.bgra[2] = (uint8_t)(argb >> 16);
    t.bgra[3] = (uint8_t)(argb >> 24);
    t.u = u;
    t.v = v;
    return t;
}

/* Quad x0..x1, y0..y1 as two clockwise triangles (a triangle list). */
static void quad(tl_vertex out[6], float x0, float y0, float x1, float y1, float z, uint32_t c)
{
    out[0] = tv(x0, y0, z, c, 0, 0);
    out[1] = tv(x1, y0, z, c, 1, 0);
    out[2] = tv(x0, y1, z, c, 0, 1);
    out[3] = tv(x1, y0, z, c, 1, 0);
    out[4] = tv(x1, y1, z, c, 1, 1);
    out[5] = tv(x0, y1, z, c, 0, 1);
}

static vr_result draw_tl(const tl_vertex *v, uint32_t n, vr_primitive prim, const vr_render_state *rs)
{
    vr_draw_desc d;
    memset(&d, 0, sizeof(d));
    d.primitive = prim;
    d.layout = &tl_layout;
    d.vertex_data = v;
    d.vertex_count = n;
    d.state = rs;
    return vr_gfx_draw(&d);
}

static void untextured(vr_render_state *rs)
{
    vr_render_state_default(rs);
    rs->stages[0].color_op = VR_TOP_SELECT_ARG1;
    rs->stages[0].color_arg1 = VR_TARG_DIFFUSE;
    rs->stages[0].alpha_op = VR_TOP_SELECT_ARG1;
    rs->stages[0].alpha_arg1 = VR_TARG_DIFFUSE;
}

static void test_formats(void)
{
    CHECK(vr_format_row_bytes(VR_FMT_BGRA8, 10) == 40);
    CHECK(vr_format_row_bytes(VR_FMT_R5G6B5, 3) == 6);
    CHECK(vr_format_level_bytes(VR_FMT_DXT1, 8, 8) == 32);
    CHECK(vr_format_level_bytes(VR_FMT_DXT5, 1, 1) == 16);
    CHECK(vr_format_level_bytes(VR_FMT_DXT3, 5, 9) == 2 * 3 * 16);
    CHECK(vr_format_is_compressed(VR_FMT_DXT5));
    CHECK(!vr_format_is_compressed(VR_FMT_L8));
    CHECK(vr__mip_count(256, 64) == 9);
    CHECK(vr__mip_count(1, 1) == 1);
}

static void test_decode(void)
{
    /* DXT1, 4-colour mode: c0 = red, c1 = blue, texel i uses index i % 4. */
    const uint8_t dxt1[8] = { 0x00, 0xF8, 0x1F, 0x00, 0xE4, 0xE4, 0xE4, 0xE4 };
    /* DXT1, 3-colour mode (c0 <= c1): index 3 is transparent black. */
    const uint8_t dxt1a[8] = { 0x1F, 0x00, 0x00, 0xF8, 0xFF, 0xFF, 0xFF, 0xFF };
    uint8_t dxt5[16] = { 255, 0, 0, 0, 0, 0, 0, 0 };
    const uint8_t r565[2] = { 0x00, 0xF8 }, a4[2] = { 0x0F, 0xF0 };
    const uint8_t bgra[4] = { 1, 2, 3, 4 };
    uint8_t out[16 * 4];

    vr__decode_rgba8(VR_FMT_DXT1, 4, 4, dxt1, out);
    /* 0xE4 = indices 0,1,2,3 for texels 0..3 */
    CHECK(out[0] == 255 && out[1] == 0 && out[2] == 0 && out[3] == 255);
    CHECK(out[4] == 0 && out[6] == 255);
    CHECK(out[8] == 170 && out[10] == 85);
    CHECK(out[12] == 85 && out[14] == 170);

    vr__decode_rgba8(VR_FMT_DXT1, 4, 4, dxt1a, out);
    CHECK(out[0] == 0 && out[1] == 0 && out[2] == 0 && out[3] == 0);

    /* DXT5: a0 = 255, a1 = 0; texel 1 uses index 2 -> (6*255 + 3) / 7. */
    dxt5[2] = 2 << 3;
    memcpy(dxt5 + 8, dxt1, 8);
    vr__decode_rgba8(VR_FMT_DXT5, 4, 4, dxt5, out);
    CHECK(out[3] == 255 && out[7] == 219 && out[11] == 255);

    vr__decode_rgba8(VR_FMT_R5G6B5, 1, 1, r565, out);
    CHECK(out[0] == 255 && out[1] == 0 && out[2] == 0 && out[3] == 255);
    vr__decode_rgba8(VR_FMT_A4R4G4B4, 1, 1, a4, out);
    CHECK(out[0] == 0 && out[1] == 0 && out[2] == 255 && out[3] == 255);
    vr__decode_rgba8(VR_FMT_BGRA8, 1, 1, bgra, out);
    CHECK(out[0] == 3 && out[1] == 2 && out[2] == 1 && out[3] == 4);
}

static void test_clear_and_viewport(void)
{
    clear(1, 0, 0, 1);
    vr_gfx_set_viewport(4, 4, 4, 4, 0, 1);
    clear(0, 0, 1, 1);
    vr_gfx_set_viewport(0, 0, W, H, 0, 1);
    read_all();
    CHECK(px_is(0, 0, 255, 0, 0, 255));
    CHECK(px_is(4, 4, 0, 0, 255, 255));
    CHECK(px_is(7, 7, 0, 0, 255, 255));
    CHECK(px_is(8, 8, 255, 0, 0, 255));
    CHECK(vr_gfx_read_pixels(0, 0, W + 1, 1, g_px) == VR_ERR_INVALID_ARG);
}

/* Two triangles sharing an edge must cover each pixel exactly once. */
static void test_fill_rule(void)
{
    vr_render_state rs;
    tl_vertex v[6];
    int x, y, ok = 1;

    untextured(&rs);
    rs.blend_enable = 1;
    rs.blend_src = VR_BLEND_ONE;
    rs.blend_dst = VR_BLEND_ONE;
    rs.depth_test_enable = 0;
    clear(0, 0, 0, 0);
    quad(v, 2, 2, 6, 6, 0.5f, 0x40404040u);
    CHECK(draw_tl(v, 6, VR_PRIM_TRIANGLE_LIST, &rs) == VR_OK);
    read_all();
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            int in = x >= 2 && x < 6 && y >= 2 && y < 6;
            if (px(x, y)[0] != (in ? 0x40 : 0))
                ok = 0;
        }
    CHECK(ok);
}

static void test_cull_and_depth(void)
{
    vr_render_state rs;
    tl_vertex v[6], ccw[3];

    untextured(&rs);
    clear(0, 0, 0, 1);
    /* Counter-clockwise on screen: culled by the default CULL_CCW. */
    ccw[0] = tv(0, 0, 0.5f, 0xFFFFFFFFu, 0, 0);
    ccw[1] = tv(0, 8, 0.5f, 0xFFFFFFFFu, 0, 0);
    ccw[2] = tv(8, 0, 0.5f, 0xFFFFFFFFu, 0, 0);
    draw_tl(ccw, 3, VR_PRIM_TRIANGLE_LIST, &rs);
    read_all();
    CHECK(px_is(1, 1, 0, 0, 0, 255));
    rs.cull = VR_CULL_NONE;
    draw_tl(ccw, 3, VR_PRIM_TRIANGLE_LIST, &rs);
    read_all();
    CHECK(px_is(1, 1, 255, 255, 255, 255));

    clear(0, 0, 0, 1);
    rs.cull = VR_CULL_CCW;
    quad(v, 0, 0, 8, 8, 0.2f, 0xFFFF0000u);
    draw_tl(v, 6, VR_PRIM_TRIANGLE_LIST, &rs);
    quad(v, 0, 0, 8, 8, 0.6f, 0xFF00FF00u);
    draw_tl(v, 6, VR_PRIM_TRIANGLE_LIST, &rs);
    read_all();
    CHECK(px_is(3, 3, 255, 0, 0, 255));
    rs.depth_func = VR_CMP_ALWAYS;
    draw_tl(v, 6, VR_PRIM_TRIANGLE_LIST, &rs);
    read_all();
    CHECK(px_is(3, 3, 0, 255, 0, 255));
}

static void test_texture_modulate(void)
{
    const uint8_t texels[2 * 2 * 4] = {
        255, 0, 0, 255,   0, 255, 0, 255,
        0, 0, 255, 255,   255, 255, 255, 128
    };
    vr_texture_desc td = { 2, 2, 1, VR_FMT_RGBA8 };
    vr_render_state rs;
    vr_texture *tex = NULL;
    tl_vertex v[6];

    CHECK(vr_texture_create(&td, &tex) == VR_OK);
    CHECK(vr_texture_update(tex, 0, texels) == VR_OK);
    CHECK(vr_texture_update(tex, 1, texels) == VR_ERR_INVALID_ARG);
    vr_render_state_default(&rs);
    rs.stages[0].texture = tex;
    clear(0, 0, 0, 1);
    quad(v, 0, 0, 4, 4, 0.5f, 0xFF808080u); /* 50% grey modulates */
    draw_tl(v, 6, VR_PRIM_TRIANGLE_LIST, &rs);
    read_all();
    CHECK(px_is(0, 0, 128, 0, 0, 255));
    CHECK(px_is(3, 0, 0, 128, 0, 255));
    CHECK(px_is(0, 3, 0, 0, 128, 255));
    CHECK(px_is(3, 3, 128, 128, 128, 128));

    /* Alpha test drops the half-transparent texel. */
    clear(0, 0, 0, 1);
    rs.alpha_test_enable = 1;
    rs.alpha_func = VR_CMP_GREATER;
    rs.alpha_ref = 0.75f;
    draw_tl(v, 6, VR_PRIM_TRIANGLE_LIST, &rs);
    read_all();
    CHECK(px_is(3, 3, 0, 0, 0, 255));
    CHECK(px_is(0, 0, 128, 0, 0, 255));
    vr_texture_destroy(tex);
}

static void test_blend_and_fog(void)
{
    vr_render_state rs;
    tl_vertex v[6];

    untextured(&rs);
    rs.depth_test_enable = 0;
    rs.blend_enable = 1;
    rs.blend_src = VR_BLEND_SRC_ALPHA;
    rs.blend_dst = VR_BLEND_INV_SRC_ALPHA;
    clear(0, 0, 1, 1);
    quad(v, 0, 0, 4, 4, 0.5f, 0x80FF0000u);
    draw_tl(v, 6, VR_PRIM_TRIANGLE_LIST, &rs);
    read_all();
    CHECK(px_is(1, 1, 128, 0, 127, 192));

    /* Fog: rhw = 0.1 -> w = 10, past fog_end, so pure fog colour. */
    untextured(&rs);
    rs.fog_enable = 1;
    rs.fog_start = 1;
    rs.fog_end = 5;
    rs.fog_color[1] = 1;
    clear(0, 0, 0, 1);
    quad(v, 0, 0, 4, 4, 0.5f, 0xFFFF0000u);
    for (int i = 0; i < 6; i++)
        v[i].rhw = 0.1f;
    draw_tl(v, 6, VR_PRIM_TRIANGLE_LIST, &rs);
    read_all();
    CHECK(px_is(1, 1, 0, 255, 0, 255));
}

static void test_mipmaps(void)
{
    uint8_t l0[4 * 4 * 4], l1[2 * 2 * 4], l2[4];
    vr_texture_desc td = { 4, 4, 0, VR_FMT_RGBA8 };
    vr_render_state rs;
    vr_texture *tex = NULL;
    tl_vertex v[6];
    int i;

    for (i = 0; i < 16; i++)
        memcpy(l0 + 4 * i, "\xff\x00\x00\xff", 4);
    for (i = 0; i < 4; i++)
        memcpy(l1 + 4 * i, "\x00\xff\x00\xff", 4);
    memcpy(l2, "\x00\x00\xff\xff", 4);
    CHECK(vr_texture_create(&td, &tex) == VR_OK);
    vr_texture_update(tex, 0, l0);
    vr_texture_update(tex, 1, l1);
    vr_texture_update(tex, 2, l2);
    vr_render_state_default(&rs);
    rs.stages[0].texture = tex;
    rs.stages[0].mip_filter = VR_MIP_NEAREST;
    clear(0, 0, 0, 1);
    quad(v, 0, 0, 1, 1, 0.5f, 0xFFFFFFFFu); /* 4x4 texels -> 1 pixel: level 2 */
    draw_tl(v, 6, VR_PRIM_TRIANGLE_LIST, &rs);
    quad(v, 4, 0, 6, 2, 0.5f, 0xFFFFFFFFu); /* 2x minification: level 1 */
    draw_tl(v, 6, VR_PRIM_TRIANGLE_LIST, &rs);
    quad(v, 8, 0, 12, 4, 0.5f, 0xFFFFFFFFu); /* 1:1: level 0 */
    draw_tl(v, 6, VR_PRIM_TRIANGLE_LIST, &rs);
    read_all();
    CHECK(px_is(0, 0, 0, 0, 255, 255));
    CHECK(px_is(4, 0, 0, 255, 0, 255));
    CHECK(px_is(9, 1, 255, 0, 0, 255));
    vr_texture_destroy(tex);
}

/* Transformed geometry with an orthographic-style matrix, buffers and indices. */
static void test_transformed_indexed(void)
{
    static const float verts[4][3] = { { -1, 1, 0.5f }, { 1, 1, 0.5f }, { 1, -1, 0.5f }, { -1, -1, 0.5f } };
    static const uint16_t idx[6] = { 0, 1, 3, 1, 2, 3 };
    /* Scale by 0.5: covers the centre 8x8 of the 16x16 target. */
    static const float m[16] = { 0.5f, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    vr_vertex_layout layout = { 1, 12, { { VR_ATTR_POSITION, VR_ATTR_FLOAT3, 0 } } };
    vr_buffer *vb = NULL, *ib = NULL;
    vr_render_state rs;
    vr_draw_desc d;

    CHECK(vr_buffer_create(VR_BUFFER_VERTEX, sizeof(verts), verts, &vb) == VR_OK);
    CHECK(vr_buffer_create(VR_BUFFER_INDEX, sizeof(idx), idx, &ib) == VR_OK);
    CHECK(vr_buffer_update(vb, 44, verts, 8) == VR_ERR_INVALID_ARG);
    untextured(&rs);
    memset(&d, 0, sizeof(d));
    d.primitive = VR_PRIM_TRIANGLE_LIST;
    d.layout = &layout;
    d.vertex_buffer = vb;
    d.vertex_count = 4;
    d.index_buffer = ib;
    d.index_count = 6;
    d.transform = m;
    d.state = &rs;
    clear(0, 0, 0, 1);
    CHECK(vr_gfx_draw(&d) == VR_OK);
    read_all();
    CHECK(px_is(4, 4, 255, 255, 255, 255));
    CHECK(px_is(11, 11, 255, 255, 255, 255));
    CHECK(px_is(3, 3, 0, 0, 0, 255));
    CHECK(px_is(12, 12, 0, 0, 0, 255));

    /* Validation: indices past vertex_count, and a too-small buffer. */
    d.vertex_count = 3;
    CHECK(vr_gfx_draw(&d) == VR_ERR_INVALID_ARG);
    d.vertex_count = 5;
    CHECK(vr_gfx_draw(&d) == VR_ERR_INVALID_ARG);
    layout.attrs[0].offset = 4;
    d.vertex_count = 4;
    CHECK(vr_gfx_draw(&d) == VR_ERR_INVALID_ARG);
    vr_buffer_destroy(vb);
    vr_buffer_destroy(ib);
}

/* A triangle crossing the near plane must be clipped, not dropped. */
static void test_near_clip(void)
{
    static const float verts[3][3] = { { -1, -1, 0.5f }, { 0, 1, -0.5f }, { 1, -1, 0.5f } };
    vr_vertex_layout layout = { 1, 12, { { VR_ATTR_POSITION, VR_ATTR_FLOAT3, 0 } } };
    vr_render_state rs;
    vr_draw_desc d;

    untextured(&rs);
    rs.cull = VR_CULL_NONE;
    memset(&d, 0, sizeof(d));
    d.primitive = VR_PRIM_TRIANGLE_LIST;
    d.layout = &layout;
    d.vertex_data = verts;
    d.vertex_count = 3;
    d.state = &rs;
    clear(0, 0, 0, 1);
    CHECK(vr_gfx_draw(&d) == VR_OK);
    read_all();
    CHECK(px_is(8, 14, 255, 255, 255, 255)); /* near the bottom edge: z > 0 */
    CHECK(px_is(8, 1, 0, 0, 0, 255));        /* near the apex: z < 0, clipped */
}

static void test_strip_fan(void)
{
    vr_render_state rs;
    tl_vertex strip[4], fan[4];

    untextured(&rs);
    clear(0, 0, 0, 1);
    /* Strip 0-1-2, 2-1-3 (swapped): both clockwise, so both drawn. */
    strip[0] = tv(0, 0, 0.5f, 0xFFFFFFFFu, 0, 0);
    strip[1] = tv(4, 0, 0.5f, 0xFFFFFFFFu, 0, 0);
    strip[2] = tv(0, 4, 0.5f, 0xFFFFFFFFu, 0, 0);
    strip[3] = tv(4, 4, 0.5f, 0xFFFFFFFFu, 0, 0);
    CHECK(draw_tl(strip, 4, VR_PRIM_TRIANGLE_STRIP, &rs) == VR_OK);
    fan[0] = tv(8, 8, 0.5f, 0xFFFF0000u, 0, 0);
    fan[1] = tv(12, 8, 0.5f, 0xFFFF0000u, 0, 0);
    fan[2] = tv(12, 12, 0.5f, 0xFFFF0000u, 0, 0);
    fan[3] = tv(8, 12, 0.5f, 0xFFFF0000u, 0, 0);
    CHECK(draw_tl(fan, 4, VR_PRIM_TRIANGLE_FAN, &rs) == VR_OK);
    read_all();
    CHECK(px_is(0, 0, 255, 255, 255, 255));
    CHECK(px_is(3, 3, 255, 255, 255, 255));
    CHECK(px_is(8, 8, 255, 0, 0, 255));
    CHECK(px_is(11, 11, 255, 0, 0, 255));
}

static void test_lines_points(void)
{
    vr_render_state rs;
    tl_vertex v[2];

    untextured(&rs);
    clear(0, 0, 0, 1);
    v[0] = tv(0, 2.5f, 0.5f, 0xFFFFFFFFu, 0, 0);
    v[1] = tv(5, 2.5f, 0.5f, 0xFFFFFFFFu, 0, 0);
    CHECK(draw_tl(v, 2, VR_PRIM_LINE_LIST, &rs) == VR_OK);
    v[0] = tv(10.5f, 10.5f, 0.5f, 0xFF00FF00u, 0, 0);
    CHECK(draw_tl(v, 1, VR_PRIM_POINT_LIST, &rs) == VR_OK);
    read_all();
    CHECK(px_is(0, 2, 255, 255, 255, 255));
    CHECK(px_is(4, 2, 255, 255, 255, 255));
    CHECK(px_is(5, 2, 0, 0, 0, 255)); /* last pixel is not drawn */
    CHECK(px_is(10, 10, 0, 255, 0, 255));
}

/* Enormous or non-finite coordinates must be clipped cheaply, not looped over. */
static void test_huge_coordinates(void)
{
    vr_render_state rs;
    tl_vertex tri[3], line[2];
    uint64_t t0 = vr_time_us();

    untextured(&rs);
    rs.cull = VR_CULL_NONE;
    clear(0, 0, 0, 1);
    tri[0] = tv(-1e10f, -1e10f, 0.5f, 0xFFFFFFFFu, 0, 0);
    tri[1] = tv(2e10f, -1e10f, 0.5f, 0xFFFFFFFFu, 0, 0);
    tri[2] = tv(-1e10f, 2e10f, 0.5f, 0xFFFFFFFFu, 0, 0);
    CHECK(draw_tl(tri, 3, VR_PRIM_TRIANGLE_LIST, &rs) == VR_OK);
    read_all();
    CHECK(px_is(8, 8, 255, 255, 255, 255)); /* covers the whole target */
    rs.depth_test_enable = 0; /* the huge triangle's depth is only ~0.5 */
    line[0] = tv(-3e9f, 4.5f, 0.5f, 0xFF00FF00u, 1e20f, 0);
    line[1] = tv(3e9f, 4.5f, 0.5f, 0xFF00FF00u, NAN, 0);
    CHECK(draw_tl(line, 2, VR_PRIM_LINE_LIST, &rs) == VR_OK);
    read_all();
    CHECK(px_is(8, 4, 0, 255, 0, 255));
    line[0] = tv(NAN, 1e30f, 0.5f, 0xFF00FF00u, 0, 0);
    CHECK(draw_tl(line, 1, VR_PRIM_POINT_LIST, &rs) == VR_OK);
    CHECK(vr_time_us() - t0 < 2000000u);
}

int main(void)
{
    vr_gfx_desc desc = { W, H, 0 };
    int w = 0, h = 0;

    test_formats();
    test_decode();
    CHECK(vr_gfx_init(&desc) == VR_OK);
    vr_gfx_get_size(&w, &h);
    CHECK(w == W && h == H);
    vr_gfx_begin_frame();
    test_clear_and_viewport();
    test_fill_rule();
    test_cull_and_depth();
    test_texture_modulate();
    test_blend_and_fog();
    test_mipmaps();
    test_transformed_indexed();
    test_near_clip();
    test_strip_fan();
    test_lines_points();
    test_huge_coordinates();
    vr_gfx_end_frame();
    vr_gfx_shutdown();

    if (g_failures) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all gfx tests passed\n");
    return 0;
}
