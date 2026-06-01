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

- syntax probe: **passing**
- object compile probe: **passing**
- emulator/runtime integration of vendored compiler entry points: **wired under `ENABLE_EXPERIMENTAL_UAE2026_JIT`**
- default/ROM translated execution reaches the NEXTSTEP desktop in the latest smoke check (`/workspace/tmp/previous-jit-bsr-metadata-default-20260526-132634`, `desktop_reached=1`)
- refreshed default opcode vector-set baseline remains clean when split into bounded chunks under the 120s rule (`/workspace/tmp/previous-opcode-harness-20260601-124403`, `/workspace/tmp/previous-opcode-harness-20260601-124502`, `/workspace/tmp/previous-opcode-harness-20260601-124558`; combined `pass=75 fail=0 score=100`).  The unfiltered default opcode harness hit the cap at `/workspace/tmp/previous-opcode-harness-20260601-124112` and is not counted.
- RAM-code MMU fast smoke remains clean from RAM execution at `0x04008000` (`/workspace/tmp/previous-mmu-fast-smoke-20260526-132114`, `pass=32 fail=0 score=100`)
- latest bounded RAM/JIT gates after the forced-fault oracle tranche are clean: focused forced-fault tuple gate `/workspace/tmp/previous-opcode-harness-20260531-090328` (`pass=11 fail=0 score=100`) and non-fault seam/call gate `/workspace/tmp/previous-opcode-harness-20260531-090739` (`pass=12 fail=0 score=100`); the broader unfiltered non-fault RAM run hit the 120s cap and is not counted (`/workspace/tmp/previous-opcode-harness-20260531-090522`).
- RAM/MMU dispatch mode is still experimental. The explicit conservative oracle `B2_JIT_RTE_FAULT_HANDOFF=1` boots to a stable desktop in the latest long/no-DC run (`/workspace/tmp/previous-jit-bsr-metadata-ram-handoff-long-20260526-133132`, `desktop_reached=1`, `stable_reached=1`). Native no-handoff remains unfixed after `9441c84`; it now has cleaner fallback BSR transaction metadata but still times out after RTE/low-PC churn in `/workspace/tmp/previous-jit-bsr-metadata-nohandoff-long-20260526-145233`.
- latest diagnostic audit keeps default `B2_JIT_PCTRACE_WORDS` non-invasive by logging only `PCTOPS` plus executable-shadow `PCTSHADOW`; live addrbank reads are opt-in via `B2_JIT_PCTRACE_LIVE=1` because they can have side effects or fault.

## Remaining blocker classes

### 1. CPU API mismatch with Previous `newcpu.h`

Examples:
- `put_long` / `get_long` not declared in the expected header path
- `get_wordi` / `get_longi` / `next_iword` mismatch
- `regs.instruction_pc` missing from Previous `regstruct`

Implication:
- vendored compiler code expects a newer CPU API surface than the active
  Previous interpreter/MMU core currently exposes.

## 2. Flag-model mismatch

Examples:
- `regflags.nzcv` missing

Implication:
- vendored ARM64 code assumes the BasiliskII/2026 flag storage model, while
  Previous still exposes the older flag structure.

## 3. Memory-layout / host-address globals missing

Examples:
- `RAMBaseHost`
- `RAMSize`
- `ROMSize`
- `ROMBaseHost`
- `ROMBaseMac`
- `MEMBaseDiff`
- `get_real_address()` / `get_virtual_address()` expectations

Implication:
- vendored compiler code assumes Basilisk-style direct-address memory globals
  that are not wired into Previous's current runtime state.

## 4. Opcode metadata mismatch

Examples:
- `fl_const_jump`
- `fl_trap`
- `table68k[].cflow`

Implication:
- the vendored compiler expects newer opcode metadata than the active Previous
  decode tables currently provide.

## 5. JIT runtime symbol/signature conflicts

Examples:
- `flush_icache` function-vs-function-pointer mismatch
- compiled op table signature mismatch in some vendored helper hooks

Implication:
- the active Previous CPU core and vendored 2026 compiler still disagree on
  some public runtime interface types.

## 6. Platform/runtime integration gaps

Examples:
- `InterruptFlags`
- `cpu_do_check_ticks`
- `kickmem_bank` / `rtarea_bank`
- Mac/Basilisk ROM helper functions (`ReadMacInt8`, `ReadMacInt32`)

Implication:
- parts of the vendored compiler still assume Basilisk-specific runtime glue
  and need Previous-specific replacements or compile-time exclusion.

## Current strategy

1. keep the working bridge/bootstrap probe in `Previous`
2. use the compiler prefs shim as the first real vendored component
3. measure direct compiler compile blockers with the syntax probe
4. bridge blocker classes in this order:
   - runtime/public type mismatches
   - memory/global shim surface
   - opcode metadata compatibility
   - compiler init path
   - translated dispatch last

## Immediate next targets

1. keep the direct compiler probes passing as guardrails while runtime work continues
2. preserve default/ROM JIT desktop stability while RAM/MMU dispatch changes land
3. add targeted regressions for the confirmed RAM-mode blockers: return-target MMU faults after `RTS`/`RTR`, RTE return-code fetch after SR/A7 switch, BSR target-fetch faults with producer-side metadata, and the current post-RTE low-PC native-resume sequence
4. continue turning the low-virtual code-fetch/MMU-safe path into semantically complete RAM behavior only when restart state, instruction-fetch faults, successful fetches, later data faults, and post-fault return state match the interpreter path
5. audit bridge/JIT state materialization before `Exception(2)` and before resuming JIT after an RTE/page-fault seam, especially PC/SR/USP/ISP, published restart flags, fetched opcode state, live D/A register spill, and the continuation/return state after low-PC faults such as `00012052`, `00005030`, `0000a7a8`, and `00004492`
6. keep RAM/MMU data effective-address translation (`Uae2026JitMmuXlateData`) separate from code/branch/dispatch-PC translation (`Uae2026JitMmuXlateCodeHost`) when adding native paths
7. keep vendored compiler globals (`uae2026_currprefs`, `uae2026_changed_prefs`, `jit_regflags`, `jit_MEMBaseDiff`) separate from Previous-native globals with incompatible layouts
