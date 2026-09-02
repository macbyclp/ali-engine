#pragma once
#include "render/mesh.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eng {

constexpr int kMaxJoints = 128;

// Bind-pose skeleton. Joint i's parent is parents[i] (-1 for root).
struct Skeleton {
    std::vector<int> parents;
    std::vector<glm::vec3> bind_t;
    std::vector<glm::quat> bind_r;
    std::vector<glm::vec3> bind_s;
    std::vector<glm::mat4> inverse_bind;
    std::vector<std::string> names;
    size_t size() const { return parents.size(); }
};

// One animated property (T/R/S) of one joint, as keyframes with linear/slerp interp.
struct AnimChannel {
    int joint = 0;
    int path = 0;                 // 0 = translation, 1 = rotation, 2 = scale
    std::vector<float> times;
    std::vector<glm::vec4> values;   // vec3 for T/S (w unused), quat for R
};

struct AnimationClip {
    std::string name;
    float duration = 0.0f;
    std::vector<AnimChannel> channels;
};

// A skinned mesh + its skeleton + its animation clips. Cached by source key.
struct SkinnedModel {
    std::shared_ptr<Mesh> mesh;
    Skeleton skeleton;
    std::unordered_map<std::string, AnimationClip> clips;

    std::string first_clip() const {
        return clips.empty() ? std::string() : clips.begin()->first;
    }
};

// Samples a clip at `t` (seconds) and returns the world joint matrices to skin with:
// out[j] = globalPose(j) * inverseBind(j). `out` is resized to skeleton.size().
void sample_pose(const Skeleton& sk, const AnimationClip& clip, float t,
                 std::vector<glm::mat4>& out);

// Procedural test model: a vertical bar with `segments` joints and a "wave" clip.
std::shared_ptr<SkinnedModel> builtin_skinned(const std::string& name);

} // namespace eng
