# UAE2026 RAM/MMU JIT correctness contract

This is the audit contract for completing `PREVIOUS_UAE2026_JIT_RAM=1` without
chasing one PC frontier at a time.  The JIT is correct only when every translated
operation has the same externally visible 68040 behavior as Previous's existing
MMU interpreter at exception, retry, and interrupt seams.

The goal is not to make the current boot progress by local patches.  The goal is
to prove that the bridge and generated code preserve the architectural state that
68040 software depends on.

## Source of truth

Use these sources in this order:

1. Documented 68040 behavior: address translation by function code, access-error
   frame contents, SSW/continuation bits, restartability, ATC maintenance,
   `PTEST`/`PFLUSH`, `RTE`, and interrupt priority rules.
2. Previous's non-JIT 68040 MMU interpreter implementation.
3. A short JIT-vs-interpreter discriminator that exercises one seam and finishes
   within 120 seconds.

A JIT heuristic is acceptable only if it can be tied to one of those sources.  If
it exists only because it moved a historical symptom, treat it as temporary until
it is replaced with explicit metadata or removed.

## Contract clauses

### 1. Restart snapshot before faultable work

Before translated code calls any helper that can enter the 040 MMU and longjmp to
exception delivery, it must publish a complete pre-access snapshot:

- `regs.fault_pc`
- `regs.instruction_pc`
- `Uae2026JitLastInstructionPc`
- pre-op `SR`
- pre-op `A7`, plus the active `USP`/`ISP`/`MSP` meaning of that stack
- current condition codes / `Uae2026JitLastFlags`
- `mmu_opcode` (`-1` before opcode fetch, fetched opcode after opcode fetch)
- restartability intent (`mmu_restart=true` unless the exact 040 path marks the
  fault non-restartable)

Rationale: exception-frame construction must describe the instruction that
faulted, not the partially mutated state after native code has already advanced
PC, stack, flags, or address registers.

### 2. Code, data, and CPU-space translations are distinct

The JIT must not use one translation path for all memory:

- opcode fetch / dispatch / branch target: code-space translation
- ordinary data EA: data-space translation
- `MOVES`, table walks, and SFC/DFC accesses: function-code-aware CPU-space path

Rationale: the 68040 MMU translates by access type and function code.  A stale or
wrong address-space view can execute correct bytes but produce wrong fault state,
or can fault on data where the interpreter would fetch code.

### 3. Side effects before faultable accesses are transactions

If translated code mutates architectural state before a later faultable access,
that mutation must be represented explicitly as a transaction:

- auto-increment / predecrement address updates
- `BSR`/`JSR` return-address push before target fetch
- `RTS`/`RTR` return-address pop before target fetch
- `RTE` SR/stack switch before return-code fetch
- `MOVEM` predecrement / continuation state
- trap/exception-frame construction before nested faults

A transaction must define:

- the pre-side-effect state
- the side effect that has happened
- the faultable operation that follows
- rollback or completion behavior on access error
- when the transaction commits

Rationale: bridge-side opcode scans are fragile.  The generated code knows the
exact side effect and must publish it before the faultable access.

### 4. 68040 exception-frame and SSW state must be interpreter-equivalent

For access errors, the frame must preserve the same semantic fields as the
interpreter path:

- fault address vs continuation effective address
- read/write, size, and function-code information
- `MMU_SSW_CM` continuation semantics
- `mmufixup[]` register recovery
- non-restartable write visibility
- post-PC value where the interpreter reports a non-restartable post-advance
  fault

Rationale: NeXTSTEP handlers inspect and repair state based on the frame.  A
frame that uses the bus fault address where the 040 continuation EA is required
can restore the wrong register frame even if the immediate fault is handled.

### 5. ATC/MMU maintenance must be exact before retry

`PTEST`, `PFLUSH`, `PLPA`, `PMOVE`, and related MMU operations must update the
same ATC/MMU state as the interpreter before a faulting access is retried.

Current status: RAM/MMU JIT keeps vendored `i_MMUOP` exact.  This removed the
repeated `0501288e` retry fault, so this clause is currently a proven required
barrier, not an optimization detail.

Rationale: retrying a page-in access against stale ATC state recreates the same
fault indefinitely even when the guest handler did the right thing.

### 6. Interrupt/timer pending state is architectural at JIT seams

The JIT must preserve the same interrupt progression as the interpreter:

1. cycle accounting advances `PendingInterrupt.time` appropriately;
2. expired pending handlers are called;
3. device/timer handlers set `InterruptFlags` and/or call `TriggerInterrupt()`;
4. `SPCFLAG_INT` is surfaced;
5. compiled code exits promptly when `SPCFLAG_ALL` becomes nonzero;
6. specialties promote `SPCFLAG_INT` to `SPCFLAG_DOINT`;
7. `intlev()` returns a deliverable level when IPL allows it;
8. `Interrupt()` builds the 68040-visible interrupt frame and updates IPL.

Rationale: the current native no-handoff frontier is no longer a repeated page
retry.  The idle loop polls wakeup words that remain zero, and diagnostics show
`InterruptFlags=0` while tick traces show pending timer state.  That makes lost
wakeups/timer delivery a correctness seam, not a scheduler-loop opcode bug.

### 7. JIT dispatch exits are part of correctness

It is not enough for generated code to set `regs.spcflags`; generated code must
also stop executing the current compiled path soon enough for specialties to run.
This includes:

- checks after helper calls that can trigger interrupts;
- mid-block tick checks for long blocks;
- block-end checks;
- dispatch-loop checks before re-entering native code;
- preserving current PC/SR/flags before exiting to specialties.

Rationale: a pending interrupt that remains hidden until a long hot block exits
can starve the guest and make idle-loop diagnostics misleading.

## Current frontier interpreted through the contract

Known good:

- Code/data split and code-shadow sync fixed earlier low-virtual stale opcode
  frontiers.
- Call/return transaction work fixed known stack-shift frontiers.
- Exact MMUOP fixed the repeated `0501288e` retry fault.

Current suspicious clause:

- Clause 6/7: timer and interrupt delivery while RAM/MMU JIT remains active.

Evidence:

- Native no-handoff idle samples show the three scheduler/wakeup poll words and
  `InterruptFlags` all remain zero.
- `B2_JIT_TICKTRACE=1` shows `PendingInterrupt.type=1` and positive
  `PendingInterrupt.time` while JIT is active around `04382dxx`, with
  `spc=0`, `intlev=0`, and `intmask=7`.

Audit question:

> Does native RAM/MMU JIT advance cycles and execute pending handlers in the same
> way as the interpreter, and if it does, where is the pending interrupt lost
> before the guest-visible wakeup state changes?

## 120-second discriminator rules

Every discriminator must record:

- contract clause under test;
- hypothesis;
- command;
- timeout cap, always `<=120s` unless explicitly approved;
- expected signal;
- observed signal;
- conclusion and eliminated hypotheses.

Preferred short discriminators for the current frontier:

1. **Tick expiry trace** — prove whether `PendingInterrupt.time` reaches `<=0`
   under RAM/MMU JIT with the existing build.
2. **Handler invocation trace** — prove whether the expired pending handler runs.
3. **Interrupt surfacing trace** — prove whether `TriggerInterrupt()` sets
   `SPCFLAG_INT` / `InterruptFlags`.
4. **Specialty promotion trace** — prove whether `SPCFLAG_INT` becomes
   `SPCFLAG_DOINT` and whether `intlev()` returns a deliverable level.
5. **Dispatch-exit trace** — prove whether compiled code exits promptly when
   `spcflags` changes.

Do not run broad exact-PC sweeps or long desktop validation to choose a fix.

## Audit pass 1: current tick/interrupt frontier

This audit pass focuses only on the current no-handoff frontier and does not
change code.  The purpose is to identify which contract clause needs a short
proof discriminator.

### Relevant source paths

| Path | Responsibility | Contract relevance |
| --- | --- | --- |
| `src/includes/m68000.h::M68000_AddCycles()` | Advances cycle counters, decrements `PendingInterrupt.time` for CPU-cycle interrupts, and samples microsecond timers via `usCheckCycles`. | Clause 6.1: cycle accounting must make pending timers expire while JIT is active. |
| `src/cycInt.c` | Maintains the pending-interrupt table, selects `PendingInterrupt`, and acknowledges/reschedules handlers. | Clause 6.1/6.2: expiry and handler dispatch. |
| `src/m68000.c::Uae2026JitCpuCheckTicks()` | JIT tick bridge: adds cycles, runs DSP/i860, executes expired pending handlers, then asks `intlev()` whether an interrupt should be surfaced. | Clause 6.1-6.4. This is the first native-JIT-specific timer seam. |
| `src/cpu/uae_cpu_2026/basilisk_glue.cpp::TriggerInterrupt()` | Device/timer side interrupt surfacing; sets `SPCFLAG_INT` and records trace state. | Clause 6.3/6.4. If handlers do not call this or set `InterruptFlags`, the guest never sees wakeup state. |
| `src/cpu/uae_cpu_2026/basilisk_glue.cpp::intlev()` | Returns a deliverable level from `InterruptFlags` or deferred IRQ state. | Clause 6.7. Current traces report `intlev=0`. |
| `src/cpu/uae_cpu_2026/newcpu.cpp::m68k_do_specialties()` | Promotes `SPCFLAG_INT` to `SPCFLAG_DOINT`, calls `intlev()`, and delivers `Interrupt()`. | Clause 6.6/6.8. |
| `src/cpu/uae_cpu_2026/compiler/compemu_support.cpp::m68k_do_compile_execute()` | JIT dispatch loop; calls `Uae2026JitCpuCheckTicks(1024)` every 256 dispatches, calls `jit_one_tick()` when `jit_countdown < 0`, and exits to specialties when `SPCFLAG_ALL` is set. | Clause 7. |
| `src/cpu/uae_cpu_2026/compiler/compemu_support_arm.cpp` mid-block tick injection | For long blocks, emits `cpu_do_check_ticks()` and an immediate `spcflags` exit check every 64 traced instructions. | Clause 7. |

