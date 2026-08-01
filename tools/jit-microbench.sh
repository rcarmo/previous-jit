#!/usr/bin/env bash
# Deterministic bounded interpreter/JIT benchmark using the opcode-test sentinel.
# Timing trials are uninstrumented; separate census trials collect execution-path
# and dispatcher data so observer overhead cannot contaminate the speed ratio.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${PREVIOUS_BIN:-$ROOT/build-vnc/src/Previous}"
ASSET_ROOT="${PREVIOUS_ASSET_ROOT:-/workspace/assets/previous}"
ITERATIONS="${ITERATIONS:-5000000}"
CENSUS_ITERATIONS="${CENSUS_ITERATIONS:-100000}"
TRIALS="${TRIALS:-5}"
TIMEOUT_SEC="${TIMEOUT_SEC:-120}"
OUTDIR="${OUTDIR:-/workspace/tmp/jit-microbench-$(date +%Y%m%d-%H%M%S)}"
SENTINEL="${SENTINEL:-51a7e11e}"

[[ -x "$BIN" ]] || { echo "missing Previous binary: $BIN" >&2; exit 2; }
[[ "$ITERATIONS" =~ ^[1-9][0-9]*$ ]] || { echo "ITERATIONS must be positive" >&2; exit 2; }
[[ "$CENSUS_ITERATIONS" =~ ^[1-9][0-9]*$ ]] || { echo "CENSUS_ITERATIONS must be positive" >&2; exit 2; }
[[ "$TRIALS" =~ ^[1-9][0-9]*$ ]] || { echo "TRIALS must be positive" >&2; exit 2; }
mkdir -p "$OUTDIR"

SOURCE_IMAGE=$(ls -dt "$ASSET_ROOT"/images/nextstep33-system-en-backup-*.img 2>/dev/null | head -1 || true)
[[ -n "$SOURCE_IMAGE" && -f "$SOURCE_IMAGE" ]] || {
  SOURCE_IMAGE="$ASSET_ROOT/images/nextstep33-system-en.img"
}
[[ -f "$SOURCE_IMAGE" ]] || { echo "missing source disk image under $ASSET_ROOT/images" >&2; exit 2; }

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

make_hex() {
  local n=$1 h
  h=$(printf '%08x' "$n")
  # move.l #N,d0 ; subq.l #1,d0 ; bne.s -4 ; movea.l #sentinel,a6
  printf '203c %s %s 5380 66fc 2c7c %s %s' "${h:0:4}" "${h:4:4}" "${SENTINEL:0:4}" "${SENTINEL:4:4}"
}
HEX=$(make_hex "$ITERATIONS")
GUEST_INSNS=$((2 * ITERATIONS + 3))

SHA=$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || printf unknown)
BIN_SHA256=$(sha256sum "$BIN" | awk '{print $1}')
{
  printf 'timestamp_utc=%s\n' "$(date -u +%FT%TZ)"
  printf 'host=%s\nsha=%s\nbinary=%s\nbinary_sha256=%s\n' "$(hostname)" "$SHA" "$BIN" "$BIN_SHA256"
  printf 'source_image=%s\nsource_image_sha256=%s\n' "$SOURCE_IMAGE" "$(sha256sum "$SOURCE_IMAGE" | awk '{print $1}')"
  printf 'iterations=%s\ncensus_iterations=%s\nguest_instructions=%s\ntrials=%s\nhex=%s\n' "$ITERATIONS" "$CENSUS_ITERATIONS" "$GUEST_INSNS" "$TRIALS" "$HEX"
  printf 'kernel=%s\n' "$(uname -srmo)"
  printf 'cpu_governors=%s\n' "$(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor 2>/dev/null | sort -u | paste -sd, - || true)"
  printf 'cpu_mhz=%s\n' "$(awk -F: '/cpu MHz/{gsub(/^[ \t]+/,"",$2); print $2}' /proc/cpuinfo | sort -n | paste -sd, -)"
} > "$OUTDIR/manifest.env"

printf 'trial\tengine\telapsed_s\tguest_instructions\tminsn_s\tlog\n' > "$OUTDIR/trials.tsv"

