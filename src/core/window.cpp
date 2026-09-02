#include "core/window.hpp"
#include "core/log.hpp"
#include <cstdlib>

namespace eng {

static void gl_debug_cb(GLenum, GLenum, GLuint, GLenum severity, GLsizei,
                        const GLchar* msg, const void*) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    log::warn("GL: %s", msg);
}

void Window::framebuffer_cb(GLFWwindow* w, int width, int height) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
    self->w_ = width;
    self->h_ = height;
    glViewport(0, 0, width, height);
}

Window::Window(int width, int height, const std::string& title, bool headless)
    : w_(width), h_(height) {
    glfwSetErrorCallback([](int c, const char* d) { log::error("glfw %d: %s", c, d); });
    if (!glfwInit()) {
        log::error("glfwInit failed");
        std::exit(1);
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
    if (headless) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    win_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!win_) {
        log::error("glfwCreateWindow failed (need GL 4.5)");
        glfwTerminate();
        std::exit(1);
    }
    glfwSetWindowUserPointer(win_, this);
    glfwMakeContextCurrent(win_);
    glfwSetFramebufferSizeCallback(win_, framebuffer_cb);
    glfwSwapInterval(headless ? 0 : 1);

    if (!gladLoadGL(glfwGetProcAddress)) {
        log::error("glad: failed to load GL 4.6");
        std::exit(1);
    }

    int flags = 0;
    glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
    if (flags & GL_CONTEXT_FLAG_DEBUG_BIT) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(gl_debug_cb, nullptr);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    log::info("GL %s | %s", glGetString(GL_VERSION), glGetString(GL_RENDERER));
}

Window::~Window() {
    if (win_) glfwDestroyWindow(win_);
    glfwTerminate();
}

bool Window::should_close() const { return glfwWindowShouldClose(win_); }
void Window::poll() { glfwPollEvents(); }
void Window::swap() { glfwSwapBuffers(win_); }

} // namespace eng
