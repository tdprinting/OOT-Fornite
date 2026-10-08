#pragma once
#include "gilded_sword_model.h"
#include "glider_model.h"
#include "boulder_texture.h"
#include "scenery_model.h"
#include "ground_model.h"
#include "playtest_models.h"
#include "props.h"
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
    uint8_t a = 255;   // opacity; only the see-through ground patches (soft rims) set it, everything else is solid
};

enum class MeshKind : uint8_t { Rock, Boulder, Pillar, Roof, Golem, Glider, Dragon, Platform, Projectile, GliderFrame, Sign, Ally, Cat, Grass, Tree, CatBody, CatHead, CatTailSeg, CatLeg, LeafPile, AshDrift, SandDrift, Ripple, Decor, Clutter, ThemeTree, Grenade, Scenery, Ground, GildedSword, VictoryCrown, Count }; // Golem: the mini boss (variant = its BossKind); Glider: variant = colour scheme; Dragon: variant = wing pose
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
    // Lit like Ocarina of Time's painted scenes: faces towards the sun take a warm, golden key light and faces away fall into a cool blue fill,
    // rather than just going darker.
    void Tri(V3 a, V3 b, V3 c, Rgb col) {
        V3 n = Norm(Cross(Sub(b, a), Sub(c, a)));
        const V3 centre = {(a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3, (a.z + b.z + c.z) / 3};
        if (Dot(n, Sub(centre, inside)) < 0) { std::swap(b, c); n = {-n.x, -n.y, -n.z}; }
        static const V3 sun = Norm({-0.45f, 0.8f, 0.4f});
        const float lit = (std::max)(0.0f, Dot(n, sun)), shade = 0.54f + 0.46f * lit;
        const Rgb tint = {0.84f + 0.22f * lit, 0.88f + 0.13f * lit, 1.04f - 0.12f * lit}; // cool fill -> warm key
        auto byte = [&](float f, float k) { return static_cast<uint8_t>((std::min)(255.0f, (std::max)(0.0f, f * shade * k))); };
        for (V3 p : {a, b, c}) mesh.v.push_back({p.x, p.y, p.z, byte(col.r, tint.r), byte(col.g, tint.g), byte(col.b, tint.b)});
    }
    void Quad(V3 a, V3 b, V3 c, V3 d, Rgb col) { Tri(a, b, c, col); Tri(a, c, d, col); }
};

struct Lcg {
    uint32_t s;
    explicit Lcg(uint32_t seed) : s(seed * 2654435761u + 12345u) {}
    float Next() { s = s * 1664525u + 1013904223u; return static_cast<float>(s >> 8) / 16777216.0f; } // 0..1
};

inline void Put(MeshData& m, V3 p, Rgb c) { m.v.push_back({p.x, p.y, p.z, static_cast<uint8_t>((std::min)(255.0f, (std::max)(0.0f, c.r))), static_cast<uint8_t>((std::min)(255.0f, (std::max)(0.0f, c.g))), static_cast<uint8_t>((std::min)(255.0f, (std::max)(0.0f, c.b)))}); }
inline Rgb Mix(Rgb a, Rgb b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }

// One stone for a rock or boulder: an icosahedron split `subdiv` times and pushed out into gentle lumps, with a couple of planes taking a
// broad, soft flat off it, flattened where it sits on the ground at `c`. `size` is its half width, height and depth; `lean` tips it over
// sideways (radians) before it is set down, for slabs that have settled at an angle. It is smooth shaded: every vertex gets a normal averaged
// from the faces around it and its own colour, so the surface reads as rounded stone rather than facets. `colour(height as a fraction of
// the stone, normal, position)` picks each vertex's colour before the light (warm key, cool fill, as in Builder::Tri) is baked in.
template <class F>
inline void Stone(MeshData& m, V3 c, V3 size, float lean, int subdiv, int cuts, Lcg& rng, F colour) {
    const float t = 1.6180339887f;
    std::vector<V3> base = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    std::vector<int> tris = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
                             3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
    for (V3& q : base) q = Norm(q);
    for (int level = 0; level < subdiv; level++) {   // split every face in four, sharing edge midpoints so neighbouring faces still meet
        std::vector<int> next;
        auto mid = [&](int a, int bb) {
            const V3 mp = Norm({(base[a].x + base[bb].x) * 0.5f, (base[a].y + base[bb].y) * 0.5f, (base[a].z + base[bb].z) * 0.5f});
            for (size_t i = 12; i < base.size(); i++)
                if (std::fabs(base[i].x - mp.x) + std::fabs(base[i].y - mp.y) + std::fabs(base[i].z - mp.z) < 1e-4f) return static_cast<int>(i);
            base.push_back(mp);
            return static_cast<int>(base.size() - 1);
        };
        for (size_t k = 0; k < tris.size(); k += 3) {
            const int a = tris[k], b1 = tris[k + 1], c1 = tris[k + 2], ab = mid(a, b1), bc = mid(b1, c1), ca = mid(c1, a);
            for (int q : {a, ab, ca, ab, b1, bc, ca, bc, c1, ab, bc, ca}) next.push_back(q);
        }
        tris.swap(next);
    }
    // Gentle lumps from a few crossed waves over the sphere (not per-vertex noise, which would make it spiky).
    const float stretch = 0.84f + 0.32f * rng.Next(), w[6] = {rng.Next() * 6.28f, rng.Next() * 6.28f, rng.Next() * 6.28f, 1.6f + rng.Next(), 1.6f + rng.Next(), 2.4f + rng.Next()};
    std::vector<V3> p(base.size());
    for (size_t i = 0; i < base.size(); i++) {
        const V3 u = base[i];
        const float lump = 0.94f + 0.07f * std::sin(u.x * w[3] + w[0]) * std::cos(u.z * w[4] + w[1]) + 0.04f * std::sin((u.x + u.y - u.z) * w[5] + w[2]);
        p[i] = {u.x * lump * stretch, u.y * lump, u.z * lump / stretch};
    }
    for (int k = 0; k < cuts; k++) {   // take a broad, soft flat off the stone
        const float a = 6.2831853f * rng.Next(), e = 0.1f + 0.9f * rng.Next();
        const V3 n = Norm({std::cos(a) * std::cos(e), std::sin(e), std::sin(a) * std::cos(e)});
        const float d = 0.7f + 0.15f * rng.Next();
        for (V3& q : p) { const float over = Dot(q, n) - d; if (over > 0) { const float k2 = over * 0.85f; q = {q.x - n.x * k2, q.y - n.y * k2, q.z - n.z * k2}; } }
    }
    const float cl = std::cos(lean), sl = std::sin(lean);
    float lowest = 1e30f, top = -1e30f;
    for (V3& q : p) {
        const V3 s = {q.x * size.x, q.y * size.y, q.z * size.z};
        q = {s.x * cl - s.y * sl, s.x * sl + s.y * cl, s.z};
        lowest = (std::min)(lowest, q.y);
    }
    for (V3& q : p) { q.y = (std::max)(0.0f, q.y - lowest * 0.6f); top = (std::max)(top, q.y); } // sits on the ground with the bottom flattened
    std::vector<V3> nrm(p.size(), V3{0, 0, 0});
    const V3 inside = {0, top * 0.4f, 0};
    for (size_t k = 0; k < tris.size(); k += 3) {   // area-weighted face normals, turned outward, summed into each corner
        const V3 a = p[tris[k]], b1 = p[tris[k + 1]], c1 = p[tris[k + 2]];
        V3 n = Cross(Sub(b1, a), Sub(c1, a));
        if (Dot(n, Sub({(a.x + b1.x + c1.x) / 3, (a.y + b1.y + c1.y) / 3, (a.z + b1.z + c1.z) / 3}, inside)) < 0) n = {-n.x, -n.y, -n.z};
        for (int q = 0; q < 3; q++) { V3& v = nrm[tris[k + q]]; v = {v.x + n.x, v.y + n.y, v.z + n.z}; }
    }
    static const V3 sun = Norm({-0.45f, 0.8f, 0.4f});
    std::vector<Rgb> lit(p.size());
    for (size_t i = 0; i < p.size(); i++) {
        const V3 n = Norm(nrm[i]);
        const Rgb col = colour(p[i].y / (std::max)(top, 1.0f), n, V3{p[i].x + c.x, p[i].y + c.y, p[i].z + c.z});
        const float l = (std::max)(0.0f, Dot(n, sun)), shade = 0.54f + 0.46f * l;
        lit[i] = {col.r * shade * (0.84f + 0.22f * l), col.g * shade * (0.88f + 0.13f * l), col.b * shade * (1.04f - 0.12f * l)}; // cool fill -> warm key
    }
    for (size_t k = 0; k < tris.size(); k += 3) {
        if (p[tris[k]].y + p[tris[k + 1]].y + p[tris[k + 2]].y < 0.01f) continue;   // the flat underside is never seen
        for (int q = 0; q < 3; q++) { const V3 v = p[tris[k + q]]; Put(m, {v.x + c.x, v.y + c.y, v.z + c.z}, lit[tris[k + q]]); }
    }
}
template <class F>
inline void Stone(MeshData& m, V3 c, float radius, float squash, int subdiv, int cuts, Lcg& rng, F colour) {
    Stone(m, c, V3{radius, radius * squash, radius}, 0.0f, subdiv, cuts, rng, colour);
}

