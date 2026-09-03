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
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollidePointResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>

#include <algorithm>
#include <cstdarg>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

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

class ContactCollector final : public JPH::ContactListener {
public:
    void OnContactAdded(const JPH::Body& b1, const JPH::Body& b2,
                        const JPH::ContactManifold&, JPH::ContactSettings&) override {
        std::lock_guard<std::mutex> lk(mtx_);
        pairs_.emplace_back(b1.GetID().GetIndexAndSequenceNumber(),
                            b2.GetID().GetIndexAndSequenceNumber());
    }
    void OnContactRemoved(const JPH::SubShapeIDPair& pair) override {
        std::lock_guard<std::mutex> lk(mtx_);
        gone_.emplace_back(pair.GetBody1ID().GetIndexAndSequenceNumber(),
                           pair.GetBody2ID().GetIndexAndSequenceNumber());
    }
    std::vector<std::pair<uint32_t, uint32_t>> drain() {
        std::lock_guard<std::mutex> lk(mtx_);
        auto out = std::move(pairs_);
        pairs_.clear();
        return out;
    }
    std::vector<std::pair<uint32_t, uint32_t>> drain_gone() {
        std::lock_guard<std::mutex> lk(mtx_);
        auto out = std::move(gone_);
        gone_.clear();
        return out;
    }
private:
    std::mutex mtx_;
    std::vector<std::pair<uint32_t, uint32_t>> pairs_, gone_;
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
    ContactCollector contacts;
    JPH::PhysicsSystem system;

    JPH::BodyInterface& bi() { return system.GetBodyInterface(); }
    static JPH::BodyID id(uint32_t h) { return JPH::BodyID(h); }

    std::unordered_map<uint32_t, JPH::Ref<JPH::CharacterVirtual>> chars;
    uint32_t next_char = 1;

    std::unordered_map<uint32_t, JPH::Ref<JPH::TwoBodyConstraint>> joints;
    uint32_t next_joint = 1;
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
    p_->system.SetContactListener(&p_->contacts);
    p_->system.SetGravity(JPH::Vec3(0, -9.81f, 0));
}

PhysicsWorld::~PhysicsWorld() = default;

