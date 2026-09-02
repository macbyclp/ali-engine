#!/usr/bin/env python3
"""ali-engine driver — M15: game state, timers, behavior conditions,
multi-angle observation, checkpoints. A tiny wave-survival loop."""
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
            if "event" not in msg:
                return msg

    call("ping")
    call("scene.reset")
    call("state.clear")
    call("entity.spawn", name="arena", primitive="plane", scale=[16, 1, 16],
         base_color=[0.2, 0.22, 0.26], body={"type": "static"})

    # a "spawner" that drops an enemy cube every time it receives "wave"
    call("entity.spawn", name="spawner", primitive="cube", position=[0, 6, 0],
         scale=[0.4, 0.4, 0.4], base_color=[0.1, 0.1, 0.1], emissive=[0.8, 0.2, 0.2])
    call("behavior.set", name="spawner", behaviors=[
        {"on": "event", "name": "wave", "do": [
            {"action": "spawn", "primitive": "sphere", "position": [0, 5, 0],
             "base_color": [0.9, 0.3, 0.25],
             "body": {"type": "dynamic", "mass": 1.0, "restitution": 0.3},
             "behavior": [
                 {"on": "collision", "with": "arena", "do": [
                     {"action": "addState", "key": "score", "value": 10},
                     {"action": "setUI", "target": "hud", "text": "SCORE  ${score}"},
                     {"action": "destroy"}
                 ]}
             ]},
            {"action": "addState", "key": "wave_no", "value": 1},
            {"action": "timer", "after": 0.8, "event": "wave"},
        ]},
    ])

    # HUD driven by state
    call("ui.add", name="hud", kind="panel", anchor="top-left", pos=[0.03, 0.04],
         size=[0.26, 0.08], color=[0, 0, 0, 0.55], text="SCORE  0", text_size=24)
    call("ui.add", name="title", kind="text", anchor="top", pos=[0, 0.03],
         text="WAVE SURVIVAL", text_size=28)

    call("state.set", key="score", value=0)
    call("light.set", name="sun", direction=[-0.4, -1, -0.3], intensity=3.0)
    call("camera.set", position=[0, 7, 12], target=[0, 1, 0], fov_deg=55)

    call("timer.after", seconds=0.2, event="wave")

    call("checkpoint.save", name="start")
    call("world.step", dt=1 / 60, steps=260, substeps=2)   # ~4.3s of waves

    print("state:", call("state.list")["result"])
    call("observe.screenshot", path=str(ROOT / "screenshots" / "drive.png"))
    v = call("observe.view", position=[10, 3, 0], target=[0, 1, 0],
             path=str(ROOT / "screenshots" / "drive_side.png"))
    print("observe.view ->", v["result"]["path"])
    ent = call("observe.entities")["result"]
    print("entities in view:", sum(1 for e in ent["entities"] if e["in_view"]),
          "/", len(ent["entities"]))

    call("checkpoint.restore", name="start")
    print("after restore, entities:", len(call("entity.list")["result"]["names"]),
          " score:", call("state.get", key="score")["result"]["value"])

    call("quit")
    proc.wait(timeout=5)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
