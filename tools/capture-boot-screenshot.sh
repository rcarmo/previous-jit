#!/usr/bin/env bash
# Boot NeXTSTEP under the AArch64 JIT (full cfg) and capture a screenshot of the
# current boot/loader screen after WAIT_SECONDS. Self-cleaning.
set -euo pipefail
WAIT_SECONDS="${1:-200}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${PREVIOUS_BIN:-$ROOT/build-vnc/src/Previous}"
ASSET_ROOT="/workspace/assets/previous"
IMG="${PREVIOUS_SOURCE_IMAGE:-$ASSET_ROOT/images/nextstep33-system-en-backup-20260424-063618.img}"
OUT="${OUT:-/workspace/tmp/previous-bootshot-$(date +%H%M%S)}"
WORK="$(mktemp -d /workspace/tmp/bootshot-XXXXXX)"
mkdir -p "$OUT"

cleanup() {
  set +e
  [[ -n "${EMU_PID:-}" ]] && { kill "$EMU_PID" 2>/dev/null; sleep 1; kill -9 "$EMU_PID" 2>/dev/null; }
  [[ -n "${XVFB_PID:-}" ]] && { kill "$XVFB_PID" 2>/dev/null; wait "$XVFB_PID" 2>/dev/null; }
  rm -rf "$WORK"
}
trap cleanup EXIT

[[ -x "$BIN" ]] || { echo "no binary: $BIN" >&2; exit 1; }
RUN_IMG="$WORK/disk.img"
cp --sparse=always --reflink=auto "$IMG" "$RUN_IMG"
mkdir -p "$WORK/home/.previous"

DISP=":166"
for n in $(seq 160 180); do
  if [[ ! -e "/tmp/.X${n}-lock" && ! -S "/tmp/.X11-unix/X${n}" ]]; then DISP=":$n"; break; fi
done

cat > "$WORK/home/.previous/previous.cfg" <<EOF
[Log]
sLogFileName = stderr
nTextLogLevel = 1
nAlertDlgLogLevel = 1
bConfirmQuit = FALSE
[ConfigDialog]
bShowConfigDialogAtStartup = FALSE
[Screen]
bFullScreen = FALSE
bShowStatusbar = FALSE
bShowDriveLed = FALSE
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
EOF

Xvfb "$DISP" -screen 0 1280x1024x24 >"$WORK/xvfb.log" 2>&1 & XVFB_PID=$!
sleep 1
HOME="$WORK/home" SDL_AUDIODRIVER=dummy DISPLAY="$DISP" \
  PREVIOUS_VNC=1 PREVIOUS_VNC_PORT="$(( 9600 + RANDOM % 300 ))" PREVIOUS_RTC_UNIX_TIME=0x2ec46472 \
  PREVIOUS_UAE2026_JIT=1 PREVIOUS_UAE2026_JIT_RAM=1 PREVIOUS_UAE2026_JIT_FPU=1 PREVIOUS_UAE2026_JIT_CACHE_KB=65536 \
  "$BIN" >"$OUT/previous.log" 2>&1 & EMU_PID=$!

echo "booting ${WAIT_SECONDS}s on $DISP ..."
for ((s=0; s<WAIT_SECONDS; s++)); do kill -0 "$EMU_PID" 2>/dev/null || break; sleep 1; done

# capture while still running
DISPLAY="$DISP" xwininfo -root -tree >"$OUT/xwin_tree.txt" 2>/dev/null || true
WIN_ID="$(awk '/Previous/ {print $1; exit}' "$OUT/xwin_tree.txt" 2>/dev/null || true)"
if [[ -n "${WIN_ID:-}" ]]; then
  DISPLAY="$DISP" xwd -silent -id "$WIN_ID" -out "$WORK/win.xwd" 2>/dev/null && \
    ffmpeg -y -v error -f xwd -i "$WORK/win.xwd" -frames:v 1 "$OUT/boot-window.png" 2>/dev/null || true
fi
DISPLAY="$DISP" xwd -silent -root -out "$WORK/root.xwd" 2>/dev/null && \
  ffmpeg -y -v error -f xwd -i "$WORK/root.xwd" -frames:v 1 "$OUT/boot-root.png" 2>/dev/null || true

echo "=== boot tail ==="; tail -4 "$OUT/previous.log" 2>/dev/null || true
echo "OUT=$OUT"
ls -la "$OUT"/*.png 2>/dev/null || echo "no PNG captured"
