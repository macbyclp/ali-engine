#include "aicontrol/channel.hpp"
#include "aicontrol/commands.hpp"
#include "anim/animation_system.hpp"
#include "audio/audio.hpp"
#include "behavior/behavior_system.hpp"
#include "fx/particles.hpp"
#include "core/log.hpp"
#include "core/window.hpp"
#include "nav/navgrid.hpp"
#include "physics/physics_system.hpp"
#include "render/framebuffer.hpp"
#include "render/renderer.hpp"
#include "scene/scene.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using nlohmann::json;

int main(int argc, char** argv) {
    bool headless = false;
    bool start_playing = false;
    int width = 1280, height = 720;
    std::string scene_path;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--headless") headless = true;
        else if (a == "--play") start_playing = true;
        else if (a == "--scene" && i + 1 < argc) scene_path = argv[++i];
        else if (a == "--width" && i + 1 < argc) width = std::stoi(argv[++i]);
        else if (a == "--height" && i + 1 < argc) height = std::stoi(argv[++i]);
    }

    eng::Window window(width, height, "ali-engine", headless);
    eng::Renderer renderer(width, height);
    eng::Framebuffer offscreen(width, height, eng::ColorFormat::RGBA8, false);
    eng::Scene scene;

    if (!scene_path.empty()) scene.load_file(scene_path);
    scene.camera();   // guarantee an active camera exists

    eng::PhysicsSystem physics;
    physics.sync(scene);
    eng::BehaviorSystem behaviors;
    eng::NavGrid nav;
    eng::AudioEngine audio;

    eng::ControlChannel channel;
    eng::CommandContext ctx{scene, renderer, offscreen, physics, behaviors, nav, audio, scene_path};
    ctx.sim_running = start_playing;

    eng::log::info("ready. headless=%d  scene=%s", headless,
                   scene_path.empty() ? "(none)" : scene_path.c_str());
    channel.respond(json{{"event", "ready"},
                         {"headless", headless},
                         {"scene", scene_path}});

    fs::file_time_type last_write{};
    auto scene_mtime = [&]() -> fs::file_time_type {
        std::error_code ec;
        return ctx.scene_path.empty() ? fs::file_time_type{}
                                      : fs::last_write_time(ctx.scene_path, ec);
    };
    last_write = scene_mtime();

    double prev_time = glfwGetTime();
    while (!ctx.quit && !window.should_close()) {
        window.poll();

        double now = glfwGetTime();
        float dt = float(now - prev_time);
        prev_time = now;
        if (dt > 0.1f) dt = 0.1f;

        // hot-reload the active scene file if it changed on disk
        if (!ctx.scene_path.empty()) {
            auto mt = scene_mtime();
            if (mt != last_write && mt != fs::file_time_type{}) {
                last_write = mt;
                if (scene.load_file(ctx.scene_path))
                    channel.respond(json{{"event", "scene.reloaded"},
                                         {"path", ctx.scene_path}});
            }
        }

        // drain AI commands
        json req;
        while (channel.poll(req)) {
            json res = eng::dispatch(ctx, req);
            channel.respond(res);
            if (ctx.quit) break;
            if (req.value("method", std::string()) == "scene.load")
                last_write = scene_mtime();
        }

        eng::update_animations(scene, dt);
        eng::update_particles(scene, dt);
        {
            eng::CameraComp& c = scene.camera();
            audio.set_listener(c.position, glm::normalize(c.target - c.position));
        }
        if (ctx.sim_running) {
            behaviors.tick(scene, physics, dt);
            physics.step(scene, dt);
            physics.step_characters(scene, dt);
        } else {
            physics.sync(scene);
        }

        if (!headless) {
            renderer.render(scene, 0, window.width(), window.height());
            window.swap();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    eng::log::info("shutting down");
    return 0;
}
