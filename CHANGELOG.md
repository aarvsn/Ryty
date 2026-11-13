# Changelog

Releases before 1.0.7 predate this file; their history lives in the commit
log and the existing documentation.

## 2.1.0

This release focuses on Intel CPU support: the AMD-only instruction lowering
behind `--to-intel` is now verified end to end and the CLI guides Intel users
towards it.

### Verified Intel lowering

* **Residual verification**: after `--to-intel` conversion the ported image is
  re-scanned and any AMD-only instruction that survived lowering fails the port
  with its name and file offset (exit code 2) instead of producing a binary
  that traps on an Intel host. The scan runs on the finished Linux ELF, after
  the writer has placed the out-of-line stubs.
* **Conversion scanner API**: `Codegen::IAmd64OnlyConverter` gained
  `ScanResidual`, which lists every AMD-only instruction the converter
  recognizes in a set of code segments. `Convert` plus `ScanResidual` is the
  contract the CLI relies on: a converted image leaves no site behind.
* **Intel host guidance**: the CLI detects the host CPU vendor with CPUID
  (`Cli::HostCpuVendor` / `Cli::HostIsIntel`). On an Intel host without
  `--to-intel` it prints a warning and records one in the porting report.
* **Scheme and report**: the porting scheme and the `ryty-port-report/2` and
  `ryty-batch-report/1` reports carry the new `toIntel` field, and the
  one-line scheme description appends `Intel lowering (--to-intel)`.
* **Verbose confirmation**: `--to-intel --verbose` prints
  `Intel verification: no AMD-only instructions remain in the ported image`
  once the finished image passes the residual scan.

### Version

* Every component now reports `2.1.0`: the CLI (`--version`), the Qt6 GUI, the
  RytyNet bridge (`RYTNET_VERSION`, `User-Agent`), the Metal bridge capability
  value and the Apple `LC_SOURCE_VERSION` payload (`2.1.0.0`).

## 2.0.0

### Expanded macOS support

* **Universal binaries**: `--universal` (macOS) wraps the ported game in a
  fat (`0xCAFEBABE`) Mach-O with two slices: a native arm64 launcher and the
  x86_64 guest slice. On Apple Silicon the launcher re-execs the x86_64 slice
  through Rosetta 2 (`/usr/bin/arch -x86_64`, the same posix_spawn binary
  preference `arch(1)` uses) using only raw BSD syscalls - the arm64 slice
  links against nothing. When Rosetta is missing it prints a diagnostic that
  names the `softwareupdate --install-rosetta` command and exits 1.
* **Entitlements**: `--entitlements <file.plist>` embeds the plist as a
  `CSSLOT_ENTITLEMENTS` SuperBlob entry (slot type 5) and reserves special
  slot -5 with its SHA-256 digest, matching the layout `codesign -s -`
  produces.
* **Entitlements presets**: `--entitlements-template <preset>` embeds a
  built-in plist for the common porting cases - `macos-app-sandbox` (App
  Sandbox plus user-selected file and network grants), `macos-jit`
  (hardened-runtime JIT exceptions), `ios-device` (distribution style,
  `get-task-allow=false` plus the increased-memory-limit kernel entitlement)
  and `ios-debug` (development style, `get-task-allow=true`). An unknown name
  fails with the list of valid presets; the flag cannot be combined with
  `--entitlements`.
* **Hardened runtime**: `--hardened-runtime` sets the `CS_RUNTIME` flag
  (`0x10000`) in the CodeDirectory.
* **Configurable deployment target**: `--macos-min <major.minor[.patch]>`
  encodes the LC_BUILD_VERSION `MinOS` (default stays 11.0); the SDK field
  never drops below the minimum.
* **Custom identifier**: `--identifier <id>` sets the CodeDirectory
  identifier (default `ryty.bundle`).
* **.app bundles**: `--bundle` (macOS/iOS) writes `Name.app` with
  `Contents/MacOS/<Name>`, a generated `Info.plist` (CFBundleExecutable,
  CFBundleIdentifier, LSMinimumSystemVersion, NSHighResolutionCapable, ...),
  `PkgInfo` and the guest `libs/` inside the bundle so the default
  `@executable_path/libs` rpath works unmodified.
