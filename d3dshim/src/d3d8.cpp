// Direct3D 8 fixed-function subset on top of vr_gfx. See d3dshim/README.md
// for what is and is not covered.

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <new>
#include <vector>

#include "vr_d3d8.h"

extern "C" {
#include "vr/vr_gfx.h"
#include "vr/vr_platform.h"
}

namespace {

const int kStages = 8;

float dw2f(DWORD v)
{
    float f;
    memcpy(&f, &v, 4);
    return f;
}

DWORD f2dw(float f)
{
    DWORD v;
    memcpy(&v, &f, 4);
    return v;
}

void argb_to_rgba(D3DCOLOR c, float out[4])
{
    out[0] = ((c >> 16) & 0xff) / 255.0f;
    out[1] = ((c >> 8) & 0xff) / 255.0f;
    out[2] = (c & 0xff) / 255.0f;
    out[3] = ((c >> 24) & 0xff) / 255.0f;
}

void warn_once(bool &flag, const char *what)
{
    if (!flag) {
        vr_log(VR_LOG_WARN, "d3d8 shim: %s is not supported", what);
        flag = true;
    }
}

void identity(D3DMATRIX *m)
{
    memset(m, 0, sizeof(*m));
    m->_11 = m->_22 = m->_33 = m->_44 = 1.0f;
}

void multiply(const D3DMATRIX &a, const D3DMATRIX &b, D3DMATRIX *out)
{
    D3DMATRIX r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j] +
                        a.m[i][3] * b.m[3][j];
    *out = r;
}

bool map_format(D3DFORMAT f, vr_format *out)
{
    switch (f) {
    case D3DFMT_A8R8G8B8: *out = VR_FMT_BGRA8; return true;
    case D3DFMT_X8R8G8B8: *out = VR_FMT_BGRX8; return true;
    case D3DFMT_R5G6B5: *out = VR_FMT_R5G6B5; return true;
    case D3DFMT_A1R5G5B5: *out = VR_FMT_A1R5G5B5; return true;
    case D3DFMT_X1R5G5B5: *out = VR_FMT_X1R5G5B5; return true;
    case D3DFMT_A4R4G4B4: *out = VR_FMT_A4R4G4B4; return true;
    case D3DFMT_L8: *out = VR_FMT_L8; return true;
    case D3DFMT_A8: *out = VR_FMT_A8; return true;
    case D3DFMT_A8L8: *out = VR_FMT_A8L8; return true;
    case D3DFMT_DXT1: *out = VR_FMT_DXT1; return true;
    case D3DFMT_DXT2: /* premultiplied variants decode the same way */
    case D3DFMT_DXT3: *out = VR_FMT_DXT3; return true;
    case D3DFMT_DXT4:
    case D3DFMT_DXT5: *out = VR_FMT_DXT5; return true;
    default: return false;
    }
}

class Device;

// ---- Resources -----------------------------------------------------------------------

template <class Base>
class Resource : public Base {
public:
    explicit Resource(Device *dev) : refs_(1), device_(dev) {}
    HRESULT QueryInterface(REFIID, void **ppv) override
    {
        if (ppv)
            *ppv = nullptr;
        return E_NOINTERFACE;
    }
    unsigned long AddRef() override { return ++refs_; }
    unsigned long Release() override
    {
        unsigned long r = --refs_;
        if (!r)
            delete this;
        return r;
    }
    HRESULT GetDevice(IDirect3DDevice8 **ppDevice) override;
    DWORD SetPriority(DWORD p) override
    {
        DWORD old = priority_;
        priority_ = p;
        return old;
    }
    DWORD GetPriority() override { return priority_; }
    void PreLoad() override {}

protected:
    unsigned long refs_;
    Device *device_;
    DWORD priority_ = 0;
};

class Texture final : public Resource<IDirect3DTexture8> {
public:
    Texture(Device *dev) : Resource(dev) {}
    ~Texture() override { vr_texture_destroy(tex_); }

    HRESULT init(UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool)
    {
        vr_texture_desc d;
        if (!map_format(fmt, &vfmt_) || (usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)))
            return D3DERR_INVALIDCALL;
        d.width = (int)w;
        d.height = (int)h;
        d.levels = (int)levels;
        d.format = vfmt_;
        if (vr_texture_create(&d, &tex_) != VR_OK)
            return D3DERR_INVALIDCALL;
        width_ = w;
        height_ = h;
        format_ = fmt;
        usage_ = usage;
        pool_ = pool;
        UINT full = 1;
        for (UINT x = w, y = h; x > 1 || y > 1; x = x > 1 ? x / 2 : 1, y = y > 1 ? y / 2 : 1)
            full++;
        levels_ = levels ? levels : full;
        shadow_.resize(levels_);
        return D3D_OK;
    }

    D3DRESOURCETYPE GetType() override { return D3DRTYPE_TEXTURE; }
    DWORD SetLOD(DWORD) override { return 0; }
    DWORD GetLOD() override { return 0; }
    DWORD GetLevelCount() override { return levels_; }

    HRESULT GetLevelDesc(UINT level, D3DSURFACE_DESC *d) override
    {
        if (!d || level >= levels_)
            return D3DERR_INVALIDCALL;
        d->Format = format_;
        d->Type = D3DRTYPE_SURFACE;
        d->Usage = usage_;
        d->Pool = pool_;
        d->Width = dim(width_, level);
        d->Height = dim(height_, level);
        d->Size = (UINT)vr_format_level_bytes(vfmt_, (int)d->Width, (int)d->Height);
        d->MultiSampleType = D3DMULTISAMPLE_NONE;
        return D3D_OK;
    }

    HRESULT LockRect(UINT level, D3DLOCKED_RECT *lr, const RECT *rect, DWORD) override
    {
        if (!lr || level >= levels_)
            return D3DERR_INVALIDCALL;
        int w = (int)dim(width_, level), h = (int)dim(height_, level);
        std::vector<uint8_t> &s = shadow_[level];
        if (s.empty())
            s.resize(vr_format_level_bytes(vfmt_, w, h));
        size_t pitch = vr_format_row_bytes(vfmt_, w);
        uint8_t *p = s.data();
        if (rect) {
            if (rect->left < 0 || rect->top < 0 || rect->right > w || rect->bottom > h)
                return D3DERR_INVALIDCALL;
            if (vr_format_is_compressed(vfmt_))
                p += (size_t)(rect->top / 4) * pitch + vr_format_row_bytes(vfmt_, rect->left) *
                     (rect->left ? 1 : 0);
            else
                p += (size_t)rect->top * pitch + vr_format_row_bytes(vfmt_, (int)rect->left) *
                     (rect->left ? 1 : 0);
        }
        lr->Pitch = (INT)pitch;
        lr->pBits = p;
        return D3D_OK;
    }

    HRESULT UnlockRect(UINT level) override
    {
        if (level >= levels_ || shadow_[level].empty())
            return D3DERR_INVALIDCALL;
        return vr_texture_update(tex_, (int)level, shadow_[level].data()) == VR_OK
                   ? D3D_OK : D3DERR_INVALIDCALL;
    }

    vr_texture *handle() const { return tex_; }

private:
    static UINT dim(UINT v, UINT level)
    {
        v >>= level;
        return v ? v : 1;
    }

    vr_texture *tex_ = nullptr;
    vr_format vfmt_ = VR_FMT_RGBA8;
    UINT width_ = 0, height_ = 0, levels_ = 0;
    D3DFORMAT format_ = D3DFMT_UNKNOWN;
    DWORD usage_ = 0;
    D3DPOOL pool_ = D3DPOOL_DEFAULT;
    std::vector<std::vector<uint8_t>> shadow_;
};

// Vertex and index buffers keep a CPU copy: Lock hands it out, Unlock
// uploads the locked range, and CPU lighting reads it.
template <class Base, D3DRESOURCETYPE kType>
class Buffer : public Resource<Base> {
public:
    Buffer(Device *dev) : Resource<Base>(dev) {}
    ~Buffer() override { vr_buffer_destroy(buf_); }

