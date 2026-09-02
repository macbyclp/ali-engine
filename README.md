# ali-engine

Yapay zekanın **uçtan uca yönetebildiği** 3B oyun motoru. GUI editör yok — bir AI ajanı
sahneyi, ışıkları, kamerayı, malzemeleri makine arayüzünden kurar ve render edilen kareyi
görüntü olarak geri alır.

Native, C++20, OpenGL 4.5. Mimari ve yol haritası: [ARCHITECTURE.md](ARCHITECTURE.md).
AI kontrol protokolü: [docs/AI-PROTOCOL.md](docs/AI-PROTOCOL.md).

## Durum
**M1 ✅** AI-sürülebilir çekirdek: JSON sahne + hot-reload, stdin/stdout komut protokolü
(`entity.*`, `light.set`, `camera.*`, `scene.*`), `observe.screenshot` → PNG,
prosedürel primitifler + glTF geometri.

**M2 ✅** Render kalitesi: metallic-roughness PBR (Cook-Torrance), prosedürel-sky
IBL yaklaşımı, yönlü gölge haritası (2048, PCF 3×3), HDR RGBA16F + ACES tonemap.

**M3 ✅** Fizik: Jolt Physics entegrasyonu, RigidBody component (static/dynamic/kinematic,
box/sphere), ECS↔Jolt senkron + transform geri-yazma, `world.step`, `physics.play/pause`,
`physics.raycast`, `physics.setGravity`.

**M4 ✅** Davranış: veri-güdümlü `Behavior` component (JSON kurallar), tetikleyiciler
`start`/`tick`/`collision`/`event`, aksiyonlar (impulse, setVelocity, spin, moveToward,
setMaterial, spawn, destroy, emit), Jolt contact event'leri, `behavior.set/get`, `event.emit`.

**M5 ✅** Ölçek: frustum culling (job-parallel, mesh bounding sphere), aynı mesh'i tek
`glDrawElementsInstanced` çağrısında toplayan GPU instancing, paylaşımlı mesh önbelleği,
thread pool (`JobSystem`), `observe.stats` (entities/visible/culled/draw_calls/cpu_ms).
2000+ obje → 2 draw call.

**M7 ✅** Materyal & doku: stb_image doku yükleme (sRGB/linear), mipmap + anizotropik,
doku önbelleği, tangent hesaplama, normal mapping (TBN), emissive, AO, uv_scale.
glTF PBR materyal import (baseColor/normal/metallic-roughness/emissive/occlusion,
gömülü .glb dokular dahil). Prosedürel test dokuları (`builtin:checker/grid/bumps/...`).
Materyale göre gruplama → instancing korunur.

**M8 ✅** Sahne grafı & prefab: `Hierarchy` component + parent/child, `WorldTransform`
(update_world_transforms, keyfi derinlik), `entity.setParent`, `entity.spawn {parent}`.
Prefab = JSON alt-ağaç: `prefab.save`, `prefab.instantiate` (isim önekleme + kök konumlama).
glTF çok-node hiyerarşi: node transformları vertex'lere baked, çok-parçalı modeller korunur.

**M9 ✅** Animasyon: skinned mesh (Vertex joint/weight), `Skeleton` (bind pose + inverse
bind), `AnimationClip` (T/R/S kanalları, lineer + slerp interp), `AnimationPlayer` component,
GPU skinning (128 kemik, ayrı çizim yolu). glTF skin + animasyon import. Prosedürel test
modeli `builtin:bendbar`. `animation.play/pause/stop/list`.

**M10 ✅** Işık & gölge: `PunctualLight` component (point + spot), forward çoklu ışık
döngüsü (16'ya kadar), mesafe attenuation, spot konisi (smoothstep yumuşak kenar),
`light.add {type:"point"|"spot"}`. Yönlü ışık gölge haritası korunuyor (point/spot gölge
= gelecek).

**M11 ✅** Karakter & navigasyon: `CharacterController` (Jolt `CharacterVirtual` — kapsül,
move & slide, yerçekimi, zemin algılama, eğim limiti, zıplama), `character.create/move/
jump/moveTo`. Grid-tabanlı navmesh (`NavGrid`): statik gövdelerden bake, 8-yön A*,
`nav.bake` / `nav.path`. `character.moveTo` yol takibi + yön dönüşü.

**M12 ✅** Ses & partikül & post: **bloom** (bright-pass + ayrık gaussian + kompozit),
**exposure + vignette**. **CPU partikül sistemi** (`ParticleEmitter`: rate/lifetime/
velocity spread/gravity/renk-boyut lerp), additive camera-facing billboard'lar,
`particles.emit/stop`. **Uzamsal ses** (miniaudio): `audio.play/stop`, kamera-takipli
dinleyici, 3B konumlandırma.

## Build (Windows)
```
py -m pip install jinja2
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

## Çalıştır
```
build\Debug\engine.exe --scene scenes/demo.json                 # görünür pencere
build\Debug\engine.exe --headless --scene scenes/demo.json      # sadece AI + screenshot
python tools/drive.py                                           # örnek AI sürücüsü
```

## Yol haritası
M1 çekirdek ✅ · M2 PBR+IBL+gölge ✅ · M3 Jolt fizik ✅ · M4 davranış ✅ · M5 ölçek ✅ ·
M6 Vulkan RHI ⏸️ · M7 materyal & doku ✅ · M8 sahne grafı & prefab ✅ · M9 animasyon ✅ ·
M10 ışık & gölge ✅ · M11 karakter & navigasyon ✅ · **M12 ses & partikül & post ✅**

**M1–M12 tamamlandı** (M6 Vulkan hariç, ertelendi).
