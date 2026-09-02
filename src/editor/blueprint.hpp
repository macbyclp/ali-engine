#pragma once
#include "aicontrol/commands.hpp"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace ax::NodeEditor { struct EditorContext; }

namespace eng {

// A Blueprint-style visual scripting graph. Event nodes (start/tick/collision/
// event) chain into action nodes via exec pins; "Compile" turns the graph into
// the same Behavior JSON the AI writes and applies it to the selected entity.
class BlueprintEditor {
public:
    BlueprintEditor();
    ~BlueprintEditor();

    // Draws the graph panel for `target` (an entity name). Rebuilds the graph
    // from that entity's Behavior when the target changes.
    void draw(CommandContext& ctx, const std::string& target);

private:
    struct Pin { int id; bool input; bool exec; std::string name; };
    struct Node {
        int id;
        std::string kind;          // "on.start" | "act.impulse" | ...
        ImVec2 pos;
        bool placed = false;       // has ed::SetNodePosition been applied?
        nlohmann::json params;
        std::vector<Pin> pins;
        int in_exec = -1, out_exec = -1;
    };
    struct Link { int id; int a, b; };

    ax::NodeEditor::EditorContext* ctx_ = nullptr;
    std::string target_;
    std::vector<Node> nodes_;
    std::vector<Link> links_;
    int next_id_ = 1;

    int nid() { return next_id_++; }
    Node& add_node(const std::string& kind, ImVec2 pos);
    void load_from_behavior(CommandContext&, const std::string& target);
    nlohmann::json compile() const;
    void context_menu();
    const Node* node_by_out_pin(int pin) const;
    int out_link_target(int pin) const;
};

} // namespace eng
