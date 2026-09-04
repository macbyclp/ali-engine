#pragma once
#include "audio/audio.hpp"
#include "game/gamestate.hpp"
#include "input/input.hpp"
#include "physics/physics_system.hpp"
#include "scene/scene.hpp"
#include <string>
#include <vector>

namespace eng {

// Interprets Behavior components each simulation step. Data-driven: an AI emits
// JSON rules ({on, do}) and this executes them against the ECS + physics + state.
class BehaviorSystem {
public:
    void tick(Scene& scene, PhysicsSystem& physics, GameState& gs, float dt);
    void emit(const std::string& event) { pending_events_.push_back(event); }
    void reset() { pending_events_.clear(); }
    // Optional: enables the `on: input / inputPressed / inputReleased` triggers.
    void set_input(InputSystem* in) { input_ = in; }
    // Optional: enables the `sound` action.
    void set_audio(AudioEngine* a) { audio_ = a; }

private:
    std::vector<std::string> pending_events_;
    InputSystem* input_ = nullptr;
    AudioEngine* audio_ = nullptr;

    void run_rules(Scene&, PhysicsSystem&, GameState&, entt::entity self, const char* trigger,
                   const std::string& other, float dt,
                   std::vector<entt::entity>& to_destroy,
                   std::vector<nlohmann::json>& to_spawn);
    void run_actions(Scene&, PhysicsSystem&, GameState&, entt::entity self,
                     const nlohmann::json& actions, const std::string& other, float dt,
                     std::vector<entt::entity>& to_destroy,
                     std::vector<nlohmann::json>& to_spawn);
};

} // namespace eng
