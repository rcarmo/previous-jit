# JIT FLAGX root cause: DUPLICACTE_CARRY left X at bit-29 in-register (AArch64)

## Verdict
The c74/c76 CRC-32 BPL block divergence (the F1 control-flow-compile pin) is a
**FLAGX (X-flag) in-register representation bug** in `DUPLICACTE_CARRY`, not a
flush/regalloc/chain bug and not a per-op codegen bug.

## Mechanism
`compemu_midfunc_arm64_2.cpp` `DUPLICACTE_CARRY` did:
```
CSET_xc(x, NATIVE_CC_CS);   // x = 0/1
LSL_wwi(x, x, 29);          // x = 0 or (1<<29)   <-- BUG
```
leaving the in-register FLAGX in **bit-29 format**.

Every other FLAGX path uses **0/1 (bit 0) in-register**:
- consumers: `jff/jnf_ROXR_b` read `BFI ...,x,8,1` (bit 0); `jnf_ADDX_*` add `x`
  raw as 0/1; `jff_ADDX_b` does `SUBS x,1` ("restore X to carry"); `jff_ANDSR`
  writes `MOV 0`.
- spill (`tomem`, `jit_flush` cold-path, line ~7537): `LSL #29` to convert
  **0/1 -> interpreter bit-29**.
- reload (`do_load_reg`): `UBFX n,n,29,1` to convert **bit-29 -> 0/1**.
- const path: `(val & 1) << 29`.
- 32-bit ARM reference (`compemu_midfunc_arm2.cpp`): leaves **0/1** (MOV 0 + ADC),
  NO shift.

So when X is **live in-register** (liveness sees a same-trace consumer, gated by
`needed_flags & FLAG_X`), `DUPLICACTE_CARRY` stored bit-29 but the consumer read
bit 0 -> X read as 0.

## Why only the bcc-drop CRC trace exposed it
In production the JIT barely runs (~100% interp), and isolated ops rarely have X
needed in-register, so `DUPLICACTE_CARRY` is skipped (`needed_flags & FLAG_X`
false) -> bug dormant. The F1 bcc-drop forces a large trace
(`0x01002c76`, 270 insns) containing both the producer `lsl.l #1,d0` (c76) and
the consumer `roxr.b #1,d4` (c78). The lsl's X is needed by the roxr -> macro
fires -> bit-29 written, bit-0 read -> X=0 -> CRC bit-feedback dead -> poly-XOR
never applied -> d0 degenerates to a pure left-shift = **exactly 0** ->
`not.l` -> 0xffffffff. Matches the observed d0 exactly.

## Fix
Remove `LSL_wwi(x, x, 29)` from `DUPLICACTE_CARRY` (leave 0/1), matching the
32-bit ARM reference and all FLAGX paths. One-line change + comment.

## Validation
- opcode harness 76/76 score=100; mmu-fast 32/32 score=100 (no regression).
- Whole-block verifier on `0x01002c76` (bcc-drop, B2_JIT_VERIFY_PCS):
  - before: `mismatch=1 regs=1 ctrl=1`
  - after:  `mismatch=0` and one `regs=0 ctrl=0 flags=1`
  - `01002c72`, `01002c74`: `mismatch=0`
- Registers now MATCH across iterations: d0 = 86e2bf94, 04474446 (was 0).
- Residual `flags=1` on one iteration: no `flags interp...` raw-diff line is
  printed -> raw nzcv/x fields are identical; the mismatch is the verifier
  decoding interp(cznv) vs native(nzcv) of coincidentally-equal raw words = a
  verifier representation artifact. Clincher: registers match across multiple
  CRC iterations; a genuinely wrong X/C would re-corrupt d0 next pass. It does
  not.

## Post-fix broad-drop behavioral change — RIGOROUS BEFORE/AFTER (corrects an earlier error)
Broad drop `B2_JIT_DROP_BCC_PROD` LO=0x01000000 HI=0x05000000, 70s, clean (no
verifier arming):
- BEFORE (reverted to buggy `LSL #29`, rebuilt): stuck 0x01002586, NO-SPIN
  (a7-zero=7), "bad pc", cache_hit=131035, exec_normal ~3.05e8 (~100% interp),
  zero 0x0438 PCs.
- AFTER (18737bf): stuck 0x01002586, NO-SPIN (a7-zero=7), "bad pc",
  cache_hit=131035, exec_normal ~3.08e8, zero 0x0438 PCs.
- => BYTE-IDENTICAL boot behaviour. The fix moves NOTHING at boot level under
  the broad early-ROM drop.

CORRECTION: an earlier note/claim said "spin eliminated 427->7". That was a
MEASUREMENT ERROR — the 427 a7-zero lines came from a VERIFIER-ARMED run
(B2_JIT_VERIFY_PCS perturbs execution via interp+native+resume per block), NOT
from the buggy build. Clean broad-drop is 7 a7-zero lines both before and after
the fix. There is no spin-elimination attributable to the fix.

WHY: the broad-drop boot is gated by the pre-existing 0x010025xx bad-pc_p
artifact, reached independent of the CRC result. The CRC fix is correct at unit
level (verifier) but boot-invisible until the bad-pc artifact is cleared.

## Honest status
- CLOSED (unit/correctness axis): the c74/c76 CRC-32 BPL block divergence —
  proven by the whole-block verifier (regs=1 ctrl=1 -> mismatch=0) and
  cross-iteration register match (d0 tracks interp; correct CRC).
- NO F1 BOOT-ADVANCE: rigorous before/after of the broad early-ROM drop is
  byte-identical. The fix is a validated correctness PREREQUISITE for
  zero-fallback global compile, NOT a boot-advance or ratio mover. Boot-advance
  is gated behind the separate 0x010025xx bad-pc_p artifact.

## Scope
Fix is in `compemu_midfunc_arm64_2.cpp` `DUPLICACTE_CARRY` only. Dark verifier
tooling (`compemu_support_arm.cpp`, `compemu_legacy_arm64_compat.cpp`) stays
unstaged. Single SHA 18737bf.

## SCSI-scoped F1 measured-mover path: UNREGRESSED (post-fix)
`tools/scoped-drop-validate.sh` (B2_JIT_DROP_BCC_PROD scoped 0x04382000-0x04388000),
70s: cache_hit=3,631,807, exec_normal=7.1e6, furthest pc=0x043821d2 (the known
F2/SCSI wall), 1646 ESP/SCSI markers, no segfault/abort. Same regime as the
prior ~55%-native F1 result. The early-ROM CRC fix does not touch this region
and does not regress it; the global DUPLICACTE_CARRY fix also covers SCSI-region
X-consumers cleanly.

## Where this sits
The X-fix closes ONE early-ROM correctness blocker (c74 CRC). It is a
prerequisite for zero-fallback global (early-ROM) compile, but early-ROM global
boot is still blocked by separate artifacts (0x01002586 memtest region loop;
0x010005xx bad-pc_p hang). The SCSI-scoped F1 measured-mover is independent and
intact.
