#include "render/renderer.hpp"
#include "core/log.hpp"
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace eng {

// ---------------- GLSL ----------------
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

// instance attributes: 3..6 = mat4 model, 7 = (albedo.rgb, roughness), 8 = (metallic,..)
static const char* kInstanceInputs = R"(
layout(location=3) in mat4 iModel;
layout(location=7) in vec4 iAlbedoRough;
layout(location=8) in vec4 iMetallic;
)";

static const char* kShadowVert = R"(
layout(location=0) in vec3 aPos;
layout(location=3) in mat4 iModel;
uniform mat4 uLightVP;
void main() { gl_Position = uLightVP * iModel * vec4(aPos, 1.0); }
)";
static const char* kShadowFrag = R"(
void main() {}
)";

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

static const char* kPbrVert = R"(
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
uniform mat4 uViewProj;
uniform mat4 uLightVP;
out vec3 vWorld;
out vec3 vN;
out vec4 vLightSpace;
out vec3 vAlbedo;
out float vRough;
out float vMetallic;
void main() {
    vec4 w = iModel * vec4(aPos, 1.0);
    vWorld = w.xyz;
    vN = normalize(mat3(transpose(inverse(iModel))) * aNormal);
    vLightSpace = uLightVP * w;
    vAlbedo = iAlbedoRough.rgb;
    vRough = iAlbedoRough.a;
    vMetallic = iMetallic.x;
    gl_Position = uViewProj * w;
}
)";
static const char* kPbrFrag = R"(
in vec3 vWorld;
in vec3 vN;
in vec4 vLightSpace;
in vec3 vAlbedo;
in float vRough;
in float vMetallic;
out vec4 FragColor;

uniform vec3 uCamPos;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform float uSunIntensity;
uniform sampler2DShadow uShadowMap;

const float PI = 3.14159265359;
float D_GGX(float NoH, float a) { float a2=a*a; float d=NoH*NoH*(a2-1.0)+1.0; return a2/max(PI*d*d,1e-7); }
float G_SchlickGGX(float NoX, float k) { return NoX/(NoX*(1.0-k)+k); }
float G_Smith(float NoV, float NoL, float r) { float k=(r+1.0)*(r+1.0)/8.0; return G_SchlickGGX(NoV,k)*G_SchlickGGX(NoL,k); }
vec3 fresnel(float ct, vec3 F0) { return F0 + (1.0-F0)*pow(1.0-ct,5.0); }
vec3 fresnelRough(float ct, vec3 F0, float r) { return F0 + (max(vec3(1.0-r),F0)-F0)*pow(1.0-ct,5.0); }

float shadowFactor(vec4 lsp, vec3 N, vec3 L) {
    vec3 p = lsp.xyz / lsp.w; p = p*0.5+0.5;
    if (p.z > 1.0) return 1.0;
    float bias = max(0.0025*(1.0-dot(N,L)), 0.0008);
    float sh = 0.0;
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap,0));
    for (int x=-1;x<=1;++x) for (int y=-1;y<=1;++y)
        sh += texture(uShadowMap, vec3(p.xy + vec2(x,y)*texel, p.z - bias));
    return sh/9.0;
}

void main() {
    vec3 N = normalize(vN);
    vec3 V = normalize(uCamPos - vWorld);
    vec3 L = normalize(-uSunDir);
    vec3 H = normalize(V + L);
    float NoV = max(dot(N,V),1e-4);
    float NoL = max(dot(N,L),0.0);
    float NoH = max(dot(N,H),0.0);
    vec3 F0 = mix(vec3(0.04), vAlbedo, vMetallic);
    float rough = clamp(vRough, 0.04, 1.0);

    float D = D_GGX(NoH, rough*rough);
    float G = G_Smith(NoV, NoL, rough);
    vec3  F = fresnel(max(dot(H,V),0.0), F0);
    vec3 spec = (D*G*F) / max(4.0*NoV*NoL, 1e-4);
    vec3 kd = (1.0-F)*(1.0-vMetallic);
    float sh = shadowFactor(vLightSpace, N, L);
    vec3 direct = (kd*vAlbedo/PI + spec) * uSunColor * uSunIntensity * NoL * sh;

    vec3 skyUp = skyColor(vec3(0,1,0), normalize(uSunDir));
    vec3 skyDn = skyColor(vec3(0,-1,0), normalize(uSunDir));
    vec3 irr = mix(skyDn, skyUp, N.y*0.5+0.5);
    vec3 R = reflect(-V, N);
    vec3 pref = mix(skyColor(R, normalize(uSunDir)), irr, rough);
    vec3 Fr = fresnelRough(NoV, F0, rough);
    vec3 kdA = (1.0-Fr)*(1.0-vMetallic);
    vec3 ambient = (kdA*vAlbedo*irr + pref*Fr) * 0.55;

    FragColor = vec4(ambient + direct, 1.0);
}
)";

static const char* kTonemapFrag = R"(
in vec2 vUV;
uniform sampler2D uHDR;
out vec4 FragColor;
vec3 aces(vec3 x) { return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0); }
void main() {
    vec3 hdr = texture(uHDR, vUV).rgb;
    FragColor = vec4(pow(aces(hdr), vec3(1.0/2.2)), 1.0);
}
)";

