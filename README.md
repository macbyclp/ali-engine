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
M6 Vulkan RHI ⏸️ · M7 materyal & doku ✅ · **M8 sahne grafı & prefab ✅** · M9 animasyon ·
M10 ışık & gölge · M11 karakter & navigasyon · M12 ses & partikül & post
