#pragma once
#include <string>

namespace eng {

// An equirectangular HDR environment map (Radiance .hdr), uploaded as an RGB16F
// 2D texture with a full mip chain. Sampling a blurred mip approximates
// irradiance / prefiltered specular -- not physically exact, but a large step
// up from the procedural-sky approximation and cheap.
class EnvMap {
public:
    ~EnvMap();
    // Load / reload from a file. Returns false and keeps the previous texture on
    // failure. `path` empty => release the texture.
    bool load(const std::string& path);

    bool ok() const { return tex_ != 0; }
    unsigned texture() const { return tex_; }
    int max_lod() const { return max_lod_; }
    const std::string& path() const { return path_; }

private:
    unsigned tex_ = 0;
    int max_lod_ = 0;
    std::string path_;
    void release();
};

} // namespace eng
