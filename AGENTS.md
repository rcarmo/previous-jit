# Agent guide — `previous` (rcarmo-jit fork)

This is the `rcarmo-jit/main` fork of [previous](https://github.com/probonopd/previous),
a NeXT-cube emulator.  This fork adds:

* a UAE 2026 dynamic-recompiler (M68K → ARM64) bridge under `src/cpu/uae_cpu_2026/`,
* a libvncserver-based headless VNC server (`src/vnc_server.c`),
* a headless-test harness (`tools/headless-*.sh`, `tools/perf-baseline*`),
* a top-level `Makefile` that wraps every workflow you'd otherwise run by hand.

If you're starting a new session here, read this file end-to-end before
touching the JIT or the VNC server.  The JIT bridge is full of subtle
exception-frame conventions that are easy to break.

## TL;DR — workflows

* **Boot the emulator headless on `:5901` with the canonical JIT+handoff recipe:**
  `make headless-jit`
* **Stop a detached headless run:**
  `make headless-stop`
* **Rebuild after a code change:**
  `make build`
* **Measure JIT vs interpreter vs pre-tuning baseline (4 configs, ~25 min):**
  `make perf-baseline`
* **Quick 2-config sanity check (~8 min):**
  `make perf-baseline-quick`
* **Probe the live VNC server's encoding negotiation:**
  `make vnc-probe`
* **Probe VNC bytes/sec under scripted cursor motion (12 s window):**
  `make vnc-probe-motion`
* **Measure raw JIT vs interpreter throughput on a tight M68K loop:**
  `./tools/jit-microbench.sh` (or with `ITERATIONS=N`).

Every target accepts overrides on the command line, e.g. `make headless-jit
VNC_PORT=5910 DISPLAY_NAME=:210 RUNDIR=/workspace/tmp/previous-alt`.
`make help` lists them all.

## Repository conventions

* All test runs **must** go through `make` targets, not ad-hoc shell.  Add a
  new target rather than copy-pasting commands.  Detached processes set up
  through these targets use `setsid + nohup` so they survive tool-call
  boundaries on the headless host.
* `src/cpu/newcpu.c` is the **compiled** interpreter (Hatari's vendored copy);
  `src/cpu/uae_cpu_2026/newcpu.cpp` is the **UAE-2026 vendored copy** used by
  the JIT compiler unit only.  When you change interpreter behaviour, edit
  `src/cpu/newcpu.c` and confirm the object file (`build-vnc/.../newcpu.c.o`)
  picks up your change — not `newcpu.cpp.o`, which is not built.
* Code style: tabs for newcpu.c (Hatari convention), 4-space for our own .cpp.
* Every commit must pass `git diff --check` and `make build`.
* Don't claim a fix works without a passing test.  Revert speculative changes
  rather than leaving them dangling in the tree.

## JIT bridge cheat-sheet

The JIT bridge lives in `src/cpu/uae2026_jit_bridge.cpp`.  The interesting
public surface:

| Symbol                              | Purpose                                          |
|------------------------------------|--------------------------------------------------|
| `Uae2026JitBridgeIsActive`         | True iff JIT is selected for the next dispatch. |
| `Uae2026JitBridgeCompileExecute`   | The JIT dispatch entry point.                   |
| `Uae2026JitBridgeResumeFromHandoff`| Re-enables JIT after a one-shot interp handoff. |
| `Uae2026JitInterpResumeCountdown`  | Set on handoff; decremented by interpreter loop. |

### Environment knobs

These are read in `snapshot_bridge_prefs` (search for `env_truthy` /
`env_int` in `uae2026_jit_bridge.cpp`).  All disabled by default unless noted.

| Variable                                       | Default | Meaning |
|------------------------------------------------|---------|---------|
| `PREVIOUS_UAE2026_JIT`                         | off     | Master JIT switch. |
| `PREVIOUS_UAE2026_JIT_RAM`                     | off     | Enable JIT for RAM-resident code (kernel + user). Required for the canonical recipe. |
| `PREVIOUS_UAE2026_JIT_BOOTSTRAP`               | follows `JIT` | Init the JIT cache and prefs shim early. |
| `PREVIOUS_UAE2026_JIT_CACHE_KB`                | 8192    | JIT translation cache size in KB. |
| `PREVIOUS_UAE2026_JIT_FPU`                     | off     | Compile FPU ops in JIT (vs interpreter fall-back). |
| `PREVIOUS_UAE2026_JIT_LAZY_FLUSH`              | on      | Lazy cache invalidation. |
| `PREVIOUS_UAE2026_JIT_CONST_JUMP`              | on      | Compile static branches as constant jumps. |
| `B2_JIT_RTE_FAULT_HANDOFF`                     | off     | On first RTE-fault, disable JIT and run the rest in interpreter. **Canonical boot recipe.** |
| `B2_JIT_RTE_FAULT_HANDOFF_DISABLE`             | off     | Force-disable the above even if set in another env. |
| `B2_JIT_RTE_FAULT_HANDOFF_SKIP_N`              | 0       | Defer handoff to the Nth RTE fault. Bisection helper. Per audit: SKIP_N=6 boots, SKIP_N=7 stalls. |
| `B2_JIT_RTE_FAULT_HANDOFF_RESUME_INSNS`        | 0       | **One-shot handoff** — disable JIT for only N interpreter instructions, then call `Uae2026JitBridgeResumeFromHandoff` and let JIT take over again. 0 keeps the historical permanent handoff. |
| `B2_JIT_TRACE_*`                               | off     | Various diagnostic tracers.  Search `uae2026_jit_bridge.cpp` for the full list. |

### The canonical headless boot recipe

```bash
PREVIOUS_UAE2026_JIT=1
PREVIOUS_UAE2026_JIT_RAM=1
B2_JIT_RTE_FAULT_HANDOFF=1
PREVIOUS_RTC_UNIX_TIME=0x2ec46472
```

This is encoded in `tools/headless-launch.sh`'s `jit` mode and surfaced as
`make headless-jit`.  With these settings NeXTSTEP 3.3 reaches the File
Viewer in ~3-4 minutes on the Orange Pi 6 host.

### The cmd-185 stall

Pure JIT without handoff stalls during early SCSI boot at the
`MO_IntStatus` polling loop (`PC=0x0400fd04`, polling `0x02112004`).
Bisection (`B2_JIT_RTE_FAULT_HANDOFF_SKIP_N`) showed:

* `SKIP_N=6` — boots: JIT handles the first 6 RTE faults natively, then
  hands off forever.
* `SKIP_N=7` — stalls: JIT's handling of fault #7 puts kernel state into
  a divergent path the polling loop cannot escape.

Full audit at `/workspace/notes/macemu-jit-cmd185-audit.md` (838 lines).
The handoff workaround is the canonical recipe; native no-handoff
`JIT_RAM=1` was declared exhausted as a productive target.

### Compiler-correctness ratchet

After the `cpu_model=68000` silent-noop bug was fixed (commit `2779c47`),
the JIT actually emits native code and surfaces opcode-handler bugs the
old silent path masked.  The fixes so far:

* `1d57829` — **VREGS 22 → 32**.  `compemu_arm.h` capped virtual
  registers at 22 (D0..D7 + A0..A7 + PC_P + FLAGX + FLAGTMP + S1..S3)
  but the legacy `compemu.cpp` opcode handlers `scratchie++` from S1
  upwards, and 391 of them allocate ≥4 scratch slots (max 6 in
  `op_83b_0_comp_ff`).  Slots past S3 overflowed `live.state[]` and
  tripped `set_status invalid vreg N` in compile_block; in practice
  the first hit was `op_51c8_0_comp_ff` (DBcc) at PC=0x01002c70 in
  Previous's ROM init.  Same commit adds a backtrace + m68k-context
  dump in `set_status()` before `jit_abort()` so future out-of-range
  vregs print the offending opcode handler.

Open items (real JIT can compile + run code, but boot still diverges):

* JIT-emitted code for the CRC/checksum loop at PC=0x01002c76–0x01002c7e
  (`lsl.l #1,d0` / `roxr.b #1,d4` / `roxr.l #1,d1` / `eor.b d1,d4`)
  appears to produce divergent register state and ultimately a bus
  error pushing an exception frame to `sp=0` (`[NextBus] Bus error
  lput at FFFFFFFC` + fatal double MMU exception).  Pinning down
  which opcode codegen is wrong needs an opcode-by-opcode oracle
  comparison against the interpreter.

### Why the JIT looks slow today

**Fixed in commit `2779c47`.**  Before that fix, the JIT was secretly a
no-op for every workload: the global `PrefsFindInt32` stub returned 0
for the "cpu" key, which made `currprefs.cpu_model = 68000` inside the
UAE-2026 compiler, which made `compile_block`'s
`cpu_model >= 68020` gate fail on every call.  Native code was never
emitted; every dispatch dropped back into `execute_normal` and
re-interpreted the block.

After the fix, on `tools/jit-microbench.sh` (a 40 M m68k-insn tight loop):

| Configuration               | Throughput            |
|----------------------------|----------------------:|
| Interpreter                | 11.63 M m68k-insn/s   |
| JIT (pre-fix, silent no-op)|  4.13 M m68k-insn/s   |
| JIT (post-fix)             | 99.26 M m68k-insn/s   |

At 200 M m68k insns the JIT settles at **473.93 M m68k-insn/s** (38.6×
faster than the interpreter); the difference is amortisation of the
one-time bridge/compile cost.

**Caveat**: actually-running JIT now exposes latent UAE 2026 compiler
bugs that were previously masked.  In particular, Previous's ROM init
blocks trip a `jit_abort("set_status invalid vreg 22")` inside the
compiler.  Until that's debugged, the `make headless-jit` recipe keeps
`PREVIOUS_UAE2026_JIT=0` so the canonical NeXTSTEP boot stays on the
interpreter — same behaviour the recipe had in practice before the fix,
just now honest about it.

`B2_JIT_RTE_FAULT_HANDOFF_RESUME_INSNS=N` (commit `51a7f8a`) is the
in-progress per-event handoff that lets the JIT survive past a single
RTE fault.  Combined with the cpu_model fix, this is the path to a JIT-
driven boot once the remaining compiler bugs are cleared.

## VNC server cheat-sheet

* All VNC code is in `src/vnc_server.c`.  It runs in its own SDL thread.
* Pixel format: 32 bpp RGBA little-endian (RGBA bytes in memory).
* Keyboard input uses a magic-tag contract: VNC events stamp `SDL_Keysym.unused
  = 0x564E4350 ('VNCP')` so `ShortCut_CheckKeys` in `src/keymap.c` knows to
  skip host-shortcut handling for VNC-origin events.
* The dispatch loop pulls frame snapshots from `VNCServerUpdateRGBA`,
  walks `VNC_DIRTY_TILE`-sized tiles to find the bounding box of changed
  pixels, and calls `rfbMarkRectAsModified` only on the dirty rect.
* Tight encoding quality defaults (`tightQualityLevel=9`,
  `tightCompressLevel=4`, `turboQualityLevel=95`, `turboSubsampLevel=0`)
  are set per-client in `vnc_new_client_hook`.

### Probing a live server

`make vnc-probe` runs a Python RFB client that prints the server's pixel
format and the first FramebufferUpdate.  `make vnc-probe-motion` adds a
12-second cursor sweep and reports bytes/sec on the wire.

### Headline tuning numbers (commit `ffea9a6` vs `9551f97`)

| Metric                       | `9551f97` (baseline) | `ffea9a6` (current) | Ratio |
|------------------------------|---------------------:|--------------------:|------:|
| Idle bandwidth               | 109 MB/s             | 0 B/s               | ∞     |
| Idle update rate             | 29 upd/s             | 0 upd/s             | ∞     |
| Cursor-motion bandwidth      | 100 MB/s             | 267 KB/s            | 376×  |
| Cursor-motion update rate    | 27 upd/s             | 7 upd/s             | 3.9×  |

## Build expectations

The compiled emulator binary is `build-vnc/src/Previous` (Release).  The
build object that matters for interpreter changes is
`build-vnc/src/cpu/CMakeFiles/UaeCpu.dir/newcpu.c.o`.  The UAE-2026
vendored `newcpu.cpp` under `uae_cpu_2026/` is **not** compiled into the
final binary; it only seeds the JIT compiler unit.  Don't waste time
editing it for runtime behaviour changes.

`make build` will configure the cmake tree on first run, then incremental
builds afterwards.  Warnings from `cpu/zip.c` and `cpu/newcpu.c` (about
`strncpy` and `sprintf` bounds) are pre-existing and not from our code.

## Runtime layout

* Logs: `$(RUNDIR)/previous.log`, `$(RUNDIR)/xvfb.log` (default
  `RUNDIR=/workspace/tmp/previous-interactive`).
* Headless display: `Xvfb :198` on the canonical run.
* VNC: `192.168.1.245:5901` for the tablet target on the Orange Pi host.
* Disk image: `nextstep33-system-en-run.img` (sparse copy of the
  `nextstep33-system-en-backup-*` master).  The "warm" copy retains
  filesystem state between boots and is what the perf harness uses for
  consistency.

## When something breaks

1. `make headless-stop`.
2. Look at the **last** "UAE2026 bridge:" line in `previous.log`.
   * `JIT block exit source=...` repeating with the same address is the
     classic cmd-185 stall pattern (the kernel is polling a hardware
     register that's never going to fire).
   * `RTE fault handoff to interpreter count=N` is the handoff firing.
     `count=1` is normal with `B2_JIT_RTE_FAULT_HANDOFF=1`.
3. If the JIT compiled the wrong thing for a specific opcode, run
   `tools/uae2026-opcode-harness.sh PREVIOUS_OPCODE_FILTER='<regex>'`
   to reproduce in isolation.  Cross-check against the interpreter
   golden in the same harness.
4. Don't patch ROM as a fix unless absolutely all interpreter-vs-JIT
   debugging has been exhausted; the four extant ROM patches are
   documented in the repo `README.md` and only legitimate because they
   compensate for missing hardware emulation, not for JIT bugs.

## See also

* `/workspace/notes/macemu-jit-cmd185-audit.md` — the cmd-185 audit
  (838 lines, ground truth for everything we know about why pure JIT
  stalls).
* `/workspace/notes/macemu-jit-render-investigation.md` — earlier render
  pipeline investigation.
* `/workspace/notes/previous-jit.md` — assorted JIT notes.
* Upstream README at `README.md`.