* **Signature-aware verification**: `--verify` now parses the embedded
  ad-hoc SuperBlob of Apple outputs and recomputes every 4096-byte page hash
  plus the entitlements special-slot hash, and additionally walks the load
  commands to check the `LC_BUILD_VERSION` platform and deployment target
  against the requested options, the `MH_PIE` flag on arm64 slices and the
  CodeDirectory identifier; universal binaries verify each slice. A single
  flipped byte fails the port with exit code 2.

### New iOS target (experimental)

* **`--target ios` / `--ios`**: produces a signed arm64 Mach-O guest
  container: `CPU_TYPE_ARM64`, `MH_PIE`, `LC_BUILD_VERSION` platform 2 with
  `--ios-min <ver>` (default 14.0), 16 KB segment alignment, unversioned iOS
  framework imports (Metal, MetalKit, UIKit, QuartzCore, libSystem) and the
  mandatory ad-hoc code signature. The ported x86-64 guest image rides in a
  read-only `__GUEST` segment; executing it on device requires the A64
  translation runtime, which ships separately.
* **Entitlements in the arm64 signature**: `--entitlements` and
  `--entitlements-template` plists are embedded into the arm64 container's
  SuperBlob exactly like the macOS writer does (previously the entitlements
  options only reached the x86_64 slice).
* **.ipa packaging**: `--ipa` (iOS device target) zips the `.app` bundle into
  `Name.ipa` (`Payload/Name.app/...`) with a dependency-free stored-format
  ZIP writer (`Io::ZipWriter`).
* **Provisioning profiles**: `--provision <file>` copies a `.mobileprovision`
  into the bundle as `embedded.mobileprovision`. The profile is validated
  before any output byte is written - it must be an XML plist or a CMS/DER
  container, and a plist profile whose `application-identifier` does not
  cover the bundle identifier prints a warning (a trailing `.*` wildcard
  covers prefixes). Device deployment still requires re-signing with
  `codesign`/`ldid` and a provisioning profile - the embedded ad-hoc
  signature covers simulator and sideloading preparation workflows.

### New iOS Simulator target (experimental)

* **`--target ios-sim` / `--ios-simulator`**: builds the same arm64 guest
  container but declares `LC_BUILD_VERSION` platform 7
  (`PLATFORM_IOSSIMULATOR`), which is what Xcode's simulators expect. The
  `--ios-min` deployment target applies, `MH_PIE` is enforced and the
  unversioned iOS framework imports are identical to the device target.
* **Provisioning-free bundles**: `--bundle` on the simulator writes an
  `Info.plist` with `CFBundleSupportedPlatforms = iPhoneSimulator` and no
  `PkgInfo`, so the `.app` drops straight into a simulator without any
  signing or profile work; the device-only options `--ipa` and `--provision`
  are rejected up front with a clear message.

### Unified porting scheme (all platforms)

* **Resolved scheme descriptions**: every port now follows a resolved,
  per-target scheme - output format, architecture, loader entry, signature,
  on-disk layout and library search path - built by a dedicated `PortScheme`
  layer shared by the CLI, the JSON reports, the batch runner, the GUI and the
  web studio, so all front ends describe a port the same way. The CLI prints
  `Porting scheme: ...` for every run and once per batch; every
  `ryty-port-report/2` and `ryty-batch-report/1` document records the plan
  under `"scheme"`.
* **Linux**: the loader path is configurable with `--interp <path>`
  (default `/lib64/ld-linux-x86-64.so.2`; musl hosts use
  `/lib/ld-musl-x86_64.so.1`), every port emits a non-executable
  `PT_GNU_STACK` declaration (NX stack), and `--rpath ""` now omits
  `DT_RUNPATH` from the executable and every guest module instead of
  baking an empty path. The deep `--verify` pass walks the program header
  table and validates the interpreter, `PT_DYNAMIC`, the run path and the
  NX stack.
* **Windows**: the deep `--verify` pass validates the COFF machine (x86-64),
  the PE32+ optional header, a non-zero entry point and the requested
  subsystem (GUI/console).
