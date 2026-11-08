/* Direct3D 8 fixed-function API subset implemented on vr_gfx.
 *
 * Source compatible with code written for d3d8.h: names, numeric values and
 * method signatures follow the documented Direct3D 8 API. Interfaces are
 * plain C++ abstract classes (not a binary COM ABI). Programmable vertex
 * and pixel shaders, stencil, render targets and fixed-function features
 * listed in d3dshim/README.md are not implemented yet; such calls return
 * D3DERR_INVALIDCALL or are accepted and ignored as documented there. */

#ifndef VR_D3D8_H
#define VR_D3D8_H

#include <stddef.h>
#include <stdint.h>

#ifndef __cplusplus
#error "vr_d3d8.h requires C++"
#endif

/* ---- Basic Windows types -------------------------------------------------------- */

#ifndef VR_D3D8_NO_WIN_TYPES
typedef int32_t  HRESULT;
typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef uint8_t  BYTE;
typedef int32_t  LONG;
typedef uint32_t UINT;
typedef int32_t  INT;
typedef int      BOOL;
typedef float    FLOAT;
typedef void    *HWND;
typedef void    *HMONITOR;
typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { void *unused; } RGNDATA;
typedef struct { DWORD Data1; WORD Data2, Data3; BYTE Data4[8]; } GUID;
typedef GUID IID;
#define REFIID const IID &
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr)    (((HRESULT)(hr)) < 0)
#define S_OK          ((HRESULT)0)
#define E_FAIL        ((HRESULT)0x80004005)
#define E_NOINTERFACE ((HRESULT)0x80004002)
#define E_OUTOFMEMORY ((HRESULT)0x8007000E)
#define MAKEFOURCC(a, b, c, d) \
    ((DWORD)(BYTE)(a) | ((DWORD)(BYTE)(b) << 8) | ((DWORD)(BYTE)(c) << 16) | ((DWORD)(BYTE)(d) << 24))
#endif

#define D3D_SDK_VERSION 220

#define MAKE_D3DHRESULT(code)   ((HRESULT)(0x88760000u | (code)))
#define D3D_OK                  S_OK
#define D3DERR_NOTAVAILABLE     MAKE_D3DHRESULT(2154)
#define D3DERR_INVALIDCALL      MAKE_D3DHRESULT(2156)
#define D3DERR_OUTOFVIDEOMEMORY MAKE_D3DHRESULT(380)
#define D3DERR_DEVICELOST       MAKE_D3DHRESULT(2152)

typedef DWORD D3DCOLOR;
#define D3DCOLOR_ARGB(a, r, g, b) \
    ((D3DCOLOR)((((a) & 0xff) << 24) | (((r) & 0xff) << 16) | (((g) & 0xff) << 8) | ((b) & 0xff)))
#define D3DCOLOR_RGBA(r, g, b, a) D3DCOLOR_ARGB(a, r, g, b)
#define D3DCOLOR_XRGB(r, g, b)    D3DCOLOR_ARGB(0xff, r, g, b)

/* ---- Structures ------------------------------------------------------------------ */

typedef struct _D3DVECTOR { float x, y, z; } D3DVECTOR;
typedef struct _D3DCOLORVALUE { float r, g, b, a; } D3DCOLORVALUE;
typedef struct _D3DRECT { LONG x1, y1, x2, y2; } D3DRECT;

typedef struct _D3DMATRIX {
    union {
        struct {
            float _11, _12, _13, _14;
            float _21, _22, _23, _24;
            float _31, _32, _33, _34;
            float _41, _42, _43, _44;
        };
        float m[4][4];
    };
} D3DMATRIX;

typedef struct _D3DVIEWPORT8 {
    DWORD X, Y, Width, Height;
    float MinZ, MaxZ;
} D3DVIEWPORT8;

typedef struct _D3DMATERIAL8 {
    D3DCOLORVALUE Diffuse, Ambient, Specular, Emissive;
    float         Power;
} D3DMATERIAL8;

typedef enum _D3DLIGHTTYPE {
    D3DLIGHT_POINT = 1,
    D3DLIGHT_SPOT = 2,
    D3DLIGHT_DIRECTIONAL = 3,
    D3DLIGHT_FORCE_DWORD = 0x7fffffff
} D3DLIGHTTYPE;

