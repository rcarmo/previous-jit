# Previous AArch64 JIT Port Audit

## Status

- Repository: `/workspace/projects/previous`
- Branch: `audit/uae-jit-port-plan`
- Base build-fix commit: `59b1131` (`Fix AArch64/GCC build for Previous on modern toolchains`)
- Native binary currently builds at: `build-clean/src/Previous`
- Experimental VNC output path is now available via `PREVIOUS_VNC=1` (port with `PREVIOUS_VNC_PORT`, default `5900`)

## Executive summary

`Previous` is a **plausible but nontrivial** target for porting the modern BasiliskII AArch64 JIT work.

Why it is plausible:
- It already uses a **WinUAE-derived CPU/MMU/FPU core**.
- It already has the expected `newcpu` / `cpummu` / `fpp` / `memory` split.
- It still carries an older **dormant JIT subtree** under `src/cpu/jit/`.

Why it is nontrivial:
- The active CPU core in `Previous` is **not** wired to build the JIT layer today.
- The existing `src/cpu/jit/` code is older and x86-oriented.
- The newer BasiliskII work lives in a **different integration generation** (`uae_cpu_2026/compiler`).
- NeXT workloads depend heavily on **MMU correctness**, which increases JIT risk substantially.

## Current CPU/JIT structure in Previous

### Active CPU/MMU/FPU path

These files are actively involved in CPU emulation:
- `src/cpu/newcpu.c`
- `src/cpu/newcpu.h`
- `src/cpu/cpummu.c`
- `src/cpu/cpummu030.c`
- `src/cpu/fpp.c`
- `src/cpu/fpp-softfloat.h`
- `src/cpu/memory.c`
- `src/cpu/hatari-glue.c`
- `src/cpu/cpuemu_common.c`

The CMake wiring in `src/cpu/CMakeLists.txt` currently builds:
- generated: `cpudefs.c`, `cpustbl.c`, `cpuemu_31.c`, `cpuemu_32.c`
- core: `cpummu.c`, `cpummu030.c`, `cpuemu_common.c`, `hatari-glue.c`, `memory.c`, `newcpu.c`, `readcpu.c`, `fpp.c`

This means the active configuration is already using the MMU-enabled WinUAE CPU path (`cpuemu_31`, `cpuemu_32`).

### Dormant JIT subtree

`Previous` still contains an older JIT subtree under `src/cpu/jit/`:
- `compemu.h`
- `compemu_support.c`
- `compemu_support_codegen.c`
- `compemu_fpp.c`
- `gencomp.c`
- `codegen_x86.c`
- `compemu_optimizer_x86.c`
- `compemu_raw_x86.c`
- etc.

However:
- `src/cpu/sysconfig.h` has `//#define JIT`
- `src/cpu/CMakeLists.txt` does **not** compile the JIT subtree
- the included JIT code is **x86-oriented**, not the modern AArch64 path we have in BasiliskII

## Relevant BasiliskII JIT source generation

The modern BasiliskII work is in:
- `BasiliskII/src/uae_cpu_2026/`
- especially `BasiliskII/src/uae_cpu_2026/compiler/`

Important files there:
- `compiler/compemu.h`
- `compiler/compemu_support.cpp`
- `compiler/compemu_support_arm.cpp`
- `compiler/compemu_legacy_arm64_compat.cpp`
- `compiler/codegen_arm64.cpp`
- `compiler/codegen_arm64.h`
- `compiler/compemu_midfunc_arm64.cpp`
- `compiler/compemu_midfunc_arm64_2.cpp`
- `compiler/compemu_prefs.cpp`
- `compiler/gencomp_arm.c`
- `compiler/compstbl_arm.cpp`

## Porting conclusion

### What looks reusable in principle

The following concepts are portable:
1. **AArch64 code generator** (`codegen_arm64.*`)
2. **ARM64 midfunc/helper emission** (`compemu_midfunc_arm64*`)
3. **legacy helper compatibility layer** (`compemu_legacy_arm64_compat.cpp`)
4. **allocator fixes for non-low-32-bit host addresses**
5. **lazy JIT bring-up / build-on-first-use strategy**
6. **generator/build wiring for JIT compiler tables**

### What is not directly portable as-is

1. **Basilisk memory/runtime glue**
   - Basilisk-specific direct-addressing assumptions
   - cache/tag allocator behavior
   - exception/restart handling assumptions

