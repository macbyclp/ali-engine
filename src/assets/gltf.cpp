#include "assets/gltf.hpp"
#include "assets/texture.hpp"
#include "core/log.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
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

    // Build a mesh -> world-transform map by walking the node tree, so multi-part
    // models keep their layout instead of collapsing every mesh to the origin.
    std::vector<glm::mat4> mesh_xform(data->meshes_count, glm::mat4(1.0f));
    std::vector<char> mesh_seen(data->meshes_count, 0);
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        const cgltf_node& node = data->nodes[i];
        if (!node.mesh) continue;
        cgltf_size mi = cgltf_mesh_index(data, node.mesh);
        float w[16];
        cgltf_node_transform_world(&node, w);
        mesh_xform[mi] = glm::make_mat4(w);
        mesh_seen[mi] = 1;
    }

    for (cgltf_size m = 0; m < data->meshes_count; ++m) {
        const cgltf_mesh& mesh = data->meshes[m];
        glm::mat4 xf = mesh_xform[m];
        glm::mat3 nxf = glm::mat3(glm::inverseTranspose(xf));
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
                glm::vec3 p{pos[i*3], pos[i*3+1], pos[i*3+2]};
                v.pos = glm::vec3(xf * glm::vec4(p, 1.0f));
                if (nrm.size() >= (i+1)*3)
                    v.normal = glm::normalize(nxf * glm::vec3(nrm[i*3], nrm[i*3+1], nrm[i*3+2]));
                if (uv.size() >= (i+1)*2) v.uv = {uv[i*2], uv[i*2+1]};
                if (tan.size() >= (i+1)*4) {
                    glm::vec3 t = glm::normalize(glm::mat3(xf) * glm::vec3(tan[i*4], tan[i*4+1], tan[i*4+2]));
                    v.tangent = glm::vec4(t, tan[i*4+3]);
                }
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

// ---- skinned ----
std::shared_ptr<SkinnedModel> load_gltf_skinned(const std::string& path) {
    cgltf_options opts{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&opts, path.c_str(), &data) != cgltf_result_success ||
        cgltf_load_buffers(&opts, data, path.c_str()) != cgltf_result_success) {
        log::error("gltf skinned load failed: %s", path.c_str());
        if (data) cgltf_free(data);
        return nullptr;
    }
    if (data->skins_count == 0) { cgltf_free(data); return nullptr; }

    const cgltf_skin& skin = data->skins[0];
    int nj = (int)skin.joints_count;
    auto model = std::make_shared<SkinnedModel>();
    Skeleton& sk = model->skeleton;
    sk.parents.assign(nj, -1);
    sk.bind_t.resize(nj); sk.bind_r.resize(nj); sk.bind_s.assign(nj, glm::vec3(1));
    sk.inverse_bind.resize(nj);
    sk.names.resize(nj);

    auto joint_index = [&](const cgltf_node* n) -> int {
        for (int j = 0; j < nj; ++j) if (skin.joints[j] == n) return j;
        return -1;
    };
    for (int j = 0; j < nj; ++j) {
        const cgltf_node* jn = skin.joints[j];
        sk.names[j] = jn->name ? jn->name : ("joint" + std::to_string(j));
        sk.parents[j] = jn->parent ? joint_index(jn->parent) : -1;
        glm::vec3 t(0), s(1); glm::quat r(1, 0, 0, 0);
        if (jn->has_translation) t = {jn->translation[0], jn->translation[1], jn->translation[2]};
        if (jn->has_rotation) r = {jn->rotation[3], jn->rotation[0], jn->rotation[1], jn->rotation[2]};
        if (jn->has_scale) s = {jn->scale[0], jn->scale[1], jn->scale[2]};
        sk.bind_t[j] = t; sk.bind_r[j] = r; sk.bind_s[j] = s;
        float m[16];
        if (skin.inverse_bind_matrices)
            cgltf_accessor_read_float(skin.inverse_bind_matrices, j, m, 16);
        sk.inverse_bind[j] = skin.inverse_bind_matrices ? glm::make_mat4(m) : glm::mat4(1.0f);
    }

    // geometry with JOINTS_0 / WEIGHTS_0
    std::vector<Vertex> verts;
    std::vector<uint32_t> indices;
    for (cgltf_size m = 0; m < data->meshes_count && verts.empty(); ++m) {
        for (cgltf_size p = 0; p < data->meshes[m].primitives_count; ++p) {
            const cgltf_primitive& prim = data->meshes[m].primitives[p];
            if (prim.type != cgltf_primitive_type_triangles) continue;
            std::vector<float> pos, nrm, uv, jw, ww;
            for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
                const cgltf_attribute& at = prim.attributes[a];
                if (at.type == cgltf_attribute_type_position) read_accessor_vec(at.data, pos, 3);
                else if (at.type == cgltf_attribute_type_normal) read_accessor_vec(at.data, nrm, 3);
                else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) read_accessor_vec(at.data, uv, 2);
                else if (at.type == cgltf_attribute_type_joints && at.index == 0) read_accessor_vec(at.data, jw, 4);
                else if (at.type == cgltf_attribute_type_weights && at.index == 0) read_accessor_vec(at.data, ww, 4);
            }
            if (pos.empty()) continue;
            uint32_t base = (uint32_t)verts.size();
            size_t n = pos.size() / 3;
            for (size_t i = 0; i < n; ++i) {
                Vertex v;
                v.pos = {pos[i*3], pos[i*3+1], pos[i*3+2]};
                if (nrm.size() >= (i+1)*3) v.normal = {nrm[i*3], nrm[i*3+1], nrm[i*3+2]};
                if (uv.size() >= (i+1)*2) v.uv = {uv[i*2], uv[i*2+1]};
                if (jw.size() >= (i+1)*4)
                    v.joints = glm::ivec4((int)jw[i*4], (int)jw[i*4+1], (int)jw[i*4+2], (int)jw[i*4+3]);
                if (ww.size() >= (i+1)*4)
                    v.weights = {ww[i*4], ww[i*4+1], ww[i*4+2], ww[i*4+3]};
                verts.push_back(v);
            }
            if (prim.indices)
                for (cgltf_size i = 0; i < prim.indices->count; ++i)
                    indices.push_back(base + (uint32_t)cgltf_accessor_read_index(prim.indices, i));
        }
    }
    if (verts.empty()) { cgltf_free(data); return nullptr; }
    model->mesh = std::make_shared<Mesh>(verts, indices);

    // animations
    for (cgltf_size ai = 0; ai < data->animations_count; ++ai) {
        const cgltf_animation& ga = data->animations[ai];
        AnimationClip clip;
        clip.name = ga.name ? ga.name : ("clip" + std::to_string(ai));
        for (cgltf_size ci = 0; ci < ga.channels_count; ++ci) {
            const cgltf_animation_channel& gc = ga.channels[ci];
            int jidx = joint_index(gc.target_node);
            if (jidx < 0) continue;
            AnimChannel ch;
            ch.joint = jidx;
            ch.path = gc.target_path == cgltf_animation_path_type_translation ? 0
                    : gc.target_path == cgltf_animation_path_type_rotation ? 1
                    : gc.target_path == cgltf_animation_path_type_scale ? 2 : -1;
            if (ch.path < 0) continue;
            const cgltf_accessor* in = gc.sampler->input;
            const cgltf_accessor* out = gc.sampler->output;
            int comps = ch.path == 1 ? 4 : 3;
            std::vector<float> times, vals;
            read_accessor_vec(in, times, 1);
            read_accessor_vec(out, vals, comps);
            for (size_t k = 0; k < times.size(); ++k) {
                ch.times.push_back(times[k]);
                glm::vec4 val(0);
                for (int c = 0; c < comps; ++c) val[c] = vals[k * comps + c];
                ch.values.push_back(val);
                clip.duration = std::max(clip.duration, times[k]);
            }
            clip.channels.push_back(std::move(ch));
        }
        if (!clip.channels.empty()) model->clips[clip.name] = std::move(clip);
    }

    cgltf_free(data);
    log::info("gltf skinned: %s (%d joints, %zu clips)", path.c_str(), nj, model->clips.size());
    return model;
}

} // namespace eng
