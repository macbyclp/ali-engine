#include "render/renderer.hpp"
#include "core/log.hpp"
#include "scene/transform_system.hpp"
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
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

// vertex 0..3 = pos/normal/uv/tangent, 11 = joints, 12 = weights
// instance 4..7 = mat4, 8 = albedo+rough, 9 = metallic + uvscale, 10 = emissive
static const char* kInstanceInputs = R"(
layout(location=4) in mat4 iModel;
layout(location=8) in vec4 iAlbedoRough;
layout(location=9) in vec4 iMetalUV;
layout(location=10) in vec4 iEmissive;
layout(location=11) in ivec4 aJoints;
layout(location=12) in vec4 aWeights;
uniform int uSkinned;
uniform mat4 uJoints[128];
mat4 skinMatrix() {
    return aWeights.x * uJoints[aJoints.x] + aWeights.y * uJoints[aJoints.y]
         + aWeights.z * uJoints[aJoints.z] + aWeights.w * uJoints[aJoints.w];
}
)";

static const char* kShadowVert = R"(
layout(location=0) in vec3 aPos;
layout(location=4) in mat4 iModel;
layout(location=11) in ivec4 aJoints;
layout(location=12) in vec4 aWeights;
uniform mat4 uLightVP;
uniform int uSkinned;
uniform mat4 uJoints[128];
void main() {
    vec3 p = aPos;
    if (uSkinned == 1) {
        mat4 sm = aWeights.x * uJoints[aJoints.x] + aWeights.y * uJoints[aJoints.y]
                + aWeights.z * uJoints[aJoints.z] + aWeights.w * uJoints[aJoints.w];
        p = (sm * vec4(aPos, 1.0)).xyz;
    }
    gl_Position = uLightVP * iModel * vec4(p, 1.0);
}
)";
static const char* kShadowFrag = R"(void main() {})";

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
layout(location=3) in vec4 aTangent;
uniform mat4 uViewProj;
uniform mat4 uLightVP;
out vec3 vWorld;
out vec3 vN;
out vec3 vT;
out vec3 vB;
out vec2 vUV;
out vec4 vLightSpace;
out vec3 vAlbedo;
out float vRough;
out float vMetallic;
out vec3 vEmissive;
void main() {
    vec3 p = aPos;
    vec3 nrm = aNormal;
    vec3 tng = aTangent.xyz;
    if (uSkinned == 1) {
        mat4 sm = skinMatrix();
        p = (sm * vec4(aPos, 1.0)).xyz;
        mat3 sm3 = mat3(sm);
        nrm = sm3 * aNormal;
        tng = sm3 * aTangent.xyz;
    }
    vec4 w = iModel * vec4(p, 1.0);
    vWorld = w.xyz;
    mat3 nm = mat3(transpose(inverse(iModel)));
    vN = normalize(nm * nrm);
    vT = normalize(nm * tng);
    vB = cross(vN, vT) * aTangent.w;
    vUV = aUV * iMetalUV.yz;
    vLightSpace = uLightVP * w;
    vAlbedo = iAlbedoRough.rgb;
    vRough = iAlbedoRough.a;
    vMetallic = iMetalUV.x;
    vEmissive = iEmissive.rgb;
    gl_Position = uViewProj * w;
}
)";
static const char* kPbrFrag = R"(
in vec3 vWorld;
in vec3 vN;
in vec3 vT;
in vec3 vB;
in vec2 vUV;
in vec4 vLightSpace;
in vec3 vAlbedo;
in float vRough;
in float vMetallic;
in vec3 vEmissive;
out vec4 FragColor;

uniform vec3 uCamPos;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform float uSunIntensity;
uniform sampler2DShadow uShadowMap;
uniform sampler2D uBaseColorMap;
uniform sampler2D uNormalMap;
uniform sampler2D uMRMap;
uniform sampler2D uEmissiveMap;
uniform sampler2D uAOMap;
uniform int uHas;   // bitmask: 1 base, 2 normal, 4 mr, 8 emissive, 16 ao

