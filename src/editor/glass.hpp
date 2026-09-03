#pragma once
#include "render/framebuffer.hpp"
#include "render/shader.hpp"
#include <memory>

struct ImDrawList;

namespace eng {

// Produces the frosted backdrop for the liquid-glass editor: downsamples the
// rendered scene and gaussian-blurs it. Editor panels then draw this blurred
// texture behind themselves (via ImGui's background draw list), clipped to each
// panel's screen rect, for a real backdrop-blur look.
class GlassLayer {
public:
    GlassLayer();
    ~GlassLayer();
    GlassLayer(const GlassLayer&) = delete;
    GlassLayer& operator=(const GlassLayer&) = delete;

    // Blit `scene_tex` (full-window scene colour) to the default framebuffer,
    // then build a blurred copy. Returns the blurred texture id (covers the
    // whole window; sample it with screen-space UVs).
    unsigned frame(unsigned scene_tex, int w, int h);

private:
    Shader blit_, blur_;
    std::unique_ptr<Framebuffer> a_, b_;
    unsigned vao_ = 0;
    int bw_ = 0, bh_ = 0;
    void ensure(int w, int h);
};

// Draws one liquid-glass card into `dl`: soft shadow, blurred backdrop (clipped
// to the rect via UVs into `blur_tex`), glass tint, top specular sheen, hairline.
void draw_glass_card(ImDrawList* dl, unsigned blur_tex, int screen_w, int screen_h,
                     float x0, float y0, float x1, float y1, float rounding,
                     unsigned accent = 0);

} // namespace eng
