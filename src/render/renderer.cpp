#include "render/renderer.hpp"
#include "core/log.hpp"
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <string>

namespace eng {

// ---------- shared GLSL ----------
static const char* kSkyGLSL = R"(
vec3 skyColor(vec3 dir, vec3 sunDir) {
    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 horizon = vec3(0.60, 0.66, 0.76);
    vec3 zenith  = vec3(0.14, 0.27, 0.52);
    vec3 ground  = vec3(0.10, 0.10, 0.11);
    vec3 col = (dir.y < 0.0)
        ? mix(horizon, ground, clamp(-dir.y * 3.0, 0.0, 1.0))
        : mix(horizon, zenith, pow(t, 0.55));
    float sun  = pow(max(dot(dir, -sunDir), 0.0), 800.0);
    float glow = pow(max(dot(dir, -sunDir), 0.0), 8.0);
    col += vec3(1.0, 0.92, 0.74) * sun * 4.0;
    col += vec3(1.0, 0.78, 0.55) * glow * 0.25;
    return col;
}
)";

// ---------- shadow depth pass ----------
static const char* kShadowVert = R"(
layout(location=0) in vec3 aPos;
uniform mat4 uLightVP;
uniform mat4 uModel;
void main() { gl_Position = uLightVP * uModel * vec4(aPos, 1.0); }
)";
static const char* kShadowFrag = R"(
void main() {}
)";

// ---------- fullscreen sky ----------
static const char* kFsVert = R"(
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 1.0, 1.0);
}
)";
static const char* kSkyFrag = R"(
in vec2 vUV;
uniform mat4 uInvViewProj;
uniform vec3 uCamPos;
uniform vec3 uSunDir;
out vec4 FragColor;
void main() {
    vec4 far = uInvViewProj * vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(far.xyz / far.w - uCamPos);
    FragColor = vec4(skyColor(dir, normalize(uSunDir)), 1.0);
}
)";

// ---------- PBR ----------
static const char* kPbrVert = R"(
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
uniform mat4 uModel;
uniform mat4 uViewProj;
uniform mat3 uNormalMat;
uniform mat4 uLightVP;
out vec3 vWorld;
out vec3 vN;
out vec4 vLightSpace;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld = w.xyz;
    vN = normalize(uNormalMat * aNormal);
    vLightSpace = uLightVP * w;
    gl_Position = uViewProj * w;
}
)";
static const char* kPbrFrag = R"(
in vec3 vWorld;
in vec3 vN;
in vec4 vLightSpace;
out vec4 FragColor;

uniform vec3 uCamPos;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform float uSunIntensity;
uniform vec3 uAlbedo;
uniform float uMetallic;
uniform float uRoughness;
uniform sampler2DShadow uShadowMap;

const float PI = 3.14159265359;

float D_GGX(float NoH, float a) {
    float a2 = a * a;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}
float G_SchlickGGX(float NoX, float k) { return NoX / (NoX * (1.0 - k) + k); }
float G_Smith(float NoV, float NoL, float rough) {
    float k = (rough + 1.0) * (rough + 1.0) / 8.0;
    return G_SchlickGGX(NoV, k) * G_SchlickGGX(NoL, k);
}
vec3 fresnel(float ct, vec3 F0) { return F0 + (1.0 - F0) * pow(1.0 - ct, 5.0); }
vec3 fresnelRough(float ct, vec3 F0, float r) {
    return F0 + (max(vec3(1.0 - r), F0) - F0) * pow(1.0 - ct, 5.0);
}

float shadowFactor(vec4 lsp, vec3 N, vec3 L) {
    vec3 p = lsp.xyz / lsp.w;
    p = p * 0.5 + 0.5;
    if (p.z > 1.0) return 1.0;
    float bias = max(0.0025 * (1.0 - dot(N, L)), 0.0008);
    float sh = 0.0;
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            sh += texture(uShadowMap, vec3(p.xy + vec2(x, y) * texel, p.z - bias));
    return sh / 9.0;
}

void main() {
    vec3 N = normalize(vN);
    vec3 V = normalize(uCamPos - vWorld);
    vec3 L = normalize(-uSunDir);
    vec3 H = normalize(V + L);
    float NoV = max(dot(N, V), 1e-4);
    float NoL = max(dot(N, L), 0.0);
    float NoH = max(dot(N, H), 0.0);

    vec3 F0 = mix(vec3(0.04), uAlbedo, uMetallic);
    float rough = clamp(uRoughness, 0.04, 1.0);

    // direct
    float D = D_GGX(NoH, rough * rough);
    float G = G_Smith(NoV, NoL, rough);
    vec3  F = fresnel(max(dot(H, V), 0.0), F0);
    vec3 spec = (D * G * F) / max(4.0 * NoV * NoL, 1e-4);
    vec3 kd = (1.0 - F) * (1.0 - uMetallic);
    vec3 radiance = uSunColor * uSunIntensity;
    float sh = shadowFactor(vLightSpace, N, L);
    vec3 direct = (kd * uAlbedo / PI + spec) * radiance * NoL * sh;

    // ambient (procedural hemisphere IBL approximation)
    vec3 skyUp = skyColor(vec3(0, 1, 0), normalize(uSunDir));
    vec3 skyDn = skyColor(vec3(0, -1, 0), normalize(uSunDir));
    vec3 irradiance = mix(skyDn, skyUp, N.y * 0.5 + 0.5);
    vec3 R = reflect(-V, N);
    vec3 prefiltered = mix(skyColor(R, normalize(uSunDir)), irradiance, rough);
    vec3 Fr = fresnelRough(NoV, F0, rough);
    vec3 kdA = (1.0 - Fr) * (1.0 - uMetallic);
    vec3 ambient = (kdA * uAlbedo * irradiance + prefiltered * Fr) * 0.55;

    vec3 color = ambient + direct;
    FragColor = vec4(color, 1.0);
}
)";

