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

enum class MeshKind : uint8_t { Rock, Boulder, Pillar, Roof, Golem, Glider, Dragon, Count }; // Golem: the mini boss (variant = its BossKind); Glider: variant = colour scheme; Dragon: variant = wing pose
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

// A blocky mini-boss golem about 200 tall: stumpy legs, a barrel chest, long arms with big fists, a small head with glowing eyes and a
// crest. The three kinds share the shape and differ in colour: grey stone, dark rock veined with lava, and pale ice.
inline MeshData Golem(uint32_t kind) {
    struct Palette { Rgb body, dark, glow, crest; };
    static const Palette palettes[3] = {
        {{138, 132, 122}, {96, 92, 84}, {255, 230, 120}, {170, 160, 140}},
        {{78, 60, 56}, {52, 38, 36}, {255, 120, 30}, {210, 70, 20}},
        {{176, 214, 232}, {120, 160, 190}, {120, 230, 255}, {232, 248, 255}},
    };
    const Palette& pal = palettes[kind % 3];
    Builder b;
    auto box = [&](float cx, float cy, float cz, float hx, float hy, float hz, Rgb col) {
        b.inside = {cx, cy, cz};
        const V3 p[8] = {{cx - hx, cy - hy, cz - hz}, {cx + hx, cy - hy, cz - hz}, {cx + hx, cy + hy, cz - hz}, {cx - hx, cy + hy, cz - hz},
                         {cx - hx, cy - hy, cz + hz}, {cx + hx, cy - hy, cz + hz}, {cx + hx, cy + hy, cz + hz}, {cx - hx, cy + hy, cz + hz}};
        b.Quad(p[0], p[1], p[2], p[3], col); b.Quad(p[4], p[5], p[6], p[7], col); b.Quad(p[0], p[1], p[5], p[4], col);
        b.Quad(p[3], p[2], p[6], p[7], col); b.Quad(p[0], p[3], p[7], p[4], col); b.Quad(p[1], p[2], p[6], p[5], col);
    };
    box(-34, 40, 0, 26, 40, 26, pal.dark);      // legs
    box(34, 40, 0, 26, 40, 26, pal.dark);
    box(0, 115, 0, 62, 36, 40, pal.body);       // hips and belly
    box(0, 170, 0, 74, 34, 44, pal.body);       // chest
    box(-96, 178, 0, 24, 26, 30, pal.dark);     // shoulders
    box(96, 178, 0, 24, 26, 30, pal.dark);
    box(-108, 110, 6, 20, 56, 22, pal.body);    // arms hanging down
    box(108, 110, 6, 20, 56, 22, pal.body);
    box(-108, 40, 12, 30, 24, 30, pal.dark);    // fists
    box(108, 40, 12, 30, 24, 30, pal.dark);
    box(0, 226, 6, 34, 28, 32, pal.body);       // head
    box(0, 262, 4, 10, 20, 26, pal.crest);      // crest
    box(-14, 230, 38, 8, 6, 2, pal.glow);       // eyes
    box(14, 230, 38, 8, 6, 2, pal.glow);
    box(0, 176, 46, 22, 14, 2, pal.glow);       // glowing core in the chest
    return b.mesh;
}

