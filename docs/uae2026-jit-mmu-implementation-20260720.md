# UAE2026 JIT/MMU implementation record — 2026-07-20

This records the implementation of the 2026-07-19 JIT/MMU runbook on
`main`, starting from `531e533`.

## Implemented contracts

### Explicit semantic-helper outcome

Faultable semantic helpers begin with an exact opcode snapshot and end with an
explicit logical-PC outcome. Logical `regs.pc` is never reconstructed from a
translated host pointer while MMU/RAM dispatch is active.

RTE executes Previous's exact 68040 handler once, republishes Previous-owned
CCR/SR/A7, translates the restored logical target, and exits through a
preserve-logical-PC dispatcher.

### Call/return transactions

BSR, JSR, RTS and RTR have source-generated transaction producers. Exact stack
semantics commit before the faultable target translation. A target-fetch fault
therefore preserves the completed push/pop and reports the target logical PC;
stack-access faults still retain pre-semantic transaction metadata.

No bridge recovery decision scans bytes around `fault_pc`, and no call/return
recovery decision names an absolute guest PC.

### Data translation and restart publication

MMU-active byte, word and long reads/writes use Previous's translated bank
helpers. Address-only constant-read exemptions are gone. Faultable accesses
publish the complete pre-operation restart tuple. MOVES retains SFC/DFC-aware
accessors and cross-page accesses retain canonical helper ordering.

### Virtual execution identity

MMU blocks are keyed by:

- logical guest PC;
- MMU translation generation;
- supervisor/user execution context;
- translated host pointer as byte/coherence identity, not architectural PC.

The physical hot-tag entry is promoted only after full-key validation. MMU block
ends return through keyed dispatch, and MMU direct chaining/dependency creation
is disabled. Constant edges carry logical and translated targets separately;
dynamic/helper exits preserve the already-published logical PC.

PC-relative code generation remains alias-safe: generated PC16/PC8r bases use
logical `start_pc` plus only an intra-trace host delta.

### Canonical MOVEC and invalidation

Imported MOVEC wrappers forward to Previous's canonical implementation and use
its legality checks, masks and storage. `regs.tcr` is authoritative; `regs.tc`
is a compatibility mirror written only in `mmu_set_tc()`, with assertions at
JIT MOVEC boundaries.

A central translation-change service increments the execution generation and
hard-flushes compiled translations. It is reached by TC/ATC changes, TTR
changes, changed SFC/DFC and changed URP/SRP. The hard flush sets the native
block-exit flag, so a control-state write cannot execute a subsequent fetch or
memory access under old assumptions.

### Retired symptom patches

Removed:

- shifted-PC BSR opcode-window rollback scanning;
- the absolute-PC JSR target-fetch exception;
- two absolute-PC byte-write compatibility cases;
- four absolute-PC video-alias instruction handlers.

Video aliases now use the normal translated bank-helper path. Remaining bridge
normalisers are architecture/tuple-derived and narrowly oracle-backed (MOVES,
MOVEM continuation, nested trap-frame writes, and two non-restartable byte-write
opcode shapes). Absolute addresses remaining in the bridge are memory-map
ranges or diagnostics-only trace selectors.

## Focused evidence

All runs used `PREVIOUS_UAE2026_JIT_RAM=1` and
`B2_JIT_RTE_FAULT_HANDOFF_DISABLE=1`.

| Contract | Artifact | Result |
| --- | --- | --- |
| Keyed dispatch: conditional, DBcc, indirect/helper exits | `/workspace/tmp/previous-mmu-identity-focused-final-20260719-233808` | 3/3, score 100 |
| Canonical MOVEC forwarding/invalidation | `/workspace/tmp/previous-mmu-movec-focused-20260719-235429` | 3/3, score 100 |
| TC, roots, TTRs, PFLUSHA, PTESTR | `/workspace/tmp/previous-mmu-control-matrix-20260719-235734` | 7/7, score 100 |
| Post-cleanup call/return, byte-write, video aliases | `/workspace/tmp/previous-bridge-cleanup-final-20260720-000659` | 9/9, score 100 |
| RTE post-commit target fetch | `/workspace/tmp/previous-rte-fetch-final-20260720-000829` | 1/1, score 100; raw interpreter/JIT tuples match |
| DBF keyed edge after FPU fallback | `/workspace/tmp/previous-dbf-keyed-fix-20260720-0446` | 5/5, score 100; exact six-byte FPU-immediate + DBF taken/fall-through cell included |
| FPU mixed-PC outcome after translation promotion | `/workspace/tmp/previous-fpu-archpc-fix-20260720-0522`; trace `/workspace/tmp/previous-fpu-archpc-trace-20260720-0525` | 1/1, score 100; compiled FPP at `01001002` exits to legal DBF at `01001008` |
| Promoted long-BSR call/return | pre-fix `/workspace/tmp/previous-bsr-long-promoted-prefx-20260720-0602`; fixed `/workspace/tmp/previous-bsr-long-archpc-fix-20260720-0605` | pre-fix timeout with repeated call/push; fixed 1/1, score 100, A7 restored |

