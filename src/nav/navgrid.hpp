#pragma once
#include "scene/scene.hpp"
#include "scene/transform_system.hpp"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>
#include <vector>

namespace eng {

// A coarse XZ occupancy grid baked from static/kinematic bodies. Enough for
// "walk around the furniture" A* pathfinding; not a polygon navmesh.
class NavGrid {
public:
    void bake(Scene& scene, const glm::vec3& min, const glm::vec3& max, float cell) {
        cell_ = std::max(cell, 0.1f);
        min_ = min; max_ = max;
        w_ = std::max(1, (int)std::ceil((max.x - min.x) / cell_));
        d_ = std::max(1, (int)std::ceil((max.z - min.z) / cell_));
        blocked_.assign((size_t)w_ * d_, 0);

        update_world_transforms(scene);
        for (auto [e, wt, rb] : scene.registry.view<WorldTransform, RigidBody>().each()) {
            if (rb.type == "dynamic") continue;   // only static/kinematic obstacles
            auto* mrp = scene.registry.try_get<MeshRenderer>(e);
            if (mrp && mrp->primitive == "plane") continue;   // the ground itself
            glm::vec3 c = wt.position;
            glm::vec3 s{glm::length(glm::vec3(wt.matrix[0])), 0,
                        glm::length(glm::vec3(wt.matrix[2]))};
            float hx = 0.5f * s.x + 0.5f * cell_, hz = 0.5f * s.z + 0.5f * cell_;
            int x0 = clampx(world_to_x(c.x - hx)), x1 = clampx(world_to_x(c.x + hx));
            int z0 = clampz(world_to_z(c.z - hz)), z1 = clampz(world_to_z(c.z + hz));
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x) blocked_[idx(x, z)] = 1;
        }
        baked_ = true;
    }

    bool ready() const { return baked_; }

    // A* from `a` to `b`, returns world-space waypoints (empty if no path).
    std::vector<glm::vec3> path(const glm::vec3& a, const glm::vec3& b) const {
        if (!baked_) return {};
        int sx = clampx(world_to_x(a.x)), sz = clampz(world_to_z(a.z));
        int gx = clampx(world_to_x(b.x)), gz = clampz(world_to_z(b.z));
        int start = idx(sx, sz), goal = idx(gx, gz);

        std::vector<float> g((size_t)w_ * d_, 1e18f);
        std::vector<int> came((size_t)w_ * d_, -1);
        using QN = std::pair<float, int>;
        std::priority_queue<QN, std::vector<QN>, std::greater<QN>> open;
        g[start] = 0;
        open.push({heur(sx, sz, gx, gz), start});

        const int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
        const int dz[8] = {0, 0, 1, -1, 1, -1, 1, -1};
        while (!open.empty()) {
            int cur = open.top().second; open.pop();
            if (cur == goal) break;
            int cx = cur % w_, cz = cur / w_;
            for (int k = 0; k < 8; ++k) {
                int nx = cx + dx[k], nz = cz + dz[k];
                if (nx < 0 || nz < 0 || nx >= w_ || nz >= d_) continue;
                int ni = idx(nx, nz);
                if (blocked_[ni] && ni != goal) continue;
                float step = (k < 4) ? 1.0f : 1.41421f;
                float ng = g[cur] + step;
                if (ng < g[ni]) {
                    g[ni] = ng; came[ni] = cur;
                    open.push({ng + heur(nx, nz, gx, gz), ni});
                }
            }
        }
        if (came[goal] < 0 && goal != start) return {};

        std::vector<glm::vec3> pts;
        for (int c = goal; c != -1; c = came[c]) {
            int cx = c % w_, cz = c / w_;
            pts.push_back({min_.x + (cx + 0.5f) * cell_, a.y, min_.z + (cz + 0.5f) * cell_});
            if (c == start) break;
        }
        std::reverse(pts.begin(), pts.end());
        if (!pts.empty()) pts.back() = {b.x, a.y, b.z};
        return pts;
    }

private:
    int w_ = 0, d_ = 0;
    float cell_ = 1.0f;
    glm::vec3 min_{0}, max_{0};
    std::vector<uint8_t> blocked_;
    bool baked_ = false;

    int idx(int x, int z) const { return z * w_ + x; }
    int world_to_x(float x) const { return (int)std::floor((x - min_.x) / cell_); }
    int world_to_z(float z) const { return (int)std::floor((z - min_.z) / cell_); }
    int clampx(int x) const { return std::clamp(x, 0, w_ - 1); }
    int clampz(int z) const { return std::clamp(z, 0, d_ - 1); }
    static float heur(int x, int z, int gx, int gz) {
        return std::sqrt(float((x - gx) * (x - gx) + (z - gz) * (z - gz)));
    }
};

} // namespace eng
