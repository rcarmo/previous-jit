# Plan: migrate `compemu` away from giant generated C bodies toward inline ARM64 emission

## Problem

`src/cpu/uae_cpu_2026/compiler/compemu.cpp` is currently a transplanted, generated compiler body.
It has three structural problems in `Previous`:

1. it is hard to regenerate reproducibly for the exact bridge configuration
2. opcode coverage depends on old generator switches (`USE_JIT2`, table variants, legacy metadata)
3. debugging missing opcodes requires editing generated output instead of editing one obvious ARM64 path

## Goal

Move from “opaque generated compiler bodies + dispatch table glue” to a model where opcode lowering is
owned by a small set of explicit ARM64 helpers and inline emitters.

That does **not** mean writing every opcode as raw instruction words immediately. It means making the
bridge own the lowering logic instead of depending on a monolithic generated file.

## Target end state

### Keep

- `codegen_arm64.cpp/h` — raw AArch64 encoders
- `compemu_midfunc_arm64*.cpp` — reusable lowering helpers
- `jit_native_helpers.h` + C helpers for privileged/complex ops
- opcode metadata tables (`readcpu`, mnemonics, addressing metadata)

### Retire incrementally

- giant generated opcode bodies in `compiler/compemu.cpp`
- hand-maintained drift between `compemu.cpp` and `compstbl_arm.cpp`
- fragile generator-only feature gates such as `USE_JIT2` deciding runtime opcode coverage

## Migration stages

### Stage 0 — done first: deterministic harness

Use `docs/uae2026-opcode-harness.md` as the gate.

Exit criteria:

- opcode vectors run in interpreter mode without full boot
- JIT mode can be compared on the same vectors
- every new lowering change is exercised here before boot tests

### Stage 1 — classify opcode families

Split current missing/risky opcodes into three buckets:

#### A. pure inline-midfunc candidates

These should be emitted directly through `compemu_midfunc_arm64*` helpers:

- condition-code consumers/producers (`Scc`, `DBcc`, `Bcc` family)
- word memory shifts/rotates (`ASLW`, `ASRW`, `LSLW`, `LSRW`, `ROLW`, `RORW`, `ROXLW`, `ROXRW`)
- simple SR/CCR reads/writes where flag/state lowering is already available inline

#### B. call-helper candidates

These should keep a native block but flush/register-sync around a C helper:

- `DIVS/DIVU/DIVL/MULL`
- `PACK/UNPK`
- `MOVEC`, `MOVES`
- `BF*` family until an inline bitfield emitter exists

#### C. interpreter-only holdouts

Leave these for last unless they show up in traces:

- full FPU (`FPP`, `FBcc`, `FScc`) if MPFR precision/state makes inline lowering awkward
- rare supervisor/MMU forms not hit in current NeXT traces

### Stage 2 — introduce bridge-owned per-family emitters

Create explicit family emitters in the ARM64 support layer instead of adding more generated opcode bodies.

Suggested layout:

- `compiler/compemu_inline_arm64_control.cpp` — SR/CCR/condition family
- `compiler/compemu_inline_arm64_shift.cpp` — memory shift/rotate family
- `compiler/compemu_inline_arm64_muldiv.cpp` — mul/div family
- `compiler/compemu_inline_arm64_bitfield.cpp` — `BF*` family

Each emitter should:

1. decode only the bits it needs from the opcode/extension words
2. use existing midfuncs where possible
3. fall back to `call_helper()` for the tricky parts
4. be reachable from a small, readable dispatch registration layer

### Stage 3 — replace generated dispatch registration

Stop treating `compstbl_arm.cpp` as a giant generated artifact.

Instead:

- keep opcode metadata from `readcpu`
- register handlers from explicit family builders
- use small tables/macros only for repetitive opcode ranges

Example direction:

- `register_bcc_family(0x6000..0x6fff)`
- `register_scc_family(0x50c0..0x5ff9)`
- `register_shift_mem_family({...})`

This removes the current “missing in `compstbl`, present in `compemu`” class of bug entirely.

### Stage 4 — shrink generated `compemu.cpp`

Once a family is owned by inline emitters, remove its generated bodies from the build.

A practical transitional model is:

- keep generated `compemu.cpp` only for the still-unmigrated families
- route migrated families to bridge-owned emitters first
- compare generated vs inline output in the opcode harness until confidence is high

### Stage 5 — optional direct raw-encoder pass

Only after stages 0–4 are stable should we consider bypassing parts of the mid-layer and emitting raw
AArch64 more aggressively.

That should be reserved for:

- hot ALU/branch/shift families
- places where register allocator behaviour is already well understood
- cases where midfunc abstraction measurably blocks performance or correctness

## Testing gates

Every migration step should clear, in order:

1. `tools/uae2026-opcode-harness.sh`
2. `tools/headless-jit-bootstrap-probe.sh`
3. `tools/headless-jit-bridge-smoke.sh`
4. full NeXT boot / desktop harness

