# FINALIZED implement-ready spec — oracle compare: native side is ALWAYS nzcv

Status: **IMPLEMENT-READY, HEAD-anchored at `0b30f27`.** This is the single
deliverable for the current STATIC cycle and the exact change the next ACTIVE
slot must land as a committed, oracle-verified diff. One function, one edit, no
island, no capture-coupling, no longjmp/RAII fragility.

## Why this and not a leaf handler

The 0800 register-direct bitop guard (`a2ca746`) and every ranked
Bcc/LINK/UNLK/RTS/JSR/PEA barrier are gated on a CLEAN oracle. They cannot be
gate-validated (the auditor's own "oracle check that proves it mismatch=0" is
impossible) while the verifier mis-decodes the native flag snapshot. So the one
change that (a) is fully root-caused, (b) is a single edit, and (c) actually
drops the window mismatch count is the oracle compare fix below. It is itself a
committed code change that flips real compiled blocks to mismatch=0.

## Root cause (empirically confirmed, run `…-partc-fix-verify-074243`)

In `jit_block_verify_run` (compemu_support_arm.cpp:761) the verifier runs the
block twice from one restored entry snapshot:

- interp re-run via `cpufunctbl` (compemu_support_arm.cpp:786-789) leaves
  `regflags` in **cznv** (src/cpu interpreter, Z@bit14).
- native re-run via `pushall_call_handler` (compemu_support_arm.cpp:812) leaves
  `regflags` in **nzcv** (ARM64 JIT, Z@bit30) — **always**, INCLUDING fallback
  (`exec_nostats`/`optlev==0`/`forced_interpreter_barrier`) blocks, because the
  compiled epilogue restores nzcv after any interpreter fallback. Confirmed:
  every native snapshot in the window is `nzcv=0x40000000`.

The compare (compemu_support_arm.cpp:807, 711-712) selects the native decoder by:

```c
const bool actual_is_nzcv = !jit_block_verify_actual_exact_exec;   // line 807
...
uae_u16 act_ccr = actual_is_nzcv ? ccr_from_nzcv(actual->flags.nzcv, actual->flags.x)
                                 : ccr_from_cznv(actual->flags.nzcv, actual->flags.x);
```

`jit_block_verify_actual_exact_exec` is set `true` for fallback blocks at
compemu_support_arm.cpp:7109 (`(optlev == 0) || forced_interpreter_barrier`).
So for fallback blocks `actual_is_nzcv == false` and the verifier decodes a
**genuinely-nzcv** snapshot (`0x40000000`) with `ccr_from_cznv` → reads Z=0
instead of Z=1 → spurious `flags=1`. Window evidence:

```
13 × regs=0 ctrl=0 flags=1     detail: interp nzcv=00004000 (cznv) native nzcv=40000000 (nzcv)
```

`ccr_from_cznv(0x40000000)` Z = (0x40000000>>14)&1 = 0; `ccr_from_nzcv(0x40000000)`
Z = (0x40000000>>30)&1 = 1 = the interp's Z. The branch is the bug.

## THE EDIT (single, HEAD-anchored)

File: `src/cpu/uae_cpu_2026/compiler/compemu_support_arm.cpp`

1. **Compare decode (lines 711-712).** Native is always nzcv — drop the branch:

```c
//  uae_u16 act_ccr = actual_is_nzcv ? ccr_from_nzcv(actual->flags.nzcv, actual->flags.x)
//                                   : ccr_from_cznv(actual->flags.nzcv, actual->flags.x);
    uae_u16 act_ccr = ccr_from_nzcv(actual->flags.nzcv, actual->flags.x);
```

   Expected (interp) side stays `ccr_from_cznv` (line 710) — the cpufunctbl loop
   genuinely leaves cznv. No change to the interp re-run loop.

2. **Drop the dead parameter** to keep the contract honest (optional but
   preferred): remove `bool actual_is_nzcv` from the signature (line 702), remove
   `const bool actual_is_nzcv = ...;` (line 807) and the argument at the call
   site (line 819). Leave `jit_block_verify_actual_exact_exec` and its assignment
   at line 7109 in place ONLY if still read elsewhere; `grep` shows it is read
   only at 807 → it becomes dead and should also be removed (lines 638, 800,
   7108-7109) in the same commit. If removal risks scope-creep, leave the static
   set-but-unused and gate the cleanup to a follow-up; the decode change alone is
   sufficient to pass the gate.

No other call sites: `jit_block_verify_compare(` appears only at 702 (def) and
819 (call).

## ORACLE CHECK THAT PROVES IT (the hard gate)

Env (identical to the PART C run): default RAM/MMU boot to the MVSR2 window,
`PREVIOUS_UAE2026_JIT=1 JIT_RAM=1 JIT_FPU=1 JIT_CACHE_KB=65536`
`B2_JIT_RTE_FAULT_HANDOFF=0 B2_JIT_VERIFY_BLOCKS=0x0409ec00-0x0409ed00`.

PASS criteria (all required):
1. The **13 × `regs=0 ctrl=0 flags=1`** entries in the window go to
   **mismatch=0** → window total drops **17 → 4** (the 4 residual `regs=1` are
   Finding B / entry-boundary seam, explicitly OUT of scope here).
2. Grep proof: `grep -c 'mismatch=1 regs=0 ctrl=0 flags=1' <log>` == 0 in the
   window; `grep -c 'mismatch=0' <log>` increases by exactly the 13 blocks
   (ecbe/ec70/ece0/ec68 families) that were flags-only before.
3. Non-regression: opcode harness 75/75 score=100; MMU fast smoke 32/32
   score=100. (Both passed clean at HEAD on the naive run: `…-074246`,
   `…-074803`.)

FAIL handling: if any previously-passing block flips to mismatch=1, the interp
side is NOT uniformly cznv → STOP, do not force; that would be Finding B leaking
into the output compare and needs the entry-boundary root-cause first.

## What this unblocks (next, in order — NOT part of this commit)

1. Re-land 0800 EA-mode guard (`a2ca746`) and re-measure the skipped count now
   that the oracle is trustworthy on flag-only blocks.
2. Finding B (residual 4× regs=1): root-cause the execute_normal→block entry
   flag layout (sometimes nzcv, sometimes cznv) — candidate C, real-exec seam.
3. Spec/land ranked barriers (Bcc 6706/62b8/6704/6c9c, LINK 4e56, UNLK 4e5e,
   RTS 4e75, JSR 4e92, PEA 4878) against the now-clean oracle.
