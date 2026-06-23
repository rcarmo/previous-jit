# Boot frontier relocation: c74/cb4 is NOT the blocker — JIT hangs at SCSI/ESP init

**Date:** 2026-06-20
**Slot:** post-c74 REGONLY register/next_pc sweep (auditor-triggered after leaked-boot CPU cleanup)
**Repo:** `previous` (rcarmo-jit/main), HEAD at investigation: `0479171`

## TL;DR

The "wall ~0x01002cb4 / c74 region" framing is **obsolete**. That region runs in
the **interpreter** on the natural boot path and completes fine. The real, current
JIT boot blocker is a **hang in the SCSI/ESP device-init path** at ROM
`PC=0x04387150` ("ESP Command: reset SCSI bus"). The interpreter sails past it; the
JIT freezes. This is a **JIT divergence, not a Previous emulation gap.**

> **UPDATE 2026-06-22 — diagnosis complete; see [final section](#session-2026-06-22--pure-jit-recompile-churn-root-cause-found-fix-selected).**
> The chronological log below (2026-06-20/21) traces the natural-boot-path framing
> through to ROOT CAUSE 4 (the `trace_barrier_op` bailout) and the bsr-misclassification
> fix. The 2026-06-22 session carried that forward: under **pure-JIT barrier drops** the
> SCSI loop and the NextBus board-ROM checksum *do* compile, but the checksum loop then
> **recompiles ~7000× instead of running native**. Root cause is now **probe-confirmed**: a
> stale forward-edge native chain to the `execute_normal` trampoline that bypasses a
> *correct* `cache_tags` dispatch. Fix selected (a self-resolving chain thunk); five simpler
> candidates eliminated with data. Implementation is the only remaining work. Jump to the
> final section for the complete, current picture.

## Evidence chain

### 1. REGONLY sweep over c74 + CRC loop = LOCKSTEP CLEAN
- `tools/lockstep-sweep.sh` (REGONLY, window `0x01002700-0x01003200`).
- 200001 DUT steps, **0 register/next_pc divergence**, `field=ccr: 0` (CCR
  advisory path correctly suppressed). The known dead-flag CCR diffs
  (`gold_ccr=09 dut_ccr=00` at `c7a`/op `e219`) did NOT fire — registers + next_pc
  matched throughout. Confirms efe336a: c74 is not a JIT blocker.
- The sweep stayed pinned inside the c72→c8c CRC outer loop (BNE `66e4` at c8c →
  c72). Gold per-step shadow is far too slow to clear this loop's trip count, so a
  contiguous lockstep sweep cannot reach past it.

### 2. The c74/CRC region is INTERPRETER-executed on the natural boot
- Plain JIT boot with `B2_JIT_TRACE_PCS=0x01002400-0x01002e00` (compiled-op PC
  tracer) → **0 JITPCHIT hits**. The region is not JIT-compiled on the boot path.
- The boot log shows it reached via `exec_normal` (interpreter): LED toggles
  `SCR2 LED change ... PC=0x01002c70 / 0x01002cb4 / 0x01002bb4 / 0x01002c50`,
  i.e. it passes the cb4 "wall" and keeps going.

### 3. Plain JIT boot progresses to SCSI init, then HANGS
- After cb4: MMU memory-sizing probes (slot scan faults at f2fffff0/f4fffff0/
  f6fffff0, d3 = 2,4,6), then NeXT ROM SCSI/ESP/floppy driver init at
  `PC=0x04386xxx–0x04387xxx`.
- Frontier freezes at `ESP Command write at $02114003 val=$03 PC=$04387150`
  ("reset SCSI bus", config $57 ⇒ "not interrupting"). **Identical log (4206
  lines) at 35s and 75s ⇒ frozen, not slow.**
- Canonical recipe `B2_JIT_RTE_FAULT_HANDOFF=1` hangs at the **exact same point**
  (4206 lines, no handoff marker — it's a poll spin, not an RTE fault).

### 4. Interpreter-only boot blows past the same point
- `PREVIOUS_JIT_OVERRIDE=0 PREVIOUS_JIT_RAM_OVERRIDE=0`: **1,333,639 log lines**,
  actively churning. Reaches ROM `0x04387786`, RAM driver `0x0100a6xx`, and FPU
  packed-decimal ops at `0x05054296`. Alive, not hung.

⇒ JIT-on freezes at SCSI reset; JIT-off does not. **The SCSI hang is a JIT bug.**

## Hypothesis for the JIT bug

After the non-interrupting SCSI bus reset (`val=$03` → ESP, config $57 suppresses
the reset IRQ), the driver polls an ESP status register (MMIO `0x02114xxx`). The
JIT-compiled poll loop appears to read a **stale / wrong ESP status** and spins
forever, where the interpreter reads the live value and proceeds. Likely a JIT
**MMIO read ordering / value-caching** issue around the ESP status register, not an
ALU/flag divergence.

## Next slot (re-aimed hunt)

1. Lockstep/trace the ROM region around `0x04387150` and the poll loop immediately
   after the SCSI-reset command write, comparing JIT vs interpreter ESP-status
   reads. Use `B2_JIT_TRACE_PCS` first (cheap) to map the poll loop PCs, then a
   contiguous REGONLY lockstep window seeded at the poll-loop head.
2. Confirm whether the divergence is an MMIO read value vs a branch on a
   misread status bit. Root-cause the specific JIT handler / memory path.
3. Single-SHA per-handler fix; gate on boot advancing past `0x04387150` AND the
   opcode regression staying 75/75 green.

## Harness changes made this slot (all default-preserving)

- `tools/fg-verify-window.sh`: env-overridable `PREVIOUS_JIT_OVERRIDE` (default 1),
  `PREVIOUS_JIT_RAM_OVERRIDE` (default 1), `B2_JIT_RTE_FAULT_HANDOFF` (default 0),
  and `B2_JIT_LOCKSTEP_REGONLY` passthrough. Defaults reproduce the prior behaviour.
- `tools/lockstep-sweep.sh`: REGONLY register/next_pc-only sweep wrapper.
- `Makefile`: `lockstep-sweep` and `lockstep-selftest` targets.

---

## CORRECTION (same day, longer run) — 0x04387150 is NOT a freeze; it is a slow poll

The section-3 conclusion ("identical 4206 lines at 35s and 75s ⇒ frozen") was
**premature — the runs were too short.** Re-run of the DEFAULT plain JIT boot
(`B2_JIT_RTE_FAULT_HANDOFF=0`, the wrapper default) for **260s**:

- **16015 log lines** (vs 4206 at 75s) — **11809 lines AFTER** the last
  `PC=0x04387150` ("reset SCSI bus") occurrence at line 4206.
- **1011 SCSI block reads**, offsets advancing **0 → 149248**.
- Reaches `[ESP] Select … Target: 0`, `SCSI command: Read sector`, and at the 260s
  timeout is **mid-transfer** reading "16 block(s) at offset 149248" — 31 distinct
  PCs in the last 800 lines (not a tight stall), still advancing.

### What actually happens at 0x04387150
The SCSI bus reset is configured **non-interrupting** (config `$57`), so the driver
must **poll** for reset/select completion instead of taking an IRQ. Under the JIT
that poll is a long, **silent** spin (no log output) that takes **>75s but <~150s**
of wall time to satisfy, then the driver proceeds to SELECT + sector reads. The
35s/75s snapshots both landed inside that silent poll → looked identical → looked
frozen. They were not. The boot is **slow, not hung.**

### Revised frontier
- c74/CRC: REGONLY-clean (no JIT divergence) — unchanged, confirmed.
- SCSI/ESP `0x04387150`: **NOT a JIT divergence/freeze.** The default JIT boot
  passes it and reads 1000+ disk blocks (kernel load), reaching the same ROM PC
  frontier (`0x04387786`) as the interpreter.
- **The current frontier is THROUGHPUT, not correctness:** the JIT boot is alive
  and loading NeXTSTEP from SCSI disk, just far slower than the interpreter
  (verbose device logging + per-op JIT/emulation overhead). No JIT divergence has
  been observed anywhere on the boot path through 1011 disk blocks.

### Revised next step
Stop hunting a phantom SCSI divergence. Instead: (a) reduce boot wall-time
(lower device-log verbosity; the SCSI/ESP `nTextLogLevel=5` spam dominates), and
(b) run a long boot to confirm the JIT reaches kernel handoff / FPU region
(`0x05054296`, where the interpreter is) — only THEN, if a real divergence appears,
lockstep it. NOTE: `B2_JIT_RTE_FAULT_HANDOFF=1` was separately reported to hang at
the same PC and is NOT covered by this correction (default=0 is what progresses).

---

## CONFIRMATION 2 — RTE_FAULT_HANDOFF=1 also progresses; poll is INTERPRETER-run

Applied the auditor's location-guard (B2_JIT_TRACE_PCS at the poll PC) + tested the
one config cb14fb6 claimed "hangs at the exact same point":

- **RTE_FAULT_HANDOFF=1, 260s:** 15756 lines, **11550 after** the last 0x04387150,
  **994 SCSI block reads** advancing 0 → 149199, active ESP commands at PC=0x043876ee
  at the timeout. **Also NOT frozen** — same slow-poll-then-progress as default.
  cb14fb6's "=1 hangs (4206 identical)" is the same too-short-run artifact.
- **Location guard: `B2_JIT_TRACE_PCS=0x04387100-0x04387200` → 0 JIT-compiled-op
  hits.** The SCSI/ESP poll runs in the **INTERPRETER**, not JIT-compiled — expected,
  because RAM-JIT mode (`PREVIOUS_UAE2026_JIT_RAM=1`) compiles RAM code
  (`0x01xxxxxx`+) and runs ROM (`0x04xxxxxx`) in the interpreter.

### The "JIT MMIO read-caching at 0x04387150" hypothesis is MOOT
There is no freeze (both configs read ~1000 disk blocks past it), AND the poll is
not JIT codegen (0 compiled hits) — so there is no JIT MMIO read to CSE/hoist there.
The whole SCSI-divergence hunt is closed.

### Accurate frontier model
Under pure-RAM-JIT: ROM (POST + SCSI/ESP driver, `0x04xxxxxx`) executes in the
**interpreter** — slow but correct — loading the NeXTSTEP kernel from disk into RAM.
The **JIT-critical phase is kernel execution in RAM**, which only begins AFTER the
disk load completes. No JIT divergence exists on the ROM-driver boot path. To
exercise/validate the JIT (and surface any real divergence), the boot must finish
the disk load and start running the RAM-resident kernel. Current gate = THROUGHPUT
(slow interp ROM driver streaming thousands of blocks), not a JIT correctness bug.

---

## CORRECTION 3 (auditor fallback-counter ask) — it IS ~100% interpreter, NOT throughput

The auditor asked the right question: is the ~100x slowdown overhead, or silent
interp fallbacks? **B2_JIT_DIAG=1 over the 260s default boot (RTE_FAULT_HANDOFF=0):**

```
JIT_DIAG t=260s dispatch=8099987 exec_normal=8098912 compile=13
  (fresh=13 opt0=13) recompile_block=1068 cache_miss=1 pc=0x04382e22
```

- **exec_normal / dispatch = 8,098,912 / 8,099,987 = 99.99% INTERPRETER.**
- **compile_block called only 13 times in 260s** — the JIT compiled 13 tiny opt0
  blocks total and ran everything else in the interpreter.
- So the slowdown is NOT verbose-logging overhead — **the JIT is barely engaging.**

### Address-map error in CONFIRMATION 2 — corrected
CONFIRMATION 2 claimed `0x04387xxx` is ROM and "RAM-JIT runs ROM in interp." WRONG.
`uae2026_jit_bridge.cpp:1775`: **RAM = 0x04000000..0x08000000**, ROM =
0x01000000..0x01020000. So `0x04387xxx` (the SCSI driver / boot poll loop) is
**RAM** — code that RAM-JIT *should* compile. Its 0-compiled-hits means **RAM code
is running in the interpreter**, i.e. the real coverage gap, not an expected
ROM-in-interp. (c74 @ 0x01002xxx genuinely IS ROM and interp-run — that part holds.)

### Revised conclusion (reverses "throughput not correctness")
The boot is MMU-enabled early (`68040 MMU: enabled=1` @ 0x01000a44); the diag PC
sits in MMU-translated RAM (`0x04382xxx`) and runs in interp. PRIME hypothesis:
**MMU-enabled RAM code is not being JIT-compiled** (dispatcher routes it to
exec_normal without ever calling compile_block), so essentially the entire
real-OS boot runs interpreted. This is a JIT coverage/correctness gap (per the
zero-interp-fallback goal), and it is the real target — NOT throughput, NOT a
phantom SCSI MMIO bug.

### Next
Find why the dispatcher routes MMU-enabled RAM PCs (0x0438xxxx) to exec_normal
instead of compile_block: is there an mmu_enabled / code-translation gate that
forces interp? Is compile_block being called and bailing, or never called? (counter
says never called — only 13×). Root-cause the compile-trigger gap for MMU RAM code;
that single fix should collapse the 8M interp calls and is the actual path to
kernel handoff + File Viewer.

---

## ROOT CAUSE 4 — the 99.99% interp is the trace_barrier bailout list

`compemu_legacy_arm64_compat.cpp:~1556` — the block builder returns WITHOUT calling
compile_block whenever the block contains a `trace_barrier_op`:

```
trace_barrier_op = current_is_bcc(&!drop_bcc) || current_is_dbcc
  || current_is_stack_pop_move || current_is_stack_push_pea || current_is_return
  || current_is_link_unlk || current_is_immediate_bitop
  || current_is_ethernet_reset_island || current_is_jsr_jmp;
if (trace_barrier_op) { ...; return; }   // <-- block NOT compiled -> runs in interp
```

These barriers — conditional branches, DBcc, RTS/RTE, JSR/JMP, LINK/UNLK, stack
push/pop moves — appear in nearly EVERY real basic block. So almost every block
bails here and runs in the interpreter: **that is the mechanism behind
compile_block=13 / exec_normal=8.1M (99.99% interp).**

This is precisely the "no trace_barrier / interpreter early-returns" the hard goal
names. The path to a JIT boot (and File Viewer) is to make blocks containing these
ops **compile with correct native control-flow/stack semantics** instead of bailing.

### Re-centred frontier (supersedes the c74 + SCSI detours)
- c74 (ROM, interp by design) and the SCSI "freeze" (slow poll) were both detours.
- The REAL blocker, quantified: the JIT only compiles trivial straight-line blocks;
  every control-flow/stack barrier returns to interp. ~100% of the boot is interp.
- Prior single-barrier attempts (Bcc native-continuation, immediate-bitop ec26050)
  were chipping at THIS list; most Bcc attempts were falsified/reverted.

### Next (the actual work)
Pick the highest-frequency barrier on the boot path and implement correct native
JIT codegen for it so its blocks compile (start: Bcc and JSR/JMP — the control-flow
backbone — then RTS/RTE, DBcc, stack moves). Gate each barrier removal on: blocks
now compile (compile_block count rises, exec_normal share drops), REGONLY lockstep
clean on the newly-compiled blocks, and 75/75 + 32/32 green. Instrument a per-barrier
hit counter first to rank them by boot-path frequency.

---

## NEXT-SLOT LAUNCH MAP — Bcc native codegen (the 60% barrier)

Gate confirmed = deliberate trace_barrier in execute_normal:~1556. Target = Bcc
(59.6% of bailouts). Entry points for the fix campaign:

- Barrier site: `compemu_legacy_arm64_compat.cpp:~1556` — `current_is_bcc` →
  `if (trace_barrier_op) return;`. The fix lets Bcc traces reach `compile_block`
  (B2_JIT_LOCKSTEP_NOBCC already drops it inside the lockstep window — reuse that
  scoping to validate before a global drop).
- Native Bcc handlers: `compemu_arm.cpp` — op_6000 (BRA, unconditional, no cc),
  op_6001/op_60ff (BRA.B/.L), op_6100 (BSR); CONDITIONAL Bcc = op_62xx..op_6fxx
  (cc 2..15) — these emit native jcc + two endblock exits (taken/not-taken) and are
  where the bugs live.
- endblock / direct-chain machinery: `codegen_arm64.cpp:609`
  (`compemu_raw_endblock_pc_inreg`), `:700` (`_isconst`), counter
  `jit_endblock_inreg_count` (codegen_arm64.cpp:1330). Comment at
  compemu_legacy_arm64_compat.cpp:~1521 names the failure modes: "Bcc/DBcc can
  stop early or fall through into extension words after a few compiled iterations"
  + "endblock/direct-chain state bugs".

Method (per auditor): make conditional-Bcc native codegen correct (taken/not-taken
endblock + direct-chain across the branch, flag-condition read), then drop the Bcc
barrier; validate with REGONLY lockstep on the newly-compiled Bcc blocks +
compile_block-up/exec_normal-down + 75/75 & 32/32. TRIPWIRE: if the conditional
continuation is architecturally broken with large blast radius (why it was
barriered), name the mechanism and surface as a scope call — do NOT churn reverts
(prior full-drop/pure-terminator/forward-only/flag-boundary-sync attempts were all
reverted for exactly these endblock/direct-chain reasons).

---

## Bcc campaign — validation-reachability constraint (measured this slot)

Tried a broad NOBCC+REGONLY sweep (window 0x01002000-0x01010000, 500k maxsteps) to
characterize compiled-Bcc correctness across many blocks. Result: LOCKSTEP_OK, 0
divergence — BUT only 2 blocks compiled in-window: the per-step gold-interp
(~100x slowdown) kept the run pinned in the c74 CRC loop for all 500k steps; it
never traversed to diverse Bcc blocks. So:

- POSITIVE: c74's compiled Bcc is REGONLY-clean (reconfirmed) — a real signal the
  Bcc codegen is not uniformly broken.
- CONSTRAINT: a contiguous REGONLY lockstep sweep CANNOT reach the high-frequency
  RAM Bcc blocks (0x0438xxxx, ~60% of bailouts) — they're deep in the boot and the
  lockstep is far too slow to get there. Validation for the campaign must NOT rely
  on a single contiguous sweep reaching them.

### Implication for the Bcc campaign validation strategy
Options to validate deep RAM Bcc blocks as the barrier is dropped:
(a) Lockstep windows that EXCLUDE early hot loops (e.g. skip c74) so steps aren't
    burned before the target region — but the target must still be reached.
(b) Per-block / checkpoint validation: arm REGONLY only at a specific Bcc block PC
    once the boot reaches it (seed at that PC), rather than a contiguous sweep.
(c) Drop the Bcc barrier GLOBALLY but keep the architectural block-verify + a
    compiled-block REGONLY spot-check at first compile of each new Bcc block
    (the gate already verifies each newly-compiled block as it starts compiling).
Recommended: (c) — it matches the auditor's "gate verifies each newly-compiled
block organically" and avoids the unreachability of a contiguous deep sweep.

---

## First-compile REGONLY spot-check — design constraints (auditor reinforcements)

The spot-check validator (arm lockstep AT each barrier block the moment it
compiles, not a sweep from boot-start) MUST obey two fidelity rules, reusing the
§11.x tooling already built:

1. SEED FIDELITY — seed gold from the DUT's TRUE register+memory state at the
   barrier-block ENTRY, using the state-keyed seeding from the seed-fix (d2ac44a)
   + architectural mask. NOT a fresh-from-boot gold — that would re-introduce the
   stale-seed problem already solved. The spot-check bounds the comparison to a
   short window at the point of interest, seeded from real DUT entry state.

2. SELF-VALIDATION BEFORE TRUST — before trusting the spot-check on Bcc, prove it
   fires clean on a KNOWN-GOOD compile: a correctly-compiled low-risk barrier
   (RTS/RTE or LINK/UNLK) must read REGONLY-clean (zero divergence) through the
   spot-check first. Only after the spot-check is shown to report clean on a known
   -good compile is a subsequent real Bcc divergence trustworthy. (Same discipline
   that gated the original lockstep with the known-good 0x01002400-0x01002700
   region reading LOCKSTEP_OK.)

Build order: spot-check harness -> self-validate on RTS/LINK (clean) -> prove it
catches a real divergence -> only THEN apply to Bcc. Validate the tool, then trust
it — do not debug Bcc codegen through an unproven validator.

---

## First-compile spot-check harness — step 1 implemented (barrier-drop generalization)

The proven lockstep+REGONLY machinery only dropped the **Bcc** trace barrier
inside the lockstep window (`B2_JIT_LOCKSTEP_NOBCC`). To SELF-VALIDATE the
spot-check on a known-good low-risk barrier (rts/rte or link/unlk) BEFORE trusting
it on Bcc, the barrier-drop is now generalized:

- `B2_JIT_LOCKSTEP_DROP=<list>` (default empty => no effect) drops the named
  trace-barrier families inside `jit_lockstep_window_pc` only. Tokens: `bcc`,
  `dbcc`, `rts`/`rte`/`ret`, `link`/`unlk`. `B2_JIT_LOCKSTEP_NOBCC` stays as a
  backward-compatible alias for `bcc`.
- Each dropped family also forces `must_end` at its op so the block actually
  compiles ending at the barrier (parallel to the original drop_bcc/Bcc rule),
  letting the lockstep DUT hook arm + step the gold interpreter against the
  newly-compiled barrier block.

Scoped to the window, default-off => normal boots and all regression gates are
unperturbed. Validation: opcode harness 75/75 (`score=100`), RAM-code MMU fast
smoke 32/32 (`score=100`), build clean.

Next (reserve for a clear CPU slot per the concurrent-boot cap): drive
`B2_JIT_LOCKSTEP_DROP=rts` (then `link`) over a known-good RTS/LINK barrier-block
window with `B2_JIT_LOCKSTEP_REGONLY=1` and confirm `LOCKSTEP_OK` (zero
register/next_pc divergence) — the self-validation gate — before applying the
spot-check to the deep RAM Bcc blocks.

---

## Spot-check self-validation MUST be two-sided (auditor, before trusting on Bcc)

Step 1 done (56c96fc): B2_JIT_LOCKSTEP_DROP generalizes the in-window barrier drop
to {bcc,dbcc,rts,link}. Before the spot-check is trusted on the Bcc prize, it must
be self-validated BOTH ways — a validator that only ever reads OK is worthless:

(a) POSITIVE — known-GOOD compile reads clean. Drop a low-risk barrier (rts/rte or
    link/unlk) in-window, REGONLY spot-check the newly-compiled block → LOCKSTEP_OK,
    zero register/next_pc divergence. (Have the harness; run when CPU clears.)

(b) NEGATIVE — known-BROKEN compile is FLAGGED. Deliberately perturb ONE barrier's
    native codegen (a known-wrong emit, e.g. an off-by-one displacement or a
    dropped flag on the rts/link handler) and confirm the spot-check reports
    NON-ZERO register/next_pc divergence at that block. This proves the spot-check
    actually FIRES on a real divergence — §11.2 discipline applied to the validator
    itself. Revert the perturbation after.

Only after BOTH (a) clean-on-correct AND (b) fires-on-broken is a subsequent real
Bcc divergence trustworthy. Then deep Bcc = codegen-only fight; scope-call if
architecturally stuck. (Reserve the heavy boots for a clear idx-1/idx-2 slot per the
concurrent-CPU cap.)

---

## PROVE phase result — spot-check FAILS the NEGATIVE self-validation (harness not yet trustworthy)

Ran the two-sided self-validation on RTS (B2_JIT_LOCKSTEP_DROP=rts + REGONLY,
window 0x01000000-0x01003000). Result is the disciplined catch the auditor's
negative half is for:

- POSITIVE (correct RTS codegen): LOCKSTEP_OK, 300001 steps, 0 divergence.
- NEGATIVE #1 (corrupt RTS return PC, arm_ADD_l_ri(newad,2) in op_4e75 _ff+_nf):
  LOCKSTEP_OK — **did NOT fire.**
- NEGATIVE #2 (corrupt RTS A7 pop, lea 15,15,6 — REGISTER-observable): LOCKSTEP_OK
  — **also did NOT fire.**
- Diagnostic: only **3 distinct compiled blocks** validated across the 300001
  steps. The contiguous lockstep is PINNED in a tiny early compiled loop (the
  ~100x gold-interp slowdown, 10d46c9's reachability constraint) and never reaches
  the RTS blocks. So the POSITIVE LOCKSTEP_OK was **VACUOUS** (no RTS validated),
  and both NEGATIVES are silent for the same reason.

### Conclusion — do NOT trust the spot-check on Bcc yet
A validator that does not fire on a deliberately-broken compile is worthless. The
current harness (56c96fc) only generalized the in-window barrier DROP mask; the
lockstep validation is still the CONTIGUOUS straight-line gate, which pins in early
loops and never validates the dropped barrier blocks. **56c96fc is necessary but
NOT sufficient.**

### Required next step (was already specified in 10d46c9, now PROVEN necessary)
Implement the actual FIRST-COMPILE per-block spot-check: arm REGONLY in a BOUNDED
window AT each newly-compiled barrier block (seeded from the DUT's true entry state
per bb0c275), so the comparison happens exactly at the barrier block — not a
contiguous sweep that pins elsewhere. Re-run the SAME two-sided test; only when
NEGATIVE #1 and #2 FIRE (register/next_pc divergence at the RTS block) and POSITIVE
reads clean is the validator trustworthy. Only THEN proceed to Bcc.

(Perturbations reverted; binary rebuilt clean; opcode regression 75/75 green.)

---

## PROVE phase, next_pc half — validator is BLIND to branch-target divergence (critical, blocks Bcc)

Per the auditor's required next_pc-fire test (Bcc diverges by next_pc, not register):
perturbed the BPL.B taken-target (+2) in op_6a01_0_comp_ff/_nf, then DROP=bcc +
REGONLY on the c74 window (0x01002700-0x01002d00):

- The c74 loop block (0x01002c76, containing the bpl.s at 0x01002c7e) COMPILED
  under DROP=bcc and the lockstep validated it across all 300001 steps.
- Result: **LOCKSTEP_OK — the corrupted branch target did NOT fire.** NON-VACUOUS
  (the block was compiled+exercised), so this is a real validator blind spot.

### Root cause (structural)
The REGONLY contiguous lockstep cannot see control-flow divergence:
1. For an end-of-block control transfer, the emit site sets dut.next_pc =
   _ls_next = 0 (i+1==blocklen), and the compare does `if (dut.next_pc && gold!=dut)`
   -> skipped. The branch TARGET is never compared.
2. The per-step gold RESEED (g_ls_pending_regs = dut post-state) makes gold ADOPT
   dut's (wrong) PC on the next step, so a wrong branch never surfaces downstream.

So the validator catches REGISTER divergences (compared AT the op, e.g. a d0
miscompile fires) but is BLIND to NEXT_PC/branch-target divergences. Bcc bugs are
branch-target divergences -> **the current validator CANNOT validate Bcc.** A
register-only negative pass (d0 fires) does NOT cover the branch class.

### Required harness fix BEFORE Bcc
The first-compile per-block spot-check must, for a compiled control-transfer block,
capture dut.next_pc = the ACTUAL post-op regs.pc (not static _ls_next=0) and compare
it to gold's computed target BEFORE reseeding. Then re-run this exact test; gate =
the corrupted BPL target FIRES field=next_pc at 0x01002c7e AND a correct compile
reads clean. Only then is the validator trustworthy for the 60% Bcc prize.

NOTE (record accuracy): no B2_JIT_PROVE_PERTURB mechanism or d0-at-0x010005c0
negative exists in THIS repo (HEAD a25655e -> this commit); a register-fires result
described elsewhere is a sibling-session artifact, not Previous. For Previous the
register-fire path is untested and the next_pc path is PROVEN blind.

---

## Re-prove gate = FULL 4-POINT MATRIX (auditor, after conflation correction)

Both structural fixes are proven-required BEFORE Bcc:
  (i)  REACHABILITY (RTS finding a25655e): first-compile per-block spot-check —
       arm a bounded REGONLY window AT each newly-compiled barrier block, seeded
       from DUT true entry state (bb0c275).
  (ii) NEXT_PC VISIBILITY (BPL finding 4961356): for a control-transfer block,
       capture dut.next_pc = ACTUAL post-op regs.pc (not static _ls_next=0) and
       compare to gold's target BEFORE the reseed.

Because the earlier "register-fires" (d0 @010005c0 / B2_JIT_PROVE_PERTURB) was a
SIBLING conflation, register-teeth is ALSO untested in Previous. So the re-prove is
a full matrix; trust the validator ONLY when ALL FOUR are green AT the barrier block:

  (1) POSITIVE: correct compile -> clean on BOTH dimensions (no register, no next_pc
      divergence) at the barrier block.
  (2) NEXT_PC TEETH: corrupt BPL taken-target (+2) -> FIRES field=next_pc at
      0x01002c7e.
  (3) REGISTER TEETH: corrupt a register write (e.g. RTS A7 pop, or a d-reg emit)
      -> FIRES field=register at that block.
  (4) REACHABILITY: confirm the perturbed block actually COMPILED + was COMPARED
      (non-vacuous — the failure mode that made a25655e's positive meaningless).

All four green -> only THEN the Bcc codegen fight. Until then the validator would
silently pass every Bcc (next_pc) bug.

---

## FIX phase, next_pc validator + FULL 4-POINT MATRIX GREEN (2026-06-21)

Implemented the next_pc-capture half (the dimension proven blind at 4961356) as a
branch-target MEMBERSHIP check, not a next-arrival cross-check. The emit site
publishes the DUT's TWO compile-time branch targets (taken + not-taken, as guest
PCs via `get_virtual_address`) into `g_ls_dut_taken_pc`/`g_ls_dut_nottaken_pc`/
`g_ls_dut_is_branch` alongside the existing `g_ls_dut_nextpc` store. In
`jit_ls_dut_dump`, after the gold step, a control-transfer op asserts that gold's
architecturally-correct successor `gold.next_pc` is one of the DUT's two compiled
targets; if codegen corrupted the taken/not-taken target it is absent from the set
=> `field=next_pc` FIRES at the producer PC, before the pending reseed launders it.

Why membership, not next-arrival: a per-op "gold-predicted successor == next
instrumented cur_pc" cross-check FALSE-FIRES on a clean compile, because only
barrier-dropped blocks are instrumented — the DUT's next instrumented op is the
loop top (0x01002c76), not the BPL's immediate successor (0x01002c86). Membership
runs entirely in the C hook on compile-time constants, so it also never perturbs
the delicate chained-endblock condition codegen (no inline flush/call between the
condition eval and the raw_jcc).

### FULL 4-POINT MATRIX (window 0x01002700-0x01002d00, DROP/NOBCC, REGONLY)
1. POSITIVE — clean compile: GATE=GREEN, LOCKSTEP_OK, no register/next_pc fire.
   gold.next_pc=01002c86 ∈ {dut_taken=01002c86, dut_nottaken=01002c80}.
2. NEXT_PC teeth — `B2_JIT_PROVE_CORRUPT_TAKEN=1` (taken-target +2 in window):
   FIRES `field=next_pc gold=01002c86 dut_taken=01002c88 dut_nottaken=01002c80`
   at pc=01002c7e (op 6a06 BPL). Non-vacuous.
3. REGISTER teeth — `B2_JIT_PROVE_CORRUPT_REG=1` (flushed D2 +1 in window):
   FIRES `field=d2 gold=.. dut=..+1` across the windowed ops. Register dimension
   now PROVEN firing in Previous (was untested / sibling-conflated at 4961356).
4. REACHABILITY — the barrier block 0x01002c76 (containing the BPL at 0x01002c7e)
   COMPILED under the dropped Bcc barrier and was COMPARED across 300001 steps in
   the positive run; teeth fire on the first taken iteration. Non-vacuous.

Both teeth were env-gated, default-off, and REVERTED after the gate; the binary
was rebuilt clean. Regression green: opcode harness 75/75 score=100; fast MMU
smoke 32/32 score=100; positive lockstep re-confirmed GREEN after revert.

The validator now catches BOTH register AND next_pc/branch-target divergence ->
it is trustworthy for the Bcc (60%) codegen fight.

---

## c74 Bcc VERIFY (trustworthy validator) — existing codegen CLEAN at c74

With the next_pc-aware validator (7604ec8, 4-matrix green), re-ran the c74 Bcc
positive: B2_JIT_LOCKSTEP_DROP=bcc + REGONLY, window 0x01002700-0x01002d00, UNPERTURBED.

- Result: LOCKSTEP_OK, 300001 steps, 0 divergence (neither register nor next_pc).
- c74 bpl block (0x01002c76, bpl.s @ 0x2c7e) compiled and validated.

=> The EXISTING Bcc codegen (op_6a01 BPL.B) is CORRECT for the c74 case, now proven
by an oracle with teeth on BOTH dimensions. The simple short-backward-branch path
works.

### Important caveat (the risk is NOT at c74)
c74 is a SIMPLE short backward branch. The 4 prior Bcc reverts
(full-drop/pure-terminator/forward-only/flag-boundary-sync) failed on the COMPLEX
endblock/direct-chain/extension-word cases — NOT representable by c74. So "c74 Bcc
clean" is necessary but NOT sufficient: it proves the simple case + validates the
oracle end-to-end on real Bcc, but the boot-advancing deep-RAM Bcc blocks
(0x0438xxxx) include the complex cases the barrier was protecting against.

### Next (the actual codegen fight — fresh, oracle-watched, incremental)
The "land Bcc" single-SHA = narrowing/removing current_is_bcc from trace_barrier_op
so Bcc blocks compile for real. A naive global drop is the KNOWN-FAILING path (the
4 reverts). The disciplined route: extend the proven first-compile spot-check to the
deep-RAM Bcc blocks (the arm), let the trustworthy oracle catch the complex-case
endblock/direct-chain divergences AS they compile, fix per-case, gate each on
4-matrix-clean + compile_block-up + 75/75 & 32/32 single-SHA — or surface a scope
call if architecturally stuck. This is a substantial high-revert-risk effort: do it
fresh with the oracle watching, not rushed.

---

## DEEP-RAM Bcc fight — oracle FIRED on a REAL codegen bug (reproduced + disambiguated + localized)

Fresh slot (auditor-resumed). Extended the first-compile spot-check to deep RAM.

### Reachability PROVEN
Harvest (B2_JIT_BARRIER_STATS, no drop): the dominant Bcc barrier hot-spot IS
deep-RAM — pc samples cluster at 0x04382df4 / e2c / e08 / d96 (bcc grew
2.9M->4.0M over the window; dbcc plateaued ~1.097M). So "0x0438xxxx = the 60%"
is CONFIRMED (earlier doubt about it was wrong — the brief 0x04380882->043808d8
NextBus bus-error probe is a different, one-shot region).

Tight-window spot-check (B2_JIT_LOCKSTEP_PCS=0x04382c00-0x04383200, DROP=bcc,
REGONLY) ARMED at the real hot block (359 LSDBG) — deep-RAM reachability works.

### Oracle FIRED — field=d0, dut runs one op AHEAD of gold
LOCKSTEP_DIVERGE field=d0 at MOVE.B ops (1010/1030), e.g. step2 pc=04382da6
gold d0=07fffc00 dut=07fffc08; dut's value at op N == gold's value at op N+1
(dut one memory-element ahead). gold reads the SAME real memory as dut
(ls_step_gold uses cpufunctbl[] on real RAM, no separate shadow), so a same-PC
byte divergence = native execution diverging from interpreter ground truth.

### Disambiguated: REAL codegen bug, NOT a harness artifact
Candidate artifact: lockstep gold double-reading shared ESP device memory
(side-effecting reads). Ruled OUT: compiled the block with gold stopped after 1
step (MAXSTEPS=1, no ongoing double-reads) — boot STILL stalls identically at
0x04382dde (Bus error $0211a004, 95 ESP commands vs 329 in pure interpreter).
0x0211a004 NEVER appears in the interpreter harvest => codegen-only. The
trustworthy oracle correctly caught a real bug.

### Block + fault localization
Loop 0x04382d9c-0x04382e2c (byte-scan/checksum):
  d9c 2240 move.l d0,a1     d9e 2429 move.l (d16,a1),d2   da2 2069 move.l (d16,a1),a0
  da6 1010 move.b (a0),d0   da8 2079 move.l (abs.L),a0     dae 203c move.l #imm,d0
  db4 1030 move.b (d8,a0,Xn),d0  ... dc6 1030 ... dda 1030  (indexed reads)
  df4 6706 beq.s            e2c 62f4 bhi.s (backward loop branch)
The indexed read feeding 0x04382dde computes out-of-range 0x0211a004
(ESP base 0x02114000 + 0x6004 overrun) -> bus error -> boot stall.

### Mechanism hypothesis (endblock/direct-chain class — the 4 prior reverts)
These addressing modes ((d16,An),(abs.L),(d8,An,Xn)) compile correctly in
straight-line ROM. The bug is CONTEXTUAL to this deep-RAM block compiling +
chaining across its backward Bcc (e2c). Leading hypothesis: native continuation
carries wrong register state (the indexed base/index) across the chained backward
branch, so the EA overruns. Divergence appears from the block's first compared op
=> could also be wrong CHAIN-ENTRY state rather than an internal-op bug. Next
root-cause step: instrument to capture a0/Xn at the first divergence and at the
chained re-entry, to pin chain-entry-state vs internal-op vs index-carry. Honor
the hard tripwire: if endblock/direct-chain is architecturally broken with large
blast radius, name the mechanism + surface the scope call — do NOT revert-churn.

### REFINED root cause — localized DATA-VALUE op bug, NOT architectural (chain + address refuted)

Two decisive narrowing tests:

1. CHAIN refuted: B2_JIT_FORCE_NONDIRECT_HANDLER=1 (direct-chain OFF) + DROP=bcc +
   MAXSTEPS=1 — boot STILL stalls at 0x04382dde (94 ESP cmds vs 95 chained). Not
   the direct-chain state-carry.

2. ADDRESS refuted via block-verify (native-vs-interp full compare, the better tool
   here — no gold device double-read): block 0x04382d9c len=25 mismatch=1 with
   regs=1 ctrl=0 flags=0 mem=0 — ONLY d0/d1 diverge; ALL a-registers + monitored
   memory MATCH. So the indexed reads hit the RIGHT addresses; the bug is in the
   DATA-VALUE arithmetic, not the EA.

Divergence signature: reg[0]/reg[1] interp=0008c6c6 native=0008c6c7 (off-by-one
low bit; magnitude data-dependent across entries, e.g. 0008c6e0 vs 0006e000). The
block assembles a 24-bit value: move.b (d8,a0,Xn),d0 reads + clr/move.b d0,d1 +
swap d1 + clr.w d1 + asl.l #8,d0 + or.l d0,d1 (x2) + andi.l + move.l d2,d0 +
eor.l d1,d0 + btst + beq. The native byte-assembly/shift/or/eor produces a value
off by one low bit -> btst/beq mis-branches -> eventual EA overrun to 0x0211a004.

=> This is a TRACTABLE per-op arithmetic codegen bug in the byte-assembly sequence
(asl.l #8 / or.l / move.b-to-d1 / eor.l), NOT the endblock/direct-chain/extension-
word architectural class. The earlier lockstep per-op d0 "fire" was partly gold
device-double-read noise; block-verify is the trustworthy pinpoint here. NOT a
scope call. Next: native-disassemble block 0x04382d9c (B2_JIT_DUMP) to identify
which arithmetic op emits the off-by-one, fix that handler, re-verify
(block-verify mismatch=0 + boot past 0x04382dde + 75/75 & 32/32) -> single SHA.

### Mechanism PINNED = (b) internal codegen bug (auditor a/b/c discriminator resolved)

jit_block_verify_run seeds BOTH engines from the IDENTICAL entry snapshot
(jit_block_verify_entry_state restored before the interp re-run AND before the
native compile+run), then compares end state. Block 0x04382d9c diverges (d0/d1
off-by-one, a-regs+mem match) from that identical clean entry in a SINGLE block
execution. Therefore:
  (a) wrong state carried INTO the block (entry-state)  -> REFUTED (identical entry seed)
  (c) Xn/index drift across the backward-Bcc chain      -> REFUTED (single clean-entry block already diverges; no iteration/chain involved)
  (b) internal codegen bug inside the block             -> CONFIRMED
The defect is in this block's own emitted code: either op_1030's indexed
(d8,a0,Xn) source-EA via calc_disp_ea_020 brief path, or one byte-assembly
arithmetic op (move.b->d1 / swap / asl.l#8 / or.l / eor.l).

Per-op verify (B2_JIT_VERIFY_PCS) did NOT cleanly pin the op: under it the block
fell back / faulted at d9c (addr 0x08002077, just past RAM end) instead of
compiling, so no JITVERIFY lines — the verify instrumentation perturbs the trace.
Clean pin therefore needs native disassembly: instrument B2_JIT_DUMP to emit the
compiled bytes for block 0x04382d9c, disassemble the AArch64, and check the
indexed-EA emission (sign_extend_16_rr + lea_l_brr_indexed scale/disp8) and the
asl.l#8/or.l/eor.l emitters against the off-by-one. Then fix the single emitter,
re-verify (block-verify mismatch=0 + boot past 0x04382dde + 75/75 & 32/32) ->
single SHA. NOT a scope call — mechanism (b) is a localized handler fix.

### Emitter code-read narrowing (auditor's or.l/eor.l lead RULED OUT)

Read the candidate emitters directly (no boot):
- jnf_OR_l = ORR_rrr(d,d,s); jnf_EOR_l = EOR_rrr(d,d,s) — CLEAN. The or.l(de4)/
  eor.l(dee) here are no-flags (beq consumes btst's Z, not the or/eor), so they
  hit these clean reg-reg paths. Auditor's "low bit from or.l/eor.l" lead RULED OUT.
- lea_l_brr_indexed: offset is IM8 = uae_s32 (signed); negative disp8 -> SUB s,-offset
  correctly. EA-offset path CLEAN.
Remaining suspects (need disasm to pin): sign_extend_16_rr (word-index extension in
calc_disp_ea_020 brief), the move.b readbyte zero-extension into d0, asl.l#8 (e180),
swap d1 (4841), or the andi.l masks. Bit-0 corruption with data-dependent magnitude
(0008c6c6/c7 small vs 0008c6e0/0006e000 large) is most consistent with a WRONG BYTE
being read (indexed EA index/scale/extension), not the reg-reg combiners. Definitive
pin = B2_JIT_DUMP native disasm of block 0x04382d9c mapped op-by-op. Then careful
single-emitter fix + re-verify (block-verify mismatch=0 on 0x04382d9c + boot past
0x04382dde + 75/75 & 32/32) -> single SHA. Still mechanism (b), still not a scope call.

### DISASM REFRAME — EA codegen is CORRECT; block reads side-effecting IO; earlier block-verify conclusion CORRECTED

Native-disassembled block 0x04382d9c (B2_JIT_DUMP_BLOCK, reverted after).
Brief ext word for all three indexed move.b = 0x0800 = LONG index D0, scale 1,
disp 0. Each is preceded by move.l #0x0201a00X,d0, so EA = A0 + D0 (D0 a constant
0x0201a001/2/3). Native emission for db4/dc6/dda is IDENTICAL and CORRECT:
  ldr w17,[x28,#32] (A0); ldr w16,[x28] (D0); add w15,w17,w16 (EA=A0+D0)
then readbyte helper, bfxil byte into D0. => The indexed-EA codegen is CORRECT.
The "off-by-one arithmetic/EA op" hypothesis is REFUTED by disasm.

A0 + 0x0201a00X lands in IO space: the stall is Bus error $0211a004 (ioMem.c) —
these are DEVICE-REGISTER reads, not RAM. Two consequences:

1. block-verify is UNRELIABLE for this block (CORRECTS the earlier "(b) internal,
   (c) refuted" conclusion). block-verify runs interp THEN native, both reading the
   side-effecting IO device (status-clear-on-read / FIFO-advance), so the single-
   block "d0/d1 off-by-one from identical entry" is a DEVICE-DOUBLE-READ ARTIFACT,
   not a codegen divergence — the same flaw as gold-lockstep. NO interp-comparison
   oracle validates an IO-polling block.
2. Device-read ROUTING is not the bug either: B2_JIT_ALL_SPECIAL_MEM=1 (force all
   reads device-aware) STILL stalls identically at 0x04382dde (95 vs 329 ESP cmds).

REAL mechanism = loop-carried / cross-block, NOT a single op. D0 is a const (same
both engines) and the EA codegen is correct, yet native lands at an INVALID IO addr
(0x0211a004) while interp stays valid — so A0 diverges at runtime. A0 = *(0x0439f538)
(a RAM pointer, loop-carried). The block ends in bsr.l (0x61ff @ e22, itself
mis-classified as "bcc") + backward bhi.s loop (e2c). So compiling the loop + its
bsr callee diverges over iterations via the 0x0439f538 pointer -> A0 corrupts ->
invalid IO addr -> bus error -> stall.

USABLE oracle remains: boot-progress delta (native-only 95 ESP cmds vs interp 329)
is oracle-free and does NOT suffer the device-double-read problem — so a fix CAN be
validated by boot-advance even though per-op/block oracles cannot. But the bug is
loop-carried through the bsr callee + RAM pointer, NOT this block's emitted ops.
This is materially harder than a single-handler fix and approaches the architectural
device-polling-loop class. Decision surfaced to auditor: deep cross-block/callee
disasm (bigger effort) vs scope call. NOT churning a speculative op fix.

### ROOT CAUSE FIXED — bsr.l was mis-classified as Bcc (auditor lead confirmed)

The deep-RAM "Bcc codegen bug" was largely a CLASSIFICATION error, not a Bcc-codegen
defect. current_is_bcc = ((op & 0xf000)==0x6000 && op != 0x6000) swept in BSR
(0x61xx) — but bsr is a subroutine CALL, not a conditional branch. So dropping the
Bcc barrier (B2_JIT_LOCKSTEP_DROP=bcc, and any eventual Bcc-barrier removal) ALSO
compiled the bsr through the conditional-branch trace path, breaking call/return and
corrupting the loop-carried pointer (A0 = *(0x0439f538)) -> invalid IO addr
0x0211a004 -> bus-error stall at 0x04382dde.

PROOF (oracle-free boot-delta, immune to the device-double-read problem that makes
block-verify/gold-lockstep invalid for this IO-polling block):
- DROP=bcc with bsr ALSO dropped: 95 ESP cmds, stuck at 0x04382dde.
- DROP=bcc with bsr kept BARRIERED: 895 ESP cmds, boot reaches DMA init (0x0407c03c).
=> The deep-RAM Bcc LOOP codegen is SOUND; the stall was the bsr confound. This also
retroactively explains the device-read "off-by-one" artifacts: the loop was running
with a corrupted A0 from the mis-compiled bsr.

FIX (single change, compemu_legacy_arm64_compat.cpp): classify bsr (0x61xx) as a CALL
(current_is_bsr), exclude it from current_is_bcc, and barrier it unconditionally like
jsr_jmp. No default-boot behavior change (bsr was already barriered when !drop_bcc);
the fix ensures a Bcc drop/removal never compiles a call.

GATE: 75/75 opcode harness score=100; 32/32 MMU fast smoke score=100; boot-delta
95->895 ESP cmds + DMA init reached. RESIDUAL: a 0x04382dde spin still recurs later
in the run (after DMA init) — a separate frontier to diagnose next, no longer the
bsr confound.

---

## SESSION 2026-06-22 — pure-JIT recompile churn: root cause found, fix selected

**Date:** 2026-06-22
**Repo:** `previous` (rcarmo-jit/main), HEAD `b8b9829`
**Status:** Diagnosis **complete and probe-confirmed**. Fix **selected** (self-resolving
chain thunk). Five simpler candidates **empirically eliminated**. Only the
(high-blast-radius, lockstep-gated) implementation remains.

### TL;DR (current)

- **The campaign goal is empirically reachable.** A pure-interpreter boot reaches the
  kernel handoff region **`PC=0x050542A0`** (goal `0x05054296`), then hits an
  unimplemented **FPU packed-decimal op `OP=F210-4C00`** — a separate, later frontier.
  So nothing structural prevents reaching the kernel; only the JIT path stalls earlier.
- Under **pure-JIT barrier drops** (the zero-fallback goal), the boot-critical
  loops *do* compile, but the **NextBus board-ROM checksum loop at `0x01002c74`
  recompiles ~7000× each iteration** (`compile=49513`, `recomp=49506`) instead of
  running natively → never advances to the kernel.
- **Root cause (probe-confirmed):** a **stale forward-edge native chain to the
  `popall_execute_normal` trampoline** that bypasses a *correct, consistent*
  `cache_tags` dispatch. Not a count/optlevel issue, not a cache_tags inconsistency,
  not a flush, not a key/cacheline problem — all refuted with data.
- **Fix:** make endblock chains target a **self-resolving thunk** (resolves the live
  handler via `cache_tags` on hit; cannot persist a trampoline by construction).

### What this region actually is

The `0x01002c72–0x01002c8c` loop is the NeXT ROM's **board-presence CRC/checksum**:
`MOVE.B (A1)+,D1` over a contiguous RAM buffer, inner `DBF D2` (8 bytes) doing
`LSL/ROXR/ROR/EOR` bit-mixing, outer `BNE` on a `D3 ≈ 0x20000` countdown — a bounded
128 KB checksum. Instrumentation (`B2_INTERP_LOWPC_TRACE`) proved it is **not** an
interrupt-wait (`spcflags=0` throughout) and **not** a device poll (`A1` walks RAM
sequentially, not a fixed MMIO address) — it is pure computation. The earlier
NextBus `F2FFFFF0` bus-error is a **correct board-presence probe-miss** (interp hits it
the same 2×); the hang is JIT-specific, not a device-emulation gap.

### Why it churns (the mechanism, probe-by-probe)

1. **`exec_normal` is pessimal.** With un-dropped barriers (DBcc/Bcc), the region runs
   trace-build → hit-barrier → interpret-one-op → repeat, ~tens× slower than pure
   interpretation. Barrier coverage is therefore *throughput-critical*, not just a
   correctness nicety.
2. **Dropping the barriers compiles the loop but it recompiles ~7000×.** `recomp≈compile`
   with only `fresh=7` distinct blocks (the dead `cache_hit` counter is ignored here).
3. **Arrival-path probe (39/40 → A_CHAIN):** at `check_for_cache_miss`, the recompiled
   blocks are `BI_ACTIVE`, `is_primary=1`, and `cache_tags[cl].handler` is the **compiled**
   handler (not `execute_normal`) — the dispatch state is *perfect*. The only way to reach
   `execute_normal` with a correct cache_tags handler is a **native chain hardcoded to the
   `popall_execute_normal` trampoline that bypasses cache_tags**. ⇒ root is **(A)** a stale
   chain; **(B)** a cache_tags handler/bi inconsistency is **refuted**.
4. **Cycling driver (dep probe):** the DBF fall-through edge `c86→c8a` is created while
   `c8a` is INVALID → chain written to the trampoline. `set_dhtu(c8a)` *does* walk and
   `adjust_jmpdep` that exact slot when `c8a` compiles (`will_adjust=1`), but it **races a
   continuous re-invalidation**: stale chain → recompile → new instance supersedes old →
   target INVALID → chain re-staled. The per-event re-point is correct but never reaches
   steady state. **Self-sustaining.**

### Candidates eliminated with data (no wrong line committed)

| Candidate | Verdict |
| --- | --- |
| canonical/raw PC key, phantom blockinfo | refuted (chain probe 1: `raw==canon`, no phantom) |
| stale **back-edge** chain | refuted (chain probe 2: back-edges target ACTIVE compiled handlers; `set_dhtu` fires) |
| flush-driven invalidation | refuted (`flush_hard=0`, `check_checksum good=0/bad=0`) |
| intra-loop cacheline collision | refuted (6 blocks, all-distinct cachelines) |
| count-graduation / `cpu_compatible` flag | refuted (Previous **already** runs the JIT at `cpu_compatible=false`; forcing it changed nothing — `compat=0, max_optlev=2, ram_disp=1`) |
| DBcc handler in isolation | refuted (handlers already exist; not the gate) |
| direct-edge promotion (piece-b) | refuted/banked (operates above the churning layer; byte-identical ON/OFF) |
| **return-1-for-active-primary** (re-dispatch instead of recompile) | **kills the churn** (`recomp 49506→0`) **but bounce-spins** through `execute_normal` without advancing (`exec_normal=736M`, PC pinned at `c76`). Proves native chaining is required, not merely no-recompile. |

### The fix (selected)

**(ii) Self-updating thunk**, as the *general* endblock chain-target mechanism (not a
one-edge patch — the bounce-spin showed a re-dispatched block immediately hits the *next*
un-thunked edge). On hit, the thunk loads the live `cache_tags[cacheline(target)].handler`
and jumps to it (optionally patching the chain), so it **cannot persist a trampoline by
construction** — robust to the re-invalidation race rather than trying to win it. Keep the
fix to this one mechanism (`return-1` is redundant once chains run native).

### Validation gate (hard) for the implementation

1. Dropped checksum loop runs **native**: `recomp` bounded **and** `exec_normal` plateau
   (not the 736M bounce-spin) **and guest PC advances past `c76`** (the `D3` outer counter
   decrements / loop completes).
2. Boot advances **`0x01002c74 → 0x050542A0`**.
3. **Baseline regression:** drops-OFF behavior byte-identical to baseline.
4. **Lockstep** (note: the lockstep validator has a known *next_pc blind spot* for
   end-of-block transfers — set up branch-target verification for chaining changes).
5. **Post-fix check** (answers the last open sub-question for free): does re-invalidation /
   cache-growth stop? If yes, the churn *was* the whole story; if not, a separate
   invalidation source (flush/cache-reclaim) remains to pin. Either way the thunk is correct.

### Reference

BasiliskII (`/workspace/projects/macemu/BasiliskII/src/uae_cpu_2026/compiler/`) is the same
JIT family and boots further — read its endblock chain-target handling (thunk vs raw handler
address) as a reference, but **verify empirically** (a static B2 cross-read misfired earlier
this session — Previous's effective `cpu_compatible` is `false` at runtime despite the Hatari
`bCompatibleCpu=true` config layer, which does not propagate to the JIT).

### Shipped this session (default-OFF, inert)

- `a026529` `drop_bsr` — JIT barrier drop for BSR (default-off, env-gated)
- `2ae8892` `drop_jsr` — JIT barrier drop for `JSR (An)` op `4e90` (default-off)
- `ebde577` `tools/fullloop-drop-validate.sh` — full SCSI-loop barrier-drop boot harness
- `b8b9829` `tools/interp-nextbus-probe.sh` — pure-interp NextBus/kernel-reachability reference

The production-ON flip of the drops remains gated on lockstep certification of the
BSR/RTS/JSR control-transfer path.

### Full investigation record

The complete, self-corrected probe-by-probe chain (including the reverted diagnostic patches
and exact log signatures) is maintained in the workspace note
`notes/case1-scsi-spin-finding.md`.

## CORRECTION (2026-06-23): 9ecfc74/e04d965 IS the SCSI chain fix, not "pending"

The commit message for 9ecfc74 ("eliminate recompile-churn") labeled itself a re-dispatch SAFETY NET with
"fast-path chain resolution still pending." A runtime region-split of check_for_cache_miss calls (with the fix
in) corrects that: scoped-SCSI shows scsi(0x04382xxx)=0, other(0x0100xxxx)=1.8e9 slow-path calls. The SCSI
region hits the slow path ZERO times => it FAST-CHAINS on the inline path.

So 9ecfc74 IS the chain fix for compiled regions, not a band-aid: recompiling a valid BI_ACTIVE block every
iteration was invalidating its create_jmpdep chains (recompile -> new handler -> predecessor's emitted branch
stale -> popall -> execute_normal -> recompile). Stopping the needless recompile keeps the chains valid so they
resolve to the fast path. Validated: recomp 62581->0, scsi=0 slow-path, boot past SCSI, opcode 75/75, mmu-fast
32/32.

The set_dhtu / guard / ordering hypotheses were all refuted by parity diff + measurement (byte-identical to
BasiliskII); the lever was the recompile, not set_dhtu. The REMAINING grind (0x010068fa / 0x01007e52) is pure
REGION COVERAGE — non-scoped ROM regions running interp barriers — NOT chaining. Next frontier = the SEPARATE
early-ROM 0x010005xx bad-pc_p hang under global native-compile (distinct bug). Chain bug: CLOSED + validated.
