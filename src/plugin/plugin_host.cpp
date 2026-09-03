#include "plugin/plugin_host.hpp"
#include "aicontrol/commands.hpp"
#include "core/log.hpp"
#include <filesystem>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace eng {
namespace fs = std::filesystem;

static void* os_open(const char* path) {
#if defined(_WIN32)
    return (void*)LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}
static void* os_sym(void* lib, const char* name) {
#if defined(_WIN32)
    return (void*)GetProcAddress((HMODULE)lib, name);
#else
    return dlsym(lib, name);
#endif
}
static void os_close(void* lib) {
#if defined(_WIN32)
    if (lib) FreeLibrary((HMODULE)lib);
#else
    if (lib) dlclose(lib);
#endif
}

PluginHost::~PluginHost() { unload_all(nullptr); }

void PluginHost::unload_all(CommandContext* ctx) {
    for (auto it = plugins_.rbegin(); it != plugins_.rend(); ++it) {
        if (it->plugin && ctx) it->plugin->on_unload(*ctx);
        it->plugin.reset();
        os_close(it->lib);
    }
    plugins_.clear();
}

void PluginHost::add(std::unique_ptr<IPlugin> plugin, CommandContext& ctx) {
    if (!plugin) return;
    log::info("plugin: loaded '%s' v%s", plugin->name(), plugin->version());
    plugin->on_load(ctx);
    plugins_.push_back({std::move(plugin), nullptr});
}

std::string PluginHost::load_library(const std::string& path, CommandContext& ctx) {
    void* lib = os_open(path.c_str());
    if (!lib) { log::error("plugin: cannot open %s", path.c_str()); return {}; }
    auto create = reinterpret_cast<PluginCreateFn>(os_sym(lib, "eng_plugin_create"));
    if (!create) {
        log::error("plugin: %s has no eng_plugin_create", path.c_str());
        os_close(lib);
        return {};
    }
    IPlugin* raw = create();
    if (!raw) { os_close(lib); return {}; }
    std::string nm = raw->name();
    log::info("plugin: loaded '%s' v%s from %s", raw->name(), raw->version(), path.c_str());
    raw->on_load(ctx);
    plugins_.push_back({std::unique_ptr<IPlugin>(raw), lib});
    return nm;
}

int PluginHost::load_dir(const std::string& dir, CommandContext& ctx) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return 0;
#if defined(_WIN32)
    const char* ext = ".dll";
#elif defined(__APPLE__)
    const char* ext = ".dylib";
#else
    const char* ext = ".so";
#endif
    int n = 0;
    for (auto& de : fs::directory_iterator(dir, ec)) {
        if (de.path().extension() == ext && !load_library(de.path().string(), ctx).empty()) ++n;
    }
    return n;
}

void PluginHost::update(CommandContext& ctx, float dt) {
    for (auto& e : plugins_) if (e.plugin) e.plugin->on_update(ctx, dt);
}

std::optional<nlohmann::json> PluginHost::dispatch(CommandContext& ctx, const std::string& method,
                                                  const nlohmann::json& params) {
    for (auto& e : plugins_) {
        if (!e.plugin) continue;
        if (auto r = e.plugin->on_command(ctx, method, params)) return r;
    }
    return std::nullopt;
}

nlohmann::json PluginHost::list() const {
    nlohmann::json arr = nlohmann::json::array();
    for (auto& e : plugins_)
        if (e.plugin)
            arr.push_back({{"name", e.plugin->name()}, {"version", e.plugin->version()},
                           {"dll", e.lib != nullptr}});
    return arr;
}

} // namespace eng
