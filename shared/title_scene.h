#pragma once
// The start screen: a low-poly island at dusk (Hyrule Castle on its hill, pines, glowing chests, players gliding down under striped canopies,
// Majora's Moon hanging in the sky and the purple storm wall on the horizon) behind the logo, which lands with a flash and burns with the
// orange title flames and rising embers of Ocarina of Time's own title screen. "PRESS START" pulses below it.
// Game-independent: it only emits coloured triangles, the logo's rectangle and centred text to a Canvas. The mod draws them with ImGui
// (features/StartScreen.inc); tools/title_scene/preview.cpp rasterises them to a picture for checking without the game.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "skin_model_types.h"

namespace royale::title {

constexpr float kLogoAt = 1.1f;          // seconds after the screen appears: the logo lands
constexpr float kPromptAt = 2.4f;        // "PRESS START" starts to show
constexpr float kOutroSeconds = 1.1f;    // from the press to the menu

// 0xRRGGBBAA
struct Canvas {
    virtual ~Canvas() = default;
    virtual void Tri(float x0, float y0, uint32_t c0, float x1, float y1, uint32_t c1, float x2, float y2, uint32_t c2) = 0;
    virtual void Logo(float x0, float y0, float x1, float y1, float alpha) = 0;
    // Centred on cx, top at y, `size` pixels tall, with a dark outline.
    virtual void Text(float cx, float y, float size, uint32_t rgba, const char* text) = 0;
};

namespace detail {
constexpr float kPi = 3.14159265f;
inline float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
inline float Smooth(float a, float b, float v) { v = Clamp01((v - a) / (b - a)); return v * v * (3 - 2 * v); }
inline float EaseOut(float v) { v = Clamp01(v); return 1 - (1 - v) * (1 - v) * (1 - v); }
inline float EaseOutBack(float v) { v = Clamp01(v); const float c = 1.9f; return 1 + (c + 1) * std::pow(v - 1, 3.0f) + c * std::pow(v - 1, 2.0f); }
inline float Hash(int n) { uint32_t x = static_cast<uint32_t>(n) * 0x9E3779B9u; x ^= x >> 15; x *= 0x85EBCA6Bu; x ^= x >> 13; return (x & 0xFFFFFF) / 16777215.0f; }
inline uint32_t Rgba(float r, float g, float b, float a) {
    auto c = [](float v) { return static_cast<uint32_t>(std::clamp(v, 0.0f, 255.0f) + 0.5f); };
    return c(r) << 24 | c(g) << 16 | c(b) << 8 | c(a);
}
inline uint32_t Alpha(uint32_t rgba, float k) { return (rgba & 0xFFFFFF00u) | static_cast<uint32_t>((rgba & 255) * Clamp01(k)); }
inline uint32_t Mix(uint32_t a, uint32_t b, float t) {
    t = Clamp01(t);
    auto ch = [&](int s) { return ((a >> s) & 255) * (1 - t) + ((b >> s) & 255) * t; };
    return Rgba(ch(24), ch(16), ch(8), ch(0));
}
inline uint32_t Shade(uint32_t rgba, float k) {
    return Rgba(((rgba >> 24) & 255) * k, ((rgba >> 16) & 255) * k, ((rgba >> 8) & 255) * k, rgba & 255);
}

struct V3 { float x, y, z; };
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline V3 Norm(V3 a) { const float l = std::sqrt(Dot(a, a)); return l > 1e-6f ? a * (1 / l) : V3{0, 1, 0}; }

// An affine transform (3 x 3 rotation-scale plus translation) for posing the hero's limbs.
struct Xf {
    float m[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
    V3 Point(V3 p) const { return {m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3], m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3], m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3]}; }
    V3 Dir(V3 p) const { return {m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z, m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z, m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z}; }
    Xf operator*(const Xf& b) const {
        Xf r;
        for (int i = 0; i < 3; i++) for (int j = 0; j < 4; j++)
            r.m[i][j] = m[i][0] * b.m[0][j] + m[i][1] * b.m[1][j] + m[i][2] * b.m[2][j] + (j == 3 ? m[i][3] : 0.0f);
        return r;
    }
    // A turn of `a` radians about the X (axis 0), Y (1) or Z (2) axis through `pivot`.
    static Xf Turn(int axis, float a, V3 pivot = {0, 0, 0}) {
        Xf r;
        const float c = std::cos(a), s = std::sin(a);
        const int i = (axis + 1) % 3, j = (axis + 2) % 3;
        r.m[i][i] = c; r.m[i][j] = -s; r.m[j][i] = s; r.m[j][j] = c;
        const float p[3] = {pivot.x, pivot.y, pivot.z};
        for (int k = 0; k < 3; k++) r.m[k][3] = p[k] - (r.m[k][0] * p[0] + r.m[k][1] * p[1] + r.m[k][2] * p[2]);
        return r;
    }
    static Xf Move(V3 d, float scale = 1) {
        Xf r;
        r.m[0][0] = r.m[1][1] = r.m[2][2] = scale;
        r.m[0][3] = d.x; r.m[1][3] = d.y; r.m[2][3] = d.z;
        return r;
    }
};

// A triangle waiting to be painted far to near.
struct Tri2 { float x[3], y[3]; uint32_t c[3]; float depth; };

struct Camera {
    V3 eye, right, up, fwd;
    float focal, cx, cy;
    // Screen position and depth; false behind the camera.
    bool Project(V3 p, float& sx, float& sy, float& depth) const {
        const V3 d = p - eye;
        depth = Dot(d, fwd);
        if (depth < 1.0f) return false;
        sx = cx + Dot(d, right) * focal / depth;
        sy = cy - Dot(d, up) * focal / depth;
        return true;
    }
};

// The island: a lumpy disc of land rising to a rocky peak, with the castle's flat-topped hill.
inline float RawHeight(float x, float z) {
    const float a = std::atan2(z, x);
    const float coast = 60 + 6 * std::sin(3 * a + 0.4f) + 4 * std::sin(5 * a + 1.3f) + 2.5f * std::sin(9 * a);
    const float r = std::sqrt(x * x + z * z) / coast;
    const float land = 1 - Smooth(0.72f, 1.04f, r);
    const float castleHill = 9 * std::exp(-((x - 16) * (x - 16) + (z + 8) * (z + 8)) / 260.0f);
    const float peak = 17 * std::exp(-((x + 26) * (x + 26) + (z + 24) * (z + 24)) / 230.0f);
    const float rolls = 2.2f * std::sin(x * 0.13f + 1.0f) * std::cos(z * 0.11f) + 1.2f * std::sin(x * 0.31f + z * 0.27f);
    return land * (2.5f + castleHill + peak + rolls * (1 - castleHill / 12)) - (1 - land) * 5;
}
constexpr V3 kCastle{16, 0, -8};
constexpr float kCastleRadius = 21;   // the plateau the castle and its moat stand on
constexpr V3 kMeadow{34, 0, 4};       // the open meadow before the gate, where the little hero runs
constexpr float kMeadowRadius = 12;
inline float Height(float x, float z) {
    static const float plateau = RawHeight(kCastle.x, kCastle.z);
    const float d = std::sqrt((x - kCastle.x) * (x - kCastle.x) + (z - kCastle.z) * (z - kCastle.z));
    const float k = 1 - Smooth(kCastleRadius - 2, kCastleRadius + 9, d);
    return RawHeight(x, z) * (1 - k) + plateau * k;
}
} // namespace detail

class Scene {
  public:
    // t: seconds since the screen appeared. outro: seconds since start was pressed, or < 0. reduced: no camera swoop and no flashes.
    void Draw(Canvas& out, float W, float H, float t, float outro, bool reduced, float logoAspect, const std::string& version) {
        using namespace detail;
        if (W <= 0 || H <= 0) return;
        tris_.clear();
        const float k = reduced ? 1.0f : EaseOut(t / 3.4f);
        const float yaw = 0.95f + (reduced ? 0.0f : t * 0.035f + (1 - k) * 0.7f);
        const float dist = 175 - 63 * k, height = 92 - 50 * k;
        cam_.eye = {std::sin(yaw) * dist, height, std::cos(yaw) * dist};
        const V3 target{0, 6 + (1 - k) * 10, 0};
        cam_.fwd = Norm(target - cam_.eye);
        cam_.right = Norm(Cross(cam_.fwd, {0, 1, 0}));
        cam_.up = Cross(cam_.right, cam_.fwd);
        cam_.focal = H / (2 * std::tan(25 * kPi / 180));
        cam_.cx = W * 0.5f;
        cam_.cy = H * 0.74f;   // the island sits low, under the logo
        W_ = W; H_ = H; t_ = t;
        // the sun sits low on the horizon a little left of the view; the island is lit by a key light from behind the camera's shoulder
        const float fa = std::atan2(cam_.fwd.z, cam_.fwd.x) - 0.42f;
        sun_ = Norm({std::cos(fa), 0.035f, std::sin(fa)});
        light_ = Norm({0.91f, 0.75f, -0.41f});
        // flashes from the storm's lightning and from the logo landing
        const float bolt = reduced ? 0.0f : Lightning(t);
        Sky(out, W, H, t, bolt);
        Clouds(t);
        Mountains();
        Storm(t, bolt);
        Island();
        Castle(t);
        Trees();
        Chests(t);
        Runner(t);
        Moon(t);
        Gliders(t);
        Fairies(t);
        Flush(out);
        Vignette(out, W, H);
        LogoAndFlames(out, W, H, t, outro, reduced, logoAspect);
        Prompt(out, W, H, t, outro, reduced);
        out.Text(W - H * 0.11f, H - H * 0.045f, H * 0.022f, 0xF4E9CAB0, version.c_str());
        // the fade in from black, the white flash as the logo lands and, after start, the white flash and the fade to black
        if (!reduced && t < kLogoAt + 0.6f) {
            const float flash = t > kLogoAt ? (1 - Clamp01((t - kLogoAt) / 0.55f)) * 0.75f : 0;
            if (flash > 0) Rect(out, 0, 0, W, H, Rgba(255, 246, 220, 255 * flash));
        }
        if (t < 1.0f) Rect(out, 0, 0, W, H, Rgba(0, 0, 0, 255 * (1 - EaseOut(t / 1.0f))));
        if (outro >= 0) {
            if (!reduced && outro < 0.4f) Rect(out, 0, 0, W, H, Rgba(255, 250, 235, 200 * (1 - outro / 0.4f)));
            Rect(out, 0, 0, W, H, Rgba(0, 0, 0, 255 * Smooth(0.35f, kOutroSeconds - 0.05f, outro)));
        }
    }

