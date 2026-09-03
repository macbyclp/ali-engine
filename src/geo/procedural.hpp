#pragma once
#include "geo/meshdata.hpp"
#include <nlohmann/json.hpp>

namespace eng {

// Evaluate a procedural-mesh recipe. `build` is a JSON array of steps:
//   {"op":"add|subtract|intersect", "shape":"box|sphere|cylinder|cone|torus|plane",
//    ...shape params..., "translate":[x,y,z], "rotate":[x,y,z], "scale":[x,y,z]}
// The first step seeds the accumulator (its op is ignored); each later step is
// combined into it. Returns an empty MeshData on an empty/invalid recipe.
MeshData build_procedural(const nlohmann::json& build);

} // namespace eng
