#pragma once
#include "render/gl.hpp"
#include <glm/glm.hpp>

namespace eng {

class Shader {
public:
    Shader() = default;
    Shader(const char* vertex_src, const char* fragment_src);   // "#version 450 core" auto-prepended
    ~Shader();
    Shader(Shader&&) noexcept;
    Shader& operator=(Shader&&) noexcept;
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    void use() const;
    void set(const char* n, int v) const;
    void set(const char* n, float v) const;
    void set(const char* n, const glm::vec2& v) const;
    void set(const char* n, const glm::vec3& v) const;
    void set(const char* n, const glm::mat3& v) const;
    void set(const char* n, const glm::mat4& v) const;
    unsigned id() const { return prog_; }

private:
    unsigned prog_ = 0;
    static unsigned compile(unsigned type, const char* src);
};

} // namespace eng
