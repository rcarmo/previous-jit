# Previous JIT

![Previous JIT icon](docs/icon-256.png)

This repository is an **ARM64 JIT-focused fork of [Previous](https://previous.alternative-system.com/)**.
Its goal is to do for the NeXT emulator **Previous** what
[`rcarmo/macemu-jit`](https://github.com/rcarmo/macemu) did for BasiliskII/SheepShaver:
bring over a newer AArch64-capable JIT toolchain, wire it into the emulator cleanly,
and build a fast validation loop around it.

In practical terms, this tree is an attempt to create an **ARM-JIT enabled version of Previous**
for modern AArch64 systems, while keeping the original emulator usable and keeping the upstream
codebase recognizable.

## Current status

This remains an experimental AArch64 JIT fork, but the transplanted compiler,
RAM/MMU bridge, timing path, verifier and bounded validation stack are integrated.
[`docs/current-jit-status.md`](docs/current-jit-status.md) is the precedence point
for current status; dated reports retain their checkpoint evidence.

The accepted product policy at `eb7e7dd` is:

- `MVSR2` (`MOVE SR/CCR,<ea>`) and word-sized `MV2SR` (`MOVE <ea>,SR`) use the
  exact generated 68040 handler by default;
- `B2_JIT_NATIVE_FULL_SR=1` enables both compiled full-SR wrappers only as a
  diagnostic inverse;
- `PREVIOUS_UAE2026_JIT_RAM=1` remains experimental, with
  `B2_JIT_RTE_FAULT_HANDOFF=1` as the conservative desktop-acceptance oracle;
- native RAM/MMU no-handoff still has no current desktop-reaching proof.

The final exact-by-default boot used an immutable post-logout source image
(mode `0444`, SHA-256
`1d0a76447fec28d0a2737cf42021bef9136ca43c188f22aee25a3c7fa1252c8d`).
It reached Workspace/File Viewer on the first poll, remained stable for 120
seconds, and emitted zero verifier mismatch, panic, fatal, `strange rights`,
abort or SR-helper matches. Artifact:
`/workspace/tmp/previous-postlogout-normal-sr-default-20260802-115440`.

![NeXTSTEP Workspace desktop reached headlessly on AArch64](docs/desktop-headless-boot.png)

The compiled full-SR wrappers remain diagnostic because the immutable split A/B
found no boot-safe subset: compiled `MV2SR.W` caused a permanent WindowServer
startup loop, while compiled `MVSR2` reproduced
`ipc_right_copyin_header: strange rights`. See
[`docs/sr-native-helper-validation-20260731.md`](docs/sr-native-helper-validation-20260731.md).

Current bounded evidence includes:

- full opcode plus forced-fault suite: 155/155;
- RAM/MMU fast smoke: 67/67;
- CPU-state harness: 38/38;
- exact-by-default and native-inverse focused SR/fault matrices: 11/11 each;
- block-verifier ledger: 67/67 RAM/MMU and clean targeted 2/2, with explicit
  compare/skip/longjmp/specialty denominators;
- exact timing at 256 SCSI transactions, 400 CycInt pairs and 194 exceptions,
  with 6 ppm cycle and 8 ppm retirement offsets;
- bounded median speedups of 1.765103× cold-process and 6.283813× warm
  in-process CPU-loop execution.

The remaining high-risk boundary is native RAM/MMU resume with the conservative
RTE/page-fault handoff disabled. New policy changes there require focused
at-most-120-second opcode/MMU/fault discriminators and explicit producer
metadata before any longer boot-frontier evidence.

## Project layout

### Core JIT bring-up pieces

- `src/cpu/uae2026_jit_bridge.cpp` — bridge between Previous and the transplanted JIT runtime
- `src/cpu/uae2026_compiler_unit.cpp` — unity-build wrapper for vendored compiler pieces
- `src/cpu/uae_cpu_2026/` — vendored UAE 2026 JIT/compiler subtree
- `src/m68000.c` / `src/cpu/newcpu.c` — opcode test-mode integration and CPU loop hooks

### Harnesses

- `tools/headless-nextstep-harness.sh` — fresh-image headless boot harness
- `tools/headless-jit-bootstrap-probe.sh` — bootstrap-only probe
- `tools/headless-jit-bridge-smoke.sh` — full bridge smoke test
- `tools/uae2026-opcode-harness.sh` — interpreter vs JIT opcode equivalence harness
- `tools/uae2026-opcode-vectors.sh` — curated risky/missing opcode vectors
- `tools/jit-microbench.sh` — matched bounded interpreter/JIT cold/warm speed and execution-path census
- `tools/jit-timing-anchor.sh` — fixed-host interpreter/JIT timing and interrupt-cadence differential at a guest-driven SCSI anchor
- `tools/fullloop-drop-validate.sh` — full SCSI-loop barrier-drop boot harness (pure-JIT frontier)
- `tools/interp-nextbus-probe.sh` — pure-interp NextBus/kernel-reachability reference

### Docs

- `docs/current-jit-status.md` — current policy, accepted evidence, immutable boot closure and open boundaries
- `docs/sr-native-helper-validation-20260731.md` — native SR/CCR helper audit, exact-default policy, inverse control and immutable full-boot closure
- `docs/bounded-jit-benchmark-20260731.md` — fixed-frequency bounded benchmark, explicit coverage denominator, and separate cold-process (1.765×) / warm in-process (6.284×) results
- `docs/timing-anchor-validation-20260801.md` — exact SCSI/CycInt/exception cadence comparison at a bounded guest-work coordinate
- `docs/mmu-generation-churn-anchor-20260801.md` — bounded default-vs-blanket generation-key census and the measured cross-page safety boundary
- `docs/mmu-generated-dispatch-anchor-20260801.md` — guarded inline full-identity dispatch, inverse control, focused gates and the clean 256-I/O A/B (22.82M direct hits)
- `docs/uae2026-jit-bringup.md`
- `docs/uae2026-jit-mmu-strategy.md`
- `docs/uae2026-compiler-blockers.md`
- `docs/aarch64-jit-port-audit.md`
- `docs/uae2026-opcode-harness.md`
- `docs/uae2026-compemu-inline-assembly-plan.md`
- `docs/boot-frontier-scsi-hang.md` — pure-JIT boot frontier: SCSI/NextBus-checksum recompile-churn diagnosis + fix plan

## Build

Example experimental build:

```bash
cmake -S . -B build-vnc -DENABLE_VNC=ON -DENABLE_EXPERIMENTAL_UAE2026_JIT=ON
cmake --build build-vnc -j$(nproc)
```

## Validation

Current validation flow:

```bash
./tools/headless-jit-bootstrap-probe.sh
./tools/headless-jit-bridge-smoke.sh
./tools/uae2026-opcode-harness.sh
./tools/uae2026-mmu-fast-smoke.sh
./tools/fg-verify-window.sh 0x01000000-0x0100ffff 45
./tools/jit-microbench.sh
./tools/jit-timing-anchor.sh
./tools/uae2026-compiler-syntax-probe.sh
./tools/uae2026-compiler-object-probe.sh
```

Notes:

- automated boot harnesses use a **fresh writable copy per run**; the accepted source fixture is immutable and hash-pinned
- Linux startup disables host ASLR by default for deterministic JIT mappings
- `PREVIOUS_UAE2026_JIT=0` gives an interpreter baseline for harness comparison
- `PREVIOUS_UAE2026_JIT_RAM=1` enables the experimental RAM/MMU dispatch path; native RAM mode now stays in translated execution unless the explicit oracle handoff is requested
- `PREVIOUS_UAE2026_JIT_MMU_FAST_DISPATCH=1` opts into the accepted generated full-identity MMU dispatcher; unset/`0` retains the exact C-dispatch inverse while this remains experimental
- with fast dispatch enabled, the shared popall predicate is the default; `PREVIOUS_UAE2026_JIT_MMU_FAST_DISPATCH_SHARED=0` retains the accepted inline inverse for exact A/B runs
- block-verifier reports use an explicit `attempted`/`terminal` denominator and separate compare, skip, longjmp and specialty outcomes; see `docs/verifier-coverage-accounting-20260801.md`
- `B2_JIT_RTE_FAULT_HANDOFF=1` requests the conservative RTE/page-fault interpreter handoff oracle; leaving it unset (or setting `B2_JIT_RTE_FAULT_HANDOFF_DISABLE=1`) preserves the unaccepted native no-handoff path for diagnosis
- `B2_JIT_NATIVE_FULL_SR=1` enables the compiled full-SR wrappers as a diagnostic inverse; exact generated handling is the product default
- `B2_JIT_LOW_VIRTUAL_SINGLESTEP=1`, `B2_JIT_LOW_VIRTUAL_PREFETCH_GUARD=1`, `B2_JIT_EXACT_EXEC_PCS`, `B2_JIT_PCTRACE_WORDS`, and opt-in `B2_JIT_PCTRACE_LIVE=1` are diagnostics for low-user-virtual MMU/code-fetch and state-divergence analysis; they are not default-on fixes
- RAM/MMU code paths must keep data-space and code-space translations separate: the private bank `xlateaddr` is for data effective addresses, while branch/return/dispatch PC materialization uses the dedicated code-space host translator
- the vendored compiler unity build keeps its Basilisk/UAE prefs symbols renamed away from Previous's native `currprefs`/`changed_prefs`; do not reintroduce same-name globals with incompatible struct layouts

## What is being migrated

The broad plan is:

1. get the transplanted JIT stable enough to execute inside Previous
2. use the opcode harness to validate missing/risky opcode families quickly
3. move away from opaque generated `compemu.cpp` ownership toward explicit ARM64 lowering
4. eventually make this a real AArch64 JIT-enabled Previous tree, not just a staging port

See `docs/uae2026-compemu-inline-assembly-plan.md` for the current migration plan.

## Relationship to upstream Previous

Upstream Previous is a NeXT Computer emulator based on Hatari and WinUAE CPU core work.
It emulates:

- NeXT Computer (original 68030 Cube)
- NeXTcube
- NeXTcube Turbo
- NeXTstation
- NeXTstation Turbo
- NeXTstation Color
- NeXTstation Turbo Color
- NeXTdimension Graphics Board

This fork is not trying to replace upstream identity or history; it is a focused JIT porting branch
with extra tooling, docs, and experimental runtime code.

## Running Previous

You still need ROM images and normal Previous configuration/assets to run the emulator.

While the emulator is running, you can open the configuration menu with `F12`, toggle fullscreen
with `F11`, and initiate a clean shutdown with `F10`.

## Contributors

Original Previous was written by Andreas Grabher, Simon Schubiger and Gilles Fetis.

Many thanks go to the members of the NeXT International Forums and to the original emulator authors
and contributors whose work this fork builds on.
