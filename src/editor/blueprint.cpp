#include "editor/blueprint.hpp"
#include "core/i18n.hpp"
#include "core/log.hpp"

#include <imgui_node_editor.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace ed = ax::NodeEditor;
using nlohmann::json;

namespace eng {

using i18n::L;
using i18n::T;

// ---------------- json helpers (rules may come from anywhere, so never throw on odd types) ----------------
static std::string sval(const json& j, const char* key, const std::string& def = std::string()) {
    if (j.is_object()) {
        auto it = j.find(key);
        if (it != j.end() && it->is_string()) return it->get<std::string>();
    }
    return def;
}
static float fval(const json& j, const char* key, float def) {
    if (j.is_object()) {
        auto it = j.find(key);
        if (it != j.end() && it->is_number()) return it->get<float>();
    }
    return def;
}
static bool bval(const json& j, const char* key, bool def) {
    if (j.is_object()) {
        auto it = j.find(key);
        if (it != j.end() && it->is_boolean()) return it->get<bool>();
    }
    return def;
}
static bool contains_ci(const std::string& hay, const char* needle) {
    std::string h = hay, n = needle;
    for (char& c : h) c = (char)std::tolower((unsigned char)c);
    for (char& c : n) c = (char)std::tolower((unsigned char)c);
    return h.find(n) != std::string::npos;
}

// ---------------- node catalog ----------------
struct Spec { const char* label; const char* category; bool is_event; };
static const std::vector<std::pair<std::string, Spec>>& catalog() {
    // categories must be contiguous: the add menu groups consecutive entries
    static const std::vector<std::pair<std::string, Spec>> c = {
        {"on.start", {"On Start", "Events", true}},
        {"on.tick", {"On Tick", "Events", true}},
        {"on.collision", {"On Collision", "Events", true}},
        {"on.enter", {"On Enter", "Events", true}},
        {"on.exit", {"On Exit", "Events", true}},
        {"on.event", {"On Event", "Events", true}},
        {"on.input", {"On Input", "Events", true}},
        {"on.custom", {"On Custom Trigger", "Events", true}},
        {"act.impulse", {"Impulse", "Physics", false}},
        {"act.setVelocity", {"Set Velocity", "Physics", false}},
        {"act.move", {"Move", "Physics", false}},
        {"act.spin", {"Spin", "Transform", false}},
        {"act.moveToward", {"Move Toward", "Transform", false}},
        {"act.setColor", {"Set Color", "Material", false}},
        {"act.setMaterial", {"Set Material", "Material", false}},
        {"act.spawn", {"Spawn", "Scene", false}},
        {"act.destroy", {"Destroy", "Scene", false}},
        {"act.emit", {"Emit Event", "Logic", false}},
        {"act.setState", {"Set State", "Logic", false}},
        {"act.addState", {"Add State", "Logic", false}},
        {"act.timer", {"Timer", "Logic", false}},
        {"act.setUI", {"Set UI", "UI", false}},
        {"act.sound", {"Play Sound", "Audio", false}},
        {"act.animParam", {"Anim Param", "Animation", false}},
        {"act.log", {"Log", "Debug", false}},
        {"act.raw", {"Raw Action (JSON)", "Advanced", false}},
    };
    return c;
}
static const Spec* spec_of(const std::string& kind) {
    for (auto& [k, s] : catalog()) if (k == kind) return &s;
    return nullptr;
}
static bool is_event(const std::string& kind) { return kind.rfind("on.", 0) == 0; }
static bool has_with(const std::string& kind) { return kind == "on.collision" || kind == "on.enter" || kind == "on.exit"; }

// input modes as stored in the node ("mode") and the engine trigger each one compiles to
static const char* kModeNames[] = {"pressed", "held", "released"};
static const char* kModeTriggers[] = {"inputPressed", "input", "inputReleased"};
static int mode_index(const std::string& m) {
    for (int i = 0; i < 3; ++i) if (m == kModeNames[i]) return i;
    return 0;
}

static std::string trigger_of(const std::string& kind, const json& params) {
    if (kind == "on.start") return "start";
    if (kind == "on.tick") return "tick";
    if (kind == "on.collision") return "collision";
    if (kind == "on.enter") return "enter";
    if (kind == "on.exit") return "exit";
    if (kind == "on.input") return kModeTriggers[mode_index(sval(params, "mode", "pressed"))];
    if (kind == "on.custom") return sval(params, "on", "tick");
    return "event";
}

static json default_params(const std::string& kind) {
    if (has_with(kind)) return {{"with", ""}};
    if (kind == "on.event") return {{"name", "wave"}};
    if (kind == "on.input") return {{"input", "jump"}, {"mode", "pressed"}};
    if (kind == "on.custom") return {{"on", "tick"}};
    if (kind == "act.impulse") return {{"impulse", {0, 6, 0}}};
    if (kind == "act.setVelocity") return {{"velocity", {0, 0, 0}}};
    if (kind == "act.move") return {{"velocity", {0, 0, 0}}, {"keep_y", true}};
    if (kind == "act.spin") return {{"axis", {0, 1, 0}}, {"speed_deg", 90.0}};
    if (kind == "act.moveToward") return {{"target", {0, 0, 0}}, {"speed", 3.0}};
    if (kind == "act.setColor") return {{"color", {1.0, 0.3, 0.2}}};
    if (kind == "act.setMaterial") return {{"base_color", {0.8, 0.8, 0.8}}, {"metallic", 0.0}, {"roughness", 0.5}};
    if (kind == "act.spawn") return {{"primitive", "sphere"}, {"position", {0, 3, 0}}};
    if (kind == "act.destroy") return {{"target", ""}};
    if (kind == "act.emit") return {{"event", "hit"}};
    if (kind == "act.setState") return {{"key", "phase"}, {"value", "combat"}};
    if (kind == "act.addState") return {{"key", "score"}, {"value", 10.0}};
    if (kind == "act.timer") return {{"after", 1.0}, {"event", "wave"}};
    if (kind == "act.setUI") return {{"target", "hud"}, {"text", "SCORE ${score}"}};
    if (kind == "act.sound") return {{"file", ""}, {"volume", 1.0}};
    if (kind == "act.animParam") return {{"param", "speed"}, {"value", 1.0}};
    if (kind == "act.log") return {{"message", "hello"}};
    if (kind == "act.raw") return {{"action", "log"}, {"message", "hello"}};
    return json::object();
}

static ImVec4 header_color(const std::string& kind) {
    if (is_event(kind)) return ImVec4(1.0f, 0.55f, 0.40f, 1);
    const Spec* s = spec_of(kind);
    std::string c = s ? s->category : "";
    if (c == "Physics" || c == "Transform") return ImVec4(0.55f, 0.75f, 1.0f, 1);
    if (c == "Material") return ImVec4(0.95f, 0.65f, 0.95f, 1);
    if (c == "Scene") return ImVec4(0.45f, 0.90f, 0.80f, 1);
    if (c == "Logic") return ImVec4(0.75f, 0.65f, 1.0f, 1);
    if (c == "UI") return ImVec4(0.55f, 0.95f, 0.55f, 1);
    if (c == "Audio") return ImVec4(1.0f, 0.75f, 0.55f, 1);
    if (c == "Animation") return ImVec4(0.95f, 0.90f, 0.45f, 1);
    return ImVec4(0.75f, 0.75f, 0.78f, 1);
}

// ---------------- lifecycle ----------------
BlueprintEditor::BlueprintEditor() {
    ed::Config cfg;
    cfg.SettingsFile = nullptr;
    ctx_ = ed::CreateEditor(&cfg);
}
BlueprintEditor::~BlueprintEditor() { if (ctx_) ed::DestroyEditor(ctx_); }

BlueprintEditor::Node& BlueprintEditor::add_node(const std::string& kind, ImVec2 pos, bool with_defaults) {
    Node n;
    n.id = nid();
    n.kind = kind;
    n.pos = pos;
    n.params = with_defaults ? default_params(kind) : json::object();
    if (!is_event(kind)) { n.in_exec = nid(); n.pins.push_back({n.in_exec, true, true, "in"}); }
    n.out_exec = nid();
    n.pins.push_back({n.out_exec, false, true, "out"});
    nodes_.push_back(std::move(n));
    return nodes_.back();   // position applied in draw(), once the editor is current
}

int BlueprintEditor::out_link_target(int pin) const {
    for (auto& l : links_)
        if (l.a == pin)
            for (size_t i = 0; i < nodes_.size(); ++i)
                if (nodes_[i].in_exec == l.b) return (int)i;
    return -1;
}

const BlueprintEditor::Pin* BlueprintEditor::find_pin(int pin) const {
    for (auto& n : nodes_)
        for (auto& p : n.pins)
            if (p.id == pin) return &p;
    return nullptr;
}

int BlueprintEditor::node_of_pin(int pin) const {
    for (size_t i = 0; i < nodes_.size(); ++i)
        for (auto& p : nodes_[i].pins)
            if (p.id == pin) return (int)i;
    return -1;
}

bool BlueprintEditor::reaches(int from_node, int to_node) const {
    int cur = from_node, guard = 0;
    while (cur != -1 && guard++ < 1000) {
        if (cur == to_node) return true;
        cur = out_link_target(nodes_[cur].out_exec);
    }
    return false;
}

// ---------------- load / compile ----------------
void BlueprintEditor::load_rules(const json& rules) {
    nodes_.clear(); links_.clear(); next_id_ = 1;
    if (!rules.is_array()) return;

    float y = 40;
    for (const json& rule : rules) {
        if (!rule.is_object()) continue;
        const std::string on = sval(rule, "on", "tick");
        std::string kind = on == "start" ? "on.start" : on == "tick" ? "on.tick"
                         : on == "collision" ? "on.collision" : on == "enter" ? "on.enter"
                         : on == "exit" ? "on.exit" : on == "event" ? "on.event"
                         : (on == "input" || on == "inputPressed" || on == "inputReleased") ? "on.input"
                         : "on.custom";

        json params = json::object();
        json extra = json::object();
        for (auto& [k, v] : rule.items()) {
            if (k == "do") continue;                                     // rebuilt from the chain
            if (k == "on") { if (kind == "on.custom") params["on"] = v; continue; }
            if (k == "if") { params["if"] = v; continue; }
            if (k == "with" && has_with(kind)) { params["with"] = v; continue; }
            if (k == "name" && kind == "on.event") { params["name"] = v; continue; }
            if (k == "action" && kind == "on.input") { params["input"] = v; continue; }
            extra[k] = v;                                                // unknown key: kept verbatim
        }
        if (kind == "on.input")
            params["mode"] = on == "input" ? "held" : on == "inputReleased" ? "released" : "pressed";

        add_node(kind, {40, y}, false);
        const size_t ev_idx = nodes_.size() - 1;   // add_node() below may reallocate, so no references
        nodes_[ev_idx].params = params;
        nodes_[ev_idx].extra = extra;

        int prev_out = nodes_[ev_idx].out_exec;
        float x = 300;
        const json empty = json::array();
        const json& acts = rule.contains("do") && rule["do"].is_array() ? rule["do"] : empty;
        for (const json& a : acts) {
            if (!a.is_object()) continue;
            const std::string name = sval(a, "action");
            const std::string akind = "act." + name;
            const bool known = !name.empty() && akind != "act.raw" && spec_of(akind);

            add_node(known ? akind : "act.raw", {x, y}, false);
            Node& an = nodes_.back();
            if (known) { an.params = a; an.params.erase("action"); }
            else { an.params = a; an.raw_buf = a.dump(2); }
            links_.push_back({nid(), prev_out, an.in_exec});
            prev_out = an.out_exec;
            x += 260;
        }
        y += rule.contains("if") ? 230 : 180;
    }
}

void BlueprintEditor::load_from_behavior(CommandContext& ctx, const std::string& target) {
    target_ = target;
    nodes_.clear(); links_.clear(); next_id_ = 1;
    auto e = ctx.scene.find(target);
    if (e != entt::null)
        if (auto* b = ctx.scene.registry.try_get<Behavior>(e)) load_rules(b->rules);
    applied_ = compile().dump();
}

json BlueprintEditor::compile() const {
    json rules = json::array();
    for (const Node& n : nodes_) {
        if (!is_event(n.kind)) continue;
        json rule = n.extra.is_object() ? n.extra : json::object();
        rule["on"] = trigger_of(n.kind, n.params);
        if (has_with(n.kind) && n.params.contains("with")) {
            const json& w = n.params["with"];
            if (!w.is_string() || !w.get<std::string>().empty()) rule["with"] = w;   // empty == "any": omit
        }
        if (n.kind == "on.event") rule["name"] = n.params.contains("name") ? n.params["name"] : json("wave");
        if (n.kind == "on.input") rule["action"] = n.params.contains("input") ? n.params["input"] : json("");
        if (n.params.contains("if") && n.params["if"].is_object() && !n.params["if"].empty())
            rule["if"] = n.params["if"];

        json do_ = json::array();
        int cur = out_link_target(n.out_exec);
        int guard = 0;
        while (cur != -1 && guard++ < 200) {
            const Node& a = nodes_[cur];
            json act = a.params;
            if (a.kind != "act.raw") act["action"] = a.kind.substr(4);   // strip "act."
            do_.push_back(act);
            cur = out_link_target(a.out_exec);
        }
        rule["do"] = do_;
        rules.push_back(rule);
    }
    return rules;
}

json BlueprintEditor::roundtrip(const json& rules) {
    load_rules(rules);
    return compile();
}

// ---------------- node param widgets ----------------
static json parse_value(const std::string& s) {
    if (s == "true") return true;
    if (s == "false") return false;
    if (!s.empty()) {
        char* end = nullptr;
        double d = std::strtod(s.c_str(), &end);
        if (end && *end == '\0') return d;
    }
    return s;
}
static std::string value_text(const json& v) { return v.is_string() ? v.get<std::string>() : v.dump(); }

// Rule-level condition ("if"): {"key": k, <op>: value}. Anything of another shape is left untouched.
static void edit_condition(json& p) {
    static const char* ops[] = {"eq", "ne", "gt", "gte", "lt", "lte", "exists"};
    static const char* op_items = "==\0!=\0>\0>=\0<\0<=\0exists\0";
    bool has = p.contains("if") && p["if"].is_object();
    if (ImGui::Checkbox(L("Condition"), &has)) {
        if (has) p["if"] = {{"key", "score"}, {"gte", 0}};
        else p.erase("if");
    }
    if (!has) return;
    json& c = p["if"];
    int op = -1, count = 0;
    for (int i = 0; i < 7; ++i) if (c.contains(ops[i])) { op = i; ++count; }
    if (count != 1 || !c.contains("key") || !c["key"].is_string()) {
        ImGui::TextDisabled("%s", c.dump().c_str());   // custom shape: shown, never rewritten
        return;
    }
    char kbuf[96];
    std::snprintf(kbuf, sizeof(kbuf), "%s", c["key"].get<std::string>().c_str());
    if (ImGui::InputText(L("key"), kbuf, sizeof(kbuf))) c["key"] = std::string(kbuf);
    int nop = op;
    if (ImGui::Combo(L("operator"), &nop, op_items) && nop != op) {
        json v = c[ops[op]];
        c.erase(ops[op]);
        c[ops[nop]] = nop == 6 ? json(true) : (v.is_boolean() ? json(0) : v);
        op = nop;
    }
    if (op == 6) {
        bool b = bval(c, "exists", true);
        if (ImGui::Checkbox(L("value"), &b)) c["exists"] = b;
    } else {
        char vbuf[96];
        std::snprintf(vbuf, sizeof(vbuf), "%s", value_text(c[ops[op]]).c_str());
        if (ImGui::InputText(L("value"), vbuf, sizeof(vbuf))) c[ops[op]] = parse_value(vbuf);
    }
}

static void edit_params(const std::string& kind, json& p, std::string& raw_buf, bool& raw_bad) {
    ImGui::PushItemWidth(150);
    auto vec3 = [&](const char* key) {
        float v[3] = {0, 0, 0};
        if (p.contains(key) && p[key].is_array() && p[key].size() == 3)
            for (int i = 0; i < 3; ++i) if (p[key][i].is_number()) v[i] = p[key][i].get<float>();
        if (ImGui::DragFloat3(L(key), v, 0.05f)) p[key] = {v[0], v[1], v[2]};
    };
    auto str = [&](const char* key) {
        char buf[96] = {0};
        std::snprintf(buf, sizeof(buf), "%s", value_text(p.contains(key) ? p[key] : json("")).c_str());
        if (ImGui::InputText(L(key), buf, sizeof(buf))) p[key] = std::string(buf);
    };
    // string field whose value may legitimately be a number/bool (state values)
    auto any = [&](const char* key) {
        char buf[96] = {0};
        std::snprintf(buf, sizeof(buf), "%s", value_text(p.contains(key) ? p[key] : json("")).c_str());
        if (ImGui::InputText(L(key), buf, sizeof(buf))) p[key] = parse_value(buf);
    };
    auto flt = [&](const char* key, float def, float speed = 0.1f) {
        float f = fval(p, key, def);
        if (ImGui::DragFloat(L(key), &f, speed)) p[key] = f;
    };
    auto boolean = [&](const char* key, bool def) {
        bool b = bval(p, key, def);
        if (ImGui::Checkbox(L(key), &b)) p[key] = b;
    };
    auto color = [&](const char* key, const float* def) {
        float c[3] = {def[0], def[1], def[2]};
        if (p.contains(key) && p[key].is_array() && p[key].size() >= 3)
            for (int i = 0; i < 3; ++i) if (p[key][i].is_number()) c[i] = p[key][i].get<float>();
        if (ImGui::ColorEdit3(L(key), c)) p[key] = {c[0], c[1], c[2]};
    };

    if (has_with(kind)) str("with");
    else if (kind == "on.event") str("name");
    else if (kind == "on.input") {
        str("input");
        int m = mode_index(sval(p, "mode", "pressed"));
        static const char* items = "pressed\0held\0released\0";
        if (ImGui::Combo(L("mode"), &m, items)) p["mode"] = kModeNames[m];
    }
    else if (kind == "on.custom") str("on");
    else if (kind == "act.impulse") vec3("impulse");
    else if (kind == "act.setVelocity") vec3("velocity");
    else if (kind == "act.move") { vec3("velocity"); boolean("keep_y", true); }
    else if (kind == "act.spin") { vec3("axis"); flt("speed_deg", 90.0f); }
    else if (kind == "act.moveToward") { vec3("target"); flt("speed", 1.0f); }
    else if (kind == "act.setColor") { static const float d[3] = {1, 1, 1}; color("color", d); }
    else if (kind == "act.setMaterial") {
        static const float d[3] = {0.8f, 0.8f, 0.8f};
        color("base_color", d); flt("metallic", 0.0f, 0.01f); flt("roughness", 0.5f, 0.01f);
    }
    else if (kind == "act.spawn") { str("primitive"); vec3("position"); }
    else if (kind == "act.destroy") str("target");
    else if (kind == "act.emit") str("event");
    else if (kind == "act.setState") { str("key"); any("value"); }
    else if (kind == "act.addState") { str("key"); flt("value", 1.0f); }
    else if (kind == "act.timer") { flt("after", 1.0f); str("event"); }
    else if (kind == "act.setUI") { str("target"); str("text"); }
    else if (kind == "act.sound") { str("file"); flt("volume", 1.0f, 0.01f); boolean("loop", false); boolean("spatial", false); }
    else if (kind == "act.animParam") { str("target"); str("param"); flt("value", 1.0f); }
    else if (kind == "act.log") str("message");
    else if (kind == "act.raw") {
        if (raw_buf.empty()) raw_buf = p.dump(2);
        ImGui::TextDisabled("%s", T("any action, as JSON"));
        std::vector<char> buf(raw_buf.begin(), raw_buf.end());
        buf.resize(std::max<size_t>(buf.size() + 256, 512), 0);
        if (ImGui::InputTextMultiline("##raw", buf.data(), buf.size(), ImVec2(230, 90))) {
            raw_buf = buf.data();
            json parsed = json::parse(raw_buf, nullptr, false);
            raw_bad = !parsed.is_object();
            if (!raw_bad) p = parsed;
        }
        if (raw_bad) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", T("invalid JSON (not applied)"));
    }
    ImGui::PopItemWidth();
}

// ---------------- draw ----------------
void BlueprintEditor::context_menu() {
    ed::Suspend();
    if (ed::ShowBackgroundContextMenu()) { ImGui::OpenPopup("bp_add"); filter_[0] = 0; }
    if (ImGui::BeginPopup("bp_add")) {
        ImVec2 mouse = ed::ScreenToCanvas(ImGui::GetMousePosOnOpeningCurrentPopup());
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(190);
        ImGui::InputTextWithHint("##bpf", T("Search node"), filter_, sizeof(filter_));
        ImGui::Separator();
        if (filter_[0]) {
            for (auto& [kind, s] : catalog())
                if ((contains_ci(T(s.label), filter_) || contains_ci(s.label, filter_)) &&
                    ImGui::MenuItem(L(s.label))) {
                    add_node(kind, mouse);   // placed in draw()
                    ImGui::CloseCurrentPopup();
                }
        } else {
            const char* cat = nullptr;
            for (auto& [kind, s] : catalog()) {
                if (!cat || std::string(cat) != s.category) {
                    if (cat) ImGui::EndMenu();
                    cat = s.category;
                    if (!ImGui::BeginMenu(L(cat))) { cat = nullptr; continue; }
                }
                if (ImGui::MenuItem(L(s.label))) add_node(kind, mouse);
            }
            if (cat) ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
    ed::Resume();
}

void BlueprintEditor::duplicate_selected() {
    int n = ed::GetSelectedObjectCount();
    if (n <= 0) return;
    std::vector<ed::NodeId> ids(n);
    int got = ed::GetSelectedNodes(ids.data(), n);
    std::vector<Node> copies;
    for (int i = 0; i < got; ++i) {
        int id = (int)(intptr_t)ids[i].AsPointer();
        for (const Node& src : nodes_) {
            if (src.id != id) continue;
            Node c = src;
            ImVec2 p = ed::GetNodePosition(src.id);
            c.pos = ImVec2(p.x + 40, p.y + 40);
            c.placed = false;
            c.id = nid();
            for (Pin& pin : c.pins) {
                int old = pin.id;
                pin.id = nid();
                if (old == src.in_exec) c.in_exec = pin.id;
                if (old == src.out_exec) c.out_exec = pin.id;
            }
            copies.push_back(std::move(c));
        }
    }
    for (Node& c : copies) nodes_.push_back(std::move(c));
}

void BlueprintEditor::draw(CommandContext& ctx, const std::string& target) {
    if (target != target_) load_from_behavior(ctx, target);

    ImGui::Text(T("Blueprint  ·  %s"), target.empty() ? T("(no target)") : target.c_str());
    ImGui::SameLine();
    if (ImGui::Button(L("Compile")) && !target.empty()) {
        json rules = compile();
        json res = dispatch(ctx, {{"method", "behavior.set"}, {"params", {{"name", target}, {"behaviors", rules}}}});
        int loose = 0;   // action nodes nothing leads to: they are not part of any rule
        for (const Node& nd : nodes_) {
            if (is_event(nd.kind)) continue;
            bool linked = false;
            for (const Link& l : links_) if (l.b == nd.in_exec) { linked = true; break; }
            if (!linked) ++loose;
        }
        char msg[160];
        if (res.value("ok", false)) {
            std::snprintf(msg, sizeof(msg), T("Compiled: %d rule(s)"), (int)rules.size());
            status_ = msg;
            if (loose) { std::snprintf(msg, sizeof(msg), T("  (%d unconnected node(s) skipped)"), loose); status_ += msg; }
            applied_ = rules.dump();
        } else {
            status_ = std::string(T("Compile failed: ")) + res.dump();
        }
        status_until_ = ImGui::GetTime() + 5.0;
    }
    ImGui::SameLine();
    if (ImGui::Button(L("Reload")) && !target.empty()) { target_.clear(); load_from_behavior(ctx, target); }
    ImGui::SameLine();
    if (ImGui::Button(L("Clear"))) { nodes_.clear(); links_.clear(); }
    ImGui::SameLine();
    if (!target.empty() && compile().dump() != applied_) {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.25f, 1), "%s", T("modified - not compiled"));
        ImGui::SameLine();
    }
    if (ImGui::GetTime() < status_until_) {
        ImGui::TextColored(ImVec4(0.5f, 0.95f, 0.6f, 1), "%s", status_.c_str());
        ImGui::SameLine();
    }
    ImGui::TextDisabled(T("right-click: add node   drag pins: wire   Del: remove   Ctrl+D: duplicate   ·   graph = %s's Behavior"),
                        target.empty() ? "?" : target.c_str());

    // Ctrl+D is checked out here, where the Blueprint window (not the node canvas) is current
    const bool dup_req = ImGui::GetIO().KeyCtrl && !ImGui::GetIO().WantTextInput &&
                         ImGui::IsKeyPressed(ImGuiKey_D, false) &&
                         ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

    ed::SetCurrentEditor(ctx_);
    ed::Begin("bp", ImVec2(0, 0));

    std::vector<int> linked_in;   // input pins that something leads into
    for (const Link& l : links_) linked_in.push_back(l.b);

    for (Node& n : nodes_) {
        if (!n.placed) { ed::SetNodePosition(n.id, n.pos); n.placed = true; }
        ed::BeginNode(n.id);
        const Spec* s = spec_of(n.kind);
        ImGui::PushStyleColor(ImGuiCol_Text, header_color(n.kind));
        ImGui::TextUnformatted(s ? T(s->label) : n.kind.c_str());
        ImGui::PopStyleColor();
        if (!is_event(n.kind) && std::find(linked_in.begin(), linked_in.end(), n.in_exec) == linked_in.end())
            ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", T("not connected - will not run"));
        ImGui::Dummy(ImVec2(4, 2));

        for (Pin& pin : n.pins) {
            if (pin.input) {
                ed::BeginPin(pin.id, ed::PinKind::Input);
                ImGui::Text(T("-> %s"), T(pin.name.c_str()));
                ed::EndPin();
            }
        }
        edit_params(n.kind, n.params, n.raw_buf, n.raw_bad);
        if (is_event(n.kind)) {
            ImGui::PushItemWidth(150);   // same width as the other fields, or the node grows and overlaps its neighbours
            edit_condition(n.params);
            ImGui::PopItemWidth();
        }
        for (Pin& pin : n.pins) {
            if (!pin.input) {
                ed::BeginPin(pin.id, ed::PinKind::Output);
                ImGui::Text(T("%s ->"), T(pin.name.c_str()));
                ed::EndPin();
            }
        }
        ed::EndNode();
    }
    for (Link& l : links_) ed::Link(l.id, l.a, l.b);

    if (ed::BeginCreate()) {
        ed::PinId pa, pb;
        if (ed::QueryNewLink(&pa, &pb) && pa && pb) {
            int a = (int)(intptr_t)pa.AsPointer(), b = (int)(intptr_t)pb.AsPointer();
            const Pin* A = find_pin(a);
            const Pin* B = find_pin(b);
            // a link must join one output to one input on different nodes, and never close a loop
            bool ok = A && B && A->input != B->input && node_of_pin(a) != node_of_pin(b);
            int out = 0, in = 0;
            if (ok) {
                out = A->input ? b : a;
                in = A->input ? a : b;
                ok = !reaches(node_of_pin(in), node_of_pin(out));
            }
            if (!ok) {
                ed::RejectNewItem(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), 2.0f);
            } else if (ed::AcceptNewItem()) {
                // one chain per output: wiring it again replaces the old link
                links_.erase(std::remove_if(links_.begin(), links_.end(),
                                            [&](const Link& x) { return x.a == out; }), links_.end());
                links_.push_back({nid(), out, in});
            }
        }
    }
    ed::EndCreate();