### Current evidence

Short log analysis from `/workspace/tmp/previous-jit-ticktrace-nohandoff-20260528-063101`:

- JIT RAM dispatch is active and no interpreter RTE handoff is used.
- `JITTICK` fires, so `Uae2026JitCpuCheckTicks()` is being called.
- `PendingInterrupt.type=1`, i.e. `CYC_INT_CPU`.
- `PendingInterrupt.time` changes over time and sometimes resets upward, which
  suggests cycle accounting and at least some pending handler/reschedule activity
  is happening.
- The same samples show `spc=0`, `intlev=0`, and `intmask=7`.
- The idle-poll diagnostic shows `InterruptFlags=0` and the three polled wakeup
  longwords remain zero in the `040674xx` loop.

### Audit interpretation

The first broken-looking seam is not yet proven to be cycle accounting.  The
existing trace shows the JIT tick bridge runs and `PendingInterrupt.time` is not
constant.  The more likely unproven seam is between expired pending handler
execution and guest-visible interrupt/wakeup state:

```text
PendingInterrupt expires
  -> CALL_VAR(PendingInterrupt.pFunction)
  -> handler should update device/timer state and/or call TriggerInterrupt()
  -> InterruptFlags/SPCFLAG_INT should become nonzero
  -> JIT should exit native code and run specialties
```

Current traces prove only the beginning and end of that chain: tick checks happen,
but no guest-visible interrupt/wakeup is present at the sampled frontier.  They do
not yet prove whether the handler ran, whether `TriggerInterrupt()` ran, or whether
`SPCFLAG_INT` was set and then lost.

### Short discriminator priorities

All discriminators must use the existing build and `timeout <= 120`.

1. **Tick expiry / handler discriminator**
   - Rationale: prove whether `PendingInterrupt.time <= 0` is reached and whether
     the JIT bridge executes the pending handler.
   - Needed signal: trace before/after the `while (PendingInterrupt.time <= 0 &&
     PendingInterrupt.pFunction)` loop, including whether the loop body ran.
   - If existing logs are insufficient, add a default-off diagnostic before any
     semantic fix.

2. **Interrupt surfacing discriminator**
   - Rationale: prove whether expired handlers call `TriggerInterrupt()` or set
     `InterruptFlags`.
   - Existing hook: `B2_TRACE_IRQMANAGED=1` logs `IRQM trigger`, but only inside
     deferred mode today.  If deferred mode is off, a default-off unconditional
     trigger trace may be needed.

3. **SPCFLAG dispatch-exit discriminator**
   - Rationale: if `SPCFLAG_INT` is set, prove compiled code exits and
     `m68k_do_specialties()` sees it.
   - Existing hooks: `B2_TRACE_INTDECIDE=1` / `B2_TRACE_IRQMANAGED=1` may be
     enough when `SPCFLAG_INT` is actually set.

### Hypotheses eliminated for now

- A local `040674xx` opcode bug is not supported: the loop is a poller and the
  polled state remains zero.
- Another local `0501288e` MMU retry fix is not supported: exact `i_MMUOP`
  removed that repeated fault.
- A broad exact-PC sweep is not justified: the suspicious seam is now timer and
  interrupt state publication while native JIT remains active.

### Discriminator 1: tick expiry under native RAM/MMU JIT

- Contract clause: Clause 6.1, cycle accounting must expire pending timers while
  RAM/MMU JIT remains active.
- Hypothesis: native no-handoff stalls because JIT tick accounting never drives
  `PendingInterrupt.time` to expiry.
- Command cap: 120s.
- Command summary: existing `build-vnc/src/Previous`, headless harness with
  `PREVIOUS_UAE2026_JIT=1`, `PREVIOUS_UAE2026_JIT_RAM=1`,
  `B2_JIT_RTE_FAULT_HANDOFF_DISABLE=1`, `B2_JIT_TICKTRACE=1`, shortened harness
  waits.
- Artifact: `/workspace/tmp/previous-jit-discriminator-tick-expiry-20260528-082204`.
- Expected signal: at least one `JITTICK` sample with `PendingInterrupt.time <= 0`.
- Observed signal:
  - `JITTICK_COUNT=90`
  - first sample: `pc=010005fa`, `pending_type=1`, `pending_time=366601`,
    `spc=0`, `intlev=0`, `intmask=7`
  - last sample: `pc=04382db4`, `pending_type=1`, `pending_time=-10393`,
    `spc=0`, `intlev=0`, `intmask=7`
  - `MIN_PENDING_TIME=-10393`
  - `NONZERO_SPC_SAMPLES=0`
  - `NONZERO_INTLEV_SAMPLES=0`
- Conclusion: cycle countdown expiry is not the first broken clause.  The next
  unproven seam is expired handler execution and interrupt/wakeup surfacing:
  `CALL_VAR(PendingInterrupt.pFunction)` -> `TriggerInterrupt()` /
  `InterruptFlags` / `SPCFLAG_INT`.
- Eliminated hypothesis: the idle stall is caused solely by `PendingInterrupt.time`
  never reaching zero under RAM/MMU JIT.

### Discriminator 2: interrupt surfacing with existing managed-IRQ traces

- Contract clause: Clause 6.2-6.7, expired handlers must surface guest-visible
  interrupt state and dispatch must promote/deliver it.
- Hypothesis: existing `B2_TRACE_IRQMANAGED` / `B2_TRACE_INTDECIDE` hooks are
  enough to show whether `TriggerInterrupt()`, deferred `intlev()`, and
  `SPCFLAG_INT` promotion happen during a short RAM/MMU JIT run.
- Command cap: 120s.
- Command summary: existing `build-vnc/src/Previous`, headless harness with
  `PREVIOUS_UAE2026_JIT=1`, `PREVIOUS_UAE2026_JIT_RAM=1`,
  `B2_JIT_RTE_FAULT_HANDOFF_DISABLE=1`, `B2_JIT_TICKTRACE=1`,
  `B2_JIT_MANAGED_IRQ=1`, `B2_TRACE_IRQMANAGED=1`, `B2_TRACE_INTDECIDE=1`,
  shortened harness waits.
- Artifact: `/workspace/tmp/previous-jit-discriminator-irq-surface-20260528-082445`.
- Expected signal: at least one `IRQM trigger`, `IRQM intlev sample/accept`,
  `INTDECIDE`, or `spec-promote/spec-doint` line after tick expiry.
- Observed signal:
  - `JITTICK=87`
  - `IRQM_trigger=0`, `IRQM_sample=0`, `IRQM_accept=0`, `IRQM_busy=0`,
    `IRQM_consume=0`
  - `INTDECIDE=0`, `promote=0`, `doint=0`, `taken=0`
  - `MIN_PENDING_TIME=2200`, so this run did not reach expiry within the cap.
- Conclusion: inconclusive for interrupt surfacing.  The existing hooks are useful
  only after expiry, and this managed-IRQ run did not expire the pending timer in
  120 seconds.  Do not treat the absence of `IRQM` lines as proof that handlers
  fail to call `TriggerInterrupt()`.
- Next required proof: a default-off tick-handler discriminator in
  `Uae2026JitCpuCheckTicks()` that logs before/after the expired-handler loop,
  including whether the loop body ran and how `PendingInterrupt`, `InterruptFlags`,
  `spcflags`, `intlev()`, and `intmask` changed.  This is diagnostic only, not a
  semantic fix.

### Discriminator 3: expired handler execution at the RAM/JIT frontier

- Contract clause: Clause 6.2-6.4, expired pending handlers must run and surface
  guest-visible interrupt/wakeup state when appropriate.
- Hypothesis: `PendingInterrupt.time` expires but the JIT bridge never executes
  `CALL_VAR(PendingInterrupt.pFunction)`.
- Diagnostic added: default-off `B2_JIT_TRACE_TICK_HANDLER=1` around
  `Uae2026JitCpuCheckTicks()`'s expired-handler loop, with
  `B2_JIT_TRACE_TICK_HANDLER_LIMIT=N`.
- Build cap: incremental `cmake --build build-vnc -j$(nproc)` completed within
  the 120s cap.
