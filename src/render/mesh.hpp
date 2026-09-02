#pragma once
#include "render/gl.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace eng {

struct Vertex {
    glm::vec3 pos{0};
    glm::vec3 normal{0, 1, 0};
    glm::vec2 uv{0};
};

class Mesh {
public:
    Mesh() = default;
    Mesh(const std::vector<Vertex>& verts, const std::vector<uint32_t>& indices);
    ~Mesh();
    Mesh(Mesh&&) noexcept;
    Mesh& operator=(Mesh&&) noexcept;
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void draw() const;
    int index_count() const { return count_; }

    static std::shared_ptr<Mesh> cube();
    static std::shared_ptr<Mesh> sphere(int segments = 32);
    static std::shared_ptr<Mesh> plane(float size = 1.0f);

private:
    unsigned vao_ = 0, vbo_ = 0, ebo_ = 0;
    int count_ = 0;
    void release();
};

} // namespace eng
