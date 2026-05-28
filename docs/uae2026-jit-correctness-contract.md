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
