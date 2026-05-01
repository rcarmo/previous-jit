# UAE 2026 JIT Bring-up Log

This file tracks the incremental transplant of the BasiliskII `uae_cpu_2026` work into `Previous`.
Update it as code lands so the repository always explains the current experimental state.

## Goals

1. Keep interpreter-mode `Previous` booting reliably.
2. Stage the BasiliskII 2026 CPU/JIT sources inside this tree.
3. Add a fresh-image test harness so each automated boot starts from a clean snapshot.
4. Add a build-time gate for experimental JIT integration before enabling any runtime path.
5. Only enable real runtime JIT after the bridge, prefs shim, and MMU-sensitive smoke tests are stable.

## Current status

- `src/cpu/uae_cpu_2026/` is vendored from BasiliskII.
- `tools/sync-uae-2026.sh` refreshes that subtree from `/workspace/projects/macemu/BasiliskII/src/uae_cpu_2026`.
- `tools/headless-nextstep-harness.sh` boots a **fresh copied image** for each run and drives recovery/desktop entry over VNC.
- Linux startup now disables host ASLR by default (with a one-time self re-exec) unless `PREVIOUS_DISABLE_ASLR=0` is set.
- `ENABLE_EXPERIMENTAL_UAE2026_JIT=ON` compiles an experimental bridge.
- The bridge now includes a **Previous-specific compiler-facing prefs shim** that drives the vendored `compemu_prefs.cpp` logic from Previous state/env and reports whether a runtime-disabled compiler bootstrap would be safe (`requested`, `bootstrap_ready`, `aslr`, cache size, MMU/FPU state).
- The bridge now performs a **runtime-disabled bootstrap allocation probe**: it allocates and clears an executable cache buffer when the experimental JIT is requested and the safety checks pass, but still does not hand execution to translated code.
- `tools/headless-jit-bootstrap-probe.sh` verifies the bridge/bootstrap path without waiting for a full desktop boot.
- `tools/headless-jit-bridge-smoke.sh` rebuilds the experimental binary and currently proves bridge logging, ASLR active, and bootstrap allocation active. **Translated-execution desktop reachability is not yet restored**; the latest runtime tranche reaches the ROM SCSI boot path but does not reach the Workspace.
- `tools/uae2026-compiler-syntax-probe.sh` records the current compile-time blocker set for direct vendored compiler integration.
- `tools/uae2026-compiler-object-probe.sh` compiles the vendored ARM64 compiler core to an object file under the probe prelude without linking it into `Previous` yet.
- Current blocker inventory lives in `docs/uae2026-compiler-blockers.md`.

## Implemented milestones

### 2026-04-24 — staging + headless harness

Added:
- `src/cpu/uae_cpu_2026/`
- `tools/sync-uae-2026.sh`
- `tools/previous_headless_vnc.py`
- `tools/headless-nextstep-harness.sh`

Headless harness behavior:
- chooses latest desktop-capable English backup by default
- copies it to a temporary per-run image
- boots under `Xvfb`
- drives VNC input
- runs `fsck -y /dev/rsd0a`
- exits single-user shell
- waits for `Workspace` / `File Viewer`

Key env knobs:
- `PREVIOUS_BIN`
- `PREVIOUS_SOURCE_IMAGE`
- `PREVIOUS_KEEP_RUN_IMAGE=1`
- `PREVIOUS_VNC_PORT`
- `PREVIOUS_HEADLESS_OUTDIR`

### 2026-04-24 — ASLR disable on emulator boot

Implemented in `src/main.c`.

Behavior:
- on Linux, `Previous` requests `ADDR_NO_RANDOMIZE`
- if needed it re-execs itself through `/proc/self/exe`
- this happens before normal emulator initialization
- opt out with:

```bash
PREVIOUS_DISABLE_ASLR=0 ./build-vnc/src/Previous
```

Reason:
- deterministic host virtual addresses are useful during JIT bring-up, cache allocation, and pointer-heavy diagnostics.

### 2026-04-24 — experimental build gate

Implemented in CMake:
- `ENABLE_EXPERIMENTAL_UAE2026_JIT=ON`

Current behavior:
- adds `src/cpu/uae2026_jit_bridge.cpp`
- compiles a C++ bridge that includes vendored UAE 2026 headers
- logs bridge presence during CPU init
- **does not** alter runtime dispatch or enable native JIT execution yet

This started as a compile-only integration checkpoint.

### 2026-04-24 — compiler prefs shim + bootstrap probe + bridge smoke harness

Added/updated:
- `src/cpu/uae2026_jit_bridge.h`
- `src/cpu/uae2026_jit_bridge.cpp`
- `src/cpu/uae2026_compiler_prefs_shim.h`
- `src/cpu/uae2026_compiler_prefs_shim.cpp`
- `src/cpu/uae_cpu_2026/prefs.h`
- `src/cpu/uae_cpu_2026/sysdeps.h`
- `tools/headless-jit-bootstrap-probe.sh`
- `tools/headless-jit-bridge-smoke.sh`
- `tools/headless-nextstep-harness.sh`
- `tools/sync-uae-2026.sh`
- `tools/uae2026-compiler-syntax-probe.sh`
- `tools/uae2026-compiler-object-probe.sh`
- `docs/uae2026-compiler-blockers.md`

