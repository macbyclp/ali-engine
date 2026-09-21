#include "core/window.hpp"
#include "core/log.hpp"
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#if defined(__linux__)
#include <dlfcn.h>
#endif

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


// ---------------------------------------------------------------------------
// Display-less headless backend (Linux): a surfaceless/pbuffer EGL desktop-GL context,
// so the engine runs with no X server / xvfb. libEGL is dlopen'ed and the few EGL
// declarations we need are repeated here, so the build has no EGL dependency. If
// anything is missing we return false and the caller falls back to a hidden GLFW
// window (which needs X -- run under xvfb-run there).
// ---------------------------------------------------------------------------
struct EglState {
    void* lib = nullptr;
    void* dpy = nullptr;
    void* ctx = nullptr;
    void* surf = nullptr;
    void* (*getProc)(const char*) = nullptr;
    unsigned (*makeCurrent)(void*, void*, void*, void*) = nullptr;
    unsigned (*destroyCtx)(void*, void*) = nullptr;
    unsigned (*destroySurf)(void*, void*) = nullptr;
    unsigned (*terminate)(void*) = nullptr;
};

#if defined(__linux__)
static bool egl_headless_init(EglState*& out, int& gl_ok) {
    using EGLint = int;
    using EGLBoolean = unsigned;
    gl_ok = 0;
    void* lib = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return false;
    auto sym = [&](const char* n) { return dlsym(lib, n); };
    auto getProc = (void* (*)(const char*))sym("eglGetProcAddress");
    auto initialize = (EGLBoolean(*)(void*, EGLint*, EGLint*))sym("eglInitialize");
    auto bindAPI = (EGLBoolean(*)(unsigned))sym("eglBindAPI");
    auto chooseConfig = (EGLBoolean(*)(void*, const EGLint*, void**, EGLint, EGLint*))sym("eglChooseConfig");
    auto createCtx = (void* (*)(void*, void*, void*, const EGLint*))sym("eglCreateContext");
    auto createPbuf = (void* (*)(void*, void*, const EGLint*))sym("eglCreatePbufferSurface");
    auto makeCurrent = (EGLBoolean(*)(void*, void*, void*, void*))sym("eglMakeCurrent");
    auto getDisplay = (void* (*)(void*))sym("eglGetDisplay");
    if (!getProc || !initialize || !bindAPI || !chooseConfig || !createCtx || !createPbuf ||
        !makeCurrent || !getDisplay) { dlclose(lib); return false; }

    auto queryDevices = (EGLBoolean(*)(EGLint, void**, EGLint*))getProc("eglQueryDevicesEXT");
    auto getPlatformDisplay = (void* (*)(unsigned, void*, const EGLint*))getProc("eglGetPlatformDisplayEXT");

    // candidate displays: every EGL device (GPU / llvmpipe), then the default display
    std::vector<void*> displays;
    if (queryDevices && getPlatformDisplay) {
        void* devs[16]; EGLint n = 0;
        if (queryDevices(16, devs, &n))
            for (int i = 0; i < n; ++i)
                if (void* d = getPlatformDisplay(0x313F /*EGL_PLATFORM_DEVICE_EXT*/, devs[i], nullptr))
                    displays.push_back(d);
    }
    if (void* d = getDisplay(nullptr)) displays.push_back(d);

    const EGLint cfg_attr[] = {0x3033 /*SURFACE_TYPE*/, 0x0001 /*PBUFFER*/,
                               0x3040 /*RENDERABLE_TYPE*/, 0x0008 /*OPENGL_BIT*/,
                               0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3025 /*DEPTH*/, 24, 0x3038};
    const EGLint ctx_attr[] = {0x3098, 4, 0x30FB, 5, 0x30FD /*PROFILE_MASK*/, 0x1 /*CORE*/, 0x3038};
    const EGLint pb_attr[] = {0x3057, 16, 0x3056, 16, 0x3038};

    for (void* dpy : displays) {
        EGLint maj = 0, min = 0;
        if (!initialize(dpy, &maj, &min)) continue;
        if (!bindAPI(0x30A2 /*EGL_OPENGL_API*/)) continue;
        void* cfg = nullptr; EGLint nc = 0;
        if (!chooseConfig(dpy, cfg_attr, &cfg, 1, &nc) || nc < 1) continue;
        void* ctx = createCtx(dpy, cfg, nullptr, ctx_attr);
        if (!ctx) continue;
        void* surf = createPbuf(dpy, cfg, pb_attr);
        if (!makeCurrent(dpy, surf, surf, ctx)) continue;
        auto* st = new EglState;
        st->lib = lib; st->dpy = dpy; st->ctx = ctx; st->surf = surf; st->getProc = getProc;
        st->makeCurrent = makeCurrent;
        st->destroyCtx = (EGLBoolean(*)(void*, void*))sym("eglDestroyContext");
        st->destroySurf = (EGLBoolean(*)(void*, void*))sym("eglDestroySurface");
        st->terminate = (EGLBoolean(*)(void*))sym("eglTerminate");
        out = st;
        gl_ok = 1;
        return true;
    }
    dlclose(lib);
    return false;
}
#endif

