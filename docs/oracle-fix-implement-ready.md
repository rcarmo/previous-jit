# IMPLEMENT-READY: oracle Fix B + Fix A, exact diffs (+ corrected :6256 analysis)

Builds on docs/oracle-flag-layout-seam.md and
docs/oracle-seam-direction-and-real-exec-finding.md.
All anchors verified against HEAD source. No CPU run performed.

## Exact layouts (verified)

INTERP cznv (src/cpu/m68k.h, struct {unsigned int cznv; unsigned int x;}):
  N=cznv bit15, Z=cznv bit14, C=cznv bit8, V=cznv bit0, X=x bit8
JIT nzcv (uae_cpu_2026/m68k.h ARM block, struct {uae_u32 nzcv; uae_u32 x;}):
  N=nzcv bit31, Z=nzcv bit30, C=nzcv bit29, V=nzcv bit28, X=x bit29
Both are the SAME shared 8 bytes: word0 (cznv|nzcv) + word1 (x).

Architectural CCR (m68k SR low 5 bits): X=bit4 N=bit3 Z=bit2 V=bit1 C=bit0.

## Conversion math (cznv -> nzcv), used by Fix B

    uae_u32 cz = regflags.nzcv;   /* word0 currently holds the interp cznv value */
    uae_u32 xw = regflags.x;
    uae_u32 N=(cz>>15)&1, Z=(cz>>14)&1, C=(cz>>8)&1, V=(cz>>0)&1, X=(xw>>8)&1;
    regflags.nzcv = (N<<31)|(Z<<30)|(C<<29)|(V<<28);
    regflags.x    = (X<<29);

## FIX B (verifier): convert entry to nzcv for the JIT run ONLY

File: src/cpu/uae_cpu_2026/compiler/compemu_support_arm.cpp
Function: jit_block_verify_run()
Anchor: the SECOND `jit_block_verify_snapshot_restore(&jit_block_verify_entry_state);`
(the one immediately followed by `jit_block_verify_compile_active = true;`).
The FIRST restore (followed by the cpufunctbl interp loop) is LEFT UNTOUCHED —
the interp run must keep the cznv entry it expects.

Replace:
```
    jit_block_verify_snapshot_restore(&jit_block_verify_entry_state);
    regs.spcflags = 0;
    InterruptFlags = 0;
    jit_block_verify_compile_active = true;
```
with:
```
    jit_block_verify_snapshot_restore(&jit_block_verify_entry_state);
    /* Fix B: the entry snapshot was captured in execute_normal (interpreter,
       cznv/bit14 layout). The compiled block's entry reload (MSR_NZCV_x of
       regflags.nzcv) reads word0 as nzcv/bit30. Re-encode the live entry CCR
       cznv->nzcv so the JIT run starts from the SAME architectural flags the
       interp run used. Interp run above is left in cznv (it reads cznv). */
    {
        uae_u32 cz = regflags.nzcv, xw = regflags.x;
        uae_u32 N=(cz>>15)&1, Z=(cz>>14)&1, C=(cz>>8)&1, V=(cz>>0)&1, X=(xw>>8)&1;
        regflags.nzcv = (N<<31)|(Z<<30)|(C<<29)|(V<<28);
        regflags.x    = (X<<29);
    }
    regs.spcflags = 0;
    InterruptFlags = 0;
    jit_block_verify_compile_active = true;
```

## FIX A (verifier): compare architectural CCR, not raw word0

File: same. Function: jit_block_verify_compare().
After Fix B, expected(interp) flags are cznv and actual(JIT) flags are nzcv, so
raw `flags.nzcv` differ by layout even when architecturally equal. Compare CCR.

