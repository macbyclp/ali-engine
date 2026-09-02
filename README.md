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
M1 çekirdek ✅ · M2 PBR+IBL+gölge ✅ · M3 Jolt fizik ✅ · M4 davranış sistemi · M5 ölçek · M6 Vulkan RHI
