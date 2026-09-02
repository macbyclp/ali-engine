#pragma once
#include "render/gl.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace eng {

// A baked bitmap font atlas (ASCII 32..126) from a TTF via stb_truetype.
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
    Glyph glyphs_[95]{};
    int atlas_w_ = 512, atlas_h_ = 512;
};

} // namespace eng
