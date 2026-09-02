#include "render/shader.hpp"
#include "core/log.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <vector>

namespace eng {

unsigned Shader::compile(unsigned type, const char* src) {
    unsigned s = glCreateShader(type);
    const char* parts[2] = { "#version 450 core\n", src };
    glShaderSource(s, 2, parts, nullptr);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        int len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> lg(len > 1 ? len : 1);
        glGetShaderInfoLog(s, len, nullptr, lg.data());
        log::error("shader %s: %s", type == GL_VERTEX_SHADER ? "vs" : "fs", lg.data());
    }
    return s;
}

Shader::Shader(const char* vs_src, const char* fs_src) {
    unsigned vs = compile(GL_VERTEX_SHADER, vs_src);
    unsigned fs = compile(GL_FRAGMENT_SHADER, fs_src);
    prog_ = glCreateProgram();
    glAttachShader(prog_, vs);
    glAttachShader(prog_, fs);
    glLinkProgram(prog_);
    int ok = 0;
    glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char lg[1024];
        glGetProgramInfoLog(prog_, sizeof(lg), nullptr, lg);
        log::error("link: %s", lg);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
}

Shader::~Shader() { if (prog_) glDeleteProgram(prog_); }
Shader::Shader(Shader&& o) noexcept : prog_(o.prog_) { o.prog_ = 0; }
Shader& Shader::operator=(Shader&& o) noexcept {
    if (this != &o) { if (prog_) glDeleteProgram(prog_); prog_ = o.prog_; o.prog_ = 0; }
    return *this;
}

void Shader::use() const { glUseProgram(prog_); }
void Shader::set(const char* n, int v) const { glUniform1i(glGetUniformLocation(prog_, n), v); }
void Shader::set(const char* n, float v) const { glUniform1f(glGetUniformLocation(prog_, n), v); }
void Shader::set(const char* n, const glm::vec3& v) const {
    glUniform3fv(glGetUniformLocation(prog_, n), 1, glm::value_ptr(v));
}
void Shader::set(const char* n, const glm::mat3& v) const {
    glUniformMatrix3fv(glGetUniformLocation(prog_, n), 1, GL_FALSE, glm::value_ptr(v));
}
void Shader::set(const char* n, const glm::mat4& v) const {
    glUniformMatrix4fv(glGetUniformLocation(prog_, n), 1, GL_FALSE, glm::value_ptr(v));
}

} // namespace eng
