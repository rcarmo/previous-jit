# Gap-fill spec: stop trace-barrier fallback for register-direct immediate bit-ops (0x08xx)

Status: SPEC (ready to implement + verify when a CPU slot is available)
Author context: HEAD 0990ac8 (after MVSR2 CCR-layout fix). Static cycle, no run.

## Problem (the forbidden interpreter-fallback surface)

In window `B2_JIT_VERIFY_BLOCKS=0x04090000-0x040b0000`, 14935 blocks report
`skipped=trace_barrier` (interpreter-only — the forbidden fallback). The single
most frequent terminal op is:

```
3119  op=0800   BTST #imm,D0   (register-direct, NO memory side effects)
```

`op=0800` decodes as `0000 1000 00 000 000` = BTST, EA mode field (bits 5-3) =
`000` (Dn-direct), reg `000` (D0). The bit number is an immediate extension word;
the only effect is setting the Z flag. There is **no memory access** and **no
control-flow change**.

## Root cause

The trace builder in `execute_normal_*` bails the whole block to the interpreter
whenever the terminal op matches an over-broad immediate-bitop predicate.

- File: `src/cpu/uae_cpu_2026/compiler/compemu_legacy_arm64_compat.cpp`
- Line ~1538:

```c
/* Immediate bit operations (BTST/BCHG/BCLR/BSET, 0x08xx) have
 * extension-word + memory side effects that are still unsafe across
 * native continuation; keep the trace in the interpreter. */
const bool current_is_immediate_bitop = ((opcode & 0xff00u) == 0x0800u);
```

This treats every `0x08xx` immediate bit-op as a barrier, including the
register-direct forms (`BTST/BCHG/BCLR/BSET #imm,Dn`, EA mode `000`) which have
no memory side effects. Compiled handlers already exist for all four:

```
op_800_0_comp_ff (2048, BTST)   op_840_0_comp_ff (2112, BCHG)
op_880_0_comp_ff (2176, BCLR)   op_8c0_0_comp_ff (2240, BSET)
```

(plus the `_nf` variants), confirmed in `compstbl.cpp`.

## Fix (one-line predicate narrowing, scoped + safe)

Restrict the barrier to **memory-EA** immediate bit-ops only; let register-direct
forms compile natively.

```c
/* Immediate bit operations (BTST/BCHG/BCLR/BSET, 0x08xx).
 * Only the memory-EA forms carry extension-word + memory side effects that are
 * still unsafe across native continuation.  Register-direct forms
 * (BTST/BCHG/BCLR/BSET #imm,Dn, EA mode 000) only touch Dn and the Z flag and
 * have compiled handlers (op_800/op_840/op_880/op_8c0), so let the trace
 * continue through them. */
const bool current_is_immediate_bitop =
    ((opcode & 0xff00u) == 0x0800u) && ((opcode & 0x0038u) != 0x0000u);
```

Rationale: bits 5-3 (`0x0038`) are the EA mode. `== 0x0000` is Dn-direct (safe);
any other mode is a memory/PC-relative EA (kept as a barrier, unchanged).

No new handler is required — only the predicate changes.

## Safety argument

- Register-direct `BTST #imm,Dn`: sets Z only. No memory, no PC side effects
  beyond the immediate extension word, which the compiled handler consumes via
  `comp_get_iword` and accounts for in `m68k_pc_offset`.
- Register-direct `BCHG/BCLR/BSET #imm,Dn`: modify `Dn` + set Z. Still no memory,
  no control transfer. Equivalent to any other compiled ALU op for native
  continuation.
- Memory-EA forms remain barriers (predicate keeps `(opcode & 0x0038) != 0`),
  so no behavior change for the genuinely side-effecting cases.

## Verification plan (when CPU slot returns)

1. Build: `make build JOBS=$(nproc)`.
2. Oracle (narrow window that contains a block terminating in 0x0800), e.g.
   `B2_JIT_VERIFY_BLOCKS=0x0409f500-0x0409f600` (block 0409f520 ends at
   pc=0409f576 op=0800 per existing logs). Bounded, self-cleaning foreground run.
   Expect: that block flips from `skipped=trace_barrier` to a compiled verdict
   with `mismatch=0`.
3. Re-sweep `0x04090000-0x040b0000`; expect the `skipped=trace_barrier` count to
   drop by approximately the 0800 share (~3119), with **no** new `mismatch=1`.
4. If clean: commit (per-handler/per-barrier scope) and push to rcarmo-jit/main.

## Expected impact

- Removes ~3119 interpreter-fallback block terminations in this window alone
  (the largest single contributor), directly shrinking the zero-fallback gap.
- Lowest-risk first step: a register-only predicate narrowing with existing
  handlers, no codegen, no memory semantics touched.

## Follow-on candidates (same trace_barrier list, by frequency)

After 0800, the next contributors are control-flow/stack ops that genuinely end
blocks and need more than a predicate tweak:
```
6706 BEQ.S / 62b8 BHI.S / 6704 / 6c9c (Bcc family ~ forward/backward branch unroll safety)
4e56 LINK / 4e5e UNLK / 4e75 RTS / 4e92 JSR(A2) / 4878 PEA
```
These require the branch-loop / stack-frame native-continuation correctness work
(separate, larger specs) and should follow the BTST predicate fix.

---

## VERIFICATION OUTCOME (implemented + tested, then reverted)

Implemented the one-line EA-mode guard and ran the oracle. Result:

- **Correct in isolation:** `op=0800` trace-barrier skips → 0; the BTST now
  compiles through (block `0409f520` `len 24→25`).
- **Impact ≠ predicted −3119:** the dominant pattern is `BTST`→`Bcc`, so blocks
  **rebind** to the trailing `Bcc` barrier (`0409f520`: `op=0800`→`op=6706`) and
  still fall back. The binding barrier is the `Bcc`, not the `BTST`. Skipped
  **block count** barely changes.
- **Oracle unreliable here:** the full-window sweep showed NEW intermittent
  `mismatch=1` on neighbor MVSR2 blocks (`0409ecbe`, `0409ec70`: D0 differs by
  the N bit, `2600↔2608`), traced to commit `0990ac8` + the verifier flag-layout
  seam (below), NOT to the BTST change.

**Decision:** reverted (unvalidatable until the oracle seam is fixed). The fix is
still correct; re-land it after the oracle fix lands.
