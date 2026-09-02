#pragma once
#include "physics/physics_world.hpp"
#include "scene/scene.hpp"
#include <unordered_map>

namespace eng {

// Bridges the ECS and the physics world: creates/destroys Jolt bodies to match
// RigidBody components, steps the simulation, writes dynamic transforms back.
class PhysicsSystem {
public:
    void sync(Scene& scene);                    // reconcile bodies with ECS
    void step(Scene& scene, float dt, int substeps = 1);
    void teleport(Scene& scene, const std::string& name);  // push ECS transform -> body

    PhysicsWorld& world() { return world_; }
    RayHit raycast(const glm::vec3& o, const glm::vec3& d, float max_d) const {
        return world_.raycast(o, d, max_d);
    }

private:
    PhysicsWorld world_;
    std::unordered_map<uint32_t, entt::entity> handle_to_entity_;
};

} // namespace eng