typedef struct _D3DLIGHT8 {
    D3DLIGHTTYPE  Type;
    D3DCOLORVALUE Diffuse, Specular, Ambient;
    D3DVECTOR     Position, Direction;
    float         Range, Falloff;
    float         Attenuation0, Attenuation1, Attenuation2;
    float         Theta, Phi;
} D3DLIGHT8;

typedef struct _D3DLOCKED_RECT {
    INT   Pitch;
    void *pBits;
} D3DLOCKED_RECT;

/* ---- Enumerations (values match d3d8types.h) --------------------------------------- */

typedef enum _D3DFORMAT {
    D3DFMT_UNKNOWN = 0,
    D3DFMT_R8G8B8 = 20,
    D3DFMT_A8R8G8B8 = 21,
    D3DFMT_X8R8G8B8 = 22,
    D3DFMT_R5G6B5 = 23,
    D3DFMT_X1R5G5B5 = 24,
    D3DFMT_A1R5G5B5 = 25,
    D3DFMT_A4R4G4B4 = 26,
    D3DFMT_R3G3B2 = 27,
    D3DFMT_A8 = 28,
    D3DFMT_A8R3G3B2 = 29,
    D3DFMT_X4R4G4B4 = 30,
    D3DFMT_A8P8 = 40,
    D3DFMT_P8 = 41,
    D3DFMT_L8 = 50,
    D3DFMT_A8L8 = 51,
    D3DFMT_A4L4 = 52,
    D3DFMT_V8U8 = 60,
    D3DFMT_L6V5U5 = 61,
    D3DFMT_X8L8V8U8 = 62,
    D3DFMT_Q8W8V8U8 = 63,
    D3DFMT_V16U16 = 64,
    D3DFMT_W11V11U10 = 65,
    D3DFMT_UYVY = 0x59565955, /* MAKEFOURCC('U','Y','V','Y') */
    D3DFMT_YUY2 = 0x32595559, /* MAKEFOURCC('Y','U','Y','2') */
    D3DFMT_DXT1 = 0x31545844, /* MAKEFOURCC('D','X','T','1') */
    D3DFMT_DXT2 = 0x32545844,
    D3DFMT_DXT3 = 0x33545844,
    D3DFMT_DXT4 = 0x34545844,
    D3DFMT_DXT5 = 0x35545844,
    D3DFMT_D16_LOCKABLE = 70,
    D3DFMT_D32 = 71,
    D3DFMT_D15S1 = 73,
    D3DFMT_D24S8 = 75,
    D3DFMT_D16 = 80,
    D3DFMT_D24X8 = 77,
    D3DFMT_D24X4S4 = 79,
    D3DFMT_VERTEXDATA = 100,
    D3DFMT_INDEX16 = 101,
    D3DFMT_INDEX32 = 102,
    D3DFMT_FORCE_DWORD = 0xffffffff
} D3DFORMAT;

typedef enum _D3DPRIMITIVETYPE {
    D3DPT_POINTLIST = 1,
    D3DPT_LINELIST = 2,
    D3DPT_LINESTRIP = 3,
    D3DPT_TRIANGLELIST = 4,
    D3DPT_TRIANGLESTRIP = 5,
    D3DPT_TRIANGLEFAN = 6,
    D3DPT_FORCE_DWORD = 0x7fffffff
} D3DPRIMITIVETYPE;

typedef enum _D3DTRANSFORMSTATETYPE {
    D3DTS_VIEW = 2,
    D3DTS_PROJECTION = 3,
    D3DTS_TEXTURE0 = 16,
    D3DTS_TEXTURE1 = 17,
    D3DTS_TEXTURE2 = 18,
    D3DTS_TEXTURE3 = 19,
    D3DTS_TEXTURE4 = 20,
    D3DTS_TEXTURE5 = 21,
    D3DTS_TEXTURE6 = 22,
    D3DTS_TEXTURE7 = 23,
    D3DTS_FORCE_DWORD = 0x7fffffff
} D3DTRANSFORMSTATETYPE;
#define D3DTS_WORLDMATRIX(index) ((D3DTRANSFORMSTATETYPE)((index) + 256))
#define D3DTS_WORLD D3DTS_WORLDMATRIX(0)

