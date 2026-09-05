# JIT implementation review — 2026-09-05

Review base: `17bc4e5db4a6e68666f94ddcaf2e1180618382c3`.

Scope: dispatcher fault boundaries, exact-handler fault ownership, ARM64
constant folding and focused regression coverage. This is not an exhaustive
instruction-set audit or a new desktop/performance acceptance run.

## Reproduced defects and fixes

1. **Initial dispatcher instruction-fetch fault.**
   `Uae2026JitBridgeCompileExecute()` translated its code-host address before
   installing its `setjmp`/try frame. A fault could jump through stale exception
   state, and the entry path had not refreshed the complete restart snapshot.
   Translation and cache promotion now run inside the live catch boundary;
   PC/instruction PC, SR, A7 and both JIT flag words are published first.
2. **Interpreter rollback oracle.**
   `m68k_run_mmu040()` saved flags in an automatic non-volatile object modified
   after `setjmp` and read after `longjmp`. The optimised first-fetch fault
   restored zero CCR instead of the seeded bits. The snapshot is now volatile.
   Initial-fetch clear/set-CCR vectors compare the complete raw tuple, without
   the older fault-normalisation mask. Both engines retain `SR=271f` in the
   set-CCR case; the old interpreter emitted `2700`.
3. **Exact-handler fault tuple overwritten.**
   The expanded RAM suite reproduced `fault_move_to_sr_read`: the interpreter
   kept `MMU_EA=0`, but a bridge address-range heuristic replaced it with
   `0400a000`. Exact-opcode producers now bypass those low-PC/RAM heuristics.
   Permanent RAM (`04008000`) and low-PC (`00008000`) vectors cover both gates.
   The helper state is global and survives `THROW`; successful-call cleanup is
   skipped by the jump, and bridge cleanup occurs after exception handling.
4. **No-flags constant-fold width leakage.**
   `jnf_LSL_b_imm`/`jnf_LSL_w_imm` failed to mask the shifted result before
   merging preserved upper bits. Long `LSL`, `ROL` and `ROR` wrote 64-bit host
   intermediates directly to the constant state, letting later folds consume
   bits above bit 31. Masked byte/word results and unsigned 32-bit shift/rotate
   operands now match native register width.

The initial integration vectors passed because current production policy uses
full-flags handlers for flag-setting instructions. That policy was not relaxed.
`make jit-constfold-regression` instead extracts the actual no-flags production
bodies, compiles them with a 64-bit state model and UBSan, and checks all counts
0–63 over nine edge/pattern inputs. Runtime emission stubs abort; the oracle is
independent width arithmetic/bit-by-bit rotation. This tests helper semantics,
not production route selection. Five repeated opcode blocks separately cover
native reuse (required `NATEXEC` entries; missing entries fail the harness).

Before fixes, the original helper matrix failed 1,166/6,336 checks. Extending it
to the adjacent right-rotate helper found another 680 failures before its fix.
Final direct result: **7,488/7,488**, no UBSan error.

## Validation

Host: Orange Pi 6 Plus, CIX P1 (CD8180/CD8160), 12 CPU cores, 16 GB-class RAM,
NVMe storage; Debian Trixie AArch64, host-native workspace `/workspace` resolving
to `/home/agent/workspace`. No container or service changes.

| Gate | Result | Artifact under `/workspace/tmp/` |
|---|---|---|
| `make build` | pass; interpreter, bridge and compiler unity objects rebuilt | `previous-review-build-20260905.log` and final harness build logs |
| Direct helper/UBSan | 7,488 checks; zero failures | `previous-constfold-final-20260905.log` |
| Full opcode + forced faults | 164/164; no infrastructure failures | `previous-review-full-final-20260905/` |
| Expanded RAM/MMU subset | 88/88 at `04008000` (explicit low-PC override retained) | `previous-review-mmu-final-20260905/` |
| Native CPU-state | 38/38, zero skips | `previous-review-cpustate-20260905.log` |
| Focused fetch/SR/reuse, strict raw fetch tuples and native-entry gates | 10/10; 15 native entries across five reuse vectors | `previous-review-focused-strict-20260905/` |

Emulator arms used one-second observation waits and exited after their synthetic
programs. Aggregate suites take several minutes; these are not long guest boot
runs. Source disk was opened write-protected. No immutable desktop boot was
rerun. Existing warnings in interpreter diagnostic formatting and compiler
extern definitions are not new.

Final binary SHA-256:
`d93f8d3a6e4f22ec4ee54aadf1ef34a90628fbc10041a167ba783b95828c8903`.

Reproduction through Make targets:

```sh
make build jit-constfold-regression
PREVIOUS_OPCODE_INCLUDE_FAULTS=1 PREVIOUS_UAE2026_JIT_RAM=1 \
  B2_JIT_RTE_FAULT_HANDOFF_DISABLE=1 \
  PREVIOUS_OPCODE_INTERP_WAIT_SEC=1 PREVIOUS_OPCODE_JIT_WAIT_SEC=1 \
  make jit-opcode-regression
PREVIOUS_OPCODE_INCLUDE_FAULTS=1 \
  PREVIOUS_MMU_FAST_FILTER='sr_|scc_|dbvc|dbvs|chk2_|cas|movep_|movem_|moves_|movec_|jsr_|bsr_|seam_|fault_initial_code_fetch|fold_' \
  PREVIOUS_OPCODE_INTERP_WAIT_SEC=1 PREVIOUS_OPCODE_JIT_WAIT_SEC=1 \
  make jit-mmu-regression
PREVIOUS_CPUSTATE_WAIT_SEC=1 make jit-cpustate
```

Independent bounded diff/test review (`github-copilot/gpt-5.4`) approved with
limitations: body extraction does not prove route selection; exact-helper state
must survive faults. Source inspection and the low/RAM fault regressions verify
that lifetime. Earlier review attempts timed out and are not counted as reviews.

## Unchanged boundaries

Exact generated full-SR handlers remain default; compiled full-SR is diagnostic.
The conservative RTE-fault handoff policy is unchanged. Native no-handoff
Workspace acceptance and paired full-boot performance remain unproven. The
historical immutable boot evidence remains pinned to its August binary, not
silently transferred to this build. See [current status](current-jit-status.md).