* **Apple**: `--min-os <ver>` sets one deployment target for whichever Apple
  target is selected (macOS or the iOS family); it cannot be combined with
  `--macos-min`/`--ios-min` and is rejected on non-Apple targets. Explicit
  `--identifier` values are validated up front (letters, digits, dots,
  hyphens), and the automatic `com.ryty.port.<name>` default is sanitized so
  awkward input file names can never produce an invalid bundle identifier.
  An explicit empty `--rpath ""` now also omits `LC_RPATH` from macOS images
  instead of falling back to `@executable_path/libs`.
* **GUI**: a live scheme line under the options previews the resolved plan
  (format, architecture, platform, signature, layout) as options change;
  every target is passed through the same `--target <name>` flag; the Apple
  deployment target and the Linux ELF interpreter have dedicated persisted
  fields.
* **Web studio**: a porting-scheme card in the summary tab, a live one-line
  scheme preview under the settings, per-target writer notes in the port log
  (`[linux]`, `[windows]`, `[apple]`), interpreter and run-path inputs, and
  the same `scheme` object in the generated JSON report.
* New config-file keys: `min-os`, `interp`; `rpath` now accepts the empty
  value.

### Command line interface

* **Batch mode**: `ryty --batch <dir> [--batch-out <dir>]` ports every ELF
  image in a folder into its own subfolder of the output directory, prints a
  per-file summary table (console, result, stubs, guest modules, elapsed time)
  and writes a `ryty-batch-report/1` JSON report plus per-file
  `ryty-port-report/2` reports. Non-ELF files and Ryty metadata files are
  skipped automatically; a failing file never stops the batch (exit code 1)
  and `--report` combined with `--batch` fails fast with a clear message.
* **Options files**: `--config <file>` applies INI-style options
  (`to-intel = true`, `unused-filter = 1`, `rpath = ...`, sections and
  comments allowed) at the position the flag appears, so later command line
  flags still win. Unknown keys or invalid values fail with the offending
  line number instead of being ignored.
* **`--version` / `-V`**: prints `ryty 2.0.0` and exits.
* **`--verify`**: re-reads the finished output from disk and validates its
  structure against the resolved scheme: ELF64 x86-64 header and program
  headers plus the Linux interpreter/dynamic/NX-stack checks, the PE32+
  machine/subsystem/entry checks, or the Mach-O header plus the Apple
  signature/build-version checks described above; failures abort the port
  with exit code 2.
* **`--dry-run`**: runs the complete pipeline without writing the executable,
  guest modules, registry or marker files; reports are still written and
  record `"dryRun": true`. Incompatible with `--autorun`.
* **`--verbose`**: prints the input digest (SHA-256), per-site lowering
  details, registry path, elapsed time and output size.
* `--target` accepts `linux`, `windows`, `macos`, `ios` and `ios-sim`; the
  target flags are mutually exclusive and validated up front, as are
  target/option combinations (`--universal` macOS only, `--ipa`/`--provision`
  iOS device only, `--bundle`/entitlements/hardened runtime Apple targets
  only, `--provision` requires `--bundle`, `--entitlements-template` conflicts
  with `--entitlements`).
* New config-file keys: `ios`, `ios-simulator` (alias `target-ios-sim`),
  `universal`, `bundle`, `ipa`, `hardened-runtime`, `macos-min`, `ios-min`,
  `identifier`, `entitlements`, `entitlements-template`, `provision`.
* `--autorun` rejects Apple targets (the output is not a host executable)
  instead of failing at launch time.
* The porting pipeline was factored into a reusable `PortOneFile` entry point
  shared by single-file and batch modes, so both paths behave identically.
* The JSON porting report moved to schema `ryty-port-report/2` and now
  carries `version`, `inputSha256`, `outputSha256`, `elapsedMs`, `dryRun`,
  `verified` and an `apple` section (`platform`, `universal`, `identifier`,
  `minimumOsVersion`, `entitlements`, `entitlementsTemplate`,
  `hardenedRuntime`, `bundle`, `ipa`) for Apple-target ports.

### Qt6 GUI (ryty-gui)

* **Dark / light theme** with a View menu toggle; the choice persists and
  `--dark` / `--light` command line options override it for one launch.
* **Drag & drop**: dropping an ELF file fills the input row (and suggests an
  output name); dropping a folder switches to batch mode automatically.
