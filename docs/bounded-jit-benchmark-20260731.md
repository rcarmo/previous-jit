# Bounded matched interpreter/JIT benchmark — 2026-07-31

## Why this workload

The copied NeXTSTEP system-image fixture currently panics with the same IPC
`strange rights` failure in both clean `75df2be` controls and the SR tree. It is
therefore not a valid boot-performance oracle. This benchmark replaces only the
performance workload; it does **not** turn the failed copied-disk boot into a
passing full-boot gate.

`tools/jit-microbench.sh` runs a deterministic opcode-test program that both
engines terminate at the same sentinel:

```asm
        move.l  #N,d0
loop:   subq.l  #1,d0
        bne.s   loop
        movea.l #$51a7e11e,a6
        stop    #$2700             ; appended by opcode-test mode
```

The measured workload contains exactly `2*N + 3` guest instructions: the
initial MOVE, two instructions per loop iteration, the sentinel MOVEA, and the
STOP trailer. The final register dump is fail-closed: D0 must be zero, A6 must
contain the sentinel, and PC must be the exact post-STOP address.

## Method

- one immutable binary and SHA for both engines;
- identical read-only source image and generated config;
- five paired trials, alternating engine order to reduce thermal/drift bias;
- no path observer or diagnostic counters in timing trials;
- separate shorter instrumented trials for execution-path and dispatcher
  census, so instrumentation overhead cannot contaminate the speed ratio;
- interpreter arm must report `active=0`;
- JIT arm must report `active=1` and non-zero native retirement;
- every trial must reach the same architectural sentinel state.

The script writes `manifest.env`, `trials.tsv`, `summary.env`, raw logs and
machine-readable final census files under its output directory.

## Result

Artifact: `/workspace/tmp/jit-microbench-77375f8-20260801-002502`

| Property | Value |
|---|---:|
| Source SHA | `77375f89508ea2d6df6ba98f0cbc37fc563e4941` |
| Binary SHA-256 | `e1ebc7b53bfd4c03c8f6e012a9e4f0917dbce5216d2d60d9180d95e2893bde33` |
| Source-image SHA-256 | `1e85ffb4b7c464f9a96dd2ded4bb1168f60c30a3c3cd5cc7389ad89c399c9d18` |
| Host/kernel | Orange Pi 6 Plus; Linux 6.6.89-cix aarch64 |
| Governor | `schedutil` on all 12 CPUs |
| Timed guest instructions/trial | 10,000,003 |
| Paired trials | 5 |
| Interpreter mean | 1.107932 s |
| JIT mean | 0.547903 s |
| Speedup | **2.022132×** |
| Interpreter range | 1.064803–1.148533 s |
| JIT range | 0.531129–0.569195 s |

The separately instrumented JIT trial used 100,000 loop iterations and
reported:

```text
JITHELPERCENSUS tag=final active=1 total=7 inblock=0 trace=7
  nostats=0 nostats_lim=0 obs=199996/0/7/0 disp=33335 comp=2 cyc=400007
JITBENCHDIAG dispatch=33335 exec_normal=3 compile=2 fresh=2 recomp=0
  opt0=0 optgt0=2 nostats=0 cache_miss=0 recompile=0 avg_block=2.500
```

Thus 199,996 observed retirements were native, seven were first-pass trace
retirements, and none used compiled interpreter fallback or `exec_nostats`.
Both engines ended with:

```text
D0=00000000 A6=51a7e11e SR=2700 PC=01001014
```

The four initial MMU-generation bumps are bridge/ATC setup, not workload
retranslation: the run compiled two fresh blocks and performed zero recompiles.

## Runtime Bcc cycle accuracy

The generated 68040 interpreter has 45 handlers with runtime-variable numeric
cycle returns; all are Bcc forms. Taken edges cost 3 cycles, byte fall-through
costs 2, and word/long fall-through costs 4. Optlev-2 previously charged both
installed exits with the edge observed during one-time tracing.

The JIT now computes each Bcc exit's cumulative retired charge from the runtime
edge. `B2_JIT_REAL_CYCLES=0` retains the old flat-charge inverse control.
`tools/jit-cycle-accuracy.sh` forces taken loop edges followed by the opposite
fall-through edge for byte, word and long forms. The post-fix result is:

| Form | Interpreter cycles | JIT cycles | Native / trace / fallback |
|---|---:|---:|---:|
| byte | 400007 | 400007 | 199996 / 7 / 0 |
| word | 400009 | 400009 | 199996 / 7 / 0 |
| long | 400009 | 400009 | 199996 / 7 / 0 |

Artifact: `/workspace/tmp/jit-cycle-accuracy-final-20260801-000705`.
The flat inverse fails the byte case (`400007` versus `400004`), proving the
oracle detects wrong charging. A 100,000-iteration benchmark-loop comparison
also moved from the pre-fix one-cycle error (`400007` interpreter versus
`400008` JIT) to exact `400007/400007`.

Post-change gates: full opcode/fault 155/155
(`/workspace/tmp/previous-cycle-full-opcode-20260801-000752`), RAM/MMU 67/67
(`/workspace/tmp/previous-mmu-fast-smoke-20260801-001836`), targeted branch
semantics 3/3 (`/workspace/tmp/previous-cycle-opcodes-20260801-000723`), and
CPU-state 38/38.

## Reproduction

```bash
cmake --build build-vnc -j$(nproc)
ITERATIONS=5000000 CENSUS_ITERATIONS=100000 TRIALS=5 \
  ./tools/jit-microbench.sh
./tools/jit-cycle-accuracy.sh
```

Use `OUTDIR=...` to choose a stable artifact path. Timing claims should not be
made from the census runs, and full-boot acceptance remains blocked until a
fresh immutable image/CoW fixture completes the required Workspace/File Viewer
milestones in both control and candidate configurations.
