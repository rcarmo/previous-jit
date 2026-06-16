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

## Additional allocator/free-list comparison (later 2026-06-16)

Persistent traces were collected in `build-vnc/verify-04381222.log` (JIT) and
`build-vnc/interp-alloc-trace.log` (interpreter).  Normalizing `REQ_WRITE` lines
from the common `04381222` allocator/global initialization point shows:

- JIT and interpreter allocator/free-list writes match in data for the JIT trace
  overlap; the first visible difference in that overlap is only `spc`:
  - JIT: `spc=00000000`
  - interpreter: `spc=00000008`
- The interpreter then continues after the overlap and successfully cycles the
  free list through:
  - `0409b12a -> 040b3026 = 1071a000`
  - `0409b130 -> 040b302e = 00000001`
  - then subsequent pages (`1071a630`, `1071ac60`, ...)
- The JIT faults on the first store that initializes the page-memory backing the
  `1071a000` insertion:

```text
0409b11e: MOVE.L $040b3026,(A0)
A0 = 1071a000
```

This means the allocator path itself is not JIT-only: interpreter also reaches
this allocator/free-list code before current-thread initialization.  The next
root-cause target is why JIT's MMU/page state faults when initializing
`1071a000`, while the interpreter can initialize and insert that page into the
free list and continue.

Rejected after this comparison:

- Broad masked-`SPCFLAG_INT` preservation/serviceability patches: these avoid the
  old fault path but stall at the early hardware-test banner.
- Hardcoded/current-thread repair remains invalid and must not be used.

Next useful direction: compare MMU/page-table state at the interpreter's
successful `1071a000` initialization against JIT's `0409b11e` fault, focusing on
MMU translation/protection rather than scheduler-current-thread symptoms.
