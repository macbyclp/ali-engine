#include "behavior/behavior_system.hpp"
#include "core/log.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdio>
#include <string>

using nlohmann::json;

namespace eng {

static glm::vec3 v3(const json& j, glm::vec3 fb) {
    if (j.is_array() && j.size() == 3)
        return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
    return fb;
}
static std::string name_of(Scene& s, entt::entity e) {
    auto* n = s.registry.try_get<Name>(e);
    return n ? n->value : std::string();
}

// Replaces ${key} tokens with GameState values (numbers printed without trailing .0).
static std::string interp(const std::string& in, const GameState& gs) {
    std::string out;
    for (size_t i = 0; i < in.size();) {
        if (in[i] == '$' && i + 1 < in.size() && in[i + 1] == '{') {
            size_t end = in.find('}', i + 2);
            if (end != std::string::npos) {
                std::string key = in.substr(i + 2, end - i - 2);
                if (gs.values.contains(key)) {
                    const auto& v = gs.values.at(key);
                    if (v.is_number_integer()) out += std::to_string(v.get<long long>());
                    else if (v.is_number()) {
                        char buf[32];
                        std::snprintf(buf, sizeof(buf), "%g", v.get<double>());
                        out += buf;
                    } else if (v.is_string()) out += v.get<std::string>();
                }
                i = end + 1;
                continue;
            }
        }
        out += in[i++];
    }
    return out;
}

static void spawn_from_json(Scene& scene, const json& p) {
    auto e = scene.create(p.value("name", std::string("spawned")));
    auto& t = scene.registry.get<Transform>(e);
    t.position = v3(p.value("position", json()), t.position);
    t.euler_deg = v3(p.value("rotation", json()), t.euler_deg);
    if (p.contains("scale")) {
        if (p["scale"].is_number()) t.scale = glm::vec3(p["scale"].get<float>());
        else t.scale = v3(p["scale"], t.scale);
    }
    MeshRenderer mr;
    mr.primitive = p.value("primitive", std::string("cube"));
    mr.gltf_path = p.value("gltf_path", std::string());
    mr.base_color = v3(p.value("base_color", json()), mr.base_color);
    mr.metallic = p.value("metallic", mr.metallic);
    mr.roughness = p.value("roughness", mr.roughness);
    mr.emissive = v3(p.value("emissive", json()), mr.emissive);
    mr.base_color_map = p.value("base_color_map", std::string());
    mr.normal_map = p.value("normal_map", std::string());
    mr.metallic_roughness_map = p.value("metallic_roughness_map", std::string());
    scene.registry.emplace<MeshRenderer>(e, mr);
    if (p.contains("body")) {
        const json& jb = p["body"];
        RigidBody rb;
        rb.type = jb.value("type", std::string("dynamic"));
        rb.shape = jb.value("shape", std::string());
        rb.mass = jb.value("mass", rb.mass);
        rb.restitution = jb.value("restitution", rb.restitution);
        rb.friction = jb.value("friction", rb.friction);
        scene.registry.emplace<RigidBody>(e, rb);
    }
    if (p.contains("behavior"))
        scene.registry.emplace<Behavior>(e, Behavior{p["behavior"], false});
    scene.resolve_gpu_meshes();
}

void BehaviorSystem::run_actions(Scene& scene, PhysicsSystem& physics, GameState& gs,
                                 entt::entity self, const json& actions, const std::string& other,
                                 float dt, std::vector<entt::entity>& to_destroy,
                                 std::vector<json>& to_spawn) {
    if (!actions.is_array()) return;
    std::string self_name = name_of(scene, self);
    auto* t = scene.registry.try_get<Transform>(self);

    for (const json& a : actions) {
        const std::string act = a.value("action", std::string());

        if (act == "log") {
            log::info("behavior[%s]: %s", self_name.c_str(),
                      a.value("message", std::string()).c_str());
        } else if (act == "setVelocity") {
            physics.set_velocity(self_name, scene, v3(a.value("velocity", json()), glm::vec3(0)));
        } else if (act == "impulse") {
            physics.impulse(self_name, scene, v3(a.value("impulse", json()), glm::vec3(0)));
        } else if (act == "spin" && t) {
            glm::vec3 axis = v3(a.value("axis", json()), glm::vec3(0, 1, 0));
            t->euler_deg += axis * a.value("speed_deg", 90.0f) * dt;
            physics.teleport(scene, self_name);
        } else if (act == "moveToward" && t) {
            glm::vec3 target = v3(a.value("target", json()), t->position);
            float speed = a.value("speed", 1.0f);
            glm::vec3 d = target - t->position;
            float len = glm::length(d);
            if (len > 1e-4f) t->position += (d / len) * glm::min(speed * dt, len);
            physics.teleport(scene, self_name);
        } else if (act == "setMaterial" || act == "setColor") {
            if (auto* mr = scene.registry.try_get<MeshRenderer>(self)) {
                mr->base_color = v3(a.value("base_color", a.value("color", json())), mr->base_color);
                mr->metallic = a.value("metallic", mr->metallic);
                mr->roughness = a.value("roughness", mr->roughness);
                mr->emissive = v3(a.value("emissive", json()), mr->emissive);
            }
        } else if (act == "spawn") {
            json p = a;
            p.erase("action");
            if (a.value("relative", false) && t) {
                glm::vec3 base = t->position;
                p["position"] = json::array({base.x + v3(a.value("position", json()), glm::vec3(0)).x,
                                             base.y + v3(a.value("position", json()), glm::vec3(0)).y,
                                             base.z + v3(a.value("position", json()), glm::vec3(0)).z});
            }
            to_spawn.push_back(std::move(p));
        } else if (act == "destroy") {
            std::string tgt = a.value("target", self_name);
            auto e = scene.find(tgt);
            if (e != entt::null) to_destroy.push_back(e);
        } else if (act == "emit") {
            pending_events_.push_back(a.value("event", std::string()));
        } else if (act == "setState") {
            if (a.contains("key")) gs.values[a["key"].get<std::string>()] = a.value("value", json());
        } else if (act == "addState") {
            if (a.contains("key")) {
                std::string k = a["key"].get<std::string>();
                double cur = gs.values.contains(k) && gs.values[k].is_number()
                                 ? gs.values[k].get<double>() : 0.0;
                gs.values[k] = cur + a.value("value", 1.0);
            }
        } else if (act == "timer") {
            gs.timers.push_back({a.value("after", 1.0f), a.value("event", std::string())});
        } else if (act == "setUI") {
            auto e = scene.find(a.value("target", std::string()));
            if (e != entt::null) {
                if (auto* ui = scene.registry.try_get<UIElement>(e)) {
                    if (a.contains("text")) ui->text = interp(a["text"].get<std::string>(), gs);
                    if (a.contains("value")) ui->value = a["value"].get<float>();
                    if (a.contains("visible")) ui->visible = a["visible"].get<bool>();
                }
            }
        }
    }
}

void BehaviorSystem::run_rules(Scene& scene, PhysicsSystem& physics, GameState& gs,
                               entt::entity self, const char* trigger, const std::string& other,
                               float dt, std::vector<entt::entity>& to_destroy,
                               std::vector<json>& to_spawn) {
    auto* b = scene.registry.try_get<Behavior>(self);
    if (!b || !b->rules.is_array()) return;
    for (const json& rule : b->rules) {
        if (rule.value("on", std::string()) != trigger) continue;
        if (std::string(trigger) == "collision") {
            std::string want = rule.value("with", std::string());
            if (!want.empty() && want != other) continue;
        }
        if (rule.contains("if") && !gs.check(rule["if"])) continue;
        run_actions(scene, physics, gs, self, rule.value("do", json::array()), other, dt,
                    to_destroy, to_spawn);
    }
}

void BehaviorSystem::tick(Scene& scene, PhysicsSystem& physics, GameState& gs, float dt) {
    std::vector<entt::entity> to_destroy;
    std::vector<json> to_spawn;

    gs.tick(dt, pending_events_);   // timer -> events

    std::vector<std::string> events;
    events.swap(pending_events_);

    std::vector<entt::entity> ents;
    for (auto [e, b] : scene.registry.view<Behavior>().each()) ents.push_back(e);

    for (const std::string& ev : events) {
        for (auto e : ents) {
            auto* b = scene.registry.try_get<Behavior>(e);
            if (!b) continue;
            for (const json& rule : b->rules) {
                if (rule.value("on", std::string()) == "event" &&
                    rule.value("name", std::string()) == ev) {
                    if (rule.contains("if") && !gs.check(rule["if"])) continue;
                    run_actions(scene, physics, gs, e, rule.value("do", json::array()), "", dt,
                                to_destroy, to_spawn);
                }
            }
        }
    }

    for (auto [a, c] : physics.drain_contacts()) {
        run_rules(scene, physics, gs, a, "collision", name_of(scene, c), dt, to_destroy, to_spawn);
        run_rules(scene, physics, gs, c, "collision", name_of(scene, a), dt, to_destroy, to_spawn);
    }

    for (auto e : ents) {
        auto* b = scene.registry.try_get<Behavior>(e);
        if (!b) continue;
        if (!b->started) {
            run_rules(scene, physics, gs, e, "start", "", dt, to_destroy, to_spawn);
            b->started = true;
        }
        run_rules(scene, physics, gs, e, "tick", "", dt, to_destroy, to_spawn);
    }

    for (auto& p : to_spawn) spawn_from_json(scene, p);
    for (auto e : to_destroy)
        if (scene.registry.valid(e)) scene.registry.destroy(e);
    if (!to_spawn.empty() || !to_destroy.empty()) physics.sync(scene);
}

} // namespace eng
