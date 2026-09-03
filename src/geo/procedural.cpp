#include "geo/procedural.hpp"
#include "geo/csg.hpp"
#include "core/log.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace eng {
using nlohmann::json;

static glm::vec3 v3(const json& j, glm::vec3 def) {
    if (j.is_array() && j.size() == 3)
        return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
    return def;
}

static MeshData one_shape(const json& s) {
    std::string shape = s.value("shape", std::string("box"));
    MeshData d;
    if (shape == "box")
        d = make_box(v3(s.value("size", json()), glm::vec3(1)));
    else if (shape == "sphere")
        d = make_sphere(s.value("radius", 0.5f), s.value("segments", 24));
    else if (shape == "cylinder")
        d = make_cylinder(s.value("radius", 0.5f), s.value("height", 1.0f),
                          s.value("segments", 24), s.value("capped", true));
    else if (shape == "cone")
        d = make_cone(s.value("radius", 0.5f), s.value("height", 1.0f), s.value("segments", 24));
    else if (shape == "torus")
        d = make_torus(s.value("radius", 0.5f), s.value("tube", 0.15f),
                       s.value("segments", 32), s.value("sides", 16));
    else if (shape == "plane") {
        glm::vec3 sz = v3(s.value("size", json()), glm::vec3(1));
        d = make_plane({sz.x, sz.z != 0 ? sz.z : sz.y}, s.value("subdiv", 1));
    } else {
        log::warn("procedural: unknown shape '%s'", shape.c_str());
        return d;
    }

    glm::mat4 m(1.0f);
    m = glm::translate(m, v3(s.value("translate", json()), glm::vec3(0)));
    glm::vec3 rot = v3(s.value("rotate", json()), glm::vec3(0));
    if (rot != glm::vec3(0)) {
        m = glm::rotate(m, glm::radians(rot.y), {0, 1, 0});
        m = glm::rotate(m, glm::radians(rot.x), {1, 0, 0});
        m = glm::rotate(m, glm::radians(rot.z), {0, 0, 1});
    }
    m = glm::scale(m, v3(s.value("scale", json()), glm::vec3(1)));
    d.fix_winding();
    d.transform(m);
    return d;
}

MeshData build_procedural(const json& build) {
    if (!build.is_array() || build.empty()) return {};
    MeshData acc = one_shape(build[0]);
    for (size_t i = 1; i < build.size(); ++i) {
        MeshData nxt = one_shape(build[i]);
        if (nxt.verts.empty()) continue;
        std::string op = build[i].value("op", std::string("add"));
        if (op == "subtract") acc = csg(acc, nxt, CsgOp::Subtract);
        else if (op == "intersect") acc = csg(acc, nxt, CsgOp::Intersect);
        else if (op == "union" || op == "add") acc = csg(acc, nxt, CsgOp::Union);
        else acc.append(nxt);   // "merge": cheap concat, no boolean
    }
    return acc;
}

} // namespace eng
