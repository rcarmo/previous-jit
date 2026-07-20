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

## Final gates still required

Before push, run serially on an idle host:

1. final build;
2. complete MMU CPU-state harness;
3. complete opcode harness including fault cells;
4. default non-MMU boot smoke;
5. RAM/MMU no-handoff boot discriminator;
6. generated reproducibility and clean-tree checks.

Record the exact final artifacts and results here or in the final delivery note.