- Run cap: 120s.
- Artifact: `/workspace/tmp/previous-jit-discriminator-tick-handler-tail-20260528-083235`.
- Observed signal:
  - `TICKS=90`, `JITTICK_HANDLER_POST=512`
  - late `JITTICK` samples reached `04382dxx` with the final sampled
    `pending_time=-10393`
  - handler loop body ran at the RAM frontier (`calls=1` in late
    `JITTICK_HANDLER_POST` rows around `04382d9c..04382e08`)
  - runtime handler pointers map to the PIE base `0xaaaaaaaa0000`:
    - `0xaaaaaab94844` -> `Main_EventHandlerInterrupt`
    - `0xaaaaaabaa2b0` -> `Video_InterruptHandler_VBL`
    - `0xaaaaaab8bae0` -> `ENET_IO_Handler`
    - `0xaaaaaab88f40` -> `ESP_InterruptHandler`
    - `0xaaaaaab96600` -> `ECC_IO_Handler`
    - `0xaaaaaaba7808` -> `Hardclock_InterruptHandler`
  - all logged post-handler states had `spc=0` and `pending_autovec=0`; intmask
    was either `7` or `5` in the logged rows.
- Conclusion: the expired-handler loop does run; the next seam is not handler
  invocation.  Some early handlers are not expected to assert a CPU interrupt
  (`Video_InterruptHandler_VBL`, `Main_EventHandlerInterrupt`).  Interrupt-capable
  handlers (`ESP_InterruptHandler`, `ENET_IO_Handler`, `Hardclock_InterruptHandler`)
  also execute during the short run, so the next audit target is how native JIT
  notices interrupt pins after handlers call `set_interrupt()`.
- Eliminated hypothesis: JIT stalls solely because expired pending handlers are
  never called.

### Audit finding: interrupt pin polling mismatch

Previous's interpreter loop does this after every interpreted instruction:

```c
while (PendingInterrupt.time <= 0 && PendingInterrupt.pFunction && !(regs.spcflags & SPCFLAG_STOP))
    CALL_VAR(PendingInterrupt.pFunction);
intr = intlev();
if (intr > regs.intmask || (intr == 7 && intr > lastintr))
    do_interrupt(intr, false);
lastintr = intr;
```

The native JIT path calls `Uae2026JitCpuCheckTicks()` periodically, and that
function also calls `intlev()`, but the dispatch loop otherwise decides whether
to leave native code from `SPCFLAG_ALL`.  Device handlers in the Hatari/Previous
side usually call `set_interrupt()`, which updates `intStat`; they do not
necessarily set `SPCFLAG_INT` themselves.  Therefore a pending interrupt can be
visible to `intlev()` without `SPCFLAG_ALL` being nonzero.

Contract implication: Clause 6/7 require native JIT to poll interrupt pins at an
architecturally equivalent seam, not only when a tick helper happens to run or
when `SPCFLAG_ALL` is already set.  The narrow fix target should be a dispatch
boundary interrupt-pin poll that mirrors the interpreter's `intlev()` check and
sets/handles the pending interrupt without relying on a local opcode patch.

### Fix target: dispatch-boundary interrupt pin polling

- Violated contract clause: Clause 6/7. The interpreter polls `intlev()` after
  every instruction, while native JIT dispatch previously relied on `spcflags`
  already being set or on a tick helper noticing a deliverable interrupt.
- Fix rationale: Hatari/Previous device handlers usually publish interrupt state
  by calling `set_interrupt()`, which updates interrupt status/mask registers.
  That state is visible to `intlev()` but does not by itself set `spcflags`.
  Therefore the RAM/MMU JIT dispatch loop must poll `intlev()` at native block
  boundaries and surface Previous's `SPCFLAG_INT` when `intr > regs.intmask` (or
  the NMI `intr==7 && intr>lastintr` rule matches).
- Implementation: `jit_poll_interrupt_pins_for_dispatch()` in
  `src/cpu/uae_cpu_2026/compiler/compemu_support.cpp`, gated to
  `PREVIOUS_UAE2026_JIT_RAM=1`.  It sets Previous's `SPCFLAG_INT` bit (`0x008`)
  and leaves actual delivery to the existing `m68k_do_specialties()` bridge.
- Diagnostic: `B2_JIT_TRACE_DISPATCH_INT=1` emits `JIT_DISPATCH_INT` when the
  dispatch poll surfaces a pending interrupt.

### Discriminator 4: dispatch interrupt poll after fix

- Contract clause: Clause 6/7, native JIT must notice pending interrupt pins even
  when no previous `SPCFLAG_INT` is already set.
- Command cap: 120s.
- Artifact: `/workspace/tmp/previous-jit-discriminator-dispatch-int-20260528-083837`.
- Expected signal: at least one `JIT_DISPATCH_INT` line if an interrupt is pending
  and deliverable at a dispatch boundary.
- Observed signal:
  - `JIT_DISPATCH_INT 1 pc=010036c4 intr=6 intmask=5 spc=00000008`
  - `JIT_DISPATCH_INT 2 pc=01007e7c intr=6 intmask=5 spc=00000008`
  - short run also had `tick=90`, `handler_post=128`, and only the expected early
    ROM probe MMU exceptions.
- Conclusion: the new dispatch-boundary poll is active and surfaces pending
  interrupt pins through Previous's `SPCFLAG_INT` bit.  This proves the intended
  contract seam is wired; it does not claim full desktop validation.

### Capped gates after dispatch poll fix

- Incremental build: `timeout 120 cmake --build build-vnc -j$(nproc)` passed.
- Broad MMU opcode filter (`sr_|scc_|dbvc|dbvs|chk2_|cas|movep_|movem_|moves_|movec_|jsr_|bsr_|seam_`) exceeded the 120s cap and was not counted.
- Narrow existing-build opcode gate: `/workspace/tmp/previous-opcode-small-dispatch-int-20260528-084308`, filter `seam_|bsr_|jsr_`, passed with `total=12`, `pass=12`, `fail=0`, `infra_fail=0`, `score=100`, `PREVIOUS_UAE2026_JIT_RAM=1`, and `B2_JIT_RTE_FAULT_HANDOFF_DISABLE=1`.

Full desktop/stability validation is intentionally not run here because it would
exceed the 120s cap; ask for explicit approval before doing that validation.

## Expanded missing-case audit matrix

This matrix inventories the known RAM/MMU JIT correctness case families from the
source, repository docs, notes, and recent fixes.  It is intentionally broader
than the current interrupt frontier.  Each row maps a family to the contract
clause it can violate, classifies current implementation status, and defines a
bounded proof target.  A long desktop/stability run is not a discriminator for
this audit.

Status vocabulary:

- **Proven exact** — source path directly uses the interpreter/MMU implementation
  or already has a matching short discriminator.
- **Guarded/exact fallback** — native lowering is avoided in RAM/MMU mode, usually
  by an interpreter barrier or a `compstbl` fallback.
- **Transaction-backed** — generated/fallback code publishes explicit side-effect
  metadata that the bridge can resolve after an MMU longjmp.
- **Heuristic compatibility shim** — the bridge infers state from opcode windows,
  fault addresses, or known PCs; keep only until producer metadata replaces it.
- **Unaudited gap** — no static proof or bounded discriminator currently covers
  the whole family.

