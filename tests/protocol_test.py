#!/usr/bin/env python3
"""End-to-end protocol tests: drives a real `engine --headless` over stdin/stdout.

usage: protocol_test.py <engine> [--keep]
Runs display-less (EGL) when no DISPLAY is set, otherwise on a hidden window.
Exit code 0 = all pass. Snapshots (PNG) land in $ALI_TEST_OUT (default: <cwd>/screenshots/test).
"""
import json, os, subprocess, sys, tempfile, time, wave, struct, math
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENGINE = sys.argv[1] if len(sys.argv) > 1 else str(ROOT / "build" / "engine")
OUT = Path(os.environ.get("ALI_TEST_OUT", ROOT / "screenshots" / "test"))
OUT.mkdir(parents=True, exist_ok=True)
REL = os.path.relpath(OUT, ROOT)          # engine cwd = ROOT, so writes stay inside its write root

fails = []
def check(cond, what, extra=""):
    print(("PASS " if cond else "FAIL ") + what + (f"  [{extra}]" if extra and not cond else ""))
    if not cond: fails.append(what)

class Engine:
    def __init__(self, *args):
        self.p = subprocess.Popen([ENGINE, "--headless", *args], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=ROOT)
        self.n = 0
        ready = json.loads(self.p.stdout.readline())
        assert ready.get("event") == "ready", ready
    def call(self, method, **params):
        self.n += 1
        self.p.stdin.write(json.dumps({"id": self.n, "method": method, "params": params}) + "\n")
        self.p.stdin.flush()
        while True:
            r = json.loads(self.p.stdout.readline())
            if r.get("id") == self.n: return r
    def close(self):
        try: self.call("quit")
        except Exception: pass
        try: self.p.wait(timeout=10)
        except Exception: self.p.kill()

def pixels(path):
    try:
        from PIL import Image
    except ImportError:
        return None
    return Image.open(path).convert("RGB")

