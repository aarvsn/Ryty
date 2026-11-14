# Using the ryty CLI

## Basic usage

```
ryty [options] <input.elf> <output>
```

The input is a PS5 or PS4 ELF executable (`eboot.bin` or a decrypted self). The
output is a native Linux ELF, a Windows PE executable with `--windows`, an experimental macOS Mach-O executable with `--macos`, an experimental iOS arm64 guest container with `--target ios`, or the same iOS container for the iOS Simulator with `--target ios-sim`.
Unsupported or unexpected states strictly fail with `FAIL: <reason>` and a
nonzero exit code; nothing is silently approximated.

## The porting scheme

Every port follows a resolved, per-target scheme: the output format and
architecture, the loader entry, the signature, the on-disk layout and the
library search path. The CLI prints the resolved scheme for every run (`Porting
scheme: ...`, also once per batch run), the GUI shows it live under the options,
the web studio renders it as a scheme card, and every JSON port report and
batch report records it under `"scheme"`. All front ends therefore describe a
port the same way.

| Target | Format | Loader entry | Signature | Layout |
| ------ | ------ | ------------ | --------- | ------ |
| `linux` | SysV ELF64 (x86-64) | `PT_INTERP` loader (`--interp`) + `PT_GNU_STACK` NX | none | flat |
| `windows` | PE32+ (x86-64) | PE entry stub, GUI/console subsystem | unsigned | flat |
| `macos` | Mach-O 64 (x86_64; optional arm64 launcher) | dyld, `LC_BUILD_VERSION` platform 1 | ad-hoc CodeDirectory | flat or `.app` |
| `ios` | Mach-O 64 (arm64) | dyld, `LC_BUILD_VERSION` platform 2 | ad-hoc CodeDirectory | `.app` / `.ipa` |
| `ios-sim` | Mach-O 64 (arm64) | dyld, `LC_BUILD_VERSION` platform 7 | ad-hoc CodeDirectory | `.app` |

The scheme is also enforced by `--verify`: the ELF pass walks the program
headers and checks the interpreter, the dynamic section, the run path and the
non-executable stack; the PE pass validates machine, PE32+ optional header,
entry point and subsystem; the Mach-O pass re-computes the ad-hoc signature and
checks `LC_BUILD_VERSION` (see `--verify` below).

## Options

