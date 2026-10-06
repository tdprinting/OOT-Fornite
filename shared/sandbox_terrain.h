#pragma once
#include "fortnite_map_data.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace royale {
namespace sandbox {

// The Sandbox test map's ground. It is the Fortnite Map's mesh (the same 64 x 64 squares, the same drawing and collision code, see fortnite_map.h)
// with a different set of heights, colours and ground cover: a flat arena in the middle, ringed by cliffs and sea, with a test course on it. Nothing
// here knows about the game's types, so the unit tests can check it.
//
//   north        three ramps (17, 27 and 56 degrees) up to a high plateau
//   north-east   the boss pad
//   east         a sheer cliff over a plateau, and a ramp up it
//   south-east   the cart lot, with a kicker (jump) and a bumpy road
//   south        a pond to swim in
//   west         the loot plaza (a raised stone square) where every item lies
//   north-west   the glider hill, high enough for a long glide down
//   middle       the spawn pad, the block course (stone steps) and the cover yard
using namespace fortnite_data;

inline constexpr float kSpawnX = 0.0f, kSpawnZ = -400.0f;   // where you start

struct Zone {
    const char* name;
    const char* blurb;
    float x, z, radius;   // the spot to teleport to (the ground is found under it) and how far the place reaches
};
// The first kZoneCount of the Sandbox's 24 place names are these, in this order (shared/map.h, kPoiNames).
inline constexpr int kZoneCount = 12;
inline constexpr Zone kZones[kZoneCount] = {
    {"Spawn Pad",     "Flat ground to start from",                                  kSpawnX, kSpawnZ, 380.0f},
    {"Loot Plaza",    "Every item in the game, laid out in rows, and chests to open", -2300.0f, 300.0f, 700.0f},
    {"Cart Lot",      "Lon Lon Buggies, a kicker to jump and a bumpy road",            1700.0f, -1900.0f, 800.0f},
    {"Boss Pad",      "Flat and empty: spawn bosses here",                              2300.0f, 1900.0f, 750.0f},
    {"Glider Hill",   "The top of the hill: jump off and glide",                        -2100.0f, 2050.0f, 200.0f},
    {"Test Pond",     "Deep enough to swim in, with a sandy beach",                    -1300.0f, -1800.0f, 650.0f},
    {"Ramp Row",      "Ramps of 17, 27 and 56 degrees up to the plateau",                  0.0f, 1500.0f, 900.0f},
    {"Cliff Edge",    "The top of a sheer cliff, for falling and gliding",                2900.0f, 100.0f, 500.0f},
    {"Block Course",  "Stone steps 60, 120 and 180 tall: climbing and jumping",           300.0f, -1500.0f, 450.0f},
    {"Cover Yard",    "Boulders, pillars and walls to hide behind",                     1000.0f, -250.0f, 500.0f},
    {"Grove",         "A few trees and tall grass",                                     900.0f, 900.0f, 450.0f},
    {"Plateau",       "The high ground at the top of the ramps",                           0.0f, 2650.0f, 600.0f},
};

// ---- the shape of the ground --------------------------------------------------------------------------------------------------------------
inline float Clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }
inline float Smooth(float t) { t = Clamp01(t); return t * t * (3.0f - 2.0f * t); }
inline float Rise(float v, float a, float b) { return Smooth((v - a) / (b - a)); }   // 0 below a, 1 above b, smooth between
inline float Lin(float v, float a, float b) { return Clamp01((v - a) / (b - a)); }   // the same, in a straight line (a ramp)
// 1 inside the rectangle, fading to 0 over `f` beyond its edges.
inline float Box(float x, float z, float x0, float x1, float z0, float z1, float f) {
    return Rise(x, x0 - f, x0) * (1.0f - Rise(x, x1, x1 + f)) * Rise(z, z0 - f, z0) * (1.0f - Rise(z, z1, z1 + f));
}

inline constexpr float kPondX = -1300.0f, kPondZ = -1800.0f;
inline constexpr float kHillX = -2100.0f, kHillZ = 2050.0f, kHillTop = 400.0f;
inline constexpr float kPlateauHeight = 300.0f, kCliffHeight = 350.0f, kPlazaHeight = 90.0f;

