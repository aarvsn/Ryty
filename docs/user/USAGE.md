# Using the ryty CLI

## Basic usage

```
ryty [options] <input.elf> <output>
```

The input is a PS5 ELF executable (`eboot.bin` or a decrypted self). The
output is a native Linux ELF or, with `--windows`, a Windows PE executable.
Unsupported or unexpected states strictly fail with `FAIL: <reason>` and a
nonzero exit code; nothing is silently approximated.

## Options

| Option | Effect |
| ------ | ------ |
| `--windows` | Produce a Windows PE executable instead of a Linux ELF. |
| `--to-intel` | Lower AMD-only instructions in the executable and bundled PRX files (see [instruction lowering](../dev/INSTRUCTION_LOWERING.md)). Recommended on Intel hosts. |
| `unused-filter=0\|1\|2` | Unused NID filter level. `0` keeps every reference; `1` runs the CFG/GOT filter and is resilient - when the analysis cannot run, every reference is kept and a warning is printed; `2` runs the strict reachability filter and compacts the PLT. |
| `--skip-sce-module` | Do not process `sce_module`/`sce_modules` PRX files. |
| `--exclude-sce-module <file>` | Skip a single PRX file. Repeatable. Conflicts with `--skip-sce-module`. |
| `--skip-syscall-check` | Skip the syscall-presence validation of the input. |
| `--registry` | Write `<output>.registry.json` with every external reference and resolved call sites. |
| `--report <file>` | Write a JSON porting report (see below). |
| `--rpath <path>` | Library search path baked into the output (default `$ORIGIN/libs`). |
| `--lazy-binding` | Use lazy PLT binding. |
| `--windows-gui` | Mark the Windows output as a GUI subsystem binary (requires `--windows`). |
| `--windows-diagnostics` | Add Windows dependency diagnostics (requires `--windows`). |
| `--autorun` | Launch the ported executable after a successful port. |

## The JSON report

`--report <file>` writes a `ryty-port-report/1` document:

```json
{
  "schema": "ryty-port-report/1",
  "input": "eboot.bin",
  "output": "/abs/path/out.elf",
  "system": "linux",
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
`trampoline` for out-of-line stubs. Offsets are decimal file offsets.

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