Add two static helpers above jit_block_verify_compare():
```
static inline uae_u16 ccr_from_cznv(uae_u32 cz, uae_u32 xw) {
    return (uae_u16)((((xw>>8)&1)<<4)|(((cz>>15)&1)<<3)|(((cz>>14)&1)<<2)|(((cz>>0)&1)<<1)|((cz>>8)&1));
}
static inline uae_u16 ccr_from_nzcv(uae_u32 nz, uae_u32 xw) {
    return (uae_u16)((((xw>>29)&1)<<4)|(((nz>>31)&1)<<3)|(((nz>>30)&1)<<2)|(((nz>>28)&1)<<1)|((nz>>29)&1));
}
```
Replace:
```
    bool flags_mismatch = expected->flags.nzcv != actual->flags.nzcv || expected->flags.x != actual->flags.x;
```
with:
```
    /* expected = interp (cznv); actual = JIT (nzcv). Compare the 5 m68k CCR bits. */
    bool flags_mismatch =
        ccr_from_cznv(expected->flags.nzcv, expected->flags.x) !=
        ccr_from_nzcv(actual->flags.nzcv,   actual->flags.x);
```
(Optionally also gate the `if (... flags.nzcv != ...)` diagnostic print on the
same CCR compare so the dump only fires on real CCR divergence.)

Expected after B+A: narrow MVSR2 window 0x0409ec00-0x0409ed00 -> 0409ecbe /
0409ec70 intermittent mismatch=1 goes to 0; 0409ecda stays regs=0 and its
flags residual clears too (now CCR-compared).

## :6256 — DO NOT blindly edit (corrected)

compemu_support_arm.cpp:6256 emits, per compiled block entry:
```
LOAD_U64(REG_WORK1,&regflags.nzcv); LDR_wXi(REG_WORK2,...); MSR_NZCV_x(REG_WORK2);
```
This is CORRECT for the common JIT->JIT path (word0 already nzcv). A blind
cznv->nzcv conversion here would CORRUPT that majority case. So :6256 stays.

The real-exec seam exists only at interp->JIT boundaries (predecessor ran in
execute_normal/cznv, successor is a compiled block reading nzcv). The correct
real-exec fix canonicalizes word0 cznv->nzcv ONCE when leaving the interpreter,
not at every block entry. Candidate site: the execute_normal return/handoff to
JIT dispatch (and any exact-exec interpreter-op site that leaves flags live).
This is probe-gated (below) and is a SEPARATE change from Fix B+A.

## Probe (one bounded run, after Fix B+A land, next CPU slot)

Goal: confirm whether real execution already canonicalizes at interp->JIT, or
the boundary fix is needed.
- Window 0x0409ec00-0x0409ed00, gated to block 0409ecbe.
- Temporary stderr just before the :6256 reload (guard: only when about to run
  the 0409ecbe compiled block in NON-verify execution): print regflags.nzcv and
  whether ((nzcv>>14)&1) (cznv-Z) vs ((nzcv>>30)&1) (nzcv-Z) carries the live Z.
- If word0 is cznv-shaped at a real interp->JIT entry: implement the handoff
  canonicalization. If already nzcv: the seam is verifier-only and Fix B+A
  fully closes it.

## Landing order (next CPU slot, per-change commits)
1. Fix B + Fix A together (verifier honest) -> re-verify 0x0409ec00-0x0409ed00.
2. Re-land the 0800 EA-mode guard (docs/gapfill-spec-immediate-bitop-0800.md)
   -> re-sweep 0x04090000-0x040b0000, expect no new mismatch=1 (oracle now CCR-true).
3. Run the probe; if confirmed, add interp->JIT handoff canonicalization.
4. Then spec/land the ranked Bcc/LINK/UNLK/RTS/JSR/PEA barrier gap-fills.

---

# ONE-SHOT CHECKLIST (next active slot = build -> verify -> commit, no re-derivation)

All anchors verified unique against HEAD (compemu_support_arm.cpp):
- line 698  `static void jit_block_verify_compare(...)`  (helper insert point: just ABOVE this)
- line 706  `bool flags_mismatch = expected->flags.nzcv != actual->flags.nzcv || expected->flags.x != actual->flags.x;`  (Fix A; 1 occurrence)
- line 790  JIT-run `jit_block_verify_snapshot_restore(&jit_block_verify_entry_state);` (uniquely followed by `jit_block_verify_compile_active = true;` at 793) (Fix B insert AFTER 790)
- line 773  interp-run restore (followed by `for (int i...` at 776) -> DO NOT TOUCH
- :6256 NZCV reload -> DO NOT TOUCH

