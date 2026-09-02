#pragma once
#include "render/framebuffer.hpp"
#include "render/renderer.hpp"
#include "scene/scene.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace eng {

struct CommandContext {
    Scene& scene;
    Renderer& renderer;
    Framebuffer& offscreen;
    std::string scene_path;   // currently loaded scene file (for hot-reload + default save)
    bool quit = false;
};

// Executes one request on the main thread, returns the response object.
// Supported methods:
//   ping
//   scene.load {path} | scene.save {path?} | scene.reset | scene.state
//   entity.list | entity.spawn {name?,primitive?,gltf_path?,position?,rotation?,scale?,base_color?}
//   entity.destroy {name} | entity.setTransform {name,position?,rotation?,scale?}
//   entity.setMaterial {name,base_color?,metallic?,roughness?}
//   light.set {name?,direction?,color?,intensity?}
//   camera.set {position?,target?,fov_deg?} | camera.get
//   observe.screenshot {path?,width?,height?}
//   quit
nlohmann::json dispatch(CommandContext& ctx, const nlohmann::json& request);

} // namespace eng