// The height of the ground at (x, z) before it is cut into the 64 x 64 squares.
inline float Height(float x, float z) {
    const float r = std::hypot(x, z);
    float h = -Rise(r, 3300.0f, 3900.0f) * 760.0f;                       // the rim of the island: cliffs down into the sea
    const float pond = 1.0f - Rise(std::hypot(x - kPondX, z - kPondZ), 150.0f, 950.0f);
    h -= 460.0f * pond;                                                  // the pond: water level is -227, so about 550 wide
    float top = h;
    auto raise = [&top](float f) { if (f > 0.0f) top = std::max(top, f); };   // a feature only lifts the ground where it is
    // North: a plateau with three ramps up to it (17, 27 and 56 degrees: the last is too steep to walk).
    raise(kPlateauHeight * Box(x, z, -1500.0f, 1500.0f, 2300.0f, 2900.0f, 150.0f));
    raise(kPlateauHeight * Lin(z, 1300.0f, 2300.0f) * Box(x, z, -1450.0f, -750.0f, 1000.0f, 2400.0f, 60.0f));
    raise(kPlateauHeight * Lin(z, 1700.0f, 2300.0f) * Box(x, z, -500.0f, 200.0f, 1000.0f, 2400.0f, 60.0f));
    raise(kPlateauHeight * Lin(z, 2250.0f, 2300.0f) * Box(x, z, 450.0f, 1150.0f, 1000.0f, 2400.0f, 60.0f));
    // East: a sheer cliff over a plateau, and a ramp (26 degrees) up it from the south.
    raise(kCliffHeight * Box(x, z, 2500.0f, 3200.0f, -700.0f, 700.0f, 120.0f));
    raise(kCliffHeight * Lin(z, -1500.0f, -700.0f) * Box(x, z, 2550.0f, 3050.0f, -1600.0f, 0.0f, 100.0f));
    // West: the loot plaza, a raised square with gentle sides.
    raise(kPlazaHeight * Box(x, z, -2900.0f, -1700.0f, -300.0f, 900.0f, 300.0f));
    // South-east: a kicker (a ramp that ends in a drop) and a bumpy road.
    raise(200.0f * Lin(z, -1300.0f, -700.0f) * Box(x, z, 1300.0f, 2100.0f, -1400.0f, -700.0f, 120.0f));
    top += 40.0f * std::sin(x / 140.0f) * std::sin(z / 140.0f) * Box(x, z, 100.0f, 1100.0f, -2600.0f, -1800.0f, 200.0f);
    // North-west: the glider hill, a cone with a flat top that dies away before the sea.
    const float cone = std::min(kHillTop, 0.6f * (900.0f - std::hypot(x - kHillX, z - kHillZ)));   // 31 degrees, with a flat top
    raise(cone * (1.0f - Rise(r, 3100.0f, 3500.0f)));
    return top;
}

// ---- colours and ground cover ----------------------------------------------------------------------------------------------------------
inline float Noise(float x, float z) {   // cheap and repeatable: a little variation on the grass
    const float v = std::sin(x * 0.0173f + std::sin(z * 0.0091f) * 2.0f) * std::sin(z * 0.0211f + 1.3f) + std::sin((x + z) * 0.0063f);
    return 0.5f * v;
}
inline bool InPlaza(float x, float z) { return x > -2900.0f && x < -1700.0f && z > -300.0f && z < 900.0f; }
inline float PadDistance(float x, float z) { return std::hypot(x - 2300.0f, z - 1900.0f); }
inline float LotDistance(float x, float z) { return std::hypot(x - 1700.0f, z + 1900.0f); }