    HRESULT init(UINT length, DWORD usage, DWORD fvf_or_format, D3DPOOL pool)
    {
        if (!length)
            return D3DERR_INVALIDCALL;
        data_.assign(length, 0);
        if (vr_buffer_create(kType == D3DRTYPE_INDEXBUFFER ? VR_BUFFER_INDEX : VR_BUFFER_VERTEX,
                             length, nullptr, &buf_) != VR_OK)
            return D3DERR_OUTOFVIDEOMEMORY;
        usage_ = usage;
        extra_ = fvf_or_format;
        pool_ = pool;
        return D3D_OK;
    }

    D3DRESOURCETYPE GetType() override { return kType; }

    HRESULT Lock(UINT offset, UINT size, BYTE **out, DWORD flags) override
    {
        if (!out || offset > data_.size())
            return D3DERR_INVALIDCALL;
        if (!size)
            size = (UINT)data_.size() - offset;
        if (size > data_.size() - offset)
            return D3DERR_INVALIDCALL;
        *out = data_.data() + offset;
        if (!(flags & D3DLOCK_READONLY)) {
            lo_ = lo_ < offset ? lo_ : offset;
            hi_ = hi_ > offset + size ? hi_ : offset + size;
        }
        return D3D_OK;
    }

    HRESULT Unlock() override
    {
        if (hi_ > lo_)
            vr_buffer_update(buf_, lo_, data_.data() + lo_, hi_ - lo_);
        lo_ = (UINT)-1;
        hi_ = 0;
        return D3D_OK;
    }

    void fill_desc(D3DFORMAT fmt, D3DVERTEXBUFFER_DESC *d) const
    {
        d->Format = fmt;
        d->Type = kType;
        d->Usage = usage_;
        d->Pool = pool_;
        d->Size = (UINT)data_.size();
        d->FVF = extra_;
    }

    const uint8_t *data() const { return data_.data(); }
    size_t size() const { return data_.size(); }
    vr_buffer *handle() const { return buf_; }
    DWORD extra() const { return extra_; }

private:
    std::vector<uint8_t> data_;
    vr_buffer *buf_ = nullptr;
    DWORD usage_ = 0, extra_ = 0;
    D3DPOOL pool_ = D3DPOOL_DEFAULT;
    UINT lo_ = (UINT)-1, hi_ = 0;
};

class VertexBuffer final : public Buffer<IDirect3DVertexBuffer8, D3DRTYPE_VERTEXBUFFER> {
public:
    explicit VertexBuffer(Device *dev) : Buffer(dev) {}
    HRESULT GetDesc(D3DVERTEXBUFFER_DESC *d) override
    {
        if (!d)
            return D3DERR_INVALIDCALL;
        fill_desc(D3DFMT_VERTEXDATA, d);
        return D3D_OK;
    }
};

class IndexBuffer final : public Buffer<IDirect3DIndexBuffer8, D3DRTYPE_INDEXBUFFER> {
public:
    explicit IndexBuffer(Device *dev) : Buffer(dev) {}
    HRESULT GetDesc(D3DINDEXBUFFER_DESC *d) override
    {
        if (!d)
            return D3DERR_INVALIDCALL;
        D3DVERTEXBUFFER_DESC v;
        fill_desc((D3DFORMAT)extra(), &v);
        d->Format = v.Format;
        d->Type = v.Type;
        d->Usage = v.Usage;
        d->Pool = v.Pool;
        d->Size = v.Size;
        return D3D_OK;
    }
};

// ---- FVF decoding --------------------------------------------------------------------

struct FvfInfo {
    bool     rhw = false, has_normal = false, has_diffuse = false, has_specular = false;
    uint32_t pos = 0, normal = 0, diffuse = 0, specular = 0, stride = 0;
    int      tex_count = 0;
    uint32_t tex[8] = {};
    int      tex_size[8] = {};
};

