#pragma once
#include <glm/glm.hpp>
#include <algorithm>

namespace eng {

// Axis-aligned bounding box, center + half-extents.
struct AABB {
    glm::vec3 center{0};
    glm::vec3 half{0.5f};

    glm::vec3 min() const { return center - half; }
    glm::vec3 max() const { return center + half; }
};

inline bool overlaps(const AABB& a, const AABB& b) {
    return std::abs(a.center.x - b.center.x) <= (a.half.x + b.half.x) &&
           std::abs(a.center.y - b.center.y) <= (a.half.y + b.half.y) &&
           std::abs(a.center.z - b.center.z) <= (a.half.z + b.half.z);
}

// Minimum translation vector to push `a` out of `b`. Zero if not overlapping.
inline glm::vec3 resolve_penetration(const AABB& a, const AABB& b) {
    if (!overlaps(a, b)) return glm::vec3(0);
    glm::vec3 d = a.center - b.center;
    glm::vec3 pen = (a.half + b.half) - glm::abs(d);
    // Push along the axis of least penetration.
    if (pen.x < pen.y && pen.x < pen.z)
        return glm::vec3(d.x < 0 ? -pen.x : pen.x, 0, 0);
    if (pen.y < pen.z)
        return glm::vec3(0, d.y < 0 ? -pen.y : pen.y, 0);
    return glm::vec3(0, 0, d.z < 0 ? -pen.z : pen.z);
}

} // namespace eng
