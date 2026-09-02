#!/usr/bin/env python3
"""Render README media: hero still + an animated GIF, driven headless over the
JSON protocol. Frames are PNGs from observe.screenshot; the GIF is assembled
with Pillow.

Usage: python tools/gen_media.py [engine.exe]
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
ENGINE = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "Debug" / "engine.exe"
MEDIA = ROOT / "media"
MEDIA.mkdir(exist_ok=True)


class Engine:
    def __init__(self, w, h):
        self.p = subprocess.Popen(
            [str(ENGINE), "--headless", "--width", str(w), "--height", str(h)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, cwd=ROOT,
        )

    def call(self, method, **params):
        self.p.stdin.write(json.dumps({"method": method, "params": params}) + "\n")
        self.p.stdin.flush()
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError("engine closed the connection")
            msg = json.loads(line)
            if "event" not in msg:
                return msg

    def close(self):
        self.call("quit")
        self.p.wait(timeout=5)


def hero_still():
    e = Engine(1600, 900)
    e.call("scene.load", path="scenes/showcase.json")
    e.call("world.step", dt=1 / 60, steps=80, substeps=2)
    e.call("observe.screenshot", path=str(MEDIA / "hero.png"))
    e.close()
    print("media/hero.png")


def showcase_gif():
    e = Engine(768, 432)
    e.call("scene.load", path="scenes/showcase.json")
    tmp = Path(tempfile.mkdtemp())
    frames = []
    e.call("world.step", dt=1 / 60, steps=30, substeps=2)
    for i in range(40):
        e.call("world.step", dt=1 / 60, steps=4, substeps=2)
        fp = tmp / f"f{i:03d}.png"
        e.call("observe.screenshot", path=str(fp))
        frames.append(Image.open(fp).convert("RGB"))
    e.close()

    pal = frames[len(frames) // 2].resize((640, 360)).quantize(colors=96)
    small = [f.resize((640, 360), Image.LANCZOS).quantize(palette=pal, dither=Image.FLOYDSTEINBERG)
             for f in frames]
    small[0].save(MEDIA / "showcase.gif", save_all=True, append_images=small[1:],
                  duration=66, loop=0, optimize=True)
    print("media/showcase.gif", f"({(MEDIA / 'showcase.gif').stat().st_size // 1024} KB)")


def feature_stills():
    # materials
    e = Engine(1200, 500)
    e.call("scene.reset")
    e.call("entity.spawn", name="floor", primitive="plane", scale=[14, 1, 14],
           base_color_map="builtin:grid", uv_scale=[10, 10], roughness=0.8)
    for i in range(6):
        r = i / 5.0
        e.call("entity.spawn", name=f"s{i}", primitive="sphere",
               position=[(i - 2.5) * 2.0, 1.2, 0], scale=[1.1, 1.1, 1.1],
               base_color=[0.9, 0.85, 0.8], metallic=1.0, roughness=max(0.05, r))
    e.call("entity.spawn", name="nrm", primitive="sphere", position=[0, 1.2, -3],
           scale=[1.6, 1.6, 1.6], base_color=[0.6, 0.4, 0.35], normal_map="builtin:bumps")
    e.call("light.set", name="sun", direction=[-0.5, -1, -0.3], intensity=3.2)
    e.call("camera.set", position=[0, 4, 9], target=[0, 1, -0.5], fov_deg=55)
    e.call("observe.screenshot", path=str(MEDIA / "materials.png"))
    e.close()
    print("media/materials.png")

    # lighting
    e = Engine(1200, 500)
    e.call("scene.reset")
    e.call("entity.spawn", name="floor", primitive="plane", scale=[16, 1, 16],
           base_color=[0.8, 0.8, 0.82], roughness=0.55)
    for i in range(-3, 4):
        for j in range(-2, 3):
            e.call("entity.spawn", name=f"p{i}_{j}", primitive="cube",
                   position=[i * 2.1, 0.9, j * 2.1], scale=[0.5, 1.8, 0.5],
                   base_color=[0.78, 0.78, 0.8])
    e.call("light.set", name="sun", direction=[-0.4, -1, -0.3], intensity=0.3)
    e.call("light.add", name="a", type="point", position=[-4, 2.5, 0],
           color=[1.0, 0.25, 0.2], intensity=16, range=9)
    e.call("light.add", name="b", type="point", position=[4, 2.5, 0],
           color=[0.2, 0.4, 1.0], intensity=16, range=9)
    e.call("light.add", name="s", type="spot", position=[0, 7, 5],
           direction=[0, -1, -0.6], color=[1.0, 0.95, 0.7], intensity=45,
           range=16, inner_deg=14, outer_deg=24)
    e.call("camera.set", position=[0, 6, 13], target=[0, 1, 0], fov_deg=55)
    e.call("observe.screenshot", path=str(MEDIA / "lighting.png"))
    e.close()
    print("media/lighting.png")


if __name__ == "__main__":
    hero_still()
    feature_stills()
    showcase_gif()
