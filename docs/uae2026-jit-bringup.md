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
- `PREVIOUS_UAE2026_JIT_RAM=1` enables the experimental RAM/MMU dispatch mode. RAM mode no longer auto-drops to the interpreter at the RTE/page-fault seam; the conservative desktop-boot oracle is explicit via `B2_JIT_RTE_FAULT_HANDOFF=1`. The native 100%-JIT path still does not reach the desktop. The current low-PC comparison frontier is post-RTE resume state after the matched `00008b24` data fault: with default-off `B2_JIT_LOW83_CODEHOST=1 B2_JIT_LOW7F_CODEHOST=1`, native matches the handoff oracle through nine low catches and then diverges with an extra native-only catch at `0000ee58` (`addr=0001402a`).
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

Latest translated-execution debug checkpoint (2026-05-24):
- full opcode harness passes: `total=74`, `jit_ok=74`, `pass=74`, `fail=0`, `infra_fail=0`, `score=100` (`/workspace/tmp/previous-opcode-harness-20260524-081241`)
- RAM-code MMU fast smoke passes from `B2_TEST_ADDR=0x04008000`: `total=31`, `pass=31`, `fail=0`, `score=100` (`/workspace/tmp/previous-mmu-fast-smoke-20260524-081030`)
- default/ROM JIT smoke reaches the desktop in the latest no-DC check (`/workspace/tmp/previous-jit-lowpcdiag-default-20260523-202501`)
- RAM translation remains gated by `PREVIOUS_UAE2026_JIT_RAM=1`; true RAM dispatch is counted only for `0x04000000..0x07ffffff` dispatch PCs so bogus zero-PC recovery does not look like RAM progress
- explicit handoff RAM-requested mode reaches true RAM dispatch and boots to a stable desktop when `B2_JIT_RTE_FAULT_HANDOFF=1` is set (`/workspace/tmp/previous-jit-explicit-handoff-ram-20260522-090029`: `desktop_reached=1`, `stable_reached=1`, `jit_ram_dispatch_seen=1`, `jit_dispatch_lines=6755`, `jit_last_pc=0409f5cc`)
- automatic RAM-mode RTE handoff remains removed, so `PREVIOUS_UAE2026_JIT_RAM=1` preserves the native 100%-JIT path unless `B2_JIT_RTE_FAULT_HANDOFF=1` is explicitly set. Native no-handoff is not fixed: the current grounded discriminator uses the explicit-handoff oracle and default-off `B2_JIT_LOW83_CODEHOST=1 B2_JIT_LOW7F_CODEHOST=1`; it matches oracle low catches through `00008b24`, then native surfaces an extra `0000ee58` catch (`addr=0001402a`) absent from the 2600-entry oracle trace.
- committed RAM-mode fixes restore both MMU fixup slots, add conservative auto-update EA rollback, force auto-update and complex return-family opcodes through exact fallback barriers, add code-space MMU translation for branch/dispatch PCs, canonicalize post-exception PC/SR, save the active supervisor exception SP/ISP across bridge catches, and remove or gate RAM direct shortcuts that bypass 040 restart/fixup bookkeeping
- RAM/MMU data/code translation remains split: the private bank `xlateaddr` uses data-space `Uae2026JitMmuXlateData()`, while `Uae2026JitMmuXlateCodeHost()` is used for dispatch PC materialization and ARM64 `get_n_addr_jmp()` branch/return targets
- bridge MMU restart handoff uses `m68k_setpc(restart_pc)` rather than `m68k_setpci(restart_pc)` so `regs.pc`/`pc_p`/`pc_oldp` are coherent before translated execution resumes
- bridge-side transaction coverage now includes explicit `call_push` producers for BSR paths and return-pop producers for `RTS`/`RTR` fallback paths; the proven historical `00003372/00003374 -> 00012b04` BSR seam still keeps its legacy compatibility scan until a producer covers that exact shifted-PC/native path
- generated/native `jit_op_rte()` routes through the exact interpreter RTE implementation for the opcode semantics, but bridge-caught RTE/page-fault seams no longer disable JIT automatically in RAM mode; set `B2_JIT_RTE_FAULT_HANDOFF=1` to request the conservative interpreter handoff oracle
- zero-PC vector recovery is disabled while the 040 MMU is enabled, so zero PC remains a diagnostic symptom instead of masking a bad RTE/page-fault resume by jumping to vector 2

