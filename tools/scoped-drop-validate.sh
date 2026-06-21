#!/usr/bin/env bash
# scoped-drop-validate.sh [BOOT_SECONDS]
# Boots NeXTSTEP under pure RAM JIT with the SCOPED production Bcc-drop over the
# SCSI loop PC range, with JIT diag, to validate the hard-flush cache-reclaim fix.
# PASS = no SIGSEGV + peak_cache < 64MB + bounded recomp + ESP progress past 895.
set -uo pipefail

BOOT_SECONDS="${1:-180}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${PREVIOUS_BIN:-$ROOT/build-vnc/src/Previous}"
ASSET_ROOT="/workspace/assets/previous"
IMG="${PREVIOUS_SOURCE_IMAGE:-$ASSET_ROOT/images/nextstep33-system-en-backup-20260424-063618.img}"
WORK="$(mktemp -d /workspace/tmp/scoped-drop-XXXXXX)"
LOG="$WORK/boot.log"
SAVE_LOG="${SAVE_LOG:-/workspace/tmp/scoped-drop-boot.log}"

cleanup() {
  set +e
  [[ -n "${EMU_PID:-}" ]] && { kill "$EMU_PID" 2>/dev/null; sleep 1; kill -9 "$EMU_PID" 2>/dev/null; }
  [[ -n "${XVFB_PID:-}" ]] && { kill "$XVFB_PID" 2>/dev/null; wait "$XVFB_PID" 2>/dev/null; }
  cp "$LOG" "$SAVE_LOG" 2>/dev/null
  rm -rf "$WORK"
}
trap cleanup EXIT

[[ -x "$BIN" ]] || { echo "no binary: $BIN" >&2; exit 1; }
[[ -f "$IMG" ]] || { echo "no image: $IMG" >&2; exit 1; }

RUN_IMG="$WORK/disk.img"
cp --sparse=always --reflink=auto "$IMG" "$RUN_IMG"
mkdir -p "$WORK/home/.previous"

DISP=":199"
for n in $(seq 140 180); do
  if [[ ! -e "/tmp/.X${n}-lock" && ! -S "/tmp/.X11-unix/X${n}" ]]; then DISP=":$n"; break; fi
done
VNC_PORT="$(( 9600 + RANDOM % 300 ))"

cat > "$WORK/home/.previous/previous.cfg" <<EOF
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
bEnablePot = TRUE
bExtendedPot = FALSE
bEnableSoundTest = TRUE
bEnableSCSITest = TRUE
bLoopPot = FALSE
bVerbose = TRUE
[HardDisk]
szImageName0 = $RUN_IMG
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
nRTC = FALSE
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

Xvfb "$DISP" -screen 0 1280x900x24 >"$WORK/xvfb.log" 2>&1 & XVFB_PID=$!
sleep 1

HOME="$WORK/home" SDL_AUDIODRIVER=dummy \
  PREVIOUS_VNC=1 PREVIOUS_VNC_PORT="$VNC_PORT" PREVIOUS_RTC_UNIX_TIME=0x2ec46472 \
  DISPLAY="$DISP" \
  PREVIOUS_UAE2026_JIT=1 PREVIOUS_UAE2026_JIT_RAM=1 PREVIOUS_UAE2026_JIT_FPU=1 \
  PREVIOUS_UAE2026_JIT_CACHE_KB=65536 \
  B2_JIT_DIAG=1 \
  B2_JIT_DROP_BCC_PROD=1 \
  B2_JIT_DROP_BCC_PROD_LO=0x04382000 \
  B2_JIT_DROP_BCC_PROD_HI=0x04388000 \
  "$BIN" >"$LOG" 2>&1 & EMU_PID=$!

START=$(date +%s)
EXITED=0
for ((s=0; s<BOOT_SECONDS; s++)); do
  if ! kill -0 "$EMU_PID" 2>/dev/null; then EXITED=1; break; fi
  sleep 1
done
END=$(date +%s)
wait "$EMU_PID" 2>/dev/null; RC=$?
RUNTIME=$((END-START))
EMU_PID=""

echo "=== scoped-drop validate: runtime=${RUNTIME}s exited_early=${EXITED} rc=${RC} ==="
if [[ "$EXITED" == "1" ]]; then
  if [[ "$RC" -ge 128 ]]; then echo "!! PROCESS DIED with signal $((RC-128)) (139=SIGSEGV) at ~${RUNTIME}s"; else echo "process exited rc=$RC at ~${RUNTIME}s"; fi
else
  echo "OK: survived full ${BOOT_SECONDS}s window (no crash)"
fi
echo "--- last JIT_DIAG line (compile/recomp/peak_cache) ---"
grep 'JIT_DIAG t=' "$LOG" | tail -1
echo "--- ESP / SCSI progress markers ---"
grep -ciE 'ESP|SCSI' "$LOG"
echo "--- any segfault/abort in log ---"
grep -iE 'segmentation|sigsegv|abort|jit_abort|target=0x19' "$LOG" | head -5
echo "log saved -> $SAVE_LOG"
