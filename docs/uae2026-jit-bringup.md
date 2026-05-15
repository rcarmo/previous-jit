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
- `tools/headless-jit-bridge-smoke.sh` rebuilds the experimental binary and proves bridge logging, ASLR active, bootstrap allocation active, and desktop reachability with default/ROM translated execution enabled.
- `PREVIOUS_UAE2026_JIT_RAM=1` enables the experimental RAM/MMU dispatch mode. RAM mode now records true RAM dispatch, but remains blocked before desktop; the current discriminator is low-user-virtual instruction-fetch/MMU restart state around `0x00003200..0x00003400`.
- `tools/uae2026-compiler-syntax-probe.sh` records the current compile-time blocker set for direct vendored compiler integration.
- `tools/uae2026-compiler-object-probe.sh` compiles the vendored ARM64 compiler core to an object file under the probe prelude.
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

Latest translated-execution debug checkpoint (2026-05-14):
- opcode harness passes: `total=62`, `jit_ok=62`, `pass=62`, `fail=0`, `infra_fail=0`, `score=100` (`/workspace/tmp/previous-opcode-harness-prefetch-guard-audit-20260514-150218`)
- default/ROM JIT smoke passes and stayed stable for 60 seconds: `desktop_reached=1`, `stable_reached=1`, `jit_ram_requested=0`, `jit_ram_dispatch_seen=0`, `jit_dispatch_lines=4805`, `jit_last_pc=0100bb08` in `/workspace/tmp/previous-jit-prefetch-guard-audit-default-stable-20260514-151230`
- RAM translation remains gated by `PREVIOUS_UAE2026_JIT_RAM=1`; true RAM dispatch is counted only for `0x04000000..0x07ffffff` dispatch PCs so bogus zero-PC recovery does not look like RAM progress
- baseline RAM mode still fails before desktop at the low-user-virtual `00003352` probe fault (`CMPI.B #$40,(8,A4)`, `addr=00000008`, `A4=0`), while `B2_JIT_RTE_FAULT_HANDOFF=1` proves the guest state is recoverable by reaching a stable desktop (`/workspace/tmp/previous-jit-bridge-smoke-rte-handoff-stable-ram-20260514-032751`)
- committed RAM-mode fixes restore both MMU fixup slots, add conservative auto-update EA rollback, force auto-update and return-family opcodes through exact fallback barriers, add code-space MMU translation for branch/dispatch PCs, canonicalize post-exception PC/SR, save the active supervisor exception SP/ISP across bridge catches, and remove or gate RAM direct shortcuts that bypass 040 restart/fixup bookkeeping
- RAM/MMU data/code translation remains split: the private bank `xlateaddr` uses data-space `Uae2026JitMmuXlateData()`, while `Uae2026JitMmuXlateCodeHost()` is used for dispatch PC materialization and ARM64 `get_n_addr_jmp()` branch/return targets
- bridge MMU restart handoff uses `m68k_setpc(restart_pc)` rather than `m68k_setpci(restart_pc)` so `regs.pc`/`pc_p`/`pc_oldp` are coherent before translated execution resumes
- noisy 68040 MMU exception-frame logging is gated behind `B2_JIT_TRACE_MMU_FRAME=1`; the prefetch-guard mismatch trace is separately gated behind `B2_JIT_TRACE_PREFETCH_GUARD=1`
- build-hygiene audit keeps vendored compiler prefs renamed away from Previous-native `currprefs`/`changed_prefs`, guards duplicate `USE_JIT`, casts AArch64 instruction words before emission, and adds a defensive vreg bounds check around ARM64 `set_status()`; latest prefetch-guard audit rebuild was warning-free (`/workspace/tmp/previous-build-prefetch-audit.log`)
- RTE restart hardening publishes pre-interpreter-fallback SR/A7/opcode state for both trace-time and compiled fallback paths, preserves the interpreter-updated post-pop `regs.isp` when an `RTE` has switched to user mode before faulting, and restores pre-RTE supervisor state if a RAM/MMU fault escapes after a partial `RTE`
- native RAM/MMU helper calls now force a full `flush(1)` and publish the current JIT flag snapshot into `Uae2026JitLastFlags` before bank/code-host helpers can raise a 68040 MMU exception; the bridge restart path restores this snapshot when `mmu_restart` is set
- frontier history moved from the original advanced-A1/auto-update corruption, to repeated handler `RTE` faults at `04002186`, to a `040014b2` `MOVES.L D0,(A0)` DFC write fault, to the current low-user-virtual `00003352` probe failure
- low-virtual code-fetch discriminator: `B2_JIT_LOW_VIRTUAL_SINGLESTEP=1` single-steps only the confirmed ROM probe window (`0x00003200..0x00003400`) by default through a real 040 MMU opcode fetch plus `cpufunctbl`, with optional `B2_JIT_LOW_VIRTUAL_SINGLESTEP_START/END` for range bisection. This eliminates the repeated `00003352`/`addr=00000008` loop and reaches `root on sd@`, but still no desktop within 600/1200s; widening toward `0x00014000` exposes a later post-root stall at `0401da6c` in an FPU/math compare branch.
- native low-virtual prefetch guard discriminator: `B2_JIT_LOW_VIRTUAL_PREFETCH_GUARD=1` performs the interpreter-style 040 opcode fetch/restart-state publication for the default `0x00003200..0x00003400` window (optional `B2_JIT_LOW_VIRTUAL_PREFETCH_START/END`). It now runs both at the dispatch boundary, so low-virtual blocks that fall through `execute_normal()`/fallback are covered, and before native low-virtual L2 ops. It publishes `instruction_pc`/`fault_pc` before faultable opcode fetch, leaves `mmu_opcode=0xffff` for instruction-fetch faults, and republishes the opcode actually returned by `Uae2026JitMmuFetchOpcode()` after a successful fetch so later native data faults restart with interpreter-like opcode state.
- guard-on RAM discriminator still sees repeated `00003352` data faults, but progresses past them to `root on sd@` while continuing to execute native low-virtual code; the dispatch-level guard did not reduce that data-fault loop, so the remaining issue is no longer missing opcode-fetch publication for fallback blocks. Latest guard-on run: `/workspace/tmp/previous-jit-dispatch-prefetch-guard-ram-20260515-052015` (`jit_ram_dispatch_seen=1`, `jit_dispatch_lines=165482`, `jit_last_pc=04061496`, no fetched/compiled opcode mismatches, no desktop). `B2_JIT_SYNC_TICKS=1` also reached `root on sd@` but not desktop (`/workspace/tmp/previous-jit-dispatch-prefetch-syncticks-ram-20260515-053514`).
- diagnostic `B2_JIT_DUMP_PC_WORDS_START/END[/LIMIT]` dumps sampled dispatch PC words and key registers under `PREVIOUS_JIT_TRACE_DC=1`; the post-root RAM timeout samples map mainly to VM/scheduler-style kernel paths, including lookup loops at `0406135a`/`04061496` over `040b0b00..040b12a8`, plus allocator/object paths around `040533xx`, `04057axx`, `040589xx`, `0405c7xx`, and `040600xx` (`/workspace/tmp/previous-jit-prefetch-guard-pcwords-20260515-050115`, disassembly `/workspace/tmp/pcwords_all_disasm.txt`).
- do **not** rewrite `fault_pc`/`instruction_pc` to `mmu_fault_addr` for RTE faults; that diagnostic was tested and rejected because the opcode context and access address must remain distinct
- follow-up wiring already replaced the no-op MOVEC bridge stubs with control-register state updates (`VBR`, stack pointers, `TC`, `TT*`, `SRP/URP`, `CACR`), routes RAM-mode writable/unclassified data accesses through live addrbank helpers, and leaves only immutable ROM-shadow reads direct in RAM mode
- next work should either make the low-virtual code-fetch/MMU-safe path semantically complete enough to be default RAM behavior or add a minimal regression that exercises the same restart seam without a 600s boot