    if (ed::BeginDelete()) {
        ed::LinkId lid;
        while (ed::QueryDeletedLink(&lid)) {
            if (ed::AcceptDeletedItem()) {
                int id = (int)(intptr_t)lid.AsPointer();
                links_.erase(std::remove_if(links_.begin(), links_.end(),
                                            [&](const Link& x) { return x.id == id; }), links_.end());
            }
        }
        ed::NodeId nidd;
        while (ed::QueryDeletedNode(&nidd)) {
            if (ed::AcceptDeletedItem()) {
                int id = (int)(intptr_t)nidd.AsPointer();
                // drop the links that touched the node too
                std::vector<int> pins;
                for (const Node& x : nodes_) if (x.id == id) for (const Pin& p : x.pins) pins.push_back(p.id);
                links_.erase(std::remove_if(links_.begin(), links_.end(), [&](const Link& x) {
                    return std::find(pins.begin(), pins.end(), x.a) != pins.end() ||
                           std::find(pins.begin(), pins.end(), x.b) != pins.end(); }), links_.end());
                nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(),
                                            [&](const Node& x) { return x.id == id; }), nodes_.end());
            }
        }
    }
    ed::EndDelete();

    if (dup_req) duplicate_selected();
    context_menu();
    ed::End();
    ed::SetCurrentEditor(nullptr);
}

