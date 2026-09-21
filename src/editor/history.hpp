#pragma once
#include "scene/scene.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eng {

// Delta-based undo/redo for the scene.
//
// The baseline is one serialized JSON string per entity (shared, immutable). A history
// step stores only the entities that changed (before/after), so 64 steps of "moved one
// object" in a 5000-entity scene cost a few hundred bytes each instead of 64 full-scene
// copies. commit() is meant to run when an edit gesture *ends* (not every frame);
// undo()/redo() touch only the entities in the step. No ImGui dependency.
class SceneHistory {
public:
    // Adopt the current scene as the baseline without recording a step.
    void reset(Scene& scene);
    // Diff the scene against the baseline; records a step when something changed.
    bool commit(Scene& scene);
    bool undo(Scene& scene);
    bool redo(Scene& scene);

    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }
    size_t undo_depth() const { return undo_.size(); }
    size_t step_bytes() const;        // memory held by all recorded steps (undo + redo)
    size_t baseline_bytes() const;    // memory held by the shared baseline

private:
    using Blob = std::shared_ptr<const std::string>;   // null = "did not exist"
    struct Change { std::string key; Blob before, after; };
    using Step = std::vector<Change>;
    static constexpr size_t kMaxSteps = 64;

    std::unordered_map<std::string, Blob> base_;
    std::vector<Step> undo_, redo_;
    bool have_base_ = false;

    std::unordered_map<std::string, Blob> snapshot(const Scene& scene) const;
    void apply(Scene& scene, const Step& step, bool use_after);
};

} // namespace eng
