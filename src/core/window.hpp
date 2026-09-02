#pragma once
#include "render/gl.hpp"
#include <string>

namespace eng {

// Owns the GL 4.5 context. Can run hidden (headless) for AI-only / CI use:
// rendering and screenshots still work, there is just no visible window.
class Window {
public:
    Window(int width, int height, const std::string& title, bool headless);
    ~Window();

    bool should_close() const;
    void poll();
    void swap();

    int width() const { return w_; }
    int height() const { return h_; }
    float aspect() const { return h_ ? float(w_) / float(h_) : 1.0f; }
    GLFWwindow* handle() const { return win_; }

private:
    GLFWwindow* win_ = nullptr;
    int w_, h_;
    static void framebuffer_cb(GLFWwindow*, int, int);
};

} // namespace eng