A later RAM/MMU boot at
`/workspace/tmp/previous-final-ram-mmu-nohandoff-fpu-fix-20260720-0530`
advanced through FPU and disk reads but stopped after ROM SCSI reset at
`04387150`. The next instruction sequence contains long BSR at `04387156`.
A promoted harness replica proved the helper was committing raw `regs.pc` after
the direct-address implementation advanced `pc_p`: the BSR replayed and pushed
A7 down by four bytes per dispatch. BSR now commits `m68k_getpc()`, which is the
common architectural target for both exact-MMU and direct-PC implementations.

The first RAM/MMU no-handoff boot attempt at
`/workspace/tmp/previous-final-ram-mmu-nohandoff-dbf-fix-20260720-0450`
reached ROM FPP `01005252` twice through fallback, then promoted it on the third
visit and incorrectly committed `01005254`; extension words `5822`, `0001`, and
DBF displacement `ff98` were subsequently decoded as opcodes, producing line-F
at `0100525a`. The bounded trace is
`/workspace/tmp/previous-fpu-dbf-pctrace-20260720-0515`.

Native Previous FPU helpers can advance both PC tuple components: generated JIT
code advances `pc_p` over opcode/extension words while 68040 MMU operand fetches
advance logical `regs.pc`. Their explicit outcome policy now commits
`m68k_getpc()`, not either field in isolation. Other semantic-helper policies are
unchanged. The permanent regression uses three loop visits so the FPP service is
actually promoted before its successor is checked.

The final RAM/MMU boot discriminator exposed one integration defect not reached
by the original identity cells: keyed mode allowed compilation to continue past
a DBF even though its `PC_P` is runtime-selected. When a reused block changed
from its traced loop edge to fall-through, later linear finalisation could
publish the DBF displacement word as the next opcode PC. Keyed compilation now
stops at DBF while its generated predicate and `register_branch()` logical edge
pair are live, then uses the ordinary two-edge keyed finaliser. Non-MMU runtime
`PC_P` dispatch is unchanged. The permanent regression vector mirrors the ROM's
FPU-immediate + DBF sequence and exercises both edges over block reuse.

The implementation did not require a generated `compemu.cpp` edit after the
call/return producer tranche; source/generated parity is preserved.

### Scoped restore-barrier discriminator

`B2_JIT_RESTORE_BARRIER_PCS` optionally confines an enabled
`B2_JIT_RESTORE_BARRIERS` family to logical-PC ranges. It is default-off; an
unset or empty range retains the existing whole-family diagnostic behaviour.
The filter applies to the per-opcode barriers and the DBcc block downgrade.

This was needed because restoring every `0x6xxx` branch prevented the RAM/MMU
boot from reaching the post-reset timer calibration routine. A single-op run at
`0x04382df4` executed the wrap-extension `BEQ` exactly, then observed for 150
seconds. It produced no later ESP Select, Inquiry, or SCSI command, eliminating
that Bcc/CCR boundary as the no-handoff stall cause. Artifact:
`/workspace/tmp/previous-timer-beq-exact-20260720-073257`.

## Interrupt vector dispatch PC contract

A RAM/MMU run after the DMA/CCR repair exposed a valid level-6 format-0
interrupt frame at `A7=0x040010f4` (`SR=0x2300`, saved
`PC=0x04081b98`, vector word `0x0078`), followed by execution at
`0x04081b9c` with the frame still active. The apparent pointer corruption
(`0x1b840078`) was the saved-PC/vector tail of an older unretired frame.

