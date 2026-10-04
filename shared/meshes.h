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

enum class MeshKind : uint8_t { Rock, Boulder, Pillar, Roof, Golem, Glider, Dragon, Platform, Projectile, GliderFrame, Sign, Ally, Cat, Grass, Tree, SnowPatch, CatBody, CatHead, CatTailSeg, CatLeg, Puddle, Ripple, Count }; // Golem: the mini boss (variant = its BossKind); Glider: variant = colour scheme; Dragon: variant = wing pose
constexpr int kMeshVariants = 4; // different rolls of the same kind, picked by the prop's rotation
constexpr int kMeshVariantSlots = 32; // golem: one per BossKind; dragon: wing pose (0-3) plus 4 per theme (fire, water, forest, shadow, sand)

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
    static const Palette palettes[7] = {
        {{138, 132, 122}, {96, 92, 84}, {255, 230, 120}, {170, 160, 140}},   // stone
        {{78, 60, 56}, {52, 38, 36}, {255, 120, 30}, {210, 70, 20}},         // lava
        {{176, 214, 232}, {120, 160, 190}, {120, 230, 255}, {232, 248, 255}}, // frost
        {{96, 130, 84}, {58, 86, 52}, {170, 240, 120}, {70, 170, 60}},       // moss
        {{70, 130, 170}, {44, 90, 126}, {110, 235, 235}, {180, 220, 240}},   // tide
        {{86, 64, 110}, {50, 36, 70}, {200, 110, 255}, {140, 90, 190}},      // shade
        {{200, 170, 110}, {150, 120, 76}, {255, 220, 110}, {230, 190, 120}}, // dune
    };
    const Palette& pal = palettes[kind % 7];
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
inline MeshData Glider(uint32_t variant, bool wings = true) {
    static const Rgb schemes[4][2] = {
        {{230, 70, 60}, {245, 235, 220}}, {{70, 130, 235}, {245, 220, 90}}, {{70, 190, 100}, {245, 245, 235}}, {{170, 90, 230}, {250, 210, 120}}};
    const Rgb* sc = schemes[variant % 4];
    const Rgb strut = {120, 90, 56};
    Builder b;
    const V3 nose = {0, 156, 78}, tail = {0, 150, -64};
    const V3 tipL = {-118, 140, -58}, tipR = {118, 140, -58};
    auto lerp = [](V3 a, V3 c, float t) { return V3{a.x + (c.x - a.x) * t, a.y + (c.y - a.y) * t, a.z + (c.z - a.z) * t}; };
    for (int side = 0; side < (wings ? 2 : 0); side++) {   // (the cloth version of the glider draws the wings itself, see cloth.h)
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
    bar({-18, 80, 2}, {-46, 143, 12}, 3.5f);   // the struts down to the hand bar
    bar({18, 80, 2}, {46, 143, 12}, 3.5f);
    {   // the hand bar Link hangs from (he is drawn with both arms up gripping it)
        const V3 c = {0, 80, 2};
        const float hx = 26.0f, hy = 3.0f, hz = 3.0f;
        b.inside = c;
        const V3 q[8] = {{c.x - hx, c.y - hy, c.z - hz}, {c.x + hx, c.y - hy, c.z - hz}, {c.x + hx, c.y + hy, c.z - hz}, {c.x - hx, c.y + hy, c.z - hz},
                         {c.x - hx, c.y - hy, c.z + hz}, {c.x + hx, c.y - hy, c.z + hz}, {c.x + hx, c.y + hy, c.z + hz}, {c.x - hx, c.y + hy, c.z + hz}};
        const Rgb grip = {96, 66, 40};
        b.Quad(q[0], q[1], q[2], q[3], grip); b.Quad(q[4], q[5], q[6], q[7], grip); b.Quad(q[0], q[1], q[5], q[4], grip);
        b.Quad(q[3], q[2], q[6], q[7], grip); b.Quad(q[0], q[3], q[7], q[4], grip); b.Quad(q[1], q[2], q[6], q[5], grip);
    }
    bar({0, 138, -50}, {0, 138, 70}, 3.5f);    // the keel
    return b.mesh;
}

// The fire dragon: a big winged reptile, nose towards +z, about 1200 across with its wings out. The variant is the wing pose
// (0 up, 1 level, 2 down, 3 level), so cycling the variants flaps its wings.
inline MeshData Dragon(uint32_t variant) {
    const uint32_t pose = variant % 4, theme = (variant / 4) % 5;
    struct Look { Rgb body, dark, belly, glow, skin; };
    static const Look looks[5] = {
        {{168, 44, 32}, {104, 28, 24}, {236, 176, 84}, {255, 210, 70}, {204, 66, 44}},     // fire
        {{40, 110, 170}, {24, 70, 120}, {170, 225, 235}, {150, 255, 255}, {70, 160, 200}},  // water
        {{60, 140, 64}, {36, 90, 44}, {200, 220, 120}, {255, 240, 120}, {110, 180, 70}},    // forest
        {{80, 56, 110}, {44, 30, 70}, {150, 120, 190}, {255, 90, 200}, {110, 70, 150}},     // shadow
        {{200, 160, 84}, {140, 104, 52}, {240, 220, 160}, {255, 120, 60}, {230, 190, 110}}, // sand
    };
    const Look& lk = looks[theme];
    const Rgb body = lk.body, dark = lk.dark, belly = lk.belly, glow = lk.glow, bone = {70, 40, 36}, skin = lk.skin;
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

// A climbing block: 150 across, 60 (variant 0), 120 (variant 1) or 180 (variant 2) tall, with a lighter slab on top, an inset panel and darker
// stripes around the sides, so it reads as a stone step from far off.
inline MeshData Platform(uint32_t variant) {
    const float h = 60.0f * static_cast<float>(variant % 3 + 1), half = 75.0f;
    const Rgb body = {156, 150, 138}, dark = {112, 106, 98}, top = {196, 190, 172}, panel = {170, 160, 140};
    Builder b;
    auto box = [&](float x0, float y0, float z0, float x1, float y1, float z1, Rgb col) {
        b.inside = {(x0 + x1) * 0.5f, (y0 + y1) * 0.5f, (z0 + z1) * 0.5f};
        const V3 p[8] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}, {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
        b.Quad(p[0], p[1], p[2], p[3], col); b.Quad(p[4], p[5], p[6], p[7], col); b.Quad(p[0], p[1], p[5], p[4], col);
        b.Quad(p[3], p[2], p[6], p[7], col); b.Quad(p[0], p[3], p[7], p[4], col); b.Quad(p[1], p[2], p[6], p[5], col);
    };
    box(-half, 0, -half, half, h - 8.0f, half, body);                 // the block
    box(-half - 4.0f, h - 12.0f, -half - 4.0f, half + 4.0f, h, half + 4.0f, top); // the slab on top, a little proud
    box(-half + 18.0f, h, -half + 18.0f, half - 18.0f, h + 2.0f, half - 18.0f, panel); // a raised panel
    for (float y = 14.0f; y < h - 16.0f; y += 28.0f) box(-half - 2.0f, y, -half - 2.0f, half + 2.0f, y + 7.0f, half + 2.0f, dark); // courses of stone
    return b.mesh;
}

// Things in flight, nose towards +z, standing on y = 0: variants 0-3 arrows (plain, fire, ice, light), 4 a seed, 5 a bomb, 6 a bombchu, 7 a purple homing
// bombchu, 8 a deku nut, 9 a boomerang. Drawn by the game layer for every arrow, seed and bomb anybody looses.
inline MeshData Projectile(uint32_t variant) {
    Builder b;
    auto box = [&](float cx, float cy, float cz, float hx, float hy, float hz, Rgb col) {
        b.inside = {cx, cy, cz};
        const V3 p[8] = {{cx - hx, cy - hy, cz - hz}, {cx + hx, cy - hy, cz - hz}, {cx + hx, cy + hy, cz - hz}, {cx - hx, cy + hy, cz - hz},
                         {cx - hx, cy - hy, cz + hz}, {cx + hx, cy - hy, cz + hz}, {cx + hx, cy + hy, cz + hz}, {cx - hx, cy + hy, cz + hz}};
        b.Quad(p[0], p[1], p[2], p[3], col); b.Quad(p[4], p[5], p[6], p[7], col); b.Quad(p[0], p[1], p[5], p[4], col);
        b.Quad(p[3], p[2], p[6], p[7], col); b.Quad(p[0], p[3], p[7], p[4], col); b.Quad(p[1], p[2], p[6], p[5], col);
    };
    auto ball = [&](float cx, float cy, float cz, float r, Rgb col) {   // an octahedron
        b.inside = {cx, cy, cz};
        const V3 t = {cx, cy + r, cz}, d = {cx, cy - r, cz}, px = {cx + r, cy, cz}, nx = {cx - r, cy, cz}, pz = {cx, cy, cz + r}, nz = {cx, cy, cz - r};
        b.Tri(t, px, pz, col); b.Tri(t, pz, nx, col); b.Tri(t, nx, nz, col); b.Tri(t, nz, px, col);
        b.Tri(d, pz, px, col); b.Tri(d, nx, pz, col); b.Tri(d, nz, nx, col); b.Tri(d, px, nz, col);
    };
    const Rgb wood = {150, 98, 52}, white = {240, 240, 235};
    switch (variant % 10) {
        case 0: case 1: case 2: case 3: {
            static const Rgb heads[4] = {{205, 210, 220}, {255, 140, 40}, {120, 225, 245}, {255, 240, 130}};
            const Rgb head = heads[variant % 4];
            box(0, 8, 45, 1.8f, 1.8f, 45.0f, wood);                                     // shaft
            b.inside = {0, 8, 98};                                                       // the head: a four-sided point
            const V3 base[4] = {{-5, 3, 90}, {5, 3, 90}, {5, 13, 90}, {-5, 13, 90}};
            const V3 tip = {0, 8, 112};
            for (int i = 0; i < 4; i++) b.Tri(base[i], base[(i + 1) % 4], tip, head);
            box(0, 8, 6, 0.8f, 7.0f, 7.0f, variant == 0 ? white : head);                // fletching, one vane up and down...
            box(0, 8, 6, 7.0f, 0.8f, 7.0f, variant == 0 ? white : head);                // ...and one side to side
            break;
        }
        case 4: ball(0, 8, 0, 8.0f, {150, 100, 50}); box(0, 8, 0, 8.5f, 1.2f, 8.5f, {110, 70, 30}); break;
        case 5: ball(0, 16, 0, 16.0f, {40, 44, 60}); box(0, 34, 0, 2.0f, 4.0f, 2.0f, wood); box(0, 40, 0, 3.0f, 3.0f, 3.0f, {255, 150, 40}); break;
        case 6: case 7: {
            const Rgb body = variant == 6 ? Rgb{225, 60, 60} : Rgb{150, 60, 220};
            box(0, 10, 0, 7.0f, 7.0f, 15.0f, body);
            box(-6, 18, 12, 3.0f, 4.0f, 3.0f, white); box(6, 18, 12, 3.0f, 4.0f, 3.0f, white);   // the ears
            box(0, 10, -17, 2.0f, 2.0f, 3.0f, {255, 170, 60});                                     // the lit fuse
            break;
        }
        case 8: ball(0, 9, 0, 9.0f, {140, 90, 40}); box(0, 18, 0, 1.5f, 3.0f, 1.5f, {70, 160, 70}); break;
        default:                                                                                     // a boomerang: two arms, bent
            box(-10, 6, 6, 12.0f, 2.5f, 4.0f, {240, 140, 50}); box(10, 6, 6, 12.0f, 2.5f, 4.0f, {240, 140, 50}); box(0, 6, 0, 5.0f, 2.5f, 7.0f, {200, 100, 30});
            break;
    }
    return b.mesh;
}


// A wooden signpost about 150 tall: a post and a broad board facing +z. The writing is drawn by the game's overlay, not the mesh.
inline MeshData Sign() {
    Builder b;
    auto box = [&](float cx, float cy, float cz, float hx, float hy, float hz, Rgb col) {
        b.inside = {cx, cy, cz};
        const V3 p[8] = {{cx - hx, cy - hy, cz - hz}, {cx + hx, cy - hy, cz - hz}, {cx + hx, cy + hy, cz - hz}, {cx - hx, cy + hy, cz - hz},
                         {cx - hx, cy - hy, cz + hz}, {cx + hx, cy - hy, cz + hz}, {cx + hx, cy + hy, cz + hz}, {cx - hx, cy + hy, cz + hz}};
        b.Quad(p[0], p[1], p[2], p[3], col); b.Quad(p[4], p[5], p[6], p[7], col); b.Quad(p[0], p[1], p[5], p[4], col);
        b.Quad(p[3], p[2], p[6], p[7], col); b.Quad(p[0], p[3], p[7], p[4], col); b.Quad(p[1], p[2], p[6], p[5], col);
    };
    box(0, 50, 0, 9, 50, 9, {112, 76, 44});        // the post
    box(0, 118, 6, 72, 36, 5, {138, 94, 54});      // the frame
    box(0, 118, 11, 62, 28, 3, {206, 164, 104});   // the board itself, paler
    return b.mesh;
}

// The four hireable allies, about 150 to 230 tall, blocky like the golems: variant 0 a Kokiri (green tunic, leaf cap), 1 a Zora (blue, fins), 2 a
// Goron (broad and rocky), 3 a Gerudo (red hair, purple and gold).
inline MeshData Ally(uint32_t variant) {
    Builder b;
    auto box = [&](float cx, float cy, float cz, float hx, float hy, float hz, Rgb col) {
        b.inside = {cx, cy, cz};
        const V3 p[8] = {{cx - hx, cy - hy, cz - hz}, {cx + hx, cy - hy, cz - hz}, {cx + hx, cy + hy, cz - hz}, {cx - hx, cy + hy, cz - hz},
                         {cx - hx, cy - hy, cz + hz}, {cx + hx, cy - hy, cz + hz}, {cx + hx, cy + hy, cz + hz}, {cx - hx, cy + hy, cz + hz}};
        b.Quad(p[0], p[1], p[2], p[3], col); b.Quad(p[4], p[5], p[6], p[7], col); b.Quad(p[0], p[1], p[5], p[4], col);
        b.Quad(p[3], p[2], p[6], p[7], col); b.Quad(p[0], p[3], p[7], p[4], col); b.Quad(p[1], p[2], p[6], p[5], col);
    };
    const Rgb skin[4] = {{238, 206, 170}, {120, 175, 205}, {176, 112, 76}, {210, 160, 108}};
    const Rgb boots = {84, 58, 40};
    switch (variant % 4) {
        case 0: {   // Kokiri: small
            const Rgb tunic = {70, 150, 66}, cap = {52, 120, 54};
            box(-16, 30, 0, 12, 30, 12, boots); box(16, 30, 0, 12, 30, 12, boots);                  // legs
            box(0, 80, 0, 30, 24, 18, tunic);                                                     // body
            box(-40, 78, 0, 10, 22, 10, tunic); box(40, 78, 0, 10, 22, 10, tunic);                // arms
            box(-40, 52, 0, 9, 8, 9, skin[0]); box(40, 52, 0, 9, 8, 9, skin[0]);                  // hands
            box(0, 124, 0, 26, 22, 22, skin[0]);                                                  // big head
            box(0, 150, -2, 28, 10, 24, cap); box(0, 168, -12, 14, 14, 16, cap);                  // the leaf cap with its point
            box(-10, 126, 21, 5, 5, 2, {30, 30, 40}); box(10, 126, 21, 5, 5, 2, {30, 30, 40});   // eyes
            break;
        }
        case 1: {   // Zora: tall, blue, with fins
            const Rgb fin = {70, 130, 190}, belly = {220, 235, 240};
            box(-18, 44, 0, 13, 44, 13, skin[1]); box(18, 44, 0, 13, 44, 13, skin[1]);
            box(0, 112, 0, 32, 30, 18, skin[1]); box(0, 112, 14, 22, 24, 6, belly);               // body and pale front
            box(-46, 108, 0, 10, 28, 10, skin[1]); box(46, 108, 0, 10, 28, 10, skin[1]);
            box(-60, 100, 0, 3, 24, 16, fin); box(60, 100, 0, 3, 24, 16, fin);                    // arm fins
            box(0, 168, 0, 22, 22, 22, skin[1]);                                                  // head
            box(0, 196, -26, 10, 30, 34, fin);                                                    // the long head fin sweeping back
            box(-11, 170, 21, 5, 5, 2, {250, 220, 90}); box(11, 170, 21, 5, 5, 2, {250, 220, 90});
            break;
        }
        case 2: {   // Goron: broad and rocky
            const Rgb rock = {128, 100, 80}, dark = {92, 70, 56};
            box(-34, 36, 0, 24, 36, 24, dark); box(34, 36, 0, 24, 36, 24, dark);
            box(0, 108, 0, 64, 42, 46, skin[2]);                                                  // huge round body
            box(-86, 104, 0, 24, 40, 26, skin[2]); box(86, 104, 0, 24, 40, 26, skin[2]);          // thick arms
            box(-88, 60, 6, 26, 20, 28, rock); box(88, 60, 6, 26, 20, 28, rock);                  // stone fists
            box(0, 168, 4, 30, 24, 28, skin[2]);                                                  // head
            for (int i = -2; i <= 2; i++) box(i * 16.0f, 202 - std::abs(i) * 4.0f, -10, 7, 12, 10, rock);   // rocky spikes along the back of the head
            box(-12, 172, 30, 5, 5, 2, {30, 24, 20}); box(12, 172, 30, 5, 5, 2, {30, 24, 20});
            break;
        }
        default: {  // Gerudo: tall, red hair, purple and gold
            const Rgb cloth = {120, 60, 160}, gold = {236, 196, 90}, hair = {204, 70, 40};
            box(-16, 46, 0, 12, 46, 12, cloth); box(16, 46, 0, 12, 46, 12, cloth);
            box(0, 112, 0, 26, 28, 16, cloth); box(0, 122, 12, 20, 6, 6, gold);                   // body and a gold sash
            box(-38, 110, 0, 9, 28, 9, skin[3]); box(38, 110, 0, 9, 28, 9, skin[3]);
            box(0, 166, 0, 20, 22, 20, skin[3]);                                                  // head
            box(0, 188, -4, 22, 14, 24, hair); box(0, 168, -26, 12, 36, 12, hair);                // hair and a long tail
            box(0, 150, 14, 14, 3, 6, gold);                                                      // veil band
            box(-10, 170, 19, 4, 4, 2, {40, 30, 20}); box(10, 170, 19, 4, 4, 2, {40, 30, 20});
            break;
        }
    }
    return b.mesh;
}

// Lilo: a grey tabby with a white chest, belly, socks and a white blaze down her face, sitting up, about 70 tall, nose towards +z.
inline MeshData Cat() {
    Builder b;
    auto box = [&](float cx, float cy, float cz, float hx, float hy, float hz, Rgb col) {
        b.inside = {cx, cy, cz};
        const V3 p[8] = {{cx - hx, cy - hy, cz - hz}, {cx + hx, cy - hy, cz - hz}, {cx + hx, cy + hy, cz - hz}, {cx - hx, cy + hy, cz - hz},
                         {cx - hx, cy - hy, cz + hz}, {cx + hx, cy - hy, cz + hz}, {cx + hx, cy + hy, cz + hz}, {cx - hx, cy + hy, cz + hz}};
        b.Quad(p[0], p[1], p[2], p[3], col); b.Quad(p[4], p[5], p[6], p[7], col); b.Quad(p[0], p[1], p[5], p[4], col);
        b.Quad(p[3], p[2], p[6], p[7], col); b.Quad(p[0], p[3], p[7], p[4], col); b.Quad(p[1], p[2], p[6], p[5], col);
    };
    const Rgb grey = {128, 122, 118}, dark = {72, 66, 64}, white = {238, 236, 232}, pink = {236, 150, 150}, eye = {30, 26, 20};
    box(0, 30, -4, 14, 16, 22, grey);                 // body, sitting up
    box(0, 28, 14, 10, 14, 8, white);                 // white chest
    box(0, 12, 4, 11, 4, 16, white);                  // belly
    box(-13, 14, -14, 6, 14, 10, grey); box(13, 14, -14, 6, 14, 10, grey);   // haunches
    box(-12, 3, -6, 5, 3, 9, white); box(12, 3, -6, 5, 3, 9, white);          // hind paws
    box(-7, 20, 20, 4, 20, 4, white); box(7, 20, 20, 4, 20, 4, white);        // front legs, white
    box(-7, 3, 24, 5, 3, 6, white); box(7, 3, 24, 5, 3, 6, white);            // front paws
    for (int i = 0; i < 4; i++) box(0, 46.5f, -14.0f + i * 7.0f, 10, 0.8f, 2.2f, dark);   // tabby stripes down the back
    box(-14.5f, 36, -6, 0.8f, 6, 2.2f, dark); box(14.5f, 36, -6, 0.8f, 6, 2.2f, dark);
    box(0, 58, 14, 14, 12, 11, grey);                 // head
    box(0, 58, 25.2f, 5, 10, 1.5f, white);            // the white blaze down the face
    box(0, 52, 25, 9, 5, 2, white);                   // white muzzle
    box(0, 55.5f, 27, 2.6f, 1.6f, 1.2f, pink);        // nose
    box(-7.5f, 61, 25.4f, 3.8f, 4.6f, 1, eye); box(7.5f, 61, 25.4f, 3.8f, 4.6f, 1, eye);   // big dark eyes
    box(-6.8f, 62.6f, 26.4f, 1.1f, 1.1f, 0.5f, white); box(8.2f, 62.6f, 26.4f, 1.1f, 1.1f, 0.5f, white);   // glints
    box(-9, 74, 10, 3.6f, 7, 2.4f, grey); box(9, 74, 10, 3.6f, 7, 2.4f, grey);   // pointed ears
    box(-9, 73, 12.4f, 2, 5, 0.6f, pink); box(9, 73, 12.4f, 2, 5, 0.6f, pink);   // pink insides
    box(0, 12, -30, 3.2f, 3.2f, 9, grey);             // the tail, curling up behind in stripes
    box(0, 14, -40, 3.2f, 3.2f, 7, dark);
    box(0, 22, -46, 3.2f, 7, 3.2f, grey);
    box(0, 34, -46, 3.2f, 6, 3.2f, dark);
    box(0, 44, -46, 3.2f, 5, 3.2f, grey);
    return b.mesh;
}


// ---- foliage: grass tufts, trees and drifts of snow (scattered over the field by the game layer, purely for looks) ---------------------------
// `variant` = shape (0-3) + 4 * season (spring, summer, autumn, winter), so the leaves turn with the weather's season. Grass is made 4 times
// life size (the game layer shrinks it) so its thin blades survive the whole-number vertex coordinates.
inline void Put(MeshData& m, V3 p, Rgb c) { m.v.push_back({p.x, p.y, p.z, static_cast<uint8_t>((std::min)(255.0f, (std::max)(0.0f, c.r))), static_cast<uint8_t>((std::min)(255.0f, (std::max)(0.0f, c.g))), static_cast<uint8_t>((std::min)(255.0f, (std::max)(0.0f, c.b)))}); }
inline Rgb Mix(Rgb a, Rgb b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }

inline MeshData Grass(uint32_t variant) {
    const int shape = static_cast<int>(variant % 4), season = static_cast<int>(variant / 4 % 4);
    static const Rgb root[4] = {{40, 120, 40}, {34, 100, 36}, {120, 90, 36}, {120, 140, 120}};
    static const Rgb tip[4] = {{150, 230, 80}, {120, 200, 70}, {226, 190, 80}, {226, 236, 230}};
    MeshData m;
    Lcg rng(900 + variant);
    const int blades = 7 + shape;
    for (int i = 0; i < blades; i++) {
        const float a = (static_cast<float>(i) + rng.Next() * 0.6f) / blades * 6.2831853f;
        const float r0 = 6.0f + rng.Next() * 22.0f;                       // where the blade stands
        const float height = (shape == 1 ? 150.0f : 100.0f) + rng.Next() * 70.0f;
        const float lean = 0.25f + rng.Next() * 0.5f;                     // how far the tip arches outwards
        const V3 base = {std::cos(a) * r0, 0, std::sin(a) * r0};
        const V3 out = {std::cos(a), 0, std::sin(a)}, side = {-std::sin(a), 0, std::cos(a)};
        const float w = 9.0f + rng.Next() * 3.0f;
        const float t = rng.Next() * 0.35f;
        const Rgb c0 = Mix(root[season], tip[season], 0.0f), c1 = Mix(root[season], tip[season], 0.55f + t), c2 = Mix(root[season], tip[season], 1.0f);
        auto at = [&](float f, float hw) { // a point along the blade: f 0..1 up it, hw half width
            const float up = height * f, bend = lean * height * f * f;
            return V3{base.x + out.x * bend + side.x * hw, up, base.z + out.z * bend + side.z * hw};
        };
        const V3 l0 = at(0, -w), r0p = at(0, w), l1 = at(0.5f, -w * 0.7f), r1 = at(0.5f, w * 0.7f), tp = at(1.0f, 0);
        Put(m, l0, c0); Put(m, r0p, c0); Put(m, r1, c1);
        Put(m, l0, c0); Put(m, r1, c1); Put(m, l1, c1);
        Put(m, l1, c1); Put(m, r1, c1); Put(m, tp, c2);
    }
    if (shape == 3 && season < 3) { // a few wild flowers standing in this one
        static const Rgb petal[3] = {{255, 140, 190}, {255, 230, 90}, {250, 150, 70}};
        for (int i = 0; i < 3; i++) {
            const float a = rng.Next() * 6.2831853f, r = 8.0f + rng.Next() * 14.0f, h = 80.0f + rng.Next() * 40.0f;
            const V3 c = {std::cos(a) * r, h, std::sin(a) * r};
            const Rgb col = petal[(i + season) % 3];
            for (int k = 0; k < 4; k++) {
                const float b0 = k * 1.5707963f, b1 = b0 + 1.5707963f;
                Put(m, c, {255, 245, 200}); Put(m, {c.x + std::cos(b0) * 16, c.y + 4, c.z + std::sin(b0) * 16}, col); Put(m, {c.x + std::cos(b1) * 16, c.y + 4, c.z + std::sin(b1) * 16}, col);
            }
            Put(m, {c.x - 2, 0, c.z}, {50, 110, 40}); Put(m, {c.x + 2, 0, c.z}, {50, 110, 40}); Put(m, c, {90, 160, 60});
        }
    }
    return m;
}

inline void Blob(Builder& b, float cx, float cy, float cz, float rx, float ry, float rz, Rgb col, uint32_t seed, float snowTop = 0.0f) {
    const float t = 1.6180339887f;
    V3 base[12] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    static const int faces[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                                     {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    Lcg rng(seed);
    V3 p[12];
    for (int i = 0; i < 12; i++) {
        const V3 n = Norm(base[i]);
        const float j = 0.82f + rng.Next() * 0.36f;
        p[i] = {cx + n.x * rx * j, cy + n.y * ry * j, cz + n.z * rz * j};
    }
    b.inside = {cx, cy, cz};
    for (const auto& f : faces) {
        const V3 centre = {(p[f[0]].x + p[f[1]].x + p[f[2]].x) / 3, (p[f[0]].y + p[f[1]].y + p[f[2]].y) / 3, (p[f[0]].z + p[f[1]].z + p[f[2]].z) / 3};
        const bool top = snowTop > 0 && (centre.y - cy) > ry * (1.0f - snowTop * 1.2f) * 0.4f && (centre.y - cy) > 0;
        b.Tri(p[f[0]], p[f[1]], p[f[2]], top ? Rgb{240, 244, 250} : col);
    }
}

inline void Prism(Builder& b, float cx, float cz, float y0, float y1, float r0, float r1, int sides, Rgb col) {
    b.inside = {cx, (y0 + y1) * 0.5f, cz};
    for (int i = 0; i < sides; i++) {
        const float a0 = static_cast<float>(i) / sides * 6.2831853f, a1 = static_cast<float>(i + 1) / sides * 6.2831853f;
        const V3 p00 = {cx + std::cos(a0) * r0, y0, cz + std::sin(a0) * r0}, p01 = {cx + std::cos(a1) * r0, y0, cz + std::sin(a1) * r0};
        const V3 p10 = {cx + std::cos(a0) * r1, y1, cz + std::sin(a0) * r1}, p11 = {cx + std::cos(a1) * r1, y1, cz + std::sin(a1) * r1};
        b.Quad(p00, p01, p11, p10, col);
    }
}

inline MeshData Tree(uint32_t variant) {
    const int shape = static_cast<int>(variant % 4), season = static_cast<int>(variant / 4 % 4);
    static const Rgb leaf[4] = {{104, 190, 70}, {50, 140, 52}, {214, 110, 36}, {88, 130, 90}};
    static const Rgb leaf2[4] = {{130, 214, 84}, {70, 164, 60}, {232, 170, 50}, {110, 150, 112}};
    const float snow = season == 3 ? 1.0f : 0.0f;
    Builder b;
    const Rgb bark = shape == 3 ? Rgb{226, 222, 212} : Rgb{104, 70, 40};
    switch (shape) {
        case 1: { // a pine: tall trunk, three stacked cones
            Prism(b, 0, 0, 0, 120, 14, 9, 8, bark);
            const Rgb green = season == 2 ? Rgb{60, 110, 60} : leaf[1];
            const float heights[3] = {70, 150, 230}, radii[3] = {96, 72, 48};
            for (int i = 0; i < 3; i++) {
                const float y0 = heights[i], y1 = y0 + 110;
                b.inside = {0, (y0 + y1) * 0.5f, 0};
                for (int k = 0; k < 8; k++) {
                    const float a0 = k / 8.0f * 6.2831853f, a1 = (k + 1) / 8.0f * 6.2831853f;
                    const V3 q0 = {std::cos(a0) * radii[i], y0, std::sin(a0) * radii[i]}, q1 = {std::cos(a1) * radii[i], y0, std::sin(a1) * radii[i]};
                    b.Tri(q0, q1, {0, y1, 0}, (snow > 0 && k % 2 == 0) ? Rgb{236, 242, 248} : (i % 2 ? Mix(green, leaf2[1], 0.4f) : green));
                    b.Tri(q0, q1, {0, y0 - 6, 0}, Mix(green, Rgb{0, 0, 0}, 0.35f));
                }
            }
            break;
        }
        case 2: { // a wide, round-crowned tree
            Prism(b, 0, 0, 0, 110, 20, 14, 8, bark);
            Blob(b, 0, 190, 0, 118, 86, 118, leaf[season], 710 + variant, snow);
            Blob(b, 70, 150, 30, 76, 60, 76, leaf2[season], 720 + variant, snow);
            Blob(b, -64, 160, -26, 80, 62, 80, leaf2[season], 730 + variant, snow);
            break;
        }
        case 3: { // a slender birch: pale trunk, a light crown
            Prism(b, 0, 0, 0, 170, 9, 6, 6, bark);
            Prism(b, 0, 0, 40, 46, 9.6f, 9.6f, 6, {40, 36, 30});
            Prism(b, 0, 0, 100, 106, 8.2f, 8.2f, 6, {40, 36, 30});
            Blob(b, 0, 230, 0, 72, 90, 72, leaf2[season], 740 + variant, snow);
            Blob(b, 26, 170, 18, 46, 50, 46, leaf[season], 750 + variant, snow);
            break;
        }
        default: { // an oak
            Prism(b, 0, 0, 0, 120, 18, 12, 8, bark);
            Prism(b, 20, 0, 90, 150, 7, 5, 6, bark);   // a bough
            Blob(b, 0, 200, 0, 96, 78, 96, leaf[season], 700 + variant, snow);
            Blob(b, 64, 170, 20, 62, 52, 62, leaf2[season], 701 + variant, snow);
            Blob(b, -56, 176, -24, 66, 54, 66, leaf2[season], 702 + variant, snow);
            break;
        }
    }
    return b.mesh;
}

// A low mound of snow, 200 across and a little over a hand tall: scattered close together they read as a snowy ground. Variants 4-7 are a
// blanket instead: a broad, nearly flat sheet about 300 across with a soft hump, laid between the mounds once the snow lies deep.
inline MeshData SnowBlanket(uint32_t variant);
inline MeshData SnowPatch(uint32_t variant) {
    if (variant >= 4) return SnowBlanket(variant);
    Builder b;
    b.inside = {0, -20, 0};
    Lcg rng(800 + variant);
    const int n = 10;
    const float rad[3] = {100.0f, 72.0f, 38.0f}, hgt[3] = {0.0f, 9.0f, 15.0f};
    std::vector<V3> ring[3];
    for (int r = 0; r < 3; r++) {
        for (int i = 0; i < n; i++) {
            const float a = static_cast<float>(i) / n * 6.2831853f;
            const float j = r == 0 ? 0.78f + rng.Next() * 0.44f : 0.9f + rng.Next() * 0.2f;
            ring[r].push_back({std::cos(a) * rad[r] * j, hgt[r] + (r == 0 ? 0.0f : rng.Next() * 3.0f), std::sin(a) * rad[r] * j});
        }
    }
    const Rgb snow = {244, 247, 252};
    for (int r = 0; r < 2; r++)
        for (int i = 0; i < n; i++) {
            const int k = (i + 1) % n;
            b.Quad(ring[r][i], ring[r][k], ring[r + 1][k], ring[r + 1][i], r == 0 ? Rgb{226, 234, 246} : snow);
        }
    for (int i = 0; i < n; i++) b.Tri(ring[2][i], ring[2][(i + 1) % n], {0, 17.0f, 0}, snow);
    return b.mesh;
}


inline MeshData SnowBlanket(uint32_t variant) {
    Builder b;
    b.inside = {0, -40, 0};
    Lcg rng(900 + variant);
    const int n = 12;
    const float rad[3] = {150.0f, 105.0f, 50.0f}, hgt[3] = {0.0f, 3.5f, 6.0f};
    std::vector<V3> ring[3];
    for (int r = 0; r < 3; r++)
        for (int i = 0; i < n; i++) {
            const float a = (static_cast<float>(i) + (r == 1 ? 0.5f : 0.0f)) / n * 6.2831853f;
            const float j = r == 0 ? 0.75f + rng.Next() * 0.5f : 0.85f + rng.Next() * 0.3f;
            ring[r].push_back({std::cos(a) * rad[r] * j, hgt[r] + (r == 0 ? 0.0f : rng.Next() * 1.5f), std::sin(a) * rad[r] * j});
        }
    // The faces are almost level, so the sun shades them all alike; a cool blue edge and a warm top keep the low-poly facets readable.
    const Rgb edge = {214, 226, 244}, mid = {236, 242, 250}, top = {250, 251, 253};
    for (int r = 0; r < 2; r++)
        for (int i = 0; i < n; i++) {
            const int k = (i + 1) % n;
            const Rgb c = Mix(r == 0 ? edge : mid, r == 0 ? mid : top, rng.Next() * 0.5f);
            b.Quad(ring[r][i], ring[r][k], ring[r + 1][k], ring[r + 1][i], c);
        }
    for (int i = 0; i < n; i++) b.Tri(ring[2][i], ring[2][(i + 1) % n], {0, 7.0f, 0}, top);
    return b.mesh;
}

// A rain puddle, about 200 across and flat on the ground: a rim of wet, dark earth, then water that is deep blue-grey at the edge and pales
// towards a patch of reflected sky set off to one side (the side the sun comes from). Colours are per vertex and blend smoothly, the way the
// game's own water is painted; the game layer draws it see-through so the ground shows under the shallow edge.
inline MeshData Puddle(uint32_t variant) {
    MeshData m;
    Lcg rng(1000 + variant);
    const int n = 14;
    float jag[n];
    for (int i = 0; i < n; i++) jag[i] = 0.72f + rng.Next() * 0.45f;
    // Rings from the rim inwards: radius (share of the outline), how far the centre slides towards the sky patch, and colour.
    struct RingDef { float r, shift; Rgb c; };
    const RingDef rings[4] = { {1.00f, 0.00f, {78, 64, 48}}, {0.86f, 0.05f, {46, 58, 72}}, {0.58f, 0.18f, {72, 92, 116}}, {0.26f, 0.34f, {146, 170, 196}} };
    const V3 sky = {-38.0f, 0, 26.0f};   // where the bright reflection sits (towards the sun, upper left front)
    auto at = [&](int ring, int i) {
        const float a = static_cast<float>(i) / n * 6.2831853f;
        const float r = 100.0f * rings[ring].r * jag[i];
        return V3{std::cos(a) * r + sky.x * rings[ring].shift, 0.0f, std::sin(a) * r + sky.z * rings[ring].shift};
    };
    for (int r = 0; r < 3; r++)
        for (int i = 0; i < n; i++) {
            const int k = (i + 1) % n;
            const V3 a = at(r, i), b = at(r, k), c = at(r + 1, k), d = at(r + 1, i);
            Put(m, a, rings[r].c); Put(m, b, rings[r].c); Put(m, c, rings[r + 1].c);
            Put(m, a, rings[r].c); Put(m, c, rings[r + 1].c); Put(m, d, rings[r + 1].c);
        }
    const V3 centre = {sky.x * 0.42f, 0.0f, sky.z * 0.42f};
    const Rgb glint = {178, 198, 220};
    for (int i = 0; i < n; i++) { Put(m, at(3, i), rings[3].c); Put(m, at(3, (i + 1) % n), rings[3].c); Put(m, centre, glint); }
    return m;
}

// One ring of a raindrop's ripple: a thin flat band 40 across, bright at its crest and fading to the water's colour on both sides. The game
// layer grows and fades it out over a moment, a few at a time on every puddle while it rains.
inline MeshData Ripple(uint32_t) {
    MeshData m;
    const int n = 12;
    const float rad[3] = {15.0f, 18.0f, 20.0f};
    const Rgb col[3] = {{120, 140, 162}, {222, 232, 244}, {120, 140, 162}};
    for (int r = 0; r < 2; r++)
        for (int i = 0; i < n; i++) {
            const float a0 = static_cast<float>(i) / n * 6.2831853f, a1 = static_cast<float>(i + 1) / n * 6.2831853f;
            const V3 p = {std::cos(a0) * rad[r], 0, std::sin(a0) * rad[r]}, q = {std::cos(a1) * rad[r], 0, std::sin(a1) * rad[r]};
            const V3 s = {std::cos(a1) * rad[r + 1], 0, std::sin(a1) * rad[r + 1]}, t = {std::cos(a0) * rad[r + 1], 0, std::sin(a0) * rad[r + 1]};
            Put(m, p, col[r]); Put(m, q, col[r]); Put(m, s, col[r + 1]);
            Put(m, p, col[r]); Put(m, s, col[r + 1]); Put(m, t, col[r + 1]);
        }
    return m;
}

// ---- the cat, in parts (Lilo): smooth shaded ellipsoids in the markings of a grey tabby with a white chest, belly, muzzle and paws -----------------
// The cat is drawn as a body, a head, four legs and a chain of tail segments so it can walk, sit, groom, stretch and pounce (see the game layer).
// Every part's pivot sits at the origin and stands up from y = 0: legs from the paw (y 0) to the hip (y 24), tail segments from their base along +y,
// the head from the base of the skull. Colours are per vertex with the light baked in, so the surfaces look rounded rather than faceted.
inline Rgb Light(Rgb c, V3 n) {
    static const V3 sun = Norm({-0.45f, 0.8f, 0.4f});
    const float shade = 0.56f + 0.44f * (std::max)(0.0f, Dot(Norm(n), sun));
    return {c.r * shade, c.g * shade, c.b * shade};
}

template <class F>
inline void Ellipsoid(MeshData& m, V3 c, V3 r, int rings, int segs, F colour) {
    const float pi = 3.14159265f;
    std::vector<V3> pos, nrm;
    for (int i = 0; i <= rings; i++) {
        const float lat = -pi * 0.5f + pi * static_cast<float>(i) / rings;
        for (int j = 0; j <= segs; j++) {
            const float a = 2.0f * pi * static_cast<float>(j % segs) / segs;
            const V3 d = {std::cos(lat) * std::cos(a), std::sin(lat), std::cos(lat) * std::sin(a)};
            pos.push_back({c.x + d.x * r.x, c.y + d.y * r.y, c.z + d.z * r.z});
            nrm.push_back(Norm({d.x / r.x, d.y / r.y, d.z / r.z}));
        }
    }
    auto at = [&](int i, int j) { return i * (segs + 1) + j; };
    auto vert = [&](int k) { Put(m, pos[k], Light(colour(nrm[k], pos[k]), nrm[k])); };
    for (int i = 0; i < rings; i++)
        for (int j = 0; j < segs; j++) {
            const int a = at(i, j), b = at(i, j + 1), c2 = at(i + 1, j + 1), d = at(i + 1, j);
            if (i > 0) { vert(a); vert(b); vert(c2); }               // (the first band's lower edge is a point at the pole)
            if (i < rings - 1) { vert(a); vert(c2); vert(d); }
        }
}

inline MeshData CatBody() {
    const Rgb grey = {132, 126, 120}, dark = {74, 68, 66}, white = {240, 238, 232};
    MeshData m;
    auto fur = [&](V3 n, V3 p) {   // tabby stripes across the back and flanks, white underneath
        if (n.y < -0.55f) return white;
        if (p.z > 14.0f && n.y < 0.5f) return white;                              // the chest
        const float band = std::sin(p.z * 0.42f);
        const bool stripe = band > 0.45f && n.y > -0.2f;
        const bool spine = n.y > 0.8f && std::fabs(p.x) < 4.0f;
        return (stripe || spine) ? dark : grey;
    };
    Ellipsoid(m, {0, 34, -2}, {13.5f, 14, 22}, 7, 12, fur);                        // the barrel
    Ellipsoid(m, {0, 33, 16}, {12, 13, 11}, 5, 10, fur);                            // the chest, white at the front
    Ellipsoid(m, {-9, 32, -17}, {9.5f, 12, 12}, 5, 8, fur);                         // haunches
    Ellipsoid(m, {9, 32, -17}, {9.5f, 12, 12}, 5, 8, fur);
    return m;
}

inline MeshData CatHead(uint32_t variant) {   // 0 eyes open, 1 eyes shut, 2 eyes open and mouth open (a meow), 3 eyes half shut
    const Rgb grey = {132, 126, 120}, dark = {74, 68, 66}, white = {240, 238, 232}, pink = {238, 150, 156}, eye = {26, 22, 18};
    MeshData m;
    auto skull = [&](V3 n, V3 p) {
        if (n.z > 0.3f && n.y < 0.15f) return white;                              // the muzzle's white
        if (std::fabs(p.x) < 2.2f && n.y > 0.1f && n.z > -0.2f) return white;     // the blaze down the forehead
        const bool stripe = n.y > 0.4f && (std::fabs(p.x) > 3.0f && std::fabs(p.x) < 5.0f);
        return stripe ? dark : grey;
    };
    Ellipsoid(m, {0, 11, 4}, {12, 10.5f, 11}, 6, 10, skull);                       // the skull
    Ellipsoid(m, {0, 7.2f, 12.5f}, {6.4f, 4.4f, 5.6f}, 4, 8, [&](V3, V3) { return white; });   // the muzzle
    Ellipsoid(m, {-8.5f, 7, 8}, {4.4f, 3.6f, 4.2f}, 3, 6, [&](V3, V3) { return white; });      // cheek fluff
    Ellipsoid(m, {8.5f, 7, 8}, {4.4f, 3.6f, 4.2f}, 3, 6, [&](V3, V3) { return white; });
    for (int side = -1; side <= 1; side += 2) {                                    // pointed ears, pink inside
        const float x = side * 8.0f;
        const V3 b0 = {x - side * 3.5f, 18, 1}, b1 = {x + side * 3.5f, 17.5f, 1}, b2 = {x, 17.5f, 6}, tip = {x + side * 0.8f, 28.5f, 2.5f};
        const Rgb c = grey;
        Put(m, b0, Light(c, {0, 0.2f, -1})); Put(m, b1, Light(c, {0, 0.2f, -1})); Put(m, tip, Light(c, {0, 0.6f, -1}));
        Put(m, b0, Light(c, {-1, 0.3f, 0})); Put(m, b2, Light(c, {-1, 0.3f, 0})); Put(m, tip, Light(c, {-1, 0.6f, 0}));
        Put(m, b1, Light(c, {1, 0.3f, 0})); Put(m, b2, Light(c, {1, 0.3f, 0})); Put(m, tip, Light(c, {1, 0.6f, 0}));
        Put(m, {x - side * 2.0f, 18.6f, 5.2f}, pink); Put(m, {x + side * 2.0f, 18.4f, 5.2f}, pink); Put(m, {x, 26, 3.6f}, pink);   // the inside
    }
    const bool shut = variant == 1, half = variant == 3;
    for (int side = -1; side <= 1; side += 2) {
        const float x = side * 5.4f;
        if (shut) {                                                                // a curved line: eyes shut
            Put(m, {x - 3.0f, 12.0f, 14.4f}, eye); Put(m, {x + 3.0f, 12.0f, 14.4f}, eye); Put(m, {x, 11.0f, 14.8f}, eye);
        } else {
            Ellipsoid(m, {x, 12.4f, 13.2f}, {3.3f, half ? 1.9f : 3.6f, 1.6f}, 4, 8, [&](V3, V3) { return eye; });   // big dark eyes
            Put(m, {x + 0.8f, 13.9f, 14.7f}, white); Put(m, {x + 2.0f, 13.9f, 14.7f}, white); Put(m, {x + 1.4f, 15.1f, 14.7f}, white);   // the glint
        }
    }
    Put(m, {-2.0f, 9.0f, 17.6f}, pink); Put(m, {2.0f, 9.0f, 17.6f}, pink); Put(m, {0, 7.4f, 18.4f}, pink);   // the nose
    if (variant == 2) { Put(m, {-2.2f, 5.6f, 17.4f}, {120, 40, 50}); Put(m, {2.2f, 5.6f, 17.4f}, {120, 40, 50}); Put(m, {0, 3.0f, 16.6f}, {200, 90, 100}); }   // mouth open
    for (int side = -1; side <= 1; side += 2)                                      // whiskers
        for (int k = 0; k < 3; k++) {
            const float y = 6.6f + k * 1.2f, spread = (k - 1) * 3.0f;
            Put(m, {side * 5.0f, y, 16.2f}, white); Put(m, {side * 5.0f, y + 0.5f, 16.0f}, white); Put(m, {side * 15.0f, y + spread * 0.7f, 17.0f}, white);
        }
    for (int k = -1; k <= 1; k++) {                                                // the tabby 'M' on the forehead
        Put(m, {k * 3.2f - 0.7f, 17.0f, 8.5f}, dark); Put(m, {k * 3.2f + 0.7f, 17.0f, 8.5f}, dark); Put(m, {k * 3.2f, 21.0f, 7.0f}, dark);
    }
    return m;
}

inline MeshData CatTailSeg(uint32_t variant) {   // 0 grey, 1 dark, 2 the dark rounded tip
    const Rgb grey = {132, 126, 120}, dark = {74, 68, 66};
    MeshData m;
    const Rgb col = variant == 0 ? grey : dark;
    const float r0 = 3.6f, r1 = 3.2f, len = 12.0f;
    for (int i = 0; i < 6; i++) {
        const float a0 = 6.2831853f * i / 6, a1 = 6.2831853f * (i + 1) / 6;
        const V3 p00 = {std::cos(a0) * r0, 0, std::sin(a0) * r0}, p01 = {std::cos(a1) * r0, 0, std::sin(a1) * r0};
        const V3 p10 = {std::cos(a0) * r1, len, std::sin(a0) * r1}, p11 = {std::cos(a1) * r1, len, std::sin(a1) * r1};
        const V3 n0 = {std::cos(a0), 0, std::sin(a0)}, n1 = {std::cos(a1), 0, std::sin(a1)};
        Put(m, p00, Light(col, n0)); Put(m, p01, Light(col, n1)); Put(m, p11, Light(col, n1));
        Put(m, p00, Light(col, n0)); Put(m, p11, Light(col, n1)); Put(m, p10, Light(col, n0));
    }
    if (variant == 2) Ellipsoid(m, {0, len, 0}, {3.2f, 3.4f, 3.2f}, 3, 6, [&](V3, V3) { return dark; });   // the rounded tip
    else for (int i = 0; i < 6; i++) { Put(m, {0, len, 0}, Light(col, {0, 1, 0})); Put(m, {std::cos(6.2831853f * i / 6) * r1, len, std::sin(6.2831853f * i / 6) * r1}, Light(col, {0, 1, 0})); Put(m, {std::cos(6.2831853f * (i + 1) / 6) * r1, len, std::sin(6.2831853f * (i + 1) / 6) * r1}, Light(col, {0, 1, 0})); }
    return m;
}

inline MeshData CatLeg(uint32_t variant) {   // 0 a front leg, 1 a hind leg (a fuller thigh); the paw is white, standing on y 0, hip at y 24
    const Rgb grey = {132, 126, 120}, white = {240, 238, 232};
    MeshData m;
    const bool hind = variant == 1;
    Ellipsoid(m, {0, hind ? 17.0f : 18.0f, 0}, {hind ? 6.0f : 4.4f, hind ? 8.5f : 7.5f, hind ? 7.0f : 4.8f}, 4, 6, [&](V3 n, V3) { return n.y < -0.4f && !hind ? white : grey; });
    const float r0 = hind ? 3.0f : 3.4f, r1 = hind ? 2.6f : 3.0f;
    for (int i = 0; i < 6; i++) {   // the lower leg, white in front ('mittens') and on the hind
        const float a0 = 6.2831853f * i / 6, a1 = 6.2831853f * (i + 1) / 6;
        const V3 p00 = {std::cos(a0) * r1, 3.0f, std::sin(a0) * r1}, p01 = {std::cos(a1) * r1, 3.0f, std::sin(a1) * r1};
        const V3 p10 = {std::cos(a0) * r0, 14.0f, std::sin(a0) * r0}, p11 = {std::cos(a1) * r0, 14.0f, std::sin(a1) * r0};
        const V3 n0 = {std::cos(a0), 0, std::sin(a0)}, n1 = {std::cos(a1), 0, std::sin(a1)};
        const Rgb c = variant == 0 ? white : Rgb{200, 196, 190};
        Put(m, p00, Light(c, n0)); Put(m, p01, Light(c, n1)); Put(m, p11, Light(c, n1));
        Put(m, p00, Light(c, n0)); Put(m, p11, Light(c, n1)); Put(m, p10, Light(c, n0));
    }
    Ellipsoid(m, {0, 2.0f, 1.2f}, {3.6f, 2.0f, 4.6f}, 3, 6, [&](V3, V3) { return white; });   // the paw
    return m;
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
        case MeshKind::GliderFrame: return mesh_detail::Glider(0, false);
        case MeshKind::Sign: return mesh_detail::Sign();
        case MeshKind::Ally: return mesh_detail::Ally(variant);
        case MeshKind::Cat: return mesh_detail::Cat();
        case MeshKind::CatBody: return mesh_detail::CatBody();
        case MeshKind::CatHead: return mesh_detail::CatHead(variant);
        case MeshKind::CatTailSeg: return mesh_detail::CatTailSeg(variant);
        case MeshKind::CatLeg: return mesh_detail::CatLeg(variant);
        case MeshKind::Grass: return mesh_detail::Grass(variant);
        case MeshKind::Tree: return mesh_detail::Tree(variant);
        case MeshKind::SnowPatch: return mesh_detail::SnowPatch(variant);
        case MeshKind::Puddle: return mesh_detail::Puddle(variant);
        case MeshKind::Ripple: return mesh_detail::Ripple(variant);
        case MeshKind::Dragon: return mesh_detail::Dragon(variant);
        case MeshKind::Platform: return mesh_detail::Platform(variant);
        case MeshKind::Projectile: return mesh_detail::Projectile(variant);
        default: return {};
    }
}

} // namespace royale
