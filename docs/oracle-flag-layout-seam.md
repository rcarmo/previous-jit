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

## OPEN QUESTION — RESOLVED STATICALLY (2026-06-18, post-outage, no run)

Question was: does the src/cpu interpreter in THIS build use bit14, or is it
overridden to bit30 by a force-included prelude
(`uae2026_compiler_probe_prelude.h:38` defines FLAGBIT_Z 30)?

ANSWER: cpufunctbl handlers are **genuinely bit14 (cznv layout)**. The verifier
reference is wrong-layout vs the JIT. No runtime probe needed. Three independent
static facts:

1. The prelude is force-included ONLY by the two probe tools
   (`tools/uae2026-compiler-syntax-probe.sh:18`,
   `tools/uae2026-compiler-object-probe.sh:20` via `-include`). `grep -rn
   uae2026_compiler_probe_prelude` finds NO reference in any CMakeLists /
   Makefile / build target. The real `newcpu.c` / `build_cpufunctbl()` build
   never sees the prelude.
2. `src/cpu/newcpu.h:334-336` and `src/cpu/m68k.h:34-36` define
   FLAGBIT_N=15, Z=14, C=8, V=0 for the interpreter handlers that populate
   `cpufunctbl`. These are the headers actually compiled into the interpreter.
3. The shared symbol is proven by aliasing, not by accident: every layout
   variant of `struct flag_struct` in `uae_cpu_2026/m68k.h` is declared
   `extern struct flag_struct regflags __asm__ ("regflags")` — the SAME 8-byte
   object at offset 0. The interpreter variants name word-0 `cznv` (bit14/15,
   x86-mirror); the JIT variant (line 454/750) names the identical word-0
   `nzcv` (bit31/30, ARM64). Same memory, two incompatible bit assignments.

So: the interp reference run leaves word-0 in cznv/bit14 layout; the JIT run
leaves it in nzcv/bit30 layout; the verifier diffs them raw → intermittent
false mismatch. Confirmed end-to-end without executing anything.

### Probe — NO LONGER REQUIRED
The bit14-vs-bit30 probe is obviated by the static resolution above. (Original
plan: a temporary stderr dump in `jit_block_verify_compare` for block 0409ecbe
extracting `((nzcv>>14)&1)` vs `((nzcv>>30)&1)`. Skip it.)

## Candidate fixes — DECISION (after static resolution)

With the layout question resolved, the decision is forced:

- Fix A (canonicalize the *comparison* to architectural CCR) is NECESSARY but
  NOT SUFFICIENT. It cleans flag-only mismatches, but it CANNOT fix the MVSR2
  D0 mismatch: by the time MVSR2 has run, the wrong CCR is already baked into
  the GP register, so comparing the *outputs* still differs.
- The MVSR2 D0 divergence originates at ENTRY: the verifier restores ONE entry
  `regflags` snapshot and feeds it to both engines, but that word-0 is in only
  one layout. Whichever engine's layout it does NOT match starts the block from
  wrong condition codes.
- Therefore the oracle fix must canonicalize ENTRY flags per engine: **Fix B**
  (re-encode the entry CCR into each engine's own layout before its run), or
  **Fix C** (unify the reference engine's layout to the JIT's). B is the
  minimal correct change scoped to the verifier; C is broader and also forces
  auditing the real exact-exec JIT<->interp boundary.

RECOMMENDED LANDING ORDER (next CPU slot, per-change commits):
  1. Fix B in `jit_block_verify_*`: at entry, derive the 5 architectural CCR
     bits from the snapshot, then materialize the interp run's `regflags.cznv`
     via the bit14/15 layout and the JIT run's `regflags.nzcv` via bit31/30,
     so both engines start from equivalent flags. Add Fix A on the output
     compare as defense-in-depth (compare CCR, not raw nzcv).
  2. Re-run the narrow MVSR2 window (`0x0409ec00-0x0409ed00`); expect the
     intermittent `mismatch=1` on 0409ecbe/0409ec70 to go to 0.
  3. THEN re-land the 0800 EA-mode guard (already specced) and re-sweep.
  4. Audit the REAL exact-exec boundary for the same entry-layout seam
     (Fix C concern) before landing the flag-crossing Bcc/LINK/UNLK/RTS/JSR/PEA
     barriers.

### Original options (for reference)

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
