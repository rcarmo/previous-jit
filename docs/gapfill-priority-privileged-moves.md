# Gap-fill priority: privileged-move fallbacks that contaminate the MVSR2 window

Context: e82b263 proved Fix B+A is unsound on exact-exec blocks. The MVSR2
window is fallback-heavy because 46fc/46c6/48e7 run via the INTERPRETER
(JIT_EXACT_EXEC), which both (a) IS the forbidden fallback the goal targets and
(b) makes the verifier "actual" snapshot cznv, breaking Fix A/B.

Sequence (auditor-locked): (1) make the verifier execution-mode-aware (gate Fix
B + actual-side Fix A on a per-block native-compiled vs exact-exec boolean), THEN
(2) gap-fill the privileged moves so they COMPILE (nzcv) — advancing zero-fallback
AND de-contaminating the window (compiled actual=nzcv -> Fix A/B finally hold).

## Op status + in-window fallback frequency
Source: build-vnc/sweep-04090000-btst-fix.log (59192 lines, post-BTST sweep).

| op   | decode                         | JIT_FALLBACK | handler today                          | blocker |
|------|--------------------------------|--------------|----------------------------------------|---------|
| 46fc | MOVE #imm,SR  (MV2SR.W imm)     | 8            | op_fullsr_mv2sr_w_comp_ff (EXISTS)     | barrier line 1210 (i_MV2SR sz_word, gated jit_allow_ram_dispatch_env) |
| 46c6 | MOVE D6,SR    (MV2SR.W Dn)      | 2            | op_fullsr_mv2sr_w_comp_ff (EXISTS)     | same barrier line 1210 |
| 48e7 | MOVEM.L <list>,-(A7)            | 2            | op_48e0 MVMLE family (EXISTS) but MMU restart/fixup path | 040 movem restart bookkeeping (legacy_ram_direct_movem_long_predec) |

## DECISION: gap-fill MV2SR.W (46fc + 46c6) FIRST

Reasons:
1. Highest combined frequency: 46fc(8)+46c6(2) = **10** vs MOVEM 48e7 = 2.
2. Lowest effort: the compiled handler ALREADY EXISTS
   (op_fullsr_mv2sr_w_comp_ff, routed at line 5755 for 0x46c0-0x46ff). The
   "gap-fill" is NARROWING/removing the force-barrier at line 1210, not new
   codegen.
3. Maximum de-contamination: 46fc/46c6 are the exact ops that produced the four
   in-window false/corrupt verdicts (0409ecda/ec68/ec6c/ecd4). Compiling them
   makes those blocks' actual = nzcv, so the exec-mode-aware Fix A/B assumptions
   hold there.

## CAVEAT (why it is NOT a free un-barrier)
Line 1210's comment: MV2SR.W is kept exact because the NeXT RTC/SCSI boot path
"misbranches out of the SR-save/restore loop and vectors into the early kernel
exception handler before its globals are initialised." So removing the barrier
risks reintroducing that boot misbranch. It MUST be validated via the
exec-mode-aware verifier (mismatch=0 on the now-compiled MV2SR.W blocks) AND a
boot-progress check (past 0x04387 SCSI, no early-exception vector), not blindly.

## Expected deltas (next slot, after exec-mode-aware verifier lands)
- Compiling MV2SR.W removes ~**10** exact-exec fallbacks per window pass
  (46fc 8 + 46c6 2) and de-contaminates ~**4** false/corrupt MVSR2-window
  verdicts -> the window can finally read mismatch=0 on real divergences.
- MOVEM 48e7 (~2, MMU-restart-sensitive) is the lower-priority follow-on.

## Landing order
1. exec-mode-aware verifier (per-block actual_is_nzcv; gate Fix B + actual-side
   Fix A). Validate on 0x0409ec00-0x0409ed00 with tools/fg-verify-window.sh.
2. Narrow the line-1210 MV2SR.W barrier so op_fullsr_mv2sr_w_comp_ff compiles;
   re-verify the window -> the 46fc/46c6 blocks become native (actual=nzcv) and
   should read mismatch=0; confirm boot still advances past the SCSI poll.
3. Re-sweep 0x04090000-0x040b0000; report fallback-count drop + zero new mismatch.
4. THEN 48e7 MOVEM, then the ranked Bcc/LINK/UNLK/RTS/JSR/PEA barriers.
