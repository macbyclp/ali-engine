#include "bench/microbench.hpp"
#include "editor/history.hpp"
#include "scene/transform_system.hpp"
#include <chrono>
#include <cstdio>
#include <functional>

using nlohmann::json;

namespace eng {

using Clock = std::chrono::steady_clock;
static double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

static json make_scene(int n) {
    json ents = json::array();
    int side = 1;
    while (side * side < n) ++side;
    for (int i = 0; i < n; ++i) {
        json e;
        e["name"] = "e" + std::to_string(i);
        e["transform"] = {{"position", {float(i % side) * 1.5f, 0.5f, float(i / side) * 1.5f}},
                          {"rotation", {0, float(i % 90), 0}}, {"scale", {1, 1, 1}}};
        e["mesh"] = {{"primitive", i % 3 == 0 ? "sphere" : "cube"},
                     {"base_color", {0.2f + 0.6f * float(i % 7) / 7.0f, 0.5f, 0.4f}}};
        if (i % 5 == 4) e["parent"] = "e" + std::to_string(i - 1);
        ents.push_back(e);
    }
    return {{"entities", ents}};
}

int run_microbench(int n, CommandContext& ctx) {
    Scene& scene = ctx.scene;
    json out;
    out["n"] = n;
    json src = make_scene(n);

    auto t0 = Clock::now();
    scene.load_json(src);
    out["load_ms"] = ms_since(t0);

    // --- world transforms (cache counters are reported for the idle / moving phases): idle (nothing changed) and 1% moving, 3 calls = one sim frame's worth
    update_world_transforms(scene);
    const int reps = 100;
    t0 = Clock::now();
    for (int i = 0; i < reps; ++i) update_world_transforms(scene);
    out["world_idle_ms_per_call"] = ms_since(t0) / reps;
    out["world_idle_recomputed"] = g_world_stats.recomputed;   // cumulative since load
    uint64_t rc0 = g_world_stats.recomputed;

    std::vector<Transform*> movers;
    {
        int k = 0;
        for (auto [e, t] : scene.registry.view<Transform>().each())
            if (k++ % 100 == 0) movers.push_back(&t);
    }
    t0 = Clock::now();
    for (int i = 0; i < reps; ++i) {
        for (auto* t : movers) t->position.y += 0.001f;
        update_world_transforms(scene);
        update_world_transforms(scene);
        update_world_transforms(scene);   // physics.sync + particles + renderer
    }
    out["world_1pct_moving_ms_per_frame_3calls"] = ms_since(t0) / reps;
    out["world_1pct_moving_recomputed_per_frame"] = double(g_world_stats.recomputed - rc0) / reps;

    // correctness: cached world matrices == from-scratch recursion
    {
        update_world_transforms(scene);
        int bad = 0;
        std::function<glm::mat4(entt::entity, int)> naive = [&](entt::entity e, int d) -> glm::mat4 {
            glm::mat4 l = scene.registry.get<Transform>(e).matrix();
            if (auto* h = scene.registry.try_get<Hierarchy>(e); h && !h->parent_name.empty() && d < 64) {
                entt::entity p = scene.find(h->parent_name);
                if (p != entt::null && p != e) return naive(p, d + 1) * l;
            }
            return l;
        };
        for (auto [e, t, wt] : scene.registry.view<Transform, WorldTransform>().each()) {
            glm::mat4 m = naive(e, 0);
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    if (std::abs(m[c][r] - wt.matrix[c][r]) > 1e-3f) { ++bad; c = r = 4; }
        }
        out["world_cache_mismatches"] = bad;
    }

    // --- spawn 200 entities into the big scene (each one used to re-resolve every mesh)
    const int spawns = 200;
    t0 = Clock::now();
    for (int i = 0; i < spawns; ++i)
        dispatch(ctx, {{"method", "entity.spawn"},
                       {"params", {{"name", "s" + std::to_string(i)}, {"primitive", "cube"},
                                   {"position", {float(i), 3.0f, 0.0f}}}}});
    out["spawn_total_ms"] = ms_since(t0);
    out["spawn_ms_each"] = out["spawn_total_ms"].get<double>() / spawns;

    // --- old-style undo bookkeeping: full-scene JSON string per check
    t0 = Clock::now();
    size_t bytes = 0;
    for (int i = 0; i < 20; ++i) bytes = scene.to_json().dump().size();
    out["snapshot_ms_each"] = ms_since(t0) / 20;
    out["snapshot_bytes"] = bytes;

    // --- delta history: one edit gesture on the big scene
    {
        SceneHistory h;
        t0 = Clock::now();
        h.reset(scene);
        out["history_baseline_ms"] = ms_since(t0);
        out["history_baseline_bytes"] = h.baseline_bytes();
        const int steps = 64;
        double commit_total = 0;
        for (int i = 0; i < steps; ++i) {
            auto e = scene.find("e" + std::to_string(i * 7));
            scene.registry.get<Transform>(e).position.x += 0.25f;
            auto tc = Clock::now();
            h.commit(scene);
            commit_total += ms_since(tc);
        }
        out["history_commit_ms_each"] = commit_total / steps;
        out["history_64_steps_bytes"] = h.step_bytes();
        out["history_64_steps_old_style_bytes"] = bytes * steps;
        auto e0 = scene.find("e0");
        float before = scene.registry.get<Transform>(scene.find("e7")).position.x;
        t0 = Clock::now();
        int undone = 0;
        while (h.undo(scene)) ++undone;
        out["history_undo_all_ms"] = ms_since(t0);
        out["history_undone_steps"] = undone;
        float after = scene.registry.get<Transform>(scene.find("e7")).position.x;
        out["history_undo_restored"] = (after < before);
        (void)e0;
        while (h.redo(scene)) {}
        out["history_redo_restored"] =
            std::abs(scene.registry.get<Transform>(scene.find("e7")).position.x - before) < 1e-4f;
    }

    std::printf("MICROBENCH %s\n", out.dump().c_str());
    std::fflush(stdout);
    return 0;
}

} // namespace eng
