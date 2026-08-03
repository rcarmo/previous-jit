# JIT block-verifier coverage accounting — 2026-08-01

## Scope

This tranche repairs the block verifier's coverage ledger without changing translated opcode semantics. It closes four observer defects:

1. logged `SKIP-*` classes were not counted, so a report could not distinguish clean comparisons from questions the verifier never validly asked;
2. nested native `longjmp` exits were inferred only from orphaned snapshot cleanup;
3. pending `spcflags`/interrupt entries were cleared for the bounded ordinary replay and then omitted from coverage entirely;
4. the sweep used a direct-mapped 8,192-slot PC table, so unrelated colliding PCs could overwrite or suppress one another.

Code checkpoint: `6ff7872fd03fc85ff9f53a501c384b01657438c6`

Final binary SHA-256: `cf7de9a13592c25b56fabcd3f311b1bbfb549a71ea35a4e89bcb6c22f9b63457`

This report is instrumentation evidence and makes no boot-acceptance claim for
its 2026-08-01 copied fixture. That checkpoint-specific limitation was later
superseded: the immutable post-logout fixture passed exact-by-default
Workspace/File Viewer acceptance on 2026-08-02. See
[`current-jit-status.md`](current-jit-status.md) and
[`sr-native-helper-validation-20260731.md`](sr-native-helper-validation-20260731.md).

## Outcome contract

Each call to `jit_block_verify_run()` increments `attempted` and must end in exactly one terminal class:

- `compared`, split into `passed` and `mismatched`;
- `skip_entry` for an arm/run PC-key mismatch;
- `skip_snapshot` for allocation failure at resume/native/interpreter capture;
- `skip_span` when no exact native/interpreter retirement span exists;
- `skip_io` when either replay touches non-snapshotted device I/O;
- `skip_noreach` when the engines disagree on the end PC;
- `abort_longjmp` when the bridge's exception boundary resumes abnormally.

The report prints `attempted` and `terminal` separately so `attempted != terminal` is immediately visible. Arm-stage snapshot and trace faults are also explicit (`arm_snapshot_failed`, `arm_abort_longjmp`) but remain outside the attempted-run denominator.

The bridge callback now receives the actual `setjmp` value. It records the abnormal outcome before clearing the reentrancy latch, releases orphaned RAM snapshots, and resets the static entry snapshot after a discarded run.

## Specialty-entry coverage

Ordinary semantic comparison still clears `regs.spcflags` and `InterruptFlags` to obtain a bounded one-block question. It no longer implies coverage of pending specialty behavior.

`execute_normal()` now observes the real specialty seam separately for targeted PCs, preserving the entry PC, `spcflags`, and interrupt flags and classifying the exit as:

- `specialty_resume` — same PC and no remaining `SPCFLAG_ALL` work;
- `specialty_redirect` — specialty handling changed the architectural PC;
- `specialty_pending` — specialty work remains pending;
- `specialty_abort` — the bridge longjmp boundary interrupted the specialty pass.

`specialty_irq` counts entries with non-zero `InterruptFlags`. These outcomes are deliberately outside the ordinary comparison denominator.

## Distinct sweep sampling

The sweep retains its active-list cursor, because cached direct-edge-only blocks are otherwise invisible. Recency is now an exact, open-addressed set with 16,384 slots and a bounded 8,192-PC generation at at most 50% load.

Two PCs with the same home slot coexist. A focused source-equivalent probe used `0x00000002` and `0x00004002`, which collide at slot 13,154, and proved both are independently inserted and recognised. At 8,192 distinct PCs the table deliberately starts a new bounded generation instead of silently evicting a colliding peer. `JITSWEEPSTATS` reports eligible, selected, generation-distinct, duplicate and probe counts.

## Evidence

