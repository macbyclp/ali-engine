#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <string>
#include <vector>

namespace eng {

// A tiny global key/value store the AI and behaviours use for game logic
// (phase, score, flags...). Plus one-shot timers that fire named events.
struct GameState {
    nlohmann::json values = nlohmann::json::object();

    struct Timer { float remaining; std::string event; };
    std::vector<Timer> timers;

    // Advances timers; appends fired event names to `out`.
    void tick(float dt, std::vector<std::string>& out) {
        for (auto& t : timers) {
            t.remaining -= dt;
            if (t.remaining <= 0.0f) out.push_back(t.event);
        }
        timers.erase(std::remove_if(timers.begin(), timers.end(),
                                    [](const Timer& t) { return t.remaining <= 0.0f; }),
                     timers.end());
    }

    // Evaluates a behaviour rule's optional "if" clause:
    //   {"key": "phase", "eq": "combat"}  |  {"key":"score","gte":100}  |  {"key":"x","exists":true}
    bool check(const nlohmann::json& cond) const {
        if (!cond.is_object() || !cond.contains("key")) return true;
        std::string k = cond["key"].get<std::string>();
        bool has = values.contains(k);
        if (cond.contains("exists")) return has == cond["exists"].get<bool>();
        if (!has) return false;
        const auto& v = values.at(k);
        if (cond.contains("eq")) return v == cond["eq"];
        if (cond.contains("ne")) return v != cond["ne"];
        if (v.is_number()) {
            double d = v.get<double>();
            if (cond.contains("gt")) return d > cond["gt"].get<double>();
            if (cond.contains("gte")) return d >= cond["gte"].get<double>();
            if (cond.contains("lt")) return d < cond["lt"].get<double>();
            if (cond.contains("lte")) return d <= cond["lte"].get<double>();
        }
        return true;
    }
};

} // namespace eng
