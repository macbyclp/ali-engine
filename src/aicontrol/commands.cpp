#include "aicontrol/commands.hpp"
#include "plugin/plugin_host.hpp"
#include "anim/animation_system.hpp"
#include "anim/animator.hpp"
#include "fx/particles.hpp"
#include "scene/transform_system.hpp"
#include "render/shader.hpp"
#include "render/gl.hpp"
#include "core/log.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
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

// A world-space ray from a camera through a normalized device point.
struct PickRay { glm::vec3 origin{0}, dir{0, 0, -1}; };
static PickRay pick_ray(const CameraComp& cam, glm::vec2 ndc, float aspect) {
    glm::mat4 inv = glm::inverse(cam.proj(aspect) * cam.view());
    glm::vec4 a = inv * glm::vec4(ndc.x, ndc.y, -1.0f, 1.0f);
    glm::vec4 b = inv * glm::vec4(ndc.x, ndc.y, 1.0f, 1.0f);
    glm::vec3 pa = glm::vec3(a) / a.w, pb = glm::vec3(b) / b.w;
    return {pa, glm::normalize(pb - pa)};
}
static bool ray_sphere(glm::vec3 o, glm::vec3 d, glm::vec3 c, float r, float& t) {
    glm::vec3 m = o - c;
    float b = glm::dot(m, d);
    float cc = glm::dot(m, m) - r * r;
    if (cc > 0.0f && b > 0.0f) return false;
    float disc = b * b - cc;
    if (disc < 0.0f) return false;
    t = -b - std::sqrt(disc);
    if (t < 0.0f) t = 0.0f;
    return true;
}
// max scale factor baked into a world matrix (for scaling local bounds radii)
static float max_scale(const glm::mat4& m) {
    return glm::max(glm::length(glm::vec3(m[0])),
                    glm::max(glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2]))));
}

static json ok(const json& id, json result = json::object()) {
    return {{"id", id}, {"ok", true}, {"result", std::move(result)}};
}

// Resolve a { "path": ... } param to a writable file path, creating parent dirs.
// Falls back to ./screenshots/<def> when no path is given.
static std::string resolve_out_path(const json& p, const char* def) {
    std::string path = p.value("path", std::string());
    if (path.empty()) {
        fs::path dir = fs::current_path() / "screenshots";
        fs::create_directories(dir);
        return (dir / def).string();
    }
    if (fs::path(path).has_parent_path()) {
        std::error_code ec;
        fs::create_directories(fs::path(path).parent_path(), ec);
    }
    return path;
}

// Leaked process-lifetime shader (GL context is gone by the time function-local
// statics would be destroyed, so never free it).
static eng::Shader& flat_shader() {
    static eng::Shader* s = new eng::Shader(
        "layout(location=0) in vec3 aPos;\n"
        "uniform mat4 uMVP;\n"
        "void main(){ gl_Position = uMVP * vec4(aPos, 1.0); }\n",
        "out vec4 F;\n"
        "uniform vec3 uColor;\n"
        "void main(){ F = vec4(uColor, 1.0); }\n");
    return *s;
}
static eng::Shader& depth_shader() {
    static eng::Shader* s = new eng::Shader(
        "layout(location=0) in vec3 aPos;\n"
        "uniform mat4 uMVP; uniform mat4 uMV;\n"
        "out float vViewZ;\n"
        "void main(){ vViewZ = -(uMV * vec4(aPos, 1.0)).z; gl_Position = uMVP * vec4(aPos, 1.0); }\n",
        "in float vViewZ; out vec4 F;\n"
        "uniform float uNear; uniform float uFar;\n"
        "void main(){ float g = clamp(1.0 - (vViewZ - uNear) / max(uFar - uNear, 1e-3), 0.0, 1.0);\n"
        "             F = vec4(vec3(g), 1.0); }\n");
    return *s;
}

