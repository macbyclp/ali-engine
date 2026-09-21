# ali-engine — Mimari

## Vizyon
Yapay zekanın **uçtan uca yönetebildiği** 3B oyun motoru. Unreal'de bir insanın editörle
yaptığı her şeyi (sahne kurma, obje yerleştirme, malzeme ayarı, ışık, kamera, oynanış
mantığı) bir AI ajanı **makine arayüzü** üzerinden yapar. İnsanlar için ImGui tabanlı bir
editör de vardır (`src/editor/`); editör de AI ile aynı `eng::dispatch` komut yolunu kullanır.

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
| Kontrol | Satır-bazlı JSON (stdin/stdout) + dosya izleme | Dil-bağımsız, denetlenebilir, hem canlı hem dosya-tabanlı |

## Kontrol yüzeyi (AI ↔ motor)
İki yol, ikisi de aynı komut setini kullanır:

**A. Sahne dosyaları + hot-reload.** `scenes/*.json` gerçeğin kaynağı. AI dosyayı yazar,
motor değişikliği izler ve anında yeniden yükler. Versiyonlanabilir, diff'lenebilir, geri alınabilir.

**B. Canlı komut kanalı.** Motor stdin/stdout üzerinde satır-bazlı JSON komutları okur
(`src/aicontrol/channel.cpp`; soket yok). AI ajanı çalışırken komut yollar:
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
  ai-control/     stdin/stdout komut kanalı, dosya izleyici, komut yönlendirici
  scene/          JSON <-> ECS serileştirme, sahne graf, prefab
  ecs/            EnTT dünyası, component tanımları, sistemler
  render/         GL45 backend, PBR, gölge, IBL, HDR, headless FBO -> PNG
  physics/        Jolt sarmalayıcı, ECS senkronu
  assets/         glTF/doku yükleme, önbellek
  core/           pencere, döngü, zaman, log, matematik
```

## Yol haritası
- **M1 — AI-sürülebilir çekirdek ✅:** JSON sahne formatı, hot-reload, stdin/stdout JSON
  satır protokolü, `entity.*`, `camera.*`, `observe.screenshot`, `scene.state`.
  glTF mesh + prosedürel primitifler.
- **M2 — Görüntü kalitesi ✅:** metallic-roughness PBR, prosedürel-sky IBL yaklaşımı,
  yönlü gölge haritası (PCF), HDR RGBA16F + ACES tonemap.
- **M3 — Dünya ✅:** Jolt fizik, RigidBody, raycast, `world.step`, `physics.*` komutları.
- **M4 — Davranış ✅:** veri-güdümlü Behavior component, tetikleyiciler (start/tick/
  collision/event), aksiyonlar, contact event kuyruğu.
- **M5 — Ölçek ✅:** frustum culling (job-parallel), GPU instancing, mesh önbelleği,
  thread pool, `observe.stats`.
- **M6 — Vulkan RHI:** ⏸️ ertelendi (2026-09-02). GL 4.5 backend yeterli; Vulkan'ın
  getirisi (çok iş parçacıklı komut kaydı, düşük sürücü yükü) şu an gerekli değil.
  Gelecek: render katmanını `GraphicsBackend` arayüzüne ayır, Vulkan backend ekle.

## İlkeler
- **Headless her zaman çalışır.** Pencere olmadan render + screenshot alınabilir (CI, AI döngüsü).
- **Deterministik.** Aynı sahne + aynı komutlar = aynı sonuç. Sabit adımlı simülasyon.
- **Her şey veri.** Kod dışı her durum JSON'a serileşir; AI onu üretebilir/denetleyebilir.
- **Komut = tek giriş noktası.** Hot-reload de stdin kanalı da aynı komut işleyicisine iner.

## Notlar: v0.1.3 sonrası yapısal değişiklikler
- **Dönüş = kuaterniyon.** `Transform::rotation` (glm::quat) tek gerçek kaynaktır; Euler derece (`euler_deg()` /
  `set_euler_deg()`, R = Rz·Ry·Rx) yalnızca bir görünümdür ve JSON/editör/AI protokolünde eski alan olarak sürer.
  Fizik Jolt'un kuaterniyonunu doğrudan yazar (Euler'e dönüşüm yok).
- **Komut tablosu.** `aicontrol/commands.cpp` artık bir `metot -> işleyici` tablosu (sıralı `std::map`);
  `commands.list` bu tablodan üretilir. Eklentiler tabloda olmayan metotlarda devreye girer.
- **Dünya transformu önbelleği.** `WorldTransform`, kendini üreten yerel pozu + ebeveyn sürümünü saklar;
  `update_world_transforms()` yalnızca girdisi değişen matrisi yeniden kurar (dirty bayrağı yok).
- **Artımlı mesh çözümleme.** `Scene::resolve_gpu_mesh(entity)`; tüm sahne yalnızca yüklemede çözülür.
- **Delta undo.** `editor/history.*`: entity başına önce/sonra JSON, hareket bittiğinde diff; boşta kare maliyeti 0.
- **Saydam geçiş.** `MeshRenderer::alpha < 1` -> opak geçişten sonra arkadan öne sıralı blend; gölge/SSAO dışı.
- **Ekransız headless.** `core/window.cpp`: dlopen'lı EGL (pbuffer) bağlamı; olmazsa gizli GLFW penceresi.
- **Güvenlik.** Yazma kökü + `plugin.load` bayrağı (bkz. docs/AI-PROTOCOL.md).
