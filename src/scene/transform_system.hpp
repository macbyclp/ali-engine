#pragma once
#include "ecs/components.hpp"
#include "scene/scene.hpp"
#include <functional>
#include <unordered_map>

namespace eng {

// Resolves every entity's WorldTransform = parent.world * local. Handles arbitrary
// depth; cycles are broken (an entity parented into a cycle falls back to local).
inline void update_world_transforms(Scene& scene) {
    auto& reg = scene.registry;
    std::unordered_map<entt::entity, glm::mat4> done;

    std::function<glm::mat4(entt::entity, int)> resolve = [&](entt::entity e, int depth) -> glm::mat4 {
        if (auto it = done.find(e); it != done.end()) return it->second;
        auto* t = reg.try_get<Transform>(e);
        glm::mat4 local = t ? t->matrix() : glm::mat4(1.0f);
        glm::mat4 world = local;
        if (depth < 64) {
            if (auto* h = reg.try_get<Hierarchy>(e); h && !h->parent_name.empty()) {
                entt::entity p = scene.find(h->parent_name);
                if (p != entt::null && p != e)
                    world = resolve(p, depth + 1) * local;
            }
        }
        done[e] = world;
        return world;
    };

    for (auto [e, t] : reg.view<Transform>().each()) {
        glm::mat4 w = resolve(e, 0);
        auto& wt = reg.get_or_emplace<WorldTransform>(e);
        wt.matrix = w;
        wt.position = glm::vec3(w[3]);
    }
}

} // namespace eng
