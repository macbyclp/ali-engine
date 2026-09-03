#include "anim/animation.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace eng {

static glm::mat4 trs(const glm::vec3& t, const glm::quat& r, const glm::vec3& s) {
    return glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(r) * glm::scale(glm::mat4(1.0f), s);
}

// find keyframe interval and blend factor
static void locate(const std::vector<float>& times, float t, int& i0, int& i1, float& a) {
    if (times.empty()) { i0 = i1 = 0; a = 0; return; }
    if (t <= times.front()) { i0 = i1 = 0; a = 0; return; }
    if (t >= times.back()) { i0 = i1 = (int)times.size() - 1; a = 0; return; }
    int hi = (int)(std::upper_bound(times.begin(), times.end(), t) - times.begin());
    i0 = hi - 1; i1 = hi;
    float span = times[i1] - times[i0];
    a = span > 1e-6f ? (t - times[i0]) / span : 0.0f;
}

void sample_local(const Skeleton& sk, const AnimationClip& clip, float t,
                  std::vector<JointPose>& out) {
    size_t n = sk.size();
    out.resize(n);
    for (size_t j = 0; j < n; ++j) out[j] = {sk.bind_t[j], sk.bind_r[j], sk.bind_s[j]};

    float ct = clip.duration > 0.0f ? std::fmod(t, clip.duration) : 0.0f;
    for (const AnimChannel& ch : clip.channels) {
        if (ch.joint < 0 || (size_t)ch.joint >= n || ch.times.empty()) continue;
        int i0, i1; float a;
        locate(ch.times, ct, i0, i1, a);
        if (ch.path == 1) {
            glm::quat q0(ch.values[i0].w, ch.values[i0].x, ch.values[i0].y, ch.values[i0].z);
            glm::quat q1(ch.values[i1].w, ch.values[i1].x, ch.values[i1].y, ch.values[i1].z);
            out[ch.joint].r = glm::normalize(glm::slerp(q0, q1, a));
        } else {
            glm::vec3 v = glm::mix(glm::vec3(ch.values[i0]), glm::vec3(ch.values[i1]), a);
            if (ch.path == 0) out[ch.joint].t = v; else out[ch.joint].s = v;
        }
    }
}

void compose_pose(const Skeleton& sk, const std::vector<JointPose>& local,
                  std::vector<glm::mat4>& out) {
    size_t n = sk.size();
    std::vector<glm::mat4> global(n);
    for (size_t j = 0; j < n; ++j) {
        glm::mat4 m = trs(local[j].t, local[j].r, local[j].s);
        int p = sk.parents[j];
        global[j] = (p >= 0 && (size_t)p < n) ? global[p] * m : m;
    }
    out.resize(n);
    for (size_t j = 0; j < n; ++j) out[j] = global[j] * sk.inverse_bind[j];
}

void sample_pose(const Skeleton& sk, const AnimationClip& clip, float t,
                 std::vector<glm::mat4>& out) {
    std::vector<JointPose> local;
    sample_local(sk, clip, t, local);
    compose_pose(sk, local, out);
}

