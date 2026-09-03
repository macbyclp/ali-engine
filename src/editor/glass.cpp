#include "editor/glass.hpp"
#include "render/gl.hpp"
#include <imgui.h>
#include <algorithm>

namespace eng {

static const char* kFsVert = R"(
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
static const char* kBlitFrag = R"(
in vec2 vUV;
uniform sampler2D uTex;
out vec4 F;
void main() { F = vec4(texture(uTex, vUV).rgb, 1.0); }
)";
static const char* kBlurFrag = R"(
in vec2 vUV;
uniform sampler2D uTex;
uniform vec2 uDir;
out vec4 F;
void main() {
    float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 c = texture(uTex, vUV).rgb * w[0];
    for (int i = 1; i < 5; ++i) {
        c += texture(uTex, vUV + uDir * float(i)).rgb * w[i];
        c += texture(uTex, vUV - uDir * float(i)).rgb * w[i];
    }
    F = vec4(c, 1.0);
}
)";

GlassLayer::GlassLayer() : blit_(kFsVert, kBlitFrag), blur_(kFsVert, kBlurFrag) {
    glCreateVertexArrays(1, &vao_);
}
GlassLayer::~GlassLayer() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

void GlassLayer::ensure(int w, int h) {
    int nw = std::max(1, w / 3), nh = std::max(1, h / 3);
    if (nw == bw_ && nh == bh_ && a_) return;
    bw_ = nw; bh_ = nh;
    a_ = std::make_unique<Framebuffer>(bw_, bh_, ColorFormat::RGBA8, false);
    b_ = std::make_unique<Framebuffer>(bw_, bh_, ColorFormat::RGBA8, false);
}

unsigned GlassLayer::frame(unsigned scene_tex, int w, int h) {
    ensure(w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBindVertexArray(vao_);

    // 1. scene -> default framebuffer, full window
    Framebuffer::bind_default(w, h);
    blit_.use();
    glBindTextureUnit(0, scene_tex);
    blit_.set("uTex", 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // 2. downsample scene into a_
    a_->bind();
    blit_.use();
    glBindTextureUnit(0, scene_tex);
    blit_.set("uTex", 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // 3. separable gaussian, a few iterations, ping-pong a_ <-> b_
    blur_.use();
    blur_.set("uTex", 0);
    for (int i = 0; i < 5; ++i) {
        b_->bind();
        glBindTextureUnit(0, a_->color_texture());
        blur_.set("uDir", glm::vec2(2.2f / bw_, 0.0f));
        glDrawArrays(GL_TRIANGLES, 0, 3);
        a_->bind();
        glBindTextureUnit(0, b_->color_texture());
        blur_.set("uDir", glm::vec2(0.0f, 2.2f / bh_));
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    Framebuffer::bind_default(w, h);
    return a_->color_texture();
}

// ---------------------------------------------------------------------------
void draw_glass_card(ImDrawList* dl, unsigned blur_tex, int sw, int sh,
                     float x0, float y0, float x1, float y1, float r, unsigned accent) {
    (void)accent;
    ImVec2 a(x0, y0), b(x1, y1);

    // soft drop shadow (a few stacked translucent rounded rects)
    for (int i = 6; i >= 1; --i) {
        float e = i * 2.0f;
        int alpha = 8 + i * 3;
        dl->AddRectFilled(ImVec2(a.x - e, a.y - e + 4), ImVec2(b.x + e, b.y + e + 6),
                          IM_COL32(0, 0, 0, alpha), r + e, 0);
    }

    // frosted backdrop: blurred scene, clipped to the card rect via UVs
    ImVec2 uv0(a.x / sw, a.y / sh), uv1(b.x / sw, b.y / sh);
    uv0.y = 1.0f - uv0.y; uv1.y = 1.0f - uv1.y;   // scene texture is bottom-up
    dl->AddImageRounded((ImTextureID)(intptr_t)blur_tex, a, b, uv0, uv1,
                        IM_COL32(255, 255, 255, 255), r);

    // glass tint + slight vertical gradient (lighter at top)
    dl->AddRectFilledMultiColor(a, b,
        IM_COL32(30, 33, 42, 120), IM_COL32(30, 33, 42, 120),
        IM_COL32(18, 19, 26, 140), IM_COL32(18, 19, 26, 140));

    // top specular sheen
    float sheen = std::min(26.0f, (b.y - a.y) * 0.4f);
    dl->AddRectFilledMultiColor(ImVec2(a.x, a.y), ImVec2(b.x, a.y + sheen),
        IM_COL32(255, 255, 255, 34), IM_COL32(255, 255, 255, 34),
        IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));

    // hairline: bright top-left, dim bottom-right
    dl->AddRect(a, b, IM_COL32(255, 255, 255, 40), r, ImDrawFlags_RoundCornersAll, 1.2f);
    dl->AddLine(ImVec2(a.x + r * 0.5f, a.y + 0.6f), ImVec2(b.x - r * 0.5f, a.y + 0.6f),
                IM_COL32(255, 255, 255, 70), 1.0f);
}

} // namespace eng
