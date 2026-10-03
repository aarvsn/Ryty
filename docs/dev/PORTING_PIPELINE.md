# Porting pipeline

Ryty converts a PS5 executable into a native Linux or Windows executable
without emulation: guest code runs as host code, and PS5 system calls are
served by the bundled PRX library implementations. The pipeline:

1. **Read** - `ElfReader` parses the ELF header, program headers, sections
   and the dynamic segment. Bounds are validated against the file size;
   dynamic strings are bounded by `DT_STRSZ` and section-name strings by
   their string table size.
2. **Instruction scan** - the code segments (executable `PT_LOAD`s) are
   linearly decoded into instruction records. Undecodable bytes fail the
   port with the exact file offset.
3. **Intel conversion** (`--to-intel`) - AMD-only instructions are lowered:
   either rewritten in place (same length, e.g. `FEMMS` to `EMMS`) or
   replaced by a jump into a generated out-of-line stub (trampoline).
   Sites shorter than a relative jump absorb following safe instructions.
   Branch targets entering an AMD-only instruction abort the port.
4. **NID reference collection** - relocations (`R_X86_64_JUMP_SLOT`,
   `R_X86_64_GLOB_DAT`, `R_X86_64_64`) become NID references. GOT slots must
   be 8-byte aligned for the slot-based relocation types.
5. **Unused NID filtering** - level 1 keeps PLT references and filters
   non-PLT references through a CFG/GOT access analysis; when the analysis
   cannot run it keeps everything and warns. Level 2 additionally uses
   strict reachability (`EhFrameReader`, `StrictReachability`) and compacts
   the PLT.
6. **Dynamic section rebuild** - a new SysV dynamic section with the kept
   references, needed libraries and fresh `.rela`/`.rela.plt` content is
   built by `SysVDynamicSectionBuilder`.
7. **Guest modules** - `sce_module`/`sce_modules` PRX files are re-linked
   with the same Intel conversion when present (skippable).
8. **Patching** - `LinuxElfPatcher` or `WindowsElfPatcher` writes the final
   headers, entry stub and (for Windows) import tables, trampolines and TLS
   templates. Trampolines are placed within reachable relative-jump distance
   of their sites.

## Failure model

Every stage either completes exactly or throws `Domain::RelinkerException`
or `Codegen::CodegenException` with a human-readable reason and, when
relevant, the file offset. The CLI prints `FAIL: <reason> (offset 0x...)`
and exits with code 2.
