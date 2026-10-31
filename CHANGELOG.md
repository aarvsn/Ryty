# Changelog

Releases before 1.0.7 predate this file; their history lives in the commit
log and the existing documentation.

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
