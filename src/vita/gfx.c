/* Vita graphics backend on vitaGL.
 *
 * Each distinct fixed-function configuration (texture combiners, fog,
 * alpha test, vertex colour byte order) gets a small Cg shader pair that is
 * generated, compiled at runtime and cached. vitaGL's runtime compiler
 * needs libshacccg.suprx on the device (ur0:data/), as is usual for vitaGL
 * applications. Matrices are uploaded as four column vectors so no layout
 * convention needs to be assumed. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <vitaGL.h>

#include "vr/vr_gfx.h"
#include "vr/vr_platform.h"
#include "../gfx_internal.h"
#include "../internal.h"
#include "../shader_cache.h"

/* Compiled programs are cached in <data root>/shader_cache/ so that only the
 * first launch pays for runtime shader compilation. Delete the folder to
 * clear it; stale or damaged files are detected and replaced. */
#define CACHE_DIR      "shader_cache"
#define CACHE_MAX_SIZE (1024 * 1024)

#define MAX_PROGRAMS 128
#define SRC_SIZE     4096

enum { A_POS, A_C0, A_C1, A_T0, A_T1, A_COUNT };

struct vr_buffer {
    vr_buffer_type type;
    size_t         size;
    GLuint         id;
};

struct vr_texture {
    int       width, height, levels;
    vr_format format;
    GLuint    id;
};

/* Everything that changes generated shader code. Zero-initialised and
 * compared with memcmp, so it must have no padding-dependent content. */
typedef struct {
    uint8_t pretransformed, pos_w1, c0_bgra, c1_bgra;
    uint8_t stages; /* active stages */
    uint8_t color_op[VR_MAX_TEXTURE_STAGES], alpha_op[VR_MAX_TEXTURE_STAGES];
    uint8_t color_arg[VR_MAX_TEXTURE_STAGES][2], alpha_arg[VR_MAX_TEXTURE_STAGES][2];
    uint8_t texcoord[VR_MAX_TEXTURE_STAGES];
    uint8_t fog, specular, alpha_func;
} shader_key;

typedef struct {
    shader_key key;
    GLuint     program;
    GLint      attr[A_COUNT];
    GLint      u_m[4], u_vp, u_dr, u_bias, u_off, u_scale, u_tfactor, u_fog, u_fog_color, u_aref, u_tex[2];
} program_entry;

static struct {
    int           ready;
    int           width, height, vsync;
    int           vp_x, vp_y, vp_w, vp_h;
    float         vp_min_z, vp_max_z;
    GLuint        white;
    program_entry programs[MAX_PROGRAMS];
    int           program_count;
    int           gl_initialized;
    uint64_t      compiler_id; /* hash of the GL version/renderer strings */
    int           cache_dir_ready, cache_write_warned;
} g;

/* ---- Device -------------------------------------------------------------------- */

vr_result vr_gfx_init(const vr_gfx_desc *desc)
{
    static const uint8_t white[4] = { 255, 255, 255, 255 };

    if (g.ready)
        vr_gfx_shutdown();
    /* The Vita display is fixed; other sizes are not supported. */
    if (desc && ((desc->width && desc->width != 960) || (desc->height && desc->height != 544)))
        return VR_ERR_UNSUPPORTED;
    if (!g.gl_initialized) {
        /* vitaGL compiles shaders with SceShaccCg; it looks in these two places. */
        SceIoStat st;
        if (sceIoGetstat("ur0:data/libshacccg.suprx", &st) < 0 &&
            sceIoGetstat("ur0:data/external/libshacccg.suprx", &st) < 0) {
            vr_log(VR_LOG_ERROR, "vr_gfx: ur0:data/libshacccg.suprx is missing; extract it "
                   "with ShaRKF00D (needed to compile shaders)");
            return VR_ERR_UNSUPPORTED;
        }
        vglInit(0x800000);
    }
    g.gl_initialized = 1;
    {
        const char *ver = (const char *)glGetString(GL_VERSION);
        const char *ren = (const char *)glGetString(GL_RENDERER);
        g.compiler_id = vr__fnv1a64(ver ? ver : "", ver ? strlen(ver) : 0, VR__FNV_SEED);
        g.compiler_id = vr__fnv1a64(ren ? ren : "", ren ? strlen(ren) : 0, g.compiler_id);
    }
    g.width = 960;
    g.height = 544;
    g.vsync = desc ? desc->vsync : 0;
    vglWaitVblankStart(g.vsync ? GL_TRUE : GL_FALSE);

    glGenTextures(1, &g.white);
    glBindTexture(GL_TEXTURE_2D, g.white);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    g.ready = 1;
    vr_gfx_set_viewport(0, 0, g.width, g.height, 0.0f, 1.0f);
    return VR_OK;
}

void vr_gfx_shutdown(void)
{
    int i;

    if (!g.ready)
        return;
    for (i = 0; i < g.program_count; i++)
        if (g.programs[i].program)
            glDeleteProgram(g.programs[i].program);
    glDeleteTextures(1, &g.white);
    /* vitaGL has no teardown call; the GPU context lives until exit, so a
     * later vr_gfx_init must not initialize it again. */
    memset(&g, 0, sizeof(g));
    g.gl_initialized = 1;
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
    if (g.ready)
        vglSwapBuffers(GL_FALSE);
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
    if (!g.ready)
        return;
    /* GL window coordinates have their origin at the bottom left. */
    glViewport(g.vp_x, g.height - g.vp_y - g.vp_h, g.vp_w, g.vp_h);
    glDepthRangef(min_z, max_z);
}

