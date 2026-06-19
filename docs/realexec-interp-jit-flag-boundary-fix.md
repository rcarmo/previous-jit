# IMPLEMENT-READY: real-exec interp→JIT cznv↔nzcv boundary canonicalization

Static spec (no CPU run this cycle). All anchors verified against HEAD
(`3b52435`). This is the durable RUNTIME fix for the 4 real native bugs that the
now-trustworthy exec-mode-aware oracle (`8665c74`) correctly reports:

    0409ecbe  0409ec70  0409ece0  0409ec68   (native-MVSR2-after-interp, D0 Z-bit)

It is independent of the verifier: Fix B was dropped (8665c74) precisely because
it would MASK this real seam. This spec fixes the real execution path.

## Root cause (confirmed static + 8665c74)

`regflags` is ONE shared 8-byte object {word0; x}. Two incompatible bit
assignments alias it:
- INTERPRETER (`cpufunctbl`, src/cpu, FLAGBIT_Z=14): word0 = **cznv**
  N=bit15 Z=bit14 C=bit8 V=bit0, X=x bit8.
- JIT native (uae_cpu_2026 ARM64 NZCV): word0 = **nzcv**
  N=bit31 Z=bit30 C=bit29 V=bit28, X=x bit29.

The compiled block-entry reload at `compemu_support_arm.cpp` (the
`LOAD_U64 &regflags.nzcv; LDR; MSR_NZCV_x`, "`:6256`") unconditionally treats
word0 as nzcv. When a compiled block's first flag-consuming op (canonically
MVSR2 = MOVE SR,Dn) follows an INTERPRETED predecessor that left word0 in cznv,
the reload mis-reads cznv-as-nzcv → wrong CCR baked into the destination GP
register. Exactly the 4 bugs (D0 Z-bit divergence). Boot mostly survives because
CCR is usually DEAD across boundaries; it bites only the live-CCR-at-entry
minority (MVSR2/Scc/Bcc as the first op of a compiled block).

The same seam exists symmetrically (JIT→interp: native leaves nzcv, interpreter
reads cznv) and is masked the same way.

## Design: ONE canonical boundary layout = nzcv

Make **word0 = nzcv at every inter-block boundary**. The interpreter becomes the
only cznv "island": it converts nzcv→cznv on entry, runs internally in cznv
(unchanged `cpufunctbl`), and converts cznv→nzcv on every exit. Every other
flag-writer that can return to the dispatcher already leaves nzcv (native code)
EXCEPT the JIT SR-writer helpers that call `MakeFromSR()` (which writes cznv);
those get a trailing cznv→nzcv so the invariant holds everywhere.

After this, `:6256` and ALL native codegen stay UNTOUCHED and always read a
correct nzcv word0. `jit_op_mvsr2` (already rebuilds CCR from nzcv) stays
correct. The exec-mode-aware verifier stays correct (it still sees interp=cznv,
native=nzcv per-engine).

File for all edits: `src/cpu/uae_cpu_2026/compiler/compemu_legacy_arm64_compat.cpp`
(the active AArch64 path: `exec_nostats` @1108, `execute_normal` @1248, all
`jit_op_*` SR helpers live here).

### Conversion math (verified layouts)

```c
/* word0 nzcv -> cznv (interpreter-entry: present incoming flags to cpufunctbl) */
static inline void jit_flags_nzcv_to_cznv(void) {
    uae_u32 nz = regflags.nzcv, xw = regflags.x;
    uae_u32 N=(nz>>31)&1, Z=(nz>>30)&1, C=(nz>>29)&1, V=(nz>>28)&1, X=(xw>>29)&1;
    regflags.nzcv = (N<<15)|(Z<<14)|(C<<8)|(V<<0);
    regflags.x    = (X<<8);
}
/* word0 cznv -> nzcv (boundary-exit: publish canonical flags for native reload) */
static inline void jit_flags_cznv_to_nzcv(void) {
    uae_u32 cz = regflags.nzcv, xw = regflags.x;
    uae_u32 N=(cz>>15)&1, Z=(cz>>14)&1, C=(cz>>8)&1, V=(cz>>0)&1, X=(xw>>8)&1;
    regflags.nzcv = (N<<31)|(Z<<30)|(C<<29)|(V<<28);
    regflags.x    = (X<<29);
}
```

