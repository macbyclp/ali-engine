#include "scene/scene.hpp"
#include "anim/animation.hpp"
#include "anim/animator.hpp"
#include "assets/gltf.hpp"
#include "geo/procedural.hpp"
#include "assets/texture.hpp"
#include "core/log.hpp"
#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <vector>

using nlohmann::json;

namespace eng {

// ---- glm <-> json helpers ----
static json v3(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }
static glm::vec3 v3(const json& j, const glm::vec3& fallback) {
    if (!j.is_array() || j.size() != 3) return fallback;
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

Scene::Scene() {
    registry.on_construct<Name>().connect<&Scene::on_name_set>(*this);
    registry.on_update<Name>().connect<&Scene::on_name_set>(*this);
    registry.on_destroy<Name>().connect<&Scene::on_name_removed>(*this);
}
Scene::~Scene() {
    // tear entities down now, while index_ (touched by the on_destroy handler) is
    // still alive; the registry member is destroyed after this body returns.
    registry.clear();
}

void Scene::on_name_set(entt::registry& r, entt::entity e) {
    index_[r.get<Name>(e).value] = e;
}
void Scene::on_name_removed(entt::registry& r, entt::entity e) {
    auto it = index_.find(r.get<Name>(e).value);
    if (it != index_.end() && it->second == e) index_.erase(it);
}

std::string Scene::unique_name(const std::string& base) const {
    if (index_.find(base) == index_.end()) return base;
    for (int i = 1;; ++i) {
        std::string cand = base + "." + std::to_string(i);
        if (index_.find(cand) == index_.end()) return cand;
    }
}

entt::entity Scene::create(const std::string& name) {
    auto e = registry.create();
    registry.emplace<Transform>(e);
    registry.emplace<Name>(e, unique_name(name.empty() ? "entity" : name));
    return e;
}

entt::entity Scene::find(const std::string& name) const {
    auto it = index_.find(name);
    if (it != index_.end() && registry.valid(it->second)) return it->second;
    return entt::null;
}

bool Scene::destroy(const std::string& name) {
    auto e = find(name);
    if (e == entt::null) return false;
    registry.destroy(e);
    return true;
}

std::vector<std::string> Scene::names() const {
    std::vector<std::string> out;
    for (auto [e, n] : registry.view<Name>().each()) out.push_back(n.value);
    return out;
}

CameraComp& Scene::camera() {
    for (auto [e, c] : registry.view<CameraComp>().each()) return c;
    auto e = create("camera");
    return registry.emplace<CameraComp>(e);
}

void Scene::clear() {
    registry.clear();
    index_.clear();
    env = json::object();
}

entt::entity Scene::load_entity(const json& je) {
    {
        auto e = registry.create();
        registry.emplace<Name>(e, unique_name(je.value("name", std::string("entity"))));

        Transform t;
        if (je.contains("transform")) {
            const auto& jt = je["transform"];
            t.position = v3(jt.value("position", json()), t.position);
            t.euler_deg = v3(jt.value("rotation", json()), t.euler_deg);
            t.scale = v3(jt.value("scale", json()), t.scale);
        }
        registry.emplace<Transform>(e, t);

        if (je.contains("parent") && je["parent"].is_string())
            registry.emplace<Hierarchy>(e, Hierarchy{je["parent"].get<std::string>()});

        if (je.contains("mesh")) {
            const auto& jm = je["mesh"];
            MeshRenderer mr;
            mr.primitive = jm.value("primitive", std::string("cube"));
            mr.gltf_path = jm.value("gltf_path", std::string());
            if (jm.contains("build")) mr.build = jm["build"];
            mr.base_color = v3(jm.value("base_color", json()), mr.base_color);
            mr.metallic = jm.value("metallic", mr.metallic);
            mr.roughness = jm.value("roughness", mr.roughness);
            mr.emissive = v3(jm.value("emissive", json()), mr.emissive);
            if (jm.contains("uv_scale") && jm["uv_scale"].is_array() && jm["uv_scale"].size() == 2)
                mr.uv_scale = {jm["uv_scale"][0].get<float>(), jm["uv_scale"][1].get<float>()};
            mr.base_color_map = jm.value("base_color_map", std::string());
            mr.normal_map = jm.value("normal_map", std::string());
            mr.metallic_roughness_map = jm.value("metallic_roughness_map", std::string());
            mr.emissive_map = jm.value("emissive_map", std::string());
            mr.ao_map = jm.value("ao_map", std::string());
            registry.emplace<MeshRenderer>(e, mr);
        }
        if (je.contains("light")) {
            const auto& jl = je["light"];
            std::string lt = jl.value("type", std::string("directional"));
            if (lt == "point" || lt == "spot") {
                PunctualLight pl;
                pl.spot = (lt == "spot");
                pl.color = v3(jl.value("color", json()), pl.color);
                pl.intensity = jl.value("intensity", pl.intensity);
                pl.range = jl.value("range", pl.range);
                pl.direction = v3(jl.value("direction", json()), pl.direction);
                pl.inner_deg = jl.value("inner_deg", pl.inner_deg);
                pl.outer_deg = jl.value("outer_deg", pl.outer_deg);
                pl.cast_shadows = jl.value("cast_shadows", pl.cast_shadows);
                registry.emplace<PunctualLight>(e, pl);
            } else {
                DirectionalLight dl;
                dl.direction = v3(jl.value("direction", json()), dl.direction);
                dl.color = v3(jl.value("color", json()), dl.color);
                dl.intensity = jl.value("intensity", dl.intensity);
                registry.emplace<DirectionalLight>(e, dl);
            }
        }
        if (je.contains("body")) {
            const auto& jb = je["body"];
            RigidBody rb;
            rb.type = jb.value("type", rb.type);
            rb.shape = jb.value("shape", rb.shape);
            rb.mass = jb.value("mass", rb.mass);
            rb.restitution = jb.value("restitution", rb.restitution);
                rb.sensor = jb.value("sensor", rb.sensor);
            rb.friction = jb.value("friction", rb.friction);
            registry.emplace<RigidBody>(e, rb);
        }
        if (je.contains("behavior")) {
            registry.emplace<Behavior>(e, Behavior{je["behavior"], false});
        }
        if (je.contains("particles")) {
            const auto& jp = je["particles"];
            ParticleEmitter em;
            em.rate = jp.value("rate", em.rate);
            em.lifetime = jp.value("lifetime", em.lifetime);
            em.velocity = v3(jp.value("velocity", json()), em.velocity);
            em.velocity_spread = v3(jp.value("velocity_spread", json()), em.velocity_spread);
            em.gravity = v3(jp.value("gravity", json()), em.gravity);
            em.start_size = jp.value("start_size", em.start_size);
            em.end_size = jp.value("end_size", em.end_size);
            if (jp.contains("start_color") && jp["start_color"].size() == 4)
                em.start_color = {jp["start_color"][0], jp["start_color"][1], jp["start_color"][2], jp["start_color"][3]};
            if (jp.contains("end_color") && jp["end_color"].size() == 4)
                em.end_color = {jp["end_color"][0], jp["end_color"][1], jp["end_color"][2], jp["end_color"][3]};
            registry.emplace<ParticleEmitter>(e, em);
        }
        if (je.contains("ui")) {
            const auto& ju = je["ui"];
            UIElement ui;
            ui.kind = ju.value("kind", ui.kind);
            ui.anchor = ju.value("anchor", ui.anchor);
            if (ju.contains("pos") && ju["pos"].size() == 2)
                ui.pos = {ju["pos"][0].get<float>(), ju["pos"][1].get<float>()};
            if (ju.contains("size") && ju["size"].size() == 2)
                ui.size = {ju["size"][0].get<float>(), ju["size"][1].get<float>()};
            if (ju.contains("color") && ju["color"].size() == 4)
                ui.color = {ju["color"][0], ju["color"][1], ju["color"][2], ju["color"][3]};
            ui.text = ju.value("text", ui.text);
            ui.text_size = ju.value("text_size", ui.text_size);
            ui.value = ju.value("value", ui.value);
            ui.order = ju.value("order", ui.order);
            registry.emplace<UIElement>(e, ui);
        }
        if (je.contains("character")) {
            const auto& jc = je["character"];
            CharacterController cc;
            cc.radius = jc.value("radius", cc.radius);
            cc.height = jc.value("height", cc.height);
            cc.move_speed = jc.value("move_speed", cc.move_speed);
            cc.jump_speed = jc.value("jump_speed", cc.jump_speed);
            registry.emplace<CharacterController>(e, cc);
        }
        if (je.contains("animation")) {
            const auto& ja = je["animation"];
            AnimationPlayer ap;
            ap.clip = ja.value("clip", std::string());
            ap.speed = ja.value("speed", 1.0f);
            ap.loop = ja.value("loop", true);
            ap.playing = ja.value("playing", true);
            registry.emplace<AnimationPlayer>(e, ap);
        }
        if (je.contains("animator")) {
            registry.emplace<AnimatorController>(e, animator_from_json(je["animator"]));
            registry.get_or_emplace<AnimationPlayer>(e);
        }
        if (je.contains("terrain")) {
            const auto& jt = je["terrain"];
            TerrainComp tc;
            tc.data.size = jt.value("size", tc.data.size);
            tc.data.resolution = jt.value("resolution", tc.data.resolution);
            tc.data.height = jt.value("height", tc.data.height);
            tc.data.octaves = jt.value("octaves", tc.data.octaves);
            tc.data.frequency = jt.value("frequency", tc.data.frequency);
            tc.data.seed = jt.value("seed", tc.data.seed);
            if (jt.contains("heights") && jt["heights"].is_array()) {
                tc.data.heights = jt["heights"].get<std::vector<float>>();
                tc.data.sculpted = true;
            } else {
                tc.data.regenerate_noise();
            }
            registry.emplace<TerrainComp>(e, std::move(tc));
            auto& mr = registry.get_or_emplace<MeshRenderer>(e);
            mr.primitive = "terrain";
        }
        if (je.contains("camera")) {
            const auto& jc = je["camera"];
            CameraComp c;
            c.position = v3(jc.value("position", json()), c.position);
            c.target = v3(jc.value("target", json()), c.target);
            c.fov_deg = jc.value("fov_deg", c.fov_deg);
            registry.emplace<CameraComp>(e, c);
        }
        return e;
    }
}

void Scene::load_json(const json& j) {
    clear();
    if (j.contains("environment") && j["environment"].is_object()) env = j["environment"];
    for (const auto& je : j.value("entities", json::array())) load_entity(je);
    resolve_gpu_meshes();
}

json Scene::to_json() const {
    json out;
    if (env.is_object() && !env.empty()) out["environment"] = env;
    out["entities"] = json::array();
    for (auto [e, n] : registry.view<Name>().each()) {
        json je;
        je["name"] = n.value;
        if (auto* hh = registry.try_get<Hierarchy>(e); hh && !hh->parent_name.empty())
            je["parent"] = hh->parent_name;
        if (auto* t = registry.try_get<Transform>(e)) {
            je["transform"] = {
                {"position", v3(t->position)},
                {"rotation", v3(t->euler_deg)},
                {"scale", v3(t->scale)},
            };
        }
        if (auto* mr = registry.try_get<MeshRenderer>(e)) {
            je["mesh"] = {
                {"primitive", mr->primitive},
                {"base_color", v3(mr->base_color)},
                {"metallic", mr->metallic},
                {"roughness", mr->roughness},
            };
            auto& jm = je["mesh"];
            if (!mr->gltf_path.empty()) jm["gltf_path"] = mr->gltf_path;
            if (mr->build.is_array() && !mr->build.empty()) jm["build"] = mr->build;
            if (glm::dot(mr->emissive, mr->emissive) > 0.0f) jm["emissive"] = v3(mr->emissive);
            if (mr->uv_scale != glm::vec2(1.0f))
                jm["uv_scale"] = json::array({mr->uv_scale.x, mr->uv_scale.y});
            if (!mr->base_color_map.empty()) jm["base_color_map"] = mr->base_color_map;
            if (!mr->normal_map.empty()) jm["normal_map"] = mr->normal_map;
            if (!mr->metallic_roughness_map.empty()) jm["metallic_roughness_map"] = mr->metallic_roughness_map;
            if (!mr->emissive_map.empty()) jm["emissive_map"] = mr->emissive_map;
            if (!mr->ao_map.empty()) jm["ao_map"] = mr->ao_map;
        }
        if (auto* dl = registry.try_get<DirectionalLight>(e)) {
            je["light"] = {
                {"type", "directional"},
                {"direction", v3(dl->direction)},
                {"color", v3(dl->color)},
                {"intensity", dl->intensity},
            };
        }
        if (auto* pl = registry.try_get<PunctualLight>(e)) {
            je["light"] = {
                {"type", pl->spot ? "spot" : "point"},
                {"color", v3(pl->color)},
                {"intensity", pl->intensity},
                {"range", pl->range},
            };
            if (pl->spot) {
                je["light"]["direction"] = v3(pl->direction);
                je["light"]["inner_deg"] = pl->inner_deg;
                je["light"]["outer_deg"] = pl->outer_deg;
                if (!pl->cast_shadows) je["light"]["cast_shadows"] = false;
            }
        }
        if (auto* rb = registry.try_get<RigidBody>(e)) {
            je["body"] = {
                {"type", rb->type},
                {"mass", rb->mass},
                {"restitution", rb->restitution},
                {"friction", rb->friction},
                {"sensor", rb->sensor},
            };
            if (!rb->shape.empty()) je["body"]["shape"] = rb->shape;
        }
        if (auto* b = registry.try_get<Behavior>(e)) {
            if (b->rules.is_array() && !b->rules.empty()) je["behavior"] = b->rules;
        }
        if (auto* ap = registry.try_get<AnimationPlayer>(e)) {
            je["animation"] = {{"clip", ap->clip}, {"speed", ap->speed},
                               {"loop", ap->loop}, {"playing", ap->playing}};
        }
        if (auto* ac = registry.try_get<AnimatorController>(e)) {
            if (!ac->states.empty()) je["animator"] = animator_to_json(*ac);
        }
        if (auto* tc = registry.try_get<TerrainComp>(e)) {
            const TerrainData& td = tc->data;
            je["terrain"] = {{"size", td.size}, {"resolution", td.resolution},
                             {"height", td.height}, {"octaves", td.octaves},
                             {"frequency", td.frequency}, {"seed", td.seed}};
            if (td.sculpted) {
                json h = json::array();
                for (float v : td.heights) h.push_back(std::round(v * 1000.0f) / 1000.0f);
                je["terrain"]["heights"] = std::move(h);
            }
        }
        if (auto* cc = registry.try_get<CharacterController>(e)) {
            je["character"] = {{"radius", cc->radius}, {"height", cc->height},
                               {"move_speed", cc->move_speed}, {"jump_speed", cc->jump_speed}};
        }
        if (auto* ui = registry.try_get<UIElement>(e)) {
            je["ui"] = {
                {"kind", ui->kind}, {"anchor", ui->anchor},
                {"pos", json::array({ui->pos.x, ui->pos.y})},
                {"size", json::array({ui->size.x, ui->size.y})},
                {"color", json::array({ui->color.x, ui->color.y, ui->color.z, ui->color.w})},
                {"text", ui->text}, {"text_size", ui->text_size},
                {"value", ui->value}, {"order", ui->order},
            };
        }
        if (auto* em = registry.try_get<ParticleEmitter>(e)) {
            je["particles"] = {
                {"rate", em->rate}, {"lifetime", em->lifetime},
                {"velocity", v3(em->velocity)}, {"velocity_spread", v3(em->velocity_spread)},
                {"gravity", v3(em->gravity)}, {"start_size", em->start_size}, {"end_size", em->end_size},
                {"start_color", json::array({em->start_color.x, em->start_color.y, em->start_color.z, em->start_color.w})},
                {"end_color", json::array({em->end_color.x, em->end_color.y, em->end_color.z, em->end_color.w})},
            };
        }
        if (auto* c = registry.try_get<CameraComp>(e)) {
            je["camera"] = {
                {"position", v3(c->position)},
                {"target", v3(c->target)},
                {"fov_deg", c->fov_deg},
            };
        }
        out["entities"].push_back(je);
    }
    return out;
}

bool Scene::load_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) { log::error("scene not found: %s", path.c_str()); return false; }
    try {
        json j; f >> j;
        load_json(j);
        log::info("scene loaded: %s", path.c_str());
        return true;
    } catch (const std::exception& ex) {
        log::error("scene parse error: %s", ex.what());
        return false;
    }
}

