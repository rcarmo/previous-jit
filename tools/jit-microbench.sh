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
TRIALS="${TRIALS:-7}"
WARM_TRIALS="${WARM_TRIALS:-9}"
TIMEOUT_SEC="${TIMEOUT_SEC:-120}"
OUTDIR="${OUTDIR:-/workspace/tmp/jit-microbench-$(date +%Y%m%d-%H%M%S)}"
SENTINEL="${SENTINEL:-51a7e11e}"
BENCH_CPU="${BENCH_CPU:-11}"
LOCK_FILE="${BENCH_LOCK_FILE:-/run/lock/previous-jit-benchmark.lock}"

[[ -x "$BIN" ]] || { echo "missing Previous binary: $BIN" >&2; exit 2; }
[[ "$ITERATIONS" =~ ^[1-9][0-9]*$ ]] || { echo "ITERATIONS must be positive" >&2; exit 2; }
[[ "$CENSUS_ITERATIONS" =~ ^[1-9][0-9]*$ ]] || { echo "CENSUS_ITERATIONS must be positive" >&2; exit 2; }
[[ "$TRIALS" =~ ^[1-9][0-9]*$ ]] || { echo "TRIALS must be positive" >&2; exit 2; }
[[ "$WARM_TRIALS" =~ ^[1-9][0-9]*$ ]] || { echo "WARM_TRIALS must be positive" >&2; exit 2; }
[[ "$BENCH_CPU" =~ ^[0-9]+$ ]] || { echo "BENCH_CPU must be a CPU number" >&2; exit 2; }
command -v flock >/dev/null || { echo "flock is required" >&2; exit 2; }
command -v taskset >/dev/null || { echo "taskset is required" >&2; exit 2; }
[[ -z "$(git -C "$ROOT" status --porcelain)" ]] || {
  echo "refusing benchmark from dirty source tree" >&2; exit 2;
}
mkdir -p "$OUTDIR"

# Own one cpufreq policy for the complete series.  The workload and all of its
# threads inherit one-core affinity.  Any unavailable write or failed readback
# aborts the run; the EXIT trap restores the original policy on every path.
exec 9>"$LOCK_FILE"
flock -n 9 || { echo "benchmark lock is already held: $LOCK_FILE" >&2; exit 2; }
if pgrep -x Previous >/dev/null; then
  echo "another Previous process is active; refusing shared-host timing" >&2
  pgrep -a -x Previous >&2 || true
  exit 2
fi
CPU_POLICY=$(readlink -f "/sys/devices/system/cpu/cpu$BENCH_CPU/cpufreq" 2>/dev/null || true)
[[ -d "$CPU_POLICY" ]] || { echo "CPU $BENCH_CPU has no cpufreq policy" >&2; exit 2; }
sudo -n true 2>/dev/null || { echo "passwordless sudo is required for fixed-frequency ownership" >&2; exit 2; }
OLD_GOVERNOR=$(<"$CPU_POLICY/scaling_governor")
OLD_MIN_FREQ=$(<"$CPU_POLICY/scaling_min_freq")
OLD_MAX_FREQ=$(<"$CPU_POLICY/scaling_max_freq")
FIXED_FREQ=$(<"$CPU_POLICY/cpuinfo_max_freq")
POLICY_OWNED=0
restore_host() {
  local rc=$? restore_failed=0
  trap - EXIT INT TERM
  if ((POLICY_OWNED)); then
    printf '%s' "$OLD_MIN_FREQ" | sudo -n tee "$CPU_POLICY/scaling_min_freq" >/dev/null || restore_failed=1
    printf '%s' "$OLD_MAX_FREQ" | sudo -n tee "$CPU_POLICY/scaling_max_freq" >/dev/null || restore_failed=1
    printf '%s' "$OLD_GOVERNOR" | sudo -n tee "$CPU_POLICY/scaling_governor" >/dev/null || restore_failed=1
    if [[ "$(<"$CPU_POLICY/scaling_governor")" != "$OLD_GOVERNOR" ||
          "$(<"$CPU_POLICY/scaling_min_freq")" != "$OLD_MIN_FREQ" ||
          "$(<"$CPU_POLICY/scaling_max_freq")" != "$OLD_MAX_FREQ" ]]; then
      restore_failed=1
    fi
  fi
  if ((restore_failed)); then
    printf 'ERROR: failed to restore %s (governor=%s min=%s max=%s)\n' \
      "$CPU_POLICY" "$(<"$CPU_POLICY/scaling_governor")" \
      "$(<"$CPU_POLICY/scaling_min_freq")" "$(<"$CPU_POLICY/scaling_max_freq")" >&2
    ((rc == 0)) && rc=4
  fi
  exit "$rc"
}
trap restore_host EXIT INT TERM
POLICY_OWNED=1
printf '%s' performance | sudo -n tee "$CPU_POLICY/scaling_governor" >/dev/null
printf '%s' "$FIXED_FREQ" | sudo -n tee "$CPU_POLICY/scaling_max_freq" >/dev/null
printf '%s' "$FIXED_FREQ" | sudo -n tee "$CPU_POLICY/scaling_min_freq" >/dev/null
[[ "$(<"$CPU_POLICY/scaling_governor")" == performance &&
   "$(<"$CPU_POLICY/scaling_min_freq")" == "$FIXED_FREQ" &&
   "$(<"$CPU_POLICY/scaling_max_freq")" == "$FIXED_FREQ" ]] || {
  echo "failed to establish fixed-frequency ownership on $CPU_POLICY" >&2; exit 2;
}

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
GUEST_CYCLES=$((4 * ITERATIONS + 7))

