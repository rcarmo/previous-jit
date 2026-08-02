# UAE2026 SR-family native helper validation — 2026-07-31

## Scope

This tranche audited the unconditional interpreter barrier around the 68040
SR family. The compiled helpers remain available for bounded diagnostics, but
the immutable full-boot A/B completed on 2026-08-02 showed that neither
compiled wrapper family is safe as a product default. The accepted policy is
therefore exact generated-68040 handling by default, with
`B2_JIT_NATIVE_FULL_SR=1` as the same-binary inverse control.

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
| Exact-by-default focused SR/fault matrix | 11/11; helper traces 0 | `/workspace/tmp/previous-opcode-harness-20260802-115047` |
| Native inverse focused SR/fault matrix | 11/11; `mvsr2` route observed | `/workspace/tmp/previous-sr-native-inverse-focused-20260802-115205` |
| Default/inverse repeat routing | default 0/2; inverse 2/2 (`mvsr2`, `mv2sr`) | `/workspace/tmp/previous-sr-default-routing-20260802-115331`, `/workspace/tmp/previous-sr-native-inverse-routing-20260802-115341` |
| Immutable unforced default boot | Workspace/File Viewer at first poll; stable 120 s; zero mismatch/panic/helper matches | `/workspace/tmp/previous-postlogout-normal-sr-default-20260802-115440` |

`B2_JIT_TRACE_SR_HELPERS=1` provides a bounded runtime proof. The native repeat
gate emitted:

```
JITSRHELPER n=1 kind=mvsr2 pc=01001002 op=40c1 sr=2710
JIT_STRICT_SUMMARY native=1 trace=5 warmup=0 verify=0 blocks=2 opt0=0 fallback=0 exec_nostats=0 pc=01001002
JITSRHELPER n=1 kind=mv2sr pc=01001002 op=46fc sr=2715
JIT_STRICT_SUMMARY native=1 trace=5 warmup=0 verify=0 blocks=2 opt0=0 fallback=0 exec_nostats=0 pc=01001002
```

## Immutable full-boot closure — 2026-08-02

The earlier copied image was unsuitable for acceptance because it stopped at
the first-boot language UI. A clean guest logout supplied the missing filesystem
flush boundary. The resulting source fixture is immutable and hash-pinned:

- image: `/workspace/assets/previous/images/nextstep33-system-en-postlogout-20260802.img`;
- mode/size: `0444`, 536,870,912 bytes;
- SHA-256: `1d0a76447fec28d0a2737cf42021bef9136ca43c188f22aee25a3c7fa1252c8d`.

Pure interpreter and whole-SR exact controls both reached complete
Workspace/File Viewer on the first desktop poll and remained stable for 120
seconds. Complementary split arms then isolated the compiled wrappers:

- `MVSR2` exact / `MV2SR.W` compiled avoided the IPC panic but entered a
  permanent WindowServer startup loop;
- `MV2SR.W` exact / `MVSR2` compiled reproduced the stable
  `ipc_right_copyin_header: strange rights` panic;
- using the generated authoritative opcode body with fallback-style completion
  still failed, excluding either duplicated opcode semantics or completion
  alone as the sole cause.

No boot-safe compiled subset exists. `MVSR2` and word-sized `MV2SR` therefore
use the exact generated handler by default. `B2_JIT_NATIVE_FULL_SR=1` enables
both compiled wrappers only as an explicit diagnostic inverse; the SR-wide and
per-family exact controls remain stronger overrides.

The final unforced arm used binary SHA-256
`c1512ee533af4aa0335659da788b6d256ae349224f3b3ee756dd8db6568d4a4e` and no SR
policy override. It reached `desktop_01`, retained Workspace/File Viewer at
`stable_120s`, returned `harness_rc=0`, emitted zero mismatch/panic/helper
matches, and left the source mode and hash unchanged. The service, VNC port and
display were cleanly released after the run.

## Reproduction

```bash
make build JOBS=$(nproc)

PREVIOUS_OPCODE_INCLUDE_FAULTS=1 \
  ./tools/uae2026-opcode-harness.sh

./tools/uae2026-mmu-fast-smoke.sh
./tools/uae2026-cpustate-harness.sh

# Product default: both full-SR families route exact, so no helper trace.
B2_JIT_TRACE_SR_HELPERS=1 \
PREVIOUS_OPCODE_FILTER='^(move_sr_native_repeat|move_to_sr_native_repeat)$' \
  ./tools/uae2026-opcode-harness.sh

# Diagnostic inverse: both compiled wrappers route once in the repeat pair.
B2_JIT_NATIVE_FULL_SR=1 B2_JIT_TRACE_SR_HELPERS=1 \
PREVIOUS_OPCODE_FILTER='^(move_sr_native_repeat|move_to_sr_native_repeat)$' \
  ./tools/uae2026-opcode-harness.sh

# Stronger exact overrides remain available for family isolation.
B2_JIT_NATIVE_FULL_SR=1 B2_JIT_KEEP_MVSR2_EXACT=1 \
PREVIOUS_OPCODE_FILTER='^(move_sr_native_repeat|move_to_sr_native_repeat)$' \
  ./tools/uae2026-opcode-harness.sh
```
