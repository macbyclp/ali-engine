#include "physics/physics_system.hpp"
#include "scene/transform_system.hpp"
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
// world matrix -> (position, rotation), ignoring scale/shear
inline void decompose(const glm::mat4& m, glm::vec3& pos, glm::quat& rot) {
    pos = glm::vec3(m[3]);
    glm::mat3 r{glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])),
               glm::normalize(glm::vec3(m[2]))};
    rot = glm::quat_cast(r);
}
}

namespace eng {

static BodyType parse_type(const std::string& s) {
    if (s == "static") return BodyType::Static;
    if (s == "kinematic") return BodyType::Kinematic;
    return BodyType::Dynamic;
}

static BodyDesc describe(const glm::mat4& world, const RigidBody& rb, const MeshRenderer* mr) {
    BodyDesc d;
    d.type = parse_type(rb.type);
    d.mass = rb.mass;
    d.restitution = rb.restitution;
    d.friction = rb.friction;
    d.sensor = rb.sensor;
    decompose(world, d.position, d.rotation);

    glm::vec3 scale{glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])),
                    glm::length(glm::vec3(world[2]))};

    std::string shape = rb.shape;
    if (shape.empty() && mr) shape = (mr->primitive == "sphere") ? "sphere" : "box";

    if (shape == "sphere") {
        d.shape = "sphere";
        d.radius = 0.5f * glm::max(scale.x, glm::max(scale.y, scale.z));
    } else {
        d.shape = "box";
        glm::vec3 h = 0.5f * glm::abs(scale);
        if (mr && mr->primitive == "plane") h = glm::vec3(glm::abs(scale.x), 0.05f, glm::abs(scale.z));
        d.half_extents = h;
    }
    return d;
}

