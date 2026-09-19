#pragma once
#include "render/gl.hpp"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace eng {

// A baked bitmap font atlas from a TTF via stb_truetype. Text is UTF-8. Covers Basic Latin,
// Latin-1, Latin Extended-A (Turkish: ğ Ğ ş Ş ı İ ç ö ü), typographic punctuation and '₺'.
class Font {
public:
    struct Glyph { float x0, y0, x1, y1; float u0, v0, u1, v1; float advance; };

    Font(const std::string& ttf_path, float pixel_height);
    ~Font();
    bool ok() const { return tex_ != 0; }
    unsigned texture() const { return tex_; }
    float line_height() const { return line_h_; }

    // Appends screen-space quads for `text` starting at (x, y) (top-left, pixels).
    // Each quad = 6 vertices of (vec2 pos, vec2 uv). Returns advance width.
    float layout(const std::string& text, float x, float y, float scale,
                 std::vector<glm::vec4>& out) const;
    float measure(const std::string& text, float scale) const;

private:
    unsigned tex_ = 0;
    float pixel_h_ = 32.0f;
    float line_h_ = 32.0f;
    std::unordered_map<unsigned, Glyph> glyphs_;   // by Unicode code point
    int atlas_w_ = 1024, atlas_h_ = 1024;
};

} // namespace eng