## Edit 1 (Fix A helpers) — insert immediately ABOVE line 698
```
static inline uae_u16 ccr_from_cznv(uae_u32 cz, uae_u32 xw) {
    return (uae_u16)((((xw>>8)&1)<<4)|(((cz>>15)&1)<<3)|(((cz>>14)&1)<<2)|(((cz>>0)&1)<<1)|((cz>>8)&1));
}
static inline uae_u16 ccr_from_nzcv(uae_u32 nz, uae_u32 xw) {
    return (uae_u16)((((xw>>29)&1)<<4)|(((nz>>31)&1)<<3)|(((nz>>30)&1)<<2)|(((nz>>28)&1)<<1)|((nz>>29)&1));
}
```

## Edit 2 (Fix A compare) — replace line 706
```
    bool flags_mismatch =
        ccr_from_cznv(expected->flags.nzcv, expected->flags.x) !=
        ccr_from_nzcv(actual->flags.nzcv,   actual->flags.x);
```

## Edit 3 (Fix B entry convert) — insert AFTER line 790 (JIT-run restore), before `regs.spcflags = 0;`
```
    {   /* entry snapshot is interpreter cznv(bit14); JIT entry-reload reads nzcv(bit30) */
        uae_u32 cz = regflags.nzcv, xw = regflags.x;
        uae_u32 N=(cz>>15)&1, Z=(cz>>14)&1, C=(cz>>8)&1, V=(cz>>0)&1, X=(xw>>8)&1;
        regflags.nzcv = (N<<31)|(Z<<30)|(C<<29)|(V<<28);
        regflags.x    = (X<<29);
    }
```

## Build
```
make build JOBS=$(nproc)
```

## Verify 1 — oracle honest (narrow MVSR2 window)
Foreground, bounded, self-cleaning; reuse the harness:
`tools/headless-write-cfg.sh` + image
`/workspace/assets/previous/images/nextstep33-system-en-backup-20260424-063618.img`,
env `PREVIOUS_UAE2026_JIT=1 JIT_RAM=1 JIT_FPU=1 JIT_CACHE_KB=65536
B2_JIT_RTE_FAULT_HANDOFF=0 B2_JIT_VERIFY_BLOCKS=0x0409ec00-0x0409ed00`.
EXPECT (this window):
- 0409ecbe, 0409ec70: intermittent `mismatch=1` -> **0** (regs N-bit divergence gone)
- 0409ecda: `mismatch=0` (regs already 0; flags now CCR-compared, residual gone)
- total `mismatch=1` in window -> **0**
If clean: COMMIT Fix B+A (one commit), push rcarmo-jit/main.

## Verify 2 — re-land 0800 guard (separate commit)
Re-apply the EA-mode guard (docs/gapfill-spec-immediate-bitop-0800.md):
`compemu_legacy_arm64_compat.cpp` predicate ->
`((opcode & 0xff00u) == 0x0800u) && ((opcode & 0x0038u) != 0x0000u)`.
Build, re-sweep `B2_JIT_VERIFY_BLOCKS=0x04090000-0x040b0000`.
EXPECT (honest, corrected):
- `op=0800` trace-barrier skips -> **0** (BTST compiles through)
- total skipped count ~**unchanged** (BTST blocks rebind to the trailing Bcc
  barrier) — the −3119 is NOT expected; the win is BTST-no-longer-a-barrier
- **NO new `mismatch=1`** (oracle now CCR-true, so the prior MVSR2 false
  positives are gone) -> this is the gate that lets the 0800 guard COMMIT
If clean: COMMIT 0800 guard, push.

## Verify 3 (optional, probe) — real-exec interp->JIT boundary
Only after Verify 1+2 land. Bounded probe at the :6256 reload gated to 0409ecbe
(non-verify execution) to confirm whether word0 is cznv-shaped at a real
interp->JIT entry. If yes: add cznv->nzcv canonicalization at the
execute_normal->JIT handoff (NOT at :6256). Separate commit.

## NOTE
No CPU run this static cycle; sibling BasiliskII holds the slot (Xvfb :99 +
/workspace/tmp/basiliskii-rom-harness are theirs — leave intact).
