#!/usr/bin/env bash
# Bounded interpreter/JIT timing-semantic differential. Terminates on a
# guest-driven SCSI transaction count, never on a wall-time performance claim.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${PREVIOUS_BIN:-$ROOT/build-vnc/src/Previous}"
ASSET_ROOT="${PREVIOUS_ASSET_ROOT:-/workspace/assets/previous}"
ANCHOR_IO="${ANCHOR_IO:-256}"
CYCARM_LIMIT="${CYCARM_LIMIT:-400}"
TIMEOUT_SEC="${TIMEOUT_SEC:-300}"
BENCH_CPU="${BENCH_CPU:-11}"
MAX_COUNTER_DELTA_PPM="${MAX_COUNTER_DELTA_PPM:-100}"
OUTDIR="${OUTDIR:-/workspace/tmp/jit-timing-anchor-$(date +%Y%m%d-%H%M%S)}"
LOCK_FILE="${BENCH_LOCK_FILE:-/run/lock/previous-jit-benchmark.lock}"

[[ -x "$BIN" ]] || { echo "missing Previous binary: $BIN" >&2; exit 2; }
[[ "$ANCHOR_IO" =~ ^[1-9][0-9]*$ ]] || { echo "ANCHOR_IO must be positive" >&2; exit 2; }
[[ "$CYCARM_LIMIT" =~ ^[1-9][0-9]*$ ]] || { echo "CYCARM_LIMIT must be positive" >&2; exit 2; }
[[ "$BENCH_CPU" =~ ^[0-9]+$ ]] || { echo "BENCH_CPU must be a CPU number" >&2; exit 2; }
[[ "$MAX_COUNTER_DELTA_PPM" =~ ^[0-9]+$ ]] || { echo "MAX_COUNTER_DELTA_PPM must be non-negative" >&2; exit 2; }
[[ -z "$(git -C "$ROOT" status --porcelain)" ]] || {
  echo "refusing timing anchor from dirty source tree" >&2; exit 2;
}
command -v flock >/dev/null || { echo "flock is required" >&2; exit 2; }
command -v taskset >/dev/null || { echo "taskset is required" >&2; exit 2; }
mkdir -p "$OUTDIR"

SOURCE_IMAGE=$(ls -dt "$ASSET_ROOT"/images/nextstep33-system-en-backup-*.img 2>/dev/null | head -1 || true)
[[ -n "$SOURCE_IMAGE" && -f "$SOURCE_IMAGE" ]] || SOURCE_IMAGE="$ASSET_ROOT/images/nextstep33-system-en.img"
[[ -f "$SOURCE_IMAGE" ]] || { echo "missing source disk image" >&2; exit 2; }

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

run_engine() {
  local engine=$1 home="$OUTDIR/home-$1" log="$OUTDIR/$1.log"
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
    PREVIOUS_RTC_UNIX_TIME=0x2ec46472 \
    B2_SCSI_TRACE=1 B2_SCSI_TRACE_STOP_AT="$ANCHOR_IO" \
    B2_CYCINT_TRACE="$CYCARM_LIMIT" B2_JIT_GUEST_PATH=1 \
    "${engine_env[@]}" "$BIN" >"$log" 2>&1 || rc=$?
  ((rc == 0)) || { echo "$engine anchor failed rc=$rc (see $log)" >&2; return 1; }
  [[ "$(grep -c '^SCSIIO ' "$log")" == "$ANCHOR_IO" ]] || {
    echo "$engine did not emit exactly $ANCHOR_IO SCSI tuples" >&2; return 1;
  }
  grep -Eq "^TIMINGANCHOR io=${ANCHOR_IO} active=$([[ $engine == jit ]] && echo 1 || echo 0) " "$log" || {
    echo "$engine missing exact timing anchor" >&2; return 1;
  }
  grep -q '^TIMINGANCHOR_CYCINT ' "$log" || { echo "$engine missing CycInt anchor" >&2; return 1; }
  [[ "$(grep -c '^CYCARM ' "$log")" == "$CYCARM_LIMIT" ]] || {
    echo "$engine did not emit exactly $CYCARM_LIMIT CycInt arm tuples" >&2; return 1;
  }
  grep '^SCSIIO ' "$log" > "$OUTDIR/$engine-scsi.txt"
  grep '^CYCARM ' "$log" | sed -E \
    's/^CYCARM [0-9]+ handler=([0-9]+) cycles=([0-9]+).*/\1 \2/' \
    > "$OUTDIR/$engine-cycarm-sequence.txt"
  grep -E '^(TIMINGANCHOR |TIMINGANCHOR_CYCINT )' "$log" > "$OUTDIR/$engine-anchor.txt"
}

