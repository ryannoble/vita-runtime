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

Configure with `-DVR_BUILD_SAMPLES=ON` to link `vr_platform_smoke`. On Vita its
log should appear at `ux0:data/vr-platform-smoke/log.txt`, report at least 2000
microseconds elapsed, file result `-2` (not found), and the buttons held at launch. The sample is an ELF;
SELF/VPK packaging is left to the consuming application. Cross-compilation and
linking have been checked; on-device timing and persistent logging still require
hardware validation.
