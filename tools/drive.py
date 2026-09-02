#!/usr/bin/env python3
"""ali-engine driver — M10 lighting demo (point + spot lights)."""
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
         base_color=[0.8, 0.8, 0.82], roughness=0.6)

    # a grid of white pillars to catch the light
    for i in range(-3, 4):
        for j in range(-2, 3):
            call("entity.spawn", name=f"p_{i}_{j}", primitive="cube",
                 position=[i * 2.2, 0.9, j * 2.2], scale=[0.5, 1.8, 0.5],
                 base_color=[0.75, 0.75, 0.78], roughness=0.5)

    # dim the sun, then add coloured point + spot lights
    call("light.set", name="sun", direction=[-0.4, -1, -0.3], intensity=0.4)
    call("light.add", name="lamp_r", type="point", position=[-4, 2.5, 0],
         color=[1.0, 0.25, 0.2], intensity=14, range=9)
    call("light.add", name="lamp_b", type="point", position=[4, 2.5, 0],
         color=[0.2, 0.4, 1.0], intensity=14, range=9)
    call("light.add", name="spot", type="spot", position=[0, 7, 5],
         direction=[0, -1, -0.6], color=[1.0, 0.95, 0.7], intensity=40,
         range=16, inner_deg=14, outer_deg=24)

    call("camera.set", position=[0, 6, 14], target=[0, 1, 0], fov_deg=55)

    call("observe.screenshot", path=str(ROOT / "screenshots" / "drive.png"))
    print("stats:", call("observe.stats")["result"])
    print("state lights:", [e["name"] for e in call("scene.state")["result"]["entities"]
                            if "light" in e])
    call("scene.save", path=str(ROOT / "scenes" / "generated.json"))
    call("quit")
    proc.wait(timeout=5)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
