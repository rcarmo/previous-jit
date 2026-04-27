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
- emulator/runtime integration of vendored compiler entry points: **not yet wired**

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

1. decide which compatibility pieces should stay probe-only and which should
   become real runtime shims
2. start compiling the minimum vendored compiler/runtime objects inside the
   tree under an experimental, non-dispatch build target
3. wire a real `compiler_init()` bring-up path while keeping translated block
   dispatch disabled
