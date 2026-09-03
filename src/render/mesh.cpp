#include "render/mesh.hpp"
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace eng {

// Per-vertex tangents from UV gradients (Lengyel's method). Overwrites tangents.
static void compute_tangents(std::vector<Vertex>& v, const std::vector<uint32_t>& idx) {
    std::vector<glm::vec3> tan(v.size(), glm::vec3(0)), bit(v.size(), glm::vec3(0));
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        uint32_t a = idx[i], b = idx[i + 1], c = idx[i + 2];
        glm::vec3 e1 = v[b].pos - v[a].pos, e2 = v[c].pos - v[a].pos;
        glm::vec2 d1 = v[b].uv - v[a].uv, d2 = v[c].uv - v[a].uv;
        float f = d1.x * d2.y - d2.x * d1.y;
        if (std::abs(f) < 1e-8f) continue;
        f = 1.0f / f;
        glm::vec3 t = f * (d2.y * e1 - d1.y * e2);
        glm::vec3 bt = f * (d1.x * e2 - d2.x * e1);
        for (uint32_t k : {a, b, c}) { tan[k] += t; bit[k] += bt; }
    }
    for (size_t i = 0; i < v.size(); ++i) {
        glm::vec3 n = v[i].normal;
        glm::vec3 t = tan[i];
        if (glm::length(t) < 1e-8f) t = glm::abs(n.y) < 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1);
        t = glm::normalize(t - n * glm::dot(n, t));
        float w = (glm::dot(glm::cross(n, t), bit[i]) < 0.0f) ? -1.0f : 1.0f;
        v[i].tangent = glm::vec4(t, w);
    }
}

Mesh::Mesh(const std::vector<Vertex>& verts_in, const std::vector<uint32_t>& indices) {
    std::vector<Vertex> verts = verts_in;
    compute_tangents(verts, indices);
    count_ = static_cast<int>(indices.size());

    glm::vec3 lo(1e9f), hi(-1e9f);
    for (const auto& v : verts) { lo = glm::min(lo, v.pos); hi = glm::max(hi, v.pos); }
    if (!verts.empty()) {
        bc_ = 0.5f * (lo + hi);
        he_ = 0.5f * (hi - lo);
        float r = 0.0f;
        for (const auto& v : verts) r = glm::max(r, glm::length(v.pos - bc_));
        br_ = r;
    }

    glCreateVertexArrays(1, &vao_);
    glCreateBuffers(1, &vbo_);
    glCreateBuffers(1, &ebo_);
    glNamedBufferStorage(vbo_, verts.size() * sizeof(Vertex), verts.data(), 0);
    glNamedBufferStorage(ebo_, indices.size() * sizeof(uint32_t), indices.data(), 0);

    glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(Vertex));
    glVertexArrayElementBuffer(vao_, ebo_);
    for (unsigned i = 0; i < 3; ++i) glEnableVertexArrayAttrib(vao_, i);
    glVertexArrayAttribFormat(vao_, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, pos));
    glVertexArrayAttribFormat(vao_, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, normal));
    glVertexArrayAttribFormat(vao_, 2, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, uv));
    for (unsigned i = 0; i < 3; ++i) glVertexArrayAttribBinding(vao_, i, 0);
}

void Mesh::release() {
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    vao_ = vbo_ = ebo_ = 0;
    count_ = 0;
}
Mesh::~Mesh() { release(); }
Mesh::Mesh(Mesh&& o) noexcept
    : vao_(o.vao_), vbo_(o.vbo_), ebo_(o.ebo_), count_(o.count_) {
    o.vao_ = o.vbo_ = o.ebo_ = 0; o.count_ = 0;
}
Mesh& Mesh::operator=(Mesh&& o) noexcept {
    if (this != &o) {
        release();
        vao_ = o.vao_; vbo_ = o.vbo_; ebo_ = o.ebo_; count_ = o.count_;
        o.vao_ = o.vbo_ = o.ebo_ = 0; o.count_ = 0;
    }
    return *this;
}

void Mesh::draw() const {
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, count_, GL_UNSIGNED_INT, nullptr);
}

std::shared_ptr<Mesh> Mesh::cube() {
    const glm::vec3 n[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    const float h = 0.5f;
    std::vector<Vertex> v;
    std::vector<uint32_t> idx;
    auto face = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 nn) {
        uint32_t base = static_cast<uint32_t>(v.size());
        v.push_back({a, nn, {0,0}}); v.push_back({b, nn, {1,0}});
        v.push_back({c, nn, {1,1}}); v.push_back({d, nn, {0,1}});
        idx.insert(idx.end(), {base, base+1, base+2, base, base+2, base+3});
    };
    // Each quad wound CCW as seen from outside (matches GL default front face).
    face({ h,-h, h},{ h,-h,-h},{ h, h,-h},{ h, h, h}, n[0]); // +X
    face({-h,-h,-h},{-h,-h, h},{-h, h, h},{-h, h,-h}, n[1]); // -X
    face({-h, h, h},{ h, h, h},{ h, h,-h},{-h, h,-h}, n[2]); // +Y
    face({-h,-h,-h},{ h,-h,-h},{ h,-h, h},{-h,-h, h}, n[3]); // -Y
    face({-h,-h, h},{ h,-h, h},{ h, h, h},{-h, h, h}, n[4]); // +Z
    face({ h,-h,-h},{-h,-h,-h},{-h, h,-h},{ h, h,-h}, n[5]); // -Z
    return std::make_shared<Mesh>(v, idx);
}

std::shared_ptr<Mesh> Mesh::sphere(int seg) {
    std::vector<Vertex> v;
    std::vector<uint32_t> idx;
    for (int y = 0; y <= seg; ++y) {
        for (int x = 0; x <= seg; ++x) {
            float xs = (float)x / seg, ys = (float)y / seg;
            float px = std::cos(xs * glm::two_pi<float>()) * std::sin(ys * glm::pi<float>());
            float py = std::cos(ys * glm::pi<float>());
            float pz = std::sin(xs * glm::two_pi<float>()) * std::sin(ys * glm::pi<float>());
            glm::vec3 p{px, py, pz};
            v.push_back({p * 0.5f, glm::normalize(p), {xs, ys}});
        }
    }
    for (int y = 0; y < seg; ++y) {
        for (int x = 0; x < seg; ++x) {
            uint32_t i0 = y * (seg + 1) + x;
            uint32_t i1 = i0 + seg + 1;
            idx.insert(idx.end(), {i0, i1, i0 + 1, i0 + 1, i1, i1 + 1});
        }
    }
    return std::make_shared<Mesh>(v, idx);
}

std::shared_ptr<Mesh> Mesh::plane(float s) {
    std::vector<Vertex> v = {
        {{-s,0,-s},{0,1,0},{0,0}}, {{ s,0,-s},{0,1,0},{1,0}},
        {{ s,0, s},{0,1,0},{1,1}}, {{-s,0, s},{0,1,0},{0,1}},
    };
    std::vector<uint32_t> idx = {0, 2, 1, 0, 3, 2};
    return std::make_shared<Mesh>(v, idx);
}

} // namespace eng
