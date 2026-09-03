#include "anim/animator.hpp"
#include "anim/animation_system.hpp"
#include <cmath>

namespace eng {
using nlohmann::json;

static bool cond_holds(const AnimCondition& c,
                       const std::unordered_map<std::string, float>& params) {
    auto it = params.find(c.param);
    float v = it == params.end() ? 0.0f : it->second;
    if (c.op == "trigger") return v != 0.0f;
    if (c.op == ">")  return v >  c.value;
    if (c.op == "<")  return v <  c.value;
    if (c.op == ">=") return v >= c.value;
    if (c.op == "<=") return v <= c.value;
    if (c.op == "==") return v == c.value;
    if (c.op == "!=") return v != c.value;
    return false;
}

static void enter_state(AnimationPlayer& ap, const AnimState& s, float blend) {
    if (blend > 0.0f && !ap.clip.empty() && ap.clip != s.clip) {
        ap.prev_clip = ap.clip;
        ap.prev_time = ap.time;
        ap.fade_dur = blend;
        ap.fade_left = blend;
    }
    ap.clip = s.clip;
    ap.time = 0.0f;
    ap.speed = s.speed;
    ap.loop = s.loop;
    ap.playing = true;
}

void update_animators(Scene& scene, float dt) {
    (void)dt;
    for (auto [e, mr, ctrl, ap] :
         scene.registry.view<MeshRenderer, AnimatorController, AnimationPlayer>().each()) {
        if (ctrl.states.empty()) continue;

        if (!ctrl.started) {
            ctrl.current = ctrl.entry.empty() ? ctrl.states.front().name : ctrl.entry;
            if (const AnimState* s = ctrl.state(ctrl.current)) enter_state(ap, *s, 0.0f);
            ctrl.started = true;
        }

        // normalized progress through the current clip (for exit_time gates)
        float norm = 0.0f;
        if (mr.skinned) {
            auto ci = mr.skinned->clips.find(ap.clip);
            if (ci != mr.skinned->clips.end() && ci->second.duration > 0.0f)
                norm = ap.time / ci->second.duration;
        }

        for (const AnimTransition& t : ctrl.transitions) {
            bool any = t.from.empty() || t.from == "*";
            if (!any && t.from != ctrl.current) continue;
            if (t.to == ctrl.current) continue;
            if (t.exit_time > 0.0f && norm < t.exit_time) continue;
            bool all = true;
            for (const AnimCondition& c : t.when)
                if (!cond_holds(c, ctrl.params)) { all = false; break; }
            if (!all) continue;

            const AnimState* ns = ctrl.state(t.to);
            if (!ns) continue;
            enter_state(ap, *ns, t.blend);
            ctrl.current = t.to;
            for (const AnimCondition& c : t.when)          // consume triggers
                if (c.op == "trigger") ctrl.params[c.param] = 0.0f;
            break;
        }
    }
}

// ------------------------------ serialisation ------------------------------
AnimatorController animator_from_json(const json& j) {
    AnimatorController c;
    c.entry = j.value("entry", std::string());
    for (const auto& js : j.value("states", json::array())) {
        AnimState s;
        s.name = js.value("name", std::string());
        s.clip = js.value("clip", s.name);
        s.speed = js.value("speed", 1.0f);
        s.loop = js.value("loop", true);
        c.states.push_back(std::move(s));
    }
    for (const auto& jt : j.value("transitions", json::array())) {
        AnimTransition t;
        t.from = jt.value("from", std::string());
        t.to = jt.value("to", std::string());
        t.blend = jt.value("blend", 0.15f);
        t.exit_time = jt.value("exit_time", 0.0f);
        for (const auto& jc : jt.value("when", json::array())) {
            AnimCondition cc;
            cc.param = jc.value("param", std::string());
            cc.op = jc.value("op", std::string(">"));
            cc.value = jc.value("value", 0.0f);
            t.when.push_back(std::move(cc));
        }
        c.transitions.push_back(std::move(t));
    }
    if (j.contains("params") && j["params"].is_object())
        for (auto& [k, v] : j["params"].items())
            c.params[k] = v.is_boolean() ? (v.get<bool>() ? 1.0f : 0.0f) : v.get<float>();
    return c;
}

json animator_to_json(const AnimatorController& c) {
    json j;
    if (!c.entry.empty()) j["entry"] = c.entry;
    j["states"] = json::array();
    for (const auto& s : c.states)
        j["states"].push_back({{"name", s.name}, {"clip", s.clip},
                               {"speed", s.speed}, {"loop", s.loop}});
    j["transitions"] = json::array();
    for (const auto& t : c.transitions) {
        json jt{{"to", t.to}, {"blend", t.blend}};
        if (!t.from.empty()) jt["from"] = t.from;
        if (t.exit_time > 0.0f) jt["exit_time"] = t.exit_time;
        jt["when"] = json::array();
        for (const auto& cc : t.when)
            jt["when"].push_back({{"param", cc.param}, {"op", cc.op}, {"value", cc.value}});
        j["transitions"].push_back(std::move(jt));
    }
    if (!c.params.empty()) {
        j["params"] = json::object();
        for (auto& [k, v] : c.params) j["params"][k] = v;
    }
    return j;
}

} // namespace eng
