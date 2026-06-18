# GATING ISSUE: verifier flag-layout seam (bit14 interp ref vs bit30 JIT)

Status: SPEC + open runtime probe. This blocks validating ALL flag-touching JIT
coverage (MVSR2, BTST-exposed blocks, and the ranked Bcc/LINK/UNLK/RTS/JSR/PEA
barriers). Fix the oracle BEFORE landing more flag-related handlers/predicates.

## Confirmed statically (nm + build_cpufunctbl)

- `regflags` is ONE shared object (`C regflags`, 8 bytes = {nzcv/cznv; x}); both
  newcpu.c and the uae2026 JIT unit reference the same storage.
- `cpufunctbl` is ONE shared table, populated by `build_cpufunctbl()` in
  `src/cpu/newcpu.c` with the src/cpu interpreter handlers.
- src/cpu headers declare `FLAGBIT_N=15, FLAGBIT_Z=14`; the JIT uses
  `FLAGBIT_N=31, Z=30, C=29, V=28` (ARM64 NZCV). Bits do not overlap.

## Symptom

The verifier (`jit_block_verify_run` / `jit_block_verify_compare`,
compemu_support_arm.cpp ~700-819) runs the block twice from one restored entry
snapshot: once via `cpufunctbl` (bit14 interp) and once via the compiled JIT
(bit30), then compares raw `regflags.nzcv`, `regflags.x`, and `regs.regs[]`.

When entry flags are not canonical in BOTH layouts, the two engines read
different condition codes, so:
- flag-only blocks mismatch on `flags.nzcv` (e.g. 0409ecda residual, artifact);
- flag-reading ops (MVSR2 = MOVE SR,Dn) copy the divergent flags into a GP
  register, producing an INTERMITTENT `regs=1` mismatch (0409ecbe/0409ec70).
Intermittency = entry-state dependent = layout artifact, not a handler bug.

## OPEN QUESTION (needs a 1-shot runtime probe next slot)

Does the src/cpu interpreter in THIS build actually use bit14, or is it overridden
to bit30 by a force-included prelude (uae2026_compiler_probe_prelude.h:38 defines
FLAGBIT_Z 30)? The fix differs:
- If cpufunctbl handlers are genuinely bit14: the verifier reference is
  wrong-layout vs the JIT -> normalize.
- If they are bit30 (prelude): the seam is elsewhere; re-root-cause.

### Probe (cheap, gated, one bounded run)
Add a temporary stderr dump in `jit_block_verify_compare` for block 0409ecbe only:
print `expected->flags.nzcv`, `actual->flags.nzcv`, and `expected/actual` D0,
plus the result of a bit14-Z extraction `((nzcv>>14)&1)` vs bit30-Z
`((nzcv>>30)&1)` for both snapshots. One narrow-window run (~6min) tells us which
layout each engine left in regflags at the boundary.

## Candidate fixes (pick after probe)

A) **Canonicalize the comparison:** in `jit_block_verify_compare`, before diffing,
   convert each snapshot's `regflags` to the 5 architectural CCR bits using that
   snapshot's own layout, and compare CCR — not raw nzcv. (Handles flag-only
   mismatches; does NOT fix MVSR2 D0 regs mismatch.)

B) **Canonicalize entry flags for both runs:** at entry, derive architectural CCR
   from the (currently-valid) entry regflags and re-encode it so a bit14 read and
   a bit30 read agree. Requires knowing which layout the entry value is in.

C) **Unify the reference engine layout:** make the verifier's interpreter
   reference run in the JIT's bit30 layout (or vice versa) so shared regflags is
   read consistently. Most robust; largest change. This also implies the REAL
   system must convert flags at every JIT<->interp (exact-exec) boundary — verify
   that path too, since the same seam could affect normal execution.

## Downstream (blocked on this)

The ranked trace-barrier gap-fills — Bcc (6706/62b8/6704/6c9c), LINK (4e56),
UNLK (4e5e), RTS (4e75), JSR (4e92), PEA (4878) — all end blocks whose verdicts
depend on flag state crossing boundaries. They cannot be gate-validated until the
oracle reads flags consistently. Spec them after the oracle fix.
