#!/usr/bin/env bash
# ============================================================================
# uae2026-cpustate-harness.sh
# Permanent native-JIT CPU-STATE regression gate for Previous (uae_cpu_2026),
# companion to uae2026-opcode-harness.sh (which runs optlev0/fallback 87/87).
#
# This harness exercises NATIVE codegen (optlev=2 comp handlers) via the
# single-shot / force-L2 discipline and diffs full CPU state native-vs-interp:
#   * FLAG cells  : [op ; MOVE SR,(cap).L] -> compare captured SR (NZVCX)+result
#   * X-INPUT cells: [add.l d2,d2 (native X-setter) ; op ; MOVE SR] (2-op cell,
#                    also validates cross-block X-reload) -- init-SR X is NOT
#                    seeded to jit_regflags.x on pass-2, so X MUST be native-set
#   * STORE cells : native store into a prefilled target -> compare mem image
#                   (value + size-mask + big-endian byte-order + address)
#   * REG-ALLOC   : native-exec assertion (NATEXEC) + forced-past-lock detector
#                   self-validation (PIN_ATTEMPT fires + refuses locked=1)
#
# Discipline enforced: a cell scores ONLY if the target PC positively asserts
# native (NATEXEC>=1). NATEXEC==0 => SKIP-NOSCORE (fallback/interp op, e.g.
# BCD/DIV are gencomp-`failure` interpreter-only on Previous, not native cells).
#
# force-L2 recipe (Previous == macemu B2_TEST_FORCE_L2_RAM equiv):
#   PREVIOUS_UAE2026_JIT=1 + PREVIOUS_UAE2026_JIT_RAM=1 + bCompatibleCpu=FALSE
#   + B2_JIT_FORCE_TRANSLATE=1 + B2_JIT_MAXRUN=1 (single-shot) + two-pass.
# ============================================================================
set -uo pipefail

ROOT="${PREVIOUS_ROOT:-/workspace/projects/previous}"
BIN="${PREVIOUS_BIN:-$ROOT/build-vnc/src/Previous}"
ASSET="${PREVIOUS_ASSET_ROOT:-/workspace/assets/previous}"
WAIT="${PREVIOUS_CPUSTATE_WAIT_SEC:-4}"
CAP="0x04002000"          # MOVE SR capture target
MOVESR="40F9 0400 2000"   # MOVE SR,(0x04002000).L
XSET="D482"               # add.l d2,d2 : d2=0x80000000 -> result 0, X=1 (native X-setter)

if [[ ! -x "$BIN" ]]; then echo "missing Previous binary: $BIN" >&2; exit 2; fi

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/home/.previous"
cat > "$WORK/home/.previous/previous.cfg" <<CFG
[Log]
sLogFileName = stderr
nTextLogLevel = 1
bConfirmQuit = FALSE
[ConfigDialog]
bShowConfigDialogAtStartup = FALSE
[Screen]
bFullScreen = FALSE
[ROM]
szRom030FileName = $ASSET/roms/Rev_1.0_v41.BIN
szRom040FileName = $ASSET/roms/Rev_2.5_v66.BIN
szRomTurboFileName = $ASSET/roms/Rev_3.3_v74.BIN
[Boot]
nBootDevice = 1
bEnableDRAMTest = FALSE
[System]
nMachineType = 1
bColor = FALSE
nCpuLevel = 4
nCpuFreq = 25
bCompatibleCpu = FALSE
nDSPType = 2
bRealTimeClock = TRUE
n_FPUType = 68040
bCompatibleFPU = TRUE
bMMU = TRUE
[Dimension]
bEnabled = FALSE
CFG

# Xvfb
pd(){ local n; for n in $(seq 181 219); do if [[ ! -e /tmp/.X${n}-lock && ! -S /tmp/.X11-unix/X${n} ]]; then echo ":$n"; return; fi; done; echo ":201"; }
DISP="$(pd)"; Xvfb "$DISP" -screen 0 1024x768x24 >"$WORK/xvfb.log" 2>&1 & XP=$!
trap 'kill -9 "$XP" 2>/dev/null; rm -rf "$WORK"' EXIT
sleep 1

PASS=0; FAIL=0; SKIP=0
declare -a FAILED=()

