#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ASSET_ROOT="/workspace/assets/previous"
CONFIG="${1:-$ASSET_ROOT/configs/previous-example.cfg}"
BIN="${PREVIOUS_BIN:-$ROOT/build-vnc/src/Previous}"
PORT="${PREVIOUS_VNC_PORT:-5999}"
OUTDIR="${PREVIOUS_SMOKE_OUTDIR:-/workspace/tmp/previous-vnc-smoke-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUTDIR"

pick_display() {
  local n
  for n in $(seq 99 180); do
    if [[ ! -e "/tmp/.X${n}-lock" && ! -S "/tmp/.X11-unix/X${n}" ]]; then
      echo ":$n"
      return 0
    fi
  done
  echo ":199"
}

cleanup() {
  set +e
  if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
    kill "$EMU_PID" 2>/dev/null || true
    sleep 0.5
    kill -KILL "$EMU_PID" 2>/dev/null || true
  fi
  if [[ -n "${XVFB_PID:-}" ]] && kill -0 "$XVFB_PID" 2>/dev/null; then
    kill "$XVFB_PID" 2>/dev/null || true
    wait "$XVFB_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

if [[ ! -x "$BIN" ]]; then
  echo "error: Previous binary not found at $BIN" >&2
  exit 1
fi

if [[ ! -f "$CONFIG" ]]; then
  echo "error: config file not found at $CONFIG" >&2
  exit 1
fi

DISPLAY_NAME="$(pick_display)"
Xvfb "$DISPLAY_NAME" -screen 0 1280x900x24 >"$OUTDIR/xvfb.log" 2>&1 &
XVFB_PID=$!
sleep 1

PREVIOUS_VNC=1 PREVIOUS_VNC_PORT="$PORT" DISPLAY="$DISPLAY_NAME" \
  "$BIN" --configfile "$CONFIG" >"$OUTDIR/previous.log" 2>&1 &
EMU_PID=$!

sleep 2
ss -ltnp >"$OUTDIR/ss.txt" 2>&1 || true
DISPLAY="$DISPLAY_NAME" xwininfo -root -tree >"$OUTDIR/xwin_tree.txt" 2>"$OUTDIR/xwininfo.err" || true
WIN_ID="$(awk '/Previous/ {print $1; exit}' "$OUTDIR/xwin_tree.txt")"

convert_xwd() {
  local src="$1"
  local dst="$2"
  if command -v xwdtopnm >/dev/null 2>&1 && command -v pnmtopng >/dev/null 2>&1; then
    xwdtopnm "$src" | pnmtopng > "$dst"
    return 0
  fi
  if command -v ffmpeg >/dev/null 2>&1; then
    ffmpeg -y -v error -f xwd -i "$src" -frames:v 1 "$dst" >/dev/null 2>&1
    return 0
  fi
  return 1
}

if [[ -n "$WIN_ID" ]]; then
  DISPLAY="$DISPLAY_NAME" xwd -silent -id "$WIN_ID" -out "$OUTDIR/window.xwd" >"$OUTDIR/xwd_window.log" 2>&1 || true
  convert_xwd "$OUTDIR/window.xwd" "$OUTDIR/window.png" >"$OUTDIR/convert-window.log" 2>&1 || true
fi

DISPLAY="$DISPLAY_NAME" xwd -silent -root -out "$OUTDIR/root.xwd" >"$OUTDIR/xwd_root.log" 2>&1 || true
convert_xwd "$OUTDIR/root.xwd" "$OUTDIR/root.png" >"$OUTDIR/convert-root.log" 2>&1 || true

echo "display=$DISPLAY_NAME" | tee "$OUTDIR/result.env"
echo "port=$PORT" | tee -a "$OUTDIR/result.env"
echo "window_id=${WIN_ID:-}" | tee -a "$OUTDIR/result.env"
echo "binary=$BIN" | tee -a "$OUTDIR/result.env"
echo "config=$CONFIG" | tee -a "$OUTDIR/result.env"
if rg -q ":$PORT\b" "$OUTDIR/ss.txt"; then
  echo "vnc_listening=1" | tee -a "$OUTDIR/result.env"
else
  echo "vnc_listening=0" | tee -a "$OUTDIR/result.env"
fi

if [[ -f "$OUTDIR/window.png" ]]; then
  echo "screenshot=$OUTDIR/window.png" | tee -a "$OUTDIR/result.env"
elif [[ -f "$OUTDIR/root.png" ]]; then
  echo "screenshot=$OUTDIR/root.png" | tee -a "$OUTDIR/result.env"
fi

echo "$OUTDIR"