| Case family | Contract clause(s) | Current status | Rationale / current source of truth | ≤120s discriminator or static proof target |
| --- | --- | --- | --- | --- |
| Restart snapshot before RAM/MMU helpers | 1, 4 | **Static-audited with residual partial publishers** | `Uae2026JitPublishFallbackState()` and `jit_publish_code_fetch_state()` record the full restart tuple (`fault_pc`, `Uae2026JitLastInstructionPc`, `SR`, `A7`, flags, `mmu_restart`, and `mmu_opcode`) for fallback and primary code-fetch paths.  The audit below also identifies code-host/compiled helper callsites that publish only `regs.pc`/`fault_pc`/`Uae2026JitLastInstructionPc`; these are not conversion candidates until upgraded or covered by a passing oracle. | Static proof recorded below.  Do not treat partial publishers as transaction-equivalent for call/return/trap/MOVEM conversion; add a focused forced-fault oracle before broadening them. |
| Code-space vs data-space translation | 2 | **Guarded/exact fallback**, with scoped code-host ranges | Data effective-address bank translation uses `Uae2026JitMmuXlateData()`.  Dispatch/branch/return materialization uses `Uae2026JitMmuXlateCodeHost()`.  Confirmed non-identity high-user and low33 post-RTE fetches use code-host bytes; broader low-virtual broadening was tested and reverted after regressions. | Static proof: list the current code-host gating predicates (`05000000..07ffffff`, low33 post-RTE, env-only low12b/low83/low7f) and confirm no normal data EA path calls the code-host helper. |
| CPU-space accesses, `MOVES`, SFC/DFC | 2, 4 | **Guarded/exact fallback** | RAM/MMU dispatch now forces every `table68k[op].mnemo == i_MOVES` through the interpreter barrier because the AArch64 MOVES gapfill is helper-backed through normal data helpers, while exact MOVES needs SFC/DFC-aware `sfc_get_*`/`dfc_put_*` and MOVES-specific SSW handling in `mmu_bus_error()`. | Static proof: verify the `i_MOVES` RAM/MMU barrier stays in `jit_force_interpreter_barrier_opcode()`; run focused `PREVIOUS_OPCODE_FILTER='moves_|movec_sfc|movec_dfc' ./tools/uae2026-opcode-harness.sh` after touching this family. |
| Auto-increment/predecrement memory EAs (`Aipi`/`Apdi`) | 1, 3, 4 | **Guarded/exact fallback**, plus **heuristic compatibility shim** | RAM/MMU mode forces any opcode with source/destination `Aipi`/`Apdi` through an interpreter barrier.  The bridge still contains conservative postincrement/predecrement restoration for helper/fallback escapes and known MOVES signatures, gated by restartability where needed. | Static proof: verify `jit_force_interpreter_barrier_opcode()` still catches all `table68k[op].smode/dmode == Aipi/Apdi`.  If changing this, add targeted opcode vectors for restartable postincrement read and non-restartable predecrement write. |
| BSR return-address push before target instruction fetch | 3, 4 | **Transaction-backed**, but one **heuristic compatibility shim** remains | Generated/fallback BSR paths publish `call_push` metadata and fallback BSR target metadata.  The historical `00003372/00003374 -> 00012b04` seam still uses the legacy bridge scan (`JIT_CALL_TARGET_ROLLBACK`) rather than `JIT_CALL_TARGET_ROLLBACK_TXN`. | Static proof: preserve the legacy scan until a bounded trace shows `JIT_CALL_TARGET_ROLLBACK_TXN` for the historical seam.  Existing `bsr_word_call_return` vector proves normal call/return, not target-fetch-fault rollback. |
| JSR return-address push before target instruction fetch | 3, 4 | **Guarded/exact fallback**, selected metadata remains **known forced-fault mismatch** | RAM/MMU dispatch now forces `i_JSR` through the interpreter barrier.  JSR `call_push` metadata remains narrow (`0000003e`, `00003c26`, `00008334`, `0000c52c`, `05027706`) because broad rollback stalled earlier boot probes.  The synthetic `fault_jsr_target_fetch` oracle confirms the interpreter commits the return push and frames the target fetch at `04008100`, while current JIT still diverges before the forced target-fetch tuple even after the exact barrier. | Keep the JSR exact barrier and explicit metadata allowlist.  Latest discriminator artifact: `/workspace/tmp/previous-opcode-harness-20260529-200633`; do not broaden rollback/canonicalization until this forced-fault vector matches. |
| RTS/RTR return target fetch after stack pop | 3, 4 | **Guarded/exact fallback** and **transaction-backed**, but forced target-fetch still mismatches | Return-family opcodes are interpreter barriers in RAM/MMU dispatch.  Fallback paths publish `return_pop` metadata for `RTS`/`RTR` so target-fetch faults can restore A7 from producer metadata rather than opcode-window guessing.  The latest `fault_rts_target_fetch` oracle shows the pop is committed (`A7=04010004`) but JIT still falls through to the same `PC=08000000` target-stream mismatch class after reaching `04008100`. | Static proof: verify `op == 0x4e75/0x4e77` remains in the RAM/MMU barrier list and fallback loops still call `Uae2026JitMmuTxnBeginReturnPopCurrentA7()`.  Do not native-lower return-family opcodes until `fault_rts_target_fetch` / `fault_rtr_target_fetch` match. |
| RTE SR/stack switch before return-code fetch | 1, 3, 4 | **Proven exact opcode path**, but native post-fault resume remains an **unaudited gap** | `jit_op_rte()` routes through `cpufunctbl[0x4e73]` after publishing fallback state.  Bridge-caught RTE/page-fault seams no longer auto-handoff unless `B2_JIT_RTE_FAULT_HANDOFF=1`, preserving native no-handoff for diagnosis. | Static proof: keep `RTE` on the RAM/MMU interpreter-barrier list.  Bounded discriminator target: short trace that catches `04001ae6 -> low user PC` and compares pre/post `Exception(2)` `SR/A7/USP/ISP/MSP`, frame format, and `pc_p` without running to desktop. |
| MOVEM continuation frames and `MMU_SSW_CM` | 3, 4 | **Guarded/exact fallback**, partly proven | Bridge now preserves `MMU_SSW_CM` continuation EA instead of replacing it with `mmu_fault_addr`.  RAM direct MOVEM predecrement shortcut is default-off because it bypasses interpreter MOVEM restart/fixup bookkeeping.  Fast vectors cover normal MOVEM frame restore. | Static proof: keep `B2_JIT_RAM_DIRECT_MOVEM_PREDEC` default-off and verify `regs.mmu_effective_addr` is not overwritten when `MMU_SSW_CM` is set.  Future vector: MOVEM predecrement MMU write fault with continuation EA assertion. |
| Non-restartable write faults and post-advance PC | 4 | **Heuristic compatibility shim** for known byte stores; generalized behavior is an **unaudited gap** | Confirmed `0500b6ae` and `0500bc98` byte-store seams advance `fault_pc` only when `mmu_restart == false`.  The later `0500b6b0` candidate was reverted because it was already the visible post-advance PC. | Bounded oracle/discriminator is defined below: first collect interpreter `FAULTDUMP` for the exact store shape with a forced write fault, then require the JIT to match `fault_pc`, `instruction_pc`, visible `PC`, `mmu_restart`, `SSW`, writeback fields, and side effects.  Do not add new PC shims from boot fault-window symptoms alone. |
| ATC/MMU maintenance (`PTEST`, `PFLUSH`, `PMOVE`, `PLPA`, vendored `i_MMUOP`) | 5 | **Proven exact** | RAM/MMU dispatch forces vendored `i_MMUOP` exact.  This removed the repeated native `0501288e` stale/invalid retry fault where the interpreter could scan `00038000` successfully. | Static proof: verify `table68k[op].mnemo == i_MMUOP` remains a RAM/MMU barrier.  Regression discriminator: grep a capped fault-window run for `0501288e=0` only after touching this family. |
| MOVEC/SR control-state changes and stale allocator state | 1, 6, 7 | **Guarded/exact fallback / block-ending barrier** | MOVEC helpers may update VBR/SFC/DFC and set `spcflags` in RAM mode; `jit_force_interpreter_barrier_opcode()` ends the block for `4e7a/4e7b`.  SR-write exacting was tested as a broad fix and reverted because it did not move the frontier. | Static proof: keep `MOVEC` block-ending barrier and focused `movec_vbr/sfc/dfc_roundtrip` vectors green.  Do not add a broad SR barrier unless a specific clause violation is proven. |
| Trap/exception-frame construction before nested faults | 3, 4 | **Guarded/exact fallback**, but nested MMU frame-fault behavior is a **known forced-fault mismatch** | RAM/MMU dispatch now forces trap-family opcodes (`i_TRAP`, `i_TRAPV`, `i_TRAPcc`, `i_FTRAPcc`, `i_BKPT`, and `i_ILLG` A/F-line traps) through the interpreter barrier.  The `fault_trap_frame_write` oracle shows the bridge still publishes a pre-trap PC/EA tuple for a frame-stack write fault, so the transaction model still lists `trap_frame` as future work. | Static proof: confirm the trap-family barrier remains in `jit_force_interpreter_barrier_opcode()`.  Discriminator artifact: `/workspace/tmp/previous-opcode-harness-20260529-194451`; do not add trap-frame metadata/native lowering until the interpreter/JIT tuple matches. |
| FPU/FMOVEM/MMU fixup interactions | 3, 4 | **Guarded/exact fallback / not current native RAM focus**, still high-risk | FPU opcodes mostly fallback unless JIT FPU is enabled; `mmufixup[]` is shared with FPU/MOVEM paths and can restore address registers after MMU faults.  The RAM/MMU audit has not proven native FPU+MMU restart behavior. | Static proof: keep `PREVIOUS_UAE2026_JIT_FPU` off for RAM/MMU correctness work unless explicitly testing FPU.  Future vector: FMOVEM memory fault only after integer MMU cases are clean. |
| Timer/interrupt surfacing while native JIT remains active | 6, 7 | **Transaction-independent correctness fix with capped proof** | Dispatch-boundary polling now mirrors the interpreter's `intlev()` check and surfaces `SPCFLAG_INT` when pins are deliverable.  Capped discriminator emitted `JIT_DISPATCH_INT` twice. | Existing proof: `/workspace/tmp/previous-jit-discriminator-dispatch-int-20260528-083837`.  Future proof target: after any tick/dispatch change, rerun the same ≤120s `B2_JIT_TRACE_DISPATCH_INT=1` discriminator; do not use a desktop run as the first signal. |
| Zero-PC/vector recovery under 040 MMU | 1, 4 | **Guarded diagnostic behavior** | Zero-PC vector recovery is disabled while the 040 MMU is enabled so PC=0 remains a symptom instead of masking bad RTE/page-fault resume state. | Static proof: verify zero-PC recovery remains disabled when `regs.mmu_enabled`; use `JIT_ZERO_PC` grep only as a symptom count, not as a recovery success metric. |

### Prioritized next narrow audit targets

1. **Upgrade residual partial restart publishers only behind forced-fault
   oracles.**  The static audit below proves the primary fallback/code-fetch
   paths, but also identifies code-host and compiled-helper callsites that publish
   only part of the restart tuple.
2. **Keep the remaining BSR legacy scan until producer metadata covers the
   historical `00003372/00003374 -> 00012b04` seam.**  Until then, the scan is a
   documented compatibility shim, not a correctness model.
3. **Keep JSR rollback/canonicalization narrow.**  The synthetic
   `fault_jsr_target_fetch` oracle exists and fails by tuple mismatch, so the
   boot-seam allowlist must remain explicit.