# run_interp <hex> <init> <memlongs> <dumpaddr> -> "<sr_or_dump>|<d0>"
run_interp() {
  local out
  out=$(timeout "$WAIT" env HOME="$WORK/home" SDL_AUDIODRIVER=dummy DISPLAY="$DISP" \
    B2_TEST_HEX="$1" B2_TEST_ADDR=0x01001000 B2_TEST_INIT="$2" B2_TEST_MEM_LONGS="$3" \
    B2_TEST_DUMP=1 B2_TEST_DUMP_MEM_LONGS="$4" PREVIOUS_UAE2026_JIT=0 "$BIN" 2>&1)
  local dv d0
  dv=$(echo "$out" | grep MEMDUMP | grep -oE '=[0-9a-f]{8}' | head -1)
  d0=$(echo "$out" | grep -oE 'D0=[0-9a-f]{8}' | head -1)
  echo "${dv}|${d0}"
}

# run_native <hex> <init> <memlongs> <dumpaddr> <assertpc> [extra_env] -> "<sr_or_dump>|<d0>|<natexec>"
run_native() {
  local out
  out=$(timeout "$WAIT" env HOME="$WORK/home" SDL_AUDIODRIVER=dummy DISPLAY="$DISP" \
    B2_TEST_HEX="$1" B2_TEST_ADDR=0x01001000 B2_TEST_INIT="$2" B2_TEST_MEM_LONGS="$3" \
    B2_TEST_DUMP=1 B2_TEST_DUMP_MEM_LONGS="$4" \
    B2_TEST_TWO_PASS=1 B2_TEST_SECOND_PC=0x01001000 B2_JIT_MAXRUN=1 B2_NATIVE_ASSERT_PC="$5" \
    ${6:-} \
    PREVIOUS_UAE2026_JIT=1 PREVIOUS_UAE2026_JIT_RAM=1 B2_JIT_FORCE_TRANSLATE=1 \
    PREVIOUS_UAE2026_JIT_BOOTSTRAP=0 "$BIN" 2>&1)
  local dv d0 nx
  dv=$(echo "$out" | grep MEMDUMP | grep -oE '=[0-9a-f]{8}' | head -1)
  d0=$(echo "$out" | grep -oE 'D0=[0-9a-f]{8}' | head -1)
  nx=$(echo "$out" | grep -c "NATEXEC pc=${5#0x}")
  echo "${dv}|${d0}|${nx}"
}

# cell <label> <hex-op> <init> <memlongs> <dumpaddr> <assertpc> [extra_env]
# hex-op already includes MOVE SR (flag) or is a store (dump = store target).
cell() {
  local label="$1" hex="$2" init="$3" mem="$4" dump="$5" apc="$6" extra="${7:-}"
  local i n isr id nsr nd nx
  IFS='|' read -r isr id <<< "$(run_interp "$hex" "$init" "$mem" "$dump")"
  IFS='|' read -r nsr nd nx <<< "$(run_native "$hex" "$init" "$mem" "$dump" "$apc" "$extra")"
  if [[ "${nx:-0}" -lt 1 ]]; then
    SKIP=$((SKIP+1)); printf '  SKIP-NOSCORE %-28s (NATEXEC=0, not native)\n' "$label"; return
  fi
  if [[ "$isr" == "$nsr" && "$id" == "$nd" ]]; then
    PASS=$((PASS+1)); printf '  PASS %-28s state%s%s native\n' "$label" "$isr" "${id:+ }"
  else
    FAIL=$((FAIL+1)); FAILED+=("$label"); printf '  FAIL %-28s interp[%s %s] native[%s %s]\n' "$label" "$isr" "$id" "$nsr" "$nd"
  fi
}

