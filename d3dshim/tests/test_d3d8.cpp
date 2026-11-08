// Tests for the Direct3D 8 shim, rendered by the host reference rasterizer.
// The real target is 64x48 and the application's back buffer is 32x24, so
// every test also checks the 2x coordinate scaling.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <d3d8.h>

extern "C" {
#include "vr/vr_gfx.h"
}

static int g_failures;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                           \
        }                                                                           \
    } while (0)

static const int RW = 64, RH = 48; // real
static uint8_t g_px[RW * RH * 4];

static void read_all()
{
    CHECK(vr_gfx_read_pixels(0, 0, RW, RH, g_px) == VR_OK);
}

// Real-pixel colour check with +-2 tolerance.
static bool px_is(int x, int y, int r, int g, int b)
{
    const uint8_t *p = g_px + ((size_t)y * RW + (size_t)x) * 4;
    return abs(p[0] - r) <= 2 && abs(p[1] - g) <= 2 && abs(p[2] - b) <= 2;
}

struct TLVertex {
    float x, y, z, rhw;
    D3DCOLOR color;
    float u, v;
};
static const DWORD kTLFVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

struct LVertex {
    float x, y, z;
    float nx, ny, nz;
};
static const DWORD kLFVF = D3DFVF_XYZ | D3DFVF_NORMAL;

struct CVertex {
    float x, y, z;
    D3DCOLOR color;
};
static const DWORD kCFVF = D3DFVF_XYZ | D3DFVF_DIFFUSE;

// Quad in D3D pixel convention: (-0.5, -0.5)..(x1 - 0.5, y1 - 0.5) covers
// pixels [x0, x1) x [y0, y1) exactly, with texels centred on pixels.
static void tl_quad(TLVertex v[4], float x0, float y0, float x1, float y1, D3DCOLOR c)
{
    const float o = -0.5f;
    TLVertex q[4] = { { x0 + o, y0 + o, 0.5f, 1, c, 0, 0 }, { x1 + o, y0 + o, 0.5f, 1, c, 1, 0 },
                      { x0 + o, y1 + o, 0.5f, 1, c, 0, 1 }, { x1 + o, y1 + o, 0.5f, 1, c, 1, 1 } };
    memcpy(v, q, sizeof(q));
}

static void test_clear_and_pretransformed(IDirect3DDevice8 *dev)
{
    TLVertex v[4];

    CHECK(dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 255), 1.0f, 0) == D3D_OK);
    dev->SetVertexShader(kTLFVF);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    tl_quad(v, 2, 2, 6, 4, D3DCOLOR_XRGB(255, 0, 0));
    CHECK(dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(TLVertex)) == D3D_OK);
    read_all();
    // Virtual pixels 2..5 x 2..3 = real 4..11 x 4..7.
    CHECK(px_is(4, 4, 255, 0, 0));
    CHECK(px_is(11, 7, 255, 0, 0));
    CHECK(px_is(3, 4, 0, 0, 255));
    CHECK(px_is(12, 4, 0, 0, 255));
    CHECK(px_is(4, 8, 0, 0, 255));
    CHECK(px_is(4, 3, 0, 0, 255));

    // Clear with a rectangle (virtual 0..2 x 0..1 -> real 0..4 x 0..2).
    D3DRECT r = { 0, 0, 2, 1 };
    dev->Clear(1, &r, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 255, 0), 1.0f, 0);
    read_all();
    CHECK(px_is(3, 1, 0, 255, 0));
    CHECK(px_is(4, 1, 0, 0, 255));
    CHECK(px_is(0, 2, 0, 0, 255));
}