The causal boundary was the active JIT `m68k_do_specialties()` shim:
Previous's MMU `Exception()` enters a vector with indirect
`m68k_setpci()`, which updates logical `regs.pc` but intentionally leaves
`regs.pc_p/pc_oldp` unchanged. Returning to JIT dispatch therefore indexed
the interrupted native block and skipped the interrupt handler and RTE.
Accepted interrupts now translate the explicit vector PC through
`Uae2026JitPrepareMmuDispatchTarget()` and publish the complete PC triple
before returning to generated dispatch.

Focused evidence:

- `/workspace/tmp/previous-interrupt-vector-pc-fix-focused-20260720-124132`
  — successful format-0 level-6 RTE plus target-fetch-fault RTE, 2/2,
  score 100, handoff disabled;
- `/workspace/tmp/previous-headless-20260720-124157`
  — the former `0x04081b9c / 0x1b840090` floppy initialisation fault is
  absent; boot mounts root and reaches first-user-process loading before a
  later Mach IPC panic.

## MOVES postincrement write-fault commitment

The later init-loader failure was not stale PFLUSH state. The passing default
path deliberately faults once at virtual address `1`; the native RAM/MMU path
then differed by faulting again at the retry mapping `0x2000`. Both accesses
were `_copyoutmsg` DFC stores using `MOVES.B D0,(A1)+` (`0x0e19/0x0800`)
and `MOVES.L D1,(A1)+` (`0x0e99/0x1800`).

The exact 68040 handlers commit A1's postincrement and the post-extension PC
before these non-restartable write faults. The helper catch path restored the
pre-op A1/fault-PC tuple. Forced-fault interpreter/JIT vectors now cover both
byte and long forms. The bridge reapplies the committed `(An)+` increment and
canonical post-extension tuple only when opcode, direction, `mmu_opcode`, SSW
size/function code, restart state, and fault address all agree. Word and other
EA forms remain excluded pending oracle coverage.

Focused evidence:

- `/workspace/tmp/previous-moves-postinc-oracle-pre` — both new cells fail
  before the fix, solely on A1 and `FAULT_PC`;
- `/workspace/tmp/previous-moves-postinc-oracle-post` — exact cells 2/2;
- `/workspace/tmp/previous-moves-postinc-fault-matrix-all` — all forced-fault
  cells 15/15, score 100, handoff disabled.

### Runtime SFC/DFC selection

The next boot discriminator showed that postincrement recovery was correct but
`_copyoutmsg` still produced `/e\0c/ma\0...`: the first post-fault native
`MOVES.B D0,(A1)+` advanced A1 without writing the user byte `t`. A helper-entry
probe captured the exact opcode and source (`0x0e19`, `D0=0x74`, `A1=3`), while
no `dfc_put_byte(3, 0x74)` call occurred.

The unity compiler preamble deliberately undefines `FULLMMU`, so the MOVES
runtime helper had compiled its SFC/DFC branches out and used ordinary physical
bank accesses even when `regs.mmu_enabled` was true. The interpreter-resumed
`e` iteration used DFC correctly; the first native `t` iteration did not.
MOVES now selects Previous's SFC/DFC helpers from runtime MMU state and retains
the existing physical/bank path only when the MMU is inactive. This preserves
function-code semantics independently of the vendored compiler's compile-time
core variant.

Focused evidence after the repair:

- serial unity-object rebuild and link passed; rebuilt `jit_runtime_moves`
  disassembly contains calls/tail-calls to all six `sfc_get_*`/`dfc_put_*`
  helpers;
- `/workspace/tmp/previous-moves-runtime-fc-focused-0a16db6-20260721-022613`
  — ordinary MOVES plus four forced-fault forms, 5/5, score 100;
- `/workspace/tmp/previous-moves-runtime-fc-ram-focused-0a16db6-20260721-022726`
  — the same five cells at `0x04008000` with RAM/MMU JIT and RTE handoff
  disabled, 5/5, score 100;
- `/workspace/tmp/previous-copyout-native-dfc3-runtime-fc-20260721-022828`
  — rebuilt no-handoff boot reaches the original `_copyoutmsg` demand fault,
  advances beyond the former `_execve`/directory-vnode denial to a later
  `0x03ffffd4` user-stack demand fault, and continues into Ethernet polling.
  The VNC driver timed out during its initial framebuffer capture, so this is a
  boot-frontier discriminator, not desktop evidence.

