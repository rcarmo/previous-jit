# JIT Lockstep Differential Tracer — Build-Ready Spec (Option A)

Status: **DESIGN ONLY — not built.** Ready to execute if Rui selects (A).
Canonical location: `projects/previous/docs/` (this file). A stray doc-only copy
exists under `projects/macemu/BasiliskII/` from an earlier mis-targeted commit
(`4bf0af0c`); it is harmless and will be git-rm'd from the macemu subtree at
BasiliskII's hand-off. THIS is the authoritative spec.
Target bug: pure-JIT boot hang at `0x0100254e` (CRC/Bcc-liveness region,
blocks `253e`/`2556`). Author: @previous slot deliverable. HEAD anchor `2dd64e3`.

---

## 1. Why the existing oracle is structurally blind (the thing we are replacing)

Two snapshot oracles exist today in `compemu_support_arm.cpp`:

- **Per-block** `jit_block_verify_run()` (≈ line 752): on each compiled block it
  captures the JIT's *live* entry state, runs the interpreter from that entry to
  the first `fl_end_block`, restores, compiles+runs the native block, compares.
- **Per-op** `jit_verify_pre/post()` (≈ lines 837/850, emitted at ≈ 6502): wraps
  one instrumented op; re-runs the interpreter on that single op from the
  captured pre-state and compares.

Both share **two fatal properties** for this class of bug:

1. **Re-seeding.** Each comparison reseeds the gold interpreter from the JIT's
   *own current live state*. So if the JIT drifts in block N and that wrong
   value only faults/hangs in block N+k, every per-block comparison still reads
   `mismatch=0`, because interp-replay and native-replay start from the *same
   already-drifted* state and agree. This is exactly the observed
   "clean everywhere I window, yet 780s hang at `0100254e`."

