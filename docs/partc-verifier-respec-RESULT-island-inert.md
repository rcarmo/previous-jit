# PART C RESULT: verifier flag-layout fix does NOT reach window 4→0 (this slot)

Date 2026-06-19, ACTIVE slot (index 1). Built + ran the real-boot verifier A/B
gate for PART C. Outcome: the gate is **not met**, and the work surfaced two
deeper, independent problems that block any single-commit verifier patch. Working
tree reverted to clean HEAD `83c558d`. No code landed.

Validation env (all runs): default RAM/MMU boot to the kernel MVSR2 window,
`PREVIOUS_UAE2026_JIT=1 JIT_RAM=1 JIT_FPU=1 JIT_CACHE_KB=65536`,
`B2_JIT_RTE_FAULT_HANDOFF=0 B2_JIT_VERIFY_BLOCKS=0x0409ec00-0x0409ed00`.

## A/B #1 — the depth-guarded island (EDIT1′/2/3′) is INERT

Ran the 6-wrap depth-guarded `JitInterpFlagIsland` (exec_nostats, jit_op_stop,
verify interp loop, orsr/andsr/eorsr; execute_normal NOT wrapped) with
`B2_JIT_FLAG_ISLAND=1` vs `=0`:

```
island ON : ecbe/ec70/ece0/ec68 each {m=0 then m=1}  → 4 mismatch=1
island OFF: ecbe/ec70/ece0      each {m=0 then m=1}  → 3 mismatch=1 (ec68 sampled m=0)
mismatch detail IDENTICAL in both: reg[0] interp=2600 native=2604 (and 2700/2704)
chronology byte-identical (same log line numbers 23683/23685/23712/23717/23755/23761)
```

ON ≡ OFF, value-for-value. **The island has zero effect.**

### Root cause of inertness (structural, confirmed)
The bridge handles 68040 MMU faults with `setjmp`/`longjmp`
(`uae2026_jit_bridge.cpp:1316,1492`, `<csetjmp>`) and `TRY`/`ENDTRY`
(`src/cpu/newcpu.c:1327…`). `longjmp` does **not** run C++ destructors, so the
`~JitInterpFlagIsland()` in `exec_nostats` is skipped on every faulting fallback.
`jit_interp_flag_island_depth` leaks +1 per fault. Across the ~12-min RAM/MMU boot
(thousands of MMU faults) the counter is ≫0 long before the window, so every
constructor's `depth==0` guard is false → the conversion never runs. The
depth-guard RAII design is fundamentally unviable on the longjmp fault path.

This also re-confirms the (B) probe (`83c558d`): the island is a no-op in real
execution — not because the conversion would be harmless, but because it never
executes.

## A/B #2 — naive unconditional verifier-only conversion: trades regs for flags

Replaced the inert island with a single **unconditional** inline `nzcv→cznv`
conversion in the verify interp re-run loop only (longjmp-irrelevant; no real-path
wraps, no depth counter). Rationale, validated by the `JIT_EXACT_EXEC_NOSTATS`
markers: the **native** re-run reads correctly (D0=2604) because a compiled
block's prologue converts nzcv→cznv before any `exec_nostats` MV2SR fallback; the
verifier's direct `cpufunctbl` loop bypasses that prologue and mis-reads
nzcv-as-cznv (D0=2600).

Non-regression first: opcode harness 75/75 score=100
(`/workspace/tmp/previous-opcode-harness-20260619-074246`), MMU fast smoke 32/32
score=100 (`/workspace/tmp/previous-mmu-fast-smoke-20260619-074803`).

Verify window result (`/workspace/tmp/previous-partc-fix-verify-074243`):
```
17 mismatch=1 in window:
  13 × regs=0 ctrl=0 flags=1   ← D0 FIXED, but flags now mismatch
   4 × regs=1 ctrl=0 flags=1   ← D0 still wrong + flags mismatch
detail: interp nzcv=00004000 (cznv, Z@bit14)  native nzcv=40000000 (nzcv, Z@bit30)
```

The conversion fixed D0 (regs 1→0) in 13/17 — proving the diagnosis — but exposed
two new facts:

### Finding A — native re-run is ALWAYS nzcv; `actual_is_nzcv` is miscomputed
Every native snapshot in the window is nzcv (`0x40000000`), **including fallback
(`exec_nostats`) blocks** — the compiled epilogue restores nzcv after the
interpreter fallback. But the verifier sets
`actual_is_nzcv = !jit_block_verify_actual_exact_exec`, which is **false** for
fallback blocks, so the compare decodes a genuinely-nzcv snapshot with
`ccr_from_cznv` → spurious flags mismatch. Before the conversion, flags appeared
to "match" only because the interp side was *also* nzcv-misdecoded identically
(MV2SR/MVSR2 don't write flags, so the interp loop left the unconverted nzcv entry
untouched). The fix: native is always nzcv ⇒ compare must use `ccr_from_nzcv(actual)`
unconditionally (drop the `actual_is_nzcv` branch). The reverted island had this
half-right but wrongly used `ccr_from_nzcv(expected)` for the interp side too.

### Finding B — the verifier entry snapshot is NOT always nzcv (residual 4× regs=1)
The unconditional `nzcv→cznv` conversion left 4 residual `regs=1` (D0 still 2600).
An unconditional conversion can only leave D0 wrong if the entry was already cznv
for those occurrences — i.e. the execute_normal-captured entry boundary layout is
**entry-state-dependent**, sometimes nzcv and sometimes cznv at the same PC. This
contradicts the (B) probe's "entry always nzcv", which was measured at
`jit_op_mvsr2` (mid-block) — a different point than the verifier's block-entry
capture. The mixed entry layout is itself a **real-execution JIT↔interp boundary
seam** (candidate-C territory), not a verifier-only artifact.

## Conclusion / redirect

- PART C as specced (depth-guarded island) is dead: inert via longjmp depth leak.
- A verifier-local conversion is necessary but not sufficient. The correct, fully
  scoped fix is now two coupled changes plus one unresolved root-cause:
  1. **Compare:** native is always nzcv ⇒ `exp = ccr_from_cznv(interp)`,
     `act = ccr_from_nzcv(native)`, drop `actual_is_nzcv`. (Clears the 13 flags=1.)
  2. **Interp entry:** convert to cznv **only when the entry is nzcv** — which
     requires resolving Finding B: determine why/when the execute_normal entry
     boundary is cznv vs nzcv. This is the real-exec boundary seam and must be
     root-caused (likely the same conversion the compiled prologue does must be
     applied — and verified — at the execute_normal→block entry in normal
     execution, candidate C in `oracle-flag-layout-seam.md`).
- 0800 register-direct bitop guard (`a2ca746`) re-land stays **BLOCKED**: its gate
  is a clean oracle, which is not achieved.

## Artifacts
- island ON : `/workspace/tmp/previous-partc-islandON-071913/previous.log`
- island OFF: `/workspace/tmp/previous-partc-islandOFF-072934/previous.log`
- naive fix : `/workspace/tmp/previous-partc-fix-verify-074243/previous.log`
- smokes    : opcode `…-074246` (75/75), mmu-fast `…-074803` (32/32)