* **Live progress**: a progress bar driven by real CLI milestones, an elapsed
  time readout and a Cancel button that terminates the running port.
* **Batch mode checkbox**: the GUI passes `--batch` / `--batch-out` when
  enabled, so folder-wide porting works without the terminal.
* **Target system**: Linux, Windows, macOS, iOS and the new iOS Simulator
  entries; the Apple-only options enable automatically per target.
* **Apple options**: universal binary, `.app` bundle and hardened-runtime
  checkboxes plus an **entitlements preset** combo (None,
  `macos-app-sandbox`, `macos-jit`, `ios-device`, `ios-debug`); all
  persisted with the other options.
* **Log export** saves the colored porting log to a text file; window
  geometry, every option and the last paths persist across sessions via
  QSettings. Tooltips explain every option; Help > About shows the release
  notes.

### Web porting studio (demo/web)

* **Section header viewer** with decoded section types and per-row hex
  shortcuts.
* **Hex viewer**: canonical offset + hex + ASCII dumps of any segment,
  section or raw file offset, with a jump-to-offset field and selectable
  byte ranges.
* **Live NID filter**: substring or `/regex/` filtering with a match counter.
* **Compare tab**: diffs a second ELF against the loaded one - console
  target, entry point, needed libraries and the exact added/removed NID sets.
* **Apple target selector**: macOS, iOS and iOS Simulator entries plus an
  entitlements preset selector; Apple ports log the Mach-O
  `LC_BUILD_VERSION` platform and embed the chosen preset in the report's
  new `apple` object.
* **Offline PWA**: `manifest.webmanifest` and a precaching service worker
  (`sw.js`) make the studio installable and fully usable without network
  after the first visit; caches are versioned per release.
* **Dark / light theme** toggle persisted in `localStorage`, defaulting to
  the OS preference. Copy-report button (with selection fallback), honest
  metadata+binary export, DT_SYMTAB/DT_SYMENT-bounded symbol scanning (up to
  4096 entries), and an accessible dropzone (keyboard focus, ARIA label).

### Runtime components

* **macOS / iOS**: `LC_SOURCE_VERSION` reports 2.0.0.0
  (`0x0002000000000000ULL`).
* **libRytyNet**: bridge version 2.0.0 (`0x00020000u`); HTTP requests
  identify as `User-Agent: RytyNet/2.0.0 (experimental)`.
* **Metal bridge**: `RytyMetalGetDeviceCapabilities` reports 2.0.0
  (`0x00020000`), including the non-Apple test stub.

### Tests and infrastructure

* New C++ unit tests: Apple version encoding (LC_BUILD_VERSION and
  LC_SOURCE_VERSION packing), ad-hoc signature build/verify including
  tamper detection and entitlement special slots, arm64 launcher/iOS guest
  image structure, the iOS Simulator container (platform 7, entitlements in
  the arm64 SuperBlob) and the entitlements presets.
* New integration suite `apple_target_tests` (28 checks): universal slice
  verification, `--macos-min`/`--identifier`/`--entitlements`/
  `--entitlements-template`/`--hardened-runtime` fields, `.app` bundle
  layout and Info.plist content, iOS arm64 container with framework
  imports, iOS Simulator platform/PIE/provisioning-free bundles, `.ipa` ZIP
  contents with provisioning profile, provisioning validation (garbage,
  mismatch warning, wildcard coverage), config-file keys and all
  target/option validation errors.
* New unit/integration coverage: config file parsing (including unknown-key
  rejection), SHA-256 (FIPS 180-4 vectors plus incremental update), the new
  CLI flags, `--version`, report version/digests/verify, `--config` (apply +
  reject), `--dry-run` writes nothing, and batch mode (happy path with
  per-file reports and a failure path with exit code 1).
* The macOS signature suite and CLI integration tests track version 2.0.0.

## 1.1.2

### Versioning and Maintenance
* **Version Update**: Bumped project version to 1.1.2 across the relinker CLI, Qt6 GUI (`ryty-gui`), Metal API bridge (`core/libs/metal`), RytyNet online bridge (`libRytyNet`), web porting studio (`demo/web`), macOS `LC_SOURCE_VERSION` (`1.1.2.0`), and test suites.

