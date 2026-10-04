#pragma once
#include "loot.h"
#include <vector>

namespace royale {

// Scenery scattered over the map to make it denser to fight in: rocks and boulders to hide behind, bushes, stumps. Generated from
// the match seed so the host, every client and the bots' navigation grid agree on exactly where each one is. The game decides
// how to draw each kind; the server only needs the footprint (to keep bots from walking through them).
enum class PropKind : uint8_t { Rock, Boulder, Bush, Pillar, Roof, PlatformLow, PlatformMid, PlatformHigh, Count }; // Roof: a cottage roof (no collision), part of a building;
// the platforms are 150 wide stone blocks 60, 120 and 180 tall: stacked side by side they are a staircase that needs jumping and clambering

struct Prop {
    Vec2 pos;
    PropKind kind;
    uint16_t rot; // binary angle, 0x10000 = a full turn
};

constexpr bool IsPlatform(PropKind k) { return k == PropKind::PlatformLow || k == PropKind::PlatformMid || k == PropKind::PlatformHigh; }
constexpr float PlatformHeight(PropKind k) { return k == PropKind::PlatformLow ? 60.0f : k == PropKind::PlatformMid ? 120.0f : k == PropKind::PlatformHigh ? 180.0f : 0.0f; }
constexpr float kPlatformHalf = 75.0f; // half the width of a platform: they are always square and line up with the map's axes

// Footprint radius in map units; 0 means bots can walk through it.
constexpr float PropRadius(PropKind k) {
    switch (k) {
        case PropKind::Rock: return 28.0f;
        case PropKind::Boulder: return 75.0f;
        case PropKind::Pillar: return 40.0f;
        case PropKind::PlatformLow: case PropKind::PlatformMid: case PropKind::PlatformHigh: return 105.0f;
        default: return 0.0f;
    }
}

inline std::vector<Prop> GenerateProps(uint64_t seed, Circle map, int count, const PlacementFn& valid = nullptr) {
    Rng rng(seed ^ 0x70726F70ull); // "prop"
    std::vector<Prop> out;
    out.reserve(count);
    for (int n = 0; n < count; n++) {
        const uint32_t roll = rng.Below(100);
        // Lots of bushes and rocks: the things you cut and break for rupees and ammo.
        const PropKind kind = roll < 34 ? PropKind::Rock : roll < 46 ? PropKind::Boulder : roll < 92 ? PropKind::Bush : PropKind::Pillar;
        const Vec2 at = RandomPointIn(rng, map, valid, 0.97f);
        if (valid && !valid(at)) continue; // RandomPointIn gives up after 40 tries; don't put scenery in the void
        out.push_back({at, kind, static_cast<uint16_t>(rng.Below(0x10000))});
    }
    return out;
}

} // namespace royale