typedef enum _D3DRENDERSTATETYPE {
    D3DRS_ZENABLE = 7,
    D3DRS_FILLMODE = 8,
    D3DRS_SHADEMODE = 9,
    D3DRS_LINEPATTERN = 10,
    D3DRS_ZWRITEENABLE = 14,
    D3DRS_ALPHATESTENABLE = 15,
    D3DRS_LASTPIXEL = 16,
    D3DRS_SRCBLEND = 19,
    D3DRS_DESTBLEND = 20,
    D3DRS_CULLMODE = 22,
    D3DRS_ZFUNC = 23,
    D3DRS_ALPHAREF = 24,
    D3DRS_ALPHAFUNC = 25,
    D3DRS_DITHERENABLE = 26,
    D3DRS_ALPHABLENDENABLE = 27,
    D3DRS_FOGENABLE = 28,
    D3DRS_SPECULARENABLE = 29,
    D3DRS_ZVISIBLE = 30,
    D3DRS_FOGCOLOR = 34,
    D3DRS_FOGTABLEMODE = 35,
    D3DRS_FOGSTART = 36,
    D3DRS_FOGEND = 37,
    D3DRS_FOGDENSITY = 38,
    D3DRS_EDGEANTIALIAS = 40,
    D3DRS_ZBIAS = 47,
    D3DRS_RANGEFOGENABLE = 48,
    D3DRS_STENCILENABLE = 52,
    D3DRS_STENCILFAIL = 53,
    D3DRS_STENCILZFAIL = 54,
    D3DRS_STENCILPASS = 55,
    D3DRS_STENCILFUNC = 56,
    D3DRS_STENCILREF = 57,
    D3DRS_STENCILMASK = 58,
    D3DRS_STENCILWRITEMASK = 59,
    D3DRS_TEXTUREFACTOR = 60,
    D3DRS_WRAP0 = 128,
    D3DRS_WRAP1 = 129,
    D3DRS_WRAP2 = 130,
    D3DRS_WRAP3 = 131,
    D3DRS_WRAP4 = 132,
    D3DRS_WRAP5 = 133,
    D3DRS_WRAP6 = 134,
    D3DRS_WRAP7 = 135,
    D3DRS_CLIPPING = 136,
    D3DRS_LIGHTING = 137,
    D3DRS_AMBIENT = 139,
    D3DRS_FOGVERTEXMODE = 140,
    D3DRS_COLORVERTEX = 141,
    D3DRS_LOCALVIEWER = 142,
    D3DRS_NORMALIZENORMALS = 143,
    D3DRS_DIFFUSEMATERIALSOURCE = 145,
    D3DRS_SPECULARMATERIALSOURCE = 146,
    D3DRS_AMBIENTMATERIALSOURCE = 147,
    D3DRS_EMISSIVEMATERIALSOURCE = 148,
    D3DRS_VERTEXBLEND = 151,
    D3DRS_CLIPPLANEENABLE = 152,
    D3DRS_SOFTWAREVERTEXPROCESSING = 153,
    D3DRS_POINTSIZE = 154,
    D3DRS_POINTSIZE_MIN = 155,
    D3DRS_POINTSPRITEENABLE = 156,
    D3DRS_POINTSCALEENABLE = 157,
    D3DRS_POINTSCALE_A = 158,
    D3DRS_POINTSCALE_B = 159,
    D3DRS_POINTSCALE_C = 160,
    D3DRS_MULTISAMPLEANTIALIAS = 161,
    D3DRS_MULTISAMPLEMASK = 162,
    D3DRS_PATCHEDGESTYLE = 163,
    D3DRS_PATCHSEGMENTS = 164,
    D3DRS_DEBUGMONITORTOKEN = 165,
    D3DRS_POINTSIZE_MAX = 166,
    D3DRS_INDEXEDVERTEXBLENDENABLE = 167,
    D3DRS_COLORWRITEENABLE = 168,
    D3DRS_TWEENFACTOR = 170,
    D3DRS_BLENDOP = 171,
    D3DRS_POSITIONORDER = 172,
    D3DRS_NORMALORDER = 173,
    D3DRS_FORCE_DWORD = 0x7fffffff
} D3DRENDERSTATETYPE;

