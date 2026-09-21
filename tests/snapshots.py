#!/usr/bin/env python3
"""Renders the verification snapshots with the engine's own observe.screenshot.
usage: snapshots.py <engine> <outdir>     (engine runs with --write-root <outdir>, no display needed)
"""
import json, subprocess, sys, os
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
ENGINE, OUT = sys.argv[1], Path(sys.argv[2]).resolve()
OUT.mkdir(parents=True, exist_ok=True)

class E:
    def __init__(self, *a):
        self.p = subprocess.Popen([ENGINE, "--headless", "--write-root", str(OUT), *a], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, cwd=ROOT)
        self.p.stdout.readline(); self.n = 0
    def c(self, m, **p):
        self.n += 1
        self.p.stdin.write(json.dumps({"id": self.n, "method": m, "params": p}) + "\n"); self.p.stdin.flush()
        while True:
            r = json.loads(self.p.stdout.readline())
            if r.get("id") == self.n:
                if not r["ok"]: print("ERR", m, r["error"])
                return r.get("result")
    def shot(self, name, w=960, h=540): return self.c("observe.screenshot", path=str(OUT / name), width=w, height=h)
    def close(self):
        self.c("quit"); self.p.wait(timeout=10)

def base(e, sun=1.6, floor=(0.30, 0.32, 0.36)):
    e.c("scene.reset")
    e.c("entity.spawn", name="sun", light={"type": "directional", "intensity": sun, "direction": [-0.5, -1, -0.4]})
    e.c("entity.spawn", name="floor", primitive="plane", scale=[30, 1, 30], base_color=list(floor))

# 01 quaternion: a row of boxes with Euler poses (incl. y=135 and gimbal pitch=90) + a tumbled physics box
e = E(); base(e)
poses = [[0, 0, 0], [0, 45, 0], [0, 135, 0], [30, 40, 50], [90, 0, 0], [10, 135, 20], [0, 0, 60]]
for i, r in enumerate(poses):
    e.c("entity.spawn", name=f"pose{i}", primitive="cube", position=[-6 + i * 2, 1.0, 0], scale=[0.9, 0.5, 1.6],
        rotation=r, base_color=[0.25 + 0.1 * i, 0.5, 0.9 - 0.1 * i])
e.c("entity.spawn", name="floorbody", primitive="cube", position=[0, -0.5, 4], scale=[30, 1, 6], body={"type": "static"}, base_color=[0.5, 0.5, 0.55])
e.c("entity.spawn", name="tumble", primitive="cube", position=[0, 4, 4], rotation=[30, 40, 50], body={"type": "dynamic"}, base_color=[0.95, 0.5, 0.2])
for _ in range(6): e.c("world.step", steps=20, dt=0.016)
e.c("camera.set", position=[0, 7, 11], target=[0, 0.8, 1.5], fov_deg=55)
e.shot("01_quaternion_poses_and_physics.png")
e.close()

# 02 alpha: three overlapping glass spheres (sorted back-to-front) over an opaque box; opaque control on the right
e = E(); base(e)
e.c("entity.spawn", name="box", primitive="cube", position=[0, 1, 0], scale=[2, 2, 2], base_color=[0.9, 0.25, 0.2])
for i, (x, z, col) in enumerate([(-1.2, 1.8, [0.2, 0.6, 1.0]), (0.2, 2.8, [0.2, 1.0, 0.4]), (1.4, 3.8, [1.0, 0.9, 0.2])]):
    e.c("entity.spawn", name=f"glass{i}", primitive="sphere", position=[x, 1.2, z], scale=[2.2, 2.2, 2.2], base_color=col, alpha=0.45)
e.c("entity.spawn", name="opaque_ctl", primitive="sphere", position=[5, 1.2, 2], scale=[2.2, 2.2, 2.2], base_color=[0.2, 0.6, 1.0])
e.c("camera.set", position=[0, 4, 11], target=[1.5, 1, 2], fov_deg=50)
print("stats", e.c("observe.stats")["transparent"], "transparent")
e.shot("02_alpha_blended_pass.png")
e.close()

# 03 headless without a display (EGL) + command table: the shipped showcase scene
e = E("--scene", "scenes/showcase.json")
e.shot("03_showcase_headless_egl_table_dispatch.png")
e.close()

