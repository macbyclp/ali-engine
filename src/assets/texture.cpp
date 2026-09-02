#include "assets/texture.hpp"
#include "core/log.hpp"
#include <cmath>
#include <filesystem>
#include <unordered_map>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace fs = std::filesystem;

namespace eng {

Texture::Texture(int w, int h, int channels, const unsigned char* pixels, bool srgb)
    : w_(w), h_(h) {
    GLenum internal, format;
    switch (channels) {
        case 1: internal = GL_R8; format = GL_RED; break;
        case 2: internal = GL_RG8; format = GL_RG; break;
        case 3: internal = srgb ? GL_SRGB8 : GL_RGB8; format = GL_RGB; break;
        default: internal = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8; format = GL_RGBA; break;
    }
    int levels = 1 + (int)std::floor(std::log2((float)std::max(w, h)));

    glCreateTextures(GL_TEXTURE_2D, 1, &tex_);
    glTextureStorage2D(tex_, levels, internal, w, h);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage2D(tex_, 0, 0, 0, w, h, format, GL_UNSIGNED_BYTE, pixels);
    glGenerateTextureMipmap(tex_);
    glTextureParameteri(tex_, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(tex_, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameteri(tex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(tex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    GLfloat aniso = 1.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &aniso);
    glTextureParameterf(tex_, GL_TEXTURE_MAX_ANISOTROPY, std::min(aniso, 8.0f));
}

Texture::~Texture() {
    if (tex_) glDeleteTextures(1, &tex_);
}

std::shared_ptr<Texture> Texture::from_memory(const unsigned char* data, int len, bool srgb) {
    int w, h, n;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* px = stbi_load_from_memory(data, len, &w, &h, &n, 0);
    if (!px) { log::error("stbi decode failed"); return nullptr; }
    auto t = std::make_shared<Texture>(w, h, n, px, srgb);
    stbi_image_free(px);
    return t;
}

std::shared_ptr<Texture> Texture::from_file(const std::string& path, bool srgb) {
    int w, h, n;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &n, 0);
    if (!px) { log::error("texture load failed: %s", path.c_str()); return nullptr; }
    auto t = std::make_shared<Texture>(w, h, n, px, srgb);
    stbi_image_free(px);
    return t;
}

std::shared_ptr<Texture> Texture::builtin(const std::string& name, bool srgb) {
    const int S = 256;
    std::vector<unsigned char> px(S * S * 4, 255);
    auto set = [&](int x, int y, int r, int g, int b, int a = 255) {
        unsigned char* p = &px[(y * S + x) * 4];
        p[0] = (unsigned char)r; p[1] = (unsigned char)g; p[2] = (unsigned char)b; p[3] = (unsigned char)a;
    };
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            if (name == "checker") {
                bool c = ((x / 32) + (y / 32)) & 1;
                int v = c ? 230 : 40;
                set(x, y, v, v, v);
            } else if (name == "grid") {
                bool line = (x % 32 < 2) || (y % 32 < 2);
                set(x, y, line ? 20 : 200, line ? 20 : 200, line ? 30 : 210);
            } else if (name == "uv") {
                set(x, y, x, y, 128);
            } else if (name == "normal" || name == "flat_normal") {
                set(x, y, 128, 128, 255);
            } else if (name == "bumps") {
                // a wobbly normal map from a sine height field
                float hx = std::sin(x * 0.19f) * std::cos(y * 0.11f);
                float hy = std::cos(x * 0.13f) * std::sin(y * 0.17f);
                int nx = (int)(128 + hx * 90);
                int ny = (int)(128 + hy * 90);
                set(x, y, nx, ny, 235);
            } else {
                set(x, y, 200, 200, 200);
            }
        }
    return std::make_shared<Texture>(S, S, 4, px.data(), srgb);
}

static std::unordered_map<std::string, std::shared_ptr<Texture>>& tex_cache() {
    static std::unordered_map<std::string, std::shared_ptr<Texture>> cache;
    return cache;
}

void Texture::put(const std::string& key, bool srgb, std::shared_ptr<Texture> tex) {
    tex_cache()[key + (srgb ? "#s" : "#l")] = std::move(tex);
}

std::shared_ptr<Texture> Texture::resolve(const std::string& key, bool srgb,
                                          const std::string& base_dir) {
    auto& cache = tex_cache();
    std::string ck = key + (srgb ? "#s" : "#l");
    auto it = cache.find(ck);
    if (it != cache.end()) return it->second;

    std::shared_ptr<Texture> t;
    if (key.rfind("builtin:", 0) == 0) {
        t = builtin(key.substr(8), srgb);
    } else {
        std::string path = key;
        if (!base_dir.empty() && !fs::path(key).is_absolute())
            path = (fs::path(base_dir) / key).string();
        t = from_file(path, srgb);
    }
    cache[ck] = t;
    return t;
}

} // namespace eng
