#!/usr/bin/env bash
# Exact interpreter/JIT cycle comparison for Bcc blocks whose runtime edge
# changes after compilation. Covers byte, word and long displacement forms.

set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${PREVIOUS_BIN:-$ROOT/build-vnc/src/Previous}"
ASSET_ROOT="${PREVIOUS_ASSET_ROOT:-/workspace/assets/previous}"
ITERATIONS="${ITERATIONS:-100000}"
OUTDIR="${OUTDIR:-/workspace/tmp/jit-cycle-accuracy-$(date +%Y%m%d-%H%M%S)}"
TIMEOUT_SEC="${TIMEOUT_SEC:-60}"
SENTINEL=51a7e11e

[[ -x "$BIN" ]] || { echo "missing Previous binary: $BIN" >&2; exit 2; }
mkdir -p "$OUTDIR"
SOURCE_IMAGE=$(ls -dt "$ASSET_ROOT"/images/nextstep33-system-en-backup-*.img 2>/dev/null | head -1 || true)
[[ -n "$SOURCE_IMAGE" ]] || SOURCE_IMAGE="$ASSET_ROOT/images/nextstep33-system-en.img"
[[ -f "$SOURCE_IMAGE" ]] || { echo "missing source image" >&2; exit 2; }

write_config() {
  local home=$1
  mkdir -p "$home/.previous"
  cat > "$home/.previous/previous.cfg" <<CFG
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
szRom030FileName = $ASSET_ROOT/roms/Rev_1.0_v41.BIN
szRom040FileName = $ASSET_ROOT/roms/Rev_2.5_v66.BIN
szRomTurboFileName = $ASSET_ROOT/roms/Rev_3.3_v74.BIN
[Boot]
nBootDevice = 1
bEnableDRAMTest = FALSE
bEnablePot = FALSE
bExtendedPot = FALSE
bEnableSoundTest = FALSE
bEnableSCSITest = FALSE
bVerbose = FALSE
[HardDisk]
szImageName0 = $SOURCE_IMAGE
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
CFG
}

h=$(printf '%08x' "$ITERATIONS")
declare -A HEX PC
# move.l #N,d0 ; subq.l #1,d0 ; bne.{b,w,l} subq ; sentinel
HEX[byte]="203c ${h:0:4} ${h:4:4} 5380 66fc 2c7c 51a7 e11e"
HEX[word]="203c ${h:0:4} ${h:4:4} 5380 6600 fffc 2c7c 51a7 e11e"
HEX[long]="203c ${h:0:4} ${h:4:4} 5380 66ff ffff fffc 2c7c 51a7 e11e"
PC[byte]=01001014
PC[word]=01001016
PC[long]=01001018

printf 'case\tinterp_cycles\tjit_cycles\tnative\ttrace\tfallback\tresult\n' > "$OUTDIR/results.tsv"

run_case() {
  local form=$1
  local engine=$2
  local log="$OUTDIR/$form.$engine.log"
  local home="$OUTDIR/home-$form-$engine"
  local -a engine_env
  write_config "$home"
  if [[ "$engine" == jit ]]; then
    engine_env=(PREVIOUS_UAE2026_JIT=1 PREVIOUS_UAE2026_JIT_RAM=1 PREVIOUS_UAE2026_JIT_BOOTSTRAP=0)
  else
    engine_env=(PREVIOUS_UAE2026_JIT=0 PREVIOUS_UAE2026_JIT_RAM=0 PREVIOUS_UAE2026_JIT_BOOTSTRAP=0)
  fi
  timeout "$TIMEOUT_SEC" env HOME="$home" SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    B2_TEST_HEX="${HEX[$form]}" B2_TEST_ADDR=0x01001000 B2_TEST_DUMP=1 \
    B2_TEST_INIT="0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2700" \
    B2_JIT_BENCH_REPORT=1 B2_JIT_HELPER_CENSUS=1000000000 \
    B2_JIT_GUEST_PATH=1 B2_JIT_STATS=1 B2_JIT_DIAG=1 \
    "${engine_env[@]}" "$BIN" >"$log" 2>&1
  grep -Eq "REGDUMP: D0=00000000 .*A6=$SENTINEL .*PC=${PC[$form]}" "$log" || {
    echo "$form/$engine missed sentinel" >&2; return 1;
  }
  grep '^JITHELPERCENSUS tag=final ' "$log" | tail -1
}

for form in byte word long; do
  iline=$(run_case "$form" interp)
  jline=$(run_case "$form" jit)
  icyc=$(sed -n 's/.* cyc=\([-0-9]*\).*/\1/p' <<<"$iline")
  jcyc=$(sed -n 's/.* cyc=\([-0-9]*\).*/\1/p' <<<"$jline")
  obs=$(sed -n 's/.* obs=\([0-9]*\)\/\([0-9]*\)\/\([0-9]*\)\/\([0-9]*\).*/\1 \3 \4/p' <<<"$jline")
  read -r native trace fallback <<<"$obs"
  result=PASS
  [[ -n "$icyc" && "$icyc" == "$jcyc" && "$native" -gt 0 && "$fallback" -eq 0 ]] || result=FAIL
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$form" "$icyc" "$jcyc" "$native" "$trace" "$fallback" "$result" | tee -a "$OUTDIR/results.tsv"
  [[ "$result" == PASS ]] || exit 1
done

printf 'PASS: all Bcc edge-flip cycle totals match (%s)\nArtifacts: %s\n' "$ITERATIONS iterations" "$OUTDIR"
