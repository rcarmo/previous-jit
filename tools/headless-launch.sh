#!/usr/bin/env bash
# tools/headless-launch.sh DISPLAY VNC_PORT RUNDIR BIN MODE [RESUME_INSNS]
#
# Launches Xvfb + Previous as two transient `systemd-run --user` units so
# they survive shell/tool-call boundaries on the headless host.  Each unit
# owns its own cgroup; the parent process exiting does not kill them.
#
# MODE in {jit, interp, oneshot}; oneshot also takes RESUME_INSNS as $6.
#
# Stop a unit later with `make headless-stop` (or `systemctl --user stop ...`).
set -eu

DISPLAY_NAME=$1
VNC_PORT=$2
RUNDIR=$3
BIN=$4
MODE=$5
RESUME_INSNS=${6:-0}

case "$MODE" in
  jit|interp|oneshot) ;;
  *) echo "headless-launch: MODE must be jit|interp|oneshot, got '$MODE'"; exit 2;;
esac

# Use the display number (e.g. :198 -> 198) as a unit-name suffix so multiple
# runs on different displays coexist.
DISP_SLUG=$(echo "$DISPLAY_NAME" | tr -d ':')
XVFB_UNIT="previous-xvfb-${DISP_SLUG}.service"
PREV_UNIT="previous-emulator-${DISP_SLUG}.service"

# Tear down any stale instance on this display first so the new run owns the
# port/display cleanly.
systemctl --user stop "$XVFB_UNIT" "$PREV_UNIT" 2>/dev/null || true
systemctl --user reset-failed "$XVFB_UNIT" "$PREV_UNIT" 2>/dev/null || true
# Belt and braces: any stray Xvfb/Previous on this display from a prior shell
# run that wasn't started via systemd-run.  Match on argv[0] (basename) only
# so we don't accidentally match the shell process that's executing us with
# $BIN in its command line.
BIN_BASENAME=$(basename "$BIN")
pkill -9 -x Xvfb 2>/dev/null || true
pkill -9 -x "$BIN_BASENAME" 2>/dev/null || true
sleep 1

# Xvfb unit
systemd-run --user --unit="$XVFB_UNIT" \
    --description="Xvfb for headless Previous on ${DISPLAY_NAME}" \
    --setenv=DISPLAY="$DISPLAY_NAME" \
    -p StandardOutput=append:"$RUNDIR/xvfb.log" \
    -p StandardError=append:"$RUNDIR/xvfb.log" \
    -- /usr/bin/Xvfb "$DISPLAY_NAME" -screen 0 1280x900x24
sleep 1

# Previous unit
env_args=(
    --setenv=HOME="$RUNDIR/home"
    --setenv=SDL_AUDIODRIVER=dummy
    --setenv=DISPLAY="$DISPLAY_NAME"
    --setenv=PREVIOUS_VNC=1
    --setenv=PREVIOUS_VNC_PORT="$VNC_PORT"
    --setenv=PREVIOUS_RTC_UNIX_TIME=0x2ec46472
)
case "$MODE" in
  jit)
    # IMPORTANT: With the cpu_model=68000 fix (commit 2779c47), the JIT
    # actually emits native blocks now, which exposes latent UAE-2026
    # compiler bugs in Previous's ROM/kernel code (e.g. "set_status
    # invalid vreg 22").  Until those are debugged, keep the canonical
    # NeXTSTEP boot recipe on the interpreter: bootstrap the JIT cache
    # (needed for the bridge to wire up correctly) but disable runtime
    # JIT dispatch so we don't crash mid-boot.  This matches the
    # behaviour the recipe had *in practice* before the fix, because
    # the JIT was silently a no-op.
    env_args+=(
        --setenv=PREVIOUS_UAE2026_JIT=0
        --setenv=PREVIOUS_UAE2026_JIT_RAM=0
        --setenv=B2_JIT_RTE_FAULT_HANDOFF=1
    );;
  oneshot)
    env_args+=(
        --setenv=PREVIOUS_UAE2026_JIT=1
        --setenv=PREVIOUS_UAE2026_JIT_RAM=1
        --setenv=B2_JIT_RTE_FAULT_HANDOFF=1
        --setenv=B2_JIT_RTE_FAULT_HANDOFF_RESUME_INSNS="$RESUME_INSNS"
    );;
  interp) : ;;
esac

systemd-run --user --unit="$PREV_UNIT" \
    --description="Previous emulator on ${DISPLAY_NAME} (${MODE})" \
    "${env_args[@]}" \
    -p StandardOutput=append:"$RUNDIR/previous.log" \
    -p StandardError=append:"$RUNDIR/previous.log" \
    -- "$BIN"
sleep 2

# Sanity-report the new units.
echo "headless-launch[$MODE]: $XVFB_UNIT $PREV_UNIT  (VNC :$VNC_PORT, log $RUNDIR/previous.log)"
systemctl --user is-active "$XVFB_UNIT" "$PREV_UNIT"
