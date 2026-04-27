#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${PREVIOUS_BIN:-$ROOT/build-vnc/src/Previous}"
ASSET_ROOT="/workspace/assets/previous"
OUTDIR="${PREVIOUS_JIT_BOOTSTRAP_OUTDIR:-/workspace/tmp/previous-jit-bootstrap-$(date +%Y%m%d-%H%M%S)}"
BOOTSTRAP_WAIT="${PREVIOUS_JIT_BOOTSTRAP_WAIT:-15}"
KEEP_RUN_IMAGE="${PREVIOUS_KEEP_RUN_IMAGE:-0}"

pick_display() {
  local n
  for n in $(seq 139 180); do
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
if [[ ! -x "$BIN" ]]; then
  echo "Previous binary not found: $BIN" >&2
  exit 1
fi
if [[ ! -f "$SOURCE_IMAGE" ]]; then
  echo "source image not found: $SOURCE_IMAGE" >&2
  exit 1
fi

mkdir -p "$OUTDIR/home/.previous"
RUN_IMAGE="$OUTDIR/nextstep33-system-en-run.img"
cp --sparse=always --reflink=auto "$SOURCE_IMAGE" "$RUN_IMAGE"

cat > "$OUTDIR/home/.previous/previous.cfg" <<EOF
[Log]
sLogFileName = stderr
sTraceFileName = stderr
nTextLogLevel = 5
nAlertDlgLogLevel = 1
bConfirmQuit = FALSE

[ConfigDialog]
bShowConfigDialogAtStartup = FALSE

[Screen]
bFullScreen = FALSE
bShowStatusbar = TRUE
bShowDriveLed = TRUE

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
bEnablePot = TRUE
bExtendedPot = FALSE
bEnableSoundTest = TRUE
bEnableSCSITest = TRUE
bLoopPot = FALSE
bVerbose = TRUE

[HardDisk]
szImageName0 = $RUN_IMAGE
nDeviceType0 = 1
bDiskInserted0 = TRUE
bWriteProtected0 = FALSE

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

DISPLAY_NAME="$(pick_display)"
XVFB_PID=""
EMU_PID=""
cleanup() {
  set +e
  if [[ -n "$EMU_PID" ]]; then
    kill "$EMU_PID" 2>/dev/null || true
    sleep 1
    kill -9 "$EMU_PID" 2>/dev/null || true
  fi
  if [[ -n "$XVFB_PID" ]]; then
    kill "$XVFB_PID" 2>/dev/null || true
    wait "$XVFB_PID" 2>/dev/null || true
  fi
  if [[ "$KEEP_RUN_IMAGE" != "1" ]]; then
    rm -f "$RUN_IMAGE"
  fi
}
trap cleanup EXIT

Xvfb "$DISPLAY_NAME" -screen 0 1280x900x24 >"$OUTDIR/xvfb.log" 2>&1 & XVFB_PID=$!
sleep 1
HOME="$OUTDIR/home" SDL_AUDIODRIVER=dummy DISPLAY="$DISPLAY_NAME" \
PREVIOUS_UAE2026_JIT=1 PREVIOUS_UAE2026_JIT_BOOTSTRAP=1 \
PREVIOUS_UAE2026_JIT_CACHE_KB="${PREVIOUS_UAE2026_JIT_CACHE_KB:-8192}" \
PREVIOUS_UAE2026_JIT_FPU="${PREVIOUS_UAE2026_JIT_FPU:-0}" \
PREVIOUS_UAE2026_JIT_LAZY_FLUSH="${PREVIOUS_UAE2026_JIT_LAZY_FLUSH:-1}" \
PREVIOUS_UAE2026_JIT_CONST_JUMP="${PREVIOUS_UAE2026_JIT_CONST_JUMP:-1}" \
"$BIN" >"$OUTDIR/previous.log" 2>&1 & EMU_PID=$!

sleep "$BOOTSTRAP_WAIT"
set +e
kill "$EMU_PID" 2>/dev/null || true
sleep 1
kill -9 "$EMU_PID" 2>/dev/null || true
EMU_PID=""
set -e

bridge_compiled=0
bootstrap_ready=0
bootstrap_active=0
aslr_active=0
if grep -q 'UAE2026 bridge:' "$OUTDIR/previous.log" 2>/dev/null; then
  bridge_compiled=1
fi
if grep -q 'bootstrap_ready=yes' "$OUTDIR/previous.log" 2>/dev/null; then
  bootstrap_ready=1
fi
if grep -q 'bootstrap_active=yes' "$OUTDIR/previous.log" 2>/dev/null; then
  bootstrap_active=1
fi
if grep -q 'aslr=yes' "$OUTDIR/previous.log" 2>/dev/null; then
  aslr_active=1
fi

cat > "$OUTDIR/result.env" <<EOF
binary=$BIN
source_image=$SOURCE_IMAGE
run_image=$RUN_IMAGE
bridge_compiled=$bridge_compiled
bootstrap_ready=$bootstrap_ready
bootstrap_active=$bootstrap_active
aslr_active=$aslr_active
display=$DISPLAY_NAME
wait_sec=$BOOTSTRAP_WAIT
EOF

cat "$OUTDIR/result.env"
echo "METRIC bridge_compiled=$bridge_compiled"
echo "METRIC bootstrap_ready=$bootstrap_ready"
echo "METRIC bootstrap_active=$bootstrap_active"
echo "METRIC aslr_active=$aslr_active"
echo "OUTDIR=$OUTDIR"

if [[ "$bridge_compiled" != "1" || "$bootstrap_ready" != "1" || "$bootstrap_active" != "1" || "$aslr_active" != "1" ]]; then
  exit 2
fi
