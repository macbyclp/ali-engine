#!/usr/bin/env bash
# Offline dev build for a git worktree: reuses the FetchContent sources of the main checkout
# (no network, no re-download). Usage: tools/dev-build.sh [build-dir] [ninja -j N]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MAIN="${ALI_MAIN_CHECKOUT:-$HOME/Belgeler/ali-engine}"
BUILD="${1:-$ROOT/build}"
JOBS="${2:-4}"
ARGS=()
for d in "$MAIN"/build/_deps/*-src; do
  n="$(basename "$d" -src)"
  ARGS+=("-DFETCHCONTENT_SOURCE_DIR_$(echo "$n" | tr a-z A-Z)=$d")
done
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_FULLY_DISCONNECTED=ON "${ARGS[@]}" >/dev/null
cmake --build "$BUILD" -j "$JOBS"
