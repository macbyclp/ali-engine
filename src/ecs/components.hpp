#pragma once
#include "anim/animation.hpp"
#include "assets/texture.hpp"
#include "render/mesh.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>

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
};

struct Transform {
    glm::vec3 position{0};
    glm::vec3 euler_deg{0};   // XYZ degrees
    glm::vec3 scale{1};

    glm::mat4 matrix() const {
        glm::mat4 m(1.0f);
        m = glm::translate(m, position);
        m = glm::rotate(m, glm::radians(euler_deg.z), {0, 0, 1});
        m = glm::rotate(m, glm::radians(euler_deg.y), {0, 1, 0});
        m = glm::rotate(m, glm::radians(euler_deg.x), {1, 0, 0});
        m = glm::scale(m, scale);
        return m;
    }
};

// Which primitive/asset to draw + its PBR material.
// Texture keys: a file path, or "builtin:<checker|grid|uv|normal|bumps>".
struct MeshRenderer {
    std::string primitive = "cube";      // cube | sphere | plane | gltf
    std::string gltf_path;               // used when primitive == "gltf"

    glm::vec3 base_color{0.8f};
    float metallic = 0.0f;
    float roughness = 0.8f;
    glm::vec3 emissive{0.0f};
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
};

// Physics body. Shape defaults are derived from MeshRenderer::primitive + Transform::scale
// when not given explicitly. handle/registered are runtime-only (not serialized).
struct RigidBody {
    std::string type = "dynamic";   // static | dynamic | kinematic
    std::string shape;              // "", box, sphere  ("" = auto from primitive)
    float mass = 1.0f;
    float restitution = 0.2f;
    float friction = 0.5f;
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

    glm::mat4 view() const { return glm::lookAt(position, target, {0, 1, 0}); }
    glm::mat4 proj(float aspect) const {
        return glm::perspective(glm::radians(fov_deg), aspect, near_z, far_z);
    }
};

} // namespace eng
