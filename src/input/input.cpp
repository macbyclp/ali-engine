#include "input/input.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cctype>

namespace eng {
using nlohmann::json;

static std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

bool key_code(const std::string& name_in, int& out_code, int& out_kind) {
    std::string n = upper(name_in);
    out_kind = 0;

    if (n.rfind("MOUSE", 0) == 0) {
        out_kind = 1;
        int b = n.size() > 5 ? n[5] - '1' : 0;
        out_code = std::clamp(b, 0, 7);
        return true;
    }
    if (n.rfind("PAD:", 0) == 0) {
        out_kind = 2;
        std::string b = n.substr(4);
        static const std::unordered_map<std::string, int> pad = {
            {"A", GLFW_GAMEPAD_BUTTON_A}, {"B", GLFW_GAMEPAD_BUTTON_B},
            {"X", GLFW_GAMEPAD_BUTTON_X}, {"Y", GLFW_GAMEPAD_BUTTON_Y},
            {"LB", GLFW_GAMEPAD_BUTTON_LEFT_BUMPER},
            {"RB", GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER},
            {"BACK", GLFW_GAMEPAD_BUTTON_BACK}, {"START", GLFW_GAMEPAD_BUTTON_START},
            {"UP", GLFW_GAMEPAD_BUTTON_DPAD_UP}, {"DOWN", GLFW_GAMEPAD_BUTTON_DPAD_DOWN},
            {"LEFT", GLFW_GAMEPAD_BUTTON_DPAD_LEFT}, {"RIGHT", GLFW_GAMEPAD_BUTTON_DPAD_RIGHT},
        };
        auto it = pad.find(b);
        if (it == pad.end()) return false;
        out_code = it->second;
        return true;
    }

    if (n.size() == 1) {
        char c = n[0];
        if (c >= 'A' && c <= 'Z') { out_code = GLFW_KEY_A + (c - 'A'); return true; }
        if (c >= '0' && c <= '9') { out_code = GLFW_KEY_0 + (c - '0'); return true; }
    }
    static const std::unordered_map<std::string, int> named = {
        {"SPACE", GLFW_KEY_SPACE}, {"ENTER", GLFW_KEY_ENTER}, {"RETURN", GLFW_KEY_ENTER},
        {"ESCAPE", GLFW_KEY_ESCAPE}, {"ESC", GLFW_KEY_ESCAPE}, {"TAB", GLFW_KEY_TAB},
        {"BACKSPACE", GLFW_KEY_BACKSPACE}, {"DELETE", GLFW_KEY_DELETE},
        {"LEFT", GLFW_KEY_LEFT}, {"RIGHT", GLFW_KEY_RIGHT},
        {"UP", GLFW_KEY_UP}, {"DOWN", GLFW_KEY_DOWN},
        {"SHIFT", GLFW_KEY_LEFT_SHIFT}, {"LSHIFT", GLFW_KEY_LEFT_SHIFT},
        {"RSHIFT", GLFW_KEY_RIGHT_SHIFT},
        {"CTRL", GLFW_KEY_LEFT_CONTROL}, {"LCTRL", GLFW_KEY_LEFT_CONTROL},
        {"RCTRL", GLFW_KEY_RIGHT_CONTROL},
        {"ALT", GLFW_KEY_LEFT_ALT}, {"LALT", GLFW_KEY_LEFT_ALT}, {"RALT", GLFW_KEY_RIGHT_ALT},
        {"F1", GLFW_KEY_F1}, {"F2", GLFW_KEY_F2}, {"F3", GLFW_KEY_F3}, {"F4", GLFW_KEY_F4},
        {"F5", GLFW_KEY_F5}, {"F6", GLFW_KEY_F6}, {"F7", GLFW_KEY_F7}, {"F8", GLFW_KEY_F8},
        {"F9", GLFW_KEY_F9}, {"F10", GLFW_KEY_F10}, {"F11", GLFW_KEY_F11}, {"F12", GLFW_KEY_F12},
        {"COMMA", GLFW_KEY_COMMA}, {"PERIOD", GLFW_KEY_PERIOD}, {"MINUS", GLFW_KEY_MINUS},
        {"EQUAL", GLFW_KEY_EQUAL},
    };
    auto it = named.find(n);
    if (it == named.end()) return false;
    out_code = it->second;
    return true;
}

void InputSystem::bind(const std::string& action, const std::vector<std::string>& keys) {
    bindings_[action] = keys;
    state_[action];   // ensure it exists so pressed/released stay well-defined
}
void InputSystem::unbind(const std::string& action) {
    bindings_.erase(action);
    state_.erase(action);
    virt_.erase(action);
}

void InputSystem::set_virtual(const std::string& action, bool held) {
    virt_[action] = held;
    state_[action];
}

void InputSystem::update(float) {
    // Ignore the keyboard/mouse when the window is not focused -- a game must not
    // react while the player has alt-tabbed away. Virtual (AI) input is unaffected.
    bool focused = win_ && glfwGetWindowAttrib(win_, GLFW_FOCUSED) == GLFW_TRUE;

    GLFWgamepadstate pad{};
    bool have_pad = focused && glfwJoystickIsGamepad(GLFW_JOYSTICK_1) &&
                    glfwGetGamepadState(GLFW_JOYSTICK_1, &pad);

    for (auto& [action, btn] : state_) {
        btn.prev = btn.down;
        bool hw = false;
        auto bi = bindings_.find(action);
        if (focused && bi != bindings_.end()) {
            for (const std::string& k : bi->second) {
                int code = 0, kind = 0;
                if (!key_code(k, code, kind)) continue;
                if (kind == 0 && glfwGetKey(win_, code) == GLFW_PRESS) { hw = true; break; }
                if (kind == 1 && glfwGetMouseButton(win_, code) == GLFW_PRESS) { hw = true; break; }
                if (kind == 2 && have_pad && pad.buttons[code] == GLFW_PRESS) { hw = true; break; }
            }
        }
        auto vi = virt_.find(action);
        btn.down = hw || (vi != virt_.end() && vi->second);
    }

    if (win_) {
        double mx = 0, my = 0;
        glfwGetCursorPos(win_, &mx, &my);
        glm::vec2 m{(float)mx, (float)my};
        mouse_delta_ = have_mouse_ ? m - mouse_ : glm::vec2(0);
        mouse_ = m;
        have_mouse_ = true;
    }
}

bool InputSystem::down(const std::string& a) const {
    auto it = state_.find(a);
    return it != state_.end() && it->second.down;
}
bool InputSystem::pressed(const std::string& a) const {
    auto it = state_.find(a);
    return it != state_.end() && it->second.down && !it->second.prev;
}
bool InputSystem::released(const std::string& a) const {
    auto it = state_.find(a);
    return it != state_.end() && !it->second.down && it->second.prev;
}
float InputSystem::axis(const std::string& neg, const std::string& pos) const {
    return (down(pos) ? 1.0f : 0.0f) - (down(neg) ? 1.0f : 0.0f);
}

json InputSystem::state_json() const {
    json actions = json::object();
    for (auto& [a, b] : state_)
        actions[a] = {{"down", b.down}, {"pressed", b.down && !b.prev},
                      {"released", !b.down && b.prev}};
    return {{"actions", actions},
            {"mouse", json::array({mouse_.x, mouse_.y})},
            {"mouse_delta", json::array({mouse_delta_.x, mouse_delta_.y})}};
}

json InputSystem::bindings_json() const {
    json j = json::object();
    for (auto& [a, keys] : bindings_) j[a] = keys;
    return j;
}

} // namespace eng
