#include "engine/mesh.hpp"

namespace eng {

Mesh::Mesh(const std::vector<Vertex>& verts, const std::vector<unsigned int>& indices) {
    count_ = static_cast<int>(indices.size());
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
                 indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
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
    o.vao_ = o.vbo_ = o.ebo_ = 0;
    o.count_ = 0;
}
Mesh& Mesh::operator=(Mesh&& o) noexcept {
    if (this != &o) {
        release();
        vao_ = o.vao_; vbo_ = o.vbo_; ebo_ = o.ebo_; count_ = o.count_;
        o.vao_ = o.vbo_ = o.ebo_ = 0;
        o.count_ = 0;
    }
    return *this;
}

void Mesh::draw() const {
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, count_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

Mesh Mesh::cube() {
    // 24 verts (per-face normals), 36 indices. Unit cube centered on origin.
    const float h = 0.5f;
    std::vector<Vertex> v = {
        // +X
        {  h,-h,-h, 1,0,0}, {  h,-h, h, 1,0,0}, {  h, h, h, 1,0,0}, {  h, h,-h, 1,0,0},
        // -X
        { -h,-h, h,-1,0,0}, { -h,-h,-h,-1,0,0}, { -h, h,-h,-1,0,0}, { -h, h, h,-1,0,0},
        // +Y
        { -h, h,-h, 0,1,0}, {  h, h,-h, 0,1,0}, {  h, h, h, 0,1,0}, { -h, h, h, 0,1,0},
        // -Y
        { -h,-h, h, 0,-1,0}, {  h,-h, h, 0,-1,0}, {  h,-h,-h, 0,-1,0}, { -h,-h,-h, 0,-1,0},
        // +Z
        { -h,-h, h, 0,0,1}, {  h,-h, h, 0,0,1}, {  h, h, h, 0,0,1}, { -h, h, h, 0,0,1},
        // -Z
        {  h,-h,-h, 0,0,-1}, { -h,-h,-h, 0,0,-1}, { -h, h,-h, 0,0,-1}, {  h, h,-h, 0,0,-1},
    };
    std::vector<unsigned int> idx;
    for (unsigned int f = 0; f < 6; ++f) {
        unsigned int b = f * 4;
        idx.insert(idx.end(), { b, b+1, b+2, b, b+2, b+3 });
    }
    return Mesh(v, idx);
}

Mesh Mesh::plane(float s) {
    std::vector<Vertex> v = {
        { -s, 0, -s, 0,1,0 }, {  s, 0, -s, 0,1,0 },
        {  s, 0,  s, 0,1,0 }, { -s, 0,  s, 0,1,0 },
    };
    std::vector<unsigned int> idx = { 0, 1, 2, 0, 2, 3 };
    return Mesh(v, idx);
}

} // namespace eng
