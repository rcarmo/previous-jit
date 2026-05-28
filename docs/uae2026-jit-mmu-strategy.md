# UAE2026 JIT MMU Strategy

This document is the implementation strategy for making `PREVIOUS_UAE2026_JIT_RAM=1`
semantically match Previous's 68040 MMU interpreter. It replaces the current pattern of
one-off fixes for each newly exposed low-PC seam.

The top-down correctness contract is maintained separately in
[`uae2026-jit-correctness-contract.md`](uae2026-jit-correctness-contract.md). Use that
contract to judge whether a proposed JIT/MMU/timer fix is valid before treating a moved
boot frontier as progress.  The contract now also contains the expanded missing-case
audit matrix; every new RAM/MMU change should identify which row it advances and which
≤120s discriminator or static proof applies.

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
`A3=0001aa00`; `9441c84` also aligns fallback BSR transaction target metadata so the
bridge no longer re-decodes BSR extension words through transient `regs.pc_p`. The
latest low-user JSR transaction checkpoint covers `00008334: JSR (A1) -> 00007f72`,
letting native no-handoff progress through the former low7f/low8a stack divergence to
`root on sd@`, but it still times out before desktop in the later `040674d0..04067500`
kernel loop.

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
     fallback-loop hooks, compiled-block fallback hooks, AArch64 legacy-loop hooks, and
     producer-side fallback BSR target metadata (`9441c84`) so fallback transactions no longer
     decode BSR extension words through transient `regs.pc_p`.
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

## Current audit priority after the expanded missing-case review

The expanded matrix in the correctness contract changes the near-term priority
from per-PC frontier chasing to replacing or proving the remaining shims:

1. Keep the historical BSR rollback scan only until producer metadata covers the
   `00003372/00003374 -> 00012b04` target-fetch fault in a bounded trace.  The
   2026-05-28 bounded proof in the correctness contract found no such TXN
   coverage yet; existing traces either use the legacy scan for the shifted seam
   or catch `00012b04` directly.
2. Prove JSR target-fetch behavior beyond the current allowlist with a synthetic
   target-fetch-fault discriminator before broadening native transaction policy.
3. Audit post-RTE/page-fault resume state (`SR`, active A7, `USP/ISP/MSP`, frame
   fields, and `pc_p`) separately from exact RTE opcode execution.
4. Treat non-restartable write PC advancement as interpreter-oracle data, not a
   pattern to generalize from local symptoms.

## Current frontier after the low-user JSR transaction checkpoint

