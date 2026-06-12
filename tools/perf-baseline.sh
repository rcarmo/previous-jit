#!/usr/bin/env bash
# tools/perf-baseline.sh [--quick] OUTDIR CUR_BIN BASE_BIN
#
# Runs the 4-configuration perf-baseline matrix (or 2 with --quick) and emits
# OUTDIR/results.csv plus per-config OUTDIR/<label>.run.log JSONL streams.
set -euo pipefail

QUICK=0
if [[ "${1:-}" == "--quick" ]]; then QUICK=1; shift; fi

OUTDIR=${1:?missing OUTDIR}
CUR_BIN=${2:?missing CUR_BIN}
BASE_BIN=${3:?missing BASE_BIN}

mkdir -p "$OUTDIR"
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
BENCH="$SCRIPT_DIR/perf-baseline-bench.py"
[[ -f "$BENCH" ]] || { echo "missing $BENCH"; exit 2; }

run_one() {
    local label=$1 binary=$2 display=$3 port=$4 jit=$5
    local rundir="$OUTDIR/$label"
    echo "=== $label ==="
    mkdir -p "$rundir/home/.previous"
    [[ -f "$rundir/nextstep33-system-en-run.img" ]] || \
        cp --sparse=always --reflink=auto \
            /workspace/tmp/previous-interactive/nextstep33-system-en-run.img \
            "$rundir/nextstep33-system-en-run.img"
    python3 "$BENCH" \
        --label "$label" --binary "$binary" --display "$display" \
        --vnc-port "$port" --rundir "$rundir" --jit "$jit" \
        --boot-timeout 420 --idle-secs 12 --motion-secs 12 \
        > "$OUTDIR/$label.run.log" 2>&1
    tail -1 "$OUTDIR/$label.run.log"
}

run_one cur-jit  "$CUR_BIN"  :201 5921 on
run_one cur-int  "$CUR_BIN"  :202 5922 off
if [[ "$QUICK" == "0" ]]; then
    run_one base-jit "$BASE_BIN" :203 5923 on
    run_one base-int "$BASE_BIN" :204 5924 off
fi

# Consolidate into CSV
{
    echo "label,boot_sec,idle_bytes_per_sec,idle_updates_per_sec,idle_cpu_pct,motion_bytes_per_sec,motion_updates_per_sec,motion_cpu_pct"
    for label in cur-jit cur-int base-jit base-int; do
        [[ -f "$OUTDIR/$label.run.log" ]] || continue
        python3 - "$OUTDIR/$label.run.log" "$label" <<'PY'
import json, sys
log, label = sys.argv[1], sys.argv[2]
phases = {}
boot = ""
for line in open(log):
    try: j = json.loads(line)
    except: continue
    if j.get('phase') in ('idle','motion'): phases[j['phase']] = j
    elif 'boot_sec' in j and 'phase' not in j: boot = j['boot_sec']
i = phases.get('idle', {}); m = phases.get('motion', {})
print(f"{label},{boot},{i.get('bytes_per_sec',0):.0f},{i.get('updates_per_sec',0):.2f},{i.get('cpu_pct',0):.1f},{m.get('bytes_per_sec',0):.0f},{m.get('updates_per_sec',0):.2f},{m.get('cpu_pct',0):.1f}")
PY
    done
} > "$OUTDIR/results.csv"

echo
echo "=== results ==="
cat "$OUTDIR/results.csv"
echo
echo "CSV: $OUTDIR/results.csv"