| Option | Effect |
| ------ | ------ |
| `--windows` | Produce a Windows PE executable instead of a Linux ELF. |
| `--macos` | Produce an experimental macOS Mach-O executable with Metal graphics API support. The output carries an ad-hoc code signature (SHA-256 CodeDirectory) so it launches without `codesign`. |
| `--ios`, `--target ios` | Produce an experimental iOS arm64 guest container (see below). |
| `--ios-simulator`, `--target ios-sim` | Produce the same arm64 guest container for the iOS Simulator: `LC_BUILD_VERSION` platform 7, provisioning-free `.app` bundles, device-only options rejected. |
| `--target <linux\|windows\|macos\|ios\|ios-sim>` | Target system selector; equivalent to the individual flags. |
| `--version`, `-V` | Print `ryty 2.1.0` and exit. |
| `--config <file>` | Apply INI-style options from a file before the remaining command line flags (new in 2.0.0, see below). |
| `--batch <dir>` | Batch mode: port every ELF image in `<dir>` (new in 2.0.0, see below). |
| `--batch-out <dir>` | Output directory for `--batch` (default `<batch dir>/ported`). |
| `--verify` | After writing, re-read the output and validate its structure; a failed verification aborts with exit code 2 (new in 2.0.0). Linux outputs verify the ELF header, the program header table, the requested `PT_INTERP` loader path (`--interp`), `PT_DYNAMIC` with its `DT_RUNPATH`, and the non-executable `PT_GNU_STACK` declaration. Windows outputs verify the COFF machine (x86-64), the PE32+ optional header, a non-zero entry point and the requested subsystem (GUI/console). For macOS/iOS outputs this also re-computes every page hash of the embedded ad-hoc code signature, checks the `LC_BUILD_VERSION` platform and deployment target against the requested options, the `MH_PIE` flag on arm64 slices and the CodeDirectory identifier; universal binaries verify each slice. |
| `--dry-run` | Run the complete pipeline but write no executable, guest modules, registry or marker; reports are still written and record `"dryRun": true` (new in 2.0.0). |
| `--verbose` | Print per-site lowering details, the input SHA-256 digest, timings and output size (new in 2.0.0). |
| `--rytynet` | Experimental: enable the RytyNet online-mode bridge for this port. Writes an `<output>.rytynet` marker, records `"rytyNet": true` in the report and adds an experimental warning. See [RytyNet](RYTNET.md). |
| `--ps4` | Treat input executable as a PlayStation 4 game binary. |
| `--ps5` | Treat input executable as a PlayStation 5 game binary. |
| `--console <auto\|ps4\|ps5>` | Console platform selection (default `auto`). |
| `--to-intel` | Lower AMD-only instructions in the executable and bundled PRX files (see [instruction lowering](../dev/INSTRUCTION_LOWERING.md)). Recommended on Intel hosts: the ported image is re-scanned afterwards and the port fails if any AMD-only instruction survived. The CLI warns when it runs on an Intel host without this flag. |
| `unused-filter=0\|1\|2` | Unused NID filter level. `0` keeps every reference; `1` runs the CFG/GOT filter and is resilient - when the analysis cannot run, every reference is kept and a warning is printed; `2` runs the strict reachability filter and compacts the PLT. |
| `--skip-sce-module` | Do not process `sce_module`/`sce_modules` PRX files. |
| `--exclude-sce-module <file>` | Skip a single PRX file. Repeatable. Conflicts with `--skip-sce-module`. |
| `--skip-syscall-check` | Skip the syscall-presence validation of the input. |
| `--registry` | Write `<output>.registry.json` with every external reference and resolved call sites. |
| `--report <file>` | Write a JSON porting report (see below). Cannot be combined with `--batch` (per-file reports are written automatically). |
| `--rpath <path>` | Library search path baked into the output (default `$ORIGIN/libs`). Pass `--rpath ""` to omit `DT_RUNPATH` / `LC_RPATH` from the executable and every guest module (new in 2.0.0). |
| `--interp <path>` | Linux target: dynamic loader path baked into `PT_INTERP` (default `/lib64/ld-linux-x86-64.so.2`; musl hosts use `/lib/ld-musl-x86_64.so.1`). Rejected on the other targets (new in 2.0.0). |
| `--lazy-binding` | Use lazy PLT binding. |
| `--windows-gui` | Mark the Windows output as a GUI subsystem binary (requires `--windows`). Default behavior since v1.1.1. |
| `--windows-console` | Mark the Windows output as a Console subsystem binary (requires `--windows`). |
| `--windows-diagnostics` | Add Windows dependency diagnostics (requires `--windows`). |
| `--autorun` | Launch the ported executable after a successful port. Incompatible with `--dry-run` and with the Apple targets (`--macos`/`--ios`, new in 2.0.0). |

## Apple targets

### Expanded macOS support

| Option | Effect |
| ------ | ------ |
| `--universal` | Wrap the macOS port in a fat (universal) Mach-O: a native arm64 launcher slice plus the x86_64 guest slice. On Apple Silicon the launcher re-execs the x86_64 slice through Rosetta 2 and prints `softwareupdate --install-rosetta` guidance when it is missing. Requires `--macos`. |
| `--macos-min <ver>` | LC_BUILD_VERSION deployment target, e.g. `12.0` (default `11.0`). The SDK field never drops below the minimum. Requires `--macos`. |
| `--min-os <ver>` | One deployment target for whichever Apple target is selected (macOS or the iOS family). Cannot be combined with `--macos-min` / `--ios-min`; rejected on non-Apple targets (new in 2.0.0). |
| `--identifier <id>` | CodeDirectory / bundle identifier (default `ryty.bundle`, or `com.ryty.port.<name>` with `--bundle`; the automatic default is sanitized to valid identifier characters). Explicit identifiers are validated up front: letters, digits, dots and hyphens only. |
| `--entitlements <plist>` | Embed the plist file as the `CSSLOT_ENTITLEMENTS` signature slot; its SHA-256 lands in special slot -5, exactly like `codesign -s -`. Requires an Apple target. |
| `--entitlements-template <preset>` | Embed a built-in entitlements preset instead of a file: `macos-app-sandbox` (App Sandbox + files/network grants), `macos-jit` (hardened-runtime JIT exceptions), `ios-device` (distribution style, `get-task-allow=false` + increased memory) or `ios-debug` (development style, `get-task-allow=true`). Cannot be combined with `--entitlements`. Requires an Apple target. |
| `--hardened-runtime` | Set the `CS_RUNTIME` flag in the CodeDirectory. Requires an Apple target. |
| `--bundle` | Write `Name.app` (`Contents/MacOS/<Name>`, generated `Info.plist`, `PkgInfo`) with the guest `libs/` inside the bundle so `@executable_path/libs` keeps working. Requires an Apple target. |

