#include "render/envmap.hpp"
#include "render/gl.hpp"
#include "core/log.hpp"
#include <cmath>

#include <stb_image.h>   // implementation lives in assets/texture.cpp

namespace eng {

EnvMap::~EnvMap() { release(); }

void EnvMap::release() {
    if (tex_) glDeleteTextures(1, &tex_);
    tex_ = 0;
    max_lod_ = 0;
    path_.clear();
}

bool EnvMap::load(const std::string& path) {
    if (path == path_) return tex_ != 0;
    if (path.empty()) { release(); return false; }

    stbi_set_flip_vertically_on_load(1);
    int w = 0, h = 0, n = 0;
    float* px = stbi_loadf(path.c_str(), &w, &h, &n, 3);
    stbi_set_flip_vertically_on_load(0);
    if (!px) {
        log::error("envmap: cannot load %s (%s)", path.c_str(), stbi_failure_reason());
        return false;
    }

    release();
    int mips = 1 + (int)std::floor(std::log2((float)std::max(w, h)));
    glCreateTextures(GL_TEXTURE_2D, 1, &tex_);
    glTextureStorage2D(tex_, mips, GL_RGB16F, w, h);
    glTextureSubImage2D(tex_, 0, 0, 0, w, h, GL_RGB, GL_FLOAT, px);
    glGenerateTextureMipmap(tex_);
    glTextureParameteri(tex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(tex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(tex_, GL_TEXTURE_WRAP_S, GL_REPEAT);        // longitude wraps
    glTextureParameteri(tex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // latitude clamps
    stbi_image_free(px);

    max_lod_ = mips - 1;
    path_ = path;
    log::info("envmap: loaded %s (%dx%d, %d mips)", path.c_str(), w, h, mips);
    return true;
}

} // namespace eng
