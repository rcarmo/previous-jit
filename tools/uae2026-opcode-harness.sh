#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${PREVIOUS_BUILD_DIR:-$ROOT/build-vnc}"
BIN="${PREVIOUS_BIN:-$BUILD_DIR/src/Previous}"
ASSET_ROOT="${PREVIOUS_ASSET_ROOT:-/workspace/assets/previous}"
OUTDIR="${PREVIOUS_OPCODE_HARNESS_OUTDIR:-/workspace/tmp/previous-opcode-harness-$(date +%Y%m%d-%H%M%S)}"
INTERP_WAIT_SEC="${PREVIOUS_OPCODE_INTERP_WAIT_SEC:-3}"
JIT_WAIT_SEC="${PREVIOUS_OPCODE_JIT_WAIT_SEC:-1}"
KEEP_TMP_HOME="${PREVIOUS_OPCODE_KEEP_HOME:-0}"

mkdir -p "$OUTDIR"
source "$ROOT/tools/uae2026-opcode-vectors.sh"

pick_display() {
  local n
  for n in $(seq 138 180); do
    if [[ ! -e "/tmp/.X${n}-lock" && ! -S "/tmp/.X11-unix/X${n}" ]]; then
      echo ":$n"
      return 0
    fi
  done
  echo ":199"
}

choose_source_image() {
  local latest_backup
  latest_backup=$(ls -dt "$ASSET_ROOT"/images/nextstep33-system-en-backup-*.img 2>/dev/null | head -n 1 || true)
  if [[ -n "$latest_backup" ]]; then
    printf '%s\n' "$latest_backup"
  else
    printf '%s\n' "$ASSET_ROOT/images/nextstep33-system-en.img"
  fi
}

SOURCE_IMAGE="${PREVIOUS_SOURCE_IMAGE:-$(choose_source_image)}"
if [[ ! -f "$SOURCE_IMAGE" ]]; then
  echo "missing source image: $SOURCE_IMAGE" >&2
  exit 1
fi

cmake -S "$ROOT" -B "$BUILD_DIR" -DENABLE_VNC=ON -DENABLE_EXPERIMENTAL_UAE2026_JIT=ON >"$OUTDIR/cmake-configure.log" 2>&1
cmake --build "$BUILD_DIR" -j"$(nproc)" >"$OUTDIR/cmake-build.log" 2>&1
if [[ ! -x "$BIN" ]]; then
  echo "missing Previous binary: $BIN" >&2
  exit 1
fi

write_config() {
  local home_dir="$1"
  mkdir -p "$home_dir/.previous"
  cat > "$home_dir/.previous/previous.cfg" <<EOF
[Log]
sLogFileName = stderr
nTextLogLevel = 5
bConfirmQuit = FALSE

[ConfigDialog]
bShowConfigDialogAtStartup = FALSE

[Screen]
bFullScreen = FALSE
bShowStatusbar = FALSE
bShowDriveLed = FALSE

[Keyboard]
bSwapCmdAlt = FALSE
nKeymapType = 0
szMappingFileName =

[Sound]
bEnableSound = FALSE
bEnableMicrophone = FALSE

[ROM]
szRom030FileName = $ASSET_ROOT/roms/Rev_1.0_v41.BIN
szRom040FileName = $ASSET_ROOT/roms/Rev_2.5_v66.BIN
szRomTurboFileName = $ASSET_ROOT/roms/Rev_3.3_v74.BIN

[Boot]
nBootDevice = 1
bEnableDRAMTest = FALSE
bEnablePot = FALSE
bExtendedPot = FALSE
bEnableSoundTest = FALSE
bEnableSCSITest = FALSE
bLoopPot = FALSE
bVerbose = FALSE

[HardDisk]
szImageName0 = $SOURCE_IMAGE
nDeviceType0 = 1
bDiskInserted0 = TRUE
bWriteProtected0 = TRUE

[Floppy]
bDriveConnected0 = FALSE
bDiskInserted0 = FALSE
bWriteProtected0 = TRUE

[System]
nMachineType = 1
bColor = FALSE
bTurbo = FALSE
bNBIC = TRUE
nCpuLevel = 4
nCpuFreq = 25
bCompatibleCpu = TRUE
bRealtime = FALSE
nDSPType = 2
bDSPMemoryExpansion = TRUE
bRealTimeClock = TRUE
n_FPUType = 68040
bCompatibleFPU = TRUE
bMMU = TRUE

[Dimension]
bEnabled = FALSE
bMainDisplay = FALSE
bI860Thread = FALSE
szRomFileName = $ASSET_ROOT/roms/dimension_eeprom.bin
EOF
}

