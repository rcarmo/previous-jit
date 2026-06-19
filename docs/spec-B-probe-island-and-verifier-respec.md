# IMPLEMENT-READY: (B) probe-validated island fix + verifier re-spec

Static, HEAD `8248412`. Direction (B) per auditor: validate the real-exec island
with a real-exec D0 probe (the authoritative gate — the verifier is provably blind
here), commit gated on the probe, THEN re-spec the verifier separately.

Confirmed real-exec validity: in block 0409ecbe, op0 `40c0` MVSR2 is NOT barriered
→ compiles native (`jit_op_mvsr2`); op1 `46c6` MV2SR exec_nostats. So the
native-MVSR2-after-interp misread is a REAL bug, not a verifier artifact.

File for island edits: `compemu_support_arm.cpp` (helpers) +
`compemu_legacy_arm64_compat.cpp` (exec_nostats/execute_normal/jit_op_stop/jit_op_mvsr2).

## PART B1 — island fix, env-gated for A/B

### B1a — helpers + depth-guarded island + env gate
Insert in `compemu_support_arm.cpp` ABOVE `jit_runtime_orsr_word` (the
`live.flags_in_flags = TRASH; }` then the orsr decl):

```c
static inline bool jit_flag_island_enabled(void) {
    static int en = -1;
    if (en < 0) { const char* e = getenv("B2_JIT_FLAG_ISLAND");
                  en = (e && *e && strcmp(e,"0")==0) ? 0 : 1; }   /* default ON */
    return en != 0;
}
static inline void jit_flags_nzcv_to_cznv(void) {
    if (!jit_flag_island_enabled()) return;
    uae_u32 nz=regflags.nzcv, xw=regflags.x;
    uae_u32 N=(nz>>31)&1,Z=(nz>>30)&1,C=(nz>>29)&1,V=(nz>>28)&1,X=(xw>>29)&1;
    regflags.nzcv=(N<<15)|(Z<<14)|(C<<8)|(V<<0); regflags.x=(X<<8);
}
static inline void jit_flags_cznv_to_nzcv(void) {
    if (!jit_flag_island_enabled()) return;
    uae_u32 cz=regflags.nzcv, xw=regflags.x;
    uae_u32 N=(cz>>15)&1,Z=(cz>>14)&1,C=(cz>>8)&1,V=(cz>>0)&1,X=(xw>>8)&1;
    regflags.nzcv=(N<<31)|(Z<<30)|(C<<29)|(V<<28); regflags.x=(X<<29);
}
static int jit_interp_flag_island_depth = 0;
struct JitInterpFlagIsland {
    JitInterpFlagIsland()  { if (jit_interp_flag_island_depth++ == 0) jit_flags_nzcv_to_cznv(); }
    ~JitInterpFlagIsland() { if (--jit_interp_flag_island_depth == 0) jit_flags_cznv_to_nzcv(); }
};
```
Gate inside the helpers (not the struct) so the depth counter stays balanced when
disabled.

### B1b — full island on the live SR-writers (they MakeSR-READ + MakeFromSR-WRITE)
First statement `JitInterpFlagIsland _g;` in: `jit_runtime_orsr_word` (3799-area),
`jit_runtime_andsr_word`, `jit_runtime_eorsr_word`, and `jit_op_stop`
(compemu_legacy:2137). NOTE: jit_op_rte stays PROBE-GATED (separate, after the
direct-cpufunctbl analysis).

### B1c — island on the interpreter dispatchers
First statement `JitInterpFlagIsland _g;` in `exec_nostats` (compemu_legacy:1108)
and `execute_normal` (compemu_legacy:1248), after the opening `{`.

## PART B2 — the real-exec D0 probe (the GATE)

Add to `jit_op_mvsr2` (compemu_legacy:2065), right after `value` is computed and
before it is stored:

