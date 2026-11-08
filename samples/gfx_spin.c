/* Original vr_gfx sample: a spinning, vertex-coloured, textured square over
 * a pretransformed backdrop. Runs for about five seconds. No game data. */
#include <math.h>
#include <string.h>

#include "vr/vr_gfx.h"
#include "vr/vr_platform.h"

typedef struct {
    float x, y, z;
    uint8_t rgba[4];
    float u, v;
} vertex;

typedef struct {
    float x, y, z, rhw;
    uint8_t rgba[4];
} screen_vertex;

int main(void)
{
    static const vr_vertex_layout layout = {
        3, sizeof(vertex),
        { { VR_ATTR_POSITION, VR_ATTR_FLOAT3, 0 },
          { VR_ATTR_COLOR0, VR_ATTR_UBYTE4_RGBA, 12 },
          { VR_ATTR_TEXCOORD0, VR_ATTR_FLOAT2, 16 } }
    };
    static const vr_vertex_layout screen_layout = {
        2, sizeof(screen_vertex),
        { { VR_ATTR_POSITION_T, VR_ATTR_FLOAT4, 0 },
          { VR_ATTR_COLOR0, VR_ATTR_UBYTE4_RGBA, 16 } }
    };
    static const vertex quad[4] = {
        { -0.5f, 0.5f, 0, { 255, 64, 64, 255 }, 0, 0 },
        { 0.5f, 0.5f, 0, { 64, 255, 64, 255 }, 4, 0 },
        { -0.5f, -0.5f, 0, { 64, 64, 255, 255 }, 0, 4 },
        { 0.5f, -0.5f, 0, { 255, 255, 64, 255 }, 4, 4 }
    };
    static const screen_vertex backdrop[4] = {
        { 0, 0, 0.99f, 1, { 20, 20, 40, 255 } },
        { 960, 0, 0.99f, 1, { 20, 20, 40, 255 } },
        { 0, 544, 0.99f, 1, { 60, 20, 60, 255 } },
        { 960, 544, 0.99f, 1, { 60, 20, 60, 255 } }
    };
    vr_platform_desc pdesc = { "vr-gfx-spin", NULL };
    vr_gfx_desc gdesc = { 0, 0, 1 };
    vr_texture_desc tdesc = { 8, 8, 0, VR_FMT_RGBA8 };
    uint8_t checker[8 * 8 * 4], level[4 * 4 * 4];
    vr_render_state rs, flat;
    vr_texture *tex = NULL;
    vr_draw_desc d;
    int i, l;

    if (vr_platform_init(&pdesc) != VR_OK || vr_gfx_init(&gdesc) != VR_OK)
        return 1;
    for (i = 0; i < 64; i++) {
        uint8_t c = ((i % 8) / 2 + (i / 16)) % 2 ? 255 : 96;
        checker[4 * i] = checker[4 * i + 1] = checker[4 * i + 2] = c;
        checker[4 * i + 3] = 255;
    }
    if (vr_texture_create(&tdesc, &tex) != VR_OK)
        return 1;
    vr_texture_update(tex, 0, checker);
    for (l = 1; l < 4; l++) { /* flat grey mips */
        memset(level, 176, sizeof(level));
        vr_texture_update(tex, l, level);
    }

    vr_render_state_default(&flat);
    flat.cull = VR_CULL_NONE;
    flat.stages[0].color_op = VR_TOP_SELECT_ARG1;
    flat.stages[0].color_arg1 = VR_TARG_DIFFUSE;
    flat.stages[0].alpha_arg1 = VR_TARG_DIFFUSE;
    vr_render_state_default(&rs);
    rs.cull = VR_CULL_NONE;
    rs.stages[0].texture = tex;
    rs.stages[0].min_filter = rs.stages[0].mag_filter = VR_FILTER_LINEAR;
    rs.stages[0].mip_filter = VR_MIP_LINEAR;

    while (vr_time_us() < 5000000u) {
        const float clear[4] = { 0, 0, 0, 1 };
        float t = (float)vr_time_us() / 1e6f, c = cosf(t), s = sinf(t);
        float aspect = 544.0f / 960.0f;
        /* Rotation about z, aspect correction, row-vector convention. */
        float m[16] = { c * aspect, s, 0, 0, -s * aspect, c, 0, 0, 0, 0, 1, 0, 0, 0, 0.5f, 1 };

        vr_gfx_begin_frame();
        vr_gfx_clear(VR_CLEAR_COLOR | VR_CLEAR_DEPTH, clear, 1.0f);
        memset(&d, 0, sizeof(d));
        d.primitive = VR_PRIM_TRIANGLE_STRIP;
        d.layout = &screen_layout;
        d.vertex_data = backdrop;
        d.vertex_count = 4;
        d.state = &flat;
        vr_gfx_draw(&d);
        d.layout = &layout;
        d.vertex_data = quad;
        d.transform = m;
        d.state = &rs;
        vr_gfx_draw(&d);
        vr_gfx_end_frame();
    }
    vr_texture_destroy(tex);
    vr_gfx_shutdown();
    vr_platform_shutdown();
    return 0;
}
