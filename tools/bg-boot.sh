#!/usr/bin/env bash
# bg-boot.sh MODE DISP VNCPORT WORKDIR [EXTRA_ENV...]
# MODE: jit | interp. Boots Previous detached; writes emu.log in WORKDIR.
set -eu
MODE=$1; DISP=$2; VNC=$3; W=$4; shift 4
ROOT=/workspace/projects/previous
ASSET=/workspace/assets/previous
IMG=$ASSET/images/nextstep33-system-en-backup-20260424-063618.img
mkdir -p "$W/home/.previous"
cp --sparse=always "$IMG" "$W/disk.img"
cat > "$W/home/.previous/previous.cfg" <<EOF
[ConfigDialog]
bShowConfigDialogAtStartup = FALSE
[ROM]
szRom040FileName = $ASSET/roms/Rev_2.5_v66.BIN
[Boot]
nBootDevice = 1
bEnableDRAMTest = FALSE
bEnablePot = TRUE
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
bMMU = TRUE
n_FPUType = 68040
bCompatibleFPU = TRUE
[Dimension]
bEnabled = FALSE
EOF
if [ "$MODE" = jit ]; then JITV=1; else JITV=0; fi
setsid bash -c "
  Xvfb $DISP -screen 0 1120x832x24 >$W/xvfb.log 2>&1 &
  echo \$! > $W/xv.pid
  sleep 1
  HOME=$W/home SDL_AUDIODRIVER=dummy DISPLAY=$DISP PREVIOUS_VNC=1 PREVIOUS_VNC_PORT=$VNC \
    PREVIOUS_RTC_UNIX_TIME=0x2ec46472 \
    PREVIOUS_UAE2026_JIT=$JITV PREVIOUS_UAE2026_JIT_RAM=$JITV PREVIOUS_UAE2026_JIT_FPU=1 \
    PREVIOUS_UAE2026_JIT_CACHE_KB=65536 $* \
    $ROOT/build-vnc/src/Previous >$W/emu.log 2>&1 &
  echo \$! > $W/emu.pid
" >/dev/null 2>&1 &
sleep 2
echo "started $MODE in $W (DISP=$DISP VNC=$VNC) emu.pid=$(cat $W/emu.pid 2>/dev/null)"
