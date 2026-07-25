#!/usr/bin/env bash
set -eu
ROOT=/workspace/projects/previous
BIN=$ROOT/build-vnc/src/Previous
ASSET=/workspace/assets/previous
IMG=$ASSET/images/nextstep33-system-en-backup-20260424-063618.img
SECS=${1:-180}
LOG=${2:-/workspace/tmp/prog.log}
W=$(mktemp -d /workspace/tmp/prog-XXXXXX)
trap 'kill $EMU 2>/dev/null; sleep 1; kill -9 $EMU 2>/dev/null; kill $XV 2>/dev/null; cp $W/emu.log "$LOG" 2>/dev/null; rm -rf $W' EXIT
cp --sparse=always "$IMG" $W/disk.img
mkdir -p $W/home/.previous
DISP=:171
cat > $W/home/.previous/previous.cfg <<EOF
[Log]
sLogFileName = stderr
sTraceFileName = stderr
nTextLogLevel = 3
bConfirmQuit = FALSE
[ConfigDialog]
bShowConfigDialogAtStartup = FALSE
[ROM]
szRom040FileName = $ASSET/roms/Rev_2.5_v66.BIN
szRomTurboFileName = $ASSET/roms/Rev_3.3_v74.BIN
[Boot]
nBootDevice = 1
bEnableDRAMTest = FALSE
bEnablePot = TRUE
bVerbose = TRUE
[HardDisk]
szImageName0 = $W/disk.img
nDeviceType0 = 1
bDiskInserted0 = TRUE
[Floppy]
bDriveConnected0 = FALSE
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
EOF
Xvfb $DISP -screen 0 1280x900x24 >$W/xvfb.log 2>&1 & XV=$!
sleep 1
HOME=$W/home SDL_AUDIODRIVER=dummy DISPLAY=$DISP PREVIOUS_VNC=1 PREVIOUS_VNC_PORT=9701 \
  PREVIOUS_RTC_UNIX_TIME=0x2ec46472 \
  PREVIOUS_UAE2026_JIT=1 PREVIOUS_UAE2026_JIT_RAM=1 PREVIOUS_UAE2026_JIT_FPU=1 \
  PREVIOUS_UAE2026_JIT_CACHE_KB=65536 \
  $BIN >$W/emu.log 2>&1 & EMU=$!
for ((s=0;s<SECS;s++)); do kill -0 $EMU 2>/dev/null || break; sleep 1; done
echo "done -> $LOG"
