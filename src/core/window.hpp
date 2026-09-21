#pragma once
#include "render/gl.hpp"
#include <string>

namespace eng {

struct EglState;   // window.cpp

// Owns the GL 4.5 context. Can run hidden (headless) for AI-only / CI use:
// rendering and screenshots still work, there is just no visible window.
class Window {
public:
    Window(int width, int height, const std::string& title, bool headless);
    ~Window();

    bool should_close() const;
    void poll();
    void swap();
    void set_vsync(bool on);

    int width() const { return w_; }
    int height() const { return h_; }
    float aspect() const { return h_ ? float(w_) / float(h_) : 1.0f; }
    GLFWwindow* handle() const { return win_; }
    // true when running on a display-less EGL context (no GLFW window at all)
    bool egl_headless() const { return egl_ != nullptr; }

private:
    GLFWwindow* win_ = nullptr;
    EglState* egl_ = nullptr;   // dlopen'ed libEGL + display/context (window.cpp)
    int w_, h_;
    static void framebuffer_cb(GLFWwindow*, int, int);
};

} // namespace eng
