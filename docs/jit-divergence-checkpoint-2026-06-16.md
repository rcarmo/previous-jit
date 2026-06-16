# JIT divergence checkpoint — 2026-06-16

## Proven committed baseline

- `5de1299 jit: route direct MMIO faults through interpreter`
- This generic bridge fallback cleared the direct-MMIO treadmill for NeXT IO ranges including:
  - hardclock `0x021160xx`
  - BMAP/SCC `0x021180xx`
  - floppy/NBIC-related `0x021141xx`
  - Ethernet/SCC-related `0x021060xx`

## Current blocker

Pure JIT reaches a JIT-only path that faults at:

```text
0409b11e: MOVE.L $040b3026,(A0)
A0/A2/D4 = 1071a000
fault addr = 1071a000
```

The bridge delivers the page fault, but the guest page-fault handler then fails:

```text
040a5b74: MOVE.L (0x48,A0),D5
A3 loaded from $040b62c4 is 0
A0 becomes 4e71f4d8
fault addr = 4e71f520
```

So the symptom is a handler crash caused by `$040b62c4` (current-thread/current-activation style kernel global) still being zero when the page fault is delivered.

## Interpreter comparison

Interpreter initializes `$040b62c4` before this situation:

```text
040a2c8e -> 040b62c4 = 10128cec
040a2a96 -> 040b62c4 = 1012987c
040a2a96 -> 040b62c4 = 101292e0
...
```

The interpreter trace also shows early allocator/global writes retaining pending interrupt state:

```text
04381222 -> writes 040b3024..040b3033, spc=00000008
```

JIT reaches the analogous early write with `spc=00000000` and later hits `0409b11e` before any nonzero `$040b62c4` initializer write.

## Rejected / reverted experiments

These were tested and reverted because they did not fix the ordering divergence or caused stalls/crashes:

- Repairing `$040b62c4` from a cached/shadow value.
- Hardcoded seeding of `$040b62c4`.
- Deferring the `0409b11e` fault for a pending interrupt.
- Stronger dispatch tick charging.
- Reducing mid-block tick interval.
- Exact islands around:
  - `043819e0-04381a30`
  - `040a0800-040a0860`
  - `0409b000-0409b220`
  - `04090000-040a2d00`
- Globally preserving/masking `SPCFLAG_INT`; this avoided the old fault path but stalled at the hardware-test banner.

## Current hypothesis

The bug is a first control-flow/timing split before `0409b11e`: pure JIT enters an allocator/page-fault path before the interpreter reaches the scheduler/current-thread initializer at `040a2c8e/040a2a96`.

The next useful work is to compare PC/state traces earlier than the `04381222` allocator/global zeroing point and find the first branch/control-flow difference that causes JIT to enter the `0409b11e` path prematurely.
