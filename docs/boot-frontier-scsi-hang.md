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
