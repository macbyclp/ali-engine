#!/usr/bin/env bash
# Build a Windows x64 release zip from an already-built Release engine.exe.
#   cmake --build build --config Release --target engine
#   bash packaging/pack.sh 0.1.1
set -euo pipefail
cd "$(dirname "$0")/.."

VER="${1:?usage: pack.sh <version>  e.g. pack.sh 0.1.1}"
NAME="ali-engine-v${VER}-win64"
OUT="dist/${NAME}"
EXE="build/Release/engine.exe"

[ -f "$EXE" ] || { echo "missing $EXE -- build Release first"; exit 1; }

rm -rf "$OUT" "dist/${NAME}.zip"
mkdir -p "$OUT/docs" "$OUT/scenes" "$OUT/tools" "$OUT/assets/fonts"

cp "$EXE" "$OUT/"
# VC++ runtime -- pull from the VS redist dir, fall back to the system32 copies
for dll in msvcp140.dll vcruntime140.dll vcruntime140_1.dll; do
    src="$(ls -1 "C:/Program Files/Microsoft Visual Studio/2022/"*/VC/Redist/MSVC/*/x64/*CRT/$dll 2>/dev/null | head -1 || true)"
    [ -n "$src" ] || src="/c/Windows/System32/$dll"
    cp "$src" "$OUT/"
done

cp README.md LICENSE ARCHITECTURE.md "$OUT/"
cp packaging/RELEASE.txt "$OUT/"
cp docs/AI-PROTOCOL.md "$OUT/docs/"
cp scenes/*.json "$OUT/scenes/"
cp tools/drive.py tools/gen_media.py "$OUT/tools/"
cp assets/fonts/LiberationSans-Regular.ttf assets/fonts/LiberationSans-LICENSE.txt "$OUT/assets/fonts/"

( cd dist && powershell -NoProfile -Command "Compress-Archive -Path '${NAME}/*' -DestinationPath '${NAME}.zip' -Force" )
echo "built dist/${NAME}.zip"
( cd dist && du -h "${NAME}.zip" )
