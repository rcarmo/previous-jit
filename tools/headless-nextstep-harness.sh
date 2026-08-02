#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${PREVIOUS_BIN:-$ROOT/build-vnc/src/Previous}"
ASSET_ROOT="/workspace/assets/previous"
OUTDIR="${PREVIOUS_HEADLESS_OUTDIR:-/workspace/tmp/previous-headless-$(date +%Y%m%d-%H%M%S)}"
VNC_PORT="${PREVIOUS_VNC_PORT:-6048}"
KEEP_RUN_IMAGE="${PREVIOUS_KEEP_RUN_IMAGE:-0}"
BOOT_WAIT="${PREVIOUS_BOOT_WAIT:-130}"
SHELL_WAIT="${PREVIOUS_SHELL_WAIT:-5}"
FSCK_WAIT="${PREVIOUS_FSCK_WAIT:-90}"
DESKTOP_TIMEOUT="${PREVIOUS_DESKTOP_TIMEOUT:-1200}"
DESKTOP_POLL="${PREVIOUS_DESKTOP_POLL:-30}"
STABLE_WAIT="${PREVIOUS_STABLE_WAIT:-0}"
SHOW_STATUSBAR="${PREVIOUS_SHOW_STATUSBAR:-FALSE}"
SHOW_DRIVE_LED="${PREVIOUS_SHOW_DRIVE_LED:-FALSE}"
LOG_LEVEL="${PREVIOUS_LOG_LEVEL:-1}"
INSTALL_ISO="${PREVIOUS_INSTALL_ISO:-}"
NORMAL_BOOT="${PREVIOUS_NORMAL_BOOT:-0}"

# JIT defaults: enable the AArch64 JIT bridge with the RAM/MMU dispatch and
# the conservative RTE-fault interpreter handoff oracle, since that is the
# combination known to boot NeXTSTEP cleanly to the Workspace desktop today.
# Override with PREVIOUS_UAE2026_JIT=0 etc. for interpreter baselines.
export PREVIOUS_UAE2026_JIT="${PREVIOUS_UAE2026_JIT:-1}"
export PREVIOUS_UAE2026_JIT_RAM="${PREVIOUS_UAE2026_JIT_RAM:-1}"
if [[ -z "${B2_JIT_RTE_FAULT_HANDOFF_DISABLE:-}" ]]; then
  export B2_JIT_RTE_FAULT_HANDOFF="${B2_JIT_RTE_FAULT_HANDOFF:-1}"
fi
RTC_CHIP="${PREVIOUS_RTC_CHIP:-MC68HC68T1}"
# NeXTSTEP 3.3 treats far-future host dates as a preposterous RTC value.
# Keep the headless system image in its native 1994 date range unless callers
# explicitly request a different RTC timestamp.
RTC_UNIX_TIME="${PREVIOUS_RTC_UNIX_TIME:-0x2ec46472}"
case "$RTC_CHIP" in
  MC68HC68T1|old|OLD|0|false|FALSE) RTC_CHIP_BOOL=FALSE ;;
  MCCS1850|new|NEW|1|true|TRUE) RTC_CHIP_BOOL=TRUE ;;
  *) echo "unknown PREVIOUS_RTC_CHIP: $RTC_CHIP" >&2; exit 1 ;;
esac

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
  echo "source image not found: $SOURCE_IMAGE" >&2
  exit 1
fi
if [[ ! -x "$BIN" ]]; then
  echo "Previous binary not found: $BIN" >&2
  exit 1
fi
if [[ -n "$INSTALL_ISO" && ! -f "$INSTALL_ISO" ]]; then
  echo "install ISO not found: $INSTALL_ISO" >&2
  exit 1
fi

mkdir -p "$OUTDIR/home/.previous"
RUN_IMAGE="$OUTDIR/nextstep33-system-en-run.img"
cp --sparse=always --reflink=auto "$SOURCE_IMAGE" "$RUN_IMAGE"
# Immutable fixture sources may intentionally be mode 0444.  Previous must see
# each disposable copy as writable so first-boot fsck and normal guest writes
# cannot be mistaken for a physically write-protected root disk.
chmod u+w "$RUN_IMAGE"

INSTALL_TARGET_CONFIG=""
if [[ -n "$INSTALL_ISO" ]]; then
  INSTALL_TARGET_CONFIG=$(cat <<EOF
szImageName1 = $INSTALL_ISO
nDeviceType1 = 2
bDiskInserted1 = TRUE
bWriteProtected1 = TRUE
EOF
)
fi

cat > "$OUTDIR/home/.previous/previous.cfg" <<EOF
[Log]
sLogFileName = stderr
sTraceFileName = stderr
nTextLogLevel = $LOG_LEVEL
nAlertDlgLogLevel = 1
bConfirmQuit = FALSE

[ConfigDialog]
bShowConfigDialogAtStartup = FALSE

[Screen]
bFullScreen = FALSE
bShowStatusbar = $SHOW_STATUSBAR
bShowDriveLed = $SHOW_DRIVE_LED

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
$INSTALL_TARGET_CONFIG

[Floppy]
bDriveConnected0 = FALSE
bDiskInserted0 = FALSE
bWriteProtected0 = TRUE

[System]
nMachineType = 1
bColor = FALSE
bTurbo = FALSE
bNBIC = TRUE
nRTC = $RTC_CHIP_BOOL
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
HOME="$OUTDIR/home" SDL_AUDIODRIVER=dummy PREVIOUS_VNC=1 PREVIOUS_VNC_PORT="$VNC_PORT" PREVIOUS_RTC_UNIX_TIME="$RTC_UNIX_TIME" DISPLAY="$DISPLAY_NAME" \
  "$BIN" >"$OUTDIR/previous.log" 2>&1 & EMU_PID=$!

set +e
driver_args=(
  --port "$VNC_PORT"
  --outdir "$OUTDIR"
  --boot-wait "$BOOT_WAIT"
  --shell-wait "$SHELL_WAIT"
  --fsck-wait "$FSCK_WAIT"
  --desktop-timeout "$DESKTOP_TIMEOUT"
  --desktop-poll "$DESKTOP_POLL"
  --stable-wait "$STABLE_WAIT"
)
if [[ "$NORMAL_BOOT" != "0" ]]; then
  driver_args+=(--normal-boot)
fi
python3 "$ROOT/tools/previous_headless_vnc.py" "${driver_args[@]}"
RC=$?
set -e

{
  echo "binary=$BIN"
  echo "source_image=$SOURCE_IMAGE"
  echo "run_image=$RUN_IMAGE"
  echo "display=$DISPLAY_NAME"
  echo "vnc_port=$VNC_PORT"
  echo "keep_run_image=$KEEP_RUN_IMAGE"
  echo "rtc_chip=$RTC_CHIP"
  echo "rtc_chip_bool=$RTC_CHIP_BOOL"
  echo "rtc_unix_time=$RTC_UNIX_TIME"
  echo "install_iso=$INSTALL_ISO"
  echo "normal_boot=$NORMAL_BOOT"
  if [[ -f "$OUTDIR/result.env" ]]; then
    cat "$OUTDIR/result.env"
  fi
} > "$OUTDIR/harness.env"

cat "$OUTDIR/harness.env"
echo "OUTDIR=$OUTDIR"
exit "$RC"
