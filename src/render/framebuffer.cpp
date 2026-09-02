#include "render/framebuffer.hpp"
#include "core/log.hpp"
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace eng {

Framebuffer::Framebuffer(int w, int h) : w_(w), h_(h) { create(); }
Framebuffer::~Framebuffer() { destroy(); }

void Framebuffer::create() {
    glCreateFramebuffers(1, &fbo_);

    glCreateTextures(GL_TEXTURE_2D, 1, &color_);
    glTextureStorage2D(color_, 1, GL_RGBA8, w_, h_);
    glTextureParameteri(color_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(color_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glCreateRenderbuffers(1, &depth_);
    glNamedRenderbufferStorage(depth_, GL_DEPTH24_STENCIL8, w_, h_);

    glNamedFramebufferTexture(fbo_, GL_COLOR_ATTACHMENT0, color_, 0);
    glNamedFramebufferRenderbuffer(fbo_, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth_);

    if (glCheckNamedFramebufferStatus(fbo_, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        log::error("framebuffer incomplete");
}

void Framebuffer::destroy() {
    if (color_) glDeleteTextures(1, &color_);
    if (depth_) glDeleteRenderbuffers(1, &depth_);
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    fbo_ = color_ = depth_ = 0;
}

void Framebuffer::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, w_, h_);
}
void Framebuffer::unbind() { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

void Framebuffer::resize(int w, int h) {
    if (w == w_ && h == h_) return;
    destroy();
    w_ = w; h_ = h;
    create();
}

bool Framebuffer::save_png(const std::string& path) const {
    std::vector<unsigned char> px(size_t(w_) * h_ * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTextureImage(color_, 0, GL_RGBA, GL_UNSIGNED_BYTE, (int)px.size(), px.data());
    stbi_flip_vertically_on_write(1);
    int ok = stbi_write_png(path.c_str(), w_, h_, 4, px.data(), w_ * 4);
    if (!ok) log::error("stbi_write_png failed: %s", path.c_str());
    return ok != 0;
}

} // namespace eng
