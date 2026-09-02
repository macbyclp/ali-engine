#pragma once
#include "engine/gl.hpp"
#include <vector>

namespace eng {

// Interleaved vertex: position(3) + normal(3).
struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
};

class Mesh {
public:
    Mesh() = default;
    Mesh(const std::vector<Vertex>& verts, const std::vector<unsigned int>& indices);
    ~Mesh();

    Mesh(Mesh&&) noexcept;
    Mesh& operator=(Mesh&&) noexcept;
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void draw() const;

    static Mesh cube();
    static Mesh plane(float size);

private:
    unsigned int vao_ = 0, vbo_ = 0, ebo_ = 0;
    int count_ = 0;
    void release();
};

} // namespace eng
