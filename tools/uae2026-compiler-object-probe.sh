#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUTDIR="${PREVIOUS_UAE2026_OBJECT_OUTDIR:-/workspace/tmp/previous-uae2026-object-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUTDIR"

OBJ="$OUTDIR/compemu_support_arm.o"

CMD=(
  c++
  -std=gnu++17
  -c
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
  -o "$OBJ"
)

set +e
"${CMD[@]}" >"$OUTDIR/stdout.log" 2>"$OUTDIR/stderr.log"
RC=$?
set -e

OBJ_SIZE=0
if [[ -f "$OBJ" ]]; then
  OBJ_SIZE=$(stat -c %s "$OBJ")
fi

cat >"$OUTDIR/result.env" <<EOF
rc=$RC
outdir=$OUTDIR
object=$OBJ
object_size=$OBJ_SIZE
EOF

cat "$OUTDIR/result.env"
echo "--- first 120 stderr lines ---"
sed -n '1,120p' "$OUTDIR/stderr.log"

if [[ "$RC" != "0" ]]; then
  exit "$RC"
fi