bool Scene::save_file(const std::string& path) const {
    std::ofstream f(path);
    if (!f) { log::error("cannot write scene: %s", path.c_str()); return false; }
    f << to_json().dump(2) << "\n";
    return true;
}

// Shared mesh cache so identical primitives/models resolve to the SAME GPU mesh.
// This is what lets the renderer batch them into one instanced draw call.
static std::shared_ptr<Mesh>& cache_slot(const std::string& key) {
    static std::unordered_map<std::string, std::shared_ptr<Mesh>> cache;
    return cache[key];
}
static std::unordered_map<std::string, GltfMaterial>& gltf_mat_cache() {
    static std::unordered_map<std::string, GltfMaterial> m;
    return m;
}

nlohmann::json Scene::export_subtree(const std::string& root) const {
    json full = to_json();
    // collect root + descendants by walking parent links
    std::vector<std::string> keep{root};
    bool grew = true;
    while (grew) {
        grew = false;
        for (const auto& je : full["entities"]) {
            std::string nm = je.value("name", std::string());
            std::string par = je.value("parent", std::string());
            if (par.empty()) continue;
            bool in = std::find(keep.begin(), keep.end(), nm) != keep.end();
            bool parIn = std::find(keep.begin(), keep.end(), par) != keep.end();
            if (parIn && !in) { keep.push_back(nm); grew = true; }
        }
    }
    json out;
    out["entities"] = json::array();
    for (const auto& je : full["entities"])
        if (std::find(keep.begin(), keep.end(), je.value("name", std::string())) != keep.end())
            out["entities"].push_back(je);
    return out;
}