const float PI = 3.14159265359;
float D_GGX(float NoH, float a){float a2=a*a;float d=NoH*NoH*(a2-1.0)+1.0;return a2/max(PI*d*d,1e-7);}
float G_SchlickGGX(float NoX,float k){return NoX/(NoX*(1.0-k)+k);}
float G_Smith(float NoV,float NoL,float r){float k=(r+1.0)*(r+1.0)/8.0;return G_SchlickGGX(NoV,k)*G_SchlickGGX(NoL,k);}
vec3 fresnel(float ct,vec3 F0){return F0+(1.0-F0)*pow(1.0-ct,5.0);}
vec3 fresnelRough(float ct,vec3 F0,float r){return F0+(max(vec3(1.0-r),F0)-F0)*pow(1.0-ct,5.0);}

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
    vec3 albedo = vAlbedo;
    if ((uHas & 1) != 0) albedo *= texture(uBaseColorMap, vUV).rgb;
    float metallic = vMetallic;
    float rough = vRough;
    if ((uHas & 4) != 0) { vec3 mr = texture(uMRMap, vUV).rgb; rough *= mr.g; metallic *= mr.b; }
    rough = clamp(rough, 0.04, 1.0);

    vec3 N = normalize(vN);
    if ((uHas & 2) != 0) {
        vec3 n = texture(uNormalMap, vUV).xyz * 2.0 - 1.0;
        mat3 TBN = mat3(normalize(vT), normalize(vB), N);
        N = normalize(TBN * n);
    }
    vec3 V = normalize(uCamPos - vWorld);
    vec3 L = normalize(-uSunDir);
    vec3 H = normalize(V + L);
    float NoV = max(dot(N,V),1e-4);
    float NoL = max(dot(N,L),0.0);
    float NoH = max(dot(N,H),0.0);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float D = D_GGX(NoH, rough*rough);
    float G = G_Smith(NoV, NoL, rough);
    vec3  F = fresnel(max(dot(H,V),0.0), F0);
    vec3 spec = (D*G*F) / max(4.0*NoV*NoL, 1e-4);
    vec3 kd = (1.0-F)*(1.0-metallic);
    float sh = shadowFactor(vLightSpace, N, L);
    vec3 direct = (kd*albedo/PI + spec) * uSunColor * uSunIntensity * NoL * sh;

    vec3 skyUp = skyColor(vec3(0,1,0), normalize(uSunDir));
    vec3 skyDn = skyColor(vec3(0,-1,0), normalize(uSunDir));
    vec3 irr = mix(skyDn, skyUp, N.y*0.5+0.5);
    vec3 R = reflect(-V, N);
    vec3 pref = mix(skyColor(R, normalize(uSunDir)), irr, rough);
    vec3 Fr = fresnelRough(NoV, F0, rough);
    vec3 kdA = (1.0-Fr)*(1.0-metallic);
    float ao = ((uHas & 16) != 0) ? texture(uAOMap, vUV).r : 1.0;
    vec3 ambient = (kdA*albedo*irr + pref*Fr) * 0.55 * ao;

    vec3 emissive = vEmissive;
    if ((uHas & 8) != 0) emissive += texture(uEmissiveMap, vUV).rgb;

    FragColor = vec4(ambient + direct + emissive, 1.0);
}
)";

static const char* kTonemapFrag = R"(
in vec2 vUV;
uniform sampler2D uHDR;
out vec4 FragColor;
vec3 aces(vec3 x){return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);}
void main(){ FragColor = vec4(pow(aces(texture(uHDR,vUV).rgb), vec3(1.0/2.2)), 1.0); }
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
    glm::vec4 metal_uv;     // metallic, uvscale.x, uvscale.y, _
    glm::vec4 emissive;
};

