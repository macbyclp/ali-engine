#include "render/renderer.hpp"
#include <glm/gtc/matrix_inverse.hpp>

namespace eng {

static const char* kVert = R"(
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
uniform mat4 uModel;
uniform mat4 uViewProj;
uniform mat3 uNormalMat;
out vec3 vN;
out vec3 vWorld;
void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorld = world.xyz;
    vN = normalize(uNormalMat * aNormal);
    gl_Position = uViewProj * world;
}
)";

static const char* kFrag = R"(
in vec3 vN;
in vec3 vWorld;
uniform vec3 uBaseColor;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform float uLightIntensity;
uniform vec3 uCamPos;
out vec4 FragColor;
void main() {
    vec3 n = normalize(vN);
    vec3 l = normalize(-uLightDir);
    float diff = max(dot(n, l), 0.0);
    vec3 ambient = 0.15 * uBaseColor;
    vec3 color = ambient + uBaseColor * uLightColor * uLightIntensity * diff;
    color = pow(color, vec3(1.0 / 2.2));           // gamma
    FragColor = vec4(color, 1.0);
}
)";

Renderer::Renderer() : lit_(kVert, kFrag) {}

void Renderer::render(Scene& scene, int fbw, int fbh) {
    glViewport(0, 0, fbw, fbh);
    glClearColor(0.09f, 0.11f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    CameraComp& cam = scene.camera();
    float aspect = fbh ? float(fbw) / float(fbh) : 1.0f;
    glm::mat4 view_proj = cam.proj(aspect) * cam.view();

    DirectionalLight light;
    for (auto [e, dl] : scene.registry.view<DirectionalLight>().each()) { light = dl; break; }

    lit_.use();
    lit_.set("uViewProj", view_proj);
    lit_.set("uLightDir", glm::normalize(light.direction));
    lit_.set("uLightColor", light.color);
    lit_.set("uLightIntensity", light.intensity);
    lit_.set("uCamPos", cam.position);

    for (auto [e, t, mr] : scene.registry.view<Transform, MeshRenderer>().each()) {
        if (!mr.gpu) continue;
        glm::mat4 model = t.matrix();
        lit_.set("uModel", model);
        lit_.set("uNormalMat", glm::mat3(glm::inverseTranspose(model)));
        lit_.set("uBaseColor", mr.base_color);
        mr.gpu->draw();
    }
}

} // namespace eng
