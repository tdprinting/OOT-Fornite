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

// Boulders come in shapes (see meshes.h, Boulder) and sizes, both read from bits of the prop's rotation, so every client draws the same
// boulder without anything more being sent. BoulderTop is how high you stand on one of full size.
constexpr int kBoulderShapes = 6;
constexpr int BoulderShape(uint16_t rot) { return static_cast<int>((rot >> 4) % kBoulderShapes); }
constexpr float BoulderScale(uint16_t rot) { return 0.86f + 0.28f * static_cast<float>((rot * 40503u >> 12) & 15u) / 15.0f; }
constexpr float BoulderHeight(int shape) {   // how tall the model stands
    switch (shape) {
        case 1: return 176.0f;   // standing slab
        case 2: return 60.0f;    // table rock
        case 3: return 100.0f;   // split
        case 4: return 140.0f;   // stack
        case 5: return 84.0f;    // huddle
        default: return 96.0f;   // dome
    }
}
constexpr float BoulderTop(int shape) { return BoulderHeight(shape) * 0.92f; }   // a little below the very top: the crown is rounded
// A rotation that gives a boulder of the wanted shape (and an otherwise random turn and size).
inline uint16_t RotForShape(Rng& rng, int shape) {
    const uint32_t r = rng.Below(0x10000), turns = (r >> 4) / kBoulderShapes * kBoulderShapes + static_cast<uint32_t>(shape % kBoulderShapes);
    return static_cast<uint16_t>(((turns > 0xFFFu ? turns - kBoulderShapes : turns) << 4) | (r & 15u));
}

// Loose scenery over the whole map. `clear` are circles kept free of it (the towns, see PoiClearings). Boulders gathered into shapes come
// on top of these (poi.h, AddFormation).
inline std::vector<Prop> GenerateProps(uint64_t seed, Circle map, int count, const PlacementFn& valid = nullptr, const std::vector<Circle>* clear = nullptr) {
    Rng rng(seed ^ 0x70726F70ull); // "prop"
    std::vector<Prop> out;
    out.reserve(count);
    for (int n = 0; n < count; n++) {
        const uint32_t roll = rng.Below(100);
        // Lots of bushes and rocks: the things you cut and break for rupees and ammo.
        const PropKind kind = roll < 34 ? PropKind::Rock : roll < 46 ? PropKind::Boulder : roll < 92 ? PropKind::Bush : PropKind::Pillar;
        const Vec2 at = RandomPointIn(rng, map, valid, 0.97f);
        const uint16_t rot = static_cast<uint16_t>(rng.Below(0x10000));
        if (valid && !valid(at)) continue; // RandomPointIn gives up after 40 tries; don't put scenery in the void
        bool inTown = false;
        if (clear) for (const Circle& c : *clear) inTown = inTown || Distance(at, c.center) < c.radius;
        if (inTown) continue;
        out.push_back({at, kind, rot});
    }
    return out;
}

} // namespace royale
