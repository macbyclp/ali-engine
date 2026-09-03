#include "aicontrol/channel.hpp"
#include "aicontrol/commands.hpp"
#include "anim/animation_system.hpp"
#include "anim/animator.hpp"
#include "audio/audio.hpp"
#include "behavior/behavior_system.hpp"
#include "fx/particles.hpp"
#include "game/gamestate.hpp"
#include "input/input.hpp"
#include "core/log.hpp"
#include "core/window.hpp"
#include "editor/editor.hpp"
#include "nav/navgrid.hpp"
#include "physics/physics_system.hpp"
#include "plugin/plugin_host.hpp"
#include "plugin/example_spin.hpp"
#include "render/framebuffer.hpp"
#include "render/renderer.hpp"
#include "scene/scene.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using nlohmann::json;

int main(int argc, char** argv) {
    bool headless = false;
    bool editor_mode = false;
    bool start_playing = false;
    int width = 1280, height = 720;
    std::string scene_path;
    std::string shot_path;      // --shot <png>: grab the window then quit
    int shot_frame = 45;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--headless") headless = true;
        else if (a == "--editor") editor_mode = true;
        else if (a == "--play") start_playing = true;
        else if (a == "--scene" && i + 1 < argc) scene_path = argv[++i];
        else if (a == "--width" && i + 1 < argc) width = std::stoi(argv[++i]);
        else if (a == "--height" && i + 1 < argc) height = std::stoi(argv[++i]);
        else if (a == "--shot" && i + 1 < argc) shot_path = argv[++i];
        else if (a == "--shot-frame" && i + 1 < argc) shot_frame = std::stoi(argv[++i]);
    }

    if (editor_mode) headless = false;
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
    eng::GameState game;

    // Only the headless AI-driving mode should quit when stdin closes; a human
    // running a window (plain or --editor) doesn't pipe commands.
    eng::ControlChannel channel(headless);
    eng::CommandContext ctx{scene, renderer, offscreen, physics, behaviors, nav, audio, game, scene_path};
    ctx.sim_running = start_playing;

    eng::InputSystem input;
    input.attach(headless ? nullptr : window.handle());
    behaviors.set_input(&input);
    ctx.input = &input;
    auto apply_scene_input = [&] {
        for (auto& [action, keys] : scene.input_map.items())
            if (keys.is_array()) input.bind(action, keys.get<std::vector<std::string>>());
    };
    apply_scene_input();

    eng::PluginHost plugins;
    ctx.plugins = &plugins;
    plugins.add(std::make_unique<eng::SpinPlugin>(), ctx);
    plugins.load_dir("plugins", ctx);   // any *.dll / *.so next to the binary

    std::unique_ptr<eng::Editor> editor;
    if (editor_mode) editor = std::make_unique<eng::Editor>(window.handle());

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
    long frame_no = 0;
    while (!ctx.quit && !window.should_close()) {
        window.poll();
        ++frame_no;

        double now = glfwGetTime();
        float dt = float(now - prev_time);
        prev_time = now;
        if (dt > 0.1f) dt = 0.1f;

        // hot-reload the active scene file if it changed on disk
        if (!ctx.scene_path.empty()) {
            auto mt = scene_mtime();
            if (mt != last_write && mt != fs::file_time_type{}) {
                last_write = mt;
                if (scene.load_file(ctx.scene_path)) {
                    apply_scene_input();
                    channel.respond(json{{"event", "scene.reloaded"},
                                         {"path", ctx.scene_path}});
                }
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

        if (editor) editor->begin_frame();

        bool sim = editor ? editor->wants_play() : ctx.sim_running;
        input.update(dt);
        plugins.update(ctx, dt);
        eng::update_animators(scene, dt);
        eng::update_animations(scene, dt);
        eng::update_particles(scene, dt);
        {
            eng::CameraComp& c = scene.camera();
            audio.set_listener(c.position, glm::normalize(c.target - c.position));
        }
        if (sim) {
            behaviors.tick(scene, physics, game, dt);
            physics.step(scene, dt);
            physics.step_characters(scene, dt);
        } else {
            physics.sync(scene);
        }

        if (editor) {
            int W = window.width(), H = window.height();
            offscreen.resize(W, H);
            renderer.render(scene, offscreen.id(), W, H);   // scene at full window size
            editor->background(offscreen.color_texture(), W, H);   // blit + frosted blur
            editor->draw(ctx, offscreen.color_texture(), W, H);    // glass panels over it
            editor->end_frame();
            window.swap();
        } else if (!headless) {
            renderer.render(scene, 0, window.width(), window.height());
            window.swap();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        if (!shot_path.empty() && frame_no >= shot_frame && !headless) {
            if (eng::save_window_png(shot_path, window.width(), window.height()))
                eng::log::info("shot saved: %s", shot_path.c_str());
            else
                eng::log::error("shot failed: %s", shot_path.c_str());
            ctx.quit = true;
        }
    }

    eng::log::info("shutting down");
    return 0;
}