## Immediate next tranche

1. keep the new opcode harness as the inner loop
2. fix bridge stability so JIT mode reaches `REGDUMP`
3. move the VC/VS condition family first (`Bcc/Scc/DBcc`) because it is small and heavily represented
4. then move memory word shifts/rotates
5. finally do `DIV*`/`MULL` via helper-backed native blocks

That order gives the fastest path from “crashes on first JIT entry” to “opcode-family equivalence under harness”.

## 2026-05-15 Previous RAM/MMU checkpoint

The opcode harness remains a reliable green gate for the current curated vector set (`pass=62 fail=0 score=100`; latest `/workspace/tmp/previous-opcode-harness-pctrace-live-gate-20260515-154924`), and default/ROM JIT boot reaches the NEXTSTEP desktop with a 60s stability wait (`/workspace/tmp/previous-jit-pctrace-live-gate-default-20260515-155450`). The current blocker has moved out of generic opcode bring-up and into RAM/MMU exception/restart semantics around low-user-virtual instruction fetches and post-root low-user-virtual state restoration.

Current RAM-mode lessons for the migration plan:

- Auto-update EA opcodes (`Aipi`/`Apdi`) are high-risk under 68040 MMU restart because native paths can update address registers before a helper longjmps out for a page fault. RAM dispatch therefore routes those forms through exact fallback barriers while restart/fixup handling is audited.
- `MOVES.* reg,(An)+` is no longer short-circuited by the RAM direct helper. It must use exact interpreter/helper semantics until SFC/DFC, restart, and exception-frame behavior are covered by a targeted regression.
- Return-family opcodes, especially `RTE`, need hard barriers and post-fallback PC canonicalization because interpreter helpers may use `m68k_setpci()` and leave `pc_p` stale for JIT resumption.
- Bridge-delivered MMU restarts should use full `m68k_setpc()` materialization, not `m68k_setpci()`, when the next translated handoff depends on a coherent `regs.pc`/`pc_p`/`pc_oldp` tuple.
- Code-space MMU translation is required for RAM dispatch PC materialization and branch/return targets. Data-space translation is not sufficient for instruction fetch, but it is still required for ordinary data effective-address `xlateaddr` use.
- Keep the two paths explicit: `Uae2026JitMmuXlateCodeHost()` is for instruction/branch/return/dispatch host pointers; the private RAM/MMU bank `xlateaddr` remains data-space via `Uae2026JitMmuXlateData()`.
- Keep vendored compiler globals renamed away from Previous-native globals. In particular, Basilisk/UAE compiler prefs use `uae2026_currprefs`/`uae2026_changed_prefs`; Previous-native `currprefs`/`changed_prefs` have a different struct layout and must not share symbols.
- The earlier low-ROM probe frontier around `0x00003200..0x00003400` and `00003352`/`addr=00000008` is cleared by the low-virtual code-fetch fixes in plain RAM mode. The current frontier is later post-root state divergence around `00003964`: JIT reaches a live `MOVE.L (A2),-(A7)` with `A2=00000002`, while the interpreter oracle reaches the same stream with `A2=03ffffd8` after the `0000394a..0000395a` prologue/helper path. Keep `fault_pc`/`instruction_pc` distinct from `mmu_fault_addr`; rewriting one into the other produced worse/incorrect exception-frame behavior.
- If `RTE` has already switched from supervisor to user mode before an instruction-fetch/page fault, preserve the interpreter-updated post-pop `regs.isp`; do not overwrite it with the cached pre-RTE exception-frame SP except as a last-ditch fallback when `regs.isp` is missing.
- For native RAM/MMU bank and code-host helper calls, publish the current flushed JIT flag snapshot to `Uae2026JitLastFlags` before the helper can fault; the bridge restart path restores this snapshot when `mmu_restart` is set.
- The low-user-virtual ROM probe window (`0x00003200..0x00003400`) remains the strongest discriminator for the original JIT resume seam: interpreter handoff reaches desktop, low-virtual single-step reaches `root on sd@`, and the native prefetch guard reaches `root on sd@` without fetched/compiled opcode mismatches. Plain RAM now also reaches `root on sd@`; the next discriminator is the `0000394a..00003964` low-user-virtual helper/prologue path that corrupts or fails to restore A2 before a native data fault.
- A native low-virtual code-fetch guard must publish interpreter-like state both before a faultable opcode fetch (`mmu_opcode=0xffff`, `instruction_pc=fault_pc=pc`) and after a successful fetch (restart opcode equals the MMU-fetched opcode, not a stale compile-time literal) before it can become default RAM behavior.

Immediate harness gaps: add minimal RAM/MMU regressions for “auto-update EA fault -> MMU handler -> `RTE` to low user virtual PC”, for “low-virtual opcode fetch succeeds -> native data access faults with restart opcode state preserved”, and for the `0000394a..00003964` low-user-virtual call/prologue path where A2 must survive the helper call before `MOVE.L (A2),-(A7)` faults or continues.
