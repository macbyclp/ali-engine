#pragma once
#include "geo/meshdata.hpp"
#include <string>
#include <vector>

namespace eng {

// Heightmap terrain. `heights` is resolution*resolution normalized 0..1, row-major
// over Z then X. Built from fractal noise, then optionally sculpted.
struct TerrainData {
    float size = 40.0f;        // world XZ extent (square, centred on origin)
    int resolution = 80;       // vertices per side
    float height = 6.0f;       // world height at heights==1
    int octaves = 5;
    float frequency = 1.0f;
    int seed = 1337;
    std::vector<float> heights;
    bool sculpted = false;

    void regenerate_noise();                                   // fills `heights`
    void sculpt(float wx, float wz, float radius, float strength,
                const std::string& mode);                      // raise|lower|smooth|flatten
    float sample(float wx, float wz) const;                    // world height at (x,z)
    MeshData build() const;
};

} // namespace eng