SHA=$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || printf unknown)
BIN_SHA256=$(sha256sum "$BIN" | awk '{print $1}')
{
  printf 'timestamp_utc=%s\n' "$(date -u +%FT%TZ)"
  printf 'host=%s\nsha=%s\nbinary=%s\nbinary_sha256=%s\n' "$(hostname)" "$SHA" "$BIN" "$BIN_SHA256"
  printf 'source_image=%s\nsource_image_sha256=%s\n' "$SOURCE_IMAGE" "$(sha256sum "$SOURCE_IMAGE" | awk '{print $1}')"
  printf 'iterations=%s\ncensus_iterations=%s\nguest_instructions=%s\nguest_cycles=%s\ncold_trials=%s\nwarm_trials=%s\nhex=%s\n' "$ITERATIONS" "$CENSUS_ITERATIONS" "$GUEST_INSNS" "$GUEST_CYCLES" "$TRIALS" "$WARM_TRIALS" "$HEX"
  printf 'timing_scope_cold=process_launch_to_process_exit\n'
  printf 'timing_scope_warm=in_process_post_warmup_cpu_loop\n'
  printf 'kernel=%s\n' "$(uname -srmo)"
  printf 'bench_cpu=%s\ncpu_policy=%s\npolicy_cpus=%s\n' "$BENCH_CPU" "$CPU_POLICY" "$(<"$CPU_POLICY/related_cpus")"
  printf 'cpu_governor=%s\nfixed_frequency_khz=%s\n' "$(<"$CPU_POLICY/scaling_governor")" "$(<"$CPU_POLICY/scaling_min_freq")"
  printf 'original_cpu_governor=%s\noriginal_min_frequency_khz=%s\noriginal_max_frequency_khz=%s\n' "$OLD_GOVERNOR" "$OLD_MIN_FREQ" "$OLD_MAX_FREQ"
} > "$OUTDIR/manifest.env"

