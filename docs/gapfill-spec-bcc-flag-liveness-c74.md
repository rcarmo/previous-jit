# Fix spec: c74 terminal-Bcc — root cause is FLAG-LIVENESS, not op codegen

Status: SPEC (decisive disasm + static codegen audit). HEAD anchor `a4795bd`.
Supersedes the falsified boundary-sync hypothesis (`oracle-flag-layout-seam-realexec-bcc.md`).

## Decisive disassembly of block 0x01002c74 (len 6) — a CRC-32 loop

```
1002c74: 7407   moveq #7,d2
1002c76: e388   lsl.l #1,d0          jff_LSL_l
1002c78: e214   roxr.b #1,d4         jff_ROXR_b   (rotate through X)
1002c7a: e219   ror.b  #1,d1         jff_ROR_b
1002c7c: b304   eor.b  d1,d4         jff_EOR_b    <- last flag-setter (N producer)
1002c7e: 6a06   bpl.s  0x1002c86     <- terminal Bcc, tests N
1002c80: 0a80 04C11DB7 eori.l #CRC32_POLY,d0
1002c86: 51ca   dbf d2,0x1002c76     (next block, still barriered)
```

Confirmed CRC-32 (`eori.l #0x04C11DB7`). Terminal **BPL.S tests N**; interp N=0
(takes branch to 2c86), JIT N=1 (falls through) → routes into the
`0x0100254e/2568` LED-spin. Inverting bit = **N**.

## Op codegen AUDITED and RULED OUT (static, arm64_2.cpp)

- **`jff_EOR_b` (line 3697)** — the N producer for the BPL: `SIGNED8_REG_2_REG`
  both operands, `EOR`, then `TST`. N = bit31 of the sign-extended XOR =
  `bit7(a)^bit7(b)` = bit7 of the result byte = **correct**. Z correct. NOT the bug.
- **`jff_ROXR_b` (line 7037)** — builds the 9-bit `{X,d7..d0}` field, duplicates,
  `LSR` by count, N via `result<<24; TST`, C/X from the shifted-out bit =
  **correct**. NOT the bug.
- Variant-handler propagation (`compemu_support_arm.cpp:5831`): `e214` → base
  handler `e010` (both ROXR.B; the inline `LSR.B` comment is stale) — mapping is
  **correct** (the shared handler re-extracts count=1/dst=d4 from the opcode).

The individual arithmetic/flag codegens produce the right N. So the runtime N=1
is NOT a wrong-N computation — it is a **stale N**: EOR.B's N was never
materialized into the live NZCV the BPL reads.

## Root cause (narrowed, decisive): flag-liveness for the force-ended terminal Bcc

Every flag-setting compiled handler is gated:
```c
dont_care_flags();
start_needflags();
   jff_EOR_b(d,s);      /* emits N/Z only if needed_flags says so */
live_flags();
end_needflags();
```
`needed_flags` comes from compile_block's backward flag-liveness pass. For block
c74 the terminal op is the BPL, which *uses* N. If the liveness pass does NOT
propagate the terminal Bcc's cc-flag use back into `needed_flags` at the EOR,
the `start/end_needflags` optimization drops N → the `compemu_raw_jcc_l_oponly`
reads **stale N** (left by an earlier op / prior block) → wrong branch.

Prime suspect — the branch-resolution JOIN_DROP (`compemu_support_arm.cpp:6866`):
```c
uae_u8 x = bi1->needed_flags;      /* fall-through successor */
... x |= bi2->needed_flags;        /* taken successor       */
if (!(x & FLAG_CZNV)) { flush_flags(); dont_care_flags(); }
```
This decides flag-liveness purely from the **successor** blocks' needs — it never
adds the branch's OWN cc-flag use. For a CRC inner loop whose successors
(`eori.l`, `dbf`) don't need CZNV, `x & FLAG_CZNV == 0` → flags dropped — even
though the BPL itself needs N THIS instant. With the Bcc barriered (today) the
block is interpreted and N is always live, so the bug is masked; compiling the
Bcc terminal exposes it. This also explains why the boundary-sync fix failed
(the flags are dropped intra-block, not mis-converted at a boundary) and why
forward-only Bcc also hung (same liveness drop).

## idx-1 discriminator (cheap, decisive — early-ROM repro)

Re-enable Bcc (full `must_end`), add a gated compile-time print in compile_block
for `block_m68k_pc == 0x01002c74`:
```
fprintf(stderr, "C74FLAGS needed_at_eor=%02x bi_needed=%02x join_x=%02x\n",
        <needed_flags at the 2c7c op>, bi->needed_flags, x);
```
Run `B2_JIT_VERIFY_BLOCKS=0x01002500-0x01002d00` ~120s.
- If `needed_at_eor & FLAG_N == 0` ⇒ **confirmed**: N is dropped before the BPL.

