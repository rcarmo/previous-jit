# DECISIVE: the 4 "native-MVSR2-after-interp bugs" are VERIFIER ARTIFACTS

Date 2026-06-19, ACTIVE slot. Built the env-gated island + MVSR2_PROBE, ran the
real-boot A/B (the authoritative gate). Result overturns the premise. Island
reverted (no-op); HEAD 05147fa clean.

## Probe result (real boot, non-verify, pc=0409ecbe)

```
island ON : 8x value=2604 (b30Z=1 b14Z=0) , 3x value=2608 (b30Z=0 b14Z=0)
island OFF: 8x value=2604 (b30Z=1 b14Z=0) , 3x value=2608 (b30Z=0 b14Z=0)   IDENTICAL
b14Z=1 (cznv entry) count: ON=0  OFF=0
```

- Island ON == OFF: the island has ZERO effect on real execution of the MVSR2.
- `b14Z=0` in 100% of samples: the real-exec entry word0 is ALWAYS nzcv. The
  predecessor leaves nzcv; there is no cznv-entry-into-native-MVSR2 in real boot.
- Therefore the native MVSR2 reads the correct nzcv flags and produces the
  CORRECT value (2604 when Z set, 2608 when N set). There is NO real bug.

## What the verifier was actually reporting

The verify snapshot is captured at the real boundary = nzcv (execute_normal is
NOT islanded on HEAD, so capture@1478 sees the predecessor's nzcv exit). Then:
- native re-run: restores nzcv, `:6256` reads nzcv -> CORRECT D0 (2604).
- interp re-run (`compemu_support_arm.cpp:785`, direct `cpufunctbl`): restores
  nzcv, reads it as cznv (b14Z=0) -> D0=2600.
So the mismatch is the verifier's INTERP REFERENCE being wrong (it reads the nzcv
snapshot with cznv layout). The NATIVE side was right all along. The 4
mismatches (0409ecbe/ec70/ece0/ec68) are pure verifier artifacts.

## Consequence

- DO NOT land the real-exec island. It is a no-op for these sites (proven) and
  adds conversion overhead/risk to interpreter + SR-writer paths for no gain.
- The fix is VERIFIER-ONLY: make the verify INTERP re-run read the nzcv snapshot
  correctly (island-convert nzcv->cznv around the cpufunctbl loop), AND keep the
  native re-run consistent.

## PART C (verifier re-spec) — precisely scoped, for next slot

Snapshot is nzcv (do NOT island execute_normal — that would flip the snapshot to
cznv and re-break the native re-run, as observed in the first island attempt).
Make BOTH verify re-runs read the nzcv snapshot correctly, then compare nzcv-vs-nzcv:

1. Helpers: jit_flags_nzcv_to_cznv / cznv_to_nzcv + depth-guarded JitInterpFlagIsland
   (define above jit_block_verify_compare in compemu_support_arm.cpp; math validated).
2. INTERP re-run: scope-wrap the `for (...) (*cpufunctbl[opcode])(opcode)` loop
   (compemu_support_arm.cpp ~776-787) in `{ JitInterpFlagIsland _g; for(...){...} }`
   so it converts nzcv->cznv for cpufunctbl and back; capture `interp` AFTER the
   scope (so `interp` is nzcv).
3. NATIVE re-run consistency: the native re-run's exact-exec ops + SR-writer
   helpers (jit_runtime_orsr/andsr/eorsr_word, jit_op_stop) ALSO read the nzcv
   snapshot via the cznv interpreter -> island-wrap `exec_nostats` (legacy:1108)
   and those helpers so they read the snapshot correctly. Do NOT wrap
   execute_normal (preserve the nzcv capture). jit_op_mvsr2 already rebuilds from
   nzcv (untouched); jit_op_rte stays probe-gated.
4. Compare: with both re-runs ending in nzcv, set exp_ccr = ccr_from_nzcv(expected)
   (was ccr_from_cznv) and act = ccr_from_nzcv(actual); drop the actual_is_nzcv
   branch (or hardwire true). The regs memcmp now matches (both compute D0 from
   the same nzcv flags).
5. Validate: window 0x0409ec00-0x0409ed00 -> ecbe/ec70/ece0/ec68 mismatch=1 -> 0
   AND ecda/ec6c/ecd4 stay clean. Smokes (opcode 66/0, MMU X-bit) for safety.
   COMMIT (verifier-only; one commit).

## Net
The (B) probe did its job: it proved the island fixes nothing in real execution,
so the "4 bugs" are downgraded to oracle false-positives. No island SHA (reverted).
PART C makes the oracle stop false-reporting them (verifier-only), and unblocks the
real zero-fallback work (the trace-barrier gap-fills) on a clean oracle.
