#!/usr/bin/env bash
# Run a command that needs a window system: xvfb-run if present, else an existing $DISPLAY,
# else Xephyr (nested), else exit 77 (CTest SKIP). Usage: with_display.sh <cmd> [args...]
if command -v xvfb-run >/dev/null 2>&1; then exec xvfb-run -a "$@"; fi
if [ -n "${DISPLAY:-}" ]; then
  if command -v Xephyr >/dev/null 2>&1; then
    N=$((90 + RANDOM % 9))
    Xephyr ":$N" -screen 1280x800 -ac -br -noreset >/dev/null 2>&1 &
    XP=$!
    sleep 1.5
    DISPLAY=":$N" "$@"; rc=$?
    kill "$XP" 2>/dev/null
    exit $rc
  fi
  exec "$@"
fi
echo "no display / xvfb / Xephyr available -- skipping"; exit 77