### EDIT 1 — conversion helpers + re-entrancy-safe RAII guard

Insert immediately ABOVE `void exec_nostats(void)` (line 1108):

```c
static inline void jit_flags_nzcv_to_cznv(void) { /* ...as above... */ }
static inline void jit_flags_cznv_to_nzcv(void) { /* ...as above... */ }

/* Interpreter island: word0 is nzcv at block boundaries, cznv inside the
   interpreter. Convert in on entry, out on every exit. Depth-guarded so nested
   re-entry (verifier reentrant path, helper callbacks) converts only once. */
static int jit_interp_flag_island_depth = 0;
struct JitInterpFlagIsland {
    JitInterpFlagIsland() {
        if (jit_interp_flag_island_depth++ == 0) jit_flags_nzcv_to_cznv();
    }
    ~JitInterpFlagIsland() {
        if (--jit_interp_flag_island_depth == 0) jit_flags_cznv_to_nzcv();
    }
};
```

### EDIT 2 — guard the two interpreter functions

In `exec_nostats(void)` (1108) and `execute_normal(void)` (1248), insert as the
FIRST statement of the function body (above every existing statement, so all
return paths — bad-pc rederive, Exception(2), trace-barrier, end_block,
compile_block — are covered):

```c
    JitInterpFlagIsland _jit_flag_island;
```

Rationale: RAII covers all returns uniformly. Entry conversion runs once;
each early/late `return` runs the exit conversion via the destructor.

### EDIT 3 — SR-writer helpers leave canonical nzcv

The helpers that call `MakeFromSR()` (writes cznv) outside the interpreter
island must convert their result cznv→nzcv before returning to the dispatcher,
or a following native block reads cznv-as-nzcv. Append `jit_flags_cznv_to_nzcv();`
as the LAST statement (after the final `MakeFromSR()`) of:

- `jit_op_MakeFromSR` (1606)
- `jit_op_orsr`  (1621)  — ORI to SR/CCR
- `jit_op_andsr` (1635)  — ANDI to SR/CCR
- `jit_op_eorsr` (1649)  — EORI to SR/CCR
- `jit_op_rtr`   (2124)  — pops CCR then MakeFromSR
- `jit_op_stop`  (2137)  — loads SR then MakeFromSR

DO NOT touch `jit_op_rte` (2112): it routes through `cpufunctbl[0x4e73]` (the
interpreter), which already runs under the island via the bridge/fallback path
and ends by re-entering dispatch through normal SR rebuild; adding a second
conversion would double-encode. Validate RTE explicitly (see probe).

`jit_op_mvsr2` (2065): UNTOUCHED — it already rebuilds the SR READ directly from
nzcv and never relies on MakeSR's cznv view.

## Explicitly UNTOUCHED

- `compemu_support_arm.cpp` `:6256` block-entry NZCV reload (correct for the now-
  uniform nzcv boundary; a blind convert here would corrupt the JIT→JIT majority).
- All native codegen / `compile_block`.
- The exec-mode-aware verifier (`jit_block_verify_*`, 8665c74) — still
  per-engine-layout correct.

## Risk analysis

1. **MakeFromSR→interpreter path** (the one regression vector): if a non-island
   site leaves cznv and the interpreter is entered next, the island entry
   (nzcv→cznv) double-converts → corruption. EDIT 3 closes the known SR-writer
   sites so they leave nzcv; the island entry is then always given nzcv. Any
   remaining `MakeFromSR` reachable at a boundary must be enumerated by the
   probe before landing (grep below).
