#!/usr/bin/env bash
# tools/lockstep-sweep.sh — REGONLY post-c74 register/next_pc-ONLY sweep.
#
# Established trust boundary (see docs/jit-lockstep-tracer-spec.md §14):
#   register/next_pc compare = TRUSTWORTHY; CCR compare = advisory-only
#   (stale-capture/dead-flag false positives).
#
# This wrapper arms the lockstep tracer in REGONLY sweep mode
# (B2_JIT_LOCKSTEP_REGONLY=1) over a WIDE window starting PAST the c74 region,
# so the sweep runs to the FIRST real register/next_pc divergence without the
# CCR advisory cap being burned by benign dead-flag noise. c74 itself is proven
# clean (efe336a): all registers + branch-controlling EOR.B N match gold.
#
# Default window: 0x01002700 (just past c74) .. 0x01003200 (past wall ~0x01002cb4).
# Override via LOCKSTEP_WIN / LOCKSTEP_MAXSTEPS / LOCKSTEP_BOOT_SECONDS.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WIN="${LOCKSTEP_WIN:-0x01002700-0x01003200}"
MAXSTEPS="${LOCKSTEP_MAXSTEPS:-200000}"
BOOT_SECONDS="${LOCKSTEP_BOOT_SECONDS:-30}"
VERIFY_AWAY="${LOCKSTEP_VERIFY_RANGE:-0x0409f500-0x0409f600}"  # keep VERIFY_BLOCKS off the sweep window
LOG="${LOCKSTEP_LOG:-/workspace/tmp/lockstep-sweep-$(date +%Y%m%d-%H%M%S).log}"

export B2_JIT_LOCKSTEP_PCS="$WIN"
export B2_JIT_LOCKSTEP_MAXSTEPS="$MAXSTEPS"
export B2_JIT_LOCKSTEP_NOBCC=1
export B2_JIT_LOCKSTEP_REGONLY=1
export B2_JIT_LOCKSTEP_DEBUG="${LOCKSTEP_DEBUG:-1}"
export SAVE_LOG="$LOG"
export KEEP_LOG=1

echo "lockstep REGONLY sweep: window=$WIN maxsteps=$MAXSTEPS boot=${BOOT_SECONDS}s -> $LOG"
timeout $((BOOT_SECONDS * 4 + 30)) "$ROOT/tools/fg-verify-window.sh" "$VERIFY_AWAY" "$BOOT_SECONDS" >/dev/null 2>&1 || true

echo "=== LOCKSTEP trace (first 60) ==="
grep -E 'LSDBG_GOLD|LOCKSTEP_' "$LOG" | head -60 || echo "(no lockstep output — window may not have armed)"

echo "=== REGONLY divergence summary ==="
# In REGONLY mode the CCR path is suppressed, so any LOCKSTEP_DIVERGE is a real
# register/next_pc divergence. field=ccr should NEVER appear here.
if grep -q 'field=ccr' "$LOG"; then
  echo "WARNING: field=ccr seen under REGONLY (should be suppressed) — investigate REGONLY gate"
fi

if grep -q 'LOCKSTEP_DIVERGE' "$LOG"; then
  echo "GATE=RED — FIRST REAL register/next_pc divergence:"
  grep -E 'LOCKSTEP_DIVERGE|field=(d[0-7]|a[0-7]|next_pc|pc)' "$LOG" | head -10
  exit 2
elif grep -q 'LOCKSTEP_OK' "$LOG"; then
  steps=$(grep -c 'LOCKSTEP_OK' "$LOG" || true)
  echo "GATE=GREEN — $steps LOCKSTEP_OK steps, NO register/next_pc divergence in window $WIN"
  echo "=> advance window further past the wall and re-sweep"
  exit 0
else
  echo "GATE=INCONCLUSIVE — window did not arm / no compiled straight-line run in $WIN"
  exit 3
fi
