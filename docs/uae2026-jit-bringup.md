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
- `PREVIOUS_UAE2026_JIT_RAM=1` enables the experimental RAM/MMU dispatch mode. RAM mode now records true RAM dispatch, but remains blocked before desktop in the 68040 MMU RTE/page-fault path.
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

Latest translated-execution debug checkpoint (2026-05-12):
- opcode harness passes: `total=62`, `jit_ok=62`, `pass=62`, `fail=0`, `infra_fail=0`, `score=100` (`/workspace/tmp/previous-opcode-harness-20260512-213730`)
- default/ROM JIT smoke passes and stayed stable for 60 seconds: `desktop_reached=1`, `stable_reached=1`, `jit_ram_requested=0`, `jit_ram_dispatch_seen=0`, `jit_dispatch_lines=4805`, `jit_last_pc=0100bb08` in `/workspace/tmp/previous-jit-bridge-smoke-mmu-flags-default-20260512-214256`
- RAM translation remains gated by `PREVIOUS_UAE2026_JIT_RAM=1`
- RAM-mode smoke now records true RAM dispatch (`jit_ram_dispatch_seen=1`) instead of only ROM execution
- the current RAM-mode blocker is not default JIT, opcode harness parity, or early NBIC/MO/SCR2/RTC bring-up; it is nested 68040 MMU exception delivery around an RTE from the MMU handler back to low user virtual PCs
- committed RAM-mode fixes restore both MMU fixup slots, add conservative auto-update EA rollback, force auto-update and return-family opcodes through exact fallback barriers, add code-space MMU translation for branch/dispatch PCs, canonicalize post-exception PC/SR, save the active supervisor exception SP/ISP across bridge catches, and remove the RAM direct `MOVES.* reg,(An)+` shortcut
- follow-up audit fixes split RAM/MMU data and code translation paths: the private bank `xlateaddr` again uses data-space `Uae2026JitMmuXlateData()`, while `Uae2026JitMmuXlateCodeHost()` is used for dispatch PC materialization and ARM64 `get_n_addr_jmp()` branch/return targets
- the bridge MMU restart handoff now uses `m68k_setpc(restart_pc)` instead of `m68k_setpci(restart_pc)` so `pc_p`/`pc_oldp` are rebuilt consistently before resuming translated execution
- noisy 68040 MMU exception-frame logging is gated behind `B2_JIT_TRACE_MMU_FRAME=1` rather than being emitted by default
- follow-up build-hygiene audit fixes keep vendored compiler prefs renamed away from Previous-native `currprefs`/`changed_prefs`, guard duplicate `USE_JIT`, cast AArch64 instruction words before emission, and add a defensive vreg bounds check around ARM64 `set_status()`
- the latest clean build had no compiler/linker warnings in `/workspace/tmp/build-audit-prefs.log`
- preserving `regs.isp` after an `RTE` has already switched to user mode avoids overwriting the interpreter's post-pop supervisor stack with the cached pre-RTE exception-frame SP; this removes the immediate panic in the latest RAM smokes but does not reach desktop
- native RAM/MMU helper calls now publish the current JIT flag snapshot into `Uae2026JitLastFlags` after flushing live state and before calling bank/code-translation helpers that can raise a 68040 MMU exception; otherwise the bridge restart path can restore stale flags after a helper fault
- follow-up RAM/MMU helper hardening now uses a full `flush(1)` before RAM-dispatch bank helpers and code-host translation helpers can raise a 68040 MMU exception, so dirty/constant live D/A/PC/SR state is materialized before the bridge delivers `Exception(2)`; opcode harness and default/ROM desktop smoke still pass, but RAM mode still times out before desktop in the RTE/page-fault loop (`/workspace/tmp/previous-jit-bridge-smoke-mmu-fullflush-ram-20260513-141618`, `jit_dispatch_lines=364826`, `jit_last_pc=040a5348`)
- follow-up RTE restart hardening publishes pre-interpreter-fallback SR/A7/opcode state for both trace-time and compiled fallback paths, and the bridge restores that pre-RTE supervisor state if a RAM/MMU fault escapes after `RTE` has only partially loaded the frame SR; this removes the repeated `pc=04002186 sr=0010` loop while keeping opcode harness/default desktop validation green
- current RAM-mode frontier after the RTE-state fix first moved to a repeated `MOVES.L D0,(A0)` DFC write fault at `040014b2` (`op=0e90 ext=0800`, `addr=03f7fffc`, `dfc=1`) with handler return through `04001f52`; RAM mode still failed before desktop in `/workspace/tmp/previous-jit-bridge-smoke-rte-state-ram-20260513-232254` (`jit_dispatch_lines=329616`, `jit_last_pc=0405f748`)
- follow-up zero-PC recovery hardening switches to supervisor state before translating the vector PC with `jit_set_guest_pc_fast()`; this avoids faulting on the handler address while still in user mode. Opcode harness and default/ROM desktop smoke remain green (`/workspace/tmp/previous-opcode-harness-zero-pc-20260514-010248`, `/workspace/tmp/previous-jit-bridge-smoke-zero-pc-default-20260514-012555`), and RAM mode now reaches a repeated user-mode `00003352` `CMPI.B #$40,(8,A4)` fault at `addr=00000008` (`/workspace/tmp/previous-jit-bridge-smoke-zero-pc-ram-20260514-010835`, `jit_dispatch_lines=385584`, `jit_last_pc=04021968`)
- audit cleanup after the `00003352` discriminator removed the unused RAM direct `MOVES.* (An)+` helper and made the RAM direct `MOVEM.L reglist,-(An)` helper opt-in via `B2_JIT_RAM_DIRECT_MOVEM_PREDEC=1`; by default MOVEM predecrement now uses the generated/interpreter 040 path so MMU write faults keep normal restart/fixup bookkeeping. Validation: opcode harness `/workspace/tmp/previous-opcode-harness-dead-moves-cleanup-20260514-083322` (`pass=62 fail=0`) and default/ROM smoke `/workspace/tmp/previous-jit-bridge-smoke-audit-cleanup-default-20260514-083927` (`desktop_reached=1 stable_reached=1`). RAM mode remains blocked at the repeated `00003352` fault (`/workspace/tmp/previous-jit-bridge-smoke-movem-shortcut-ram-20260514-081648`).
- low-virtual code-fetch discriminator: `B2_JIT_LOW_VIRTUAL_SINGLESTEP=1` now single-steps only the confirmed ROM probe window (`0x00003200..0x00003400`) by default through a real 040 MMU opcode fetch plus `cpufunctbl`, with optional `B2_JIT_LOW_VIRTUAL_SINGLESTEP_START/END` for range bisection. This eliminates the repeated `00003352`/`addr=00000008` loop and reaches `root on sd@` in RAM mode, but still no desktop within 600/1200s; widening the range toward `0x00014000` exposes a later post-root stall at `0401da6c`, mapped to an FPU/math compare branch in kernel text. The helper publishes `instruction_pc`/`fault_pc` before faultable opcode fetch and uses the normal `cpu_check_ticks()` path after interpreter execution.
- diagnostic disassembly of the post-panic frontier showed a hardclock/SCR2 path around `0406bb40..0406bbd8` and a later scheduler loop around `040a5304..040a536a` that saves/checks queue state and branches through `040a5348`; the latest timeout remains in kernel hardclock/soft-interrupt activity rather than the earlier immediate panic
- the latest RAM-mode traces no longer reproduce the original advanced-A1 `00003334` corruption, but still fail before desktop; the MMU-flag-publishing RAM smoke reached true RAM dispatch (`jit_dispatch_lines=204174`, `jit_ram_dispatch_seen=1`, `jit_last_pc=040602fe`) and timed out at/after `root on sd@` without a log/OCR panic in `/workspace/tmp/previous-jit-bridge-smoke-ram-mmu-flags-final-20260512-214851`
- do **not** rewrite `fault_pc`/`instruction_pc` to `mmu_fault_addr` for RTE faults; that diagnostic was tested and rejected because the opcode context and access address must remain distinct
- harness tracking records `jit_dispatch_lines`, `jit_ram_dispatch_seen`, `jit_last_pc`, and `jit_ram_requested`; set `PREVIOUS_UAE2026_JIT_RAM=1` to attempt experimental RAM translation and distinguish ROM-only desktop success from RAM-translated progress
- experimental RAM mode has stricter RAM-dispatch accounting: `jit_ram_dispatch_seen` only counts `0x04000000..0x07ffffff`, not bogus `pc=00000000`
- current RAM-requested runs do enter true RAM dispatch and reach early kernel activity past `root on sd@`, but still fail before Workspace/File Viewer because RTE/page-fault state is not yet fully JIT-safe
- follow-up wiring replaced the no-op MOVEC bridge stubs with control-register state updates (`VBR`, stack pointers, `TC`, `TT*`, `SRP/URP`, `CACR`); default smoke and opcode harness still pass
- NBIC/device access now requests a JIT block exit instead of delegating to the interpreter; current RAM-mode runs no longer use interpreter-resume scaffolding, and the first post-NBIC bad return into `0x0b03f800` was narrowed to ROM `delay()` call/return handling on the VRAM-backed stack
- the legacy Hatari Python UI is now build-gated behind `ENABLE_HATARI_PYTHON_UI=OFF` by default; it is not part of the JIT/headless path
- RAM-mode memory codegen no longer relies on bridge-level `B2_JIT_ALL_SPECIAL_MEM`; it routes RAM-mode writable/unclassified data accesses through the live addrbank helpers while allowing direct reads only from immutable ROM shadows
- follow-up RAM-mode fixes added native callsite helpers for the two ROM delay calls that were corrupting return addresses (`0100969c`, `0100ce26`), a native helper for the ROM VBR global lookup at `0100139a -> 010003c2`, and a MO interrupt-status block-exit barrier; RAM mode now gets past the NBIC/MO reset crash path and reaches the graphical boot window, but the display is still corrupted and no true RAM dispatch is recorded yet
- the ROM SCR2/RTC bit-banged NVRAM read/write helpers at `010077aa` and `010076c6` are now handled natively in RAM mode, with stack arguments read through live memory instead of the JIT shadow; this removes the `Main Memory Configuration Test Failed` path and restores progress to the graphical boot/window stage, but the framebuffer is still corrupted and no true writable-RAM dispatch is recorded yet

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
3. Add a minimal targeted regression for the confirmed RAM/MMU pattern: MMU fault during an auto-update EA followed by MMU-handler `RTE` back to a low user virtual PC.
4. Audit RTE/page-fault state without conflating `fault_pc`/`instruction_pc` and `mmu_fault_addr`; RAM/MMU helper calls now force a full live-state flush before helper-delivered `Exception(2)`, so the remaining likely seam is RTE restart/frame state across nested handler returns to low user virtual PCs.
5. Once RAM mode reaches desktop, capture the final RAM-mode screenshot and update this log with metrics.

## Guardrails

- Keep the headless harness using a **fresh image copy per run**.
- Do not mutate the canonical desktop snapshot during automated bring-up.
- Keep interpreter boot working while the experimental bridge is enabled.
- Log every structural bring-up step in this file as it lands.
