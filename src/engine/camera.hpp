#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace eng {

// Simple fly / FPS camera. yaw/pitch in degrees.
struct Camera {
    glm::vec3 pos{0.0f, 2.0f, 6.0f};
    float yaw = -90.0f;
    float pitch = -10.0f;
    float fov = 60.0f;

    glm::vec3 front() const {
        float cy = cosf(glm::radians(yaw)), sy = sinf(glm::radians(yaw));
        float cp = cosf(glm::radians(pitch)), sp = sinf(glm::radians(pitch));
        return glm::normalize(glm::vec3(cy * cp, sp, sy * cp));
    }
    glm::vec3 right() const {
        return glm::normalize(glm::cross(front(), glm::vec3(0, 1, 0)));
    }

    void add_look(float dx, float dy, float sensitivity = 0.1f) {
        yaw   += dx * sensitivity;
        pitch -= dy * sensitivity;
        pitch = glm::clamp(pitch, -89.0f, 89.0f);
    }

    glm::mat4 view() const { return glm::lookAt(pos, pos + front(), glm::vec3(0, 1, 0)); }
    glm::mat4 proj(float aspect) const {
        return glm::perspective(glm::radians(fov), aspect, 0.05f, 500.0f);
    }
};

} // namespace eng