### Run the vendored compiler object probe

```bash
cd /workspace/projects/previous
./tools/uae2026-compiler-object-probe.sh
```

Expected success metrics:
- `rc=0`
- object file produced for `compemu_support_arm.cpp`

## Next steps

1. Keep `./tools/uae2026-opcode-harness.sh` green before and after every RAM/MMU change.
2. Preserve the default/ROM JIT desktop smoke (`desktop_reached=1`, preferably with `PREVIOUS_STABLE_WAIT=60`) while debugging RAM mode.
3. Add minimal targeted regressions for the confirmed RAM/MMU patterns: auto-update EA fault -> MMU-handler `RTE` back to low user virtual PC, and low-virtual opcode fetch -> native data fault with fetched opcode restart state preserved.
4. Audit RTE/page-fault state without conflating `fault_pc`/`instruction_pc` and `mmu_fault_addr`; RAM/MMU helper calls now force a full live-state flush before helper-delivered `Exception(2)`, and the remaining seam is low-user-virtual code-fetch/MMU-safe JIT resume after nested handler returns.
5. Promote the low-virtual prefetch guard from diagnostic to default RAM behavior only after instruction-fetch faults, successful fetches, and later data faults all match interpreter restart semantics.
6. Once RAM mode reaches desktop, capture the final RAM-mode screenshot and update this log with metrics.

## Guardrails

- Keep the headless harness using a **fresh image copy per run**.
- Do not mutate the canonical desktop snapshot during automated bring-up.
- Keep interpreter boot working while the experimental bridge is enabled.
- Log every structural bring-up step in this file as it lands.
