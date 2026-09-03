#pragma once
#include "render/mesh.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace eng {

// A CPU-side triangle mesh: the currency for procedural generation and CSG,
// before it is uploaded to a GPU Mesh.
struct MeshData {
    std::vector<Vertex> verts;
    std::vector<uint32_t> idx;

    void transform(const glm::mat4& m);
    void append(const MeshData& o);
    void recompute_normals();
    // Flip any triangle whose geometric normal disagrees with its vertex
    // normals, so the whole mesh winds consistently outward (CSG needs this).
    void fix_winding();
    std::shared_ptr<Mesh> upload() const;
};

// ---- parametric primitives (unit-ish, centred at origin) ----
MeshData make_box(glm::vec3 size);
MeshData make_sphere(float radius, int segs = 24);
MeshData make_cylinder(float radius, float height, int segs = 24, bool capped = true);
MeshData make_cone(float radius, float height, int segs = 24);
MeshData make_torus(float radius, float tube, int segs = 32, int sides = 16);
MeshData make_plane(glm::vec2 size, int subdiv = 1);

} // namespace eng
