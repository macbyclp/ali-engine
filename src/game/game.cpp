#include "game/game.hpp"
#include "engine/camera.hpp"
#include "engine/mesh.hpp"
#include "engine/shader.hpp"
#include "engine/collision.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

using namespace eng;

namespace {

const char* kVert = R"(
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
out vec3 vNormal;
out vec3 vWorld;
void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorld = world.xyz;
    vNormal = mat3(uModel) * aNormal;
    gl_Position = uProj * uView * world;
}
)";

const char* kFrag = R"(
in vec3 vNormal;
in vec3 vWorld;
uniform vec3 uColor;
uniform vec3 uLightDir;
out vec4 FragColor;
void main() {
    vec3 n = normalize(vNormal);
    float diff = max(dot(n, normalize(-uLightDir)), 0.0);
    vec3 c = uColor * (0.25 + 0.75 * diff);
    FragColor = vec4(c, 1.0);
}
)";

struct Box {
    AABB aabb;
    glm::vec3 color;
};

class Game : public Scene {
public:
    void init(Window& win) override {
        win.set_mouse_captured(true);
        shader_ = Shader(kVert, kFrag);
        cube_ = Mesh::cube();
        plane_ = Mesh::plane(25.0f);

        // Static level: a few boxes to walk into.
        boxes_.push_back({ {{ 4, 0.5f,  0}, {0.5f, 0.5f, 0.5f}}, {0.9f, 0.3f, 0.3f} });
        boxes_.push_back({ {{-3, 1.0f,  2}, {0.5f, 1.0f, 0.5f}}, {0.3f, 0.8f, 0.4f} });
        boxes_.push_back({ {{ 0, 0.5f, -5}, {2.0f, 0.5f, 0.5f}}, {0.4f, 0.5f, 0.9f} });
        boxes_.push_back({ {{ 6, 1.5f, -6}, {1.5f, 1.5f, 1.5f}}, {0.9f, 0.7f, 0.2f} });

        player_.center = glm::vec3(0, 1.0f, 3);
        player_.half = glm::vec3(0.4f, 0.9f, 0.4f);
        cam_.pos = player_.center + glm::vec3(0, 0.6f, 0);
    }

    void update(Window& win, float dt) override {
        if (win.key_pressed(GLFW_KEY_ESCAPE)) {
            captured_ = !captured_;
            win.set_mouse_captured(captured_);
        }
        if (captured_)
            cam_.add_look((float)win.mouse_dx(), (float)win.mouse_dy());

        // Horizontal movement in camera space (no vertical component).
        glm::vec3 fwd = cam_.front();
        fwd.y = 0;
        fwd = glm::length(fwd) > 0.0001f ? glm::normalize(fwd) : glm::vec3(0, 0, -1);
        glm::vec3 rightv = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));

        glm::vec3 wish(0);
        if (win.key(GLFW_KEY_W)) wish += fwd;
        if (win.key(GLFW_KEY_S)) wish -= fwd;
        if (win.key(GLFW_KEY_D)) wish += rightv;
        if (win.key(GLFW_KEY_A)) wish -= rightv;
        if (glm::length(wish) > 0.0001f) wish = glm::normalize(wish);

        const float speed = 6.0f;
        vel_.x = wish.x * speed;
        vel_.z = wish.z * speed;

        // Gravity + jump.
        vel_.y -= 20.0f * dt;
        if (on_ground_ && win.key(GLFW_KEY_SPACE)) {
            vel_.y = 8.0f;
            on_ground_ = false;
        }

        // Integrate + resolve per axis so sliding along walls works.
        on_ground_ = false;
        move_axis(glm::vec3(vel_.x * dt, 0, 0));
        move_axis(glm::vec3(0, 0, vel_.z * dt));
        move_axis(glm::vec3(0, vel_.y * dt, 0));

        // Ground plane at y = 0.
        float feet = player_.center.y - player_.half.y;
        if (feet < 0.0f) {
            player_.center.y -= feet;
            vel_.y = 0;
            on_ground_ = true;
        }

        cam_.pos = player_.center + glm::vec3(0, player_.half.y * 0.7f, 0);
    }

    void render(Window& win) override {
        glClearColor(0.08f, 0.10f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        shader_.use();
        shader_.set("uView", cam_.view());
        shader_.set("uProj", cam_.proj(win.aspect()));
        shader_.set("uLightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)));

        draw(glm::mat4(1.0f), glm::vec3(0.22f, 0.24f, 0.27f), plane_);

        for (const auto& b : boxes_) {
            glm::mat4 m = glm::translate(glm::mat4(1.0f), b.aabb.center);
            m = glm::scale(m, b.aabb.half * 2.0f);
            draw(m, b.color, cube_);
        }
    }

private:
    void draw(const glm::mat4& model, const glm::vec3& color, const Mesh& m) {
        shader_.set("uModel", model);
        shader_.set("uColor", color);
        m.draw();
    }

    void move_axis(glm::vec3 delta) {
        player_.center += delta;
        for (const auto& b : boxes_) {
            glm::vec3 mtv = resolve_penetration(player_, b.aabb);
            if (mtv != glm::vec3(0)) {
                player_.center += mtv;
                if (mtv.y > 0 && delta.y < 0) { vel_.y = 0; on_ground_ = true; }
                if (mtv.y < 0 && delta.y > 0) vel_.y = 0;
                if (mtv.x != 0) vel_.x = 0;
                if (mtv.z != 0) vel_.z = 0;
            }
        }
    }

    Shader shader_;
    Mesh cube_, plane_;
    Camera cam_;
    std::vector<Box> boxes_;
    AABB player_;
    glm::vec3 vel_{0};
    bool on_ground_ = false;
    bool captured_ = true;
};

} // namespace

std::unique_ptr<eng::Scene> make_game() { return std::make_unique<Game>(); }
