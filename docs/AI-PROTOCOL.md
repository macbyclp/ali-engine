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
| `entity.spawn` | `{name?, primitive?, gltf_path?, position?, rotation?, scale?, <material>, body?}` | `{name}` |
| `entity.destroy` | `{name}` | — |
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
| `nav.bake` | `{min?, max?, cell?}` | — statik gövdelerden grid |
| `nav.path` | `{from, to}` | `{waypoints:[...]}` |
| `particles.emit` | `{name, position?, rate?, lifetime?, velocity?, velocity_spread?, gravity?, start_color?, end_color?, start_size?, end_size?}` | — |
| `particles.stop` | `{name}` | — |
| `audio.play` | `{file, volume?, loop?, spatial?, position?}` | `{handle}` |
| `audio.stop` | `{handle}` | — |
| `ui.add` / `ui.set` | `{name, kind?, anchor?, pos?, size?, color?, fill_color?, text?, text_size?, text_color?, value?, visible?, order?}` | `{name}` |
| `ui.remove` | `{name}` | — |
| `observe.view` | `{position?, target?, fov_deg?, width?, height?, path?}` | `{path,w,h}` — sahne kamerası bozulmaz |
| `observe.entities` | — | `{entities:[{name,position,distance,in_view,screen}]}` |
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
| `behavior.set` | `{name, behaviors:[...]}` | — |
| `behavior.get` | `{name}` | `{rules}` |
| `event.emit` | `{event}` | — (bir sonraki step'te işlenir) |
| `observe.screenshot` | `{path?, width?, height?}` | `{path, width, height}` |
| `observe.stats` | — | `{entities, visible, culled, draw_calls, instances, groups, cpu_ms}` |
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

## AI döngüsü (tipik)
1. `scene.load` veya `scene.reset`
2. `entity.spawn` / `light.set` / `camera.set` ile sahneyi kur
3. `observe.screenshot` → PNG yolunu al, **karyi görüntü olarak incele**
4. Beğenmediysen 2'ye dön; beğendiysen `scene.save`