2. **Forced flag materialization.** The per-op path emits `flush(1)` *before*
   the verify callout (line ≈ 6505), and every block boundary flushes flags.
   The CRC bug is a **flag-liveness drop** (`needed_flags` drops EOR.B's N
   because the terminal Bcc's own cc-use isn't OR'd in). Instrumenting with a
   flush **materializes the very flag the bug drops**, hiding it. Snapshot
   granularity therefore cannot see an intra-block, never-materialized NZCV.

Plus the comparator carries known **layout artifacts**: stale `regs.pc`
(native `regs.pc` = block-start while `pc_p` is correct → c74 red herring),
and raw `nzcv` vs `cznv` word compares (`flag_struct.cznv` is the byte-shuffled
x86-mirror layout: N=bit7, Z=bit6, V=bit11, C=bit0; host PSTATE NZCV is
N=31/Z=30/C=29/V=28 — comparing raw words is meaningless).

**Conclusion:** we need *two continuously-independent* architectural states,
advanced in true lockstep at **instruction granularity**, compared in
**canonical M68K layout**, halting at the **first** real divergence — and the
JIT side must be observed **without any added flush**.

---

## 2. Design principle

```
  GOLD (interpreter)            DUT (JIT, real machine)
  ----------------              -----------------------
  own regs' + own RAM' copy     real regs + real RAM
  step exactly 1 m68k insn      run 1 m68k insn (native handler)
        |                              |
        +---------- compare ----------+   canonical CCR + 16 regs + next-PC + touched mem
        |
   first mismatch  -> dump both states in canonical layout, halt, exit(42)
```

- The gold side never reads DUT state after arming (no re-seed). It is seeded
  **once** when the window is first entered, then runs free.
- Comparison is **per-instruction**, not per-block, so an intra-block dropped
  flag is caught at the exact producer op.
- The DUT is observed by a **read-only, flush-free** state-dump callout
  (the "same-PC full-state-dump" family). It must not perturb liveness.

---

## 3. Non-perturbation — the hard constraint

The DUT callout must capture live NZCV **without** calling `flush()`,
`flush_flags()`, `dont_care_flags()`, or anything that mutates the
register-allocator / flag-liveness bookkeeping. Requirements:

- **Flags:** emit a raw `MRS Xn, NZCV` (host PSTATE) into a scratch GPR, then
  store that word to a fixed C global (`g_ls_dut_pstate`). This reads the *same*
  live NZCV the next op would consume. It does **not** invoke `flush_flags()`,
  so the don't-care drop under test is preserved. Add one raw encoder
  `compemu_raw_mrs_nzcv(reg)` in `codegen_arm64.h` (MRS NZCV = `0xD53B4200|Rt`)
  — pure read, no allocator state touched.
- **X flag:** the X bit is kept in `regflags.x`, materialized by the existing
  flag machinery; capture it by storing `live_flags`'s x slot only if it is
  already resident. Simplest correct option: in the window, force X to be
  materialized to memory per op via the existing `preserve_flags_before_nzcv_clobber`
  *only when X is the operand under test*; otherwise read `regflags.x`. (X is not
  the c74 producer; N is. Keep X handling minimal and document it as a known
  lower-fidelity field until needed.)
- **GP/addr regs:** store live vregs to the `regs.regs[0..15]` mirror via a
  **copy-out that does not free/anti-alias** the allocator. Use the existing
  `flush(1)` *machinery internals* only in a non-committing form — i.e. add
  `jit_ls_writeback_regs()` that, for each of D0..D7/A0..A7, if the vreg is
  resident emits `STR` to `&regs.regs[i]` **without** marking it clean/free
  (no `remove_offset`, no `f_disassociate`). Registers are not the perturbation
  risk (the bug is flags), so a plain non-freeing writeback is sufficient and
  proven safe by analogy to the existing `flush` STR path.
- **Next-PC:** do **not** read `regs.pc` (stale). Emit `compemu_raw_set_pc_i`-style
  materialization of `pc_hist[i+1].location` → guest PC = `host_to_m68k(loc)` and
  store to `g_ls_dut_nextpc`. This is the value the comparator treats as
  authoritative PC.

The callout target is a C function `jit_ls_dut_dump(uae_u32 cur_m68k_pc, uae_u32 opcode)`
that reads `g_ls_dut_pstate`, `g_ls_dut_nextpc`, the `regs.regs[]` mirror, and
`regflags.x`, packs them into the canonical vector (§5), and runs the compare (§6).

---

## 4. Hook sites (exact anchors)

### 4.1 Arming / window gate
Add env ranges parsed by the existing `jit_pc_in_env_ranges()` machinery
(`compemu_support_arm.cpp` ≈ line 517 env table):

- `B2_JIT_LOCKSTEP_PCS` — window range(s), e.g. `0x01002400-0x01002700`.
- `B2_JIT_LOCKSTEP_MAXSTEPS` — safety cap (default 200000).
- `B2_JIT_LOCKSTEP_DUMP_RADIUS` — bytes of touched-mem window to diff (default 64).

Add `static inline bool jit_lockstep_target_pc(uae_u32 pc)` next to
`jit_trace_target_pc` (≈ line 576).

### 4.2 DUT per-op dump emission
In the compile loop, in the **same `if (_verify_this_op || _trace_this_op)`
family** at ≈ 6502, add a parallel `_lockstep_this_op = jit_lockstep_target_pc(op_m68k_pc)`
branch that, **after** `comptbl[...](opcode)` compiles the op (so we capture the
post-op state) and **without** the pre-op `flush(1)`, emits:

```
  jit_ls_writeback_regs();              // non-freeing STR of resident vregs
  compemu_raw_mrs_nzcv(scratch);        // live host PSTATE, NO flush_flags
  compemu_raw_mov_l_rm? -> store scratch to &g_ls_dut_pstate
  set g_ls_dut_nextpc = host_to_m68k(pc_hist[i+1].location)
  compemu_raw_mov_l_ri(REG_PAR1, op_m68k_pc);
  compemu_raw_mov_l_ri(REG_PAR2, opcode);
  compemu_raw_call((uintptr)jit_ls_dut_dump);
  // re-init_comp(); restore comp_pc_p; (same pattern as trace branch)
```

Critical: the dump is emitted **inline mid-block**, between ops, and must
`init_comp()`/restore `comp_pc_p` like the trace path so the next op still
compiles into the same block (do **not** set `was_comp=0` unless the bug
reproduces only at block heads — keep block shape identical to an uninstrumented
run so liveness is unchanged).

### 4.3 GOLD interpreter step + compare driver
`jit_ls_dut_dump()` is the synchronization point. On each call:

1. If gold not yet armed for this window: seed gold state once
   (see §4.4), set `g_ls_armed=true`, record `g_ls_expect_pc = cur_m68k_pc`.
2. Assert `cur_m68k_pc == g_ls_expect_pc`; a control-flow mismatch here is
   itself a divergence (DUT took a different branch) → dump+halt.
3. Advance GOLD by exactly one instruction at `g_ls_gold_pc`:
   `op = get_opcode_cft_map(get_word(g_ls_gold_pc)); (*cpufunctbl[op])(op);`
   executed against the **gold shadow** (see §4.4), updating gold regs/flags/mem.
4. Build canonical vectors for GOLD and DUT (§5), compare (§6).
5. Set `g_ls_expect_pc = g_ls_dut_nextpc` (DUT-authoritative next PC) and
   `g_ls_gold_pc = ` gold's own computed next PC; if they differ → divergence.

### 4.4 Gold shadow state
Two viable implementations, cheapest first:

- **(Cheap, recommended first) single-RAM swap.** Keep one extra `regstruct
  g_ls_gold_regs`, `flag_struct g_ls_gold_flags`, and a malloc'd
  `g_ls_gold_mem` (RAMSize+ROMSize), seeded once on arming. To step gold,
  temporarily swap the globals (`regs`/`regflags` and a RAM pointer indirection)
  to the gold copies, run one `cpufunctbl` op, swap back. This reuses the
  proven `jit_block_verify_snapshot_capture/restore` primitives (≈ 624/643) but
  **without re-seeding from DUT** — seed once, then only swap. Cost: per-op two
  `regstruct` swaps + RAM-pointer swap (no full RAM memcpy per step). RAM is
  shared storage; to keep gold and DUT memory independent you must mirror writes
  — see (b) if shared RAM is unacceptable.
- **(Correct, heavier) dual RAM.** Gold gets its own `RAMBaseHost'`; the
  interpreter's `get/put` for the gold step is redirected to the gold bank via a
  thread-local base. This fully isolates memory. Only needed if the divergence
  is a memory-write divergence; for the CRC/flag bug, registers+flags+next-PC
  catch it first, so start with (a) and diff only a **touched-mem window**
  (`B2_JIT_LOCKSTEP_DUMP_RADIUS` around each op's EA) rather than full RAM.

For the `0100254e` CRC bug specifically, **(a) with register+flag+next-PC
compare and no RAM diff** is the cheapest path to first-divergence and is
expected to fire at the EOR.B/BPL producer the moment N is read stale.

---

## 5. Canonical state vector (kills the 3 known artifacts)

Reduce **both** sides to this before compare — never compare raw host words:

```c
struct ls_arch {
    uae_u32 d[8], a[8];   // D0..D7, A0..A7 (A7 = active SP)
    uae_u32 next_pc;      // guest PC of next insn (NOT regs.pc)
    uae_u8  ccr;          // canonical M68K CCR: bit4=X bit3=N bit2=Z bit1=V bit0=C
};
```

- **PC:** gold uses its computed next PC; DUT uses `g_ls_dut_nextpc`
  (`host_to_m68k(pc_hist[i+1].location)`). `regs.pc`, `pc_oldp`, `fault_pc`
  are **never** compared. Kills the stale-pc / c74 artifact.
- **CCR (kills nzcv/cznv artifact):** decode each side to the 5 boolean bits.
  - GOLD: from `regflags.cznv` via `GET_NFLG/GET_ZFLG/GET_VFLG/GET_CFLG` and
    `regflags.x` (m68k.h: N=bit7, Z=bit6, V=bit11, C=bit0).
  - DUT: from the captured host PSTATE `g_ls_dut_pstate`
    (N=bit31, Z=bit30, C=bit29, V=bit28) + `regflags.x`.
  - Compare only the assembled 5-bit `ccr`. Layout differences become
    structurally impossible to misread.
- **spcflags / countdown / interrupt_flags:** excluded (dispatcher state).

---

## 6. Divergence detection + dump

```
compare(gold.ls_arch, dut.ls_arch):
  if equal: g_ls_steps++; if g_ls_steps>MAXSTEPS -> "no divergence in window" exit(0)
  else:
    fprintf(stderr,
      "LOCKSTEP_DIVERGE step=%lu pc=%08x op=%04x field=%s\n", ...);
    // per-field, only the differing fields:
    //   reg d%d/a%d  gold=%08x dut=%08x
    //   nextpc       gold=%08x dut=%08x
    //   ccr          gold=X?N?Z?V?C? dut=X?N?Z?V?C?
    // plus a 4-op back-trace ring of (pc,opcode) leading in
    halt: raise(SIGABRT) or exit(42)
```

The **first** line is the answer: the earliest PC + field where JIT and a
free-running interpreter disagree, in canonical layout, with no snapshot
reseed and no flush masking. For the CRC bug the predicted first hit is:

```
LOCKSTEP_DIVERGE pc=01002c7e op=6a.. field=ccr  gold=...N0... dut=...N1...
```

(i.e. BPL at `2c7e` reads N — gold N reflects EOR.B `2c7c`, DUT N is stale)
— or one op earlier at the EOR.B if we compare CCR post-producer. Either way it
names the exact instruction, which the per-block oracle structurally cannot.

---

## 7. Cheapest path to first-divergence at `0100254e`

1. Arm `B2_JIT_LOCKSTEP_PCS=0x01002400-0x01002700` (the confirmed spin window).
2. Gold = single-RAM-swap shadow (§4.4a), **register+flag+next-PC compare only**,
   no RAM diff (DUMP_RADIUS=0). This is the minimal config that catches a
   register/flag/branch divergence and is enough for a CRC/Bcc-liveness bug.
3. Run pure-JIT boot (`optlev=2`, no Bcc barrier) until LOCKSTEP_DIVERGE fires.
4. If it fires on `next_pc` only (branch taken differently) walk back the 4-op
   ring to the flag producer; if on `ccr`, you already have the producer.
5. Only if no register/flag/PC divergence appears in the whole window (unlikely)
   escalate to dual-RAM (§4.4b) + touched-mem diff to catch a silent memory
   write divergence.

Expected total new code: ~1 raw encoder (`mrs_nzcv`), ~1 non-freeing reg
writeback helper, ~3 globals, ~1 env gate, ~1 compile-loop branch (~25 lines
mirroring the existing trace branch), ~1 `jit_ls_dut_dump` + gold-step driver
(~120 lines reusing snapshot capture/restore). No gencomp regeneration needed
(all in `compemu_support_arm.cpp` + `codegen_arm64.h`).

---

## 8. Build / run / validation plan (when A is greenlit)

Project = `projects/previous` (NeXT). The compiler tree (`src/cpu/uae_cpu_2026`)
is shared with BasiliskII, so the codegen anchors in sections 3-4 are identical;
only the build/run harness differs.

```bash
cd /workspace/projects/previous
make build JOBS=$(nproc)            # incremental, no gencomp change

# regression must stay green (tracer is default-OFF / env-gated):
tools/fg-verify-window.sh 0x0409f500-0x0409f600 780   # expect no NEW mismatch=1

# first-divergence run (self-contained harness boots NeXT 040 ROM Rev_2.5_v66.BIN):
B2_JIT_LOCKSTEP_PCS=0x01002400-0x01002700 \
B2_JIT_LOCKSTEP_MAXSTEPS=200000 \
SAVE_LOG=$PWD/build-vnc/lockstep.log KEEP_LOG=1 \
  tools/fg-verify-window.sh 0x01002400-0x01002700 120
grep -m1 LOCKSTEP_DIVERGE build-vnc/lockstep.log
```

(Add `B2_JIT_LOCKSTEP_*` to the harness env passthrough or export inline. Bcc
barrier stays ON for this run — the tracer catches the JIT/interp divergence at
the first compiled flag-consumer; if the bug only manifests with Bcc compiled,
also drop `current_is_bcc` from `trace_barrier_op` for the run.)

Acceptance gates:
- Tracer default-off ⇒ `jit-test/run.sh` unchanged (297/0) and a normal boot
  byte-identical in block shape to pre-change (diff `JIT_CODEGEN` sizes if needed).
- Tracer-on ⇒ emits exactly one `LOCKSTEP_DIVERGE` line naming a concrete
  `pc`+`field`, reproducible across runs.
- Self-test: arm the window on a **known-good** block (e.g. a plain ALU chain in
  the ROM that already verifies clean) ⇒ must report `no divergence` to prove
  the tracer doesn't manufacture false positives from its own layout decode.

---

## 9. Risks & mitigations

- **R1 — the dump callout itself perturbs liveness.** Mitigation: dump is
  MRS-read + non-freeing STR only; no `flush_flags/dont_care/flush(1)`. Validate
  R1 by comparing `JIT_CODEGEN` block boundaries with tracer on vs off — block
  shapes must match. If they don't, the instrumentation moved a boundary and the
  result is suspect.
- **R2 — gold shadow desync from shared RAM (§4.4a).** For pure register/flag
  bugs this is fine; if the window writes memory that feeds back into flags,
  promote to dual-RAM (§4.4b). Documented as the escalation step, not the default.
- **R3 — A7/SP and supervisor-stack aliasing** when the window crosses an
  exception. Mitigation: if `cur_m68k_pc` leaves the window or SR S-bit toggles,
  stop the window cleanly (`g_ls_armed=false`) rather than chase trap frames;
  re-arm on re-entry. The `0100254e` CRC spin is user-mode steady-state so this
  should not trigger.
- **R4 — X-flag fidelity** (§3). N/Z/V/C are exact; X is best-effort until a bug
  needs it. The CRC bug is N, so this does not block.

---

## 10. One-paragraph summary for the decision

This replaces a snapshot oracle that *re-seeds the gold from the DUT every block*
and *flushes flags at every instrumented op* — two properties that make it
structurally unable to see accumulated drift or a never-materialized NZCV — with
a free-running interpreter held in true per-instruction lockstep against the real
JIT machine, observed by a flush-free MRS-NZCV + non-freeing register dump, and
compared only in canonical M68K CCR+reg+next-PC layout. It halts at the first PC
where the two architectures actually disagree. For `0100254e` the cheapest
config (single-RAM gold, register/flag/next-PC compare) is predicted to name the
CRC loop's flag producer directly — the answer the per-block oracle can only
red-herring around.

---

## 11. Instant-execute prep (refinements for greenlight)

### 11.1 §4.4b dual-RAM escalation — concrete trigger (no judgement call)

Start ALWAYS with single-RAM gold (§4.4a) + register/flag/next-PC compare,
`B2_JIT_LOCKSTEP_DUMP_RADIUS=0`. Escalate to dual-RAM **iff and only iff** BOTH:

1. The single-RAM run reports `no divergence in window` (exit 0) across the full
   `0x01002400-0x01002700` arming window AND `MAXSTEPS` not hit — i.e. regs+CCR
   +next-PC agree at every step; yet
2. The same boot still live-locks at `0x0100254e` (LED-spin > 100 lines in the
   780s run).

That conjunction is the *only* signature of a silent memory-write divergence the
register/flag/next-PC compare cannot see. Do NOT pre-emptively build dual-RAM.
Intermediate step before full dual-RAM: re-run single-RAM with
`DUMP_RADIUS=64` (touched-mem window diff around each op EA) — this catches a
local store/load divergence at ~zero extra cost and only if it too is clean do
you build the thread-local dual bank (§4.4b). Escalation ladder, in order:
`radius=0` → `radius=64` → dual-RAM. Stop at the first that fires.

### 11.2 Known-good self-test blocks (proves the tracer never false-positives)

The self-test must arm the lockstep on blocks the *current* per-block oracle
already reports `mismatch=0`, OUTSIDE the suspect CRC region, covering the flag
shapes the real bug touches (rotate/shift/logic producing N/Z/C/X). Pick at
execute time from a quick clean sweep (no guessing now):

```bash
# enumerate clean compiled blocks in a calm kernel window:
tools/fg-verify-window.sh 0x0409f000-0x0409f200 780 2>&1 \
 | grep 'mismatch=0' | grep -oE 'block=[0-9a-f]+' | sort -u | head
```

Required self-test coverage (arm each; expect `no divergence`):
- one straight-line integer-ALU block (ADD/SUB/MOVE) — baseline reg+Z/N.
- one shift/rotate block (LSL/LSR/ROXR) — exercises X materialisation (the
  lower-fidelity field in §3/R4); confirms the X decode path doesn't manufacture
  a false CCR diff.
- one logic block (AND/OR/EOR) — the exact family of the c74 producer.
If ANY self-test block reports a divergence, the tracer's own `ls_arch` decode
(§5) is wrong — fix the decode before trusting a real `LOCKSTEP_DIVERGE`.

### 11.3 Non-perturbation gate (mandatory, before trusting any result)

Build once, capture `JIT_CODEGEN` block boundaries / emitted sizes for the
`0x01002400-0x01002700` window with the tracer env UNSET vs SET. The two must be
identical (the dump is MRS + non-freeing STR only). A boundary shift means the
instrumentation moved liveness and the result is void — fix per R1 before use.

---

## 12. IMPLEMENTATION STATUS (built, env-gated, default-off) — checkpoint

Implemented in `compemu_support_arm.cpp` (env gate + `jit_lockstep_target_pc`,
the `ls_arch` canonical vector, `ls_step_gold`, `jit_ls_dut_dump`, and the
compile-loop DUT emit hook). Default-OFF (no `B2_JIT_LOCKSTEP_PCS` ⇒ inert; a
normal boot is unperturbed, `LOCKSTEP` lines = 0, no new oracle mismatch).

**Works:** arms once, fires, emits canonical `LOCKSTEP_DIVERGE pc=.. op=.. field=..
gold=.. dut=..`. The self-test caught and I fixed TWO real tracer bugs:
1. **Arming double-execution** — seeding gold at `cur_pc` (already executed by the
   DUT) made gold re-run that op. Fix: seed gold at `g_ls_dut_nextpc`, no compare
   on the arming step.
2. **Single-step desync** — only *compiled* ops are instrumented, but the DUT also
   interprets the gaps (e.g. c80 `eori`, c86 `dbf`), so stepping gold once per
   dump desynced. Fix: gold **replays all ops** (incl. uninstrumented) until it
   re-executes `cur_pc`, then compares (`field=path_lost` if gold can't reach it).

**BLOCKER (spec R3, confirmed):** `ls_step_gold` runs `cpufunctbl[]`
**reentrantly** from inside live JIT block execution against **shared RAM/MMU**.
When the gold replay reaches an I/O / memory op (the CRC region borders DMA /
NextBus setup) it faults reentrantly → `UAE2026 bridge: fatal double MMU
exception` + runaway. The shared-RAM single-RAM gold (§4.4a) is NOT safe for a
window whose replay touches device memory or can fault.

**Remaining work = the gold execution context (§4.4b escalation, brought
forward):** the gold step must run in an **isolated, fault-guarded context**:
- wrap `ls_step_gold` in `setjmp`/the bridge's fault buffer so a gold MMU/bus
  fault is contained (and itself counts as a divergence vs the DUT), AND/OR
- give gold its own RAM/IO bank (dual-RAM) or restrict arming windows to
  pure-register straight-line regions that never fault (the CRC ALU ops do not
  fault; the desync replay dragging in c86/c80 + neighbours is what reaches I/O).
A cheaper interim: shrink the window to the pure-ALU sub-range and replay-guard so
gold never executes a memory/IO op; if it must, treat it as `field=gold_fault`.

Status: infrastructure committed; gold-context isolation is the next build step
before the tracer can pin the `0100254e` first-divergence.

---

## 13. UPDATE — gold fault-guard LANDED + tracer now arms on the Bcc region

**R3 blocker RESOLVED (setjmp fault-guard, single-RAM).** `ls_step_gold` now runs
the gold `cpufunctbl[op]` inside a local try-frame (`setjmp(gold_buf)` +
`__pushtry/__poptry`, the same nesting the bridge uses at
`uae2026_jit_bridge.cpp:1492`). A gold MMU/bus fault from a reentrant I/O access
is caught **locally**: we unwind the try-frame, set `g_ls_gold_faulted`, leave the
gold shadow unchanged, and the driver ends the window cleanly with
`LOCKSTEP_END ... field=gold_fault`. Confirmed: an armed window that previously
produced `fatal double MMU exception` + machine reset now runs to completion with
**zero** double-faults (`grep -c 'fatal double MMU' = 0`).

**Arming on the Bcc region (new env gate `B2_JIT_LOCKSTEP_NOBCC`, default-off).**
The legacy trace builder (`compemu_legacy_arm64_compat.cpp`) bails the whole
trace to the interpreter at the `current_is_bcc` barrier and never calls
`compile_block`, so the lockstep DUT hook (which lives inside `compile_block`)
could not arm on the c74 Bcc region. Added a window-scoped drop:
`B2_JIT_LOCKSTEP_NOBCC=1` removes the Bcc barrier **only** for PCs inside the
lockstep window (`jit_lockstep_window_pc`), so the block reaches `compile_block`
and the hook arms. Default-off and window-scoped ⇒ normal boots unperturbed.
NOTE: run with the harness `VERIFY_BLOCKS` range pointed **away** from the
lockstep window — `verify_this_block` diverts to `jit_block_verify_run` and
*also* skips `compile_block`, suppressing the hook.

**First armed run (window `0x01002400-0x01002700`, NOBCC, pure-JIT):**
```
LOCKSTEP_DIVERGE step=0 pc=01002606 op=2002 field=d0 gold=00000000 dut=00000003  gold_ccr=00 dut_ccr=00
```
(op 2002 = MOVE.L D2,D0). The fault-guard held (0 double-faults) and the boot
ran on past the line (KMS LED traffic follows).

**CAVEAT — this is NOT yet a trusted architectural first-divergence.** It fired
at **step=0**, i.e. the very first compare after arming, on a plain register move
(gold d0=0 vs dut d0=3 ⇒ gold d2=0 vs dut d2=3 going in). A divergence at step 0
is the canonical signature of a **seed/replay fidelity bug**, not a JIT codegen
bug: the gold was seeded from the DUT's flushed `regs.regs[]` mirror at arming,
and if that mirror lagged the true DUT `d2` (or the replay desynced control flow)
gold carries a wrong value forward and "diverges" immediately. Per §11.2 this is
exactly what the **known-good self-test must rule out before trusting any
`LOCKSTEP_DIVERGE`**. NEXT slot: run the §11.2 self-test (arm a clean ALU/shift/
logic block OUTSIDE the suspect region; require `no divergence`). If the self-test
also fires at step=0, the seed/replay path is the bug — fix the arming-seed
mirror (capture true live regs at arming, not the possibly-stale flushed mirror)
and/or assert `cur_pc == g_ls_expect_pc` hard before comparing. Only once the
self-test is clean does the c74 region run name a trustworthy producer.

Landed code (single commit): `ls_step_gold` setjmp guard + `g_ls_gold_faulted`
driver path + `LOCKSTEP_END field=gold_fault`; `jit_lockstep_window_pc` export;
`B2_JIT_LOCKSTEP_NOBCC` window-scoped Bcc drop in the legacy trace builder;
`tools/fg-verify-window.sh` `B2_JIT_LOCKSTEP_*` env passthrough. All default-off.

---

## 13. SELF-TEST RESULT (gate) — tracer NOT yet trustworthy: seed/sync stale

Ran the §11.2 trustworthiness gate. The fault-guard (R3) holds — armed runs on the
c74/2600 region complete with 0 double-faults. But the FIRST-divergence is a
tracer artifact, not architectural:

```
LOCKSTEP_DIVERGE step=0 pc=01002628 op=0c03 field=d1 gold=000013ff dut=00000008
```
`op=0c03` is `CMPI.B #imm,D3` — it does NOT write D1, yet **D1 differs at step=0**
(the first compare after arming). A register the current op cannot have changed,
differing on the first compare, is the binary signature the auditor named:
**the gold seed/sync is stale, not a real divergence.** (Earlier the same
signature appeared as MOVE.L D2,D0 gold=0 vs dut=3.) Gate = RED; do not trust any
`LOCKSTEP_DIVERGE` yet.

### Root cause of the stale seed
Seeded-once gold requires the gold PC trajectory to match the DUT's exactly. The
arming seeds `g_ls_gold_pc = g_ls_dut_nextpc` (the arming op's *fall-through*).
When arming lands on a control-flow op, or when the next instrumented dump is in a
different block reached via a branch, gold replays the WRONG path → its registers
desync from the DUT while the per-op compare reports the desync as a divergence on
an untouched register at step 0.

### Fix direction (next build step, before trusting anything)
The robust fix is exact gold tracking inside the window. Options, cheapest first:
1. **Arm only on a straight-line run**: require the arming op and the next
   `MAXSTEPS` dumps to be CONSECUTIVE sequential PCs in one block (no branch, no
   block boundary between dumps). Defer arming until such a run; abandon (re-arm)
   if a gap appears. This makes `gold_pc` exact.
2. **Instrument every op in the window** (drop the compiled-only restriction by
   also dumping at the interp-fallback boundary) so gold steps exactly one op per
   dump and never has to guess a replay path.
3. Seed gold at `cur_pc` with the op's PRE-state (capture before the op runs, via
   a pre-op hook mirroring the existing `jit_verify_pre`) so the first compare is
   the arming op itself with a known-good seed.

Validation gate restated: a known-good straight-line block must run to
`LOCKSTEP_OK` (no step=0 untouched-reg divergence) BEFORE any c74 result is
trusted. The provisional-(A) build is sound; the seed/sync fix is the remaining
trustworthiness work.

---

## 14. SEED-FIX LANDED — phantom eliminated; 3 of 4 tracer-fidelity bugs fixed

Acted on the §13 RED gate. The step=0 untouched-register phantom is **GONE**.
Root-caused and fixed THREE distinct tracer-fidelity bugs (all confirmed via the
env-gated `B2_JIT_LOCKSTEP_DEBUG` trace on the c74 window `0x01002400-0x01002700`,
NOBCC, RAM boot). Tracer stays default-off; regression green
(`uae2026-mmu-fast-smoke` 32/32, `uae2026-opcode-harness` 75/75, score=100).

### Fix 1 — straight-line pre-state seed (the seed/sync fix the auditor named)
Replaced seeded-once free-running gold + 40k-step wrong-path catch-up with a
**straight-line lockstep gate**: gold is compared only when this op's PC equals
the previous instrumented op's fall-through (`cur_pc == g_ls_pending_next`). The
seed is the previous op's true DUT post-state (the exact PRE-state of this op),
so gold runs the SAME single instruction from the SAME entry. Branch/block/gap
runs roll the pending seed forward and re-sync WITHOUT comparing, so an untouched
register can never spuriously diverge. Eliminates the `CMPI.B D3 -> d1` /
`MOVE.L D2,D0 -> d0` step=0 phantom.

### Fix 2 — gold opcode double-byte-swap + stale data-view fetch
`ls_step_gold` fetched the gold opcode with
`get_opcode_cft_map((uae_u16)get_word(pc))`. `get_opcode_cft_map = uae_bswap_16`
is meant for RAW host-pointer derefs (`DO_GET_OPCODE`), but `get_word()` already
returns the host-order opcode — so the word was **double-byte-swapped**
(`0x2002 MOVE.L D2,D0` -> `0x0220`, executed as ANDI.B, zeroing D0). In RAM/MMU
mode the data-view `get_word` can also read a stale word differing from the
executable code stream the DUT ran. Fix: dispatch gold on the **exact opcode the
DUT executed** (already passed to the hook), `cpufunctbl[opcode & 0xffff]` — the
same raw big-endian dispatch the interpreter's `GET_OPCODE` uses. Added
`fill_prefetch_0()` after `m68k_setpc` for extension-word reads.

### Fix 3 — gold flag-layout bridge (interpreter cznv vs JIT nzcv)
The compiler unit does `#define regflags jit_regflags` (nzcv/x layout); the
interpreter handlers (`cpufunctbl[]`) write the REAL `regflags` (legacy cznv,
N=15/Z=14/C=8/V=0). The gold step seeded+captured the WRONG flag struct, so every
flag-setting op read stale flags (`ASR.L` result=0 with Z unset). Fix: added
interpreter-unit accessors `Uae2026InterpCanonicalCcr5()` /
`Uae2026InterpSeedCcr5()` (in `uae2026_jit_bridge.cpp`, where `regflags` is the
real cznv symbol) and carry the gold seed/result CCR **canonically** (5-bit
X/N/Z/V/C) across the layout seam. After this, `ASR.L D1,D0` reads
`gold_ccr=04 == dut_ccr=04` (Z correct).

### Remaining blocker (4th, precisely located) — DUT-side dead-flag capture
Window now advances to step=2 `MOVEQ #3,D6`: gold correctly `ccr=00` (result 3,
Z clear) but DUT `ccr=04` — the **stale Z from the prior ASR**. The JIT drops
flag computation for flag-DEAD ops (`dont_care_flags`); `flush(1)` at the dump
site cannot materialise a flag that was never computed, so the DUT's
`jit_regflags` carries the previous op's Z. This is NOT a seed bug and NOT a real
architectural divergence — it is the documented flag-liveness / flush-free-NZCV
DUT-capture limitation (§3, R1, R4). Reaching `LOCKSTEP_OK` on a flag-setting
block requires the DUT side to either (a) read live host PSTATE flush-free
(MRS NZCV per §3) AND only compare flags the JIT actually computed for that op
(liveness mask), or (b) restrict the self-test to a block whose every op's flags
are live (consumed by the next op). The seed/dispatch/gold-layout fidelity is now
correct; the DUT dead-flag mask is the next gate.

Landed in `compemu_support_arm.cpp` (straight-line gate, opcode dispatch,
canonical CCR carry) + `uae2026_jit_bridge.cpp` (interp CCR accessors).
`B2_JIT_LOCKSTEP_DEBUG=1` adds the LSDBG/LSDBG_GOLD per-op trace. All default-off.

---

## 14. TRACER TRUSTWORTHY + FIRST REAL RESULT — c74 CLEARED

Seed-fix landed (straight-line pre-state gate) + `must_end` on the NOBCC-dropped
Bcc (so the c74 CRC block actually compiles/instruments) + log-and-continue
enumeration (cap 40).

**Trustworthiness gate GREEN:** the `0x01002400-0x01002700` region — which
previously false-positived `step=0` on an untouched register — now runs
`LOCKSTEP_OK` (201 clean compare steps). No more seed/sync artifact.

**First trustworthy result on the c74 CRC window (`0x01002c00-0x01002d00`),
reproducible across runs:** every divergence is CCR-only, at `ROR.B` (c7a) and
`LSL.L` (c76), in the **N/Z/C** bits. Crucially:
- **X (bit4) always matches**, all **registers always match**, and `ROXR` (c78),
  `EOR` (c7c, the BPL's N producer) and the `BPL` (c7e) show **zero divergence**.
- The diverging N/Z/C of ROR.B/LSL.L are **dead** — the next `eor.b` overwrites
  N/Z and clears C/V before any use.

**Conclusion: c74 is CORRECT and is NOT the boot blocker.** Its live state (X,
registers, the EOR-N the branch tests, and the branch target) all match the
interpreter. The dead-flag ROR.B/LSL.L N/Z/C differences are a benign JIT
flag-codegen nicety, not the hang. This independently CONFIRMS the earlier
liveness disproof and the stale-pc artifact reclassification — with a trustworthy
per-instruction tool instead of the red-herring-prone per-block oracle.

**Redirect:** the wrong-branch that routes to the `0100254e` spin is in a
DIFFERENT block. Next: widen / re-point the lockstep window to the actual
control-flow that reaches 254e (sweep the Bcc-terminated blocks on the path),
find the first divergence with a LIVE consequence (a register, X, or the EOR-N /
branch target). The tracer is now the right instrument for that hunt.

Optional cleanup: the dead-flag ROR.B/LSL.L N/Z/C divergences are worth a
separate low-priority `jff_ROR_b`/`jff_LSL_l` flag-fidelity fix, but they do NOT
gate boot.