4. **Treat remaining MOVEM/RTE/trap/FPU work as exact until metadata and focused
   discriminators match.**  A raw no-handoff boot fault-window is only a suspicion
   source, not a conversion proof.

### Static audit: restart snapshot publication before faultable helpers

- Contract clauses: 1 and 4.  Any faultable RAM/MMU helper or code-host
  translation must publish enough state for `Exception(2)` to build the same
  access-error tuple as the interpreter.
- Full restart publishers:
  - `Uae2026JitPublishFallbackState(pc, opcode)` records `regs.fault_pc`,
    `Uae2026JitLastInstructionPc`, `Uae2026JitLastSr`, `Uae2026JitLastA7`,
    `Uae2026JitLastFlags`, `mmu_restart=true`, and `mmu_opcode=opcode`.
  - `jit_publish_code_fetch_state(pc)` additionally writes `regs.pc=pc` and
    publishes `mmu_opcode=ffff` before code-host translation.
- Covered callsites:
  - `jit_canonicalize_code_pc_if_ram_mmu()` and `jit_fetch_opcode_via_code_host()`
    call `jit_publish_code_fetch_state()` immediately before
    `Uae2026JitMmuXlateCodeHost()`.
  - `jit_fetch_opcode_for_current_pc()` publishes code-fetch state before the
    low-virtual `Uae2026JitMmuFetchOpcode()` path.
  - `Uae2026JitPrefetchGuard()` publishes a prefetch tuple before the faultable
    fetch and republishes the fetched opcode after success, so later data faults
    use the decoded opcode.
  - The fallback execute loops commit any active transaction and call
    `Uae2026JitPublishFallbackState()` before `cpufunctbl[]` fallback execution;
    RTE fallback also explicitly republishes `0x4e73`.
  - L2 per-instruction barriers emit a direct call to
    `Uae2026JitPublishFallbackState()` before invoking fallback helpers.
- Partial publishers that are **not** transaction-equivalent:
  - `jit_sync_fault_pc_for_bank_helper()` runs before native bank helpers
    (`readmem_special()` / `writemem_special()`), but it only stores `regs.pc`,
    `regs.fault_pc`, and `Uae2026JitLastInstructionPc`.  It relies on earlier
    fallback/block publication for `SR`, `A7`, flags, `mmu_restart`, and opcode.
  - `get_n_addr_jmp_mmu()` flushes and then publishes only `regs.pc`,
    `regs.fault_pc`, and `Uae2026JitLastInstructionPc` before the faultable
    `Uae2026JitMmuXlateCodeHost()` target translation.
  - Bad-`pc_p` recovery in `execute_normal()` and `jit_set_guest_pc_fast()` still
    rebuild `pc_p` through `Uae2026JitMmuXlateCodeHost()` without publishing the
    full restart tuple first.  `Uae2026JitCanonicalizePcAfterFallback()` now uses
    `m68k_getpc()` and republishes a code-fetch-style tuple before RAM/MMU
    code-host translation, but the JSR oracle below proves that is not sufficient
    to make target-fetch rollback/canonicalization transaction-equivalent.
- Decision: the main fallback and explicit code-fetch paths are covered, but the
  partial publishers above must not be used as proof for native call/return/trap
  conversion.  Any upgrade should be narrow and paired with a forced-fault oracle
  that proves the full interpreter tuple, rather than inferred from a boot-frontier
  change.

### Bounded proof 1: historical BSR target-fetch seam

- Contract clauses: 3 and 4, call-side side effects and 68040 exception-frame
  restart equivalence.
- Question: does the historical shifted-PC seam
  `00003372/00003374 -> 00012b04` now use explicit `call_push` transaction
  metadata (`JIT_CALL_TARGET_ROLLBACK_TXN`) instead of the legacy bridge scan?
- Static source proof:
  - `bridge_restore_call_target_fault_side_effects()` calls
    `bridge_rollback_mmu_txn()` before the legacy BSR opcode-window scan.
  - Therefore, if the historical seam has live producer metadata, the first
    bridge signal must be `JIT_CALL_TARGET_ROLLBACK_TXN`; falling through to
    `JIT_CALL_TARGET_ROLLBACK` means no usable transaction was active for that
    bridge catch.
  - Current generated/fallback BSR producer hooks exist, but this proof only
    covers whether they reach the historical shifted-PC catch.
- Log proof from existing bounded/diagnostic artifacts:
  - Older traces that hit the historical catch still show the legacy scan, e.g.
    `/workspace/tmp/previous-jit-pchit01003300-ram-20260517-231928` and
    `/workspace/tmp/previous-jit-native-rte-catch64-20260523-024641` contain
    `JIT_CALL_TARGET_ROLLBACK fault_pc=00003372 op_pc=00003374 op=61ff
    addr=00012b04 sp=03ffffc8`.
  - The newer long no-handoff trace
    `/workspace/tmp/previous-jit-jsr8334-native-nohandoff-ram-20260527-014652`
    proves the trace hook was enabled because other seams emit
    `JIT_CALL_TARGET_ROLLBACK_TXN` (`00008334`, `0000c52c`, `0000003e`,
    `00003c26`), but it does not emit a TXN for the historical BSR seam.  That
    run catches `00012b04` directly with `A7=03ffffc4`, so it no longer proves
    producer metadata coverage for the old shifted `00003372/00003374` catch.
  - A fresh capped run
    `/workspace/tmp/previous-bsr-proof-current-20260528-191959` was stopped at
    the 120s limit and did not reach `00003372`, `00003374`, `00012b04`, or any
    call-target rollback line; it is recorded only as an inconclusive capped
    attempt, not as coverage.
- Conclusion: the historical BSR seam is **not proven transaction-covered**.
  Keep the legacy BSR scan as a compatibility shim.  Do not remove it until a
  bounded trace or synthetic target-fetch-fault discriminator shows
  `JIT_CALL_TARGET_ROLLBACK_TXN` for the `00003372/00003374 -> 00012b04` shape.

### Discriminator: JSR target-fetch fault beyond the allowlist

- Contract clauses: 3 and 4, call-side side effects and target instruction-fetch
  access-error framing.
- Goal: determine, from an interpreter oracle, whether a non-allowlisted
  `JSR (An)` target-fetch fault should roll back the return-address push or
  commit/canonicalize the fault at the target PC.  Do this before broadening the
  current JSR transaction policy beyond the explicit boot-seam allowlist.
- Why this must be synthetic/short: the real boot seams that motivated the
  allowlist occur too late or too nondeterministically for reliable ≤120s proof,
  and previous broad JSR rollback experiments perturbed early boot.
- Implemented harness pieces, default-off and opcode-test-only:
  - `B2_TEST_EXPECT_EXCEPTION=2` makes the opcode harness treat one expected
    access error as success and print `FAULTDUMP:` instead of requiring
    `REGDUMP:`.
  - `B2_TEST_CODE_FAULT_ADDR=<addr>` forces an opcode-test-only target-fetch
    fault.  It is ignored unless `Uae2026OpcodeTestModeActive()` is true.  When
    the interpreter or JIT code fetches that exact logical PC, it raises the same
    access-error path the 040 MMU would use after publishing normal code-fetch
    restart state.
  - `FAULTDUMP` includes vector, `fault_pc`, `regs.instruction_pc`,
    `m68k_getpc()`, `mmu_fault_addr`, `mmu_effective_addr`, `mmu_opcode`,
    `mmu_restart`, `mmu_ssw`, `SR`, active `A7`, `USP/ISP/MSP`, writeback fields,
    `spcflags`, and configured stack dumps.
- Implemented vector, deliberately outside the current JSR allowlist:
  - Name: `fault_jsr_target_fetch`.
  - Test PC: `TEST=0x04008000`.
  - Code: `JSR (A0); MOVEQ #$55,D7` (`4E90 7E55`; the harness appends the
    sentinel and `STOP #$2700`).
  - Init: `A0=TEST+0x100`, `A7=0x04010000`, user SR (`0010`).
  - Fault trigger: `B2_TEST_CODE_FAULT_ADDR=TEST+0x100`.  The stack write to
    `0x0400fffc` remains mapped so the only forced fault is the target code fetch
    after the return-address push.
- Bounded discriminator run:
  - Command shape:
    `PREVIOUS_OPCODE_INCLUDE_FAULTS=1 PREVIOUS_OPCODE_FILTER='^fault_jsr_target_fetch$' PREVIOUS_UAE2026_JIT_RAM=1 B2_JIT_RTE_FAULT_HANDOFF_DISABLE=1 PREVIOUS_OPCODE_TEST_ADDR=0x04008000 ./tools/uae2026-opcode-harness.sh`.
  - Artifact: `/workspace/tmp/previous-opcode-harness-20260529-194942`.
  - Metrics: `total=1`, `interp_ok=1`, `jit_ok=1`, `pass=0`, `fail=1`,
    `infra_fail=0`, `score=0`.
- Oracle result:
  - Interpreter commits the return-address push: `A7=0400fffc` and
    `MEMDUMP 0400fffc=04008002`.  It frames a restartable target code-fetch
    fault at `PC=04008100`, `INSTRUCTION_PC=04008100`, `MMU_ADDR=04008100`,
    `MMU_OPCODE=ffff`, `MMU_RESTART=1`, `MMU_SSW=0542`.
  - JIT also leaves the post-push stack visible, but diverges before matching the
    target-fetch tuple: `PC=08000000`, `FAULT_PC=08000000`,
    `INSTRUCTION_PC=08000000`, `MMU_ADDR=08000002`, `MMU_OPCODE=0000`,
    `MMU_SSW=0121`, with no `JIT_CALL_TARGET_*` rollback/canonicalization line in
    the focused log.
