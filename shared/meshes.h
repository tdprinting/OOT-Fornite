#pragma once
#include "storm.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace royale {

// Our own low-poly models, built in code instead of taken from the game's files: rocks, boulders, stone posts and a cottage roof, in the
// flat-shaded, chunky look of Ocarina of Time. This file is pure geometry (triangles with a colour each), so it is unit-tested on its
// own; the game layer turns it into a display list. Units match the map (a Link is about 60 tall).
struct MeshVertex {
    float x, y, z;
    uint8_t r, g, b;
};

enum class MeshKind : uint8_t { Rock, Boulder, Pillar, Roof, Count };
constexpr int kMeshVariants = 4; // different rolls of the same kind, picked by the prop's rotation

struct MeshData {
    std::vector<MeshVertex> v; // three vertices per triangle, each triangle flat-coloured
    size_t Triangles() const { return v.size() / 3; }
    void Bounds(float mn[3], float mx[3]) const {
        mn[0] = mn[1] = mn[2] = 1e30f;
        mx[0] = mx[1] = mx[2] = -1e30f;
        for (const MeshVertex& p : v) {
            const float c[3] = {p.x, p.y, p.z};
            for (int i = 0; i < 3; i++) { mn[i] = (std::min)(mn[i], c[i]); mx[i] = (std::max)(mx[i], c[i]); }
        }
    }
};

namespace mesh_detail {

struct V3 { float x, y, z; };
inline V3 Sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 Norm(V3 a) { const float l = std::sqrt(Dot(a, a)); return l > 1e-6f ? V3{a.x / l, a.y / l, a.z / l} : V3{0, 1, 0}; }

struct Rgb { float r, g, b; };

struct Builder {
    MeshData mesh;
    V3 inside = {0, 0, 0}; // a point inside the (roughly convex) shape, so each face can be turned to face outward

