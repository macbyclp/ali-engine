#pragma once
#include "geo/meshdata.hpp"

namespace eng {

// Constructive solid geometry on triangle meshes (BSP method, after csg.js).
// Inputs should be closed manifolds for clean results.
enum class CsgOp { Union, Subtract, Intersect };
MeshData csg(const MeshData& a, const MeshData& b, CsgOp op);

} // namespace eng
