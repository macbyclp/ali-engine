#include "aicontrol/channel.hpp"
#include "aicontrol/commands.hpp"
#include "core/log.hpp"
#include "core/window.hpp"
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
    int width = 1280, height = 720;
    std::string scene_path;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--headless") headless = true;
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

    eng::ControlChannel channel;
    eng::CommandContext ctx{scene, renderer, offscreen, scene_path};

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

    while (!ctx.quit && !window.should_close()) {
        window.poll();

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