// Grass tufts in a loose ring round the foot of a rock: five blades each, darker at the root and sunlit at the tip, leaning outward.
// Blades are a hand wide so they survive the whole-number vertex coordinates the game layer rounds to. `tip` is the colour of the tips.
inline void FootGrass(MeshData& m, float radius, int tufts, Lcg& rng, Rgb root = {48, 96, 40}, Rgb tipA = {128, 184, 72}, Rgb tipB = {104, 164, 62}) {
    for (int k = 0; k < tufts; k++) {
        const float a = 6.2831853f * (k + 0.7f * rng.Next()) / tufts, out = radius * (0.92f + 0.3f * rng.Next());
        const V3 c = {std::cos(a) * out, 0, std::sin(a) * out};
        const float tall = radius * (0.32f + 0.22f * rng.Next());
        for (int blade = 0; blade < 5; blade++) {
            const float ba = a + (blade - 2) * 0.55f + 0.5f * (rng.Next() - 0.5f), side = a + 1.5708f + blade * 0.7f;
            const float wd = (std::max)(2.5f, radius * 0.07f), lean = tall * (0.25f + 0.25f * rng.Next());
            const V3 tipP = {c.x + std::cos(ba) * lean, tall * (0.75f + 0.3f * rng.Next()), c.z + std::sin(ba) * lean};
            Put(m, {c.x - std::cos(side) * wd, 0, c.z - std::sin(side) * wd}, root);
            Put(m, {c.x + std::cos(side) * wd, 0, c.z + std::sin(side) * wd}, root);
            Put(m, tipP, rng.Next() < 0.5f ? tipA : tipB);
        }
    }
}

// The boulder texture (shared/boulder_texture.h, made from the Meshy texture in the project's "boulder" folder), read with wrap-around and
// smoothing between its pixels.
inline Rgb TexAt(float u, float v) {
    const int n = kBoulderTexSize;
    u = (u - std::floor(u)) * n; v = (v - std::floor(v)) * n;
    const int x0 = static_cast<int>(u) % n, y0 = static_cast<int>(v) % n, x1 = (x0 + 1) % n, y1 = (y0 + 1) % n;
    const float fx = u - std::floor(u), fy = v - std::floor(v);
    auto px = [&](int x, int y) { const uint8_t* q = &kBoulderTex[(y * n + x) * 3]; return Rgb{static_cast<float>(q[0]), static_cast<float>(q[1]), static_cast<float>(q[2])}; };
    return Mix(Mix(px(x0, y0), px(x1, y0), fx), Mix(px(x0, y1), px(x1, y1), fx), fy);
}
// The texture laid onto a stone from three sides at once (top, front, side) and blended by which way the surface faces, so it wraps round
// without the smearing a single flat projection gives. One copy of the tile covers `tile` units.
inline Rgb TexOnStone(V3 q, V3 n, float tile) {
    float wx = std::fabs(n.x), wy = std::fabs(n.y), wz = std::fabs(n.z);
    wx *= wx * wx; wy *= wy * wy; wz *= wz * wz;
    const float sum = (std::max)(1e-4f, wx + wy + wz);
    const Rgb a = TexAt(q.z / tile, q.y / tile), b = TexAt(q.x / tile + 0.37f, q.z / tile + 0.61f), c = TexAt(q.x / tile + 0.71f, q.y / tile + 0.13f);
    return {(a.r * wx + b.r * wy + c.r * wz) / sum, (a.g * wx + b.g * wy + c.g * wz) / sum, (a.b * wx + b.b * wy + c.b * wz) / sum};
}

// How stone looks on each map (by royale::Theme: meadow, water, shadow, fire, desert). The texture's own colours are the meadow stone; the
// other places tint it the way their scenes are painted, and dress the tops in what lies about there: moss, algae, ash or sand.
struct StoneLook {
    Rgb tint;        // multiplies the texture's colour
    float grey;      // how far its colour is pulled towards grey (0 = as painted)
    float contrast;  // how strongly the texture's mottling shows
    Rgb cap;         // what grows or settles on top
    float capAmount; // 0 = bare tops
    Rgb lichen, foot;
    Rgb grassRoot, grassTip;
    bool grass;      // tufts round the foot
};
constexpr int kStoneLooks = 5;
inline const StoneLook& StoneLookOf(uint32_t theme) {
    static const StoneLook looks[kStoneLooks] = {
        {{1.18f, 1.16f, 1.22f}, 0.25f, 4.6f, {84, 140, 56}, 0.75f, {176, 172, 126}, {76, 70, 52}, {48, 96, 40}, {128, 184, 72}, true},     // Hyrule Field: olive stone, mossy caps
        {{1.02f, 1.12f, 1.38f}, 0.45f, 4.0f, {58, 120, 92}, 0.65f, {150, 168, 150}, {52, 60, 58}, {40, 90, 60}, {100, 170, 110}, true},    // Lake Hylia: cool, wet, green-teal algae
        {{1.06f, 1.08f, 1.20f}, 0.6f, 4.4f, {66, 92, 54}, 0.5f, {150, 150, 134}, {50, 48, 46}, {44, 70, 40}, {96, 130, 70}, true},      // Kakariko: grey slate with dark moss
        {{0.92f, 0.70f, 0.66f}, 0.25f, 4.6f, {120, 112, 108}, 0.7f, {170, 96, 60}, {40, 30, 28}, {0, 0, 0}, {0, 0, 0}, false},          // Death Mountain: dark basalt, ash on top, ember-red streaks
        {{1.42f, 1.16f, 0.92f}, 0.0f, 4.0f, {236, 206, 148}, 0.85f, {214, 170, 110}, {120, 82, 52}, {0, 0, 0}, {0, 0, 0}, false},       // Desert Colossus: sandstone, drifts of sand
    };
    return looks[theme % kStoneLooks];
}

// The paint for one stone: the texture's colour and mottling, then OoT's soft leaning layers on top, lighter tops, patches of lichen, the
// map's cap (moss, algae, ash or sand) fading in over the top and a damp dark foot. `capped` = whether this stone gets a cap at all.
inline auto StonePaint(const StoneLook& look, Lcg& rng, float size, bool capped) {
    const float ph[4] = {rng.Next() * 6.2831853f, rng.Next() * 6.2831853f, rng.Next() * 6.2831853f, rng.Next() * 6.2831853f};
    const float tilt = (rng.Next() - 0.5f) * 0.8f, scale = 34.0f / size, tile = (std::max)(80.0f, size * 2.0f);
    const Rgb mean = {kBoulderTexMean[0], kBoulderTexMean[1], kBoulderTexMean[2]};
    return [=](float h, V3 n, V3 q) {
        auto field = [&](float f) { f *= scale; return std::sin(q.x * f + ph[0]) * std::sin(q.z * f * 1.3f + ph[1]) + 0.6f * std::sin((q.x + q.z) * f * 2.1f + ph[2]); };
        auto smooth = [](float e0, float e1, float x) { const float u = (std::min)(1.0f, (std::max)(0.0f, (x - e0) / (e1 - e0))); return u * u * (3 - 2 * u); };
        const Rgb t = TexOnStone(q, n, tile);
        Rgb c = {(mean.r + (t.r - mean.r) * look.contrast) * look.tint.r, (mean.g + (t.g - mean.g) * look.contrast) * look.tint.g,
                 (mean.b + (t.b - mean.b) * look.contrast) * look.tint.b};
        const float g = (c.r + c.g + c.b) / 3.0f;
        c = Mix(c, {g, g, g}, look.grey);
        const float layer = std::sin((q.y + q.x * tilt) * 0.33f * scale + ph[3]) * 0.7f + 0.3f * field(0.06f);   // leaning bands, -1..1
        const float k = 1.0f + 0.10f * layer;
        c = {c.r * k, c.g * k, c.b * k};
        c = Mix(c, {c.r * 1.18f, c.g * 1.16f, c.b * 1.1f}, smooth(0.6f, 0.95f, n.y) * 0.6f);   // tops catch the light
        c = Mix(c, look.lichen, smooth(0.95f, 1.35f, field(0.09f)) * 0.55f);                   // patches of lichen (embers on the mountain)
        if (capped) c = Mix(c, look.cap, look.capAmount * smooth(0.35f, 0.75f, n.y) * smooth(0.6f, 0.85f, h + 0.12f * field(0.12f)));
        return Mix(c, look.foot, 1.0f - smooth(0.04f, 0.2f, h));                                 // damp where it meets the ground
    };
}