struct MatKey {
    Mesh* mesh;
    unsigned base, normal, mr, emissive, ao;
    bool operator==(const MatKey& o) const {
        return mesh == o.mesh && base == o.base && normal == o.normal &&
               mr == o.mr && emissive == o.emissive && ao == o.ao;
    }
};
struct MatKeyHash {
    size_t operator()(const MatKey& k) const {
        size_t h = std::hash<void*>()(k.mesh);
        for (unsigned v : {k.base, k.normal, k.mr, k.emissive, k.ao})
            h = h * 1099511628211ull ^ v;
        return h;
    }
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
    for (unsigned i = 0; i < 4; ++i) {
        glEnableVertexArrayAttrib(draw_vao_, i);
        glVertexArrayAttribBinding(draw_vao_, i, 0);
    }
    glVertexArrayAttribFormat(draw_vao_, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, pos));
    glVertexArrayAttribFormat(draw_vao_, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, normal));
    glVertexArrayAttribFormat(draw_vao_, 2, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, uv));
    glVertexArrayAttribFormat(draw_vao_, 3, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, tangent));
    glEnableVertexArrayAttrib(draw_vao_, 11);
    glVertexArrayAttribIFormat(draw_vao_, 11, 4, GL_INT, offsetof(Vertex, joints));
    glVertexArrayAttribBinding(draw_vao_, 11, 0);
    glEnableVertexArrayAttrib(draw_vao_, 12);
    glVertexArrayAttribFormat(draw_vao_, 12, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, weights));
    glVertexArrayAttribBinding(draw_vao_, 12, 0);

    for (unsigned c = 0; c < 4; ++c) {
        glEnableVertexArrayAttrib(draw_vao_, 4 + c);
        glVertexArrayAttribFormat(draw_vao_, 4 + c, 4, GL_FLOAT, GL_FALSE,
                                  offsetof(InstanceData, model) + c * sizeof(glm::vec4));
        glVertexArrayAttribBinding(draw_vao_, 4 + c, 1);
    }
    auto inst_vec4 = [&](unsigned loc, size_t off) {
        glEnableVertexArrayAttrib(draw_vao_, loc);
        glVertexArrayAttribFormat(draw_vao_, loc, 4, GL_FLOAT, GL_FALSE, (GLuint)off);
        glVertexArrayAttribBinding(draw_vao_, loc, 1);
    };
    inst_vec4(8, offsetof(InstanceData, albedo_rough));
    inst_vec4(9, offsetof(InstanceData, metal_uv));
    inst_vec4(10, offsetof(InstanceData, emissive));
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
    update_world_transforms(scene);

    CameraComp& cam = scene.camera();
    float aspect = h ? float(w) / float(h) : 1.0f;
    glm::mat4 view = cam.view();
    glm::mat4 proj = cam.proj(aspect);
    glm::mat4 view_proj = proj * view;

    DirectionalLight light;
    for (auto [e, dl] : scene.registry.view<DirectionalLight>().each()) { light = dl; break; }
    glm::vec3 sun_dir = glm::normalize(light.direction);

    struct Item { MeshRenderer* mr; Mesh* mesh; glm::mat4 model; glm::vec3 wc; float wr; };
    std::vector<Item> items;
    struct SkinItem { MeshRenderer* mr; glm::mat4 model; const std::vector<glm::mat4>* joints; };
    std::vector<SkinItem> skinned;
    glm::vec3 sc(0); int nn = 0;
    for (auto [e, wt, mr] : scene.registry.view<WorldTransform, MeshRenderer>().each()) {
        if (!mr.gpu) continue;
        const glm::mat4& model = wt.matrix;
        glm::vec3 s{glm::length(glm::vec3(model[0])), glm::length(glm::vec3(model[1])),
                    glm::length(glm::vec3(model[2]))};
        float ms = std::max({s.x, s.y, s.z});
        glm::vec3 wc = glm::vec3(model * glm::vec4(mr.gpu->bounds_center(), 1.0f));

        if (mr.skinned) {
            auto* ap = scene.registry.try_get<AnimationPlayer>(e);
            const std::vector<glm::mat4>* jm =
                (ap && !ap->joint_matrices.empty()) ? &ap->joint_matrices : nullptr;
            skinned.push_back({&mr, model, jm});
            sc += wt.position; ++nn;
            continue;
        }
        items.push_back({&mr, mr.gpu.get(), model, wc, mr.gpu->bounds_radius() * ms});
        sc += wt.position; ++nn;
    }
    stats_.entities = (int)items.size();
    glm::vec3 center = nn ? sc / float(nn) : glm::vec3(0);
    float radius = 6.0f;
    for (auto& it : items) radius = std::max(radius, glm::length(it.wc - center) + it.wr);
    radius += 2.0f;

    Frustum fr; fr.from(view_proj);
    std::vector<char> vis(items.size(), 0);
    jobs_.parallel_for(items.size(), [&](size_t i) {
        vis[i] = fr.sphere_in(items[i].wc, items[i].wr) ? 1 : 0;
    });

    auto tid = [](const std::shared_ptr<Texture>& t) { return t ? t->id() : 0u; };
    std::unordered_map<MatKey, std::vector<InstanceData>, MatKeyHash> visible_groups, all_groups;
    for (size_t i = 0; i < items.size(); ++i) {
        MeshRenderer& m = *items[i].mr;
        MatKey key{items[i].mesh, tid(m.t_base), tid(m.t_normal), tid(m.t_mr),
                   tid(m.t_emissive), tid(m.t_ao)};
        InstanceData d{items[i].model,
                       glm::vec4(m.base_color, m.roughness),
                       glm::vec4(m.metallic, m.uv_scale.x, m.uv_scale.y, 0),
                       glm::vec4(m.emissive, 0)};
        all_groups[key].push_back(d);
        if (vis[i]) { visible_groups[key].push_back(d); stats_.visible++; }
    }
    stats_.culled = stats_.entities - stats_.visible;
    ensure_instances(std::max<size_t>(items.size() * 2, 1) * sizeof(InstanceData));

    glm::vec3 light_eye = center - sun_dir * (radius * 2.0f);
    glm::vec3 up = std::abs(sun_dir.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    glm::mat4 light_vp = glm::ortho(-radius, radius, -radius, radius, 0.1f, radius * 4.0f)
                       * glm::lookAt(light_eye, center, up);

    GLintptr cursor = 0;
    auto upload = [&](const std::vector<InstanceData>& insts) -> GLintptr {
        GLsizeiptr bytes = insts.size() * sizeof(InstanceData);
        if (cursor + bytes > (GLintptr)instance_capacity_) cursor = 0;
        glNamedBufferSubData(instance_vbo_, cursor, bytes, insts.data());
        GLintptr at = cursor;
        cursor += bytes;
        return at;
    };

    auto draw_skinned = [&](Shader& sh) {
        for (auto& si : skinned) {
            Mesh* mesh = si.mr->gpu.get();
            if (!mesh) continue;
            InstanceData d{si.model, glm::vec4(si.mr->base_color, si.mr->roughness),
                           glm::vec4(si.mr->metallic, si.mr->uv_scale.x, si.mr->uv_scale.y, 0),
                           glm::vec4(si.mr->emissive, 0)};
            std::vector<InstanceData> one{d};
            GLintptr at = upload(one);
            if (si.joints) {
                int cnt = std::min((int)si.joints->size(), kMaxJoints);
                sh.set("uSkinned", 1);
                glUniformMatrix4fv(glGetUniformLocation(sh.id(), "uJoints"), cnt, GL_FALSE,
                                   (const float*)si.joints->data());
            } else {
                sh.set("uSkinned", 0);
            }
            glVertexArrayVertexBuffer(draw_vao_, 0, mesh->vbo(), 0, sizeof(Vertex));
            glVertexArrayElementBuffer(draw_vao_, mesh->ebo());
            glVertexArrayVertexBuffer(draw_vao_, 1, instance_vbo_, at, sizeof(InstanceData));
            glDrawElementsInstanced(GL_TRIANGLES, mesh->index_count(), GL_UNSIGNED_INT, nullptr, 1);
            sh.set("uSkinned", 0);
            stats_.draw_calls++;
            stats_.instances += 1;
        }
    };

    // pass 1: shadow
    shadow_map_.bind();
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glCullFace(GL_FRONT);
    shadow_.use();
    shadow_.set("uLightVP", light_vp);
    shadow_.set("uSkinned", 0);
    glBindVertexArray(draw_vao_);
    for (auto& [key, insts] : all_groups) {
        if (insts.empty()) continue;
        GLintptr at = upload(insts);
        glVertexArrayVertexBuffer(draw_vao_, 0, key.mesh->vbo(), 0, sizeof(Vertex));
        glVertexArrayElementBuffer(draw_vao_, key.mesh->ebo());
        glVertexArrayVertexBuffer(draw_vao_, 1, instance_vbo_, at, sizeof(InstanceData));
        glDrawElementsInstanced(GL_TRIANGLES, key.mesh->index_count(), GL_UNSIGNED_INT,
                                nullptr, (GLsizei)insts.size());
    }
    draw_skinned(shadow_);
    glCullFace(GL_BACK);

    // pass 2: HDR scene
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

    glBindTextureUnit(0, shadow_map_.depth_texture());
    pbr_.use();
    pbr_.set("uSkinned", 0);
    pbr_.set("uViewProj", view_proj);
    pbr_.set("uLightVP", light_vp);
    pbr_.set("uCamPos", cam.position);
    pbr_.set("uSunDir", sun_dir);
    pbr_.set("uSunColor", light.color);
    pbr_.set("uSunIntensity", light.intensity);
    pbr_.set("uShadowMap", 0);
    pbr_.set("uBaseColorMap", 1);
    pbr_.set("uNormalMap", 2);
    pbr_.set("uMRMap", 3);
    pbr_.set("uEmissiveMap", 4);
    pbr_.set("uAOMap", 5);
    glBindVertexArray(draw_vao_);
    for (auto& [key, insts] : visible_groups) {
        if (insts.empty()) continue;
        int has = 0;
        if (key.base) { glBindTextureUnit(1, key.base); has |= 1; }
        if (key.normal) { glBindTextureUnit(2, key.normal); has |= 2; }
        if (key.mr) { glBindTextureUnit(3, key.mr); has |= 4; }
        if (key.emissive) { glBindTextureUnit(4, key.emissive); has |= 8; }
        if (key.ao) { glBindTextureUnit(5, key.ao); has |= 16; }
        pbr_.set("uHas", has);
        GLintptr at = upload(insts);
        glVertexArrayVertexBuffer(draw_vao_, 0, key.mesh->vbo(), 0, sizeof(Vertex));
        glVertexArrayElementBuffer(draw_vao_, key.mesh->ebo());
        glVertexArrayVertexBuffer(draw_vao_, 1, instance_vbo_, at, sizeof(InstanceData));
        glDrawElementsInstanced(GL_TRIANGLES, key.mesh->index_count(), GL_UNSIGNED_INT,
                                nullptr, (GLsizei)insts.size());
        stats_.draw_calls++;
        stats_.instances += (int)insts.size();
    }
    pbr_.set("uHas", 0);
    draw_skinned(pbr_);
    stats_.groups = (int)visible_groups.size();
    stats_.entities += (int)skinned.size();
    stats_.visible += (int)skinned.size();

    // pass 3: tonemap
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
