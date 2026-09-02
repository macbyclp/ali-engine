#include "engine/app.hpp"

namespace eng {

App::App(int width, int height, const char* title) : win_(width, height, title) {}

void App::frame() {
    double now = glfwGetTime();
    float frame_dt = static_cast<float>(now - prev_time_);
    prev_time_ = now;
    if (frame_dt > 0.25f) frame_dt = 0.25f;   // clamp after a stall
    accumulator_ += frame_dt;

    win_.poll();

    while (accumulator_ >= kFixedStep) {
        scene_->update(win_, kFixedStep);
        accumulator_ -= kFixedStep;
    }

    scene_->render(win_);
    win_.swap();
}

void App::web_tick(void* self) { static_cast<App*>(self)->frame(); }

void App::run(std::unique_ptr<Scene> scene) {
    scene_ = std::move(scene);
    scene_->init(win_);
    prev_time_ = glfwGetTime();

#ifdef ENGINE_WEB
    emscripten_set_main_loop_arg(&App::web_tick, this, 0, 1);
#else
    while (!win_.should_close()) frame();
#endif
}

} // namespace eng
