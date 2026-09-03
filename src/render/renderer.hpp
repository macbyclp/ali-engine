#pragma once
#include "core/jobs.hpp"
#include "render/framebuffer.hpp"
#include "render/shader.hpp"
#include "scene/scene.hpp"
#include "ui/font.hpp"
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
    static constexpr int kCascades = 3;
    static constexpr int kSpotShadows = 4;   // 2x2 atlas tiles for spot lights

    Shader pbr_, sky_, shadow_, tonemap_, bright_, blur_, particle_;
    Shader ssao_, ssao_blur_;
    Shader ui_solid_, ui_text_;
    std::unique_ptr<Framebuffer> hdr_, bloom_a_, bloom_b_;
    unsigned csm_fbo_ = 0, csm_tex_ = 0;
    int csm_size_ = 2048;
    unsigned spot_fbo_ = 0, spot_atlas_ = 0;
    int spot_atlas_size_ = 2048;

    // SSAO: full-res depth prepass -> half-res AO -> blur
    unsigned depth_fbo_ = 0, depth_tex_ = 0;
    unsigned ao_fbo_ = 0, ao_tex_ = 0, ao_blur_fbo_ = 0, ao_blur_tex_ = 0;
    unsigned ao_noise_ = 0;
    int ao_w_ = 0, ao_h_ = 0, depth_w_ = 0, depth_h_ = 0;
    glm::vec3 ao_kernel_[24];
    void ensure_ssao(int w, int h);
    std::unique_ptr<Font> font_;
    unsigned empty_vao_ = 0;
    unsigned draw_vao_ = 0;
    unsigned instance_vbo_ = 0;
    size_t instance_capacity_ = 0;
    unsigned particle_vao_ = 0, particle_vbo_ = 0;
    size_t particle_capacity_ = 0;
    unsigned ui_vao_ = 0, ui_vbo_ = 0;
    size_t ui_capacity_ = 0;
    JobSystem jobs_;
    RenderStats stats_;

    void ensure_hdr(int w, int h);
    void ensure_instances(size_t bytes);
    void bloom_pass(int w, int h);
    void ui_pass(Scene& scene, int w, int h);
};

} // namespace eng