typedef enum _D3DTEXTURESTAGESTATETYPE {
    D3DTSS_COLOROP = 1,
    D3DTSS_COLORARG1 = 2,
    D3DTSS_COLORARG2 = 3,
    D3DTSS_ALPHAOP = 4,
    D3DTSS_ALPHAARG1 = 5,
    D3DTSS_ALPHAARG2 = 6,
    D3DTSS_BUMPENVMAT00 = 7,
    D3DTSS_BUMPENVMAT01 = 8,
    D3DTSS_BUMPENVMAT10 = 9,
    D3DTSS_BUMPENVMAT11 = 10,
    D3DTSS_TEXCOORDINDEX = 11,
    D3DTSS_ADDRESSU = 13,
    D3DTSS_ADDRESSV = 14,
    D3DTSS_BORDERCOLOR = 15,
    D3DTSS_MAGFILTER = 16,
    D3DTSS_MINFILTER = 17,
    D3DTSS_MIPFILTER = 18,
    D3DTSS_MIPMAPLODBIAS = 19,
    D3DTSS_MAXMIPLEVEL = 20,
    D3DTSS_MAXANISOTROPY = 21,
    D3DTSS_BUMPENVLSCALE = 22,
    D3DTSS_BUMPENVLOFFSET = 23,
    D3DTSS_TEXTURETRANSFORMFLAGS = 24,
    D3DTSS_ADDRESSW = 25,
    D3DTSS_COLORARG0 = 26,
    D3DTSS_ALPHAARG0 = 27,
    D3DTSS_RESULTARG = 28,
    D3DTSS_FORCE_DWORD = 0x7fffffff
} D3DTEXTURESTAGESTATETYPE;

#define D3DTSS_TCI_PASSTHRU                    0x00000000
#define D3DTSS_TCI_CAMERASPACENORMAL           0x00010000
#define D3DTSS_TCI_CAMERASPACEPOSITION         0x00020000
#define D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR 0x00030000

typedef enum _D3DTEXTUREOP {
    D3DTOP_DISABLE = 1,
    D3DTOP_SELECTARG1 = 2,
    D3DTOP_SELECTARG2 = 3,
    D3DTOP_MODULATE = 4,
    D3DTOP_MODULATE2X = 5,
    D3DTOP_MODULATE4X = 6,
    D3DTOP_ADD = 7,
    D3DTOP_ADDSIGNED = 8,
    D3DTOP_ADDSIGNED2X = 9,
    D3DTOP_SUBTRACT = 10,
    D3DTOP_ADDSMOOTH = 11,
    D3DTOP_BLENDDIFFUSEALPHA = 12,
    D3DTOP_BLENDTEXTUREALPHA = 13,
    D3DTOP_BLENDFACTORALPHA = 14,
    D3DTOP_BLENDTEXTUREALPHAPM = 15,
    D3DTOP_BLENDCURRENTALPHA = 16,
    D3DTOP_PREMODULATE = 17,
    D3DTOP_MODULATEALPHA_ADDCOLOR = 18,
    D3DTOP_MODULATECOLOR_ADDALPHA = 19,
    D3DTOP_MODULATEINVALPHA_ADDCOLOR = 20,
    D3DTOP_MODULATEINVCOLOR_ADDALPHA = 21,
    D3DTOP_BUMPENVMAP = 22,
    D3DTOP_BUMPENVMAPLUMINANCE = 23,
    D3DTOP_DOTPRODUCT3 = 24,
    D3DTOP_MULTIPLYADD = 25,
    D3DTOP_LERP = 26,
    D3DTOP_FORCE_DWORD = 0x7fffffff
} D3DTEXTUREOP;

#define D3DTA_SELECTMASK     0x0000000f
#define D3DTA_DIFFUSE        0x00000000
#define D3DTA_CURRENT        0x00000001
#define D3DTA_TEXTURE        0x00000002
#define D3DTA_TFACTOR        0x00000003
#define D3DTA_SPECULAR       0x00000004
#define D3DTA_TEMP           0x00000005
#define D3DTA_COMPLEMENT     0x00000010
#define D3DTA_ALPHAREPLICATE 0x00000020

typedef enum _D3DTEXTUREFILTERTYPE {
    D3DTEXF_NONE = 0,
    D3DTEXF_POINT = 1,
    D3DTEXF_LINEAR = 2,
    D3DTEXF_ANISOTROPIC = 3,
    D3DTEXF_FLATCUBIC = 4,
    D3DTEXF_GAUSSIANCUBIC = 5,
    D3DTEXF_FORCE_DWORD = 0x7fffffff
} D3DTEXTUREFILTERTYPE;

typedef enum _D3DTEXTUREADDRESS {
    D3DTADDRESS_WRAP = 1,
    D3DTADDRESS_MIRROR = 2,
    D3DTADDRESS_CLAMP = 3,
    D3DTADDRESS_BORDER = 4,
    D3DTADDRESS_MIRRORONCE = 5,
    D3DTADDRESS_FORCE_DWORD = 0x7fffffff
} D3DTEXTUREADDRESS;

