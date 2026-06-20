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
