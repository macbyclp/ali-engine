#pragma once
#include "render/mesh.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace eng {

// Material pulled from a glTF, in the form MeshRenderer expects.
struct GltfMaterial {
    glm::vec3 base_color{1.0f};
    float metallic = 1.0f;
    float roughness = 1.0f;
    glm::vec3 emissive{0.0f};
    std::string base_color_map;          // texture keys (file path or "gltf:...#imgN")
    std::string normal_map;
    std::string metallic_roughness_map;
    std::string emissive_map;
    std::string ao_map;
};

// Loads the first mesh of a .gltf/.glb (merging primitives) plus its material.
// Referenced textures (external files or embedded images) are registered in the
// Texture cache under the returned keys.
std::shared_ptr<Mesh> load_gltf_mesh(const std::string& path, GltfMaterial* out_material = nullptr);

} // namespace eng
