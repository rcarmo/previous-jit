# Stable guest timing anchor validation — 2026-08-01

## Scope

This gate validates timing and interrupt semantics at a bounded, guest-driven
coordinate. It does not use the copied disk's eventual desktop outcome as an
oracle and makes no full-boot acceptance claim.

Commit: `5fa984f1c1105418d19fddebdd915a89c38ab28d`

Artifact:
`/workspace/tmp/jit-timing-anchor-final-5fa984f-20260801-011608`

## Method

`tools/jit-timing-anchor.sh` runs interpreter and JIT arms against the same
immutable, read-only source image and terminates both after exactly 256 SCSI
block transactions (`B2_SCSI_TRACE_STOP_AT=256`). The SCSI tuple
`sequence/type/LBA/content-hash` is the coordinate because guest semantics drive
it. Exception serials cease to be a safe cross-engine coordinate once interrupt
arrival differs, and wall time is not a guest coordinate.

The harness:

- refuses dirty source and concurrent `Previous` processes;
- pins the complete emulator to CPU 11;
- owns CPU 11's cpufreq policy at `performance`, fixed `2600198 kHz`;
- restores and verifies the original `schedutil` policy and limits;
- uses the source image read-only, with no per-run disk mutation;
- byte-compares the complete SCSI tuple streams;
- compares the first 400 CycInt handler/requested-delay pairs;
- compares exact per-handler arm/fire counts and delivered exception count;
- reports emulated-cycle and observed-retirement deltas in ppm.

The raw CPU PC and `PendingInterrupt.time` are retained but not equality gates.
The stop executes from inside asynchronous SCSI service; interpreter and JIT may
be at different instructions in the same polling/transfer path because JIT
deadlines are drained at safe block boundaries.

## Result

Exact equalities:

- 256/256 SCSI transaction tuples, byte-identical;
- first 400 CycInt handler/requested-delay pairs, byte-identical;
- delivered exceptions: 194/194;
- CycInt handler counts:
  - handler 1: 783 armed / 782 fired;
  - handler 4: 100 / 100;
  - handler 5: 43 / 43;
  - handler 15: 2202 / 2201.

Bounded counter offsets at the asynchronous stop:

| Counter | Interpreter | JIT | Absolute relative delta |
|---|---:|---:|---:|
| Emulated cycles | 287,616,170 | 287,618,155 | 6 ppm |
| Observed retirements | 84,592,715 | 84,593,458 | 8 ppm |

JIT retirement split: 84,585,573 native, 7,885 trace, zero `exec_nostats`, zero
compiled fallback.

Raw asynchronous-stop state, retained for audit:

- PC: interpreter `04382e08`, JIT `04382e22`;
- pending-time remainder: interpreter `10161`, JIT `29676`.

The result validates product timing architecture—retired-cycle charging and
safe MMU block-boundary draining—without enabling the diagnostic
`B2_JIT_RETIREMENT_TICK_EVERY` path. Historical evidence shows that retirement
injection changes cadence and can observe stale live A7; it is not a production
oracle.

## Reproduction

```bash
cmake --build build-vnc -j$(nproc)
ANCHOR_IO=256 CYCARM_LIMIT=400 TIMEOUT_SEC=300 BENCH_CPU=11 \
OUTDIR=/workspace/tmp/jit-timing-anchor-$(git rev-parse --short HEAD)-$(date +%Y%m%d-%H%M%S) \
  ./tools/jit-timing-anchor.sh
```
