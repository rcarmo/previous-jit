# UAE2026 JIT MMU Strategy

This document is the implementation contract for making `PREVIOUS_UAE2026_JIT_RAM=1`
semantically match Previous's 68040 MMU interpreter. It replaces the current pattern of
one-off fixes for each newly exposed low-PC seam.

## Problem statement

RAM/MMU JIT mode now gets past the historical low-virtual failures (`00003352` and
`00003964`), the post-BSR `init exited with 212` divergence, the later panic-monitor
`bad exception stack format` failure, the repeated `000000de` loop, the high-user
`050abffe`/`050069cc` frontiers, and the later low42 `000042f8` / `addr=0000020c`
A3-restore loop. `PREVIOUS_UAE2026_JIT_RAM=1` preserves the native JIT path instead
of auto-dropping to the interpreter at the RTE/page-fault seam; set
`B2_JIT_RTE_FAULT_HANDOFF=1` explicitly to use the conservative desktop-boot oracle.
The remaining native bug is still incomplete JIT resume after that seam. The current
bounded comparison no longer stops at `000042f8`: preserving `MMU_SSW_CM` MOVEM
continuation effective addresses lets no-handoff enter `000042e2` with the oracle
`A3=0001aa00` and progress into later `040017b0/040017b2` plus RTE/low-PC churn before
timeout. Native RAM desktop boot remains unresolved.

The fixes that moved the frontier all point to the same missing abstraction: translated
code can perform irreversible architectural side effects before a faultable 68040
code/data access, and the bridge currently reconstructs too much restart state
heuristically.

Concrete examples:

- `00003372/00003374 -> 00012b04`: native `BSR.L` pushed a return address before the target
  instruction fetch faulted. Retrying without rollback pushed twice and shifted the low
  user stack, later causing `00003964` to execute with `A2=00000002`.
- low virtual/high-user opcode fetch: stale data-view opcode reads (`PCTOPS`) disagreed with
  code-space translation/live shadow (`PCTSHADOW`/`PCTLIVE`) until code fetch was separated
  from data access and synced through `Uae2026JitMmuXlateCodeHost()`. The post-RTE low-PC
  seam at `00003334` exposed the same issue again (`0200/0c80` vs `204f/9efc`), the later
  high-user zero-walk at `05054b0e..050abffe` showed `PCTOPS=0000` while the code-translated
  host page held real user instructions, and the later `00008334` divergence was narrowed to
  stale data-view `op=2010` where the oracle executes `4e91` (`JSR (A1)`) to `00007f72`.
- RTE/page-fault seams: `RTE` can partially switch SR/A7 and then fault on a code fetch;
  exception delivery needs pre-op supervisor state but must not destroy post-pop ISP state.
- MOVEM continuation frames: when the 68040 sets `MMU_SSW_CM`, the frame effective address
  is the MOVEM continuation EA (`mmu040_movem_ea`), not necessarily the bus fault address.
  Overwriting it with `mmu_fault_addr` shifts saved-register frames; the cleared low42
  failure saved `A3=0001aa00` at `0000aef4` but restored `A3=00000000` at `0000b226`.
- non-restartable byte-store seams: the 040 interpreter can report certain user data-write
  faults after PC has advanced and source/destination side effects have already occurred
  (for example `MOVE.B D2,(A0)` at `0500b6ae` and `MOVE.B (A2)+,(A0)` at `0500bc98`).

The strategy below is to make every MMU-faultable translated operation explicit about:

1. the restart PC/opcode/SR/A7/flags snapshot,
2. side effects that have happened before the faultable access,
3. which address space is being translated (code, data, SFC/DFC), and
4. how to roll back or complete the operation before `Exception(2)` is built.

## Invariants

1. **No faultable helper without a restart snapshot.**
   Before calling any helper that can longjmp via the MMU (`Uae2026JitMmuFetchOpcode`,
   `Uae2026JitMmuXlateCodeHost`, `Uae2026JitMmuGet*`, `Uae2026JitMmuPut*`, or SFC/DFC
   helpers), translated code must publish:
   - `regs.fault_pc`
   - `regs.instruction_pc`
   - `Uae2026JitLastInstructionPc`
   - pre-op `SR`
   - pre-op `A7`
   - current flags (`Uae2026JitLastFlags`)
   - `mmu_opcode` (`-1` before opcode fetch, fetched opcode after successful fetch)
   - `mmu_restart = true`

2. **Code and data translations remain separate.**
   - code fetch/dispatch/branch target: `Uae2026JitMmuFetchOpcode()` or
     `Uae2026JitMmuXlateCodeHost()`
   - normal data EA: data-space helpers / addrbank path
   - `MOVES` and CPU-space accesses: SFC/DFC-aware helpers, never normal data helpers

3. **Instruction side effects are transactional around faultable accesses.**
   If a translated instruction mutates architectural state before a later MMU-faultable
   access, it must register a rollback/completion record before that access. Examples:
   - postincrement/predecrement EA updates
   - `BSR`/`JSR` return-address push before target code fetch
   - `RTE` SR/A7 switch before fetching the return PC's opcode
   - `MOVEM` predecrement register writes and address updates
   - `TRAP`/exception frame construction if a nested code/data access can fault