bool decode_fvf(DWORD fvf, FvfInfo *f)
{
    DWORD pos = fvf & D3DFVF_POSITION_MASK;
    uint32_t off = 0;

    *f = FvfInfo();
    if (pos == D3DFVF_XYZRHW) {
        f->rhw = true;
        off = 16;
    } else if (pos == D3DFVF_XYZ) {
        off = 12;
    } else if (pos >= D3DFVF_XYZB1 && pos <= D3DFVF_XYZB5) {
        off = 12 + 4 * ((pos - D3DFVF_XYZ) / 2); /* blend weights are skipped */
    } else {
        return false;
    }
    if (fvf & D3DFVF_NORMAL) {
        f->has_normal = true;
        f->normal = off;
        off += 12;
    }
    if (fvf & D3DFVF_PSIZE)
        off += 4;
    if (fvf & D3DFVF_DIFFUSE) {
        f->has_diffuse = true;
        f->diffuse = off;
        off += 4;
    }
    if (fvf & D3DFVF_SPECULAR) {
        f->has_specular = true;
        f->specular = off;
        off += 4;
    }
    f->tex_count = (int)((fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT);
    if (f->tex_count > 8)
        return false;
    for (int i = 0; i < f->tex_count; i++) {
        static const int sizes[4] = { 2, 3, 4, 1 };
        f->tex_size[i] = sizes[(fvf >> (16 + 2 * i)) & 3];
        f->tex[i] = off;
        off += 4 * (uint32_t)f->tex_size[i];
    }
    f->stride = off;
    return true;
}

vr_attr_type float_type(int n)
{
    return n == 1 ? VR_ATTR_FLOAT1 : n == 2 ? VR_ATTR_FLOAT2 : n == 3 ? VR_ATTR_FLOAT3 : VR_ATTR_FLOAT4;
}

// ---- Device --------------------------------------------------------------------------

struct Light {
    D3DLIGHT8 desc;
    bool      enabled;
};

// Vertex produced by CPU lighting.
struct LitVertex {
    float   x, y, z;
    uint8_t c0[4], c1[4]; /* RGBA */
    float   t0[2], t1[2];
};

class Direct3D;

class Device final : public IDirect3DDevice8 {
public:
    Device(Direct3D *d3d, const D3DPRESENT_PARAMETERS &pp);
    ~Device() override;

    HRESULT QueryInterface(REFIID, void **ppv) override
    {
        if (ppv)
            *ppv = nullptr;
        return E_NOINTERFACE;
    }
    unsigned long AddRef() override { return ++refs_; }
    unsigned long Release() override
    {
        unsigned long r = --refs_;
        if (!r)
            delete this;
        return r;
    }

    HRESULT GetDirect3D(IDirect3D8 **out) override;
    HRESULT TestCooperativeLevel() override { return D3D_OK; }
    HRESULT Reset(D3DPRESENT_PARAMETERS *pp) override
    {
        if (!pp)
            return D3DERR_INVALIDCALL;
        set_virtual_size(pp->BackBufferWidth, pp->BackBufferHeight);
        reset_state(pp->EnableAutoDepthStencil != 0);
        return D3D_OK;
    }
    HRESULT Present(const RECT *, const RECT *, HWND, const RGNDATA *) override
    {
        vr_gfx_end_frame();
        return D3D_OK;
    }

    HRESULT CreateTexture(UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool,
                          IDirect3DTexture8 **out) override
    {
        if (!out)
            return D3DERR_INVALIDCALL;
        Texture *t = new (std::nothrow) Texture(this);
        if (!t)
            return E_OUTOFMEMORY;
        HRESULT hr = t->init(w, h, levels, usage, fmt, pool);
        if (FAILED(hr)) {
            t->Release();
            return hr;
        }
        *out = t;
        return D3D_OK;
    }

    HRESULT CreateVertexBuffer(UINT len, DWORD usage, DWORD fvf, D3DPOOL pool,
                               IDirect3DVertexBuffer8 **out) override
    {
        if (!out)
            return D3DERR_INVALIDCALL;
        VertexBuffer *b = new (std::nothrow) VertexBuffer(this);
        if (!b)
            return E_OUTOFMEMORY;
        HRESULT hr = b->init(len, usage, fvf, pool);
        if (FAILED(hr)) {
            b->Release();
            return hr;
        }
        *out = b;
        return D3D_OK;
    }

    HRESULT CreateIndexBuffer(UINT len, DWORD usage, D3DFORMAT fmt, D3DPOOL pool,
                              IDirect3DIndexBuffer8 **out) override
    {
        if (!out || (fmt != D3DFMT_INDEX16 && fmt != D3DFMT_INDEX32))
            return D3DERR_INVALIDCALL;
        IndexBuffer *b = new (std::nothrow) IndexBuffer(this);
        if (!b)
            return E_OUTOFMEMORY;
        HRESULT hr = b->init(len, usage, (DWORD)fmt, pool);
        if (FAILED(hr)) {
            b->Release();
            return hr;
        }
        *out = b;
        return D3D_OK;
    }

    HRESULT BeginScene() override
    {
        vr_gfx_begin_frame();
        return D3D_OK;
    }
    HRESULT EndScene() override { return D3D_OK; }

    HRESULT Clear(DWORD count, const D3DRECT *rects, DWORD flags, D3DCOLOR color, float z,
                  DWORD) override
    {
        float rgba[4];
        unsigned vf = 0;
        argb_to_rgba(color, rgba);
        if (flags & D3DCLEAR_TARGET)
            vf |= VR_CLEAR_COLOR;
        if (flags & D3DCLEAR_ZBUFFER)
            vf |= VR_CLEAR_DEPTH;
        if (!count || !rects) {
            vr_gfx_clear(vf, rgba, z);
            return D3D_OK;
        }
        for (DWORD i = 0; i < count; i++) {
            // Clip each rectangle to the viewport, as D3D does.
            LONG x1 = rects[i].x1 > (LONG)vp_.X ? rects[i].x1 : (LONG)vp_.X;
            LONG y1 = rects[i].y1 > (LONG)vp_.Y ? rects[i].y1 : (LONG)vp_.Y;
            LONG x2 = rects[i].x2 < (LONG)(vp_.X + vp_.Width) ? rects[i].x2 : (LONG)(vp_.X + vp_.Width);
            LONG y2 = rects[i].y2 < (LONG)(vp_.Y + vp_.Height) ? rects[i].y2 : (LONG)(vp_.Y + vp_.Height);
            if (x2 <= x1 || y2 <= y1)
                continue;
            apply_viewport(x1, y1, x2 - x1, y2 - y1, vp_.MinZ, vp_.MaxZ);
            vr_gfx_clear(vf, rgba, z);
        }
        apply_viewport((LONG)vp_.X, (LONG)vp_.Y, (LONG)vp_.Width, (LONG)vp_.Height, vp_.MinZ, vp_.MaxZ);
        return D3D_OK;
    }

    HRESULT SetTransform(D3DTRANSFORMSTATETYPE s, const D3DMATRIX *m) override
    {
        D3DMATRIX *dst = transform(s);
        if (!dst || !m)
            return D3DERR_INVALIDCALL;
        *dst = *m;
        return D3D_OK;
    }
    HRESULT GetTransform(D3DTRANSFORMSTATETYPE s, D3DMATRIX *m) override
    {
        D3DMATRIX *src = transform(s);
        if (!src || !m)
            return D3DERR_INVALIDCALL;
        *m = *src;
        return D3D_OK;
    }
    HRESULT MultiplyTransform(D3DTRANSFORMSTATETYPE s, const D3DMATRIX *m) override
    {
        D3DMATRIX *dst = transform(s);
        if (!dst || !m)
            return D3DERR_INVALIDCALL;
        multiply(*m, *dst, dst);
        return D3D_OK;
    }

    HRESULT SetViewport(const D3DVIEWPORT8 *v) override
    {
        if (!v)
            return D3DERR_INVALIDCALL;
        vp_ = *v;
        apply_viewport((LONG)v->X, (LONG)v->Y, (LONG)v->Width, (LONG)v->Height, v->MinZ, v->MaxZ);
        return D3D_OK;
    }
    HRESULT GetViewport(D3DVIEWPORT8 *v) override
    {
        if (!v)
            return D3DERR_INVALIDCALL;
        *v = vp_;
        return D3D_OK;
    }

    HRESULT SetMaterial(const D3DMATERIAL8 *m) override
    {
        if (!m)
            return D3DERR_INVALIDCALL;
        material_ = *m;
        return D3D_OK;
    }
    HRESULT GetMaterial(D3DMATERIAL8 *m) override
    {
        if (!m)
            return D3DERR_INVALIDCALL;
        *m = material_;
        return D3D_OK;
    }

    HRESULT SetLight(DWORD i, const D3DLIGHT8 *l) override
    {
        if (!l)
            return D3DERR_INVALIDCALL;
        if (i >= lights_.size())
            lights_.resize(i + 1, Light{ default_light(), false });
        lights_[i].desc = *l;
        return D3D_OK;
    }
    HRESULT GetLight(DWORD i, D3DLIGHT8 *l) override
    {
        if (!l || i >= lights_.size())
            return D3DERR_INVALIDCALL;
        *l = lights_[i].desc;
        return D3D_OK;
    }
    HRESULT LightEnable(DWORD i, BOOL enable) override
    {
        if (i >= lights_.size())
            lights_.resize(i + 1, Light{ default_light(), false });
        lights_[i].enabled = enable != 0;
        return D3D_OK;
    }
    HRESULT GetLightEnable(DWORD i, BOOL *enable) override
    {
        if (!enable || i >= lights_.size())
            return D3DERR_INVALIDCALL;
        *enable = lights_[i].enabled ? TRUE : FALSE;
        return D3D_OK;
    }

    HRESULT SetRenderState(D3DRENDERSTATETYPE s, DWORD v) override
    {
        if ((unsigned)s >= 256)
            return D3DERR_INVALIDCALL;
        rs_[s] = v;
        return D3D_OK;
    }
    HRESULT GetRenderState(D3DRENDERSTATETYPE s, DWORD *v) override
    {
        if ((unsigned)s >= 256 || !v)
            return D3DERR_INVALIDCALL;
        *v = rs_[s];
        return D3D_OK;
    }

    HRESULT GetTexture(DWORD stage, IDirect3DBaseTexture8 **out) override
    {
        if (stage >= (DWORD)kStages || !out)
            return D3DERR_INVALIDCALL;
        *out = textures_[stage];
        if (*out)
            (*out)->AddRef();
        return D3D_OK;
    }
    HRESULT SetTexture(DWORD stage, IDirect3DBaseTexture8 *t) override
    {
        if (stage >= (DWORD)kStages)
            return D3DERR_INVALIDCALL;
        if (t)
            t->AddRef();
        if (textures_[stage])
            textures_[stage]->Release();
        textures_[stage] = t;
        return D3D_OK;
    }
    HRESULT GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD *v) override
    {
        if (stage >= (DWORD)kStages || (unsigned)type >= 32 || !v)
            return D3DERR_INVALIDCALL;
        *v = tss_[stage][type];
        return D3D_OK;
    }
    HRESULT SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD v) override
    {
        if (stage >= (DWORD)kStages || (unsigned)type >= 32)
            return D3DERR_INVALIDCALL;
        tss_[stage][type] = v;
        return D3D_OK;
    }

    HRESULT DrawPrimitive(D3DPRIMITIVETYPE type, UINT start, UINT count) override
    {
        if (!stream_ || !stride_)
            return D3DERR_INVALIDCALL;
        return draw(type, count, stream_->data(), stream_->size(), stream_->handle(), stride_,
                    start, vertex_count(type, count), nullptr, nullptr, D3DFMT_INDEX16, 0, 0);
    }

    HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE type, UINT min_index, UINT num_vertices,
                                 UINT start_index, UINT count) override
    {
        if (!stream_ || !stride_ || !indices_)
            return D3DERR_INVALIDCALL;
        D3DFORMAT fmt = (D3DFORMAT)indices_->extra();
        size_t isz = fmt == D3DFMT_INDEX32 ? 4 : 2;
        size_t need = ((size_t)start_index + vertex_count(type, count)) * isz;
        if (need > indices_->size())
            return D3DERR_INVALIDCALL;
        return draw(type, count, stream_->data(), stream_->size(), stream_->handle(), stride_,
                    base_vertex_, min_index + num_vertices, indices_->data() + start_index * isz,
                    indices_->handle(), fmt, start_index * isz, min_index);
    }

    HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT count, const void *data, UINT stride) override
    {
        if (!data || !stride)
            return D3DERR_INVALIDCALL;
        UINT n = vertex_count(type, count);
        return draw(type, count, (const uint8_t *)data, (size_t)n * stride, nullptr, stride, 0, n,
                    nullptr, nullptr, D3DFMT_INDEX16, 0, 0);
    }

    HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type, UINT min_index, UINT num_vertices,
                                   UINT count, const void *index_data, D3DFORMAT index_fmt,
                                   const void *vertex_data, UINT stride) override
    {
        if (!index_data || !vertex_data || !stride ||
            (index_fmt != D3DFMT_INDEX16 && index_fmt != D3DFMT_INDEX32))
            return D3DERR_INVALIDCALL;
        UINT n = min_index + num_vertices;
        return draw(type, count, (const uint8_t *)vertex_data, (size_t)n * stride, nullptr, stride,
                    0, n, (const uint8_t *)index_data, nullptr, index_fmt, 0, min_index);
    }

    HRESULT SetVertexShader(DWORD handle) override
    {
        FvfInfo f;
        // Programmable shader handles have bit 0 set, which no FVF has.
        if ((handle & D3DFVF_RESERVED0) || !decode_fvf(handle, &f)) {
            warn_once(warned_shader_, "programmable vertex shaders");
            return D3DERR_INVALIDCALL;
        }
        fvf_ = handle;
        return D3D_OK;
    }
    HRESULT GetVertexShader(DWORD *h) override
    {
        if (!h)
            return D3DERR_INVALIDCALL;
        *h = fvf_;
        return D3D_OK;
    }
    HRESULT SetStreamSource(UINT n, IDirect3DVertexBuffer8 *vb, UINT stride) override
    {
        if (n != 0) {
            warn_once(warned_streams_, "vertex streams other than 0");
            return D3DERR_INVALIDCALL;
        }
        if (vb)
            vb->AddRef();
        if (stream_)
            stream_->Release();
        stream_ = static_cast<VertexBuffer *>(vb);
        stride_ = stride;
        return D3D_OK;
    }
    HRESULT GetStreamSource(UINT n, IDirect3DVertexBuffer8 **vb, UINT *stride) override
    {
        if (n != 0 || !vb || !stride)
            return D3DERR_INVALIDCALL;
        *vb = stream_;
        if (stream_)
            stream_->AddRef();
        *stride = stride_;
        return D3D_OK;
    }
    HRESULT SetIndices(IDirect3DIndexBuffer8 *ib, UINT base) override
    {
        if (ib)
            ib->AddRef();
        if (indices_)
            indices_->Release();
        indices_ = static_cast<IndexBuffer *>(ib);
        base_vertex_ = base;
        return D3D_OK;
    }
    HRESULT GetIndices(IDirect3DIndexBuffer8 **ib, UINT *base) override
    {
        if (!ib || !base)
            return D3DERR_INVALIDCALL;
        *ib = indices_;
        if (indices_)
            indices_->AddRef();
        *base = base_vertex_;
        return D3D_OK;
    }
    HRESULT SetPixelShader(DWORD handle) override
    {
        if (handle) {
            warn_once(warned_pshader_, "pixel shaders");
            return D3DERR_INVALIDCALL;
        }
        return D3D_OK;
    }
    HRESULT GetPixelShader(DWORD *h) override
    {
        if (!h)
            return D3DERR_INVALIDCALL;
        *h = 0;
        return D3D_OK;
    }

