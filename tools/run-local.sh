#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ASSET_ROOT="/workspace/assets/previous"
CONFIG="${1:-$ASSET_ROOT/configs/previous-example.cfg}"
BIN="${PREVIOUS_BIN:-$ROOT/build-clean/src/Previous}"

if [[ ! -x "$BIN" ]]; then
  echo "error: Previous binary not found at $BIN" >&2
  echo "hint: build it first, e.g. under $ROOT/build-clean" >&2
  exit 1
fi

if [[ ! -f "$CONFIG" ]]; then
  echo "error: config file not found at $CONFIG" >&2
  exit 1
fi

missing=0
for p in \
  "$ASSET_ROOT/roms/Rev_1.0_v41.BIN" \
  "$ASSET_ROOT/roms/Rev_2.5_v66.BIN" \
  "$ASSET_ROOT/roms/Rev_3.3_v74.BIN"
  do
  if [[ ! -f "$p" ]]; then
    echo "warning: missing ROM asset: $p" >&2
    missing=1
  fi
done

if [[ ! -e "$ASSET_ROOT/images" ]]; then
  echo "warning: image directory missing: $ASSET_ROOT/images" >&2
  missing=1
fi

if [[ "$missing" -ne 0 ]]; then
  echo "hint: populate /workspace/assets/previous/{roms,images} first" >&2
fi

exec "$BIN" --configfile "$CONFIG"