typedef enum _D3DBLEND {
    D3DBLEND_ZERO = 1,
    D3DBLEND_ONE = 2,
    D3DBLEND_SRCCOLOR = 3,
    D3DBLEND_INVSRCCOLOR = 4,
    D3DBLEND_SRCALPHA = 5,
    D3DBLEND_INVSRCALPHA = 6,
    D3DBLEND_DESTALPHA = 7,
    D3DBLEND_INVDESTALPHA = 8,
    D3DBLEND_DESTCOLOR = 9,
    D3DBLEND_INVDESTCOLOR = 10,
    D3DBLEND_SRCALPHASAT = 11,
    D3DBLEND_BOTHSRCALPHA = 12,
    D3DBLEND_BOTHINVSRCALPHA = 13,
    D3DBLEND_FORCE_DWORD = 0x7fffffff
} D3DBLEND;

typedef enum _D3DBLENDOP {
    D3DBLENDOP_ADD = 1,
    D3DBLENDOP_SUBTRACT = 2,
    D3DBLENDOP_REVSUBTRACT = 3,
    D3DBLENDOP_MIN = 4,
    D3DBLENDOP_MAX = 5,
    D3DBLENDOP_FORCE_DWORD = 0x7fffffff
} D3DBLENDOP;

typedef enum _D3DCMPFUNC {
    D3DCMP_NEVER = 1,
    D3DCMP_LESS = 2,
    D3DCMP_EQUAL = 3,
    D3DCMP_LESSEQUAL = 4,
    D3DCMP_GREATER = 5,
    D3DCMP_NOTEQUAL = 6,
    D3DCMP_GREATEREQUAL = 7,
    D3DCMP_ALWAYS = 8,
    D3DCMP_FORCE_DWORD = 0x7fffffff
} D3DCMPFUNC;

typedef enum _D3DCULL {
    D3DCULL_NONE = 1,
    D3DCULL_CW = 2,
    D3DCULL_CCW = 3,
    D3DCULL_FORCE_DWORD = 0x7fffffff
} D3DCULL;

typedef enum _D3DZBUFFERTYPE {
    D3DZB_FALSE = 0,
    D3DZB_TRUE = 1,
    D3DZB_USEW = 2,
    D3DZB_FORCE_DWORD = 0x7fffffff
} D3DZBUFFERTYPE;

typedef enum _D3DFOGMODE {
    D3DFOG_NONE = 0,
    D3DFOG_EXP = 1,
    D3DFOG_EXP2 = 2,
    D3DFOG_LINEAR = 3,
    D3DFOG_FORCE_DWORD = 0x7fffffff
} D3DFOGMODE;

typedef enum _D3DMATERIALCOLORSOURCE {
    D3DMCS_MATERIAL = 0,
    D3DMCS_COLOR1 = 1,
    D3DMCS_COLOR2 = 2,
    D3DMCS_FORCE_DWORD = 0x7fffffff
} D3DMATERIALCOLORSOURCE;

typedef enum _D3DPOOL {
    D3DPOOL_DEFAULT = 0,
    D3DPOOL_MANAGED = 1,
    D3DPOOL_SYSTEMMEM = 2,
    D3DPOOL_SCRATCH = 3,
    D3DPOOL_FORCE_DWORD = 0x7fffffff
} D3DPOOL;

typedef enum _D3DRESOURCETYPE {
    D3DRTYPE_SURFACE = 1,
    D3DRTYPE_VOLUME = 2,
    D3DRTYPE_TEXTURE = 3,
    D3DRTYPE_VOLUMETEXTURE = 4,
    D3DRTYPE_CUBETEXTURE = 5,
    D3DRTYPE_VERTEXBUFFER = 6,
    D3DRTYPE_INDEXBUFFER = 7,
    D3DRTYPE_FORCE_DWORD = 0x7fffffff
} D3DRESOURCETYPE;

typedef enum _D3DMULTISAMPLE_TYPE {
    D3DMULTISAMPLE_NONE = 0,
    D3DMULTISAMPLE_FORCE_DWORD = 0x7fffffff
} D3DMULTISAMPLE_TYPE;

typedef enum _D3DSWAPEFFECT {
    D3DSWAPEFFECT_DISCARD = 1,
    D3DSWAPEFFECT_FLIP = 2,
    D3DSWAPEFFECT_COPY = 3,
    D3DSWAPEFFECT_COPY_VSYNC = 4,
    D3DSWAPEFFECT_FORCE_DWORD = 0xffffffff
} D3DSWAPEFFECT;

