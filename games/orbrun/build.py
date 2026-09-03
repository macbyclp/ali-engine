#!/usr/bin/env python3
"""Generates games/orbrun/orbrun.json -- a small time-attack collectathon that
runs entirely on the ali-engine JSON scene format. No C++.

Grab all 8 orbs before the timer runs out. Touch the sweeping red bar and you
lose. WASD + Space, third-person follow camera.
"""
import json
import math
import pathlib

ARENA = 26.0          # inner playable half-extent is ARENA/2
HALF = ARENA / 2
WALL_H = 2.0
ORBS = 8
TIME_LIMIT = 40.0

ents = []


def add(e):
    ents.append(e)


# ---------------------------------------------------------------- environment
add({"name": "sun", "light": {"type": "directional", "direction": [-0.45, -0.85, -0.4],
                              "color": [1.0, 0.96, 0.9], "intensity": 1.15}})
add({"name": "camera", "camera": {
    "position": [0, 14, 18], "target": [0, 1, 0], "fov_deg": 55,
    "follow": "player", "follow_offset": [0, 9, 13], "follow_look": [0, 1.2, 0],
    "follow_stiffness": 4.0}})

# floor: a big CSG slab so it has real thickness for the collider
add({"name": "floor",
     "transform": {"position": [0, -0.5, 0]},
     "mesh": {"primitive": "procedural", "base_color_map": "builtin:checker",
              "roughness": 0.9, "uv_scale": [6, 6],
              "build": [{"shape": "box", "size": [ARENA, 1.0, ARENA]}]},
     "body": {"type": "static"}})

# four boundary walls
for i, (px, pz, sx, sz) in enumerate([
        (0, -HALF, ARENA, 0.6), (0, HALF, ARENA, 0.6),
        (-HALF, 0, 0.6, ARENA), (HALF, 0, 0.6, ARENA)]):
    add({"name": f"wall_{i}",
         "transform": {"position": [px, WALL_H / 2, pz]},
         "mesh": {"primitive": "procedural", "base_color": [0.30, 0.33, 0.40],
                  "roughness": 0.7,
                  "build": [{"shape": "box", "size": [sx, WALL_H, sz]}]},
         "body": {"type": "static"}})

# a few decorative pillars to navigate around (CSG: fluted columns)
for i, (x, z) in enumerate([(-6, -4), (6, 5), (-5, 7), (7, -6)]):
    add({"name": f"pillar_{i}",
         "transform": {"position": [x, 1.6, z]},
         "mesh": {"primitive": "procedural", "base_color": [0.7, 0.68, 0.62],
                  "metallic": 0.1, "roughness": 0.5,
                  "build": [
                      {"shape": "cylinder", "radius": 0.7, "height": 3.2, "segments": 6},
                      {"shape": "box", "size": [2.2, 0.35, 2.2], "translate": [0, 1.6, 0]},
                      {"op": "union", "shape": "box", "size": [2.2, 0.35, 2.2],
                       "translate": [0, -1.6, 0]}]},
         "body": {"type": "static"}})

# ---------------------------------------------------------------- player
add({"name": "player",
     "transform": {"position": [9.5, 1.2, 9.5]},
     "mesh": {"primitive": "sphere", "base_color": [0.25, 0.6, 1.0],
              "metallic": 0.3, "roughness": 0.2},
     "body": {"type": "dynamic", "mass": 1.0, "restitution": 0.1, "friction": 0.4},
     "behavior": [
         {"on": "input", "action": "fwd",   "do": [{"action": "move", "velocity": [0, 0, -9]}]},
         {"on": "input", "action": "back",  "do": [{"action": "move", "velocity": [0, 0, 9]}]},
         {"on": "input", "action": "left",  "do": [{"action": "move", "velocity": [-9, 0, 0]}]},
         {"on": "input", "action": "right", "do": [{"action": "move", "velocity": [9, 0, 0]}]},
         {"on": "inputPressed", "action": "jump", "do": [{"action": "impulse", "impulse": [0, 6, 0]}]},
         {"on": "collision", "with": "hazard", "do": [{"action": "emit", "event": "hit"}]}
     ]})

# ---------------------------------------------------------------- orbs
for i in range(ORBS):
    a = (i / ORBS) * math.tau + 0.4
    r = 3.5 + 8.0 * ((i * 5) % ORBS) / (ORBS - 1)
    x, z = round(math.cos(a) * r, 2), round(math.sin(a) * r, 2)
    add({"name": f"orb_{i}",
         "transform": {"position": [x, 1.1, z], "scale": [0.55, 0.55, 0.55]},
         "mesh": {"primitive": "sphere", "base_color": [1.0, 0.85, 0.2],
                  "emissive": [1.4, 1.0, 0.15], "roughness": 0.3},
         "body": {"type": "kinematic", "sensor": True},
         "behavior": [
             {"on": "tick", "do": [{"action": "spin", "axis": [0, 1, 0], "speed_deg": 120}]},
             {"on": "enter", "with": "player", "do": [
                 {"action": "addState", "key": "orbs", "value": 1},
                 {"action": "emit", "event": "grab"},
                 {"action": "destroy"}]}
         ]})

