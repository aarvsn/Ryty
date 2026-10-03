# Instruction lowering

Intel hosts lack several AMD-only instructions. With `--to-intel`, the
converter replaces them so the ported executable runs on both vendors. Two
lowering kinds exist:

* **In place** - the instruction is rewritten to a same-length equivalent
  (nops, an opcode swap, or register zeroing).
* **Trampoline** - the site is overwritten by `jmp rel32` and execution
  continues in a generated out-of-line stub that reproduces the exact
  instruction semantics, restores every register except the architectural
  destination, and jumps back.

## Covered instructions

| Instruction | Encoding | Lowering | Strategy |
| ----------- | -------- | -------- | -------- |
| MONITORX | `0F 01 FA` | in place | replaced by nops |
| MWAITX | `0F 01 FB` | in place | replaced by `pause` |
| CLZERO | `0F 01 FC` | trampoline | cache-line aligned non-temporal stores |
| RDPRU | `0F 01 FD` | trampoline | returns `EDX:EAX = 0` (no MSR access on Intel) |
| MCOMMIT | `F3 0F 01 FA` | in place | replaced by a 4-byte nop |
| RDPID | `F3 0F C7 /7` (+REX.B) | in place | destination register zeroed |
| PREFETCHW | `0F 0D` | in place | opcode swap to `PREFETCHT0` (`0F 18`) |
| FEMMS | `0F 0E` | in place | opcode swap to `EMMS` (`0F 77`) |
| EXTRQ / INSERTQ | `66/F2 0F 78-79` | both | field extraction/insertion stubs |
| MOVNTSS / MOVNTSD | `F3/F2 0F 2B` | in place | opcode swap to the matching MOVNT store |
| SHA1NEXTE | `0F 38 C8` | trampoline | SSE2 SHA-1 next-state stub |
| SHA1MSG1 | `0F 38 C9` | trampoline | SSE2 schedule helper |
| SHA1MSG2 | `0F 38 CA` | trampoline | SSE2 schedule completion |
| SHA1RNDS4 | `0F 3A CC imm8` | trampoline | full 4-round SHA-1 round group (all four imm groups) |
| SHA256RNDS2 | `0F 38 CB` | trampoline | full 2-round SHA-256 round group (implicit XMM0 preserved as an input) |
| SHA256MSG1 | `0F 38 CC` | trampoline | SSE2 schedule helper |
| SHA256MSG2 | `0F 38 CD` | trampoline | SSE2 schedule completion |

All stubs use only SSE2 so they execute on any x86-64 host. Stubs spill and
restore every scratch XMM register; only the architectural destination
register changes. Stack usage goes below the 128-byte red zone, which the
stub reserves up front.

## Sequence absorption

A trampoline site needs 5 bytes for the jump. When the AMD-only instruction
is shorter, the converter absorbs following instructions: AMD-only
neighbours join the same stub (the lowering runs them back to back before
the return jump) and simple sequential instructions become trailing bytes
executed verbatim inside the stub. Instructions that cannot move (branches,
RIP-relative access, unsupported matches) abort the port instead of being
relocated.

## Verification

* `ShaLoweringVerificationTests` executes every generated stub with random
  operands (including the destination-equals-source aliasing case) and
  compares the result against the hardware SHA-NI instruction - 360,360
  assertions per run on a SHA-NI host. On hosts without SHA-NI the test
  falls back to pure C reference models derived from the same semantics.
* `Amd64OnlyConverterTests` pins operand decoding (prefixes, REX, memory
  forms), golden stub bodies, executed EXTRQ/INSERTQ/CLZERO/SHA-256 stub
  results, converter-level substitution classification and failure offsets.
* The semantics of the SHA-1 family were validated directly against hardware
  (Intel SDM pseudocode plus probing on a SHA-NI host); the C reference
  models in the tests mirror the validated behaviour, including the
  `W0E = SRC2[127:96]` pre-combined E-plus-W operand of `SHA1RNDS4`.

## Adding a lowering

1. Extend `DecodedInstruction` with a predicate that pins the encoding
   (prefixes, escape map, ModRM constraints).
2. Add a substitution-table entry (name only) and the lowering emitter
   (`StubBodyBuilder` emits prefixes, REX, opcodes, ModRM, RIP-relative
   constants, spills and restores).
3. Wire the match into `Amd64OnlyInstructionMatcher::Match` and, when the
   instruction can be moved, into `MatchSequence`.
4. Add tests: decode cases, a converter classification case, and - for
   arithmetic instructions - execution fuzzing against a reference.
