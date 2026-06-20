# Gap-fill spec: Bcc-family native continuation (the real zero-fallback boot lever)

Status: SPEC (ready to implement + verify on next CPU slot). Static prep.
HEAD anchor: `ec26050` (after 0800 immediate-bitop barrier narrowing).

## Why this is the lever (evidence)

After `ec26050` narrowed the 0800 barrier, a narrow-oracle sweep of
`0x0409f500-0x0409f600` showed **every** block still falls back, all rebinding to
control-flow/stack barriers. Binding-barrier frequency in that window:

```
3860  op=6706  BEQ.S    \  Bcc family  = dominant
1930  op=62b8  BHI.S    /
1930  op=4e56  LINK
1929  op=4e75  RTS
1929  op=4e5e  UNLK
```

`0409f520` (the ex-0800 block) now binds on `op=6706`. The Bcc family
(`(opcode & 0xf000)==0x6000 && opcode!=0x6000`) is the single largest binding
barrier — unlike 0800, freeing it should produce a **real block-count drop**.

## Root cause (structural, not absent/miswired codegen)

Three facts establish the cause:

1. **Native Bcc codegen EXISTS and is correct.** `op_6xxx_comp_ff` in
   `compemu.cpp` (e.g. `op_6200_0_comp_ff` ~29672) uses the standard UAE
   deferred-branch mechanism: `register_branch(not_taken, taken, native_cc)` +
   `make_flags_live()`, registered in `compstbl_arm.cpp` (lines ~1165+). The cc
   constants are the **native-CC enum remap** (BHI→7, LS→6, CC→3, CS→2, NE→5,
   EQ→4, GE→13, LT→12), verified consistent — NOT a wrong-cc bug.

2. **The trace builder already has correct branch-terminator logic.**
   `compemu_legacy_arm64_compat.cpp:1565-1578`: when `end_block(opcode)` is true
   it follows *forward* short branches (target>cur, <512B, blocklen<32) to keep
   straight-line code together, and forces `must_end` on *backward* branches
   ("let block chaining handle the backward edge" — DBRA/DBcc loop-unroll-safe).
   `end_block(opcode)` = `prop[opcode].cflow & fl_end_block`, which is **set for
   Bcc**.

3. **But `current_is_bcc` is also a `trace_barrier_op` (legacy:1538/1541), and
   the barrier check runs BEFORE `end_block`.** So a Bcc hits
   `if (trace_barrier_op) return;` (whole block → interpreter) and **never
   reaches** the correct `end_block` terminator path. The barrier is a
   belt-and-suspenders over-correction that disables ALL Bcc compilation.

