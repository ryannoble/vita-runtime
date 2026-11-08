/* Host graphics backend: a software reference rasterizer.
 *
 * It favours clarity and determinism over speed so that tests can check
 * exact pixels. When built with VR_HOST_SDL2 it also shows the back buffer
 * in a window (set VR_HEADLESS=1 to disable). */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "vr/vr_gfx.h"
#include "../gfx_internal.h"

#ifdef VR_HOST_SDL2
#include <SDL.h>
#endif

#define MAX_LEVELS 16
#define NVAR       13 /* c0[4] c1[4] t0[2] t1[2] fog */
#define V_C0  0
#define V_C1  4
#define V_T0  8
#define V_T1  10
#define V_FOG 12

struct vr_buffer {
    vr_buffer_type type;
    size_t         size;
    uint8_t       *data;
};

struct vr_texture {
    int       width, height, levels;
    vr_format format;
    uint8_t  *rgba[MAX_LEVELS]; /* decoded RGBA8 per level */
};

typedef struct {
    float x, y, z, w;    /* clip space */
    float var[NVAR];
} clip_vtx;

typedef struct {
    float sx, sy, sz, inv_w;
    float var[NVAR];     /* premultiplied by inv_w */
} scr_vtx;

static struct {
    int      ready;
    int      width, height;
    uint8_t *color;
    float   *depth;
    int      vp_x, vp_y, vp_w, vp_h;
    float    vp_min_z, vp_max_z;
#ifdef VR_HOST_SDL2
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *screen;
#endif
} g;

/* ---- Small helpers -------------------------------------------------------------- */

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int compare(vr_compare func, float a, float b)
{
    switch (func) {
    case VR_CMP_NEVER: return 0;
    case VR_CMP_LESS: return a < b;
    case VR_CMP_EQUAL: return a == b;
    case VR_CMP_LEQUAL: return a <= b;
    case VR_CMP_GREATER: return a > b;
    case VR_CMP_NOTEQUAL: return a != b;
    case VR_CMP_GEQUAL: return a >= b;
    default: return 1;
    }
}

/* ---- Device -------------------------------------------------------------------- */

