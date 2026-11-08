# Direct3D 8 shim

A source-compatible subset of the Direct3D 8 fixed-function pipeline, built on
`vr_gfx`. A port that already renders through `IDirect3DDevice8` can usually
keep its rendering code and link this library instead of the Windows runtime.

It is written in C++11. The interfaces are plain abstract C++ classes, not a
binary COM ABI: there is no real `IUnknown` layout, `QueryInterface` always
returns `E_NOINTERFACE`, and you must compile your code against these headers.

## Using it

```cpp
#include <d3d8.h>   // or "vr_d3d8.h"; d3d8.h just includes vr_d3d8.h

IDirect3D8 *d3d = Direct3DCreate8(D3D_SDK_VERSION);
```

```cmake
target_link_libraries(my_port PRIVATE vr_d3d8)
```

The `vr_d3d8` target links `vita_runtime` and adds `d3dshim/include` to your
include path. It is built when the CMake option `VR_BUILD_D3D8` is `ON` (the
default). Turn it off for a C-only build.

`CreateDevice` initializes `vr_gfx` if it is not already running. The
`FullScreen_PresentationInterval` field of the present parameters selects
vsync (`D3DPRESENT_INTERVAL_IMMEDIATE` turns it off). Window handles and
behavior flags are ignored.

## Resolution scaling

The Vita display is fixed at 960x544. The back-buffer size in the present
parameters is a virtual size: your code keeps working in its own coordinates
and the shim scales them to the real display.

- Viewports (`SetViewport`) and `Clear` rectangles are scaled by
  `real size / virtual size` and rounded to whole pixels.
- Pretransformed vertices (`D3DFVF_XYZRHW`) are scaled with the same factors
  when drawn (`screen_scale` in `vr_gfx`).
- Direct3D 8 puts pixel centres on integer coordinates, while `vr_gfx` samples
  at `x + 0.5`. The shim passes a `screen_offset` of +0.5 so that a D3D
  coordinate of 0 lands on the centre of pixel 0. For pretransformed vertices
  the offset is half a virtual pixel (0.5 × scale), so 2D quads cover whole
  scaled pixels. For transformed geometry it is half a real pixel, which keeps
  viewport edges covered when scaling. The two are the same at 1:1.
- A back-buffer size of 0 means "use the real size".
- `GetAdapterDisplayMode` reports 960x544 at 60 Hz, `D3DFMT_X8R8G8B8`.

The scaling is a stretch. It does not preserve aspect ratio.

## Supported

| Area | What works |
| ---- | ---------- |
| Objects | `Direct3DCreate8`, one adapter, `CreateDevice`, `Reset`, `Present`, `BeginScene`/`EndScene`, `CheckDeviceFormat` (see formats below) |
| Primitives | point list, line list/strip, triangle list/strip/fan; `DrawPrimitive`, `DrawIndexedPrimitive`, and the `...UP` variants; 16-bit and 32-bit indices; base vertex index |
| Vertex formats | FVF codes passed to `SetVertexShader`: `XYZ`, `XYZRHW`, `XYZB1`..`XYZB5` (weights skipped), normal, point size (skipped), diffuse, specular, up to 8 texture coordinate sets with 1, 2, 3 or 4 components |
| Buffers | vertex and index buffers (`Lock`/`Unlock` with a CPU copy; `Unlock` uploads the locked range), stream 0 only |
| Textures | 2D textures with mip chains, `LockRect`/`UnlockRect` per level, formats `A8R8G8B8`, `X8R8G8B8`, `R5G6B5`, `A1R5G5B5`, `X1R5G5B5`, `A4R4G4B4`, `L8`, `A8`, `A8L8`, `DXT1`..`DXT5` (DXT2 and DXT4 decode as DXT3 and DXT5) |
| Transforms | world (index 0), view, projection, texture matrices are stored; `MultiplyTransform` |
| Render states | `ZENABLE`, `ZWRITEENABLE`, `ZFUNC`, `ZBIAS`, alpha test (`ALPHATESTENABLE`, `ALPHAFUNC`, `ALPHAREF`), alpha blending (`SRCBLEND`, `DESTBLEND` including `BOTHSRCALPHA` and `BOTHINVSRCALPHA`, `BLENDOP`), `CULLMODE`, `COLORWRITEENABLE`, `SPECULARENABLE`, `TEXTUREFACTOR`, linear fog, plus the lighting states below |
| Clear | colour and depth, with optional rectangles clipped to the viewport |
| Texture stages | two stages (see limits below): colour and alpha ops `DISABLE`, `SELECTARG1/2`, `MODULATE`, `MODULATE2X/4X`, `ADD`, `ADDSIGNED`, `ADDSIGNED2X`, `SUBTRACT`, `ADDSMOOTH`, `BLENDDIFFUSEALPHA`, `BLENDTEXTUREALPHA`, `BLENDFACTORALPHA`, `BLENDCURRENTALPHA`, `DOTPRODUCT3`; arguments `DIFFUSE`, `CURRENT`, `TEXTURE`, `TFACTOR`, `SPECULAR`, with `COMPLEMENT` and `ALPHAREPLICATE`; `TEXCOORDINDEX` (set index only); `ADDRESSU/V` wrap, mirror, clamp; point and linear mag/min/mip filters |

