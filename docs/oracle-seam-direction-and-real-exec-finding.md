# Oracle-seam: Fix-B direction resolved + a REAL-execution concern (static)

Builds on docs/oracle-flag-layout-seam.md (sibling d6f5e8a: cpufunctbl is bit14).
This pins the Fix-B conversion DIRECTION and surfaces that the seam is not
purely a verifier artifact.

## Fix-B direction is determined (no probe needed for direction)

- The verifier ENTRY snapshot is captured in `execute_normal` (the interpreter
  trace builder) at compat.cpp:1478, BEFORE any op of the trace runs. At that
  point word-0 of regflags is whatever the interpreter path left =
  **bit14 cznv** (interpreter-ready).
- Both verifier runs restore that one bit14 snapshot. The interp run reads it
  correctly (bit14). The JIT run reads word-0 as **bit30 nzcv** -> wrong CCR.
- Evidence matches: at 0409ecbe/0409ec70 the JIT (native) D0 was WRONG (2608)
  and the interp D0 was RIGHT (2600). So the JIT run is the one starting from
  mis-read flags.

=> Fix B converts the ENTRY CCR from bit14(cznv) -> bit30(nzcv) for the JIT run
   ONLY; the interp run is left untouched. Minimal, direction-pinned. Fix A
   (compare architectural CCR, not raw nzcv) stays as output-side defense.

## REAL-execution concern (NOT verifier-only)

The JIT block-entry reload at compemu_support_arm.cpp:6256:
```
LOAD_U64(REG_WORK1, &regflags.nzcv); LDR_wXi(REG_WORK2,...); MSR_NZCV_x(REG_WORK2);
```
unconditionally loads word-0 into hardware NZCV ASSUMING bit30. In real
execution, when a JIT block follows an INTERPRETED block (execute_normal, bit14)
with LIVE condition codes, word-0 holds a bit14 cznv value; the reload mis-reads
it as bit30 -> the compiled block starts from wrong flags.

- This is the same mechanism the verifier exposes, but it can occur in normal
  pure-JIT execution at any interp->JIT boundary where CCR is live into the
  first compiled op (canonical trigger: MOVE SR,Dn / MVSR2 as the block's first
  instruction, exactly 0409ecbe/0409ec70).
- Boot mostly survives because CCR is usually DEAD across block boundaries
  (most blocks recompute flags before use; control-transfer terminators don't
  consume incoming CCR). It bites only the live-CCR-at-entry minority.

### Implication for the fix
Fix B (verifier entry canonicalization) makes the ORACLE trustworthy but MASKS
this real boundary seam. The durable fix must canonicalize regflags layout at
the real interp<->JIT boundary too (Fix C territory), e.g.:
  - convert cznv->nzcv when entering a compiled block whose predecessor was
    interpreted, or
  - unify the interpreter and JIT on one flag layout.
Lowest-risk staged plan:
  1. Land Fix B+A (verifier only) so the oracle stops lying; re-validate MVSR2
     (0409ec00-0409ed00) -> intermittent mismatch=0; re-land 0800 guard.
  2. Bounded probe to CONFIRM the real-exec seam: dump regflags.nzcv (hex) at
     the block-entry reload for 0409ecbe when its predecessor was interpreted;
     if the value is a bit14-shaped cznv (Z at bit14, not bit30), the real
     boundary needs conversion.
  3. If confirmed: add the cznv->nzcv canonicalization at the JIT block-entry
     reload (or at the execute_normal->compile handoff). Re-verify and re-sweep.

## Probe plan (bounded, one run, next CPU slot)
- Narrow window 0x0409ec00-0x0409ed00, B2_TRACE gated to block 0409ecbe only.
- Temporary stderr at compemu_support_arm.cpp:6256 prelude: print
  `regflags.nzcv`, and the bit14-Z `((nzcv>>14)&1)` vs bit30-Z `((nzcv>>30)&1)`
  for the entry, plus whether the predecessor block was interpreted.
- Pass criterion: identify which Z bit carries the live flag at JIT entry; that
  fixes both the verifier direction (already inferred bit14) and confirms the
  real-exec need.