run_case() {
  local name="$1"
  local hex_code="$2"
  local use_jit="$3"
  local sentinel="$4"
  local outfile="$5"
  local home_dir="$OUTDIR/home-${name}-${use_jit}"
  local log="$OUTDIR/${name}.${use_jit}.log"
  local reason_file="$OUTDIR/${name}.${use_jit}.reason"
  local full_hex
  local wait_sec="$INTERP_WAIT_SEC"
  local rc=0
  local dump_count
  local found_dump=0
  local pid=""

  write_config "$home_dir"
  full_hex="$hex_code 2C7C ${sentinel:0:4} ${sentinel:4:4}"
  echo ok > "$reason_file"

  local -a env_vars=(
    HOME="$home_dir"
    SDL_VIDEODRIVER=x11
    SDL_AUDIODRIVER=dummy
    DISPLAY="$DISPLAY_NAME"
    B2_TEST_HEX="$full_hex"
    B2_TEST_DUMP=1
    PREVIOUS_UAE2026_JIT_BOOTSTRAP=0
    PREVIOUS_UAE2026_JIT_CACHE_KB="${PREVIOUS_UAE2026_JIT_CACHE_KB:-8192}"
    PREVIOUS_UAE2026_JIT_FPU="${PREVIOUS_UAE2026_JIT_FPU:-0}"
    PREVIOUS_UAE2026_JIT_LAZY_FLUSH="${PREVIOUS_UAE2026_JIT_LAZY_FLUSH:-1}"
    PREVIOUS_UAE2026_JIT_CONST_JUMP="${PREVIOUS_UAE2026_JIT_CONST_JUMP:-1}"
  )
  if [[ "$use_jit" == "jit" ]]; then
    wait_sec="$JIT_WAIT_SEC"
    env_vars+=(PREVIOUS_UAE2026_JIT=1 B2_JIT_FORCE_TRANSLATE=1)
  else
    env_vars+=(PREVIOUS_UAE2026_JIT=0)
  fi

  (
    env "${env_vars[@]}" "$BIN" > "$log" 2>&1
  ) >/dev/null 2>&1 & pid=$!
  sleep "$wait_sec"

  set +e
  if kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    sleep 1
    kill -9 "$pid" 2>/dev/null || true
  fi
  wait "$pid" >/dev/null 2>&1
  rc=$?
  set -e

  dump_count=$(grep -c '^REGDUMP:' "$log" 2>/dev/null || true)
  if [[ "$dump_count" -gt 0 ]]; then
    found_dump=1
  fi
  if [[ "$found_dump" -ne 1 ]]; then
    if [[ "$rc" -eq 124 || "$rc" -eq 137 || "$rc" -eq 143 ]]; then
      echo timeout > "$reason_file"
    else
      echo "emu_exit_$rc" > "$reason_file"
    fi
    return 1
  fi
  if [[ "$dump_count" -eq 0 ]]; then
    echo no_regdump > "$reason_file"
    return 1
  fi
  if [[ "$dump_count" -ne 1 ]]; then
    echo multi_regdump > "$reason_file"
    return 1
  fi

  grep '^REGDUMP:' "$log" > "$outfile"
  if ! grep -qi "A6=$sentinel" "$outfile"; then
    echo sentinel_mismatch > "$reason_file"
    return 1
  fi

  if [[ "$KEEP_TMP_HOME" != "1" ]]; then
    rm -rf "$home_dir"
  fi
  return 0
}

DISPLAY_NAME="$(pick_display)"
XVFB_PID=""
cleanup() {
  set +e
  if [[ -n "$XVFB_PID" ]]; then
    kill "$XVFB_PID" 2>/dev/null || true
    wait "$XVFB_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

Xvfb "$DISPLAY_NAME" -screen 0 1280x900x24 >"$OUTDIR/xvfb.log" 2>&1 & XVFB_PID=$!
sleep 1

pass=0
fail=0
infra_fail=0
interp_ok=0
jit_ok=0
total=0
: > "$OUTDIR/failures.txt"
: > "$OUTDIR/infra.txt"

idx=0
for name in "${TEST_ORDER[@]}"; do
  total=$((total + 1))
  idx=$((idx + 1))
  hex_code="${TESTS[$name]}"
  sentinel=$(printf '%08X' $((0xA6000000 + idx)))
  interp_out="$OUTDIR/${name}.interp.regdump"
  jit_out="$OUTDIR/${name}.jit.regdump"

  if ! run_case "$name" "$hex_code" interp "$sentinel" "$interp_out"; then
    infra_fail=$((infra_fail + 1))
    echo "$name interp $(cat "$OUTDIR/${name}.interp.reason")" >> "$OUTDIR/infra.txt"
    continue
  fi
  interp_ok=$((interp_ok + 1))
  if ! run_case "$name" "$hex_code" jit "$sentinel" "$jit_out"; then
    infra_fail=$((infra_fail + 1))
    echo "$name jit $(cat "$OUTDIR/${name}.jit.reason")" >> "$OUTDIR/infra.txt"
    continue
  fi
  jit_ok=$((jit_ok + 1))

  if diff -u "$interp_out" "$jit_out" > "$OUTDIR/${name}.diff"; then
    pass=$((pass + 1))
    rm -f "$OUTDIR/${name}.diff"
  else
    fail=$((fail + 1))
    echo "$name" >> "$OUTDIR/failures.txt"
  fi
done

score=0
if [[ "$total" -gt 0 ]]; then
  score=$((pass * 100 / total))
fi

cat > "$OUTDIR/result.env" <<EOF
binary=$BIN
source_image=$SOURCE_IMAGE
display=$DISPLAY_NAME
total=$total
interp_ok=$interp_ok
jit_ok=$jit_ok
pass=$pass
fail=$fail
infra_fail=$infra_fail
score=$score
EOF

cat "$OUTDIR/result.env"
echo "METRIC total=$total"
echo "METRIC interp_ok=$interp_ok"
echo "METRIC jit_ok=$jit_ok"
echo "METRIC pass=$pass"
echo "METRIC fail=$fail"
echo "METRIC infra_fail=$infra_fail"
echo "METRIC score=$score"
echo "OUTDIR=$OUTDIR"

if [[ -s "$OUTDIR/infra.txt" ]]; then
  echo "--- infra failures ---"
  cat "$OUTDIR/infra.txt"
fi
if [[ -s "$OUTDIR/failures.txt" ]]; then
  echo "--- equivalence failures ---"
  cat "$OUTDIR/failures.txt"
fi

if [[ "$fail" -ne 0 || "$infra_fail" -ne 0 ]]; then
  exit 2
fi
