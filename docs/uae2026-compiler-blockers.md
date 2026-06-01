# UAE 2026 Compiler Integration Blockers

This note tracks the first hard blockers for compiling the vendored
`uae_cpu_2026/compiler/compemu_support_arm.cpp` directly inside `Previous`.

The goal is to convert the current bridge/bootstrap probe into a real vendored
compiler bootstrap while still keeping translated dispatch disabled.

## Current probes

### Syntax probe

```bash
cd /workspace/projects/previous
./tools/uae2026-compiler-syntax-probe.sh
```

This runs a syntax-only compile of the vendored ARM64 compiler core with the
expected experimental defines and writes logs under `/workspace/tmp/...`.

### Object probe

```bash
cd /workspace/projects/previous
./tools/uae2026-compiler-object-probe.sh
```

This compiles `compemu_support_arm.cpp` to an object file under the same probe
prelude, without linking it into `Previous` yet.

## Current status

- syntax probe: **passing** (`/workspace/tmp/previous-uae2026-syntax-20260601-125758`, `rc=0`; blocker counters for instruction PC, memory globals, flag NZCV, opcode cflow, and icache conflicts are all zero)
- object compile probe: **passing** (`/workspace/tmp/previous-uae2026-object-20260601-125759`, `rc=0`, `object_size=441168`)
- emulator/runtime integration of vendored compiler entry points: **wired under `ENABLE_EXPERIMENTAL_UAE2026_JIT`**
- default/ROM bootstrap probe is refreshed and clean (`/workspace/tmp/previous-jit-bootstrap-20260601-130216`, `bridge_compiled=1`, `bootstrap_ready=1`, `bootstrap_active=1`, `aslr_active=1`); default/ROM translated execution reaches the NEXTSTEP desktop in the historical smoke check (`/workspace/tmp/previous-jit-bsr-metadata-default-20260526-132634`, `desktop_reached=1`), which was not rerun under the 120s rule
- refreshed default opcode vector-set baseline remains clean when split into bounded chunks under the 120s rule (`/workspace/tmp/previous-opcode-harness-20260601-124403`, `/workspace/tmp/previous-opcode-harness-20260601-124502`, `/workspace/tmp/previous-opcode-harness-20260601-124558`; combined `pass=75 fail=0 score=100`).  The unfiltered default opcode harness hit the cap at `/workspace/tmp/previous-opcode-harness-20260601-124112` and is not counted.
- refreshed RAM-code MMU fast-smoke vector set remains clean from RAM execution at `0x04008000` when split into bounded chunks (`/workspace/tmp/previous-opcode-harness-20260601-125250`, `/workspace/tmp/previous-opcode-harness-20260601-125405`; combined `pass=32 fail=0 score=100`).  The unchunked wrapper hit the cap at `/workspace/tmp/previous-mmu-fast-smoke-20260601-125024` and is not counted.
- latest bounded RAM/JIT gates after the forced-fault oracle tranche are clean: focused forced-fault tuple gate `/workspace/tmp/previous-opcode-harness-20260531-090328` (`pass=11 fail=0 score=100`) and non-fault seam/call gate `/workspace/tmp/previous-opcode-harness-20260531-090739` (`pass=12 fail=0 score=100`); the broader unfiltered non-fault RAM run hit the 120s cap and is not counted (`/workspace/tmp/previous-opcode-harness-20260531-090522`).
- RAM/MMU dispatch mode is still experimental. The explicit conservative oracle `B2_JIT_RTE_FAULT_HANDOFF=1` has a historical stable desktop artifact (`/workspace/tmp/previous-jit-bsr-metadata-ram-handoff-long-20260526-133132`, `desktop_reached=1`, `stable_reached=1`) but was not rerun under the current 120s cap. Native no-handoff still has no desktop-reaching proof; current progress is measured by the bounded opcode/MMU/fault gates above.
- latest diagnostic audit keeps default `B2_JIT_PCTRACE_WORDS` non-invasive by logging only `PCTOPS` plus executable-shadow `PCTSHADOW`; live addrbank reads are opt-in via `B2_JIT_PCTRACE_LIVE=1` because they can have side effects or fault.

## Historical compile-blocker classes

The direct compiler syntax/object probes now pass under the probe prelude.  The
classes below are retained as integration-risk categories, not as current
syntax-probe failures.  Reclassify a category back to an active blocker only if a
future probe or linked-runtime integration step produces a concrete failure.

