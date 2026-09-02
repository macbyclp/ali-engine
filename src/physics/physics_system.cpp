#include "physics/physics_system.hpp"
#include <glm/gtc/quaternion.hpp>
#include <unordered_set>

namespace eng {

static BodyType parse_type(const std::string& s) {
    if (s == "static") return BodyType::Static;
    if (s == "kinematic") return BodyType::Kinematic;
    return BodyType::Dynamic;
}

static BodyDesc describe(const Transform& t, const RigidBody& rb, const MeshRenderer* mr) {
    BodyDesc d;
    d.type = parse_type(rb.type);
    d.mass = rb.mass;
    d.restitution = rb.restitution;
    d.friction = rb.friction;
    d.position = t.position;
    d.rotation = glm::quat(glm::radians(t.euler_deg));

    std::string shape = rb.shape;
    if (shape.empty() && mr) {
        if (mr->primitive == "sphere") shape = "sphere";
        else shape = "box";   // cube, plane, gltf -> box proxy
    }
    if (shape == "sphere") {
        d.shape = "sphere";
        d.radius = 0.5f * glm::max(t.scale.x, glm::max(t.scale.y, t.scale.z));
    } else {
        d.shape = "box";
        glm::vec3 h = 0.5f * glm::abs(t.scale);
        if (mr && mr->primitive == "plane") h = glm::vec3(glm::abs(t.scale.x), 0.05f, glm::abs(t.scale.z));
        d.half_extents = h;
    }
    return d;
}

void PhysicsSystem::sync(Scene& scene) {
    auto& reg = scene.registry;

    // create bodies for new RigidBody components
    for (auto [e, t, rb] : reg.view<Transform, RigidBody>().each()) {
        if (rb.registered) continue;
        const MeshRenderer* mr = reg.try_get<MeshRenderer>(e);
        rb.handle = world_.add_body(describe(t, rb, mr));
        rb.registered = true;
        handle_to_entity_[rb.handle] = e;
    }

    // destroy bodies whose entity or RigidBody is gone
    std::unordered_set<uint32_t> alive;
    for (auto [e, rb] : reg.view<RigidBody>().each())
        if (rb.registered) alive.insert(rb.handle);

    for (auto it = handle_to_entity_.begin(); it != handle_to_entity_.end();) {
        if (!alive.count(it->first)) {
            world_.remove_body(it->first);
            it = handle_to_entity_.erase(it);
        } else {
            ++it;
        }
    }
}

void PhysicsSystem::step(Scene& scene, float dt, int substeps) {
    sync(scene);
    substeps = glm::clamp(substeps, 1, 32);
    for (int i = 0; i < substeps; ++i) world_.step(dt / substeps);

    for (auto [e, t, rb] : scene.registry.view<Transform, RigidBody>().each()) {
        if (!rb.registered || rb.type == "static") continue;
        glm::vec3 pos; glm::quat rot;
        world_.get_transform(rb.handle, pos, rot);
        t.position = pos;
        glm::vec3 eul = glm::eulerAngles(rot);
        t.euler_deg = glm::degrees(eul);
    }
}

void PhysicsSystem::teleport(Scene& scene, const std::string& name) {
    auto e = scene.find(name);
    if (e == entt::null) return;
    auto* rb = scene.registry.try_get<RigidBody>(e);
    auto* t = scene.registry.try_get<Transform>(e);
    if (!rb || !t || !rb->registered) return;
    world_.set_transform(rb->handle, t->position, glm::quat(glm::radians(t->euler_deg)));
    world_.set_linear_velocity(rb->handle, glm::vec3(0));
}

} // namespace eng