// Axis-aligned world bounds of an entity's mesh / terrain. Returns false when the
// entity has no drawable extent (bare light / camera).
static bool world_aabb(entt::registry& reg, entt::entity e, glm::vec3& mn, glm::vec3& mx) {
    glm::vec3 lmn, lmx;
    if (auto* tc = reg.try_get<TerrainComp>(e)) {
        float half = tc->data.size * 0.5f;
        float top = tc->data.height;
        for (float v : tc->data.heights) top = glm::max(top, v * tc->data.height);
        lmn = {-half, 0.0f, -half};
        lmx = {half, top, half};
    } else if (auto* mr = reg.try_get<MeshRenderer>(e); mr && mr->gpu) {
        glm::vec3 he;
        if (mr->primitive == "cube" || mr->primitive == "sphere") he = glm::vec3(0.5f);
        else if (mr->primitive == "plane") he = glm::vec3(1.0f, 0.02f, 1.0f);
        else he = glm::vec3(mr->gpu->bounds_radius());
        glm::vec3 c = mr->gpu->bounds_center();
        lmn = c - he;
        lmx = c + he;
    } else {
        return false;
    }
    const glm::mat4* w = reg.try_get<WorldTransform>(e)
                             ? &reg.get<WorldTransform>(e).matrix : nullptr;
    glm::mat4 m = w ? *w : glm::mat4(1.0f);
    mn = glm::vec3(1e9f);
    mx = glm::vec3(-1e9f);
    for (int i = 0; i < 8; ++i) {
        glm::vec3 corner{(i & 1) ? lmx.x : lmn.x, (i & 2) ? lmx.y : lmn.y, (i & 4) ? lmx.z : lmn.z};
        glm::vec3 p = glm::vec3(m * glm::vec4(corner, 1.0f));
        mn = glm::min(mn, p);
        mx = glm::max(mx, p);
    }
    return true;
}