printf 'trial\tengine\telapsed_s\tguest_instructions\tminsn_s\tlog\n' > "$OUTDIR/cold-process.tsv"
printf 'sample\tengine\telapsed_s\tguest_instructions\tminsn_s\tlog\n' > "$OUTDIR/warm-in-process.tsv"

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
      B2_JIT_BENCH_REPORT=1 B2_JIT_BENCH_EXPECTED_INSNS="$run_guest_insns"
      B2_JIT_HELPER_CENSUS=1000000000 B2_JIT_GUEST_PATH=1
      B2_JIT_STATS=1 B2_JIT_DIAG=1
    )
  fi

  local t0 t1 elapsed rc=0
  t0=$(date +%s.%N)
  timeout "$TIMEOUT_SEC" taskset -c "$BENCH_CPU" env \
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
    grep -q '^JITBENCHCOVERAGE .*reconciled=1 ' "$log" || {
      echo "$label has an unreconciled coverage denominator" >&2; return 1;
    }
    if [[ "$engine" == jit ]]; then
      grep -Eq "^JITBENCHCOVERAGE active=1 architectural=${run_guest_insns} observed=${run_guest_insns} stop_unobserved=0 reconciled=1 paths=[1-9][0-9]*/" "$log" || {
        echo "$label has no exact measured native retirement denominator" >&2; return 1;
      }
      grep -q '^JITBENCHDIAG ' "$log" || { echo "$label missing dispatcher diagnostics" >&2; return 1; }
    else
      grep -Eq "^JITBENCHCOVERAGE active=0 architectural=${run_guest_insns} observed=$((run_guest_insns - 1)) stop_unobserved=1 reconciled=1 " "$log" || {
        echo "$label has an unexpected interpreter STOP-observer denominator" >&2; return 1;
      }
    fi
  else
    local rate
    rate=$(awk -v n="$run_guest_insns" -v t="$elapsed" 'BEGIN { printf "%.6f", n/t/1000000 }')
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$trial" "$engine" "$elapsed" "$run_guest_insns" "$rate" "$log" >> "$OUTDIR/cold-process.tsv"
    printf 'cold %-6s trial=%s elapsed=%8.3fs rate=%8.3f Minsn/s\n' "$engine" "$trial" "$elapsed" "$rate"
  fi
}

run_warm() {
  local engine=$1
  local home="$OUTDIR/home-${engine}-warm"
  local log="$OUTDIR/${engine}-warm.log"
  local -a engine_env
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
  local rc=0
  timeout "$TIMEOUT_SEC" taskset -c "$BENCH_CPU" env \
    HOME="$home" SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    B2_TEST_HEX="$HEX" B2_TEST_ADDR=0x01001000 \
    B2_TEST_INIT="0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2700" B2_TEST_DUMP=1 \
    B2_TEST_BENCH_REPEATS="$WARM_TRIALS" B2_TEST_BENCH_SENTINEL="$SENTINEL" \
    "${engine_env[@]}" "$BIN" >"$log" 2>&1 || rc=$?
  ((rc == 0)) || { echo "$engine warm series failed rc=$rc (see $log)" >&2; return 1; }
  local lines
  lines=$(grep -c '^JITBENCHWARM ' "$log" || true)
  [[ "$lines" == "$WARM_TRIALS" ]] || {
    echo "$engine warm series returned $lines/$WARM_TRIALS samples" >&2; return 1;
  }
  grep -Eq "REGDUMP: D0=00000000 .*A6=${SENTINEL} .*PC=01001014" "$log" || {
    echo "$engine warm series missed the final sentinel" >&2; return 1;
  }
  awk -v engine="$engine" -v insns="$GUEST_INSNS" -v expected_cycles="$GUEST_CYCLES" -v sentinel="$SENTINEL" -v logfile="$log" '
    /^JITBENCHWARM / {
      sample=active=ns=cycles=valid=d0=a6=pc=""
      for (i=1; i<=NF; i++) {
        split($i, kv, "=")
        if (kv[1]=="sample") sample=kv[2]
        else if (kv[1]=="active") active=kv[2]
        else if (kv[1]=="elapsed_ns") ns=kv[2]
        else if (kv[1]=="cycles") cycles=kv[2]
        else if (kv[1]=="valid") valid=kv[2]
        else if (kv[1]=="d0") d0=kv[2]
        else if (kv[1]=="a6") a6=kv[2]
        else if (kv[1]=="pc") pc=kv[2]
      }
      want_active=(engine == "jit" ? 1 : 0)
      if (valid != 1 || cycles != expected_cycles || active != want_active ||
          d0 != "00000000" || a6 != sentinel || pc != "01001014") exit 3
      seconds=ns/1000000000
      printf "%s\t%s\t%.9f\t%s\t%.6f\t%s\n", sample, engine, seconds, insns, insns/seconds/1000000, logfile
    }' "$log" >> "$OUTDIR/warm-in-process.tsv" || {
      echo "$engine warm samples failed cycle/sentinel validation" >&2; return 1;
    }
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

