#pragma once
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace eng {

struct CommandContext;

// A plugin extends the engine at runtime: it can answer new JSON commands, run
// per-frame logic, and react to load/unload. Compile one into the engine and
// register it via PluginHost::add(), or ship a DLL that exports
//     extern "C" eng::IPlugin* eng_plugin_create();
// and drop it in the plugins/ folder.
class IPlugin {
public:
    virtual ~IPlugin() = default;

    virtual const char* name() const = 0;
    virtual const char* version() const { return "0"; }

    virtual void on_load(CommandContext&) {}
    virtual void on_unload(CommandContext&) {}
    virtual void on_update(CommandContext&, float /*dt*/) {}

    // Return a JSON response object to claim `method`; return std::nullopt to
    // let the engine (or the next plugin) handle it.
    virtual std::optional<nlohmann::json> on_command(
        CommandContext&, const std::string& /*method*/, const nlohmann::json& /*params*/) {
        return std::nullopt;
    }
};

using PluginCreateFn = IPlugin* (*)();

} // namespace eng
