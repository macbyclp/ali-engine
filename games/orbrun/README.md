# ORB RUN

A tiny time-attack collectathon built **entirely on the ali-engine JSON scene
format** — no C++, no plugin. Grab all 8 orbs in 40 seconds; touch the sweeping
red bar and it's game over.

```
build\Debug\engine.exe --play --scene games/orbrun/orbrun.json
```

- **WASD / arrows** — roll · **Space** — jump · third-person follow camera
- Orbs, timer, win/lose banner are all `behavior` rules + `ui` panels reacting to
  `enter` triggers and `timer` events
- The level (floor, walls, fluted pillars) is procedural CSG; the hazard is a
  kinematic sensor parented to a spinning pivot
- Lighting: studio HDRI + SSAO + shadows

`build.py` regenerates `orbrun.json` (tweak arena size, orb count, timer there).

## What it exercises

named-action input · trigger volumes (`enter`) · behaviour events + timers +
conditions · game state · screen UI with `${state}` interpolation · camera
follow rig · procedural mesh colliders · HDRI/SSAO/shadows.
