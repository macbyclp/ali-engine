#!/usr/bin/env python3
"""Minimal driver for ali-engine's JSON line protocol.

Usage: python tools/drive.py [engine.exe] [gltf_path]

Demonstrates skeletal animation (M9) with the builtin skinned test model,
plus a static prop if a glTF path is given.
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
         base_color_map="builtin:checker", uv_scale=[8, 8], roughness=0.9)

    # three copies of the builtin skinned bar, animation phase-offset by start time
    for i, x in enumerate([-3, 0, 3]):
        call("entity.spawn", name=f"bar{i}", primitive="skinned",
             gltf_path="builtin:bendbar", position=[x, 0, 0],
             base_color=[0.3 + 0.2 * i, 0.6, 0.85 - 0.2 * i],
             animation={"clip": "wave", "speed": 1.0 + 0.3 * i})
        print(f"bar{i} clips:", call("animation.list", name=f"bar{i}")["result"]["clips"])

    if GLTF:
        call("entity.spawn", name="model", primitive="skinned", gltf_path=GLTF,
             position=[0, 0, -4])
        print("model clips:", call("animation.list", name="model")["result"])
        call("animation.play", name="model")

    call("light.set", name="sun", direction=[-0.5, -1, -0.35], intensity=3.2)
    call("camera.set", position=[0, 3.5, 11], target=[0, 2, 0], fov_deg=55)

    call("world.step", dt=1 / 60, steps=40)
    call("observe.screenshot", path=str(ROOT / "screenshots" / "drive.png"))
    call("world.step", dt=1 / 60, steps=30)
    call("observe.screenshot", path=str(ROOT / "screenshots" / "drive_b.png"))
    print("stats:", call("observe.stats")["result"])
    call("scene.save", path=str(ROOT / "scenes" / "generated.json"))
    call("quit")
    proc.wait(timeout=5)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