private:
    static D3DLIGHT8 default_light()
    {
        D3DLIGHT8 l;
        memset(&l, 0, sizeof(l));
        l.Type = D3DLIGHT_DIRECTIONAL;
        l.Diffuse.r = l.Diffuse.g = l.Diffuse.b = 1.0f;
        l.Direction.z = 1.0f;
        return l;
    }

    static UINT vertex_count(D3DPRIMITIVETYPE t, UINT prims)
    {
        switch (t) {
        case D3DPT_POINTLIST: return prims;
        case D3DPT_LINELIST: return prims * 2;
        case D3DPT_LINESTRIP: return prims ? prims + 1 : 0;
        case D3DPT_TRIANGLELIST: return prims * 3;
        case D3DPT_TRIANGLESTRIP:
        case D3DPT_TRIANGLEFAN: return prims ? prims + 2 : 0;
        default: return 0;
        }
    }

    D3DMATRIX *transform(D3DTRANSFORMSTATETYPE s)
    {
        if (s == D3DTS_VIEW)
            return &view_;
        if (s == D3DTS_PROJECTION)
            return &proj_;
        if (s == D3DTS_WORLD)
            return &world_;
        if (s >= D3DTS_TEXTURE0 && s <= D3DTS_TEXTURE7)
            return &tex_[s - D3DTS_TEXTURE0];
        return nullptr;
    }

    void set_virtual_size(UINT w, UINT h)
    {
        int rw = 0, rh = 0;
        vr_gfx_get_size(&rw, &rh);
        virt_w_ = w ? w : (UINT)rw;
        virt_h_ = h ? h : (UINT)rh;
        scale_x_ = (float)rw / (float)virt_w_;
        scale_y_ = (float)rh / (float)virt_h_;
    }

    // D3D coordinates are in the application's back-buffer size; scale them
    // to the real one.
    void apply_viewport(LONG x, LONG y, LONG w, LONG h, float min_z, float max_z)
    {
        int x0 = (int)floorf((float)x * scale_x_ + 0.5f), y0 = (int)floorf((float)y * scale_y_ + 0.5f);
        int x1 = (int)floorf((float)(x + w) * scale_x_ + 0.5f);
        int y1 = (int)floorf((float)(y + h) * scale_y_ + 0.5f);
        vr_gfx_set_viewport(x0, y0, x1 - x0, y1 - y0, min_z, max_z);
    }

    void reset_state(bool depth);
    void build_state(const FvfInfo &f, vr_render_state *s, int coord_set[2]);
    void light_vertices(const FvfInfo &f, const uint8_t *src, UINT stride, UINT first, UINT count,
                        const int coord_set[2], LitVertex *out);
    HRESULT draw(D3DPRIMITIVETYPE type, UINT prims, const uint8_t *vdata, size_t vsize, vr_buffer *vb,
                 UINT stride, UINT base_vertex, UINT vcount, const uint8_t *idata, vr_buffer *ib,
                 D3DFORMAT ifmt, size_t ib_offset, UINT min_index);

    unsigned long refs_ = 1;
    Direct3D *d3d_;
    DWORD rs_[256];
    DWORD tss_[kStages][32];
    IDirect3DBaseTexture8 *textures_[kStages] = {};
    D3DMATRIX world_, view_, proj_, tex_[8];
    D3DVIEWPORT8 vp_;
    D3DMATERIAL8 material_;
    std::vector<Light> lights_;
    DWORD fvf_ = 0;
    VertexBuffer *stream_ = nullptr;
    UINT stride_ = 0;
    IndexBuffer *indices_ = nullptr;
    UINT base_vertex_ = 0;
    UINT virt_w_ = 0, virt_h_ = 0;
    float scale_x_ = 1.0f, scale_y_ = 1.0f;
    std::vector<LitVertex> lit_;
    std::vector<uint32_t> remap_;
    bool warned_shader_ = false, warned_streams_ = false, warned_pshader_ = false;
    bool warned_stage_ = false, warned_op_ = false, warned_textrans_ = false;
    bool warned_fog_ = false, warned_stencil_ = false;
};

// ---- Direct3D object -------------------------------------------------------------------

