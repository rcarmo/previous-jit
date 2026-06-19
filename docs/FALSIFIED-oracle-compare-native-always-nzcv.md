# FALSIFIED — "native side is ALWAYS nzcv" oracle compare fix

Status: **REJECTED — confirmed regression.** The single-edit fix in
`FINAL-spec-oracle-compare-native-always-nzcv.md` was implemented as real code,
built clean, and A/B-tested against the same narrow window. It does the
**opposite** of what the spec predicted: it breaks 13 currently-passing blocks.

## The edit under test

`compemu_support_arm.cpp:711-712`, native CCR decode in
`jit_block_verify_compare`:

```c
// BASELINE (HEAD ea7eb9a):
uae_u16 act_ccr = actual_is_nzcv ? ccr_from_nzcv(actual->flags.nzcv, actual->flags.x)
                                 : ccr_from_cznv(actual->flags.nzcv, actual->flags.x);
// SPEC "FIX" (forced):
uae_u16 act_ccr = ccr_from_nzcv(actual->flags.nzcv, actual->flags.x);
```

## A/B result (identical progress, JITBLOCKVERIFY=1457 both runs)

Env: `tools/fg-verify-window.sh 0x0409ec00-0x0409ed00`, pure JIT, no handoff
(`B2_JIT_RTE_FAULT_HANDOFF=0`), cold boot to MVSR2 kernel window.

| binary | mismatch=0 (pass) | flags-only mm1 | regs=1 mm1 | window total mm |
|---|---|---|---|---|
| baseline (ea7eb9a) | **13** | **0** | 4 | **4** |
| spec "fix" (forced nzcv) | 0 | 13 | 4 | **17** |

The spec claimed baseline=17 (13 flags + 4 regs) dropping to 4. **Reality is
inverted:** baseline window is already at 4 mismatches (only the regs=1
Finding-B entry-boundary seam at ec68/ec70/ecbe/ece0); the "fix" *raises* it to
17 by flipping 13 passing fallback blocks into flags-only mismatches.

The 13 blocks that pass at baseline and break under the fix:
`ec28, ec2c, ec68, ec6c, ec70, ecbe, ecd4, ecda, ece0`.

## Root-cause correction

The premise "native re-run via `pushall_call_handler` ALWAYS leaves regflags in
nzcv" is **false for interpreter-fallback blocks**. For these 13 blocks the
native snapshot is genuinely **cznv** (the fallback path runs the src/cpu
interpreter and leaves cznv; the compiled epilogue does not re-encode to nzcv in
this path). The existing `actual_is_nzcv` branch — set from
`jit_block_verify_actual_exact_exec` at 7109 (`optlev==0 ||
forced_interpreter_barrier`) — already decodes them correctly as cznv → match.
Forcing `ccr_from_nzcv` misreads the genuine-cznv snapshot → spurious flags=1.

So `actual_is_nzcv` is a **live, correct** discriminator, NOT dead code. The
verifier is already correct on flag-only blocks at HEAD.

## Disposition

- Fix DISCARDED (not committed; git stash dropped). Tree clean at ea7eb9a.
- ABSOLUTE FAIL rule tripped: 13 passing blocks → mismatch=1. STOP.
- The real remaining window defect is the **4× regs=1** (Finding B,
  entry-boundary flag/register seam at ec68/ec70/ecbe/ece0) — pre-existing,
  unaffected by this edit, and the correct next target.
- The 0800 EA-mode bitop guard is NOT unblocked by this (the oracle was already
  clean on flag-only blocks); it must be validated against the 4× regs=1 seam
  understanding, not a phantom 13-block flags artifact.

## What the auditor needs to know

The "oracle compare fix" deliverable is a **misdiagnosis**: the verifier flag
decode at HEAD is already correct. Escalate: re-baseline the barrier program on
window-total=4 (regs=1 Finding B), drop the 17→4 framing entirely.
