# Plan: migrate `compemu` away from giant generated C bodies toward inline ARM64 emission

## Problem

`src/cpu/uae_cpu_2026/compiler/compemu.cpp` is currently a transplanted, generated compiler body.
It has three structural problems in `Previous`:

1. it is hard to regenerate reproducibly for the exact bridge configuration
2. opcode coverage depends on old generator switches (`USE_JIT2`, table variants, legacy metadata)
3. debugging missing opcodes requires editing generated output instead of editing one obvious ARM64 path

## Goal

Move from “opaque generated compiler bodies + dispatch table glue” to a model where opcode lowering is
owned by a small set of explicit ARM64 helpers and inline emitters.

That does **not** mean writing every opcode as raw instruction words immediately. It means making the
bridge own the lowering logic instead of depending on a monolithic generated file.

## Target end state

### Keep

- `codegen_arm64.cpp/h` — raw AArch64 encoders
- `compemu_midfunc_arm64*.cpp` — reusable lowering helpers
- `jit_native_helpers.h` + C helpers for privileged/complex ops
- opcode metadata tables (`readcpu`, mnemonics, addressing metadata)

### Retire incrementally

- giant generated opcode bodies in `compiler/compemu.cpp`
- hand-maintained drift between `compemu.cpp` and `compstbl_arm.cpp`
- fragile generator-only feature gates such as `USE_JIT2` deciding runtime opcode coverage

## Migration stages

### Stage 0 — done first: deterministic harness

Use `docs/uae2026-opcode-harness.md` as the gate.

Exit criteria:

- opcode vectors run in interpreter mode without full boot
- JIT mode can be compared on the same vectors
- every new lowering change is exercised here before boot tests

### Stage 1 — classify opcode families

Split current missing/risky opcodes into three buckets:

#### A. pure inline-midfunc candidates

These should be emitted directly through `compemu_midfunc_arm64*` helpers:

- condition-code consumers/producers (`Scc`, `DBcc`, `Bcc` family)
- word memory shifts/rotates (`ASLW`, `ASRW`, `LSLW`, `LSRW`, `ROLW`, `RORW`, `ROXLW`, `ROXRW`)
- simple SR/CCR reads/writes where flag/state lowering is already available inline

#### B. call-helper candidates

These should keep a native block but flush/register-sync around a C helper:

- `DIVS/DIVU/DIVL/MULL`
- `PACK/UNPK`
- `MOVEC`, `MOVES`
- `BF*` family until an inline bitfield emitter exists

#### C. interpreter-only holdouts

Leave these for last unless they show up in traces:

- full FPU (`FPP`, `FBcc`, `FScc`) if MPFR precision/state makes inline lowering awkward
- rare supervisor/MMU forms not hit in current NeXT traces

### Stage 2 — introduce bridge-owned per-family emitters

Create explicit family emitters in the ARM64 support layer instead of adding more generated opcode bodies.

Suggested layout:

- `compiler/compemu_inline_arm64_control.cpp` — SR/CCR/condition family
- `compiler/compemu_inline_arm64_shift.cpp` — memory shift/rotate family
- `compiler/compemu_inline_arm64_muldiv.cpp` — mul/div family
- `compiler/compemu_inline_arm64_bitfield.cpp` — `BF*` family

Each emitter should:

1. decode only the bits it needs from the opcode/extension words
2. use existing midfuncs where possible
3. fall back to `call_helper()` for the tricky parts
4. be reachable from a small, readable dispatch registration layer

### Stage 3 — replace generated dispatch registration

Stop treating `compstbl_arm.cpp` as a giant generated artifact.

Instead:

- keep opcode metadata from `readcpu`
- register handlers from explicit family builders
- use small tables/macros only for repetitive opcode ranges

Example direction:

- `register_bcc_family(0x6000..0x6fff)`
- `register_scc_family(0x50c0..0x5ff9)`
- `register_shift_mem_family({...})`

This removes the current “missing in `compstbl`, present in `compemu`” class of bug entirely.

### Stage 4 — shrink generated `compemu.cpp`

Once a family is owned by inline emitters, remove its generated bodies from the build.

A practical transitional model is:

- keep generated `compemu.cpp` only for the still-unmigrated families
- route migrated families to bridge-owned emitters first
- compare generated vs inline output in the opcode harness until confidence is high

### Stage 5 — optional direct raw-encoder pass

Only after stages 0–4 are stable should we consider bypassing parts of the mid-layer and emitting raw
AArch64 more aggressively.

That should be reserved for:

- hot ALU/branch/shift families
- places where register allocator behaviour is already well understood
- cases where midfunc abstraction measurably blocks performance or correctness

## Testing gates

Every migration step should clear, in order:

1. `tools/uae2026-opcode-harness.sh`
2. `tools/headless-jit-bootstrap-probe.sh`
3. `tools/headless-jit-bridge-smoke.sh`
4. full NeXT boot / desktop harness

## Immediate next tranche

1. keep the new opcode harness as the inner loop
2. fix bridge stability so JIT mode reaches `REGDUMP`
3. move the VC/VS condition family first (`Bcc/Scc/DBcc`) because it is small and heavily represented
4. then move memory word shifts/rotates
5. finally do `DIV*`/`MULL` via helper-backed native blocks

That order gives the fastest path from “crashes on first JIT entry” to “opcode-family equivalence under harness”.
