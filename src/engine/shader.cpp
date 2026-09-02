#include "engine/shader.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <cstdio>
#include <vector>

namespace eng {

unsigned int Shader::compile(unsigned int type, const char* src) {
    unsigned int s = glCreateShader(type);
#ifdef ENGINE_WEB
    const char* header = "#version 300 es\nprecision highp float;\n";
#else
    const char* header = "#version 330 core\n";
#endif
    const char* parts[2] = { header, src };
    glShaderSource(s, 2, parts, nullptr);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        int len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len > 1 ? len : 1);
        glGetShaderInfoLog(s, len, nullptr, log.data());
        std::fprintf(stderr, "shader compile error (%s):\n%s\n",
                     type == GL_VERTEX_SHADER ? "vertex" : "fragment", log.data());
    }
    return s;
}

Shader::Shader(const char* vertex_src, const char* fragment_src) {
    unsigned int vs = compile(GL_VERTEX_SHADER, vertex_src);
    unsigned int fs = compile(GL_FRAGMENT_SHADER, fragment_src);
    prog_ = glCreateProgram();
    glAttachShader(prog_, vs);
    glAttachShader(prog_, fs);
    glLinkProgram(prog_);
    int ok = 0;
    glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prog_, sizeof(log), nullptr, log);
        std::fprintf(stderr, "program link error:\n%s\n", log);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
}

Shader::~Shader() {
    if (prog_) glDeleteProgram(prog_);
}

Shader::Shader(Shader&& o) noexcept : prog_(o.prog_) { o.prog_ = 0; }
Shader& Shader::operator=(Shader&& o) noexcept {
    if (this != &o) {
        if (prog_) glDeleteProgram(prog_);
        prog_ = o.prog_;
        o.prog_ = 0;
    }
    return *this;
}

void Shader::use() const { glUseProgram(prog_); }
void Shader::set(const char* n, int v) const { glUniform1i(glGetUniformLocation(prog_, n), v); }
void Shader::set(const char* n, float v) const { glUniform1f(glGetUniformLocation(prog_, n), v); }
void Shader::set(const char* n, const glm::vec3& v) const {
    glUniform3fv(glGetUniformLocation(prog_, n), 1, glm::value_ptr(v));
}
void Shader::set(const char* n, const glm::mat4& v) const {
    glUniformMatrix4fv(glGetUniformLocation(prog_, n), 1, GL_FALSE, glm::value_ptr(v));
}

} // namespace eng
