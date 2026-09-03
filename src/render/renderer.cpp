#include "render/renderer.hpp"
#include "core/log.hpp"
#include "scene/transform_system.hpp"
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace eng {

// ---------------- GLSL ----------------
// skyColor() is the procedural fallback. envColor() samples a loaded equirect
// HDR (uEnv / uHasEnv / uEnvMaxLod / uEnvRot / uEnvIntensity) at a mip chosen by
// the caller -- lod 0 for the sky, a high lod for irradiance, roughness*max for
// prefiltered specular. skyRadiance() picks whichever source is active.
static const char* kSkyGLSL = R"(
uniform sampler2D uEnv;
uniform int   uHasEnv;
uniform float uEnvMaxLod;
uniform float uEnvRot;
uniform float uEnvIntensity;
const float SKY_PI = 3.14159265359;

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
vec3 envColor(vec3 dir, float lod) {
    // atan(0,0) is undefined (NaN on some drivers) -- guard the poles where the
    // longitude is arbitrary anyway.
    float lon = (abs(dir.x) < 1e-5 && abs(dir.z) < 1e-5) ? 0.0 : atan(dir.z, dir.x);
    vec2 uv = vec2(lon / (2.0 * SKY_PI) + 0.5 + uEnvRot,
                   1.0 - acos(clamp(dir.y, -1.0, 1.0)) / SKY_PI);
    vec3 c = textureLod(uEnv, uv, lod).rgb * uEnvIntensity;
    return clamp(c, 0.0, 64.0);   // keep a blown HDR sun from poisoning the sum
}
vec3 skyRadiance(vec3 dir, vec3 sunDir) {
    return uHasEnv == 1 ? envColor(dir, 0.0) : skyColor(dir, sunDir);
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
    FragColor = vec4(skyRadiance(dir, normalize(uSunDir)), 1.0);
}
)";

static const char* kPbrVert = R"(
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
layout(location=3) in vec4 aTangent;
uniform mat4 uViewProj;
out vec3 vWorld;
out vec3 vN;
out vec3 vT;
out vec3 vB;
out vec2 vUV;
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
in vec3 vAlbedo;
in float vRough;
in float vMetallic;
in vec3 vEmissive;
out vec4 FragColor;

uniform vec3 uCamPos;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform float uSunIntensity;
uniform sampler2DArrayShadow uShadowArray;
uniform mat4 uCascadeVP[3];
uniform float uCascadeSplit[3];
uniform mat4 uView;
uniform int uNumLights;
uniform vec4 uLightPos[16];    // xyz position, w range
uniform vec4 uLightColor[16];  // rgb color*intensity, w = spot ? 1 : 0
uniform vec4 uLightDir[16];    // xyz cone axis, w unused
uniform vec2 uLightCone[16];   // x cos(inner), y cos(outer)
uniform sampler2D uBaseColorMap;
uniform sampler2D uNormalMap;
uniform sampler2D uMRMap;
uniform sampler2D uEmissiveMap;
uniform sampler2D uAOMap;
uniform int uHas;   // bitmask: 1 base, 2 normal, 4 mr, 8 emissive, 16 ao
uniform sampler2D uSSAO;
uniform int uSSAOEnabled;
uniform vec2 uViewport;
uniform sampler2DShadow uSpotAtlas;
uniform mat4 uSpotVP[4];
uniform int uSpotSlot[16];       // atlas tile index per light, -1 = no shadow

