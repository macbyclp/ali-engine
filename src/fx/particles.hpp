#pragma once
#include "scene/scene.hpp"
#include "scene/transform_system.hpp"
#include <algorithm>

namespace eng {

// Spawns and integrates CPU particles for every ParticleEmitter. Rendered by the
// renderer as additive camera-facing billboards.
inline void update_particles(Scene& scene, float dt) {
    update_world_transforms(scene);
    for (auto [e, wt, em] : scene.registry.view<WorldTransform, ParticleEmitter>().each()) {
        auto rnd = [&]() {
            em.seed = em.seed * 1664525u + 1013904223u;
            return (em.seed >> 8) / float(1 << 24);   // [0,1)
        };
        // integrate
        for (auto& p : em.particles) {
            p.vel += em.gravity * dt;
            p.pos += p.vel * dt;
            p.life -= dt;
        }
        em.particles.erase(
            std::remove_if(em.particles.begin(), em.particles.end(),
                           [](const Particle& p) { return p.life <= 0.0f; }),
            em.particles.end());

        // spawn
        if (em.emitting) {
            em.accum += em.rate * dt;
            int n = (int)em.accum;
            em.accum -= n;
            for (int i = 0; i < n && em.particles.size() < 20000; ++i) {
                Particle p;
                p.pos = wt.position;
                glm::vec3 j((rnd() - 0.5f), (rnd() - 0.5f), (rnd() - 0.5f));
                p.vel = em.velocity + j * em.velocity_spread * 2.0f;
                p.max_life = em.lifetime * (0.6f + 0.4f * rnd());
                p.life = p.max_life;
                em.particles.push_back(p);
            }
        }
    }
}

} // namespace eng