Window::Window(int width, int height, const std::string& title, bool headless)
    : w_(width), h_(height) {
#if defined(__linux__)
    // Headless + no display server (or ALI_HEADLESS=egl): try a real display-less context first.
    // ALI_HEADLESS=glfw forces the old hidden-GLFW-window path (needs X / xvfb).
    if (headless) {
        const char* mode = std::getenv("ALI_HEADLESS");
        bool have_display = std::getenv("DISPLAY") || std::getenv("WAYLAND_DISPLAY");
        bool want_egl = mode ? std::strcmp(mode, "egl") == 0 : !have_display;
        if (want_egl) {
            int ok = 0;
            if (egl_headless_init(egl_, ok) && ok) {
                // GLFW is still used for timing/input plumbing; the null platform needs no display.
                glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
                glfwSetErrorCallback([](int c, const char* d) { log::error("glfw %d: %s", c, d); });
                if (!glfwInit()) { log::error("glfwInit (null platform) failed"); std::exit(1); }
                auto getproc = egl_->getProc;
                if (!gladLoadGL((GLADloadfunc)getproc)) {
                    log::error("glad: failed to load GL via EGL");
                    std::exit(1);
                }
                glEnable(GL_DEPTH_TEST);
                glEnable(GL_CULL_FACE);
                log::info("EGL headless (no display) | GL %s | %s", glGetString(GL_VERSION),
                          glGetString(GL_RENDERER));
                return;
            }
            log::warn("EGL headless unavailable, falling back to a hidden GLFW window (needs X / xvfb)");
        }
    }
#endif
    glfwSetErrorCallback([](int c, const char* d) { log::error("glfw %d: %s", c, d); });
    if (!glfwInit()) {
        log::error("glfwInit failed (no display server and no usable EGL device -- run under xvfb-run, or install libEGL/Mesa)");
        std::exit(1);
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // The debug context makes the driver validate every call; opt in with ALI_GL_DEBUG=1.
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, std::getenv("ALI_GL_DEBUG") ? GLFW_TRUE : GLFW_FALSE);
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
    if (egl_) {
        egl_->makeCurrent(egl_->dpy, nullptr, nullptr, nullptr);
        if (egl_->destroySurf && egl_->surf) egl_->destroySurf(egl_->dpy, egl_->surf);
        if (egl_->destroyCtx) egl_->destroyCtx(egl_->dpy, egl_->ctx);
        if (egl_->terminate) egl_->terminate(egl_->dpy);
        delete egl_;   // libEGL stays loaded until exit on purpose
        egl_ = nullptr;
    }
    if (win_) glfwDestroyWindow(win_);
    glfwTerminate();
}

bool Window::should_close() const { return win_ ? glfwWindowShouldClose(win_) : false; }
void Window::poll() { glfwPollEvents(); }
void Window::swap() { if (win_) glfwSwapBuffers(win_); }
void Window::set_vsync(bool on) { if (win_) glfwSwapInterval(on ? 1 : 0); }

} // namespace eng
