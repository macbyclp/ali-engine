#!/usr/bin/env python3
"""Minimal driver for ali-engine's JSON line protocol.

Usage: python tools/drive.py [engine.exe]

Demonstrates the scene graph (parent/child) and prefabs.
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
    call("entity.spawn", name="floor", primitive="plane", scale=[16, 1, 16],
         base_color_map="builtin:checker", uv_scale=[8, 8], roughness=0.9)

    # --- scene graph: a turret assembled from parented parts ---
    call("entity.spawn", name="turret", primitive="cube", position=[0, 0.4, 0],
         scale=[1.6, 0.8, 1.6], base_color=[0.35, 0.38, 0.42])
    call("entity.spawn", name="turret_head", primitive="cube", position=[0, 0.9, 0],
         scale=[1.0, 0.6, 1.0], base_color=[0.7, 0.5, 0.3], parent="turret")
    call("entity.spawn", name="turret_barrel", primitive="cube",
         position=[0, 0.1, 1.1], scale=[0.18, 0.18, 1.6],
         base_color=[0.2, 0.2, 0.25], parent="turret_head")
    # rotate only the head; the barrel follows because it's a child
    call("behavior.set", name="turret_head", behaviors=[
        {"on": "tick", "do": [{"action": "spin", "axis": [0, 1, 0], "speed_deg": 45}]}
    ])

    # --- prefab: build a "tree", save it, stamp copies ---
    call("entity.spawn", name="tree", primitive="cube", position=[0, 0.6, 0],
         scale=[0.3, 1.2, 0.3], base_color=[0.4, 0.26, 0.15])
    call("entity.spawn", name="tree_crown", primitive="sphere", position=[0, 1.1, 0],
         scale=[1.3, 1.3, 1.3], base_color=[0.2, 0.55, 0.25], parent="tree")
    print("prefab.save:", call("prefab.save", root="tree",
                                path=str(ROOT / "prefabs" / "tree.json")))
    call("entity.destroy", name="tree")
    call("entity.destroy", name="tree_crown")

    for i, (x, z) in enumerate([(-5, -3), (5, -4), (-4, 4), (6, 3)]):
        print("instantiate:", call("prefab.instantiate",
              path=str(ROOT / "prefabs" / "tree.json"),
              name=f"tree{i}", position=[x, 0, z])["result"]["created"])

    call("light.set", name="sun", direction=[-0.5, -1, -0.35], intensity=3.2)
    call("camera.set", position=[7, 5, 10], target=[0, 1, 0], fov_deg=55)
    call("world.step", dt=1 / 60, steps=45)   # let the head rotate a bit

    call("observe.screenshot", path=str(ROOT / "screenshots" / "drive.png"))
    print("stats:", call("observe.stats")["result"])
    print("entities:", call("entity.list")["result"]["names"])
    call("scene.save", path=str(ROOT / "scenes" / "generated.json"))
    call("quit")
    proc.wait(timeout=5)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