## 1.1.1

### Bug Fixes and Coverage Patch
* **Windows PE Subsystem**: Default PE subsystem set to GUI mode (`IMAGE_SUBSYSTEM_WINDOWS_GUI`) to prevent console windows from launching when running games; added `--windows-console` flag for explicit console output.
* **libSceJpegEnc Subsampling**: Returns `SCE_JPEG_ERROR_UNSUPPORTED_SAMPLING` (`0x80650105`) for 4:2:2 and grayscale encoding requests, documenting `stb_image` limitations in `TechnicalDebt.md`.
* **libSceKeyboard Layout Handling**: `sceKeyboardGetKey2Char` returns `KEYBOARD_ERROR_INVALID_ARG` for Japanese 106-key layout (`KEYBOARD_ARRANGEMENT_106`) instead of throwing `std::runtime_error`.
* **libSceAudiodec PCM Word Size**: `validateWordSize` returns `SCE_AUDIODEC_ERROR_INVALID_WORD_LENGTH` (`0x807F0010`) for 24-bit PCM (`iBwPcm == 0`) instead of throwing.
* **libScePlayerInvitationDialog ABI**: Promoted to silent stubs list and added `NotImplemented_nid_no_patch` on `scePlayerInvitationDialogOpen`.
* **RytyNet TLS Handling**: `libSceHttp` and `libRytyNet` return `ERROR_SSL` (`0x80432021`) when `https://` is requested with RytyNet enabled, logging `[rytynet] HTTPS not supported`.
* **Version & Coverage**: Updated version string to 1.1.1 across CLI, Qt6 GUI, Metal bridge, RytyNet, web studio, macOS `LC_SOURCE_VERSION`, and unit tests. Updated `COMPATIBILITY.md`.

## 1.1.0

### Versioning and Infrastructure Cleanup
* Bumped project version to 1.1.0 across all components, including the relinker CLI, Qt6 GUI (`ryty-gui`), Metal API bridge (`core/libs/metal`), RytyNet online bridge (`libRytyNet`), web porting studio (`demo/web`), and macOS patcher `LC_SOURCE_VERSION` (`1.1.0.0`).
* Updated test assertions in C++ unit tests and Python signature validation suite for version 1.1.0 compatibility.
* Reviewed and updated repository documentation (`TechnicalDebt.md`, `GPU_SUPPORT.md`, `RYTNET.md`, `docs/README.md`) to reflect full AMD instruction lowering coverage and current architecture specifications.

## 1.0.9

### Relinker Hardening
* String parsing in `GuestImageReader.cpp` is strictly bounded by `strSize`, ensuring malformed offsets or missing NUL terminators cannot cause buffer reads past the dynamic string table.

### Web-Based Porting
* Added an interactive web-based porting application in `demo/web` inspired by AnyPS5 demo/web. Features client-side ELF header parsing, dynamic tag and NID analysis, auto console target detection (PS4 vs PS5), target architecture selection, instruction lowering controls, JSON report generation, and porting metadata export.

### Expanded macOS Support
* `MacOsElfPatcher` now sets `LC_SOURCE_VERSION` to 1.0.9.0 (`0x0001000000090000ULL`) and supports multi-RPATH runtime search paths. Extended Metal API bridging context in `RytyMetalRenderer`.

### Improved NVIDIA GPU Support
* Enhanced Vulkan shader recompiler device targeting for NVIDIA GPUs (`0x10DE`), incorporating Ampere/Ada/Hopper architecture optimizations, wave32 subgroup width pinning, and vendor pipeline state flags.

### Improved RytyNet Service
* Updated `libRytyNet` bridge to version 1.0.9 (`0x00010009u`). Enhanced HTTP header handling (`Authorization`, `Accept-Language`, custom headers), request timeouts, and option settings in `libSceHttp`. Added unit tests for bridge request and status operations.

## 1.0.7

### Increased macOS support

* Ports made with `--macos` now carry a real **ad-hoc code signature**: the
  writer embeds a `LC_CODE_SIGNATURE` SuperBlob with a CodeDirectory v0x20400
  whose SHA-256 hashes cover every 4096-byte page of the finished image
  (previously the blob helpers existed but were never attached, and unsigned
  Mach-O executables are killed by the kernel on launch). `LC_SOURCE_VERSION`
  reports 1.0.7.0.