The barrier comment ("Bcc/DBcc can stop early or fall through into extension
words after a few compiled iterations") describes a **forward-unroll** artifact —
exactly the `continue`/follow path at 1577, not the pure-terminator path.

## Ranked hazards

| # | hazard | status under this fix |
|---|--------|----------------------|
| H1 | Bcc barrier pre-empts `end_block` terminator → all Bcc blocks interpreter-only | **FIXED** (highest impact) |
| H2 | forward-follow unroll "fall through extension words after a few iterations" | **AVOIDED** — force `must_end` (pure terminator, no unroll); strictly safer than native `end_block` follow |
| H3 | backward loop / DBRA unroll executing fixed count | already handled (block ends at branch); **DBcc stays a barrier** |
| H4 | flag-liveness across the trace | low — `make_flags_live()` at the branch; flags are live at a terminator |

## Fix (minimal, root-caused — pure terminator, no unroll)

File: `src/cpu/uae_cpu_2026/compiler/compemu_legacy_arm64_compat.cpp`

1. Remove `current_is_bcc` from the `trace_barrier_op` OR-chain (line ~1541):

```c
const bool trace_barrier_op = current_is_dbcc || current_is_stack_pop_move ||
    current_is_stack_push_pea || current_is_return || current_is_link_unlk ||
    current_is_immediate_bitop || current_is_ethernet_reset_island ||
    current_is_jsr_jmp;
```

(keep the `current_is_bcc` declaration; only drop it from this OR.)

2. After the `must_end` computation (line ~1563), force a Bcc to terminate the
   block WITHOUT forward-follow unrolling:

```c
bool must_end = helper_callsite || __atomic_load_n(&regs.spcflags, __ATOMIC_ACQUIRE) || blocklen >= maxrun_limit;
/* Compile up to AND INCLUDING the Bcc as a chaining terminator (register_branch
 * emits the conditional + two successor-block edges). Force must_end so we never
 * forward-unroll past it — that unroll is the "fall through extension words"
 * hazard the old barrier guarded against. Backward edges already end the block;
 * DBcc stays a barrier. */
if (current_is_bcc)
    must_end = true;
```

Net effect: a Bcc now compiles as the last op of its block via the existing
`register_branch` chaining, instead of forcing the whole block to the
interpreter. No new codegen.

## Validation (verifier-INDEPENDENT first, then oracle)

1. **Real-exec block-count DROP (the lever — unlike 0800 this MUST move):**
   narrow run `B2_JIT_VERIFY_BLOCKS=0x0409f500-0x0409f600`, ~780s. Blocks
   `0409f520/f57c/f582/f5c6/f5cc` (termop 6706/62b8) flip from
   `skipped=trace_barrier` → a compiled verdict. Expect the window
   `skipped=trace_barrier` total to drop by those blocks' share. This is the
   genuine boot-lever delta.
2. **Oracle mismatch=0 on the newly-compiled Bcc blocks. FAIL rule ABSOLUTE:**
   if ANY currently-passing block flips to `mismatch=1`, STOP + revert (a real
   divergence was being masked, not a false positive).
3. **Verifier-independent real-exec probe:** boot must reach at least the same
   PC wall (no new hang/regression) — confirms the compiled branches take the
   correct runtime direction, not just in the oracle re-run. (B)-style: optional
   D0/PC-trajectory check at `0409f520`'s 6706 site over N executions vs interp.

If clean: commit per-barrier scope (Bcc only; DBcc/LINK/UNLK/RTS remain follow-on)
and push to `rcarmo-jit/main` with the measured block-count drop.

## Follow-on (after Bcc lands, same binding list)

`LINK 4e56` / `UNLK 4e5e` / `RTS 4e75` — stack-frame native continuation
(separate spec; A7-frame correctness). Rank after Bcc since Bcc is ~2x their
combined frequency in this window.

---

## VERIFICATION OUTCOME (implemented + tested @ HEAD 511fc44 → REVERTED)

Applied both edits (dropped `current_is_bcc` from `trace_barrier_op` + forced
`must_end` pure terminator), incremental build clean, ran narrow oracle
`0x0409f500-0x0409f600` (780s).

**HARD REGRESSION — early-ROM boot broke.** Boot reached only PC ~`0x01002cb4`
(early ROM, 0x0100xxxx) then fell into an infinite spin toggling the SCR2 LED at
`$0200d003` between PC `0x0100254e` ↔ `0x01002568` (860 toggles, 84% of the log).
It **never reached** the `0x0409xxxx` kernel range the 0800 baseline reached
(0 JITBLOCKVERIFY lines in the verify window). No panic/segv — a live-lock.

**Conclusion — the diagnosis was incomplete.** The structural finding (the
barrier pre-empts the correct `end_block` terminator path) is still true, but the
native Bcc COMPILED path itself diverges in early ROM — the bug is in the
compiled handler / `register_branch` chaining / flag-liveness, **NOT** the
forward-unroll. "Pure terminator, no unroll" does not make it safe; the barrier
was guarding a real native-Bcc codegen bug, not just an unroll artifact. FAIL
rule (boot regression) → reverted; binary restored to `ec26050` behavior.

**Next (root-cause BEFORE re-enabling):** the divergence is reachable EARLY —
the spin is at PC `0x01002568`/`0x0100254e` and boot dies right after
`0x01002cb4`. That is a cheap, fast repro (no 13-min wait to the kernel window).
Single-step JIT-vs-interp at the first Bcc executed in `0x01002500-0x01002d00`
(verifier-independent real-exec comparator — the transferable BasiliskII
technique): find the first PC where the compiled Bcc takes a different
direction / wrong fall-through PC than the interpreter, then fix that codegen
(suspects, in order): (1) `register_branch` not_taken/taken target PCs vs
`m68k_pc_offset` extension-word accounting; (2) `make_flags_live()` timing
(flags consumed by the cc test not yet materialized at the branch); (3) native-cc
mapping for the specific cc that early ROM uses at `0x01002568`. Only re-enable
the barrier drop after that block compiles `mismatch=0` AND boot passes
`0x01002cb4` to the kernel range.

---

## ROOT-CAUSE NARROWED (single-step probe @ early-ROM repro) — it's FLAGS, not branch-direction

Ran the JIT-vs-interp comparator on the cheap early repro
`B2_JIT_VERIFY_BLOCKS=0x01002500-0x01002d00`. Decisive results:

**Pinned the first diverging block: `0x01002c74` (len 6), terminal Bcc.**
Comparator (full-Bcc enable): `mismatch=1 regs=1 ctrl=1 flags=1`, 189× vs 4×
match (intermittent = entry-state-dependent). Detail line:
```
pc interp=01002c80/...2c74  native=01002c74/...2c80   (regs.pc base stale, pc_p correct)
sr interp=2710 native=2708 / native=2718              (low SR byte: interp 0x10=X, native 0x08=N / 0x18)
```

**Eliminated hypotheses:**
- **regs.pc commit is correct.** Block-end branch resolution
  (`compemu_support_arm.cpp:6899+`) emits `compemu_raw_endblock_pc_isconst` for
  BOTH targets, writing the full PC triple. The comparator's "stale regs.pc =
  block-start" is an artifact (it reads `regs.pc` base, not the synced PC; native
  `pc_p` is correct). NOT the bug.