- Follow-up exact-barrier attempt:
  - RAM/MMU dispatch now treats the JSR opcode range (`(op & 0xffc0) == 0x4e80`)
    as an interpreter barrier and `Uae2026JitMmuXlateCode()` honors
    `B2_TEST_CODE_FAULT_ADDR` for code-host translation attempts.
    `Uae2026JitCanonicalizePcAfterFallback()` also uses `m68k_getpc()` and
    republishes a code-fetch-style tuple before code-host translation.
  - Normal focused JSR/call vectors remained green:
    `/workspace/tmp/previous-opcode-harness-20260529-210216` (`total=9`,
    `pass=9`, `fail=0`, `infra_fail=0`).
  - Code-host target materialization now routes through `Uae2026JitMmuXlateCode()`
    even when the 040 MMU runtime flag is clear, so opcode-test forced code-fault
    hooks are honored on target streams.  The forced hook publishes
    `INSTRUCTION_PC`/`LastInstructionPc` at the target, clears `FAULT_PC` and
    `MMU_EA` to match the interpreter's code-fetch tuple shape, and preserves
    `MMU_OPCODE=ffff`.
  - Focused JSR/RTS target-fault run after this change:
    `/workspace/tmp/previous-opcode-harness-20260529-210206` (`total=2`,
    `interp_ok=2`, `jit_ok=2`, `pass=0`, `fail=2`, `infra_fail=0`).  The old
    stale `PC=08000000` / `MMU_ADDR=08000002` tuple is gone; both JIT cases now
    report the target `PC=04008100`, `FAULT_PC=00000000`,
    `INSTRUCTION_PC=04008100`, `MMU_ADDR=04008100`, `MMU_EA=00000000`,
    `MMU_OPCODE=ffff`, `MMU_RESTART=1`, `MMU_SSW=0542`.
  - The oracle still does **not** pass because `SR`/`SPC` differ (`SR=0010`,
    `SPC=00000000` on JIT versus `SR=0000`, `SPC=00000008` on the interpreter),
    so it remains a diagnostic oracle rather than conversion permission.
  - Source-path audit of the residual `SR`/`SPC` delta:
    - Interpreter forced faults are caught in `m68k_run_mmu040()` after the loop's
      local restart snapshot `f` is restored, then `Uae2026OpcodeTestModeHandleExpectedException()`
      dumps immediately before `Exception(2)`.
    - Bridge forced faults are caught in `Uae2026JitBridgeEnter()` and restore
      `regflags` from `Uae2026JitLastFlags` before the same opcode-test dump hook.
      That preserves the entry X bit from the seeded `SR=0010`, while the
      interpreter oracle reports `SR=0000`.
    - The `SPC=00000008` vs `SPC=00000000` difference is likewise pre-Exception
      opcode-test harness state: the JIT bridge has not run the interpreter loop's
      pending-interrupt/special-flag path before dumping.  Do not paper over this
      in rollback/canonicalization code; if exact fault-oracle equality is needed,
      add an explicit harness normalization rule and justify it separately.
- Policy: the discriminator exists and does **not** pass.  Keep the current JSR
  metadata allowlist narrow, keep JSR exact in RAM/MMU mode, do not add broad
  rollback/canonicalization from symptoms, and require explicit producer metadata
  plus a passing `fault_jsr_target_fetch` tuple before changing JSR policy again.

### Post-RTE/page-fault resume audit

- Contract clauses: 1, 3, 4, and 7.  `RTE` can switch `SR`/active stack and then
  fault fetching the return code stream; the bridge must build the access-error
  frame from the correct pre/post-RTE state and then resume native dispatch with
  a coherent `PC`/`pc_p` tuple.
- Static source proof:
  - `jit_op_rte()` is exact for opcode semantics: it publishes fallback state and
    calls `cpufunctbl[0x4e73]`, so the frame reads, `SR` transition, stack switch,
    and target prefetch come from the interpreter implementation.
  - RAM/MMU dispatch also keeps return-family opcodes (`RTE`, `RTD`, `RTS`,
    `TRAPV`, `RTR`) on the interpreter-barrier list, so native lowering is not
    used for these paths.
  - On MMU longjmp, the bridge first restores published flags/PC for restartable
    faults and applies `mmufixup[]`; then it applies JIT transactions and the
    known auto-EA/write shims.
  - For a bridge-caught RTE fault, if `RTE` has already loaded user `SR` and the
    cached pre-op `SR` was supervisor, the bridge restores the cached
    pre-instruction supervisor `SR/A7` before `Exception(2)` so the exception is
    framed as a faulting `RTE`, not as an unrelated user-mode handler fault.
  - If `RTE` already switched to user mode and saved the post-pop supervisor
    stack in `regs.isp`, the bridge preserves that value; it uses
    `Uae2026JitLastExceptionSp` only if `regs.isp` is missing.
  - The bridge deliberately does **not** rewrite RTE `fault_pc` or
    `instruction_pc` to `mmu_fault_addr`; the opcode context and access address
    remain distinct.
  - After `Exception(2)`, the bridge captures the supervisor exception stack in
    `Uae2026JitLastExceptionSp`/`ISP` or `MSP`, calls `MakeSR()`, canonicalizes
    `handled_pc` with `m68k_setpc(handled_pc)`, and only performs the old
    interpreter handoff when `B2_JIT_RTE_FAULT_HANDOFF=1` is explicitly set.
  - On the next native dispatch boundary, RAM/MMU dispatch re-derives
    `regs.pc_p` and `regs.pc_oldp` from `m68k_getpc()` via
    `Uae2026JitMmuXlateCodeHost()`.  This is the static proof for post-exception
    `pc_p` coherence; the existing low-PC resume trace records the pre/post
    exception state but not the subsequent dispatch `pc_p` in the same line.
- Short trace excerpt from existing diagnostic artifact
  `/workspace/tmp/previous-jit-nohandoff-lowpcresume-late1200-20260526-054912`
  (excerpt only; no new long run was performed):
  - First RTE/page-fault catch: `pc=04001ae6`, `op=4e73`, `fault_pc=04001ae6`,
    `addr=00003334`, `sr=2004`, `sp=101322e8`, `spc=00000008`; the bridge
    handles it at vector `04001f52` with supervisor `sr=2004` and stack
    `101322ac`.
  - First following low-user access-error catch: `JIT_LOWPC_RESUME PRE n=0` has
    `pc=00003344`, `fault_pc=0000333e`, `addr=00016004`, `mmu_restart=0`,
    `sr=0000`, `s=0`, active `a7=03ffffc8`, `usp=03ffffd4`, `isp=101322f0`,
    and cached `lastpc=0000333e`, `lastsr=00000000`, `lasta7=03ffffc8`.
  - `JIT_LOWPC_RESUME POST n=1` has `pc=04001f52`, `sr=2000`, `s=1`, active
    `a7=101322b4`, `usp=03ffffc8`, `isp=101322b4`, `spc=00000000`, and frame
    fields `fr_vec=0413`, `fr8=22ea0413`, `fr12=228a0000`, matching the handled
    vector-2 frame line.
- Conclusion: the current static path and trace prove the bridge preserves the
  critical RTE/page-fault resume invariants through `Exception(2)` and vector
  entry: `SR`, active `A7`, `USP/ISP/MSP`, opcode-vs-fault-address separation,
  and frame fields are recorded and canonicalized.  The remaining native resume
  failures are therefore not justified by a broad RTE opcode rewrite.  Any future
  RTE-resume optimization must first add a focused dispatch trace that captures
  the first post-`JIT_LOWPC_RESUME POST` native dispatch `pc_p` alongside
  `regs.pc` and the code-host words, still under the ≤120s rule.

### Discriminator design: non-restartable write PC advancement

- Contract clause: 4, exact 68040 access-error frame equivalence.
- Goal: decide, per exact write opcode and EA shape, whether a non-restartable
  data-write fault is reported at the write instruction PC or at the already
  advanced post-instruction PC.  This is an interpreter-oracle question, not a
  bridge pattern-matching question.
- Current narrow shim:
  - `uae2026_jit_bridge.cpp` advances only `0500b6ae` and `0500bc98`, only for
    `prb == 2 && !mmu_restart`, then updates `regs.fault_pc`,
    `regs.instruction_pc`, and `m68k_getpc()` together.
  - The nearby `0500b6b0` hot loop is **not** a new approved shim.  A candidate
    that advanced it was reverted because the capped diagnostic
    `/workspace/tmp/previous-jit-byte-store-b6b0-nohandoff-20260527-151758` did
    not move the frontier; the safer interpretation is that `0500b6b0` is
    already the visible post-advance PC from the existing `0500b6ae` shape.
