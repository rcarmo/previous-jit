# SLOT FINDING: island fix fixes real-exec but is UNVALIDATABLE by the verifier gate

Date 2026-06-19, ACTIVE slot. Attempted to land the corrected EDIT1'/EDIT2/EDIT3'
(island canonicalization). Built clean, ran the gate — REVERTED. Evidence below.

## What was implemented + tested

EDIT1' (helpers + depth-guarded JitInterpFlagIsland in compemu_support_arm.cpp
above the SR helpers), EDIT2 (island guard at start of exec_nostats @1108 and
execute_normal @1248), EDIT3' (island guard in the LIVE SR-writers
jit_runtime_orsr/andsr/eorsr_word + jit_op_stop; jit_op_rte kept probe-gated).
Note: the live helpers call MakeSR (READ) AND MakeFromSR (WRITE), so they need the
FULL island, not just an exit convert.

Build clean. Gate run `tools/fg-verify-window.sh 0x0409ec00-0x0409ed00 720`:

```
total mismatch=1: 4   (UNCHANGED)
0409ecbe/ec70/ece0/ec68  regs=1  reg[0] interp=2600 native=2604 / 2700 native=2704
0409ecda/ec6c/ecd4: still clean
window reached (1464 verifies) -> boot passed the 0x04387 SCSI poll
```

The 4 target bugs did NOT flip to 0. Identical D0 divergence as before the fix.

## Root: the verifier cannot OBSERVE a boundary fix (capture-coupling)

Structurally confirmed (no ambiguity):
- The verify ENTRY snapshot is captured at `compemu_legacy:1478`, which is INSIDE
  `execute_normal` (starts @1248). An island guard at the function start converts
  nzcv->cznv BEFORE the capture -> the snapshot is **cznv**.
- The verifier then re-runs the block in ISOLATION from that snapshot:
  - native run: restore cznv, `:6256` reads word0 as nzcv -> MISREADS -> wrong D0
    (the 4 bugs persist), regardless of the island.
  - interp run (`compemu_support_arm.cpp:785`, `(*cpufunctbl[opcode])(opcode)`)
    is NOT island-wrapped -> reads cznv directly -> correct.
- So the compare still sees native!=interp. The island fixes REAL execution
  (interp predecessors now leave nzcv in normal flow) but the verifier's
  capture-mid-island + isolated native re-run cannot reflect it.

Per the spec's OWN gate ("correctness gated on oracle 4->0, not boot"), the fix is
therefore UNVALIDATABLE as specified. Shipping an unvalidated flag-conversion
(whose sign error would NOT obviously break boot — spec's own risk note) is the
exact thing to avoid. Reverted.

## The two are coupled: real-exec island + exec-mode-aware verifier must co-design

To make the oracle observe the boundary fix, ALL of:
1. Capture the entry snapshot at the TRUE boundary = **nzcv**. Either place the
   execute_normal island guard AFTER `jit_block_verify_entry_capture` (so capture
   sees pre-island nzcv) OR convert the captured snapshot cznv->nzcv in
   `jit_block_verify_entry_capture`.
2. Island-wrap the verifier INTERP re-run (the cpufunctbl loop @785) so it reads
   the nzcv snapshot via nzcv->cznv and leaves cznv->nzcv.
3. Re-examine the exec-mode-aware compare (8665c74): once both re-runs start from
   nzcv and convert per-engine, the actual/expected layouts change; the
   ccr_from_cznv(expected) vs ccr_from_nzcv(actual) decode must be re-derived for
   the island world (likely both become nzcv at capture and the per-engine decode
   keys off WHERE the snapshot is taken).

This is a co-design of two subsystems, not a 3-edit drop. EDIT2 alone is correct
for real execution but invisible to (and inconsistent with) the current verifier.

## Alternative validation (cheaper than the co-design, real-exec-direct)

Instead of bending the verifier, validate the island against REAL execution at the
known site:
- Temporary stderr in `jit_op_mvsr2` (or at 0409ecbe in non-verify execution)
  logging D0/SR with the island ON vs OFF (B2_JIT_FLAG_ISLAND=0/1 env gate on the
  JitInterpFlagIsland conversions).
- PASS: with island ON, the MVSR2 at 0409ecbe yields D0 matching the interpreter
  (Z bit correct) in REAL execution; OFF reproduces the wrong Z.
Then land the island gated/validated on real-exec, and SEPARATELY co-design the
verifier (or accept the 4 verifier reports become known real-exec-correct
artifacts until the verifier is updated).

## Verdict
Do NOT land EDIT1-3 as a verifier-gated change — the gate cannot pass by
construction. Next slot: either (A) co-design verifier capture+interp-island with
EDIT1-3 as ONE invariant, or (B) land the island validated by the real-exec D0
probe and re-spec the verifier separately. Recommend (B): smaller, decoupled,
and it directly proves the 4 real bugs are fixed.

HEAD a212285, tree clean, fix reverted, no procs, /workspace/tmp clean.