#ifdef VR_HOST_SDL2
static void window_open(void)
{
    const char *headless = getenv("VR_HEADLESS");

    if ((headless && *headless && *headless != '0') ||
        SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
        return;
    g.window = SDL_CreateWindow("vita-runtime", SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, g.width, g.height, 0);
    if (g.window)
        g.renderer = SDL_CreateRenderer(g.window, -1, 0);
    if (g.renderer)
        g.screen = SDL_CreateTexture(g.renderer, SDL_PIXELFORMAT_ABGR8888,
                                     SDL_TEXTUREACCESS_STREAMING, g.width, g.height);
    if (!g.screen) {
        if (g.renderer)
            SDL_DestroyRenderer(g.renderer);
        if (g.window)
            SDL_DestroyWindow(g.window);
        g.window = NULL;
        g.renderer = NULL;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
}

static void window_close(void)
{
    if (!g.window)
        return;
    SDL_DestroyTexture(g.screen);
    SDL_DestroyRenderer(g.renderer);
    SDL_DestroyWindow(g.window);
    g.screen = NULL;
    g.renderer = NULL;
    g.window = NULL;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
#endif

vr_result vr_gfx_init(const vr_gfx_desc *desc)
{
    int w = desc && desc->width > 0 ? desc->width : 960;
    int h = desc && desc->height > 0 ? desc->height : 544;

    if (w > 8192 || h > 8192)
        return VR_ERR_INVALID_ARG;
    vr_gfx_shutdown();
    g.color = calloc((size_t)w * (size_t)h, 4);
    g.depth = malloc((size_t)w * (size_t)h * sizeof(float));
    if (!g.color || !g.depth) {
        free(g.color);
        free(g.depth);
        g.color = NULL;
        g.depth = NULL;
        return VR_ERR_NO_MEMORY;
    }
    g.width = w;
    g.height = h;
    g.ready = 1;
    vr_gfx_set_viewport(0, 0, w, h, 0.0f, 1.0f);
    {
        const float black[4] = { 0, 0, 0, 1 };
        vr_gfx_clear(VR_CLEAR_COLOR | VR_CLEAR_DEPTH, black, 1.0f);
    }
#ifdef VR_HOST_SDL2
    window_open();
#endif
    return VR_OK;
}

void vr_gfx_shutdown(void)
{
#ifdef VR_HOST_SDL2
    window_close();
#endif
    free(g.color);
    free(g.depth);
    memset(&g, 0, sizeof(g));
}

void vr_gfx_get_size(int *width, int *height)
{
    if (width)
        *width = g.width;
    if (height)
        *height = g.height;
}

void vr_gfx_begin_frame(void)
{
}

void vr_gfx_end_frame(void)
{
#ifdef VR_HOST_SDL2
    SDL_Event ev;

    if (!g.window)
        return;
    while (SDL_PeepEvents(&ev, 1, SDL_GETEVENT, SDL_QUIT, SDL_WINDOWEVENT) > 0) {
    }
    SDL_UpdateTexture(g.screen, NULL, g.color, g.width * 4);
    SDL_RenderClear(g.renderer);
    SDL_RenderCopy(g.renderer, g.screen, NULL, NULL);
    SDL_RenderPresent(g.renderer);
#endif
}

void vr_gfx_set_viewport(int x, int y, int width, int height, float min_z, float max_z)
{
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > g.width)
        width = g.width - x;
    if (y + height > g.height)
        height = g.height - y;
    g.vp_x = x;
    g.vp_y = y;
    g.vp_w = width > 0 ? width : 0;
    g.vp_h = height > 0 ? height : 0;
    g.vp_min_z = min_z;
    g.vp_max_z = max_z;
}

void vr_gfx_clear(unsigned flags, const float rgba[4], float depth)
{
    uint8_t c[4] = { 0, 0, 0, 0 };
    int x, y, i;

    if (!g.ready)
        return;
    if (rgba)
        for (i = 0; i < 4; i++)
            c[i] = (uint8_t)(clampf(rgba[i], 0.0f, 1.0f) * 255.0f + 0.5f);
    for (y = g.vp_y; y < g.vp_y + g.vp_h; y++) {
        for (x = g.vp_x; x < g.vp_x + g.vp_w; x++) {
            size_t p = (size_t)y * (size_t)g.width + (size_t)x;
            if (flags & VR_CLEAR_COLOR)
                memcpy(g.color + p * 4, c, 4);
            if (flags & VR_CLEAR_DEPTH)
                g.depth[p] = depth;
        }
    }
}

vr_result vr_gfx_read_pixels(int x, int y, int width, int height, void *rgba8)
{
    int row;

    if (!g.ready)
        return VR_ERR_UNSUPPORTED;
    if (!rgba8 || x < 0 || y < 0 || width <= 0 || height <= 0 ||
        x + width > g.width || y + height > g.height)
        return VR_ERR_INVALID_ARG;
    for (row = 0; row < height; row++)
        memcpy((uint8_t *)rgba8 + (size_t)row * (size_t)width * 4,
               g.color + ((size_t)(y + row) * (size_t)g.width + (size_t)x) * 4,
               (size_t)width * 4);
    return VR_OK;
}

/* ---- Buffers ------------------------------------------------------------------- */

vr_result vr_buffer_create(vr_buffer_type type, size_t size, const void *data, vr_buffer **out)
{
    vr_buffer *buf;

    if (!out || !size || (unsigned)type > VR_BUFFER_INDEX)
        return VR_ERR_INVALID_ARG;
    buf = malloc(sizeof(*buf));
    if (!buf)
        return VR_ERR_NO_MEMORY;
    buf->data = calloc(1, size);
    if (!buf->data) {
        free(buf);
        return VR_ERR_NO_MEMORY;
    }
    if (data)
        memcpy(buf->data, data, size);
    buf->type = type;
    buf->size = size;
    *out = buf;
    return VR_OK;
}

vr_result vr_buffer_update(vr_buffer *buf, size_t offset, const void *data, size_t size)
{
    if (!buf || (!data && size) || offset > buf->size || size > buf->size - offset)
        return VR_ERR_INVALID_ARG;
    if (size)
        memcpy(buf->data + offset, data, size);
    return VR_OK;
}

void vr_buffer_destroy(vr_buffer *buf)
{
    if (!buf)
        return;
    free(buf->data);
    free(buf);
}

/* ---- Textures ------------------------------------------------------------------ */

vr_result vr_texture_create(const vr_texture_desc *desc, vr_texture **out)
{
    vr_texture *tex;
    int max_levels, i;

    if (!desc || !out || desc->width <= 0 || desc->height <= 0 ||
        desc->width > 4096 || desc->height > 4096 ||
        (unsigned)desc->format >= VR_FMT_COUNT || desc->levels < 0)
        return VR_ERR_INVALID_ARG;
    max_levels = vr__mip_count(desc->width, desc->height);
    if (desc->levels > max_levels)
        return VR_ERR_INVALID_ARG;
    tex = calloc(1, sizeof(*tex));
    if (!tex)
        return VR_ERR_NO_MEMORY;
    tex->width = desc->width;
    tex->height = desc->height;
    tex->format = desc->format;
    tex->levels = desc->levels ? desc->levels : max_levels;
    for (i = 0; i < tex->levels; i++) {
        size_t n = (size_t)vr__mip_dim(tex->width, i) * (size_t)vr__mip_dim(tex->height, i);
        tex->rgba[i] = calloc(n, 4);
        if (!tex->rgba[i]) {
            vr_texture_destroy(tex);
            return VR_ERR_NO_MEMORY;
        }
    }
    *out = tex;
    return VR_OK;
}

vr_result vr_texture_update(vr_texture *tex, int level, const void *data)
{
    if (!tex || !data || level < 0 || level >= tex->levels)
        return VR_ERR_INVALID_ARG;
    vr__decode_rgba8(tex->format, vr__mip_dim(tex->width, level),
                     vr__mip_dim(tex->height, level), data, tex->rgba[level]);
    return VR_OK;
}

void vr_texture_destroy(vr_texture *tex)
{
    int i;

    if (!tex)
        return;
    for (i = 0; i < MAX_LEVELS; i++)
        free(tex->rgba[i]);
    free(tex);
}

/* ---- Sampling ------------------------------------------------------------------- */

static int address(vr_address mode, int i, int size)
{
    switch (mode) {
    case VR_ADDRESS_CLAMP:
        return i < 0 ? 0 : (i >= size ? size - 1 : i);
    case VR_ADDRESS_MIRROR: {
        int period = 2 * size, m = i % period;
        if (m < 0)
            m += period;
        return m < size ? m : period - 1 - m;
    }
    default: {
        int m = i % size;
        return m < 0 ? m + size : m;
    }
    }
}

static void texel(const vr_texture *t, int level, const vr_texture_stage *st, int x, int y,
                  float out[4])
{
    int w = vr__mip_dim(t->width, level), h = vr__mip_dim(t->height, level);
    const uint8_t *p;
    int i;

    x = address(st->address_u, x, w);
    y = address(st->address_v, y, h);
    p = t->rgba[level] + ((size_t)y * (size_t)w + (size_t)x) * 4;
    for (i = 0; i < 4; i++)
        out[i] = p[i] / 255.0f;
}

static void sample_level(const vr_texture *t, int level, const vr_texture_stage *st,
                         vr_filter filter, float u, float v, float out[4])
{
    int w = vr__mip_dim(t->width, level), h = vr__mip_dim(t->height, level);
    float x = u * (float)w, y = v * (float)h;

    if (filter == VR_FILTER_NEAREST) {
        texel(t, level, st, (int)floorf(x), (int)floorf(y), out);
    } else {
        float fx = x - 0.5f, fy = y - 0.5f;
        int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
        float ax = fx - (float)x0, ay = fy - (float)y0;
        float a[4], b[4], c[4], d[4];
        int i;

        texel(t, level, st, x0, y0, a);
        texel(t, level, st, x0 + 1, y0, b);
        texel(t, level, st, x0, y0 + 1, c);
        texel(t, level, st, x0 + 1, y0 + 1, d);
        for (i = 0; i < 4; i++)
            out[i] = (a[i] * (1 - ax) + b[i] * ax) * (1 - ay) + (c[i] * (1 - ax) + d[i] * ax) * ay;
    }
}

static void sample(const vr_texture_stage *st, const float uv[2], const float duv[4], float out[4])
{
    const vr_texture *t = st->texture;
    float lod, lx, ly;
    int i;

    if (!t) {
        out[0] = out[1] = out[2] = out[3] = 1.0f;
        return;
    }
    lx = sqrtf(duv[0] * duv[0] * (float)(t->width * t->width) +
               duv[1] * duv[1] * (float)(t->height * t->height));
    ly = sqrtf(duv[2] * duv[2] * (float)(t->width * t->width) +
               duv[3] * duv[3] * (float)(t->height * t->height));
    lod = log2f(lx > ly ? lx : ly);
    if (!(lod > 0.0f) || st->mip_filter == VR_MIP_NONE || t->levels == 1) {
        sample_level(t, 0, st, lod > 0.0f ? st->min_filter : st->mag_filter, uv[0], uv[1], out);
        return;
    }
    lod = clampf(lod, 0.0f, (float)(t->levels - 1));
    if (st->mip_filter == VR_MIP_NEAREST) {
        sample_level(t, (int)(lod + 0.5f), st, st->min_filter, uv[0], uv[1], out);
    } else {
        int l0 = (int)lod, l1 = l0 + 1 < t->levels ? l0 + 1 : l0;
        float f = lod - (float)l0, b[4];

        sample_level(t, l0, st, st->min_filter, uv[0], uv[1], out);
        sample_level(t, l1, st, st->min_filter, uv[0], uv[1], b);
        for (i = 0; i < 4; i++)
            out[i] = out[i] * (1 - f) + b[i] * f;
    }
}

/* ---- Fragment shading ------------------------------------------------------------- */

typedef struct {
    const float *diffuse, *specular, *current, *texture, *tfactor;
} combine_inputs;

static void get_arg(int arg, const combine_inputs *in, float out[4])
{
    const float *src;
    int i;

    switch (arg & 0x0F) {
    case VR_TARG_DIFFUSE: src = in->diffuse; break;
    case VR_TARG_SPECULAR: src = in->specular; break;
    case VR_TARG_TEXTURE: src = in->texture; break;
    case VR_TARG_TFACTOR: src = in->tfactor; break;
    default: src = in->current; break;
    }
    for (i = 0; i < 4; i++)
        out[i] = (arg & VR_TARG_ALPHA_REPLICATE) ? src[3] : src[i];
    if (arg & VR_TARG_COMPLEMENT)
        for (i = 0; i < 4; i++)
            out[i] = 1.0f - out[i];
}

/* Applies op to channels [first, first + count) of a1/a2 into out. */
static void combine(vr_texture_op op, const float *a1, const float *a2,
                    const combine_inputs *in, int first, int count, float *out)
{
    int i;

    if (op == VR_TOP_DOTPRODUCT3) {
        float d = 4.0f * ((a1[0] - 0.5f) * (a2[0] - 0.5f) + (a1[1] - 0.5f) * (a2[1] - 0.5f) +
                          (a1[2] - 0.5f) * (a2[2] - 0.5f));
        for (i = first; i < first + count; i++)
            out[i] = clampf(d, 0.0f, 1.0f);
        return;
    }
    for (i = first; i < first + count; i++) {
        float x = a1[i], y = a2[i], r, f;

        switch (op) {
        case VR_TOP_SELECT_ARG1: r = x; break;
        case VR_TOP_SELECT_ARG2: r = y; break;
        case VR_TOP_MODULATE: r = x * y; break;
        case VR_TOP_MODULATE2X: r = 2 * x * y; break;
        case VR_TOP_MODULATE4X: r = 4 * x * y; break;
        case VR_TOP_ADD: r = x + y; break;
        case VR_TOP_ADD_SIGNED: r = x + y - 0.5f; break;
        case VR_TOP_ADD_SIGNED2X: r = 2 * (x + y - 0.5f); break;
        case VR_TOP_SUBTRACT: r = x - y; break;
        case VR_TOP_ADD_SMOOTH: r = x + y - x * y; break;
        case VR_TOP_BLEND_DIFFUSE_ALPHA: f = in->diffuse[3]; r = x * f + y * (1 - f); break;
        case VR_TOP_BLEND_TEXTURE_ALPHA: f = in->texture[3]; r = x * f + y * (1 - f); break;
        case VR_TOP_BLEND_FACTOR_ALPHA: f = in->tfactor[3]; r = x * f + y * (1 - f); break;
        case VR_TOP_BLEND_CURRENT_ALPHA: f = in->current[3]; r = x * f + y * (1 - f); break;
        default: r = in->current[i]; break;
        }
        out[i] = clampf(r, 0.0f, 1.0f);
    }
}

static float blend_factor(vr_blend_factor f, int ch, const float *s, const float *d, const float *k)
{
    switch (f) {
    case VR_BLEND_ZERO: return 0.0f;
    case VR_BLEND_ONE: return 1.0f;
    case VR_BLEND_SRC_COLOR: return s[ch];
    case VR_BLEND_INV_SRC_COLOR: return 1.0f - s[ch];
    case VR_BLEND_SRC_ALPHA: return s[3];
    case VR_BLEND_INV_SRC_ALPHA: return 1.0f - s[3];
    case VR_BLEND_DST_ALPHA: return d[3];
    case VR_BLEND_INV_DST_ALPHA: return 1.0f - d[3];
    case VR_BLEND_DST_COLOR: return d[ch];
    case VR_BLEND_INV_DST_COLOR: return 1.0f - d[ch];
    case VR_BLEND_SRC_ALPHA_SAT:
        return ch == 3 ? 1.0f : (s[3] < 1.0f - d[3] ? s[3] : 1.0f - d[3]);
    case VR_BLEND_CONSTANT: return k[ch];
    case VR_BLEND_INV_CONSTANT: return 1.0f - k[ch];
    }
    return 1.0f;
}

/* var: perspective-corrected varyings; duv: d(uv)/dx, d(uv)/dy per stage. */
static void shade_pixel(const vr_render_state *rs, int x, int y, float z,
                        const float *var, const float duv[2][4])
{
    size_t p = (size_t)y * (size_t)g.width + (size_t)x;
    float cur[4], tex[4], a1[4], a2[4], out[4], dst[4];
    const float *diffuse = var + V_C0, *specular = var + V_C1;
    combine_inputs in;
    int s, i;

    z += rs->depth_bias;
    if (z < 0.0f || z > 1.0f)
        return;
    if (rs->depth_test_enable && !compare(rs->depth_func, z, g.depth[p]))
        return;

    memcpy(cur, diffuse, sizeof(cur));
    in.diffuse = diffuse;
    in.specular = specular;
    in.tfactor = rs->texture_factor;
    for (s = 0; s < VR_MAX_TEXTURE_STAGES; s++) {
        const vr_texture_stage *st = &rs->stages[s];
        const float *uv = var + (st->texcoord == 1 ? V_T1 : V_T0);

        if (st->color_op == VR_TOP_DISABLE)
            break;
        sample(st, uv, duv[st->texcoord == 1 ? 1 : 0], tex);
        in.current = cur;
        in.texture = tex;
        memcpy(out, cur, sizeof(out));
        get_arg(st->color_arg1, &in, a1);
        get_arg(st->color_arg2, &in, a2);
        combine(st->color_op, a1, a2, &in, 0, 3, out);
        if (st->alpha_op != VR_TOP_DISABLE && st->color_op != VR_TOP_DOTPRODUCT3) {
            get_arg(st->alpha_arg1, &in, a1);
            get_arg(st->alpha_arg2, &in, a2);
            combine(st->alpha_op, a1, a2, &in, 3, 1, out);
        }
        if (st->color_op == VR_TOP_DOTPRODUCT3)
            out[3] = out[0]; /* D3D replicates the dot product into alpha */
        memcpy(cur, out, sizeof(cur));
    }

    if (rs->specular_enable)
        for (i = 0; i < 3; i++)
            cur[i] = clampf(cur[i] + specular[i], 0.0f, 1.0f);
    if (rs->fog_enable) {
        float range = rs->fog_end - rs->fog_start;
        float f = range != 0.0f ? clampf((rs->fog_end - var[V_FOG]) / range, 0.0f, 1.0f) : 1.0f;
        for (i = 0; i < 3; i++)
            cur[i] = cur[i] * f + rs->fog_color[i] * (1.0f - f);
    }
    if (rs->alpha_test_enable) {
        /* Compare at 8-bit precision like fixed-function hardware. */
        float a8 = floorf(cur[3] * 255.0f + 0.5f), ref8 = floorf(rs->alpha_ref * 255.0f + 0.5f);
        if (!compare(rs->alpha_func, a8, ref8))
            return;
    }

    for (i = 0; i < 4; i++)
        dst[i] = g.color[p * 4 + (size_t)i] / 255.0f;
    if (rs->blend_enable) {
        for (i = 0; i < 4; i++) {
            float sf = blend_factor(rs->blend_src, i, cur, dst, rs->blend_constant);
            float df = blend_factor(rs->blend_dst, i, cur, dst, rs->blend_constant);
            float sv = cur[i] * sf, dv = dst[i] * df, r;
            switch (rs->blend_op) {
            case VR_BLEND_OP_SUBTRACT: r = sv - dv; break;
            case VR_BLEND_OP_REV_SUBTRACT: r = dv - sv; break;
            case VR_BLEND_OP_MIN: r = cur[i] < dst[i] ? cur[i] : dst[i]; break;
            case VR_BLEND_OP_MAX: r = cur[i] > dst[i] ? cur[i] : dst[i]; break;
            default: r = sv + dv; break;
            }
            out[i] = r;
        }
    } else {
        memcpy(out, cur, sizeof(out));
    }
    for (i = 0; i < 4; i++)
        if (rs->color_write_mask & (1u << i))
            g.color[p * 4 + (size_t)i] = (uint8_t)(clampf(out[i], 0.0f, 1.0f) * 255.0f + 0.5f);
    if (rs->depth_test_enable && rs->depth_write_enable)
        g.depth[p] = z;
}

/* ---- Rasterization ------------------------------------------------------------- */

static float edge(const scr_vtx *a, const scr_vtx *b, float px, float py)
{
    return (b->sx - a->sx) * (py - a->sy) - (b->sy - a->sy) * (px - a->sx);
}

static int top_left(const scr_vtx *a, const scr_vtx *b)
{
    float dx = b->sx - a->sx, dy = b->sy - a->sy;
    return (dy == 0.0f && dx > 0.0f) || dy < 0.0f;
}

static void interp(const scr_vtx *v[3], const float l[3], float *var, float *inv_w)
{
    float iw = l[0] * v[0]->inv_w + l[1] * v[1]->inv_w + l[2] * v[2]->inv_w;
    float w = iw != 0.0f ? 1.0f / iw : 0.0f;
    int i;

    for (i = 0; i < NVAR; i++)
        var[i] = (l[0] * v[0]->var[i] + l[1] * v[1]->var[i] + l[2] * v[2]->var[i]) * w;
    if (inv_w)
        *inv_w = iw;
}

static void raster_triangle(const vr_render_state *rs, const scr_vtx *a, const scr_vtx *b,
                            const scr_vtx *c)
{
    const scr_vtx *v[3];
    float area, minx, maxx, miny, maxy;
    int x0, x1, y0, y1, x, y, tl[3], k;

    area = edge(a, b, c->sx, c->sy);
    if (area == 0.0f || !(area == area))
        return;
    /* area > 0 is clockwise on screen (y down). */
    if ((rs->cull == VR_CULL_CW && area > 0.0f) || (rs->cull == VR_CULL_CCW && area < 0.0f))
        return;
    v[0] = a;
    if (area > 0.0f) {
        v[1] = b;
        v[2] = c;
    } else {
        v[1] = c;
        v[2] = b;
        area = -area;
    }
    tl[0] = top_left(v[1], v[2]);
    tl[1] = top_left(v[2], v[0]);
    tl[2] = top_left(v[0], v[1]);

    minx = fminf(v[0]->sx, fminf(v[1]->sx, v[2]->sx));
    maxx = fmaxf(v[0]->sx, fmaxf(v[1]->sx, v[2]->sx));
    miny = fminf(v[0]->sy, fminf(v[1]->sy, v[2]->sy));
    maxy = fmaxf(v[0]->sy, fmaxf(v[1]->sy, v[2]->sy));
    x0 = minx < (float)g.vp_x ? g.vp_x : (int)floorf(minx);
    y0 = miny < (float)g.vp_y ? g.vp_y : (int)floorf(miny);
    x1 = maxx > (float)(g.vp_x + g.vp_w - 1) ? g.vp_x + g.vp_w - 1 : (int)ceilf(maxx);
    y1 = maxy > (float)(g.vp_y + g.vp_h - 1) ? g.vp_y + g.vp_h - 1 : (int)ceilf(maxy);

    for (y = y0; y <= y1; y++) {
        for (x = x0; x <= x1; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float e[3], l[3], var[NVAR], vx[NVAR], vy[NVAR], duv[2][4], z;
            int inside = 1;

            e[0] = edge(v[1], v[2], px, py);
            e[1] = edge(v[2], v[0], px, py);
            e[2] = edge(v[0], v[1], px, py);
            for (k = 0; k < 3; k++)
                if (e[k] < 0.0f || (e[k] == 0.0f && !tl[k]))
                    inside = 0;
            if (!inside)
                continue;
            for (k = 0; k < 3; k++)
                l[k] = e[k] / area;
            interp(v, l, var, NULL);
            z = l[0] * v[0]->sz + l[1] * v[1]->sz + l[2] * v[2]->sz;

            /* Texture coordinate derivatives from neighbouring pixel centres. */
            {
                float lx[3], ly[3];
                lx[0] = edge(v[1], v[2], px + 1, py) / area;
                lx[1] = edge(v[2], v[0], px + 1, py) / area;
                lx[2] = 1.0f - lx[0] - lx[1];
                ly[0] = edge(v[1], v[2], px, py + 1) / area;
                ly[1] = edge(v[2], v[0], px, py + 1) / area;
                ly[2] = 1.0f - ly[0] - ly[1];
                interp(v, lx, vx, NULL);
                interp(v, ly, vy, NULL);
                for (k = 0; k < 2; k++) {
                    int o = k ? V_T1 : V_T0;
                    duv[k][0] = vx[o] - var[o];
                    duv[k][1] = vx[o + 1] - var[o + 1];
                    duv[k][2] = vy[o] - var[o];
                    duv[k][3] = vy[o + 1] - var[o + 1];
                }
            }
            shade_pixel(rs, x, y, z, var, (const float(*)[4])duv);
        }
    }
}

static int in_viewport(int x, int y)
{
    return x >= g.vp_x && y >= g.vp_y && x < g.vp_x + g.vp_w && y < g.vp_y + g.vp_h;
}

static void raster_point(const vr_render_state *rs, const scr_vtx *a)
{
    static const float zero_duv[2][4];
    float var[NVAR], w = a->inv_w != 0.0f ? 1.0f / a->inv_w : 0.0f;
    int x = (int)floorf(a->sx), y = (int)floorf(a->sy), i;

    if (!in_viewport(x, y))
        return;
    for (i = 0; i < NVAR; i++)
        var[i] = a->var[i] * w;
    shade_pixel(rs, x, y, a->sz, var, zero_duv);
}

static void raster_line(const vr_render_state *rs, const scr_vtx *a, const scr_vtx *b)
{
    static const float zero_duv[2][4];
    float dx = b->sx - a->sx, dy = b->sy - a->sy;
    int steps = (int)ceilf(fmaxf(fabsf(dx), fabsf(dy))), s, i;

    for (s = 0; s < steps; s++) { /* last pixel excluded, like D3D */
        float t = ((float)s + 0.5f) / (float)steps;
        float iw = a->inv_w + (b->inv_w - a->inv_w) * t, w = iw != 0.0f ? 1.0f / iw : 0.0f;
        float var[NVAR];
        int x = (int)floorf(a->sx + dx * t), y = (int)floorf(a->sy + dy * t);

        if (!in_viewport(x, y))
            continue;
        for (i = 0; i < NVAR; i++)
            var[i] = (a->var[i] + (b->var[i] - a->var[i]) * t) * w;
        shade_pixel(rs, x, y, a->sz + (b->sz - a->sz) * t, var, zero_duv);
    }
}

/* ---- Vertex processing ------------------------------------------------------------ */

static void to_screen(const clip_vtx *c, int pretransformed, scr_vtx *s)
{
    int i;

    if (pretransformed) {
        s->sx = c->x;
        s->sy = c->y;
        s->sz = c->z;
        s->inv_w = c->w != 0.0f ? 1.0f / c->w : 1.0f;
    } else {
        float iw = 1.0f / c->w;
        s->sx = (float)g.vp_x + (c->x * iw + 1.0f) * 0.5f * (float)g.vp_w;
        s->sy = (float)g.vp_y + (1.0f - c->y * iw) * 0.5f * (float)g.vp_h;
        s->sz = g.vp_min_z + c->z * iw * (g.vp_max_z - g.vp_min_z);
        s->inv_w = iw;
    }
    for (i = 0; i < NVAR; i++)
        s->var[i] = c->var[i] * s->inv_w;
}

static void lerp_clip(const clip_vtx *a, const clip_vtx *b, float t, clip_vtx *o)
{
    int i;

    o->x = a->x + (b->x - a->x) * t;
    o->y = a->y + (b->y - a->y) * t;
    o->z = a->z + (b->z - a->z) * t;
    o->w = a->w + (b->w - a->w) * t;
    for (i = 0; i < NVAR; i++)
        o->var[i] = a->var[i] + (b->var[i] - a->var[i]) * t;
}

/* Clips against z >= 0 (D3D near plane) and w > epsilon; draws a fan. */
static void clip_and_draw_triangle(const vr_render_state *rs, const clip_vtx in[3])
{
    clip_vtx poly[8], tmp[8];
    scr_vtx sv[8];
    int n = 3, plane, i, m;

    memcpy(poly, in, 3 * sizeof(clip_vtx));
    for (plane = 0; plane < 2; plane++) {
        m = 0;
        for (i = 0; i < n; i++) {
            const clip_vtx *a = &poly[i], *b = &poly[(i + 1) % n];
            float da = plane ? a->w - 1e-5f : a->z, db = plane ? b->w - 1e-5f : b->z;
            if (da >= 0.0f)
                tmp[m++] = *a;
            if ((da >= 0.0f) != (db >= 0.0f))
                lerp_clip(a, b, da / (da - db), &tmp[m++]);
        }
        n = m;
        if (n < 3)
            return;
        memcpy(poly, tmp, (size_t)n * sizeof(clip_vtx));
    }
    for (i = 0; i < n; i++)
        to_screen(&poly[i], 0, &sv[i]);
    for (i = 1; i + 1 < n; i++)
        raster_triangle(rs, &sv[0], &sv[i], &sv[i + 1]);
}

static void transform_vertex(const vr_draw_desc *d, const uint8_t *vtx, int *pretransformed,
                             clip_vtx *out)
{
    static const float white[4] = { 1, 1, 1, 1 };
    float pos[4] = { 0, 0, 0, 1 }, tmp[4];
    int i, has_pos_t = 0;

    memset(out, 0, sizeof(*out));
    memcpy(out->var + V_C0, white, sizeof(white));
    for (i = 0; i < d->layout->count; i++) {
        const vr_vertex_attr *a = &d->layout->attrs[i];
        vr__fetch_attr(vtx, a, tmp);
        switch (a->semantic) {
        case VR_ATTR_POSITION:
            memcpy(pos, tmp, 3 * sizeof(float));
            break;
        case VR_ATTR_POSITION_T:
            memcpy(pos, tmp, sizeof(pos));
            if (a->type != VR_ATTR_FLOAT4)
                pos[3] = 1.0f;
            has_pos_t = 1;
            break;
        case VR_ATTR_COLOR0: memcpy(out->var + V_C0, tmp, 4 * sizeof(float)); break;
        case VR_ATTR_COLOR1: memcpy(out->var + V_C1, tmp, 4 * sizeof(float)); break;
        case VR_ATTR_TEXCOORD0: memcpy(out->var + V_T0, tmp, 2 * sizeof(float)); break;
        case VR_ATTR_TEXCOORD1: memcpy(out->var + V_T1, tmp, 2 * sizeof(float)); break;
        default: break;
        }
    }
    *pretransformed = has_pos_t;
    if (has_pos_t) {
        out->x = pos[0];
        out->y = pos[1];
        out->z = pos[2];
        out->w = pos[3] != 0.0f ? 1.0f / pos[3] : 1.0f; /* rhw -> w */
    } else if (d->transform) {
        const float *m = d->transform;
        out->x = pos[0] * m[0] + pos[1] * m[4] + pos[2] * m[8] + m[12];
        out->y = pos[0] * m[1] + pos[1] * m[5] + pos[2] * m[9] + m[13];
        out->z = pos[0] * m[2] + pos[1] * m[6] + pos[2] * m[10] + m[14];
        out->w = pos[0] * m[3] + pos[1] * m[7] + pos[2] * m[11] + m[15];
    } else {
        out->x = pos[0];
        out->y = pos[1];
        out->z = pos[2];
        out->w = 1.0f;
    }
    out->var[V_FOG] = out->w;
}

vr_result vr_gfx_draw(const vr_draw_desc *d)
{
    vr_render_state defaults;
    const vr_render_state *rs;
    const uint8_t *verts, *indices = NULL;
    uint32_t count, prims, p;
    vr_result r;

    if (!g.ready)
        return VR_ERR_UNSUPPORTED;
    r = vr__validate_draw(d, d && d->vertex_buffer ? d->vertex_buffer->size : 0,
                          d && d->index_buffer ? d->index_buffer->size : 0);
    if (r != VR_OK)
        return r;
    if (!d->state) {
        vr_render_state_default(&defaults);
        rs = &defaults;
    } else {
        rs = d->state;
    }
    verts = d->vertex_buffer ? d->vertex_buffer->data + d->vertex_offset : d->vertex_data;
    if (d->index_count)
        indices = d->index_buffer ? d->index_buffer->data + d->index_offset : d->index_data;
    count = d->index_count ? d->index_count : d->vertex_count;
    prims = vr__prim_count(d->primitive, count);

    for (p = 0; p < prims; p++) {
        uint32_t slot[3];
        clip_vtx cv[3];
        int pre = 0, k, nv;

        vr__prim_vertices(d->primitive, p, slot);
        nv = d->primitive == VR_PRIM_POINT_LIST ? 1 :
             (d->primitive <= VR_PRIM_LINE_STRIP ? 2 : 3);
        for (k = 0; k < nv; k++) {
            uint32_t idx = vr__index_at(d, indices, slot[k]);
            if (idx >= d->vertex_count)
                return VR_ERR_INVALID_ARG; /* index outside the declared range */
            transform_vertex(d, verts + ((size_t)d->base_vertex + idx) * d->layout->stride, &pre,
                             &cv[k]);
        }
        if (pre || nv < 3) {
            scr_vtx sv[3];
            for (k = 0; k < nv; k++) {
                if (!pre && (cv[k].w <= 1e-5f || cv[k].z < 0.0f))
                    break;
                to_screen(&cv[k], pre, &sv[k]);
            }
            if (k < nv)
                continue;
            if (nv == 1)
                raster_point(rs, &sv[0]);
            else if (nv == 2)
                raster_line(rs, &sv[0], &sv[1]);
            else
                raster_triangle(rs, &sv[0], &sv[1], &sv[2]);
        } else {
            clip_and_draw_triangle(rs, cv);
        }
    }
    return VR_OK;
}
