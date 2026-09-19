#include "ui/font.hpp"
#include "core/log.hpp"
#include <cstdio>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace eng {

// Decodes one UTF-8 code point at s[i], advancing i. Malformed bytes yield U+FFFD.
static unsigned next_codepoint(const std::string& s, size_t& i) {
    unsigned char c = s[i++];
    if (c < 0x80) return c;
    int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : -1;
    if (extra < 0) return 0xFFFD;
    unsigned cp = c & (0x3F >> extra);
    for (int k = 0; k < extra; ++k) {
        if (i >= s.size() || (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3F);
    }
    return cp;
}

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
    // {first code point, count}: Basic Latin..Latin Extended-A, dashes, curly quotes, bullet, ellipsis, lira.
    struct Range { int first, count; };
    static const Range kRanges[] = {{0x20, 0x180 - 0x20}, {0x2013, 2}, {0x2018, 6}, {0x2022, 1}, {0x2026, 1}, {0x20BA, 1}};
    constexpr int kNumRanges = sizeof(kRanges) / sizeof(kRanges[0]);
    std::vector<std::vector<stbtt_packedchar>> packed(kNumRanges);
    stbtt_pack_range pr[kNumRanges];
    for (int r = 0; r < kNumRanges; ++r) {
        packed[r].resize(kRanges[r].count);
        pr[r] = {};
        pr[r].font_size = pixel_height;
        pr[r].first_unicode_codepoint_in_range = kRanges[r].first;
        pr[r].num_chars = kRanges[r].count;
        pr[r].chardata_for_range = packed[r].data();
    }
    stbtt_pack_context pc;
    if (!stbtt_PackBegin(&pc, bitmap.data(), atlas_w_, atlas_h_, 0, 1, nullptr)) {
        log::error("font: cannot start atlas packing");
        return;
    }
    if (!stbtt_PackFontRanges(&pc, ttf.data(), 0, pr, kNumRanges)) log::warn("font: atlas may be truncated");
    stbtt_PackEnd(&pc);

    for (int r = 0; r < kNumRanges; ++r) {
        for (int i = 0; i < kRanges[r].count; ++i) {
            const stbtt_packedchar& c = packed[r][i];
            Glyph g;
            g.x0 = c.xoff;  g.y0 = c.yoff;
            g.x1 = c.xoff2; g.y1 = c.yoff2;
            g.u0 = c.x0 / float(atlas_w_); g.v0 = c.y0 / float(atlas_h_);
            g.u1 = c.x1 / float(atlas_w_); g.v1 = c.y1 / float(atlas_h_);
            g.advance = c.xadvance;
            glyphs_[unsigned(kRanges[r].first + i)] = g;
        }
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
    for (size_t i = 0; i < text.size();) {
        unsigned ch = next_codepoint(text, i);
        auto it = glyphs_.find(ch);
        if (it == glyphs_.end()) { cx += pixel_h_ * 0.4f * scale; continue; }
        const Glyph& g = it->second;
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
    for (size_t i = 0; i < text.size();) {
        unsigned ch = next_codepoint(text, i);
        auto it = glyphs_.find(ch);
        w += (it == glyphs_.end() ? pixel_h_ * 0.4f : it->second.advance) * scale;
    }
    return w;
}

} // namespace eng
