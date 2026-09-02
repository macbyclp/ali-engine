#pragma once
#include "core/jobs.hpp"
#include "render/framebuffer.hpp"
#include "render/shader.hpp"
#include "scene/scene.hpp"
#include <memory>

namespace eng {

struct RenderStats {
    int entities = 0;
    int visible = 0;
    int culled = 0;
    int draw_calls = 0;
    int instances = 0;
    int groups = 0;
    float cpu_ms = 0.0f;
};

// M2 shading + M5 scale: frustum culling (job-parallel) and GPU instancing by mesh.
class Renderer {
public:
    Renderer(int width, int height);
    ~Renderer();
    void render(Scene& scene, unsigned target_fbo, int width, int height);
    const RenderStats& stats() const { return stats_; }

private:
    Shader pbr_, sky_, shadow_, tonemap_, bright_, blur_, particle_;
    std::unique_ptr<Framebuffer> hdr_, bloom_a_, bloom_b_;
    Framebuffer shadow_map_;
    unsigned empty_vao_ = 0;
    unsigned draw_vao_ = 0;
    unsigned instance_vbo_ = 0;
    size_t instance_capacity_ = 0;
    unsigned particle_vao_ = 0, particle_vbo_ = 0;
    size_t particle_capacity_ = 0;
    JobSystem jobs_;
    RenderStats stats_;

    void ensure_hdr(int w, int h);
    void ensure_instances(size_t bytes);
    void bloom_pass(int w, int h);
};

} // namespace eng
