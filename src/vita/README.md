# Vita backend

The platform backend implements initialization, monotonic timing, sleep, and
logging with VitaSDK. It links the user-mode `SceLibKernel_stub`,
`SceIofilemgr_stub`, and `SceKernelThreadMgr_stub` libraries.

With a NULL data root, the backend uses `ux0:data/<app_name>` (default app name:
`vita-runtime`). The app name must be a single directory component in this case.
An explicitly supplied root must be nonempty and fit in the path buffer with
`/log.txt` appended. Initialization attempts to create the root directory;
parents of a custom nested root must already exist.

Logs append to `<data_root>/log.txt` and always go to `sceClibPrintf` too. An
unavailable directory/file does not fail initialization: console output remains
available. Records are bounded to 2047 bytes including the newline. Shutdown
closes the log handle. Sleep requests larger than the SDK's 32-bit argument are
split into chunks.

The filesystem backend uses `sceIoOpen`/`sceIoRead`/`sceIoLseek`/`sceIoClose`
and matches the host behavior: unsafe or over-long paths return
`VR_ERR_INVALID_ARG`, open failures return `VR_ERR_NOT_FOUND`, read errors
return `VR_ERR_IO`, short reads are retried until the request is filled or end
of file, and `vr_file_size()` restores the current position.

The input backend initializes lazily on the first `vr_input_poll()`: analog-wide
sampling, and touch sampling on both panels. Sticks map 0..255 to -1..1 (up is
negative Y) and go through the shared radial deadzone (`src/input_common.c`).
Touch points (at most `VR_MAX_TOUCHES` per panel) are normalized to 0..1 using
each panel's active area from `sceTouchGetPanelInfo`. Links `SceCtrl_stub` and
`SceTouch_stub`.

## Graphics

`vr_gfx` runs on vitaGL. Each distinct fixed-function configuration (texture
combiners, fog, alpha test, vertex colour byte order) gets a small Cg shader
pair that is generated, compiled at runtime and cached. vitaGL's runtime
compiler needs `libshacccg.suprx` extracted to `ur0:data/` on the device, as for
any vitaGL application.

The display size is fixed at 960x544. `vr_gfx_init` returns
`VR_ERR_UNSUPPORTED` for any other size; 0 selects the default.

vitaGL gaps and how the backend handles them:

- No `vglEnd` (no way to tear the GPU context down): `vr_gfx_shutdown` frees
  the runtime's objects but keeps the context, and a later `vr_gfx_init`
  reuses it instead of calling `vglInit` again.
- No `GL_TEXTURE_MAX_LEVEL`: a texture with a partial mip chain cannot be
  limited to its levels, so it is sampled from level 0 with no mip filtering.
  Give textures a full chain to get mipmapping.
- No constant blend colour: `VR_BLEND_CONSTANT` and `VR_BLEND_INV_CONSTANT`
  become `GL_ONE` and `GL_ZERO`, and a one-time warning is logged.

## Audio

`vr_audio` mixes on a high-priority thread and writes through `sceAudioOut` in
512-frame grains; the blocking output call paces the thread. At 48 kHz it uses
the MAIN port, at lower rates the BGM port. Rates above 48 kHz return
`VR_ERR_UNSUPPORTED`. Output is stereo 16-bit.

Configure with `-DVR_BUILD_SAMPLES=ON` to link `vr_platform_smoke` and `vr_gfx_spin`. On Vita the smoke sample's
log should appear at `ux0:data/vr-platform-smoke/log.txt`, report at least 2000
microseconds elapsed, file result `-2` (not found), and the buttons held at launch. The sample is an ELF;
SELF/VPK packaging is left to the consuming application. Cross-compilation and
linking have been checked; on-device timing and persistent logging still require
hardware validation. `vr_gfx_spin` draws a spinning textured square for about
five seconds over a pretransformed backdrop. It needs `libshacccg.suprx` on the device and
has not been run on hardware yet.
