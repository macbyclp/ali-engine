#!/usr/bin/env python3
"""Minimal driver for ali-engine's JSON line protocol.

Usage:
    python tools/drive.py [path-to-engine.exe]

Builds a small physics scene, simulates it, screenshots the result, quits.
A stand-in for whatever AI agent will eventually drive the engine.
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
                print("event:", msg)
                continue
            return msg

    print(call("ping"))
    call("scene.reset")

    # static floor + walls
    call("entity.spawn", name="floor", primitive="plane", scale=[12, 1, 12],
         base_color=[0.22, 0.24, 0.27], body={"type": "static"})

    # a small tower of dynamic boxes + a falling sphere
    for i in range(5):
        call("entity.spawn", name=f"box{i}", primitive="cube",
             position=[0, 0.5 + i * 1.05, 0], rotation=[0, i * 7, 0],
             base_color=[0.85, 0.45 - i * 0.05, 0.3],
             body={"type": "dynamic", "mass": 1.0, "restitution": 0.1})
    call("entity.spawn", name="ball", primitive="sphere", position=[0.4, 8, 0.2],
         base_color=[0.4, 0.6, 0.95], metallic=0.9, roughness=0.25,
         body={"type": "dynamic", "mass": 2.0, "restitution": 0.4})

    call("light.set", name="sun", direction=[-0.5, -1, -0.35], intensity=3.2)
    call("camera.set", position=[7, 5, 9], target=[0, 2, 0], fov_deg=55)

    print("gravity:", call("physics.getGravity"))
    print("raycast down from above:", call("physics.raycast", origin=[0, 10, 0],
                                           direction=[0, -1, 0], max_distance=50))

    # simulate ~2.5 seconds
    call("world.step", dt=1 / 120, steps=300, substeps=2)

    shot = call("observe.screenshot", path=str(ROOT / "screenshots" / "drive.png"))
    print("screenshot:", shot)
    state = call("scene.state")["result"]["entities"]
    for ent in state:
        if ent["name"] == "box0":
            print("box0 after sim:", ent["transform"]["position"])
    call("scene.save", path=str(ROOT / "scenes" / "generated.json"))
    call("quit")
    proc.wait(timeout=5)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