// ---------- tonemap ----------
static const char* kTonemapFrag = R"(
in vec2 vUV;
uniform sampler2D uHDR;
out vec4 FragColor;
vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
void main() {
    vec3 hdr = texture(uHDR, vUV).rgb;
    vec3 mapped = aces(hdr);
    FragColor = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}
)";

// ------------------------------------------------------------------

Renderer::Renderer(int w, int h)
    : pbr_(kPbrVert, (std::string(kSkyGLSL) + kPbrFrag).c_str()),
      sky_(kFsVert, (std::string(kSkyGLSL) + kSkyFrag).c_str()),
      shadow_(kShadowVert, kShadowFrag),
      tonemap_(kFsVert, kTonemapFrag),
      shadow_map_(2048, 2048, ColorFormat::None, /*depth_sampled=*/true) {
    hdr_ = std::make_unique<Framebuffer>(w, h, ColorFormat::RGBA16F, false);
    glCreateVertexArrays(1, &empty_vao_);
}

void Renderer::ensure_hdr(int w, int h) {
    if (hdr_->width() != w || hdr_->height() != h) hdr_->resize(w, h);
}

void Renderer::render(Scene& scene, unsigned target_fbo, int w, int h) {
    ensure_hdr(w, h);

    CameraComp& cam = scene.camera();
    float aspect = h ? float(w) / float(h) : 1.0f;
    glm::mat4 view = cam.view();
    glm::mat4 proj = cam.proj(aspect);
    glm::mat4 view_proj = proj * view;

    DirectionalLight light;
    for (auto [e, dl] : scene.registry.view<DirectionalLight>().each()) { light = dl; break; }
    glm::vec3 sun_dir = glm::normalize(light.direction);

    // scene bounds (rough): sphere over renderable positions
    glm::vec3 center(0);
    int count = 0;
    for (auto [e, t, mr] : scene.registry.view<Transform, MeshRenderer>().each()) {
        center += t.position; ++count;
    }
    if (count) center /= float(count);
    float radius = 4.0f;
    for (auto [e, t, mr] : scene.registry.view<Transform, MeshRenderer>().each()) {
        float s = std::max({std::abs(t.scale.x), std::abs(t.scale.y), std::abs(t.scale.z)});
        radius = std::max(radius, glm::length(t.position - center) + s);
    }
    radius += 2.0f;

    glm::vec3 light_eye = center - sun_dir * (radius * 2.0f);
    glm::vec3 up = std::abs(sun_dir.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    glm::mat4 light_view = glm::lookAt(light_eye, center, up);
    glm::mat4 light_proj = glm::ortho(-radius, radius, -radius, radius, 0.1f, radius * 4.0f);
    glm::mat4 light_vp = light_proj * light_view;

    // ---- pass 1: shadow depth ----
    shadow_map_.bind();
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glCullFace(GL_FRONT);
    shadow_.use();
    shadow_.set("uLightVP", light_vp);
    for (auto [e, t, mr] : scene.registry.view<Transform, MeshRenderer>().each()) {
        if (!mr.gpu) continue;
        shadow_.set("uModel", t.matrix());
        mr.gpu->draw();
    }
    glCullFace(GL_BACK);

    // ---- pass 2: scene into HDR ----
    hdr_->bind();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // sky fills the buffer first; meshes depth-test over it
    glDisable(GL_DEPTH_TEST);
    sky_.use();
    sky_.set("uInvViewProj", glm::inverse(view_proj));
    sky_.set("uCamPos", cam.position);
    sky_.set("uSunDir", sun_dir);
    glBindVertexArray(empty_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);

    glBindTextureUnit(0, shadow_map_.depth_texture());
    pbr_.use();
    pbr_.set("uViewProj", view_proj);
    pbr_.set("uLightVP", light_vp);
    pbr_.set("uCamPos", cam.position);
    pbr_.set("uSunDir", sun_dir);
    pbr_.set("uSunColor", light.color);
    pbr_.set("uSunIntensity", light.intensity);
    pbr_.set("uShadowMap", 0);
    for (auto [e, t, mr] : scene.registry.view<Transform, MeshRenderer>().each()) {
        if (!mr.gpu) continue;
        glm::mat4 model = t.matrix();
        pbr_.set("uModel", model);
        pbr_.set("uNormalMat", glm::mat3(glm::inverseTranspose(model)));
        pbr_.set("uAlbedo", mr.base_color);
        pbr_.set("uMetallic", mr.metallic);
        pbr_.set("uRoughness", mr.roughness);
        mr.gpu->draw();
    }

    // ---- pass 3: tonemap HDR -> target ----
    glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    tonemap_.use();
    glBindTextureUnit(0, hdr_->color_texture());
    tonemap_.set("uHDR", 0);
    glBindVertexArray(empty_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);
}

} // namespace eng
