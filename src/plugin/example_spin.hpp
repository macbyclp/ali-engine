#pragma once
#include "aicontrol/commands.hpp"
#include "plugin/plugin.hpp"
#include "scene/scene.hpp"
#include <glm/glm.hpp>
#include <unordered_map>

namespace eng {

// Reference plugin: adds `spin.set {name, axis?, speed}` and `spin.clear {name}`,
// and spins the tagged entities every frame. Shows the three extension points
// (on_command + on_update + state). Compiled into the engine; a real plugin
// would live in its own DLL exporting eng_plugin_create().
class SpinPlugin final : public IPlugin {
public:
    const char* name() const override { return "spin"; }
    const char* version() const override { return "1.0"; }

    std::optional<nlohmann::json> on_command(CommandContext& ctx, const std::string& m,
                                             const nlohmann::json& p) override {
        if (m == "spin.set") {
            std::string n = p.at("name").get<std::string>();
            if (ctx.scene.find(n) == entt::null)
                return nlohmann::json{{"ok", false}, {"error", "no such entity"}};
            Spin s;
            if (p.contains("axis") && p["axis"].is_array() && p["axis"].size() == 3)
                s.axis = glm::normalize(glm::vec3(p["axis"][0].get<float>(),
                                                  p["axis"][1].get<float>(),
                                                  p["axis"][2].get<float>()));
            s.deg_per_sec = p.value("speed", 90.0f);
            spins_[n] = s;
            return nlohmann::json{{"ok", true}, {"result", {{"spinning", spins_.size()}}}};
        }
        if (m == "spin.clear") {
            spins_.erase(p.value("name", std::string()));
            return nlohmann::json{{"ok", true}};
        }
        return std::nullopt;
    }

    void on_update(CommandContext& ctx, float dt) override {
        for (auto it = spins_.begin(); it != spins_.end();) {
            entt::entity e = ctx.scene.find(it->first);
            auto* t = e != entt::null ? ctx.scene.registry.try_get<Transform>(e) : nullptr;
            if (!t) { it = spins_.erase(it); continue; }
            t->euler_deg += it->second.axis * it->second.deg_per_sec * dt;
            ++it;
        }
    }

private:
    struct Spin { glm::vec3 axis{0, 1, 0}; float deg_per_sec = 90.0f; };
    std::unordered_map<std::string, Spin> spins_;
};

} // namespace eng
