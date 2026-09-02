#pragma once
#include "render/mesh.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <string>

namespace eng {

// Stable, AI-facing identifier for an entity.
struct Name {
    std::string value;
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

// Which primitive/asset to draw. For M1: procedural primitives or a loaded glTF.
struct MeshRenderer {
    std::string primitive = "cube";      // cube | sphere | plane | gltf
    std::string gltf_path;               // used when primitive == "gltf"
    glm::vec3 base_color{0.8f};
    float metallic = 0.0f;
    float roughness = 0.8f;
    std::shared_ptr<Mesh> gpu;           // resolved at load, not serialized
};

struct DirectionalLight {
    glm::vec3 direction{-0.4f, -1.0f, -0.3f};
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
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