### Lighting

When `D3DRS_LIGHTING` is `TRUE` (the Direct3D default) and the vertices are
untransformed (not `XYZRHW`), the shim lights vertices on the CPU and draws the
result. This applies to every draw call, so scenes that do not need lighting
should set `D3DRS_LIGHTING` to `FALSE`. Pretransformed vertices are never
lit.

- Light types: directional, point and spot. Lights are set with `SetLight` and must be
  enabled with `LightEnable`.
- Point and spot lights use range cut-off and the three attenuation terms. Spot
  lights use theta, phi and falloff.
- Colour terms: global ambient (`D3DRS_AMBIENT`), light ambient and diffuse,
  and specular when `D3DRS_SPECULARENABLE` is set (Blinn half vector, material
  power, camera position taken from the view matrix).
- Material colour sources: diffuse, ambient, specular and emissive may each
  come from the material or from the vertex colour 1 (diffuse) or 2 (specular)
  via `D3DRS_*MATERIALSOURCE`, when `D3DRS_COLORVERTEX` is `TRUE` and the
  vertex has that colour. Otherwise the material is used.
- `D3DRS_NORMALIZENORMALS` is honoured. Normals are transformed by the world
  matrix only (no inverse transpose).
- Only the first two texture coordinate sets referenced by stages 0 and 1 are
  passed through, and only their first two components.

## Not supported yet

Items marked "warns" log a one-time warning through `vr_log` at `VR_LOG_WARN`
the first time they are used (for example
`d3d8 shim: pixel shaders is not supported`). Everything else listed here is
silently ignored.

| Feature | Behavior |
| ------- | -------- |
| Programmable vertex shaders | `SetVertexShader` with a non-FVF handle warns and returns `D3DERR_INVALIDCALL` |
| Pixel shaders | `SetPixelShader` with a nonzero handle warns and returns `D3DERR_INVALIDCALL` |
| Vertex streams other than 0 | warns, returns `D3DERR_INVALIDCALL` |
| More than two texture stages | stages after the second are dropped; warns. Textures can still be set on stages 2..7 |
| Some texture ops | `PREMODULATE`, `MODULATEALPHA_ADDCOLOR`, `MODULATECOLOR_ADDALPHA`, `MODULATEINVALPHA_ADDCOLOR`, `MODULATEINVCOLOR_ADDALPHA`, `BUMPENVMAP`, `BUMPENVMAPLUMINANCE`, and any other op not listed above fall back to `MODULATE`, with a warning. `BLENDTEXTUREALPHAPM` warns too but is drawn as plain `BLENDTEXTUREALPHA`. `D3DTA_TEMP` is treated as `CURRENT` |
| Texture transforms and generated coordinates | `TEXTURETRANSFORMFLAGS` and `TCI_*` coordinate generation are ignored; warns. Texture matrices are stored but not applied |
| Exponential fog | `EXP` and `EXP2` warn; fog is off for that draw. Only linear fog works. Range-based fog and per-vertex fog are not used |
| Stencil | stencil render states are stored; enabling stencil warns and has no effect. The stencil value in `Clear` is ignored |
| Render targets and depth buffers | textures cannot be created with `D3DUSAGE_RENDERTARGET` or `D3DUSAGE_DEPTHSTENCIL`. There is no `SetRenderTarget`, `GetBackBuffer`, `CreateRenderTarget`, `CreateDepthStencilSurface` or `CopyRects` |
| Texture kinds | no cube or volume textures, no surfaces, no palettised formats. `SetLOD` is a no-op and `GetLOD` returns 0 |
| Vertex blending | weights in `XYZB1`..`XYZB5` are skipped, not applied. Only world matrix 0 is available |
| Point sprites and point size | point lists draw, but `POINTSIZE*`, `POINTSCALE*` and `POINTSPRITEENABLE` are not applied |
| Fill and shade modes | wireframe and point fill, and flat shading, are not applied (always solid, Gouraud) |
| User clip planes, scissor, dithering, multisampling | not applied |
| `GetDeviceCaps` | absent from `IDirect3D8` and `IDirect3DDevice8`. Query formats with `CheckDeviceFormat` only |
| Other | `Present` ignores its arguments, `TestCooperativeLevel` always returns `D3D_OK`, `QueryInterface` is not implemented, windowed mode and multiple adapters are not modelled |

Not every unsupported render state warns. If a port behaves differently from
Windows, check this list before looking elsewhere.

## Tests

`tests/test_d3d8.cpp` builds as `vr_d3d8_tests` when this repository is the
top-level project, and runs under `ctest` with `VR_HEADLESS=1` against the host
software rasterizer.
