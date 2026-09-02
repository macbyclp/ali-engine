#pragma once
#include "scene/scene.hpp"

namespace eng {

// Advances every AnimationPlayer and fills its joint_matrices from the clip.
inline void update_animations(Scene& scene, float dt) {
    for (auto [e, mr, ap] : scene.registry.view<MeshRenderer, AnimationPlayer>().each()) {
        if (!mr.skinned) continue;
        const SkinnedModel& model = *mr.skinned;

        std::string clip_name = ap.clip.empty() ? model.first_clip() : ap.clip;
        auto it = model.clips.find(clip_name);
        if (it == model.clips.end()) continue;
        const AnimationClip& clip = it->second;

        if (ap.playing) ap.time += dt * ap.speed;
        if (ap.loop && clip.duration > 0.0f)
            ap.time = std::fmod(ap.time, clip.duration);
        else if (clip.duration > 0.0f)
            ap.time = std::min(ap.time, clip.duration);

        sample_pose(model.skeleton, clip, ap.time, ap.joint_matrices);
    }
}

} // namespace eng