run_one() {
  local engine=$1 trial=$2 instrumented=$3
  local label="${engine}-${trial}${instrumented:+-census}"
  local home="$OUTDIR/home-$label"
  local log="$OUTDIR/$label.log"
  local run_iterations="$ITERATIONS" run_hex="$HEX" run_guest_insns="$GUEST_INSNS"
  if [[ -n "$instrumented" ]]; then
    run_iterations="$CENSUS_ITERATIONS"
    run_hex=$(make_hex "$run_iterations")
    run_guest_insns=$((2 * run_iterations + 3))
  fi
  local -a engine_env report_env
  write_config "$home"
  if [[ "$engine" == jit ]]; then
    engine_env=(
      PREVIOUS_UAE2026_JIT=1 PREVIOUS_UAE2026_JIT_RAM=1
      PREVIOUS_UAE2026_JIT_BOOTSTRAP=0 PREVIOUS_UAE2026_JIT_CONST_JUMP=0
      PREVIOUS_UAE2026_JIT_LAZY_FLUSH=1 PREVIOUS_UAE2026_JIT_CACHE_KB=8192
    )
  else
    engine_env=(PREVIOUS_UAE2026_JIT=0 PREVIOUS_UAE2026_JIT_RAM=0 PREVIOUS_UAE2026_JIT_BOOTSTRAP=0)
  fi
  report_env=()
  if [[ -n "$instrumented" ]]; then
    report_env=(
      B2_JIT_BENCH_REPORT=1 B2_JIT_HELPER_CENSUS=1000000000
      B2_JIT_GUEST_PATH=1 B2_JIT_STATS=1 B2_JIT_DIAG=1
    )
  fi

  local t0 t1 elapsed rc=0
  t0=$(date +%s.%N)
  timeout "$TIMEOUT_SEC" env \
    HOME="$home" SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    B2_TEST_HEX="$run_hex" B2_TEST_ADDR=0x01001000 \
    B2_TEST_INIT="0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2700" B2_TEST_DUMP=1 \
    "${engine_env[@]}" "${report_env[@]}" "$BIN" >"$log" 2>&1 || rc=$?
  t1=$(date +%s.%N)
  elapsed=$(awk -v a="$t0" -v b="$t1" 'BEGIN { printf "%.6f", b-a }')

  if ((rc != 0)); then
    echo "$label failed rc=$rc (see $log)" >&2
    return 1
  fi
  grep -Eq "REGDUMP: D0=00000000 .*A6=${SENTINEL} .*PC=01001014" "$log" || {
    echo "$label did not reach the exact sentinel state (see $log)" >&2
    return 1
  }
  if [[ -n "$instrumented" ]]; then
    grep -q 'JITHELPERCENSUS tag=final ' "$log" || {
      echo "$label missing final census" >&2; return 1;
    }
    if [[ "$engine" == jit ]]; then
      grep -Eq 'JITHELPERCENSUS tag=final active=1 .*obs=[1-9][0-9]*/' "$log" || {
        echo "$label has no measured native retirement" >&2; return 1;
      }
      grep -q '^JITBENCHDIAG ' "$log" || { echo "$label missing dispatcher diagnostics" >&2; return 1; }
    fi
  else
    local rate
    rate=$(awk -v n="$run_guest_insns" -v t="$elapsed" 'BEGIN { printf "%.6f", n/t/1000000 }')
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$trial" "$engine" "$elapsed" "$run_guest_insns" "$rate" "$log" >> "$OUTDIR/trials.tsv"
    printf '%-9s trial=%s elapsed=%8.3fs rate=%8.3f Minsn/s\n' "$engine" "$trial" "$elapsed" "$rate"
  fi
}

printf 'Previous bounded JIT benchmark: SHA %s, binary %s\n' "$SHA" "$BIN_SHA256"
printf 'Workload: %s exact guest instructions, %s matched trials\n' "$GUEST_INSNS" "$TRIALS"
for ((trial=1; trial<=TRIALS; trial++)); do
  # Alternate order to limit drift and thermal bias.
  if ((trial % 2)); then
    run_one interp "$trial" ""
    run_one jit "$trial" ""
  else
    run_one jit "$trial" ""
    run_one interp "$trial" ""
  fi
done

# One separate, fully observed run per engine. These are coverage evidence, not timing samples.
run_one interp report "yes"
run_one jit report "yes"

awk -F '\t' 'NR > 1 { sum[$2]+=$3; n[$2]++ }
  END { for (e in sum) printf "%s_mean_s=%.6f\n", e, sum[e]/n[e] }' "$OUTDIR/trials.tsv" | sort > "$OUTDIR/summary.env"
interp_mean=$(awk -F= '$1=="interp_mean_s"{print $2}' "$OUTDIR/summary.env")
jit_mean=$(awk -F= '$1=="jit_mean_s"{print $2}' "$OUTDIR/summary.env")
ratio=$(awk -v i="$interp_mean" -v j="$jit_mean" 'BEGIN { printf "%.6f", i/j }')
printf 'speedup_interp_over_jit=%s\n' "$ratio" >> "$OUTDIR/summary.env"

# Preserve the machine-readable final snapshots beside the raw logs.
grep '^JITHELPERCENSUS tag=final ' "$OUTDIR/interp-report-census.log" > "$OUTDIR/interp-census.txt"
grep -E '^(JITHELPERCENSUS tag=final |JITIDENT |JITBENCHDIAG )' "$OUTDIR/jit-report-census.log" > "$OUTDIR/jit-census.txt"

printf '\nResult: interpreter/JIT speedup = %sx\nArtifacts: %s\n' "$ratio" "$OUTDIR"
cat "$OUTDIR/summary.env"