void PhysicsSystem::sync(Scene& scene) {
    auto& reg = scene.registry;
    update_world_transforms(scene);

    // create bodies for new RigidBody components
    for (auto [e, wt, rb] : reg.view<WorldTransform, RigidBody>().each()) {
        if (rb.registered) continue;
        const MeshRenderer* mr = reg.try_get<MeshRenderer>(e);
        BodyDesc bd = describe(wt.matrix, rb, mr);
        // A terrain entity gets a collider matching its heightfield (always static).
        if (const TerrainComp* tc = reg.try_get<TerrainComp>(e)) {
            const TerrainData& td = tc->data;
            if ((int)td.heights.size() == td.resolution * td.resolution && td.resolution >= 2) {
                bd.type = BodyType::Static;
                bd.shape = "heightfield";
                bd.hf_samples = td.heights.data();
                bd.hf_count = td.resolution;
                bd.hf_size = td.size;
                bd.hf_height = td.height;
            }
        }
        rb.handle = world_.add_body(bd);
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

void PhysicsSystem::sync_joints(Scene& scene) {
    auto& reg = scene.registry;
    auto body_of = [&](const std::string& nm) -> uint32_t {
        auto e = scene.find(nm);
        if (e == entt::null) return 0;
        auto* rb = reg.try_get<RigidBody>(e);
        return (rb && rb->registered) ? rb->handle : 0;
    };

    for (auto [e, j] : reg.view<Joint>().each()) {
        if (j.registered) continue;
        uint32_t ha = body_of(j.a);
        uint32_t hb = j.b.empty() ? 0 : body_of(j.b);
        if (!ha) continue;                       // body a not ready yet; retry next sync
        if (!j.b.empty() && !hb) continue;
        JointDesc d;
        d.type = j.type;
        d.body_a = ha;
        d.body_b = hb;
        d.point = j.point;
        d.axis = j.axis;
        d.min_dist = j.min;
        d.max_dist = j.max;
        d.length = j.length;
        d.stiffness = j.stiffness;
        d.damping = j.damping;
        j.handle = world_.create_joint(d);
        j.registered = j.handle != 0;
        if (j.registered) joint_handles_.insert(j.handle);
    }

    std::unordered_set<uint32_t> alive;
    for (auto [e, j] : reg.view<Joint>().each())
        if (j.registered) alive.insert(j.handle);
    for (auto it = joint_handles_.begin(); it != joint_handles_.end();) {
        if (!alive.count(*it)) { world_.remove_joint(*it); it = joint_handles_.erase(it); }
        else ++it;
    }
}

void PhysicsSystem::step(Scene& scene, float dt, int substeps) {
    sync(scene);
    sync_joints(scene);
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

void PhysicsSystem::rebuild_body(Scene& scene, const std::string& name) {
    auto e = scene.find(name);
    if (e == entt::null) return;
    auto* rb = scene.registry.try_get<RigidBody>(e);
    if (!rb || !rb->registered) return;
    world_.remove_body(rb->handle);
    handle_to_entity_.erase(rb->handle);
    rb->registered = false;
    sync(scene);
}

void PhysicsSystem::impulse(const std::string& name, Scene& scene, const glm::vec3& j) {
    auto e = scene.find(name);
    if (e == entt::null) return;
    if (auto* rb = scene.registry.try_get<RigidBody>(e); rb && rb->registered)
        world_.add_impulse(rb->handle, j);
}
void PhysicsSystem::set_velocity(const std::string& name, Scene& scene, const glm::vec3& v) {
    auto e = scene.find(name);
    if (e == entt::null) return;
    if (auto* rb = scene.registry.try_get<RigidBody>(e); rb && rb->registered)
        world_.set_linear_velocity(rb->handle, v);
}

void PhysicsSystem::sync_characters(Scene& scene) {
    std::unordered_set<uint32_t> alive;
    for (auto [e, t, cc] : scene.registry.view<Transform, CharacterController>().each()) {
        if (!cc.registered) {
            cc.handle = world_.create_character(t.position, cc.radius, cc.height);
            cc.registered = true;
            character_handles_.insert(cc.handle);
        }
        alive.insert(cc.handle);
    }
    for (auto it = character_handles_.begin(); it != character_handles_.end();) {
        if (!alive.count(*it)) { world_.destroy_character(*it); it = character_handles_.erase(it); }
        else ++it;
    }
}

void PhysicsSystem::step_characters(Scene& scene, float dt) {
    sync_characters(scene);
    glm::vec3 g = world_.gravity();
    for (auto [e, t, cc] : scene.registry.view<Transform, CharacterController>().each()) {
        if (!cc.registered) continue;

        glm::vec3 want = cc.desired_velocity;
        // follow a path if one is set
        if (!cc.path.empty() && cc.path_idx < cc.path.size()) {
            glm::vec3 tgt = cc.path[cc.path_idx];
            glm::vec3 d = tgt - t.position; d.y = 0.0f;
            float dist = glm::length(d);
            if (dist < 0.35f) {
                if (++cc.path_idx >= cc.path.size()) { cc.path.clear(); cc.path_idx = 0; }
            } else {
                want = (d / dist) * cc.move_speed;
            }
        }

        cc.on_ground = world_.character_on_ground(cc.handle);
        glm::vec3 v = want;
        // vertical: keep falling unless grounded; jump on request
        if (cc.on_ground) cc.vertical_vel = cc.want_jump ? cc.jump_speed : 0.0f;
        else cc.vertical_vel += g.y * dt;
        cc.want_jump = false;
        v.y = cc.vertical_vel;

        world_.character_set_velocity(cc.handle, v);
        world_.character_update(cc.handle, dt);

        glm::vec3 center = world_.character_position(cc.handle);
        t.position = center - glm::vec3(0, 0.5f * cc.height, 0);

        // face travel direction
        glm::vec3 flat(want.x, 0, want.z);
        if (glm::length(flat) > 0.1f)
            t.euler_deg.y = glm::degrees(std::atan2(flat.x, flat.z));

        cc.desired_velocity = glm::vec3(0);
    }
}

std::vector<std::pair<entt::entity, entt::entity>> PhysicsSystem::drain_contacts() {
    std::vector<std::pair<entt::entity, entt::entity>> out;
    for (auto [ha, hb] : world_.drain_contacts()) {
        auto ia = handle_to_entity_.find(ha);
        auto ib = handle_to_entity_.find(hb);
        if (ia != handle_to_entity_.end() && ib != handle_to_entity_.end())
            out.emplace_back(ia->second, ib->second);
    }
    return out;
}

std::vector<std::pair<entt::entity, entt::entity>> PhysicsSystem::drain_separations() {
    std::vector<std::pair<entt::entity, entt::entity>> out;
    for (auto [ha, hb] : world_.drain_separations()) {
        auto ia = handle_to_entity_.find(ha);
        auto ib = handle_to_entity_.find(hb);
        if (ia != handle_to_entity_.end() && ib != handle_to_entity_.end())
            out.emplace_back(ia->second, ib->second);
    }
    return out;
}

} // namespace eng