typedef enum _D3DDEVTYPE {
    D3DDEVTYPE_HAL = 1,
    D3DDEVTYPE_REF = 2,
    D3DDEVTYPE_SW = 3,
    D3DDEVTYPE_FORCE_DWORD = 0xffffffff
} D3DDEVTYPE;

typedef struct _D3DSURFACE_DESC {
    D3DFORMAT           Format;
    D3DRESOURCETYPE     Type;
    DWORD               Usage;
    D3DPOOL             Pool;
    UINT                Size;
    D3DMULTISAMPLE_TYPE MultiSampleType;
    UINT                Width, Height;
} D3DSURFACE_DESC;

typedef struct _D3DVERTEXBUFFER_DESC {
    D3DFORMAT       Format;
    D3DRESOURCETYPE Type;
    DWORD           Usage;
    D3DPOOL         Pool;
    UINT            Size;
    DWORD           FVF;
} D3DVERTEXBUFFER_DESC;

typedef struct _D3DINDEXBUFFER_DESC {
    D3DFORMAT       Format;
    D3DRESOURCETYPE Type;
    DWORD           Usage;
    D3DPOOL         Pool;
    UINT            Size;
} D3DINDEXBUFFER_DESC;

typedef struct _D3DDISPLAYMODE {
    UINT      Width, Height, RefreshRate;
    D3DFORMAT Format;
} D3DDISPLAYMODE;

typedef struct _D3DPRESENT_PARAMETERS_ {
    UINT                BackBufferWidth, BackBufferHeight;
    D3DFORMAT           BackBufferFormat;
    UINT                BackBufferCount;
    D3DMULTISAMPLE_TYPE MultiSampleType;
    D3DSWAPEFFECT       SwapEffect;
    HWND                hDeviceWindow;
    BOOL                Windowed;
    BOOL                EnableAutoDepthStencil;
    D3DFORMAT           AutoDepthStencilFormat;
    DWORD               Flags;
    UINT                FullScreen_RefreshRateInHz;
    UINT                FullScreen_PresentationInterval;
} D3DPRESENT_PARAMETERS;

#define D3DPRESENT_INTERVAL_DEFAULT   0x00000000
#define D3DPRESENT_INTERVAL_ONE       0x00000001
#define D3DPRESENT_INTERVAL_IMMEDIATE 0x80000000

#define D3DADAPTER_DEFAULT 0
#define D3DCREATE_SOFTWARE_VERTEXPROCESSING 0x00000020
#define D3DCREATE_HARDWARE_VERTEXPROCESSING 0x00000040
#define D3DCREATE_MIXED_VERTEXPROCESSING    0x00000080

#define D3DCLEAR_TARGET  0x00000001
#define D3DCLEAR_ZBUFFER 0x00000002
#define D3DCLEAR_STENCIL 0x00000004

#define D3DUSAGE_RENDERTARGET       0x00000001
#define D3DUSAGE_DEPTHSTENCIL       0x00000002
#define D3DUSAGE_WRITEONLY          0x00000008
#define D3DUSAGE_SOFTWAREPROCESSING 0x00000010
#define D3DUSAGE_DONOTCLIP          0x00000020
#define D3DUSAGE_POINTS             0x00000040
#define D3DUSAGE_RTPATCHES          0x00000080
#define D3DUSAGE_NPATCHES           0x00000100
#define D3DUSAGE_DYNAMIC            0x00000200

#define D3DLOCK_READONLY        0x00000010
#define D3DLOCK_DISCARD         0x00002000
#define D3DLOCK_NOOVERWRITE     0x00001000
#define D3DLOCK_NOSYSLOCK       0x00000800
#define D3DLOCK_NO_DIRTY_UPDATE 0x00008000

#define D3DCOLORWRITEENABLE_RED   (1L << 0)
#define D3DCOLORWRITEENABLE_GREEN (1L << 1)
#define D3DCOLORWRITEENABLE_BLUE  (1L << 2)
#define D3DCOLORWRITEENABLE_ALPHA (1L << 3)