4. **The bridge is only the last-resort transaction resolver.**
   Prefer publishing transaction metadata from generated code/helper wrappers at the exact
   point of the side effect. Bridge-side opcode-window scanning (such as the current BSR
   rollback) is acceptable only as a conservative compatibility shim until metadata exists.

5. **Interpreter oracle must be able to trace the same seam.**
   Every new MMU transaction fix should have a small trace or harness discriminator that
   compares JIT and interpreter state at the seam without requiring a full 600s boot when
   practical.

## MMU transaction model

Introduce a small per-thread/global transaction record for the JIT bridge:

```c
struct Uae2026JitMmuTxn {
    uint32_t valid;
    uint32_t kind;          // none, autoinc, predec, call_push, rte, movem, trap, custom
    uint32_t pc;
    uint16_t opcode;
    uint16_t flags;
    uint32_t pre_sr;
    uint32_t pre_a7;
    uint32_t side_reg;      // A/D register number or -1
    uint32_t side_old;
    uint32_t side_new;
    uint32_t aux0;
    uint32_t aux1;
};
```

Required operations:

- `BeginFaultableOp(pc, opcode)` publishes the restart snapshot.
- `RecordSideEffect(kind, ...)` records exactly one architectural mutation that must be
  undone if the following helper faults. For multi-step instructions, use a small stack or
  a conservative interpreter barrier until full metadata exists.
- `CommitFaultableOp()` clears the transaction after the faultable helper returns.
- `RollbackFaultableOp()` is called by the bridge before `Exception(2)` and applies the
  transaction-specific undo.

Initial transaction kinds:

| Kind | Side effect | Rollback on MMU fault |
| --- | --- | --- |
| `autoinc` | `An += size` before data access | restore `An = old` |
| `predec` | `An -= size` before data access | restore `An = old` |
| `call_push` | `A7 -= 4; (A7)=return_pc` before target code fetch | restore `A7 = old`; update USP/ISP/MSP |
| `rte_partial` | SR/A7 switched from exception frame | restore pre-op supervisor state for exception delivery; preserve post-pop ISP when valid |
| `movem_predec` | predecrement EA/register sequence | exact fallback until full register bitmap rollback exists |
| `trap_frame` | exception/syscall frame push before nested faultable access | exact fallback until frame metadata exists |

## Code generation rules

### Dispatch and opcode fetch

- Low virtual/high-user RAM/MMU dispatch must fetch opcodes via the code-space MMU/code-host helper when the active code mapping is non-identity. Keep early ROM/low-overlay identity cases on the legacy 040 path unless a trace proves the code-host path is required.
- Before opcode fetch: `mmu_opcode = -1` and full restart state is published.
- After successful opcode fetch: republish with the fetched opcode.
- If opcode fetch faults, no instruction side effects should have happened yet.

### Branches and calls

- Conditional branches with no side effects can translate the target through code-space MMU
  after publishing restart state.
- `BSR`/`JSR` must be treated as `call_push` transactions if the return address is pushed
  before target code translation/fetch.
- The current bridge-side `BSR` scan (`00003372 -> 00012b04`) should become generated-code
  metadata once the transaction record exists.

### Returns

- `RTS` has no side effect before target fetch beyond popping the return PC. If target code
  fetch can fault after the pop, either:
  - fetch/translate the target before committing the pop, or
  - record a transaction that can restore `A7` and the return PC.
- `RTE` must remain an exact/interpreter barrier or use a dedicated `rte_partial` transaction
  until all 68040 format-frame and SR/stack edge cases are represented.

### Data accesses

- Data helpers must not use code-space translation.
- `MOVES` must route through SFC/DFC helpers and publish SFC/DFC in the transaction/log state.
- MMU fixup slots (`mmufixup[]`) and JIT transaction rollback should be reconciled: fixup slots
  are interpreter/MMU-level address-register recovery, while the JIT transaction record covers
  side effects introduced by translated code before entering the helper.

### Fallback opcodes

- Any fallback opcode executed via `cpufunctbl[]` inside JIT loops must leave shared state
  coherent before returning to translated code. This includes PC, SR, flags, A7/USP/ISP/MSP,
  and `spcflags`.
- If the fallback opcode can perform complex exception/syscall behavior (`TRAP`, `RTE`,
  `MOVEC`, `MOVES`, `PFLUSH`, `PMOVE`, etc.), prefer ending the JIT block or exact-fallbacking
  the surrounding block until a transaction exists.

## Bridge responsibilities

On MMU longjmp:

1. Restore published flags from `Uae2026JitLastFlags` when `mmu_restart` is set.
2. Canonicalize PC from `regs.fault_pc` / `Uae2026JitLastInstructionPc` without replacing it
   with `mmu_fault_addr`.