- Committed fix: `e7d280b jit: rollback BSR target-fetch faults`. The proven historical seam still logs `JIT_CALL_TARGET_ROLLBACK fault_pc=00003372 op_pc=00003374 op=61ff addr=00012b04`, not `JIT_CALL_TARGET_ROLLBACK_TXN`, so the bridge scan remains the active compatibility shim for that exact case.
- Follow-up transaction coverage adds explicit `call_push` metadata producers for generated BSR, generic fallback BSR, compiled-block fallback BSR, and AArch64 legacy-loop BSR paths. As of `9441c84`, fallback BSR paths pass producer-side decoded targets into the bridge rather than asking the bridge to re-read extension words from `regs.pc_p`. The low-user `00008334: JSR (A1) -> 00007f72` seam now also publishes a generated/fallback JSR call-push transaction in RAM/MMU mode. Return-family fallback paths now publish return-pop transactions for `RTS`/`RTR` target-fetch faults.
- Bridge gating keeps auto-EA rollback limited to restartable cases that need it and prevents the legacy BSR scan from treating stack-push/absolute-control extension words as BSR opcodes. This keeps the old `00003964/A2=00000002` regression away while matching the interpreter's non-restartable `MOVE.L D0,-(SP)` fault at `0000c53c`.
- The generated/native `jit_op_rte()` helper routes through the exact interpreter RTE implementation, avoiding a duplicate hand-coded frame decoder.
- Zero-PC vector recovery is disabled while the 040 MMU is enabled; in that mode, zero PC is treated as a symptom to diagnose rather than recovered by jumping to vector 2.
- RAM/MMU mode no longer hands bridge-caught RTE/page-fault seams to the interpreter by default. Validation: `/workspace/tmp/previous-jit-no-auto-handoff-ram-20260522-091833` kept JIT active with `jit_ram_dispatch_seen=1` and no `RTE fault handoff to interpreter` / `JIT_FALLBACK` log entries; it does not yet reach the desktop.
- The explicit oracle path remains available with `B2_JIT_RTE_FAULT_HANDOFF=1`: `/workspace/tmp/previous-jit-jsr8334-native-ram-handoff-20260527-012754` reached and held the desktop with `desktop_reached=1`, `stable_reached=1`, and `jit_ram_dispatch_seen=1`.
- The native resume path remains unfixed. After code-shadow, low-user call transaction fixes, MOVEM continuation-EA preservation, fallback BSR metadata alignment, and the generated/fallback `00008334` JSR transaction, the latest long no-handoff run (`/workspace/tmp/previous-jit-jsr8334-native-nohandoff-ram-20260527-014652`) gets past the previous `00007f72/00008a60` stack divergence and OCR reaches `root on sd@`, then times out before desktop around the later `040674d0/040674f6/040674fa/04067500` kernel loop (`jit_last_pc=040674d0`). The earlier `050abffe` / `050ac000` RAM-boundary/code-fetch loop, `050069cc` / `504f2472` dispatch fault, and low-user `00012052/00005030/0000a7a8/00004492` frontiers are no longer the latest completed-run frontier. `B2_JIT_TRACE_LOWPC_RESUME=1` remains the default-off bridge diagnostic for this seam; it logs pre/post-`Exception(2)` low-PC state without changing resume behavior.
- Follow-up non-mutating diagnostics mapped the `040674d0..04067500` loop in `/workspace/tmp/previous-jit-pcwords-040674-nohandoff-20260527-122700`: `040674d0` loads the scheduler/run-queue table at `040b6d1c`, masks the selected entry with `-8`, and branches into `040674f6..04067504`, which polls longwords at `A4=040c32f8`, `D6=040b7410`, and `D5=040c32e8` until one becomes nonzero. The sampled state (`D3=0`, `D5=040c32e8`, `D6=040b7410`, `A4=040c32f8`, `SR=2004`) makes this high-kernel loop look like an idle/wakeup symptom, not the local root cause.
- The same diagnostic run shows the key oracle split clearly: explicit handoff (`/workspace/tmp/previous-jit-jsr8334-native-ram-handoff-20260527-012754`) has only the first nine bridge-caught MMU exceptions and then boots via the interpreter; native no-handoff keeps JIT active after the first `04001ae6 -> 00003334` RTE fault and accumulates later user/code faults, with repeated hot fault PCs such as `0000aef4`, `0000b04e`, `0500b6b0`, `050171be`, and `05017336`. The next actionable comparison should target those post-root user faults or the transaction/restart state that allows them, not the kernel idle loop itself.
- Follow-up default-off bridge fault-window tracing (`B2_JIT_TRACE_FAULT_WORDS_START/END[/LIMIT]`) in `/workspace/tmp/previous-jit-faultwords-0500-nohandoff-20260527-145037` identified the hot `0500b6b0` loop as the same visible byte-store area as the earlier `0500b6ae`: the bridge sees `fault_pc=0500b6b0`, `mmu_opcode=1082` (`MOVE.B D2,(A0)`), `mmu_restart=0`, and sequential fault addresses `00038000..0003800b`. However, a candidate that also advanced `0500b6b0` did not move the frontier (`/workspace/tmp/previous-jit-byte-store-b6b0-nohandoff-20260527-151758`), so it was reverted; `0500b6b0` appears to be the already-advanced visible PC from the existing `0500b6ae` shim rather than a separate missing advance. The fault-window diagnostic now also records the last code-host opcode-fetch words (`JIT_FAULT_CODEHOST_LAST`) so high-user faults with stale live/data-view words can be correlated with the actual code stream that produced `mmu_opcode`.
- Handoff/interpreter oracle comparison for `0501288e` (`/workspace/tmp/previous-jit-oracle-interp-0501288e-20260527-170708`) confirms the same code stream executes successfully after the RTE handoff: `0501288e: TST.B (A2)` advances to `05012890` with `A2` in mapped user/kernel code/data (`050a6789`, `050a67ad`, later `0401292a`) and desktop is reached. Native no-handoff reaches the same code-host stream with `A2=00038000`, `A4=00038000`, `D4=ffffffff`, `A6=03ffbe5c`, `A7=03ffbc88`; compared with the closest oracle pass (`A2=0401292a`, `D4=0000000f`, `A6=03ffbe44`, `A7=03ffbc70`), the failure looks like bad caller/frame state before the local `TST.B`, not a local opcode-fetch or TST implementation bug. A default-off code-host ring diagnostic (`B2_JIT_TRACE_CODEHOST_RING=1`) captured the local native path in `/workspace/tmp/previous-jit-codehost-ring-0500-nohandoff-20260527-183604`: `05012772` dispatches a format specifier through the table at `0501278c`, and `05012874..0501288e` performs the `%s` vararg load (`MOVEA.L (12,A6),A0; ADDQ #4,A0; MOVE.L A0,(12,A6); MOVEA.L (-4,A0),A2; ...; TST.B (A2)`). The native frame is shifted by `+0x18` versus the oracle (`A6/A7`), so the next target is the earlier caller/varargs-frame producer or a post-RTE/user-frame resume seam that left that frame advanced. A follow-up default-off fault-frame diagnostic (`B2_JIT_TRACE_FAULT_FRAME=1`, emitted with the fault-window trace) dumps the user frame/varargs words around `A6`, `A7`, and `12(A6)`, with optional `B2_JIT_TRACE_FAULT_FRAME_MMU=1` to translate user-stack addresses through the 040 data MMU for diagnosis. The translated capture `/workspace/tmp/previous-jit-faultframe-mmu-0500-nohandoff-20260527-221238` confirms `12(A6)=03fffef8` after the local varargs increment, the consumed string argument at `03fffef4` is `00038000`, and the caller return at `4(A6)` is `050181c0`. The 64-entry ring capture `/workspace/tmp/previous-jit-codehost-ring64-0500-nohandoff-long-bg-20260527-220828` maps that caller: `05018192` builds the formatter argument list, `050181ba: BSR.L 050126ca`, and the formatter at `050126ca` consumes caller `16(A6)` as the varargs vector. A larger handoff/interpreter oracle (`/workspace/tmp/previous-jit-oracle-interp-050126c0-05012900-limit5000-20260528-014644`) then corrected the interpretation: the interpreter reaches the same `A2=A4=00038000` path and continues, so `00038000` is valid after the handler runs. The native bug is stale/incorrect MMU retry state for that user data read. Keeping vendored `i_MMUOP` instructions exact in RAM/MMU mode removed the repeated native `0501288e` fault (`/workspace/tmp/previous-jit-mmuop-exact-nohandoff-0500-20260528-022715`, `grep 0501288e=0`), though native no-handoff still times out in the later idle/frontier path.
- Exacting all low virtual code or kernel text, global optlev0, flush-each-op, and the sampled high-kernel `0409f592..0409f5d0` polling window did not move earlier/current frontiers. Targeted `04067400..04067600`, logical `05012800..05012900`, and physical/code-host `04508800..04508900` exact-exec discriminators also did not change the outcome (`/workspace/tmp/previous-jit-exact-pctrace-040674-nohandoff-20260527-124743`, `/workspace/tmp/previous-jit-exact-0501288e-nohandoff-20260527-155058`, `/workspace/tmp/previous-jit-exact-phys-0450888e-nohandoff-20260527-173214`); `B2_JIT_TRACE_PCS=0x0501288e,0x0450888e` likewise emitted no `JITPCHIT` in `/workspace/tmp/previous-jit-pchit-0501288e-nohandoff-20260527-175240`.
- Post-MMUOP follow-up extended the default-off `B2_JIT_DUMP_PC_WORDS_*` diagnostic to include `InterruptFlags` plus the live longwords currently addressed by the idle loop's `A4`, `D6`, and `D5` registers. In `/workspace/tmp/previous-jit-pcwords-live-040674-nohandoff-long-20260528-060335`, native no-handoff stayed in the `040674d0..04067500` loop with `poll_a4=0`, `poll_d6=0`, `poll_d5=0`, and `live=0`, while `B2_JIT_TICKTRACE=1` in `/workspace/tmp/previous-jit-ticktrace-nohandoff-20260528-063101` later showed JIT-active RAM dispatch looping around `04382d9c/04382dfc/04382e22` with `pending_type=1`, positive `pending_time`, `spc=0`, `intlev=0`, and `intmask=7`. A narrow SR-write exact-barrier discriminator passed fast gates but did not move the correctly-enabled RAM/JIT smoke and was reverted. The next fix should target the lost wakeup/timer/interrupt-delivery path after native RAM/MMU resume, not another local `040674xx` opcode or `0501288e` MMU retry change.
