#pragma once
#include "aicontrol/commands.hpp"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace ax::NodeEditor { struct EditorContext; }

namespace eng {

// A Blueprint-style visual scripting graph. Event nodes (start/tick/collision/enter/exit/
// event/input) chain into action nodes via exec pins; "Compile" turns the graph into the
// same Behavior JSON the AI writes and applies it to the selected entity.
//
// Loading a Behavior and compiling it again is lossless: rule-level conditions ("if"), unknown
// rule keys and actions the editor has no dedicated node for (kept as Raw Action nodes) all
// survive, so opening an AI-written behavior in the graph can never damage it.
class BlueprintEditor {
public:
    BlueprintEditor();
    ~BlueprintEditor();

    // Draws the graph panel for `target` (an entity name). Rebuilds the graph
    // from that entity's Behavior when the target changes.
    void draw(CommandContext& ctx, const std::string& target);

    // Loads `rules` into the graph and compiles it straight back (used by the self-test).
    nlohmann::json roundtrip(const nlohmann::json& rules);

    // Round-trips synthetic and real rules; prints "BPTEST ..." lines. True when everything matched.
    bool selftest();

private:
    struct Pin { int id; bool input; bool exec; std::string name; };
    struct Node {
        int id;
        std::string kind;          // "on.start" | "act.impulse" | "act.raw" | ...
        ImVec2 pos;
        bool placed = false;       // has ed::SetNodePosition been applied?
        nlohmann::json params;
        nlohmann::json extra = nlohmann::json::object();   // event nodes: unknown rule keys, kept verbatim
        std::vector<Pin> pins;
        int in_exec = -1, out_exec = -1;
        std::string raw_buf;       // act.raw: text being edited
        bool raw_bad = false;      // act.raw: text is not valid JSON
    };
    struct Link { int id; int a, b; };

    ax::NodeEditor::EditorContext* ctx_ = nullptr;
    std::string target_;
    std::vector<Node> nodes_;
    std::vector<Link> links_;
    int next_id_ = 1;
    std::string applied_;          // compile() output at the last load / compile, to spot edits
    std::string status_;
    double status_until_ = 0;
    char filter_[48] = {0};        // node search box in the add menu

    int nid() { return next_id_++; }
    Node& add_node(const std::string& kind, ImVec2 pos, bool with_defaults = true);
    void load_rules(const nlohmann::json& rules);
    void load_from_behavior(CommandContext&, const std::string& target);
    nlohmann::json compile() const;
    void context_menu();
    void duplicate_selected();
    int node_of_pin(int pin) const;
    const Pin* find_pin(int pin) const;
    bool reaches(int from_node, int to_node) const;   // follows exec links
    int out_link_target(int pin) const;
};

} // namespace eng
