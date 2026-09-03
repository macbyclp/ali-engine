#pragma once
#include "plugin/plugin.hpp"
#include <memory>
#include <string>
#include <vector>

namespace eng {

// Owns the loaded plugins. dispatch() is consulted for methods the core engine
// does not recognise; update() ticks every plugin each frame.
class PluginHost {
public:
    PluginHost() = default;
    ~PluginHost();
    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    // Take ownership of an in-process plugin and call on_load.
    void add(std::unique_ptr<IPlugin> plugin, CommandContext& ctx);

    // Load one plugin DLL/.so. Returns the plugin name, or "" on failure.
    std::string load_library(const std::string& path, CommandContext& ctx);
    // Load every shared library in `dir`.
    int load_dir(const std::string& dir, CommandContext& ctx);

    void update(CommandContext& ctx, float dt);
    std::optional<nlohmann::json> dispatch(CommandContext& ctx, const std::string& method,
                                           const nlohmann::json& params);
    nlohmann::json list() const;

private:
    struct Entry {
        std::unique_ptr<IPlugin> plugin;
        void* lib = nullptr;   // OS handle when loaded from a shared library
    };
    std::vector<Entry> plugins_;
    void unload_all(CommandContext* ctx);
};

} // namespace eng
