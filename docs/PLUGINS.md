# Plugins

A plugin extends the engine at runtime. It can:

- **answer new JSON commands** (`on_command`) — grow the AI protocol
- **run per-frame logic** (`on_update`) — new systems
- **react to load/unload** (`on_load` / `on_unload`)

The engine looks for shared libraries in a `plugins/` folder next to the binary
at startup, and `plugin.load {path}` loads one on demand. `plugin.list` reports
what is loaded.

## The interface

```cpp
#include "plugin/plugin.hpp"   // from the ali-engine source tree

class MyPlugin final : public eng::IPlugin {
public:
    const char* name()    const override { return "my-plugin"; }
    const char* version() const override { return "1.0"; }

    std::optional<nlohmann::json> on_command(
        eng::CommandContext& ctx, const std::string& method,
        const nlohmann::json& params) override
    {
        if (method != "my.hello") return std::nullopt;   // not ours
        return nlohmann::json{{"ok", true}, {"result", {{"msg", "hi"}}}};
    }

    void on_update(eng::CommandContext& ctx, float dt) override { /* ... */ }
};

extern "C" eng::IPlugin* eng_plugin_create() { return new MyPlugin(); }
```

`CommandContext` gives you `scene`, `renderer`, `physics`, `behaviors`, `nav`,
`audio`, `game` — the same handles the core commands use.

A returned object is sent back verbatim; `id` and `ok` are filled in if missing.
Return `std::nullopt` to let the engine (or the next plugin) try the method.

## Building a plugin DLL

```cmake
add_library(my_plugin SHARED my_plugin.cpp)
target_include_directories(my_plugin PRIVATE
    ${ALI_ENGINE_SRC}                       # for plugin/plugin.hpp, aicontrol/commands.hpp ...
    ${nlohmann_json_SOURCE_DIR}/include
    ${entt_SOURCE_DIR}/src ${glm_SOURCE_DIR})
set_target_properties(my_plugin PROPERTIES PREFIX "" SUFFIX ".dll")
```

Drop `my_plugin.dll` into `plugins/` next to `engine.exe`. Build it with the
same compiler/runtime as the engine.

## Reference plugin

`src/plugin/example_spin.hpp` (`SpinPlugin`) is compiled into the engine and
registered at startup. It adds:

| Method | params | effect |
| --- | --- | --- |
| `spin.set` | `{name, axis?, speed}` | spin `name` about `axis` at `speed` °/s |
| `spin.clear` | `{name}` | stop spinning it |

It shows all three extension points plus per-plugin state.