- Why a raw boot fault-window is insufficient:
  - `mmu_bus_error()` records write size/function-code state, writeback fields,
    `regs.mmu_fault_addr`, and throws vector 2, but it does not itself prove
    which logical `PC` the exact 040 interpreter had already published.
  - `gencpu.c::gen_set_fault_pc()` is the generated-interpreter point that
    marks 68040 writes non-restartable (`mmu_restart = false`) and syncs the
    visible PC before the write helper.  Therefore the oracle must capture the
    generated interpreter's fault state at the specific opcode/EA, not infer it
    from `mmu_restart == false` alone.
  - Existing `seam_byte_store_d2_fault_shape` and
    `seam_byte_copy_postinc_fault_shape` opcode vectors cover normal mapped
    side-effect shapes and memory dumps; they are not MMU write-fault oracles.
- Proposed default-off opcode-test extension:
  - Reuse the exception harness mode from the JSR target-fetch design:
    `B2_TEST_EXPECT_EXCEPTION=2` makes one expected vector-2 access error a
    successful test and emits `FAULTDUMP:` instead of requiring `REGDUMP:`.
  - Add an opcode-test-only forced data-write fault trigger, for example
    `B2_TEST_DATA_FAULT_ADDR=<addr>` plus optional `B2_TEST_DATA_FAULT_SIZE=B/W/L`
    and `B2_TEST_DATA_FAULT_WRITE=1`.  The trigger must be ignored unless
    `Uae2026OpcodeTestModeActive()` is true, and it must enter the same 040 MMU
    access-error path (`mmu_bus_error(..., write=true, size, nonmmu=false)`) that
    a real protected write uses after normal restart-state publication.
  - `FAULTDUMP` must include at least: vector, `fault_pc`,
    `regs.instruction_pc`, `m68k_getpc()`, `mmu_fault_addr`, `mmu_effective_addr`,
    `mmu_opcode`, `mmu_restart`, `mmu_ssw`, `wb2/wb3` status/address/data, `SR`,
    active `A7`, `USP/ISP/MSP`, all data/address registers, top stack longs, and
    requested memory dumps around the destination/source operands.
- Required oracle vectors before any new PC shim:
  1. `MOVE.B D2,(A0)` (`1082`), with `A0` equal to the forced write-fault
     address and `D2` containing a nonzero byte.  This is the synthetic shape for
     the approved `0500b6ae` seam and the rejected `0500b6b0` candidate.
  2. `MOVE.B (A2)+,(A0)` (`109a`), with source mapped/readable, destination equal
     to the forced write-fault address, and dumps of `A0`, old `A2`, new `A2`, and
     destination memory.  This is the synthetic shape for the approved
     `0500bc98` seam and proves the source postincrement side effect.
  3. A negative control such as `MOVE.L D0,-(A7)` with a forced stack write fault,
     matching the known `0000c53c` non-restartable low-virtual behavior where the
     bridge must not apply the high-user byte-store shim.
- Acceptance rule:
  - First run interpreter-only and record the oracle tuple
    `(fault_pc, instruction_pc, visible_pc, mmu_restart, mmu_ssw, wb fields,
    side-effect registers, memory dumps)`.
  - Then run JIT/RAM/MMU with the same forced fault.  A PC shim is valid only if
    the JIT differs from the interpreter solely by arriving at the pre-write PC
    while all already-committed side effects match, and advancing to the
    interpreter's post-PC tuple makes every recorded field match.
  - If the interpreter reports a pre-write PC, if `mmu_restart` is true, if SSW
    write/size/function-code bits differ, or if side effects differ, do not add a
    PC shim; keep the opcode exact or add explicit transaction metadata instead.
- Runtime budget: the forced-fault opcode discriminator should run one
  interpreter and one JIT instance in the existing opcode harness and remain well
  under 120s.  A capped no-handoff boot fault-window may still be used to find
  suspicious PCs, but it is not an acceptance test for new advancement shims.

### Static proof 4: RAM/MMU guard invariants before semantic edits

- Contract clauses: 1, 2, 3, 4, and 5.  These are guardrails that must remain
  true before any rollback/translation/native-lowering semantic edit is allowed.
- Restart snapshot publication:
  - `Uae2026JitPublishFallbackState(pc, opcode)` writes `regs.fault_pc`,
    `Uae2026JitLastInstructionPc`, pre-op `SR`, pre-op active `A7`, flags,
    `mmu_restart=true`, and `mmu_opcode=opcode`.
  - `jit_publish_code_fetch_state(pc)` additionally writes `regs.pc=pc` and
    publishes `mmu_opcode=-1` before a faultable code fetch.
  - Native bank helper calls go through `jit_sync_fault_pc_for_bank_helper()` for
    read/write helpers and `jit_prepare_for_mmu_helper_call()` for explicit MMU
    helper calls; both publish or flush live state before faultable work.
  - Dispatch/fallback loops call `Uae2026JitMmuTxnCommit()` and then
    `Uae2026JitPublishFallbackState()` before executing fallback/interpreter
    opcodes.
- Code/data/CPU-space split:
  - `Uae2026JitMmuXlateData()` translates with `data=true`; `Uae2026JitMmuXlateCode()`
    and `Uae2026JitMmuXlateCodeHost()` are code-space paths with `data=false`.
  - JIT dispatch/branch/return target materialization uses the code-host path
    (`jit_fetch_opcode_via_code_host()`, `get_n_addr_jmp_mmu()`, and RAM/MMU
    dispatch `pc_p` rebuilds).
  - Normal generated memory reads/writes use data/bank helpers (`readmem_special`,
    `writemem_special`, `Uae2026JitMmuGet*`, `Uae2026JitMmuPut*`), not code-host
    translation.
  - CPU-space MOVES is guarded exact in RAM/MMU mode; see below.
- MOVES/SFC/DFC guard:
  - The AArch64 `compstbl_arm.cpp` has helper-backed MOVES gapfill entries, but
    `jit_op_moves()` uses normal `get_*`/`put_*` helpers and therefore cannot be
    the correctness source for SFC/DFC function-code faults.
  - RAM/MMU dispatch now forces `table68k[op].mnemo == i_MOVES` through the
    interpreter barrier, preserving `sfc_get_*`/`dfc_put_*`, `ismoves`, and the
    MOVES-specific SSW/function-code rewrite in `mmu_bus_error()`.
- Auto-EA barriers:
  - `jit_force_interpreter_barrier_opcode()` keeps any opcode whose source or
    destination mode is `Aipi` or `Apdi` exact while RAM dispatch is enabled.
    This protects pre/post-update address-register restart state until explicit
    auto-EA transaction metadata replaces the bridge shim.
- MOVEC control-state barrier:
  - `jit_force_interpreter_barrier_opcode()` treats `4e7a`/`4e7b` (`MOVEC2` /
    `MOVE2C`) as RAM/MMU interpreter barriers, ending the block so VBR/SFC/DFC,
    `spcflags`, and allocator-visible guest state are reloaded before the next
    native instruction.
- MMUOP exact barrier:
  - `jit_force_interpreter_barrier_opcode()` forces `table68k[op].mnemo ==
    i_MMUOP` exact in RAM/MMU dispatch.  This keeps `PTEST`/`PFLUSH`/`PMOVE`/ATC
    state changes in the vendored interpreter path and prevents stale native MMU
    retry state.
- Trap-frame barrier:
  - `jit_force_interpreter_barrier_opcode()` now forces trap-family opcodes exact
    in RAM/MMU dispatch (`i_TRAP`, `i_TRAPV`, `i_TRAPcc`, `i_FTRAPcc`, `i_BKPT`,
    and `i_ILLG` A/F-line traps).  This preserves interpreter exception-frame
    construction until explicit `trap_frame` transaction metadata and nested-fault
    discriminators exist.
  - `fault_trap_frame_write` is the bounded nested-fault discriminator: `TRAP #0`
    from user mode switches to supervisor, starts a format-0 exception frame, and
    forces a word-write fault at `0400fff8` while writing the stacked SR.
    Artifact `/workspace/tmp/previous-opcode-harness-20260529-194451` finished
    under the 120s cap (`total=1`, `interp_ok=1`, `jit_ok=1`, `pass=0`,
    `fail=1`, `infra_fail=0`).
  - Oracle result: interpreter reports the post-trap tuple `PC=04008002`,
    `FAULT_PC=00000000`, `INSTRUCTION_PC=04008002`, `MMU_EA=00000000`,
    `SPC=00000008`; JIT reports `PC=04008000`, `FAULT_PC=04008000`,
    `INSTRUCTION_PC=04008000`, `MMU_EA=0400fff8`, `SPC=00000000`.  The partial
    frame words match (`0400fff8=00000400`, `0400fffc=80020080`), so the
    remaining mismatch is the bridge-published PC/EA/restart tuple.
  - Decision: keep trap-family paths exact and do not add a trap-frame PC/EA shim
    or native trap lowering until explicit `trap_frame` transaction metadata makes
    this oracle pass.
- Zero-PC MMU behavior:
  - The `JIT_ZERO_PC` recovery branch in RAM dispatch is gated by
    `_pc == 0 && jit_allow_ram_dispatch_env() && !regs.mmu_enabled`; with the 040
    MMU enabled, PC=0 is not recovered and remains a diagnostic symptom.
  - The bridge also canonicalizes `handled_pc` after `Exception(2)` and logs the
    frame state, but this is vector-entry canonicalization, not a blanket zero-PC
    recovery while MMU is active.