// A small rock: one rounded stone with a pebble or two against it, in the map's stone. `variant` = shape seed + kBoulderShapes * theme.
inline MeshData Lump(float radius, float squash, uint32_t variant) {
    const StoneLook& look = StoneLookOf(variant / kBoulderShapes);
    Lcg rng(100 + variant % kBoulderShapes * 7 + variant / kBoulderShapes * 131);
    MeshData m;
    Stone(m, {0, 0, 0}, radius, squash, 2, 2, rng, StonePaint(look, rng, radius, false));
    for (int k = 0; k < 2; k++) {   // smaller stones tumbled against its foot
        const float a = 6.2831853f * (k + rng.Next() * 0.6f) / 2, r = radius * (0.2f + 0.12f * rng.Next()), out = radius * (0.82f + 0.1f * rng.Next());
        Stone(m, {std::cos(a) * out, 0, std::sin(a) * out}, r, 0.7f, 0, 1, rng, StonePaint(look, rng, r, false));
    }
    if (look.grass) FootGrass(m, radius, 7, rng, look.grassRoot, look.grassTip, Mix(look.grassTip, look.grassRoot, 0.25f));
    return m;
}

// Boulders, in six shapes so a field of them doesn't look like one stone copied (royale::BoulderShape picks it from the prop's rotation):
// a round mossy dome, a tall standing slab leaning a little, a broad flat-topped table rock, a boulder split in two by a crack, a stack of
// two with a smaller stone balanced on top, and a huddle of three. All are painted from the boulder texture in the map's stone (StoneLook)
// and fit the same footprint (PropRadius), so collision and standing on top (BoulderTop) match what you see.
inline MeshData Boulder(uint32_t variant) {
    const int shape = static_cast<int>(variant % kBoulderShapes);
    const StoneLook& look = StoneLookOf(variant / kBoulderShapes);
    Lcg rng(200 + shape * 17 + variant / kBoulderShapes * 977);
    const float tall = BoulderHeight(shape), k = 1.0f / 1.58f;   // a stone of half height y stands about 1.58 y tall once set down (see Stone)
    MeshData m;
    auto stone = [&](V3 at, V3 size, float lean, int subdiv, bool capped) { Stone(m, at, size, lean, subdiv, subdiv > 0 ? 2 : 1, rng, StonePaint(look, rng, size.x, capped)); };
    auto pebbles = [&](int n, float ring) {   // smaller stones tumbled against the foot
        for (int i = 0; i < n; i++) {
            const float a = 6.2831853f * (i + rng.Next() * 0.6f) / n, r = 82.0f * (0.18f + 0.1f * rng.Next()), out = ring * (0.86f + 0.12f * rng.Next());
            stone({std::cos(a) * out, 0, std::sin(a) * out}, {r, r * 0.7f, r}, 0.0f, 0, false);
        }
    };
    switch (shape) {
        case 0:   // round mossy dome
            stone({0, 0, 0}, {82, tall * k, 82}, 0.0f, 2, true);
            pebbles(3, 76);
            break;
        case 1:   // tall standing slab, settled at a slight lean, with a rubble foot
            stone({0, 0, 0}, {58, tall * k, 34}, 0.12f, 2, true);
            pebbles(4, 58);
            break;
        case 2:   // broad table rock: wide and low, flattened on top so you can stand on it
            stone({0, 0, 0}, {92, tall * k, 80}, 0.0f, 2, true);
            pebbles(2, 90);
            break;
        case 3:   // split in two: halves leaning apart with a dark crack between them
            stone({-34, 0, 0}, {50, tall * k, 70}, -0.16f, 1, true);
            stone({36, 0, 4}, {46, tall * k * 0.92f, 66}, 0.2f, 1, true);
            pebbles(3, 80);
            break;
        case 4: { // a stack: a big base stone with a smaller one balanced on it
            const float base = tall * 0.58f, seat = base * 0.86f;
            stone({0, 0, 0}, {80, base * k, 74}, 0.0f, 1, false);
            stone({6, seat, -4}, {44, (tall - seat) * k, 40}, 0.1f, 1, true);
            pebbles(3, 78);
            break;
        }
        default:  // a huddle of three
            stone({-30, 0, -22}, {52, tall * k, 50}, 0.08f, 1, true);
            stone({34, 0, -14}, {44, tall * k * 0.8f, 44}, -0.1f, 1, true);
            stone({0, 0, 38}, {40, tall * k * 0.66f, 42}, 0.0f, 1, true);
            pebbles(2, 84);
            break;
    }
    float high = 1.0f;   // the lumps and flats vary a stone's height a little: bring the top to exactly the shape's height
    for (const MeshVertex& v : m.v) high = (std::max)(high, v.y);
    for (MeshVertex& v : m.v) v.y *= tall / high;
    if (look.grass) FootGrass(m, 82.0f, 8, rng, look.grassRoot, look.grassTip, Mix(look.grassTip, look.grassRoot, 0.25f));
    return m;
}

// A round (eight-sided) stone post: a footing, a shaft with a mossy foot, and a capital. Eight sides so it looks the same from any angle,
// which lets a row of them stand as a wall.
// The stone colour of each map (royale::Theme) for the built pieces (posts, blocks): a tint on their olive stone.
inline Rgb BuiltStone(uint32_t theme, Rgb c) {
    static const Rgb tint[kStoneLooks] = {{1.0f, 1.0f, 1.0f}, {0.9f, 1.0f, 1.1f}, {0.92f, 0.94f, 1.0f}, {0.72f, 0.6f, 0.58f}, {1.22f, 1.04f, 0.84f}};
    const Rgb k = tint[theme % kStoneLooks];
    return {(std::min)(255.0f, c.r * k.r), (std::min)(255.0f, c.g * k.g), (std::min)(255.0f, c.b * k.b)};
}

// `theme` = the map's (royale::Theme): its stone, and its moss (algae by the lake, ash on the mountain, sand in the desert) round the foot.
inline MeshData Post(uint32_t theme = 0) {
    struct Ring { float r, y; Rgb col; };
    const StoneLook& look = StoneLookOf(theme);
    const Rgb foot = look.grass ? Mix(look.cap, Rgb{80, 132, 58}, 0.5f) : look.cap;
    Ring rings[] = {{40, 0, {84, 80, 70}},     {40, 26, {116, 112, 96}},  {33, 26, {110, 106, 92}},  {33, 62, foot},
                    {33, 74, {138, 132, 112}}, {33, 170, {148, 142, 120}}, {42, 170, {166, 158, 132}}, {42, 200, {180, 170, 140}}};
    for (Ring& g : rings) if (g.y != 62) g.col = BuiltStone(theme, g.col);
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
    for (int i = 0; i < n; i++) b.Tri({0, top.y, 0}, at(top, i), at(top, (i + 1) % n), BuiltStone(theme, {188, 178, 146}));
    return b.mesh;
}