float spotShadow(int slot, vec3 worldPos) {
    if (slot < 0) return 1.0;
    vec4 lp = uSpotVP[slot] * vec4(worldPos, 1.0);
    if (lp.w <= 0.0) return 1.0;
    vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
    if (p.z > 1.0 || p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return 1.0;
    vec2 tile = vec2(float(slot - 2 * (slot / 2)), float(slot / 2)) * 0.5;
    vec2 uv = tile + p.xy * 0.5;
    float bias = 0.0009;
    float sh = 0.0;
    vec2 tx = vec2(0.5) / vec2(textureSize(uSpotAtlas, 0));
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            sh += texture(uSpotAtlas, vec3(uv + vec2(x, y) * tx, p.z - bias));
    return sh / 9.0;
}

const float PI = 3.14159265359;
float D_GGX(float NoH, float a){float a2=a*a;float d=NoH*NoH*(a2-1.0)+1.0;return a2/max(PI*d*d,1e-7);}
float G_SchlickGGX(float NoX,float k){return NoX/(NoX*(1.0-k)+k);}
float G_Smith(float NoV,float NoL,float r){float k=(r+1.0)*(r+1.0)/8.0;return G_SchlickGGX(NoV,k)*G_SchlickGGX(NoL,k);}
vec3 fresnel(float ct,vec3 F0){return F0+(1.0-F0)*pow(1.0-ct,5.0);}
vec3 fresnelRough(float ct,vec3 F0,float r){return F0+(max(vec3(1.0-r),F0)-F0)*pow(1.0-ct,5.0);}

// BRDF * NoL for one light direction L (unit, surface->light). No radiance term.
vec3 brdf(vec3 N, vec3 V, vec3 L, vec3 albedo, float rough, float metallic, vec3 F0) {
    vec3 H = normalize(V + L);
    float NoV = max(dot(N, V), 1e-4);
    float NoL = max(dot(N, L), 0.0);
    float NoH = max(dot(N, H), 0.0);
    float D = D_GGX(NoH, rough * rough);
    float G = G_Smith(NoV, NoL, rough);
    vec3  F = fresnel(max(dot(H, V), 0.0), F0);
    vec3 spec = (D * G * F) / max(4.0 * NoV * NoL, 1e-4);
    vec3 kd = (1.0 - F) * (1.0 - metallic);
    return (kd * albedo / PI + spec) * NoL;
}

float shadowFactor(vec3 worldPos, vec3 N, vec3 L) {
    float viewDepth = -(uView * vec4(worldPos, 1.0)).z;
    int c = 2;
    if (viewDepth < uCascadeSplit[0]) c = 0;
    else if (viewDepth < uCascadeSplit[1]) c = 1;

    vec4 lsp = uCascadeVP[c] * vec4(worldPos, 1.0);
    vec3 p = lsp.xyz / lsp.w; p = p * 0.5 + 0.5;
    if (p.z > 1.0) return 1.0;
    float bias = max(0.0018 * (1.0 - dot(N, L)), 0.0006) * (c == 0 ? 1.0 : (c == 1 ? 2.0 : 4.0));
    vec2 texel = 1.0 / vec2(textureSize(uShadowArray, 0).xy);
    float sh = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            sh += texture(uShadowArray, vec4(p.xy + vec2(x, y) * texel, float(c), p.z - bias));
    return sh / 9.0;
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

    float sh = shadowFactor(vWorld, N, L);
    vec3 direct = brdf(N, V, L, albedo, rough, metallic, F0) * uSunColor * uSunIntensity * sh;

    for (int i = 0; i < uNumLights; ++i) {
        vec3 lp = uLightPos[i].xyz;
        vec3 Lv = lp - vWorld;
        float dist = length(Lv);
        if (dist < 1e-4) continue;
        Lv /= dist;
        float range = max(uLightPos[i].w, 0.001);
        float att = clamp(1.0 - dist / range, 0.0, 1.0);
        att *= att;
        if (uLightColor[i].w > 0.5) {
            float cd = dot(-Lv, normalize(uLightDir[i].xyz));
            att *= smoothstep(uLightCone[i].y, uLightCone[i].x, cd);
        }
        if (att <= 0.0) continue;
        if (uLightColor[i].w > 0.5) att *= spotShadow(uSpotSlot[i], vWorld);
        if (att <= 0.0) continue;
        direct += brdf(N, V, Lv, albedo, rough, metallic, F0) * uLightColor[i].rgb * att;
    }

    vec3 sd = normalize(uSunDir);
    vec3 R = reflect(-V, N);
    vec3 irr, pref;
    if (uHasEnv == 1) {
        // diffuse: a blurred mip stands in for the cosine-weighted hemisphere
        // integral; scale down and clamp so the HDR sun disc doesn't wash it out
        irr  = min(envColor(N, max(uEnvMaxLod - 1.5, 0.0)), vec3(3.0)) * 0.24;
        // specular: roughness picks the mip; a lighter clamp keeps highlights
        pref = min(envColor(R, clamp(rough, 0.0, 1.0) * (uEnvMaxLod - 1.0)), vec3(40.0));
    } else {
        vec3 skyUp = skyColor(vec3(0,1,0), sd);
        vec3 skyDn = skyColor(vec3(0,-1,0), sd);
        irr = mix(skyDn, skyUp, N.y*0.5+0.5);
        pref = mix(skyColor(R, sd), irr, rough);
    }
    vec3 Fr = fresnelRough(NoV, F0, rough);
    vec3 kdA = (1.0-Fr)*(1.0-metallic);
    float ao = ((uHas & 16) != 0) ? texture(uAOMap, vUV).r : 1.0;
    if (uSSAOEnabled == 1) ao *= texture(uSSAO, gl_FragCoord.xy / uViewport).r;
    vec3 ambient = (kdA*albedo*irr + pref*Fr) * 0.55 * ao;

    vec3 emissive = vEmissive;
    if ((uHas & 8) != 0) emissive += texture(uEmissiveMap, vUV).rgb;

    FragColor = vec4(ambient + direct + emissive, 1.0);
}
)";

static const char* kSsaoFrag = R"(
in vec2 vUV;
uniform sampler2D uDepth;
uniform sampler2D uNoise;
uniform mat4 uProj;
uniform mat4 uInvProj;
uniform vec2 uNoiseScale;   // screen / 4
uniform vec3 uKernel[32];
uniform int uKernelCount;
uniform float uRadius;
uniform float uIntensity;
out float FragColor;

vec3 viewPos(vec2 uv) {
    float d = texture(uDepth, uv).r * 2.0 - 1.0;
    vec4 c = uInvProj * vec4(uv * 2.0 - 1.0, d, 1.0);
    return c.xyz / c.w;
}
void main() {
    float d0 = texture(uDepth, vUV).r;
    if (d0 >= 1.0) { FragColor = 1.0; return; }       // sky
    vec3 P = viewPos(vUV);
    vec3 N = normalize(cross(dFdx(P), dFdy(P)));
    vec3 rvec = normalize(texture(uNoise, vUV * uNoiseScale).xyz * 2.0 - 1.0);
    vec3 T = normalize(rvec - N * dot(rvec, N));
    mat3 TBN = mat3(T, cross(N, T), N);

    float occ = 0.0;
    for (int i = 0; i < uKernelCount; ++i) {
        vec3 sp = P + (TBN * uKernel[i]) * uRadius;
        vec4 off = uProj * vec4(sp, 1.0);
        off.xyz /= off.w;
        vec2 suv = off.xy * 0.5 + 0.5;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;
        float sd = viewPos(suv).z;
        float rc = smoothstep(0.0, 1.0, uRadius / abs(P.z - sd));
        occ += (sd >= sp.z + 0.02 ? 1.0 : 0.0) * rc;
    }
    occ = 1.0 - (occ / float(uKernelCount)) * uIntensity;
    FragColor = clamp(occ, 0.0, 1.0);
}
)";
static const char* kAoBlurFrag = R"(
in vec2 vUV;
uniform sampler2D uAO;
out float FragColor;
void main() {
    vec2 texel = 1.0 / vec2(textureSize(uAO, 0));
    float sum = 0.0;
    for (int x = -2; x < 2; ++x)
        for (int y = -2; y < 2; ++y)
            sum += texture(uAO, vUV + vec2(x, y) * texel).r;
    FragColor = sum / 16.0;
}
)";