- Conclusion: the guard invariants are now statically documented.  The only gap
  found during proof was MOVES: the AArch64 gapfill was not an exact SFC/DFC
  source, so RAM/MMU dispatch now barriers all `i_MOVES` before further semantic
  rollback edits.  Do not weaken these guards unless a focused `FAULTDUMP` oracle
  proves equivalent metadata or native lowering.

### Conversion gate: call-target and auto-EA rollback shims

- Contract clauses: 1, 3, and 4.  This gate decides whether any remaining
  bridge-side opcode-window reconstruction can be removed or broadened.
- Bounded discriminator run:
  - Command shape:
    `PREVIOUS_OPCODE_INCLUDE_FAULTS=1 PREVIOUS_OPCODE_FILTER='^(fault_bsr_target_fetch|fault_rts_target_fetch|fault_rtr_target_fetch|fault_rte_return_fetch|fault_write_byte_d2|fault_write_byte_postinc|moves_dfc_write_fault|moves_sfc_read_fault|movem_predec_write_fault)$' PREVIOUS_UAE2026_JIT_RAM=1 B2_JIT_RTE_FAULT_HANDOFF_DISABLE=1 PREVIOUS_OPCODE_TEST_ADDR=0x04008000 ./tools/uae2026-opcode-harness.sh`.
  - Artifact: `/workspace/tmp/previous-opcode-harness-20260529-192504`.
  - Metrics: `total=9`, `interp_ok=9`, `jit_ok=7`, `pass=0`, `fail=7`,
    `infra_fail=2`, `score=0`, `jit_ram_requested=1`,
    `rte_handoff_disabled=1`.
  - Infra failures: `fault_bsr_target_fetch` JIT reached normal `REGDUMP` instead
    of `FAULTDUMP` (`PC=04008016`, stack contains return `04008002`);
    `fault_rts_target_fetch` JIT timed out after repeatedly compiling/falling
    through target `04008100` instead of delivering the forced code-fetch fault.
  - Representative mismatches:
    - `fault_rtr_target_fetch`: interpreter faults at target fetch
      `PC=04008100`, `A7=04010006`, `MMU_OPCODE=ffff`, `MMU_RESTART=1`,
      `MMU_SSW=0542`; JIT faults against stack-frame state (`PC=04010000`,
      `MMU_OPCODE=0010`, `MMU_RESTART=0`, `MMU_SSW=0021`).
    - `fault_rte_return_fetch`: interpreter records the return-code fetch with
      post-RTE `SR=0000`, `A7=04010000`, `ISP=04010008`, `MMU_OPCODE=ffff`,
      `MMU_RESTART=1`; JIT again faults from stack-frame data state
      (`PC=04010000`, `SR=0014`, `MMU_OPCODE=0010`, `MMU_RESTART=0`) instead of
      the target code fetch.
    - `fault_write_byte_d2`: interpreter oracle reports post-write
      `PC=04008002`, `FAULT_PC=00000000`, `MMU_EA=00000000`, `SPC=00000008`;
      JIT reports pre-write `PC=04008000`, `FAULT_PC=04008000`,
      `MMU_EA=0400a000`, `SPC=00000000`.  This confirms the existing high-user
      byte-store post-PC shim class is real but does **not** authorize new PCs
      without exact oracle matching.
    - `fault_write_byte_postinc`: interpreter and JIT agree on rolled-back
      `A2=0400a011`, but the same post-write/pre-write PC tuple mismatch remains.
    - `moves_dfc_write_fault`: interpreter reports post-MOVES
      `PC=0400800a`, `FAULT_PC=00000000`, `MMU_EA=00000000`; JIT reports
      `PC=04008006`, `FAULT_PC=04008006`, `MMU_EA=0400a000`.  The focused oracle
      still shows JIT/interpreter PC tuple differences under forced fault, so
      MOVES remains exact/guarded and is not a native metadata candidate yet.
    - `moves_sfc_read_fault`: restartable read state matches on data/control
      (`MMU_RESTART=1`, `MMU_SSW=0501`, `PC=04008006`), but bridge-published
      `FAULT_PC`/`MMU_EA` still diverge from the interpreter tuple.
    - `movem_predec_write_fault`: interpreter preserves continuation EA
      `MMU_EA=0400a020`, `SR=0000`, `SPC=00000008`; JIT reports
      `MMU_EA=0400a01c`, `SR=0010`, `SPC=00000000`.  Keep MOVEM exact until the
      format-7 continuation metadata is explicit and matched.
- Post-`af54814` forced-fault baseline:
  - An unfiltered `PREVIOUS_OPCODE_INCLUDE_FAULTS=1` opcode-harness run exceeded
    the 120s cap before reaching the full fault set; artifact
    `/workspace/tmp/previous-opcode-harness-20260529-201425` is **not** counted as
    validation.
  - Focused command shape:
    `PREVIOUS_OPCODE_INCLUDE_FAULTS=1 PREVIOUS_OPCODE_FILTER='^(fault_bsr_target_fetch|fault_jsr_target_fetch|fault_rts_target_fetch|fault_rtr_target_fetch|fault_rte_return_fetch|fault_trap_frame_write|fault_write_byte_d2|fault_write_byte_postinc|moves_dfc_write_fault|moves_sfc_read_fault|movem_predec_write_fault)$' PREVIOUS_UAE2026_JIT_RAM=1 B2_JIT_RTE_FAULT_HANDOFF_DISABLE=1 PREVIOUS_OPCODE_TEST_ADDR=0x04008000 ./tools/uae2026-opcode-harness.sh`.
  - Current artifact after code-host forced-fault routing:
    `/workspace/tmp/previous-opcode-harness-20260529-210536`.
  - Metrics: `total=11`, `interp_ok=11`, `jit_ok=10`, `pass=0`, `fail=10`,
    `infra_fail=1`, `score=0`, `jit_ram_requested=1`,
    `rte_handoff_disabled=1`.
  - Infra failure: `fault_bsr_target_fetch` JIT still exits via normal emulator
    completion (`emu_exit_0`) instead of a matching forced `FAULTDUMP`.
  - Equivalence failures: `fault_jsr_target_fetch`, `fault_rts_target_fetch`,
    `fault_rtr_target_fetch`, `fault_rte_return_fetch`, `fault_trap_frame_write`,
    `fault_write_byte_d2`, `fault_write_byte_postinc`, `moves_dfc_write_fault`,
    `moves_sfc_read_fault`, and `movem_predec_write_fault`.
  - Pattern change vs the older 9-vector run: adding the JSR and trap-frame
    oracles broadens coverage, and code-host forced-fault routing now makes
    `fault_jsr_target_fetch` / `fault_rts_target_fetch` reach the target
    `PC=04008100` tuple instead of the stale `PC=08000000` class.  The overall
    conversion decision is unchanged: no rollback/canonicalization family passes
    the oracle gate because the remaining JSR/RTS differences are `SR`/`SPC`.
  - Focused RTS follow-up after the JSR opcode-range barrier:
    `/workspace/tmp/previous-opcode-harness-20260529-203718` (`total=1`,
    `interp_ok=1`, `jit_ok=1`, `pass=0`, `fail=1`, `infra_fail=0`) confirmed the
    return pop itself was committed on both sides (`A7=04010004`,
    `MEMDUMP 04010000=04008100`) while the JIT target fetch still landed in the
    stale `PC=08000000` / `MMU_ADDR=08000002` tuple.
  - After routing code-host target materialization through the forced code-fault
    oracle, `/workspace/tmp/previous-opcode-harness-20260529-210206` confirms the
    RTS target tuple now reaches `PC=04008100` / `MMU_ADDR=04008100`; the remaining
    mismatch is `SR`/`SPC`.  As with JSR, this is a pre-`Exception(2)` opcode-test
    dump-state delta between the interpreter loop catch path and the bridge catch
    path, not permission to broaden return-pop rollback.
- Call-target decision:
  - Do **not** remove the legacy BSR scan.  The historical proof already showed
    `00003372/00003374 -> 00012b04` is not transaction-covered, and the synthetic
    BSR target-fetch oracle did not produce a matching JIT `FAULTDUMP`.
  - Do **not** broaden JSR call-push rollback beyond the existing allowlist.  The
    `fault_jsr_target_fetch` discriminator is now implemented, but it fails by
    target-fetch tuple mismatch and therefore is not an equivalence oracle.
  - Keep return-pop transaction metadata for RTS/RTR producer paths, but do not
    use the current forced-fault mismatch as permission for native return-family
    lowering; return-family opcodes remain exact barriers in RAM/MMU mode.
- Auto-EA decision:
  - Do **not** replace `bridge_restore_autoea_fault_side_effects()` yet.  The
    function remains a conservative bridge compatibility shim for helper/fallback
    escapes: exact postincrement `fault_addr+inc` restoration, restartable-only
    predecrement restoration, and MOVES signatures.
  - RAM/MMU dispatch continues to barrier all `Aipi`/`Apdi` opcodes, so there is
    no approved native auto-EA producer path to convert until explicit autoinc /
    predec transaction metadata and passing forced-fault oracles exist.
- Conclusion: the conversion gate did **not** pass.  The correct action is to
  preserve the documented compatibility shims and exact barriers, not to convert
  or broaden rollback policy.  Future conversion work must first make the
  focused `FAULTDUMP` vectors match interpreter tuples under the ≤120s rule.
