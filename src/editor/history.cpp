#include "editor/history.hpp"

using nlohmann::json;

namespace eng {

// environment / input map are not entities; they get reserved keys no entity name can use
static const char* kEnvKey = "\x01" "environment";
static const char* kInputKey = "\x01" "input";

std::unordered_map<std::string, SceneHistory::Blob> SceneHistory::snapshot(const Scene& scene) const {
    std::unordered_map<std::string, Blob> m;
    for (auto [e, n] : scene.registry.view<Name>().each())
        m[n.value] = std::make_shared<const std::string>(scene.entity_json(e).dump());
    if (scene.env.is_object() && !scene.env.empty())
        m[kEnvKey] = std::make_shared<const std::string>(scene.env.dump());
    if (scene.input_map.is_object() && !scene.input_map.empty())
        m[kInputKey] = std::make_shared<const std::string>(scene.input_map.dump());
    return m;
}

void SceneHistory::reset(Scene& scene) {
    base_ = snapshot(scene);
    have_base_ = true;
}

bool SceneHistory::commit(Scene& scene) {
    if (!have_base_) { reset(scene); return false; }
    auto cur = snapshot(scene);
    Step step;
    for (auto& [k, blob] : cur) {
        auto it = base_.find(k);
        if (it == base_.end()) step.push_back({k, nullptr, blob});
        else if (*it->second != *blob) step.push_back({k, it->second, blob});
        else blob = it->second;   // unchanged: keep sharing the old string
    }
    for (auto& [k, blob] : base_)
        if (!cur.count(k)) step.push_back({k, blob, nullptr});
    base_ = std::move(cur);
    if (step.empty()) return false;
    undo_.push_back(std::move(step));
    if (undo_.size() > kMaxSteps) undo_.erase(undo_.begin());
    redo_.clear();
    return true;
}

void SceneHistory::apply(Scene& scene, const Step& step, bool use_after) {
    // pass 1: remove every entity the step touches, pass 2: recreate the wanted ones
    // (so parent links / name uniqueness never see half-applied state)
    for (const auto& c : step) {
        if (c.key == kEnvKey || c.key == kInputKey) continue;
        scene.destroy(c.key);
    }
    for (const auto& c : step) {
        const Blob& want = use_after ? c.after : c.before;
        if (c.key == kEnvKey) { scene.env = want ? json::parse(*want) : json::object(); continue; }
        if (c.key == kInputKey) { scene.input_map = want ? json::parse(*want) : json::object(); continue; }
        if (want) {
            entt::entity e = scene.load_entity(json::parse(*want));
            scene.resolve_gpu_mesh(e);
            // baseline = what the live entity serializes to now, so a re-serialization
            // difference can never show up as a phantom edit on the next commit
            base_[c.key] = std::make_shared<const std::string>(scene.entity_json(e).dump());
        } else {
            base_.erase(c.key);
        }
    }
    for (const auto& c : step) {
        if (c.key != kEnvKey && c.key != kInputKey) continue;
        const Blob& want = use_after ? c.after : c.before;
        if (want) base_[c.key] = want; else base_.erase(c.key);
    }
}

bool SceneHistory::undo(Scene& scene) {
    if (undo_.empty()) return false;
    Step step = std::move(undo_.back());
    undo_.pop_back();
    apply(scene, step, /*use_after=*/false);
    redo_.push_back(std::move(step));
    return true;
}

bool SceneHistory::redo(Scene& scene) {
    if (redo_.empty()) return false;
    Step step = std::move(redo_.back());
    redo_.pop_back();
    apply(scene, step, /*use_after=*/true);
    undo_.push_back(std::move(step));
    return true;
}

size_t SceneHistory::step_bytes() const {
    size_t n = 0;
    auto count = [&](const std::vector<Step>& v) {
        for (const auto& s : v)
            for (const auto& c : s) {
                n += c.key.size();
                if (c.before) n += c.before->size();
                if (c.after) n += c.after->size();
            }
    };
    count(undo_); count(redo_);
    return n;
}

size_t SceneHistory::baseline_bytes() const {
    size_t n = 0;
    for (const auto& [k, b] : base_) n += k.size() + (b ? b->size() : 0);
    return n;
}

} // namespace eng
