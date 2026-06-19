# SLOT FINDINGS: exec-mode-aware oracle landed; Fix B misguided; MV2SR.W handler broken

Date 2026-06-19. CPU-run slot (rotation index 1). Builds on e82b263 / 3fdb8eb.

## What landed (8665c74)

exec-mode-aware verifier (Fix A only): per-block native-vs-exact signal
(`forced_interpreter_barrier || optlev==0`, captured in compile_block) threads
into jit_block_verify_compare; `actual` decoded via ccr_from_nzcv when native,
ccr_from_cznv when exact-exec; compares the 5 architectural CCR bits.

Validated window 0x0409ec00-0x0409ed00 (tools/fg-verify-window.sh, 720s):
- exact-exec layout-contamination FALSE POSITIVES cleared: 0409ecda/ec6c/ecd4
  now seen-but-CLEAN (were the e82b263 false flags=1).
- 4 remaining mismatch=1 are REAL: 0409ecbe/ec70/ece0/ec68, regs-only, D0 Z-bit
  (interp 2600/2700 vs native 2604/2704). All are
  [40cX MOVE SR,Dn (native MVSR2, reads entry CCR); 46cX MOVE Dn,SR (exact-exec)].
- stable, no segfault, tree/tmp clean.

## Finding 1: Fix B (entry cznv->nzcv conversion) is MISGUIDED — dropped

Fix B pre-converted the verifier's entry flags to nzcv before the JIT run so a
native block's :6256 reload reads them correctly. But in REAL execution the entry
flags ARE cznv (left by an interpreter predecessor), and the :6256 reload
unconditionally assumes nzcv — so the native block genuinely mis-reads in real
execution. Fix B makes the VERIFIER pass while real execution still fails => it
MASKS a real bug. Correct oracle REPORTS it. Fix A (exec-mode CCR compare) alone
is the trustworthy oracle. Fix B removed.

The 4 real bugs are the genuine interp->JIT flag-layout seam (native block reading
live entry CCR after an interpreter block). The durable fix is real-execution
boundary canonicalization (cznv->nzcv when leaving the interpreter) OR removing
the interp predecessors by compiling the privileged moves (see Finding 2).

## Finding 2: MV2SR.W compiled handler is BROKEN — gap-fill blocked

Un-barriering MV2SR.W (line 1228, env-gated to compile via
op_fullsr_mv2sr_w_comp_ff) was tested. Result: SEGFAULT, and the window
mismatches worsened to regs=1 ctrl=1 mem=1 (control-flow + memory divergence).
This is exactly the boot misbranch the line-1228 comment warned about: the
compiled handler is wrong. So the auditor's "compile 46fc/46c6" gap-fill is
BLOCKED by a broken handler, not merely a barrier. Reverted; barrier kept.

Implication: the MV2SR.W gap-fill needs op_fullsr_mv2sr_w_comp_ff FIXED first
(root-cause the segfault + ctrl/mem divergence) before it can compile. That is a
handler-codegen task, larger than a barrier flip. Re-rank: the safe cheap wins
are the trace-barrier register-direct ops, not the privileged moves whose handler
is broken.

## Next slot
1. Either (a) fix op_fullsr_mv2sr_w_comp_ff (root-cause segfault) so MV2SR.W can
   compile and remove the interp predecessors; or (b) implement the real-exec
   interp->JIT boundary cznv->nzcv canonicalization (fixes the 4 real bugs AND is
   the general seam fix). (b) is higher-leverage.
2. With the oracle now trustworthy (8665c74), the register-direct 0x0800 BTST
   guard (docs/gapfill-spec-immediate-bitop-0800.md) can be re-validated cleanly
   (no exec-mode contamination on those blocks).
