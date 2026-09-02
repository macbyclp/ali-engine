#pragma once
#include "engine/window.hpp"
#include <memory>

namespace eng {

class Window;

// A game/scene the engine drives. Fixed-timestep update, variable-rate render.
class Scene {
public:
    virtual ~Scene() = default;
    virtual void init(Window& win) = 0;
    virtual void update(Window& win, float dt) = 0;   // dt is the fixed step
    virtual void render(Window& win) = 0;
};

class App {
public:
    App(int width, int height, const char* title);
    void run(std::unique_ptr<Scene> scene);

    static constexpr float kFixedStep = 1.0f / 60.0f;

private:
    Window win_;
    std::unique_ptr<Scene> scene_;
    double prev_time_ = 0.0;
    float accumulator_ = 0.0f;

    void frame();          // one iteration of the loop
    static void web_tick(void* self);
};

} // namespace eng