std::vector<std::string> Scene::instantiate(const json& prefab, const std::string& new_root,
                                            const glm::vec3& at, bool use_at) {
    // find the prefab's root (an entity whose parent is absent or outside the set)
    std::vector<std::string> src_names;
    for (const auto& je : prefab.value("entities", json::array()))
        src_names.push_back(je.value("name", std::string()));

    std::string src_root;
    for (const auto& je : prefab.value("entities", json::array())) {
        std::string par = je.value("parent", std::string());
        if (par.empty() || std::find(src_names.begin(), src_names.end(), par) == src_names.end()) {
            src_root = je.value("name", std::string());
            break;
        }
    }

    std::unordered_map<std::string, std::string> remap;
    for (const auto& s : src_names)
        remap[s] = (s == src_root) ? new_root : new_root + "/" + s;

    // append the prefab's entities directly into the live registry -- existing
    // entities, physics bodies and runtime state are untouched.
    std::vector<std::string> created;
    for (json je : prefab.value("entities", json::array())) {
        std::string nm = je.value("name", std::string());
        je["name"] = remap.count(nm) ? remap[nm] : nm;
        if (je.contains("parent")) {
            std::string par = je["parent"].get<std::string>();
            if (remap.count(par)) je["parent"] = remap[par];
        }
        if (use_at && nm == src_root)
            je["transform"]["position"] = json::array({at.x, at.y, at.z});
        entt::entity e = load_entity(je);
        created.push_back(registry.get<Name>(e).value);
    }
    resolve_gpu_meshes();
    return created;
}