class Direct3D final : public IDirect3D8 {
public:
    HRESULT QueryInterface(REFIID, void **ppv) override
    {
        if (ppv)
            *ppv = nullptr;
        return E_NOINTERFACE;
    }
    unsigned long AddRef() override { return ++refs_; }
    unsigned long Release() override
    {
        unsigned long r = --refs_;
        if (!r)
            delete this;
        return r;
    }
    UINT GetAdapterCount() override { return 1; }
    HRESULT GetAdapterDisplayMode(UINT adapter, D3DDISPLAYMODE *mode) override
    {
        if (adapter || !mode)
            return D3DERR_INVALIDCALL;
        mode->Width = 960;
        mode->Height = 544;
        mode->RefreshRate = 60;
        mode->Format = D3DFMT_X8R8G8B8;
        return D3D_OK;
    }
    HRESULT CheckDeviceFormat(UINT adapter, D3DDEVTYPE, D3DFORMAT, DWORD usage, D3DRESOURCETYPE rtype,
                              D3DFORMAT fmt) override
    {
        vr_format v;
        if (adapter)
            return D3DERR_INVALIDCALL;
        if (rtype == D3DRTYPE_TEXTURE && !(usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)))
            return map_format(fmt, &v) ? D3D_OK : D3DERR_NOTAVAILABLE;
        if ((usage & D3DUSAGE_DEPTHSTENCIL) && (fmt == D3DFMT_D16 || fmt == D3DFMT_D24X8 || fmt == D3DFMT_D24S8))
            return D3D_OK;
        return D3DERR_NOTAVAILABLE;
    }
    HRESULT CreateDevice(UINT adapter, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *pp,
                         IDirect3DDevice8 **out) override
    {
        vr_gfx_desc gd;
        int w = 0, h = 0;
        if (adapter || !pp || !out)
            return D3DERR_INVALIDCALL;
        vr_gfx_get_size(&w, &h);
        if (!w) {
            // The Vita display has a fixed size; the device scales the
            // application's back-buffer coordinates onto it.
            gd.width = 0;
            gd.height = 0;
            gd.vsync = pp->FullScreen_PresentationInterval != D3DPRESENT_INTERVAL_IMMEDIATE;
            if (vr_gfx_init(&gd) != VR_OK)
                return D3DERR_NOTAVAILABLE;
        }
        Device *dev = new (std::nothrow) Device(this, *pp);
        if (!dev)
            return E_OUTOFMEMORY;
        *out = dev;
        return D3D_OK;
    }

private:
    unsigned long refs_ = 1;
};

template <class Base>
HRESULT Resource<Base>::GetDevice(IDirect3DDevice8 **ppDevice)
{
    if (!ppDevice)
        return D3DERR_INVALIDCALL;
    *ppDevice = static_cast<IDirect3DDevice8 *>(device_);
    device_->AddRef();
    return D3D_OK;
}

Device::Device(Direct3D *d3d, const D3DPRESENT_PARAMETERS &pp) : d3d_(d3d)
{
    d3d_->AddRef();
    set_virtual_size(pp.BackBufferWidth, pp.BackBufferHeight);
    reset_state(pp.EnableAutoDepthStencil != 0);
}

Device::~Device()
{
    for (int i = 0; i < kStages; i++)
        if (textures_[i])
            textures_[i]->Release();
    if (stream_)
        stream_->Release();
    if (indices_)
        indices_->Release();
    d3d_->Release();
}

HRESULT Device::GetDirect3D(IDirect3D8 **out)
{
    if (!out)
        return D3DERR_INVALIDCALL;
    *out = d3d_;
    d3d_->AddRef();
    return D3D_OK;
}

