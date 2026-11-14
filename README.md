# Ryty

Automatic porting of PlayStation 5 and PlayStation 4 executables to native Linux, Windows,
and experimental macOS and iOS binaries.

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
* **Verified Intel lowering** (2.1.0) - after `--to-intel` the ported image is
  re-scanned; any AMD-only instruction that survived lowering fails the port
  with its name and offset instead of producing a binary that traps. The CLI
  detects Intel hosts and points at `--to-intel` when it is missing.
* **PlayStation 4 and PlayStation 5 Support** - Auto-detection and explicit console selection (`--ps4`, `--ps5`, `--console auto|ps4|ps5`) to port both PS4 and PS5 executables.
* **Expanded macOS target with Metal** (2.0.0) - ad-hoc signed Mach-O with the Metal bridge, `--universal` fat binaries (native arm64 Rosetta launcher + x86_64 guest slice) for Apple Silicon, `--entitlements`/`--entitlements-template` presets, `--hardened-runtime`, `--macos-min`, `--identifier` and `--bundle` (.app with Info.plist).
* **Experimental iOS target** (2.0.0) - `--target ios` produces a signed arm64 guest container (16 KB alignment, iOS framework imports, embedded `__GUEST` payload, entitlements embedded in the arm64 signature), `--bundle`/`--ipa` package it as an installable `.app`/`.ipa` with a validated provisioning profile (`--provision`).
* **Experimental iOS Simulator target** (2.0.0) - `--target ios-sim` declares `LC_BUILD_VERSION` platform 7 so the container runs in Xcode's simulators; `.app` bundles carry `iPhoneSimulator` keys and need no provisioning, device-only flags are rejected up front.
* **Batch mode** (2.0.0) - `--batch <dir>` ports every ELF in a folder into per-file output folders with a summary table, per-file `ryty-port-report/2` reports and a `batch-report.json` summary.
* **Options files** (2.0.0) - `--config port.ini` stores porting options; unknown keys fail loudly. `--verify`, `--dry-run`, `--verbose` and `--version` round out the diagnostics toolset, and reports carry SHA-256 digests of input and output. Since 2.0.0, `--verify` also re-computes every page hash of the embedded ad-hoc code signature of Apple outputs and checks the `LC_BUILD_VERSION` platform/deployment target, `MH_PIE` on arm64 slices and the CodeDirectory identifier.
* **Qt6 GUI** (`ryty-gui`) on Linux, Windows, and macOS, shipped with the Qt6 runtime
  bundled (self-contained AppImage on Linux, windeployqt DLL set on Windows) - now with dark/light theme, drag & drop, live progress and persisted options (2.0.0).
* **Web-Based Porting Studio** (`demo/web`) - browser-based client-side ELF parser, auto console detection, NID inspector, section/hex viewers, ELF compare and JSON porting report generator; installable offline PWA since 2.0.0.
* **Hardened parsing** - `DT_STRSZ`-bounded strings, 8-byte GOT slot
  alignment checks, and a resilient level-1 NID filter that keeps every
  reference when CFG analysis cannot run.

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
PRX files. Since 2.1.0 the ported image is re-scanned and the port fails if any
AMD-only instruction survived, and the CLI warns when it runs on an Intel host
without `--to-intel`. Unsupported instructions or stub jumps outside the
x86-64 relative branch range produce an error.

New in 2.1.0 (Intel hosts):

```sh
ryty --to-intel --verbose in.elf out.elf   # lower AMD-only instructions and confirm none remain
```

New in 2.0.0:

```sh
ryty --version                    # print the release
ryty --batch ./games --windows    # port every ELF in ./games into ./games/ported
ryty --config port.ini in.bin out # read options from an INI file
ryty --dry-run --verify in.bin out.elf   # analyze without writing, then verify
```

New in 2.0.0 (Apple targets):

```sh
ryty --macos --universal --bundle in.bin game          # double-clickable Apple Silicon-ready .app
ryty --macos --macos-min 12.0 --entitlements dev.plist in.bin game
ryty --macos --bundle --entitlements-template macos-app-sandbox in.bin game
ryty --target ios --bundle --ipa --provision dev.mobileprovision in.bin game
ryty --target ios-sim --bundle in.bin game             # runs in Xcode's iOS Simulator
```

The 2.0.0 porting scheme: every target resolves to an explicit plan - output
format, architecture, loader entry, signature, layout and library path - which
the CLI prints (`Porting scheme: ...`), the GUI and web studio preview live,
and every JSON report records under `"scheme"`:

```sh
ryty --interp /lib/ld-musl-x86_64.so.1 --rpath "" --verify in.bin out.elf
                                     # musl loader, no DT_RUNPATH, NX stack
ryty --target macos --min-os 12.0 in.bin game
                                     # one deployment target for every Apple target
```

## GUI

`ryty-gui` wraps the CLI: pick input/output (or drag & drop them onto the
window), choose the target system, set the porting options, watch the live
progress and colored log, export the log, and optionally emit the JSON
report. A live scheme line previews the resolved porting plan (format,
architecture, loader entry, signature, layout) for the current options, and
the Apple deployment target (`--min-os`) and Linux ELF interpreter
(`--interp`) have dedicated fields. The dark/light theme, window layout and
every option persist across
sessions. Batch mode ports every ELF in a folder from the same window. It
launches the `ryty` binary from its own directory. Downloads from CI ship
the GUI with every Qt6 file it needs. 

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
