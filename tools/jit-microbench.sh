#!/usr/bin/env bash
# tools/jit-microbench.sh
#
# Measure raw JIT vs interpreter throughput on a tight M68K loop using
# the existing opcode-test harness mechanism (B2_TEST_HEX).  No NeXTSTEP
# boot involved; this isolates pure CPU emulation speed.
#
# The loop is:
#       move.l #N, d0       ; counter
#   L:  subq.l #1, d0
#       bne.s L
#
# Each iteration = 2 m68k instructions, so total ~ 2N executed.
# Run under JIT and under interpreter; report wall-clock + ratio.

set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${PREVIOUS_BIN:-$ROOT/build-vnc/src/Previous}"
ASSET_ROOT="${PREVIOUS_ASSET_ROOT:-/workspace/assets/previous}"
ITERATIONS="${ITERATIONS:-50000000}"
OUTDIR="${OUTDIR:-/workspace/tmp/jit-microbench}"

mkdir -p "$OUTDIR/home/.previous"

SOURCE_IMAGE=$(ls -dt ${ASSET_ROOT}/images/nextstep33-system-en-backup-*.img 2>/dev/null | head -1)
[[ -n "$SOURCE_IMAGE" ]] || { echo "no source disk image under $ASSET_ROOT/images"; exit 2; }

# Write a minimal cfg.  We use the standard ROM/disk so the bridge boots,
# but immediately enter test mode and run the bench code.
cat > "$OUTDIR/home/.previous/previous.cfg" <<CFG
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
[Sound]
bEnableSound = FALSE
[ROM]
szRom030FileName = ${ASSET_ROOT}/roms/Rev_1.0_v41.BIN
szRom040FileName = ${ASSET_ROOT}/roms/Rev_2.5_v66.BIN
szRomTurboFileName = ${ASSET_ROOT}/roms/Rev_3.3_v74.BIN
[Boot]
nBootDevice = 1
bEnableDRAMTest = FALSE
bEnablePot = FALSE
bExtendedPot = FALSE
bEnableSoundTest = FALSE
bEnableSCSITest = FALSE
bVerbose = FALSE
[HardDisk]
szImageName0 = ${SOURCE_IMAGE}
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
nRTC = TRUE
nCpuLevel = 4
nCpuFreq = 25
bCompatibleCpu = ${COMPATIBLE_CPU:-TRUE}
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

# Encode the loop: move.l #N, d0 ; subq.l #1, d0 ; bne.s -4
#   203c  XXXX XXXX  5380  66fc
hex_iters=$(printf "%08x" "$ITERATIONS")
hi=${hex_iters:0:4}
lo=${hex_iters:4:4}
HEX="203c $hi $lo 5380 66fc"
echo "loop: move.l #$ITERATIONS,d0 ; subq.l #1,d0 ; bne.s -4"
echo "      HEX = '$HEX'"
echo "      iterations = $ITERATIONS, total m68k insns ~ $((2*ITERATIONS))"
echo

run() {
    local label=$1 jit=$2
    local home="$OUTDIR/home"
    local log="$OUTDIR/$label.log"
    rm -f "$log"
    local jit_env=()
    if [[ "$jit" == on ]]; then
        jit_env=(
            PREVIOUS_UAE2026_JIT=1
            PREVIOUS_UAE2026_JIT_RAM=1
            PREVIOUS_UAE2026_JIT_BOOTSTRAP=1
        )
    else
        jit_env=(PREVIOUS_UAE2026_JIT=0 PREVIOUS_UAE2026_JIT_BOOTSTRAP=0)
    fi
    local t0=$(date +%s.%N)
    timeout 60 env \
        HOME="$home" \
        SDL_VIDEODRIVER=offscreen \
        SDL_AUDIODRIVER=dummy \
        B2_TEST_HEX="$HEX" \
        B2_TEST_ADDR=0x01001000 \
        B2_TEST_INIT="0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2700" \
        B2_TEST_DUMP=1 \
        "${jit_env[@]}" \
        "$BIN" > "$log" 2>&1 || true
    local t1=$(date +%s.%N)
    local dt=$(awk -v a=$t0 -v b=$t1 'BEGIN{printf "%.3f", b-a}')
    local ips=$(awk -v n=$((2*ITERATIONS)) -v t=$dt 'BEGIN{printf "%.2f", n/t/1e6}')
    echo "$label: dt=${dt}s   ~${ips} M m68k-insn/s"
}

run interp off
run jit    on