// ---------------- self-test ----------------
// Every rule must survive load -> compile unchanged (a rule without "do" compiles to "do": [],
// which is equivalent). Prints BPTEST lines; used by ALI_BLUEPRINT_SELFTEST=1.
bool BlueprintEditor::selftest() {
    auto normalized = [](json rules) {
        for (json& r : rules) if (r.is_object() && !r.contains("do")) r["do"] = json::array();
        return rules;
    };
    bool all_ok = true;
    auto check = [&](const char* what, const json& rules) {
        json want = normalized(rules), got = roundtrip(rules);
        bool ok = want == got;
        if (!ok) {
            all_ok = false;
            for (size_t i = 0; i < want.size() && i < got.size(); ++i)
                if (want[i] != got[i]) {
                    std::printf("BPTEST %s: MISMATCH rule %zu\n  want %s\n  got  %s\n", what, i,
                                want[i].dump().c_str(), got[i].dump().c_str());
                    break;
                }
            if (want.size() != got.size())
                std::printf("BPTEST %s: rule count %zu -> %zu\n", what, want.size(), got.size());
        }
        return ok;
    };

    json synthetic = json::parse(R"([
      {"on":"start","do":[{"action":"setState","key":"phase","value":"combat"},{"action":"log","message":"go"}]},
      {"on":"tick","if":{"key":"over","lt":1},"do":[{"action":"spin","axis":[0,1,0],"speed_deg":40}]},
      {"on":"collision","with":"floor","do":[{"action":"impulse","impulse":[0,9,0]},{"action":"addState","key":"score","value":10}]},
      {"on":"enter","with":"player","do":[{"action":"destroy"}]},
      {"on":"exit","do":[{"action":"emit","event":"left"}]},
      {"on":"event","name":"wave","if":{"key":"phase","eq":"combat"},"do":[
         {"action":"spawn","primitive":"sphere","position":[0,5,0],"behavior":[{"on":"tick","do":[{"action":"log","message":"nested"}]}]},
         {"action":"timer","after":0.8,"event":"wave"}]},
      {"on":"input","action":"move_x","do":[{"action":"move","velocity":[1,0,0],"keep_y":true}]},
      {"on":"inputPressed","action":"jump","do":[{"action":"impulse","impulse":[0,6,0]},
         {"action":"sound","file":"a.wav","volume":0.5,"loop":false,"spatial":true}]},
      {"on":"inputReleased","action":"jump","do":[{"action":"setMaterial","base_color":[1,0,0],"metallic":0.2,"roughness":0.4},
         {"action":"animParam","param":"speed","value":2}]},
      {"on":"tick","priority":5,"do":[{"action":"futureAction","x":1}]},
      {"on":"weirdTrigger","do":[]},
      {"on":"event","name":"noop"},
      {"on":"tick","if":{"key":"a","gt":1,"lt":5},"do":[{"action":"setUI","target":"hud","text":"${a}"}]}
    ])");
    bool syn = check("synthetic", synthetic);
    std::printf("BPTEST synthetic: %s (%zu rules)\n", syn ? "PASS" : "FAIL", synthetic.size());

    // loop protection: linking a chain's tail back to its head must be refused (reaches() drives that check)
    {
        load_rules(json::parse(R"([{"on":"tick","do":[{"action":"log","message":"a"},{"action":"log","message":"b"}]}])"));
        bool ok = nodes_.size() == 3 && reaches(0, 2) && reaches(1, 2) && !reaches(2, 1) && !reaches(2, 0) && reaches(1, 1);
        std::printf("BPTEST loop guard: %s\n", ok ? "PASS" : "FAIL");
        all_ok = all_ok && ok;
    }

    // real behaviors shipped with the engine
    const char* files[] = {"games/orbrun/orbrun.json", "scenes/showcase.json", "scenes/generated.json",
                           "scenes/playtest.json", "scenes/ai_game.json"};
    for (const char* rel : files) {
        std::ifstream f(std::string(ENGINE_ASSET_DIR) + "/" + rel);
        if (!f) { std::printf("BPTEST %s: (file not found)\n", rel); all_ok = false; continue; }
        json scene = json::parse(f, nullptr, false);
        if (!scene.is_object() || !scene.contains("entities")) continue;
        int rules = 0, ok = 0;
        for (const json& e : scene["entities"]) {
            if (!e.contains("behavior") || !e["behavior"].is_array()) continue;
            rules += (int)e["behavior"].size();
            std::string label = std::string(rel) + ":" + sval(e, "name", "?");
            if (check(label.c_str(), e["behavior"])) ok += (int)e["behavior"].size();
        }
        std::printf("BPTEST %s: %d/%d rules survive load->compile  %s\n", rel, ok, rules,
                    ok == rules ? "PASS" : "FAIL");
        if (ok != rules) all_ok = false;
    }
    std::printf("BPTEST result: %s\n", all_ok ? "ALL PASS" : "FAILURES");
    std::fflush(stdout);
    return all_ok;
}

} // namespace eng