### 1. CPU API mismatch with Previous `newcpu.h`

Previously observed examples:
- `put_long` / `get_long` not declared in the expected header path
- `get_wordi` / `get_longi` / `next_iword` mismatch
- `regs.instruction_pc` missing from Previous `regstruct`

Current status:
- covered by the probe prelude/shims for direct `compemu_support_arm.cpp`
  syntax/object compilation.
- still an integration-risk category for code paths outside the probe surface.

## 2. Flag-model mismatch

Previously observed examples:
- `regflags.nzcv` missing

Current status:
- the probe reports `missing_flag_nzcv=0`.
- keep flag-model separation audited because runtime JIT flag publication still
  matters for RAM/MMU restart correctness.

## 3. Memory-layout / host-address globals missing

Previously observed examples:
- `RAMBaseHost`
- `RAMSize`
- `ROMSize`
- `ROMBaseHost`
- `ROMBaseMac`
- `MEMBaseDiff`
- `get_real_address()` / `get_virtual_address()` expectations

Current status:
- the probe reports `missing_memory_globals=0` for the current prelude-covered
  compiler source.
- runtime data/code translation must still remain explicit; probe globals are not
  permission to collapse `Uae2026JitMmuXlateData()` and
  `Uae2026JitMmuXlateCodeHost()`.

## 4. Opcode metadata mismatch

Previously observed examples:
- `fl_const_jump`
- `fl_trap`
- `table68k[].cflow`

Current status:
- the probe reports `opcode_cflow_mismatch=0`.
- metadata compatibility is still an integration-risk category when generated
  tables or additional compiler units are pulled into the build.

## 5. JIT runtime symbol/signature conflicts

Previously observed examples:
- `flush_icache` function-vs-function-pointer mismatch
- compiled op table signature mismatch in some vendored helper hooks

Current status:
- the probe reports `flush_icache_conflict=0` for the current compiler source.
- linked-runtime and additional helper-hook signatures remain future integration
  checks.

## 6. Platform/runtime integration gaps

Previously observed examples:
- `InterruptFlags`
- `cpu_do_check_ticks`
- `kickmem_bank` / `rtarea_bank`
- Mac/Basilisk ROM helper functions (`ReadMacInt8`, `ReadMacInt32`)

Current status:
- direct compiler object compilation is clean, but platform/runtime glue remains
  a linked-integration and behavior-risk category rather than a syntax blocker.

## Current strategy

1. keep the working bridge/bootstrap probe in `Previous`
2. keep the compiler prefs shim and probe prelude as guardrails for direct
   compiler-source compatibility
3. rerun syntax/object probes after touching vendored compiler glue or public CPU
   interface shims
4. bridge linked-runtime integration in this order:
   - runtime/public type mismatches beyond the probe surface
   - memory/global shim surface with explicit data/code MMU separation
   - opcode metadata compatibility for any additional generated tables
   - compiler init path
   - translated dispatch last

## Immediate next targets

1. keep the direct compiler probes passing as guardrails while runtime work continues
2. preserve default/ROM JIT desktop stability while RAM/MMU dispatch changes land
3. keep the covered targeted RAM-mode regressions green: BSR/JSR target-fetch faults, return-target MMU faults after `RTS`/`RTR`, RTE return-code fetch after SR/A7 switch, trap-frame faults, non-restartable writes, MOVES SFC/DFC faults, and MOVEM continuation faults; add new regressions only for broader shapes before changing policy
4. continue turning the low-virtual code-fetch/MMU-safe path into semantically complete RAM behavior only when restart state, instruction-fetch faults, successful fetches, later data faults, and post-fault return state match the interpreter path
5. audit bridge/JIT state materialization before `Exception(2)` and before resuming JIT after an RTE/page-fault seam, especially PC/SR/USP/ISP, published restart flags, fetched opcode state, live D/A register spill, and the continuation/return state after low-PC faults such as `00012052`, `00005030`, `0000a7a8`, and `00004492`
6. keep RAM/MMU data effective-address translation (`Uae2026JitMmuXlateData`) separate from code/branch/dispatch-PC translation (`Uae2026JitMmuXlateCodeHost`) when adding native paths
7. keep vendored compiler globals (`uae2026_currprefs`, `uae2026_changed_prefs`, `jit_regflags`, `jit_MEMBaseDiff`) separate from Previous-native globals with incompatible layouts
