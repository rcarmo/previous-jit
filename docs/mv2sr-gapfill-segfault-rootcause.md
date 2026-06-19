# MV2SR.W gap-fill segfault: static root-cause + fix direction

Date 2026-06-19, static cycle (no build). Diagnoses the segfault from narrowing
the line-1228 barrier so 46fc/46c6 compile via op_fullsr_mv2sr_w_comp_ff.

## Confirmed statically

1. Narrowing `jit_force_interpreter_barrier_opcode` (line 1228, checked at compile
   loop 6343) routes 46c0-46ff into NATIVE codegen via
   `op_fullsr_mv2sr_w_comp_ff`. The other barrier
   `jit_force_exact_exec_nostats_opcode` (line 1078, the 0x46c0 family) is checked
   at 6705 — but only in the codegen-FAILURE path. op_fullsr_mv2sr_w_comp_ff
   SUCCEEDS, so 6705 is never reached. No double-emission.

2. `op_fullsr_mv2sr_w_comp_ff` (line 4099) delegates to
   `jit_runtime_mv2sr_word_full` via `jit_emit_runtime_helper_barrier`. That helper
   path is SOUND and IDENTICAL to the WORKING `op_move_l_d8anxn_absw_comp_ff`,
   `op_move_l_reg_d16an_comp_ff`, `op_movea_l_postinc_an_comp_ff` (same
   non-advancing m68k_pc_offset, same arg1=opcode, helper does EA + m68k_incpc).
   It sets `jit_force_runtime_pc_endblock` -> compile loop 6488 ends the block and
   sets `forced_interpreter_barrier=true` -> endblock 7110 applies `set_dhtu`
   (NON-DIRECT dispatch). So block-end + non-direct revalidation ARE handled.

3. `jit_runtime_mv2sr_word_full` (line 3835) handles ALL EA modes correctly,
   including Dn (0x00) and #imm (0x38/case 4). EA logic is NOT the bug.

## The discriminator: MV2SR.W transitions SUPERVISOR mode

The working comparators never change the S (supervisor) bit:
- `jit_runtime_orsr/andsr/eorsr_word`: `regs.sr |= / &= / ^= src` from an
  already-supervisor state -> S stays 1 (cannot transition to user). No A7 swap.
- move_l / movea: never touch SR.

`jit_runtime_mv2sr_word_full` does `regs.sr = src; MakeFromSR()` — a FULL replace.
In the NeXT boot SR-save/restore loop, MV2SR.W restores SR values with S=0, so
`MakeFromSR()` (newcpu.cpp) takes the `olds != regs.s` path and:
  - swaps A7: saves m68k_areg(7) to usp/isp/msp and loads the other SP, AND
  - calls `mmu_set_super(regs.s)` — changes MMU user/supervisor translation.

That global CPU-mode transition (A7 swap + MMU mode flip) mid-compiled-block is
exactly the failure the line-1228 comment documents: "misbranches out of the
SR-save/restore loop and vectors into the early kernel exception handler before
its globals are initialised." The Part1+2 run's `regs=1 ctrl=1 mem=1` + SIGSEGV
matches: wrong stack (A7) -> wrong push (mem), wrong vector (ctrl), bad deref.

## Why this is NOT a cheap barrier-flip (re-prioritize)

`op_fullsr_mv2sr_w_comp_ff` is already interpreter-bound (it calls a C helper that
runs MakeFromSR); it is NOT real native SR codegen. Its only "win" over the
exec-nostats barrier is avoiding a block split — but on an S-transition it is
unsafe. Making it safe means routing mode-changing MV2SR.W through the full
exec-nostats path, which is what the barrier already does. So compiling MV2SR.W
yields ~no zero-fallback benefit and high risk. RECOMMEND: keep the MV2SR.W
barrier; drop MV2SR.W from the gap-fill list.

Better next targets (now that the oracle is trustworthy, 8665c74):
1. Re-validate the register-direct 0x0800 BTST EA-mode guard
   (docs/gapfill-spec-immediate-bitop-0800.md) — genuinely cheap, no mode change,
   and the oracle no longer false-positives on exact-exec neighbors.
2. The real-exec interp->JIT cznv->nzcv boundary canonicalization — fixes the 4
   real native-MVSR2-after-interp bugs (0409ecbe/ec70/ece0/ec68) AND the general
   seam; higher leverage than any single privileged-move handler.

## If MV2SR.W is still pursued: confirmation diagnostic (next slot, one build)

Before any handler edit, CONFIRM the S-transition root:
- In `jit_runtime_mv2sr_word_full`, before/after MakeFromSR, log when
  `olds != regs.s` (B2_TRACE_MV2SR), dumping regs.sr, old/new S, A7, usp/isp.
- Narrow the barrier (B2_JIT_KEEP_MV2SR_EXACT=0) and run
  tools/fg-verify-window.sh; correlate the SIGSEGV / first ctrl-mem mismatch with
  an S-transition log line. Expected: the segfaulting MV2SR.W is one that flips S.
- Fix THEN: route only S-transitioning MV2SR.W to exec-nostats (detect in the
  helper is too late; gate at compile by keeping the barrier for the full family),
  or add explicit post-MakeFromSR A7/MMU re-sync + a dispatcher re-entry. Given
  the low value (point above), prefer keeping the barrier.