// Flat perception pass: clears `ctx.offscreen` (resized to w x h) and draws every
// visible mesh with `sh`, calling `per(entity, model, view, proj)` before each
// draw to bind per-entity uniforms. Restores the default framebuffer after.
static void perception_pass(
    CommandContext& ctx, Scene& scene, int w, int h, eng::Shader& sh,
    const std::function<void(entt::entity, const glm::mat4&, const glm::mat4&, const glm::mat4&)>& per) {
    update_world_transforms(scene);
    ctx.offscreen.resize(w, h);
    CameraComp& cam = scene.camera();
    glm::mat4 view = cam.view();
    glm::mat4 proj = cam.proj(h ? float(w) / h : 1.0f);

    glBindFramebuffer(GL_FRAMEBUFFER, ctx.offscreen.id());
    glViewport(0, 0, w, h);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    sh.use();
    for (auto [e, wt, mr] : scene.registry.view<WorldTransform, MeshRenderer>().each()) {
        if (!mr.gpu) continue;
        per(e, wt.matrix, view, proj);
        mr.gpu->draw();
    }
    Framebuffer::bind_default(w, h);
    glEnable(GL_CULL_FACE);
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

    if (ctx.recording && method != "quit" && method.rfind("record.", 0) != 0) {
        ctx.record_file << req.dump() << '\n';
        ctx.record_file.flush();
    }

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
            ctx.physics.clear();
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

            if (p.contains("light") && p["light"].is_object()) {
                const json& jl = p["light"];
                std::string lt = jl.value("type", std::string("point"));
                if (lt == "directional") {
                    DirectionalLight dl;
                    dl.direction = v3(jl.value("direction", json()), dl.direction);
                    dl.color = v3(jl.value("color", json()), dl.color);
                    dl.intensity = jl.value("intensity", dl.intensity);
                    scene.registry.emplace<DirectionalLight>(e, dl);
                } else {
                    PunctualLight pl;
                    pl.spot = (lt == "spot");
                    pl.color = v3(jl.value("color", json()), pl.color);
                    pl.intensity = jl.value("intensity", pl.intensity);
                    pl.range = jl.value("range", pl.range);
                    pl.direction = v3(jl.value("direction", json()), pl.direction);
                    pl.inner_deg = jl.value("inner_deg", pl.inner_deg);
                    pl.outer_deg = jl.value("outer_deg", pl.outer_deg);
                    pl.cast_shadows = jl.value("cast_shadows", pl.cast_shadows);
                    scene.registry.emplace<PunctualLight>(e, pl);
                }
                scene.registry.remove<MeshRenderer>(e);   // a light isn't a mesh
            }

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
                rb.sensor = jb.value("sensor", rb.sensor);
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
        if (method == "terrain.create") {
            std::string name = p.value("name", std::string("terrain"));
            auto e = scene.find(name);
            if (e == entt::null) e = scene.create(name);
            TerrainComp tc;
            tc.data.size = p.value("size", tc.data.size);
            tc.data.resolution = std::clamp(p.value("resolution", tc.data.resolution), 8, 384);
            tc.data.height = p.value("height", tc.data.height);
            tc.data.octaves = p.value("octaves", tc.data.octaves);
            tc.data.frequency = p.value("frequency", tc.data.frequency);
            tc.data.seed = p.value("seed", tc.data.seed);
            tc.data.regenerate_noise();
            scene.registry.emplace_or_replace<TerrainComp>(e, std::move(tc));
            auto& mr = scene.registry.get_or_emplace<MeshRenderer>(e);
            mr.primitive = "terrain";
            apply_material(mr, p);
            if (mr.base_color == glm::vec3(0.8f)) mr.base_color = {0.42f, 0.48f, 0.34f};
            scene.registry.get_or_emplace<Transform>(e);
            // terrain is walkable by default -- a static heightfield collider
            if (p.value("body", true)) {
                auto& rb = scene.registry.get_or_emplace<RigidBody>(e);
                rb.type = "static";
                rb.registered = false;
            } else {
                scene.registry.remove<RigidBody>(e);
            }
            scene.resolve_gpu_meshes();
            ctx.physics.sync(scene);
            return ok(id, {{"name", scene.registry.get<Name>(e).value},
                           {"resolution", scene.registry.get<TerrainComp>(e).data.resolution}});
        }
        if (method == "terrain.sculpt") {
            auto e = scene.find(p.at("name").get<std::string>());
            if (e == entt::null) return fail(id, "no such entity");
            auto* tc = scene.registry.try_get<TerrainComp>(e);
            if (!tc) return fail(id, "entity has no terrain");
            glm::vec3 at = v3(p.value("at", p.value("position", json())), glm::vec3(0));
            tc->data.sculpt(at.x, at.z, p.value("radius", 5.0f), p.value("strength", 1.0f),
                            p.value("mode", std::string("raise")));
            scene.resolve_gpu_meshes();
            ctx.physics.rebuild_body(scene, p.at("name").get<std::string>());  // refresh terrain collider
            return ok(id);
        }
        if (method == "terrain.height") {
            auto e = scene.find(p.at("name").get<std::string>());
            if (e == entt::null) return fail(id, "no such entity");
            auto* tc = scene.registry.try_get<TerrainComp>(e);
            if (!tc) return fail(id, "entity has no terrain");
            glm::vec3 at = v3(p.value("at", json()), glm::vec3(0));
            return ok(id, {{"height", tc->data.sample(at.x, at.z)}});
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
                pl.cast_shadows = p.value("cast_shadows", pl.cast_shadows);
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
                if (ctx.input) ctx.input->update(dt);
                if (ctx.plugins) ctx.plugins->update(ctx, dt);
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
        if (method == "render.set") {
            if (p.is_object()) for (auto& [k, v] : p.items()) scene.env[k] = v;
            return ok(id, {{"environment", scene.env}});
        }
        if (method == "render.get") return ok(id, {{"environment", scene.env}});
        if (method == "input.map") {
            if (!ctx.input) return fail(id, "input unavailable");
            if (p.contains("bindings") && p["bindings"].is_object()) {
                for (auto& [a, keys] : p["bindings"].items())
                    ctx.input->bind(a, keys.get<std::vector<std::string>>());
            } else {
                std::string a = p.at("action").get<std::string>();
                const json& k = p.at("keys");
                ctx.input->bind(a, k.is_string() ? std::vector<std::string>{k.get<std::string>()}
                                                 : k.get<std::vector<std::string>>());
            }
            return ok(id, {{"bindings", ctx.input->bindings_json()}});
        }
        if (method == "input.unmap") {
            if (!ctx.input) return fail(id, "input unavailable");
            if (p.contains("action")) ctx.input->unbind(p["action"].get<std::string>());
            else ctx.input->clear();
            return ok(id);
        }
        if (method == "input.state") {
            if (!ctx.input) return fail(id, "input unavailable");
            return ok(id, ctx.input->state_json());
        }
        // Scripted / AI input: hold or release an action. Gameplay cannot tell it
        // apart from a key press, so an agent can play its own game.
        if (method == "input.set") {
            if (!ctx.input) return fail(id, "input unavailable");
            if (p.contains("actions") && p["actions"].is_object()) {
                for (auto& [a, v] : p["actions"].items())
                    ctx.input->set_virtual(a, v.get<bool>());
            } else if (p.contains("action")) {
                ctx.input->set_virtual(p["action"].get<std::string>(), p.value("down", true));
            } else {
                ctx.input->clear_virtual();
            }
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
        if (method == "joint.create") {
            std::string a = p.at("a").get<std::string>();
            auto ea = scene.find(a);
            if (ea == entt::null) return fail(id, "no such entity: " + a);
            Joint j;
            j.a = a;
            j.b = p.value("b", std::string());
            if (!j.b.empty() && scene.find(j.b) == entt::null)
                return fail(id, "no such entity: " + j.b);
            j.type = p.value("type", std::string("point"));
            j.point = v3(p.value("point", json()), j.point);
            j.axis = v3(p.value("axis", json()), j.axis);
            j.min = p.value("min", j.min);
            j.max = p.value("max", j.max);
            j.length = p.value("length", j.length);
            j.stiffness = p.value("stiffness", j.stiffness);
            j.damping = p.value("damping", j.damping);
            scene.registry.emplace_or_replace<Joint>(ea, j);
            ctx.physics.sync(scene);
            ctx.physics.sync_joints(scene);
            auto* jr = scene.registry.try_get<Joint>(ea);
            if (!jr || !jr->registered)
                return fail(id, "joint create failed (both bodies need a RigidBody)");
            return ok(id, {{"a", a}, {"b", j.b}, {"type", j.type}});
        }
        if (method == "joint.remove") {
            std::vector<entt::entity> hit;
            bool by_a = p.contains("a"), by_b = p.contains("b");
            for (auto [e, j] : scene.registry.view<Joint>().each()) {
                if (by_a && j.a == p["a"].get<std::string>()) hit.push_back(e);
                else if (by_b && j.b == p["b"].get<std::string>()) hit.push_back(e);
                else if (!by_a && !by_b) hit.push_back(e);
            }
            for (auto e : hit) scene.registry.remove<Joint>(e);
            ctx.physics.sync_joints(scene);
            return ok(id, {{"removed", (int)hit.size()}});
        }
        if (method == "physics.overlapSphere") {
            glm::vec3 c = v3(p.at("center"), glm::vec3(0));
            float r = p.value("radius", 1.0f);
            ctx.physics.sync(scene);
            json names = json::array();
            for (uint32_t h : ctx.physics.world().overlap_sphere(c, r)) {
                auto e = ctx.physics.entity_for_body(h);
                if (e != entt::null && scene.registry.all_of<Name>(e))
                    names.push_back(scene.registry.get<Name>(e).value);
            }
            return ok(id, {{"entities", names}});
        }
        if (method == "physics.spherecast") {
            glm::vec3 o = v3(p.at("origin"), glm::vec3(0));
            glm::vec3 d = v3(p.at("direction"), glm::vec3(0, -1, 0));
            float r = p.value("radius", 0.5f);
            float maxd = p.value("max_distance", 1000.0f);
            ctx.physics.sync(scene);
            RayHit h = ctx.physics.world().sphere_cast(o, d, r, maxd);
            if (!h.hit) return ok(id, {{"hit", false}});
            json res = {{"hit", true}, {"point", v3(h.point)}, {"normal", v3(h.normal)},
                        {"distance", h.distance}};
            auto e = ctx.physics.entity_for_body(h.body);
            if (e != entt::null && scene.registry.all_of<Name>(e))
                res["entity"] = scene.registry.get<Name>(e).value;
            return ok(id, res);
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
        if (method == "observe.pick") {
            update_world_transforms(scene);
            CameraComp& cam = scene.camera();
            int vw = p.value("width", ctx.offscreen.width());
            int vh = p.value("height", ctx.offscreen.height());
            float aspect = vh ? float(vw) / vh : 1.0f;
            glm::vec2 ndc;
            if (p.contains("ndc") && p["ndc"].is_array() && p["ndc"].size() == 2) {
                ndc = {p["ndc"][0].get<float>(), p["ndc"][1].get<float>()};
            } else if (p.contains("screen") && p["screen"].is_array() && p["screen"].size() == 2) {
                float sx = p["screen"][0].get<float>(), sy = p["screen"][1].get<float>();
                ndc = {sx / vw * 2.0f - 1.0f, 1.0f - sy / vh * 2.0f};
            } else {
                return fail(id, "observe.pick needs screen:[x,y] or ndc:[x,y]");
            }
            float maxd = p.value("max_distance", 1000.0f);
            PickRay ray = pick_ray(cam, ndc, aspect);

            ctx.physics.sync(scene);
            RayHit h = ctx.physics.raycast(ray.origin, ray.dir, maxd);
            bool hit_any = h.hit;
            float best_t = h.hit ? h.distance : maxd;
            glm::vec3 best_pt = h.hit ? h.point : ray.origin + ray.dir * maxd;
            glm::vec3 best_nrm = h.hit ? h.normal : glm::vec3(0, 1, 0);
            std::string best_name;
            if (h.hit) {
                for (auto [e, rb] : scene.registry.view<RigidBody>().each())
                    if (rb.registered && rb.handle == h.body)
                        best_name = scene.registry.get<Name>(e).value;
            }
            // entities without a body: ray vs world-space bounding sphere
            for (auto [e, n, wt, mr] : scene.registry.view<Name, WorldTransform, MeshRenderer>().each()) {
                if (scene.registry.all_of<RigidBody>(e) || !mr.gpu) continue;
                glm::vec3 c = glm::vec3(wt.matrix * glm::vec4(mr.gpu->bounds_center(), 1.0f));
                float r = mr.gpu->bounds_radius() * max_scale(wt.matrix);
                float t;
                if (ray_sphere(ray.origin, ray.dir, c, r, t) && t < best_t) {
                    hit_any = true;
                    best_t = t;
                    best_name = n.value;
                    best_pt = ray.origin + ray.dir * t;
                    best_nrm = glm::length(best_pt - c) > 1e-5f ? glm::normalize(best_pt - c)
                                                               : glm::vec3(0, 1, 0);
                }
            }
            json r = {{"hit", hit_any}};
            if (hit_any) {
                if (!best_name.empty()) r["entity"] = best_name;
                r["point"] = v3(best_pt);
                r["normal"] = v3(best_nrm);
                r["distance"] = best_t;
            }
            return ok(id, r);
        }
        if (method == "observe.segment") {
            int w = p.value("width", ctx.offscreen.width());
            int h = p.value("height", ctx.offscreen.height());
            json by_index = json::object();   // "1" -> name
            json by_color = json::object();   // "r,g,b" -> name   (background is "0,0,0")
            int next = 1;
            perception_pass(ctx, scene, w, h, flat_shader(),
                [&](entt::entity e, const glm::mat4& m, const glm::mat4& v, const glm::mat4& pr) {
                    int idv = next++;
                    // Knuth multiplicative hash spreads consecutive ids across the
                    // colour cube so small scenes still look distinct.
                    uint32_t hb = (uint32_t)idv * 2654435761u;
                    int r = (hb >> 16) & 0xFF, g = (hb >> 8) & 0xFF, b = hb & 0xFF;
                    if ((r | g | b) == 0) b = 0x40;   // never collide with the background
                    std::string nm = scene.registry.all_of<Name>(e)
                                         ? scene.registry.get<Name>(e).value : std::string();
                    by_index[std::to_string(idv)] = nm;
                    by_color[std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b)] = nm;
                    flat_shader().set("uMVP", pr * v * m);
                    flat_shader().set("uColor", glm::vec3(r / 255.0f, g / 255.0f, b / 255.0f));
                });
            std::string path = resolve_out_path(p, "segment.png");
            if (!ctx.offscreen.save_png(path)) return fail(id, "segment write failed");
            return ok(id, {{"colorKey", by_index}, {"colors", by_color},
                           {"path", path}, {"width", w}, {"height", h}});
        }
        if (method == "observe.depth") {
            int w = p.value("width", ctx.offscreen.width());
            int h = p.value("height", ctx.offscreen.height());
            CameraComp& cam = scene.camera();
            // Auto-fit the greyscale range to the visible geometry unless overridden.
            update_world_transforms(scene);
            glm::mat4 vmat = cam.view();
            float lo = 1e9f, hi = -1e9f;
            for (auto [e, wt, mr] : scene.registry.view<WorldTransform, MeshRenderer>().each()) {
                if (!mr.gpu) continue;
                glm::vec3 c = glm::vec3(wt.matrix * glm::vec4(mr.gpu->bounds_center(), 1.0f));
                float rad = mr.gpu->bounds_radius() * max_scale(wt.matrix);
                float vz = -(vmat * glm::vec4(c, 1.0f)).z;
                lo = glm::min(lo, vz - rad);
                hi = glm::max(hi, vz + rad);
            }
            if (hi <= lo) { lo = cam.near_z; hi = glm::min(cam.far_z, 80.0f); }
            float near_z = p.value("near", glm::max(lo, cam.near_z));
            float far_z = p.value("far", hi);
            perception_pass(ctx, scene, w, h, depth_shader(),
                [&](entt::entity, const glm::mat4& m, const glm::mat4& v, const glm::mat4& pr) {
                    depth_shader().set("uMVP", pr * v * m);
                    depth_shader().set("uMV", v * m);
                    depth_shader().set("uNear", near_z);
                    depth_shader().set("uFar", far_z);
                });
            std::string path = resolve_out_path(p, "depth.png");
            if (!ctx.offscreen.save_png(path)) return fail(id, "depth write failed");
            return ok(id, {{"path", path}, {"width", w}, {"height", h},
                           {"near", near_z}, {"far", far_z}});
        }
        if (method == "observe.describe") {
            update_world_transforms(scene);
            CameraComp& cam = scene.camera();
            int vw = ctx.offscreen.width(), vh = ctx.offscreen.height();
            glm::mat4 vp = cam.proj(vh ? float(vw) / vh : 1.0f) * cam.view();

            auto kind_of = [&](entt::entity e) -> const char* {
                if (scene.registry.all_of<CameraComp>(e)) return "camera";
                if (scene.registry.any_of<DirectionalLight, PunctualLight>(e)) return "light";
                if (scene.registry.all_of<TerrainComp>(e)) return "terrain";
                if (scene.registry.all_of<RigidBody>(e)) return "body";
                if (scene.registry.all_of<MeshRenderer>(e)) return "mesh";
                return "mesh";
            };

            struct Box { std::string name; glm::vec3 mn, mx, c; entt::entity e; bool terrain; };
            std::vector<Box> boxes;
            json ents = json::array();
            for (auto [e, n, wt] : scene.registry.view<Name, WorldTransform>().each()) {
                glm::vec3 mn, mx;
                bool has = world_aabb(scene.registry, e, mn, mx);
                glm::vec4 clip = vp * glm::vec4(wt.position, 1.0f);
                bool on_screen = clip.w > 0.0f && std::abs(clip.x) <= clip.w &&
                                 std::abs(clip.y) <= clip.w && clip.z >= -clip.w && clip.z <= clip.w;
                json je = {{"name", n.value}, {"kind", kind_of(e)},
                           {"position", v3(wt.position)}, {"on_screen", on_screen}};
                if (has) je["size"] = json::array({mx.x - mn.x, mx.y - mn.y, mx.z - mn.z});
                ents.push_back(je);
                if (has) boxes.push_back({n.value, mn, mx, 0.5f * (mn + mx), e,
                                          scene.registry.all_of<TerrainComp>(e)});
            }

            // pairwise relations between entities whose AABBs are close
            json rels = json::array();
            auto overlap = [](float amn, float amx, float bmn, float bmx) {
                return amn <= bmx && bmn <= amx;
            };
            auto within_box = [](const Box& a, const Box& b) {
                return a.mn.x >= b.mn.x - 1e-3f && a.mx.x <= b.mx.x + 1e-3f &&
                       a.mn.y >= b.mn.y - 1e-3f && a.mx.y <= b.mx.y + 1e-3f &&
                       a.mn.z >= b.mn.z - 1e-3f && a.mx.z <= b.mx.z + 1e-3f;
            };
            for (size_t i = 0; i < boxes.size(); ++i)
                for (size_t j = i + 1; j < boxes.size(); ++j) {
                    // orient so `hi` is the upper / `a` the candidate that rests on `b`
                    const Box& bi = boxes[i];
                    const Box& bj = boxes[j];
                    const Box& a = bi.c.y >= bj.c.y ? bi : bj;   // higher one
                    const Box& b = bi.c.y >= bj.c.y ? bj : bi;
                    glm::vec3 ext_a = a.mx - a.mn, ext_b = b.mx - b.mn;
                    float ra = glm::length(ext_a) * 0.5f, rb = glm::length(ext_b) * 0.5f;
                    float cd = glm::length(a.c - b.c);
                    if (cd > 1.6f * (ra + rb) + 0.5f) continue;   // not close: skip
                    bool xz = overlap(a.mn.x, a.mx.x, b.mn.x, b.mx.x) &&
                              overlap(a.mn.z, a.mx.z, b.mn.z, b.mx.z);
                    const char* rel = nullptr;
                    std::string ra_name = a.name, rb_name = b.name;
                    // a terrain's AABB is a tall slab — compare against its real surface
                    if (b.terrain) {
                        auto* tc = scene.registry.try_get<TerrainComp>(b.e);
                        float surf = tc ? tc->data.sample(a.c.x, a.c.z) : b.mx.y;
                        if (xz && a.mn.y <= surf + 0.3f && a.mn.y >= surf - 0.5f * ext_a.y - 0.3f)
                            rel = "on";
                        else if (xz && a.mn.y > surf) rel = "above";
                        if (rel) { rels.push_back({{"a", a.name}, {"rel", rel}, {"b", b.name}}); continue; }
                    }
                    // a terrain's AABB is a tall slab — never treat it as a container
                    if (!b.terrain && within_box(a, b)) rel = "inside";
                    else if (!a.terrain && within_box(b, a)) { rel = "inside"; std::swap(ra_name, rb_name); }
                    else if (xz && a.c.y > b.c.y && a.mn.y <= b.mx.y + 0.25f &&
                             a.mn.y >= b.mx.y - 0.5f * ext_a.y) rel = "on";
                    else if (xz && a.mn.y > b.mx.y + 0.1f) rel = "above";
                    else if (bi.mx.x < bj.mn.x) { rel = "left_of"; ra_name = bi.name; rb_name = bj.name; }
                    else if (bj.mx.x < bi.mn.x) { rel = "left_of"; ra_name = bj.name; rb_name = bi.name; }
                    else if (cd < 1.4f * (ra + rb) &&
                             glm::max(ra, rb) < 3.0f * glm::min(ra, rb)) rel = "near";
                    if (rel) rels.push_back({{"a", ra_name}, {"rel", rel}, {"b", rb_name}});
                }

            return ok(id, {{"camera", {{"position", v3(cam.position)},
                                       {"forward", v3(glm::normalize(cam.target - cam.position))}}},
                           {"entities", ents}, {"relations", rels}});
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

        if (method == "record.start") {
            std::string path = resolve_out_path(p, "record.jsonl");
            ctx.record_file.close();
            ctx.record_file.clear();
            ctx.record_file.open(path, std::ios::out | std::ios::trunc);
            if (!ctx.record_file) return fail(id, "cannot open: " + path);
            ctx.recording = true;
            ctx.record_path = path;
            return ok(id, {{"path", path}});
        }
        if (method == "record.stop") {
            ctx.recording = false;
            ctx.record_file.flush();
            ctx.record_file.close();
            return ok(id, {{"path", ctx.record_path}});
        }
        if (method == "record.play") {
            std::string path = p.at("path").get<std::string>();
            std::ifstream f(path);
            if (!f) return fail(id, "record not found: " + path);
            bool was_rec = ctx.recording;
            ctx.recording = false;   // never re-record while replaying
            // reset the physics world so a replay is reproducible from any state
            ctx.physics.clear();
            for (auto [e, rb] : scene.registry.view<RigidBody>().each()) rb.registered = false;
            for (auto [e, j] : scene.registry.view<Joint>().each()) j.registered = false;
            for (auto [e, cc] : scene.registry.view<CharacterController>().each()) cc.registered = false;
            int n = 0, failed = 0;
            std::string line;
            while (std::getline(f, line)) {
                if (line.empty()) continue;
                json rq;
                try { rq = json::parse(line); } catch (...) { continue; }
                std::string m = rq.value("method", std::string());
                if (m == "quit" || m.rfind("record.", 0) == 0) continue;
                json rr = dispatch(ctx, rq);
                if (!rr.value("ok", false)) ++failed;
                ++n;
            }
            ctx.recording = was_rec;
            return ok(id, {{"played", n}, {"failed", failed}, {"path", path}});
        }

        if (method == "plugin.list")
            return ok(id, {{"plugins", ctx.plugins ? ctx.plugins->list() : json::array()}});
        if (method == "plugin.load") {
            if (!ctx.plugins) return fail(id, "plugin host unavailable");
            std::string path = p.at("path").get<std::string>();
            std::string nm = ctx.plugins->load_library(path, ctx);
            if (nm.empty()) return fail(id, "plugin load failed: " + path);
            return ok(id, {{"name", nm}});
        }

        // plugins get a shot at anything the core engine does not recognise
        if (ctx.plugins) {
            if (auto r = ctx.plugins->dispatch(ctx, method, p)) {
                json out = std::move(*r);
                if (!out.contains("id")) out["id"] = id;
                if (!out.contains("ok")) out["ok"] = true;
                return out;
            }
        }

        return fail(id, "unknown method: " + method);
    } catch (const std::exception& ex) {
        return fail(id, std::string("exception: ") + ex.what());
    }
}

} // namespace eng
