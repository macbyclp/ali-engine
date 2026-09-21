#pragma once
#include "anim/animation.hpp"
#include "assets/texture.hpp"
#include "geo/terrain.hpp"
#include "render/mesh.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <entt/entt.hpp>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eng {

// Stable, AI-facing identifier for an entity.
struct Name {
    std::string value;
};

// Scene-graph link. parent is resolved from parent_name each hierarchy update.
struct Hierarchy {
    std::string parent_name;
};

// Resolved world-space transform, written by update_world_transforms() each frame.
struct WorldTransform {
    glm::mat4 matrix{1.0f};
    glm::vec3 position{0.0f};

    // cache bookkeeping owned by update_world_transforms(): the inputs `matrix` was built from
    glm::vec3 k_pos{0.0f};
    glm::quat k_rot{1, 0, 0, 0};
    glm::vec3 k_scale{1.0f};
    entt::entity parent = entt::null;
    uint64_t parent_ver = 0;
    uint64_t ver = 0;
    uint64_t stamp = 0;
    bool valid = false;
    bool busy = false;
};

// Local transform. Orientation is stored as a quaternion (`rotation`) -- the single
// source of truth, so physics / gizmos never round-trip through Euler angles and
// never hit gimbal flips. Euler degrees (XYZ order, R = Rz*Ry*Rx -- the historic
// convention of scene JSON, the editor and the AI protocol) are a *view* of it:
// euler_deg() / set_euler_deg(). A cached Euler triple is kept for as long as
// `rotation` is unchanged, so a value an author typed (e.g. y = 135) reads back
// unchanged instead of being re-derived into an equivalent-but-different triple.
struct Transform {
    glm::vec3 position{0};
    glm::quat rotation{1, 0, 0, 0};   // (w, x, y, z)
    glm::vec3 scale{1};

    static glm::quat quat_from_euler_deg(const glm::vec3& e) {
        glm::vec3 r = glm::radians(e);
        return glm::angleAxis(r.z, glm::vec3(0, 0, 1)) * glm::angleAxis(r.y, glm::vec3(0, 1, 0)) *
               glm::angleAxis(r.x, glm::vec3(1, 0, 0));
    }
    static glm::vec3 euler_deg_from_quat(const glm::quat& q) {
        glm::mat3 m = glm::mat3_cast(q);   // m[col][row]; R = Rz*Ry*Rx
        float sy = glm::clamp(-m[0][2], -1.0f, 1.0f);
        float y = std::asin(sy), x, z;
        if (std::abs(sy) < 0.99999f) {
            x = std::atan2(m[1][2], m[2][2]);
            z = std::atan2(m[0][1], m[0][0]);
        } else {                                   // gimbal lock: fold z into x
            z = 0.0f;
            x = std::atan2(-m[2][1], m[1][1]);
        }
        return glm::degrees(glm::vec3(x, y, z)) + glm::vec3(0.0f);   // +0: no "-0" in JSON
    }

    glm::vec3 euler_deg() const {
        if (!cache_valid_ || cache_q_ != rotation) {
            cache_e_ = euler_deg_from_quat(rotation);
            cache_q_ = rotation;
            cache_valid_ = true;
        }
        return cache_e_;
    }
    void set_euler_deg(const glm::vec3& e) {
        rotation = quat_from_euler_deg(e);
        cache_e_ = e; cache_q_ = rotation; cache_valid_ = true;
    }
    void add_euler_deg(const glm::vec3& d) { set_euler_deg(euler_deg() + d); }
    void set_rotation(const glm::quat& q) { rotation = glm::normalize(q); }

    glm::mat4 matrix() const {
        glm::mat4 m = glm::mat4_cast(rotation);
        m[0] *= scale.x; m[1] *= scale.y; m[2] *= scale.z;
        m[3] = glm::vec4(position, 1.0f);
        return m;
    }

private:
    mutable glm::vec3 cache_e_{0};
    mutable glm::quat cache_q_{1, 0, 0, 0};
    mutable bool cache_valid_ = false;
};

