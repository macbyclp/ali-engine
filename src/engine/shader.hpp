#pragma once
#include "engine/gl.hpp"
#include <glm/glm.hpp>
#include <string>

namespace eng {

class Shader {
public:
    Shader() = default;
    // Compiles from GLSL source strings (not files) so the web build has no fetch.
    Shader(const char* vertex_src, const char* fragment_src);
    ~Shader();

    Shader(Shader&&) noexcept;
    Shader& operator=(Shader&&) noexcept;
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    void use() const;
    void set(const char* name, int v) const;
    void set(const char* name, float v) const;
    void set(const char* name, const glm::vec3& v) const;
    void set(const char* name, const glm::mat4& v) const;

    unsigned int id() const { return prog_; }

private:
    unsigned int prog_ = 0;
    static unsigned int compile(unsigned int type, const char* src);
};

} // namespace eng
