# vita-runtime

An engine-agnostic runtime for bringing PC games to the PS Vita. It gives a
port one small C API for platform services, files, input, and (planned)
graphics and audio, with a Vita backend and a host backend for testing on a PC.

The project contains **no game-specific code or data**, and it never will.
See [CONTRIBUTING.md](CONTRIBUTING.md).

## Status

| Module                        | Host backend    | Vita backend |
| ----------------------------- | --------------- | ------------ |
| `vr_platform` (time, logging) | done            | planned      |
| `vr_fs` (read-only files)     | done            | planned      |
| `vr_input` (pad, touch)       | headless or SDL2 gamepad | planned      |
| `vr_gfx` (GXM)                | planned         | planned      |
| `vr_audio`                    | planned         | planned      |
| Direct3D 8/9 shim             | planned         | planned      |
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
