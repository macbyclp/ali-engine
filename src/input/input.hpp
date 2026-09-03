#pragma once
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

struct GLFWwindow;

namespace eng {

// Named-action input. Bindings map an action ("jump") to physical keys / mouse
// buttons / gamepad buttons; gameplay only ever asks about the action.
//
// Headless matters here: the AI drives the same actions through `input.set`, so
// a game is playable by a human at a window and by an agent over the JSON
// channel with no difference in the game code.
class InputSystem {
public:
    void attach(GLFWwindow* w) { win_ = w; }

    void bind(const std::string& action, const std::vector<std::string>& keys);
    void unbind(const std::string& action);
    void clear() { bindings_.clear(); state_.clear(); virt_.clear(); }

    // Poll hardware (no-op when headless) and fold in virtual state, then
    // recompute pressed/released edges. Call once per frame before gameplay.
    void update(float dt);

    bool down(const std::string& action) const;
    bool pressed(const std::string& action) const;    // went down this frame
    bool released(const std::string& action) const;   // went up this frame
    // -1..1 from a negative/positive action pair, e.g. axis("left","right").
    float axis(const std::string& neg, const std::string& pos) const;

    // AI / scripted input: hold or release an action until told otherwise.
    void set_virtual(const std::string& action, bool held);
    void clear_virtual() { virt_.clear(); }

    glm::vec2 mouse() const { return mouse_; }
    glm::vec2 mouse_delta() const { return mouse_delta_; }
    float scroll() const { return scroll_; }
    void add_scroll(float s) { scroll_ += s; }

    nlohmann::json state_json() const;
    nlohmann::json bindings_json() const;
    const std::unordered_map<std::string, std::vector<std::string>>& bindings() const {
        return bindings_;
    }

private:
    struct Btn { bool down = false, prev = false; };
    GLFWwindow* win_ = nullptr;
    std::unordered_map<std::string, std::vector<std::string>> bindings_;
    std::unordered_map<std::string, Btn> state_;
    std::unordered_map<std::string, bool> virt_;
    glm::vec2 mouse_{0}, mouse_delta_{0};
    float scroll_ = 0.0f;
    bool have_mouse_ = false;
};

// "W", "Space", "Escape", "Left", "Mouse1", "Pad:A", "Pad:Start" ... -> code.
// Returns false for an unknown name.
bool key_code(const std::string& name, int& out_code, int& out_kind);   // kind: 0 key, 1 mouse, 2 pad

} // namespace eng
