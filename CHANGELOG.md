# Changelog

All notable changes to Ryty are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.2] - 2026-10-04

### Added
- **Enhanced macOS & Metal Porting**: Expanded Mach-O bundle structure generation (`LC_LOAD_DYLIB`, `LC_RPATH`) in `MacOsElfPatcher` and enhanced Metal graphics bridge (`RytyMetalRenderer`, `RytyMetalView`) with pipeline state setup, render pass encoder configuration, viewport management, and device queries.
- **Porting Pipeline Enhancements**: Improved JSON report metadata, expanded CLI arguments, and improved guest module relocation handling.

### Fixed
- **Windows Qt6 Missing Files**: Fixed issue where Windows builds missed required Qt6 platform plugins (e.g. `platforms/qwindows.dll`) and deployment DLLs by passing target directory and deployment parameters to `windeployqt6` / `windeployqt` in CMake and CI workflow.

### Optimized
- **Buffer & Memory Allocation**: Fast-path vector reservation and capacity pre-allocations across I/O writers, ELF patchers, dependency stub builders, and instruction scanning routines.

## [1.0.1] - 2026-10-03

### Added
- **PlayStation 4 game support**: Auto-detection and explicit console selection (`--ps4`, `--ps5`, `--console auto|ps4|ps5`) in CLI and GUI to support porting PlayStation 4 executables.
- **Experimental macOS support**: Experimental target system (`--macos` / `--target macos`) with Mach-O binary emission (`MacOsElfPatcher`), Objective-C (`.m`) and Objective-C++ (`.mm`) runtime support, and Apple's Metal graphics renderer bridge (`RytyMetalRenderer`, `RytyMetalView`).
- **Target System in GUI**: Added "macOS (Mach-O / Metal, experimental)" to the Qt6 GUI launcher.

### Optimized
- **Instruction Scanner**: Fast-path prefix filtering for AMD64-only instruction scanning, skipping non-AMD instructions in bulk.
- **Memory allocations & lookups**: Pre-allocated container capacities and optimized lookup indexes in `ControlFlowGraph`, `GotAccessIndex`, `CacheKey`, and I/O buffer routines (`ByteReader`, `ByteWriter`).

## [1.0.0] - 2026-10-03

Ryty 1.0 is the first release of the fork of AnyPS5. It keeps the upstream
relinker architecture and PRX library set while adding the changes below.

### Added

- Qt6 GUI launcher (`app/gui`, target `ryty-gui`) for Linux and Windows with
  input/output pickers, target-system switch, every CLI option, live colored
  log and an optional JSON report toggle. Builds only when Qt6 is found and
  is skipped gracefully otherwise.
- Single-file CI (`.github/workflows/CI.yml`): Linux and Windows jobs plus a
  CLI-only job. The Linux job publishes a self-contained GUI AppImage with
  the Qt6 runtime embedded (linuxdeploy + linuxdeploy-plugin-qt); the Windows
  job ships the GUI with the full windeployqt6 DLL/plugin set.
- `--report <file>` flag: JSON porting report with options, per-site
  instruction substitutions, summary counters and warnings.
- Instruction-lowering coverage for the complete AMD SHA extension set:
  SHA1RNDS4 (all four imm8 round groups), SHA1NEXTE, SHA1MSG1, SHA1MSG2,
  SHA256RNDS2, SHA256MSG1 and SHA256MSG2, each lowered to SSE2-only stubs.
- Instruction-lowering coverage for RDPRU (zero-returning trampoline),
  MCOMMIT (nop), RDPID including REX.B (register zeroing), PREFETCHW
  (PREFETCHT0 rewrite) and FEMMS (EMMS rewrite).
- Short-site sequence absorption: in-place-lowerable instructions and RDPRU
  can be absorbed into a neighbouring stub so sites shorter than a relative
  jump still port.
- `ShaLoweringVerificationTests`: executes every generated SHA stub and
  compares it against the hardware SHA-NI instruction (360,360 assertions;
  falls back to pure C reference models on hosts without SHA-NI).
- CMake presets (`linux-release`, `linux-release-cli`, `windows-release`) and
  link-time optimization for release builds.
- Open-source project files: CI workflow, issue/PR templates, this
  changelog, code of conduct, security policy, author list, editor and
  formatting configuration.

### Changed

- Project rebranded from AnyPS5 to Ryty: CLI target `ryty`, `ryty-input.ini`,
  `ryty-entitlements.ini`, `RTY_*` environment variables and CMake options.
  Legacy `ANYPS5_*` environment variables keep working at runtime.
- Level-1 NID filtering is resilient: when CFG/GOT analysis cannot run
  (missing entry points, analysis failure), the filter keeps every reference
  and warns instead of aborting the port.
- Dynamic string reads are bounded: names are limited to `DT_STRSZ`
  (dynamic strings) and to the section size (section header string table).
- GOT slots for GLOB_DAT/JUMP_SLOT relocations must be 8-byte aligned;
  misaligned slots fail the port with a precise offset instead of producing
  a broken binary.

### Fixed

- SHA opcode classification: SHA1 and SHA256 message/round opcodes
  (`0F 38 C8`..`0F 38 CD`, `0F 3A CC`) are decoded by escape map and
  operation; previously SHA-1 family instructions were not lowered and a
  memory-operand or prefixed variant could abort the port.