/* Flexible vertex format */
#define D3DFVF_RESERVED0      0x001
#define D3DFVF_POSITION_MASK  0x00E
#define D3DFVF_XYZ            0x002
#define D3DFVF_XYZRHW         0x004
#define D3DFVF_XYZB1          0x006
#define D3DFVF_XYZB2          0x008
#define D3DFVF_XYZB3          0x00a
#define D3DFVF_XYZB4          0x00c
#define D3DFVF_XYZB5          0x00e
#define D3DFVF_NORMAL         0x010
#define D3DFVF_PSIZE          0x020
#define D3DFVF_DIFFUSE        0x040
#define D3DFVF_SPECULAR       0x080
#define D3DFVF_TEXCOUNT_MASK  0xf00
#define D3DFVF_TEXCOUNT_SHIFT 8
#define D3DFVF_TEX0           0x000
#define D3DFVF_TEX1           0x100
#define D3DFVF_TEX2           0x200
#define D3DFVF_TEX3           0x300
#define D3DFVF_TEX4           0x400
#define D3DFVF_TEX5           0x500
#define D3DFVF_TEX6           0x600
#define D3DFVF_TEX7           0x700
#define D3DFVF_TEX8           0x800
#define D3DFVF_LASTBETA_UBYTE4 0x1000
#define D3DFVF_TEXTUREFORMAT1 3
#define D3DFVF_TEXTUREFORMAT2 0
#define D3DFVF_TEXTUREFORMAT3 1
#define D3DFVF_TEXTUREFORMAT4 2
#define D3DFVF_TEXCOORDSIZE1(i) (D3DFVF_TEXTUREFORMAT1 << ((i) * 2 + 16))
#define D3DFVF_TEXCOORDSIZE2(i) (D3DFVF_TEXTUREFORMAT2)
#define D3DFVF_TEXCOORDSIZE3(i) (D3DFVF_TEXTUREFORMAT3 << ((i) * 2 + 16))
#define D3DFVF_TEXCOORDSIZE4(i) (D3DFVF_TEXTUREFORMAT4 << ((i) * 2 + 16))

/* ---- Interfaces ----------------------------------------------------------------------- */

struct IUnknown {
    virtual HRESULT QueryInterface(REFIID riid, void **ppv) = 0;
    virtual unsigned long AddRef() = 0;
    virtual unsigned long Release() = 0;
protected:
    virtual ~IUnknown() {}
};

struct IDirect3DDevice8;

struct IDirect3DResource8 : public IUnknown {
    virtual HRESULT GetDevice(IDirect3DDevice8 **ppDevice) = 0;
    virtual DWORD SetPriority(DWORD PriorityNew) = 0;
    virtual DWORD GetPriority() = 0;
    virtual void PreLoad() = 0;
    virtual D3DRESOURCETYPE GetType() = 0;
};

struct IDirect3DBaseTexture8 : public IDirect3DResource8 {
    virtual DWORD SetLOD(DWORD LODNew) = 0;
    virtual DWORD GetLOD() = 0;
    virtual DWORD GetLevelCount() = 0;
};

struct IDirect3DTexture8 : public IDirect3DBaseTexture8 {
    virtual HRESULT GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) = 0;
    virtual HRESULT LockRect(UINT Level, D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) = 0;
    virtual HRESULT UnlockRect(UINT Level) = 0;
};

struct IDirect3DVertexBuffer8 : public IDirect3DResource8 {
    virtual HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, BYTE **ppbData, DWORD Flags) = 0;
    virtual HRESULT Unlock() = 0;
    virtual HRESULT GetDesc(D3DVERTEXBUFFER_DESC *pDesc) = 0;
};

struct IDirect3DIndexBuffer8 : public IDirect3DResource8 {
    virtual HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, BYTE **ppbData, DWORD Flags) = 0;
    virtual HRESULT Unlock() = 0;
    virtual HRESULT GetDesc(D3DINDEXBUFFER_DESC *pDesc) = 0;
};

struct IDirect3D8;

struct IDirect3DDevice8 : public IUnknown {
    virtual HRESULT GetDirect3D(IDirect3D8 **ppD3D8) = 0;
    virtual HRESULT TestCooperativeLevel() = 0;
    virtual HRESULT Reset(D3DPRESENT_PARAMETERS *pPresentationParameters) = 0;
    virtual HRESULT Present(const RECT *pSourceRect, const RECT *pDestRect, HWND hDestWindowOverride,
                            const RGNDATA *pDirtyRegion) = 0;