# Warm timings exclude process/config/device initialization. Each engine gets
# one process-local warmup run (not recorded), followed by register/SR/PC replay
# samples that preserve process, device phase and JIT cache. This register-only,
# interrupt-masked loop mutates no guest memory; this is not a machine snapshot.
run_warm interp
run_warm jit

# One separate, fully observed run per engine. These are coverage evidence, not timing samples.
run_one interp report "yes"
run_one jit report "yes"

emit_stats() {
  local table=$1 scope=$2 engine=$3
  local -a values
  mapfile -t values < <(awk -F '\t' -v e="$engine" 'NR > 1 && $2 == e { print $3 }' "$table" | sort -n)
  local n=${#values[@]}
  ((n > 0)) || { echo "no $scope $engine samples" >&2; return 1; }
  local median
  if ((n % 2)); then
    median=${values[n / 2]}
  else
    median=$(awk -v a="${values[n / 2 - 1]}" -v b="${values[n / 2]}" 'BEGIN { printf "%.9f", (a+b)/2 }')
  fi
  local mean
  mean=$(printf '%s\n' "${values[@]}" | awk '{s+=$1} END {printf "%.9f", s/NR}')
  printf '%s_%s_n=%s\n' "$scope" "$engine" "$n"
  printf '%s_%s_median_s=%s\n' "$scope" "$engine" "$median"
  printf '%s_%s_mean_s=%s\n' "$scope" "$engine" "$mean"
  printf '%s_%s_min_s=%s\n' "$scope" "$engine" "${values[0]}"
  printf '%s_%s_max_s=%s\n' "$scope" "$engine" "${values[n - 1]}"
}
{
  emit_stats "$OUTDIR/cold-process.tsv" cold_process interp
  emit_stats "$OUTDIR/cold-process.tsv" cold_process jit
  emit_stats "$OUTDIR/warm-in-process.tsv" warm_in_process interp
  emit_stats "$OUTDIR/warm-in-process.tsv" warm_in_process jit
} > "$OUTDIR/summary.env"
cold_interp_median=$(awk -F= '$1=="cold_process_interp_median_s"{print $2}' "$OUTDIR/summary.env")
cold_jit_median=$(awk -F= '$1=="cold_process_jit_median_s"{print $2}' "$OUTDIR/summary.env")
warm_interp_median=$(awk -F= '$1=="warm_in_process_interp_median_s"{print $2}' "$OUTDIR/summary.env")
warm_jit_median=$(awk -F= '$1=="warm_in_process_jit_median_s"{print $2}' "$OUTDIR/summary.env")
cold_ratio=$(awk -v i="$cold_interp_median" -v j="$cold_jit_median" 'BEGIN { printf "%.6f", i/j }')
warm_ratio=$(awk -v i="$warm_interp_median" -v j="$warm_jit_median" 'BEGIN { printf "%.6f", i/j }')
printf 'cold_process_speedup_interp_over_jit=%s\n' "$cold_ratio" >> "$OUTDIR/summary.env"
printf 'warm_in_process_speedup_interp_over_jit=%s\n' "$warm_ratio" >> "$OUTDIR/summary.env"

# Preserve the machine-readable final snapshots beside the raw logs.
grep -E '^(JITHELPERCENSUS tag=final |JITBENCHCOVERAGE )' "$OUTDIR/interp-report-census.log" > "$OUTDIR/interp-census.txt"
grep -E '^(JITHELPERCENSUS tag=final |JITBENCHCOVERAGE |JITIDENT |JITBENCHDIAG )' "$OUTDIR/jit-report-census.log" > "$OUTDIR/jit-census.txt"

printf '\nResult: cold process speedup = %sx; warm in-process speedup = %sx\nArtifacts: %s\n' "$cold_ratio" "$warm_ratio" "$OUTDIR"
cat "$OUTDIR/summary.env"