    // Logo placement, for callers that want to put something relative to it.
    static void LogoRect(float W, float H, float aspect, float& x0, float& y0, float& x1, float& y1) {
        const float w = std::min(W * 0.66f, H * 0.47f * aspect), h = w / aspect;
        x0 = (W - w) * 0.5f; x1 = x0 + w; y0 = H * 0.04f; y1 = y0 + h;
    }

    // The player's own body (a skin model: shared/skin_model_types.h) for the miniature hero running laps of the meadow, with the Hylian
    // Shield and the Master Sword on his back. Null draws a small flat stand-in instead.
    void SetHero(const skinmodel::Model* model) {
        if (model == hero_) return;
        hero_ = model;
        heroColours_.clear();
    }

  private:
    using V3 = detail::V3;
    const skinmodel::Model* hero_ = nullptr;
    std::vector<uint32_t> heroColours_;   // one per vertex, read from its texture once
    struct HeroVert { V3 p; float sx, sy, d; uint32_t c; bool ok; };
    std::vector<HeroVert> heroVerts_;
    detail::Camera cam_{};
    V3 sun_{}, light_{};
    std::vector<detail::Tri2> tris_;
    float horizon_ = 0, W_ = 0, H_ = 0, t_ = 0;

    // ---- primitives ----
    static void Rect(Canvas& out, float x0, float y0, float x1, float y1, uint32_t c) { Grad(out, x0, y0, x1, y1, c, c); }
    static void Grad(Canvas& out, float x0, float y0, float x1, float y1, uint32_t top, uint32_t bottom) {
        out.Tri(x0, y0, top, x1, y0, top, x1, y1, bottom);
        out.Tri(x0, y0, top, x1, y1, bottom, x0, y1, bottom);
    }
    // A soft disc: `inner` at the centre fading to `outer` at the rim.
    static void Glow(Canvas& out, float x, float y, float rx, float ry, uint32_t inner, uint32_t outer, int n = 24) {
        for (int i = 0; i < n; i++) {
            const float a0 = i * 2 * detail::kPi / n, a1 = (i + 1) * 2 * detail::kPi / n;
            out.Tri(x, y, inner, x + std::cos(a0) * rx, y + std::sin(a0) * ry, outer, x + std::cos(a1) * rx, y + std::sin(a1) * ry, outer);
        }
    }
    void Push(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c0, uint32_t c1, uint32_t c2, float depth) {
        tris_.push_back({{x0, x1, x2}, {y0, y1, y2}, {c0, c1, c2}, depth});
    }
    // A world triangle, flat shaded by the low sun and hazed with distance.
    // With `centre` (a closed solid) faces turned away from the camera are skipped; without it the face is lit from whichever side shows.
    void Face(V3 a, V3 b, V3 c, uint32_t base, float bias = 0, bool lit = true, const V3* centre = nullptr) {
        using namespace detail;
        V3 n = Norm(Cross(b - a, c - a));
        const V3 mid = (a + b + c) * (1.0f / 3);
        if (centre != nullptr) {
            if (Dot(n, mid - *centre) < 0) n = n * -1;
            if (Dot(n, cam_.eye - mid) <= 0) return;
        } else if (Dot(n, cam_.eye - mid) < 0) {
            n = n * -1;
        }
        float ax, ay, ad, bx, by, bd, cx, cy, cd;
        if (!cam_.Project(a, ax, ay, ad) || !cam_.Project(b, bx, by, bd) || !cam_.Project(c, cx, cy, cd)) return;
        uint32_t col = base;
        if (lit) {
            const float d = std::max(0.0f, Dot(n, light_));
            const float k = 0.42f + 0.85f * d;
            col = Rgba(((base >> 24) & 255) * k * 1.08f + 14 * d, ((base >> 16) & 255) * k * 0.98f + 6 * d, ((base >> 8) & 255) * k * 0.92f, base & 255);
        }
        const float depth = (ad + bd + cd) / 3;
        col = Mix(col, Rgba(196, 128, 132, base & 255), Smooth(90, 420, depth) * 0.75f);
        Push(ax, ay, bx, by, cx, cy, col, col, col, depth + bias);
    }
    void Quad(V3 a, V3 b, V3 c, V3 d, uint32_t col, float bias = 0, const V3* centre = nullptr) {
        Face(a, b, c, col, bias, true, centre);
        Face(a, c, d, col, bias, true, centre);
    }
    // An upright prism (n sides) from y0 to y1, with an optional cone on top.
    void Prism(V3 base, float r, float y0, float y1, int n, uint32_t wall, float roofH = 0, uint32_t roof = 0) {
        const V3 centre{base.x, (y0 + y1) * 0.5f, base.z}, roofCentre{base.x, y1 + roofH * 0.3f, base.z};
        for (int i = 0; i < n; i++) {
            const float a0 = i * 2 * detail::kPi / n, a1 = (i + 1) * 2 * detail::kPi / n;
            const V3 p0{base.x + std::cos(a0) * r, 0, base.z + std::sin(a0) * r}, p1{base.x + std::cos(a1) * r, 0, base.z + std::sin(a1) * r};
            Quad({p0.x, y0, p0.z}, {p1.x, y0, p1.z}, {p1.x, y1, p1.z}, {p0.x, y1, p0.z}, wall, 0, &centre);
            if (roofH > 0) Face({p0.x, y1, p0.z}, {p1.x, y1, p1.z}, {base.x, y1 + roofH, base.z}, roof, 0, true, &roofCentre);
            else Face({p1.x, y1, p1.z}, {p0.x, y1, p0.z}, {base.x, y1, base.z}, wall, 0, true, &centre);
        }
    }
    void Box(V3 c, float sx, float sy, float sz, uint32_t col, float bias = 0) {
        const float x0 = c.x - sx, x1 = c.x + sx, y0 = c.y, y1 = c.y + sy * 2, z0 = c.z - sz, z1 = c.z + sz;
        const V3 m{c.x, c.y + sy, c.z};
        Quad({x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, col, bias, &m);
        Quad({x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, col, bias, &m);
        Quad({x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, col, bias, &m);
        Quad({x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, col, bias, &m);
        Quad({x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}, col, bias, &m);
    }
    // A screen-facing sprite part: a soft disc at a world point, sized in world units.
    void Billboard(V3 p, float radius, uint32_t inner, uint32_t outer, float bias = 0, int n = 14) {
        float x, y, d;
        if (!cam_.Project(p, x, y, d)) return;
        const float r = radius * cam_.focal / d;
        for (int i = 0; i < n; i++) {
            const float a0 = i * 2 * detail::kPi / n, a1 = (i + 1) * 2 * detail::kPi / n;
            Push(x, y, x + std::cos(a0) * r, y + std::sin(a0) * r, x + std::cos(a1) * r, y + std::sin(a1) * r, inner, outer, outer, d + bias);
        }
    }
    void Flush(Canvas& out) {
        std::stable_sort(tris_.begin(), tris_.end(), [](const detail::Tri2& a, const detail::Tri2& b) { return a.depth > b.depth; });
        for (const auto& t : tris_) out.Tri(t.x[0], t.y[0], t.c[0], t.x[1], t.y[1], t.c[1], t.x[2], t.y[2], t.c[2]);
        tris_.clear();
    }

    // ---- the world ----
    static float Lightning(float t) {
        const float cycle = std::fmod(t + 1.3f, 4.6f);
        if (cycle > 0.35f) return 0;
        return (cycle < 0.08f || (cycle > 0.16f && cycle < 0.22f)) ? 1.0f : 0.35f * (1 - cycle / 0.35f);
    }
    void Sky(Canvas& out, float W, float H, float t, float bolt) {
        using namespace detail;
        // the horizon: where a point far out at eye height lands
        const V3 flat = Norm({cam_.fwd.x, 0, cam_.fwd.z});
        float hx, hy, hd;
        horizon_ = cam_.Project(cam_.eye + flat * 5000, hx, hy, hd) ? hy : H * 0.5f;
        const float hz = std::clamp(horizon_, -H, H * 2);
        const uint32_t top = Mix(Rgba(14, 18, 52, 255), Rgba(60, 44, 110, 255), bolt * 0.5f);
        const uint32_t mid = Rgba(96, 58, 128, 255), low = Rgba(236, 120, 92, 255), rim = Rgba(255, 196, 120, 255);
        Grad(out, 0, std::min(0.0f, hz - H), W, hz - H * 0.45f, top, top);
        Grad(out, 0, hz - H * 0.45f, W, hz - H * 0.16f, top, mid);
        Grad(out, 0, hz - H * 0.16f, W, hz - H * 0.03f, mid, low);
        Grad(out, 0, hz - H * 0.03f, W, hz, low, rim);
        // stars in the dark top of the sky
        for (int i = 0; i < 70; i++) {
            const float sx = std::fmod(Hash(i) * W * 3 - std::fmod(t * 4, W * 3) + W * 3, W * 3) - W;
            const float sy = Hash(i + 500) * (hz - H * 0.25f);
            if (sx < 0 || sx > W || sy < 0) continue;
            const float tw = 0.5f + 0.5f * std::sin(t * (1.3f + Hash(i + 9) * 2) + i);
            const float r = (0.6f + Hash(i + 77) * 1.2f) * H / 720;
            Glow(out, sx, sy, r * 2.4f, r * 2.4f, Rgba(255, 244, 214, 200 * tw), Rgba(255, 244, 214, 0), 6);
        }
        // the sun, sinking into the sea behind the island
        float sx = 0, sy = 0, sd = 0;
        if (cam_.Project(cam_.eye + sun_ * 5000, sx, sy, sd)) {
            Glow(out, sx, sy, H * 0.42f, H * 0.20f, Rgba(255, 170, 90, 120), Rgba(255, 120, 80, 0), 32);
            Glow(out, sx, sy, H * 0.07f, H * 0.07f, Rgba(255, 246, 200, 255), Rgba(255, 200, 120, 200), 24);
        }
        // the sea: the sunset reflected near the horizon, deep teal below
        const uint32_t seaTop = Rgba(198, 112, 112, 255), seaMid = Rgba(52, 72, 112, 255), seaLow = Rgba(12, 40, 66, 255);
        Grad(out, 0, hz, W, hz + H * 0.08f, seaTop, seaMid);
        Grad(out, 0, hz + H * 0.08f, W, std::max(H, hz + H * 0.08f), seaMid, seaLow);
        if (sd > 0) for (int i = 0; i < 26; i++) {   // the sun's glitter path on the water
            const float y = hz + 3 + std::pow(Hash(i + 40), 1.6f) * H * 0.3f;
            const float w = (6 + Hash(i + 3) * 26) * H / 720 * (1 + (y - hz) / (H * 0.12f));
            const float x = sx + (Hash(i + 90) - 0.5f) * (y - hz) * 0.9f + std::sin(t * 1.7f + i) * 4;
            const float a = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(t * 3.1f + i * 2.1f));
            Rect(out, x - w, y, x + w, y + 1.6f * H / 720, Rgba(255, 214, 160, 150 * a));
        }
    }
    void Clouds(float t) {
        using namespace detail;
        for (int i = 0; i < 9; i++) {
            const float a = i * 0.7f + t * 0.006f, r = 520 + Hash(i) * 120, y = 70 + Hash(i + 20) * 90;
            const V3 c{std::cos(a) * r, y, std::sin(a) * r};
            for (int j = 0; j < 4; j++) {
                const V3 p = c + V3{(j - 1.5f) * 22, (Hash(i * 7 + j) - 0.5f) * 8, 0};
                Billboard(p, 22 + Hash(i * 3 + j) * 12, Rgba(250, 170, 150, 110), Rgba(160, 90, 140, 0), 2000, 16);
            }
        }
    }
    void Mountains() {
        using namespace detail;
        for (int i = 0; i < 22; i++) {   // far peaks, all round the sea
            const float a = i * 2 * kPi / 22 + Hash(i) * 0.2f, r = 640 + Hash(i + 3) * 120;
            const float h = 14 + Hash(i + 6) * 22, w = 50 + Hash(i + 9) * 60;
            const V3 c{std::cos(a) * r, 0, std::sin(a) * r}, side = Norm({-std::sin(a), 0, std::cos(a)});
            const uint32_t col = Rgba(74, 52, 98, 255);
            Face(c + side * w, c - side * w, c + V3{0, h, 0}, col, 0, false);
            Face(c - side * w, c + side * w, c + V3{0, h, 0}, col, 0, false);
        }
        // Death Mountain, with its ring of smoke and a red glow at the crater
        const V3 dm{-420, 0, -360};
        const V3 side = Norm(Cross(dm - cam_.eye, {0, 1, 0}));
        const uint32_t rock = Rgba(92, 56, 72, 255);
        Face(dm + side * 120, dm - side * 120, dm + V3{0, 95, 0}, rock, 0, false);
        Face(dm - side * 120, dm + side * 120, dm + V3{0, 95, 0}, rock, 0, false);
        Billboard(dm + V3{0, 96, 0}, 14, Rgba(255, 110, 60, 200), Rgba(255, 60, 40, 0), -1);
        for (int i = 0; i < 10; i++) {
            const float a = i * 2 * kPi / 10;
            Billboard(dm + V3{std::cos(a) * 34, 108 + std::sin(a * 2) * 4, std::sin(a) * 16}, 18, Rgba(230, 170, 170, 150), Rgba(160, 110, 140, 0), -2);
        }
    }
    void Storm(float t, float bolt) {
        using namespace detail;
        const float R = 420, top = 120;
        const int n = 64;
        for (int i = 0; i < n; i++) {
            const float a0 = i * 2 * kPi / n, a1 = (i + 1) * 2 * kPi / n;
            const V3 p0{std::cos(a0) * R, -2, std::sin(a0) * R}, p1{std::cos(a1) * R, -2, std::sin(a1) * R};
            float x0, y0, d0, x1, y1, d1, x2, y2, d2, x3, y3, d3;
            if (!cam_.Project(p0, x0, y0, d0) || !cam_.Project(p1, x1, y1, d1) || !cam_.Project(p0 + V3{0, top, 0}, x2, y2, d2) ||
                !cam_.Project(p1 + V3{0, top, 0}, x3, y3, d3)) continue;
            if (d0 < 260 || d1 < 260) continue;   // only the far wall, never a sheet across the camera
            const float swirl = 0.5f + 0.5f * std::sin(a0 * 9 + t * 1.4f) * std::sin(a0 * 4 - t * 0.6f);
            const uint32_t base = Rgba(150 + 60 * bolt, 70 + 50 * bolt, 220, 120 + 60 * swirl);
            const uint32_t high = Rgba(110, 50, 190, 0);
            const float d = (d0 + d1) / 2;
            Push(x0, y0, x1, y1, x3, y3, base, base, high, d);
            Push(x0, y0, x3, y3, x2, y2, base, high, high, d);
            // the bright edge where the storm meets the sea
            Push(x0, y0, x1, y1, x1, y1 - 3, Rgba(230, 180, 255, 200), Rgba(230, 180, 255, 200), Rgba(230, 180, 255, 0), d - 0.1f);
        }
        if (bolt <= 0.3f) return;   // a lightning bolt on the far wall
        const int strike = static_cast<int>((t + 1.3f) / 4.6f);
        const float a = cam_.eye.x == 0 && cam_.eye.z == 0 ? 0 : std::atan2(-cam_.eye.z, -cam_.eye.x) + (Hash(strike) - 0.5f) * 1.1f;
        V3 p{std::cos(a) * (R - 4), top * 0.9f, std::sin(a) * (R - 4)};
        for (int s = 0; s < 9; s++) {
            const V3 q = p + V3{(Hash(strike * 31 + s) - 0.5f) * 18, -top * 0.1f, (Hash(strike * 17 + s) - 0.5f) * 18};
            float ax, ay, ad, bx, by, bd;
            if (cam_.Project(p, ax, ay, ad) && cam_.Project(q, bx, by, bd)) {
                const float w = 2.2f * cam_.cy / 360;
                const uint32_t c = Rgba(250, 236, 255, 255 * bolt);
                Push(ax - w, ay, ax + w, ay, bx + w, by, c, c, c, ad - 1);
                Push(ax - w, ay, bx + w, by, bx - w, by, c, c, c, ad - 1);
            }
            p = q;
        }
    }
    // Ground colour: wet sand, beach, two greens of meadow, then rock and a little snow on the peak; small hashed variation like a texture.
    static uint32_t Ground(float h, float x, float z) {
        using namespace detail;
        const float v = (Hash(static_cast<int>(std::floor(x * 0.7f)) * 131 + static_cast<int>(std::floor(z * 0.7f)) * 17) - 0.5f) * 0.10f;
        const float patch = 0.5f + 0.5f * std::sin(x * 0.21f + std::sin(z * 0.17f) * 2) * std::cos(z * 0.19f - x * 0.05f);
        const uint32_t sand = Rgba(232, 210, 152, 255), wet = Rgba(176, 160, 120, 255);
        const uint32_t meadow = Mix(Rgba(122, 190, 78, 255), Rgba(96, 168, 70, 255), patch + v);
        const uint32_t deep = Mix(Rgba(72, 140, 64, 255), Rgba(60, 120, 58, 255), patch);
        const uint32_t rock = Mix(Rgba(150, 136, 124, 255), Rgba(124, 112, 104, 255), patch), snow = Rgba(244, 244, 250, 255);
        if (h < 0.4f) return Mix(wet, sand, (h + 1.2f) / 1.6f);
        if (h < 1.6f) return Mix(sand, meadow, Smooth(0.5f, 1.6f, h));
        if (h < 12.5f) return Mix(meadow, deep, Smooth(5, 12, h) + v);
        if (h < 17.5f) return Mix(deep, rock, Smooth(12.5f, 15, h));
        return Mix(rock, snow, Smooth(17.5f, 19, h));
    }
    // A world point lit by the key light with its own normal (smooth shading), then hazed with distance.
    uint32_t Lit(uint32_t base, V3 p, V3 n) const {
        using namespace detail;
        const float d = std::max(0.0f, Dot(n, light_)), rim = std::pow(std::max(0.0f, 1 - Dot(n, Norm(cam_.eye - p))), 3.0f);
        const float k = 0.48f + 0.78f * d;
        uint32_t col = Rgba(((base >> 24) & 255) * k * 1.06f + 12 * d + 40 * rim, ((base >> 16) & 255) * k + 6 * d + 22 * rim,
                            ((base >> 8) & 255) * k * 0.94f + 18 * rim, base & 255);
        return Mix(col, Rgba(196, 128, 132, base & 255), Smooth(90, 420, Dot(p - cam_.eye, cam_.fwd)) * 0.75f);
    }
    void Island() {
        using namespace detail;
        // A fine grid with a normal and a colour at every corner, so slopes shade smoothly instead of in facets.
        constexpr int N = 66;
        constexpr float S = 156.0f / N, O = -78;
        struct Corner { V3 p; uint32_t c; float sx, sy, d; bool ok; float h; };
        static std::vector<Corner> grid;
        grid.resize((N + 1) * (N + 1));
        for (int i = 0; i <= N; i++) for (int j = 0; j <= N; j++) {
            const float x = O + i * S, z = O + j * S, h = Height(x, z);
            const float e = 0.8f;
            const V3 n = Norm({Height(x - e, z) - Height(x + e, z), 2 * e, Height(x, z - e) - Height(x, z + e)});
            Corner& c = grid[i * (N + 1) + j];
            c.h = h;
            c.p = {x, std::max(h, -1.4f), z};
            c.c = Lit(Ground(h, x, z), c.p, n);
            if (h < 0) {   // shallow lagoon water over the sand, melting into the sea
                c.c = Mix(c.c, Rgba(64, 150, 158, 255), Smooth(0, -1.0f, h) * 0.85f);
                c.c = Alpha(c.c, 1 - Smooth(-0.6f, -1.35f, h));
            }
            c.ok = cam_.Project(c.p, c.sx, c.sy, c.d);
        }
        for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) {
            const Corner& a = grid[i * (N + 1) + j]; const Corner& b = grid[(i + 1) * (N + 1) + j];
            const Corner& c = grid[(i + 1) * (N + 1) + j + 1]; const Corner& d = grid[i * (N + 1) + j + 1];
            if (!a.ok || !b.ok || !c.ok || !d.ok) continue;
            if (std::max(std::max(a.h, b.h), std::max(c.h, d.h)) < -1.3f) continue;
            Push(a.sx, a.sy, d.sx, d.sy, c.sx, c.sy, a.c, d.c, c.c, (a.d + d.d + c.d) / 3);
            Push(a.sx, a.sy, c.sx, c.sy, b.sx, b.sy, a.c, c.c, b.c, (a.d + c.d + b.d) / 3);
        }
        // foam where the waves meet the beach, breathing in and out
        for (int i = 0; i < 120; i++) {
            const float a = i * 2 * kPi / 120;
            float lo = 0, hi = 80;
            for (int s = 0; s < 18; s++) { const float m = (lo + hi) / 2; (Height(std::cos(a) * m, std::sin(a) * m) > -0.6f ? lo : hi) = m; }
            const float r = lo + 0.6f + 0.6f * std::sin(t_ * 1.2f + i * 0.7f);
            Billboard({std::cos(a) * r, -0.3f, std::sin(a) * r}, 1.9f, Rgba(250, 248, 240, 110), Rgba(250, 248, 240, 0), 30, 8);
        }
    }
    // An upright block turned by `yaw` about its base centre.
    void Block(V3 base, float yaw, float sx, float sy, float sz, uint32_t col) {
        const float c = std::cos(yaw), s = std::sin(yaw);
        auto P = [&](float x, float y, float z) { return V3{base.x + x * c - z * s, base.y + y, base.z + x * s + z * c}; };
        const V3 m = P(0, sy, 0);
        const V3 p000 = P(-sx, 0, -sz), p100 = P(sx, 0, -sz), p110 = P(sx, 2 * sy, -sz), p010 = P(-sx, 2 * sy, -sz);
        const V3 p001 = P(-sx, 0, sz), p101 = P(sx, 0, sz), p111 = P(sx, 2 * sy, sz), p011 = P(-sx, 2 * sy, sz);
        Quad(p001, p101, p111, p011, col, 0, &m); Quad(p100, p000, p010, p110, col, 0, &m);
        Quad(p101, p100, p110, p111, col, 0, &m); Quad(p000, p001, p011, p010, col, 0, &m);
        Quad(p011, p111, p110, p010, col, 0, &m);
    }
    // A pitched roof (a triangular prism) over a block's footprint.
    void Gable(V3 base, float yaw, float sx, float sz, float rise, uint32_t col) {
        const float c = std::cos(yaw), s = std::sin(yaw);
        auto P = [&](float x, float y, float z) { return V3{base.x + x * c - z * s, base.y + y, base.z + x * s + z * c}; };
        const V3 m = P(0, rise * 0.3f, 0);
        Quad(P(-sx, 0, sz), P(sx, 0, sz), P(sx, rise, 0), P(-sx, rise, 0), col, 0, &m);
        Quad(P(sx, 0, -sz), P(-sx, 0, -sz), P(-sx, rise, 0), P(sx, rise, 0), col, 0, &m);
        Face(P(sx, 0, sz), P(sx, 0, -sz), P(sx, rise, 0), detail::Shade(col, 0.85f), 0, true, &m);
        Face(P(-sx, 0, -sz), P(-sx, 0, sz), P(-sx, rise, 0), detail::Shade(col, 0.85f), 0, true, &m);
    }
    void Banner(V3 pole, float t, float size, int seed) {
        using namespace detail;
        const float flap = std::sin(t * 6 + seed) * 0.3f * size;
        const uint32_t red = Rgba(214, 50, 46, 255);
        Face(pole, pole + V3{2.2f * size, -0.45f * size + flap, 0.6f * size}, pole + V3{0, -1.2f * size, 0}, red, 0, false);
        Face(pole + V3{2.2f * size, -0.45f * size + flap, 0.6f * size}, pole, pole + V3{0, -1.2f * size, 0}, red, 0, false);
    }
    // Hyrule Castle on its plateau: a moat, a ring of battlemented walls with round towers, a gatehouse and drawbridge facing the meadow,
    // the great hall and the tall keep with its spires, banners flying and windows lit for the evening.
    void Castle(float t) {
        using namespace detail;
        const float g = Height(kCastle.x, kCastle.z) - 0.2f;
        const V3 C{kCastle.x, g, kCastle.z};
        const uint32_t stone = Rgba(222, 212, 190, 255), dark = Rgba(168, 156, 140, 255), roof = Rgba(64, 98, 156, 255);
        const uint32_t trim = Rgba(232, 186, 74, 255), water = Rgba(52, 118, 150, 255);
        const float gate = 0.75f;   // the side that faces the camera at the start
        // the moat: a ring of water just inside the plateau's edge
        const int ring = 40;
        for (int i = 0; i < ring; i++) {
            const float a0 = i * 2 * kPi / ring, a1 = (i + 1) * 2 * kPi / ring, r0 = 15.5f, r1 = 18.5f;
            const V3 p0{C.x + std::cos(a0) * r0, g - 0.25f, C.z + std::sin(a0) * r0}, p1{C.x + std::cos(a1) * r0, g - 0.25f, C.z + std::sin(a1) * r0};
            const V3 q0{C.x + std::cos(a0) * r1, g - 0.25f, C.z + std::sin(a0) * r1}, q1{C.x + std::cos(a1) * r1, g - 0.25f, C.z + std::sin(a1) * r1};
            const uint32_t w = Mix(water, Rgba(150, 190, 210, 255), 0.25f + 0.25f * std::sin(t * 2 + i * 0.9f));
            Face(p0, q0, q1, w, 0.5f, false); Face(p0, q1, p1, w, 0.5f, false);
        }
        // the curtain wall: eight runs with battlements, a round tower at every corner
        const int sides = 8;
        const float R = 13.5f;
        for (int i = 0; i < sides; i++) {
            const float a0 = gate + (i + 0.5f) * 2 * kPi / sides, a1 = gate + (i + 1.5f) * 2 * kPi / sides;
            const V3 p0{C.x + std::cos(a0) * R, g, C.z + std::sin(a0) * R}, p1{C.x + std::cos(a1) * R, g, C.z + std::sin(a1) * R};
            const V3 mid = (p0 + p1) * 0.5f;
            const float len = std::sqrt(Dot(p1 - p0, p1 - p0)), yaw = std::atan2(p1.z - p0.z, p1.x - p0.x);
            Block(mid, yaw, len * 0.5f, 2.6f, 0.8f, dark);
            for (int m = 0; m < 5; m++) {   // merlons
                const float u = (m + 0.5f) / 5 - 0.5f;
                Block(mid + V3{std::cos(yaw) * u * len, 5.2f, std::sin(yaw) * u * len}, yaw, len * 0.055f, 0.45f, 0.85f, dark);
            }
            Prism({p1.x, 0, p1.z}, 1.9f, g, g + 8.5f, 8, stone, 4.5f, roof);
            Banner({p1.x, g + 13.0f, p1.z}, t, 1.0f, i);
        }
        // the gatehouse: two tall towers, a dark arch and the drawbridge over the moat
        const V3 gd{std::cos(gate), 0, std::sin(gate)}, gs{-std::sin(gate), 0, std::cos(gate)};
        for (int s = -1; s <= 1; s += 2) Prism({C.x + gd.x * R + gs.x * s * 3.2f, 0, C.z + gd.z * R + gs.z * s * 3.2f}, 2.3f, g, g + 11, 8, stone, 5.2f, roof);
        Block({C.x + gd.x * R, g + 3.6f, C.z + gd.z * R}, gate + kPi / 2, 2.6f, 1.6f, 1.0f, stone);
        const V3 archC = C + gd * (R + 0.9f);
        Face(archC + gs * 2.0f, archC - gs * 2.0f, archC - gs * 2.0f + V3{0, 3.6f, 0}, Rgba(40, 30, 30, 255), -0.3f, false);
        Face(archC + gs * 2.0f, archC - gs * 2.0f + V3{0, 3.6f, 0}, archC + gs * 2.0f + V3{0, 3.6f, 0}, Rgba(40, 30, 30, 255), -0.3f, false);
        const V3 b0 = C + gd * (R + 1.0f), b1 = C + gd * 19.5f;
        const uint32_t wood = Rgba(132, 92, 56, 255);
        Face(b0 + gs * 1.8f + V3{0, 0.1f, 0}, b1 + gs * 1.8f, b1 - gs * 1.8f, wood, 0.3f);
        Face(b0 + gs * 1.8f + V3{0, 0.1f, 0}, b1 - gs * 1.8f, b0 - gs * 1.8f + V3{0, 0.1f, 0}, wood, 0.3f);
        // the great hall with its blue roof, two side towers and the keep
        Block(C + gd * 2.0f, gate + kPi / 2, 6.5f, 3.6f, 4.0f, stone);
        Gable(C + gd * 2.0f + V3{0, 7.2f, 0}, gate + kPi / 2, 6.8f, 4.3f, 4.2f, roof);
        Block(C + gd * 2.0f + V3{0, 7.2f, 0} + gs * 6.6f, gate, 0.6f, 0.3f, 4.3f, trim);
        for (int s = -1; s <= 1; s += 2) {
            const V3 tw = C + gs * (s * 7.5f) - gd * 1.0f;
            Prism({tw.x, 0, tw.z}, 2.4f, g, g + 15, 8, stone, 0, 0);
            Prism({tw.x, 0, tw.z}, 2.8f, g + 15, g + 16, 8, dark, 6.5f, roof);
            Banner({tw.x, g + 23.5f, tw.z}, t, 1.2f, 10 + s);
        }
        const V3 keep = C - gd * 3.5f;
        Prism({keep.x, 0, keep.z}, 4.2f, g, g + 20, 8, stone, 0, 0);
        Prism({keep.x, 0, keep.z}, 4.8f, g + 20, g + 21.5f, 8, dark, 0, 0);
        Prism({keep.x, 0, keep.z}, 3.0f, g + 21.5f, g + 27, 8, stone, 10.0f, roof);
        Prism({keep.x, 0, keep.z}, 0.25f, g + 37, g + 38.5f, 4, trim, 0.8f, trim);
        Banner({keep.x, g + 41, keep.z}, t, 1.8f, 20);
        // windows lit for the evening
        for (int i = 0; i < 6; i++) {
            const float a = gate + (i - 2.5f) * 0.32f;
            Billboard({keep.x + std::cos(a) * 4.3f, g + 9 + (i % 3) * 3.6f, keep.z + std::sin(a) * 4.3f}, 0.42f, Rgba(255, 224, 150, 255), Rgba(255, 170, 80, 110), -0.3f, 6);
        }
        for (int i = 0; i < 5; i++) {
            const V3 w = C + gd * 6.6f + gs * ((i - 2) * 2.3f) + V3{0, 4.2f, 0};
            Billboard(w, 0.4f, Rgba(255, 224, 150, 255), Rgba(255, 170, 80, 110), -0.3f, 6);
        }
    }
    // The hero's lap of the meadow before the castle gate: where he is and which way he runs.
    void RunnerPath(float t, V3& at, V3& ahead) const {
        using namespace detail;
        auto path = [&](float ang) {
            const float r = kMeadowRadius - 2 + 1.5f * std::sin(ang * 2.0f);
            return V3{kMeadow.x + std::cos(ang) * r, 0, kMeadow.z + std::sin(ang) * r * 0.8f};
        };
        const float a = t * 0.55f + 2.2f;
        at = path(a);
        ahead = path(a + 0.02f);
        at.y = Height(at.x, at.z);
        ahead.y = Height(ahead.x, ahead.z);
    }
    // The player's model, posed in a run: thighs and arms swinging in opposition, knees and elbows bending, the body bobbing and leaning
    // into the run, the cap's tail flicking. Each vertex follows its one or two limbs; its colour is its own texel, lit smoothly.
    void Hero(float t) {
        using namespace detail;
        namespace sk = skinmodel;
        const sk::Model& m = *hero_;
        if (heroColours_.empty()) {
            heroColours_.assign(m.vertCount, Rgba(46, 140, 60, 255));
            for (int b = 0; b < m.batchCount; b++) {
                const sk::Batch& batch = m.batches[b];
                if (batch.tex >= m.textureCount) continue;
                const uint8_t* tex = m.textures[batch.tex];
                for (int v = batch.firstVert; v < batch.firstVert + batch.vertCount && v < m.vertCount; v++) {
                    const int u = ((m.verts[v].s / 32) % sk::kTexW + sk::kTexW) % sk::kTexW, w = ((m.verts[v].t / 32) % sk::kTexH + sk::kTexH) % sk::kTexH;
                    const uint16_t px = static_cast<uint16_t>(tex[(w * sk::kTexW + u) * 2] << 8 | tex[(w * sk::kTexW + u) * 2 + 1]);
                    heroColours_[v] = Rgba((px >> 11 & 31) * 255 / 31.0f, (px >> 6 & 31) * 255 / 31.0f, (px >> 1 & 31) * 255 / 31.0f, 255);
                }
            }
        }
        V3 at, ahead;
        RunnerPath(t, at, ahead);
        if (at.y < 0.6f) return;
        const float run = t * 10.5f, scale = 1.8f;   // about 3.2 units tall: a miniature beside the castle
        const float bob = std::abs(std::sin(run)) * 0.05f, heading = std::atan2(ahead.x - at.x, ahead.z - at.z);
        // the pose, in the model's own space (metres, Y up, facing +Z, Link's left +X)
        Xf limb[sk::kLimbCount];
        const Xf body = Xf::Move({0, bob, 0}) * Xf::Turn(0, 0.16f, {0, 0.9f, 0});   // bob, and lean into the run
        for (Xf& x : limb) x = body;
        auto anchor = [&](int l) { const sk::Anchor& a = m.anchors[l]; return V3{a.x, a.y, a.z}; };
        for (int side = 0; side < 2; side++) {
            const float ph = run + side * kPi, swing = std::sin(ph) * 0.75f;
            const int thigh = side ? sk::kLThigh : sk::kRThigh, shin = side ? sk::kLShin : sk::kRShin, foot = side ? sk::kLFoot : sk::kRFoot;
            limb[thigh] = body * Xf::Turn(0, swing, anchor(thigh));
            limb[shin] = limb[thigh] * Xf::Turn(0, 0.25f + 0.85f * std::max(0.0f, std::sin(ph + 0.6f)), anchor(shin));
            limb[foot] = limb[shin] * Xf::Turn(0, -0.3f * std::max(0.0f, std::sin(ph)), anchor(foot));
            // the arm on the same side swings against the leg
            const int shoulder = side ? sk::kLShoulder : sk::kRShoulder, fore = side ? sk::kLForearm : sk::kRForearm, hand = side ? sk::kLHand : sk::kRHand;
            limb[shoulder] = body * Xf::Turn(0, -swing * 0.9f, anchor(shoulder)) * Xf::Turn(2, side ? -1.32f : 1.32f, anchor(shoulder));
            limb[fore] = limb[shoulder] * Xf::Turn(0, -1.0f, anchor(fore));
            limb[hand] = limb[fore];
        }
        limb[sk::kHat] = body * Xf::Turn(0, 0.25f + 0.15f * std::sin(run * 0.5f), anchor(sk::kHat));
        const Xf place = Xf::Move(at) * Xf::Turn(1, heading) * Xf::Move({0, -m.floorY * scale, 0}, scale);
        Xf world[sk::kLimbCount];
        for (int l = 0; l < sk::kLimbCount; l++) world[l] = place * limb[l];
        // every vertex to the screen
        heroVerts_.resize(m.vertCount);
        for (int i = 0; i < m.vertCount; i++) {
            const sk::Vert& v = m.verts[i];
            const V3 p{v.x, v.y, v.z}, n{v.nx / 127.0f, v.ny / 127.0f, v.nz / 127.0f};
            const int b0 = v.b0 < sk::kLimbCount ? v.b0 : 0, b1 = v.b1 < sk::kLimbCount ? v.b1 : b0;
            const float w = v.w0 / 255.0f;
            const V3 wp = world[b0].Point(p) * w + world[b1].Point(p) * (1 - w);
            const V3 wn = Norm(world[b0].Dir(n) * w + world[b1].Dir(n) * (1 - w));
            HeroVert& h = heroVerts_[i];
            h.p = wp;
            h.c = Lit(heroColours_[i], wp, wn);
            h.ok = cam_.Project(wp, h.sx, h.sy, h.d);
        }
        for (int b = 0; b < m.batchCount; b++) {
            const sk::Batch& batch = m.batches[b];
            for (int k = batch.firstTri; k < batch.firstTri + batch.triCount && k < m.triCount; k++) {
                const int i0 = batch.firstVert + m.tris[k][0], i1 = batch.firstVert + m.tris[k][1], i2 = batch.firstVert + m.tris[k][2];
                if (i0 >= m.vertCount || i1 >= m.vertCount || i2 >= m.vertCount) continue;
                const HeroVert &a = heroVerts_[i0], &bb = heroVerts_[i1], &c = heroVerts_[i2];
                if (!a.ok || !bb.ok || !c.ok) continue;
                if (Dot(Cross(bb.p - a.p, c.p - a.p), cam_.eye - a.p) <= 0) continue;   // the far side of the body
                Push(a.sx, a.sy, bb.sx, bb.sy, c.sx, c.sy, a.c, bb.c, c.c, (a.d + bb.d + c.d) / 3);
            }
        }
        // the Hylian Shield and the Master Sword on his back (Link faces +Z, so his back is -Z)
        const Xf gear = place * body;
        auto G = [&](float x, float y, float z) { return gear.Point({x, y, z}); };
        auto plate = [&](V3 a, V3 b, float w, float z, uint32_t col) {   // a strip from a to b (model x, y), w wide, at depth z
            const V3 d = Norm(b - a), n{-d.y * w, d.x * w, 0};
            Face(G(a.x + n.x, a.y + n.y, z), G(b.x + n.x, b.y + n.y, z), G(b.x - n.x, b.y - n.y, z), col);
            Face(G(a.x + n.x, a.y + n.y, z), G(b.x - n.x, b.y - n.y, z), G(a.x - n.x, a.y - n.y, z), col);
        };
        plate({0.2f, 0.72f, 0}, {-0.17f, 1.48f, 0}, 0.035f, -0.17f, Rgba(70, 64, 120, 255));     // the scabbard
        plate({-0.12f, 1.4f, 0}, {-0.24f, 1.5f, 0}, 0.045f, -0.19f, Rgba(90, 80, 190, 255));      // the guard's wings
        plate({-0.17f, 1.48f, 0}, {-0.25f, 1.64f, 0}, 0.02f, -0.19f, Rgba(46, 52, 120, 255));     // the grip
        const float sz = -0.23f;
        const V3 kite[] = {{-0.2f, 1.36f, 0}, {0.2f, 1.36f, 0}, {0.21f, 1.06f, 0}, {0, 0.82f, 0}, {-0.21f, 1.06f, 0}};
        const V3 mid{0, 1.12f, 0};
        for (int i = 0; i < 5; i++) {
            const V3 p0 = kite[i], p1 = kite[(i + 1) % 5];
            Face(G(mid.x, mid.y, sz + 0.012f), G(p0.x * 1.12f, mid.y + (p0.y - mid.y) * 1.1f, sz + 0.012f), G(p1.x * 1.12f, mid.y + (p1.y - mid.y) * 1.1f, sz + 0.012f), Rgba(196, 200, 214, 255));
            Face(G(mid.x, mid.y, sz), G(p0.x, p0.y, sz), G(p1.x, p1.y, sz), Rgba(40, 70, 172, 255));
        }
        Face(G(-0.1f, 1.12f, sz - 0.01f), G(0.1f, 1.12f, sz - 0.01f), G(0, 0.92f, sz - 0.01f), Rgba(200, 40, 40, 255));   // the red bird
        Face(G(-0.06f, 1.2f, sz - 0.01f), G(0.06f, 1.2f, sz - 0.01f), G(0, 1.3f, sz - 0.01f), Rgba(240, 200, 70, 255));   // the Triforce
        // dust at his heels
        for (int i = 0; i < 3; i++) {
            const float age = std::fmod(t * 2.2f + i / 3.0f, 1.0f);
            const V3 back = at - Norm(ahead - at) * (0.6f + age * 1.6f) + V3{0, 0.3f + age * 0.6f, 0};
            Billboard(back, 0.35f + age * 0.5f, Rgba(236, 216, 176, 120 * (1 - age)), Rgba(236, 216, 176, 0), 0.5f, 8);
        }
    }
    // A flat stand-in for the hero when there is no model: a tiny figure in a green tunic, cap flapping, shield on the back.
    void Runner(float t) {
        using namespace detail;
        if (hero_ != nullptr && hero_->vertCount > 0) { Hero(t); return; }
        const float speed = 0.55f, a = t * speed + 2.2f;
        auto path = [&](float ang) {
            const float r = kMeadowRadius - 2 + 1.5f * std::sin(ang * 2.0f);
            return V3{kMeadow.x + std::cos(ang) * r, 0, kMeadow.z + std::sin(ang) * r * 0.8f};
        };
        V3 p = path(a), ahead = path(a + 0.02f);
        p.y = Height(p.x, p.z);
        ahead.y = Height(ahead.x, ahead.z);
        if (p.y < 0.6f) return;
        float x, y, d, ax, ay, ad;
        if (!cam_.Project(p, x, y, d) || !cam_.Project(ahead, ax, ay, ad)) return;
        const float s = 3.0f * cam_.focal / d;   // tiny, but big enough to read
        const float dir = ax >= x ? 1.0f : -1.0f;  // facing left or right on screen
        const float run = t * 11, bob = std::abs(std::sin(run)) * s * 0.06f;
        y -= bob;
        const uint32_t tunic = Rgba(40, 150, 64, 255), dark = Rgba(28, 104, 46, 255), skin = Rgba(246, 204, 164, 255);
        const uint32_t boots = Rgba(96, 60, 36, 255), hair = Rgba(236, 196, 84, 255), shield = Rgba(46, 76, 170, 255), steel = Rgba(214, 220, 232, 255);
        auto limb = [&](float px, float py, float ang, float len, float w, uint32_t c, float dz) {
            const float ex = px + std::sin(ang) * len, ey = py + std::cos(ang) * len, nx = std::cos(ang) * w, ny = -std::sin(ang) * w;
            Push(px - nx, py - ny, px + nx, py + ny, ex + nx, ey + ny, c, c, c, d - dz);
            Push(px - nx, py - ny, ex + nx, ey + ny, ex - nx, ey - ny, c, c, c, d - dz);
        };
        // dust kicked up behind
        for (int i = 0; i < 3; i++) {
            const float age = std::fmod(t * 2.2f + i / 3.0f, 1.0f);
            Push(x - dir * s * (0.2f + age * 0.5f) - s * 0.08f, y - s * age * 0.15f, x - dir * s * (0.2f + age * 0.5f) + s * 0.08f, y - s * age * 0.15f,
                 x - dir * s * (0.2f + age * 0.5f), y - s * (0.12f + age * 0.25f), Rgba(230, 210, 170, 140 * (1 - age)), Rgba(230, 210, 170, 140 * (1 - age)),
                 Rgba(230, 210, 170, 0), d + 0.05f);
        }
        const float hipY = y - s * 0.42f, sh = y - s * 0.78f;
        limb(x, hipY, std::sin(run) * 0.8f, s * 0.42f, s * 0.07f, boots, 0.00f);           // back leg
        limb(x - dir * s * 0.02f, sh, dir * -0.3f - std::sin(run) * 0.9f, s * 0.3f, s * 0.05f, skin, 0.01f);   // back arm
        // the shield on the back, the sword hilt over the shoulder
        limb(x - dir * s * 0.14f, sh - s * 0.12f, kPi + dir * 0.5f, s * 0.22f, s * 0.04f, steel, 0.015f);
        Push(x - dir * s * 0.1f, sh, x - dir * s * 0.32f, sh + s * 0.05f, x - dir * s * 0.22f, sh + s * 0.34f, shield, shield, shield, d - 0.02f);
        Push(x - dir * s * 0.1f, sh, x - dir * s * 0.22f, sh + s * 0.34f, x - dir * s * 0.06f, sh + s * 0.3f, shield, shield, shield, d - 0.02f);
        // the tunic, belt and front leg and arm
        Push(x - s * 0.15f, sh - s * 0.02f, x + s * 0.15f, sh - s * 0.02f, x + s * 0.2f, hipY + s * 0.04f, tunic, tunic, dark, d - 0.03f);
        Push(x - s * 0.15f, sh - s * 0.02f, x + s * 0.2f, hipY + s * 0.04f, x - s * 0.2f, hipY + s * 0.04f, tunic, dark, dark, d - 0.03f);
        limb(x - s * 0.17f, hipY - s * 0.1f, kPi / 2, s * 0.34f, s * 0.03f, boots, 0.035f);
        limb(x, hipY, -std::sin(run) * 0.8f, s * 0.42f, s * 0.07f, boots, 0.04f);
        limb(x + dir * s * 0.02f, sh, dir * -0.3f + std::sin(run) * 0.9f, s * 0.3f, s * 0.05f, skin, 0.05f);
        // the head: face, golden hair and the long green cap trailing behind
        const float hx = x + dir * s * 0.02f, hy = sh - s * 0.14f, hr = s * 0.13f;
        for (int k = 0; k < 10; k++) {
            const float a0 = k * 2 * kPi / 10, a1 = (k + 1) * 2 * kPi / 10;
            Push(hx, hy, hx + std::cos(a0) * hr, hy + std::sin(a0) * hr, hx + std::cos(a1) * hr, hy + std::sin(a1) * hr, skin, skin, skin, d - 0.06f);
        }
        Push(hx - hr, hy - hr * 0.1f, hx + hr, hy - hr * 0.1f, hx + dir * hr * 0.2f, hy - hr * 1.15f, hair, hair, hair, d - 0.065f);
        const float flap = std::sin(run * 0.5f) * s * 0.05f;
        Push(hx - hr * 1.05f, hy - hr * 0.35f, hx + hr * 1.05f, hy - hr * 0.35f, hx - dir * s * 0.42f, hy + s * 0.02f + flap, tunic, tunic, dark, d - 0.07f);
        Push(hx - hr * 0.9f, hy - hr * 0.35f, hx + hr * 0.9f, hy - hr * 0.35f, hx, hy - hr * 1.25f, tunic, tunic, tunic, d - 0.07f);
    }
    void Trees() {
        using namespace detail;
        int placed = 0;
        for (int i = 0; i < 400 && placed < 44; i++) {
            const float x = (Hash(i * 3 + 1) - 0.5f) * 120, z = (Hash(i * 3 + 2) - 0.5f) * 120;
            const float h = Height(x, z);
            if (h < 1.6f || h > 13.5f) continue;
            if ((x - kCastle.x) * (x - kCastle.x) + (z - kCastle.z) * (z - kCastle.z) < (kCastleRadius + 2) * (kCastleRadius + 2)) continue;
            if ((x - kMeadow.x) * (x - kMeadow.x) + (z - kMeadow.z) * (z - kMeadow.z) < (kMeadowRadius + 3) * (kMeadowRadius + 3)) continue;
            placed++;
            const float s = 0.65f + Hash(i + 999) * 0.5f;
            const uint32_t needles = Mix(Rgba(46, 106, 56, 255), Rgba(30, 84, 52, 255), Hash(i + 4));
            Prism({x, 0, z}, 0.45f * s, h - 0.5f, h + 1.2f * s, 5, Rgba(110, 76, 48, 255));
            Prism({x, 0, z}, 2.6f * s, h + 1.2f * s, h + 1.3f * s, 7, needles, 4.2f * s, needles);
            Prism({x, 0, z}, 1.9f * s, h + 3.6f * s, h + 3.7f * s, 7, needles, 3.6f * s, Shade(needles, 1.1f));
        }
    }
    void Chests(float t) {
        using namespace detail;
        const float spots[][2] = {{-6, 14}, {30, 18}, {-34, 4}, {4, -30}, {24, -30}, {-14, 34}};
        for (int i = 0; i < 6; i++) {
            const float x = spots[i][0], z = spots[i][1], h = Height(x, z);
            if (h < 0.5f) continue;
            Box({x, h - 0.1f, z}, 1.1f, 0.45f, 0.75f, Rgba(140, 84, 40, 255));
            Box({x, h + 0.78f, z}, 1.15f, 0.12f, 0.8f, Rgba(232, 186, 74, 255));
            // the beam of light that says loot is here
            float bx, by, bd, tx, ty, td;
            if (!cam_.Project({x, h + 1, z}, bx, by, bd) || !cam_.Project({x, h + 26, z}, tx, ty, td)) continue;
            const float pulse = 0.65f + 0.35f * std::sin(t * 2.6f + i * 1.7f), w = 1.3f * cam_.focal / bd;
            const uint32_t gold = i == 1 ? Rgba(190, 120, 255, 150 * pulse) : Rgba(255, 214, 110, 150 * pulse);
            Push(bx - w, by, bx + w, by, tx + w * 0.4f, ty, gold, gold, Alpha(gold, 0), bd - 2);
            Push(bx - w, by, tx + w * 0.4f, ty, tx - w * 0.4f, ty, gold, Alpha(gold, 0), Alpha(gold, 0), bd - 2);
            Billboard({x, h + 1.4f, z}, 3.2f, Alpha(gold, 0.9f), Alpha(gold, 0), -2.5f);
        }
    }
    void Moon(float t) {
        using namespace detail;
        // Majora's Moon, hanging over the island: the dread of Termina in a Hyrule sky
        // so far away it barely moves: it keeps its place in the upper right, beside the logo
        const float W = W_, H = H_;
        float x = W * 0.5f + std::min(W * 0.4f, H * 0.82f), y = H * 0.2f;
        const float r = cam_.focal * 0.1f, bob = std::sin(t * 0.8f) * r * 0.03f;
        const float D = 1e6f;   // drawn behind everything near
        auto disc = [&](float cx, float cy, float rx, float ry, uint32_t in, uint32_t outc, int n = 28) {
            for (int i = 0; i < n; i++) {
                const float a0 = i * 2 * kPi / n, a1 = (i + 1) * 2 * kPi / n;
                Push(cx, cy, cx + std::cos(a0) * rx, cy + std::sin(a0) * ry, cx + std::cos(a1) * rx, cy + std::sin(a1) * ry, in, outc, outc, D);
            }
        };
        y += bob;
        disc(x, y, r * 2.2f, r * 2.2f, Rgba(255, 120, 70, 70), Rgba(255, 90, 60, 0), 32);
        disc(x, y, r, r, Rgba(214, 196, 150, 255), Rgba(170, 150, 120, 255), 36);
        disc(x - r * 0.25f, y - r * 0.25f, r * 0.7f, r * 0.7f, Rgba(236, 222, 180, 180), Rgba(214, 196, 150, 0), 24);
        const float craters[][3] = {{-0.55f, -0.5f, 0.12f}, {0.6f, -0.45f, 0.1f}, {0.15f, -0.72f, 0.08f}, {0.7f, 0.35f, 0.09f}, {-0.72f, 0.3f, 0.07f}};
        for (const auto& c : craters) disc(x + c[0] * r, y + c[1] * r, c[2] * r, c[2] * r, Rgba(150, 128, 104, 255), Rgba(178, 158, 126, 255), 10);
        // the eyes: bulging, yellow, glaring
        for (int s = -1; s <= 1; s += 2) {
            const float ex = x + s * r * 0.36f, ey = y - r * 0.12f;
            disc(ex, ey, r * 0.24f, r * 0.2f, Rgba(255, 238, 150, 255), Rgba(230, 170, 60, 255), 18);
            disc(ex + s * r * 0.02f, ey + r * 0.02f, r * 0.11f, r * 0.11f, Rgba(240, 110, 30, 255), Rgba(170, 50, 20, 255), 14);
            disc(ex + s * r * 0.02f, ey + r * 0.02f, r * 0.045f, r * 0.045f, Rgba(20, 10, 10, 255), Rgba(20, 10, 10, 255), 10);
            // the angry brow
            Push(ex - r * 0.28f, ey - r * 0.2f - s * r * 0.08f, ex + r * 0.28f, ey - r * 0.2f + s * r * 0.08f, ex, ey - r * 0.36f,
                 Rgba(110, 86, 66, 255), Rgba(110, 86, 66, 255), Rgba(110, 86, 66, 255), D);
        }
        // the nose and the grimace full of teeth
        Push(x, y - r * 0.05f, x - r * 0.09f, y + r * 0.22f, x + r * 0.09f, y + r * 0.22f, Rgba(170, 146, 112, 255), Rgba(150, 126, 98, 255), Rgba(150, 126, 98, 255), D);
        const float my = y + r * 0.42f, mw = r * 0.5f, mh = r * 0.14f;
        disc(x, my, mw, mh, Rgba(40, 20, 20, 255), Rgba(60, 30, 26, 255), 20);
        for (int i = 0; i < 8; i++) {
            const float tx = x - mw * 0.85f + i * mw * 1.7f / 7, k = 1 - std::pow(std::abs(tx - x) / mw, 2.0f);
            const uint32_t tooth = Rgba(246, 240, 220, 255);
            Push(tx - mw * 0.09f, my - mh * k * 0.85f, tx + mw * 0.09f, my - mh * k * 0.85f, tx, my + mh * 0.1f, tooth, tooth, tooth, D);
            Push(tx - mw * 0.09f, my + mh * k * 0.85f, tx, my - mh * 0.1f, tx + mw * 0.09f, my + mh * k * 0.85f, tooth, tooth, tooth, D);
        }
    }
    // Players drifting down onto the island under blue and white striped canopies; one is a supply chest.
    void Gliders(float t) {
        using namespace detail;
        for (int i = 0; i < 7; i++) {
            const float cycle = 16 + Hash(i) * 6, u = std::fmod(t + Hash(i + 10) * cycle, cycle) / cycle;
            const float a = Hash(i + 20) * 2 * kPi, r0 = 30 + Hash(i + 30) * 30;
            const V3 land{std::cos(a) * r0 * 0.6f, Height(std::cos(a) * r0 * 0.6f, std::sin(a) * r0 * 0.6f) + 2, std::sin(a) * r0 * 0.6f};
            const V3 from{std::cos(a + 0.6f) * r0 * 1.3f, 75, std::sin(a + 0.6f) * r0 * 1.3f};
            V3 p = from + (land - from) * u;
            p.x += std::sin(t * 0.9f + i) * 3;
            float x, y, d;
            if (!cam_.Project(p, x, y, d)) continue;
            const float fade = Smooth(0, 0.08f, u) * (1 - Smooth(0.9f, 1, u)) * Smooth(75, 115, d);   // gone before one fills the screen
            if (fade <= 0) continue;
            const float s = 8.5f * cam_.focal / d, sway = std::sin(t * 1.6f + i * 2) * 0.12f;
            const bool drop = i == 3;
            const uint32_t blue = Rgba(40, 70, 190, 255 * fade), white = Rgba(240, 240, 248, 255 * fade), line = Rgba(40, 30, 30, 200 * fade);
            // the canopy: a striped dome above
            const float cx = x + std::sin(sway) * s * 1.0f, cy = y - s * 1.2f, rw = s * 0.95f, rh = s * 0.6f;
            const int segs = 8;
            for (int k = 0; k < segs; k++) {
                const float a0 = kPi + k * kPi / segs, a1 = kPi + (k + 1) * kPi / segs;
                const uint32_t c = k % 2 ? white : blue;
                Push(cx, cy + rh * 0.25f, cx + std::cos(a0) * rw, cy + std::sin(a0) * rh, cx + std::cos(a1) * rw, cy + std::sin(a1) * rh, c, c, c, d);
            }
            const float lw = std::max(0.8f, s * 0.04f);
            for (int side = -1; side <= 1; side += 2) {
                const float ax = cx + side * rw * 0.95f, ay = cy;
                Push(ax - lw, ay, ax + lw, ay, x + side * s * 0.1f, y - s * 0.15f, line, line, line, d - 0.01f);
            }
            if (drop) {   // a blue supply chest with gold bands
                const uint32_t box = Rgba(30, 60, 160, 255 * fade), band = Rgba(232, 186, 74, 255 * fade);
                Push(x - s * 0.3f, y - s * 0.2f, x + s * 0.3f, y - s * 0.2f, x + s * 0.3f, y + s * 0.25f, box, box, box, d - 0.02f);
                Push(x - s * 0.3f, y - s * 0.2f, x + s * 0.3f, y + s * 0.25f, x - s * 0.3f, y + s * 0.25f, box, box, box, d - 0.02f);
                Push(x - s * 0.3f, y - s * 0.02f, x + s * 0.3f, y - s * 0.02f, x + s * 0.3f, y + s * 0.05f, band, band, band, d - 0.03f);
                Push(x - s * 0.3f, y - s * 0.02f, x + s * 0.3f, y + s * 0.05f, x - s * 0.3f, y + s * 0.05f, band, band, band, d - 0.03f);
            } else {      // a little hero in a green tunic and cap
                const uint32_t tunic = Rgba(46, 140, 60, 255 * fade), skin = Rgba(244, 200, 160, 255 * fade), boots = Rgba(90, 56, 34, 255 * fade);
                Push(x - s * 0.2f, y + s * 0.3f, x + s * 0.2f, y + s * 0.3f, x, y + s * 0.5f, boots, boots, boots, d - 0.02f);
                Push(x - s * 0.22f, y + s * 0.32f, x + s * 0.22f, y + s * 0.32f, x, y - s * 0.12f, tunic, tunic, tunic, d - 0.03f);
                for (int k = 0; k < 8; k++) {
                    const float a0 = k * kPi / 4, a1 = (k + 1) * kPi / 4;
                    Push(x, y - s * 0.2f, x + std::cos(a0) * s * 0.12f, y - s * 0.2f + std::sin(a0) * s * 0.12f, x + std::cos(a1) * s * 0.12f,
                         y - s * 0.2f + std::sin(a1) * s * 0.12f, skin, skin, skin, d - 0.04f);
                }
                Push(x - s * 0.13f, y - s * 0.26f, x + s * 0.13f, y - s * 0.26f, x - s * 0.34f, y - s * 0.06f, tunic, tunic, tunic, d - 0.05f);
            }
        }
    }
    // Fairies drifting over the grass.
    void Fairies(float t) {
        using namespace detail;
        for (int i = 0; i < 16; i++) {
            const float a = Hash(i + 300) * 2 * kPi + t * (0.1f + Hash(i) * 0.15f), r = 8 + Hash(i + 60) * 42;
            const float x = std::cos(a) * r, z = std::sin(a) * r, h = Height(x, z);
            if (h < 1) continue;
            const float tw = 0.5f + 0.5f * std::sin(t * 3 + i * 1.7f);
            const uint32_t c = i % 3 == 0 ? Rgba(255, 220, 120, 230) : Rgba(170, 236, 255, 230);
            Billboard({x, h + 3 + std::sin(t * 1.3f + i) * 1.2f, z}, 0.9f + tw * 0.6f, Alpha(c, 0.4f + 0.5f * tw), Alpha(c, 0), -3, 8);
            Billboard({x, h + 3 + std::sin(t * 1.3f + i) * 1.2f, z}, 0.25f, 0xFFFFFFFF, Alpha(c, 0.6f), -3.1f, 6);
        }
    }

    // ---- the screen layer ----
    static void Vignette(Canvas& out, float W, float H) {
        using namespace detail;
        const uint32_t dark = Rgba(4, 6, 20, 150), clear = Rgba(4, 6, 20, 0);
        Grad(out, 0, 0, W, H * 0.22f, Rgba(4, 6, 20, 120), clear);
        Grad(out, 0, H * 0.72f, W, H, clear, Rgba(4, 6, 20, 190));
        out.Tri(0, 0, dark, W * 0.2f, 0, clear, W * 0.2f, H, clear);
        out.Tri(0, 0, dark, W * 0.2f, H, clear, 0, H, dark);
        out.Tri(W * 0.8f, 0, clear, W, 0, dark, W, H, dark);
        out.Tri(W * 0.8f, 0, clear, W, H, dark, W * 0.8f, H, clear);
    }
    // The logo lands with a burst, then burns: tongues of orange flame behind it, embers rising, sparkles twinkling on its rim.
    static void LogoAndFlames(Canvas& out, float W, float H, float t, float outro, bool reduced, float aspect) {
        using namespace detail;
        if (aspect <= 0) aspect = 2.0f;
        float x0, y0, x1, y1;
        Scene::LogoRect(W, H, aspect, x0, y0, x1, y1);
        const float lt = t - kLogoAt;
        if (lt < 0 && !reduced) return;
        const float in = reduced ? Clamp01((t - 0.3f) / 0.8f) : Clamp01(lt / 0.35f);
        const float scale = (reduced ? 1.0f : 1.35f - 0.35f * EaseOutBack(lt / 0.75f)) * (outro >= 0 ? 1 + EaseOut(outro / kOutroSeconds) * 0.12f : 1);
        const float bob = reduced ? 0 : std::sin(t * 1.6f) * H * 0.006f;
        const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f + bob, w = (x1 - x0) * scale, h = (y1 - y0) * scale;
        const float heat = in * (reduced ? 0.6f : Smooth(0, 1.2f, lt));
        // the warm glow and the slowly turning rays of light
        Glow(out, cx, cy, w * 0.66f, h * 0.78f, Rgba(255, 140, 50, 120 * heat), Rgba(200, 40, 20, 0), 36);
        if (!reduced) {
            const int rays = 16;
            for (int i = 0; i < rays; i++) {
                const float a = i * 2 * kPi / rays + t * 0.12f, len = w * (0.5f + 0.1f * std::sin(t * 1.5f + i * 2.3f)), half = 0.06f;
                const uint32_t c = Rgba(255, 214, 140, 34 * heat), e = Rgba(255, 180, 90, 0);
                out.Tri(cx, cy, c, cx + std::cos(a - half) * len, cy + std::sin(a - half) * len * 0.7f, e, cx + std::cos(a + half) * len, cy + std::sin(a + half) * len * 0.7f, e);
            }
        }
        // the flames, two layers: tall red-orange behind, shorter yellow in front
        for (int layer = 0; layer < 2; layer++) {
            const int n = 46;
            const float baseY = cy + h * (layer ? 0.0f : -0.04f), span = w * 0.86f;
            for (int i = 0; i < n; i++) {
                const float u = (i + 0.5f) / n, fx = cx - span * 0.5f + u * span;
                const float edge = std::sin(u * kPi);   // tallest in the middle
                const float flick = reduced ? 0.75f : 0.55f + 0.25f * std::sin(t * (5.0f + Hash(i + layer * 50) * 4) + i * 1.3f) + 0.2f * std::sin(t * 11.0f + i * 2.9f);
                const float fh = h * (layer ? 0.5f : 0.66f) * (0.55f + 0.45f * edge) * flick;
                const float fw = span / n * (layer ? 2.4f : 3.0f);
                const float sway = reduced ? 0 : std::sin(t * 3.0f + i * 0.9f) * fw * 0.5f;
                const uint32_t base = layer ? Rgba(255, 236, 140, 150 * heat) : Rgba(255, 120, 30, 150 * heat);
                const uint32_t mid = layer ? Rgba(255, 170, 50, 110 * heat) : Rgba(230, 60, 20, 95 * heat);
                const uint32_t tip = layer ? Rgba(255, 90, 20, 0) : Rgba(160, 20, 10, 0);
                const float mx = fx + sway * 0.5f, my = baseY - fh * 0.5f;
                out.Tri(fx - fw * 0.5f, baseY, base, fx + fw * 0.5f, baseY, base, mx + fw * 0.32f, my, mid);
                out.Tri(fx - fw * 0.5f, baseY, base, mx + fw * 0.32f, my, mid, mx - fw * 0.32f, my, mid);
                out.Tri(mx - fw * 0.32f, my, mid, mx + fw * 0.32f, my, mid, fx + sway, baseY - fh, tip);
            }
        }
        // the burst ring as it lands
        if (!reduced && lt < 0.9f) {
            const float k = EaseOut(lt / 0.9f), rr = w * (0.3f + 0.6f * k);
            const int n = 48;
            for (int i = 0; i < n; i++) {
                const float a0 = i * 2 * kPi / n, a1 = (i + 1) * 2 * kPi / n, th = H * 0.012f * (1 - k);
                const uint32_t c = Rgba(255, 240, 200, 230 * (1 - k)), e = Rgba(255, 200, 120, 0);
                out.Tri(cx + std::cos(a0) * rr, cy + std::sin(a0) * rr * 0.6f, c, cx + std::cos(a1) * rr, cy + std::sin(a1) * rr * 0.6f, c,
                        cx + std::cos(a0) * (rr + th * 4), cy + std::sin(a0) * (rr + th * 4) * 0.6f, e);
            }
        }
        // embers rising from behind the logo
        for (int i = 0; reduced ? false : i < 70; i++) {
            const float life = 2.2f + Hash(i) * 2.0f, u = std::fmod(t + Hash(i + 7) * life, life) / life;
            const float ex = cx + (Hash(i + 13) - 0.5f) * w * 0.95f + std::sin(t * 1.7f + i) * w * 0.03f + u * (Hash(i + 2) - 0.5f) * w * 0.15f;
            const float ey = cy + h * 0.25f - u * h * (0.8f + Hash(i + 5) * 0.6f);
            const float a = heat * Smooth(0, 0.1f, u) * (1 - Smooth(0.6f, 1, u));
            if (a <= 0.01f) continue;
            const float r = H * (0.0025f + Hash(i + 3) * 0.003f) * (1 - u * 0.5f);
            const uint32_t c = Mix(Rgba(255, 236, 150, 255 * a), Rgba(240, 70, 20, 230 * a), u);
            Glow(out, ex, ey, r * 3.5f, r * 3.5f, Alpha(c, 0.35f * a), Alpha(c, 0), 10);
            out.Tri(ex, ey - r * 1.8f, c, ex + r, ey, c, ex - r, ey, c);
            out.Tri(ex, ey + r * 1.8f, c, ex - r, ey, c, ex + r, ey, c);
        }
        out.Logo(cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f, in);
        // sparkles twinkling on the logo, the four-point stars of OoT's fairies and item gets
        for (int i = 0; reduced ? false : i < 9; i++) {
            const float period = 1.6f + Hash(i + 40) * 1.4f, u = std::fmod(t + Hash(i + 41) * period, period) / period;
            if (u > 0.35f) continue;
            const float k = std::sin(u / 0.35f * kPi) * in;
            const int slot = static_cast<int>((t + Hash(i + 41) * period) / period) * 13 + i;
            const float sx = cx + (Hash(slot) - 0.5f) * w * 0.85f, sy = cy + (Hash(slot + 1) - 0.5f) * h * 0.7f, s = H * 0.03f * k;
            const uint32_t c = Rgba(255, 255, 240, 255 * k), e = Rgba(255, 240, 200, 0);
            Glow(out, sx, sy, s * 0.6f, s * 0.6f, Rgba(255, 250, 220, 120 * k), e, 10);
            out.Tri(sx - s, sy, e, sx, sy - s * 0.12f, c, sx, sy + s * 0.12f, c);
            out.Tri(sx + s, sy, e, sx, sy + s * 0.12f, c, sx, sy - s * 0.12f, c);
            out.Tri(sx, sy - s, e, sx + s * 0.12f, sy, c, sx - s * 0.12f, sy, c);
            out.Tri(sx, sy + s, e, sx - s * 0.12f, sy, c, sx + s * 0.12f, sy, c);
        }
    }
    static void Prompt(Canvas& out, float W, float H, float t, float outro, bool reduced) {
        using namespace detail;
        const float in = Clamp01((t - kPromptAt) / 0.6f);
        if (in <= 0) return;
        float a = in * (reduced ? 1.0f : 0.72f + 0.28f * std::sin((t - kPromptAt) * 3.4f));
        if (outro >= 0) a = outro < 0.45f ? (std::fmod(outro, 0.1f) < 0.05f ? 1.0f : 0.25f) : 0.0f;
        const float y = H * 0.85f, size = H * 0.05f;
        const float band = std::max(in * (outro >= 0 ? 1.0f - Clamp01((outro - 0.45f) / 0.3f) : 1.0f), 0.0f);
        Glow(out, W * 0.5f, y + size * 0.5f, H * 0.62f, size * 1.6f, Rgba(6, 8, 24, 170 * band), Rgba(6, 8, 24, 0), 28);
        // gold rules with a diamond either side
        const float half = H * 0.34f, gap = H * 0.2f, ly = y + size * 0.5f;
        const uint32_t gold = Rgba(232, 190, 90, 220 * a), clear = Rgba(232, 190, 90, 0);
        for (int s = -1; s <= 1; s += 2) {
            const float x0 = W * 0.5f + s * gap, x1 = W * 0.5f + s * half;
            out.Tri(x0, ly - 1.2f, gold, x1, ly - 0.6f, clear, x1, ly + 0.6f, clear);
            out.Tri(x0, ly - 1.2f, gold, x1, ly + 0.6f, clear, x0, ly + 1.2f, gold);
            const float dx = x0 - s * size * 0.15f, d = size * 0.16f;
            out.Tri(dx, ly - d, gold, dx + d, ly, gold, dx, ly + d, gold);
            out.Tri(dx, ly - d, gold, dx, ly + d, gold, dx - d, ly, gold);
        }
        out.Text(W * 0.5f, y, size, Rgba(255, 220, 110, 255 * a), "PRESS START");
    }
};

} // namespace royale::title
