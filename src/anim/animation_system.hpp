#pragma once
#include "scene/scene.hpp"
#include <glm/gtc/quaternion.hpp>

namespace eng {

// Advances every AnimationPlayer, cross-fading between clips, and fills
// joint_matrices from the (blended) pose.
inline void update_animations(Scene& scene, float dt) {
    for (auto [e, mr, ap] : scene.registry.view<MeshRenderer, AnimationPlayer>().each()) {
        if (!mr.skinned) continue;
        const SkinnedModel& model = *mr.skinned;

        auto clip_of = [&](const std::string& name) -> const AnimationClip* {
            std::string n = name.empty() ? model.first_clip() : name;
            auto it = model.clips.find(n);
            return it == model.clips.end() ? nullptr : &it->second;
        };
        const AnimationClip* cur = clip_of(ap.clip);
        if (!cur) continue;

        if (ap.playing) {
            ap.time += dt * ap.speed;
            if (ap.loop && cur->duration > 0.0f) ap.time = std::fmod(ap.time, cur->duration);
        }

        std::vector<JointPose> pose;
        sample_local(model.skeleton, *cur, ap.time, pose);

        if (ap.fade_left > 0.0f) {
            const AnimationClip* prev = clip_of(ap.prev_clip);
            if (prev) {
                ap.prev_time += dt;
                std::vector<JointPose> pp;
                sample_local(model.skeleton, *prev, ap.prev_time, pp);
                float w = glm::clamp(ap.fade_left / ap.fade_dur, 0.0f, 1.0f);  // prev weight
                for (size_t j = 0; j < pose.size() && j < pp.size(); ++j) {
                    pose[j].t = glm::mix(pose[j].t, pp[j].t, w);
                    pose[j].s = glm::mix(pose[j].s, pp[j].s, w);
                    pose[j].r = glm::normalize(glm::slerp(pose[j].r, pp[j].r, w));
                }
            }
            ap.fade_left -= dt;
        }

        compose_pose(model.skeleton, pose, ap.joint_matrices);
    }
}

} // namespace eng