// The skydiving glider: a striped delta wing 150 above a player's feet, hung from two struts. Variants are colour schemes.
inline MeshData Glider(uint32_t variant) {
    static const Rgb schemes[4][2] = {
        {{230, 70, 60}, {245, 235, 220}}, {{70, 130, 235}, {245, 220, 90}}, {{70, 190, 100}, {245, 245, 235}}, {{170, 90, 230}, {250, 210, 120}}};
    const Rgb* sc = schemes[variant % 4];
    const Rgb strut = {120, 90, 56};
    Builder b;
    const V3 nose = {0, 156, 78}, tail = {0, 150, -64};
    const V3 tipL = {-118, 140, -58}, tipR = {118, 140, -58};
    auto lerp = [](V3 a, V3 c, float t) { return V3{a.x + (c.x - a.x) * t, a.y + (c.y - a.y) * t, a.z + (c.z - a.z) * t}; };
    for (int side = 0; side < 2; side++) {
        const V3 tip = side == 0 ? tipL : tipR;
        for (int i = 0; i < 3; i++) {
            const float t0 = i / 3.0f, t1 = (i + 1) / 3.0f;
            const Rgb col = sc[i % 2];
            const V3 a = lerp(nose, tip, t0), c = lerp(nose, tip, t1), d = lerp(tail, tip, t1), e = lerp(tail, tip, t0);
            b.inside = {0, 80, 0};
            b.Quad(a, c, d, e, col);                                                  // top
            b.inside = {0, 240, 0};
            const V3 dn = {0, -7, 0};
            b.Quad({a.x, a.y + dn.y, a.z}, {c.x, c.y + dn.y, c.z}, {d.x, d.y + dn.y, d.z}, {e.x, e.y + dn.y, e.z}, {col.r * 0.7f, col.g * 0.7f, col.b * 0.7f}); // underside
        }
    }
    auto bar = [&](V3 from, V3 to, float w) {
        b.inside = {(from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f, (from.z + to.z) * 0.5f};
        const V3 off[4] = {{-w, 0, -w}, {w, 0, -w}, {w, 0, w}, {-w, 0, w}};
        for (int i = 0; i < 4; i++) {
            const int j = (i + 1) % 4;
            b.Quad({from.x + off[i].x, from.y, from.z + off[i].z}, {from.x + off[j].x, from.y, from.z + off[j].z},
                   {to.x + off[j].x, to.y, to.z + off[j].z}, {to.x + off[i].x, to.y, to.z + off[i].z}, strut);
        }
    };
    bar({-14, 62, 0}, {-46, 143, 12}, 3.5f);   // the struts down to the shoulders
    bar({14, 62, 0}, {46, 143, 12}, 3.5f);
    bar({0, 138, -50}, {0, 138, 70}, 3.5f);    // the keel
    return b.mesh;
}

// The fire dragon: a big winged reptile, nose towards +z, about 1200 across with its wings out. The variant is the wing pose
// (0 up, 1 level, 2 down, 3 level), so cycling the variants flaps its wings.
inline MeshData Dragon(uint32_t pose) {
    const Rgb body = {168, 44, 32}, dark = {104, 28, 24}, belly = {236, 176, 84}, glow = {255, 210, 70}, bone = {70, 40, 36}, skin = {204, 66, 44};
    Builder b;
    auto box = [&](float cx, float cy, float cz, float hx, float hy, float hz, Rgb col) {
        b.inside = {cx, cy, cz};
        const V3 p[8] = {{cx - hx, cy - hy, cz - hz}, {cx + hx, cy - hy, cz - hz}, {cx + hx, cy + hy, cz - hz}, {cx - hx, cy + hy, cz - hz},
                         {cx - hx, cy - hy, cz + hz}, {cx + hx, cy - hy, cz + hz}, {cx + hx, cy + hy, cz + hz}, {cx - hx, cy + hy, cz + hz}};
        b.Quad(p[0], p[1], p[2], p[3], col); b.Quad(p[4], p[5], p[6], p[7], col); b.Quad(p[0], p[1], p[5], p[4], col);
        b.Quad(p[3], p[2], p[6], p[7], col); b.Quad(p[0], p[3], p[7], p[4], col); b.Quad(p[1], p[2], p[6], p[5], col);
    };
    box(0, 150, 0, 72, 62, 170, body);          // body
    box(0, 108, 20, 56, 14, 140, belly);        // pale belly plates
    box(0, 192, 196, 36, 36, 62, body);         // neck, in two bends
    box(0, 238, 252, 30, 30, 50, body);
    box(0, 266, 330, 40, 30, 62, body);         // head
    box(0, 254, 398, 26, 17, 38, body);         // snout
    box(0, 232, 392, 21, 7, 40, dark);          // lower jaw
    box(-24, 304, 300, 7, 30, 8, bone);         // horns
    box(24, 304, 300, 7, 30, 8, bone);
    box(-26, 276, 372, 7, 6, 3, glow);          // eyes
    box(26, 276, 372, 7, 6, 3, glow);
    box(0, 142, -232, 52, 46, 72, body);        // tail, tapering
    box(0, 122, -338, 38, 32, 46, body);
    box(0, 104, -420, 26, 22, 40, dark);
    box(0, 92, -488, 15, 14, 36, dark);
    box(0, 90, -540, 11, 22, 18, glow);         // a flame on the tail tip
    for (int i = 0; i < 5; i++) box(0, 224 - i * 6.0f, 130 - i * 70.0f, 9, 18, 14, bone); // spikes down the back
    for (int sx = -1; sx <= 1; sx += 2) {
        box(sx * 58, 46, 96, 20, 46, 24, dark);   // legs
        box(sx * 58, 10, 112, 24, 10, 34, bone);  // claws
        box(sx * 58, 46, -92, 20, 46, 26, dark);
        box(sx * 58, 10, -76, 24, 10, 34, bone);
    }
    // Wings: an arm to the wrist and a leathery sail from the body out to the finger tips.
    const float wristY[4] = {470.0f, 290.0f, 120.0f, 290.0f};
    const float tipY[4] = {640.0f, 270.0f, 20.0f, 270.0f};
    const float wy = wristY[pose % 4], ty = tipY[pose % 4];
    for (int sx = -1; sx <= 1; sx += 2) {
        const V3 shoulder = {sx * 62.0f, 200, 70}, wrist = {sx * 360.0f, wy, 40};
        const V3 tip1 = {sx * 640.0f, ty, -60}, tip2 = {sx * 520.0f, ty * 0.8f + 20.0f, -230};
        const V3 rear = {sx * 62.0f, 170, -110};
        b.inside = {sx * 300.0f, wy * 0.6f + 60.0f, 0};
        b.Tri(shoulder, wrist, tip2, skin); b.Tri(shoulder, tip2, rear, skin); b.Tri(wrist, tip1, tip2, skin);
        b.inside = {sx * 300.0f, wy * 0.6f + 300.0f, 0};
        const V3 d = {0, -8, 0};
        auto dn = [&](V3 p) { return V3{p.x + d.x, p.y + d.y, p.z + d.z}; };
        b.Tri(dn(shoulder), dn(wrist), dn(tip2), {skin.r * 0.75f, skin.g * 0.75f, skin.b * 0.75f});
        b.Tri(dn(shoulder), dn(tip2), dn(rear), {skin.r * 0.75f, skin.g * 0.75f, skin.b * 0.75f});
        b.Tri(dn(wrist), dn(tip1), dn(tip2), {skin.r * 0.75f, skin.g * 0.75f, skin.b * 0.75f});
        // the arm bones: a bar from the shoulder to the wrist and on to the long finger
        auto bar = [&](V3 from, V3 to) {
            b.inside = {(from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f + 40.0f, (from.z + to.z) * 0.5f};
            b.Quad({from.x, from.y + 9, from.z}, {to.x, to.y + 9, to.z}, {to.x, to.y - 9, to.z}, {from.x, from.y - 9, from.z}, bone);
            b.Quad({from.x, from.y, from.z + 9}, {to.x, to.y, to.z + 9}, {to.x, to.y, to.z - 9}, {from.x, from.y, from.z - 9}, bone);
        };
        bar(shoulder, wrist);
        bar(wrist, tip1);
        bar(wrist, tip2);
    }
    return b.mesh;
}

} // namespace mesh_detail

inline MeshData BuildMesh(MeshKind kind, uint32_t variant) {
    switch (kind) {
        case MeshKind::Rock: return mesh_detail::Lump(34.0f, 0.78f, false, 100 + variant);
        case MeshKind::Boulder: return mesh_detail::Lump(82.0f, 0.82f, true, 200 + variant);
        case MeshKind::Pillar: return mesh_detail::Post();
        case MeshKind::Roof: return mesh_detail::Roof();
        case MeshKind::Golem: return mesh_detail::Golem(variant);
        case MeshKind::Glider: return mesh_detail::Glider(variant);
        case MeshKind::Dragon: return mesh_detail::Dragon(variant);
        default: return {};
    }
}

} // namespace royale
