#include "engine/window.hpp"
#include <cstdio>
#include <cstdlib>

namespace eng {

void Window::framebuffer_cb(GLFWwindow* w, int width, int height) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
    self->w_ = width;
    self->h_ = height;
    glViewport(0, 0, width, height);
}

Window::Window(int width, int height, const std::string& title) : w_(width), h_(height) {
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        std::exit(1);
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#ifndef ENGINE_WEB
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    #ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    #endif
#endif

    win_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!win_) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        std::exit(1);
    }
    glfwSetWindowUserPointer(win_, this);
    glfwMakeContextCurrent(win_);
    glfwSetFramebufferSizeCallback(win_, framebuffer_cb);
    glfwSwapInterval(1);

#ifndef ENGINE_WEB
    if (!gladLoadGL(glfwGetProcAddress)) {
        std::fprintf(stderr, "glad: failed to load GL\n");
        std::exit(1);
    }
#endif

    glEnable(GL_DEPTH_TEST);
}

Window::~Window() {
    if (win_) glfwDestroyWindow(win_);
    glfwTerminate();
}

bool Window::should_close() const { return glfwWindowShouldClose(win_); }

void Window::poll() {
    for (int i = 0; i < 512; ++i) prev_keys_[i] = curr_keys_[i];
    glfwPollEvents();
    for (int i = 0; i < 512; ++i)
        curr_keys_[i] = (glfwGetKey(win_, i) == GLFW_PRESS) ? 1 : 0;

    double x, y;
    glfwGetCursorPos(win_, &x, &y);
    if (first_mouse_) {
        mx_ = x; my_ = y;
        first_mouse_ = false;
    }
    mdx_ = x - mx_;
    mdy_ = y - my_;   // screen y grows downward
    mx_ = x;
    my_ = y;
}

void Window::swap() { glfwSwapBuffers(win_); }

bool Window::key(int k) const {
    return (k >= 0 && k < 512) ? curr_keys_[k] : false;
}
bool Window::key_pressed(int k) const {
    return (k >= 0 && k < 512) ? (curr_keys_[k] && !prev_keys_[k]) : false;
}

void Window::set_mouse_captured(bool captured) {
    glfwSetInputMode(win_, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    first_mouse_ = true;
}

} // namespace eng
