#include "geo/meshdata.hpp"
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <cmath>
#include <unordered_map>

namespace eng {

void MeshData::transform(const glm::mat4& m) {
    glm::mat3 nm = glm::inverseTranspose(glm::mat3(m));
    for (auto& v : verts) {
        v.pos = glm::vec3(m * glm::vec4(v.pos, 1.0f));
        v.normal = glm::normalize(nm * v.normal);
    }
}

void MeshData::append(const MeshData& o) {
    uint32_t base = (uint32_t)verts.size();
    verts.insert(verts.end(), o.verts.begin(), o.verts.end());
    for (uint32_t i : o.idx) idx.push_back(base + i);
}

void MeshData::recompute_normals() {
    for (auto& v : verts) v.normal = glm::vec3(0);
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        Vertex& a = verts[idx[i]]; Vertex& b = verts[idx[i + 1]]; Vertex& c = verts[idx[i + 2]];
        glm::vec3 fn = glm::cross(b.pos - a.pos, c.pos - a.pos);
        a.normal += fn; b.normal += fn; c.normal += fn;
    }
    for (auto& v : verts)
        v.normal = glm::length(v.normal) > 1e-9f ? glm::normalize(v.normal) : glm::vec3(0, 1, 0);
}

void MeshData::fix_winding() {
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        const glm::vec3& a = verts[idx[i]].pos;
        const glm::vec3& b = verts[idx[i + 1]].pos;
        const glm::vec3& c = verts[idx[i + 2]].pos;
        glm::vec3 gn = glm::cross(b - a, c - a);
        glm::vec3 vn = verts[idx[i]].normal + verts[idx[i + 1]].normal + verts[idx[i + 2]].normal;
        if (glm::dot(gn, vn) < 0.0f) std::swap(idx[i + 1], idx[i + 2]);
    }
}

std::shared_ptr<Mesh> MeshData::upload() const {
    return std::make_shared<Mesh>(verts, idx);
}

