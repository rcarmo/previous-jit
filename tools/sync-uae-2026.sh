#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${UAE2026_SOURCE_DIR:-/workspace/projects/macemu/BasiliskII/src/uae_cpu_2026}"
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

count=$(find "$DST" -type f | wc -l | tr -d ' ')
echo "synced_files=$count"
echo "source=$SRC"
echo "dest=$DST"