struct Rgb { uint8_t r, g, b; };
inline Rgb Colour(float x, float z) {
    const float h = Height(x, z);
    // How steep it is here: the rise over 60 units.
    const float sx = (Height(x + 60.0f, z) - Height(x - 60.0f, z)) / 120.0f, sz = (Height(x, z + 60.0f) - Height(x, z - 60.0f)) / 120.0f;
    const float slope = std::hypot(sx, sz);
    float r = 96, g = 152, b = 64;                                       // grass
    const float n = Noise(x, z);
    r += 7 * n; g += 9 * n; b += 5 * n;
    if (h < kWaterY) {                                                   // the sea bed and the bottom of the pond
        const float d = Clamp01((static_cast<float>(kWaterY) - h) / 260.0f);
        r = 205 - 150 * d; g = 190 - 100 * d; b = 135 - 15 * d;
    } else if (h < kWaterY + 70.0f) {                                    // the beach
        const float d = Clamp01((h - kWaterY) / 70.0f);
        r = 205 + (r - 205) * d; g = 190 + (g - 190) * d; b = 135 + (b - 135) * d;
    }
    if (InPlaza(x, z)) {                                                 // stone tiles
        const bool seam = std::fmod(std::fabs(x), 200.0f) < 22.0f || std::fmod(std::fabs(z), 200.0f) < 22.0f;
        r = seam ? 120 : 168; g = seam ? 116 : 162; b = seam ? 108 : 150;
    }
    const float pad = PadDistance(x, z);
    if (pad < 750.0f) {                                                  // the boss pad: dark red with rings
        const bool ring = std::fmod(pad, 250.0f) < 24.0f;
        r = ring ? 190 : 112; g = ring ? 70 : 54; b = ring ? 60 : 56;
    }
    const float lot = LotDistance(x, z);
    if (lot < 800.0f) {                                                  // the cart lot: packed earth with a grey ring road
        const bool road = lot > 430.0f && lot < 600.0f;
        r = road ? 98 : 158; g = road ? 98 : 128; b = road ? 104 : 92;
    }
    if (std::hypot(x - kSpawnX, z - kSpawnZ) < 380.0f) {                 // the spawn pad
        const bool edge = std::hypot(x - kSpawnX, z - kSpawnZ) > 340.0f;
        r = edge ? 226 : 210; g = edge ? 200 : 204; b = edge ? 120 : 168;
    }
    if (slope > 0.42f) {                                                 // cliffs and ramps too steep to walk: rock
        const float k = Clamp01((slope - 0.42f) / 0.3f);
        r += (146 - r) * k; g += (104 - g) * k; b += (84 - b) * k;
    }
    if (h >= kWaterY) {                                                  // a faint grid, a square every 500 units, to judge distances by
        const bool line = std::fmod(std::fabs(x), 500.0f) < 30.0f || std::fmod(std::fabs(z), 500.0f) < 30.0f;
        if (line) { r *= 0.86f; g *= 0.86f; b *= 0.86f; }
    }
    auto b8 = [](float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f)); };
    return { b8(r), b8(g), b8(b) };
}

// What the ground is for the scenery (fortnite::Cover): 0 water, 1 meadow (grass blades), 2 woods (trees), 3 paving, 4 bare ground.
inline char CoverCode(float x, float z) {
    const float h = Height(x, z);
    if (h < kWaterY) return '0';
    if (InPlaza(x, z) || PadDistance(x, z) < 750.0f || LotDistance(x, z) < 800.0f) return '3';
    if (std::hypot(x - 900.0f, z - 900.0f) < 450.0f) return '2';                                   // the grove
    if (std::hypot(x + 700.0f, z - 1000.0f) < 650.0f || std::hypot(x - kHillX, z - kHillZ) < 900.0f) return '1';   // a meadow, and the hill
    return '4';
}

struct Data {
    std::vector<int16_t> heights;   // (kCells + 1)^2
    std::vector<uint8_t> colours;   // (kCells * kSub + 1)^2 * 3
    std::string cover;              // kCoverCells^2 characters
};
inline constexpr int kCoverSquares = 256;   // the same as fortnite_data::kCoverCells

inline Data Build() {
    Data d;
    const int verts = kCells + 1;
    d.heights.resize(static_cast<size_t>(verts) * verts);
    const float cellX = 2.0f * kHalfX / kCells, cellZ = 2.0f * kHalfZ / kCells;
    for (int j = 0; j < verts; j++)
        for (int i = 0; i < verts; i++)
            d.heights[static_cast<size_t>(j) * verts + i] = static_cast<int16_t>(std::lround(std::clamp(Height(-kHalfX + cellX * i, -kHalfZ + cellZ * j), -900.0f, 3000.0f)));
    const int fine = kCells * kSub + 1;
    d.colours.resize(static_cast<size_t>(fine) * fine * 3);
    for (int j = 0; j < fine; j++)
        for (int i = 0; i < fine; i++) {
            const Rgb c = Colour(-kHalfX + cellX * i / kSub, -kHalfZ + cellZ * j / kSub);
            uint8_t* o = &d.colours[(static_cast<size_t>(j) * fine + i) * 3];
            o[0] = c.r; o[1] = c.g; o[2] = c.b;
        }
    d.cover.resize(static_cast<size_t>(kCoverSquares) * kCoverSquares);
    for (int j = 0; j < kCoverSquares; j++)
        for (int i = 0; i < kCoverSquares; i++)
            d.cover[static_cast<size_t>(j) * kCoverSquares + i] = CoverCode(-kHalfX + (i + 0.5f) * 2.0f * kHalfX / kCoverSquares, -kHalfZ + (j + 0.5f) * 2.0f * kHalfZ / kCoverSquares);
    return d;
}
inline const Data& Terrain() { static const Data d = Build(); return d; }

} // namespace sandbox
} // namespace royale