static const char* kBrightFrag = R"(
in vec2 vUV;
uniform sampler2D uSrc;
uniform float uThreshold;
out vec4 FragColor;
void main() {
    vec3 c = texture(uSrc, vUV).rgb;
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float k = max(l - uThreshold, 0.0) / max(l, 1e-4);
    FragColor = vec4(c * k, 1.0);
}
)";
static const char* kBlurFrag = R"(
in vec2 vUV;
uniform sampler2D uSrc;
uniform vec2 uDir;          // (texel, 0) or (0, texel)
out vec4 FragColor;
void main() {
    float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 c = texture(uSrc, vUV).rgb * w[0];
    for (int i = 1; i < 5; ++i) {
        c += texture(uSrc, vUV + uDir * float(i)).rgb * w[i];
        c += texture(uSrc, vUV - uDir * float(i)).rgb * w[i];
    }
    FragColor = vec4(c, 1.0);
}
)";
static const char* kTonemapFrag = R"(
in vec2 vUV;
uniform sampler2D uHDR;
uniform sampler2D uBloom;
uniform float uBloomStrength;
uniform float uExposure;
uniform float uVignette;
out vec4 FragColor;
vec3 aces(vec3 x){return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);}
void main(){
    vec3 hdr = texture(uHDR, vUV).rgb * uExposure;
    hdr += texture(uBloom, vUV).rgb * uBloomStrength;
    vec3 col = pow(aces(hdr), vec3(1.0/2.2));
    vec2 d = vUV - 0.5;
    col *= 1.0 - uVignette * dot(d, d) * 2.0;
    FragColor = vec4(col, 1.0);
}
)";
static const char* kParticleVert = R"(
layout(location=0) in vec3 iPos;
layout(location=1) in vec4 iColor;
layout(location=2) in float iSize;
uniform mat4 uViewProj;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
out vec4 vColor;
out vec2 vUV;
const vec2 quad[4] = vec2[](vec2(-1,-1), vec2(1,-1), vec2(-1,1), vec2(1,1));
void main() {
    vec2 q = quad[gl_VertexID];
    vUV = q;
    vColor = iColor;
    vec3 wp = iPos + (uCamRight * q.x + uCamUp * q.y) * iSize;
    gl_Position = uViewProj * vec4(wp, 1.0);
}
)";
static const char* kParticleFrag = R"(
in vec4 vColor;
in vec2 vUV;
out vec4 FragColor;
void main() {
    float d = dot(vUV, vUV);
    if (d > 1.0) discard;
    float a = vColor.a * (1.0 - d);
    FragColor = vec4(vColor.rgb * a, a);
}
)";

