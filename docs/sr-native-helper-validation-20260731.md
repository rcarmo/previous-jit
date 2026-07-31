# UAE2026 SR-family native helper validation — 2026-07-31

## Scope

This tranche removes the unconditional interpreter barrier around the 68040
SR family while retaining `B2_JIT_KEEP_SR_EXACT=1` as a same-binary inverse
control.

The JIT now uses ordered semantic helpers for:

- `MOVE SR,<ea>` and `MOVE CCR,<ea>` (`MVSR2`);
- `MOVE <ea>,SR` (`MV2SR.W`);
- the existing ORI/ANDI/EORI-to-SR helpers, with explicit JIT ↔ interpreter
  flag-layout conversion.

The helpers publish the exact opcode PC, use Previous's authoritative
`MakeSR()` / `MakeFromSR()`, re-enter dispatch through the logical successor,
and preserve generated 68040 read-fault versus continuation-write ordering.
Full-SR replacement therefore retains privilege checks, XNZVC materialisation,
S/M stack-bank swaps, MMU supervisor selection, interrupt re-arming, trace
state, and runtime PC ownership.

## Focused coverage

`tools/uae2026-opcode-vectors.sh` adds:

- register, indirect, postincrement, predecrement, displacement, indexed,
  absolute and PC-relative EA cases;
- user privilege violations for both directions;
- faulting source reads and destination writes;
- USP/ISP/MSP and S/M transitions;
- T1 trace delivery after exactly one successor;
- repeated vectors that force installed native blocks.

Opcode-test mode gained optional `B2_TEST_STACK_BANKS=USP ISP MSP` seeds and
expected direct-exception capture for privilege/trace vectors.

## Evidence

All paths below use `/workspace/projects/previous/build-vnc/src/Previous` built
from the same source tree.

| Gate | Result | Artifact |
|---|---:|---|
| Full opcode + forced-fault suite | 155/155 | `/workspace/tmp/previous-opcode-sr-tranche-20260731-213109` |
| RAM/MMU fast smoke | 67/67 | `/workspace/tmp/previous-mmu-full-sr-tranche-20260731-214717` |
| CPU-state harness | 38/38 | command: `./tools/uae2026-cpustate-harness.sh` |
| Focused SR matrix, strict mode | 22/22 | `/workspace/tmp/previous-sr-final-focused-20260731-232356` |
| Same-binary exact inverse control | 22/22 | `/workspace/tmp/previous-sr-final-inverse-20260731-232608` |
| Native repeat runtime proof | 2/2; `fallback=0`, `exec_nostats=0` | `/workspace/tmp/previous-sr-runtime-proof-20260731-232928` |
| Static compiler-table coverage | `40c1 → mvsr2_helper`, `46fc → mv2sr_helper` | `/workspace/tmp/previous-sr-coverage-20260731-232547/coverage.csv` |

`B2_JIT_TRACE_SR_HELPERS=1` provides a bounded runtime proof. The native repeat
gate emitted:

```
JITSRHELPER n=1 kind=mvsr2 pc=01001002 op=40c1 sr=2710
JIT_STRICT_SUMMARY native=1 trace=5 warmup=0 verify=0 blocks=2 opt0=0 fallback=0 exec_nostats=0 pc=01001002
JITSRHELPER n=1 kind=mv2sr pc=01001002 op=46fc sr=2715
JIT_STRICT_SUMMARY native=1 trace=5 warmup=0 verify=0 blocks=2 opt0=0 fallback=0 exec_nostats=0 pc=01001002
```

## Full-boot limitation

No new full-boot acceptance is claimed in this tranche. Three controls,
including a clean detached `75df2be` build, reproduced an identical NeXTSTEP
IPC `strange rights` panic with the current copied system-image fixture. This
exonerates the SR delta but invalidates that fixture for acceptance and timing.
The small result/OCR/PNG evidence is retained under
`/workspace/tmp/previous-failed-boot-evidence-20260731`; multi-gigabyte copied
run images/logs were removed.

`tools/headless-jit-bridge-smoke.sh` no longer overrides the safe
`PREVIOUS_UAE2026_JIT_CONST_JUMP=0` product default with the known-failing
constant-jump inverse control. A successful fresh immutable boot fixture, or a
bounded deterministic guest-work benchmark completed by both engines, remains
required before any performance claim.

## Reproduction

```bash
make build JOBS=$(nproc)

PREVIOUS_OPCODE_INCLUDE_FAULTS=1 \
  ./tools/uae2026-opcode-harness.sh

./tools/uae2026-mmu-fast-smoke.sh
./tools/uae2026-cpustate-harness.sh

B2_JIT_TRACE_SR_HELPERS=1 B2_JIT_STRICT_FULL=1 \
PREVIOUS_OPCODE_FILTER='^(move_sr_native_repeat|move_to_sr_native_repeat)$' \
  ./tools/uae2026-opcode-harness.sh

B2_JIT_KEEP_SR_EXACT=1 \
PREVIOUS_OPCODE_FILTER='^(move_sr|move_to_sr|sr_)' \
PREVIOUS_OPCODE_INCLUDE_FAULTS=1 \
  ./tools/uae2026-opcode-harness.sh
```
