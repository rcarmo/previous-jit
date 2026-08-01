#!/usr/bin/env bash
# tools/fg-verify-window.sh VERIFY_RANGE [BOOT_SECONDS]
# Bounded, foreground, self-cleaning block-verifier run over a PC window.
# Boots NeXTSTEP under the AArch64 JIT with B2_JIT_VERIFY_BLOCKS set and
# captures JITBLOCKVERIFY output. Avoids display :99 (sibling BasiliskII).
set -euo pipefail

RANGE="${1:?usage: fg-verify-window.sh RANGE [BOOT_SECONDS]}"
BOOT_SECONDS="${2:-120}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${PREVIOUS_BIN:-$ROOT/build-vnc/src/Previous}"
ASSET_ROOT="/workspace/assets/previous"
IMG="${PREVIOUS_SOURCE_IMAGE:-$ASSET_ROOT/images/nextstep33-system-en-backup-20260424-063618.img}"
WORK="$(mktemp -d /workspace/tmp/fg-verify-XXXXXX)"
LOG="${VERIFY_LOG:-$WORK/verify.log}"

cleanup() {
  set +e
  [[ -n "${EMU_PID:-}" ]] && { kill "$EMU_PID" 2>/dev/null; sleep 1; kill -9 "$EMU_PID" 2>/dev/null; }
  [[ -n "${XVFB_PID:-}" ]] && { kill "$XVFB_PID" 2>/dev/null; wait "$XVFB_PID" 2>/dev/null; }
  if [[ "${KEEP_LOG:-0}" == "1" && "$LOG" != "$WORK/verify.log" ]]; then :; else
    [[ -n "${SAVE_LOG:-}" ]] && cp "$LOG" "$SAVE_LOG" 2>/dev/null
  fi
  rm -rf "$WORK"
}
trap cleanup EXIT

[[ -x "$BIN" ]] || { echo "no binary: $BIN" >&2; exit 1; }
[[ -f "$IMG" ]] || { echo "no image: $IMG" >&2; exit 1; }

RUN_IMG="$WORK/disk.img"
cp --sparse=always --reflink=auto "$IMG" "$RUN_IMG"
mkdir -p "$WORK/home/.previous"

# pick a free display in 138-180 (never :99)
DISP=":199"
for n in $(seq 140 180); do
  if [[ ! -e "/tmp/.X${n}-lock" && ! -S "/tmp/.X11-unix/X${n}" ]]; then DISP=":$n"; break; fi
done
VNC_PORT="${VNC_PORT:-$(( 9600 + RANDOM % 300 ))}"

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
  PREVIOUS_UAE2026_JIT="${PREVIOUS_JIT_OVERRIDE:-1}" PREVIOUS_UAE2026_JIT_RAM="${PREVIOUS_JIT_RAM_OVERRIDE:-1}" PREVIOUS_UAE2026_JIT_FPU=1 \
  PREVIOUS_UAE2026_JIT_CACHE_KB=65536 \
  B2_JIT_RTE_FAULT_HANDOFF="${B2_JIT_RTE_FAULT_HANDOFF:-0}" \
  B2_JIT_LOCKSTEP_PCS="${B2_JIT_LOCKSTEP_PCS:-}" \
  B2_JIT_LOCKSTEP_MAXSTEPS="${B2_JIT_LOCKSTEP_MAXSTEPS:-}" \
  B2_JIT_LOCKSTEP_NOBCC="${B2_JIT_LOCKSTEP_NOBCC:-}" \
  B2_JIT_LOCKSTEP_REGONLY="${B2_JIT_LOCKSTEP_REGONLY:-}" \
  B2_JIT_VERIFY_BLOCKS="$RANGE" \
  "$BIN" >"$LOG" 2>&1 & EMU_PID=$!

# bounded wait
for ((s=0; s<BOOT_SECONDS; s++)); do
  kill -0 "$EMU_PID" 2>/dev/null || break
  sleep 1
done

# stop emulator, let log flush
kill "$EMU_PID" 2>/dev/null || true
sleep 1
kill -9 "$EMU_PID" 2>/dev/null || true
EMU_PID=""

echo "=== fg-verify-window range=$RANGE boot=${BOOT_SECONDS}s log=$LOG ==="
echo "total JITBLOCKVERIFY lines: $(grep -c JITBLOCKVERIFY "$LOG" || true)"
echo "mismatch=1 lines: $(grep -c 'mismatch=1' "$LOG" || true)"
echo "skip lines: $(grep -Ec 'SKIP-(ENTRY|SNAPSHOT|SPAN|IO|NOREACH)' "$LOG" || true)"
echo "longjmp abort lines: $(grep -Ec '(ARM-)?ABORT-LONGJMP' "$LOG" || true)"
echo "specialty outcome lines: $(grep -c 'JITBLOCKVERIFY specialty=' "$LOG" || true)"
echo "--- final denominator report ---"
grep 'JITBLOCKVERIFY stats' "$LOG" | tail -1 || true
echo "--- mismatch=1 detail (first 40) ---"
grep 'mismatch=1' "$LOG" | head -40 || true
echo "--- distinct verified block PCs (first 40) ---"
grep -oE 'block=[0-9a-f]{8}' "$LOG" | sort | uniq -c | head -40 || true
[[ -n "${SAVE_LOG:-}" ]] && cp "$LOG" "$SAVE_LOG" && echo "saved log -> $SAVE_LOG"
exit 0
