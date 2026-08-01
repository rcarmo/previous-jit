# Bounded matched interpreter/JIT benchmark — 2026-08-01

## Scope

The copied NeXTSTEP system-image fixture currently panics with the same IPC
`strange rights` failure in both clean `75df2be` controls and the SR tree. It is
not a valid boot-performance oracle. This report therefore measures only a
bounded, deterministic CPU-loop workload. It does **not** claim that the
full-boot Workspace/File Viewer acceptance gate passes.

The superseded schedutil artifact at
`/workspace/tmp/jit-microbench-77375f8-20260801-002502` is retained as
pre-final development evidence only. Its timing wrapped process launch, used no
core/frequency ownership, and its manifest named pre-final `77375f8`.

## Workload and exact denominator

`tools/jit-microbench.sh` uses opcode-test mode:

```asm
        move.l  #N,d0
loop:   subq.l  #1,d0
        bne.s   loop
        movea.l #$51a7e11e,a6
        stop    #$2700             ; appended by opcode-test mode
```

For `N` loop iterations the architectural denominator is exactly `2*N + 3`:
one initial MOVE, `N` SUBQ/BNE pairs, the sentinel MOVEA, and STOP. The fixed
final state is `D0=0`, `A6=51a7e11e`, `SR=2700`, `PC=01001014`.

The observer denominator is reported separately. At `N=100000`:

```text
interpreter: architectural=200003 observed=200002 stop_unobserved=1 reconciled=1
JIT:         architectural=200003 observed=200003 stop_unobserved=0 reconciled=1
             paths=199996/0/7/0 native_ppm=999965 trace_ppm=34
```

This is an intentional callback asymmetry, not skipped execution. The
interpreter loop recognises the synthetic STOP before calling its retirement
observer. The JIT first-pass tracer observes STOP before invoking the same
trailer service. The seven JIT trace retirements are exactly one cold trace of
the five static opcodes plus a second SUBQ/BNE trace; the remaining 199,996
retirements are native. Compiled fallback and `exec_nostats` are both zero.

## Accepted host method

Commit: `31c5d2a3eb3df02fb662dd2bcdf31a06f5b75b8a`

Binary SHA-256:
`8142bc269bc6638f20afdc0f0c3f271516b6d37f300063e90450f5a36cfa21ac`

Artifact:
`/workspace/tmp/jit-microbench-final-31c5d2a-20260801-010713`

The harness now fails closed unless all of these hold:

- source tree is clean;
- no other `Previous` process is active;
- an exclusive benchmark lock is acquired;
- the complete emulator process is pinned with `taskset` to CPU 11;
- CPU 11's cpufreq policy is acquired at governor `performance` and fixed
  `2600198 kHz`, with readback verification;
- an EXIT/INT/TERM trap restores and verifies the original governor/min/max;
- every run reaches the exact architectural sentinel;
- census totals reconcile to the explicit architectural denominator.

CPU 11 shares `policy0` with CPU 0 on this CIX P1 host. The accepted series used
CPU 11 only; the script restored `schedutil`, minimum `799865 kHz`, maximum
`2600198 kHz` after the run.

Timing is split into two explicitly different scopes:

1. **Cold process:** shell monotonic timing from process launch through process
   exit, including emulator/config/device/JIT startup.
2. **Warm in-process:** internal `CLOCK_MONOTONIC` timing around only the CPU
   loop after one unrecorded process-local warm-up. Each recorded sample
   reapplies complete register/SR/PC state while retaining process, device phase
   and JIT cache. The benchmark loop is interrupt-masked and does not mutate
   guest memory. This is deliberately called register-state replay, not a full
   machine snapshot.

Seven alternating cold pairs and nine warm samples per engine were recorded.
Timing trials carry no path/census observer. Separate instrumented census arms
are execution-path evidence, not timing samples.

## Accepted result

Each timing sample executed 10,000,003 guest instructions and charged exactly
20,000,007 emulated cycles.

| Scope / engine | n | Median | Mean | Range |
|---|---:|---:|---:|---:|
| Cold process — interpreter | 7 | 1.095543 s | 1.109478429 s | 1.092023–1.151473 s |
| Cold process — JIT | 7 | 0.620668 s | 0.618665000 s | 0.611817–0.623614 s |
| Warm in-process — interpreter | 9 | 0.799963946 s | 0.799931611 s | 0.795572937–0.804346538 s |
| Warm in-process — JIT | 9 | 0.127305504 s | 0.126424301 s | 0.123272985–0.127348063 s |

Median speedups:

- **cold process:** `1.765103×`;
- **warm in-process CPU loop:** `6.283813×`.

The two figures answer different questions and must not be merged. The cold
figure includes fixed process startup; the warm figure isolates repeated CPU
execution with a warm process and translation cache.

The census arm compiled two fresh optlev-2 blocks, performed zero recompiles,
and ended at the same `400007` emulated cycles as the interpreter for its
100,000-iteration workload.

## Runtime Bcc cycle accuracy

The generated 68040 interpreter has 45 handlers with runtime-variable numeric
cycle returns; all are Bcc forms. Taken edges cost 3 cycles, byte fall-through
costs 2, and word/long fall-through costs 4. Optlev-2 previously charged both
installed exits with the edge observed during one-time tracing.

The JIT now computes each Bcc exit's cumulative retired charge from the runtime
edge. `B2_JIT_REAL_CYCLES=0` retains the old flat-charge inverse control.
`tools/jit-cycle-accuracy.sh` passes exact interpreter/JIT totals:

| Form | Interpreter cycles | JIT cycles | Native / trace / fallback |
|---|---:|---:|---:|
| byte | 400007 | 400007 | 199996 / 7 / 0 |
| word | 400009 | 400009 | 199996 / 7 / 0 |
| long | 400009 | 400009 | 199996 / 7 / 0 |

Artifact: `/workspace/tmp/jit-cycle-accuracy-final-20260801-000705`. The flat
inverse fails the byte case (`400007` versus `400004`), proving sensitivity.

Post-change gates also pass: opcode/fault 155/155
(`/workspace/tmp/previous-opcode-harness-20260801-005523`) and CPU-state 38/38.

## Reproduction

```bash
cmake --build build-vnc -j$(nproc)
ITERATIONS=5000000 CENSUS_ITERATIONS=100000 \
TRIALS=7 WARM_TRIALS=9 BENCH_CPU=11 \
OUTDIR=/workspace/tmp/jit-microbench-final-$(git rev-parse --short HEAD)-$(date +%Y%m%d-%H%M%S) \
  ./tools/jit-microbench.sh
./tools/jit-cycle-accuracy.sh
```

Full-boot performance remains deferred until a fresh immutable image/CoW
fixture completes the required fsck/reboot, WindowServer, loginwindow and
Workspace/File Viewer milestones in both control and candidate configurations.