void Scene::resolve_gpu_meshes() {
    for (auto [e, mr] : registry.view<MeshRenderer>().each()) {
        std::string key = mr.primitive;
        if (mr.primitive == "gltf") key = "gltf:" + mr.gltf_path;

        if (mr.primitive == "procedural") {
            MeshData d = build_procedural(mr.build);
            if (d.idx.empty()) d = make_box(glm::vec3(1));
            mr.gpu = d.upload();
            continue;
        }
        if (mr.primitive == "terrain") {
            auto* tc = registry.try_get<TerrainComp>(e);
            if (tc) {
                if ((int)tc->data.heights.size() != tc->data.resolution * tc->data.resolution)
                    tc->data.regenerate_noise();
                mr.gpu = tc->data.build().upload();
            } else {
                mr.gpu = Mesh::plane(10.0f);
            }
            continue;
        }

        if (mr.primitive == "skinned") {
            static std::unordered_map<std::string, std::shared_ptr<SkinnedModel>> skin_cache;
            auto& sm = skin_cache[mr.gltf_path];
            if (!sm) {
                if (mr.gltf_path.rfind("builtin:", 0) == 0)
                    sm = builtin_skinned(mr.gltf_path.substr(8));
                else
                    sm = load_gltf_skinned(mr.gltf_path);
            }
            mr.skinned = sm;
            mr.gpu = sm ? sm->mesh : Mesh::cube();
        } else {
            auto& slot = cache_slot(key);
            if (!slot) {
                if (mr.primitive == "sphere") slot = Mesh::sphere();
                else if (mr.primitive == "plane") slot = Mesh::plane();
                else if (mr.primitive == "gltf" && !mr.gltf_path.empty())
                    slot = load_gltf_mesh(mr.gltf_path, &gltf_mat_cache()[mr.gltf_path]);
                if (!slot) slot = Mesh::cube();
            }
            mr.gpu = slot;
        }

        if (mr.primitive == "gltf" && !mr.gltf_path.empty()) {
            const GltfMaterial& gm = gltf_mat_cache()[mr.gltf_path];
            if (mr.base_color == glm::vec3(0.8f)) mr.base_color = gm.base_color;
            if (mr.metallic == 0.0f) mr.metallic = gm.metallic;
            if (mr.roughness == 0.8f) mr.roughness = gm.roughness;
            if (mr.emissive == glm::vec3(0.0f)) mr.emissive = gm.emissive;
            if (mr.base_color_map.empty()) mr.base_color_map = gm.base_color_map;
            if (mr.normal_map.empty()) mr.normal_map = gm.normal_map;
            if (mr.metallic_roughness_map.empty()) mr.metallic_roughness_map = gm.metallic_roughness_map;
            if (mr.emissive_map.empty()) mr.emissive_map = gm.emissive_map;
            if (mr.ao_map.empty()) mr.ao_map = gm.ao_map;
        }

        auto tex = [](const std::string& k, bool srgb) {
            return k.empty() ? nullptr : Texture::resolve(k, srgb);
        };
        mr.t_base = tex(mr.base_color_map, true);
        mr.t_normal = tex(mr.normal_map, false);
        mr.t_mr = tex(mr.metallic_roughness_map, false);
        mr.t_emissive = tex(mr.emissive_map, true);
        mr.t_ao = tex(mr.ao_map, false);
    }
}

} // namespace eng
