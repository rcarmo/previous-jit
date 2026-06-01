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

## 2026-05-24 Previous RAM/MMU checkpoint

The opcode harness remains a reliable green gate for the current curated vector set: the refreshed default baseline passes when split into bounded chunks under the 120s rule (`/workspace/tmp/previous-opcode-harness-20260601-124403`, `/workspace/tmp/previous-opcode-harness-20260601-124502`, `/workspace/tmp/previous-opcode-harness-20260601-124558`; combined `pass=75 fail=0 score=100`), while the unfiltered default run hit the cap at `/workspace/tmp/previous-opcode-harness-20260601-124112` and is not counted. The refreshed RAM-code MMU fast-smoke vector set also remains clean from `0x04008000` when split into bounded chunks (`/workspace/tmp/previous-opcode-harness-20260601-125250`, `/workspace/tmp/previous-opcode-harness-20260601-125405`; combined `pass=32 fail=0 score=100`), while the unchunked wrapper hit the cap at `/workspace/tmp/previous-mmu-fast-smoke-20260601-125024` and is not counted. Default/ROM JIT bootstrap is refreshed and clean (`/workspace/tmp/previous-jit-bootstrap-20260601-130216`, `bridge_compiled=1`, `bootstrap_ready=1`, `bootstrap_active=1`, `aslr_active=1`); the longer desktop smoke remains the historical `/workspace/tmp/previous-jit-bsr-metadata-default-20260526-132634` and was not rerun under the 120s rule.  Follow-up bounded RAM gates after the forced-fault oracle work are also clean: focused fault tuples (`/workspace/tmp/previous-opcode-harness-20260531-090328`, `pass=11 fail=0 score=100`) and non-fault seam/call vectors (`/workspace/tmp/previous-opcode-harness-20260531-090739`, `pass=12 fail=0 score=100`); a broader unfiltered non-fault RAM run hit the 120s cap at `/workspace/tmp/previous-opcode-harness-20260531-090522` and is not counted. RAM-requested mode now preserves native translated execution unless the explicit conservative oracle `B2_JIT_RTE_FAULT_HANDOFF=1` is requested; that oracle boots to a stable desktop in the latest long/no-DC run (`/workspace/tmp/previous-jit-bsr-metadata-ram-handoff-long-20260526-133132`). The current blocker is therefore narrower than generic opcode bring-up: native JIT resume after the RTE/page-fault seam. Commit `9441c84` aligned fallback BSR call-push transaction metadata, but native no-handoff still times out after RTE/low-PC churn.

Current RAM-mode lessons for the migration plan:

- Auto-update EA opcodes (`Aipi`/`Apdi`) are high-risk under 68040 MMU restart because native paths can update address registers before a helper longjmps out for a page fault. RAM dispatch therefore routes those forms through exact fallback barriers while restart/fixup handling is audited.
- `MOVES.* reg,(An)+` is no longer short-circuited by the RAM direct helper. It must use exact interpreter/helper semantics until SFC/DFC, restart, and exception-frame behavior are covered by a targeted regression.
- Return-family opcodes need transaction metadata when a return target fetch can fault after the stack has been popped. Fallback paths now publish return-pop transactions for `RTS`/`RTR`; keep adding producer-side metadata rather than bridge-side opcode guesses from stale low-virtual data views.
- `RTE` remains a hard seam: the native helper routes through the exact interpreter for opcode semantics, but bridge-caught RTE/page-fault seams no longer auto-handoff in RAM mode. `B2_JIT_RTE_FAULT_HANDOFF=1` is the explicit desktop-boot oracle; leaving it unset (or setting `B2_JIT_RTE_FAULT_HANDOFF_DISABLE=1`) preserves native resume for diagnosis until it is fixed.
- Bridge-delivered MMU restarts should use full `m68k_setpc()` materialization, not `m68k_setpci()`, when the next translated handoff depends on a coherent `regs.pc`/`pc_p`/`pc_oldp` tuple.
- Code-space MMU translation is required for RAM dispatch PC materialization and branch/return targets. Data-space translation is not sufficient for instruction fetch, but it is still required for ordinary data effective-address `xlateaddr` use.
- Keep the two paths explicit: `Uae2026JitMmuXlateCodeHost()` is for instruction/branch/return/dispatch host pointers; the private RAM/MMU bank `xlateaddr` remains data-space via `Uae2026JitMmuXlateData()`.
- Keep vendored compiler globals renamed away from Previous-native globals. In particular, Basilisk/UAE compiler prefs use `uae2026_currprefs`/`uae2026_changed_prefs`; Previous-native `currprefs`/`changed_prefs` have a different struct layout and must not share symbols.
- The earlier low-ROM probe frontier (`00003352`), the shifted-stack `00003964/A2=00000002` loop, the `init exited with 212` path, the panic-monitor `bad exception stack format` failure, the repeated `000000de` loop, and the high-user `050abffe`/`050069cc` frontiers are no longer the active blockers. After `9441c84`, fallback BSR call-push transactions pass producer-side decoded targets into the bridge instead of bridge-side extension decoding through transient `regs.pc_p`. Native no-handoff still does not reach the desktop; the latest long run reaches the familiar low-PC/RTE catch cluster (`00012052`, `00005030`, `0000a7a8`, `00004492`, `04001ae6`).
- If `RTE` has already switched from supervisor to user mode before an instruction-fetch/page fault, preserve the interpreter-updated post-pop `regs.isp`; do not overwrite it with the cached pre-RTE exception-frame SP except as a last-ditch fallback when `regs.isp` is missing.
- For native RAM/MMU bank and code-host helper calls, publish the current flushed JIT flag snapshot to `Uae2026JitLastFlags` before the helper can fault; the bridge restart path restores this snapshot when `mmu_restart` is set.
- A native low-virtual code-fetch guard must publish interpreter-like state both before a faultable opcode fetch (`mmu_opcode=0xffff`, `instruction_pc=fault_pc=pc`) and after a successful fetch (restart opcode equals the MMU-fetched opcode, not a stale compile-time literal) before it can become default RAM behavior.

Immediate harness gaps: add minimal RAM/MMU regressions for return-target MMU faults after `RTS`/`RTR`, for BSR target-fetch faults with producer-side transaction metadata, for RTE return-code fetch after SR/A7 switch, and for the current post-RTE low-PC resume sequence that still times out before desktop.