3. Apply any MMU fixup slots.
4. Apply the JIT transaction rollback.
5. Deliver `Exception(2)`.
6. Canonicalize the post-exception PC with `m68k_setpc(handled_pc)`.
7. Sync active supervisor/user stack pointers after `Exception()`.
8. If the exception was an RTE fault, disable JIT and hand execution to the interpreter only when `B2_JIT_RTE_FAULT_HANDOFF=1` is explicitly set and `B2_JIT_RTE_FAULT_HANDOFF_DISABLE=1` is not set. RAM/MMU mode alone must not silently leave translated execution.

## Migration plan

1. **Keep the current BSR rollback** as a compatibility shim; it is proven by:
   - `/workspace/tmp/previous-jit-bsr-rollback-trace-firstlow-20260517-094918`
   - first low entry `00012b04 ... a7=03ffffc4`
   - removal of the repeated `00003964/A2=2` loop.
2. **Add the transaction record and helpers** with no behavior change except replacing the BSR
   scan when metadata is present.
   - Implemented: bridge-side `call_push` records, generated `BSR` producer hooks, generic
     fallback-loop hooks, compiled-block fallback hooks, and AArch64 legacy-loop hooks.
   - Current limitation: the historical `00003372/00003374 -> 00012b04` seam still reaches
     the compatibility scan rather than `JIT_CALL_TARGET_ROLLBACK_TXN`; leave the scan in place
     until the remaining shifted-PC/native path is covered by explicit metadata.
3. **Convert call target paths** (`BSR`, `JSR`) to publish `call_push` transactions before
   target code translation/fetch.
4. **Convert auto-EA paths** to use the transaction record instead of opcode-pattern bridge
   reconstruction.
5. **Keep RTE/MOVEM/trap complex paths exact** until transaction metadata can cover them.
6. **Add harness vectors** for:
   - BSR target code-fetch fault after return push
   - RTS target fetch fault after return pop
   - RTE return-code fetch fault after SR/A7 switch
   - MOVES with DFC/SFC write/read fault
   - MOVEM predecrement MMU fault
7. **Only then revisit optimization/native lowering** for the remaining handoff-disabled native
   RTE-resume divergence.

## Current frontier after the low-user JSR transaction checkpoint

- Committed fix: `e7d280b jit: rollback BSR target-fetch faults`. The proven historical seam still logs `JIT_CALL_TARGET_ROLLBACK fault_pc=00003372 op_pc=00003374 op=61ff addr=00012b04`, not `JIT_CALL_TARGET_ROLLBACK_TXN`, so the bridge scan remains the active compatibility shim for that exact case.
- Follow-up transaction coverage adds explicit `call_push` metadata producers for generated BSR, generic fallback BSR, compiled-block fallback BSR, and AArch64 legacy-loop BSR paths. Return-family fallback paths now also publish return-pop transactions for `RTS`/`RTR` target-fetch faults.
- Bridge gating keeps auto-EA rollback limited to restartable cases that need it and prevents the legacy BSR scan from treating stack-push/absolute-control extension words as BSR opcodes. This keeps the old `00003964/A2=00000002` regression away while matching the interpreter's non-restartable `MOVE.L D0,-(SP)` fault at `0000c53c`.
- The generated/native `jit_op_rte()` helper routes through the exact interpreter RTE implementation, avoiding a duplicate hand-coded frame decoder.
- Zero-PC vector recovery is disabled while the 040 MMU is enabled; in that mode, zero PC is treated as a symptom to diagnose rather than recovered by jumping to vector 2.
- RAM/MMU mode no longer hands bridge-caught RTE/page-fault seams to the interpreter by default. Validation: `/workspace/tmp/previous-jit-no-auto-handoff-ram-20260522-091833` kept JIT active with `jit_ram_dispatch_seen=1` and no `RTE fault handoff to interpreter` / `JIT_FALLBACK` log entries; it does not yet reach the desktop.
- The explicit oracle path remains available with `B2_JIT_RTE_FAULT_HANDOFF=1`: `/workspace/tmp/previous-jit-explicit-handoff-ram-20260522-090029` reached and held the desktop with `desktop_reached=1`, `stable_reached=1`, and `jit_ram_dispatch_seen=1`.
- The native resume path remains unfixed but has moved: after code-shadow, low-user call transaction fixes, and default-off low-PC code-host discriminators, the best grounded comparison now matches the explicit-handoff oracle through nine low-PC catches and diverges immediately after the matched `00008b24` data fault with an extra native-only `0000ee58` catch (`addr=0001402a`). The earlier `050abffe` / `050ac000` RAM-boundary/code-fetch loop and the `050069cc` / `504f2472` dispatch fault are no longer the current completed-run frontier. `B2_JIT_TRACE_LOWPC_RESUME=1` is the default-off bridge diagnostic for this seam; it logs pre/post-`Exception(2)` low-PC state without changing resume behavior. The bridge now also refreshes `mmu_effective_addr` from `mmu_fault_addr` for low-PC data faults before building the 68040 access-error frame, matching the frame's EA word to faults such as `00008b14` (`00018000`) and `00008b24` (`0003fffc`).
- Exacting all low virtual code or kernel text, global optlev0, and flush-each-op did not move earlier frontiers. The next fix should come from the principled MMU transaction/restart model rather than another broad exact-exec discriminator.
