# Fix spec: c74 terminal-Bcc flag-layout seam (REAL-EXEC, not verifier)

Status: SPEC — ready to apply + verify on next CPU slot. Static prep, no build.
HEAD anchor: `79991da` (after the c74 single-step narrowing).

This is the **real-execution** manifestation of the gating seam recorded in
`docs/oracle-flag-layout-seam.md` ("Downstream … the same seam could affect
normal execution"). The verifier fix (Fix B) is necessary but does NOT fix this:
the boot hang at `0x0100254e/2568` is a *runtime* wrong-branch, independent of the
oracle compare. This spec fixes the runtime path (the seam doc's "Fix C — the REAL
exact-exec boundary").

## One-line root cause

A JIT-compiled `Bcc` reads its condition from `jit_regflags.nzcv` (ARM64 NZCV,
N=bit31), but the most recent flag writer before it was an **interpreter
fallback** (`cpufunctbl[opcode]`) that wrote the architectural CCR into the
**legacy** `regflags.cznv` (N=bit15). Nothing converts legacy→JIT at that
intra-batch interp→JIT transition, so the compiled `Bcc` evaluates **stale**
`jit_regflags` and takes the wrong direction.

## The two layouts on shared CCR (confirmed by source)

Two distinct flag objects, two incompatible bit assignments:

| field | object / TU | N | Z | C | V | X |
|-------|-------------|---|---|---|---|---|
| `regflags.cznv` | legacy interp, `src/cpu/{m68k,newcpu}.h`, populated by `build_cpufunctbl()` | 15 | 14 | 8 | 0 | `regflags.x` bit 8 |
| `jit_regflags.nzcv` | JIT TU (`uae_cpu_2026`, aarch64), the renamed `regflags` | 31 | 30 | 29 | 28 | `jit_regflags.x` bit 29 |

`cpufunctbl` handlers write `regflags.cznv`. The compiled JIT reads/writes
`jit_regflags.nzcv` (via `make_flags_live()` → `FLAGTMP.mem == &regflags.nzcv`,
which in the JIT TU aliases `jit_regflags`).

## EXACT inverting bit (oracle-grounded)

From the c74 single-step (commit `79991da`):

```
sr interp=2710 native=2708 / native=2718
   low byte: interp 0x10 (X=1,N=0)   native 0x08 (N=1) / 0x18 (X=1,N=1)
```

The bit that differs across BOTH native variants (`0x08` and `0x18`) vs interp
(`0x10`) is **N (negative)**:

- Architectural N (interpreter, `regflags.cznv` **bit 15**) = **0**.
- JIT-consumed N (`jit_regflags.nzcv` **bit 31**, materialized by
  `make_flags_live()`/`raw_reg_to_flags()` into the ARM N flag) = **1** (stale,
  left by a prior native op).

The c74 terminal `Bcc` tests a predicate that depends on N (BPL/BMI, or
BGE/BLT/BGT/BLE via N^V). Stale N=1 → the live `compemu_raw_jcc_l_oponly(cc)`
takes MI/LT where the interpreter takes PL/GE → control routes into the
`0x0100254e ↔ 0x01002568` LED-spin live-lock.

**The inverting bit is N; the inverting mechanism is a missing legacy(cznv,bit15)
→ JIT(nzcv,bit31) conversion at the interpreter-fallback → compiled-Bcc
boundary.** (X also mis-crosses — legacy X@bit8 vs JIT X@bit29 raw-copied — but
`Bcc` never tests X, so X is a *symptom indicator*, not the branch flipper. See
Secondary fix.)

## The conversion SITE (where the bug is, where the fix goes)

The bridge already has the correct converters and uses them at the **outer**
boundary only:

- `Uae2026BridgeCznvLegacyToJit()` / `Uae2026BridgeCznvJitToLegacy()`
  (`uae2026_jit_bridge.cpp:105-131`).
- Applied once per `Uae2026JitBridgeCompileExecute()` call at entry
  (`:1284`) and exit (`:1602/1605`).

The gap: **intra-batch** interp↔JIT transitions inside
`m68k_do_compile_execute()` are NOT covered. After every interpreter-fallback
op, the JIT only re-derives PC:

- `Uae2026JitCanonicalizePcAfterFallback()`
  (`compemu_support.cpp:253`) sets `regs.pc/pc_p/pc_oldp` and **touches no
  flags**.
- Call sites that run a fallback then canonicalize PC but never resync flags:
  - `compemu_legacy_arm64_compat.cpp:1218` (`exec_nostats` trace loop)
  - `compemu_legacy_arm64_compat.cpp:1515` (`execute_normal` trace loop)
  - `compemu_support_arm.cpp:6819` (emitted native-block barrier/fallback path)
  - exact-interpreter handlers that call `cpufunctbl[...]` directly:
    `jit_op_rte` (`compat:2126`), `jit_op_rtr`, and siblings.

## Fix (minimal, root-caused — convert flags at the fallback boundary)

Make the post-fallback canonicalizer ALSO convert the legacy CCR the interpreter
just produced into the JIT layout, so a following compiled `Bcc`/`Scc`/`addx`
sees the same CCR. This is the symmetric partner of the existing PC canonicalize.

### EDIT 1 — bridge: expose a one-call legacy→JIT flag resync

`src/cpu/uae2026_jit_bridge.cpp` (near the existing converters, ~`:131`):

```c
/* Resync the JIT flag struct from the legacy interpreter flag struct.
 * Call after any cpufunctbl[] fallback that ran inside a JIT batch, so a
 * following compiled Bcc/Scc/addx reads the CCR the interpreter just left. */
extern "C" void Uae2026BridgeSyncFlagsLegacyToJit(void)
{
    jit_regflags.nzcv = bridge_cznv_legacy_to_jit(regflags.cznv);
    jit_regflags.x    = bridge_cznv_legacy_to_jit(             /* X@8 -> X@29 */
                            (regflags.x & (1u << 8)) ? (1u << 8) : 0u) >> 1
                        | 0u; /* see Secondary fix for the clean X mapping */
    /* Minimal correct X: legacy X lives at bit 8, JIT X at bit 29 (==C). */
    jit_regflags.x = ((regflags.x >> 8) & 1u) << 29;
}
```

(Keep only the last `jit_regflags.x = …` line; the comment block above documents
why a raw `jit_regflags.x = regflags.x` is wrong. The two bridge entry/exit sites
that currently raw-copy `.x` should adopt the same bit-mapped form — Secondary
fix.)

### EDIT 2 — fold the resync into the post-fallback canonicalizer

`src/cpu/uae_cpu_2026/compiler/compemu_support.cpp:253`,
`Uae2026JitCanonicalizePcAfterFallback()`, append after the PC/pc_p rebuild:

```c
    /* The fallback ran an interpreter handler that wrote the architectural CCR
     * into the legacy regflags.cznv layout (N=15,Z=14,C=8,V=0). The JIT reads
     * flags from jit_regflags.nzcv (N=31,Z=30,C=29,V=28). Convert now so a
     * following compiled Bcc/Scc evaluates the correct, just-produced CCR
     * instead of stale JIT flags. */
    extern "C" void Uae2026BridgeSyncFlagsLegacyToJit(void);
    Uae2026BridgeSyncFlagsLegacyToJit();
```

This single helper now fixes all three fallback boundaries (the two trace loops
at `compat:1219/1516` and the emitted native-block path at
`support_arm.cpp:6819`), because all three already call the canonicalizer.

### Scope guard
- Convert **only** legacy→JIT here (the fallback just wrote legacy). The reverse
  (JIT→legacy before an interpreter op reads `cctrue`/`regflags.cznv`) is already
  handled when the JIT block exits to the bridge; if a probe shows an interp op
  reading stale legacy flags mid-batch, add the symmetric
  `Uae2026BridgeSyncFlagsJitToLegacy()` at the JIT→interp entry, but do NOT add
  it speculatively.
- Do not touch the trace-barrier `current_is_bcc` change yet — it stays a barrier
  until this flag fix lands and the c74 block proves clean.

## Secondary fix (X-bit position, not the Bcc flipper)

`uae2026_jit_bridge.cpp` lines `217`, `1285`, `1603` do `jit_regflags.x =
regflags.x` raw. Legacy X is at bit 8; JIT X at bit 29. Replace each with:

```c
jit_regflags.x = ((regflags.x >> 8) & 1u) << 29;   /* legacy X@8 -> JIT X@29 */
```

and the reverse at `:1604`-style JIT→legacy sites:

```c
regflags.x = ((jit_regflags.x >> 29) & 1u) << 8;   /* JIT X@29 -> legacy X@8 */
```

This removes the `0x10`-vs-`0x18` X discrepancy in the c74 SR dump. It does not
change `Bcc` direction (X is not a `Bcc` predicate) but is required for
correctness of ADDX/SUBX/ROXd and for a clean oracle.

## Validation (probe first, then real-exec lever)

1. **Single-step probe at the c74 Bcc (verifier-independent, the oracle pin).**
   Temporary stderr in `Uae2026JitCanonicalizePcAfterFallback()` (or a gated
   `B2_JIT_TRACE_FLAG_SEAM=1`) printing, at `before_pc == 0x01002c74`’s terminal
   Bcc execution:
   ```
   legacy.cznv=%08x -> N15=%d Z14=%d C8=%d V0=%d   jit.nzcv=%08x N31=%d Z30=%d C29=%d V28=%d
   ```
   PRE-FIX expectation: `N15=0` but `jit N31=1` (stale) at the failing entry —
   this is the inversion. POST-FIX expectation: `N31==N15`, `Z30==Z14`,
   `C29==C8`, `V28==V0` at every c74 hit.

2. **Cheap early-ROM real-exec lever (the decisive boot delta).**
   `B2_JIT_VERIFY_BLOCKS=0x01002500-0x01002d00` (~90–120s, no 13-min kernel
   wait). PRE-FIX: `0x0100254e ↔ 0x01002568` LED-spin live-lock (~860 toggles,
   never leaves `0x0100xxxx`). POST-FIX PASS GATE: boot advances **past
   `0x01002cb4`** into the `0x0409xxxx` kernel range (the LED-spin lines collapse
   to ~0), proving the compiled Bcc now takes the interpreter direction.

3. **Oracle mismatch=0, ABSOLUTE FAIL rule.** Re-run the narrow MVSR2 window
   `0x0409ec00-0x0409ed00` and the c74 block: any currently-passing block that
   flips to `mismatch=1` ⇒ STOP + revert (a real divergence was masked). The c74
   block’s own `regs/ctrl/flags` mismatch must drop from `1/1/1` to `0/0/0`.

4. **No-regression real-exec.** Full default/ROM stable smoke must keep
   `desktop_reached=1 stable_reached=1`; opcode harness `pass=65 fail=0`.

If all clean: commit the flag-seam fix (per-edit scope), then — and only then —
re-attempt the `current_is_bcc` trace-barrier drop (`docs/gapfill-spec-bcc-native-continuation.md`),
which should now produce the real block-count drop without the boot regression.

## Why this unblocks the ranked barriers

`Bcc`/`Scc`/`LINK`/`UNLK`/`RTS`/`JSR`/`PEA` continuation all end blocks whose
predicate or frame correctness depends on CCR crossing the interp↔JIT boundary.
With the per-fallback conversion in place, compiled terminators read the correct
just-produced CCR, so the gap-fill barriers can be landed and gate-validated
(mismatch=0) one by one. This is the prerequisite the seam doc named as gating
"ALL flag-touching JIT coverage".