static const char* kUiSolidVert = R"(
layout(location=0) in vec2 aPos;
uniform vec2 uScreen;
void main() {
    vec2 p = aPos / uScreen * 2.0 - 1.0;
    gl_Position = vec4(p.x, -p.y, 0.0, 1.0);
}
)";
static const char* kUiSolidFrag = R"(
uniform vec4 uColor;
out vec4 FragColor;
void main() { FragColor = uColor; }
)";
static const char* kUiTextVert = R"(
layout(location=0) in vec4 aPosUV;
uniform vec2 uScreen;
out vec2 vUV;
void main() {
    vec2 p = aPosUV.xy / uScreen * 2.0 - 1.0;
    gl_Position = vec4(p.x, -p.y, 0.0, 1.0);
    vUV = aPosUV.zw;
}
)";
static const char* kUiTextFrag = R"(
in vec2 vUV;
uniform sampler2D uAtlas;
uniform vec4 uColor;
out vec4 FragColor;
void main() { FragColor = vec4(uColor.rgb, uColor.a * texture(uAtlas, vUV).r); }
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
      ssao_(kFsVert, kSsaoFrag),
      ssao_blur_(kFsVert, kAoBlurFrag),
      tonemap_(kFsVert, kTonemapFrag),
      bright_(kFsVert, kBrightFrag),
      blur_(kFsVert, kBlurFrag),
      particle_(kParticleVert, kParticleFrag),
      ui_solid_(kUiSolidVert, kUiSolidFrag),
      ui_text_(kUiTextVert, kUiTextFrag) {
    // cascaded shadow map: one depth texture array, one FBO
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &csm_tex_);
    glTextureStorage3D(csm_tex_, 1, GL_DEPTH_COMPONENT32F, csm_size_, csm_size_, kCascades);
    glTextureParameteri(csm_tex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(csm_tex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(csm_tex_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTextureParameteri(csm_tex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float border[4] = {1, 1, 1, 1};
    glTextureParameterfv(csm_tex_, GL_TEXTURE_BORDER_COLOR, border);
    glTextureParameteri(csm_tex_, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTextureParameteri(csm_tex_, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glCreateFramebuffers(1, &csm_fbo_);
    glNamedFramebufferDrawBuffer(csm_fbo_, GL_NONE);
    glNamedFramebufferReadBuffer(csm_fbo_, GL_NONE);

    // spot-light shadow atlas: one 2D depth texture, tiled 2x2
    glCreateTextures(GL_TEXTURE_2D, 1, &spot_atlas_);
    glTextureStorage2D(spot_atlas_, 1, GL_DEPTH_COMPONENT32F, spot_atlas_size_, spot_atlas_size_);
    glTextureParameteri(spot_atlas_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(spot_atlas_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(spot_atlas_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTextureParameteri(spot_atlas_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    {
        float b[4] = {1, 1, 1, 1};
        glTextureParameterfv(spot_atlas_, GL_TEXTURE_BORDER_COLOR, b);
    }
    glTextureParameteri(spot_atlas_, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTextureParameteri(spot_atlas_, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glCreateFramebuffers(1, &spot_fbo_);
    glNamedFramebufferDrawBuffer(spot_fbo_, GL_NONE);
    glNamedFramebufferReadBuffer(spot_fbo_, GL_NONE);
    glNamedFramebufferTexture(spot_fbo_, GL_DEPTH_ATTACHMENT, spot_atlas_, 0);

    hdr_ = std::make_unique<Framebuffer>(w, h, ColorFormat::RGBA16F, false);
    bloom_a_ = std::make_unique<Framebuffer>(w / 2, h / 2, ColorFormat::RGBA16F, false);
    bloom_b_ = std::make_unique<Framebuffer>(w / 2, h / 2, ColorFormat::RGBA16F, false);

    // SSAO hemisphere kernel (cosine-ish weighted toward the origin) + 4x4 noise
    {
        auto rnd = []() { return float(std::rand()) / float(RAND_MAX); };
        for (int i = 0; i < 24; ++i) {
            glm::vec3 s(rnd() * 2 - 1, rnd() * 2 - 1, rnd());
            s = glm::normalize(s) * rnd();
            float t = float(i) / 24.0f;
            ao_kernel_[i] = s * glm::mix(0.1f, 1.0f, t * t);
        }
        glm::vec3 noise[16];
        for (auto& n : noise) n = {rnd() * 2 - 1, rnd() * 2 - 1, 0.0f};
        glCreateTextures(GL_TEXTURE_2D, 1, &ao_noise_);
        glTextureStorage2D(ao_noise_, 1, GL_RGBA16F, 4, 4);
        glTextureSubImage2D(ao_noise_, 0, 0, 0, 4, 4, GL_RGB, GL_FLOAT, noise);
        glTextureParameteri(ao_noise_, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTextureParameteri(ao_noise_, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTextureParameteri(ao_noise_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(ao_noise_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glCreateFramebuffers(1, &depth_fbo_);
        glNamedFramebufferDrawBuffer(depth_fbo_, GL_NONE);
        glNamedFramebufferReadBuffer(depth_fbo_, GL_NONE);
        glCreateFramebuffers(1, &ao_fbo_);
        glCreateFramebuffers(1, &ao_blur_fbo_);
    }

    glCreateVertexArrays(1, &empty_vao_);
    glCreateVertexArrays(1, &particle_vao_);
    glCreateBuffers(1, &particle_vbo_);
    glCreateVertexArrays(1, &ui_vao_);
    glCreateBuffers(1, &ui_vbo_);
    glEnableVertexArrayAttrib(ui_vao_, 0);
    glVertexArrayAttribFormat(ui_vao_, 0, 4, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(ui_vao_, 0, 0);

    const char* candidates[] = {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
                                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"};
    for (const char* c : candidates) {
        font_ = std::make_unique<Font>(c, 48.0f);
        if (font_->ok()) break;
    }

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

    // particle instance VAO: vec3 pos, vec4 color, float size ; divisor 1
    glEnableVertexArrayAttrib(particle_vao_, 0);
    glVertexArrayAttribFormat(particle_vao_, 0, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(particle_vao_, 0, 0);
    glEnableVertexArrayAttrib(particle_vao_, 1);
    glVertexArrayAttribFormat(particle_vao_, 1, 4, GL_FLOAT, GL_FALSE, sizeof(float) * 3);
    glVertexArrayAttribBinding(particle_vao_, 1, 0);
    glEnableVertexArrayAttrib(particle_vao_, 2);
    glVertexArrayAttribFormat(particle_vao_, 2, 1, GL_FLOAT, GL_FALSE, sizeof(float) * 7);
    glVertexArrayAttribBinding(particle_vao_, 2, 0);
    glVertexArrayBindingDivisor(particle_vao_, 0, 1);
}

Renderer::~Renderer() {
    if (csm_tex_) glDeleteTextures(1, &csm_tex_);
    if (csm_fbo_) glDeleteFramebuffers(1, &csm_fbo_);
    if (spot_atlas_) glDeleteTextures(1, &spot_atlas_);
    if (spot_fbo_) glDeleteFramebuffers(1, &spot_fbo_);
    for (unsigned t : {depth_tex_, ao_tex_, ao_blur_tex_, ao_noise_}) if (t) glDeleteTextures(1, &t);
    for (unsigned f : {depth_fbo_, ao_fbo_, ao_blur_fbo_}) if (f) glDeleteFramebuffers(1, &f);
    if (instance_vbo_) glDeleteBuffers(1, &instance_vbo_);
    if (particle_vbo_) glDeleteBuffers(1, &particle_vbo_);
    if (ui_vbo_) glDeleteBuffers(1, &ui_vbo_);
    if (draw_vao_) glDeleteVertexArrays(1, &draw_vao_);
    if (particle_vao_) glDeleteVertexArrays(1, &particle_vao_);
    if (ui_vao_) glDeleteVertexArrays(1, &ui_vao_);
    if (empty_vao_) glDeleteVertexArrays(1, &empty_vao_);
}

void Renderer::ui_pass(Scene& scene, int w, int h) {
    std::vector<std::tuple<int, uint32_t>> order;
    for (auto [e, ui] : scene.registry.view<UIElement>().each())
        if (ui.visible) order.emplace_back(ui.order, (uint32_t)e);
    if (order.empty()) return;
    std::sort(order.begin(), order.end());

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glm::vec2 screen{(float)w, (float)h};

    auto anchor_origin = [&](const std::string& a, glm::vec2 sz) -> glm::vec2 {
        float ox = 0, oy = 0;
        if (a.find("right") != std::string::npos) ox = w - sz.x;
        else if (a.find("left") == std::string::npos) ox = (w - sz.x) * 0.5f;   // center/top/bottom
        if (a.find("bottom") != std::string::npos) oy = h - sz.y;
        else if (a.find("top") == std::string::npos) oy = (h - sz.y) * 0.5f;
        return {ox, oy};
    };

    auto quad = [&](float x0, float y0, float x1, float y1, const glm::vec4& col) {
        float v[12] = {x0, y0, x1, y0, x1, y1, x0, y0, x1, y1, x0, y1};
        // pad to vec4 stride (pos.xy, 0, 0)
        glm::vec4 buf[6];
        for (int i = 0; i < 6; ++i) buf[i] = {v[i * 2], v[i * 2 + 1], 0, 0};
        if (ui_capacity_ < sizeof(buf)) {
            ui_capacity_ = 1 << 16;
            glNamedBufferData(ui_vbo_, ui_capacity_, nullptr, GL_DYNAMIC_DRAW);
        }
        glNamedBufferSubData(ui_vbo_, 0, sizeof(buf), buf);
        glVertexArrayVertexBuffer(ui_vao_, 0, ui_vbo_, 0, sizeof(glm::vec4));
        ui_solid_.use();
        ui_solid_.set("uScreen", screen);
        ui_solid_.set("uColor", col);
        glBindVertexArray(ui_vao_);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    };

    auto draw_text = [&](const std::string& s, float x, float y, float px, const glm::vec4& col) {
        if (!font_ || !font_->ok() || s.empty()) return;
        std::vector<glm::vec4> verts;
        font_->layout(s, x, y, px / 48.0f, verts);
        if (verts.empty()) return;
        size_t bytes = verts.size() * sizeof(glm::vec4);
        if (ui_capacity_ < bytes) { ui_capacity_ = bytes * 2; glNamedBufferData(ui_vbo_, ui_capacity_, nullptr, GL_DYNAMIC_DRAW); }
        glNamedBufferSubData(ui_vbo_, 0, bytes, verts.data());
        glVertexArrayVertexBuffer(ui_vao_, 0, ui_vbo_, 0, sizeof(glm::vec4));
        ui_text_.use();
        ui_text_.set("uScreen", screen);
        ui_text_.set("uColor", col);
        glBindTextureUnit(0, font_->texture());
        ui_text_.set("uAtlas", 0);
        glBindVertexArray(ui_vao_);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
    };

    for (auto& [ord, eid] : order) {
        const UIElement& ui = scene.registry.get<UIElement>((entt::entity)eid);
        glm::vec2 sz{ui.size.x * w, ui.size.y * h};
        glm::vec2 o = anchor_origin(ui.anchor, sz);
        float sx = (ui.anchor.find("right") != std::string::npos) ? -1.0f : 1.0f;
        float sy = (ui.anchor.find("bottom") != std::string::npos) ? -1.0f : 1.0f;
        float x0 = o.x + sx * ui.pos.x * w, y0 = o.y + sy * ui.pos.y * h;
        float x1 = x0 + sz.x, y1 = y0 + sz.y;

        if (ui.kind == "panel") {
            quad(x0, y0, x1, y1, ui.color);
        } else if (ui.kind == "bar") {
            quad(x0, y0, x1, y1, ui.color);
            float f = glm::clamp(ui.value, 0.0f, 1.0f);
            quad(x0 + 2, y0 + 2, x0 + 2 + (sz.x - 4) * f, y1 - 2, ui.fill_color);
        }
        if (!ui.text.empty()) {
            float tw = font_ ? font_->measure(ui.text, ui.text_size / 48.0f) : 0.0f;
            float tx = (ui.kind == "text") ? x0 : x0 + std::max(6.0f, (sz.x - tw) * 0.5f);
            float ty = (ui.kind == "text") ? y0 : y0 + (sz.y - ui.text_size) * 0.5f;
            draw_text(ui.text, tx, ty, ui.text_size, ui.text_color);
        }
    }

    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::ensure_ssao(int w, int h) {
    if (w != depth_w_ || h != depth_h_) {
        if (depth_tex_) glDeleteTextures(1, &depth_tex_);
        glCreateTextures(GL_TEXTURE_2D, 1, &depth_tex_);
        glTextureStorage2D(depth_tex_, 1, GL_DEPTH_COMPONENT32F, w, h);
        glTextureParameteri(depth_tex_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(depth_tex_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(depth_tex_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(depth_tex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glNamedFramebufferTexture(depth_fbo_, GL_DEPTH_ATTACHMENT, depth_tex_, 0);
        depth_w_ = w; depth_h_ = h;
    }
    int aw = std::max(1, w / 2), ah = std::max(1, h / 2);
    if (aw != ao_w_ || ah != ao_h_) {
        for (unsigned* tp : {&ao_tex_, &ao_blur_tex_}) {
            if (*tp) glDeleteTextures(1, tp);
            glCreateTextures(GL_TEXTURE_2D, 1, tp);
            glTextureStorage2D(*tp, 1, GL_R8, aw, ah);
            glTextureParameteri(*tp, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTextureParameteri(*tp, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameteri(*tp, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(*tp, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        glNamedFramebufferTexture(ao_fbo_, GL_COLOR_ATTACHMENT0, ao_tex_, 0);
        glNamedFramebufferTexture(ao_blur_fbo_, GL_COLOR_ATTACHMENT0, ao_blur_tex_, 0);
        ao_w_ = aw; ao_h_ = ah;
    }
}

void Renderer::ensure_hdr(int w, int h) {
    if (hdr_->width() != w || hdr_->height() != h) {
        hdr_->resize(w, h);
        bloom_a_->resize(std::max(1, w / 2), std::max(1, h / 2));
        bloom_b_->resize(std::max(1, w / 2), std::max(1, h / 2));
    }
}

void Renderer::bloom_pass(int w, int h) {
    int bw = std::max(1, w / 2), bh = std::max(1, h / 2);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(empty_vao_);

    bloom_a_->bind();
    glClear(GL_COLOR_BUFFER_BIT);
    bright_.use();
    glBindTextureUnit(0, hdr_->color_texture());
    bright_.set("uSrc", 0);
    bright_.set("uThreshold", 1.0f);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    blur_.use();
    blur_.set("uSrc", 0);
    bool horizontal = true;
    for (int i = 0; i < 8; ++i) {
        Framebuffer* dst = horizontal ? bloom_b_.get() : bloom_a_.get();
        Framebuffer* src = horizontal ? bloom_a_.get() : bloom_b_.get();
        dst->bind();
        glClear(GL_COLOR_BUFFER_BIT);
        glBindTextureUnit(0, src->color_texture());
        blur_.set("uDir", horizontal ? glm::vec2(1.0f / bw, 0.0f) : glm::vec2(0.0f, 1.0f / bh));
        glDrawArrays(GL_TRIANGLES, 0, 3);
        horizontal = !horizontal;
    }
    // final blurred result ends up in bloom_a_ (after even count of swaps)
    glEnable(GL_DEPTH_TEST);
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

    // punctual lights (point + spot)
    struct GpuLight { glm::vec4 pos; glm::vec4 color; glm::vec4 dir; glm::vec2 cone; };
    std::vector<GpuLight> lights;
    std::vector<int> light_slot;                 // spot-shadow atlas slot per light, -1 = none
    glm::mat4 spot_vp[kSpotShadows];
    int spot_used = 0;
    for (auto [e, wt, pl] : scene.registry.view<WorldTransform, PunctualLight>().each()) {
        if (lights.size() >= 16) break;
        GpuLight g;
        g.pos = glm::vec4(wt.position, pl.range);
        g.color = glm::vec4(pl.color * pl.intensity, pl.spot ? 1.0f : 0.0f);
        glm::vec3 d = glm::normalize(pl.direction);
        g.dir = glm::vec4(d, 0.0f);
        g.cone = {std::cos(glm::radians(pl.inner_deg)), std::cos(glm::radians(pl.outer_deg))};
        lights.push_back(g);

        int slot = -1;
        if (pl.spot && pl.cast_shadows && spot_used < kSpotShadows) {
            slot = spot_used++;
            glm::vec3 up = std::abs(d.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
            float fov = glm::radians(std::min(179.0f, pl.outer_deg * 2.0f + 6.0f));
            glm::mat4 lproj = glm::perspective(fov, 1.0f, 0.05f, std::max(1.0f, pl.range));
            glm::mat4 lview = glm::lookAt(wt.position, wt.position + d, up);
            spot_vp[slot] = lproj * lview;
        }
        light_slot.push_back(slot);
    }

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
    // one frame uploads: kCascades shadow passes (all groups) + 1 visible pass,
    // packed consecutively into the ring buffer -- size for the worst case so the
    // cursor never wraps and stomps data a pending draw still needs.
    ensure_instances(std::max<size_t>(items.size() * (kCascades + 2), 1) * sizeof(InstanceData));

    // ---- cascaded shadow maps: fit an ortho box to each view sub-frustum ----
    glm::vec3 up = std::abs(sun_dir.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    float cs_near = cam.near_z;
    float cs_far = std::min(cam.far_z, std::max(40.0f, radius * 3.0f));
    float splits[kCascades + 1];
    splits[0] = cs_near;
    for (int i = 1; i <= kCascades; ++i) {
        float p = float(i) / kCascades;
        float lg = cs_near * std::pow(cs_far / cs_near, p);
        float ln = cs_near + (cs_far - cs_near) * p;
        splits[i] = 0.7f * lg + 0.3f * ln;
    }

    glm::mat4 cascade_vp[kCascades];
    float cascade_split_view[kCascades];
    for (int c = 0; c < kCascades; ++c) {
        cascade_split_view[c] = splits[c + 1];
        glm::mat4 sub_proj = glm::perspective(glm::radians(cam.fov_deg), aspect, splits[c], splits[c + 1]);
        glm::mat4 inv = glm::inverse(sub_proj * view);
        glm::vec3 corners[8];
        int k = 0;
        for (int x = 0; x < 2; ++x)
            for (int y = 0; y < 2; ++y)
                for (int z = 0; z < 2; ++z) {
                    glm::vec4 p = inv * glm::vec4(x ? 1.f : -1.f, y ? 1.f : -1.f, z ? 1.f : -1.f, 1.f);
                    corners[k++] = glm::vec3(p) / p.w;
                }
        glm::vec3 cc(0);
        for (auto& p : corners) cc += p;
        cc /= 8.0f;
        float r = 0.0f;
        for (auto& p : corners) r = std::max(r, glm::length(p - cc));
        r = std::ceil(r * 16.0f) / 16.0f;

        glm::mat4 lview = glm::lookAt(cc - sun_dir * (r * 6.0f), cc, up);
        glm::vec3 cls = glm::vec3(lview * glm::vec4(cc, 1.0f));
        float tpu = csm_size_ / (2.0f * r);
        cls.x = std::floor(cls.x * tpu) / tpu;
        cls.y = std::floor(cls.y * tpu) / tpu;
        glm::mat4 lproj = glm::ortho(cls.x - r, cls.x + r, cls.y - r, cls.y + r, r, r * 12.0f);
        cascade_vp[c] = lproj * lview;
    }

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

    // pass 1: shadow (one depth render per cascade)
    glBindFramebuffer(GL_FRAMEBUFFER, csm_fbo_);
    glViewport(0, 0, csm_size_, csm_size_);
    glEnable(GL_DEPTH_TEST);
    glCullFace(GL_FRONT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 2.0f);
    shadow_.use();
    shadow_.set("uSkinned", 0);
    glBindVertexArray(draw_vao_);
    for (int c = 0; c < kCascades; ++c) {
        glNamedFramebufferTextureLayer(csm_fbo_, GL_DEPTH_ATTACHMENT, csm_tex_, 0, c);
        glClear(GL_DEPTH_BUFFER_BIT);
        shadow_.set("uLightVP", cascade_vp[c]);
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
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glCullFace(GL_BACK);

    // pass 1a: spot-light shadow atlas (2x2 tiles)
    if (spot_used > 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, spot_fbo_);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.0f, 3.0f);
        glEnable(GL_SCISSOR_TEST);
        shadow_.use();
        shadow_.set("uSkinned", 0);
        glBindVertexArray(draw_vao_);
        int half = spot_atlas_size_ / 2;
        for (int s = 0; s < spot_used; ++s) {
            int tx = (s % 2) * half, ty = (s / 2) * half;
            glViewport(tx, ty, half, half);
            glScissor(tx, ty, half, half);
            glClear(GL_DEPTH_BUFFER_BIT);
            shadow_.set("uLightVP", spot_vp[s]);
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
        }
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }

    // pass 1b: SSAO -- full-res depth prepass, half-res AO, blur
    bool ssao_on = scene.env.value("ssao", true);
    float ssao_radius = scene.env.value("ssao_radius", 0.6f);
    float ssao_intensity = scene.env.value("ssao_intensity", 1.1f);
    ensure_ssao(w, h);
    if (!ssao_on) {
        // keep uSSAO pointing at a valid, all-white texture so PBR sampling of
        // texture unit 6 is always well-defined
        float white = 1.0f;
        glClearTexImage(ao_blur_tex_, 0, GL_RED, GL_FLOAT, &white);
    }
    if (ssao_on) {
        glBindFramebuffer(GL_FRAMEBUFFER, depth_fbo_);
        glViewport(0, 0, w, h);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
        shadow_.use();
        shadow_.set("uSkinned", 0);
        shadow_.set("uLightVP", view_proj);
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

        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(empty_vao_);
        glBindFramebuffer(GL_FRAMEBUFFER, ao_fbo_);
        glViewport(0, 0, ao_w_, ao_h_);
        ssao_.use();
        glBindTextureUnit(0, depth_tex_);
        glBindTextureUnit(1, ao_noise_);
        ssao_.set("uDepth", 0);
        ssao_.set("uNoise", 1);
        ssao_.set("uProj", proj);
        ssao_.set("uInvProj", glm::inverse(proj));
        ssao_.set("uNoiseScale", glm::vec2(ao_w_ / 4.0f, ao_h_ / 4.0f));
        ssao_.set("uKernelCount", 24);
        ssao_.set("uRadius", ssao_radius);
        ssao_.set("uIntensity", ssao_intensity);
        glUniform3fv(glGetUniformLocation(ssao_.id(), "uKernel"), 24, (const float*)ao_kernel_);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glBindFramebuffer(GL_FRAMEBUFFER, ao_blur_fbo_);
        glViewport(0, 0, ao_w_, ao_h_);
        ssao_blur_.use();
        glBindTextureUnit(0, ao_tex_);
        ssao_blur_.set("uAO", 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glEnable(GL_DEPTH_TEST);
    }

    // equirect HDR IBL: (re)load when the scene's hdri path changes
    {
        std::string hdri = scene.env.value("hdri", std::string());
        if (!hdri.empty() && hdri != env_.path()) {
            namespace fs = std::filesystem;
            std::string resolved = hdri;
            if (!fs::exists(resolved)) resolved = std::string(ENGINE_ASSET_DIR) + "/" + hdri;
            env_.load(resolved);
        } else if (hdri.empty() && !env_.path().empty()) {
            env_.load("");
        }
    }
    bool has_env = env_.ok();
    float env_intensity = scene.env.value("hdri_intensity", 1.0f);
    float env_rot = scene.env.value("hdri_rotation", 0.0f) / 360.0f;
    // uEnv must always point at a valid 2D texture (a mismatched sampler type on
    // a bound unit makes the whole draw a GL_INVALID_OPERATION no-op).
    glBindTextureUnit(8, has_env ? env_.texture() : ao_noise_);
    auto set_env = [&](Shader& s) {
        s.set("uEnv", 8);
        s.set("uHasEnv", has_env ? 1 : 0);
        s.set("uEnvMaxLod", (float)env_.max_lod());
        s.set("uEnvRot", env_rot);
        s.set("uEnvIntensity", env_intensity);
    };

    // pass 2: HDR scene
    hdr_->bind();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    sky_.use();
    sky_.set("uInvViewProj", glm::inverse(view_proj));
    sky_.set("uCamPos", cam.position);
    sky_.set("uSunDir", sun_dir);
    set_env(sky_);
    glBindVertexArray(empty_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);

    glBindTextureUnit(0, csm_tex_);
    pbr_.use();
    set_env(pbr_);
    pbr_.set("uSkinned", 0);
    pbr_.set("uViewProj", view_proj);
    pbr_.set("uView", view);
    pbr_.set("uCamPos", cam.position);
    pbr_.set("uSunDir", sun_dir);
    pbr_.set("uSunColor", light.color);
    pbr_.set("uSunIntensity", light.intensity);
    pbr_.set("uShadowArray", 0);
    glUniformMatrix4fv(glGetUniformLocation(pbr_.id(), "uCascadeVP"), kCascades, GL_FALSE,
                       (const float*)cascade_vp);
    glUniform1fv(glGetUniformLocation(pbr_.id(), "uCascadeSplit"), kCascades, cascade_split_view);
    pbr_.set("uNumLights", (int)lights.size());
    if (!lights.empty()) {
        unsigned prog = pbr_.id();
        std::vector<glm::vec4> pos, col, dir; std::vector<glm::vec2> cone;
        for (auto& g : lights) { pos.push_back(g.pos); col.push_back(g.color);
                                 dir.push_back(g.dir); cone.push_back(g.cone); }
        glUniform4fv(glGetUniformLocation(prog, "uLightPos"), (GLsizei)pos.size(), (const float*)pos.data());
        glUniform4fv(glGetUniformLocation(prog, "uLightColor"), (GLsizei)col.size(), (const float*)col.data());
        glUniform4fv(glGetUniformLocation(prog, "uLightDir"), (GLsizei)dir.size(), (const float*)dir.data());
        glUniform2fv(glGetUniformLocation(prog, "uLightCone"), (GLsizei)cone.size(), (const float*)cone.data());
    }
    {
        int slots[16];
        for (int i = 0; i < 16; ++i) slots[i] = (i < (int)light_slot.size()) ? light_slot[i] : -1;
        glUniform1iv(glGetUniformLocation(pbr_.id(), "uSpotSlot"), 16, slots);
        if (spot_used > 0)
            glUniformMatrix4fv(glGetUniformLocation(pbr_.id(), "uSpotVP"), spot_used, GL_FALSE,
                               (const float*)spot_vp);
        glBindTextureUnit(7, spot_atlas_);
        pbr_.set("uSpotAtlas", 7);
    }
    pbr_.set("uBaseColorMap", 1);
    pbr_.set("uNormalMap", 2);
    pbr_.set("uMRMap", 3);
    pbr_.set("uEmissiveMap", 4);
    pbr_.set("uAOMap", 5);
    pbr_.set("uSSAOEnabled", 1);   // uSSAO is always a valid texture (white when off)
    pbr_.set("uViewport", glm::vec2((float)w, (float)h));
    glBindTextureUnit(6, ao_blur_tex_);
    pbr_.set("uSSAO", 6);
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

    // particles (additive, into HDR)
    hdr_->bind();
    {
        glm::vec3 cr = glm::normalize(glm::vec3(glm::row(view, 0)));
        glm::vec3 cu = glm::normalize(glm::vec3(glm::row(view, 1)));
        struct PInst { glm::vec3 pos; glm::vec4 col; float size; };
        std::vector<PInst> insts;
        for (auto [e, em] : scene.registry.view<ParticleEmitter>().each())
            for (const auto& p : em.particles) {
                float t = 1.0f - glm::clamp(p.life / p.max_life, 0.0f, 1.0f);
                insts.push_back({p.pos, glm::mix(em.start_color, em.end_color, t),
                                 glm::mix(em.start_size, em.end_size, t)});
            }
        if (!insts.empty()) {
            size_t bytes = insts.size() * sizeof(PInst);
            if (bytes > particle_capacity_) {
                particle_capacity_ = bytes * 2;
                glNamedBufferData(particle_vbo_, particle_capacity_, nullptr, GL_DYNAMIC_DRAW);
            }
            glNamedBufferSubData(particle_vbo_, 0, bytes, insts.data());
            glVertexArrayVertexBuffer(particle_vao_, 0, particle_vbo_, 0, sizeof(PInst));
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            glDepthMask(GL_FALSE);
            particle_.use();
            particle_.set("uViewProj", view_proj);
            particle_.set("uCamRight", cr);
            particle_.set("uCamUp", cu);
            glBindVertexArray(particle_vao_);
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)insts.size());
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            stats_.draw_calls++;
        }
    }

    // bloom
    bloom_pass(w, h);

    // pass 3: tonemap + bloom composite
    glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT);
    tonemap_.use();
    glBindTextureUnit(0, hdr_->color_texture());
    glBindTextureUnit(1, bloom_a_->color_texture());
    tonemap_.set("uHDR", 0);
    tonemap_.set("uBloom", 1);
    tonemap_.set("uBloomStrength", 0.04f);
    tonemap_.set("uExposure", 1.0f);
    tonemap_.set("uVignette", 0.25f);
    glBindVertexArray(empty_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);

    ui_pass(scene, w, h);

    auto t1 = std::chrono::high_resolution_clock::now();
    stats_.cpu_ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
}

} // namespace eng
