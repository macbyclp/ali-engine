#pragma once
#include "engine/gl.hpp"
#include <string>

namespace eng {

// Owns the GLFW window + GL context. Also the raw input source.
class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    bool should_close() const;
    void poll();
    void swap();

    int width() const { return w_; }
    int height() const { return h_; }
    float aspect() const { return h_ ? float(w_) / float(h_) : 1.0f; }

    // Input snapshot for the current frame.
    bool key(int glfw_key) const;
    bool key_pressed(int glfw_key) const;   // edge: down this frame, up last frame
    double mouse_dx() const { return mdx_; }
    double mouse_dy() const { return mdy_; }
    void set_mouse_captured(bool captured);

    GLFWwindow* handle() const { return win_; }

private:
    GLFWwindow* win_ = nullptr;
    int w_, h_;
    double mx_ = 0, my_ = 0, mdx_ = 0, mdy_ = 0;
    bool first_mouse_ = true;
    unsigned char prev_keys_[512] = {0};
    unsigned char curr_keys_[512] = {0};

    static void framebuffer_cb(GLFWwindow*, int, int);
};

} // namespace eng