    virtual HRESULT CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format,
                                  D3DPOOL Pool, IDirect3DTexture8 **ppTexture) = 0;
    virtual HRESULT CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool,
                                       IDirect3DVertexBuffer8 **ppVertexBuffer) = 0;
    virtual HRESULT CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                      IDirect3DIndexBuffer8 **ppIndexBuffer) = 0;

    virtual HRESULT BeginScene() = 0;
    virtual HRESULT EndScene() = 0;
    virtual HRESULT Clear(DWORD Count, const D3DRECT *pRects, DWORD Flags, D3DCOLOR Color, float Z,
                          DWORD Stencil) = 0;
    virtual HRESULT SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX *pMatrix) = 0;
    virtual HRESULT GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX *pMatrix) = 0;
    virtual HRESULT MultiplyTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX *pMatrix) = 0;
    virtual HRESULT SetViewport(const D3DVIEWPORT8 *pViewport) = 0;
    virtual HRESULT GetViewport(D3DVIEWPORT8 *pViewport) = 0;
    virtual HRESULT SetMaterial(const D3DMATERIAL8 *pMaterial) = 0;
    virtual HRESULT GetMaterial(D3DMATERIAL8 *pMaterial) = 0;
    virtual HRESULT SetLight(DWORD Index, const D3DLIGHT8 *pLight) = 0;
    virtual HRESULT GetLight(DWORD Index, D3DLIGHT8 *pLight) = 0;
    virtual HRESULT LightEnable(DWORD Index, BOOL Enable) = 0;
    virtual HRESULT GetLightEnable(DWORD Index, BOOL *pEnable) = 0;
    virtual HRESULT SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) = 0;
    virtual HRESULT GetRenderState(D3DRENDERSTATETYPE State, DWORD *pValue) = 0;
    virtual HRESULT GetTexture(DWORD Stage, IDirect3DBaseTexture8 **ppTexture) = 0;
    virtual HRESULT SetTexture(DWORD Stage, IDirect3DBaseTexture8 *pTexture) = 0;
    virtual HRESULT GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD *pValue) = 0;
    virtual HRESULT SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) = 0;

    virtual HRESULT DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) = 0;
    virtual HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT minIndex, UINT NumVertices,
                                         UINT startIndex, UINT primCount) = 0;
    virtual HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount,
                                    const void *pVertexStreamZeroData, UINT VertexStreamZeroStride) = 0;
    virtual HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex,
                                           UINT NumVertexIndices, UINT PrimitiveCount,
                                           const void *pIndexData, D3DFORMAT IndexDataFormat,
                                           const void *pVertexStreamZeroData,
                                           UINT VertexStreamZeroStride) = 0;

    /* In Direct3D 8 a fixed-function "vertex shader" handle is an FVF code. */
    virtual HRESULT SetVertexShader(DWORD Handle) = 0;
    virtual HRESULT GetVertexShader(DWORD *pHandle) = 0;
    virtual HRESULT SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer8 *pStreamData, UINT Stride) = 0;
    virtual HRESULT GetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer8 **ppStreamData,
                                    UINT *pStride) = 0;
    virtual HRESULT SetIndices(IDirect3DIndexBuffer8 *pIndexData, UINT BaseVertexIndex) = 0;
    virtual HRESULT GetIndices(IDirect3DIndexBuffer8 **ppIndexData, UINT *pBaseVertexIndex) = 0;
    virtual HRESULT SetPixelShader(DWORD Handle) = 0;
    virtual HRESULT GetPixelShader(DWORD *pHandle) = 0;
};

struct IDirect3D8 : public IUnknown {
    virtual UINT GetAdapterCount() = 0;
    virtual HRESULT GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE *pMode) = 0;
    virtual HRESULT CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat,
                                      DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) = 0;
    virtual HRESULT CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
                                 DWORD BehaviorFlags, D3DPRESENT_PARAMETERS *pPresentationParameters,
                                 IDirect3DDevice8 **ppReturnedDeviceInterface) = 0;
};

typedef IDirect3D8             *LPDIRECT3D8;
typedef IDirect3DDevice8       *LPDIRECT3DDEVICE8;
typedef IDirect3DTexture8      *LPDIRECT3DTEXTURE8;
typedef IDirect3DBaseTexture8  *LPDIRECT3DBASETEXTURE8;
typedef IDirect3DVertexBuffer8 *LPDIRECT3DVERTEXBUFFER8;
typedef IDirect3DIndexBuffer8  *LPDIRECT3DINDEXBUFFER8;

/* Creates the Direct3D object. The vr_platform and vr_gfx layers are
 * initialized by CreateDevice if the application has not done so. */
IDirect3D8 *Direct3DCreate8(UINT SDKVersion);

#endif
