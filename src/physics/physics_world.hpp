#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace eng {

enum class BodyType { Static, Dynamic, Kinematic };

struct BodyDesc {
    BodyType type = BodyType::Dynamic;
    std::string shape = "box";        // box | sphere
    glm::vec3 half_extents{0.5f};
    float radius = 0.5f;
    float mass = 1.0f;
    float restitution = 0.2f;
    float friction = 0.5f;
    glm::vec3 position{0};
    glm::quat rotation{1, 0, 0, 0};
};

struct RayHit {
    bool hit = false;
    uint32_t body = 0;
    glm::vec3 point{0};
    glm::vec3 normal{0, 1, 0};
    float distance = 0.0f;
};

// Thin wrapper over a Jolt PhysicsSystem. Bodies are addressed by a uint32 handle
// (Jolt BodyID bits) so the ECS layer never touches Jolt headers.
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    uint32_t add_body(const BodyDesc& desc);
    void remove_body(uint32_t handle);
    void set_transform(uint32_t handle, const glm::vec3& pos, const glm::quat& rot);
    void get_transform(uint32_t handle, glm::vec3& pos, glm::quat& rot) const;
    void set_linear_velocity(uint32_t handle, const glm::vec3& v);
    void add_impulse(uint32_t handle, const glm::vec3& j);

    void set_gravity(const glm::vec3& g);
    glm::vec3 gravity() const;

    void step(float dt);
    RayHit raycast(const glm::vec3& origin, const glm::vec3& dir, float max_distance) const;

    // Contact pairs (handle,handle) detected during the most recent step(s).
    std::vector<std::pair<uint32_t, uint32_t>> drain_contacts();

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace eng