- **Branch-direction / backward-loop is NOT the (sole) cause.** Tried a scoped
  **forward-only** narrowing (enable native continuation only for strictly-forward
  short Bcc — displacement = opcode low byte; backward + Bcc.W/.L stay barriers).
  Early 90s run looked clean (LED-spin 862→1, advanced past 0x01002c86), BUT the
  full 780s boot **still live-locks** (868 LED-spin lines, kernel range never
  reached). Forward Bcc hangs too → direction/loop is not the root cause.

**Root cause (narrowed):** the compiled Bcc evaluates **divergent flags** vs the
interpreter (low SR byte interp `0x10` X vs native `0x08`/`0x18` N — the
nzcv-vs-cznv flag-layout seam) → the live `compemu_raw_jcc_l_oponly(cc)` takes the
wrong direction → control eventually routes into the `0x0100254e/2568` LED-spin
loop. The bug is in the **JIT flag state feeding the branch condition**
(`make_flags_live` / flag-layout), NOT the trace barrier, NOT regs.pc, NOT
branch-direction. This is the same flag-layout seam tracked across the oracle
work — here it has a REAL runtime consequence (not just a comparator artifact),
because a compiled Bcc *consumes* the flags for control flow.

**Decision:** reverted full-Bcc AND forward-only (both regress boot; FAIL rule).
The Bcc lever is BLOCKED on fixing JIT→Bcc flag correctness first. Next:
single-step the flag state at `0x01002c74`'s terminal Bcc — capture m68k CCR
(interp) vs the ARM NZCV `make_flags_live` materializes (JIT) at the branch, find
which flag bit inverts, fix the flag-layout conversion on that path. Only then
re-test Bcc continuation. `ec26050` (0800 handler) stands; this slot eliminated 3
hypotheses and pinned the blocker to JIT branch-flag computation.
