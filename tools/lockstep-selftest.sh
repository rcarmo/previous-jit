#!/usr/bin/env bash
# tools/lockstep-selftest.sh — §11.2/§14 trustworthiness probe for the JIT
# lockstep differential tracer.
#
# IMPORTANT: the lockstep DUT hook lives inside compile_block. Opcode-test mode
# (B2_TEST_HEX) runs injected ROM-region code via the exec_normal fallback, NOT
# compile_block, so the hook NEVER arms there (verified: no JITPCHIT/LOCKSTEP
# lines). The hook only arms during a real compiled boot. This wrapper therefore
# drives a bounded NeXTSTEP boot (fg-verify-window.sh) with the lockstep window
# armed over an early-ROM region, and reports the per-op LSDBG/LOCKSTEP trace.
#
# Default window is the c74 region that produced the historical RED gate so the
# before/after is direct. Override via env.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WIN="${LOCKSTEP_WIN:-0x01002400-0x01002700}"
MAXSTEPS="${LOCKSTEP_MAXSTEPS:-6}"
BOOT_SECONDS="${LOCKSTEP_BOOT_SECONDS:-20}"
VERIFY_AWAY="${LOCKSTEP_VERIFY_RANGE:-0x0409f500-0x0409f600}"  # keep VERIFY_BLOCKS off the lockstep window
LOG="${LOCKSTEP_LOG:-/workspace/tmp/lockstep-selftest-$(date +%Y%m%d-%H%M%S).log}"

export B2_JIT_LOCKSTEP_PCS="$WIN"
export B2_JIT_LOCKSTEP_MAXSTEPS="$MAXSTEPS"
export B2_JIT_LOCKSTEP_NOBCC=1
export B2_JIT_LOCKSTEP_DEBUG="${LOCKSTEP_DEBUG:-1}"
export SAVE_LOG="$LOG"
export KEEP_LOG=1

echo "lockstep self-test: window=$WIN maxsteps=$MAXSTEPS boot=${BOOT_SECONDS}s -> $LOG"
timeout $((BOOT_SECONDS * 4 + 30)) "$ROOT/tools/fg-verify-window.sh" "$VERIFY_AWAY" "$BOOT_SECONDS" >/dev/null 2>&1 || true

echo "=== LSDBG_GOLD / LOCKSTEP ==="
grep -E 'LSDBG_GOLD|LOCKSTEP_' "$LOG" | head -40 || echo "(no lockstep output — window may not have armed)"

if grep -q 'LOCKSTEP_OK' "$LOG"; then
  echo "GATE=GREEN (LOCKSTEP_OK on a straight-line block)"; exit 0
elif grep -q 'field=ccr' "$LOG" && ! grep -qE 'field=d[0-7]|field=a[0-7]' "$LOG"; then
  echo "GATE=AMBER (registers lockstep clean; only ccr diverges — DUT dead-flag capture, see spec §14)"; exit 4
elif grep -q 'LOCKSTEP_DIVERGE' "$LOG"; then
  echo "GATE=RED (register/next-pc divergence — investigate before trusting)"; exit 2
else
  echo "GATE=INCONCLUSIVE (no compare; window did not arm / no straight-line run)"; exit 3
fi
