# ali-engine vs. Godot 4.3

An honest, current (v0.1.3) comparison. Godot is a mature general-purpose engine;
ali-engine is a young one built around a single idea — **an AI agent driving the
whole loop, deterministically, headless, over one JSON format**.

If you want to ship a real game today, use Godot. If the workflow is "an AI
generates and iterates on the game, a human reviews it," this is what ali-engine
is for.

## Where ali-engine leads

| Area | ali-engine v0.1.3 | Godot / Unreal | Why it matters |
|---|---|---|---|
| JSON command protocol | ✅ ~80 commands, stdin/stdout | ❌ neither | an LLM drives the engine directly |
| Headless render + screenshot | ✅ first-class (`--headless`) | ⚠️ rough / indirect | CI, agent loops, batch generation |
| Segmentation buffer (per-pixel entity id) | ✅ `observe.segment` | ❌ | "the left third of the screen is an enemy" |
| Screen ray → entity | ✅ `observe.pick` | ⚠️ manual | "click on that" |
| LLM scene summary + spatial relations | ✅ `observe.describe` | ❌ | "the ball is on the box" |
| Determinism + record/replay | ✅ bit-identical verified | ⚠️ not guaranteed | reproducible tests / generation |
| One serialisation format | ✅ single human-readable JSON | ⚠️ `.tscn` + binary + import cache | the AI emits it verbatim |
| AI + human on one model | ✅ editor / Blueprint emit the AI's commands | ❌ | shared authoring |
| Visual scripting | ✅ Blueprint (compiles to & from behaviour JSON) | ❌ Godot 4 removed theirs | the AI's logic shows up as a graph |
| Virtual input (AI plays its own game) | ✅ `input.set` | ❌ | same game: human at a window or agent over JSON |
| Extensibility | ✅ ~13k LOC, plugin API | ⚠️ large codebase | a feature ships in one session |

## Where ali-engine is behind

| Area | ali-engine | Godot 4.3 | Impact | Priority |
|---|---|---|---|---|
| Cross-platform | ❌ Windows x64 only | ✅ Win/Mac/Linux/mobile/web/console | can't distribute a game | 🔴 |
| Anti-aliasing | ✅ FXAA | ✅ MSAA / TAA / FXAA | (MSAA would be sharper) | 🟢 done |
| Audio | ✅ buses, streaming, pitch, fades | ✅ + DSP effect chains | no reverb / EQ nodes yet | 🟡 |
| Animation depth | ⚠️ state machine + cross-fade | ✅ + blend trees + IK + root motion | hard to do a polished character | 🔴 |
| Navmesh | ⚠️ grid A* + LOS smoothing | ✅ Recast + agent avoidance | complex level navigation | 🟡 |
| Global illumination | ❌ | ✅ SDFGI / lightmaps / probes | lighting looks flat | 🟡 |
| Custom shaders / shader graph | ❌ one fixed PBR | ✅ shader language + visual graph | limited visual identity | 🟡 |
| IBL | ⚠️ mip-blur approximation | ✅ prefiltered + probes | reflections are coarse | 🟡 |
| SSR / SSIL | ❌ | ✅ | glass / water / metal | 🟡 |
| Particles | ⚠️ CPU billboards | ✅ GPU + collision + mesh particles | weak FX scenes | 🟡 |
| Asset pipeline | ❌ no FBX / import settings / compression | ✅ full | external content is hard | 🟡 |
| 2D | ❌ none | ✅ full 2D engine | no 2D games | ⚪ |
| Ecosystem | ❌ no community / docs site / asset store | ✅ huge | learning & support | ⚪ |
| Multiplayer / networking | ❌ | ✅ high-level API | no online games | ⚪ |
| Profiler / debugger | ❌ | ✅ | optimisation is blind | ⚪ |
| Build / CI / tests | ⚠️ single config, no CI, no tests | ✅ | fragility | ⚪ |
| Decals / LOD / occlusion culling | ❌ | ✅ | large-scene perf | ⚪ |
| Vulkan / render graph / threading | ❌ OpenGL, single-threaded main loop | ✅ Vulkan + multithreaded | perf ceiling | ⚪ |
| Light limits | ⚠️ 16 forward / 2 point-shadow / 4 spot-shadow | ✅ clustered, hundreds | dense lighting | ⚪ |

🔴 blocks real games · 🟡 quality ceiling · ⚪ ecosystem / long-term · 🟢 addressed

## Bottom line

**Godot** — mature, multi-platform, deep in every subsystem, enormous ecosystem.

**ali-engine** — narrow but sharp: the AI-native, headless, deterministic,
single-JSON engine. On that axis it does things Godot doesn't (id buffer, command
protocol, record/replay, one shared model, still has visual scripting). Off that
axis it's missing platforms, GI, advanced animation, custom shaders.
