#include "assets/gltf.hpp"
#include "assets/texture.hpp"
#include "core/log.hpp"
#include <glm/glm.hpp>
#include <filesystem>
#include <vector>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

namespace fs = std::filesystem;

namespace eng {

static void read_accessor_vec(const cgltf_accessor* acc, std::vector<float>& out, int comps) {
    out.resize(acc->count * comps);
    for (cgltf_size i = 0; i < acc->count; ++i)
        cgltf_accessor_read_float(acc, i, &out[i * comps], comps);
}

// Registers a glTF image in the Texture cache, returns the key to reference it by.
static std::string register_image(const cgltf_image* img, const cgltf_data* data,
                                  const std::string& gltf_path, int index, bool srgb) {
    if (!img) return {};
    std::string dir = fs::path(gltf_path).parent_path().string();

    if (img->uri && strncmp(img->uri, "data:", 5) != 0) {
        std::string p = (fs::path(dir) / img->uri).string();
        Texture::resolve(p, srgb);   // load + cache under the path
        return p;
    }
    // embedded via buffer view
    if (img->buffer_view) {
        const cgltf_buffer_view* bv = img->buffer_view;
        const unsigned char* ptr = (const unsigned char*)bv->buffer->data + bv->offset;
        auto tex = Texture::from_memory(ptr, (int)bv->size, srgb);
        if (!tex) return {};
        std::string key = "gltf:" + gltf_path + "#img" + std::to_string(index);
        Texture::put(key, srgb, tex);
        return key;
    }
    return {};
}

static int image_index(const cgltf_data* data, const cgltf_image* img) {
    for (cgltf_size i = 0; i < data->images_count; ++i)
        if (&data->images[i] == img) return (int)i;
    return -1;
}

std::shared_ptr<Mesh> load_gltf_mesh(const std::string& path, GltfMaterial* out_mat) {
    cgltf_options opts{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&opts, path.c_str(), &data) != cgltf_result_success) {
        log::error("gltf parse failed: %s", path.c_str());
        return nullptr;
    }
    if (cgltf_load_buffers(&opts, data, path.c_str()) != cgltf_result_success) {
        log::error("gltf buffers failed: %s", path.c_str());
        cgltf_free(data);
        return nullptr;
    }

    std::vector<Vertex> verts;
    std::vector<uint32_t> indices;
    const cgltf_material* material = nullptr;

    for (cgltf_size m = 0; m < data->meshes_count && verts.empty(); ++m) {
        const cgltf_mesh& mesh = data->meshes[m];
        for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
            const cgltf_primitive& prim = mesh.primitives[p];
            if (prim.type != cgltf_primitive_type_triangles) continue;
            if (!material) material = prim.material;

            std::vector<float> pos, nrm, uv, tan;
            for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
                const cgltf_attribute& at = prim.attributes[a];
                if (at.type == cgltf_attribute_type_position) read_accessor_vec(at.data, pos, 3);
                else if (at.type == cgltf_attribute_type_normal) read_accessor_vec(at.data, nrm, 3);
                else if (at.type == cgltf_attribute_type_texcoord && at.index == 0)
                    read_accessor_vec(at.data, uv, 2);
                else if (at.type == cgltf_attribute_type_tangent) read_accessor_vec(at.data, tan, 4);
            }
            if (pos.empty()) continue;

            uint32_t base = (uint32_t)verts.size();
            size_t n = pos.size() / 3;
            for (size_t i = 0; i < n; ++i) {
                Vertex v;
                v.pos = {pos[i*3], pos[i*3+1], pos[i*3+2]};
                if (nrm.size() >= (i+1)*3) v.normal = {nrm[i*3], nrm[i*3+1], nrm[i*3+2]};
                if (uv.size() >= (i+1)*2) v.uv = {uv[i*2], uv[i*2+1]};
                if (tan.size() >= (i+1)*4) v.tangent = {tan[i*4], tan[i*4+1], tan[i*4+2], tan[i*4+3]};
                verts.push_back(v);
            }
            if (prim.indices)
                for (cgltf_size i = 0; i < prim.indices->count; ++i)
                    indices.push_back(base + (uint32_t)cgltf_accessor_read_index(prim.indices, i));
            else
                for (size_t i = 0; i < n; ++i) indices.push_back(base + (uint32_t)i);
        }
    }

    if (out_mat && material) {
        GltfMaterial& gm = *out_mat;
        if (material->has_pbr_metallic_roughness) {
            const auto& pbr = material->pbr_metallic_roughness;
            gm.base_color = {pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]};
            gm.metallic = pbr.metallic_factor;
            gm.roughness = pbr.roughness_factor;
            if (pbr.base_color_texture.texture)
                gm.base_color_map = register_image(pbr.base_color_texture.texture->image, data, path,
                                                   image_index(data, pbr.base_color_texture.texture->image), true);
            if (pbr.metallic_roughness_texture.texture)
                gm.metallic_roughness_map = register_image(pbr.metallic_roughness_texture.texture->image, data, path,
                                                           image_index(data, pbr.metallic_roughness_texture.texture->image), false);
        }
        gm.emissive = {material->emissive_factor[0], material->emissive_factor[1], material->emissive_factor[2]};
        if (material->normal_texture.texture)
            gm.normal_map = register_image(material->normal_texture.texture->image, data, path,
                                           image_index(data, material->normal_texture.texture->image), false);
        if (material->emissive_texture.texture)
            gm.emissive_map = register_image(material->emissive_texture.texture->image, data, path,
                                             image_index(data, material->emissive_texture.texture->image), true);
        if (material->occlusion_texture.texture)
            gm.ao_map = register_image(material->occlusion_texture.texture->image, data, path,
                                       image_index(data, material->occlusion_texture.texture->image), false);
    }

    cgltf_free(data);
    if (verts.empty()) {
        log::error("gltf has no triangle geometry: %s", path.c_str());
        return nullptr;
    }
    log::info("gltf loaded: %s (%zu verts, %zu tris)", path.c_str(), verts.size(), indices.size() / 3);
    return std::make_shared<Mesh>(verts, indices);
}

} // namespace eng
