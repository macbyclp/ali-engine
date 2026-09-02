#pragma once
#include "render/shader.hpp"
#include "scene/scene.hpp"

namespace eng {

// M1 renderer: single directional light, Lambert + ambient, flat base colour.
// M2 replaces the shader with metallic-roughness PBR + IBL + shadows.
class Renderer {
public:
    Renderer();
    void render(Scene& scene, int fb_width, int fb_height);

private:
    Shader lit_;
};

} // namespace eng
