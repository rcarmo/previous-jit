#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUTDIR="${PREVIOUS_MMU_FAST_SMOKE_OUTDIR:-/workspace/tmp/previous-mmu-fast-smoke-$(date +%Y%m%d-%H%M%S)}"
BUILD_DIR="${PREVIOUS_BUILD_DIR:-$ROOT/build-vnc}"
mkdir -p "$OUTDIR"

# Fast pre-boot coverage for RAM/MMU-sensitive JIT paths. This intentionally
# avoids the full NeXT desktop harness; use it as the required gate before long
# boot/stability validation.
MMU_FILTER="${PREVIOUS_MMU_FAST_FILTER:-sr_|scc_|dbvc|dbvs|chk2_|cas|movep_|movem_|moves_|movec_|jsr_|bsr_|seam_}"

cmake -S "$ROOT" -B "$BUILD_DIR" -DENABLE_VNC=ON -DENABLE_EXPERIMENTAL_UAE2026_JIT=ON >"$OUTDIR/cmake-configure.log" 2>&1
cmake --build "$BUILD_DIR" -j"$(nproc)" >"$OUTDIR/cmake-build.log" 2>&1

OPCODE_OUT="$OUTDIR/opcode"
PREVIOUS_OPCODE_HARNESS_OUTDIR="$OPCODE_OUT" \
PREVIOUS_BUILD_DIR="$BUILD_DIR" \
PREVIOUS_OPCODE_FILTER="$MMU_FILTER" \
PREVIOUS_UAE2026_JIT_RAM=1 \
B2_JIT_RTE_FAULT_HANDOFF_DISABLE=1 \
"$ROOT/tools/uae2026-opcode-harness.sh" >"$OUTDIR/opcode.log" 2>&1

# Re-emit the opcode harness metrics as this harness' top-level result.
if [[ ! -f "$OPCODE_OUT/result.env" ]]; then
  echo "missing opcode result: $OPCODE_OUT/result.env" >&2
  exit 2
fi
# shellcheck disable=SC1090
source "$OPCODE_OUT/result.env"

cat > "$OUTDIR/result.env" <<EOF
build_dir=$BUILD_DIR
opcode_out=$OPCODE_OUT
filter=$MMU_FILTER
total=$total
interp_ok=$interp_ok
jit_ok=$jit_ok
pass=$pass
fail=$fail
infra_fail=$infra_fail
score=$score
EOF

cat "$OUTDIR/result.env"
echo "METRIC mmu_fast_total=$total"
echo "METRIC mmu_fast_pass=$pass"
echo "METRIC mmu_fast_fail=$fail"
echo "METRIC mmu_fast_infra_fail=$infra_fail"
echo "METRIC mmu_fast_score=$score"
echo "OUTDIR=$OUTDIR"

if [[ "$fail" -ne 0 || "$infra_fail" -ne 0 || "$score" -ne 100 ]]; then
  echo "--- opcode harness log ---" >&2
  tail -120 "$OUTDIR/opcode.log" >&2 || true
  exit 2
fi