```sh
ryty --macos --universal --bundle in.bin game
ryty --macos --macos-min 12.0 --entitlements game.entitlements --hardened-runtime in.bin game
ryty --macos --bundle --entitlements-template macos-app-sandbox in.bin game
```

### Experimental iOS target

`--target ios` produces a signed arm64 Mach-O guest container: platform 2 in
LC_BUILD_VERSION (`--ios-min`, default `14.0`), 16 KB segment alignment,
unversioned iOS framework imports (Metal, MetalKit, UIKit, QuartzCore,
libSystem), `MH_PIE` and the mandatory ad-hoc code signature - which, like the
macOS writer, embeds `--entitlements`/`--entitlements-template` plists as the
`CSSLOT_ENTITLEMENTS` slot. The ported x86-64 guest image is embedded in a
read-only `__GUEST` segment; running it on a device requires the A64
translation runtime, which ships separately.

| Option | Effect |
| ------ | ------ |
| `--ios-min <ver>` | iOS deployment target for LC_BUILD_VERSION (default `14.0`). Requires an iOS-family target. |
| `--bundle` | Write `Name.app` at the bundle root with an iOS `Info.plist` (`MinimumOSVersion`, `UIDeviceFamily`, `UILaunchScreen`, ...). |
| `--ipa` | Zip the bundle into `Name.ipa` (`Payload/Name.app/...`). Implies `--bundle`. Requires the iOS device target. |
| `--provision <file>` | Copy a `.mobileprovision` into the bundle as `embedded.mobileprovision`. The profile is validated before anything is written: it must be a plist or a CMS/DER container, and a plist profile whose `application-identifier` does not cover the bundle identifier prints a warning (a `.*` wildcard covers prefixes). Requires `--bundle` and the iOS device target. |

```sh
ryty --target ios --bundle --ipa --provision dev.mobileprovision in.bin game
ryty --target ios --bundle --entitlements-template ios-device in.bin game
```

Device deployment still requires re-signing with `codesign`/`ldid` and a
provisioning profile; the embedded ad-hoc signature covers simulator and
sideloading preparation workflows.

### Experimental iOS Simulator target

`--target ios-sim` (alias `--ios-simulator`) builds the same arm64 guest
container, but declares `LC_BUILD_VERSION` platform 7
(`PLATFORM_IOSSIMULATOR`), which is what Xcode's simulators expect. Bundles
get an `Info.plist` with `CFBundleSupportedPlatforms = iPhoneSimulator` and no
`PkgInfo`; device-only options (`--ipa`, `--provision`) are rejected up front.
The 16 KB alignment, framework imports, entitlements embedding and ad-hoc
signature behave exactly like the iOS device target.

```sh
ryty --target ios-sim --bundle in.bin game
```

The reports of Apple-target ports carry an
`apple` object with `platform`, `universal`, `identifier`, `minimumOsVersion`,
`entitlements` (+ `entitlementsTemplate` when a preset was used),
`hardenedRuntime`, `bundle` and `ipa`.

## Options files (`--config`)

`--config <file>` reads INI-style `key = value` lines (comments with `#` or
`;`, optional `[sections]`, bare keys mean `true`). Keys use the long flag
names without dashes - underscores and uppercase are accepted and normalized:

```ini
# port.ini
target = windows
ps5 = true
to-intel = true
unused-filter = 1
rpath = $ORIGIN/libs
report = out/report.json

[extras]
exclude-sce-module = libOld.prx
```