2. **Build system wiring**
   - Basilisk Makefile/configure logic differs substantially from Previous's CMake wiring

3. **Machine/device integration**
   - Previous uses Hatari/NeXT glue and NeXT-specific device semantics
   - NeXT boot paths are MMU-heavy and much less forgiving than classic Basilisk workloads

## Recommended strategy

### Preferred route: import the newer compiler layer

Do **not** try to evolve `Previous`'s old x86 JIT subtree directly.

Instead:
1. keep Previous's machine/device emulation
2. keep Previous's active WinUAE-derived MMU/FPU CPU path
3. graft in the **newer compiler layer** from BasiliskII's `uae_cpu_2026/compiler`
4. adapt Previous's glue/build/runtime state to that layer

Why:
- the old `src/cpu/jit/` is not the code we have been improving
- the BasiliskII AArch64 work already contains the compatibility and allocator lessons we need
- forward-porting the compiler layer is likely less risky than backporting all AArch64 work into the stale x86 JIT tree

## High-risk areas

1. **MMU correctness**
   - NeXTSTEP/OpenStep depend on 68030/68040 MMU behavior
   - this is the biggest correctness risk for any JIT attempt

2. **Exception / restart semantics**
   - page faults, bus faults, restartable FPU/MMU instructions

3. **Memory mapping model**
   - JIT assumptions about addressability and translated memory vs Previous's NeXT bus/memory model

4. **Generated table compatibility**
   - BasiliskII's JIT generator output and Previous's active CPU tables need reconciliation

## Suggested implementation phases

### Phase 0 — lock down interpreter baseline
- boot Previous reliably with ROM + disk images
- record machine/ROM combinations that boot successfully
- create repeatable smoke-test configs

### Phase 1 — build-system spike
- add an opt-in CMake option for experimental JIT build wiring
- prove the new compiler layer can at least compile into the tree

### Phase 2 — glue-layer mapping
- compare Previous's `newcpu/cpummu/memory/hatari-glue` integration against BasiliskII's `basilisk_glue + compemu_prefs + compiler_init/build_comp` expectations
- list required adapter shims

### Phase 3 — AArch64 no-op integration
- compile the new compiler layer without using it at runtime
- ensure runtime starts with JIT disabled / dormant

### Phase 4 — first-use JIT bring-up
- enable compiler initialization on a small controlled path
- chase allocator/cache/tag/runtime failures first

### Phase 5 — MMU/NeXT boot validation
- only after basic runtime survival, attempt real booting under JIT

## Concrete next code areas to compare

### Previous
- `src/cpu/newcpu.c`
- `src/cpu/newcpu.h`
- `src/cpu/cpummu.c`
- `src/cpu/cpummu030.c`
- `src/cpu/memory.c`
- `src/cpu/hatari-glue.c`
- `src/cpu/CMakeLists.txt`
- `src/includes/configuration.h`
- `src/configuration.c`

### BasiliskII
- `BasiliskII/src/uae_cpu_2026/newcpu.cpp`
- `BasiliskII/src/uae_cpu_2026/newcpu.h`
- `BasiliskII/src/uae_cpu_2026/cpummu.h`
- `BasiliskII/src/uae_cpu_2026/memory.cpp`
- `BasiliskII/src/uae_cpu_2026/basilisk_glue.cpp`
- `BasiliskII/src/uae_cpu_2026/compemu_prefs.cpp`
- `BasiliskII/src/uae_cpu_2026/compiler/*`
- `BasiliskII/src/Unix/configure.ac`
- `BasiliskII/src/Unix/Makefile.in`

## Bottom line

A port is structurally plausible, but it should be treated as a **compiler-layer transplant onto a WinUAE/MMU-heavy NeXT emulator**, not as a quick patch series. Interpreter boot with real NeXT ROM/disk images should be treated as the immediate prerequisite for any serious JIT work.

## Automation note

A libvncserver-based SDL capture path has been added for automation and remote testing. It mirrors the rendered SDL output and accepts basic keyboard/mouse input through injected SDL events. Enable it with:

```bash
PREVIOUS_VNC=1 PREVIOUS_VNC_PORT=5900 ./tools/run-local.sh /workspace/assets/previous/configs/previous-example.cfg
```

This is useful for smoke tests and remote observation while interpreter-mode boot work is being stabilized.