2. **Re-entrancy / verifier**: depth guard makes nested entry a no-op; the
   verifier captures/restores snapshots in whatever layout is live and compares
   per-engine, so the island is transparent to it.
3. **X bit**: carried in `regflags.x` (cznv bit8 ↔ nzcv bit29); both conversions
   move it. Validated by the MMU fast smoke (X-sensitive ADDX/SUBX/ROXL vectors).
4. **Boot survivability**: because CCR is dead at most boundaries, a wrong sign
   on the conversion would NOT obviously break boot — so correctness is gated on
   the narrow-window oracle delta (4→0), not on "boot still reaches desktop".

## Probe (one bounded run, BEFORE landing EDIT 2/3)

Goal: confirm word0 is cznv-shaped at a real interp→JIT entry for 0409ecbe, and
enumerate boundary `MakeFromSR` sites.

1. Static enumeration (no run):
   `grep -rn 'MakeFromSR' src/cpu/uae_cpu_2026/compiler/compemu_legacy_arm64_compat.cpp`
   confirm only the EDIT-3 set returns to a boundary (others are inside the
   interpreter island or followed by dispatch-through-exception).
2. Temporary stderr just before the `:6256` reload, guarded to the 0409ecbe
   compiled block in NON-verify execution: print `regflags.nzcv` and
   `((nzcv>>14)&1)` (cznv-Z) vs `((nzcv>>30)&1)` (nzcv-Z), plus whether the
   immediately-preceding dispatch was `exec_nostats`/`execute_normal`.
   PASS criterion: at this entry word0 carries the live Z in **bit14** (cznv)
   while the predecessor was the interpreter → confirms the fix is needed and
   the direction is cznv→nzcv.

## Validation (after EDIT 1–3, per-change commit)

Harness: `tools/headless-write-cfg.sh` + image
`/workspace/assets/previous/images/nextstep33-system-en-backup-20260424-063618.img`,
env `PREVIOUS_UAE2026_JIT=1 JIT_RAM=1 JIT_FPU=1 JIT_CACHE_KB=65536`.

1. Build: `make build JOBS=$(nproc)`.
2. Oracle delta (the gate), window `B2_JIT_VERIFY_BLOCKS=0x0409ec00-0x0409ed00`,
   `B2_JIT_RTE_FAULT_HANDOFF=0`:
   EXPECT the 4 real `mismatch=1` (0409ecbe/ec70/ece0/ec68) → **0**;
   0409ecda/ec6c/ecd4 stay clean (already clean under 8665c74).
   This is the pass/fail gate — it proves the real-exec flags now agree.
3. Opcode harness `tools/uae2026-opcode-harness.sh` → expect `pass=66 fail=0`.
4. MMU fast smoke `tools/uae2026-mmu-fast-smoke.sh` → expect `pass fail=0`
   (X-bit + condition-code vectors guard EDIT 1 math).
5. Default/ROM stable smoke → `desktop_reached=1 stable_reached=1` (no
   regression from the island on the ROM path).
6. RAM default-handoff stable smoke → `desktop_reached=1 stable_reached=1`.

If the oracle delta is 4→0 AND 3–6 stay green: COMMIT (one commit, EDIT 1–3
together — they are a single invariant and must not land split), push
`rcarmo-jit/main`, then re-sweep `0x04090000-0x040b0000` to confirm no new
`mismatch=1` system-wide before resuming the ranked Bcc/LINK/UNLK/RTS/JSR/PEA
barrier gap-fills.

## Landing order

1. Probe (confirm bit14-at-entry + enumerate MakeFromSR boundary sites).
2. EDIT 1–3 together → oracle window 4→0 gate → smokes → commit/push.
3. Full-region re-sweep (oracle now real-exec-true) → unblocks the barrier
   gap-fill ranking (docs/oracle-flag-layout-seam.md "Downstream").
4. Re-land the 0800 EA-mode guard on the real-exec-true oracle.
