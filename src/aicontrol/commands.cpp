#include "aicontrol/commands.hpp"
#include "core/log.hpp"
#include <filesystem>
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
            mr.base_color = v3(p.value("base_color", json()), mr.base_color);
            mr.metallic = p.value("metallic", mr.metallic);
            mr.roughness = p.value("roughness", mr.roughness);
            scene.registry.emplace<MeshRenderer>(e, mr);
            scene.resolve_gpu_meshes();

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
            mr->base_color = v3(p.value("base_color", json()), mr->base_color);
            mr->metallic = p.value("metallic", mr->metallic);
            mr->roughness = p.value("roughness", mr->roughness);
            return ok(id);
        }
        if (method == "light.set") {
            std::string name = p.value("name", std::string("sun"));
            auto e = scene.find(name);
            if (e == entt::null) e = scene.create(name);
            auto& dl = scene.registry.get_or_emplace<DirectionalLight>(e);
            dl.direction = v3(p.value("direction", json()), dl.direction);
            dl.color = v3(p.value("color", json()), dl.color);
            dl.intensity = p.value("intensity", dl.intensity);
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
                ctx.behaviors.tick(scene, ctx.physics, dt);
                ctx.physics.step(scene, dt, substeps);
            }
            return ok(id, {{"stepped", steps}, {"dt", dt}});
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
                fs::path dir = fs::path(ENGINE_ASSET_DIR) / "screenshots";
                fs::create_directories(dir);
                path = (dir / "latest.png").string();
            } else {
                fs::create_directories(fs::path(path).parent_path());
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