// ---------------- frustum ----------------
struct Frustum {
    glm::vec4 planes[6];
    void from(const glm::mat4& m) {
        for (int i = 0; i < 3; ++i) {
            planes[i * 2 + 0] = glm::row(m, 3) + glm::row(m, i);
            planes[i * 2 + 1] = glm::row(m, 3) - glm::row(m, i);
        }
        for (auto& p : planes) p /= glm::length(glm::vec3(p));
    }
    bool sphere_in(const glm::vec3& c, float r) const {
        for (const auto& p : planes)
            if (glm::dot(glm::vec3(p), c) + p.w < -r) return false;
        return true;
    }
};

struct InstanceData {
    glm::mat4 model;
    glm::vec4 albedo_rough;
    glm::vec4 metallic;
};

// ---------------- Renderer ----------------
Renderer::Renderer(int w, int h)
    : pbr_((std::string(kInstanceInputs) + kPbrVert).c_str(),
           (std::string(kSkyGLSL) + kPbrFrag).c_str()),
      sky_(kFsVert, (std::string(kSkyGLSL) + kSkyFrag).c_str()),
      shadow_(kShadowVert, kShadowFrag),
      tonemap_(kFsVert, kTonemapFrag),
      shadow_map_(4096, 4096, ColorFormat::None, true) {
    hdr_ = std::make_unique<Framebuffer>(w, h, ColorFormat::RGBA16F, false);
    glCreateVertexArrays(1, &empty_vao_);

    glCreateVertexArrays(1, &draw_vao_);
    glCreateBuffers(1, &instance_vbo_);
    // vertex format on binding 0
    for (unsigned i = 0; i < 3; ++i) {
        glEnableVertexArrayAttrib(draw_vao_, i);
        glVertexArrayAttribBinding(draw_vao_, i, 0);
    }
    glVertexArrayAttribFormat(draw_vao_, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, pos));
    glVertexArrayAttribFormat(draw_vao_, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, normal));
    glVertexArrayAttribFormat(draw_vao_, 2, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, uv));
    // instance format on binding 1 (mat4 = 4 attribs)
    for (unsigned c = 0; c < 4; ++c) {
        glEnableVertexArrayAttrib(draw_vao_, 3 + c);
        glVertexArrayAttribFormat(draw_vao_, 3 + c, 4, GL_FLOAT, GL_FALSE,
                                  offsetof(InstanceData, model) + c * sizeof(glm::vec4));
        glVertexArrayAttribBinding(draw_vao_, 3 + c, 1);
    }
    glEnableVertexArrayAttrib(draw_vao_, 7);
    glVertexArrayAttribFormat(draw_vao_, 7, 4, GL_FLOAT, GL_FALSE, offsetof(InstanceData, albedo_rough));
    glVertexArrayAttribBinding(draw_vao_, 7, 1);
    glEnableVertexArrayAttrib(draw_vao_, 8);
    glVertexArrayAttribFormat(draw_vao_, 8, 4, GL_FLOAT, GL_FALSE, offsetof(InstanceData, metallic));
    glVertexArrayAttribBinding(draw_vao_, 8, 1);
    glVertexArrayBindingDivisor(draw_vao_, 1, 1);
}

Renderer::~Renderer() {
    if (instance_vbo_) glDeleteBuffers(1, &instance_vbo_);
    if (draw_vao_) glDeleteVertexArrays(1, &draw_vao_);
    if (empty_vao_) glDeleteVertexArrays(1, &empty_vao_);
}

void Renderer::ensure_hdr(int w, int h) {
    if (hdr_->width() != w || hdr_->height() != h) hdr_->resize(w, h);
}
void Renderer::ensure_instances(size_t bytes) {
    if (bytes <= instance_capacity_) return;
    instance_capacity_ = std::max(bytes, instance_capacity_ * 2 + 4096);
    glNamedBufferData(instance_vbo_, instance_capacity_, nullptr, GL_DYNAMIC_DRAW);
}

