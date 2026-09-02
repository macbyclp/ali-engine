#pragma once
#include "ecs/components.hpp"
#include <entt/entt.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace eng {

// The world. Source of truth is JSON; this is its in-memory, renderable form.
// Every entity has a unique Name that the AI uses to address it.
class Scene {
public:
    entt::registry registry;

    entt::entity create(const std::string& name);   // auto-suffixes on collision
    bool destroy(const std::string& name);
    entt::entity find(const std::string& name) const;
    std::vector<std::string> names() const;

    CameraComp& camera();                            // the active camera (created if missing)

    void clear();
    void load_json(const nlohmann::json& j);
    nlohmann::json to_json() const;

    bool load_file(const std::string& path);
    bool save_file(const std::string& path) const;

    // Prefab: a JSON fragment holding an entity and its descendants.
    nlohmann::json export_subtree(const std::string& root) const;
    // Instantiate `prefab` with names prefixed by `new_root`; the subtree root
    // becomes exactly `new_root`. Returns the created names. `at` offsets the root.
    std::vector<std::string> instantiate(const nlohmann::json& prefab,
                                         const std::string& new_root,
                                         const glm::vec3& at, bool use_at);

    // Resolve MeshRenderer::gpu for every entity (call after load / primitive change).
    void resolve_gpu_meshes();

private:
    std::string unique_name(const std::string& base) const;
};

} // namespace eng
