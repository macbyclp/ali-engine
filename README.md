# ali-engine

Yapay zekanın **uçtan uca yönetebildiği** 3B oyun motoru. GUI editör yok — bir AI ajanı
sahneyi, ışıkları, kamerayı, malzemeleri makine arayüzünden kurar ve render edilen kareyi
görüntü olarak geri alır.

Native, C++20, OpenGL 4.5. Mimari ve yol haritası: [ARCHITECTURE.md](ARCHITECTURE.md).
AI kontrol protokolü: [docs/AI-PROTOCOL.md](docs/AI-PROTOCOL.md).

## Durum — M1 (AI-sürülebilir çekirdek)
- JSON sahne formatı + dosya hot-reload
- stdin/stdout JSON satır protokolü: `entity.spawn/destroy/setTransform/setMaterial`,
  `light.set`, `camera.set/get`, `scene.load/save/reset/state`
- `observe.screenshot` → PNG (headless FBO, AI'ın gözü)
- Prosedürel primitifler (cube/sphere/plane) + glTF mesh yükleme (geometri)
- Tek yönlü ışık, Lambert + ambient, gamma. **PBR M2'de.**

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
M1 çekirdek ✅ · M2 PBR+IBL+gölge · M3 Jolt fizik · M4 davranış sistemi · M5 ölçek · M6 Vulkan RHI
