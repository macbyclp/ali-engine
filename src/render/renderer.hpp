#pragma once
#include "render/framebuffer.hpp"
#include "render/shader.hpp"
#include "scene/scene.hpp"
#include <memory>

namespace eng {

// M2 renderer: metallic-roughness PBR (Cook-Torrance), procedural-sky IBL
// approximation, directional shadow map with PCF, HDR + ACES tonemap.
class Renderer {
public:
    Renderer(int width, int height);
    void render(Scene& scene, unsigned target_fbo, int width, int height);

private:
    Shader pbr_, sky_, shadow_, tonemap_;
    std::unique_ptr<Framebuffer> hdr_;
    Framebuffer shadow_map_;
    unsigned empty_vao_ = 0;

    void ensure_hdr(int w, int h);
};

} // namespace eng
