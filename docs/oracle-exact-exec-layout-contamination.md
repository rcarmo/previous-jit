# FINDING: exact-exec layout contamination invalidates Fix B+A as specified

Date: 2026-06-18. CPU-run slot (Verify 1 on the MVSR2 window).
Builds on docs/oracle-fix-implement-ready.md (the one-shot Fix B+A checklist).

## What was executed

- Implemented Fix B (entry cznv->nzcv convert for the JIT verify run) and Fix A
  (architectural CCR compare via `ccr_from_cznv` / `ccr_from_nzcv`) exactly per
  docs/oracle-fix-implement-ready.md, at the HEAD-verified anchors (698/706/790,
  773 untouched). Build clean.
- Verify 1: `B2_JIT_VERIFY_BLOCKS=0x0409ec00-0x0409ed00`, pure JIT + JIT_RAM,
  `B2_JIT_RTE_FAULT_HANDOFF=0`, booted to the SCSI region. Harness:
  `tools/fg-verify-window.sh` (bounded, foreground, self-cleaning; quiet log
  level stalls the timing-sensitive ROM poll, so it must run at log level 5 —
  the validated mvsr2fix recipe — to pace past the 0x0100 MMU-exception poll).

## Result: gate FAILED (window not clean)

Four `mismatch=1` remained in-window, NOT the predicted 0:

```
0409ecda len=5 mismatch=1 regs=0 ctrl=0 flags=1 mem=0   (no raw flag dump -> raw words EQUAL)
0409ec68 len=2 mismatch=1 regs=1 ctrl=0 flags=0 mem=0   reg[0] interp=00002614 native=00002600
0409ec6c len=4 mismatch=1 regs=0 ctrl=0 flags=1 mem=0
0409ecd4 len=6 mismatch=1 regs=0 ctrl=0 flags=1 mem=0
```

Each is immediately preceded in the log by:
```
JIT_FALLBACK op=46fc/46c6/48e7 ...
JIT_EXACT_EXEC_NOSTATS block=<pc> ...
```

## Root cause: the verify "JIT run" is NOT always nzcv

The spec assumed the JIT verify pass always produces an nzcv-layout snapshot.
False. Blocks that contain an uncompiled op fall back to **exact interpreter
execution** (`JIT_EXACT_EXEC_NOSTATS`), so their "actual" snapshot is produced
by the interpreter in **cznv** layout — the same layout as "expected".

The MVSR2 window is *dominated* by exactly these: `46fc` (MOVE #imm,SR),
`46c6` (MOVE An,SR), `48e7` (MOVEM) are not JIT-compiled, so the privileged
move-to-SR blocks all exact-exec.

Consequences for the two fixes on exact-exec blocks:

1. **Fix A is unsound.** When actual is cznv (exact-exec), `ccr_from_nzcv(actual)`
   mis-decodes a cznv word with the nzcv bit map. For 0409ecda the raw words are
   identical (no flag dump fired) yet `ccr_from_cznv(W) != ccr_from_nzcv(W)`
   because the two helpers decode the SAME bits under different layouts -> a pure
   FALSE POSITIVE (0409ecda / 0409ec6c / 0409ecd4, all flags=1).

2. **Fix B is worse than unsound — it CORRUPTS.** Pre-converting the entry to
   nzcv, then running the block via the exact interpreter (which reads cznv),
   feeds the interpreter a wrong entry CCR. 0409ec68 (`op=46c6`, MOVE A6 -> SR/CCR
   path) then computes a genuinely wrong result: reg[0] 2614 vs 2600. This is a
   NEW divergence introduced by Fix B, not a pre-existing one.

What Fix B *did* get right: on a truly-compiled MVSR2 block (0409ecda regs
divergence in the pre-fix mvsr2fix baseline was regs=1; post-fix regs=0), the
entry-flag hypothesis holds. But the window is too fallback-heavy for that to be
observable as a clean gate.

## Decision

Reverted both edits. Do NOT land Fix B+A as specified. The verifier oracle must
become execution-mode-aware before any flag-layout fix is sound.

## Corrected oracle direction (next slot)

The verifier already knows, per block, whether the JIT pass native-compiled or
exact-exec'd (it emits `JIT_EXACT_EXEC_NOSTATS`). Thread that into the compare:

- If the JIT run for the block was **exact-exec / full fallback**: actual is
  cznv. Do NOT apply Fix B entry conversion, and compare cznv-vs-cznv directly
  (`ccr_from_cznv` on both, or raw word compare). These blocks share the
  interpreter path, so they should already match without any conversion.
- If the JIT run was **natively compiled**: actual is nzcv. Apply Fix B entry
  conversion and Fix A's `ccr_from_nzcv(actual)` vs `ccr_from_cznv(expected)`.

Concretely: capture a per-block `actual_is_nzcv` boolean from the verify
compile/dispatch path (true only when the block emitted native code and ran it,
false when it routed through `JIT_EXACT_EXEC`). Gate both Fix B and the actual
side of Fix A on that boolean. Mixed blocks (some ops compiled, some fallback)
need care: if ANY op exact-exec'd, the final live regflags layout is whatever
the last executor left — treat the whole-block actual layout as cznv when the
block ended on the interpreter. Probe this before landing.

## Reusable harness added

`tools/fg-verify-window.sh RANGE [BOOT_SECONDS]` — bounded foreground verifier
over a PC window. Avoids display :99 (sibling BasiliskII). Must run at the
default log level 5 to reproduce the validated boot timing; quiet levels stall
the pure-JIT ROM poll at 0x0100xxxx. The 0x0409ec window is reached around
~10 min wall-clock (verify lines start deep, after the 0x04387 SCSI poll wall).