SHA=$(git -C "$ROOT" rev-parse HEAD)
BIN_SHA256=$(sha256sum "$BIN" | awk '{print $1}')
{
  printf 'timestamp_utc=%s\nhost=%s\nsha=%s\n' "$(date -u +%FT%TZ)" "$(hostname)" "$SHA"
  printf 'binary=%s\nbinary_sha256=%s\n' "$BIN" "$BIN_SHA256"
  printf 'source_image=%s\nsource_image_sha256=%s\n' "$SOURCE_IMAGE" "$(sha256sum "$SOURCE_IMAGE" | awk '{print $1}')"
  printf 'anchor_io=%s\ncycarm_limit=%s\nbench_cpu=%s\n' "$ANCHOR_IO" "$CYCARM_LIMIT" "$BENCH_CPU"
  printf 'cpu_policy=%s\npolicy_cpus=%s\ncpu_governor=%s\nfixed_frequency_khz=%s\n' \
    "$CPU_POLICY" "$(<"$CPU_POLICY/related_cpus")" "$(<"$CPU_POLICY/scaling_governor")" \
    "$(<"$CPU_POLICY/scaling_min_freq")"
} > "$OUTDIR/manifest.env"

run_engine interp
run_engine jit
cmp -s "$OUTDIR/interp-scsi.txt" "$OUTDIR/jit-scsi.txt" || {
  diff -u "$OUTDIR/interp-scsi.txt" "$OUTDIR/jit-scsi.txt" > "$OUTDIR/scsi.diff" || true
  echo "SCSI tuple stream diverged (see $OUTDIR/scsi.diff)" >&2; exit 1;
}
cmp -s "$OUTDIR/interp-cycarm-sequence.txt" "$OUTDIR/jit-cycarm-sequence.txt" || {
  diff -u "$OUTDIR/interp-cycarm-sequence.txt" "$OUTDIR/jit-cycarm-sequence.txt" > "$OUTDIR/cycarm-sequence.diff" || true
  echo "CycInt handler/delay sequence diverged" >&2; exit 1;
}

field() { sed -nE "s/.* $2=([^ ]+).*/\\1/p" "$OUTDIR/$1-anchor.txt" | head -1; }
interp_cycles=$(field interp cycles)
jit_cycles=$(field jit cycles)
interp_exceptions=$(field interp exceptions)
jit_exceptions=$(field jit exceptions)
interp_retire=$(field interp retire | awk -F/ '{print $1+$2+$3+$4}')
jit_retire=$(field jit retire | awk -F/ '{print $1+$2+$3+$4}')
interp_pc=$(field interp pc)
jit_pc=$(field jit pc)
interp_cycint=$(grep '^TIMINGANCHOR_CYCINT ' "$OUTDIR/interp-anchor.txt")
jit_cycint=$(grep '^TIMINGANCHOR_CYCINT ' "$OUTDIR/jit-anchor.txt")
cycint_shape() { printf '%s\n' "$1" | sed -E 's/ ptime=[^ ]+//'; }
interp_ptime=$(printf '%s\n' "$interp_cycint" | sed -nE 's/.* ptime=([^ ]+).*/\1/p')
jit_ptime=$(printf '%s\n' "$jit_cycint" | sed -nE 's/.* ptime=([^ ]+).*/\1/p')
[[ "$interp_exceptions" == "$jit_exceptions" ]] || { echo "exception count differs" >&2; exit 1; }
# This stop executes inside asynchronous SCSI service. The complete SCSI tuple
# is the guest-driven coordinate; the interrupted CPU PC may be a different
# instruction in the same polling/transfer path when block boundaries differ.
[[ "$(cycint_shape "$interp_cycint")" == "$(cycint_shape "$jit_cycint")" ]] || {
  echo "CycInt active/type or arm/fire counts differ" >&2; exit 1;
}

calc_delta_ppm() {
  awk -v a="$1" -v b="$2" 'BEGIN { d=a-b; if (d<0) d=-d; base=(a>b?a:b); printf "%d", base ? d*1000000/base : 0 }'
}
cycle_delta_ppm=$(calc_delta_ppm "$interp_cycles" "$jit_cycles")
retire_delta_ppm=$(calc_delta_ppm "$interp_retire" "$jit_retire")
((cycle_delta_ppm <= MAX_COUNTER_DELTA_PPM)) || { echo "cycle delta ${cycle_delta_ppm}ppm exceeds limit" >&2; exit 1; }
((retire_delta_ppm <= MAX_COUNTER_DELTA_PPM)) || { echo "retirement delta ${retire_delta_ppm}ppm exceeds limit" >&2; exit 1; }

{
  printf 'scsi_stream_equal=1\ncycarm_handler_delay_equal=1\ncycint_counts_equal=1\n'
  printf 'exceptions_equal=1\n'
  printf 'interp_cycles=%s\njit_cycles=%s\ncycle_delta_ppm=%s\n' "$interp_cycles" "$jit_cycles" "$cycle_delta_ppm"
  printf 'interp_retirements=%s\njit_retirements=%s\nretirement_delta_ppm=%s\n' "$interp_retire" "$jit_retire" "$retire_delta_ppm"
  printf 'exceptions=%s\ninterp_pc=%s\njit_pc=%s\n' "$interp_exceptions" "$interp_pc" "$jit_pc"
  printf 'interp_pending_time=%s\njit_pending_time=%s\n' "$interp_ptime" "$jit_ptime"
} > "$OUTDIR/summary.env"

printf 'Timing anchor passed at %s SCSI transactions (cycle delta %s ppm; retirement delta %s ppm)\n' \
  "$ANCHOR_IO" "$cycle_delta_ppm" "$retire_delta_ppm"
printf 'Artifacts: %s\n' "$OUTDIR"
cat "$OUTDIR/summary.env"
