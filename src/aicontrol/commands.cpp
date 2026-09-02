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
            return ok(id, {{"name", name}});
        }
        if (method == "entity.destroy") {
            std::string name = p.at("name").get<std::string>();
            return scene.destroy(name) ? ok(id) : fail(id, "no such entity: " + name);
        }
        if (method == "entity.setTransform") {
            auto e = scene.find(p.at("name").get<std::string>());
            if (e == entt::null) return fail(id, "no such entity");
            apply_transform(scene.registry.get_or_emplace<Transform>(e), p);
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

            ctx.offscreen.bind();
            ctx.renderer.render(scene, w, h);
            bool saved = ctx.offscreen.save_png(path);
            Framebuffer::unbind();
            if (!saved) return fail(id, "screenshot write failed");
            return ok(id, {{"path", path}, {"width", w}, {"height", h}});
        }

        return fail(id, "unknown method: " + method);
    } catch (const std::exception& ex) {
        return fail(id, std::string("exception: ") + ex.what());
    }
}

} // namespace eng
