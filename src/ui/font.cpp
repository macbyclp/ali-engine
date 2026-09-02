#include "ui/font.hpp"
#include "core/log.hpp"
#include <cstdio>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace eng {

Font::Font(const std::string& ttf_path, float pixel_height) : pixel_h_(pixel_height) {
    std::vector<unsigned char> ttf;
    if (FILE* f = std::fopen(ttf_path.c_str(), "rb")) {
        std::fseek(f, 0, SEEK_END);
        long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        ttf.resize(n > 0 ? n : 0);
        if (!ttf.empty()) { size_t r = std::fread(ttf.data(), 1, ttf.size(), f); (void)r; }
        std::fclose(f);
    }
    if (ttf.empty()) { log::error("font: cannot read %s", ttf_path.c_str()); return; }

    std::vector<unsigned char> bitmap(atlas_w_ * atlas_h_);
    stbtt_bakedchar cdata[95];
    int res = stbtt_BakeFontBitmap(ttf.data(), 0, pixel_height, bitmap.data(),
                                   atlas_w_, atlas_h_, 32, 95, cdata);
    if (res <= 0) log::warn("font: atlas may be truncated (%d)", res);

    for (int i = 0; i < 95; ++i) {
        const stbtt_bakedchar& c = cdata[i];
        Glyph& g = glyphs_[i];
        g.x0 = c.xoff; g.y0 = c.yoff;
        g.x1 = c.xoff + (c.x1 - c.x0);
        g.y1 = c.yoff + (c.y1 - c.y0);
        g.u0 = c.x0 / float(atlas_w_); g.v0 = c.y0 / float(atlas_h_);
        g.u1 = c.x1 / float(atlas_w_); g.v1 = c.y1 / float(atlas_h_);
        g.advance = c.xadvance;
    }
    line_h_ = pixel_height * 1.25f;

    glCreateTextures(GL_TEXTURE_2D, 1, &tex_);
    glTextureStorage2D(tex_, 1, GL_R8, atlas_w_, atlas_h_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage2D(tex_, 0, 0, 0, atlas_w_, atlas_h_, GL_RED, GL_UNSIGNED_BYTE, bitmap.data());
    glTextureParameteri(tex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(tex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(tex_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(tex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

Font::~Font() {
    if (tex_) glDeleteTextures(1, &tex_);
}

float Font::layout(const std::string& text, float x, float y, float scale,
                   std::vector<glm::vec4>& out) const {
    float cx = x, baseline = y + pixel_h_ * scale;
    for (unsigned char ch : text) {
        if (ch < 32 || ch > 126) { cx += pixel_h_ * 0.4f * scale; continue; }
        const Glyph& g = glyphs_[ch - 32];
        float qx0 = cx + g.x0 * scale, qy0 = baseline + g.y0 * scale;
        float qx1 = cx + g.x1 * scale, qy1 = baseline + g.y1 * scale;
        out.push_back({qx0, qy0, g.u0, g.v0});
        out.push_back({qx1, qy0, g.u1, g.v0});
        out.push_back({qx1, qy1, g.u1, g.v1});
        out.push_back({qx0, qy0, g.u0, g.v0});
        out.push_back({qx1, qy1, g.u1, g.v1});
        out.push_back({qx0, qy1, g.u0, g.v1});
        cx += g.advance * scale;
    }
    return cx - x;
}

float Font::measure(const std::string& text, float scale) const {
    float w = 0;
    for (unsigned char ch : text) {
        if (ch < 32 || ch > 126) { w += pixel_h_ * 0.4f * scale; continue; }
        w += glyphs_[ch - 32].advance * scale;
    }
    return w;
}

} // namespace eng
