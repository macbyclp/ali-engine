#!/usr/bin/env bash
# Build a Linux x64 tar.gz from an already-built engine binary.
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target engine
#   bash packaging/pack-linux.sh 0.1.1
set -euo pipefail
cd "$(dirname "$0")/.."

VER="${1:?usage: pack-linux.sh <version>}"
NAME="ali-engine-v${VER}-linux-x64"
OUT="dist/${NAME}"
EXE="build/engine"

[ -f "$EXE" ] || { echo "missing $EXE -- build Release first"; exit 1; }

rm -rf "$OUT" "dist/${NAME}.tar.gz"
mkdir -p "$OUT/plugins"
: > "$OUT/plugins/.keep"
cp "$EXE" "$OUT/"
for d in docs scenes tools assets games clients; do
    [ -d "$d" ] && cp -r "$d" "$OUT/"
done
cp README.md LICENSE "$OUT/" 2>/dev/null || true
cp packaging/RELEASE-linux.txt "$OUT/RELEASE.txt"

tar -C dist -czf "dist/${NAME}.tar.gz" "$NAME"
echo "wrote dist/${NAME}.tar.gz"