# ---------------------------------------------------------------- hazard: a sweeping bar
add({"name": "hazard_pivot",
     "transform": {"position": [0, 1.1, 0]},
     "mesh": {"primitive": "procedural", "base_color": [0.55, 0.1, 0.1],
              "emissive": [0.9, 0.05, 0.05],
              "build": [{"shape": "cylinder", "radius": 0.55, "height": 2.4}]},
     "behavior": [{"on": "tick", "do": [{"action": "spin", "axis": [0, 1, 0], "speed_deg": 20}]}]})
add({"name": "hazard",
     "parent": "hazard_pivot",
     "transform": {"position": [5.0, 0.0, 0], "scale": [7.0, 1.7, 0.6]},
     "mesh": {"primitive": "cube", "base_color": [0.9, 0.13, 0.13],
              "emissive": [0.7, 0.0, 0.0], "roughness": 0.4},
     "body": {"type": "kinematic", "sensor": True}})

# ---------------------------------------------------------------- HUD + game manager
add({"name": "hud_score", "ui": {"kind": "panel", "anchor": "top-left",
     "pos": [0.03, 0.03], "size": [0.24, 0.08], "color": [0, 0, 0, 0.45],
     "text": "Orbs 0 / 8", "text_size": 26}})
add({"name": "hud_time", "ui": {"kind": "panel", "anchor": "top-right",
     "pos": [0.03, 0.03], "size": [0.20, 0.08], "color": [0, 0, 0, 0.45],
     "text": f"{int(TIME_LIMIT)}s", "text_size": 26}})
add({"name": "hud_banner", "ui": {"kind": "panel", "anchor": "center",
     "pos": [0, 0], "size": [0.5, 0.14], "color": [0.05, 0.05, 0.08, 0.8],
     "text": "", "text_size": 44, "visible": False}})
add({"name": "hud_help", "ui": {"kind": "text", "anchor": "bottom",
     "pos": [0, 0.04], "size": [0.5, 0.05],
     "text": "WASD move   Space jump   dodge the red bar", "text_size": 18,
     "text_color": [1, 1, 1, 0.6]}})

add({"name": "manager", "behavior": [
    {"on": "start", "do": [
        {"action": "setState", "key": "orbs", "value": 0},
        {"action": "setState", "key": "over", "value": 0},
        {"action": "setUI", "target": "hud_score", "text": "Orbs ${orbs} / 8"},
        {"action": "timer", "after": 10, "event": "t30"},
        {"action": "timer", "after": 20, "event": "t20"},
        {"action": "timer", "after": 30, "event": "t10"},
        {"action": "timer", "after": 35, "event": "t5"},
        {"action": "timer", "after": TIME_LIMIT, "event": "timeup"}]},

    {"on": "event", "name": "grab", "do": [
        {"action": "setUI", "target": "hud_score", "text": "Orbs ${orbs} / 8"}]},
    {"on": "event", "name": "grab", "if": {"key": "orbs", "gte": ORBS}, "do": [
        {"action": "emit", "event": "win"}]},

    {"on": "event", "name": "t30", "if": {"key": "over", "lt": 1}, "do": [{"action": "setUI", "target": "hud_time", "text": "30s"}]},
    {"on": "event", "name": "t20", "if": {"key": "over", "lt": 1}, "do": [{"action": "setUI", "target": "hud_time", "text": "20s"}]},
    {"on": "event", "name": "t10", "if": {"key": "over", "lt": 1}, "do": [{"action": "setUI", "target": "hud_time", "text": "10s"}]},
    {"on": "event", "name": "t5",  "if": {"key": "over", "lt": 1}, "do": [{"action": "setUI", "target": "hud_time", "text": "5s!"}]},

    {"on": "event", "name": "timeup", "if": {"key": "orbs", "lt": ORBS}, "do": [{"action": "emit", "event": "lose"}]},
    {"on": "event", "name": "hit", "if": {"key": "over", "lt": 1}, "do": [{"action": "emit", "event": "lose"}]},

    {"on": "event", "name": "win", "if": {"key": "over", "lt": 1}, "do": [
        {"action": "setState", "key": "over", "value": 1},
        {"action": "setUI", "target": "hud_banner", "text": "ALL ORBS!  YOU WIN", "visible": True},
        {"action": "setUI", "target": "hud_time", "text": "done"}]},
    {"on": "event", "name": "lose", "if": {"key": "over", "lt": 1}, "do": [
        {"action": "setState", "key": "over", "value": 1},
        {"action": "setUI", "target": "hud_banner", "text": "GAME OVER", "visible": True}]}
]})

scene = {
    "environment": {"ssao": True, "ssao_intensity": 1.2,
                    "hdri": "assets/hdri/studio_1k.hdr", "hdri_intensity": 0.7},
    "input": {"fwd": ["W", "Up"], "back": ["S", "Down"],
              "left": ["A", "Left"], "right": ["D", "Right"], "jump": ["Space"]},
    "entities": ents,
}

out = pathlib.Path(__file__).with_name("orbrun.json")
out.write_text(json.dumps(scene, indent=1))
print(f"wrote {out}  ({len(ents)} entities)")
