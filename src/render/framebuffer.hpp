#pragma once
#include "render/gl.hpp"
#include <string>

namespace eng {

// Offscreen colour+depth target. Used for the AI "eye": render here, read pixels, write PNG.
class Framebuffer {
public:
    Framebuffer(int width, int height);
    ~Framebuffer();
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    void bind() const;
    static void unbind();
    void resize(int width, int height);

    int width() const { return w_; }
    int height() const { return h_; }
    unsigned color_texture() const { return color_; }

    // Reads the colour attachment and writes a PNG (flipped upright). Returns success.
    bool save_png(const std::string& path) const;

private:
    unsigned fbo_ = 0, color_ = 0, depth_ = 0;
    int w_ = 0, h_ = 0;
    void create();
    void destroy();
};

} // namespace eng