// Which primitive/asset to draw + its PBR material.
// Texture keys: a file path, or "builtin:<checker|grid|uv|normal|bumps>".
struct MeshRenderer {
    std::string primitive = "cube";      // cube | sphere | plane | gltf | skinned | procedural
    std::string gltf_path;               // used when primitive == "gltf" / "skinned"
    nlohmann::json build;                // procedural recipe (primitive == "procedural")

    glm::vec3 base_color{0.8f};
    float metallic = 0.0f;
    float roughness = 0.8f;
    glm::vec3 emissive{0.0f};
    float alpha = 1.0f;                  // < 1: drawn in the sorted, blended pass (no shadow / SSAO)
    glm::vec2 uv_scale{1.0f};

    std::string base_color_map;
    std::string normal_map;
    std::string metallic_roughness_map;
    std::string emissive_map;
    std::string ao_map;

    std::shared_ptr<Mesh> gpu;           // resolved, not serialized
    std::shared_ptr<Texture> t_base, t_normal, t_mr, t_emissive, t_ao;
    std::shared_ptr<SkinnedModel> skinned;   // set when the asset has a skeleton
};

// Plays an animation clip on a skinned MeshRenderer. joint_matrices is filled
// each frame by AnimationSystem and consumed by the renderer.
struct AnimationPlayer {
    std::string clip;
    float time = 0.0f;
    float speed = 1.0f;
    bool loop = true;
    bool playing = true;

    // crossfade: while fade_left > 0, blend from prev_clip@prev_time into clip@time
    std::string prev_clip;
    float prev_time = 0.0f;
    float fade_left = 0.0f;
    float fade_dur = 0.0f;

    std::vector<glm::mat4> joint_matrices;
};

// ---- animation state machine (drives AnimationPlayer.clip with cross-fades) ----
// One named state = one clip. A transition fires when every condition holds; the
// first match wins. Parameters are floats (bools are 0/1); a trigger is a param
// that a firing transition resets to 0.
struct AnimState {
    std::string name;
    std::string clip;
    float speed = 1.0f;
    bool loop = true;
};
struct AnimCondition {
    std::string param;
    std::string op = ">";   // > < >= <= == != , or "trigger"
    float value = 0.0f;
};
struct AnimTransition {
    std::string from;        // "" or "*" = from any state
    std::string to;
    std::vector<AnimCondition> when;
    float blend = 0.15f;
    float exit_time = 0.0f;  // >0: also require normalized clip time >= this
};
struct AnimatorController {
    std::vector<AnimState> states;
    std::vector<AnimTransition> transitions;
    std::string entry;                             // default state name
    std::string current;                           // runtime: active state
    std::unordered_map<std::string, float> params;
    bool started = false;

    const AnimState* state(const std::string& n) const {
        for (auto& s : states) if (s.name == n) return &s;
        return nullptr;
    }
};

// Heightmap terrain. Pairs with a MeshRenderer whose primitive == "terrain"
// (that carries the material); this holds the shape.
struct TerrainComp {
    TerrainData data;
};

struct DirectionalLight {
    glm::vec3 direction{-0.4f, -1.0f, -0.3f};
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
};

// Point / spot light. Position comes from WorldTransform. For spot, `direction`
// is the cone axis and inner/outer_deg the soft-edge cone half-angles.
struct PunctualLight {
    bool spot = false;
    glm::vec3 color{1.0f};
    float intensity = 5.0f;
    float range = 12.0f;
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    float inner_deg = 20.0f;
    float outer_deg = 30.0f;
    bool cast_shadows = true;   // spot lights only, atlas is limited to the first few
};

// Physics body. Shape defaults are derived from MeshRenderer::primitive + Transform::scale
// when not given explicitly. handle/registered are runtime-only (not serialized).
struct RigidBody {
    std::string type = "dynamic";   // static | dynamic | kinematic
    std::string shape;              // "", box, sphere  ("" = auto from primitive)
    float mass = 1.0f;
    float restitution = 0.2f;
    float friction = 0.5f;
    bool sensor = false;            // trigger volume: reports overlaps, pushes nothing
    uint32_t handle = 0;
    bool registered = false;
};

