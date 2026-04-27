#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${PREVIOUS_BUILD_DIR:-$ROOT/build-vnc}"
OUTDIR="${PREVIOUS_BRIDGE_SMOKE_OUTDIR:-/workspace/tmp/previous-jit-bridge-smoke-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUTDIR"

cmake -S "$ROOT" -B "$BUILD_DIR" -DENABLE_VNC=ON -DENABLE_EXPERIMENTAL_UAE2026_JIT=ON >"$OUTDIR/cmake-configure.log" 2>&1
cmake --build "$BUILD_DIR" -j"$(nproc)" >"$OUTDIR/cmake-build.log" 2>&1

HARNESS_OUT="$OUTDIR/harness"
mkdir -p "$HARNESS_OUT"

set +e
PREVIOUS_BIN="$BUILD_DIR/src/Previous" \
PREVIOUS_HEADLESS_OUTDIR="$HARNESS_OUT" \
PREVIOUS_UAE2026_JIT=1 \
PREVIOUS_UAE2026_JIT_CACHE_KB="${PREVIOUS_UAE2026_JIT_CACHE_KB:-8192}" \
PREVIOUS_UAE2026_JIT_FPU="${PREVIOUS_UAE2026_JIT_FPU:-0}" \
PREVIOUS_UAE2026_JIT_LAZY_FLUSH="${PREVIOUS_UAE2026_JIT_LAZY_FLUSH:-1}" \
PREVIOUS_UAE2026_JIT_CONST_JUMP="${PREVIOUS_UAE2026_JIT_CONST_JUMP:-1}" \
"$ROOT/tools/headless-nextstep-harness.sh" >"$OUTDIR/harness.log" 2>&1
RC=$?
set -e

PREVIOUS_LOG="$HARNESS_OUT/previous.log"
HARNESS_ENV="$HARNESS_OUT/harness.env"
RESULT_ENV="$HARNESS_OUT/result.env"

bridge_compiled=0
bootstrap_ready=0
bootstrap_active=0
aslr_active=0
desktop_reached=0

if grep -q 'UAE2026 bridge:' "$PREVIOUS_LOG" 2>/dev/null; then
  bridge_compiled=1
fi
if grep -q 'bootstrap_ready=yes' "$PREVIOUS_LOG" 2>/dev/null; then
  bootstrap_ready=1
fi
if grep -q 'bootstrap_active=yes' "$PREVIOUS_LOG" 2>/dev/null; then
  bootstrap_active=1
fi
if grep -q 'aslr=yes' "$PREVIOUS_LOG" 2>/dev/null; then
  aslr_active=1
fi
if [[ -f "$RESULT_ENV" ]] && grep -q '^desktop_reached=1$' "$RESULT_ENV"; then
  desktop_reached=1
fi

cat > "$OUTDIR/result.env" <<EOF
bridge_compiled=$bridge_compiled
bootstrap_ready=$bootstrap_ready
bootstrap_active=$bootstrap_active
aslr_active=$aslr_active
desktop_reached=$desktop_reached
harness_rc=$RC
build_dir=$BUILD_DIR
harness_out=$HARNESS_OUT
previous_log=$PREVIOUS_LOG
harness_env=$HARNESS_ENV
EOF

cat "$OUTDIR/result.env"
echo "METRIC bridge_compiled=$bridge_compiled"
echo "METRIC bootstrap_ready=$bootstrap_ready"
echo "METRIC bootstrap_active=$bootstrap_active"
echo "METRIC aslr_active=$aslr_active"
echo "METRIC desktop_reached=$desktop_reached"
echo "OUTDIR=$OUTDIR"

if [[ "$bridge_compiled" != "1" || "$bootstrap_ready" != "1" || "$bootstrap_active" != "1" || "$desktop_reached" != "1" || "$RC" != "0" ]]; then
  exit 2
fi
