# vita-runtime

An engine-agnostic runtime for bringing PC games to the PS Vita. It gives a
port one small C API for platform services, files, input, and (planned)
graphics and audio, with a Vita backend and a host backend for testing on a PC.

The project contains **no game-specific code or data**, and it never will.
See [CONTRIBUTING.md](CONTRIBUTING.md).

## Status

| Module                        | Host backend    | Vita backend |
| ----------------------------- | --------------- | ------------ |
| `vr_platform` (time, logging) | done            | done; verified on hardware |
| `vr_fs` (read-only files)     | done            | done; verified on hardware |
| `vr_input` (pad, touch)       | headless or SDL2 gamepad | done; verified on hardware (buttons, sticks, front and rear touch) |
| `vr_gfx` (3D, fixed-function) | software reference rasterizer; SDL2 window optional | vitaGL; verified on hardware (60 fps) |
| `vr_audio` (mixer)            | software mixer; SDL2 device optional | sceAudioOut; verified on hardware |
| Direct3D 8 shim (`d3dshim/`)  | done, on top of `vr_gfx` | verified on hardware (lighting, depth, culling, textures) |
| Direct3D 9 shim               | planned         | planned      |
| Shader translator (→ GXP)     | planned         | planned      |

## Building and testing (host)

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

To test with a real gamepad, install SDL2 (`brew install sdl2` on macOS or
`apt install libsdl2-dev` on Debian/Ubuntu) and configure with
`-DVR_HOST_SDL2=ON`. The default build remains headless and needs no SDL2.
The SDL2 backend uses the first available game controller, supports reconnecting,
and applies the shared stick deadzone. Touch input remains empty. Poll input on
the main thread, as required by SDL event pumping.

With `VR_HOST_SDL2=ON`, `vr_gfx` also opens a window that shows the back buffer,
and `vr_audio` opens the default audio device. Set `VR_HEADLESS=1` to disable
both. Without SDL2 there is no window or device: `vr_gfx` renders to memory
(`vr_gfx_read_pixels`) and audio advances only through `vr_audio_render`.

## Building for Vita

Install VitaSDK and set `VITASDK` to its root, then use a separate build directory:

```sh
cmake -S . -B build-vita -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
cmake --build build-vita
```

This builds `libvita_runtime.a`. Add `-DVR_BUILD_SAMPLES=ON` to also link the
`vr_platform_smoke` ELF, which exercises platform timing, logging, and the
filesystem and input backends. It also builds and runs on the host. The
`vr_gfx_spin` sample (`samples/gfx_spin.c`) draws a spinning textured square
over a pretransformed backdrop for about five seconds. See
[src/vita/README.md](src/vita/README.md) for behavior and remaining limitations.

## Using it from a port

Add it as a submodule and link the `vita_runtime` target:

```cmake
add_subdirectory(external/vita-runtime)
target_link_libraries(my_port PRIVATE vita_runtime)
```

Only include headers from `include/vr/`.

## Layout

```
include/vr/   Public C API (the only supported interface)
src/host/     Host backend (Linux/macOS, used for CI)
src/vita/     Vita backend (VitaSDK)
src/          Code shared between backends
tests/        Unit tests, run against the host backend
```

## Contributing with donated AI compute

This project accepts AI-assisted contributions through SpareCycles. See
[SPARECYCLES.md](SPARECYCLES.md).

## License

[MIT](LICENSE)
