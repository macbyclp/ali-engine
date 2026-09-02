#!/usr/bin/env python3
"""Minimal driver for ali-engine's JSON line protocol.

Usage:
    python tools/drive.py [path-to-engine.exe]

Spawns the engine headless, builds a small scene, takes a screenshot, quits.
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
    call("entity.spawn", name="floor", primitive="plane", scale=[15, 1, 15],
         base_color=[0.2, 0.22, 0.25])
    call("entity.spawn", name="cube", primitive="cube", position=[0, 1, 0],
         base_color=[0.9, 0.4, 0.3])
    call("entity.spawn", name="ball", primitive="sphere", position=[2, 1, -1],
         base_color=[0.3, 0.6, 0.9])
    call("light.set", name="sun", direction=[-0.5, -1, -0.3], intensity=1.3)
    call("camera.set", position=[6, 4, 8], target=[0, 1, 0], fov_deg=55)
    shot = call("observe.screenshot", path=str(ROOT / "screenshots" / "drive.png"))
    print("screenshot:", shot)
    call("scene.save", path=str(ROOT / "scenes" / "generated.json"))
    call("quit")
    proc.wait(timeout=5)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
