#!/usr/bin/env python3
"""Minimal driver for ali-engine's JSON line protocol.

Usage:
    python tools/drive.py [path-to-engine.exe]

Builds a material/texture showcase, screenshots it. If a glTF path is given as a
second arg, loads that too. A stand-in for whatever AI agent will drive the engine.
"""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENGINE = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "Debug" / "engine.exe"
GLTF = sys.argv[2] if len(sys.argv) > 2 else None


def main() -> int:
    if not ENGINE.exists():
        print(f"engine not found: {ENGINE}", file=sys.stderr)
        return 1

    proc = subprocess.Popen(
        [str(ENGINE), "--headless", "--width", "1100", "--height", "620"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, cwd=ROOT,
    )

    def call(method, **params):
        proc.stdin.write(json.dumps({"method": method, "params": params}) + "\n")
        proc.stdin.flush()
        while True:
            line = proc.stdout.readline()
            if not line:
                raise RuntimeError("engine closed the connection")
            msg = json.loads(line)
            if "event" in msg:
                continue
            return msg

    call("ping")
    call("scene.reset")

    call("entity.spawn", name="floor", primitive="plane", scale=[14, 1, 14],
         base_color_map="builtin:checker", uv_scale=[8, 8], roughness=0.85)

    call("entity.spawn", name="rough", primitive="sphere", position=[-3, 1.2, 0],
         scale=[1.2, 1.2, 1.2], base_color=[0.85, 0.4, 0.35], roughness=0.9, metallic=0.0)
    call("entity.spawn", name="metal", primitive="sphere", position=[0, 1.2, 0],
         scale=[1.2, 1.2, 1.2], base_color=[0.95, 0.95, 0.98], roughness=0.15, metallic=1.0)
    call("entity.spawn", name="bumped", primitive="sphere", position=[3, 1.2, 0],
         scale=[1.2, 1.2, 1.2], base_color=[0.5, 0.6, 0.85], roughness=0.4,
         normal_map="builtin:bumps")
    call("entity.spawn", name="glow", primitive="cube", position=[0, 0.5, 3.5],
         base_color=[0.1, 0.1, 0.1], emissive=[0.9, 0.4, 1.2])

    if GLTF:
        call("entity.spawn", name="model", primitive="gltf", gltf_path=GLTF,
             position=[0, 0, -3.5])

    call("light.set", name="sun", direction=[-0.5, -1, -0.35], intensity=3.2)
    call("camera.set", position=[0, 4, 9], target=[0, 1, 0], fov_deg=55)

    shot = call("observe.screenshot", path=str(ROOT / "screenshots" / "drive.png"))
    print("screenshot:", shot["result"])
    print("stats:", call("observe.stats")["result"])
    call("scene.save", path=str(ROOT / "scenes" / "generated.json"))
    call("quit")
    proc.wait(timeout=5)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
