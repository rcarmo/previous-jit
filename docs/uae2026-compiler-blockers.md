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
- default/ROM translated execution reaches the NEXTSTEP desktop and passed a 60s stability smoke in the latest audit check (`/workspace/tmp/previous-jit-prefetch-guard-audit-default-stable-20260514-151230`)
- opcode harness remains clean after the latest RAM/MMU diagnostics (`/workspace/tmp/previous-opcode-harness-prefetch-guard-audit-20260514-150218`, `pass=62 fail=0 score=100`)
- RAM/MMU dispatch mode is still experimental. Baseline RAM mode still fails before desktop around the low-user-virtual `00003352` fault, while opt-in low-virtual diagnostics show the state is recoverable: `B2_JIT_RTE_FAULT_HANDOFF=1` reaches a stable desktop, `B2_JIT_LOW_VIRTUAL_SINGLESTEP=1` clears `00003352` and reaches `root on sd@`, and `B2_JIT_LOW_VIRTUAL_PREFETCH_GUARD=1` reaches `root on sd@` while continuing to execute native low-virtual JIT code (`/workspace/tmp/previous-jit-prefetch-guard-audit-ram-20260514-151804`, no fetched/compiled opcode mismatches).
- latest build-hygiene audit removes the prior compiler/linker warning set by renaming vendored compiler prefs away from Previous-native `currprefs`/`changed_prefs`, guarding duplicate `USE_JIT`, casting AArch64 instruction-word emissions, and adding a defensive ARM64 vreg status bounds check; the latest prefetch-guard audit rebuild was warning-free (`/workspace/tmp/previous-build-prefetch-audit.log`)

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
3. add a targeted regression for the current RAM-mode blocker: MMU handler `RTE` returning to the low-user-virtual ROM probe window followed by a faulting low-memory probe (`00003352` / `addr=00000008`)
4. turn the low-virtual code-fetch/MMU-safe discriminator into semantically complete RAM behavior only after its restart state matches the interpreter path for instruction-fetch faults and later data faults
5. audit bridge/JIT state materialization before `Exception(2)`, especially PC/SR/USP/ISP, published restart flags, fetched opcode state, and live D/A register spill during RTE-triggered page faults
6. keep RAM/MMU data effective-address translation (`Uae2026JitMmuXlateData`) separate from code/branch/dispatch-PC translation (`Uae2026JitMmuXlateCodeHost`) when adding native paths
7. keep vendored compiler globals (`uae2026_currprefs`, `uae2026_changed_prefs`, `jit_regflags`, `jit_MEMBaseDiff`) separate from Previous-native globals with incompatible layouts
