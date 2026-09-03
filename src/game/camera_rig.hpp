#pragma once
#include "scene/scene.hpp"
#include "scene/transform_system.hpp"
#include <glm/glm.hpp>
#include <algorithm>

namespace eng {

// Eases the active camera toward a follow target each sim step. No-op unless
// CameraComp::follow names a live entity.
inline void update_camera_rig(Scene& scene, float dt) {
    CameraComp& cam = scene.camera();
    if (cam.follow.empty()) return;
    entt::entity e = scene.find(cam.follow);
    if (e == entt::null) return;
    auto* wt = scene.registry.try_get<WorldTransform>(e);
    if (!wt) { update_world_transforms(scene); wt = scene.registry.try_get<WorldTransform>(e); }
    if (!wt) return;

    glm::vec3 want_pos = wt->position + cam.follow_offset;
    glm::vec3 want_tgt = wt->position + cam.follow_look;
    float k = std::clamp(cam.follow_stiffness * dt, 0.0f, 1.0f);
    cam.position += (want_pos - cam.position) * k;
    cam.target += (want_tgt - cam.target) * k;
}

} // namespace eng
