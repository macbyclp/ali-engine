#include "geo/terrain.hpp"
#include <algorithm>
#include <cmath>

namespace eng {

static float hash2(int x, int y, int seed) {
    uint32_t h = (uint32_t)(x * 374761393 + y * 668265263 + seed * 2147483647);
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return float(h) / float(0xffffffffu);
}
static float vnoise(float x, float y, int seed) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float xf = x - xi, yf = y - yi;
    float u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
    float a = hash2(xi, yi, seed), b = hash2(xi + 1, yi, seed);
    float c = hash2(xi, yi + 1, seed), d = hash2(xi + 1, yi + 1, seed);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}

void TerrainData::regenerate_noise() {
    int n = std::max(2, resolution);
    resolution = n;
    heights.assign((size_t)n * n, 0.0f);
    float maxamp = 0.0f, amp = 1.0f;
    for (int o = 0; o < std::max(1, octaves); ++o) { maxamp += amp; amp *= 0.5f; }
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            float fx = float(x) / n, fz = float(z) / n;
            float sum = 0.0f, a = 1.0f, f = frequency * 3.0f;
            for (int o = 0; o < std::max(1, octaves); ++o) {
                sum += a * vnoise(fx * f, fz * f, seed + o * 101);
                a *= 0.5f;
                f *= 2.0f;
            }
            float h = sum / maxamp;
            // gentle island falloff toward the edges
            float edge = std::min({fx, fz, 1 - fx, 1 - fz}) * 4.0f;
            heights[(size_t)z * n + x] = h * std::clamp(edge, 0.0f, 1.0f);
        }
    sculpted = false;
}

void TerrainData::sculpt(float wx, float wz, float radius, float strength,
                         const std::string& mode) {
    int n = resolution;
    if ((int)heights.size() != n * n) regenerate_noise();
    float half = size * 0.5f;
    float cellx = (wx + half) / size * (n - 1);
    float cellz = (wz + half) / size * (n - 1);
    float rc = radius / size * (n - 1);
    int x0 = std::max(0, (int)(cellx - rc)), x1 = std::min(n - 1, (int)(cellx + rc + 1));
    int z0 = std::max(0, (int)(cellz - rc)), z1 = std::min(n - 1, (int)(cellz + rc + 1));

    // average for flatten
    float avg = 0.0f; int cnt = 0;
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) { avg += heights[(size_t)z * n + x]; ++cnt; }
    if (cnt) avg /= cnt;

    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            float d = std::sqrt((x - cellx) * (x - cellx) + (z - cellz) * (z - cellz)) / std::max(rc, 1e-3f);
            if (d >= 1.0f) continue;
            float w = (1.0f - d) * (1.0f - d);
            float& hgt = heights[(size_t)z * n + x];
            if (mode == "lower") hgt -= strength * w * 0.05f;
            else if (mode == "flatten") hgt += (avg - hgt) * w * strength;
            else if (mode == "smooth") {
                float s = 0.0f; int c = 0;
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -1; dx <= 1; ++dx) {
                        int nx = x + dx, nz = z + dz;
                        if (nx < 0 || nz < 0 || nx >= n || nz >= n) continue;
                        s += heights[(size_t)nz * n + nx]; ++c;
                    }
                hgt += (s / c - hgt) * w * strength;
            } else {  // raise
                hgt += strength * w * 0.05f;
            }
            hgt = std::clamp(hgt, 0.0f, 1.5f);
        }
    sculpted = true;
}

float TerrainData::sample(float wx, float wz) const {
    int n = resolution;
    if ((int)heights.size() != n * n) return 0.0f;
    float half = size * 0.5f;
    float fx = std::clamp((wx + half) / size, 0.0f, 1.0f) * (n - 1);
    float fz = std::clamp((wz + half) / size, 0.0f, 1.0f) * (n - 1);
    int x0 = (int)fx, z0 = (int)fz;
    int x1 = std::min(n - 1, x0 + 1), z1 = std::min(n - 1, z0 + 1);
    float tx = fx - x0, tz = fz - z0;
    auto H = [&](int x, int z) { return heights[(size_t)z * n + x]; };
    float a = H(x0, z0) + (H(x1, z0) - H(x0, z0)) * tx;
    float b = H(x0, z1) + (H(x1, z1) - H(x0, z1)) * tx;
    return (a + (b - a) * tz) * height;
}

MeshData TerrainData::build() const {
    MeshData d;
    int n = resolution;
    if ((int)heights.size() != n * n) return d;
    float half = size * 0.5f;
    auto H = [&](int x, int z) { return heights[(size_t)z * n + x] * height; };
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            float wx = -half + size * x / (n - 1);
            float wz = -half + size * z / (n - 1);
            float hl = H(std::max(0, x - 1), z), hr = H(std::min(n - 1, x + 1), z);
            float hd = H(x, std::max(0, z - 1)), hu = H(x, std::min(n - 1, z + 1));
            glm::vec3 nrm = glm::normalize(glm::vec3(hl - hr, 2.0f * size / (n - 1), hd - hu));
            Vertex v;
            v.pos = {wx, H(x, z), wz};
            v.normal = nrm;
            v.uv = {float(x) / (n - 1) * (size / 4.0f), float(z) / (n - 1) * (size / 4.0f)};
            d.verts.push_back(v);
        }
    for (int z = 0; z < n - 1; ++z)
        for (int x = 0; x < n - 1; ++x) {
            uint32_t i0 = (uint32_t)(z * n + x), i1 = i0 + n;
            d.idx.insert(d.idx.end(), {i0, i1, i0 + 1, i0 + 1, i1, i1 + 1});
        }
    return d;
}

} // namespace eng
