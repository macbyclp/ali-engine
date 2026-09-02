#!/usr/bin/env python3
"""ali-engine driver — M12 demo: particles + bloom (+ audio if a wav is given)."""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENGINE = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "Debug" / "engine.exe"
WAV = sys.argv[2] if len(sys.argv) > 2 else None


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
         base_color=[0.15, 0.16, 0.2], roughness=0.7)

    # glowing emissive orbs (drive the bloom) with fire fountains
    for i, x in enumerate([-4, 0, 4]):
        col = [[1.4, 0.5, 0.2], [0.3, 1.4, 0.6], [0.4, 0.6, 1.5]][i]
        call("entity.spawn", name=f"orb{i}", primitive="sphere",
             position=[x, 1.4, 0], scale=[0.5, 0.5, 0.5],
             base_color=[0.02, 0.02, 0.02], emissive=col)
        call("particles.emit", name=f"fx{i}", position=[x, 0.5, 0],
             rate=90, lifetime=1.6, velocity=[0, 4.5, 0],
             velocity_spread=[1.0, 0.6, 1.0], gravity=[0, -2.5, 0],
             start_color=col + [1.0], end_color=[col[0] * 0.3, col[1] * 0.3, col[2] * 0.3, 0.0],
             start_size=0.28, end_size=0.02)

    call("light.set", name="sun", direction=[-0.4, -1, -0.3], intensity=0.7)
    call("camera.set", position=[0, 4, 12], target=[0, 2, 0], fov_deg=55)

    if WAV:
        print("audio.play:", call("audio.play", file=WAV, volume=0.8, loop=True,
                                  spatial=True, position=[0, 1, 0]))

    call("world.step", dt=1 / 60, steps=90)
    call("observe.screenshot", path=str(ROOT / "screenshots" / "drive.png"))
    print("stats:", call("observe.stats")["result"])
    call("scene.save", path=str(ROOT / "scenes" / "generated.json"))
    call("quit")
    proc.wait(timeout=5)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
