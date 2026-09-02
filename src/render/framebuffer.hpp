#pragma once
#include "render/gl.hpp"
#include <string>

namespace eng {

enum class ColorFormat { None, RGBA8, RGBA16F };

// Flexible offscreen target: optional colour attachment, optional depth.
// Depth can be a sampled texture (for shadow maps) or a renderbuffer.
class Framebuffer {
public:
    Framebuffer(int width, int height, ColorFormat color = ColorFormat::RGBA8,
                bool depth_sampled = false);
    ~Framebuffer();
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    void bind() const;                 // binds + sets viewport to full size
    static void bind_default(int w, int h);
    void resize(int width, int height);

    unsigned id() const { return fbo_; }
    int width() const { return w_; }
    int height() const { return h_; }
    unsigned color_texture() const { return color_; }
    unsigned depth_texture() const { return depth_tex_; }

    // Read colour attachment and write a PNG (flipped upright). Returns success.
    bool save_png(const std::string& path) const;

private:
    unsigned fbo_ = 0, color_ = 0, depth_tex_ = 0, depth_rb_ = 0;
    int w_ = 0, h_ = 0;
    ColorFormat fmt_;
    bool depth_sampled_;
    void create();
    void destroy();
};

} // namespace eng