Bridge/compiler behavior now:
- uses a Previous-specific compiler prefs shim that wraps vendored `src/cpu/uae_cpu_2026/compemu_prefs.cpp`
- feeds it from `currprefs` plus env overrides
- tracks:
  - `PREVIOUS_UAE2026_JIT`
  - `PREVIOUS_UAE2026_JIT_BOOTSTRAP`
  - `PREVIOUS_UAE2026_JIT_CACHE_KB`
  - `PREVIOUS_UAE2026_JIT_FPU`
  - `PREVIOUS_UAE2026_JIT_LAZY_FLUSH`
  - `PREVIOUS_UAE2026_JIT_CONST_JUMP`
- reports whether a runtime-disabled bootstrap is considered ready:
  - experimental JIT requested
  - bootstrap path enabled
  - host is AArch64
  - ASLR disable is active
  - MMU is enabled
  - cache size is sane
- when ready, allocates a temporary executable cache region and logs:
  - `bootstrap_attempted`
  - `bootstrap_active`
  - `cache_addr`
  - `cache_bytes`

Bootstrap probe behavior:
- launches `Previous` headlessly on a fresh copied image
- waits only long enough for early CPU/JIT bridge initialization
- verifies from logs:
  - bridge compiled/logged
  - bootstrap-ready summary reported
  - bootstrap allocation active
  - ASLR active

Smoke harness behavior:
- configures/builds `Previous` with `ENABLE_EXPERIMENTAL_UAE2026_JIT=ON`
- launches the normal fresh-image headless harness
- verifies from logs/results:
  - bridge compiled/logged
  - bootstrap-ready summary reported
  - bootstrap allocation active
  - ASLR active
  - desktop reached

This is still **runtime-disabled** for translated execution, but it proves the experimental binary, fresh-image workflow, vendored compiler-prefs shim, early cache/bootstrap plumbing, bridge-state reporting, and standalone vendored compiler probing all work together.

## Build / test commands

### Sync vendored subtree

```bash
cd /workspace/projects/previous
./tools/sync-uae-2026.sh
```

### Configure and build with bridge enabled

```bash
cd /workspace/projects/previous
cmake -S . -B build-vnc -DENABLE_VNC=ON -DENABLE_EXPERIMENTAL_UAE2026_JIT=ON
cmake --build build-vnc -j$(nproc)
```

### Run the fresh-image headless harness

```bash
cd /workspace/projects/previous
./tools/headless-nextstep-harness.sh
```

Expected success metrics:
- `desktop_reached=1`
- `desktop_tag=desktop_XX`

### Run the experimental bootstrap probe

```bash
cd /workspace/projects/previous
./tools/headless-jit-bootstrap-probe.sh
```

Expected success metrics:
- `bridge_compiled=1`
- `bootstrap_ready=1`
- `bootstrap_active=1`
- `aslr_active=1`

### Run the experimental bridge smoke harness

```bash
cd /workspace/projects/previous
./tools/headless-jit-bridge-smoke.sh
```

Expected success metrics:
- `bridge_compiled=1`
- `bootstrap_ready=1`
- `bootstrap_active=1`
- `aslr_active=1`
- `desktop_reached=1`

### Run the opcode equivalence harness

```bash
cd /workspace/projects/previous
./tools/uae2026-opcode-harness.sh
```

This injects short M68K opcode vectors into the ROM mirror, runs one interpreter/JIT pass,
and compares the resulting `REGDUMP:` state instead of waiting for a full NeXT boot.
See `docs/uae2026-opcode-harness.md` for the current vector set and latest results.

Latest translated-execution debug checkpoint (2026-05-01):
- opcode harness passes: `total=62`, `jit_ok=62`, `pass=62`, `fail=0`, `infra_fail=0`, `score=100`
- JIT entry SIGSEGV was fixed by ensuring the vendored VM allocator uses executable `mmap`/`mprotect` memory instead of a `calloc` fallback
- opcode-test translated execution now syncs the JIT shadow ROM after the test harness patches `NEXTRom`; stale shadow ROM was the cause of the previous `+0x100` PC drift
- non-ROM blocks are currently held at interpreter dispatch while RAM/shadow coherency is being debugged
- smoke is still failing: latest state reaches the ROM boot/SCSI path and displays `booting SCSI target 0, lun 0`, but does not reach the desktop
- current comparison target is normal interpreter boot, which reaches `desktop_reached=1`; next work is to bisect where JIT dispatch diverges from the normal emulator path around timers/SCSI completion and RAM-vs-shadow state

### Run the vendored compiler object probe

```bash
cd /workspace/projects/previous
./tools/uae2026-compiler-object-probe.sh
```

Expected success metrics:
- `rc=0`
- object file produced for `compemu_support_arm.cpp`

## Next steps

1. Move from bridge-owned bootstrap allocation to actual vendored compiler bootstrap entry points (`compiler_init` / cache plumbing) while keeping translated dispatch disabled.
2. Add a dedicated compile/bootstrap probe that can validate compiler init separately from full desktop boot.
3. Reduce the blocker list in `docs/uae2026-compiler-blockers.md` until `compemu_support_arm.cpp` and its direct object compile pass under probe conditions.
4. Start compiling the minimum additional vendored compiler/runtime objects needed for a true no-dispatch bootstrap target inside `Previous`.
5. Only then start wiring translated block dispatch into `newcpu`.

## Guardrails

- Keep the headless harness using a **fresh image copy per run**.
- Do not mutate the canonical desktop snapshot during automated bring-up.
- Keep interpreter boot working while the experimental bridge is enabled.
- Log every structural bring-up step in this file as it lands.
