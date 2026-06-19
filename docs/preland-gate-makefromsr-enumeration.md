# PRE-LAND GATE RESULT: EDIT3 MakeFromSR coverage is WRONG — corrected enumeration

Date 2026-06-19, static. Hard gate per auditor before landing a264e7b
(docs/realexec-interp-jit-flag-boundary-fix.md). Exhaustive enumeration of every
boundary `MakeFromSR` proves EDIT3 as-written does NOT cover the live set.
DO NOT LAND a264e7b's EDIT1/EDIT3 unchanged.

## Enumeration (grep across the whole JIT unit + liveness by routing)

| site | file:line | live? | leaves cznv at boundary? | in EDIT3? |
|------|-----------|-------|--------------------------|-----------|
| jit_runtime_orsr_word  | support_arm:3799 | **LIVE** (op_fullsr_orsr -> compfunctbl 0x007c) | YES | ❌ MISSED |
| jit_runtime_andsr_word | support_arm:3817 | **LIVE** (0x027c) | YES | ❌ MISSED |
| jit_runtime_eorsr_word | support_arm:3829 | **LIVE** (0x0a7c) | YES | ❌ MISSED |
| jit_op_rte             | legacy:2112      | **LIVE** (call_helper) | YES — calls `cpufunctbl[0x4e73]` DIRECTLY (bit14 interp), NOT via the exec_nostats/execute_normal island | ❌ MISSED (spec wrongly assumed island-covered) |
| jit_op_stop            | legacy:2137      | **LIVE** (call_helper) | YES | ✅ correct |
| jit_op_MakeFromSR      | legacy:1606      | DEAD (0 refs) | n/a | ⚠ wasted |
| jit_op_orsr            | legacy:1621      | DEAD (0 refs) | n/a | ⚠ wasted |
| jit_op_andsr           | legacy:1635      | DEAD (0 refs) | n/a | ⚠ wasted |
| jit_op_eorsr           | legacy:1649      | DEAD (0 refs) | n/a | ⚠ wasted |
| jit_op_rtr             | legacy:2124      | DEAD (0 refs) | n/a | ⚠ wasted |
| jit_runtime_mv2sr_word_full | support_arm:3835 | routed 0x46c0-46ff but BARRIERED -> runs via interpreter island | island-covered now | future-only |
| jit_op_mvsr2           | legacy:2065      | LIVE | reads SR, rebuilds from nzcv | correctly untouched |

ORSR/ANDSR/EORSR/STOP are NOT in either barrier function -> they compile and their
helpers run at a real boundary. Confirmed.

## Why this fails the gate

EDIT3 as written converts 5 DEAD functions and only 1 live one (jit_op_stop),
while LEAVING UNCONVERTED the 3 live full-SR helpers (jit_runtime_orsr/andsr/
eorsr_word) AND jit_op_rte. Landing it would leave those four leaving cznv at
boundaries -> a following native block reads cznv-as-nzcv -> NEW seam bugs
(precisely the regression vector the auditor named). Net: the "clean fix" would
not fix the 4 known bugs' siblings and would add new ones.

## EDIT1 placement is also wrong

`compemu_support.cpp` includes `compemu_support_arm.cpp` (line 36) BEFORE
`compemu_legacy_arm64_compat.cpp` (line 1358). EDIT1 puts jit_flags_* helpers in
the legacy file -> NOT visible to the live SR-writers in support_arm.cpp
(jit_runtime_*_word @3799+). The helpers + the JitInterpFlagIsland struct must be
defined EARLY in compemu_support_arm.cpp (before line 3799), where they are
visible to BOTH support_arm.cpp sites AND (because it is included later) the
legacy exec_nostats/execute_normal island.

## Corrected spec (apply instead of a264e7b EDIT1/EDIT3)

- EDIT1': define jit_flags_nzcv_to_cznv / jit_flags_cznv_to_nzcv + the depth-guarded
  JitInterpFlagIsland in compemu_support_arm.cpp ABOVE line 3799 (e.g. just above
  jit_runtime_orsr_word). Math unchanged (validated layouts).
- EDIT2 (island guard on exec_nostats/execute_normal in legacy) — UNCHANGED; the
  helpers are now visible (defined earlier in support_arm.cpp).
- EDIT3' (append `jit_flags_cznv_to_nzcv();` as the LAST statement of):
    jit_runtime_orsr_word   (support_arm:3799, after MakeFromSR/m68k_incpc)
    jit_runtime_andsr_word  (support_arm:3817)
    jit_runtime_eorsr_word  (support_arm:3829)
    jit_op_stop             (legacy:2137, after MakeFromSR)
    jit_op_rte              (legacy:2112, after the `(*cpufunctbl[0x4e73])(...)` call)
  Drop ALL dead jit_op_MakeFromSR/orsr/andsr/eorsr/rtr conversions.
  jit_runtime_mv2sr_word_full: add the same ONLY if MV2SR.W is ever un-barriered.

## jit_op_rte caveat (verify in the probe)

The spec asserted RTE is island-covered and must not be converted. Enumeration
shows RTE calls `cpufunctbl[0x4e73]` directly (outside the island), so it leaves
cznv and DOES need the trailing convert. BUT RTE also runs the bridge
fault/restart path; confirm with the probe that a single trailing
`jit_flags_cznv_to_nzcv()` after the cpufunctbl call does not double-encode on the
bridge-restart branch (if the bridge re-enters via exec_nostats, the island would
re-convert — guard with the existing depth counter or convert only on the
non-restart return). Treat RTE as PROBE-GATED, not blind.

## Gate verdict
FAIL — do not land a264e7b EDIT1/EDIT3 as written. Land the corrected EDIT1'/EDIT3'
above (still ONE commit, single invariant), keep desktop_reached=1 + oracle 4->0
in the validation gate.