e = Engine()
try:
    # ---- introspection --------------------------------------------------------------
    r = e.call("commands.list")
    cmds = r["result"]["commands"]
    check(r["ok"] and len(cmds) >= 85 and "commands.list" in cmds, "commands.list returns the table", str(len(cmds)))
    check(cmds == sorted(cmds), "commands.list is sorted")
    doc = (ROOT / "docs" / "AI-PROTOCOL.md").read_text()
    missing = [c for c in cmds if c not in doc]
    check(not missing, "every command is documented in docs/AI-PROTOCOL.md", str(missing))
    check(e.call("nope.nope")["ok"] is False, "unknown method fails")

    # ---- write confinement / plugin gating -------------------------------------------
    r = e.call("scene.save", path="/tmp/ali_evil.json")
    check(not r["ok"] and "permission denied" in r["error"], "scene.save outside the write root is refused", r.get("error", ""))
    check(not os.path.exists("/tmp/ali_evil.json"), "…and nothing was written")
    r = e.call("scene.save", path="../ali_evil2.json")
    check(not r["ok"], "scene.save with .. is refused")
    r = e.call("observe.screenshot", path="/etc/ali.png")
    check(not r["ok"], "screenshot outside the root is refused")
    r = e.call("prefab.save", root="x", path="/tmp/p.json")
    check(not r["ok"], "prefab.save outside the root is refused")
    r = e.call("plugin.load", path="./plugins/none.so")
    check(not r["ok"] and "--allow-plugin-load" in r["error"], "plugin.load is off by default", r.get("error", ""))

    # ---- scene build + save->load round trip ----------------------------------------------
    e.call("scene.reset")
    e.call("entity.spawn", name="sun", light={"type": "directional", "intensity": 3.0})
    e.call("entity.spawn", name="floor", primitive="cube", position=[0, -0.5, 0], scale=[20, 1, 20],
           base_color=[0.4, 0.45, 0.4])
    e.call("entity.spawn", name="tilted", primitive="cube", position=[0, 1, 0], rotation=[10, 135, 20],
           base_color=[0.9, 0.3, 0.2])
    e.call("entity.spawn", name="glass", primitive="sphere", position=[1.5, 1, 1], base_color=[0.2, 0.5, 1.0], alpha=0.4)
    e.call("particles.emit", name="fx", position=[-2, 0.5, 0])
    e.call("particles.stop", name="fx")
    e.call("ui.add", name="hud", kind="bar", value=0.6, fill_color=[0.1, 0.9, 0.3, 1.0], color=[0, 0, 0, 0.5])
    e.call("camera.set", position=[0, 3, 8], target=[0, 1, 0], fov_deg=50)
    st = e.call("scene.state")["result"]
    cam_ent = next(x for x in st["entities"] if "camera" in x)
    cam_ent["camera"]["near_z"] = 0.25
    cam_ent["camera"]["far_z"] = 250.0
    tmp = f"{REL}/roundtrip.json"
    (ROOT / REL).mkdir(parents=True, exist_ok=True)
    (ROOT / tmp).write_text(json.dumps(st))
    e.call("scene.load", path=tmp)
    r = e.call("scene.save", path=f"{REL}/roundtrip2.json")
    check(r["ok"], "scene.save inside the root works")
    a = json.loads((ROOT / tmp).read_text())
    b = json.loads((ROOT / REL / "roundtrip2.json").read_text())
    ent = lambda j, n: next(x for x in j["entities"] if x["name"] == n)
    check(ent(b, "hud")["ui"]["fill_color"] == [0.1, 0.9, 0.3, 1.0] or
          all(abs(x - y) < 1e-6 for x, y in zip(ent(b, "hud")["ui"]["fill_color"], [0.1, 0.9, 0.3, 1.0])),
          "ui fill_color survives save->load")
    check(ent(b, "fx")["particles"]["emitting"] is False, "particle emitting=false survives save->load")
    cb = next(x for x in b["entities"] if "camera" in x)["camera"]
    check(abs(cb["near_z"] - 0.25) < 1e-6 and abs(cb["far_z"] - 250.0) < 1e-4, "camera near/far survive save->load", str(cb))
    check(abs(ent(b, "glass")["mesh"]["alpha"] - 0.4) < 1e-6, "material alpha survives save->load")
    tb = ent(b, "tilted")["transform"]
    check(tb["rotation"] == [10.0, 135.0, 20.0], "legacy Euler rotation is preserved verbatim", str(tb["rotation"]))
    q = tb.get("rotation_quat")
    check(q is not None and abs(sum(x * x for x in q) - 1) < 1e-5, "rotation_quat is written and unit length", str(q))
    # an OLD scene (Euler only, no quat) still loads to the same orientation
    old = {"entities": [{"name": "o", "transform": {"rotation": [10, 135, 20]}, "mesh": {"primitive": "cube"}}]}
    (ROOT / REL / "old.json").write_text(json.dumps(old))
    e.call("scene.load", path=f"{REL}/old.json")
    qo = ent(e.call("scene.state")["result"], "o")["transform"]["rotation_quat"]
    check(all(abs(x - y) < 1e-5 for x, y in zip(qo, q)), "Euler-only (old) scene == quaternion scene")
    e.call("scene.load", path=tmp)

    # ---- observe.* ---------------------------------------------------------------------
    s = e.call("observe.stats")["result"]
    check(s["draw_calls"] > 0 and s["transparent"] == 1, "observe.stats sees draw calls + 1 transparent mesh", str(s))
    r0 = s["world_recomputed"]
    e.call("observe.stats"); e.call("observe.stats")
    s2 = e.call("observe.stats")["result"]
    check(s2["world_recomputed"] == r0, "world transforms are not recomputed when nothing moved", f"{r0}->{s2['world_recomputed']}")
    r = e.call("observe.entities"); check(r["ok"] and len(r["result"]["entities"]) >= 4, "observe.entities")
    r = e.call("observe.describe"); check(r["ok"] and r["result"]["entities"], "observe.describe")
    r = e.call("observe.pick", ndc=[0, 0]); check(r["ok"], "observe.pick")
    for m, f in (("observe.screenshot", "shot.png"), ("observe.view", "view.png"),
                 ("observe.depth", "depth.png"), ("observe.segment", "seg.png")):
        p = f"{REL}/{f}"
        kw = {"path": p}
        if m == "observe.view": kw.update(position=[5, 4, 6], target=[0, 1, 0])
        r = e.call(m, **kw)
        check(r["ok"] and (ROOT / p).stat().st_size > 1000, f"{m} writes a PNG", str(r))
    img = pixels(ROOT / REL / "shot.png")
    if img:
        colors = len(set(img.resize((64, 36)).getdata()))
        check(colors > 30, "screenshot is not a blank frame", f"{colors} colours")
        # transparent sphere blends: pixel at its centre differs when alpha changes
        def centre():
            e.call("observe.screenshot", path=f"{REL}/a.png", width=320, height=180)
            return pixels(ROOT / REL / "a.png").getpixel((190, 96))
        c1 = centre()
        e.call("entity.setMaterial", name="glass", alpha=1.0)
        c2 = centre()
        e.call("entity.setMaterial", name="glass", alpha=0.4)
        check(c1 != c2, "alpha changes the rendered result (blended vs opaque)", f"{c1} vs {c2}")

    # ---- physics: quaternion goes straight from the solver, no Euler flips -------------------
    e.call("scene.reset")
    e.call("entity.spawn", name="floor", primitive="cube", position=[0, -0.5, 0], scale=[20, 1, 20], body={"type": "static"})
    e.call("entity.spawn", name="tumble", primitive="cube", position=[0, 3, 0], rotation=[30, 40, 50], body={"type": "dynamic"})
    for _ in range(5): e.call("world.step", steps=20, dt=0.016)
    t = next(x for x in e.call("scene.state")["result"]["entities"] if x["name"] == "tumble")["transform"]
    n = math.sqrt(sum(x * x for x in t["rotation_quat"])) if "rotation_quat" in t else 0
    check(abs(n - 1) < 1e-4 and t["position"][1] < 1.0, "physics writes a unit quaternion; body landed", str(t))

    # ---- lights: priority + quotas ----------------------------------------------------------
    e.call("scene.reset")
    e.call("entity.spawn", name="floor", primitive="cube", position=[0, -0.5, 0], scale=[30, 1, 30])
    for i in range(20):
        e.call("entity.spawn", name=f"L{i}", light={"type": "point", "intensity": 5, "range": 6, "cast_shadows": i >= 16},
               position=[(i % 5) * 4 - 8, 1.5, (i // 5) * 4 - 6])
    e.call("camera.set", position=[0, 12, 14], target=[0, 0, 0])
    s = e.call("observe.stats")["result"]
    check(s["lights_dropped"] == 4, "lights beyond 16 are dropped (and reported)", str(s["lights_dropped"]))
    check(s["shadows_dropped"] >= 1, "shadow requests beyond the point-shadow quota are reported", str(s["shadows_dropped"]))
    e.call("observe.screenshot", path=f"{REL}/lights20.png", width=640, height=360)

    # ---- audio recovery -------------------------------------------------------------------------
    wav = OUT / "beep.wav"
    with wave.open(str(wav), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(22050)
        w.writeframes(b"".join(struct.pack("<h", int(2000 * math.sin(i * 0.05))) for i in range(int(22050 * 0.3))))
    au = e.call("audio.list")["result"]
    if au["device"]:
        h = e.call("audio.play", file=str(wav), volume=0.02)["result"]["handle"]
        check(e.call("audio.list")["result"]["count"] == 1, "audio.play registers a live sound")
        time.sleep(1.2)
        check(e.call("audio.list")["result"]["count"] == 0, "finished one-shot sounds are reclaimed")
        check(e.call("observe.stats")["result"]["active_sounds"] == 0, "observe.stats.active_sounds back to 0")
    else:
        print("SKIP audio recovery (no audio device)")

    # ---- record / replay stays inside the root ---------------------------------------------------
    r = e.call("record.start", path=f"{REL}/rec.jsonl"); check(r["ok"], "record.start inside the root")
    e.call("entity.spawn", name="rec1", primitive="cube")
    e.call("record.stop")
    r = e.call("record.play", path=f"{REL}/rec.jsonl"); check(r["ok"] and r["result"]["played"] >= 1, "record.play")
finally:
    e.close()

# ---- plugin.load opt-in flag --------------------------------------------------------------------
e2 = Engine("--allow-plugin-load")
try:
    r = e2.call("plugin.load", path="./plugins/none.so")
    check(not r["ok"] and "permission denied" not in r["error"], "with --allow-plugin-load the command runs (fails on the missing file)", r.get("error", ""))
    check(e2.call("commands.list")["result"]["plugin_load_enabled"] is True, "commands.list reports plugin_load_enabled")
finally:
    e2.close()

# ---- --write-root widens the confinement --------------------------------------------------------
tmpd = tempfile.mkdtemp()
e3 = Engine("--write-root", tmpd)
try:
    r = e3.call("scene.save", path=f"{tmpd}/ok.json")
    check(r["ok"] and Path(tmpd, "ok.json").exists(), "--write-root allows writes under the new root", str(r))
    check(not e3.call("scene.save", path="/tmp/ali_evil3.json")["ok"], "…and still refuses everything else")
finally:
    e3.close()

print("\nRESULT:", "ALL PASS" if not fails else f"{len(fails)} FAILED: {fails}")
sys.exit(1 if fails else 0)