# 04 save -> load round trip: fill_color bar, non-emitting emitter, camera near/far, alpha, rotation
e = E(); base(e)
e.c("entity.spawn", name="tilted", primitive="cube", position=[-2, 1, 0], rotation=[10, 135, 20], base_color=[0.9, 0.3, 0.2])
e.c("entity.spawn", name="glass", primitive="sphere", position=[1.5, 1, 1], base_color=[0.2, 0.5, 1.0], alpha=0.4)
e.c("particles.emit", name="fx_on", position=[3, 0.2, -1], rate=60)
e.c("particles.emit", name="fx_off", position=[-4, 0.2, -1], rate=60)
e.c("particles.stop", name="fx_off")   # emitting=false must survive save -> load
e.c("ui.add", name="hud", kind="bar", value=0.6, anchor="top-left", pos=[0.04, 0.05], size=[0.3, 0.05],
    fill_color=[0.9, 0.1, 0.9, 1.0], color=[0.1, 0.1, 0.1, 0.8])
e.c("camera.set", position=[0, 3, 9], target=[0, 1, 0], fov_deg=55)
st = e.c("scene.state")
for x in st["entities"]:
    if "camera" in x: x["camera"]["near_z"] = 0.3; x["camera"]["far_z"] = 300.0
(OUT / "roundtrip_src.json").write_text(json.dumps(st))
e.c("scene.load", path=str(OUT / "roundtrip_src.json"))
for _ in range(3): e.c("world.step", steps=30, dt=0.03)
e.shot("04a_roundtrip_before_save.png")
e.c("scene.save", path=str(OUT / "roundtrip_saved.json"))
e.close()
e = E()
e.c("scene.load", path=str(OUT / "roundtrip_saved.json"))
for _ in range(3): e.c("world.step", steps=30, dt=0.03)
e.shot("04b_roundtrip_after_load.png")
e.close()

# 05 light priority: 20 point lights, only the 16 that matter to the camera are used
e = E(); base(e, sun=0.05, floor=(0.6, 0.6, 0.6))
for i in range(20):
    col = [[1, .3, .3], [.3, 1, .3], [.3, .5, 1], [1, .9, .3]][i % 4]
    e.c("entity.spawn", name=f"L{i}", light={"type": "point", "intensity": 6, "range": 5.5, "color": col, "cast_shadows": i >= 16},
        position=[(i % 5) * 4 - 8, 1.5, (i // 5) * 4 - 6])
e.c("entity.spawn", name="pillar", primitive="cube", position=[0, 1, 5], scale=[1, 2, 1], base_color=[0.8, 0.8, 0.8])
e.c("camera.set", position=[0, 9, 16], target=[0, 0, 0], fov_deg=60)
print("light stats", {k: v for k, v in e.c("observe.stats").items() if "dropped" in k})
e.shot("05_light_priority_20_lights.png")
e.close()

# 06 world-transform cache + incremental resolve: parent moved, children follow; new mesh spawned after load
e = E(); base(e)
e.c("entity.spawn", name="arm", primitive="cube", position=[0, 1, 0], scale=[1, 1, 1], base_color=[0.7, 0.7, 0.75])
for i in range(3):
    e.c("entity.spawn", name=f"link{i}", primitive="sphere", parent="arm", position=[1.5 * (i + 1), 0.3, 0], scale=[0.6, 0.6, 0.6],
        base_color=[0.9, 0.4 + 0.2 * i, 0.2])
e.c("camera.set", position=[0, 6, 11], target=[2, 1, 0], fov_deg=55)
e.shot("06a_hierarchy_before_move.png")
e.c("entity.setTransform", name="arm", position=[-2, 2.5, 1], rotation=[0, 50, 25])
e.c("entity.spawn", name="late_spawn", primitive="sphere", position=[3, 0.5, 3], base_color=[0.2, 0.9, 0.5])
r0 = e.c("observe.stats")["world_recomputed"]; r1 = e.c("observe.stats")["world_recomputed"]
print("world recomputed on repeat observe:", r1 - r0)
e.shot("06b_hierarchy_after_move_and_spawn.png")
e.close()

# 07 write confinement: a screenshot inside the root works (the refusals are asserted in protocol_test.py)
e = E("--scene", "scenes/demo.json")
print("outside:", e.p.stdin.write(json.dumps({"id": 99, "method": "observe.screenshot", "params": {"path": "/tmp/nope.png"}}) + "\n") and e.p.stdin.flush())
print(json.loads(e.p.stdout.readline()))
e.shot("07_confined_write_ok.png")
e.close()
print("done")
