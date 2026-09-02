#!/usr/bin/env python3
"""ali-engine driver — M13 UI + text demo over a 3D scene."""
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
        [str(ENGINE), "--headless", "--width", "1200", "--height", "675"],
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
    call("scene.load", path="scenes/showcase.json")
    call("world.step", dt=1 / 60, steps=70, substeps=2)

    # --- HUD ---
    call("ui.add", name="title", kind="text", anchor="top", pos=[0, 0.03],
         text="ALI-ENGINE  //  AI-DRIVEN", text_size=30,
         text_color=[1, 1, 1, 0.95])

    call("ui.add", name="hpbg", kind="bar", anchor="top-left", pos=[0.03, 0.05],
         size=[0.28, 0.045], value=0.68,
         color=[0, 0, 0, 0.5], fill_color=[0.85, 0.25, 0.2, 1.0],
         text="HP  68 / 100", text_size=20)

    call("ui.add", name="mpbg", kind="bar", anchor="top-left", pos=[0.03, 0.11],
         size=[0.28, 0.045], value=0.4,
         color=[0, 0, 0, 0.5], fill_color=[0.25, 0.5, 1.0, 1.0],
         text="MP  40 / 100", text_size=20)

    call("ui.add", name="score", kind="panel", anchor="top-right", pos=[0.03, 0.05],
         size=[0.24, 0.09], color=[0.05, 0.05, 0.08, 0.6],
         text="SCORE  14,820", text_size=24)

    call("ui.add", name="hint", kind="panel", anchor="bottom", pos=[0, 0.05],
         size=[0.5, 0.08], color=[0.0, 0.0, 0.0, 0.55],
         text="Press  [E]  to interact", text_size=22, text_color=[1, 0.9, 0.5, 1])

    call("camera.set", position=[0, 6, 16], target=[0, 1.5, 0], fov_deg=55)
    call("observe.screenshot", path=str(ROOT / "screenshots" / "drive.png"))
    call("scene.save", path=str(ROOT / "scenes" / "generated.json"))
    print("ui entities:", [e["name"] for e in call("scene.state")["result"]["entities"]
                           if "ui" in e])
    call("quit")
    proc.wait(timeout=5)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
