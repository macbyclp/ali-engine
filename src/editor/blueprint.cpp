#include "editor/blueprint.hpp"
#include "core/log.hpp"

#include <imgui_node_editor.h>
#include <algorithm>

namespace ed = ax::NodeEditor;
using nlohmann::json;

namespace eng {

// ---------------- node catalog ----------------
struct Spec { const char* label; const char* category; bool is_event; };
static const std::vector<std::pair<std::string, Spec>>& catalog() {
    static const std::vector<std::pair<std::string, Spec>> c = {
        {"on.start", {"On Start", "Events", true}},
        {"on.tick", {"On Tick", "Events", true}},
        {"on.collision", {"On Collision", "Events", true}},
        {"on.event", {"On Event", "Events", true}},
        {"act.impulse", {"Impulse", "Physics", false}},
        {"act.setVelocity", {"Set Velocity", "Physics", false}},
        {"act.spin", {"Spin", "Transform", false}},
        {"act.moveToward", {"Move Toward", "Transform", false}},
        {"act.setColor", {"Set Color", "Material", false}},
        {"act.spawn", {"Spawn", "Scene", false}},
        {"act.destroy", {"Destroy", "Scene", false}},
        {"act.emit", {"Emit Event", "Logic", false}},
        {"act.setState", {"Set State", "Logic", false}},
        {"act.addState", {"Add State", "Logic", false}},
        {"act.timer", {"Timer", "Logic", false}},
        {"act.setUI", {"Set UI", "UI", false}},
        {"act.log", {"Log", "Debug", false}},
    };
    return c;
}
static const Spec* spec_of(const std::string& kind) {
    for (auto& [k, s] : catalog()) if (k == kind) return &s;
    return nullptr;
}
static bool is_event(const std::string& kind) { return kind.rfind("on.", 0) == 0; }
static std::string trigger_of(const std::string& kind) {
    if (kind == "on.start") return "start";
    if (kind == "on.tick") return "tick";
    if (kind == "on.collision") return "collision";
    return "event";
}

static json default_params(const std::string& kind) {
    if (kind == "on.collision") return {{"with", ""}};
    if (kind == "on.event") return {{"name", "wave"}};
    if (kind == "act.impulse") return {{"impulse", {0, 6, 0}}};
    if (kind == "act.setVelocity") return {{"velocity", {0, 0, 0}}};
    if (kind == "act.spin") return {{"axis", {0, 1, 0}}, {"speed_deg", 90.0}};
    if (kind == "act.moveToward") return {{"target", {0, 0, 0}}, {"speed", 3.0}};
    if (kind == "act.setColor") return {{"color", {1.0, 0.3, 0.2}}};
    if (kind == "act.spawn") return {{"primitive", "sphere"}, {"position", {0, 3, 0}}};
    if (kind == "act.destroy") return {{"target", ""}};
    if (kind == "act.emit") return {{"event", "hit"}};
    if (kind == "act.setState") return {{"key", "phase"}, {"value", "combat"}};
    if (kind == "act.addState") return {{"key", "score"}, {"value", 10.0}};
    if (kind == "act.timer") return {{"after", 1.0}, {"event", "wave"}};
    if (kind == "act.setUI") return {{"target", "hud"}, {"text", "SCORE ${score}"}};
    if (kind == "act.log") return {{"message", "hello"}};
    return json::object();
}

// ---------------- lifecycle ----------------
BlueprintEditor::BlueprintEditor() {
    ed::Config cfg;
    cfg.SettingsFile = nullptr;
    ctx_ = ed::CreateEditor(&cfg);
}
BlueprintEditor::~BlueprintEditor() { if (ctx_) ed::DestroyEditor(ctx_); }

BlueprintEditor::Node& BlueprintEditor::add_node(const std::string& kind, ImVec2 pos) {
    Node n;
    n.id = nid();
    n.kind = kind;
    n.pos = pos;
    n.params = default_params(kind);
    if (!is_event(kind)) { n.in_exec = nid(); n.pins.push_back({n.in_exec, true, true, "in"}); }
    n.out_exec = nid();
    n.pins.push_back({n.out_exec, false, true, "out"});
    nodes_.push_back(std::move(n));
    ed::SetNodePosition(nodes_.back().id, pos);
    return nodes_.back();
}

int BlueprintEditor::out_link_target(int pin) const {
    for (auto& l : links_)
        if (l.a == pin)
            for (size_t i = 0; i < nodes_.size(); ++i)
                if (nodes_[i].in_exec == l.b) return (int)i;
    return -1;
}

// ---------------- load / compile ----------------
void BlueprintEditor::load_from_behavior(CommandContext& ctx, const std::string& target) {
    nodes_.clear(); links_.clear(); next_id_ = 1;
    target_ = target;
    auto e = ctx.scene.find(target);
    if (e == entt::null) return;
    auto* b = ctx.scene.registry.try_get<Behavior>(e);
    if (!b || !b->rules.is_array()) return;

    float y = 40;
    for (const json& rule : b->rules) {
        std::string on = rule.value("on", std::string("tick"));
        std::string kind = on == "start" ? "on.start" : on == "tick" ? "on.tick"
                         : on == "collision" ? "on.collision" : "on.event";
        Node& ev = add_node(kind, {40, y});
        if (on == "collision") ev.params["with"] = rule.value("with", std::string());
        if (on == "event") ev.params["name"] = rule.value("name", std::string("wave"));

        int prev_out = ev.out_exec;
        float x = 300;
        for (const json& a : rule.value("do", json::array())) {
            std::string act = "act." + a.value("action", std::string("log"));
            if (!spec_of(act)) continue;
            Node& an = add_node(act, {x, y});
            json p = a; p.erase("action");
            for (auto& [k, v] : p.items()) an.params[k] = v;
            links_.push_back({nid(), prev_out, an.in_exec});
            prev_out = an.out_exec;
            x += 240;
        }
        y += 180;
    }
}

json BlueprintEditor::compile() const {
    json rules = json::array();
    for (const Node& n : nodes_) {
        if (!is_event(n.kind)) continue;
        json rule;
        rule["on"] = trigger_of(n.kind);
        if (n.kind == "on.collision" && !n.params.value("with", std::string()).empty())
            rule["with"] = n.params["with"];
        if (n.kind == "on.event") rule["name"] = n.params.value("name", std::string("wave"));

        json do_ = json::array();
        int cur = out_link_target(n.out_exec);
        int guard = 0;
        while (cur != -1 && guard++ < 200) {
            const Node& a = nodes_[cur];
            json act = a.params;
            act["action"] = a.kind.substr(4);   // strip "act."
            do_.push_back(act);
            cur = out_link_target(a.out_exec);
        }
        rule["do"] = do_;
        rules.push_back(rule);
    }
    return rules;
}

// ---------------- node param widgets ----------------
static void edit_params(const std::string& kind, json& p) {
    ImGui::PushItemWidth(150);
    auto vec3 = [&](const char* key) {
        float v[3] = {0, 0, 0};
        if (p.contains(key) && p[key].is_array() && p[key].size() == 3)
            for (int i = 0; i < 3; ++i) v[i] = p[key][i].get<float>();
        if (ImGui::DragFloat3(key, v, 0.05f)) p[key] = {v[0], v[1], v[2]};
    };
    auto str = [&](const char* key) {
        char buf[96] = {0};
        std::string s = p.value(key, std::string());
        std::snprintf(buf, sizeof(buf), "%s", s.c_str());
        if (ImGui::InputText(key, buf, sizeof(buf))) p[key] = std::string(buf);
    };
    auto flt = [&](const char* key) {
        float f = p.value(key, 0.0f);
        if (ImGui::DragFloat(key, &f, 0.1f)) p[key] = f;
    };

    if (kind == "on.collision") str("with");
    else if (kind == "on.event") str("name");
    else if (kind == "act.impulse") vec3("impulse");
    else if (kind == "act.setVelocity") vec3("velocity");
    else if (kind == "act.spin") { vec3("axis"); flt("speed_deg"); }
    else if (kind == "act.moveToward") { vec3("target"); flt("speed"); }
    else if (kind == "act.setColor") {
        float c[3] = {1, 1, 1};
        if (p.contains("color")) for (int i = 0; i < 3; ++i) c[i] = p["color"][i].get<float>();
        if (ImGui::ColorEdit3("color", c)) p["color"] = {c[0], c[1], c[2]};
    } else if (kind == "act.spawn") { str("primitive"); vec3("position"); }
    else if (kind == "act.destroy") str("target");
    else if (kind == "act.emit") str("event");
    else if (kind == "act.setState") { str("key"); str("value"); }
    else if (kind == "act.addState") { str("key"); flt("value"); }
    else if (kind == "act.timer") { flt("after"); str("event"); }
    else if (kind == "act.setUI") { str("target"); str("text"); }
    else if (kind == "act.log") str("message");
    ImGui::PopItemWidth();
}

// ---------------- draw ----------------
void BlueprintEditor::context_menu() {
    ed::Suspend();
    if (ed::ShowBackgroundContextMenu()) ImGui::OpenPopup("bp_add");
    if (ImGui::BeginPopup("bp_add")) {
        ImVec2 mouse = ImGui::GetMousePosOnOpeningCurrentPopup();
        const char* cat = nullptr;
        for (auto& [kind, s] : catalog()) {
            if (!cat || std::string(cat) != s.category) {
                if (cat) ImGui::EndMenu();
                cat = s.category;
                if (!ImGui::BeginMenu(cat)) { cat = nullptr; continue; }
            }
            if (ImGui::MenuItem(s.label)) {
                Node& n = add_node(kind, mouse);
                ed::SetNodePosition(n.id, mouse);
            }
        }
        if (cat) ImGui::EndMenu();
        ImGui::EndPopup();
    }
    ed::Resume();
}

void BlueprintEditor::draw(CommandContext& ctx, const std::string& target) {
    if (target != target_) load_from_behavior(ctx, target);

    ImGui::Text("Blueprint  ·  %s", target.empty() ? "(no target)" : target.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Compile") && !target.empty()) {
        json rules = compile();
        dispatch(ctx, {{"method", "behavior.set"}, {"params", {{"name", target}, {"behaviors", rules}}}});
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) { nodes_.clear(); links_.clear(); }
    ImGui::SameLine();
    ImGui::TextDisabled("right-click: add node   drag pins: wire   Del: remove");

    ed::SetCurrentEditor(ctx_);
    ed::Begin("bp", ImVec2(0, 0));

    for (Node& n : nodes_) {
        ed::BeginNode(n.id);
        const Spec* s = spec_of(n.kind);
        ImGui::PushStyleColor(ImGuiCol_Text, is_event(n.kind) ? ImVec4(1, 0.55f, 0.4f, 1)
                                                             : ImVec4(0.55f, 0.75f, 1, 1));
        ImGui::TextUnformatted(s ? s->label : n.kind.c_str());
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(4, 2));

        for (Pin& pin : n.pins) {
            if (pin.input) {
                ed::BeginPin(pin.id, ed::PinKind::Input);
                ImGui::Text("-> %s", pin.name.c_str());
                ed::EndPin();
            }
        }
        edit_params(n.kind, n.params);
        for (Pin& pin : n.pins) {
            if (!pin.input) {
                ed::BeginPin(pin.id, ed::PinKind::Output);
                ImGui::Text("%s ->", pin.name.c_str());
                ed::EndPin();
            }
        }
        ed::EndNode();
    }
    for (Link& l : links_) ed::Link(l.id, l.a, l.b);

    if (ed::BeginCreate()) {
        ed::PinId a, b;
        if (ed::QueryNewLink(&a, &b) && a && b && a != b) {
            if (ed::AcceptNewItem())
                links_.push_back({nid(), (int)(intptr_t)a.AsPointer(), (int)(intptr_t)b.AsPointer()});
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
                nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(),
                                            [&](const Node& x) { return x.id == id; }), nodes_.end());
            }
        }
    }
    ed::EndDelete();

    context_menu();
    ed::End();
    ed::SetCurrentEditor(nullptr);
}

} // namespace eng
