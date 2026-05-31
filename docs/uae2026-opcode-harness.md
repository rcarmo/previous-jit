# UAE2026 opcode harness

This harness runs short M68K opcode vectors inside `Previous` without waiting for a full NeXT boot.
It uses the normal `Previous` binary, injects a tiny test program into the ROM mirror or RAM,
seeds the CPU register file, runs one interpreter/JIT pass, and dumps register state as
`REGDUMP:`.  Default-off fault-oracle vectors can instead stop at the first expected access
error and dump `FAULTDUMP:`.

## Files

- `tools/uae2026-opcode-harness.sh` — build + run the harness and compare interpreter vs JIT
- `tools/uae2026-mmu-fast-smoke.sh` — filtered fast smoke for MMU-sensitive vectors before full boot smokes
- `tools/uae2026-opcode-vectors.sh` — curated vectors for currently missing / risky opcode families
- `src/m68000.c` — opcode test-mode setup and `REGDUMP` emission
- `src/cpu/newcpu.c` — one-pass escape hatch for opcode test mode
- `src/cpu/uae2026_jit_bridge.cpp` — respects `PREVIOUS_UAE2026_JIT=0` for interpreter baselines

## Protocol

Environment variables reused from the BasiliskII harness:

- `B2_TEST_HEX` — space-separated 16-bit words to execute
- `B2_TEST_ADDR` — optional injection/execution address; defaults to `0x01001000`, and RAM smoke uses `0x04008000`
- `B2_TEST_DUMP=1` — print a final `REGDUMP:` line
- `B2_TEST_INIT` — optional initial `D0-D7 A0-A7 [SR]`
- `B2_TEST_MEM_LONGS` — optional address/value long pairs to seed test memory
- `B2_TEST_DUMP_MEM_LONGS` — optional long addresses to append as `MEMDUMP:` comparison lines
- `PREVIOUS_UAE2026_JIT=0|1` — interpreter vs bridge/JIT mode
- `B2_JIT_FORCE_TRANSLATE=1` — force first-block translation in JIT mode
- `B2_TEST_EXPECT_EXCEPTION=2` — default-off fault-oracle mode; one expected vector-2 access error prints `FAULTDUMP:` and stops instead of requiring the sentinel `REGDUMP:`
- `B2_TEST_CODE_FAULT_ADDR=<addr>` — opcode-test-only forced instruction-fetch fault at a logical address
- `B2_TEST_DATA_FAULT_ADDR=<addr>` with `B2_TEST_DATA_FAULT_WRITE=0|1` and optional `B2_TEST_DATA_FAULT_SIZE=B|W|L` — opcode-test-only forced data access fault
- `PREVIOUS_OPCODE_INCLUDE_FAULTS=1` — append the default-off focused fault-oracle vectors to `TEST_ORDER`

The harness appends:

- `MOVEA.L #sentinel,A6` — completion sentinel
- `STOP #$2700` — clean stop point

## Current vector set

The curated set focuses on the opcode classes that are still absent from the bridge path or are
known to exercise the brittle parts of the old generated `compemu` pipeline:

- SR ops: `ORI/ANDI/EORI to SR`, `MOVE from/to SR`
- VC/VS condition family: `Scc`, `DBcc`
- word and long divide/multiply family: `DIVS/DIVU/DIVL/MULL`
- memory shift/rotate forms: `ASLW/ASRW/LSRW/ROLW/ROXLW/ROXRW`
- bitfield family: `BFTST/BFEXTU/BFEXTS/BFFFO/BFSET/BFCLR/BFCHG/BFINS`
- packed BCD-ish helpers: `PACK/UNPK`
- privileged helpers: `MOVES`, `MOVEC`
- MMU-sensitive control/stack paths: `MOVEM.L ...,-(An)`, `JSR (An)`, `BSR.W`
- seeded user-mode pointer/stack/call state for the `050069c8` seam (`MOVEA.L (A0),A0`, `MOVEA.L (32,A0),A1`, user stack push, MOVEM frame restore, stack/hash lookup, `JSR (A0)`, and a deterministic combined pointer→hash→call chain)
- byte-store seam shapes from the native RAM/MMU fault path: `MOVE.B D2,(A0)` (`1082`) and `MOVE.B (A2)+,(A0)` (`109a`), including destination memory dumps and postincrement side effects
- default-off forced-fault oracles for the currently unaudited MMU seams: BSR/JSR target fetch after return push, RTS/RTR target fetch after stack pop, RTE return-code fetch after SR/A7 switch, TRAP exception-frame write fault, non-restartable byte stores, MOVES SFC/DFC read/write faults, and MOVEM predecrement write fault

Absolute scratch addresses were remapped from BasiliskII-style low RAM to Previous RAM at `0x0400xxxx`.
Fault-oracle vectors are not part of the default green regression set yet: they are discriminators
for proving current JIT/MMU policy.  Run them explicitly with a narrow filter and inspect the
raw `FAULTDUMP:` tuple.

For comparisons only, the harness normalizes forced-fault `FAULTDUMP:` lines for fields that are
known opcode-test catch-path state rather than part of the 68040 access-error tuple under test:
`SPC` is replaced with a placeholder and the X bit is cleared from `SR`.  The raw
`*.interp.regdump` / `*.jit.regdump` files are kept unchanged; normalized copies are written as
`*.compare` when `B2_TEST_EXPECT_EXCEPTION` is active.  Promote a fault vector only after the raw
semantic tuple has been reviewed and any normalization is documented.

## Latest run (2026-05-26)

Commands:

```bash
./tools/uae2026-mmu-fast-smoke.sh
./tools/uae2026-opcode-harness.sh
```

Observed fast MMU smoke metrics:

- `total=32`
- `interp_ok=32`
- `jit_ok=32`
- `pass=32`
- `fail=0`
- `infra_fail=0`
- `score=100`

Observed full opcode metrics:

- `total=75`
- `interp_ok=75`
- `jit_ok=75`
- `pass=75`
- `fail=0`
- `infra_fail=0`
- `score=100`

Recent artifact examples:

- `/workspace/tmp/previous-mmu-fast-smoke-20260526-132114`
- `/workspace/tmp/previous-opcode-harness-20260526-132114`
Interpretation:

- the opcode harness remains a passing regression gate for the current curated vector set
- `uae2026-mmu-fast-smoke.sh` is the required first gate for RAM/MMU changes before heavier boot smokes; it injects the MMU-sensitive vectors, including relocated seam call-chain vectors, at RAM address `0x04008000` and reports `jit_ram_requested=1` / `rte_handoff_disabled=1`
- RAM-mode boot failures are therefore being debugged as MMU/exception/restart state bugs rather than broad opcode-harness regressions
- new RAM/MMU fixes should keep the fast MMU smoke at `pass=32 fail=0 score=100` and the full opcode harness at `pass=75 fail=0 score=100` before heavier boot smokes are trusted

## Why this matters

This gives a fast, deterministic inner loop for JIT work:

1. add/repair an opcode path
2. run the small vector set in seconds/minutes instead of full NeXT boots
3. compare interpreter and JIT register dumps directly
4. only then re-run the heavier boot harnesses