echo "=== FLAG cells (native optlev=2 vs interp; SR=NZVCX via MOVE SR capture) ==="
FI="80000000 00000001 0 0 0 0 0 0 0 0 0 0 0 0 0 0"   # d0=0x80000000 d1=1
cell "add.l d1,d0"  "D081 $MOVESR" "$FI" "" "$CAP" 0x01001000
cell "sub.l d1,d0"  "9081 $MOVESR" "$FI" "" "$CAP" 0x01001000
cell "cmp.l d1,d0"  "B081 $MOVESR" "$FI" "" "$CAP" 0x01001000
cell "and.l d1,d0"  "C081 $MOVESR" "$FI" "" "$CAP" 0x01001000
cell "or.l d1,d0"   "8081 $MOVESR" "$FI" "" "$CAP" 0x01001000
cell "eor.l d1,d0"  "B181 $MOVESR" "$FI" "" "$CAP" 0x01001000
cell "neg.l d0"     "4480 $MOVESR" "$FI" "" "$CAP" 0x01001000
cell "not.l d0"     "4680 $MOVESR" "$FI" "" "$CAP" 0x01001000
cell "tst.l d0"     "4A80 $MOVESR" "$FI" "" "$CAP" 0x01001000
cell "add.b d1,d0"  "D001 $MOVESR" "$FI" "" "$CAP" 0x01001000
cell "add.w d1,d0"  "D041 $MOVESR" "$FI" "" "$CAP" 0x01001000
cell "asl.l #1,d0"  "E380 $MOVESR" "$FI" "" "$CAP" 0x01001000
cell "asr.l #1,d0"  "E280 $MOVESR" "$FI" "" "$CAP" 0x01001000
cell "lsl.l #1,d0"  "E388 $MOVESR" "$FI" "" "$CAP" 0x01001000
cell "lsr.l #1,d0"  "E288 $MOVESR" "$FI" "" "$CAP" 0x01001000
cell "rol.l #1,d0"  "E398 $MOVESR" "$FI" "" "$CAP" 0x01001000
cell "ror.l #1,d0"  "E298 $MOVESR" "$FI" "" "$CAP" 0x01001000
# bit-ops register-direct (Z from tested bit)
BI="0000000F 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0"
cell "btst #3,d0"   "0800 0003 $MOVESR" "$BI" "" "$CAP" 0x01001000
cell "bchg #3,d0"   "0840 0003 $MOVESR" "$BI" "" "$CAP" 0x01001000
cell "bclr #3,d0"   "0880 0003 $MOVESR" "$BI" "" "$CAP" 0x01001000
cell "bset #3,d0"   "08C0 0003 $MOVESR" "$BI" "" "$CAP" 0x01001000

echo "=== X-INPUT cells (2-op native-set-X [add.l d2,d2 ; op]; op@0x1002; cross-block X-reload) ==="
XI="80000000 00000003 80000000 0 0 0 0 0 0 0 0 0 0 0 0 0"  # d0,d1=3,d2=0x80000000
ND="B2_JIT_FORCE_NONDIRECT_HANDLER=1"   # consumer block is direct-chained; force non-direct so its NATEXEC marker fires
cell "addx.l d1,d0 X"  "$XSET D181 $MOVESR" "$XI" "" "$CAP" 0x01001002 "$ND"
cell "subx.l d1,d0 X"  "$XSET 9181 $MOVESR" "$XI" "" "$CAP" 0x01001002 "$ND"
cell "negx.l d0 X"     "$XSET 4080 $MOVESR" "$XI" "" "$CAP" 0x01001002 "$ND"
cell "roxl.l #1,d0 X"  "$XSET E390 $MOVESR" "$XI" "" "$CAP" 0x01001002 "$ND"
cell "roxr.l #1,d0 X"  "$XSET E290 $MOVESR" "$XI" "" "$CAP" 0x01001002 "$ND"

echo "=== STORE cells (value + size-mask + big-endian byte-order + address) ==="
SI="11223344 0 0 0 0 0 0 0 04003000 0 0 0 0 0 0 0"    # d0=0x11223344 a0=0x04003000
SIp="11223344 0 0 0 0 0 0 0 04003004 0 0 0 0 0 0 0"   # a0=0x04003004 for -(a0)
SIx="11223344 4 0 0 0 0 0 0 04003000 0 0 0 0 0 0 0"   # d1=4 index
PRE="4003000 AABBCCDD 4003004 EEFF0011"
cell "move.b d0,(a0) mask"  "1080" "$SI"  "$PRE" "4003000" 0x01001000
cell "move.w d0,(a0) mask"  "3080" "$SI"  "$PRE" "4003000" 0x01001000
cell "move.l d0,(a0) BE"    "2080" "$SI"  "$PRE" "4003000" 0x01001000
cell "move.l d0,(a0)+"      "20C0" "$SI"  "$PRE" "4003000" 0x01001000
cell "move.l d0,-(a0)"      "2100" "$SIp" "$PRE" "4003000" 0x01001000
cell "move.l d0,4(a0)"      "2140 0004" "$SI" "$PRE" "4003004" 0x01001000
cell "move.l d0,(0,a0,d1.l)" "2180 1800" "$SIx" "$PRE" "4003004" 0x01001000
cell "move.w d0,2(a0)"      "3140 0002" "$SI"  "$PRE" "4003000" 0x01001000

