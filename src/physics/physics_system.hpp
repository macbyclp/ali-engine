#pragma once
#include "physics/physics_world.hpp"
#include "scene/scene.hpp"
#include <unordered_map>
#include <unordered_set>

namespace eng {

// Bridges the ECS and the physics world: creates/destroys Jolt bodies to match
// RigidBody components, steps the simulation, writes dynamic transforms back.
class PhysicsSystem {
public:
    void sync(Scene& scene);                    // reconcile bodies with ECS
    void step(Scene& scene, float dt, int substeps = 1);
    void teleport(Scene& scene, const std::string& name);  // push ECS transform -> body

    void impulse(const std::string& name, Scene& scene, const glm::vec3& j);
    void set_velocity(const std::string& name, Scene& scene, const glm::vec3& v);

    // Character controllers: create bodies, integrate movement, write transforms.
    void sync_characters(Scene& scene);
    void step_characters(Scene& scene, float dt);

    // Contact pairs since last drain, resolved to entities (invalid if body gone).
    std::vector<std::pair<entt::entity, entt::entity>> drain_contacts();
    // pairs that stopped touching this step (drives the `exit` behaviour trigger)
    std::vector<std::pair<entt::entity, entt::entity>> drain_separations();

    PhysicsWorld& world() { return world_; }
    RayHit raycast(const glm::vec3& o, const glm::vec3& d, float max_d) const {
        return world_.raycast(o, d, max_d);
    }

private:
    PhysicsWorld world_;
    std::unordered_map<uint32_t, entt::entity> handle_to_entity_;
    std::unordered_set<uint32_t> character_handles_;
};

} // namespace eng