    // Add a triangle. Its brightness comes from the way it faces (sun from the upper left front), so the winding order given doesn't matter.
    void Tri(V3 a, V3 b, V3 c, Rgb col) {
        V3 n = Norm(Cross(Sub(b, a), Sub(c, a)));
        const V3 centre = {(a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3, (a.z + b.z + c.z) / 3};
        if (Dot(n, Sub(centre, inside)) < 0) { std::swap(b, c); n = {-n.x, -n.y, -n.z}; }
        static const V3 sun = Norm({-0.45f, 0.8f, 0.4f});
        const float shade = 0.52f + 0.48f * (std::max)(0.0f, Dot(n, sun));
        auto byte = [&](float f) { return static_cast<uint8_t>((std::min)(255.0f, (std::max)(0.0f, f * shade))); };
        for (V3 p : {a, b, c}) mesh.v.push_back({p.x, p.y, p.z, byte(col.r), byte(col.g), byte(col.b)});
    }
    void Quad(V3 a, V3 b, V3 c, V3 d, Rgb col) { Tri(a, b, c, col); Tri(a, c, d, col); }
};

struct Lcg {
    uint32_t s;
    explicit Lcg(uint32_t seed) : s(seed * 2654435761u + 12345u) {}
    float Next() { s = s * 1664525u + 1013904223u; return static_cast<float>(s >> 8) / 16777216.0f; } // 0..1
};

// Rock or boulder: a squashed, lumpy icosahedron resting on the ground. Boulders are bigger and mossy on top.
inline MeshData Lump(float radius, float squash, bool mossy, uint32_t seed) {
    const float t = 1.6180339887f;
    V3 base[12] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    static const int faces[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                                     {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    Lcg rng(seed);
    V3 p[12];
    for (int i = 0; i < 12; i++) {
        const V3 u = Norm(base[i]);
        const float lump = 0.82f + 0.36f * rng.Next();
        p[i] = {u.x * radius * lump, u.y * radius * lump * squash, u.z * radius * lump};
    }
    float lowest = 1e30f;
    for (const V3& q : p) lowest = (std::min)(lowest, q.y);
    for (V3& q : p) q.y = (std::max)(0.0f, q.y - lowest * 0.55f); // sits on the ground with the bottom flattened
    Builder b;
    b.inside = {0, radius * squash * 0.35f, 0};
    for (const auto& f : faces) {
        const V3 n = Norm(Cross(Sub(p[f[1]], p[f[0]]), Sub(p[f[2]], p[f[0]])));
        const bool up = std::fabs(n.y) > 0.55f && (p[f[0]].y + p[f[1]].y + p[f[2]].y) > radius * squash * 0.9f;
        const float v = 0.9f + 0.2f * rng.Next();
        Rgb col = mossy && up ? Rgb{86 * v, 122 * v, 66 * v} : Rgb{136 * v, 128 * v, 116 * v};
        b.Tri(p[f[0]], p[f[1]], p[f[2]], col);
    }
    return b.mesh;
}

// A round (eight-sided) stone post: a footing, a shaft with a mossy foot, and a capital. Eight sides so it looks the same from any angle,
// which lets a row of them stand as a wall.
inline MeshData Post() {
    struct Ring { float r, y; Rgb col; };
    const Ring rings[] = {{40, 0, {96, 90, 82}},    {40, 26, {120, 112, 100}}, {33, 26, {116, 108, 98}}, {33, 62, {96, 124, 80}},
                          {33, 74, {140, 130, 116}}, {33, 170, {146, 136, 122}}, {42, 170, {160, 150, 134}}, {42, 200, {170, 160, 142}}};
    const int n = 8;
    Builder b;
    b.inside = {0, 100, 0};
    auto at = [&](const Ring& g, int i) {
        const float a = 6.2831853f * (i + 0.5f) / n;
        return V3{std::cos(a) * g.r, g.y, std::sin(a) * g.r};
    };
    for (size_t k = 0; k + 1 < sizeof(rings) / sizeof(rings[0]); k++) {
        for (int i = 0; i < n; i++) b.Quad(at(rings[k], i), at(rings[k], (i + 1) % n), at(rings[k + 1], (i + 1) % n), at(rings[k + 1], i), rings[k + 1].col);
    }
    const Ring top = rings[sizeof(rings) / sizeof(rings[0]) - 1];
    for (int i = 0; i < n; i++) b.Tri({0, top.y, 0}, at(top, i), at(top, (i + 1) % n), {176, 166, 148});
    return b.mesh;
}

// A gabled cottage roof over a 360 x 280 courtyard (x along the width, z across), standing on posts 200 high. Terracotta tiles in
// stripes, plaster gable ends and a dark ridge beam.
inline MeshData Roof() {
    const float hx = 224, hz = 178, eave = 200, ridge = 318;
    Builder b;
    b.inside = {0, eave + 10, 0};
    const int strips = 6;
    for (int side = -1; side <= 1; side += 2) {
        for (int s = 0; s < strips; s++) {
            const float t0 = static_cast<float>(s) / strips, t1 = static_cast<float>(s + 1) / strips;
            const float z0 = side * hz * (1 - t0), z1 = side * hz * (1 - t1), y0 = eave + (ridge - eave) * t0, y1 = eave + (ridge - eave) * t1;
            const float v = (s % 2) ? 1.0f : 0.88f;
            b.Quad({-hx, y0, z0}, {hx, y0, z0}, {hx, y1, z1}, {-hx, y1, z1}, {178 * v, 76 * v, 54 * v});
        }
    }
    for (int end = -1; end <= 1; end += 2) b.Tri({end * hx, eave, -hz}, {end * hx, eave, hz}, {end * hx, ridge, 0}, {222, 208, 170});
    const float rb = 9;
    b.Quad({-hx - 6, ridge + rb, -rb}, {hx + 6, ridge + rb, -rb}, {hx + 6, ridge + rb, rb}, {-hx - 6, ridge + rb, rb}, {84, 58, 40});
    b.Quad({-hx - 6, ridge - rb, -rb}, {hx + 6, ridge - rb, -rb}, {hx + 6, ridge + rb, -rb}, {-hx - 6, ridge + rb, -rb}, {70, 48, 34});
    b.Quad({-hx - 6, ridge - rb, rb}, {hx + 6, ridge - rb, rb}, {hx + 6, ridge + rb, rb}, {-hx - 6, ridge + rb, rb}, {70, 48, 34});
    return b.mesh;
}

} // namespace mesh_detail

inline MeshData BuildMesh(MeshKind kind, uint32_t variant) {
    switch (kind) {
        case MeshKind::Rock: return mesh_detail::Lump(34.0f, 0.78f, false, 100 + variant);
        case MeshKind::Boulder: return mesh_detail::Lump(82.0f, 0.82f, true, 200 + variant);
        case MeshKind::Pillar: return mesh_detail::Post();
        case MeshKind::Roof: return mesh_detail::Roof();
        default: return {};
    }
}

} // namespace royale
