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

Filesystem calls are placeholders returning `VR_ERR_UNSUPPORTED` (tell/size:
`-1`), and input polling returns a zeroed state. These are issues #2 and #3;
building the library does not imply these capabilities are implemented.

Configure with `-DVR_BUILD_SAMPLES=ON` to link `vr_platform_smoke`. On Vita its
log should appear at `ux0:data/vr-platform-smoke/log.txt`, report at least 2000
microseconds elapsed, file result `-5`, and zero buttons. The sample is an ELF;
SELF/VPK packaging is left to the consuming application. Cross-compilation and
linking have been checked; on-device timing and persistent logging still require
hardware validation.
