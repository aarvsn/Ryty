# Building Ryty

## Requirements

* A C++20 compiler: GCC 11+ (GCC 15.2 recommended on Windows, see below) or
  Clang 14+.
* CMake 3.21+ and Ninja.
* Python 3 (optional - enables the Python relinker tests).
* Qt 6.2+ with the Widgets module (optional - enables the `ryty-gui` target).
* Git submodules for the runtime libraries and shader recompiler
  (`3rdparty/SDL2`, `3rdparty/ffmpeg-core`, `3rdparty/freetype`,
  `3rdparty/stb`, `3rdparty/SPIRV-*`, `3rdparty/glslang`,
  `3rdparty/Vulkan-Headers`, `3rdparty/VulkanMemoryAllocator`,
  `3rdparty/LibAtrac9`).

The relinker CLI and its tests only need the C++20 standard library. When the
3rdparty submodules are missing, CMake skips the runtime libraries, the shader
recompiler and the media decoders with a status message instead of failing.

## Presets

| Preset              | Contents                                                       |
| ------------------- | -------------------------------------------------------------- |
| `linux-release`     | CLI + tests + GUI when Qt6 is present (Linux)                   |
| `linux-release-cli` | CLI + tests only, proves the no-Qt graceful skip (Linux)        |
| `windows-release`   | CLI + tests + GUI (Windows, MSYS2 UCRT64 shell)                 |

```sh
cmake --preset linux-release
cmake --build build/linux-release --parallel
ctest --test-dir build/linux-release --output-on-failure
```

Binaries land in `build/<preset>/` (`core/relinker/ryty`, `app/gui/ryty-gui`)
and test executables in `build/<preset>/tests/`.

## CMake options

| Option                    | Default | Effect                                              |
| ------------------------- | ------- | --------------------------------------------------- |
| `BUILD_TESTING`           | OFF     | Build and register the tests                        |
| `RTY_BUILD_GUI`           | ON      | Build `ryty-gui` when Qt6 is found                  |
| `RTY_ENABLE_SPIRV_TOOLS`  | OFF     | SPIR-V validation/optimization in the shader recompiler |
| `RTY_ENABLE_TIMING_LOG`   | OFF     | Frame timing logging in the runtime                 |

Release builds enable link-time optimization when the toolchain supports it.
On Linux the CLI links statically against libgcc/libstdc++; the GUI links
dynamically against Qt (it is bundled at packaging time, see [CI](../dev/CI.md)).

## Tests

`ctest` on the `linux-release` preset runs 16 tests: IO bounds, file writing,
the AMD64-only converter (decoding, operand models, golden stub bodies,
executed stub results), the SHA lowering verification suite (executes every
generated SHA stub and compares it against the hardware SHA-NI instruction -
360,360 assertions on a SHA-NI host; pure C reference models are used on
hosts without SHA-NI), the strict NID filter, and ten Python end-to-end
relinker tests. On Windows two additional ELF-patcher tests run.

## Windows toolchain note

The runtime libraries on Windows require MinGW-w64 GCC 15.2.0
(`winlibs-gcc15`, `x86_64-ucrt-posix-seh`); compiled PRX libraries additionally
need `libgcc_s_seh-1.dll`, `libstdc++-6.dll` and `libwinpthread-1.dll` next to
them. The relinker CLI itself builds with any C++20 toolchain.