std::shared_ptr<SkinnedModel> builtin_skinned(const std::string& name) {
    // "bendbar" (default): a tall box, `segs` joints stacked on Y, "wave" clip.
    const int segs = 6;
    const float total_h = 4.0f, half_w = 0.35f;
    const float seg_h = total_h / segs;

    auto model = std::make_shared<SkinnedModel>();
    Skeleton& sk = model->skeleton;
    sk.parents.resize(segs);
    sk.bind_t.resize(segs);
    sk.bind_r.assign(segs, glm::quat(1, 0, 0, 0));
    sk.bind_s.assign(segs, glm::vec3(1));
    sk.inverse_bind.resize(segs);
    sk.names.resize(segs);
    for (int j = 0; j < segs; ++j) {
        sk.parents[j] = j - 1;
        sk.bind_t[j] = (j == 0) ? glm::vec3(0, 0, 0) : glm::vec3(0, seg_h, 0);
        sk.names[j] = "joint" + std::to_string(j);
        // world bind position of joint j is (0, j*seg_h, 0)
        sk.inverse_bind[j] = glm::translate(glm::mat4(1.0f), glm::vec3(0, -j * seg_h, 0));
    }

    // geometry: a box from y=0..total_h, rings of 4 verts per segment boundary
    std::vector<Vertex> v;
    std::vector<uint32_t> idx;
    const glm::vec3 corners[4] = {{-half_w, 0, -half_w}, {half_w, 0, -half_w},
                                  {half_w, 0, half_w}, {-half_w, 0, half_w}};
    for (int r = 0; r <= segs; ++r) {
        float y = r * seg_h;
        for (int c = 0; c < 4; ++c) {
            Vertex vert;
            vert.pos = {corners[c].x, y, corners[c].z};
            vert.normal = glm::normalize(glm::vec3(corners[c].x, 0.1f, corners[c].z));
            vert.uv = {c / 4.0f, (float)r / segs};
            float fj = y / seg_h;
            int j0 = std::min((int)std::floor(fj), segs - 1);
            int j1 = std::min(j0 + 1, segs - 1);
            float w1 = fj - j0;
            vert.joints = glm::ivec4(j0, j1, 0, 0);
            vert.weights = glm::vec4(1.0f - w1, w1, 0, 0);
            v.push_back(vert);
        }
    }
    for (int r = 0; r < segs; ++r) {
        for (int c = 0; c < 4; ++c) {
            uint32_t a = r * 4 + c, b = r * 4 + (c + 1) % 4;
            uint32_t a2 = a + 4, b2 = b + 4;
            idx.insert(idx.end(), {a, b, b2, a, b2, a2});
        }
    }
    model->mesh = std::make_shared<Mesh>(v, idx);

    // "wave" clip: each joint rotates about Z with a travelling sine
    AnimationClip clip;
    clip.name = "wave";
    clip.duration = 2.0f;
    const int keys = 32;
    for (int j = 1; j < segs; ++j) {
        AnimChannel ch;
        ch.joint = j;
        ch.path = 1;
        for (int k = 0; k <= keys; ++k) {
            float tt = clip.duration * k / keys;
            float ang = 0.35f * std::sin(tt * glm::two_pi<float>() / clip.duration
                                         - j * 0.8f);
            glm::quat q = glm::angleAxis(ang, glm::vec3(0, 0, 1));
            ch.times.push_back(tt);
            ch.values.push_back(glm::vec4(q.x, q.y, q.z, q.w));
        }
        clip.channels.push_back(std::move(ch));
    }
    model->clips["wave"] = std::move(clip);

    // procedural extras so a state machine has something to switch between:
    // "idle" -- a slow, low-amplitude sway ; "bend" -- a static lean, held.
    auto sway = [&](const char* nm, float dur, float amp, float freq) {
        AnimationClip c;
        c.name = nm;
        c.duration = dur;
        for (int j = 1; j < segs; ++j) {
            AnimChannel ch; ch.joint = j; ch.path = 1;
            for (int k = 0; k <= keys; ++k) {
                float tt = dur * k / keys;
                float ang = amp * std::sin(tt * glm::two_pi<float>() * freq / dur - j * 0.5f);
                glm::quat q = glm::angleAxis(ang, glm::vec3(0, 0, 1));
                ch.times.push_back(tt);
                ch.values.push_back(glm::vec4(q.x, q.y, q.z, q.w));
            }
            c.channels.push_back(std::move(ch));
        }
        model->clips[nm] = std::move(c);
    };
    sway("idle", 3.0f, 0.06f, 1.0f);
    sway("bend", 1.0f, 0.55f, 0.0f);   // freq 0 -> constant lean, effectively a pose

    (void)name;
    return model;
}

} // namespace eng
