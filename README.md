# Ryty

Automatic porting of PlayStation 5 and PlayStation 4 executables to native Linux, Windows,
and experimental macOS binaries.

Ryty includes a [relinker](core/relinker) that converts the executable to the
target system's native format and implementations of
[system PRX libraries](core/libs/prx) suitable for dynamic linking. No
emulation, no separate runtime process: guest code runs as host code.

Ryty is a fork of [AnyPS5](https://github.com/boykopovar/AnyPS5) - see
[authors](AUTHORS.md) and [acknowledgments](#acknowledgments).

## Highlights

* **Full AMD SHA extension lowering** - all seven SHA-NI instructions
  (SHA1RNDS4, SHA1NEXTE, SHA1MSG1/2, SHA256RNDS2, SHA256MSG1/2) are lowered
  to SSE2-only stubs, hardware-verified by a 360,360-assertion suite.
* **AMD-only instruction coverage** - RDPRU, MCOMMIT, RDPID (with REX.B),
  PREFETCHW, FEMMS, MONITORX/MWAITX, CLZERO, EXTRQ/INSERTQ and MOVNTSS/SD
  are lowered in place or through out-of-line stubs; short sites absorb
  neighbouring instructions.
* **PlayStation 4 and PlayStation 5 Support** - Auto-detection and explicit console selection (`--ps4`, `--ps5`, `--console auto|ps4|ps5`) to port both PS4 and PS5 executables.
* **Experimental macOS Target with Metal** - `--macos` target support with Objective-C / Objective-C++ Metal API graphics rendering bridge.
* **Qt6 GUI** (`ryty-gui`) on Linux, Windows, and macOS, shipped with the Qt6 runtime
  bundled (self-contained AppImage on Linux, windeployqt DLL set on Windows).
* **JSON porting report** (`--report`) with per-site substitutions, summary
  counters and warnings.
* **Hardened parsing** - `DT_STRSZ`-bounded strings, 8-byte GOT slot
  alignment checks, and a resilient level-1 NID filter that keeps every
  reference when CFG analysis cannot run.
* **Single-file CI** - one `CI.yml` builds, tests and packages both platforms
  and publishes releases for `v*` tags.

Unsupported or unexpected states strictly throw `std::runtime_error`;
`what()` is printed to stderr and the process terminates.

## Build

The relinker CLI uses only the C++20 standard library.

```sh
cmake --preset linux-release          # or windows-release in an MSYS2 UCRT64 shell
cmake --build build/linux-release --parallel
ctest --test-dir build/linux-release --output-on-failure
```

Qt6 is optional and enables the GUI; without it the build skips the GUI
gracefully (the `linux-release-cli` preset builds and tests exactly that
configuration). Full dependency and toolchain notes, including the Windows
MinGW requirement for the runtime libraries, are in
[docs/user/BUILDING.md](docs/user/BUILDING.md).

On Intel hosts, pass `--to-intel` to the relinker to lower supported
AMD-only instructions in the executable and bundled `sce_module`/`sce_modules`
PRX files. Unsupported instructions or stub jumps outside the x86-64
relative branch range produce an error.

## GUI

`ryty-gui` wraps the CLI: pick input/output, choose the target system, set
the porting options, watch the colored log and optionally emit the JSON
report. It launches the `ryty` binary from its own directory. Downloads from
CI ship the GUI with every Qt6 file it needs - see [ci/README.md](ci/README.md).

## Continuous integration

A single workflow (`.github/workflows/CI.yml`) builds and tests Linux and
Windows, packages the GUI with its runtime, and attaches both zips to GitHub
releases for `v*` tags. Details: [docs/dev/CI.md](docs/dev/CI.md).

## Documentation

* [Documentation index](docs/README.md)
* [Building](docs/user/BUILDING.md) and [usage](docs/user/USAGE.md)
* [Porting pipeline](docs/dev/PORTING_PIPELINE.md) and
  [instruction lowering](docs/dev/INSTRUCTION_LOWERING.md)
* [Conventions](docs/dev/CONVENTIONS.md) and
  [technical debt](docs/dev/TechnicalDebt.md)
* [Contributing](CONTRIBUTING.md), [security policy](SECURITY.md),
  [code of conduct](CODE_OF_CONDUCT.md), [changelog](CHANGELOG.md)

## Compatibility

See the [game compatibility list](docs/user/COMPATIBILITY.md) for tested
games and known issues.

## Input mapping

SDL-mapped game controllers are supported, including analog sticks and
triggers. Keyboard and mouse controls can be configured with a
`ryty-input.ini` file. See [input mapping](docs/user/INPUT_MAPPING.md) for
the supported devices and configuration format.

## Disclaimer

This project is intended for interoperability, research, preservation, and
compatibility purposes. It does not include, distribute, or require
copyrighted software, firmware, cryptographic keys, or proprietary libraries.
Users are responsible for ensuring that any binaries used with this project
are obtained and used in accordance with applicable laws and their respective
license terms.

## License

This project is licensed under the GNU General Public License version 2 only.

## Acknowledgments

Ryty builds on AnyPS5 by Andrii Semkiv (boykopovar) and its contributors:
the relinker architecture, the PRX system library implementations, the shader
recompiler and the test infrastructure originate from that project.
