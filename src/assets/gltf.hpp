#pragma once
#include "render/mesh.hpp"
#include <memory>
#include <string>

namespace eng {

// Loads the first mesh of a .gltf/.glb, merging its primitives. M1: geometry only
// (position/normal/uv + indices), no materials or textures yet.
std::shared_ptr<Mesh> load_gltf_mesh(const std::string& path);

} // namespace eng
