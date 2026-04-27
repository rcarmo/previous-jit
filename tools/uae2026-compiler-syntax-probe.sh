#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUTDIR="${PREVIOUS_UAE2026_SYNTAX_OUTDIR:-/workspace/tmp/previous-uae2026-syntax-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUTDIR"

CMD=(
  c++
  -std=gnu++17
  -fsyntax-only
  -DENABLE_EXPERIMENTAL_UAE2026_JIT=1
  -DUSE_JIT=1
  -DJIT=1
  -DUAE=1
  -DCPU_64_BIT=1
  -DCPU_AARCH64=1
  -include "$ROOT/src/cpu/uae2026_compiler_probe_prelude.h"
  -I"$ROOT/build-vnc"
  -I"$ROOT/src/cpu/uae_cpu_2026"
  -I"$ROOT/src"
  -I"$ROOT/src/includes"
  -I"$ROOT/src/debug"
  -I"$ROOT/src/softfloat"
  -I"$ROOT/src/cpu"
  -I/workspace/projects/macemu/BasiliskII/src/CrossPlatform
  -I/usr/include/SDL2
  "$ROOT/src/cpu/uae_cpu_2026/compiler/compemu_support_arm.cpp"
)

set +e
"${CMD[@]}" >"$OUTDIR/stdout.log" 2>"$OUTDIR/stderr.log"
RC=$?
set -e

ERRORS="$OUTDIR/stderr.log"
{
  echo "rc=$RC"
  echo "outdir=$OUTDIR"
  echo "source=$ROOT/src/cpu/uae_cpu_2026/compiler/compemu_support_arm.cpp"
  echo "compiler_prefs_shim_present=$(test -f "$ROOT/src/cpu/uae2026_compiler_prefs_shim.cpp" && echo 1 || echo 0)"
  echo "bridge_present=$(test -f "$ROOT/src/cpu/uae2026_jit_bridge.cpp" && echo 1 || echo 0)"
  echo "probe_prelude_present=$(test -f "$ROOT/src/cpu/uae2026_compiler_probe_prelude.h" && echo 1 || echo 0)"
  echo "missing_instruction_pc=$(grep -c "instruction_pc" "$ERRORS" || true)"
  echo "missing_memory_globals=$(grep -Ec "RAMBaseHost|RAMSize|ROMSize|ROMBaseMac|ROMBaseHost|MEMBaseDiff" "$ERRORS" || true)"
  echo "missing_flag_nzcv=$(grep -c "regflags.*nzcv\|member named 'nzcv'" "$ERRORS" || true)"
  echo "opcode_cflow_mismatch=$(grep -Ec "fl_const_jump|fl_trap|cflow" "$ERRORS" || true)"
  echo "flush_icache_conflict=$(grep -c "flush_icache" "$ERRORS" || true)"
} >"$OUTDIR/result.env"

cat "$OUTDIR/result.env"
echo "--- first 120 error lines ---"
sed -n '1,120p' "$ERRORS"