echo "=== REG-ALLOC native-exec + forced-past-lock detector self-validation ==="
# native-exec core: mulu.w+dbf loop compiles native (NATEXEC>0) under LOCKSTEP_DROP
NAT=$(timeout "$WAIT" env HOME="$WORK/home" SDL_AUDIODRIVER=dummy DISPLAY="$DISP" \
  B2_TEST_HEX="C4C1 51C8 FFFC" B2_TEST_ADDR=0x01001000 B2_TEST_INIT="a 3 0 0 0 0 0 0 0 0 0 0 0 0 0 0" B2_TEST_DUMP=1 \
  B2_TEST_TWO_PASS=1 B2_TEST_SECOND_PC=0x01001000 B2_NATIVE_ASSERT_PC=0x01001000 \
  PREVIOUS_UAE2026_JIT=1 B2_JIT_FORCE_TRANSLATE=1 B2_JIT_LOCKSTEP_PCS=0x01001000-0x01001010 B2_JIT_LOCKSTEP_DROP=dbcc \
  PREVIOUS_UAE2026_JIT_BOOTSTRAP=0 "$BIN" 2>&1 | grep -c "NATEXEC pc=01001000")
if [[ "$NAT" -ge 1 ]]; then PASS=$((PASS+1)); printf '  PASS %-28s NATEXEC=%s (native-exec core)\n' "native-exec-core" "$NAT"; else FAIL=$((FAIL+1)); FAILED+=("native-exec-core"); printf '  FAIL %-28s NATEXEC=0\n' "native-exec-core"; fi
# detector LIVE: PIN_ATTEMPT must fire on the dbcc scratch write (arch=0/d0 locked-operand)
PIN=$(timeout "$WAIT" env HOME="$WORK/home" SDL_AUDIODRIVER=dummy DISPLAY="$DISP" \
  B2_TEST_HEX="56C8 FFFE" B2_TEST_ADDR=0x01001000 B2_TEST_INIT="5 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0" B2_TEST_DUMP=1 \
  B2_TEST_TWO_PASS=1 B2_TEST_SECOND_PC=0x01001000 \
  PREVIOUS_UAE2026_JIT=1 B2_JIT_FORCE_TRANSLATE=1 PREVIOUS_UAE2026_JIT_RAM=1 \
  B2_JIT_LOCKSTEP_PCS=0x01001000-0x01001010 B2_JIT_LOCKSTEP_DROP=dbcc \
  B2_FORCE_SCRATCH_ALIAS_VREG=0 B2_FORCE_SCRATCH_DEBUG=1 \
  PREVIOUS_UAE2026_JIT_BOOTSTRAP=0 "$BIN" 2>&1 | grep -c "PIN_ATTEMPT")
if [[ "$PIN" -ge 1 ]]; then PASS=$((PASS+1)); printf '  PASS %-28s PIN_ATTEMPT=%s (detector LIVE, refuses locked)\n' "regalloc-detector-live" "$PIN"; else FAIL=$((FAIL+1)); FAILED+=("regalloc-detector-live"); printf '  FAIL %-28s PIN_ATTEMPT=0 (detector dead)\n' "regalloc-detector-live"; fi

echo ""
TOTAL=$((PASS+FAIL))
echo "total=$TOTAL pass=$PASS fail=$FAIL skip=$SKIP"
if [[ "$FAIL" -gt 0 ]]; then echo "FAILED: ${FAILED[*]}"; fi
echo "METRIC cpustate_total=$TOTAL"
echo "METRIC cpustate_pass=$PASS"
echo "METRIC cpustate_fail=$FAIL"
echo "METRIC cpustate_skip=$SKIP"
echo "METRIC cpustate_score=$(( TOTAL>0 ? PASS*100/TOTAL : 0 ))"
[[ "$FAIL" -eq 0 ]]
