#include "aicontrol/channel.hpp"
#include "aicontrol/commands.hpp"
#include "anim/animation_system.hpp"
#include "anim/animator.hpp"
#include "audio/audio.hpp"
#include "bench/microbench.hpp"
#include "behavior/behavior_system.hpp"
#include "fx/particles.hpp"
#include "game/gamestate.hpp"
#include "game/camera_rig.hpp"
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

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using nlohmann::json;

static int run_engine(int argc, char** argv) {
    bool headless = false;
    bool editor_mode = false;
    bool start_playing = false;
    int width = 1280, height = 720;
    std::string scene_path;
    std::string shot_path;      // --shot <png>: grab the window then quit
    int shot_frame = 45;
    bool allow_plugin_load = false;   // --allow-plugin-load: enable the plugin.load command
    std::string write_root;           // --write-root <dir>: widen where the protocol may write
    int microbench_n = 0;             // --microbench <n>: CPU scene timings on n entities, then quit
    int bench_frames = 0;       // --bench <n>: vsync off, time n frames, print stats, quit

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
        else if (a == "--microbench" && i + 1 < argc) microbench_n = std::stoi(argv[++i]);
        else if (a == "--bench" && i + 1 < argc) bench_frames = std::stoi(argv[++i]);
        else if (a == "--allow-plugin-load") allow_plugin_load = true;
        else if (a == "--write-root" && i + 1 < argc) write_root = argv[++i];
        else if (a == "--version" || a == "-V") {
            std::printf("ali-engine %s\n", ALI_VERSION);
            return 0;
        }
        else if (a == "--help" || a == "-h") {
            std::printf("usage: engine [--headless] [--editor] [--play] [--scene <json>] [--width N] [--height N]\n"
                        "              [--shot <png>] [--shot-frame N] [--bench N] [--microbench N]\n"
                        "              [--write-root <dir>] [--allow-plugin-load] [--version] [--help]\n"
                        "note: --shot needs a window (not --headless)\n"
                        "safety: the protocol only writes files under the startup working directory\n"
                        "        (or --write-root); plugin.load is off unless --allow-plugin-load\n");
            return 0;
        }
        else {
            std::fprintf(stderr, "engine: unknown or incomplete argument '%s' (see --help)\n", a.c_str());
            return 2;
        }
    }

    if (editor_mode) headless = false;
    eng::Window window(width, height, "ali-engine", headless);
    if (bench_frames > 0) window.set_vsync(false);
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
    ctx.allow_plugin_load = allow_plugin_load;
    {
        std::error_code ec;
        ctx.write_root = write_root.empty() ? fs::current_path(ec).string()
                                            : fs::absolute(write_root, ec).string();
    }

    eng::InputSystem input;
    input.attach(headless ? nullptr : window.handle());
    behaviors.set_input(&input);
    behaviors.set_audio(&audio);
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

    if (microbench_n > 0) {   // a normal return would wait on the stdin reader thread
        int rc = eng::run_microbench(microbench_n, ctx);
        std::fflush(stdout);
        std::_Exit(rc);
    }

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
    std::vector<double> bench_ms;
    double bench_cpu = 0;
    if (bench_frames > 0) { bench_ms.reserve(bench_frames); ctx.sim_running = true; }
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

        if (editor) { editor->begin_frame(); editor->sync_play_input(ctx); }

        bool sim = editor ? editor->wants_play() : ctx.sim_running;
        input.update(dt);
        plugins.update(ctx, dt);
        eng::update_animators(scene, dt);
        if (sim) eng::update_camera_rig(scene, dt);
        eng::update_animations(scene, dt);
        eng::update_particles(scene, dt);
        audio.update();
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

        // --shot reads the back buffer, so it has to happen before the swap
        const bool want_shot = !shot_path.empty() && frame_no >= shot_frame && !headless;
        auto grab_shot = [&]() {
            if (!want_shot) return;
            if (eng::save_window_png(shot_path, window.width(), window.height()))
                eng::log::info("shot saved: %s", shot_path.c_str());
            else
                eng::log::error("shot failed: %s", shot_path.c_str());
            ctx.quit = true;
        };

        if (editor) {
            int W = window.width(), H = window.height();
            offscreen.resize(W, H);
            renderer.render(scene, offscreen.id(), W, H);   // scene at full window size
            editor->background(offscreen.color_texture(), W, H);   // blit + frosted blur
            editor->draw(ctx, offscreen.color_texture(), W, H);    // glass panels over it
            editor->end_frame();
            if (std::string req = editor->take_shot_request(); !req.empty()) {
                std::error_code ec;
                if (fs::path(req).has_parent_path()) fs::create_directories(fs::path(req).parent_path(), ec);
                if (eng::save_window_png(req, window.width(), window.height()))
                    eng::log::info("shot saved: %s", req.c_str());
            }
            grab_shot();
            window.swap();
        } else if (!headless) {
            renderer.render(scene, 0, window.width(), window.height());
            grab_shot();
            window.swap();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        if (bench_frames > 0) {
            double t_sub = glfwGetTime();
            glFinish();
            double t = glfwGetTime();
            if (frame_no > 30) {   // skip warm-up
                bench_ms.push_back((t - now) * 1000.0);
                bench_cpu += (t_sub - now) * 1000.0;
            }
            if (frame_no >= bench_frames + 30) {
                std::sort(bench_ms.begin(), bench_ms.end());
                double sum = 0; for (double v : bench_ms) sum += v;
                size_t n = bench_ms.size();
                std::printf("BENCH frames=%zu avg=%.3fms fps=%.1f p50=%.3f p99=%.3f max=%.3f cpu_submit=%.3fms\n",
                            n, sum / n, 1000.0 * n / sum, bench_ms[n / 2],
                            bench_ms[size_t(n * 0.99)], bench_ms.back(), bench_cpu / n);
                std::fflush(stdout);
                ctx.quit = true;
            }
        }
    }

    eng::log::info("shutting down (frame %ld quit=%d close=%d)", frame_no, (int)ctx.quit, (int)window.should_close());
    return eng::g_exit_code;
}

// The stdin reader thread (detached, blocked in getline) holds the stdin FILE lock; a normal
// return from main() runs libc's exit-time stdio cleanup, which then waits for that lock
// forever whenever the peer keeps the pipe open. Everything owned by run_engine() is already
// destroyed at this point, so skip the libc teardown and leave directly.
int main(int argc, char** argv) {
    int rc = run_engine(argc, argv);
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(rc);
}
