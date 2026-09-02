#pragma once
#include "render/gl.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eng {

// A 2D GL texture. sRGB flag controls the internal format (albedo/emissive are
// sRGB; normal / metallic-roughness / AO are linear).
class Texture {
public:
    Texture(int w, int h, int channels, const unsigned char* pixels, bool srgb);
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    unsigned id() const { return tex_; }
    int width() const { return w_; }
    int height() const { return h_; }

    // Load from a file, an in-memory encoded blob, or a builtin generator name.
    static std::shared_ptr<Texture> from_file(const std::string& path, bool srgb);
    static std::shared_ptr<Texture> from_memory(const unsigned char* data, int len, bool srgb);
    static std::shared_ptr<Texture> builtin(const std::string& name, bool srgb);

    // Cached resolve: file path, or "builtin:<name>", or a key registered via put().
    static std::shared_ptr<Texture> resolve(const std::string& key, bool srgb,
                                            const std::string& base_dir = "");
    // Register an already-created texture under a key (used for glTF-embedded images).
    static void put(const std::string& key, bool srgb, std::shared_ptr<Texture> tex);

private:
    unsigned tex_ = 0;
    int w_ = 0, h_ = 0;
};

} // namespace eng
