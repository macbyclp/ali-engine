#!/usr/bin/env python3
"""ali-engine driver — M11 character controller + grid navigation demo."""
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
    call("entity.spawn", name="floor", primitive="plane", scale=[20, 1, 20],
         base_color_map="builtin:checker", uv_scale=[10, 10], roughness=0.9,
         body={"type": "static"})

    # a wall with a gap -> the character must route around it
    for i in range(-4, 5):
        if i in (0, 1):
            continue
        call("entity.spawn", name=f"wall{i}", primitive="cube",
             position=[i * 1.2, 0.75, 0], scale=[1.2, 1.5, 1.2],
             base_color=[0.5, 0.45, 0.4], body={"type": "static"})
    for i in range(-3, 4):
        call("entity.spawn", name=f"wall2_{i}", primitive="cube",
             position=[i * 1.2, 0.75, 6], scale=[1.2, 1.5, 1.2],
             base_color=[0.5, 0.45, 0.4], body={"type": "static"})

    call("character.create", name="hero", position=[0, 0, -6], radius=0.4, height=1.8,
         move_speed=4.5, base_color=[0.95, 0.55, 0.2])

    call("light.set", name="sun", direction=[-0.4, -1, -0.3], intensity=3.0)
    call("camera.set", position=[10, 12, -12], target=[0, 0, 2], fov_deg=55)

    print("nav.bake:", call("nav.bake", min=[-14, 0, -14], max=[14, 0, 14], cell=0.8))
    r = call("nav.path", **{"from": [0, 0, -6], "to": [0, 0, 10]})
    print("path waypoints:", len(r["result"]["waypoints"]))

    print("moveTo:", call("character.moveTo", name="hero", target=[0, 0, 10]))

    call("world.step", dt=1 / 60, steps=90)
    call("observe.screenshot", path=str(ROOT / "screenshots" / "drive.png"))
    call("world.step", dt=1 / 60, steps=120)
    call("observe.screenshot", path=str(ROOT / "screenshots" / "drive_b.png"))

    for ent in call("scene.state")["result"]["entities"]:
        if ent["name"] == "hero":
            print("hero pos:", [round(x, 2) for x in ent["transform"]["position"]])
    call("quit")
    proc.wait(timeout=5)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