// A physics constraint between two bodies (entity `a` and entity `b`; empty `b`
// pins to the world). Lives on entity `a`. handle/registered are runtime-only.
//   hinge    : swings about `axis` through `point` (world space)
//   distance : rigid rod, length clamped to [min, max]
//   spring   : distance + spring; `length` sets the rest length, `stiffness` Hz
//   fixed    : locks relative position + orientation
//   point    : ball joint at `point`, orientation free
struct Joint {
    std::string a, b;
    std::string type = "point";
    glm::vec3 point{0.0f};
    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    float min = 0.0f;
    float max = 0.0f;
    float length = -1.0f;      // <0 = use current separation
    float stiffness = 0.0f;    // spring frequency in Hz (0 = rigid)
    float damping = 0.2f;
    uint32_t handle = 0;
    bool registered = false;
};

// Data-driven behaviour. `rules` is a JSON array of { "on": trigger, "do": [actions] }
// interpreted by BehaviorSystem each step. Triggers: start | tick | collision | event.
// Kept as raw JSON so an AI can emit it verbatim.
struct Behavior {
    nlohmann::json rules = nlohmann::json::array();
    bool started = false;
};

// Kinematic capsule character (Jolt CharacterVirtual). desired_velocity is the
// horizontal intent set each frame; gravity + ground handling is automatic.
struct CharacterController {
    float radius = 0.4f;
    float height = 1.8f;
    float move_speed = 5.0f;
    float jump_speed = 6.0f;
    glm::vec3 desired_velocity{0.0f};
    bool want_jump = false;
    bool on_ground = false;
    float vertical_vel = 0.0f;   // runtime: integrated gravity / jump speed
    std::vector<glm::vec3> path;
    size_t path_idx = 0;
    uint32_t handle = 0;
    bool registered = false;
};

struct Particle {
    glm::vec3 pos{0}, vel{0};
    float life = 0.0f, max_life = 1.0f;
};

// A CPU particle emitter. Particles spawn at the entity's world position.
struct ParticleEmitter {
    float rate = 40.0f;
    float lifetime = 2.0f;
    glm::vec3 velocity{0.0f, 3.0f, 0.0f};
    glm::vec3 velocity_spread{1.2f, 0.8f, 1.2f};
    glm::vec3 gravity{0.0f, -3.0f, 0.0f};
    glm::vec4 start_color{1.0f, 0.6f, 0.2f, 1.0f};
    glm::vec4 end_color{1.0f, 0.1f, 0.0f, 0.0f};
    float start_size = 0.35f;
    float end_size = 0.02f;
    bool emitting = true;

    float accum = 0.0f;
    uint32_t seed = 0x9e3779b9u;
    std::vector<Particle> particles;
};

// Screen-space UI. pos/size are normalized (0..1) relative to `anchor`.
struct UIElement {
    std::string kind = "panel";       // panel | text | bar
    std::string anchor = "top-left";  // top-left | top-right | top | center | bottom-left | ...
    glm::vec2 pos{0.04f, 0.04f};
    glm::vec2 size{0.25f, 0.09f};
    glm::vec4 color{0.0f, 0.0f, 0.0f, 0.55f};
    glm::vec4 fill_color{0.30f, 0.80f, 0.45f, 1.0f};
    std::string text;
    float text_size = 22.0f;
    glm::vec4 text_color{1.0f, 1.0f, 1.0f, 1.0f};
    float value = 1.0f;               // bar fill 0..1
    bool visible = true;
    int order = 0;
};

// The active view. One entity in the scene carries this.
struct CameraComp {
    glm::vec3 position{0, 2, 6};
    glm::vec3 target{0, 0, 0};
    float fov_deg = 60.0f;
    float near_z = 0.05f;
    float far_z = 500.0f;
    bool active = true;

    // Follow rig: when `follow` names an entity, the camera eases toward
    // (entity + offset) looking at (entity + look_at). Updated each sim step.
    std::string follow;
    glm::vec3 follow_offset{0, 6, 10};
    glm::vec3 follow_look{0, 1, 0};
    float follow_stiffness = 6.0f;

    glm::mat4 view() const { return glm::lookAt(position, target, {0, 1, 0}); }
    glm::mat4 proj(float aspect) const {
        return glm::perspective(glm::radians(fov_deg), aspect, near_z, far_z);
    }
};

} // namespace eng
