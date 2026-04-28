#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${UAE2026_SOURCE_DIR:-/workspace/projects/macemu/BasiliskII/src/uae_cpu_2026}"
GEN_DIR="${UAE2026_GENERATED_DIR:-/workspace/projects/macemu/BasiliskII/src/Unix}"
DST="${UAE2026_DEST_DIR:-$ROOT/src/cpu/uae_cpu_2026}"

if [[ ! -d "$SRC" ]]; then
  echo "uae 2026 source directory not found: $SRC" >&2
  exit 1
fi

rm -rf "$DST"
mkdir -p "$DST"
cp -a "$SRC"/. "$DST"/

cat > "$DST/prefs.h" <<'EOF'
#ifndef PREVIOUS_UAE2026_PREFS_H
#define PREVIOUS_UAE2026_PREFS_H

#include <stdint.h>

bool PrefsFindBool(const char *name);
int32_t PrefsFindInt32(const char *name);

#endif
EOF

cat > "$DST/sysdeps.h" <<'EOF'
#ifndef PREVIOUS_UAE2026_SYSDEPS_WRAPPER_H
#define PREVIOUS_UAE2026_SYSDEPS_WRAPPER_H

#include "../sysdeps.h"

#endif
EOF

if [[ -f "$GEN_DIR/compemu.cpp" ]]; then
  cp "$GEN_DIR/compemu.cpp" "$DST/compiler/compemu.cpp"
fi
if [[ -f "$GEN_DIR/comptbl.h" ]]; then
  cp "$GEN_DIR/comptbl.h" "$DST/compiler/comptbl.h"
fi
if [[ -f "$SRC/compiler/compstbl_arm.cpp" ]]; then
  python3 - "$SRC/compiler/compstbl_arm.cpp" "$DST/compiler/compstbl_arm.cpp" <<'PY'
from pathlib import Path
import sys
src = Path(sys.argv[1]).read_text()
out = Path(sys.argv[2])
marker = 'extern const struct comptbl op_smalltbl_0_comp_ff[] = {'
idx = src.index(marker)
body = src[idx:].rstrip()
if body.endswith('#endif'):
    body = body[:body.rfind('#endif')].rstrip()
body = body.replace('extern const struct comptbl op_smalltbl_0_comp_ff[] = {', 'const struct comptbl op_smalltbl_0_comp_ff[] = {', 1)
body = body.replace('\nextern const struct comptbl op_smalltbl_0_comp_nf[] = {', '\nconst struct comptbl op_smalltbl_0_comp_nf[] = {')
preamble = '''#include "sysdeps.h"\n#include "machdep/m68k.h"\n#include "memory-uae.h"\n#include "readcpu.h"\n#include "newcpu.h"\n#include "comptbl.h"\n#include "debug.h"\n'''
out.write_text(preamble + body + '\n')
PY
fi

count=$(find "$DST" -type f | wc -l | tr -d ' ')
echo "synced_files=$count"
echo "source=$SRC"
echo "generated=$GEN_DIR"
echo "dest=$DST"