static void test_texture(IDirect3DDevice8 *dev)
{
    IDirect3DTexture8 *tex = NULL;
    D3DLOCKED_RECT lr;
    D3DSURFACE_DESC desc;
    TLVertex v[4];

    CHECK(dev->CreateTexture(2, 2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex) == D3D_OK);
    if (!tex)
        return;
    CHECK(tex->GetLevelCount() == 1);
    CHECK(tex->GetLevelDesc(0, &desc) == D3D_OK && desc.Width == 2 && desc.Size == 16);
    CHECK(tex->LockRect(0, &lr, NULL, 0) == D3D_OK && lr.Pitch == 8);
    uint32_t *row0 = (uint32_t *)lr.pBits, *row1 = (uint32_t *)((uint8_t *)lr.pBits + lr.Pitch);
    row0[0] = 0xFFFF0000u; // ARGB red
    row0[1] = 0xFF00FF00u; // green
    row1[0] = 0xFF0000FFu; // blue
    row1[1] = 0xFFFFFFFFu; // white
    CHECK(tex->UnlockRect(0) == D3D_OK);

    dev->Clear(0, NULL, D3DCLEAR_TARGET, 0, 1.0f, 0);
    dev->SetTexture(0, tex);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetVertexShader(kTLFVF);
    tl_quad(v, 0, 0, 2, 2, D3DCOLOR_XRGB(255, 255, 255));
    CHECK(dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(TLVertex)) == D3D_OK);
    read_all();
    // Each texel covers one virtual pixel = 2x2 real pixels.
    CHECK(px_is(0, 0, 255, 0, 0));
    CHECK(px_is(1, 1, 255, 0, 0));
    CHECK(px_is(2, 0, 0, 255, 0));
    CHECK(px_is(0, 2, 0, 0, 255));
    CHECK(px_is(3, 3, 255, 255, 255));
    dev->SetTexture(0, NULL);
    CHECK(tex->Release() == 0);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_CURRENT);
}

static void test_indexed_buffers(IDirect3DDevice8 *dev)
{
    IDirect3DVertexBuffer8 *vb = NULL;
    IDirect3DIndexBuffer8 *ib = NULL;
    BYTE *p;
    // Two dummy vertices, then a full-screen quad in clip space.
    const CVertex verts[6] = { { 9, 9, 9, 0 }, { 9, 9, 9, 0 },
                               { -1, 1, 0.5f, D3DCOLOR_XRGB(255, 255, 0) }, { 1, 1, 0.5f, D3DCOLOR_XRGB(255, 255, 0) },
                               { -1, -1, 0.5f, D3DCOLOR_XRGB(255, 255, 0) }, { 1, -1, 0.5f, D3DCOLOR_XRGB(255, 255, 0) } };
    const uint16_t idx[6] = { 0, 1, 2, 1, 3, 2 };

    CHECK(dev->CreateVertexBuffer(sizeof(verts), D3DUSAGE_WRITEONLY, kCFVF, D3DPOOL_MANAGED, &vb) == D3D_OK);
    CHECK(dev->CreateIndexBuffer(sizeof(idx), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib) == D3D_OK);
    CHECK(vb->Lock(0, 0, &p, 0) == D3D_OK);
    memcpy(p, verts, sizeof(verts));
    vb->Unlock();
    CHECK(ib->Lock(0, sizeof(idx), &p, 0) == D3D_OK);
    memcpy(p, idx, sizeof(idx));
    ib->Unlock();

    dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetVertexShader(kCFVF);
    dev->SetStreamSource(0, vb, sizeof(CVertex));
    dev->SetIndices(ib, 2); // base vertex skips the dummies
    CHECK(dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 4, 0, 2) == D3D_OK);
    read_all();
    CHECK(px_is(0, 0, 255, 255, 0));
    CHECK(px_is(63, 47, 255, 255, 0));
    CHECK(px_is(32, 24, 255, 255, 0));

    // Out-of-range index data is rejected.
    CHECK(dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 4, 3, 2) == D3DERR_INVALIDCALL);

    dev->SetStreamSource(0, NULL, 0);
    dev->SetIndices(NULL, 0);
    CHECK(vb->Release() == 0);
    CHECK(ib->Release() == 0);
}

