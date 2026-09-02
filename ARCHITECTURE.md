# ali-engine — Mimari

## Vizyon
Yapay zekanın **uçtan uca yönetebildiği** 3B oyun motoru. Unreal'de bir insanın editörle
yaptığı her şeyi (sahne kurma, obje yerleştirme, malzeme ayarı, ışık, kamera, oynanış
mantığı) bir AI ajanı **makine arayüzü** üzerinden yapar. GUI editör yok.

Bunun için AI'ın üç şeye ihtiyacı var:
1. **Yazma** — sahneyi ve davranışları deterministik, denetlenebilir biçimde değiştirmek
2. **Okuma** — dünyanın o anki durumunu sorgulamak (sahne graf, transformlar, fizik)
3. **Görme** — render edilmiş kareyi görüntü olarak almak (headless screenshot)

## Teknoloji
| Alan | Seçim | Neden |
| --- | --- | --- |
| Dil | C++20 | Unreal de C++; concepts/span/ranges işe yarıyor |
| Render | OpenGL 4.5 core (DSA, bindless-ready) | Hızlı ilerleme, PBR rahat; sonra Vulkan RHI eklenebilir |
| Pencere | GLFW (headless modda gizli/EGL) | Zaten var |
| Matematik | GLM | Zaten var |
| ECS | EnTT | Olgun, header-only, hızlı |
| Sahne/veri | nlohmann/json | AI'ın yazması/okuması kolay |
| Model | cgltf + stb_image | glTF 2.0, hafif |
| Fizik | Jolt Physics | Modern, deterministik, AAA'de kullanılıyor |
| Kontrol | JSON-RPC 2.0 / TCP soket + dosya izleme | Dil-bağımsız, denetlenebilir, hem canlı hem dosya-tabanlı |

## Kontrol yüzeyi (AI ↔ motor)
İki yol, ikisi de aynı komut setini kullanır:

**A. Sahne dosyaları + hot-reload.** `scenes/*.json` gerçeğin kaynağı. AI dosyayı yazar,
motor değişikliği izler ve anında yeniden yükler. Versiyonlanabilir, diff'lenebilir, geri alınabilir.

**B. Canlı komut soketi.** Motor `127.0.0.1:8787` üzerinde JSON-RPC sunucusu. AI ajanı
çalışırken komut yollar:
- `scene.load`, `scene.save`, `scene.reset`
- `entity.spawn` / `entity.destroy` / `entity.list`
- `entity.setTransform` / `entity.setMaterial` / `entity.setParent`
- `light.add` / `light.set`
- `camera.set` / `camera.get`
- `physics.raycast` / `physics.setGravity` / `world.step`
- `observe.screenshot` → PNG (base64 veya dosya yolu) — **AI'ın gözü**
- `observe.state` → tüm sahne graf JSON olarak

## Katmanlar
```
  ai-control/     JSON-RPC sunucu, dosya izleyici, komut yönlendirici
  scene/          JSON <-> ECS serileştirme, sahne graf, prefab
  ecs/            EnTT dünyası, component tanımları, sistemler
  render/         GL45 backend, PBR, gölge, IBL, HDR, headless FBO -> PNG
  physics/        Jolt sarmalayıcı, ECS senkronu
  assets/         glTF/doku yükleme, önbellek
  core/           pencere, döngü, zaman, log, matematik
```

## Yol haritası
- **M1 — AI-sürülebilir çekirdek (şimdi):** JSON sahne formatı, hot-reload, JSON-RPC soket,
  `entity.spawn/setTransform/list`, `camera.set`, `observe.screenshot`, `observe.state`.
  glTF mesh + tek yönlü ışık + düz gölgesiz render. AI bir sahne kurup bakabiliyor.
- **M2 — Görüntü kalitesi:** metallic-roughness PBR, IBL, gölge haritası, HDR + tonemap.
- **M3 — Dünya:** Jolt fizik, raycast, karakter kontrolcü, `physics.*` komutları.
- **M4 — Davranış:** component-tabanlı script/davranış sistemi, olay kuyruğu, AI'ın
  davranış grafı basabilmesi.
- **M5 — Ölçek:** frustum culling, instancing, asset streaming, iş parçacığı havuzu.
- **M6 — Vulkan RHI (opsiyonel):** render katmanını soyutla, Vulkan backend.

## İlkeler
- **Headless her zaman çalışır.** Pencere olmadan render + screenshot alınabilir (CI, AI döngüsü).
- **Deterministik.** Aynı sahne + aynı komutlar = aynı sonuç. Sabit adımlı simülasyon.
- **Her şey veri.** Kod dışı her durum JSON'a serileşir; AI onu üretebilir/denetleyebilir.
- **Komut = tek giriş noktası.** Hot-reload de soket de aynı komut işleyicisine iner.