// A gabled cottage roof over a 360 x 280 courtyard (x along the width, z across), standing on posts 200 high. Terracotta tiles in
// stripes, plaster gable ends and a dark ridge beam, in the map's own colours (`theme`): red tiles on the field, blue slate by the lake,
// Kakariko's brown shingles, dark slate on the mountain and a sun-bleached cloth awning in the desert.
inline MeshData Roof(uint32_t theme = 0) {
    static const Rgb tiles[kStoneLooks] = {{178, 76, 54}, {70, 104, 146}, {128, 78, 54}, {72, 62, 66}, {222, 186, 124}};
    const Rgb tile = tiles[theme % kStoneLooks];
    const float hx = 224, hz = 178, eave = 200, ridge = 318;
    Builder b;
    b.inside = {0, eave + 10, 0};
    const int strips = 6;
    for (int side = -1; side <= 1; side += 2) {
        for (int s = 0; s < strips; s++) {
            const float t0 = static_cast<float>(s) / strips, t1 = static_cast<float>(s + 1) / strips;
            const float z0 = side * hz * (1 - t0), z1 = side * hz * (1 - t1), y0 = eave + (ridge - eave) * t0, y1 = eave + (ridge - eave) * t1;
            const float v = (s % 2) ? 1.0f : 0.88f;
            b.Quad({-hx, y0, z0}, {hx, y0, z0}, {hx, y1, z1}, {-hx, y1, z1}, {tile.r * v, tile.g * v, tile.b * v});
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

// The skydiving glider: a hang glider built in Blender (assets/glider/glider.blend, exported to glider_model.h) with its handle bar at the origin,
// so the game can put the bar where Link's hands are. The wing's two stripes take the colours of the variant's scheme. `wings` false leaves the wing
// out (the cloth simulation draws the live one, see cloth.h).
inline MeshData Glider(uint32_t variant, bool wings = true) {
    static const Rgb schemes[4][2] = {
        {{230, 70, 60}, {245, 235, 220}}, {{70, 130, 235}, {245, 220, 90}}, {{70, 190, 100}, {245, 245, 235}}, {{170, 90, 230}, {250, 210, 120}}};
    const Rgb* sc = schemes[variant % 4];
    const Rgb fixedColours[8] = {{120, 90, 56}, {103, 30, 9}, {247, 214, 34}, sc[0], sc[1],
                                 {sc[0].r * 0.7f, sc[0].g * 0.7f, sc[0].b * 0.7f}, {sc[1].r * 0.7f, sc[1].g * 0.7f, sc[1].b * 0.7f}, {86, 60, 36}};
    Builder b;
    auto add = [&](const glider_model::Tri& t) {
        const V3 n = {t.n[0] / 127.0f, t.n[1] / 127.0f, t.n[2] / 127.0f};
        const V3 a = {t.p[0], t.p[1], t.p[2]}, c = {t.p[3], t.p[4], t.p[5]}, d = {t.p[6], t.p[7], t.p[8]};
        // Faces point the way Blender says they do; Builder::Tri turns a face outward from a point, so aim it along the face's own normal.
        b.inside = {a.x - n.x, a.y - n.y, a.z - n.z};
        b.Tri(a, c, d, fixedColours[t.colour & 7]);
    };
    for (int i = 0; i < glider_model::kFrameCount; i++) add(glider_model::kFrame[i]);
    if (wings) for (int i = 0; i < glider_model::kWingCount; i++) add(glider_model::kWing[i]);
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
    // olive stone with dark mortar, tinted to the map's stone (variant / 3 = theme, as the boulders: StoneLook)
    auto t = [&](Rgb c) { return BuiltStone(variant / 3, c); };
    const Rgb body = t({150, 144, 122}), dark = t({82, 78, 68}), top = t({192, 182, 148}), panel = t({164, 154, 124});
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

inline MeshData AuthoredMesh(const playtest_models::Vertex* vertices, size_t count, int season = 1) {
    MeshData mesh;
    mesh.v.reserve(count);
    static const float tint[4][3] = {{1.3f,1.16f,.8f},{1,1,1},{2.5f,.85f,.35f},{.9f,.95f,1.1f}};
    for (size_t i=0;i<count;++i) {
        const auto& v=vertices[i];
        const float k[3]={v.leaf?tint[season][0]:1,v.leaf?tint[season][1]:1,v.leaf?tint[season][2]:1};
        mesh.v.push_back({v.x,v.y,v.z,static_cast<uint8_t>(std::min(255.f,v.r*k[0])),static_cast<uint8_t>(std::min(255.f,v.g*k[1])),static_cast<uint8_t>(std::min(255.f,v.b*k[2]))});
    }
    return mesh;
}
inline MeshData Tree(uint32_t variant) {
    using namespace playtest_models;
    const int season=variant/4%4;
    switch(variant%4) {
        case 1:return AuthoredMesh(tree1,std::size(tree1),season);
        case 2:return AuthoredMesh(tree2,std::size(tree2),season);
        case 3:return AuthoredMesh(tree3,std::size(tree3),season);
        default:return AuthoredMesh(tree0,std::size(tree0),season);
    }
}

// ---- the lived-in world: small things scattered about each map and round its towns (drawn by the game layer near the player, purely for looks) ----
namespace decor_detail {
inline void Box(Builder& b, V3 c, V3 h, float yaw, Rgb col, Rgb top) {   // a box of half size h turned by yaw, standing on c
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    auto P = [&](float x, float y, float z) { return V3{c.x + x * cy - z * sy, c.y + y, c.z + x * sy + z * cy}; };
    b.inside = {c.x, c.y + h.y, c.z};
    const V3 p[8] = {P(-h.x, 0, -h.z), P(h.x, 0, -h.z), P(h.x, 0, h.z), P(-h.x, 0, h.z), P(-h.x, 2 * h.y, -h.z), P(h.x, 2 * h.y, -h.z), P(h.x, 2 * h.y, h.z), P(-h.x, 2 * h.y, h.z)};
    b.Quad(p[0], p[1], p[5], p[4], col); b.Quad(p[1], p[2], p[6], p[5], col); b.Quad(p[2], p[3], p[7], p[6], col); b.Quad(p[3], p[0], p[4], p[7], col);
    b.Quad(p[4], p[5], p[6], p[7], top);
}
inline void Blade(Builder& b, V3 base, float height, float ang, float lean, float w, Rgb root, Rgb tip) {   // one leaning blade or leaf, dark at the root
    const V3 out = {std::cos(ang), 0, std::sin(ang)}, side = {-std::sin(ang), 0, std::cos(ang)};
    auto at = [&](float f, float hw) { const float bend = lean * f * f; return V3{base.x + out.x * bend + side.x * hw, base.y + height * f, base.z + out.z * bend + side.z * hw}; };
    const V3 l0 = at(0, -w), r0 = at(0, w), l1 = at(0.5f, -w * 0.7f), r1 = at(0.5f, w * 0.7f), t = at(1, 0);
    const Rgb c1 = Mix(root, tip, 0.6f);
    Put(b.mesh, l0, root); Put(b.mesh, r0, root); Put(b.mesh, r1, c1);
    Put(b.mesh, l0, root); Put(b.mesh, r1, c1); Put(b.mesh, l1, c1);
    Put(b.mesh, l1, c1); Put(b.mesh, r1, c1); Put(b.mesh, t, tip);
}
inline void Flower(Builder& b, V3 at, float r, Rgb petal) {   // a flat four-petalled flower head on a stalk
    b.inside = {at.x, at.y - 10, at.z};
    for (int k = 0; k < 4; k++) {
        const float a0 = k * 1.5707963f, a1 = a0 + 1.5707963f;
        b.Tri(at, {at.x + std::cos(a0) * r, at.y + 2, at.z + std::sin(a0) * r}, {at.x + std::cos(a1) * r, at.y + 2, at.z + std::sin(a1) * r}, k % 2 ? petal : Mix(petal, {255, 255, 255}, 0.25f));
    }
    Blade(b, {at.x, 0, at.z}, at.y, 0, 0, 1.5f, {50, 110, 40}, {90, 160, 60});
}
} // namespace decor_detail

// Small scenery for each map, so the ground between the towns isn't bare. `variant` = item (0-5) + 6 * theme (meadow, water, shadow, fire, desert):
//   meadow: flower bed, ferns, a ring of mushrooms, a berry bush, tall blue flowers, a mossy fallen log
//   water:  cattails, a clump of tall reeds, pink water flowers, driftwood, lush ferns, shells and pebbles
//   shadow: dark weeds, a patch of gourds, a dead bush, a broken fence, grey toadstools, a small old gravestone
//   fire:   a Bomb Flower, a charred stump, glowing ember crystals, a heap of ash, obsidian shards, a thorny dead bush
//   desert: golden dry grass, a desert shrub, a barrel cactus, bleached bones, a broken sandstone column, a Gerudo pot
inline MeshData Decor(uint32_t variant) {
    using namespace decor_detail;
    const int item = static_cast<int>(variant % 6), theme = static_cast<int>(variant / 6 % 5);
    Builder b;
    Lcg rng(1300 + variant * 7);
    auto R = [&]() { return rng.Next(); };
    auto tuft = [&](int blades, float tall, float spread, Rgb root, Rgb tip, float w) {
        for (int i = 0; i < blades; i++) {
            const float a = (i + R() * 0.7f) / blades * 6.2831853f, r = spread * R();
            Blade(b, {std::cos(a) * r, 0, std::sin(a) * r}, tall * (0.7f + 0.5f * R()), a + (R() - 0.5f), tall * (0.15f + 0.3f * R()), w, root, tip);
        }
    };
    auto log = [&](float len, float rad, Rgb bark, Rgb top) {   // lying along x
        b.inside = {0, rad, 0};
        const int n = 6;
        for (int i = 0; i < n; i++) {
            const float a0 = i * 6.2831853f / n, a1 = (i + 1) * 6.2831853f / n;
            const V3 p0 = {-len, rad + std::cos(a0) * rad, std::sin(a0) * rad}, p1 = {-len, rad + std::cos(a1) * rad, std::sin(a1) * rad};
            const V3 q0 = {len, rad + std::cos(a0) * rad, std::sin(a0) * rad}, q1 = {len, rad + std::cos(a1) * rad, std::sin(a1) * rad};
            b.Quad(p0, p1, q1, q0, (i == 0 || i == n - 1) ? top : bark);
            b.Tri({-len, rad, 0}, p0, p1, {200, 170, 120}); b.Tri({len, rad, 0}, q1, q0, {200, 170, 120});   // cut ends
        }
    };
    switch (theme * 6 + item) {
        // ---- meadow (Hyrule Field) ----
        case 0: { static const Rgb p[3] = {{236, 64, 52}, {250, 214, 60}, {245, 240, 230}}; tuft(10, 20, 26, {40, 110, 40}, {110, 190, 70}, 3);
                  for (int i = 0; i < 9; i++) { const float a = R() * 6.28f, r = 30 * R(); Flower(b, {std::cos(a) * r, 18 + 10 * R(), std::sin(a) * r}, 6, p[i % 3]); } break; }
        case 1: for (int i = 0; i < 9; i++) { const float a = i * 0.7f; Blade(b, {0, 0, 0}, 34 + 10 * R(), a, 30, 7, {40, 100, 40}, {100, 180, 70}); } break;
        case 2: for (int i = 0; i < 7; i++) { const float a = i * 0.9f, r = 28 + 6 * R(); Prism(b, std::cos(a) * r, std::sin(a) * r, 0, 9, 2.5f, 2.5f, 5, {236, 226, 200});
                  Blob(b, std::cos(a) * r, 11, std::sin(a) * r, 8, 4, 8, i % 3 ? Rgb{214, 60, 44} : Rgb{180, 120, 70}, 1400 + i); } break;
        case 3: Blob(b, 0, 22, 0, 34, 24, 30, {54, 128, 52}, 1410); Blob(b, 18, 30, 8, 22, 18, 20, {70, 150, 60}, 1411);
                for (int i = 0; i < 8; i++) { const float a = i * 0.8f; Blob(b, std::cos(a) * 30, 24 + 14 * R(), std::sin(a) * 26, 3.5f, 3.5f, 3.5f, {200, 40, 70}, 1420 + i); } break;
        case 4: tuft(8, 30, 20, {40, 110, 40}, {110, 190, 70}, 3);
                for (int i = 0; i < 6; i++) { const float a = R() * 6.28f, r = 22 * R(); Flower(b, {std::cos(a) * r, 34 + 14 * R(), std::sin(a) * r}, 7, {90, 120, 240}); } break;
        case 5: log(55, 13, {104, 74, 44}, {84, 140, 56}); tuft(5, 14, 50, {40, 100, 40}, {100, 170, 70}, 3); break;
        // ---- water (Lake Hylia) ----
        case 6: for (int i = 0; i < 8; i++) { const float a = R() * 6.28f, r = 16 * R(), h = 60 + 30 * R(); Blade(b, {std::cos(a) * r, 0, std::sin(a) * r}, h, a, 6, 2.5f, {60, 110, 50}, {110, 170, 80});
                  if (i % 2 == 0) Prism(b, std::cos(a) * r + std::cos(a) * 4, std::sin(a) * r + std::sin(a) * 4, h * 0.7f, h * 0.9f, 3.5f, 3.5f, 5, {110, 70, 40}); } break;
        case 7: tuft(14, 80, 22, {50, 100, 60}, {140, 190, 110}, 3.5f); break;
        case 8: tuft(6, 16, 24, {40, 110, 70}, {90, 170, 110}, 6);
                for (int i = 0; i < 4; i++) { const float a = i * 1.6f, r = 18 + 8 * R(); Flower(b, {std::cos(a) * r, 8, std::sin(a) * r}, 9, {250, 150, 200}); } break;
        case 9: log(48, 8, {178, 168, 150}, {196, 190, 172}); break;
        case 10: for (int i = 0; i < 11; i++) { const float a = i * 0.57f; Blade(b, {0, 0, 0}, 40 + 14 * R(), a, 40, 8, {30, 110, 60}, {90, 200, 110}); } break;
        case 11: for (int i = 0; i < 7; i++) { const float a = R() * 6.28f, r = 30 * R(); Blob(b, std::cos(a) * r, 3, std::sin(a) * r, 6 + 3 * R(), 3, 5, i % 3 ? Rgb{236, 220, 200} : Rgb{240, 170, 160}, 1500 + i); } break;
        // ---- shadow (Kakariko) ----
        case 12: tuft(10, 40, 24, {40, 60, 36}, {96, 120, 70}, 3); break;
        case 13: for (int i = 0; i < 4; i++) { const float a = i * 1.7f, r = 22 * R(); Blob(b, std::cos(a) * r, 9, std::sin(a) * r, 11, 9, 11, i % 2 ? Rgb{226, 130, 40} : Rgb{200, 170, 60}, 1600 + i); }
                 tuft(6, 12, 34, {40, 90, 36}, {80, 140, 60}, 4); break;
        case 14: for (int i = 0; i < 9; i++) { const float a = i * 0.7f; Blade(b, {0, 0, 0}, 30 + 14 * R(), a, 24, 1.8f, {80, 64, 48}, {120, 100, 76}); } break;
        case 15: Box(b, {-30, 0, 0}, {3.5f, 22, 3.5f}, 0, {110, 82, 52}, {130, 100, 64}); Box(b, {30, 0, 0}, {3.5f, 16, 3.5f}, 0.2f, {110, 82, 52}, {130, 100, 64});
                 Box(b, {0, 26, 0}, {34, 2.5f, 2}, 0.1f, {128, 96, 60}, {150, 116, 74}); Box(b, {-4, 12, 0}, {30, 2.5f, 2}, -0.15f, {128, 96, 60}, {150, 116, 74}); break;
        case 16: for (int i = 0; i < 5; i++) { const float a = i * 1.3f, r = 16 * R(); Prism(b, std::cos(a) * r, std::sin(a) * r, 0, 12, 2.5f, 2.5f, 5, {200, 196, 180});
                 Blob(b, std::cos(a) * r, 14, std::sin(a) * r, 7, 3.5f, 7, {150, 140, 130}, 1620 + i); } break;
        case 17: Box(b, {0, 0, 0}, {16, 24, 5}, 0.05f, {128, 128, 120}, {150, 150, 140}); Blob(b, 0, 48, 0, 16, 7, 5, {128, 128, 120}, 1630);
                 tuft(5, 10, 22, {40, 80, 40}, {90, 130, 70}, 3); break;
        // ---- fire (Death Mountain) ----
        case 18: for (int i = 0; i < 6; i++) { const float a = i * 1.047f; Blade(b, {0, 0, 0}, 14, a, 22, 7, {40, 90, 50}, {70, 150, 70}); }
                 Blob(b, 0, 16, 0, 11, 11, 11, {44, 50, 80}, 1700); Blob(b, 0, 28, 0, 3, 4, 3, {250, 170, 60}, 1701); break;   // its fuse lit gold
        case 19: Prism(b, 0, 0, 0, 30, 15, 11, 7, {48, 38, 32}); Prism(b, 0, 0, 30, 33, 11, 6, 7, {230, 110, 40}); break;
        case 20: for (int i = 0; i < 5; i++) { const float a = i * 1.25f, r = 10 * R(); Prism(b, std::cos(a) * r, std::sin(a) * r, 0, 18 + 18 * R(), 6, 0.5f, 4, i % 2 ? Rgb{255, 120, 40} : Rgb{250, 200, 70}); } break;
        case 21: Blob(b, 0, 2, 0, 40, 12, 34, {120, 116, 112}, 1710); Blob(b, 14, 6, 6, 18, 9, 16, {150, 146, 140}, 1711); break;
        case 22: for (int i = 0; i < 4; i++) { const float a = i * 1.6f, r = 18 * R(); Prism(b, std::cos(a) * r, std::sin(a) * r, 0, 14 + 22 * R(), 7, 0.5f, 3, i % 2 ? Rgb{52, 46, 70} : Rgb{90, 84, 120}); } break;   // glassy, catching the light
        case 23: for (int i = 0; i < 10; i++) { const float a = i * 0.63f; Blade(b, {0, 0, 0}, 26 + 14 * R(), a, 22, 1.5f, {50, 36, 30}, {90, 70, 56}); } break;
        // ---- desert (Desert Colossus) ----
        case 24: tuft(12, 32, 22, {150, 110, 50}, {236, 200, 110}, 3); break;
        case 25: Blob(b, 0, 16, 0, 26, 16, 24, {120, 124, 70}, 1800); Blob(b, 12, 22, -6, 16, 12, 14, {140, 140, 80}, 1801); break;
        case 26: Blob(b, 0, 18, 0, 16, 20, 16, {70, 130, 70}, 1810); Prism(b, 26, 6, 0, 52, 8, 7, 6, {80, 140, 76}); Prism(b, 26, 6, 52, 58, 7, 2, 6, {240, 120, 150});
                 Prism(b, 32, 6, 26, 30, 3, 3, 4, {80, 140, 76}); break;
        case 27: for (int i = 0; i < 5; i++) { const float x = -24 + i * 12.0f; Box(b, {x, 0, 0}, {2, 10 - std::fabs(x) * 0.2f, 16}, 0, {232, 222, 196}, {244, 236, 214}); }
                 Blob(b, 40, 9, 0, 11, 9, 10, {236, 226, 200}, 1820); break;   // a ribcage and a skull
        case 28: Prism(b, 0, 0, 0, 34, 20, 19, 8, {214, 170, 110}); Prism(b, 0, 0, 34, 40, 19, 14, 8, {196, 150, 96}); Blob(b, 30, 7, 10, 12, 7, 10, {214, 170, 110}, 1830); break;
        default: Prism(b, 0, 0, 0, 18, 14, 18, 8, {180, 96, 60}); Prism(b, 0, 0, 18, 30, 18, 9, 8, {196, 110, 70}); Prism(b, 0, 0, 30, 36, 9, 11, 8, {150, 80, 50}); break;
    }
    for (MeshVertex& v : b.mesh.v) v.y = (std::max)(0.0f, v.y);   // the round bits sit on the ground, not through it
    return b.mesh;
}

// The clutter of people living there, set about each town by the game layer: `variant` 0 a crate, 1 a barrel, 2 clay pots, 3 a hay bale,
// 4 a hand cart, 5 a lantern post, 6 a fire pit, 7 a signpost.
inline MeshData Clutter(uint32_t variant) {
    using namespace decor_detail;
    Builder b;
    const Rgb wood = {150, 104, 58}, dark = {96, 64, 36}, iron = {70, 70, 76};
    switch (variant % 8) {
        case 0: Box(b, {0, 0, 0}, {20, 20, 20}, 0, wood, {170, 124, 74}); Box(b, {0, 0, 0}, {21, 3, 21}, 0, dark, dark); Box(b, {0, 34, 0}, {21, 3, 21}, 0, dark, {120, 84, 50}); break;
        case 1: Prism(b, 0, 0, 0, 22, 15, 18, 8, wood); Prism(b, 0, 0, 22, 44, 18, 15, 8, wood); Prism(b, 0, 0, 20, 24, 18.6f, 18.6f, 8, iron); Prism(b, 0, 0, 44, 45, 15, 2, 8, {120, 84, 50}); break;
        case 2: for (int i = 0; i < 3; i++) { const float x = i * 22.0f - 22.0f, s = i == 1 ? 1.2f : 0.9f; Prism(b, x, (i % 2) * 8.0f, 0, 14 * s, 8 * s, 13 * s, 8, {176, 92, 56});
                Prism(b, x, (i % 2) * 8.0f, 14 * s, 24 * s, 13 * s, 6 * s, 8, {196, 110, 70}); Prism(b, x, (i % 2) * 8.0f, 24 * s, 28 * s, 6 * s, 7 * s, 8, {150, 78, 46}); } break;
        case 3: Box(b, {0, 0, 0}, {30, 18, 20}, 0, {226, 190, 96}, {240, 210, 120}); Box(b, {0, 0, 0}, {31, 18.5f, 3}, 0, {120, 90, 50}, {120, 90, 50}); break;
        case 4: Box(b, {0, 14, 0}, {40, 10, 24}, 0, wood, {128, 88, 50}); Box(b, {50, 18, 10}, {26, 2, 2}, 0, dark, dark); Box(b, {50, 18, -10}, {26, 2, 2}, 0, dark, dark);
                for (int s = -1; s <= 1; s += 2) { Builder& bb = b; bb.inside = {0, 16, s * 26.0f};
                    for (int k = 0; k < 8; k++) { const float a0 = k * 0.785f, a1 = a0 + 0.785f; bb.Tri({0, 16, s * 26.0f}, {std::cos(a0) * 16, 16 + std::sin(a0) * 16, s * 26.0f}, {std::cos(a1) * 16, 16 + std::sin(a1) * 16, s * 26.0f}, dark); } }
                Blob(b, -10, 34, 0, 18, 10, 14, {226, 190, 96}, 1900); break;
        case 5: Prism(b, 0, 0, 0, 110, 4, 3, 6, dark); Box(b, {9, 104, 0}, {12, 1.5f, 1.5f}, 0, dark, dark); Prism(b, 18, 0, 84, 100, 7, 7, 4, {255, 214, 120});
                Prism(b, 18, 0, 100, 106, 8, 2, 4, iron); break;
        case 6: for (int i = 0; i < 8; i++) { const float a = i * 0.785f; Blob(b, std::cos(a) * 30, 5, std::sin(a) * 30, 9, 6, 9, {130, 124, 112}, 1910 + i); }
                for (int i = 0; i < 3; i++) Box(b, {0, 3 + i * 3.0f, 0}, {20, 3, 3}, i * 1.05f, dark, {60, 40, 26});
                Prism(b, 0, 0, 6, 34, 12, 0.5f, 5, {250, 150, 40}); Prism(b, 0, 0, 6, 22, 7, 0.5f, 5, {255, 230, 110}); break;
        default: Box(b, {0, 0, 0}, {3.5f, 50, 3.5f}, 0, dark, dark); Box(b, {14, 76, 0}, {24, 9, 2}, 0.05f, wood, {170, 124, 74}); Box(b, {-12, 58, 0}, {20, 8, 2}, -0.08f, wood, {170, 124, 74}); break;
    }
    for (MeshVertex& v : b.mesh.v) v.y = (std::max)(0.0f, v.y);
    return b.mesh;
}

// Trees for the places the field's trees don't belong: `variant` 0-3 a palm (the desert's oases), 4-7 a dead, burnt tree (the mountain).
inline MeshData ThemeTree(uint32_t variant) {
    Builder b;
    Lcg rng(2000 + variant);
    if (variant % 8 < 4) {
        const float lean = 0.12f + 0.1f * (variant % 4), tall = 200.0f + 30.0f * (variant % 3);
        V3 at = {0, 0, 0};
        for (int s = 0; s < 6; s++) {   // a curving trunk in rings
            const float y0 = tall * s / 6, y1 = tall * (s + 1) / 6, x0 = lean * y0 * y0 / tall, x1 = lean * y1 * y1 / tall;
            b.inside = {(x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 0};
            for (int k = 0; k < 6; k++) {
                const float a0 = k * 1.047f, a1 = a0 + 1.047f, r0 = 11.0f - s, r1 = 10.0f - s;
                b.Quad({x0 + std::cos(a0) * r0, y0, std::sin(a0) * r0}, {x0 + std::cos(a1) * r0, y0, std::sin(a1) * r0}, {x1 + std::cos(a1) * r1, y1, std::sin(a1) * r1},
                       {x1 + std::cos(a0) * r1, y1, std::sin(a0) * r1}, s % 2 ? Rgb{150, 112, 70} : Rgb{128, 92, 56});
            }
            at = {x1, y1, 0};
        }
        for (int f = 0; f < 7; f++) {   // fronds drooping from the crown
            const float a = f * 0.9f + rng.Next() * 0.3f, len = 90.0f + 20.0f * rng.Next();
            const V3 tip = {at.x + std::cos(a) * len, at.y - 40.0f - 20.0f * rng.Next(), at.z + std::sin(a) * len};
            const V3 mid = {at.x + std::cos(a) * len * 0.5f, at.y + 14.0f, at.z + std::sin(a) * len * 0.5f};
            const V3 side = {-std::sin(a) * 18.0f, 0, std::cos(a) * 18.0f};
            b.inside = {at.x, at.y - 30, at.z};
            b.Tri(at, {mid.x + side.x, mid.y, mid.z + side.z}, mid, {70, 150, 60}); b.Tri(at, mid, {mid.x - side.x, mid.y, mid.z - side.z}, {90, 170, 70});
            b.Tri({mid.x + side.x, mid.y, mid.z + side.z}, tip, mid, {80, 160, 64}); b.Tri(mid, tip, {mid.x - side.x, mid.y, mid.z - side.z}, {60, 136, 54});
        }
        Blob(b, at.x, at.y - 6, at.z, 9, 8, 9, {120, 90, 50}, 2100 + variant);   // coconuts
    } else {
        const Rgb bark = {58, 46, 40}, ember = {200, 90, 40};
        Prism(b, 0, 0, 0, 140, 15, 8, 7, bark);
        for (int k = 0; k < 4; k++) {   // bare, crooked branches
            const float a = k * 1.6f + rng.Next(), y = 70.0f + 20.0f * k, len = 50.0f + 20.0f * rng.Next();
            const V3 p0 = {std::cos(a) * 6, y, std::sin(a) * 6}, p1 = {std::cos(a) * len, y + 30 + 20 * rng.Next(), std::sin(a) * len};
            const V3 side = {-std::sin(a) * 4.0f, 0, std::cos(a) * 4.0f};
            b.inside = {0, y - 20, 0};
            b.Tri({p0.x - side.x, p0.y, p0.z - side.z}, {p0.x + side.x, p0.y, p0.z + side.z}, p1, bark);
            b.Tri({p0.x, p0.y - 5, p0.z}, {p0.x, p0.y + 5, p0.z}, p1, Mix(bark, {0, 0, 0}, 0.3f));
        }
        Prism(b, 0, 0, 0, 10, 17, 15, 7, ember);   // still smouldering at the foot
    }
    return b.mesh;
}

// ---- Hyrule Field's scenery for the Fortnite Map ----------------------------------------------------------------------------------------
// Made in Blender with painted textures (tools/scenery/build_scenery.py, assets/scenery/scenery.blend) after the Ocarina of Time field artwork:
// round, glossy oaks with sunlit tops, flowering hedges, warm red-brown rock that catches pink light, strata cliffs with a grassy lip, pink-lit snow
// crags, drifts of small white, yellow and pink flowers, and cattails at the water. The textures are baked into each corner's colour in
// shared/scenery_model.h. `variant` = item (0-7) + 8 * season (spring, summer, autumn, winter, as in FloraSeason):
//   0 a lone field oak   1 a flowering hedge   2 a boulder with its pebbles   3 a strata cliff slab   4 a snow-capped crag
//   5 white and yellow flowers   6 pink flowers with tall spikes   7 cattails and reeds
inline MeshData Scenery(uint32_t variant) {
    namespace sm = scenery_model;
    const int item = static_cast<int>(variant % 8), season = static_cast<int>(variant / 8 % 4);
    static const uint8_t* const colours[4] = {sm::kColSpring, sm::kColSummer, sm::kColAutumn, sm::kColWinter};
    int first = 0;
    for (int i = 0; i < item; i++) first += sm::kCorners[i];
    MeshData m;
    m.v.reserve(static_cast<size_t>(sm::kCorners[item]));
    for (int k = first; k < first + sm::kCorners[item]; k++) {
        const int16_t* p = &sm::kPos[k * 3];
        const uint8_t* c = &colours[season][k * 3];
        m.v.push_back({p[0] * 0.25f, p[1] * 0.25f, p[2] * 0.25f, c[0], c[1], c[2]});
    }
    return m;
}

// The ground patches (puddles and ice, snow piles and drifts, frost, fallen leaves, blossom), made in Blender (tools/scenery/build_ground.py,
// assets/scenery/ground.blend) and stored in shared/ground_model.h with their light baked into the corner colours. `variant` is the item: see
// shared/ground_patches.h for which is which. Each is about 100 units from the middle to the edge; the game stretches it to the patch it wants.
inline MeshData Ground(uint32_t variant) {
    namespace gm = ground_model;
    const int item = static_cast<int>(variant % gm::kItems);
    int first = 0;
    for (int i = 0; i < item; i++) first += gm::kCorners[i];
    MeshData m;
    m.v.reserve(static_cast<size_t>(gm::kCorners[item]));
    for (int k = first; k < first + gm::kCorners[item]; k++) {
        const int16_t* p = &gm::kPos[k * 3];
        const uint8_t* c = &gm::kCol[k * 3];
        m.v.push_back({p[0] * 0.25f, p[1] * 0.25f, p[2] * 0.25f, c[0], c[1], c[2], gm::kAlpha[k]});
    }
    return m;
}


// One ring of a raindrop's ripple: a thin flat band 40 across, bright at its crest and fading to the water's colour on both sides. The game
// layer grows and fades it out over a moment, a few at a time on every puddle while it rains.
inline MeshData Ripple(uint32_t) {
    MeshData m;
    const int n = 12;
    const float rad[3] = {15.0f, 18.0f, 20.0f};
    const Rgb col[3] = {{96, 150, 200}, {222, 236, 248}, {96, 150, 200}};
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


// ---- ground cover for the weather: puddles, fallen leaves, ash and sand drifts ----------------------------------------------------------------------------
// The same flat-shaded, chunky look as the rest (and the snow mounds above): low shapes that lie on the ground. They are made once and drawn many times by
// the game layer, which decides where and how much from how long the weather has lasted.

// A low dome, `rx` by `rz` across and `h` high, in coloured faces: colour(face number) picks each face's colour.
template <class F>
inline MeshData Mound(float rx, float rz, float h, uint32_t seed, F colour) {
    Builder b;
    b.inside = {0, -20, 0};
    Lcg rng(seed);
    const int n = 10;
    const float rad[3] = {1.0f, 0.72f, 0.38f}, hgt[3] = {0.0f, 0.55f, 0.9f};
    std::vector<V3> ring[3];
    for (int r = 0; r < 3; r++)
        for (int i = 0; i < n; i++) {
            const float a = static_cast<float>(i) / n * 6.2831853f;
            const float j = r == 0 ? 0.76f + rng.Next() * 0.48f : 0.88f + rng.Next() * 0.24f;
            ring[r].push_back({std::cos(a) * rx * rad[r] * j, h * hgt[r] + (r == 0 ? 0.0f : rng.Next() * h * 0.12f), std::sin(a) * rz * rad[r] * j});
        }
    int face = 0;
    for (int r = 0; r < 2; r++)
        for (int i = 0; i < n; i++) {
            const int k = (i + 1) % n;
            b.Tri(ring[r][i], ring[r][k], ring[r + 1][k], colour(face++));
            b.Tri(ring[r][i], ring[r + 1][k], ring[r + 1][i], colour(face++));
        }
    for (int i = 0; i < n; i++) b.Tri(ring[2][i], ring[2][(i + 1) % n], {0, h, 0}, colour(face++));
    return b.mesh;
}

// A heap of fallen leaves (red and orange, golden, brown, or a mix) in the autumn.
inline MeshData LeafPile(uint32_t variant) {
    static const Rgb pal[4][4] = {{{206, 66, 40}, {226, 120, 44}, {170, 50, 36}, {236, 150, 60}}, {{236, 196, 70}, {214, 160, 50}, {248, 220, 110}, {196, 140, 44}},
                                  {{140, 96, 52}, {110, 76, 44}, {170, 120, 66}, {96, 66, 40}}, {{206, 66, 40}, {236, 196, 70}, {140, 96, 52}, {120, 150, 60}}};
    Lcg pick(500 + variant);
    const Rgb* p = pal[variant % 4];
    return Mound(62.0f, 54.0f, 15.0f, 520 + variant, [&](int) { return p[static_cast<int>(pick.Next() * 3.99f)]; });
}

// A drift of ash (the ash weather): grey and black, with a few faces still glowing.
inline MeshData AshDrift(uint32_t variant) {
    Lcg pick(600 + variant);
    return Mound(74.0f, 64.0f, 13.0f, 620 + variant, [&](int) {
        const float r = pick.Next();
        if (r < 0.1f) return Rgb{230, 110, 40};
        if (r < 0.45f) return Rgb{58, 54, 56};
        return Rgb{112, 108, 110};
    });
}

// A ridge of sand (the sandstorm), long and low, lying across the wind.
inline MeshData SandDrift(uint32_t variant) {
    Lcg pick(700 + variant);
    return Mound(120.0f, 48.0f, 17.0f, 720 + variant, [&](int f) { return (f % 3 == 0) ? Rgb{196, 160, 100} : (pick.Next() < 0.5f ? Rgb{222, 190, 126} : Rgb{238, 212, 150}); });
}


// The Shockwave Grenade: a gunmetal ball, about the size of a bomb, with glowing purple bands around its middle and over the top, and a dark
// cap. The bands are raised rings (not paint) so they read as sharp lines, and they are not shaded, so they look lit from inside.
inline void GlowRing(MeshData& m, V3 c, float radius, float tube, int segs, bool vertical, float yaw) {
    const float pi = 3.14159265f;
    const Rgb face = {225, 150, 255}, edge = {150, 60, 230};
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    auto at = [&](int i, int k) {   // point k (0..2) of the tube's triangular cross-section at step i around the ring
        const float a = 2.0f * pi * static_cast<float>(i) / segs;
        const float t = 2.0f * pi * static_cast<float>(k) / 3.0f;
        const float r = radius + std::cos(t) * tube, h = std::sin(t) * tube;
        V3 q = vertical ? V3{std::cos(a) * r, std::sin(a) * r, h} : V3{std::cos(a) * r, h, std::sin(a) * r};
        return V3{c.x + q.x * cy + q.z * sy, c.y + q.y, c.z - q.x * sy + q.z * cy};
    };
    for (int i = 0; i < segs; i++)
        for (int k = 0; k < 3; k++) {
            const Rgb col = k == 0 ? face : edge;   // the outer side brightest
            const V3 a = at(i, k), b = at(i + 1, k), c2 = at(i + 1, (k + 1) % 3), d = at(i, (k + 1) % 3);
            Put(m, a, col); Put(m, b, col); Put(m, c2, col);
            Put(m, a, col); Put(m, c2, col); Put(m, d, col);
        }
}

inline MeshData Grenade() {
    constexpr float r = 14.0f;
    const V3 c = {0, r + 1.5f, 0};   // raised a little so the rings over the top clear the ground too
    MeshData m;
    Ellipsoid(m, c, {r, r, r}, 8, 14, [&](V3 n, V3) {
        if (n.y > 0.85f) return Rgb{70, 72, 82};                                  // the dark cap on top
        return n.y < -0.5f ? Rgb{140, 142, 152} : Rgb{176, 178, 188};            // gunmetal grey, darker underneath
    });
    GlowRing(m, c, r + 0.3f, 1.3f, 12, false, 0.0f);                             // around the middle
    GlowRing(m, c, r + 0.3f, 1.1f, 12, true, 0.0f);                              // over the top, crossing it
    GlowRing(m, c, r + 0.3f, 1.1f, 12, true, 1.5707963f);
    return m;
}

// The Gilded Sword (Blender model, see tools/gilded_sword): variant 0 is the sword (blade and hilt), variant 1 the hilt in its scabbard, for when it is
// stowed on the back. Its points are in the game's limb units (a hundredth of a game unit) with the grip at the origin and the blade along +X, so the
// game draws it in Link's hand matrix.
inline MeshData GildedSwordMesh(uint32_t variant) {
    MeshData m;
    auto put = [&](const gilded_sword_model::Tri* tris, int count) {
        for (int i = 0; i < count; i++)
            for (int k = 0; k < 3; k++)
                m.v.push_back({static_cast<float>(tris[i].p[k * 3]), static_cast<float>(tris[i].p[k * 3 + 1]), static_cast<float>(tris[i].p[k * 3 + 2]), tris[i].rgb[0], tris[i].rgb[1], tris[i].rgb[2]});
    };
    put(gilded_sword_model::kHilt, gilded_sword_model::kHiltCount);
    if (variant % 2 == 0) put(gilded_sword_model::kBlade, gilded_sword_model::kBladeCount);
    else put(gilded_sword_model::kScabbard, gilded_sword_model::kScabbardCount);
    return m;
}

} // namespace mesh_detail

inline MeshData BuildMesh(MeshKind kind, uint32_t variant) {
    switch (kind) {
        case MeshKind::Rock: return mesh_detail::Lump(34.0f, 0.78f, variant);
        case MeshKind::Boulder: return mesh_detail::Boulder(variant);
        case MeshKind::Pillar: return mesh_detail::Post(variant);
        case MeshKind::Roof: return mesh_detail::Roof(variant);
        case MeshKind::Golem: return mesh_detail::Golem(variant);
        case MeshKind::Glider: return mesh_detail::Glider(variant);
        case MeshKind::GliderFrame: return mesh_detail::Glider(0, false);
        case MeshKind::Sign: return mesh_detail::Sign();
        case MeshKind::Ally: return mesh_detail::Ally(variant);
        case MeshKind::Cat: return mesh_detail::Cat();
        case MeshKind::LeafPile: return mesh_detail::LeafPile(variant);
        case MeshKind::AshDrift: return mesh_detail::AshDrift(variant);
        case MeshKind::SandDrift: return mesh_detail::SandDrift(variant);
        case MeshKind::CatBody: return mesh_detail::CatBody();
        case MeshKind::CatHead: return mesh_detail::CatHead(variant);
        case MeshKind::CatTailSeg: return mesh_detail::CatTailSeg(variant);
        case MeshKind::CatLeg: return mesh_detail::CatLeg(variant);
        case MeshKind::Grass: return mesh_detail::Grass(variant);
        case MeshKind::Tree: return mesh_detail::Tree(variant);
        case MeshKind::Ripple: return mesh_detail::Ripple(variant);
        case MeshKind::Decor: return mesh_detail::Decor(variant);
        case MeshKind::Clutter: return mesh_detail::Clutter(variant);
        case MeshKind::ThemeTree: return mesh_detail::ThemeTree(variant);
        case MeshKind::Scenery: return mesh_detail::Scenery(variant);
        case MeshKind::Ground: return mesh_detail::Ground(variant);
        case MeshKind::Dragon: return mesh_detail::Dragon(variant);
        case MeshKind::Platform: return mesh_detail::Platform(variant);
        case MeshKind::Projectile: return mesh_detail::Projectile(variant);
        case MeshKind::Grenade: return mesh_detail::Grenade();
        case MeshKind::VictoryCrown: return mesh_detail::AuthoredMesh(playtest_models::crown,std::size(playtest_models::crown));
        case MeshKind::GildedSword: return mesh_detail::GildedSwordMesh(variant);
        default: return {};
    }
}

} // namespace royale