| Gate | Result | Artifact |
|---|---:|---|
| Unity rebuild through `compemu_support.cpp` dependency | pass | build at `6ff7872` |
| Final RAM/MMU focused gate | 67/67 | `/workspace/tmp/previous-verifier-final-mmu-20260801-092956` |
| Clean live verifier gate | 2/2 outer parity; verifier 1/1 pass | `/workspace/tmp/previous-verifier-final-live-20260801-093553` |
| Explicit abnormal-run classification | 2 runs: `attempted=terminal=1`, `abort_longjmp=1`, `compared=0` each | `/workspace/tmp/previous-verifier-arm-abort-20260801-092747` |
| ROM-range live sweep | 4 attempted / 4 terminal: 3 longjmp, 1 no-reach; 1 separate specialty resume | `/workspace/tmp/previous-verifier-rom-20260801-092812.log` |
| Exact-set collision/reset probe | pass; two colliding PCs retained, bounded reset 8192 → 1 | command below |

Representative clean report:

```text
JITBLOCKVERIFY stats arms=1 arm_snapshot_failed=0 arm_abort_longjmp=0 attempted=1 terminal=1 compared=1 passed=1 mismatched=0 skip_entry=0 skip_snapshot=0 skip_span=0 skip_io=0 skip_noreach=0 abort_longjmp=0 orphaned=0 specialty=0 specialty_irq=0 specialty_resume=0 specialty_redirect=0 specialty_pending=0 specialty_abort=0
```

Representative live limitation report:

```text
JITBLOCKVERIFY stats arms=4 arm_snapshot_failed=0 arm_abort_longjmp=0 attempted=4 terminal=4 compared=0 passed=0 mismatched=0 skip_entry=0 skip_snapshot=0 skip_span=0 skip_io=0 skip_noreach=1 abort_longjmp=3 orphaned=6 specialty=1 specialty_irq=0 specialty_resume=1 specialty_redirect=0 specialty_pending=0 specialty_abort=0
```

The second line is not a clean semantic result: its comparison denominator is zero. It is valid coverage accounting precisely because the three faults, one control-flow disagreement, and one specialty exit are named rather than hidden.

## Reproduction

```bash
cd /workspace/projects/previous

# Unity-build dependency rule, then build.
touch src/cpu/uae_cpu_2026/compiler/compemu_support.cpp
rm -f build-vnc/src/CMakeFiles/Previous.dir/cpu/uae_cpu_2026/compiler/compemu_support.cpp.o
cmake --build build-vnc -j2

# Product-semantic regression gate.
PREVIOUS_MMU_FAST_SMOKE_OUTDIR=/workspace/tmp/previous-verifier-final-mmu-$(date +%Y%m%d-%H%M%S) \
  ./tools/uae2026-mmu-fast-smoke.sh

# A small clean targeted comparison.
PREVIOUS_OPCODE_FILTER='sr_ops_combo|move_to_sr_trace' \
PREVIOUS_OPCODE_INCLUDE_FAULTS=1 \
PREVIOUS_UAE2026_JIT_RAM=1 \
B2_JIT_RTE_FAULT_HANDOFF_DISABLE=1 \
PREVIOUS_OPCODE_TEST_ADDR=0x04008000 \
B2_JIT_VERIFY_BLOCKS=0x04008000-0x04008100 \
B2_JIT_VERIFY_STATS_EVERY=1 \
  ./tools/uae2026-opcode-harness.sh

# Bounded live sweep over the ROM range observed during early boot.
B2_JIT_VERIFY_STATS_EVERY=1 \
B2_JIT_VERIFY_LOG_LIMIT=100 \
B2_JIT_VERIFY_SWEEP_EVERY=5000 \
  ./tools/fg-verify-window.sh 0x01000000-0x0100ffff 45
```

The two repeat vectors in the abnormal-run artifact intentionally demonstrate verifier perturbation and therefore fail their *outer* opcode-harness equivalence after the nested replay faults. They are classification probes only. The 67/67 RAM/MMU run and 2/2 clean targeted run are the correctness gates.