static void test_lighting(IDirect3DDevice8 *dev)
{
    D3DLIGHT8 light;
    D3DMATERIAL8 mat;
    // Full-screen quad facing -z (towards a light shining along +z).
    const LVertex quad[4] = { { -1, 1, 0.5f, 0, 0, -1 }, { 1, 1, 0.5f, 0, 0, -1 },
                              { -1, -1, 0.5f, 0, 0, -1 }, { 1, -1, 0.5f, 0, 0, -1 } };
    const uint16_t idx[6] = { 0, 1, 2, 1, 3, 2 };

    memset(&light, 0, sizeof(light));
    light.Type = D3DLIGHT_DIRECTIONAL;
    light.Diffuse.r = 1.0f;
    light.Diffuse.g = 0.5f;
    light.Direction.z = 1.0f;
    memset(&mat, 0, sizeof(mat));
    mat.Diffuse.r = mat.Diffuse.g = mat.Diffuse.b = mat.Diffuse.a = 1.0f;
    mat.Ambient.b = 1.0f;

    dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    dev->SetRenderState(D3DRS_LIGHTING, TRUE);
    dev->SetRenderState(D3DRS_AMBIENT, D3DCOLOR_XRGB(0, 0, 128));
    CHECK(dev->SetLight(0, &light) == D3D_OK);
    CHECK(dev->LightEnable(0, TRUE) == D3D_OK);
    CHECK(dev->SetMaterial(&mat) == D3D_OK);
    dev->SetVertexShader(kLFVF);
    CHECK(dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, idx, D3DFMT_INDEX16, quad,
                                      sizeof(LVertex)) == D3D_OK);
    read_all();
    // diffuse (1, 0.5, 0) from the light + ambient 0.5 blue * material ambient
    CHECK(px_is(10, 10, 255, 128, 128));

    // A light from behind contributes nothing; only ambient remains.
    light.Direction.z = -1.0f;
    dev->SetLight(0, &light);
    dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(LVertex));
    read_all();
    CHECK(px_is(10, 10, 0, 0, 128));

    // Lit indexed draw that starts at a non-zero min index.
    const uint16_t idx2[6] = { 1, 2, 3, 1, 3, 2 };
    light.Direction.z = 1.0f;
    dev->SetLight(0, &light);
    dev->SetRenderState(D3DRS_AMBIENT, 0);
    dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    CHECK(dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 1, 3, 1, idx2, D3DFMT_INDEX16, quad,
                                      sizeof(LVertex)) == D3D_OK);
    read_all();
    CHECK(px_is(60, 44, 255, 128, 0)); // bottom-right triangle (1, 2, 3) drawn
    CHECK(px_is(2, 2, 0, 0, 0));       // top-left corner (vertex 0) not drawn
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
}

// XYZB1: one blend weight between position and normal (skipped by the shim).
static void test_blend_weight_fvf(IDirect3DDevice8 *dev)
{
    struct BVertex {
        float x, y, z, w0;
        D3DCOLOR color;
    };
    const DWORD fvf = D3DFVF_XYZB1 | D3DFVF_DIFFUSE;
    const D3DCOLOR c = D3DCOLOR_XRGB(0, 255, 255);
    const BVertex quad[4] = { { -1, 1, 0.5f, 0, c }, { 1, 1, 0.5f, 0, c },
                              { -1, -1, 0.5f, 0, c }, { 1, -1, 0.5f, 0, c } };

    dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    CHECK(dev->SetVertexShader(fvf) == D3D_OK);
    CHECK(dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(BVertex)) == D3D_OK);
    read_all();
    CHECK(px_is(20, 20, 0, 255, 255));
}

