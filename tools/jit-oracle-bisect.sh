#!/usr/bin/env bash
# tools/jit-oracle-bisect.sh [TEST_HEX]
#
# Run a small M68K code blob in both interp and JIT modes via the opcode-test
# harness and diff the final register dumps.  If they differ, the JIT codegen
# for one of the opcodes is wrong.
#
# Usage:
#   tools/jit-oracle-bisect.sh "e388 e214 e219 b304"
# or override init regs:
#   B2_TEST_INIT="ffffffff,deadbeef,..." tools/jit-oracle-bisect.sh "..."

set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${PREVIOUS_BIN:-$ROOT/build-vnc/src/Previous}"
ASSET_ROOT="${PREVIOUS_ASSET_ROOT:-/workspace/assets/previous}"
OUTDIR="${OUTDIR:-/workspace/tmp/jit-oracle}"

HEX="${1:-e388 e214 e219 b304}"
INIT="${B2_TEST_INIT:-ffffffff,00000000,00000007,00000003,000000ff,00000000,00000000,00000000,01000024,01000024,00000000,00000000,00000000,00000000,00000000,00010000,2700}"

mkdir -p "$OUTDIR/home/.previous"

SOURCE_IMAGE=$(ls -dt $ASSET_ROOT/images/nextstep33-system-en-backup-*.img 2>/dev/null | head -1)
cat > "$OUTDIR/home/.previous/previous.cfg" <<CFG
[Log]
sLogFileName = stderr
nTextLogLevel = 1
bConfirmQuit = FALSE
[ConfigDialog]
bShowConfigDialogAtStartup = FALSE
[Screen]
bFullScreen = FALSE
bShowStatusbar = FALSE
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
bDiskInserted0 = FALSE
[Floppy]
bDriveConnected0 = FALSE
[System]
nMachineType = 1
nCpuLevel = 4
nCpuFreq = 25
bCompatibleCpu = TRUE
n_FPUType = 68040
bMMU = TRUE
[Dimension]
bEnabled = FALSE
CFG

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
    timeout 30 env \
        HOME="$home" \
        SDL_VIDEODRIVER=offscreen \
        SDL_AUDIODRIVER=dummy \
        B2_TEST_HEX="$HEX" \
        B2_TEST_ADDR=0x01001000 \
        B2_TEST_INIT="$INIT" \
        B2_TEST_DUMP=1 \
        "${jit_env[@]}" \
        "$BIN" > "$log" 2>&1 || true
    # Extract the REGDUMP line
    grep '^REGDUMP:' "$log" | tail -1
}

echo "HEX:  '$HEX'"
echo "INIT: '$INIT'"
echo
echo -n "interp: "; run interp off
echo -n "jit:    "; run jit    on
echo
# diff just the data
i=$(grep '^REGDUMP:' "$OUTDIR/interp.log" | tail -1 | tr -d ' ')
j=$(grep '^REGDUMP:' "$OUTDIR/jit.log"    | tail -1 | tr -d ' ')
if [[ "$i" == "$j" ]]; then
    echo "MATCH"
else
    echo "DIFF"
    # Print field-by-field
    paste <(echo "$i" | tr ',:' '\n\n' | grep =) <(echo "$j" | tr ',:' '\n\n' | grep =) | awk '$1 != $2 {print "    " $1 "  vs  " $2}'
fi
