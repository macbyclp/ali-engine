#include "aicontrol/commands.hpp"
#include "anim/animation_system.hpp"
#include "anim/animator.hpp"
#include "fx/particles.hpp"
#include "scene/transform_system.hpp"
#include "core/log.hpp"
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>

using nlohmann::json;
namespace fs = std::filesystem;

namespace eng {

static glm::vec3 v3(const json& j, glm::vec3 fb) {
    if (j.is_array() && j.size() == 3)
        return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
    return fb;
}
static json v3(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }

static json ok(const json& id, json result = json::object()) {
    return {{"id", id}, {"ok", true}, {"result", std::move(result)}};
}
static json fail(const json& id, const std::string& msg) {
    return {{"id", id}, {"ok", false}, {"error", msg}};
}

static void apply_material(MeshRenderer& mr, const json& p) {
    mr.base_color = v3(p.value("base_color", json()), mr.base_color);
    mr.metallic = p.value("metallic", mr.metallic);
    mr.roughness = p.value("roughness", mr.roughness);
    mr.emissive = v3(p.value("emissive", json()), mr.emissive);
    if (p.contains("uv_scale") && p["uv_scale"].is_array() && p["uv_scale"].size() == 2)
        mr.uv_scale = {p["uv_scale"][0].get<float>(), p["uv_scale"][1].get<float>()};
    mr.base_color_map = p.value("base_color_map", mr.base_color_map);
    mr.normal_map = p.value("normal_map", mr.normal_map);
    mr.metallic_roughness_map = p.value("metallic_roughness_map", mr.metallic_roughness_map);
    mr.emissive_map = p.value("emissive_map", mr.emissive_map);
    mr.ao_map = p.value("ao_map", mr.ao_map);
}

static void apply_transform(Transform& t, const json& p) {
    if (p.contains("position")) t.position = v3(p["position"], t.position);
    if (p.contains("rotation")) t.euler_deg = v3(p["rotation"], t.euler_deg);
    if (p.contains("scale")) {
        if (p["scale"].is_number()) t.scale = glm::vec3(p["scale"].get<float>());
        else t.scale = v3(p["scale"], t.scale);
    }
}

nlohmann::json dispatch(CommandContext& ctx, const json& req) {
    const json id = req.value("id", json(nullptr));
    const std::string method = req.value("method", std::string());
    const json p = req.value("params", json::object());
    Scene& scene = ctx.scene;

    try {
        if (method == "ping") return ok(id, {{"pong", true}});

        if (method == "quit") { ctx.quit = true; return ok(id); }

        if (method == "scene.load") {
            std::string path = p.at("path").get<std::string>();
            if (!scene.load_file(path)) return fail(id, "load failed: " + path);
            ctx.scene_path = path;
            return ok(id, {{"entities", scene.names().size()}});
        }
        if (method == "scene.save") {
            std::string path = p.value("path", ctx.scene_path);
            if (path.empty()) return fail(id, "no path and no active scene");
            if (!scene.save_file(path)) return fail(id, "save failed: " + path);
            ctx.scene_path = path;
            return ok(id, {{"path", path}});
        }
        if (method == "scene.reset") {
            scene.clear();
            return ok(id);
        }
        if (method == "scene.state") return ok(id, scene.to_json());

        if (method == "entity.list") return ok(id, {{"names", scene.names()}});

        if (method == "entity.spawn") {
            auto e = scene.create(p.value("name", std::string("entity")));
            std::string name = scene.registry.get<Name>(e).value;
            auto& t = scene.registry.get<Transform>(e);
            apply_transform(t, p);

            MeshRenderer mr;
            mr.primitive = p.value("primitive", std::string("cube"));
            mr.gltf_path = p.value("gltf_path", std::string());
            if (p.contains("build")) { mr.primitive = "procedural"; mr.build = p["build"]; }
            apply_material(mr, p);
            scene.registry.emplace<MeshRenderer>(e, mr);
            scene.resolve_gpu_meshes();

            if (p.contains("parent"))
                scene.registry.emplace<Hierarchy>(e, Hierarchy{p["parent"].get<std::string>()});

            if (p.contains("animation")) {
                const json& ja = p["animation"];
                AnimationPlayer ap;
                if (ja.is_string()) ap.clip = ja.get<std::string>();
                else {
                    ap.clip = ja.value("clip", std::string());
                    ap.speed = ja.value("speed", 1.0f);
                    ap.loop = ja.value("loop", true);
                    ap.playing = ja.value("playing", true);
                }
                scene.registry.emplace<AnimationPlayer>(e, ap);
            }

            if (p.contains("body")) {
                const json& jb = p["body"];
                RigidBody rb;
                rb.type = jb.value("type", std::string("dynamic"));
                rb.shape = jb.value("shape", std::string());
                rb.mass = jb.value("mass", rb.mass);
                rb.restitution = jb.value("restitution", rb.restitution);
                rb.friction = jb.value("friction", rb.friction);
                scene.registry.emplace<RigidBody>(e, rb);
                ctx.physics.sync(scene);
            }
            return ok(id, {{"name", name}});
        }
        if (method == "mesh.build") {
            auto e = scene.find(p.at("name").get<std::string>());
            if (e == entt::null) return fail(id, "no such entity");
            if (!p.contains("build") || !p["build"].is_array())
                return fail(id, "mesh.build needs a 'build' array");
            auto& mr = scene.registry.get_or_emplace<MeshRenderer>(e);
            mr.primitive = "procedural";
            mr.build = p["build"];
            scene.resolve_gpu_meshes();
            int tris = mr.gpu ? mr.gpu->index_count() / 3 : 0;
            return ok(id, {{"triangles", tris}});
        }
        if (method == "entity.setParent") {
            auto e = scene.find(p.at("name").get<std::string>());
            if (e == entt::null) return fail(id, "no such entity");
            std::string par = p.value("parent", std::string());
            if (par.empty()) scene.registry.remove<Hierarchy>(e);
            else scene.registry.emplace_or_replace<Hierarchy>(e, Hierarchy{par});
            return ok(id);
        }
        if (method == "prefab.save") {
            std::string root = p.at("root").get<std::string>();
            std::string path = p.at("path").get<std::string>();
            if (scene.find(root) == entt::null) return fail(id, "no such entity: " + root);
            json pf = scene.export_subtree(root);
            if (fs::path(path).has_parent_path()) {
                std::error_code ec;
                fs::create_directories(fs::path(path).parent_path(), ec);
            }
            std::ofstream f(path);
            if (!f) return fail(id, "cannot write: " + path);
            f << pf.dump(2) << "\n";
            return ok(id, {{"path", path}, {"entities", pf["entities"].size()}});
        }
        if (method == "prefab.instantiate") {
            std::string path = p.at("path").get<std::string>();
            std::string name = p.at("name").get<std::string>();
            std::ifstream f(path);
            if (!f) return fail(id, "prefab not found: " + path);
            json pf; f >> pf;
            glm::vec3 at = v3(p.value("position", json()), glm::vec3(0));
            auto created = scene.instantiate(pf, name, at, p.contains("position"));
            ctx.physics.sync(scene);
            return ok(id, {{"created", created}});
        }
        if (method == "entity.setBody") {
            auto e = scene.find(p.at("name").get<std::string>());
            if (e == entt::null) return fail(id, "no such entity");
            auto& rb = scene.registry.get_or_emplace<RigidBody>(e);
            rb.type = p.value("type", rb.type);
            rb.shape = p.value("shape", rb.shape);
            rb.mass = p.value("mass", rb.mass);
            rb.restitution = p.value("restitution", rb.restitution);
            rb.friction = p.value("friction", rb.friction);
            ctx.physics.sync(scene);
            return ok(id);
        }
        if (method == "entity.destroy") {
            std::string name = p.at("name").get<std::string>();
            return scene.destroy(name) ? ok(id) : fail(id, "no such entity: " + name);
        }
        if (method == "entity.setTransform") {
            std::string name = p.at("name").get<std::string>();
            auto e = scene.find(name);
            if (e == entt::null) return fail(id, "no such entity");
            apply_transform(scene.registry.get_or_emplace<Transform>(e), p);
            ctx.physics.teleport(scene, name);
            return ok(id);
        }
        if (method == "entity.setMaterial") {
            auto e = scene.find(p.at("name").get<std::string>());
            if (e == entt::null) return fail(id, "no such entity");
            auto* mr = scene.registry.try_get<MeshRenderer>(e);
            if (!mr) return fail(id, "entity has no mesh");
            apply_material(*mr, p);
            scene.resolve_gpu_meshes();
            return ok(id);
        }
        if (method == "light.set" || method == "light.add") {
            std::string name = p.value("name", std::string("sun"));
            std::string type = p.value("type", std::string());
            auto e = scene.find(name);
            if (e == entt::null) e = scene.create(name);

            bool want_punctual = (type == "point" || type == "spot") ||
                                 (type.empty() && scene.registry.all_of<PunctualLight>(e));
            if (want_punctual) {
                auto& pl = scene.registry.get_or_emplace<PunctualLight>(e);
                if (type == "spot") pl.spot = true;
                if (type == "point") pl.spot = false;
                pl.color = v3(p.value("color", json()), pl.color);
                pl.intensity = p.value("intensity", pl.intensity);
                pl.range = p.value("range", pl.range);
                pl.direction = v3(p.value("direction", json()), pl.direction);
                pl.inner_deg = p.value("inner_deg", pl.inner_deg);
                pl.outer_deg = p.value("outer_deg", pl.outer_deg);
                if (p.contains("position"))
                    apply_transform(scene.registry.get_or_emplace<Transform>(e), p);
            } else {
                auto& dl = scene.registry.get_or_emplace<DirectionalLight>(e);
                dl.direction = v3(p.value("direction", json()), dl.direction);
                dl.color = v3(p.value("color", json()), dl.color);
                dl.intensity = p.value("intensity", dl.intensity);
            }
            return ok(id, {{"name", scene.registry.get<Name>(e).value}});
        }
        if (method == "camera.set") {
            auto& c = scene.camera();
            c.position = v3(p.value("position", json()), c.position);
            c.target = v3(p.value("target", json()), c.target);
            c.fov_deg = p.value("fov_deg", c.fov_deg);
            return ok(id);
        }
        if (method == "camera.get") {
            auto& c = scene.camera();
            return ok(id, {{"position", v3(c.position)}, {"target", v3(c.target)},
                           {"fov_deg", c.fov_deg}});
        }
        if (method == "world.step") {
            float dt = p.value("dt", 1.0f / 60.0f);
            int steps = p.value("steps", 1);
            int substeps = p.value("substeps", 1);
            for (int i = 0; i < glm::clamp(steps, 1, 100000); ++i) {
                update_animators(scene, dt);
                update_animations(scene, dt);
                update_particles(scene, dt);
                ctx.behaviors.tick(scene, ctx.physics, ctx.game, dt);
                ctx.physics.step(scene, dt, substeps);
                ctx.physics.step_characters(scene, dt);
            }
            return ok(id, {{"stepped", steps}, {"dt", dt}});
        }
        if (method == "animation.play") {
            auto e = scene.find(p.at("name").get<std::string>());
            if (e == entt::null) return fail(id, "no such entity");
            auto& ap = scene.registry.get_or_emplace<AnimationPlayer>(e);
            float fade = p.value("fade", 0.0f);
            if (p.contains("clip")) {
                std::string next = p["clip"].get<std::string>();
                if (fade > 0.0f && next != ap.clip) {
                    ap.prev_clip = ap.clip;
                    ap.prev_time = ap.time;
                    ap.fade_left = fade;
                    ap.fade_dur = fade;
                }
                ap.clip = next;
            }
            ap.speed = p.value("speed", ap.speed);
            ap.loop = p.value("loop", ap.loop);
            ap.playing = true;
            if (p.value("restart", false)) ap.time = 0.0f;
            return ok(id);
        }
        if (method == "animation.pause") {
            auto e = scene.find(p.at("name").get<std::string>());
            if (e == entt::null) return fail(id, "no such entity");
            if (auto* ap = scene.registry.try_get<AnimationPlayer>(e)) ap->playing = false;
            return ok(id);
        }
        if (method == "animation.stop") {
            auto e = scene.find(p.at("name").get<std::string>());
            if (e == entt::null) return fail(id, "no such entity");
            if (auto* ap = scene.registry.try_get<AnimationPlayer>(e)) { ap->playing = false; ap->time = 0.0f; }
            return ok(id);
        }
        if (method == "animation.list") {
            auto e = scene.find(p.at("name").get<std::string>());
            if (e == entt::null) return fail(id, "no such entity");
            auto* mr = scene.registry.try_get<MeshRenderer>(e);
            json clips = json::array();
            if (mr && mr->skinned)
                for (auto& [k, v] : mr->skinned->clips) clips.push_back(k);
            return ok(id, {{"clips", clips}});
        }
        if (method == "animator.set") {
            auto e = scene.find(p.at("name").get<std::string>());
            if (e == entt::null) return fail(id, "no such entity");
            AnimatorController c = animator_from_json(p);
            if (c.states.empty()) return fail(id, "animator needs at least one state");
            scene.registry.emplace_or_replace<AnimatorController>(e, std::move(c));
            scene.registry.get_or_emplace<AnimationPlayer>(e);
            return ok(id, {{"states", p.value("states", json::array()).size()}});
        }
        if (method == "animator.param") {
            auto e = scene.find(p.at("name").get<std::string>());
            if (e == entt::null) return fail(id, "no such entity");
            auto* c = scene.registry.try_get<AnimatorController>(e);
            if (!c) return fail(id, "entity has no animator");
            if (p.contains("params") && p["params"].is_object()) {
                for (auto& [k, v] : p["params"].items())
                    c->params[k] = v.is_boolean() ? (v.get<bool>() ? 1.f : 0.f) : v.get<float>();
            } else {
                std::string key = p.at("param").get<std::string>();
                const json& v = p.at("value");
                c->params[key] = v.is_boolean() ? (v.get<bool>() ? 1.f : 0.f) : v.get<float>();
            }
            return ok(id);
        }
        if (method == "animator.get") {
            auto e = scene.find(p.at("name").get<std::string>());
            if (e == entt::null) return fail(id, "no such entity");
            auto* c = scene.registry.try_get<AnimatorController>(e);
            if (!c) return fail(id, "entity has no animator");
            json params = json::object();
            for (auto& [k, v] : c->params) params[k] = v;
            return ok(id, {{"current", c->current}, {"entry", c->entry},
                           {"params", params}, {"states", c->states.size()},
                           {"transitions", c->transitions.size()}});
        }
        if (method == "audio.play") {
            std::string file = p.at("file").get<std::string>();
            bool spatial = p.value("spatial", p.contains("position"));
            glm::vec3 pos = v3(p.value("position", json()), glm::vec3(0));
            uint32_t h = ctx.audio.play(file, p.value("volume", 1.0f),
                                        p.value("loop", false), spatial, pos,
                                        p.value("bus", std::string()));
            if (!h) return fail(id, ctx.audio.ok() ? "load failed" : "no audio device");
            return ok(id, {{"handle", h}});
        }
        if (method == "audio.stop") {
            if (p.contains("bus")) ctx.audio.stop_bus(p["bus"].get<std::string>());
            else ctx.audio.stop((uint32_t)p.at("handle").get<int64_t>());
            return ok(id);
        }
        if (method == "audio.bus") {
            std::string bus = p.value("bus", std::string("master"));
            if (p.contains("volume")) ctx.audio.set_bus_volume(bus, p["volume"].get<float>());
            return ok(id, {{"bus", bus}, {"volume", ctx.audio.bus_volume(bus)}});
        }
        if (method == "checkpoint.save") {
            std::string name = p.value("name", std::string("default"));
            ctx.checkpoints[name] = {{"scene", scene.to_json()}, {"state", ctx.game.values}};
            return ok(id, {{"name", name}});
        }
        if (method == "checkpoint.restore") {
            std::string name = p.value("name", std::string("default"));
            auto it = ctx.checkpoints.find(name);
            if (it == ctx.checkpoints.end()) return fail(id, "no checkpoint: " + name);
            scene.load_json(it->second.value("scene", json::object()));
            ctx.game.values = it->second.value("state", json::object());
            ctx.game.timers.clear();
            ctx.physics.sync(scene);
            return ok(id);
        }
        if (method == "state.set") {
            ctx.game.values[p.at("key").get<std::string>()] = p.value("value", json());
            return ok(id);
        }
        if (method == "state.get") {
            std::string k = p.at("key").get<std::string>();
            if (!ctx.game.values.contains(k)) return ok(id, {{"value", nullptr}});
            return ok(id, {{"value", ctx.game.values[k]}});
        }
        if (method == "state.list") return ok(id, ctx.game.values);
        if (method == "state.clear") { ctx.game.values = json::object(); ctx.game.timers.clear(); return ok(id); }
        if (method == "timer.after") {
            ctx.game.timers.push_back({p.value("seconds", 1.0f), p.at("event").get<std::string>()});
            return ok(id);
        }
        if (method == "observe.view") {
            CameraComp saved = scene.camera();
            CameraComp& c = scene.camera();
            c.position = v3(p.value("position", json()), c.position);
            c.target = v3(p.value("target", json()), c.target);
            c.fov_deg = p.value("fov_deg", c.fov_deg);
            int vw = p.value("width", ctx.offscreen.width());
            int vh = p.value("height", ctx.offscreen.height());
            ctx.offscreen.resize(vw, vh);
            std::string path = p.value("path", std::string());
            if (path.empty()) {
                fs::path dir = fs::current_path() / "screenshots";
                fs::create_directories(dir);
                path = (dir / "view.png").string();
            } else if (fs::path(path).has_parent_path()) {
                std::error_code ec;
                fs::create_directories(fs::path(path).parent_path(), ec);
            }
            ctx.renderer.render(scene, ctx.offscreen.id(), vw, vh);
            bool saved_ok = ctx.offscreen.save_png(path);
            Framebuffer::bind_default(vw, vh);
            c = saved;   // restore scene camera
            if (!saved_ok) return fail(id, "view render failed");
            return ok(id, {{"path", path}, {"width", vw}, {"height", vh}});
        }
        if (method == "ui.add" || method == "ui.set") {
            std::string name = p.at("name").get<std::string>();
            auto e = scene.find(name);
            if (e == entt::null) e = scene.create(name);
            auto& ui = scene.registry.get_or_emplace<UIElement>(e);
            ui.kind = p.value("kind", ui.kind);
            ui.anchor = p.value("anchor", ui.anchor);
            if (p.contains("pos") && p["pos"].size() == 2)
                ui.pos = {p["pos"][0].get<float>(), p["pos"][1].get<float>()};
            if (p.contains("size") && p["size"].size() == 2)
                ui.size = {p["size"][0].get<float>(), p["size"][1].get<float>()};
            if (p.contains("color") && p["color"].size() == 4)
                ui.color = {p["color"][0], p["color"][1], p["color"][2], p["color"][3]};
            if (p.contains("fill_color") && p["fill_color"].size() == 4)
                ui.fill_color = {p["fill_color"][0], p["fill_color"][1], p["fill_color"][2], p["fill_color"][3]};
            if (p.contains("text_color") && p["text_color"].size() == 4)
                ui.text_color = {p["text_color"][0], p["text_color"][1], p["text_color"][2], p["text_color"][3]};
            ui.text = p.value("text", ui.text);
            ui.text_size = p.value("text_size", ui.text_size);
            ui.value = p.value("value", ui.value);
            ui.visible = p.value("visible", ui.visible);
            ui.order = p.value("order", ui.order);
            return ok(id, {{"name", name}});
        }
        if (method == "ui.remove") {
            auto e = scene.find(p.at("name").get<std::string>());
            if (e == entt::null) return fail(id, "no such element");
            scene.registry.remove<UIElement>(e);
            return ok(id);
        }
        if (method == "particles.emit") {
            auto e = scene.find(p.at("name").get<std::string>());
            if (e == entt::null) e = scene.create(p.at("name").get<std::string>());
            apply_transform(scene.registry.get_or_emplace<Transform>(e), p);
            auto& em = scene.registry.get_or_emplace<ParticleEmitter>(e);
            em.rate = p.value("rate", em.rate);
            em.lifetime = p.value("lifetime", em.lifetime);
            em.velocity = v3(p.value("velocity", json()), em.velocity);
            em.velocity_spread = v3(p.value("velocity_spread", json()), em.velocity_spread);
            em.gravity = v3(p.value("gravity", json()), em.gravity);
            if (p.contains("start_color") && p["start_color"].size() == 4)
                em.start_color = {p["start_color"][0], p["start_color"][1], p["start_color"][2], p["start_color"][3]};
            if (p.contains("end_color") && p["end_color"].size() == 4)
                em.end_color = {p["end_color"][0], p["end_color"][1], p["end_color"][2], p["end_color"][3]};
            em.start_size = p.value("start_size", em.start_size);
            em.end_size = p.value("end_size", em.end_size);
            em.emitting = p.value("emitting", true);
            return ok(id);
        }
        if (method == "particles.stop") {
            auto e = scene.find(p.at("name").get<std::string>());
            if (e == entt::null) return fail(id, "no such emitter");
            if (auto* em = scene.registry.try_get<ParticleEmitter>(e)) em->emitting = false;
            return ok(id);
        }
        if (method == "character.create") {
            auto e = scene.find(p.at("name").get<std::string>());
            if (e == entt::null) e = scene.create(p.at("name").get<std::string>());
            apply_transform(scene.registry.get_or_emplace<Transform>(e), p);
            auto& cc = scene.registry.get_or_emplace<CharacterController>(e);
            cc.radius = p.value("radius", cc.radius);
            cc.height = p.value("height", cc.height);
            cc.move_speed = p.value("move_speed", cc.move_speed);
            cc.jump_speed = p.value("jump_speed", cc.jump_speed);
            if (!scene.registry.all_of<MeshRenderer>(e) && p.value("mesh", true)) {
                MeshRenderer mr;
                mr.primitive = "sphere";
                mr.base_color = v3(p.value("base_color", json()), glm::vec3(0.9f, 0.7f, 0.3f));
                scene.registry.emplace<MeshRenderer>(e, mr);
                scene.resolve_gpu_meshes();
            }
            ctx.physics.sync_characters(scene);
            return ok(id);
        }
        if (method == "character.move") {
            auto e = scene.find(p.at("name").get<std::string>());
            if (e == entt::null) return fail(id, "no such character");
            auto* cc = scene.registry.try_get<CharacterController>(e);
            if (!cc) return fail(id, "not a character");
            glm::vec3 dir = v3(p.value("direction", json()), glm::vec3(0));
            if (glm::length(dir) > 1e-4f) dir = glm::normalize(dir);
            cc->desired_velocity = dir * p.value("speed", cc->move_speed);
            cc->path.clear();
            return ok(id);
        }
        if (method == "character.jump") {
            auto e = scene.find(p.at("name").get<std::string>());
            if (e == entt::null) return fail(id, "no such character");
            if (auto* cc = scene.registry.try_get<CharacterController>(e)) cc->want_jump = true;
            return ok(id);
        }
        if (method == "character.moveTo") {
            auto e = scene.find(p.at("name").get<std::string>());
            if (e == entt::null) return fail(id, "no such character");
            auto* cc = scene.registry.try_get<CharacterController>(e);
            auto* t = scene.registry.try_get<Transform>(e);
            if (!cc || !t) return fail(id, "not a character");
            glm::vec3 tgt = v3(p.at("target"), t->position);
            if (ctx.nav.ready()) {
                cc->path = ctx.nav.path(t->position, tgt);
                cc->path_idx = 0;
                if (cc->path.empty()) return fail(id, "no path");
            } else {
                cc->path = {tgt};
                cc->path_idx = 0;
            }
            return ok(id, {{"waypoints", cc->path.size()}});
        }
        if (method == "nav.bake") {
            glm::vec3 mn = v3(p.value("min", json()), glm::vec3(-25, 0, -25));
            glm::vec3 mx = v3(p.value("max", json()), glm::vec3(25, 0, 25));
            float cell = p.value("cell", 1.0f);
            ctx.physics.sync(scene);
            ctx.nav.bake(scene, mn, mx, cell);
            return ok(id);
        }
        if (method == "nav.path") {
            if (!ctx.nav.ready()) return fail(id, "nav not baked");
            glm::vec3 a = v3(p.at("from"), glm::vec3(0));
            glm::vec3 b = v3(p.at("to"), glm::vec3(0));
            auto pts = ctx.nav.path(a, b);
            json arr = json::array();
            for (auto& pt : pts) arr.push_back(v3(pt));
            return ok(id, {{"waypoints", arr}});
        }
        if (method == "behavior.set") {
            auto e = scene.find(p.at("name").get<std::string>());
            if (e == entt::null) return fail(id, "no such entity");
            json rules = p.contains("behaviors") ? p["behaviors"] : p.value("rules", json::array());
            scene.registry.emplace_or_replace<Behavior>(e, Behavior{rules, false});
            return ok(id);
        }
        if (method == "behavior.get") {
            auto e = scene.find(p.at("name").get<std::string>());
            if (e == entt::null) return fail(id, "no such entity");
            auto* b = scene.registry.try_get<Behavior>(e);
            return ok(id, {{"rules", b ? b->rules : json::array()}});
        }
        if (method == "event.emit") {
            ctx.behaviors.emit(p.at("event").get<std::string>());
            return ok(id);
        }
        if (method == "physics.play")  { ctx.sim_running = true;  return ok(id); }
        if (method == "physics.pause") { ctx.sim_running = false; return ok(id); }
        if (method == "physics.setGravity") {
            ctx.physics.world().set_gravity(v3(p.at("gravity"), glm::vec3(0, -9.81f, 0)));
            return ok(id);
        }
        if (method == "physics.getGravity")
            return ok(id, {{"gravity", v3(ctx.physics.world().gravity())}});
        if (method == "physics.raycast") {
            glm::vec3 o = v3(p.at("origin"), glm::vec3(0));
            glm::vec3 d = v3(p.at("direction"), glm::vec3(0, -1, 0));
            float maxd = p.value("max_distance", 1000.0f);
            ctx.physics.sync(scene);
            RayHit h = ctx.physics.raycast(o, d, maxd);
            if (!h.hit) return ok(id, {{"hit", false}});
            json r = {{"hit", true}, {"point", v3(h.point)}, {"normal", v3(h.normal)},
                      {"distance", h.distance}};
            auto ent = scene.registry.view<RigidBody>();
            for (auto [e, rb] : ent.each())
                if (rb.registered && rb.handle == h.body)
                    r["entity"] = scene.registry.get<Name>(e).value;
            return ok(id, r);
        }
        if (method == "observe.entities") {
            update_world_transforms(scene);
            CameraComp& cam = scene.camera();
            int vw = ctx.offscreen.width(), vh = ctx.offscreen.height();
            glm::mat4 vp = cam.proj(vh ? float(vw) / vh : 1.0f) * cam.view();
            json arr = json::array();
            for (auto [e, n, wt] : scene.registry.view<Name, WorldTransform>().each()) {
                glm::vec4 clip = vp * glm::vec4(wt.position, 1.0f);
                bool in_view = clip.w > 0.0f &&
                               std::abs(clip.x) <= clip.w && std::abs(clip.y) <= clip.w &&
                               clip.z >= -clip.w && clip.z <= clip.w;
                json je = {{"name", n.value},
                           {"position", v3(wt.position)},
                           {"distance", glm::length(wt.position - cam.position)},
                           {"in_view", in_view}};
                if (clip.w > 0.0f) {
                    je["screen"] = json::array({(clip.x / clip.w * 0.5f + 0.5f) * vw,
                                                (0.5f - clip.y / clip.w * 0.5f) * vh});
                }
                arr.push_back(je);
            }
            return ok(id, {{"entities", arr}, {"width", vw}, {"height", vh}});
        }
        if (method == "observe.stats") {
            ctx.renderer.render(scene, ctx.offscreen.id(), ctx.offscreen.width(),
                                ctx.offscreen.height());
            Framebuffer::bind_default(ctx.offscreen.width(), ctx.offscreen.height());
            const auto& s = ctx.renderer.stats();
            return ok(id, {{"entities", s.entities}, {"visible", s.visible},
                           {"culled", s.culled}, {"draw_calls", s.draw_calls},
                           {"instances", s.instances}, {"groups", s.groups},
                           {"cpu_ms", s.cpu_ms}});
        }
        if (method == "observe.screenshot") {
            int w = p.value("width", ctx.offscreen.width());
            int h = p.value("height", ctx.offscreen.height());
            ctx.offscreen.resize(w, h);

            std::string path = p.value("path", std::string());
            if (path.empty()) {
                fs::path dir = fs::current_path() / "screenshots";
                fs::create_directories(dir);
                path = (dir / "latest.png").string();
            } else if (fs::path(path).has_parent_path()) {
                std::error_code ec;
                fs::create_directories(fs::path(path).parent_path(), ec);
            }

            ctx.renderer.render(scene, ctx.offscreen.id(), w, h);
            bool saved = ctx.offscreen.save_png(path);
            Framebuffer::bind_default(w, h);
            if (!saved) return fail(id, "screenshot write failed");
            return ok(id, {{"path", path}, {"width", w}, {"height", h}});
        }

        return fail(id, "unknown method: " + method);
    } catch (const std::exception& ex) {
        return fail(id, std::string("exception: ") + ex.what());
    }
}

} // namespace eng
