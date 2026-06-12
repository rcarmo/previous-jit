#!/usr/bin/env bash
# tools/headless-write-cfg.sh OUT_CFG_PATH DISK_IMG_PATH ASSET_ROOT
#
# Writes a Previous .cfg tuned for headless JIT testing into OUT_CFG_PATH.
# Quiet logs (level 1) by default for clean log files; override by editing the
# generated file if you need verbose tracing.
set -euo pipefail

OUT=$1
IMG=$2
ASSET_ROOT=$3

mkdir -p "$(dirname "$OUT")"

cat > "$OUT" <<CFG
[Log]
sLogFileName = stderr
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
szRom030FileName = ${ASSET_ROOT}/roms/Rev_1.0_v41.BIN
szRom040FileName = ${ASSET_ROOT}/roms/Rev_2.5_v66.BIN
szRomTurboFileName = ${ASSET_ROOT}/roms/Rev_3.3_v74.BIN
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
szImageName0 = ${IMG}
nDeviceType0 = 1
bDiskInserted0 = TRUE
bWriteProtected0 = FALSE
[Floppy]
bDriveConnected0 = FALSE
[System]
nMachineType = 1
bColor = FALSE
bTurbo = FALSE
bNBIC = TRUE
nRTC = TRUE
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
szRomFileName = ${ASSET_ROOT}/roms/dimension_eeprom.bin
CFG

echo "wrote $OUT"
