# Current UAE2026 JIT status

Status at `eb7e7ddc435961f5e5582eab0a1b4a507d6b4437` (2026-08-02).

This page is the precedence point for present-tense project status. Dated audit,
bring-up and benchmark reports retain the evidence available at their named
checkpoints; where one of those reports describes an older fixture, frontier or
pending gate, this page records the later result.

## Accepted product policy

- The UAE2026 AArch64 JIT, RAM/MMU bridge and generated 68040 execution paths
  are integrated under `ENABLE_EXPERIMENTAL_UAE2026_JIT`.
- `MVSR2` (`MOVE SR/CCR,<ea>`) and word-sized `MV2SR` (`MOVE <ea>,SR`) use the
  exact generated 68040 handler by default. The compiled wrappers are not safe
  product defaults.
- `B2_JIT_NATIVE_FULL_SR=1` is the diagnostic inverse that enables both
  compiled full-SR wrapper families. `B2_JIT_KEEP_SR_EXACT`,
  `B2_JIT_KEEP_MVSR2_EXACT` and `B2_JIT_KEEP_MV2SR_EXACT` are stronger exact
  overrides.
- `PREVIOUS_UAE2026_JIT_RAM=1` remains experimental. For desktop acceptance,
  `B2_JIT_RTE_FAULT_HANDOFF=1` is the conservative RTE/page-fault interpreter
  handoff oracle. Leaving it unset, or setting
  `B2_JIT_RTE_FAULT_HANDOFF_DISABLE=1`, preserves native no-handoff behaviour
  for diagnosis.
- Native RAM/MMU no-handoff still has no current desktop-reaching proof. The
  accepted exact-by-default full boot does not close that separate limitation.

## Immutable full-boot acceptance

The post-logout source fixture is immutable and hash-pinned:

- image: `/workspace/assets/previous/images/nextstep33-system-en-postlogout-20260802.img`;
- mode/size: `0444`, 536,870,912 bytes;
- SHA-256: `1d0a76447fec28d0a2737cf42021bef9136ca43c188f22aee25a3c7fa1252c8d`.

Pure interpreter and whole-SR exact controls reached Workspace/File Viewer on
the first desktop poll and remained stable for 120 seconds. The final unforced
exact-by-default JIT arm used binary SHA-256
`c1512ee533af4aa0335659da788b6d256ae349224f3b3ee756dd8db6568d4a4e` and:

- reached `desktop_01` and retained Workspace/File Viewer at `stable_120s`;
- returned `harness_rc=0` and source reverification `0`;
- emitted zero verifier mismatch, panic, fatal, `strange rights`, abort or
  `JITSRHELPER` matches;
- left the source mode and SHA-256 unchanged;
- released the emulator, VNC port 9983 and display `:140`.

Artifact:
`/workspace/tmp/previous-postlogout-normal-sr-default-20260802-115440`.

The split boot A/B explains the exact-default decision:

- `MVSR2` exact / `MV2SR.W` compiled entered a permanent WindowServer startup
  loop;
- `MV2SR.W` exact / `MVSR2` compiled reproduced
  `ipc_right_copyin_header: strange rights`;
- authoritative generated opcode semantics with fallback-style compiled
  completion still failed, so no boot-safe compiled subset was found.

Full evidence and reproduction controls are in
[`sr-native-helper-validation-20260731.md`](sr-native-helper-validation-20260731.md).

## Current bounded evidence

| Area | Accepted result | Source |
|---|---|---|
| Full opcode + forced-fault suite | 155/155 | [`sr-native-helper-validation-20260731.md`](sr-native-helper-validation-20260731.md) |
| RAM/MMU fast smoke | 67/67 | [`sr-native-helper-validation-20260731.md`](sr-native-helper-validation-20260731.md) |
| CPU-state harness | 38/38 | [`sr-native-helper-validation-20260731.md`](sr-native-helper-validation-20260731.md) |
| Exact-by-default SR/fault matrix | 11/11; zero helper traces | [`sr-native-helper-validation-20260731.md`](sr-native-helper-validation-20260731.md) |
| Native full-SR inverse | 11/11; native route observed | [`sr-native-helper-validation-20260731.md`](sr-native-helper-validation-20260731.md) |
| Default/inverse repeat routing | default 2/2 with zero helper traces; inverse 2/2 with both helpers | [`sr-native-helper-validation-20260731.md`](sr-native-helper-validation-20260731.md) |
| Block-verifier ledger | 67/67 RAM/MMU; clean targeted 2/2; explicit skip/longjmp/specialty denominators | [`verifier-coverage-accounting-20260801.md`](verifier-coverage-accounting-20260801.md) |
| Timing anchor | 256 matching SCSI transactions, 400 matching CycInt pairs, 194/194 exceptions; 6 ppm cycles and 8 ppm retirements | [`timing-anchor-validation-20260801.md`](timing-anchor-validation-20260801.md) |
| Generation-key policy | 306 fewer generation rejects; 4.27% fewer compiles and 9.69% fewer fresh compiles | [`mmu-generation-churn-anchor-20260801.md`](mmu-generation-churn-anchor-20260801.md) |
| Generated MMU dispatch | 22,820,437 direct hits, 18,085,706 safe C fallbacks; shared thunk cuts average and peak generated code by 15.60% | [`mmu-generated-dispatch-anchor-20260801.md`](mmu-generated-dispatch-anchor-20260801.md) |
| Bounded performance | 1.765103× cold-process median; 6.283813× warm in-process CPU-loop median | [`bounded-jit-benchmark-20260731.md`](bounded-jit-benchmark-20260731.md) |

The two benchmark speedups have different timing scopes and must not be merged.
The immutable full boot closes correctness acceptance for the exact-by-default
configuration; it is not a paired full-boot performance benchmark.

## Open boundaries

1. Native RAM/MMU execution with the RTE/page-fault handoff disabled still
   lacks desktop-reaching acceptance.
2. Compiled full-SR wrappers remain diagnostic-only until their wrapper-boundary
   failure is understood and a new immutable boot A/B passes.
3. Generated MMU fast dispatch remains opt-in via
   `PREVIOUS_UAE2026_JIT_MMU_FAST_DISPATCH=1`; its accepted evidence is semantic
   parity and dispatch elimination, not a wall-time claim.
4. New RAM/MMU or rollback policy must first pass focused, at-most-120-second
   opcode/MMU/fault discriminators and preserve explicit producer metadata.
5. Historical long-running boot artifacts remain provenance, not substitutes
   for the current bounded gates. The one deliberate exception is the final
   120-second stability hold in the immutable full-boot acceptance above.
