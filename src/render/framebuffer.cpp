#include "render/framebuffer.hpp"
#include "core/log.hpp"
#include <cmath>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace eng {

Framebuffer::Framebuffer(int w, int h, ColorFormat color, bool depth_sampled)
    : w_(w), h_(h), fmt_(color), depth_sampled_(depth_sampled) {
    create();
}
Framebuffer::~Framebuffer() { destroy(); }

void Framebuffer::create() {
    glCreateFramebuffers(1, &fbo_);

    if (fmt_ != ColorFormat::None) {
        GLenum internal = (fmt_ == ColorFormat::RGBA16F) ? GL_RGBA16F : GL_RGBA8;
        glCreateTextures(GL_TEXTURE_2D, 1, &color_);
        glTextureStorage2D(color_, 1, internal, w_, h_);
        glTextureParameteri(color_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(color_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(color_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(color_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glNamedFramebufferTexture(fbo_, GL_COLOR_ATTACHMENT0, color_, 0);
    } else {
        glNamedFramebufferDrawBuffer(fbo_, GL_NONE);
        glNamedFramebufferReadBuffer(fbo_, GL_NONE);
    }

    if (depth_sampled_) {
        glCreateTextures(GL_TEXTURE_2D, 1, &depth_tex_);
        glTextureStorage2D(depth_tex_, 1, GL_DEPTH_COMPONENT32F, w_, h_);
        glTextureParameteri(depth_tex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(depth_tex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(depth_tex_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTextureParameteri(depth_tex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float border[4] = {1, 1, 1, 1};
        glTextureParameterfv(depth_tex_, GL_TEXTURE_BORDER_COLOR, border);
        glTextureParameteri(depth_tex_, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTextureParameteri(depth_tex_, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        glNamedFramebufferTexture(fbo_, GL_DEPTH_ATTACHMENT, depth_tex_, 0);
    } else {
        glCreateRenderbuffers(1, &depth_rb_);
        glNamedRenderbufferStorage(depth_rb_, GL_DEPTH24_STENCIL8, w_, h_);
        glNamedFramebufferRenderbuffer(fbo_, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth_rb_);
    }

    GLenum st = glCheckNamedFramebufferStatus(fbo_, GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) log::error("framebuffer incomplete: 0x%x", st);
}

void Framebuffer::destroy() {
    if (color_) glDeleteTextures(1, &color_);
    if (depth_tex_) glDeleteTextures(1, &depth_tex_);
    if (depth_rb_) glDeleteRenderbuffers(1, &depth_rb_);
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    fbo_ = color_ = depth_tex_ = depth_rb_ = 0;
}

void Framebuffer::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, w_, h_);
}
void Framebuffer::bind_default(int w, int h) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
}

void Framebuffer::resize(int w, int h) {
    if (w == w_ && h == h_) return;
    destroy();
    w_ = w; h_ = h;
    create();
}

bool Framebuffer::save_png(const std::string& path) const {
    if (!color_) { log::error("save_png on colourless framebuffer"); return false; }
    std::vector<unsigned char> px(size_t(w_) * h_ * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTextureImage(color_, 0, GL_RGBA, GL_UNSIGNED_BYTE, (int)px.size(), px.data());
    stbi_flip_vertically_on_write(1);
    int ok = stbi_write_png(path.c_str(), w_, h_, 4, px.data(), w_ * 4);
    if (!ok) log::error("stbi_write_png failed: %s", path.c_str());
    return ok != 0;
}

bool save_window_png(const std::string& path, int w, int h) {
    std::vector<unsigned char> px(size_t(w) * h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_FRONT);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    stbi_flip_vertically_on_write(1);
    int ok = stbi_write_png(path.c_str(), w, h, 4, px.data(), w * 4);
    if (!ok) log::error("stbi_write_png failed: %s", path.c_str());
    return ok != 0;
}

} // namespace eng
