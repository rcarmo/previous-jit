#!/usr/bin/env bash
# STAGED peel-trace probe (2026-07-02) — decides bounded-vs-diffuse for the barrier-drop
# init-skip pursue. DO NOT RUN until Rui greenlights the pursue decider.
#
# Method: forward PC-trace differential. Run DEFAULT (boots, reaches init 0x01000c04)
# and BARRIER-DROP (B2_JIT_DROP_BCC_PROD=1, peels to the poll loop before init) with
# B2_JIT_PCTRACE scoped to the boot-code window. Diff the ordered PC streams to find the
# FIRST divergent control transfer (the peel point) and count how many distinct PCs are
# divergence sources.
#   one divergent transfer  -> BOUNDED  -> scoped fix for native-dominant
#   a cluster of transfers  -> DIFFUSE  -> shelve confirmed
# Only the trace decides; the three lean-shelve signals are an unmeasured prior.
#
# Usage: PREVIOUS_VNC_PORT=6090 tools/peel-trace.sh [win_start] [win_end] [count] [timeout]
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WIN_START="${1:-0x01000000}"
WIN_END="${2:-0x01009000}"
COUNT="${3:-3000000}"
TIMEOUT="${4:-110}"
OUTBASE="/workspace/tmp/peel-trace-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUTBASE"

run_cfg() {
  local tag="$1" drop="$2" port="$3"
  local out="$OUTBASE/$tag"
  export B2_JIT_SUCC_LOG=1
  export B2_JIT_PCTRACE="$COUNT"
  export B2_TRACE_PC_START="$WIN_START"
  export B2_TRACE_PC_END="$WIN_END"
  [ "$drop" = "1" ] && export B2_JIT_DROP_BCC_PROD=1 || unset B2_JIT_DROP_BCC_PROD
  export PREVIOUS_BOOT_WAIT=25 PREVIOUS_FSCK_WAIT=10 PREVIOUS_DESKTOP_TIMEOUT=70 PREVIOUS_DESKTOP_POLL=20
  export PREVIOUS_VNC_PORT="$port"
  export PREVIOUS_HEADLESS_OUTDIR="$out"
  rm -rf "$out"
  timeout "$((TIMEOUT+20))" bash "$ROOT/tools/headless-nextstep-harness.sh" >/dev/null 2>&1
  # ordered PC stream (step pc), de-noised
  grep '^PCTRACE ' "$out/previous.log" 2>/dev/null | awk '{print $2, $3}' > "$out/pcstream.txt"
  echo "$tag: $(wc -l < "$out/pcstream.txt" 2>/dev/null) PCTRACE steps"
}

echo "=== peel-trace: window $WIN_START-$WIN_END count $COUNT ==="
run_cfg default 0 "${PREVIOUS_VNC_PORT:-6090}"
run_cfg drop    1 "$(( ${PREVIOUS_VNC_PORT:-6090} + 1 ))"

# Align the two ordered PC sequences (2nd column = pc) and find first divergence.
awk '{print $2}' "$OUTBASE/default/pcstream.txt" > "$OUTBASE/default.pc"
awk '{print $2}' "$OUTBASE/drop/pcstream.txt"    > "$OUTBASE/drop.pc"
echo "=== first divergent PC (peel point) ==="
diff <(cat "$OUTBASE/default.pc") <(cat "$OUTBASE/drop.pc") | grep -E '^[<>]' | head -20
echo "=== distinct divergence-source PCs (bounded=1, diffuse=cluster) ==="
# a divergence source = a drop-side PC that appears where default took a different next-PC
comm -13 <(sort -u "$OUTBASE/default.pc") <(sort -u "$OUTBASE/drop.pc") | head -40 \
  | tee "$OUTBASE/drop-only-pcs.txt" | wc -l
echo "OUTBASE=$OUTBASE"
echo "VERDICT: inspect first-divergent-PC + drop-only count; 1 source = bounded/scoped-fix, cluster = diffuse/shelve"
