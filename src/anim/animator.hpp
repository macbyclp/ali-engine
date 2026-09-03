#pragma once
#include "scene/scene.hpp"
#include <nlohmann/json.hpp>

namespace eng {

// Ticks every AnimatorController: evaluates transitions against the current
// state + parameters and, when one fires, swaps AnimationPlayer.clip with a
// cross-fade. Call before update_animations() so the swap lands the same frame.
void update_animators(Scene& scene, float dt);

// Scene (de)serialisation for the "animator" entity block.
AnimatorController animator_from_json(const nlohmann::json& j);
nlohmann::json animator_to_json(const AnimatorController& c);

} // namespace eng