- current code checkpoint adds a bridge-side call-push transaction producer that can decode BSR and selected JSR target forms from the current code host stream, wires the producer into fallback execution for the confirmed low-user JSR seams, keeps the legacy BSR compatibility scan forward-only, and widens RAM code-shadow synchronization in `Uae2026JitMmuXlateCodeHost()` from 256 bytes to 8 KiB so page-boundary translated code fetches see coherent shadow bytes.
- recent native-resume fixes kept target-fetch canonicalization narrow to the confirmed user `05027706` `JSR (A0)` seam and added bridge PC advancement for confirmed non-restartable byte-store faults at `0500b6ae` (`MOVE.B D2,(A0)`) and `0500bc98` (`MOVE.B (A2)+,(A0)`). The corresponding normal side-effect shapes are covered by RAM-code fast vectors; those user seams are no longer the best current frontier, which is now the post-`00008b24` low-PC resume divergence.
- post-RTE low-virtual and high-user opcode fetch now use code-host bytes for confirmed non-identity RAM/MMU mappings while keeping early ROM/low-overlay SCSI boot on the legacy 040 path. Covered default windows: isolated post-kernel low-PC fetch (`vbr=040ae61c`, `00003300..00003400`) and high user mappings (`05000000..07ffffff`) where `pc_p` translates to a different physical page. This avoids stale data-view streams such as `00003334` (`0200/0c80` vs `PCTSHADOW=204f/9efc`) and the later `05054b0e..050abffe` zero-walk (`PCTOPS=0000` while `PCTSHADOW/PCTLIVE` contain real user code). Additional post-RTE low-PC code-host discriminator windows remain default-off: `B2_JIT_LOW12B_CODEHOST=1`, `B2_JIT_LOW83_CODEHOST=1`, and `B2_JIT_LOW7F_CODEHOST=1`. `LOW83+LOW7F` is useful for oracle comparison because it moves native past the stale `00008334/op=2010` stream to the oracle `00007f72` target and matches catch sequencing through `00008b24`, but it is not a desktop fix.
- noisy 68040 MMU exception-frame logging is gated behind `B2_JIT_TRACE_MMU_FRAME=1`; the prefetch-guard mismatch trace is separately gated behind `B2_JIT_TRACE_PREFETCH_GUARD=1`; `B2_JIT_PCTRACE_LIVE=1` is opt-in because live addrbank reads can have side effects or fault; `B2_JIT_TRACE_LOW33_PCP=1`/`B2_JIT_TRACE_LOWPC_PCP=1` are bridge-only, non-mutating diagnostics that log host `pc_p` words beside data-view words for low33/all low-PC post-RTE faults; `B2_JIT_TRACE_LOWPC_RESUME=1` logs bounded pre/post-`Exception(2)` low-PC resume state (USP/ISP/MSP, saved JIT restart state, flags, and stack words) for the current post-`00008b24` divergence; low-PC data faults now refresh the 68040 frame effective-address word from the actual bus fault address while leaving code-fetch faults distinct
- do **not** rewrite `fault_pc`/`instruction_pc` to `mmu_fault_addr` for RTE faults; that diagnostic was tested and rejected because the opcode context and access address must remain distinct

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
3. Continue implementing the transaction-based RAM/MMU restart model described in `docs/uae2026-jit-mmu-strategy.md`; keep the current BSR target-fetch rollback as the proven compatibility shim until generated code publishes explicit metadata for that exact seam.
4. Add minimal targeted regressions for the confirmed RAM/MMU patterns: BSR target-fetch fault after return push, `RTS`/`RTR` target fetch after return pop, RTE return-code fetch after SR/A7 switch, and the post-RTE low-PC native-resume sequence that now matches the oracle through `00008b24` before the extra native `0000ee58` catch.
5. Audit RTE/page-fault native resume without conflating `fault_pc`/`instruction_pc` and `mmu_fault_addr`; RAM/MMU helper calls now force a full live-state flush before helper-delivered `Exception(2)`, but the current divergence points at wrong post-fault resume/return state after the `00008b24` data fault, not a simple code-host byte mismatch.
6. Once native RAM/MMU resume reaches desktop without the conservative RTE handoff, capture the final RAM-mode screenshot and update this log with metrics.

## Guardrails

- Keep the headless harness using a **fresh image copy per run**.
- Do not mutate the canonical desktop snapshot during automated bring-up.
- Keep interpreter boot working while the experimental bridge is enabled.
- Log every structural bring-up step in this file as it lands.