## Candidate fix (ready once confirmed)

The terminal conditional branch must register its cc-flag USE so liveness keeps
it. At the block-end branch resolution (support_arm:6866, before the JOIN_DROP
drop), OR-in the terminal Bcc's own use_flags so a flag-needing terminator is
never dropped:
```c
uae_u8 x = bi1->needed_flags;
... x |= bi2->needed_flags;
/* The terminal conditional branch consumes the cc flags THIS block produced;
 * its own use must keep them live regardless of successor needs. */
x |= prop[cft_map(DO_GET_OPCODE(pc_hist[blocklen-1].location))].use_flags;
if (!(x & FLAG_CZNV)) { flush_flags(); dont_care_flags(); }
```
(Exact site/representation to confirm against how per-op `needed_flags` is seeded
from `bi->needed_flags`; the principle is: a flag-using terminator pins its own
cc flags live.) Then re-test: c74 mismatch→0 + early-ROM boots past 0x01002cb4.

## Gates (unchanged, absolute)

1. Oracle mismatch=0 ABSOLUTE — no passing block flips (scope to the terminal-Bcc
   path if any regresses; do not loosen).
2. Early-ROM lever: 254e/2568 LED-spin → ~0, boot past 0x01002cb4 into 0x0409xxxx.

This is a single, root-caused liveness fix at the branch terminator — not a
per-opcode tweak, not a boundary sync. If the discriminator shows N IS already
live (unlikely given the audit), fall back to per-op single-step of the 6 ops
to find the first diverging register/flag.

---

## VALIDATION OUTCOME (idx-1 active slot) — liveness hypothesis FALSIFIED; c74 is a comparator artifact

Ran the gated per-op flag-liveness print (`B2_JIT_TRACE_C74`) with Bcc enabled,
early-ROM window. Decisive output:

```
C74 op=b304 pc=01002c7c (EOR.B) set=0f use=00 livein=1f needed=0f   <- needed INCLUDES FLAG_N(0x08)
C74 op=6a06 pc=01002c7e (BPL)   set=00 use=08 livein=1f needed=00   <- BPL use_flags = N, correct
```

**N is correctly live.** The terminal BPL's `use_flags=0x08` (N) propagates into
`liveflags`, and EOR.B's `needed=0x0f` includes N → N IS materialized. The
JOIN_DROP OR-in fix would be a **no-op**. Liveness hypothesis FALSIFIED; not shipped.

**c74 reclassified as a comparator artifact.** With Bcc enabled, c74's oracle
verdict is `mismatch=1 regs=0 ctrl=1 flags=0` — registers AND flags MATCH, only
`ctrl` (next-PC) differs. That is exactly the stale-`regs.pc` artifact recorded
earlier (native `regs.pc` = block-start, `pc_p` correct; the comparator reads the
stale base). c74's compiled execution is correct; it has been a RED HERRING.

**The per-block oracle is insufficient for this lever.** Verified the actual spin
region `0x01002400-0x01002700` (blocks `0100253e`/`01002556`): **0 real
mismatches**. The oracle shows CLEAN everywhere windowed, yet the full 780s boot
still live-locks at `0x0100254e`. So the Bcc divergence is NOT pinned to a single
mis-compiled block by the snapshot-based per-block oracle — it is either
accumulated/timing-dependent or in a block the per-block compare cannot fault
(the snapshot re-run does not reproduce the exact runtime state; the comparator
itself carries stale-pc / nzcv-cznv artifacts).

## Conclusion + strategic recommendation

Four hypotheses (unroll, regs.pc, direction, boundary-sync) and now a fifth
(terminal-Bcc liveness) have each been implemented, tested, and falsified. The
common blocker is that the **per-block snapshot oracle is artifact-prone and
cannot reliably isolate the real Bcc runtime divergence**. Continuing to mine it
yields red herrings (c74), not the cause.

The decisive next step is a **verifier-INDEPENDENT lockstep differential tracer**:
run the JIT and the interpreter in true lockstep from a common state and log the
FIRST PC where the architectural state (regs + CCR + next-PC, decoded in the
correct layout) diverges — the technique the BasiliskII sibling used to kill its
own rabbit hole. This is a dedicated tooling effort, not a per-block-oracle
hypothesis. Recommend a strategic decision before more Bcc attempts:
(A) build the lockstep tracer, or (B) accept Bcc-barriered boot (correct,
~15x slow) and redirect. `ec26050` (0800) remains the only landed real handler.