void vr_gfx_clear(unsigned flags, const float rgba[4], float depth)
{
    GLbitfield mask = 0;

    if (!g.ready)
        return;
    if (flags & VR_CLEAR_COLOR) {
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(rgba ? rgba[0] : 0, rgba ? rgba[1] : 0, rgba ? rgba[2] : 0, rgba ? rgba[3] : 0);
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (flags & VR_CLEAR_DEPTH) {
        glDepthMask(GL_TRUE);
        glClearDepthf(depth);
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    glEnable(GL_SCISSOR_TEST);
    glScissor(g.vp_x, g.height - g.vp_y - g.vp_h, g.vp_w, g.vp_h);
    glClear(mask);
    glDisable(GL_SCISSOR_TEST);
}

vr_result vr_gfx_read_pixels(int x, int y, int width, int height, void *rgba8)
{
    uint8_t *dst = rgba8, *row;
    size_t pitch = (size_t)width * 4;
    int i;

    if (!g.ready)
        return VR_ERR_UNSUPPORTED;
    if (!rgba8 || x < 0 || y < 0 || width <= 0 || height <= 0 ||
        x + width > g.width || y + height > g.height)
        return VR_ERR_INVALID_ARG;
    row = malloc(pitch);
    if (!row)
        return VR_ERR_NO_MEMORY;
    glReadPixels(x, g.height - y - height, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba8);
    /* GL returns the bottom row first; flip to top row first. */
    for (i = 0; i < height / 2; i++) {
        uint8_t *a = dst + (size_t)i * pitch, *b = dst + (size_t)(height - 1 - i) * pitch;
        memcpy(row, a, pitch);
        memcpy(a, b, pitch);
        memcpy(b, row, pitch);
    }
    free(row);
    return VR_OK;
}

/* ---- Buffers ------------------------------------------------------------------- */

static GLenum buffer_target(vr_buffer_type type)
{
    return type == VR_BUFFER_INDEX ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
}

vr_result vr_buffer_create(vr_buffer_type type, size_t size, const void *data, vr_buffer **out)
{
    vr_buffer *buf;

    if (!out || !size || (unsigned)type > VR_BUFFER_INDEX)
        return VR_ERR_INVALID_ARG;
    if (!g.ready)
        return VR_ERR_UNSUPPORTED;
    buf = malloc(sizeof(*buf));
    if (!buf)
        return VR_ERR_NO_MEMORY;
    buf->type = type;
    buf->size = size;
    glGenBuffers(1, &buf->id);
    glBindBuffer(buffer_target(type), buf->id);
    glBufferData(buffer_target(type), (GLsizeiptr)size, data, GL_STATIC_DRAW);
    glBindBuffer(buffer_target(type), 0);
    *out = buf;
    return VR_OK;
}

vr_result vr_buffer_update(vr_buffer *buf, size_t offset, const void *data, size_t size)
{
    if (!buf || (!data && size) || offset > buf->size || size > buf->size - offset)
        return VR_ERR_INVALID_ARG;
    if (!size)
        return VR_OK;
    glBindBuffer(buffer_target(buf->type), buf->id);
    glBufferSubData(buffer_target(buf->type), (GLintptr)offset, (GLsizeiptr)size, data);
    glBindBuffer(buffer_target(buf->type), 0);
    return VR_OK;
}

void vr_buffer_destroy(vr_buffer *buf)
{
    if (!buf)
        return;
    glDeleteBuffers(1, &buf->id);
    free(buf);
}

/* ---- Textures ------------------------------------------------------------------ */

vr_result vr_texture_create(const vr_texture_desc *desc, vr_texture **out)
{
    vr_texture *tex;
    int max_levels;

    if (!desc || !out || desc->width <= 0 || desc->height <= 0 ||
        desc->width > 4096 || desc->height > 4096 ||
        (unsigned)desc->format >= VR_FMT_COUNT || desc->levels < 0)
        return VR_ERR_INVALID_ARG;
    if (!g.ready)
        return VR_ERR_UNSUPPORTED;
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
    glGenTextures(1, &tex->id);
    *out = tex;
    return VR_OK;
}

static GLenum dxt_format(vr_format f)
{
    switch (f) {
    case VR_FMT_DXT1: return GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
    case VR_FMT_DXT3: return GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
    default: return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    }
}

vr_result vr_texture_update(vr_texture *tex, int level, const void *data)
{
    int w, h;

    if (!tex || !data || level < 0 || level >= tex->levels)
        return VR_ERR_INVALID_ARG;
    w = vr__mip_dim(tex->width, level);
    h = vr__mip_dim(tex->height, level);
    glBindTexture(GL_TEXTURE_2D, tex->id);
    if (vr_format_is_compressed(tex->format)) {
        glCompressedTexImage2D(GL_TEXTURE_2D, level, dxt_format(tex->format), w, h, 0,
                               (GLsizei)vr_format_level_bytes(tex->format, w, h), data);
    } else {
        uint8_t *rgba = malloc((size_t)w * (size_t)h * 4);
        if (!rgba)
            return VR_ERR_NO_MEMORY;
        vr__decode_rgba8(tex->format, w, h, data, rgba);
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        free(rgba);
    }
    return VR_OK;
}

void vr_texture_destroy(vr_texture *tex)
{
    if (!tex)
        return;
    glDeleteTextures(1, &tex->id);
    free(tex);
}

/* ---- Shader generation ------------------------------------------------------------ */

static void append(char *dst, const char *fmt, ...)
{
    size_t len = strlen(dst);
    va_list args;

    if (len + 1 >= SRC_SIZE)
        return;
    va_start(args, fmt);
    vsnprintf(dst + len, SRC_SIZE - len, fmt, args);
    va_end(args);
}

static void arg_expr(int arg, int stage, char *out, size_t size)
{
    static const char *bases[] = { "cur", "v_c0", "v_c1", "tex%d", "u_tfactor" };
    char base[16], tmp[48];
    int b = arg & 0x0F;

    snprintf(base, sizeof(base), bases[b <= VR_TARG_TFACTOR ? b : 0], stage);
    if (arg & VR_TARG_ALPHA_REPLICATE)
        snprintf(tmp, sizeof(tmp), "%s.aaaa", base);
    else
        snprintf(tmp, sizeof(tmp), "%s", base);
    if (arg & VR_TARG_COMPLEMENT)
        snprintf(out, size, "(1.0 - %s)", tmp);
    else
        snprintf(out, size, "%s", tmp);
}

/* Writes "dst = saturate(op(x, y));" for one channel group. */
static void op_expr(char *src, int op, const char *dst, const char *x, const char *y, int stage)
{
    char f[16];

    switch (op) {
    case VR_TOP_SELECT_ARG1: append(src, "%s = %s;\n", dst, x); return;
    case VR_TOP_SELECT_ARG2: append(src, "%s = %s;\n", dst, y); return;
    case VR_TOP_MODULATE: append(src, "%s = saturate(%s * %s);\n", dst, x, y); return;
    case VR_TOP_MODULATE2X: append(src, "%s = saturate(2.0 * %s * %s);\n", dst, x, y); return;
    case VR_TOP_MODULATE4X: append(src, "%s = saturate(4.0 * %s * %s);\n", dst, x, y); return;
    case VR_TOP_ADD: append(src, "%s = saturate(%s + %s);\n", dst, x, y); return;
    case VR_TOP_ADD_SIGNED: append(src, "%s = saturate(%s + %s - 0.5);\n", dst, x, y); return;
    case VR_TOP_ADD_SIGNED2X: append(src, "%s = saturate(2.0 * (%s + %s - 0.5));\n", dst, x, y); return;
    case VR_TOP_SUBTRACT: append(src, "%s = saturate(%s - %s);\n", dst, x, y); return;
    case VR_TOP_ADD_SMOOTH: append(src, "%s = saturate(%s + %s - %s * %s);\n", dst, x, y, x, y); return;
    case VR_TOP_BLEND_DIFFUSE_ALPHA: snprintf(f, sizeof(f), "v_c0.a"); break;
    case VR_TOP_BLEND_TEXTURE_ALPHA: snprintf(f, sizeof(f), "tex%d.a", stage); break;
    case VR_TOP_BLEND_FACTOR_ALPHA: snprintf(f, sizeof(f), "u_tfactor.a"); break;
    case VR_TOP_BLEND_CURRENT_ALPHA: snprintf(f, sizeof(f), "cur.a"); break;
    default: return; /* DOTPRODUCT3 is handled by the caller */
    }
    append(src, "%s = lerp(%s, %s, %s);\n", dst, y, x, f);
}

static const char *const vertex_src =
    "void main(float4 a_pos, float4 a_c0, float4 a_c1, float4 a_t0, float4 a_t1,\n"
    "  uniform float4 u_m0, uniform float4 u_m1, uniform float4 u_m2, uniform float4 u_m3,\n"
    "  uniform float4 u_vp, uniform float2 u_dr, uniform float u_bias, uniform float2 u_off,\n"
    "  uniform float2 u_scale,\n"
    "  float4 out o_pos : POSITION, float4 out o_c0 : TEXCOORD0, float4 out o_c1 : TEXCOORD1,\n"
    "  float4 out o_t : TEXCOORD2, float out o_w : TEXCOORD3)\n"
    "{\n"
    "  float4 clip;\n"
    "#if PRETRANSFORMED\n"
    "#if POS_W1\n"
    "  float w = 1.0;\n"
    "#else\n"
    "  float w = 1.0 / a_pos.w;\n"
    "#endif\n"
    "  float2 sp = a_pos.xy * u_scale + u_off;\n"
    "  float2 ndc = float2((sp.x - u_vp.x) / u_vp.z * 2.0 - 1.0,\n"
    "                      1.0 - (sp.y - u_vp.y) / u_vp.w * 2.0);\n"
    "  float z01 = (a_pos.z - u_dr.x) / u_dr.y;\n"
    "  clip = float4(ndc * w, z01 * w, w);\n"
    "#else\n"
    "  float4 p = float4(a_pos.xyz, 1.0);\n"
    "  clip = float4(dot(p, u_m0), dot(p, u_m1), dot(p, u_m2), dot(p, u_m3));\n"
    "  clip.xy += float2(2.0 * u_off.x / u_vp.z, -2.0 * u_off.y / u_vp.w) * clip.w;\n"
    "#endif\n"
    "  clip.z += u_bias * clip.w;\n"
    "  o_pos = float4(clip.x, clip.y, 2.0 * clip.z - clip.w, clip.w);\n"
    "#if C0_BGRA\n"
    "  o_c0 = a_c0.zyxw;\n"
    "#else\n"
    "  o_c0 = a_c0;\n"
    "#endif\n"
    "#if C1_BGRA\n"
    "  o_c1 = a_c1.zyxw;\n"
    "#else\n"
    "  o_c1 = a_c1;\n"
    "#endif\n"
    "  o_t = float4(a_t0.xy, a_t1.xy);\n"
    "  o_w = clip.w;\n"
    "}\n";

static void build_fragment(const shader_key *k, char *src)
{
    static const char *const cmp[] = { "false", "<", "==", "<=", ">", "!=", ">=", "true" };
    int s;

    src[0] = '\0';
    append(src,
           "float4 main(float4 v_c0 : TEXCOORD0, float4 v_c1 : TEXCOORD1,\n"
           "  float4 v_t : TEXCOORD2, float v_w : TEXCOORD3,\n"
           "  uniform sampler2D u_tex0, uniform sampler2D u_tex1, uniform float4 u_tfactor,\n"
           "  uniform float2 u_fog, uniform float4 u_fog_color, uniform float u_aref) : COLOR\n"
           "{\n"
           "  float4 cur = v_c0;\n"
           "  float4 a1, a2, next;\n");
    for (s = 0; s < k->stages; s++) {
        char x[64], y[64]; /* arg_expr writes at most 8 + 47 bytes */

        append(src, "  float4 tex%d = tex2D(u_tex%d, %s);\n", s, s, k->texcoord[s] ? "v_t.zw" : "v_t.xy");
        append(src, "  next = cur;\n");
        arg_expr(k->color_arg[s][0], s, x, sizeof(x));
        arg_expr(k->color_arg[s][1], s, y, sizeof(y));
        append(src, "  a1 = %s; a2 = %s;\n", x, y);
        if (k->color_op[s] == VR_TOP_DOTPRODUCT3)
            append(src, "  next = saturate(4.0 * dot(a1.rgb - 0.5, a2.rgb - 0.5)).xxxx;\n");
        else
            op_expr(src, k->color_op[s], "  next.rgb", "a1.rgb", "a2.rgb", s);
        if (k->alpha_op[s] != VR_TOP_DISABLE && k->color_op[s] != VR_TOP_DOTPRODUCT3) {
            arg_expr(k->alpha_arg[s][0], s, x, sizeof(x));
            arg_expr(k->alpha_arg[s][1], s, y, sizeof(y));
            append(src, "  a1 = %s; a2 = %s;\n", x, y);
            if (k->alpha_op[s] == VR_TOP_DOTPRODUCT3)
                append(src, "  next.a = saturate(4.0 * dot(a1.rgb - 0.5, a2.rgb - 0.5));\n");
            else
                op_expr(src, k->alpha_op[s], "  next.a", "a1.a", "a2.a", s);
        }
        append(src, "  cur = next;\n");
    }
    if (k->specular)
        append(src, "  cur.rgb = saturate(cur.rgb + v_c1.rgb);\n");
    if (k->fog)
        append(src, "  cur.rgb = lerp(u_fog_color.rgb, cur.rgb, saturate((u_fog.y - v_w) / (u_fog.y - u_fog.x)));\n");
    if (k->alpha_func != VR_CMP_ALWAYS) {
        if (k->alpha_func == VR_CMP_NEVER)
            append(src, "  discard;\n");
        else
            append(src, "  if (!(floor(cur.a * 255.0 + 0.5) %s u_aref)) discard;\n", cmp[k->alpha_func]);
    }
    append(src, "  return cur;\n}\n");
}

/* Shaders are Cg. vitaGL treats GL_VERTEX_SHADER/GL_FRAGMENT_SHADER sources
 * as GLSL (and defers their compile to link time), so the Cg-specific types
 * are required. */
static GLuint compile(int vertex, const char *defines, const char *body)
{
    const char *parts[2] = { defines, body };
    GLuint sh = glCreateShader(vertex ? GL_CG_VERTEX_SHADER_EXT : GL_CG_FRAGMENT_SHADER_EXT);
    GLint ok = 0;

    glShaderSource(sh, 2, parts, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char info[512];
        GLsizei len = 0;
        info[0] = '\0';
        glGetShaderInfoLog(sh, (GLsizei)sizeof(info) - 1, &len, info);
        info[len > 0 && len < (GLsizei)sizeof(info) ? len : 0] = '\0';
        vr_log(VR_LOG_ERROR, "vr_gfx: %s shader compile failed: %s",
               vertex ? "vertex" : "fragment", info[0] ? info : "(no compiler log)");
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

/* ---- Shader cache ------------------------------------------------------------ */

static void cache_path(uint64_t key, const char *suffix, char *out, size_t size)
{
    snprintf(out, size, "%s/" CACHE_DIR "/%016llx%s", vr__data_root(), (unsigned long long)key,
             suffix);
}

/* Returns a linked program built from the cache file for key, or 0. */
static GLuint cache_load(uint64_t key)
{
    char path[VR_MAX_PATH];
    uint8_t *buf = NULL;
    const void *payload;
    size_t payload_size = 0, done = 0;
    SceOff size;
    SceUID fd;
    GLuint prog = 0;
    GLint linked = 0;

    cache_path(key, ".bin", path, sizeof(path));
    fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0)
        return 0;
    size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    if (size > VR__CACHE_HEADER_SIZE && size <= CACHE_MAX_SIZE)
        buf = malloc((size_t)size);
    while (buf && done < (size_t)size) {
        int got = sceIoRead(fd, buf + done, (SceSize)((size_t)size - done));
        if (got <= 0)
            break;
        done += (size_t)got;
    }
    sceIoClose(fd);
    payload = buf && done == (size_t)size ? vr__cache_unpack(key, buf, done, &payload_size) : NULL;
    if (payload) {
        /* vitaGL does not validate program binaries, hence the checked header. */
        prog = glCreateProgram();
        glProgramBinary(prog, 0, payload, (GLsizei)payload_size);
        glGetProgramiv(prog, GL_LINK_STATUS, &linked);
        if (!linked) {
            glDeleteProgram(prog);
            prog = 0;
        }
    }
    free(buf);
    if (!prog) {
        vr_log(VR_LOG_WARN, "vr_gfx: discarding unusable shader cache file %s", path);
        sceIoRemove(path);
    }
    return prog;
}

static void cache_store(GLuint prog, uint64_t key)
{
    char path[VR_MAX_PATH], tmp[VR_MAX_PATH];
    GLint len = 0;
    GLsizei got = 0;
    GLenum format = 0;
    uint8_t *bin, *file;
    size_t n, done = 0;
    SceUID fd;

    glGetProgramiv(prog, GL_PROGRAM_BINARY_LENGTH, &len);
    if (len <= 0 || len > CACHE_MAX_SIZE)
        return;
    bin = malloc((size_t)len);
    file = malloc((size_t)len + VR__CACHE_HEADER_SIZE);
    if (bin && file) {
        glGetProgramBinary(prog, len, &got, &format, bin);
    }
    n = got > 0 && got <= len ? vr__cache_pack(key, bin, (size_t)got, file, (size_t)len + VR__CACHE_HEADER_SIZE) : 0;
    free(bin);
    if (!n) {
        free(file);
        return;
    }
    if (!g.cache_dir_ready) {
        snprintf(path, sizeof(path), "%s/" CACHE_DIR, vr__data_root());
        sceIoMkdir(path, 0777); /* fails harmlessly if it exists */
        g.cache_dir_ready = 1;
    }
    /* Write to a temporary name and rename, so an interrupted write never
     * leaves a partial file under the real name. */
    cache_path(key, ".bin", path, sizeof(path));
    cache_path(key, ".tmp", tmp, sizeof(tmp));
    fd = sceIoOpen(tmp, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    while (fd >= 0 && done < n) {
        int w = sceIoWrite(fd, file + done, (SceSize)(n - done));
        if (w <= 0)
            break;
        done += (size_t)w;
    }
    if (fd >= 0)
        sceIoClose(fd);
    free(file);
    if (fd >= 0 && done == n) {
        sceIoRemove(path); /* rename does not replace an existing file */
        if (sceIoRename(tmp, path) >= 0)
            return;
    }
    if (fd >= 0)
        sceIoRemove(tmp);
    if (!g.cache_write_warned) {
        vr_log(VR_LOG_WARN, "vr_gfx: could not write the shader cache in %s/" CACHE_DIR,
               vr__data_root());
        g.cache_write_warned = 1;
    }
}

static program_entry *get_program(const shader_key *key)
{
    static const char *const attr_names[A_COUNT] = { "a_pos", "a_c0", "a_c1", "a_t0", "a_t1" };
    char defines[160], *frag;
    program_entry *e;
    GLuint vs, fs;
    uint64_t ckey, t0;
    int i;

    for (i = 0; i < g.program_count; i++)
        if (!memcmp(&g.programs[i].key, key, sizeof(*key)))
            return g.programs[i].program ? &g.programs[i] : NULL; /* 0 = failed before */
    if (g.program_count == MAX_PROGRAMS) {
        /* Simple policy: drop the whole cache when full. */
        for (i = 0; i < g.program_count; i++)
            if (g.programs[i].program)
                glDeleteProgram(g.programs[i].program);
        g.program_count = 0;
    }

    /* Reserve the cache slot first so a failure is remembered (program 0)
     * instead of being recompiled and logged on every draw. */
    e = &g.programs[g.program_count];
    memset(e, 0, sizeof(*e));
    e->key = *key;
    g.program_count++;

    frag = malloc(SRC_SIZE);
    if (!frag)
        return NULL;
    build_fragment(key, frag);
    snprintf(defines, sizeof(defines),
             "#define PRETRANSFORMED %d\n#define POS_W1 %d\n#define C0_BGRA %d\n#define C1_BGRA %d\n",
             key->pretransformed, key->pos_w1, key->c0_bgra, key->c1_bgra);

    /* The cache key covers the exact sources and the compiler build. */
    ckey = vr__fnv1a64(defines, strlen(defines), g.compiler_id);
    ckey = vr__fnv1a64(vertex_src, strlen(vertex_src), ckey);
    ckey = vr__fnv1a64(frag, strlen(frag), ckey);
    t0 = vr_time_us();
    e->program = cache_load(ckey);
    if (e->program) {
        free(frag);
        vr_log(VR_LOG_INFO, "vr_gfx: shader %016llx loaded from cache in %.1f ms",
               (unsigned long long)ckey, (double)(vr_time_us() - t0) / 1000.0);
        goto locations;
    }

    vs = compile(1, defines, vertex_src);
    fs = compile(0, "", frag);
    free(frag);
    if (!vs || !fs) {
        if (vs)
            glDeleteShader(vs);
        if (fs)
            glDeleteShader(fs);
        return NULL;
    }

    e->program = glCreateProgram();
    glAttachShader(e->program, vs);
    glAttachShader(e->program, fs);
    for (i = 0; i < A_COUNT; i++)
        glBindAttribLocation(e->program, (GLuint)i, attr_names[i]);
    glLinkProgram(e->program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    {
        GLint linked = 0;
        glGetProgramiv(e->program, GL_LINK_STATUS, &linked);
        if (!linked) {
            /* A failed program must not be queried: vitaGL dereferences its
             * shaders in glGetAttribLocation. */
            char info[512];
            GLsizei len = 0;
            info[0] = '\0';
            glGetProgramInfoLog(e->program, (GLsizei)sizeof(info) - 1, &len, info);
            info[len > 0 && len < (GLsizei)sizeof(info) ? len : 0] = '\0';
            vr_log(VR_LOG_ERROR, "vr_gfx: shader link failed: %s", info[0] ? info : "(no linker log)");
            glDeleteProgram(e->program);
            e->program = 0;
            return NULL;
        }
    }
    vr_log(VR_LOG_INFO, "vr_gfx: shader %016llx compiled in %.1f ms", (unsigned long long)ckey,
           (double)(vr_time_us() - t0) / 1000.0);
    cache_store(e->program, ckey);

locations:
    for (i = 0; i < A_COUNT; i++)
        e->attr[i] = glGetAttribLocation(e->program, attr_names[i]);
    e->u_m[0] = glGetUniformLocation(e->program, "u_m0");
    e->u_m[1] = glGetUniformLocation(e->program, "u_m1");
    e->u_m[2] = glGetUniformLocation(e->program, "u_m2");
    e->u_m[3] = glGetUniformLocation(e->program, "u_m3");
    e->u_vp = glGetUniformLocation(e->program, "u_vp");
    e->u_dr = glGetUniformLocation(e->program, "u_dr");
    e->u_bias = glGetUniformLocation(e->program, "u_bias");
    e->u_off = glGetUniformLocation(e->program, "u_off");
    e->u_scale = glGetUniformLocation(e->program, "u_scale");
    e->u_tfactor = glGetUniformLocation(e->program, "u_tfactor");
    e->u_fog = glGetUniformLocation(e->program, "u_fog");
    e->u_fog_color = glGetUniformLocation(e->program, "u_fog_color");
    e->u_aref = glGetUniformLocation(e->program, "u_aref");
    e->u_tex[0] = glGetUniformLocation(e->program, "u_tex0");
    e->u_tex[1] = glGetUniformLocation(e->program, "u_tex1");
    return e;
}

/* ---- State translation ------------------------------------------------------------ */

static GLenum gl_compare(vr_compare c)
{
    static const GLenum map[] = { GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL,
                                  GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS };
    return (unsigned)c <= VR_CMP_ALWAYS ? map[c] : GL_ALWAYS;
}

static GLenum gl_blend(vr_blend_factor f)
{
    static const GLenum map[] = {
        GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA,
        GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_DST_COLOR,
        GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA_SATURATE
    };
    static int warned;

    if (f == VR_BLEND_CONSTANT || f == VR_BLEND_INV_CONSTANT) {
        /* vitaGL has no constant blend colour. */
        if (!warned) {
            vr_log(VR_LOG_WARN, "vr_gfx: constant blend factors are not supported on Vita");
            warned = 1;
        }
        return f == VR_BLEND_CONSTANT ? GL_ONE : GL_ZERO;
    }
    return (unsigned)f <= VR_BLEND_SRC_ALPHA_SAT ? map[f] : GL_ONE;
}

static GLenum gl_blend_op(vr_blend_op op)
{
    switch (op) {
    case VR_BLEND_OP_SUBTRACT: return GL_FUNC_SUBTRACT;
    case VR_BLEND_OP_REV_SUBTRACT: return GL_FUNC_REVERSE_SUBTRACT;
    case VR_BLEND_OP_MIN: return GL_MIN;
    case VR_BLEND_OP_MAX: return GL_MAX;
    default: return GL_FUNC_ADD;
    }
}

static GLint gl_address(vr_address a)
{
    return a == VR_ADDRESS_CLAMP ? GL_CLAMP_TO_EDGE :
           (a == VR_ADDRESS_MIRROR ? GL_MIRRORED_REPEAT : GL_REPEAT);
}

static void bind_stage(int unit, const vr_texture_stage *st)
{
    GLint min;

    glActiveTexture(GL_TEXTURE0 + (GLenum)unit);
    if (!st->texture) {
        glBindTexture(GL_TEXTURE_2D, g.white);
        return;
    }
    glBindTexture(GL_TEXTURE_2D, st->texture->id);
    /* GL needs a complete mip chain; without GL_TEXTURE_MAX_LEVEL a partial
     * chain can only be sampled from level 0. */
    if (st->mip_filter == VR_MIP_NONE ||
        st->texture->levels < vr__mip_count(st->texture->width, st->texture->height))
        min = st->min_filter == VR_FILTER_LINEAR ? GL_LINEAR : GL_NEAREST;
    else if (st->mip_filter == VR_MIP_NEAREST)
        min = st->min_filter == VR_FILTER_LINEAR ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
    else
        min = st->min_filter == VR_FILTER_LINEAR ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    st->mag_filter == VR_FILTER_LINEAR ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl_address(st->address_u));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl_address(st->address_v));
}

static void apply_state(const vr_render_state *rs)
{
    if (rs->blend_enable) {
        glEnable(GL_BLEND);
        glBlendFunc(gl_blend(rs->blend_src), gl_blend(rs->blend_dst));
        glBlendEquation(gl_blend_op(rs->blend_op));
    } else {
        glDisable(GL_BLEND);
    }
    if (rs->depth_test_enable) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(gl_compare(rs->depth_func));
        glDepthMask(rs->depth_write_enable ? GL_TRUE : GL_FALSE);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    /* The image is not mirrored (NDC y up maps to the top of the screen), so
     * a triangle that looks clockwise is clockwise in GL window space too.
     * With CCW front faces, VR_CULL_CCW therefore culls GL front faces. */
    if (rs->cull == VR_CULL_NONE) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glFrontFace(GL_CCW);
        glCullFace(rs->cull == VR_CULL_CCW ? GL_FRONT : GL_BACK);
    }
    glColorMask((rs->color_write_mask & 1) ? GL_TRUE : GL_FALSE,
                (rs->color_write_mask & 2) ? GL_TRUE : GL_FALSE,
                (rs->color_write_mask & 4) ? GL_TRUE : GL_FALSE,
                (rs->color_write_mask & 8) ? GL_TRUE : GL_FALSE);
}

static void make_key(const vr_draw_desc *d, const vr_render_state *rs, shader_key *k)
{
    int i, s;

    memset(k, 0, sizeof(*k));
    for (i = 0; i < d->layout->count; i++) {
        const vr_vertex_attr *a = &d->layout->attrs[i];
        if (a->semantic == VR_ATTR_POSITION_T) {
            k->pretransformed = 1;
            k->pos_w1 = a->type != VR_ATTR_FLOAT4; /* rhw defaults to 1 */
        }
        if (a->semantic == VR_ATTR_COLOR0 && a->type == VR_ATTR_UBYTE4_BGRA)
            k->c0_bgra = 1;
        if (a->semantic == VR_ATTR_COLOR1 && a->type == VR_ATTR_UBYTE4_BGRA)
            k->c1_bgra = 1;
    }
    for (s = 0; s < VR_MAX_TEXTURE_STAGES && rs->stages[s].color_op != VR_TOP_DISABLE; s++) {
        const vr_texture_stage *st = &rs->stages[s];
        k->color_op[s] = (uint8_t)st->color_op;
        k->alpha_op[s] = (uint8_t)st->alpha_op;
        k->color_arg[s][0] = (uint8_t)st->color_arg1;
        k->color_arg[s][1] = (uint8_t)st->color_arg2;
        k->alpha_arg[s][0] = (uint8_t)st->alpha_arg1;
        k->alpha_arg[s][1] = (uint8_t)st->alpha_arg2;
        k->texcoord[s] = st->texcoord == 1;
    }
    k->stages = (uint8_t)s;
    k->fog = rs->fog_enable && rs->fog_end != rs->fog_start;
    k->specular = rs->specular_enable != 0;
    k->alpha_func = (uint8_t)(rs->alpha_test_enable ? rs->alpha_func : VR_CMP_ALWAYS);
}

static int semantic_slot(vr_attr_semantic s)
{
    switch (s) {
    case VR_ATTR_POSITION:
    case VR_ATTR_POSITION_T: return A_POS;
    case VR_ATTR_COLOR0: return A_C0;
    case VR_ATTR_COLOR1: return A_C1;
    case VR_ATTR_TEXCOORD0: return A_T0;
    default: return A_T1;
    }
}

static void attr_format(vr_attr_type t, GLint *size, GLenum *type, GLboolean *norm)
{
    *norm = GL_FALSE;
    *type = GL_FLOAT;
    switch (t) {
    case VR_ATTR_FLOAT1: *size = 1; break;
    case VR_ATTR_FLOAT2: *size = 2; break;
    case VR_ATTR_FLOAT3: *size = 3; break;
    case VR_ATTR_FLOAT4: *size = 4; break;
    default:
        *size = 4;
        *type = GL_UNSIGNED_BYTE;
        *norm = GL_TRUE;
        break;
    }
}

static GLenum gl_primitive(vr_primitive p)
{
    switch (p) {
    case VR_PRIM_POINT_LIST: return GL_POINTS;
    case VR_PRIM_LINE_LIST: return GL_LINES;
    case VR_PRIM_LINE_STRIP: return GL_LINE_STRIP;
    case VR_PRIM_TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
    case VR_PRIM_TRIANGLE_FAN: return GL_TRIANGLE_FAN;
    default: return GL_TRIANGLES;
    }
}

vr_result vr_gfx_draw(const vr_draw_desc *d)
{
    static const float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    vr_render_state defaults;
    const vr_render_state *rs;
    const float *m;
    shader_key key;
    program_entry *p;
    const uint8_t *base;
    uint32_t count;
    vr_result r;
    int i, enabled[A_COUNT] = { 0 };

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
    count = d->index_count ? d->index_count : d->vertex_count;
    if (!vr__prim_count(d->primitive, count))
        return VR_OK;
    /* The GPU does not range-check indices, so check CPU-side ones here (the
     * host backend checks all). Buffer indices must be in range: see vr_gfx.h. */
    if (d->index_count && !d->index_buffer) {
        uint32_t k;
        for (k = 0; k < d->index_count; k++)
            if (vr__index_at(d, d->index_data, k) >= d->vertex_count)
                return VR_ERR_INVALID_ARG;
    }

    make_key(d, rs, &key);
    p = get_program(&key);
    if (!p)
        return VR_ERR_UNSUPPORTED;
    glUseProgram(p->program);
    apply_state(rs);

    m = d->transform ? d->transform : identity;
    for (i = 0; i < 4; i++)
        glUniform4f(p->u_m[i], m[i], m[4 + i], m[8 + i], m[12 + i]);
    glUniform4f(p->u_vp, (float)g.vp_x, (float)g.vp_y, (float)(g.vp_w ? g.vp_w : 1),
                (float)(g.vp_h ? g.vp_h : 1));
    glUniform2f(p->u_dr, g.vp_min_z,
                g.vp_max_z != g.vp_min_z ? g.vp_max_z - g.vp_min_z : 1.0f);
    /* depth_bias is in window depth units; the shader adds it before the
     * depth-range remap, so divide by the range like the host applies it. */
    glUniform1f(p->u_bias, rs->depth_bias /
                (g.vp_max_z != g.vp_min_z ? g.vp_max_z - g.vp_min_z : 1.0f));
    glUniform2f(p->u_off, d->screen_offset[0], d->screen_offset[1]);
    glUniform2f(p->u_scale, d->screen_scale[0] != 0.0f ? d->screen_scale[0] : 1.0f,
                d->screen_scale[1] != 0.0f ? d->screen_scale[1] : 1.0f);
    glUniform4fv(p->u_tfactor, 1, rs->texture_factor);
    glUniform2f(p->u_fog, rs->fog_start, rs->fog_end);
    glUniform4fv(p->u_fog_color, 1, rs->fog_color);
    glUniform1f(p->u_aref, (float)(int)(rs->alpha_ref * 255.0f + 0.5f));
    for (i = 0; i < key.stages; i++) {
        bind_stage(i, &rs->stages[i]);
        glUniform1i(p->u_tex[i], i);
    }
    glActiveTexture(GL_TEXTURE0);

    /* Vertex attributes; base_vertex is applied by offsetting the pointers. */
    if (d->vertex_buffer) {
        glBindBuffer(GL_ARRAY_BUFFER, d->vertex_buffer->id);
        base = (const uint8_t *)(uintptr_t)d->vertex_offset;
    } else {
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        base = d->vertex_data;
    }
    base += (size_t)d->base_vertex * d->layout->stride;
    for (i = 0; i < d->layout->count; i++) {
        const vr_vertex_attr *a = &d->layout->attrs[i];
        int slot = semantic_slot(a->semantic);
        GLint size;
        GLenum type;
        GLboolean norm;

        if (p->attr[slot] < 0)
            continue;
        attr_format(a->type, &size, &type, &norm);
        glEnableVertexAttribArray((GLuint)p->attr[slot]);
        glVertexAttribPointer((GLuint)p->attr[slot], size, type, norm, (GLsizei)d->layout->stride,
                              base + a->offset);
        enabled[slot] = 1;
    }
    for (i = 0; i < A_COUNT; i++) {
        if (enabled[i] || p->attr[i] < 0)
            continue;
        glDisableVertexAttribArray((GLuint)p->attr[i]);
        /* Same defaults as the host: white diffuse, zero specular (alpha 0). */
        if (i == A_C0)
            glVertexAttrib4f((GLuint)p->attr[i], 1, 1, 1, 1);
        else if (i == A_C1)
            glVertexAttrib4f((GLuint)p->attr[i], 0, 0, 0, 0);
        else
            glVertexAttrib4f((GLuint)p->attr[i], 0, 0, 0, 1);
    }

    if (d->index_count) {
        const void *indices;
        if (d->index_buffer) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, d->index_buffer->id);
            indices = (const void *)(uintptr_t)d->index_offset;
        } else {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            indices = d->index_data;
        }
        glDrawElements(gl_primitive(d->primitive), (GLsizei)d->index_count,
                       d->index_type == VR_INDEX_32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, indices);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    } else {
        glDrawArrays(gl_primitive(d->primitive), 0, (GLsizei)d->vertex_count);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    for (i = 0; i < A_COUNT; i++)
        if (enabled[i])
            glDisableVertexAttribArray((GLuint)p->attr[i]);
    return VR_OK;
}
