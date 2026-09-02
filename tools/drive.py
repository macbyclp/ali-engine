#!/usr/bin/env python3
"""Minimal driver for ali-engine's JSON line protocol.

Usage:
    python tools/drive.py [path-to-engine.exe]

Spawns a large grid to exercise frustum culling + GPU instancing, screenshots,
prints render stats. A stand-in for whatever AI agent will drive the engine.
"""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENGINE = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "Debug" / "engine.exe"


def main() -> int:
    if not ENGINE.exists():
        print(f"engine not found: {ENGINE}", file=sys.stderr)
        return 1

    proc = subprocess.Popen(
        [str(ENGINE), "--headless", "--width", "960", "--height", "540"],
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
    call("entity.spawn", name="floor", primitive="plane", scale=[60, 1, 60],
         base_color=[0.2, 0.22, 0.25])

    # a 45x45 grid of cubes (2025 entities) -> one instanced draw call after culling
    n, spacing = 45, 2.2
    for i in range(n):
        for j in range(n):
            x = (i - n / 2) * spacing
            z = (j - n / 2) * spacing
            hue = 0.3 + 0.5 * ((i + j) % 5) / 5
            call("entity.spawn", name=f"c_{i}_{j}", primitive="cube",
                 position=[x, 0.5, z], rotation=[0, (i * j) % 90, 0],
                 base_color=[0.8, hue, 0.35])

    call("light.set", name="sun", direction=[-0.4, -1, -0.3], intensity=3.0)
    call("camera.set", position=[6, 5, 14], target=[0, 0, 0], fov_deg=60)

    call("observe.screenshot", path=str(ROOT / "screenshots" / "drive.png"))
    print("stats (camera low, most culled):", call("observe.stats")["result"])

    call("camera.set", position=[0, 90, 0.1], target=[0, 0, 0], fov_deg=70)
    call("observe.screenshot", path=str(ROOT / "screenshots" / "drive_top.png"))
    print("stats (top-down, most visible):", call("observe.stats")["result"])

    call("scene.save", path=str(ROOT / "scenes" / "generated.json"))
    call("quit")
    proc.wait(timeout=5)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