* The signature structure and every page hash are validated on every platform
  by the new `macos_signature_tests` ctest (parses the SuperBlob and compares
  against an independent SHA-256).
* New `macos-release` CMake preset; SDL on Apple now configures the
  Cocoa/Metal video path (X11/Wayland off instead of the Linux defaults);
  the GUI runs `macdeployqt -codesign=-` when available so `ryty-gui.app`
  bundles the Qt frameworks with an ad-hoc signature.
* Guest `sce_module` PRXs stay runtime-loaded ELF objects by design; the
  rationale and the path to Mach-O modules are documented in the technical
  debt list.
* CI gained a **macos** job (macos-14: build, full ctest, CLI zip) alongside
  linux, windows and linux-cli.

### NVIDIA GPU support

* **Vendor-aware physical device selection**: candidates are scored (discrete
  over integrated over virtual) instead of taking the ICD's first match, with
  per-candidate logging. `RTY_GPU_VENDOR` and `RTY_GPU_INDEX` control mixed-GPU
  systems; out-of-range index requests fail with a clear message.
* **Subgroup sizes are pinned**: `VK_EXT_subgroup_size_control` (Vulkan 1.2
  core) is enabled when advertised, and every compute pipeline pins
  `requiredSubgroupSize` to the width the recompiler generated for. This makes
  the wave32/64 emulation deterministic on NVIDIA (always 32 lanes) and keeps
  variable-width schedulers honest elsewhere. `APS5_NO_SUBGROUP_PIN=1` opts
  out.
* **Per-vendor shader variants**: `SpirvTarget` now carries the PCI vendor id
  and the pinned subgroup configuration; the recompile cache key, the
  serialized request format (v6, backward compatible read) and the module
  preamble (`OpModuleProcessed` `ryty-target-vendor:...`) key and label every
  variant, so a cache directory can be shared across machines without a
  variant compiled for one vendor serving another.
* Vendor support matrix, requirements and controls are documented in
  [GPU support](docs/dev/GPU_SUPPORT.md).

### RytyNet (experimental online mode)

* New runtime module **libRytyNet** implementing the online-mode bridge:
  a blocking HTTP/1.1 client over the host's TCP stack (status line, headers,
  Content-Length and chunked responses), and the enablement policy.
* **libSceHttp** routes `sceHttpSendRequest`/`sceHttpReadData`/
  `sceHttpGetStatusCode`/`sceHttpGetAllResponseHeaders` through the bridge
  when enabled; request headers, URLs and bodies are captured from the
  existing create/add-header calls. Without the bridge the library keeps its
  previous offline behaviour byte for byte.
* **libSceNpManager** reports the user as signed in with the local identity
  `RytyPlayer` while the bridge is active (state, online id, NpId, account
  id/country/age/language, reachability), so titles that stall while signed
  out can reach their menus. No PSN service is behind it.
* Opt-in from three places: `RTY_NET_ENABLE=1` at runtime, `ryty --rytynet`
  at port time (writes an `<output>.rytynet` marker, records `"rytyNet"` in
  the JSON report, adds an experimental warning), and the GUI's
  "RytyNet online mode (experimental)" checkbox.
* Diagnostics: `RTY_NET_LOG=1` logging, `RytyNetIsEnabled`/`RytyNetVersion`/
  `RytyNetGetStatus`/`RytyNetSetEnabled` exports. HTTPS, HTTP/2, proxies,
  keep-alive and UDP P2P are explicitly not part of this release and are
  listed in [RytyNet](docs/user/RYTNET.md).

### GUI

* Version 1.0.7; new experimental RytyNet checkbox that passes `--rytynet`.

### Infrastructure

* `.github/workflows/CI.yml` recreated with a **macos** leg (build + full
  ctest + CLI zip) in addition to linux, windows and linux-cli, and release
  zips for all three desktop platforms.
* Documentation: new [RytyNet](docs/user/RYTNET.md) and
  [GPU support](docs/dev/GPU_SUPPORT.md) pages; BUILDING, USAGE,
  COMPATIBILITY, CI and the technical debt list updated.
