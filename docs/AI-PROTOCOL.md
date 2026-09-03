# AI Kontrol Protokolü (M1)

Motor, satır-bazlı JSON ile konuşur: **stdin'e istek satırı**, **stdout'a yanıt satırı**.
Loglar stderr'e gider, stdout temiz kalır.

## Çalıştırma
```
engine --headless --scene scenes/demo.json
engine --scene scenes/demo.json            # görünür pencere + aynı protokol
```

## İstek
```json
{"id": 1, "method": "entity.spawn", "params": {"name": "box", "position": [0,1,0]}}
```
`id` isteğe bağlı, yanıtta aynen döner. `params` isteğe bağlı.

## Yanıt
```json
{"id": 1, "ok": true, "result": {"name": "box"}}
{"id": 1, "ok": false, "error": "no such entity: box"}
```

## Olaylar (motor kendiliğinden yollar)
```json
{"event": "ready", "headless": true, "scene": "scenes/demo.json"}
{"event": "scene.reloaded", "path": "scenes/demo.json"}
```

## Metotlar
| Metot | params | sonuç |
| --- | --- | --- |
| `ping` | — | `{pong:true}` |
| `scene.load` | `{path}` | `{entities:N}` |
| `scene.save` | `{path?}` | `{path}` |
| `scene.reset` | — | — |
| `scene.state` | — | tüm sahne (JSON) |
| `entity.list` | — | `{names:[...]}` |
| `entity.spawn` | `{name?, primitive?, gltf_path?, build?, position?, rotation?, scale?, <material>, body?}` | `{name}` |
| `entity.destroy` | `{name}` | — |
| `mesh.build` | `{name, build:[step,...]}` | `{triangles}` — prosedürel mesh / CSG (aşağı bkz.) |
| `terrain.create` | `{name?, size?, resolution?, height?, octaves?, frequency?, seed?, <material>}` | `{name, resolution}` |
| `terrain.sculpt` | `{name, at:[x,_,z], radius?, strength?, mode?}` — mode: `raise`\|`lower`\|`smooth`\|`flatten` | — |
| `terrain.height` | `{name, at:[x,_,z]}` | `{height}` — o noktadaki zemin yüksekliği |
| `entity.setTransform` | `{name, position?, rotation?, scale?}` | — (fizik gövdesi de ışınlanır) |
| `entity.setMaterial` | `{name, <material>}` | — |
| `entity.setBody` | `{name, type?, shape?, mass?, restitution?, friction?}` | — |
| `entity.setParent` | `{name, parent}` (boş parent = ayır) | — |
| `prefab.save` | `{root, path}` | `{path, entities}` |
| `prefab.instantiate` | `{path, name, position?}` | `{created:[...]}` |
| `animation.play` | `{name, clip?, speed?, loop?, restart?, fade?}` | — (`fade` sn → önceki klipten crossfade) |
| `animation.pause` / `animation.stop` | `{name}` | — |
| `animation.list` | `{name}` | `{clips:[...]}` |
| `animator.set` | `{name, entry?, states:[{name, clip?, speed?, loop?}], transitions:[{from?, to, blend?, exit_time?, when:[{param, op, value}]}], params?}` | `{states}` |
| `animator.param` | `{name, param, value}` veya `{name, params:{...}}` | — |
| `animator.get` | `{name}` | `{current, entry, params, ...}` |
| `character.create` | `{name, position?, radius?, height?, move_speed?, jump_speed?, base_color?}` | — |
| `character.move` | `{name, direction:[x,y,z], speed?}` | — |
| `character.jump` | `{name}` | — |
| `character.moveTo` | `{name, target:[x,y,z]}` | `{waypoints}` — navmesh yolu |
| `nav.bake` | `{min?, max?, cell?}` | — statik/kinematik gövdelerden + mesh sınırlarından grid |
| `nav.path` | `{from, to}` | `{waypoints:[...]}` — A* + görüş-hattı düzleştirme (string-pull) |
| `particles.emit` | `{name, position?, rate?, lifetime?, velocity?, velocity_spread?, gravity?, start_color?, end_color?, start_size?, end_size?}` | — |
| `particles.stop` | `{name}` | — |
| `audio.play` | `{file, volume?, loop?, spatial?, position?, bus?}` | `{handle}` |
| `audio.stop` | `{handle}` veya `{bus}` (bus'taki tüm sesler) | — |
| `audio.bus` | `{bus, volume?}` — mikser bus'u (`master` = ana çıkış) | `{bus, volume}` |
| `render.set` | `{ssao?, ssao_radius?, ssao_intensity?, ...}` — sahne `environment` bloğuna yazar | `{environment}` |
| `render.get` | — | `{environment}` |
| `input.map` | `{action, keys}` veya `{bindings:{action:[keys]}}` | `{bindings}` |
| `input.unmap` | `{action?}` (yoksa hepsi) | — |
| `input.state` | — | `{actions:{a:{down,pressed,released}}, mouse, mouse_delta}` |
| `input.set` | `{action, down?}` veya `{actions:{a:bool}}` | — sanal girdi (AI kendi oyununu oynar) |
| `plugin.list` | — | `{plugins:[{name, version, dll}]}` |
| `plugin.load` | `{path}` | `{name}` — bir eklenti kütüphanesi yükler (bkz. docs/PLUGINS.md) |
| _(eklenti metotları)_ | — | çekirdek tanımadığı metotlar eklentilere düşer |
| `ui.add` / `ui.set` | `{name, kind?, anchor?, pos?, size?, color?, fill_color?, text?, text_size?, text_color?, value?, visible?, order?}` | `{name}` |
| `ui.remove` | `{name}` | — |
| `observe.view` | `{position?, target?, fov_deg?, width?, height?, path?}` | `{path,w,h}` — sahne kamerası bozulmaz |
| `observe.entities` | — | `{entities:[{name,position,distance,in_view,screen}]}` |
| `observe.pick` | `{screen:[x,y]}` (piksel) veya `{ndc:[x,y]}`, `width?`, `height?`, `max_distance?` | `{hit, entity?, point, normal, distance}` — kameradan ışın; önce fizik gövdeleri, gövdesiz meshler için sınır-küresi |
| `state.set` / `state.get` / `state.list` / `state.clear` | `{key?, value?}` | — / `{value}` / tüm state / — |
| `timer.after` | `{seconds, event}` | — tek seferlik → event |
| `checkpoint.save` / `checkpoint.restore` | `{name?}` | — (sahne + state anlık görüntüsü) |
| `light.set` / `light.add` | `{name?, type?, color?, intensity?, direction?, position?, range?, inner_deg?, outer_deg?}` | `{name}` |
| `camera.set` | `{position?, target?, fov_deg?}` | — |
| `camera.get` | — | `{position, target, fov_deg}` |
| `world.step` | `{dt?, steps?, substeps?}` | `{stepped, dt}` — simülasyonu N adım ilerlet |
| `physics.play` / `physics.pause` | — | — (her frame otomatik adım) |
| `physics.setGravity` | `{gravity:[x,y,z]}` | — |
| `physics.getGravity` | — | `{gravity}` |
| `physics.raycast` | `{origin, direction, max_distance?}` | `{hit, point?, normal?, distance?, entity?}` |
| `physics.overlapSphere` | `{center, radius}` | `{entities:[isim]}` — broad-phase AABB yaklaşımı |
| `physics.spherecast` | `{origin, direction, radius, max_distance?}` | `{hit, entity?, point?, normal?, distance?}` — Jolt CastShape |
| `joint.create` | `{a, b?, type, point?, axis?, min?, max?, length?, stiffness?, damping?}` | `{a, b, type}` — `b` boş = dünyaya sabitle |
| `joint.remove` | `{a?}` veya `{b?}` (ikisi de yoksa hepsi) | `{removed:N}` |
| `behavior.set` | `{name, behaviors:[...]}` | — |
| `behavior.get` | `{name}` | `{rules}` |
| `event.emit` | `{event}` | — (bir sonraki step'te işlenir) |
| `observe.segment` | `{path?, width?, height?}` | `{colorKey:{"idx":name}, colors:{"r,g,b":name}, path, width, height}` — her mesh düz benzersiz renk (entity kimliği) |
| `observe.depth` | `{path?, width?, height?, near?, far?}` | `{path, width, height, near, far}` — lineer derinlik greyscale (yakın = beyaz); near/far verilmezse görünür geometriye oturtulur |
| `observe.describe` | — | `{camera:{position,forward}, entities:[{name,kind,position,size,on_screen}], relations:[{a,rel,b}]}` — LLM için sahne özeti |
| `observe.screenshot` | `{path?, width?, height?}` | `{path, width, height}` |
| `observe.stats` | — | `{entities, visible, culled, draw_calls, instances, groups, cpu_ms}` |
| `record.start` | `{path?}` | `{path}` — bundan sonraki her istek satırını dosyaya yazar |
| `record.stop` | — | `{path}` |
| `record.play` | `{path}` | `{played, failed}` — kaydı yeniden oynatır (fizik dünyası önce sıfırlanır → deterministik) |
| `quit` | — | — |

### Davranış (`behavior`)
`behavior.set` veya `entity.spawn`/`spawn` aksiyonu içinde `behavior`. Kural dizisi:
```json
[
  {"on": "start",     "do": [{"action": "impulse", "impulse": [4,3,0]}]},
  {"on": "tick",      "do": [{"action": "spin", "axis": [0,1,0], "speed_deg": 60}]},
  {"on": "collision", "with": "floor", "do": [{"action": "setColor", "color": [1,0,0]}]},
  {"on": "event", "name": "burst", "do": [{"action": "spawn", "primitive": "sphere", "position": [0,5,0]}]}
]
```
Aksiyonlar: `log`, `setVelocity {velocity}`, `impulse {impulse}`, `spin {axis,speed_deg}`,
`moveToward {target,speed}`, `setMaterial/setColor {color,metallic,roughness}`,
`spawn {…entity params…, relative?}`, `destroy {target?}`, `emit {event}`,
`setState {key,value}`, `addState {key,value}`, `timer {after,event}`,
`setUI {target, text?, value?, visible?}` (`text` içinde `${key}` state ile değişir).
Kurallara `"if": {"key":"phase","eq":"combat"}` (eq/ne/gt/gte/lt/lte/exists) koşulu eklenebilir.
Kurallar `world.step` ve `physics.play` sırasında her adımda değerlendirilir.

### Materyal (`<material>` alanları)
`{ base_color:[r,g,b], metallic, roughness, emissive:[r,g,b], uv_scale:[u,v],
base_color_map, normal_map, metallic_roughness_map, emissive_map, ao_map }`.
Doku anahtarı = dosya yolu **veya** `builtin:<checker|grid|uv|normal|bumps>`.
glTF yüklenince dosyanın materyali otomatik gelir; verdiğin alanlar üzerine yazar.

### Animasyon (`primitive: "skinned"`)
`entity.spawn {primitive: "skinned", gltf_path: "<.glb|.gltf>" veya "builtin:bendbar",
animation: {clip, speed?, loop?}}`. glTF skin + klipleri otomatik yüklenir. Sonra
`animation.play/pause/stop`, `animation.list` ile klip adları.

**Animasyon durum makinesi.** `animator.set` ile durum/geçiş grafiği tanımlanır:
her durum bir klip, her geçiş `when` koşulları sağlanınca tetiklenir (ilk eşleşen
kazanır) ve `blend` saniyede crossfade yapar. `from` boş/`"*"` = her durumdan.
`op`: `> < >= <= == !=` veya `trigger` (tetikleyen geçiş param'ı 0'a çeker).
`exit_time` (0–1): geçiş için klibin o orana ulaşması da gerekir. Parametreleri
`animator.param` veya davranış aksiyonu `animParam {target?, param, value}` ayarlar.
Grafik + parametreler sahne JSON'una `animator` bloğu olarak serialize edilir.

### Prosedürel mesh + CSG (`primitive: "procedural"`)
`entity.spawn {build: [...]}` veya `mesh.build {name, build: [...]}`. `build` bir
adım dizisidir; ilk adım tabanı kurar, sonrakiler `op` ile birleşir:
`add`/`union`, `subtract`, `intersect` (BSP boolean) veya `merge` (ucuz birleştirme).
Her adım bir `shape` + parametreleri + opsiyonel `translate`/`rotate`/`scale`:
- `box {size:[x,y,z]}`
- `sphere {radius, segments?}`
- `cylinder {radius, height, segments?, capped?}`
- `cone {radius, height, segments?}`
- `torus {radius, tube, segments?, sides?}`
- `plane {size:[x,z], subdiv?}`

Örnek — delikli blok: `[{"shape":"box","size":[2,2,2]},
{"op":"subtract","shape":"cylinder","radius":0.6,"height":3}]`. Tarif sahne
JSON'una `mesh.build` olarak serialize edilir; yeniden yüklenince yeniden üretilir.

### Ortam / post (`environment` bloğu)
Sahne JSON'unun üst düzey `environment` objesi frame ayarlarını tutar; `render.set`
ile de yazılır. Anahtarlar: `ssao` (bool, varsayılan açık), `ssao_radius` (~0.6),
`ssao_intensity` (~1.1). SSAO ekran-uzayı ambient occlusion — temas/oyuk
bölgelerinde ambient ışığı koyulaştırır.

### Tetik hacimleri (trigger volume)
`body: {"type":"kinematic","sensor":true}` — çarpışmayı raporlar ama hiçbir şeyi
itmez. Davranış tetikleri: `{"on":"enter","with":"..."}` (üst üste binme başlangıcı)
ve `{"on":"exit","with":"..."}`. `with` boşsa her şeye tepki verir. Kapı, checkpoint,
hasar bölgesi, toplanabilir eşya bunlarla kurulur.

### Girdi (input)
Adlandırılmış aksiyonlar: `input.map` ile bir aksiyona tuş/fare/gamepad bağlarsın,
oyun mantığı sadece aksiyon adını sorar. Tuş adları: `A`–`Z`, `0`–`9`, `Space`,
`Enter`, `Escape`, `Tab`, `Left/Right/Up/Down`, `Shift`, `Ctrl`, `Alt`, `F1`–`F12`,
`Mouse1`–`Mouse3`, `Pad:A/B/X/Y/LB/RB/Start/Back/Up/Down/Left/Right`.

Davranış tetikleri: `{"on":"input","action":"..."}` (basılı olduğu sürece),
`inputPressed` (basma anı, bir kez), `inputReleased` (bırakma anı).

**`input.set` sanal girdi enjekte eder** — oyun kodu bunu gerçek tuştan ayırt
edemez, yani aynı oyunu bir insan pencerede, bir AI de JSON kanalından oynayabilir.

### Terrain (`terrain` bloğu)
`terrain.create` fraktal-gürültü heightmap üretir (kare, orijin merkezli, kenara
doğru ada-sönümü). `terrain.sculpt` fırçayla yükseltir/alçaltır/yumuşatır/düzler.
Sahne JSON'una gürültü parametreleri olarak yazılır; sculpt edildiyse `heights`
dizisi de eklenir (yeniden yüklemede aynen gelir).

**Fizik çarpışması:** terrain entity'sine bir `body` eklenirse (herhangi bir tip —
her zaman static'e çevrilir) heightmap'e birebir uyan bir Jolt `HeightFieldShape`
collider kurulur, böylece cisimler zeminin üstüne oturur. `terrain.sculpt`
sonrası collider otomatik yeniden üretilir.

### Kısıtlar (`joint` bloğu)
`joint.create` iki gövde arasında bir Jolt kısıtı kurar (entity `a` üzerinde
saklanır, `a` bloğuna serialize olur). `b` boşsa dünyaya sabitlenir. `type`:
- `hinge` — `point` (dünya) ekseninde `axis` etrafında döner (kapı, sarkaç)
- `distance` — sabit çubuk, uzunluk `[min, max]` arası kısıtlı
- `spring` — `distance` + yay; `length` dinlenme boyu, `stiffness` Hz, `damping`
- `fixed` — göreli konum + yönelimi kilitler
- `point` — `point` noktasında top mafsal, yönelim serbest

Her gövdenin bir `RigidBody`'si olmalı. Gövde a hazır değilse kısıt sonraki
`world.step`'te kurulur. `joint.remove {a}` / `{b}` ilgili kısıtları siler.

### Işık (`light` bloğu)
`entity.spawn {light:{type, ...}}` veya `light.set/light.add`. `type`:
`directional` (yön güneşi, 3-cascade CSM gölgeli), `point`, `spot`. Spot alanları:
`direction`, `inner_deg`, `outer_deg`, `range`, `cast_shadows` (varsayılan açık —
ilk 4 spot ışık 2x2 gölge atlasına girer). Point ışıklarda gölge yok (henüz).

### UI (`kind`)
`panel` (renkli kutu + opsiyonel ortalı metin), `text` (sadece metin, pos'tan başlar),
`bar` (arka + `value` 0..1 kadar `fill_color` dolgu + metin). `anchor`: `top-left`,
`top-right`, `top`, `center`, `bottom-left`, `bottom-right`, `bottom` vb. `pos`/`size`
normalize (0..1); `pos` anchor kenarından içe kaçıklık. `order` çizim sırası.

### Fizik (`body`)
`entity.spawn`/`entity.setBody` içinde: `{type: "static"|"dynamic"|"kinematic",
shape?: "box"|"sphere" (boşsa primitive'den), mass?, restitution?, friction?}`.
Dinamik gövdeler `world.step` sonrası Transform'a geri yazılır. Deterministik akış:
sahneyi kur → `world.step` → `observe.screenshot`. Sürekli simülasyon için `physics.play`.

`primitive`: `cube` \| `sphere` \| `plane` \| `gltf` (+ `gltf_path`).
Vektörler `[x,y,z]`; `scale` tek sayı da olabilir. `rotation` XYZ derece.

### Algı (perception)
`observe.pick` bir ekran noktasından sahneye ışın atar (fizik gövdeleri + gövdesiz
meshlerin sınır küresi). `observe.segment` her mesh'i düz benzersiz bir renkle
çizer; `colors` sözlüğü `"r,g,b" -> isim` eşler (renkler Knuth çarpımsal hash ile
dağıtılır, arka plan `0,0,0`). `observe.depth` lineer derinliği greyscale yazar
(yakın = beyaz); `near`/`far` verilmezse görünür geometriye göre otomatik oturur.
Hepsi `ctx.offscreen`'e çizer, sonra default framebuffer'a döner.

`observe.describe` metin tabanlı bir sahne özeti verir: kamera, her entity için
`kind` (mesh/light/body/terrain/camera), dünya-AABB `size`, `on_screen`; ve
yakın entity çiftleri arasında basit `relations` (`on`, `above`, `inside`,
`left_of`, `near`). İlişki mantığı kaba AABB çıkarımıdır, kesin değildir.

### Kayıt / tekrar (record)
`record.start` açıkken `dispatch()` gelen her istek satırını (JSONL) dosyaya
ekler (`record.*` ve `quit` hariç). `record.play` dosyayı satır satır yeniden
işler; önce `PhysicsWorld` tamamen sıfırlanır, böylece aynı komut akışı bit-bit
aynı sonucu verir. Kayıt dosyasını doğrudan yeni bir motor sürecine stdin olarak
da verebilirsin.

## AI döngüsü (tipik)
1. `scene.load` veya `scene.reset`
2. `entity.spawn` / `light.set` / `camera.set` ile sahneyi kur
3. `observe.screenshot` → PNG yolunu al, **karyi görüntü olarak incele**
4. Beğenmediysen 2'ye dön; beğendiysen `scene.save`
