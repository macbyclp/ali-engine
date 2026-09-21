// Unit test for Transform's quaternion <-> Euler view (no GL context needed).
#include "ecs/components.hpp"
#include <cstdio>
#include <cstdlib>
#include <random>

using eng::Transform;

static int failures = 0;
static void check(bool c, const char* what) {
    std::printf("%s %s\n", c ? "PASS" : "FAIL", what);
    if (!c) ++failures;
}
static float qdist(const glm::quat& a, const glm::quat& b) { return 1.0f - std::abs(glm::dot(a, b)); }

int main() {
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> ang(-180.0f, 180.0f);

    // 1) Euler -> quat -> Euler -> quat describes the same orientation for random triples
    float worst = 0;
    for (int i = 0; i < 20000; ++i) {
        glm::vec3 e(ang(rng), ang(rng), ang(rng));
        glm::quat q = Transform::quat_from_euler_deg(e);
        glm::quat q2 = Transform::quat_from_euler_deg(Transform::euler_deg_from_quat(q));
        worst = std::max(worst, qdist(q, q2));
    }
    check(worst < 1e-5f, "euler_deg_from_quat inverts quat_from_euler_deg (20000 random poses)");

    // 2) matrix() equals the historic Rz*Ry*Rx composition
    glm::vec3 e(23.0f, -71.0f, 140.0f);
    Transform t; t.position = {1, 2, 3}; t.scale = {2, 1, 0.5f}; t.set_euler_deg(e);
    glm::mat4 old(1.0f);
    old = glm::translate(old, t.position);
    old = glm::rotate(old, glm::radians(e.z), {0, 0, 1});
    old = glm::rotate(old, glm::radians(e.y), {0, 1, 0});
    old = glm::rotate(old, glm::radians(e.x), {1, 0, 0});
    old = glm::scale(old, t.scale);
    glm::mat4 now = t.matrix();
    float md = 0;
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) md = std::max(md, std::abs(old[c][r] - now[c][r]));
    check(md < 1e-5f, "matrix() matches the old Euler Rz*Ry*Rx matrix");

    // 3) an author-typed triple reads back verbatim while the orientation is untouched
    t.set_euler_deg({10, 135, 20});
    check(t.euler_deg() == glm::vec3(10, 135, 20), "typed Euler triple is preserved (no equivalent-triple flip)");
    t.set_rotation(glm::angleAxis(glm::radians(15.0f), glm::vec3(0, 1, 0)) * t.rotation);
    check(t.euler_deg() != glm::vec3(10, 135, 20), "changing the quaternion invalidates the cached triple");

    // 4) gimbal lock (pitch = 90) stays finite and orientation-correct
    glm::quat g = Transform::quat_from_euler_deg({30, 90, 40});
    glm::vec3 ge = Transform::euler_deg_from_quat(g);
    check(std::isfinite(ge.x) && std::isfinite(ge.y) && std::isfinite(ge.z) &&
              qdist(g, Transform::quat_from_euler_deg(ge)) < 1e-5f,
          "gimbal-lock pose is finite and round-trips");

    // 5) add_euler_deg reproduces the old `euler += d` spin semantics
    Transform s; s.set_euler_deg({0, 350, 0});
    for (int i = 0; i < 4; ++i) s.add_euler_deg({0, 5, 0});
    check(std::abs(s.euler_deg().y - 370.0f) < 1e-4f, "add_euler_deg accumulates like the old euler_deg +=");

    std::printf("%s\n", failures ? "FAILED" : "ALL PASS");
    return failures ? 1 : 0;
}