// Default values from the Direct3D 8 documentation.
void Device::reset_state(bool depth)
{
    memset(rs_, 0, sizeof(rs_));
    rs_[D3DRS_ZENABLE] = depth ? D3DZB_TRUE : D3DZB_FALSE;
    rs_[D3DRS_FILLMODE] = 3; /* D3DFILL_SOLID */
    rs_[D3DRS_SHADEMODE] = 2; /* D3DSHADE_GOURAUD */
    rs_[D3DRS_ZWRITEENABLE] = TRUE;
    rs_[D3DRS_LASTPIXEL] = TRUE;
    rs_[D3DRS_SRCBLEND] = D3DBLEND_ONE;
    rs_[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
    rs_[D3DRS_CULLMODE] = D3DCULL_CCW;
    rs_[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
    rs_[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
    rs_[D3DRS_FOGSTART] = f2dw(0.0f);
    rs_[D3DRS_FOGEND] = f2dw(1.0f);
    rs_[D3DRS_FOGDENSITY] = f2dw(1.0f);
    rs_[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
    rs_[D3DRS_STENCILMASK] = 0xFFFFFFFF;
    rs_[D3DRS_STENCILWRITEMASK] = 0xFFFFFFFF;
    rs_[D3DRS_TEXTUREFACTOR] = 0xFFFFFFFF;
    rs_[D3DRS_CLIPPING] = TRUE;
    rs_[D3DRS_LIGHTING] = TRUE;
    rs_[D3DRS_COLORVERTEX] = TRUE;
    rs_[D3DRS_LOCALVIEWER] = TRUE;
    rs_[D3DRS_DIFFUSEMATERIALSOURCE] = D3DMCS_COLOR1;
    rs_[D3DRS_SPECULARMATERIALSOURCE] = D3DMCS_COLOR2;
    rs_[D3DRS_AMBIENTMATERIALSOURCE] = D3DMCS_MATERIAL;
    rs_[D3DRS_EMISSIVEMATERIALSOURCE] = D3DMCS_MATERIAL;
    rs_[D3DRS_POINTSIZE] = f2dw(1.0f);
    rs_[D3DRS_POINTSIZE_MAX] = f2dw(64.0f);
    rs_[D3DRS_POINTSCALE_A] = f2dw(1.0f);
    rs_[D3DRS_MULTISAMPLEANTIALIAS] = TRUE;
    rs_[D3DRS_MULTISAMPLEMASK] = 0xFFFFFFFF;
    rs_[D3DRS_COLORWRITEENABLE] = 0xF;
    rs_[D3DRS_BLENDOP] = D3DBLENDOP_ADD;

    memset(tss_, 0, sizeof(tss_));
    for (int s = 0; s < kStages; s++) {
        tss_[s][D3DTSS_COLOROP] = s == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE;
        tss_[s][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
        tss_[s][D3DTSS_COLORARG2] = D3DTA_CURRENT;
        tss_[s][D3DTSS_ALPHAOP] = s == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
        tss_[s][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
        tss_[s][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
        tss_[s][D3DTSS_TEXCOORDINDEX] = (DWORD)s;
        tss_[s][D3DTSS_ADDRESSU] = D3DTADDRESS_WRAP;
        tss_[s][D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
        tss_[s][D3DTSS_MAGFILTER] = D3DTEXF_POINT;
        tss_[s][D3DTSS_MINFILTER] = D3DTEXF_POINT;
        tss_[s][D3DTSS_MIPFILTER] = D3DTEXF_NONE;
    }
    identity(&world_);
    identity(&view_);
    identity(&proj_);
    for (int i = 0; i < 8; i++)
        identity(&tex_[i]);
    memset(&material_, 0, sizeof(material_));
    lights_.clear();
    vp_.X = 0;
    vp_.Y = 0;
    vp_.Width = virt_w_;
    vp_.Height = virt_h_;
    vp_.MinZ = 0.0f;
    vp_.MaxZ = 1.0f;
    apply_viewport(0, 0, (LONG)virt_w_, (LONG)virt_h_, 0.0f, 1.0f);
}

// ---- State translation ------------------------------------------------------------------

vr_compare map_cmp(DWORD c)
{
    switch (c) {
    case D3DCMP_NEVER: return VR_CMP_NEVER;
    case D3DCMP_LESS: return VR_CMP_LESS;
    case D3DCMP_EQUAL: return VR_CMP_EQUAL;
    case D3DCMP_LESSEQUAL: return VR_CMP_LEQUAL;
    case D3DCMP_GREATER: return VR_CMP_GREATER;
    case D3DCMP_NOTEQUAL: return VR_CMP_NOTEQUAL;
    case D3DCMP_GREATEREQUAL: return VR_CMP_GEQUAL;
    default: return VR_CMP_ALWAYS;
    }
}

vr_blend_factor map_blend(DWORD b)
{
    switch (b) {
    case D3DBLEND_ZERO: return VR_BLEND_ZERO;
    case D3DBLEND_SRCCOLOR: return VR_BLEND_SRC_COLOR;
    case D3DBLEND_INVSRCCOLOR: return VR_BLEND_INV_SRC_COLOR;
    case D3DBLEND_SRCALPHA: return VR_BLEND_SRC_ALPHA;
    case D3DBLEND_INVSRCALPHA: return VR_BLEND_INV_SRC_ALPHA;
    case D3DBLEND_DESTALPHA: return VR_BLEND_DST_ALPHA;
    case D3DBLEND_INVDESTALPHA: return VR_BLEND_INV_DST_ALPHA;
    case D3DBLEND_DESTCOLOR: return VR_BLEND_DST_COLOR;
    case D3DBLEND_INVDESTCOLOR: return VR_BLEND_INV_DST_COLOR;
    case D3DBLEND_SRCALPHASAT: return VR_BLEND_SRC_ALPHA_SAT;
    default: return VR_BLEND_ONE;
    }
}

int map_arg(DWORD a)
{
    int mods = (int)(a & (D3DTA_COMPLEMENT | D3DTA_ALPHAREPLICATE)); /* same bits in vr */
    switch (a & D3DTA_SELECTMASK) {
    case D3DTA_DIFFUSE: return VR_TARG_DIFFUSE | mods;
    case D3DTA_TEXTURE: return VR_TARG_TEXTURE | mods;
    case D3DTA_TFACTOR: return VR_TARG_TFACTOR | mods;
    case D3DTA_SPECULAR: return VR_TARG_SPECULAR | mods;
    default: return VR_TARG_CURRENT | mods; /* CURRENT, TEMP */
    }
}

bool map_op(DWORD op, vr_texture_op *out)
{
    switch (op) {
    case D3DTOP_DISABLE: *out = VR_TOP_DISABLE; return true;
    case D3DTOP_SELECTARG1: *out = VR_TOP_SELECT_ARG1; return true;
    case D3DTOP_SELECTARG2: *out = VR_TOP_SELECT_ARG2; return true;
    case D3DTOP_MODULATE: *out = VR_TOP_MODULATE; return true;
    case D3DTOP_MODULATE2X: *out = VR_TOP_MODULATE2X; return true;
    case D3DTOP_MODULATE4X: *out = VR_TOP_MODULATE4X; return true;
    case D3DTOP_ADD: *out = VR_TOP_ADD; return true;
    case D3DTOP_ADDSIGNED: *out = VR_TOP_ADD_SIGNED; return true;
    case D3DTOP_ADDSIGNED2X: *out = VR_TOP_ADD_SIGNED2X; return true;
    case D3DTOP_SUBTRACT: *out = VR_TOP_SUBTRACT; return true;
    case D3DTOP_ADDSMOOTH: *out = VR_TOP_ADD_SMOOTH; return true;
    case D3DTOP_BLENDDIFFUSEALPHA: *out = VR_TOP_BLEND_DIFFUSE_ALPHA; return true;
    case D3DTOP_BLENDTEXTUREALPHA:
    case D3DTOP_BLENDTEXTUREALPHAPM: *out = VR_TOP_BLEND_TEXTURE_ALPHA; return op == D3DTOP_BLENDTEXTUREALPHA;
    case D3DTOP_BLENDFACTORALPHA: *out = VR_TOP_BLEND_FACTOR_ALPHA; return true;
    case D3DTOP_BLENDCURRENTALPHA: *out = VR_TOP_BLEND_CURRENT_ALPHA; return true;
    case D3DTOP_DOTPRODUCT3: *out = VR_TOP_DOTPRODUCT3; return true;
    default: *out = VR_TOP_MODULATE; return false;
    }
}

vr_address map_address(DWORD a)
{
    return a == D3DTADDRESS_MIRROR ? VR_ADDRESS_MIRROR :
           (a == D3DTADDRESS_WRAP ? VR_ADDRESS_WRAP : VR_ADDRESS_CLAMP);
}

void Device::build_state(const FvfInfo &f, vr_render_state *s, int coord_set[2])
{
    vr_render_state_default(s);

    s->depth_test_enable = rs_[D3DRS_ZENABLE] != D3DZB_FALSE;
    s->depth_write_enable = rs_[D3DRS_ZWRITEENABLE] != 0;
    s->depth_func = map_cmp(rs_[D3DRS_ZFUNC]);
    s->depth_bias = -(float)rs_[D3DRS_ZBIAS] / 65536.0f; /* higher ZBIAS draws nearer */
    s->alpha_test_enable = rs_[D3DRS_ALPHATESTENABLE] != 0;
    s->alpha_func = map_cmp(rs_[D3DRS_ALPHAFUNC]);
    s->alpha_ref = (float)(rs_[D3DRS_ALPHAREF] & 0xFF) / 255.0f;
    s->blend_enable = rs_[D3DRS_ALPHABLENDENABLE] != 0;
    if (rs_[D3DRS_SRCBLEND] == D3DBLEND_BOTHSRCALPHA) {
        s->blend_src = VR_BLEND_SRC_ALPHA;
        s->blend_dst = VR_BLEND_INV_SRC_ALPHA;
    } else if (rs_[D3DRS_SRCBLEND] == D3DBLEND_BOTHINVSRCALPHA) {
        s->blend_src = VR_BLEND_INV_SRC_ALPHA;
        s->blend_dst = VR_BLEND_SRC_ALPHA;
    } else {
        s->blend_src = map_blend(rs_[D3DRS_SRCBLEND]);
        s->blend_dst = map_blend(rs_[D3DRS_DESTBLEND]);
    }
    switch (rs_[D3DRS_BLENDOP]) {
    case D3DBLENDOP_SUBTRACT: s->blend_op = VR_BLEND_OP_SUBTRACT; break;
    case D3DBLENDOP_REVSUBTRACT: s->blend_op = VR_BLEND_OP_REV_SUBTRACT; break;
    case D3DBLENDOP_MIN: s->blend_op = VR_BLEND_OP_MIN; break;
    case D3DBLENDOP_MAX: s->blend_op = VR_BLEND_OP_MAX; break;
    default: s->blend_op = VR_BLEND_OP_ADD; break;
    }
    s->cull = rs_[D3DRS_CULLMODE] == D3DCULL_NONE ? VR_CULL_NONE :
              (rs_[D3DRS_CULLMODE] == D3DCULL_CW ? VR_CULL_CW : VR_CULL_CCW);
    s->color_write_mask = rs_[D3DRS_COLORWRITEENABLE] & 0xF;
    s->specular_enable = rs_[D3DRS_SPECULARENABLE] != 0;
    argb_to_rgba(rs_[D3DRS_TEXTUREFACTOR], s->texture_factor);

    if (rs_[D3DRS_FOGENABLE]) {
        DWORD mode = rs_[D3DRS_FOGTABLEMODE] != D3DFOG_NONE ? rs_[D3DRS_FOGTABLEMODE]
                                                             : rs_[D3DRS_FOGVERTEXMODE];
        if (mode == D3DFOG_LINEAR) {
            s->fog_enable = 1;
            s->fog_start = dw2f(rs_[D3DRS_FOGSTART]);
            s->fog_end = dw2f(rs_[D3DRS_FOGEND]);
            argb_to_rgba(rs_[D3DRS_FOGCOLOR], s->fog_color);
        } else if (mode != D3DFOG_NONE) {
            warn_once(warned_fog_, "exponential fog");
        }
    }
    if (rs_[D3DRS_STENCILENABLE])
        warn_once(warned_stencil_, "stencil");

    coord_set[0] = coord_set[1] = 0;
    for (int st = 0; st < kStages; st++) {
        if (tss_[st][D3DTSS_COLOROP] == D3DTOP_DISABLE)
            break;
        if (st >= VR_MAX_TEXTURE_STAGES) {
            warn_once(warned_stage_, "more than two texture stages");
            break;
        }
        vr_texture_stage *vs = &s->stages[st];
        vr_texture_op op;
        if (!map_op(tss_[st][D3DTSS_COLOROP], &op))
            warn_once(warned_op_, "an exact match for this texture operation (approximating)");
        vs->color_op = op;
        if (!map_op(tss_[st][D3DTSS_ALPHAOP], &op))
            warn_once(warned_op_, "an exact match for this texture operation (approximating)");
        vs->alpha_op = op;
        vs->color_arg1 = map_arg(tss_[st][D3DTSS_COLORARG1]);
        vs->color_arg2 = map_arg(tss_[st][D3DTSS_COLORARG2]);
        vs->alpha_arg1 = map_arg(tss_[st][D3DTSS_ALPHAARG1]);
        vs->alpha_arg2 = map_arg(tss_[st][D3DTSS_ALPHAARG2]);
        if (tss_[st][D3DTSS_TEXTURETRANSFORMFLAGS] ||
            (tss_[st][D3DTSS_TEXCOORDINDEX] & 0xFFFF0000u))
            warn_once(warned_textrans_, "texture transforms and generated coordinates");
        coord_set[st] = (int)(tss_[st][D3DTSS_TEXCOORDINDEX] & 0xFFFF);
        vs->texcoord = st;
        IDirect3DBaseTexture8 *t = textures_[st];
        vs->texture = t && t->GetType() == D3DRTYPE_TEXTURE ? static_cast<Texture *>(t)->handle()
                                                           : nullptr;
        vs->mag_filter = tss_[st][D3DTSS_MAGFILTER] == D3DTEXF_POINT ? VR_FILTER_NEAREST
                                                                      : VR_FILTER_LINEAR;
        vs->min_filter = tss_[st][D3DTSS_MINFILTER] == D3DTEXF_POINT ? VR_FILTER_NEAREST
                                                                      : VR_FILTER_LINEAR;
        vs->mip_filter = tss_[st][D3DTSS_MIPFILTER] == D3DTEXF_NONE ? VR_MIP_NONE :
                         (tss_[st][D3DTSS_MIPFILTER] == D3DTEXF_POINT ? VR_MIP_NEAREST : VR_MIP_LINEAR);
        vs->address_u = map_address(tss_[st][D3DTSS_ADDRESSU]);
        vs->address_v = map_address(tss_[st][D3DTSS_ADDRESSV]);
    }
    if (s->stages[0].color_op != VR_TOP_DISABLE && s->stages[1].color_op == VR_TOP_DISABLE)
        s->stages[1].alpha_op = VR_TOP_DISABLE;
    (void)f;
}

// ---- CPU lighting -------------------------------------------------------------------------

struct V3 {
    float x, y, z;
};

V3 v3(float x, float y, float z)
{
    V3 r = { x, y, z };
    return r;
}
V3 sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
V3 add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 norm(V3 a)
{
    float l = sqrtf(dot(a, a));
    return l > 0.0f ? v3(a.x / l, a.y / l, a.z / l) : a;
}

V3 xform_point(const D3DMATRIX &m, V3 p)
{
    return v3(p.x * m._11 + p.y * m._21 + p.z * m._31 + m._41,
              p.x * m._12 + p.y * m._22 + p.z * m._32 + m._42,
              p.x * m._13 + p.y * m._23 + p.z * m._33 + m._43);
}

V3 xform_dir(const D3DMATRIX &m, V3 d)
{
    return v3(d.x * m._11 + d.y * m._21 + d.z * m._31,
              d.x * m._12 + d.y * m._22 + d.z * m._32,
              d.x * m._13 + d.y * m._23 + d.z * m._33);
}

// World-space camera position from a row-vector view matrix.
V3 camera_position(const D3DMATRIX &v)
{
    float a = v._11, b = v._12, c = v._13, d = v._21, e = v._22, f = v._23, g = v._31, h = v._32,
          i = v._33;
    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (fabsf(det) < 1e-12f)
        return v3(0, 0, 0);
    // p = -T * R^-1
    float inv[9] = { (e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det,
                     (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det,
                     (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det };
    V3 t = v3(-v._41, -v._42, -v._43);
    return v3(t.x * inv[0] + t.y * inv[3] + t.z * inv[6], t.x * inv[1] + t.y * inv[4] + t.z * inv[7],
              t.x * inv[2] + t.y * inv[5] + t.z * inv[8]);
}

uint8_t to_byte(float v)
{
    return (uint8_t)(v <= 0.0f ? 0 : (v >= 1.0f ? 255 : (int)(v * 255.0f + 0.5f)));
}

void bgra_to_value(const uint8_t *p, D3DCOLORVALUE *c)
{
    c->b = p[0] / 255.0f;
    c->g = p[1] / 255.0f;
    c->r = p[2] / 255.0f;
    c->a = p[3] / 255.0f;
}

void Device::light_vertices(const FvfInfo &f, const uint8_t *src, UINT stride, UINT first, UINT count,
                            const int coord_set[2], LitVertex *out)
{
    D3DCOLORVALUE global;
    V3 cam = camera_position(view_);
    bool specular = rs_[D3DRS_SPECULARENABLE] != 0;
    bool normalize = rs_[D3DRS_NORMALIZENORMALS] != 0;
    bool color_vertex = rs_[D3DRS_COLORVERTEX] != 0;
    {
        float g[4];
        argb_to_rgba(rs_[D3DRS_AMBIENT], g);
        global.r = g[0];
        global.g = g[1];
        global.b = g[2];
        global.a = g[3];
    }

    for (UINT n = 0; n < count; n++) {
        const uint8_t *vtx = src + (size_t)(first + n) * stride;
        LitVertex *o = &out[n];
        V3 p, nrm = v3(0, 0, 0);
        D3DCOLORVALUE vd = { 1, 1, 1, 1 }, vs = { 0, 0, 0, 0 };
        float amb[3] = { 0, 0, 0 }, dif[3] = { 0, 0, 0 }, spc[3] = { 0, 0, 0 };

        memcpy(&p, vtx, 12);
        o->x = p.x;
        o->y = p.y;
        o->z = p.z;
        p = xform_point(world_, p);
        if (f.has_normal) {
            memcpy(&nrm, vtx + f.normal, 12);
            nrm = xform_dir(world_, nrm);
            if (normalize)
                nrm = norm(nrm);
        }
        if (f.has_diffuse)
            bgra_to_value(vtx + f.diffuse, &vd);
        if (f.has_specular)
            bgra_to_value(vtx + f.specular, &vs);

        auto source = [&](DWORD rs, const D3DCOLORVALUE &mat) -> D3DCOLORVALUE {
            if (color_vertex && rs_[rs] == D3DMCS_COLOR1 && f.has_diffuse)
                return vd;
            if (color_vertex && rs_[rs] == D3DMCS_COLOR2 && f.has_specular)
                return vs;
            return mat;
        };
        D3DCOLORVALUE md = source(D3DRS_DIFFUSEMATERIALSOURCE, material_.Diffuse);
        D3DCOLORVALUE ma = source(D3DRS_AMBIENTMATERIALSOURCE, material_.Ambient);
        D3DCOLORVALUE ms = source(D3DRS_SPECULARMATERIALSOURCE, material_.Specular);
        D3DCOLORVALUE me = source(D3DRS_EMISSIVEMATERIALSOURCE, material_.Emissive);

        for (size_t li = 0; li < lights_.size(); li++) {
            const D3DLIGHT8 &l = lights_[li].desc;
            V3 L;
            float atten = 1.0f, spot = 1.0f;

            if (!lights_[li].enabled)
                continue;
            if (l.Type == D3DLIGHT_DIRECTIONAL) {
                L = norm(v3(-l.Direction.x, -l.Direction.y, -l.Direction.z));
            } else {
                V3 d = sub(v3(l.Position.x, l.Position.y, l.Position.z), p);
                float dist = sqrtf(dot(d, d));
                if (dist > l.Range)
                    continue;
                L = dist > 0.0f ? v3(d.x / dist, d.y / dist, d.z / dist) : v3(0, 0, 1);
                float den = l.Attenuation0 + l.Attenuation1 * dist + l.Attenuation2 * dist * dist;
                atten = den > 0.0f ? 1.0f / den : 1.0f;
                if (l.Type == D3DLIGHT_SPOT) {
                    V3 dir = norm(v3(l.Direction.x, l.Direction.y, l.Direction.z));
                    float rho = -dot(L, dir), ct = cosf(l.Theta * 0.5f), cp = cosf(l.Phi * 0.5f);
                    if (rho <= cp)
                        spot = 0.0f;
                    else if (rho < ct)
                        spot = powf((rho - cp) / (ct - cp), l.Falloff);
                }
            }
            float k = atten * spot, ndl = dot(nrm, L);
            amb[0] += k * l.Ambient.r;
            amb[1] += k * l.Ambient.g;
            amb[2] += k * l.Ambient.b;
            if (ndl > 0.0f) {
                dif[0] += k * ndl * l.Diffuse.r;
                dif[1] += k * ndl * l.Diffuse.g;
                dif[2] += k * ndl * l.Diffuse.b;
                if (specular) {
                    V3 h = norm(add(norm(sub(cam, p)), L));
                    float ndh = dot(nrm, h);
                    if (ndh > 0.0f) {
                        float sp = k * powf(ndh, material_.Power);
                        spc[0] += sp * l.Specular.r;
                        spc[1] += sp * l.Specular.g;
                        spc[2] += sp * l.Specular.b;
                    }
                }
            }
        }
        o->c0[0] = to_byte(me.r + ma.r * (global.r + amb[0]) + md.r * dif[0]);
        o->c0[1] = to_byte(me.g + ma.g * (global.g + amb[1]) + md.g * dif[1]);
        o->c0[2] = to_byte(me.b + ma.b * (global.b + amb[2]) + md.b * dif[2]);
        o->c0[3] = to_byte(md.a);
        o->c1[0] = to_byte(ms.r * spc[0]);
        o->c1[1] = to_byte(ms.g * spc[1]);
        o->c1[2] = to_byte(ms.b * spc[2]);
        o->c1[3] = to_byte(ms.a);
        for (int t = 0; t < 2; t++) {
            float *dst = t ? o->t1 : o->t0;
            int set = coord_set[t];
            dst[0] = dst[1] = 0.0f;
            if (set < f.tex_count)
                memcpy(dst, vtx + f.tex[set], f.tex_size[set] >= 2 ? 8 : 4);
        }
    }
}

// ---- Drawing --------------------------------------------------------------------------------

HRESULT Device::draw(D3DPRIMITIVETYPE type, UINT prims, const uint8_t *vdata, size_t vsize, vr_buffer *vb,
                     UINT stride, UINT base_vertex, UINT vcount, const uint8_t *idata, vr_buffer *ib,
                     D3DFORMAT ifmt, size_t ib_offset, UINT min_index)
{
    static const vr_primitive prim_map[] = { VR_PRIM_TRIANGLE_LIST, VR_PRIM_POINT_LIST,
                                             VR_PRIM_LINE_LIST, VR_PRIM_LINE_STRIP,
                                             VR_PRIM_TRIANGLE_LIST, VR_PRIM_TRIANGLE_STRIP,
                                             VR_PRIM_TRIANGLE_FAN };
    FvfInfo f;
    vr_render_state rs;
    vr_vertex_layout layout;
    vr_draw_desc d;
    D3DMATRIX wv, wvp;
    int coord_set[2];
    UINT count = vertex_count(type, prims);

    if (type < D3DPT_POINTLIST || type > D3DPT_TRIANGLEFAN || !decode_fvf(fvf_, &f) || stride < f.stride)
        return D3DERR_INVALIDCALL;
    if (!count)
        return D3D_OK;
    if ((size_t)(base_vertex + vcount) * stride > vsize)
        return D3DERR_INVALIDCALL;
    build_state(f, &rs, coord_set);

    memset(&d, 0, sizeof(d));
    d.primitive = prim_map[type];
    d.state = &rs;
    d.screen_scale[0] = scale_x_;
    d.screen_scale[1] = scale_y_;
    /* D3D pixel centres are at integer coordinates; vr_gfx's are at +0.5.
     * Pre-transformed vertices get the exact mapping (half a virtual pixel)
     * so 2D quads cover whole scaled pixels. Transformed geometry gets half
     * a real pixel, which keeps viewport edges fully covered when scaling;
     * both are the same at 1:1. */
    {
        bool rhw = (fvf_ & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
        d.screen_offset[0] = rhw ? 0.5f * scale_x_ : 0.5f;
        d.screen_offset[1] = rhw ? 0.5f * scale_y_ : 0.5f;
    }
    multiply(world_, view_, &wv);
    multiply(wv, proj_, &wvp);
    d.transform = &wvp.m[0][0];

    bool lit = rs_[D3DRS_LIGHTING] && !f.rhw;
    if (lit) {
        // Light the referenced range into a temporary array and draw that.
        UINT first = idata ? min_index : 0;
        UINT n = idata ? vcount - min_index : count;
        lit_.resize(n);
        light_vertices(f, vdata + (size_t)base_vertex * stride, stride, first, n, coord_set, lit_.data());
        layout.count = 4;
        layout.stride = sizeof(LitVertex);
        layout.attrs[0] = { VR_ATTR_POSITION, VR_ATTR_FLOAT3, offsetof(LitVertex, x) };
        layout.attrs[1] = { VR_ATTR_COLOR0, VR_ATTR_UBYTE4_RGBA, offsetof(LitVertex, c0) };
        layout.attrs[2] = { VR_ATTR_COLOR1, VR_ATTR_UBYTE4_RGBA, offsetof(LitVertex, c1) };
        layout.attrs[3] = { VR_ATTR_TEXCOORD0, VR_ATTR_FLOAT2, offsetof(LitVertex, t0) };
        layout.attrs[4] = { VR_ATTR_TEXCOORD1, VR_ATTR_FLOAT2, offsetof(LitVertex, t1) };
        layout.count = 5;
        d.layout = &layout;
        d.vertex_data = lit_.data();
        d.vertex_count = n;
        if (idata) {
            remap_.resize(count);
            for (UINT i = 0; i < count; i++) {
                uint32_t v;
                if (ifmt == D3DFMT_INDEX32) {
                    memcpy(&v, idata + 4 * (size_t)i, 4);
                } else {
                    uint16_t s;
                    memcpy(&s, idata + 2 * (size_t)i, 2);
                    v = s;
                }
                if (v < min_index || v - min_index >= n)
                    return D3DERR_INVALIDCALL;
                remap_[i] = v - min_index;
            }
            d.index_data = remap_.data();
            d.index_type = VR_INDEX_32;
            d.index_count = count;
        }
        return vr_gfx_draw(&d) == VR_OK ? D3D_OK : D3DERR_INVALIDCALL;
    }

    // Unlit: describe the application's vertices directly.
    layout.count = 0;
    layout.stride = stride;
    layout.attrs[layout.count++] = { f.rhw ? VR_ATTR_POSITION_T : VR_ATTR_POSITION,
                                     f.rhw ? VR_ATTR_FLOAT4 : VR_ATTR_FLOAT3, 0 };
    if (f.has_diffuse)
        layout.attrs[layout.count++] = { VR_ATTR_COLOR0, VR_ATTR_UBYTE4_BGRA, f.diffuse };
    if (f.has_specular)
        layout.attrs[layout.count++] = { VR_ATTR_COLOR1, VR_ATTR_UBYTE4_BGRA, f.specular };
    for (int t = 0; t < 2; t++) {
        int set = coord_set[t];
        if (set < f.tex_count && (t == 0 || rs.stages[1].color_op != VR_TOP_DISABLE))
            layout.attrs[layout.count++] = { t ? VR_ATTR_TEXCOORD1 : VR_ATTR_TEXCOORD0,
                                             float_type(f.tex_size[set] < 2 ? 1 : 2), f.tex[set] };
    }
    d.layout = &layout;
    if (vb)
        d.vertex_buffer = vb;
    else
        d.vertex_data = vdata;
    d.base_vertex = (int32_t)base_vertex;
    d.vertex_count = vcount;
    if (idata) {
        d.index_type = ifmt == D3DFMT_INDEX32 ? VR_INDEX_32 : VR_INDEX_16;
        d.index_count = count;
        if (ib) {
            d.index_buffer = ib;
            d.index_offset = ib_offset;
        } else {
            d.index_data = idata;
        }
    }
    return vr_gfx_draw(&d) == VR_OK ? D3D_OK : D3DERR_INVALIDCALL;
}

} // namespace

IDirect3D8 *Direct3DCreate8(UINT SDKVersion)
{
    (void)SDKVersion;
    return new (std::nothrow) Direct3D();
}