```c
    if (getenv("B2_TRACE_MVSR2_PROBE")) {
        static unsigned long n=0;
        if (n++ < 4000)
            fprintf(stderr, "MVSR2_PROBE pc=%08x nzcv=%08x b30Z=%u b14Z=%u value=%04x island=%d\n",
                (unsigned)m68k_getpc(), (unsigned)regflags.nzcv,
                (unsigned)((regflags.nzcv>>30)&1), (unsigned)((regflags.nzcv>>14)&1),
                (unsigned)value, jit_flag_island_enabled());
    }
```

### Run (non-verify real boot; NOT fg-verify-window — no B2_JIT_VERIFY_BLOCKS)
Reuse the boot harness; env `PREVIOUS_UAE2026_JIT=1 JIT_RAM=1 JIT_FPU=1
JIT_CACHE_KB=65536 B2_JIT_RTE_FAULT_HANDOFF=0 B2_TRACE_MVSR2_PROBE=1`, ~12min to
reach 0x0409ec region.
- Run A (island ON, default / `B2_JIT_FLAG_ISLAND=1`).
- Run B (island OFF, `B2_JIT_FLAG_ISLAND=0`).
Grep the `pc=0409ecbe` MVSR2_PROBE line in each.

### PASS criterion (authoritative — real execution)
The interpreter reference (verifier-established) for this MVSR2 is value low-byte
Z=0 (interp D0=0x2600). EXPECT:
- island OFF: `b30Z` reads garbage from cznv word0 → `value` has Z SET (0x2604).
- island ON : word0 is nzcv at entry → `b30Z` carries the true Z (0) → `value`
  Z CLEAR (0x2600), matching the interpreter.
If island ON flips this MVSR2 (and other native-MVSR2-after-interp PCs) to the
interpreter value, the REAL bug is fixed.

### Non-regression smokes (island ON), before committing
- `tools/uae2026-opcode-harness.sh` → pass=66 fail=0
- `tools/uae2026-mmu-fast-smoke.sh` → fail=0 (X-bit + CCR vectors guard the math)
- default ROM stable smoke → desktop_reached=1 stable_reached=1
- RAM default-handoff stable smoke → desktop_reached=1 stable_reached=1

If probe PASS + smokes green: COMMIT B1a-c (ONE commit, single invariant; leave the
probe instrumentation behind a getenv so it's inert by default, or drop it).

## PART C — verifier re-spec (SEPARATE commit, AFTER B lands)

The verifier is blind to the island because it snapshots inside execute_normal
(legacy:1478) AFTER the island nzcv→cznv (snapshot=cznv) and re-runs the native
block in isolation (`:6256` misreads cznv-as-nzcv). To make the oracle real-exec-
true again, co-update three points (one commit):

1. Capture at the nzcv boundary. In `jit_block_verify_entry_capture` (the snapshot
   capture), convert the captured word0 cznv→nzcv (the snapshot must represent the
   canonical boundary). Equivalent alternative: move the execute_normal island
   guard to AFTER line 1478 so the capture sees pre-island nzcv — but converting in
   the capture is cleaner and self-contained.
2. Native re-run: now reads nzcv directly (no Fix B) → correct. UNCHANGED.
3. Interp re-run: wrap the loop at `compemu_support_arm.cpp:785`
   (`(*cpufunctbl[opcode])(opcode)`) in a `JitInterpFlagIsland` so it converts the
   nzcv snapshot → cznv for cpufunctbl and back → nzcv on exit.
4. Compare: with both re-runs ending in nzcv, replace the exec-mode-aware
   `ccr_from_cznv(expected) vs ccr_from_nzcv(actual)` with `ccr_from_nzcv` on BOTH
   (both snapshots are now nzcv); drop the `actual_is_nzcv` branch (or hardwire it
   true). Re-validate the window: expect the 4 bugs → 0 (now consistent with the
   real-exec probe).

## Landing order (next active slot)
1. B1a-c + B2 probe → run A/B → PASS (island ON = interp value) + smokes green →
   COMMIT B (probe-gated), push.
2. PART C verifier re-spec → window 4→0 (now oracle-true) → COMMIT, push.
3. Full-region re-sweep 0x04090000-0x040b0000 → no new mismatch → resume the ranked
   Bcc/LINK/UNLK/RTS/JSR/PEA barrier gap-fills on the real-exec-true oracle.
