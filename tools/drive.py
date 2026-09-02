#!/usr/bin/env python3
"""Minimal driver for ali-engine's JSON line protocol.

Usage:
    python tools/drive.py [path-to-engine.exe]

Builds a scene driven by data-only Behavior rules, simulates it, screenshots.
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
                continue
            return msg

    call("ping")
    call("scene.reset")

    call("entity.spawn", name="floor", primitive="plane", scale=[12, 1, 12],
         base_color=[0.22, 0.24, 0.27], body={"type": "static"})

    # a spinning kinematic platform (behaviour: constant spin on tick)
    call("entity.spawn", name="platform", primitive="cube", position=[0, 1, 0],
         scale=[4, 0.3, 4], base_color=[0.4, 0.45, 0.5],
         body={"type": "kinematic"})
    call("behavior.set", name="platform", behaviors=[
        {"on": "tick", "do": [{"action": "spin", "axis": [0, 1, 0], "speed_deg": 60}]}
    ])

    # a box that gets kicked at start and reddens when it hits something
    call("entity.spawn", name="puck", primitive="cube", position=[0, 1.6, 0],
         base_color=[0.3, 0.7, 0.4],
         body={"type": "dynamic", "mass": 1.0, "restitution": 0.5})
    call("behavior.set", name="puck", behaviors=[
        {"on": "start", "do": [{"action": "impulse", "impulse": [4, 3, 1.5]}]},
        {"on": "collision", "do": [{"action": "setColor", "color": [0.9, 0.25, 0.2]}]},
    ])

    # an emitter: every time it receives "burst", it spawns a ball above the platform
    call("entity.spawn", name="emitter", primitive="sphere", position=[0, 4, 0],
         base_color=[0.9, 0.8, 0.3], metallic=1.0, roughness=0.2)
    call("behavior.set", name="emitter", behaviors=[
        {"on": "event", "name": "burst", "do": [
            {"action": "spawn", "primitive": "sphere", "position": [0, 5, 0],
             "base_color": [0.5, 0.6, 0.95],
             "body": {"type": "dynamic", "mass": 0.5, "restitution": 0.6}}
        ]}
    ])

    call("light.set", name="sun", direction=[-0.5, -1, -0.35], intensity=3.2)
    call("camera.set", position=[8, 6, 10], target=[0, 1.5, 0], fov_deg=55)

    # simulate, emitting a burst partway through
    call("world.step", dt=1 / 120, steps=120, substeps=2)
    call("event.emit", event="burst")
    call("world.step", dt=1 / 120, steps=120, substeps=2)
    call("event.emit", event="burst")
    call("world.step", dt=1 / 120, steps=180, substeps=2)

    print("entities:", call("entity.list")["result"]["names"])
    for ent in call("scene.state")["result"]["entities"]:
        if ent["name"] == "puck":
            print("puck color:", ent["mesh"]["base_color"],
                  " pos:", ent["transform"]["position"])

    shot = call("observe.screenshot", path=str(ROOT / "screenshots" / "drive.png"))
    print("screenshot:", shot)
    call("scene.save", path=str(ROOT / "scenes" / "generated.json"))
    call("quit")
    proc.wait(timeout=5)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