// Normals follow the inverse transpose of the world matrix: under a uniform
// world scale of 2 an unnormalized normal halves in length, as in D3D.
static void test_scaled_world_lighting(IDirect3DDevice8 *dev)
{
    D3DLIGHT8 light;
    D3DMATERIAL8 mat;
    D3DMATRIX world, id;
    const LVertex quad[4] = { { -0.5f, 0.5f, 0.5f, 0, 0, -1 }, { 0.5f, 0.5f, 0.5f, 0, 0, -1 },
                              { -0.5f, -0.5f, 0.5f, 0, 0, -1 }, { 0.5f, -0.5f, 0.5f, 0, 0, -1 } };

    memset(&light, 0, sizeof(light));
    light.Type = D3DLIGHT_DIRECTIONAL;
    light.Diffuse.r = light.Diffuse.g = light.Diffuse.b = 1.0f;
    light.Direction.z = 1.0f;
    memset(&mat, 0, sizeof(mat));
    mat.Diffuse.r = mat.Diffuse.g = mat.Diffuse.b = mat.Diffuse.a = 1.0f;
    memset(&world, 0, sizeof(world));
    world._11 = world._22 = 2.0f;
    world._33 = 2.0f;
    world._44 = 1.0f;
    world._43 = -0.5f; /* keep z = 0.5 after scaling */
    dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    dev->SetRenderState(D3DRS_LIGHTING, TRUE);
    dev->SetRenderState(D3DRS_AMBIENT, 0);
    dev->SetLight(0, &light);
    dev->LightEnable(0, TRUE);
    dev->SetMaterial(&mat);
    dev->SetTransform(D3DTS_WORLD, &world);
    dev->SetVertexShader(kLFVF);
    CHECK(dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(LVertex)) == D3D_OK);
    read_all();
    CHECK(px_is(32, 24, 128, 128, 128));
    memset(&id, 0, sizeof(id));
    id._11 = id._22 = id._33 = id._44 = 1.0f;
    dev->SetTransform(D3DTS_WORLD, &id);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);

    // Huge light indices are rejected rather than overflowing.
    CHECK(dev->SetLight(0xFFFFFFFFu, &light) == D3DERR_INVALIDCALL);
    CHECK(dev->LightEnable(0xFFFFFFFFu, TRUE) == D3DERR_INVALIDCALL);
}

static void test_state_and_transform(IDirect3DDevice8 *dev)
{
    DWORD v = 0;
    D3DMATRIX m, out;
    D3DVIEWPORT8 vp;

    CHECK(dev->GetRenderState(D3DRS_ZFUNC, &v) == D3D_OK && v == D3DCMP_LESSEQUAL);
    CHECK(dev->GetTextureStageState(1, D3DTSS_COLOROP, &v) == D3D_OK && v == D3DTOP_DISABLE);
    CHECK(dev->GetViewport(&vp) == D3D_OK && vp.Width == 32 && vp.Height == 24);
    memset(&m, 0, sizeof(m));
    m._11 = m._22 = m._33 = m._44 = 2.0f;
    dev->SetTransform(D3DTS_WORLD, &m);
    dev->MultiplyTransform(D3DTS_WORLD, &m);
    CHECK(dev->GetTransform(D3DTS_WORLD, &out) == D3D_OK && out._11 == 4.0f && out._44 == 4.0f);
    m._11 = m._22 = m._33 = m._44 = 1.0f;
    dev->SetTransform(D3DTS_WORLD, &m);
    CHECK(dev->SetPixelShader(1) == D3DERR_INVALIDCALL);
    CHECK(dev->SetVertexShader(0x12345) == D3DERR_INVALIDCALL);
}

int main()
{
    vr_gfx_desc gd = { RW, RH, 0 };
    D3DPRESENT_PARAMETERS pp;
    IDirect3DDevice8 *dev = NULL;
    D3DDISPLAYMODE mode;

    CHECK(vr_gfx_init(&gd) == VR_OK);
    IDirect3D8 *d3d = Direct3DCreate8(D3D_SDK_VERSION);
    CHECK(d3d != NULL);
    CHECK(d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode) == D3D_OK);
    CHECK(d3d->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0, D3DRTYPE_TEXTURE, D3DFMT_DXT5) == D3D_OK);
    CHECK(d3d->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0, D3DRTYPE_TEXTURE, D3DFMT_P8) ==
          D3DERR_NOTAVAILABLE);
    memset(&pp, 0, sizeof(pp));
    pp.BackBufferWidth = 32;
    pp.BackBufferHeight = 24;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D16;
    pp.Windowed = TRUE;
    CHECK(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING,
                            &pp, &dev) == D3D_OK);
    if (!dev)
        return 1;

    dev->BeginScene();
    test_state_and_transform(dev);
    test_clear_and_pretransformed(dev);
    test_texture(dev);
    test_indexed_buffers(dev);
    test_lighting(dev);
    test_blend_weight_fvf(dev);
    test_scaled_world_lighting(dev);
    dev->EndScene();
    CHECK(dev->Present(NULL, NULL, NULL, NULL) == D3D_OK);

    CHECK(dev->Release() == 0);
    CHECK(d3d->Release() == 0);
    vr_gfx_shutdown();
    if (g_failures) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all d3d8 tests passed\n");
    return 0;
}