// --------------------------------------------------------------------------
static void quad(MeshData& d, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 e, glm::vec3 n) {
    uint32_t base = (uint32_t)d.verts.size();
    d.verts.push_back({a, n, {0, 0}}); d.verts.push_back({b, n, {1, 0}});
    d.verts.push_back({c, n, {1, 1}}); d.verts.push_back({e, n, {0, 1}});
    d.idx.insert(d.idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

MeshData make_box(glm::vec3 size) {
    glm::vec3 h = size * 0.5f;
    MeshData d;
    quad(d, {h.x,-h.y, h.z}, {h.x,-h.y,-h.z}, {h.x, h.y,-h.z}, {h.x, h.y, h.z}, {1,0,0});
    quad(d, {-h.x,-h.y,-h.z}, {-h.x,-h.y, h.z}, {-h.x, h.y, h.z}, {-h.x, h.y,-h.z}, {-1,0,0});
    quad(d, {-h.x, h.y, h.z}, {h.x, h.y, h.z}, {h.x, h.y,-h.z}, {-h.x, h.y,-h.z}, {0,1,0});
    quad(d, {-h.x,-h.y,-h.z}, {h.x,-h.y,-h.z}, {h.x,-h.y, h.z}, {-h.x,-h.y, h.z}, {0,-1,0});
    quad(d, {-h.x,-h.y, h.z}, {h.x,-h.y, h.z}, {h.x, h.y, h.z}, {-h.x, h.y, h.z}, {0,0,1});
    quad(d, {h.x,-h.y,-h.z}, {-h.x,-h.y,-h.z}, {-h.x, h.y,-h.z}, {h.x, h.y,-h.z}, {0,0,-1});
    return d;
}

MeshData make_sphere(float r, int seg) {
    MeshData d;
    for (int y = 0; y <= seg; ++y)
        for (int x = 0; x <= seg; ++x) {
            float xs = (float)x / seg, ys = (float)y / seg;
            glm::vec3 p{std::cos(xs * glm::two_pi<float>()) * std::sin(ys * glm::pi<float>()),
                       std::cos(ys * glm::pi<float>()),
                       std::sin(xs * glm::two_pi<float>()) * std::sin(ys * glm::pi<float>())};
            d.verts.push_back({p * r, glm::normalize(p), {xs, ys}});
        }
    for (int y = 0; y < seg; ++y)
        for (int x = 0; x < seg; ++x) {
            uint32_t i0 = y * (seg + 1) + x, i1 = i0 + seg + 1;
            d.idx.insert(d.idx.end(), {i0, i1, i0 + 1, i0 + 1, i1, i1 + 1});
        }
    return d;
}

MeshData make_cylinder(float r, float hgt, int seg, bool capped) {
    MeshData d;
    float hy = hgt * 0.5f;
    for (int i = 0; i <= seg; ++i) {
        float a = (float)i / seg * glm::two_pi<float>();
        glm::vec3 nrm{std::cos(a), 0, std::sin(a)};
        d.verts.push_back({{nrm.x * r, -hy, nrm.z * r}, nrm, {(float)i / seg, 0}});
        d.verts.push_back({{nrm.x * r,  hy, nrm.z * r}, nrm, {(float)i / seg, 1}});
    }
    for (int i = 0; i < seg; ++i) {
        uint32_t b = i * 2;
        d.idx.insert(d.idx.end(), {b, b + 1, b + 2, b + 2, b + 1, b + 3});
    }
    if (capped) {
        auto cap = [&](float y, glm::vec3 nrm) {
            uint32_t c = (uint32_t)d.verts.size();
            d.verts.push_back({{0, y, 0}, nrm, {0.5f, 0.5f}});
            uint32_t ring = (uint32_t)d.verts.size();
            for (int i = 0; i <= seg; ++i) {
                float a = (float)i / seg * glm::two_pi<float>();
                d.verts.push_back({{std::cos(a) * r, y, std::sin(a) * r}, nrm,
                                   {std::cos(a) * 0.5f + 0.5f, std::sin(a) * 0.5f + 0.5f}});
            }
            for (int i = 0; i < seg; ++i) {
                // wind so the geometric normal matches `nrm` (outward)
                if (nrm.y > 0.0f) d.idx.insert(d.idx.end(), {c, ring + i + 1, ring + i});
                else              d.idx.insert(d.idx.end(), {c, ring + i, ring + i + 1});
            }
        };
        cap( hy, {0, 1, 0});
        cap(-hy, {0, -1, 0});
    }
    return d;
}

MeshData make_cone(float r, float hgt, int seg) {
    MeshData d;
    float hy = hgt * 0.5f;
    glm::vec3 apex{0, hy, 0};
    for (int i = 0; i < seg; ++i) {
        float a0 = (float)i / seg * glm::two_pi<float>();
        float a1 = (float)(i + 1) / seg * glm::two_pi<float>();
        glm::vec3 p0{std::cos(a0) * r, -hy, std::sin(a0) * r};
        glm::vec3 p1{std::cos(a1) * r, -hy, std::sin(a1) * r};
        glm::vec3 n = glm::normalize(glm::cross(apex - p0, p1 - p0));
        uint32_t b = (uint32_t)d.verts.size();
        d.verts.push_back({apex, n, {0.5f, 1}});
        d.verts.push_back({p0, n, {0, 0}});
        d.verts.push_back({p1, n, {1, 0}});
        d.idx.insert(d.idx.end(), {b + 1, b, b + 2});   // p0, apex, p1
    }
    uint32_t c = (uint32_t)d.verts.size();
    d.verts.push_back({{0, -hy, 0}, {0, -1, 0}, {0.5f, 0.5f}});
    uint32_t ring = (uint32_t)d.verts.size();
    for (int i = 0; i <= seg; ++i) {
        float a = (float)i / seg * glm::two_pi<float>();
        d.verts.push_back({{std::cos(a) * r, -hy, std::sin(a) * r}, {0, -1, 0}, {0, 0}});
    }
    for (int i = 0; i < seg; ++i) d.idx.insert(d.idx.end(), {c, ring + i, ring + i + 1});
    return d;
}

MeshData make_torus(float R, float rr, int seg, int sides) {
    MeshData d;
    for (int i = 0; i <= seg; ++i) {
        float u = (float)i / seg * glm::two_pi<float>();
        for (int j = 0; j <= sides; ++j) {
            float v = (float)j / sides * glm::two_pi<float>();
            glm::vec3 center{std::cos(u) * R, 0, std::sin(u) * R};
            glm::vec3 p{(R + rr * std::cos(v)) * std::cos(u), rr * std::sin(v),
                       (R + rr * std::cos(v)) * std::sin(u)};
            d.verts.push_back({p, glm::normalize(p - center), {(float)i / seg, (float)j / sides}});
        }
    }
    int stride = sides + 1;
    for (int i = 0; i < seg; ++i)
        for (int j = 0; j < sides; ++j) {
            uint32_t a = i * stride + j, b = (i + 1) * stride + j;
            d.idx.insert(d.idx.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    return d;
}

MeshData make_plane(glm::vec2 size, int sub) {
    MeshData d;
    sub = std::max(1, sub);
    glm::vec2 h = size * 0.5f;
    for (int y = 0; y <= sub; ++y)
        for (int x = 0; x <= sub; ++x) {
            float fx = (float)x / sub, fy = (float)y / sub;
            d.verts.push_back({{-h.x + fx * size.x, 0, -h.y + fy * size.y}, {0, 1, 0}, {fx, fy}});
        }
    for (int y = 0; y < sub; ++y)
        for (int x = 0; x < sub; ++x) {
            uint32_t i0 = y * (sub + 1) + x, i1 = i0 + sub + 1;
            d.idx.insert(d.idx.end(), {i0, i1 + 1, i1, i0, i0 + 1, i1 + 1});
        }
    return d;
}

} // namespace eng
