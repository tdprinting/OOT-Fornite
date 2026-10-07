#pragma once
#include "gilded_sword_model.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// The Gilded Sword's own normal and bump maps (see docs/ITEM_SURFACE_MAPS.md): engraved relief that follows the sword's design, made from the same measures as the
// Blender model so the two always agree. Each map is RGBA8 data like the game's other surface maps (flat is 128,128,255 for the normal map and 128 for the height
// map) and tiles; the game picks the map by the triangle's surface class (gilded_sword_model::Tri::cls) and maps the model's own coordinates into it.
//   Blade   one tile covers two diamonds (gold, then silver) and the blade's whole width, so the engraving lands exactly on the model's diamonds:
//           a groove just inside every diamond's edge, concentric engraved diamonds in the gold ones (and a raised boss at their heart), finer chevrons and
//           an inset border line in the silver ones, brushed lines along the plain metal between them, and hammered metal under all of it.
//   Cord    the red wrapped grip: diagonal rounded cords with a fine twist along each.
//   Metal   the guard, pommel, trims and the blade's point: brushed lines and small hammered dimples.
//   Leather the scabbard: pebbled grain.
// Everything is computed (no baked arrays), so it is cheap to keep in step with the model, and deterministic.
namespace royale {
namespace gilded_surface {

enum class Class : uint8_t { Blade = 0, Cord = 1, Metal = 2, Leather = 3, Count = 4 };

struct Map {
    int size = 0;                 // width and height in pixels
    float uvScale = 1.0f;         // model units (limb units) to map coordinates: a tile is 1 / uvScale model units across
    std::vector<uint8_t> normal;  // RGBA8, OpenGL tangent convention (green up)
    std::vector<uint8_t> height;  // RGBA8, grey
};

namespace detail {

constexpr float kPi = 3.14159265f;

inline uint32_t Hash(int x, int y, int seed) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + static_cast<uint32_t>(seed) * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// Smooth value noise in 0..1 that repeats every `periodX` cells across and `periodY` cells down (so a map tiles).
inline float Noise(float x, float y, int periodX, int periodY, int seed) {
    const float fx = std::floor(x), fy = std::floor(y);
    const float tx = x - fx, ty = y - fy;
    const float sx = tx * tx * (3.0f - 2.0f * tx), sy = ty * ty * (3.0f - 2.0f * ty);
    auto at = [&](int ix, int iy) {
        const int px = ((ix % periodX) + periodX) % periodX, py = ((iy % periodY) + periodY) % periodY;
        return static_cast<float>(Hash(px, py, seed) & 0xFFFFu) / 65535.0f;
    };
    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    const float a = at(ix, iy) + (at(ix + 1, iy) - at(ix, iy)) * sx;
    const float b = at(ix, iy + 1) + (at(ix + 1, iy + 1) - at(ix, iy + 1)) * sx;
    return a + (b - a) * sy;
}

inline float Gauss(float v, float width) { return std::exp(-(v * v) / (width * width)); }
inline float Clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

// ---- the blade: model coordinates (limb units) in, height 0..1 out ----
inline float BladeHeight(float x, float y) {
    namespace gm = gilded_sword_model;
    const float pitch = gm::kDiamondPitch, period = 2.0f * pitch;
    float xp = std::fmod(x - gm::kBladeStart, period);
    if (xp < 0) xp += period;
    const int k = static_cast<int>(xp / pitch);
    const float a = xp / pitch - static_cast<float>(k);
    const float b = y / gm::kBladeHalfWidth;
    const float d = std::fabs(2.0f * a - 1.0f) + std::fabs(b);   // 0 at a diamond's heart, 1 along its edge
    float h = 0.5f + (Noise(x / (period / 36.0f), y / (period / 36.0f), 36, 36, 3) - 0.5f) * 0.035f;   // hammered metal under everything
    if (d < 1.0f) {
        h -= 0.17f * Gauss(1.0f - d, 0.035f);                                   // the groove along every diamond's edge
        const float fade = Clamp01((0.86f - d) / 0.12f);
        if (k == 0) {                                                           // gold: concentric engraved diamonds round a raised boss
            h += 0.075f * std::sin(2.0f * kPi * 6.0f * d) * fade;
            h += 0.14f * Gauss(d, 0.12f);
        } else {                                                                // silver: finer chevrons, an inset border and a pricked centre
            h += 0.05f * std::sin(2.0f * kPi * 9.0f * d) * fade;
            h -= 0.09f * Gauss(d - 0.9f, 0.02f);
            h -= 0.10f * Gauss(d, 0.07f);
        }
    } else {
        h += 0.028f * std::sin(2.0f * kPi * y / 52.0f);                          // brushed lines along the plain metal between the diamonds
        h += (Noise(x / (period / 6.0f), y / (period / 96.0f), 6, 96, 5) - 0.5f) * 0.03f;
    }
    return Clamp01(h);
}

inline float CordHeight(float u, float v) {   // u, v in tiles
    const float f = u * 4.0f + v * 4.0f;
    const float t = f - std::floor(f);
    const float cord = std::pow(std::sin(kPi * t), 0.55f);                       // a round cord, pinched between its neighbours
    float h = 0.28f + 0.5f * cord;
    h += 0.03f * std::sin(2.0f * kPi * (u * 16.0f - v * 8.0f));                  // the twist of the strands inside each cord
    h += (Noise(u * 16.0f, v * 16.0f, 16, 16, 7) - 0.5f) * 0.03f;
    return Clamp01(h);
}

inline float MetalHeight(float u, float v) {
    float h = 0.5f + (Noise(u * 3.0f, v * 40.0f, 3, 40, 11) - 0.5f) * 0.05f;       // brushed
    h += (Noise(u * 8.0f, v * 8.0f, 8, 8, 13) - 0.5f) * 0.09f;                      // hammered dimples
    return Clamp01(h);
}

inline float LeatherHeight(float u, float v) {
    float h = 0.5f + (Noise(u * 12.0f, v * 12.0f, 12, 12, 17) - 0.5f) * 0.16f;
    h += (Noise(u * 24.0f, v * 24.0f, 24, 24, 19) - 0.5f) * 0.07f;
    return Clamp01(h);
}

} // namespace detail

// A map from a height function of the pixel's position in model units (blade) or in tiles (the rest).
inline Map Build(Class cls) {
    using namespace detail;
    Map m;
    m.size = cls == Class::Blade ? 256 : 64;
    const float tile = cls == Class::Blade ? 2.0f * gilded_sword_model::kDiamondPitch : cls == Class::Cord ? 360.0f : cls == Class::Metal ? 600.0f : 500.0f;
    m.uvScale = 1.0f / tile;
    const int n = m.size;
    std::vector<float> h(static_cast<size_t>(n) * n);
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) {
            const float u = (static_cast<float>(i) + 0.5f) / n, v = (static_cast<float>(j) + 0.5f) / n;
            float value;
            switch (cls) {
                case Class::Blade: {
                    const float y = v * tile;
                    value = BladeHeight(u * tile, y > tile * 0.5f ? y - tile : y);   // v wraps: the blade's width sits either side of v = 0
                    break;
                }
                case Class::Cord: value = CordHeight(u, v); break;
                case Class::Metal: value = MetalHeight(u, v); break;
                default: value = LeatherHeight(u, v); break;
            }
            h[static_cast<size_t>(j) * n + i] = value;
        }
    const float strength = cls == Class::Blade ? 5.0f : cls == Class::Cord ? 2.5f : 3.0f;
    m.normal.resize(static_cast<size_t>(n) * n * 4);
    m.height.resize(static_cast<size_t>(n) * n * 4);
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) {
            auto at = [&](int x, int y) { return h[static_cast<size_t>((y + n) % n) * n + static_cast<size_t>((x + n) % n)]; };
            const float dx = (at(i + 1, j) - at(i - 1, j)) * strength, dy = (at(i, j + 1) - at(i, j - 1)) * strength;
            const float len = std::sqrt(dx * dx + dy * dy + 1.0f);
            const size_t o = (static_cast<size_t>(j) * n + i) * 4;
            m.normal[o + 0] = static_cast<uint8_t>(std::lround((-dx / len * 0.5f + 0.5f) * 255.0f));
            m.normal[o + 1] = static_cast<uint8_t>(std::lround((-dy / len * 0.5f + 0.5f) * 255.0f));
            m.normal[o + 2] = static_cast<uint8_t>(std::lround((1.0f / len * 0.5f + 0.5f) * 255.0f));
            m.normal[o + 3] = 255;
            const uint8_t g = static_cast<uint8_t>(std::lround(h[static_cast<size_t>(j) * n + i] * 255.0f));
            m.height[o + 0] = m.height[o + 1] = m.height[o + 2] = g;
            m.height[o + 3] = 255;
        }
    return m;
}

} // namespace gilded_surface
} // namespace royale