void Renderer::render(Scene& scene, unsigned target_fbo, int w, int h) {
    auto t0 = std::chrono::high_resolution_clock::now();
    ensure_hdr(w, h);
    stats_ = {};

    CameraComp& cam = scene.camera();
    float aspect = h ? float(w) / float(h) : 1.0f;
    glm::mat4 view = cam.view();
    glm::mat4 proj = cam.proj(aspect);
    glm::mat4 view_proj = proj * view;

    DirectionalLight light;
    for (auto [e, dl] : scene.registry.view<DirectionalLight>().each()) { light = dl; break; }
    glm::vec3 sun_dir = glm::normalize(light.direction);

    // ---- gather renderables ----
    struct Item { Mesh* mesh; glm::mat4 model; glm::vec3 wc; float wr;
                  glm::vec3 albedo; float rough; float metallic; };
    std::vector<Item> items;
    glm::vec3 sc(0); int n = 0;
    for (auto [e, t, mr] : scene.registry.view<Transform, MeshRenderer>().each()) {
        if (!mr.gpu) continue;
        glm::mat4 model = t.matrix();
        float ms = std::max({std::abs(t.scale.x), std::abs(t.scale.y), std::abs(t.scale.z)});
        glm::vec3 wc = glm::vec3(model * glm::vec4(mr.gpu->bounds_center(), 1.0f));
        items.push_back({mr.gpu.get(), model, wc, mr.gpu->bounds_radius() * ms,
                         mr.base_color, mr.roughness, mr.metallic});
        sc += t.position; ++n;
    }
    stats_.entities = (int)items.size();
    glm::vec3 center = n ? sc / float(n) : glm::vec3(0);
    float radius = 6.0f;
    for (auto& it : items) radius = std::max(radius, glm::length(it.wc - center) + it.wr);
    radius += 2.0f;

    // ---- frustum cull (parallel) ----
    Frustum fr; fr.from(view_proj);
    std::vector<char> vis(items.size(), 0);
    jobs_.parallel_for(items.size(), [&](size_t i) {
        vis[i] = fr.sphere_in(items[i].wc, items[i].wr) ? 1 : 0;
    });

    // ---- group by mesh ----
    std::unordered_map<Mesh*, std::vector<InstanceData>> visible_groups, all_groups;
    for (size_t i = 0; i < items.size(); ++i) {
        InstanceData d{items[i].model,
                       glm::vec4(items[i].albedo, items[i].rough),
                       glm::vec4(items[i].metallic, 0, 0, 0)};
        all_groups[items[i].mesh].push_back(d);
        if (vis[i]) { visible_groups[items[i].mesh].push_back(d); stats_.visible++; }
    }
    stats_.culled = stats_.entities - stats_.visible;

    // instance buffer sizing: shadow pass (all) + visible pass, packed consecutively
    ensure_instances(std::max<size_t>(items.size() * 2, 1) * sizeof(InstanceData));

    glm::vec3 light_eye = center - sun_dir * (radius * 2.0f);
    glm::vec3 up = std::abs(sun_dir.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    glm::mat4 light_vp = glm::ortho(-radius, radius, -radius, radius, 0.1f, radius * 4.0f)
                       * glm::lookAt(light_eye, center, up);

    GLintptr instance_cursor = 0;   // running byte offset into instance_vbo_
    auto draw_groups = [&](std::unordered_map<Mesh*, std::vector<InstanceData>>& groups) {
        glBindVertexArray(draw_vao_);
        for (auto& [mesh, insts] : groups) {
            if (insts.empty()) continue;
            GLsizeiptr bytes = insts.size() * sizeof(InstanceData);
            if (instance_cursor + bytes > (GLintptr)instance_capacity_) instance_cursor = 0;
            glNamedBufferSubData(instance_vbo_, instance_cursor, bytes, insts.data());
            glVertexArrayVertexBuffer(draw_vao_, 0, mesh->vbo(), 0, sizeof(Vertex));
            glVertexArrayElementBuffer(draw_vao_, mesh->ebo());
            glVertexArrayVertexBuffer(draw_vao_, 1, instance_vbo_, instance_cursor, sizeof(InstanceData));
            glDrawElementsInstanced(GL_TRIANGLES, mesh->index_count(), GL_UNSIGNED_INT,
                                    nullptr, (GLsizei)insts.size());
            instance_cursor += bytes;
            stats_.draw_calls++;
            stats_.instances += (int)insts.size();
        }
    };

    // ---- pass 1: shadow (all casters, not camera-culled) ----
    shadow_map_.bind();
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glCullFace(GL_FRONT);
    shadow_.use();
    shadow_.set("uLightVP", light_vp);
    draw_groups(all_groups);
    glCullFace(GL_BACK);

    // ---- pass 2: HDR scene ----
    hdr_->bind();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glDisable(GL_DEPTH_TEST);
    sky_.use();
    sky_.set("uInvViewProj", glm::inverse(view_proj));
    sky_.set("uCamPos", cam.position);
    sky_.set("uSunDir", sun_dir);
    glBindVertexArray(empty_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);

    stats_.draw_calls = 0; stats_.instances = 0;   // count only visible pass
    glBindTextureUnit(0, shadow_map_.depth_texture());
    pbr_.use();
    pbr_.set("uViewProj", view_proj);
    pbr_.set("uLightVP", light_vp);
    pbr_.set("uCamPos", cam.position);
    pbr_.set("uSunDir", sun_dir);
    pbr_.set("uSunColor", light.color);
    pbr_.set("uSunIntensity", light.intensity);
    pbr_.set("uShadowMap", 0);
    draw_groups(visible_groups);
    stats_.groups = (int)visible_groups.size();

    // ---- pass 3: tonemap ----
    glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT);
    tonemap_.use();
    glBindTextureUnit(0, hdr_->color_texture());
    tonemap_.set("uHDR", 0);
    glBindVertexArray(empty_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);

    auto t1 = std::chrono::high_resolution_clock::now();
    stats_.cpu_ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
}

} // namespace eng
