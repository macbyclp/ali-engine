#include "physics/physics_world.hpp"
#include "core/log.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>

#include <algorithm>
#include <cstdarg>
#include <thread>

JPH_SUPPRESS_WARNINGS
using namespace JPH::literals;

namespace {

namespace Layers {
static constexpr JPH::ObjectLayer NON_MOVING = 0;
static constexpr JPH::ObjectLayer MOVING = 1;
static constexpr JPH::ObjectLayer NUM = 2;
}
namespace BP {
static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
static constexpr JPH::BroadPhaseLayer MOVING(1);
static constexpr JPH::uint NUM(2);
}

class BPLayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
    BPLayerInterface() {
        map_[Layers::NON_MOVING] = BP::NON_MOVING;
        map_[Layers::MOVING] = BP::MOVING;
    }
    JPH::uint GetNumBroadPhaseLayers() const override { return BP::NUM; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l) const override { return map_[l]; }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override { return "layer"; }
#endif
private:
    JPH::BroadPhaseLayer map_[Layers::NUM];
};

class ObjectVsBPFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer o1, JPH::BroadPhaseLayer o2) const override {
        if (o1 == Layers::NON_MOVING) return o2 == BP::MOVING;
        return true;
    }
};
class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer o1, JPH::ObjectLayer o2) const override {
        if (o1 == Layers::NON_MOVING) return o2 == Layers::MOVING;
        return true;
    }
};

static void trace_impl(const char* fmt, ...) {
    va_list list; va_start(list, fmt);
    char buf[1024]; vsnprintf(buf, sizeof(buf), fmt, list); va_end(list);
    eng::log::info("Jolt: %s", buf);
}

inline JPH::Vec3 to_j(const glm::vec3& v) { return {v.x, v.y, v.z}; }
inline JPH::Quat to_j(const glm::quat& q) { return {q.x, q.y, q.z, q.w}; }
inline glm::vec3 to_g(JPH::Vec3 v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
inline glm::quat to_g(JPH::Quat q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

} // namespace

namespace eng {

struct PhysicsWorld::Impl {
    JPH::TempAllocatorImpl temp{16 * 1024 * 1024};
    JPH::JobSystemThreadPool jobs{JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
        (int)std::max(1u, std::thread::hardware_concurrency() - 1)};
    BPLayerInterface bp_iface;
    ObjectVsBPFilter obj_vs_bp;
    ObjectPairFilter obj_pair;
    JPH::PhysicsSystem system;

    JPH::BodyInterface& bi() { return system.GetBodyInterface(); }
    static JPH::BodyID id(uint32_t h) { return JPH::BodyID(h); }
};

static bool g_jolt_inited = false;

static void ensure_jolt_globals() {
    if (g_jolt_inited) return;
    JPH::RegisterDefaultAllocator();   // must precede any Jolt allocation
    JPH::Trace = trace_impl;
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    g_jolt_inited = true;
}

PhysicsWorld::PhysicsWorld() {
    ensure_jolt_globals();
    p_ = std::make_unique<Impl>();     // temp allocator / job pool allocate here
    p_->system.Init(65536, 0, 65536, 10240, p_->bp_iface, p_->obj_vs_bp, p_->obj_pair);
    p_->system.SetGravity(JPH::Vec3(0, -9.81f, 0));
}

PhysicsWorld::~PhysicsWorld() = default;

uint32_t PhysicsWorld::add_body(const BodyDesc& d) {
    JPH::ShapeRefC shape;
    if (d.shape == "sphere")
        shape = new JPH::SphereShape(d.radius);
    else
        shape = new JPH::BoxShape(to_j(glm::max(d.half_extents, glm::vec3(0.02f))));

    bool moving = d.type != BodyType::Static;
    JPH::EMotionType motion = d.type == BodyType::Dynamic   ? JPH::EMotionType::Dynamic
                              : d.type == BodyType::Kinematic ? JPH::EMotionType::Kinematic
                                                             : JPH::EMotionType::Static;
    JPH::ObjectLayer layer = moving ? Layers::MOVING : Layers::NON_MOVING;

    JPH::BodyCreationSettings s(shape, to_j(d.position), to_j(d.rotation), motion, layer);
    s.mRestitution = d.restitution;
    s.mFriction = d.friction;
    if (d.type == BodyType::Dynamic) {
        s.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        s.mMassPropertiesOverride.mMass = glm::max(d.mass, 0.001f);
    }
    JPH::BodyID id = p_->bi().CreateAndAddBody(
        s, moving ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
    return id.GetIndexAndSequenceNumber();
}

void PhysicsWorld::remove_body(uint32_t h) {
    auto id = Impl::id(h);
    p_->bi().RemoveBody(id);
    p_->bi().DestroyBody(id);
}

void PhysicsWorld::set_transform(uint32_t h, const glm::vec3& pos, const glm::quat& rot) {
    p_->bi().SetPositionAndRotation(Impl::id(h), to_j(pos), to_j(rot), JPH::EActivation::Activate);
}

void PhysicsWorld::get_transform(uint32_t h, glm::vec3& pos, glm::quat& rot) const {
    JPH::RVec3 p; JPH::Quat q;
    p_->bi().GetPositionAndRotation(Impl::id(h), p, q);
    pos = to_g(JPH::Vec3(p));
    rot = to_g(q);
}

void PhysicsWorld::set_linear_velocity(uint32_t h, const glm::vec3& v) {
    p_->bi().SetLinearVelocity(Impl::id(h), to_j(v));
}

void PhysicsWorld::set_gravity(const glm::vec3& g) { p_->system.SetGravity(to_j(g)); }
glm::vec3 PhysicsWorld::gravity() const { return to_g(p_->system.GetGravity()); }

void PhysicsWorld::step(float dt) {
    if (dt <= 0.0f) return;
    p_->system.Update(dt, 1, &p_->temp, &p_->jobs);
}

RayHit PhysicsWorld::raycast(const glm::vec3& origin, const glm::vec3& dir, float max_d) const {
    RayHit out;
    glm::vec3 nd = glm::normalize(dir);
    JPH::RRayCast ray{to_j(origin), to_j(nd * max_d)};
    JPH::RayCastResult res;
    if (!p_->system.GetNarrowPhaseQuery().CastRay(ray, res)) return out;

    out.hit = true;
    out.body = res.mBodyID.GetIndexAndSequenceNumber();
    out.distance = res.mFraction * max_d;
    out.point = origin + nd * out.distance;

    JPH::BodyLockRead lock(p_->system.GetBodyLockInterface(), res.mBodyID);
    if (lock.Succeeded())
        out.normal = to_g(lock.GetBody().GetWorldSpaceSurfaceNormal(res.mSubShapeID2, to_j(out.point)));
    return out;
}

} // namespace eng
