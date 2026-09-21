#pragma once
#include "ecs/components.hpp"
#include "scene/scene.hpp"
#include <cstdint>

namespace eng {

// Counters for the world-transform cache (see observe.stats / --microbench).
struct WorldStats {
    uint64_t calls = 0;        // update_world_transforms() invocations
    uint64_t visited = 0;      // entities checked
    uint64_t recomputed = 0;   // world matrices actually rebuilt
};
inline WorldStats g_world_stats;

// Resolves every entity's WorldTransform = parent.world * local. Handles arbitrary
// depth; cycles are broken (an entity parented into a cycle falls back to local).
//
// Cached: each WorldTransform remembers the exact local pose (position/rotation/scale)
// and parent version it was built from. A matrix is rebuilt only when one of those
// inputs changed, so calling this several times a frame (physics sync, particles,
// renderer, observe.*) costs one cheap compare pass -- an entity's world matrix is
// computed at most once per change, never redundantly. Correct by construction: there is
// no dirty flag to forget to set, whoever mutates a Transform is picked up.
inline void update_world_transforms(Scene& scene) {
    auto& reg = scene.registry;
    static uint64_t next_version = 1;
    static uint64_t call_id = 0;
    const uint64_t call = ++call_id;
    ++g_world_stats.calls;

    // returns the entity's world version (changes whenever its world matrix does)
    struct Resolver {
        Scene& scene; entt::registry& reg; uint64_t call;
        uint64_t operator()(entt::entity e, int depth) {
            auto& wt = reg.get_or_emplace<WorldTransform>(e);
            if (wt.stamp == call) return wt.busy ? 0 : wt.ver;   // done (or a cycle: ignore link)
            wt.stamp = call;
            wt.busy = true;
            ++g_world_stats.visited;

            const Transform* t = reg.try_get<Transform>(e);
            entt::entity parent = entt::null;
            uint64_t parent_ver = 0;
            const glm::mat4* parent_world = nullptr;
            if (depth < 64) {
                if (auto* h = reg.try_get<Hierarchy>(e); h && !h->parent_name.empty()) {
                    entt::entity p = scene.find(h->parent_name);
                    if (p != entt::null && p != e && reg.try_get<Transform>(p)) {
                        parent_ver = (*this)(p, depth + 1);
                        if (parent_ver != 0) {     // 0 = cycle: fall back to local
                            parent = p;
                            parent_world = &reg.get<WorldTransform>(p).matrix;
                        }
                    }
                }
            }
            const bool local_changed =
                !wt.valid || !t || t->position != wt.k_pos || t->rotation != wt.k_rot || t->scale != wt.k_scale;
            const bool parent_changed = parent != wt.parent || parent_ver != wt.parent_ver;
            if (local_changed || parent_changed) {
                glm::mat4 local = t ? t->matrix() : glm::mat4(1.0f);
                wt.matrix = parent_world ? *parent_world * local : local;
                wt.position = glm::vec3(wt.matrix[3]);
                if (t) { wt.k_pos = t->position; wt.k_rot = t->rotation; wt.k_scale = t->scale; }
                wt.parent = parent;
                wt.parent_ver = parent_ver;
                wt.valid = true;
                wt.ver = next_version++;
                ++g_world_stats.recomputed;
            }
            wt.busy = false;
            return wt.ver;
        }
    } resolve{scene, reg, call};

    for (auto e : reg.view<Transform>()) resolve(e, 0);
}

} // namespace eng
