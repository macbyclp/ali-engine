#include "assets/gltf.hpp"
#include "core/log.hpp"
#include <glm/glm.hpp>
#include <vector>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

namespace eng {

static void read_accessor_vec(const cgltf_accessor* acc, std::vector<float>& out, int comps) {
    out.resize(acc->count * comps);
    for (cgltf_size i = 0; i < acc->count; ++i)
        cgltf_accessor_read_float(acc, i, &out[i * comps], comps);
}

std::shared_ptr<Mesh> load_gltf_mesh(const std::string& path) {
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

    for (cgltf_size m = 0; m < data->meshes_count && verts.empty(); ++m) {
        const cgltf_mesh& mesh = data->meshes[m];
        for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
            const cgltf_primitive& prim = mesh.primitives[p];
            if (prim.type != cgltf_primitive_type_triangles) continue;

            std::vector<float> pos, nrm, uv;
            for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
                const cgltf_attribute& at = prim.attributes[a];
                if (at.type == cgltf_attribute_type_position) read_accessor_vec(at.data, pos, 3);
                else if (at.type == cgltf_attribute_type_normal) read_accessor_vec(at.data, nrm, 3);
                else if (at.type == cgltf_attribute_type_texcoord && at.index == 0)
                    read_accessor_vec(at.data, uv, 2);
            }
            if (pos.empty()) continue;

            uint32_t base = static_cast<uint32_t>(verts.size());
            size_t n = pos.size() / 3;
            for (size_t i = 0; i < n; ++i) {
                Vertex v;
                v.pos = {pos[i*3], pos[i*3+1], pos[i*3+2]};
                if (nrm.size() >= (i+1)*3) v.normal = {nrm[i*3], nrm[i*3+1], nrm[i*3+2]};
                if (uv.size() >= (i+1)*2) v.uv = {uv[i*2], uv[i*2+1]};
                verts.push_back(v);
            }
            if (prim.indices) {
                for (cgltf_size i = 0; i < prim.indices->count; ++i)
                    indices.push_back(base + (uint32_t)cgltf_accessor_read_index(prim.indices, i));
            } else {
                for (size_t i = 0; i < n; ++i) indices.push_back(base + (uint32_t)i);
            }
        }
    }
    cgltf_free(data);

    if (verts.empty()) {
        log::error("gltf has no triangle geometry: %s", path.c_str());
        return nullptr;
    }
    log::info("gltf loaded: %s (%zu verts, %zu indices)", path.c_str(), verts.size(), indices.size());
    return std::make_shared<Mesh>(verts, indices);
}

} // namespace eng