The file is applied at the position `--config` appears: options parsed later
(from the file or the command line) override earlier ones, so an explicit flag
after `--config` always wins. Unknown keys or invalid values fail with the
file name and line number.

## Batch mode (`--batch`)

```
ryty --batch <input-dir> [--batch-out <output-dir>] [options]
```

Ports every ELF image found in the input directory (top level, non-recursive;
Ryty's own `.report.json` / `.registry.json` / `.rytynet` metadata files are
skipped). Each image lands in its own subfolder of the output directory
(default `<input-dir>/ported`) next to a per-file
`<name>.report.json`, and a `batch-report.json` summary is written:

```
== Batch summary ======================================================
  File          Console  Result       Stubs     Guests   Time (ms)
  ------------  -------  -------  ---------  ---------  ----------
  eboot.bin     ps5      OK               0          0         0.2
  dlc.elf       ps4      OK               2          0         0.4
2 ported, 0 failed, 2 total
```

A failing file never stops the batch: the run exits `0` when everything
ported and `1` when at least one file failed; missing directory or unusable
output directory exit `2`.

## The JSON report

`--report <file>` writes a `ryty-port-report/2` document (batch mode writes
the same schema per file plus a `ryty-batch-report/1` summary):

```json
{
  "schema": "ryty-port-report/2",
  "tool": "ryty",
  "version": "2.1.0",
  "scheme": {
    "system": "linux",
    "format": "SysV ELF64",
    "architecture": "x86-64",
    "signature": "none",
    "layout": "flat",
    "interpreter": "/lib64/ld-linux-x86-64.so.2",
    "runPath": "$ORIGIN/libs",
    "universal": false,
    "nxStack": true,
    "lazyBinding": false,
    "toIntel": true
  },
  "input": "eboot.bin",
  "output": "/abs/path/out.elf",
  "system": "linux",
  "dryRun": false,
  "verified": true,
  "inputSha256": "...",
  "outputSha256": "...",
  "elapsedMs": 1.4,
  "options": { "toIntel": true, "unusedFilterLevel": 1, "...": "..." },
  "intelSubstitutions": [
    { "instruction": "SHA256RNDS2", "offset": 8196, "originalLength": 8,
      "replacementLength": 431, "lowering": "trampoline" }
  ],
  "summary": {
    "inPlaceReplacements": 0,
    "trampolines": 1,
    "guestModules": 0,
    "externalReferences": 0
  },
  "guestModules": [],
  "warnings": []
}
```

`lowering` is `in-place` for same-length rewrites (nops, opcode swaps) and
`trampoline` for out-of-line stubs. Offsets are decimal file offsets. The
SHA-256 digests cover the untouched input image and the finished output file
respectively; `verified` reflects `--verify`.

## Expected runtime layout

The ported executable expects this layout next to it:

```
<game>/
    <ported executable>
    libs/
        *.prx          (sce_module/sce_modules PRX files)
    app0/
        <game resources>
```

## Environment variables (ported games)

Runtime configuration uses `RTY_*` variables; the legacy `ANYPS5_*` names
still work so that previously configured setups keep running:

| Variable | Purpose |
| -------- | ------- |
| `RTY_INPUT_CONFIG` | Path to the input mapping ini (default `ryty-input.ini` next to the executable). |
| `RTY_SHADER_CACHE_DIR`, `RTY_NO_SHADER_CACHE` | Shader disk cache location / disable switch. |
| `RTY_ENTITLEMENTS` | Entitlement data ini override. |
| `RTY_FMOD_SILENT`, `RTY_FMOD_NOSOUND` | Audio fallback switches used by the FMOD compatibility layer. |
| `RTY_NET_ENABLE=1` | Experimental: activate the RytyNet online-mode bridge (see [RytyNet](RYTNET.md)). `0` forces it off. |
| `RTY_NET_LOG=1` | Log RytyNet decisions and requests to stderr (`[rytynet]` lines). |
| `RTY_GPU_VENDOR`, `RTY_GPU_INDEX` | GPU selection overrides for mixed-GPU systems (see [GPU support](../dev/GPU_SUPPORT.md)). |