uint32_t PhysicsWorld::add_body(const BodyDesc& d) {
    JPH::ShapeRefC shape;
    if (d.shape == "heightfield" && d.hf_samples && d.hf_count >= 2) {
        int n = d.hf_count;
        const float* src = d.hf_samples;
        // Jolt wants the sample count to be a multiple of the block size (2);
        // pad an odd grid by one replicated row/column.
        std::vector<float> padded;
        int m = n;
        if (n % 2 != 0) {
            m = n + 1;
            padded.resize((size_t)m * m);
            for (int z = 0; z < m; ++z)
                for (int x = 0; x < m; ++x)
                    padded[(size_t)z * m + x] =
                        src[(size_t)std::min(z, n - 1) * n + std::min(x, n - 1)];
            src = padded.data();
        }
        float spacing = d.hf_size / float(n - 1);
        JPH::Vec3 offset(-0.5f * d.hf_size, 0.0f, -0.5f * d.hf_size);
        JPH::Vec3 scale(spacing, glm::max(d.hf_height, 1e-4f), spacing);
        JPH::HeightFieldShapeSettings hs(src, offset, scale, (JPH::uint32)m);
        auto res = hs.Create();
        if (res.IsValid()) {
            shape = res.Get();
        } else {
            eng::log::error("heightfield shape: %s", res.GetError().c_str());
            shape = new JPH::BoxShape(JPH::Vec3(0.5f * d.hf_size, 0.1f, 0.5f * d.hf_size));
        }
    }
    else if (d.shape == "sphere")
        shape = new JPH::SphereShape(d.radius);
    else
        shape = new JPH::BoxShape(to_j(glm::max(d.half_extents, glm::vec3(0.02f))));

    bool moving = d.type != BodyType::Static;
    JPH::EMotionType motion = d.type == BodyType::Dynamic   ? JPH::EMotionType::Dynamic
                              : d.type == BodyType::Kinematic ? JPH::EMotionType::Kinematic
                                                             : JPH::EMotionType::Static;
    JPH::ObjectLayer layer = moving ? Layers::MOVING : Layers::NON_MOVING;

    if (d.sensor) {   // sensors are kinematic so they also detect resting bodies
        motion = JPH::EMotionType::Kinematic;
        layer = Layers::MOVING;
        moving = true;
    }
    JPH::BodyCreationSettings s(shape, to_j(d.position), to_j(d.rotation), motion, layer);
    s.mIsSensor = d.sensor;
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
void PhysicsWorld::add_impulse(uint32_t h, const glm::vec3& j) {
    p_->bi().AddImpulse(Impl::id(h), to_j(j));
}

void PhysicsWorld::set_gravity(const glm::vec3& g) { p_->system.SetGravity(to_j(g)); }
glm::vec3 PhysicsWorld::gravity() const { return to_g(p_->system.GetGravity()); }

void PhysicsWorld::step(float dt) {
    if (dt <= 0.0f) return;
    p_->system.Update(dt, 1, &p_->temp, &p_->jobs);
}

std::vector<std::pair<uint32_t, uint32_t>> PhysicsWorld::drain_contacts() {
    return p_->contacts.drain();
}

std::vector<std::pair<uint32_t, uint32_t>> PhysicsWorld::drain_separations() {
    return p_->contacts.drain_gone();
}

uint32_t PhysicsWorld::create_character(const glm::vec3& pos, float radius, float height) {
    float cyl = std::max(0.05f, 0.5f * height - radius);
    JPH::Ref<JPH::CharacterVirtualSettings> s = new JPH::CharacterVirtualSettings();
    s->mShape = new JPH::CapsuleShape(cyl, radius);
    s->mMaxSlopeAngle = JPH::DegreesToRadians(46.0f);
    s->mSupportingVolume = JPH::Plane(JPH::Vec3(0, 1, 0), -radius);
    auto ch = new JPH::CharacterVirtual(s, to_j(pos + glm::vec3(0, 0.5f * height, 0)),
                                        JPH::Quat::sIdentity(), 0, &p_->system);
    uint32_t h = p_->next_char++;
    p_->chars[h] = ch;
    return h;
}

void PhysicsWorld::destroy_character(uint32_t h) { p_->chars.erase(h); }

void PhysicsWorld::character_set_velocity(uint32_t h, const glm::vec3& v) {
    auto it = p_->chars.find(h);
    if (it != p_->chars.end()) it->second->SetLinearVelocity(to_j(v));
}

void PhysicsWorld::character_update(uint32_t h, float dt) {
    auto it = p_->chars.find(h);
    if (it == p_->chars.end() || dt <= 0.0f) return;
    JPH::CharacterVirtual* ch = it->second;
    JPH::CharacterVirtual::ExtendedUpdateSettings us;
    ch->ExtendedUpdate(dt, p_->system.GetGravity(), us,
                       p_->system.GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
                       p_->system.GetDefaultLayerFilter(Layers::MOVING),
                       {}, {}, p_->temp);
}

glm::vec3 PhysicsWorld::character_position(uint32_t h) const {
    auto it = p_->chars.find(h);
    if (it == p_->chars.end()) return glm::vec3(0);
    return to_g(JPH::Vec3(it->second->GetPosition()));
}

bool PhysicsWorld::character_on_ground(uint32_t h) const {
    auto it = p_->chars.find(h);
    return it != p_->chars.end() &&
           it->second->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
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

static glm::vec3 any_perp(const glm::vec3& v) {
    glm::vec3 a = std::abs(v.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    return glm::normalize(glm::cross(v, a));
}

uint32_t PhysicsWorld::create_joint(const JointDesc& d) {
    const JPH::BodyLockInterface& bli = p_->system.GetBodyLockInterface();
    JPH::Body* ba = nullptr;
    JPH::Body* bb = &JPH::Body::sFixedToWorld;
    {
        JPH::BodyLockWrite la(bli, Impl::id(d.body_a));
        if (la.Succeeded()) ba = &la.GetBody();
    }
    if (d.body_b) {
        JPH::BodyLockWrite lb(bli, Impl::id(d.body_b));
        if (lb.Succeeded()) bb = &lb.GetBody();
    }
    if (!ba) return 0;

    glm::vec3 pa = to_g(ba->GetCenterOfMassPosition());
    glm::vec3 pb = (bb == &JPH::Body::sFixedToWorld) ? d.point : to_g(bb->GetCenterOfMassPosition());

    JPH::Ref<JPH::TwoBodyConstraintSettings> settings;
    if (d.type == "hinge") {
        auto* s = new JPH::HingeConstraintSettings();
        s->mSpace = JPH::EConstraintSpace::WorldSpace;
        s->mPoint1 = s->mPoint2 = to_j(d.point);
        glm::vec3 ax = glm::normalize(d.axis);
        s->mHingeAxis1 = s->mHingeAxis2 = to_j(ax);
        s->mNormalAxis1 = s->mNormalAxis2 = to_j(any_perp(ax));
        settings = s;
    } else if (d.type == "fixed") {
        auto* s = new JPH::FixedConstraintSettings();
        s->mSpace = JPH::EConstraintSpace::WorldSpace;
        s->mAutoDetectPoint = true;
        settings = s;
    } else if (d.type == "point") {
        auto* s = new JPH::PointConstraintSettings();
        s->mSpace = JPH::EConstraintSpace::WorldSpace;
        s->mPoint1 = s->mPoint2 = to_j(d.point);
        settings = s;
    } else {   // distance | spring
        auto* s = new JPH::DistanceConstraintSettings();
        s->mSpace = JPH::EConstraintSpace::WorldSpace;
        s->mPoint1 = to_j(pa);
        s->mPoint2 = to_j(pb);
        float cur = glm::length(pa - pb);
        float rest = d.length >= 0.0f ? d.length : cur;
        if (d.type == "spring") {
            s->mMinDistance = d.max_dist > d.min_dist ? d.min_dist : rest;
            s->mMaxDistance = d.max_dist > d.min_dist ? d.max_dist : rest;
            s->mLimitsSpringSettings.mFrequency = d.stiffness > 0.0f ? d.stiffness : 2.0f;
            s->mLimitsSpringSettings.mDamping = d.damping;
        } else {
            s->mMinDistance = d.max_dist > d.min_dist ? d.min_dist : cur;
            s->mMaxDistance = d.max_dist > d.min_dist ? d.max_dist : cur;
        }
        settings = s;
    }

    JPH::TwoBodyConstraint* c = settings->Create(*ba, *bb);
    if (!c) return 0;
    p_->system.AddConstraint(c);
    uint32_t h = p_->next_joint++;
    p_->joints[h] = c;
    if (d.body_a) p_->bi().ActivateBody(Impl::id(d.body_a));
    if (d.body_b) p_->bi().ActivateBody(Impl::id(d.body_b));
    return h;
}

void PhysicsWorld::remove_joint(uint32_t h) {
    auto it = p_->joints.find(h);
    if (it == p_->joints.end()) return;
    p_->system.RemoveConstraint(it->second);
    p_->joints.erase(it);
}

std::vector<uint32_t> PhysicsWorld::overlap_sphere(const glm::vec3& center, float radius) const {
    // Broad-phase AABB approximation: bodies whose broad-phase box meets the sphere.
    JPH::AllHitCollisionCollector<JPH::CollideShapeBodyCollector> collector;
    p_->system.GetBroadPhaseQuery().CollideSphere(to_j(center), radius, collector);
    std::vector<uint32_t> out;
    out.reserve(collector.mHits.size());
    for (const JPH::BodyID& id : collector.mHits)
        out.push_back(id.GetIndexAndSequenceNumber());
    return out;
}

RayHit PhysicsWorld::sphere_cast(const glm::vec3& origin, const glm::vec3& dir,
                                 float radius, float max_d) const {
    RayHit out;
    glm::vec3 nd = glm::normalize(dir);
    JPH::RefConst<JPH::SphereShape> shape = new JPH::SphereShape(glm::max(radius, 1e-3f));
    JPH::RShapeCast cast(shape, JPH::Vec3::sReplicate(1.0f),
                         JPH::RMat44::sTranslation(to_j(origin)), to_j(nd * max_d));
    JPH::ShapeCastSettings settings;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    p_->system.GetNarrowPhaseQuery().CastShape(cast, settings, JPH::RVec3::sZero(), collector);
    if (!collector.HadHit()) return out;

    out.hit = true;
    out.body = collector.mHit.mBodyID2.GetIndexAndSequenceNumber();
    out.distance = collector.mHit.mFraction * max_d;
    out.point = to_g(cast.mCenterOfMassStart.GetTranslation()) + nd * out.distance;
    out.normal = -glm::normalize(to_g(collector.mHit.mPenetrationAxis));
    return out;
}

} // namespace eng
