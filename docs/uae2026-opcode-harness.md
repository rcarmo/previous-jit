# UAE2026 opcode harness

This harness runs short M68K opcode vectors inside `Previous` without waiting for a full NeXT boot.
It uses the normal `Previous` binary, injects a tiny test program into the ROM mirror, seeds the CPU
register file, runs one interpreter/JIT pass, and dumps register state as `REGDUMP:`.

## Files

- `tools/uae2026-opcode-harness.sh` — build + run the harness and compare interpreter vs JIT
- `tools/uae2026-opcode-vectors.sh` — curated vectors for currently missing / risky opcode families
- `src/m68000.c` — opcode test-mode setup and `REGDUMP` emission
- `src/cpu/newcpu.c` — one-pass escape hatch for opcode test mode
- `src/cpu/uae2026_jit_bridge.cpp` — respects `PREVIOUS_UAE2026_JIT=0` for interpreter baselines

## Protocol

Environment variables reused from the BasiliskII harness:

- `B2_TEST_HEX` — space-separated 16-bit words to execute
- `B2_TEST_DUMP=1` — print a final `REGDUMP:` line
- `B2_TEST_INIT` — optional initial `D0-D7 A0-A7 [SR]`
- `PREVIOUS_UAE2026_JIT=0|1` — interpreter vs bridge/JIT mode
- `B2_JIT_FORCE_TRANSLATE=1` — force first-block translation in JIT mode

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

Absolute scratch addresses were remapped from BasiliskII-style low RAM to Previous RAM at `0x0400xxxx`.

## Latest run (2026-04-27)

Command:

```bash
./tools/uae2026-opcode-harness.sh
```

Observed metrics:

- `total=58`
- `interp_ok=58`
- `jit_ok=0`
- `pass=0`
- `infra_fail=58`

Interpretation:

- the **opcode harness itself is working**: all 58 vectors reach `REGDUMP` in interpreter mode
- the **bridge still crashes before producing a register dump** on every curated vector
- current failure mode is consistent with the known early JIT crash in `execute_normal()` after `JIT_ENTRY`

## Why this matters

This gives a fast, deterministic inner loop for JIT work:

1. add/repair an opcode path
2. run the small vector set in seconds/minutes instead of full NeXT boots
3. compare interpreter and JIT register dumps directly
4. only then re-run the heavier boot harnesses
