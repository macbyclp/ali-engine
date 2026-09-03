#include "geo/csg.hpp"
#include <glm/glm.hpp>
#include <algorithm>
#include <memory>

namespace eng {
namespace {

constexpr float kEps = 1e-5f;

struct CV {   // csg vertex
    glm::vec3 pos{0}, normal{0, 1, 0};
    glm::vec2 uv{0};
    static CV lerp(const CV& a, const CV& b, float t) {
        CV r;
        r.pos = glm::mix(a.pos, b.pos, t);
        r.normal = glm::mix(a.normal, b.normal, t);
        r.uv = glm::mix(a.uv, b.uv, t);
        return r;
    }
};

struct Plane {
    glm::vec3 n{0};
    float w = 0;
    bool ok() const { return glm::length(n) > 0.5f; }
    static Plane from(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
        Plane p;
        glm::vec3 cr = glm::cross(b - a, c - a);
        float len = glm::length(cr);
        if (len < 1e-12f) return p;   // degenerate
        p.n = cr / len;
        p.w = glm::dot(p.n, a);
        return p;
    }
};

struct Poly {
    std::vector<CV> v;
    Plane plane;
};

// Split polygon `p` by `pl`, sorting fragments into the four buckets.
void split(const Plane& pl, const Poly& p, std::vector<Poly>& coplanarFront,
           std::vector<Poly>& coplanarBack, std::vector<Poly>& front,
           std::vector<Poly>& back) {
    enum { COPLANAR = 0, FRONT = 1, BACK = 2, SPANNING = 3 };
    int polyType = 0;
    std::vector<int> types;
    types.reserve(p.v.size());
    for (const CV& vv : p.v) {
        float t = glm::dot(pl.n, vv.pos) - pl.w;
        int type = (t < -kEps) ? BACK : (t > kEps) ? FRONT : COPLANAR;
        polyType |= type;
        types.push_back(type);
    }
    switch (polyType) {
        case COPLANAR:
            (glm::dot(pl.n, p.plane.n) > 0 ? coplanarFront : coplanarBack).push_back(p);
            break;
        case FRONT: front.push_back(p); break;
        case BACK: back.push_back(p); break;
        case SPANNING: {
            std::vector<CV> f, b;
            for (size_t i = 0; i < p.v.size(); ++i) {
                size_t j = (i + 1) % p.v.size();
                int ti = types[i], tj = types[j];
                const CV& vi = p.v[i];
                const CV& vj = p.v[j];
                if (ti != BACK) f.push_back(vi);
                if (ti != FRONT) b.push_back(vi);
                if ((ti | tj) == SPANNING) {
                    float t = (pl.w - glm::dot(pl.n, vi.pos)) /
                              glm::dot(pl.n, vj.pos - vi.pos);
                    CV vm = CV::lerp(vi, vj, t);
                    f.push_back(vm);
                    b.push_back(vm);
                }
            }
            if (f.size() >= 3) front.push_back({f, p.plane});
            if (b.size() >= 3) back.push_back({b, p.plane});
            break;
        }
    }
}

struct Node {
    Plane plane;
    std::unique_ptr<Node> front, back;
    std::vector<Poly> polys;

    void build(const std::vector<Poly>& list) {
        if (list.empty()) return;
        if (!plane.ok()) plane = list[0].plane;
        std::vector<Poly> f, b;
        for (size_t i = 0; i < list.size(); ++i) {
            std::vector<Poly> cf, cb;
            split(plane, list[i], cf, cb, f, b);
            for (auto& x : cf) polys.push_back(std::move(x));
            for (auto& x : cb) polys.push_back(std::move(x));
        }
        if (!f.empty()) { if (!front) front = std::make_unique<Node>(); front->build(f); }
        if (!b.empty()) { if (!back) back = std::make_unique<Node>(); back->build(b); }
    }

    std::vector<Poly> clip(const std::vector<Poly>& list) const {
        if (!plane.ok()) return list;
        std::vector<Poly> f, b;
        for (const Poly& p : list) {
            std::vector<Poly> cf, cb;
            split(plane, p, cf, cb, f, b);
            for (auto& x : cf) f.push_back(std::move(x));
            for (auto& x : cb) b.push_back(std::move(x));
        }
        std::vector<Poly> out = front ? front->clip(f) : f;
        if (back) { auto bb = back->clip(b); out.insert(out.end(), bb.begin(), bb.end()); }
        // if no back node, polygons behind are dropped
        return out;
    }

    void clipTo(const Node& other) {
        polys = other.clip(polys);
        if (front) front->clipTo(other);
        if (back) back->clipTo(other);
    }

    void invert() {
        for (Poly& p : polys) {
            std::reverse(p.v.begin(), p.v.end());
            for (CV& v : p.v) v.normal = -v.normal;
            p.plane.n = -p.plane.n;
            p.plane.w = -p.plane.w;
        }
        plane.n = -plane.n;
        plane.w = -plane.w;
        front.swap(back);
        if (front) front->invert();
        if (back) back->invert();
    }

    void all(std::vector<Poly>& out) const {
        out.insert(out.end(), polys.begin(), polys.end());
        if (front) front->all(out);
        if (back) back->all(out);
    }
};

std::vector<Poly> to_polys(const MeshData& m) {
    std::vector<Poly> out;
    out.reserve(m.idx.size() / 3);
    for (size_t i = 0; i + 2 < m.idx.size(); i += 3) {
        Poly p;
        for (int k = 0; k < 3; ++k) {
            const Vertex& s = m.verts[m.idx[i + k]];
            p.v.push_back({s.pos, s.normal, s.uv});
        }
        p.plane = Plane::from(p.v[0].pos, p.v[1].pos, p.v[2].pos);
        if (p.plane.ok()) out.push_back(std::move(p));
    }
    return out;
}

MeshData to_mesh(const std::vector<Poly>& polys) {
    MeshData d;
    for (const Poly& p : polys) {
        if (p.v.size() < 3) continue;
        uint32_t base = (uint32_t)d.verts.size();
        for (const CV& v : p.v) {
            Vertex vt;
            vt.pos = v.pos;
            vt.normal = glm::length(v.normal) > 1e-6f ? glm::normalize(v.normal) : p.plane.n;
            vt.uv = v.uv;
            d.verts.push_back(vt);
        }
        for (size_t k = 1; k + 1 < p.v.size(); ++k)
            d.idx.insert(d.idx.end(), {base, base + (uint32_t)k, base + (uint32_t)k + 1});
    }
    return d;
}

}  // namespace

MeshData csg(const MeshData& ma, const MeshData& mb, CsgOp op) {
    Node a, b;
    a.build(to_polys(ma));
    b.build(to_polys(mb));

    // csg.js operations, verbatim.
    if (op == CsgOp::Union) {
        a.clipTo(b);
        b.clipTo(a);
        b.invert(); b.clipTo(a); b.invert();
    } else if (op == CsgOp::Subtract) {
        a.invert();
        a.clipTo(b);
        b.clipTo(a);
        b.invert(); b.clipTo(a); b.invert();
    } else {  // Intersect
        a.invert();
        b.clipTo(a);
        b.invert();
        a.clipTo(b);
        b.clipTo(a);
    }

    std::vector<Poly> bp;
    b.all(bp);
    a.build(bp);
    if (op != CsgOp::Union) a.invert();

    std::vector<Poly> result;
    a.all(result);
    return to_mesh(result);
}

}  // namespace eng
