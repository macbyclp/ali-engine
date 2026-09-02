#include "scene/scene.hpp"
#include "assets/gltf.hpp"
#include "core/log.hpp"
#include <fstream>
#include <unordered_map>

using nlohmann::json;

namespace eng {

// ---- glm <-> json helpers ----
static json v3(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }
static glm::vec3 v3(const json& j, const glm::vec3& fallback) {
    if (!j.is_array() || j.size() != 3) return fallback;
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

std::string Scene::unique_name(const std::string& base) const {
    if (find(base) == entt::null) return base;
    for (int i = 1;; ++i) {
        std::string cand = base + "." + std::to_string(i);
        if (find(cand) == entt::null) return cand;
    }
}

entt::entity Scene::create(const std::string& name) {
    auto e = registry.create();
    registry.emplace<Name>(e, unique_name(name.empty() ? "entity" : name));
    registry.emplace<Transform>(e);
    return e;
}

entt::entity Scene::find(const std::string& name) const {
    for (auto [e, n] : registry.view<Name>().each())
        if (n.value == name) return e;
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

void Scene::clear() { registry.clear(); }

void Scene::load_json(const json& j) {
    clear();
    for (const auto& je : j.value("entities", json::array())) {
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

        if (je.contains("mesh")) {
            const auto& jm = je["mesh"];
            MeshRenderer mr;
            mr.primitive = jm.value("primitive", std::string("cube"));
            mr.gltf_path = jm.value("gltf_path", std::string());
            mr.base_color = v3(jm.value("base_color", json()), mr.base_color);
            mr.metallic = jm.value("metallic", mr.metallic);
            mr.roughness = jm.value("roughness", mr.roughness);
            registry.emplace<MeshRenderer>(e, mr);
        }
        if (je.contains("light")) {
            const auto& jl = je["light"];
            DirectionalLight dl;
            dl.direction = v3(jl.value("direction", json()), dl.direction);
            dl.color = v3(jl.value("color", json()), dl.color);
            dl.intensity = jl.value("intensity", dl.intensity);
            registry.emplace<DirectionalLight>(e, dl);
        }
        if (je.contains("body")) {
            const auto& jb = je["body"];
            RigidBody rb;
            rb.type = jb.value("type", rb.type);
            rb.shape = jb.value("shape", rb.shape);
            rb.mass = jb.value("mass", rb.mass);
            rb.restitution = jb.value("restitution", rb.restitution);
            rb.friction = jb.value("friction", rb.friction);
            registry.emplace<RigidBody>(e, rb);
        }
        if (je.contains("behavior")) {
            registry.emplace<Behavior>(e, Behavior{je["behavior"], false});
        }
        if (je.contains("camera")) {
            const auto& jc = je["camera"];
            CameraComp c;
            c.position = v3(jc.value("position", json()), c.position);
            c.target = v3(jc.value("target", json()), c.target);
            c.fov_deg = jc.value("fov_deg", c.fov_deg);
            registry.emplace<CameraComp>(e, c);
        }
    }
    resolve_gpu_meshes();
}

json Scene::to_json() const {
    json out;
    out["entities"] = json::array();
    for (auto [e, n] : registry.view<Name>().each()) {
        json je;
        je["name"] = n.value;
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
            if (!mr->gltf_path.empty()) je["mesh"]["gltf_path"] = mr->gltf_path;
        }
        if (auto* dl = registry.try_get<DirectionalLight>(e)) {
            je["light"] = {
                {"direction", v3(dl->direction)},
                {"color", v3(dl->color)},
                {"intensity", dl->intensity},
            };
        }
        if (auto* rb = registry.try_get<RigidBody>(e)) {
            je["body"] = {
                {"type", rb->type},
                {"mass", rb->mass},
                {"restitution", rb->restitution},
                {"friction", rb->friction},
            };
            if (!rb->shape.empty()) je["body"]["shape"] = rb->shape;
        }
        if (auto* b = registry.try_get<Behavior>(e)) {
            if (b->rules.is_array() && !b->rules.empty()) je["behavior"] = b->rules;
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

void Scene::resolve_gpu_meshes() {
    for (auto [e, mr] : registry.view<MeshRenderer>().each()) {
        std::string key = mr.primitive;
        if (mr.primitive == "gltf") key = "gltf:" + mr.gltf_path;

        auto& slot = cache_slot(key);
        if (!slot) {
            if (mr.primitive == "sphere") slot = Mesh::sphere();
            else if (mr.primitive == "plane") slot = Mesh::plane();
            else if (mr.primitive == "gltf" && !mr.gltf_path.empty())
                slot = load_gltf_mesh(mr.gltf_path);
            if (!slot) slot = Mesh::cube();
        }
        mr.gpu = slot;
    }
}

} // namespace eng
