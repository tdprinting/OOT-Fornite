#pragma once
#include "hyrule_third_storm_data.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
// Hyrule Before the Third Storm's floor, water and collision (no game types), so shared/fortnite_map.h can answer for map 15 without the
// layout's dependencies. shared/hyrule_third_storm_layout.h builds the rest on these.
//
// The exporter writes the big tables (the walk grid, the minimap, the collision) as base64 text in pieces: a compiler handles tens of
// megabytes of string literal easily, but not millions of numbers in braces. Ground() decodes them once, on first use (tens of
// milliseconds), and keeps them for the life of the program.
namespace royale { namespace hyrule_third_storm {

// base64 pieces -> bytes. Throws nothing: a short or damaged table decodes short, and the size checks below catch it.
template <size_t N> std::vector<uint8_t> DecodeBase64(const char* const (&pieces)[N], size_t bytes) {
    static const auto table = [] {
        std::vector<int8_t> t(256, -1);
        const char* abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) t[static_cast<uint8_t>(abc[i])] = static_cast<int8_t>(i);
        return t;
    }();
    std::vector<uint8_t> out;
    out.reserve(bytes);
    uint32_t acc = 0; int bits = 0;
    for (const char* piece : pieces)
        for (const char* c = piece; *c; ++c) {
            const int v = table[static_cast<uint8_t>(*c)];
            if (v < 0) continue;   // '=' padding
            acc = (acc << 6) | static_cast<uint32_t>(v); bits += 6;
            if (bits >= 8) { bits -= 8; out.push_back(static_cast<uint8_t>(acc >> bits)); }
        }
    out.resize(std::min(out.size(), bytes));
    return out;
}

struct Vertex { int16_t x, y, z; };
struct Triangle { uint32_t a, b, c; int16_t nx, ny, nz; int32_t dist; uint8_t surface; };   // dist is 32 bit (patches/0040)
// The collision is big and only the game (and the map's test) needs it, so it lives in hyrule_third_storm_collision.h, decoded by
// hyrule_third_storm_draw.h, which sets this hook when it is included. Without it there is no collision (the server never needs any).
inline void (*gLoadCollision)(std::vector<Vertex>&, std::vector<Triangle>&) = nullptr;
struct GroundTables {
    std::vector<int16_t> walk;        // kGrid x kGrid floor heights (kNoFloor: none)
    std::vector<uint8_t> flags;       // kGrid x kGrid: 1 water, 2 too steep
    std::vector<uint8_t> colours;     // the minimap: 385 x 385 x (r, g, b)
    bool ok = false;                  // every table the size it should be
};
// zigzag varints (the exporter's varints())
inline bool ReadVarint(const std::vector<uint8_t>& b, size_t* k, int64_t* v) {
    uint64_t z = 0; int shift = 0;
    while (*k < b.size()) {
        const uint8_t c = b[(*k)++];
        z |= static_cast<uint64_t>(c & 0x7F) << shift; shift += 7;
        if (!(c & 0x80)) { *v = static_cast<int64_t>(z >> 1) ^ -static_cast<int64_t>(z & 1); return true; }
        if (shift > 63) return false;
    }
    return false;
}
inline const GroundTables& Ground() {
    static const GroundTables t = [] {
        GroundTables g;
        const auto w = DecodeBase64(kWalkB64, kWalkB64Bytes);
        g.walk.reserve(kWalkCount);
        size_t k = 0; int64_t d = 0, h = 0;
        while (g.walk.size() < kWalkCount && ReadVarint(w, &k, &d)) { h += d; g.walk.push_back(static_cast<int16_t>(h)); }
        const auto f = DecodeBase64(kWalkFlagsB64, kWalkFlagsB64Bytes);
        g.flags.reserve(kWalkCount);
        for (size_t i = 0; i + 1 < f.size() && g.flags.size() < kWalkCount;) {
            const uint8_t value = f[i++]; int64_t n = 0;
            size_t j = i; if (!ReadVarint(f, &j, &n)) break; i = j;
            g.flags.insert(g.flags.end(), static_cast<size_t>(std::max<int64_t>(0, n)), value);
        }
        g.colours = DecodeBase64(kColoursB64, kColoursB64Bytes);
        g.ok = g.walk.size() == static_cast<size_t>(kGrid) * kGrid && g.flags.size() == g.walk.size() && g.colours.size() == 385u * 385u * 3u;
        if (!g.ok) {   // keep every lookup in range whatever happened
            g.walk.assign(static_cast<size_t>(kGrid) * kGrid, 0); g.flags.assign(g.walk.size(), 0); g.colours.assign(385u * 385u * 3u, 90);
        }
        return g;
    }();
    return t;
}

inline bool OnMap(float x, float z) { return std::fabs(x) < kHalf && std::fabs(z) < kHalf; }
inline bool GridPoint(float x, float z, int* i, int* j) {
    const float o = -kStep * (kGrid - 1) * 0.5f;
    *i = static_cast<int>(std::lround((x - o) / kStep)); *j = static_cast<int>(std::lround((z - o) / kStep));
    return *i >= 0 && *j >= 0 && *i < kGrid && *j < kGrid;
}
// The floor at (x, z): the lowest one with head room, so inside a house it is the ground floor, not the roof. Between grid points the corners
// on the same level as the nearest one are blended (a corner more than a storey away is a wall top or a ledge). -3000 where there is none.
inline float GroundY(float x, float z) {
    const auto& walk = Ground().walk;
    const float o = -kStep * (kGrid - 1) * 0.5f;
    const float fx = std::clamp((x - o) / kStep, 0.0f, kGrid - 1.001f), fz = std::clamp((z - o) / kStep, 0.0f, kGrid - 1.001f);
    const int i = static_cast<int>(fx), j = static_cast<int>(fz); const float u = fx - i, v = fz - j;
    const int16_t c[4] = {walk[j * kGrid + i], walk[j * kGrid + i + 1], walk[(j + 1) * kGrid + i], walk[(j + 1) * kGrid + i + 1]};
    const float w[4] = {(1 - u) * (1 - v), u * (1 - v), (1 - u) * v, u * v};
    const int nearest = (u < 0.5f ? 0 : 1) + (v < 0.5f ? 0 : 2);
    int ref = nearest;
    if (c[ref] == kNoFloor) for (int k = 0; k < 4; ++k) if (c[k] != kNoFloor) { ref = k; break; }
    if (c[ref] == kNoFloor) return -3000.0f;
    float sum = 0, ws = 0;
    for (int k = 0; k < 4; ++k)
        if (c[k] != kNoFloor && std::abs(c[k] - c[ref]) < 90) { sum += w[k] * c[k]; ws += w[k]; }
    return ws > 0 ? sum / ws : static_cast<float>(c[ref]);
}
inline bool HasFloor(float x, float z) {
    int i, j;
    return OnMap(x, z) && GridPoint(x, z, &i, &j) && Ground().walk[j * kGrid + i] != kNoFloor;
}
// Walk-grid flags at (x, z): 1 water, 2 too steep. 2 outside the map.
inline int FlagsAt(float x, float z) {
    int i, j;
    return OnMap(x, z) && GridPoint(x, z, &i, &j) ? Ground().flags[j * kGrid + i] : 2;
}
// The water surface: the highest pool over (x, z), or far under the map where there is none (no sea round this map).
inline float WaterY(float x, float z) {
    float y = -30000.0f;
    for (const auto& b : kPools) if (x >= b.x0 && x <= b.x1 && z >= b.z0 && z <= b.z1) y = (std::max)(y, b.y);
    return y;
}
// How flat the floor is at (x, z) (1 flat), from the grid's neighbours on the same level.
inline float GroundUp(float x, float z) {
    if (!HasFloor(x, z)) return 0.0f;
    const float sx = (GroundY(x + kStep, z) - GroundY(x - kStep, z)) / (2 * kStep), sz = (GroundY(x, z + kStep) - GroundY(x, z - kStep)) / (2 * kStep);
    if (std::fabs(sx * kStep) > 90.0f || std::fabs(sz * kStep) > 90.0f) return (FlagsAt(x, z) & 2) ? 0.0f : 1.0f;   // a ledge next door, not a slope here
    return 1.0f / std::sqrt(1.0f + sx * sx + sz * sz);
}
inline constexpr size_t kPoolCount = sizeof(kPools) / sizeof(kPools[0]);
inline constexpr size_t kLootCount = sizeof(kLootSites) / sizeof(kLootSites[0]);
}}  // namespace royale::hyrule_third_storm
