# Changelog

Releases before 1.0.7 predate this file; their history lives in the commit
log and the existing documentation.

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