## Inline fallback flag-layout boundary

After MOVES was corrected, RAM/MMU boot reached Mach IPC but alternated between
`ipc_right_copyin_header: strange rights` and
`ipc_object_copyout_type_compat: strange rights`. The input rights values were
valid. In the copyin case, `CMP.L D0,D1` compared `0x00040000` with `0x00030000`
and produced JIT NZCV `0xa0000000` (N=1, C=1), so the following BCS had to take
the `0x00040000` arm.

GDB at the actual inline fallback handler proved the ABI mismatch:

- generated `cpuemu_31.c` entered `op_6501_31_ff` with the correct architectural
  opcode `0x6514`;
- the handler's `cctrue(5)` reads Previous's legacy `regflags.cznv`, where C is
  bit 8;
- the unity JIT had published the same shared word in ARM NZCV layout, where C
  is bit 29, so the handler observed legacy `cznv=0` and fell through;
- X had the same raw-copy defect (Previous bit 8 versus JIT bit 29).

Inline fallback now calls one central bridge service before and after the
separately compiled interpreter handler. It converts all N/Z/C/V and X bits
between the two layouts; no generated handler or opcode-specific condition is
special-cased.

Focused evidence:

- pre-fix exact handler breakpoint:
  `/workspace/tmp/previous-ipc-bcs-gdb2-075e96b-20260721-064648` —
  `pc=04059e9c`, `opcode=6514`, legacy `cznv=00000000`;
- live/store oracle:
  `/workspace/tmp/previous-ipc-cmp-flush-075e96b-20260721-055031` —
  repeated offending operands have matching live/stored JIT
  `nzcv=a0000000`, excluding CMP codegen and boundary flush;
- permanent compiled-to-fallback carry vector:
  `/workspace/tmp/previous-opcode-harness-20260721-074104` — 1/1,
  score 100 at `0x04010000`, RAM/MMU JIT enabled, handoff disabled, and log
  confirms `JIT_FALLBACK op=6508` at the forced successor block;
- no-handoff A/B:
  `/workspace/tmp/previous-fallback-flags-abi-20260721-074135` — the 900-second
  run contains zero IPC `strange rights` panics and remains active in
  hardclock/SCSI interrupt work after `root on sd@`. It does not reach desktop,
  so this is proof of the flag-ABI repair and a new boot frontier, not a final
  RAM/MMU gate pass.

## Dispatch-boundary interrupt-pin poll restoration

The compiler replacement at `2ca977e` accidentally removed
`jit_poll_interrupt_pins_for_dispatch()` and its call from the outer native
block dispatcher. That regressed the already audited Clause 6/7 contract:
device state can make `intlev()` deliverable without first setting a specialty
bit, while Previous's interpreter polls the pins after every instruction.
Wall-clock tick delivery and native cycle charging do not replace that poll.

The proven implementation from `4a2a74f` is restored unchanged in semantics:
RAM/MMU dispatch samples `intlev()` at every safe native block boundary and
surfaces `SPCFLAG_INT` when the level exceeds `regs.intmask` or the level-7 edge
rule matches. Delivery remains in `m68k_do_specialties()`, after `MakeSR()` has
synchronised JIT flags. Non-RAM dispatch is unchanged, and the bounded
`B2_JIT_TRACE_DISPATCH_INT` diagnostic remains default-off.

Evidence:

- serial unity-object rebuild and final link passed;
- `/workspace/tmp/previous-dispatch-poll-restored-nohandoff-20260721-194940`
  — clean RAM/MMU no-handoff run emitted the diagnostic cap of 256
  `JIT_DISPATCH_INT` events, proving the restored seam is live; it reached
  `root on sd0` and remained active for the corrected 1200-second desktop
  window with zero panic, IPC `strange rights`, or bad-exception-frame matches;
  desktop was not reached, so this is a contract-restoration proof rather than
  the final no-handoff gate pass.

## Final gates still required

Before push, run serially on an idle host:

1. final build;
2. complete MMU CPU-state harness;
3. complete opcode harness including fault cells;
4. default non-MMU boot smoke;
5. RAM/MMU no-handoff boot discriminator;
6. generated reproducibility and clean-tree checks.

Record the exact final artifacts and results here or in the final delivery note.
