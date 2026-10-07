#pragma once
#include "loot.h"
#include "terrain.h"
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

// Is the footprint of a new prop free of the ones already standing (and of `fixed`, the towns' pieces and the like)?
inline bool PropSpotFree(Vec2 at, float radius, const std::vector<Prop>& a, const std::vector<Prop>* b = nullptr) {
    for (const Prop& q : a) if (Distance(at, q.pos) < radius + PropRadius(q.kind) + 10.0f) return false;
    if (b) for (const Prop& q : *b) if (Distance(at, q.pos) < radius + PropRadius(q.kind) + 10.0f) return false;
    return true;
}

// Loose scenery over the whole map, laid out as things that belong together instead of sprinkled evenly: groves of bushes, fields of rocks and
// boulders with the odd standing stone, and (where the ground has a cliff or steep bank, `features`) lines of boulders along its foot. Every piece
// is on ground that exists and is not too steep, and none overlaps another or one of `fixed`. `clear` are circles kept free of it (the towns, see
// PoiClearings). Boulders gathered into shapes come on top of these (poi.h, AddFormation).
inline std::vector<Prop> GenerateProps(uint64_t seed, Circle map, int count, const PlacementFn& valid = nullptr, const std::vector<Circle>* clear = nullptr,
                                       const Ground* ground = nullptr, const TerrainFeatures* features = nullptr, const std::vector<Prop>* fixed = nullptr) {
    Rng rng(seed ^ 0x70726F70ull); // "prop"
    const Ground flat(valid, nullptr);
    const Ground& land = ground ? *ground : flat;
    std::vector<Prop> out;
    out.reserve(count);
    auto inTown = [&](Vec2 at) {
        if (clear) for (const Circle& c : *clear) if (Distance(at, c.center) < c.radius) return true;
        return false;
    };
    auto maxSlope = [](PropKind k) { return k == PropKind::Pillar ? 0.35f : k == PropKind::Boulder ? 0.5f : 0.65f; };
    auto add = [&](Vec2 at, PropKind kind) {
        if (static_cast<int>(out.size()) >= count || Distance(at, map.center) > map.radius * 0.97f || inTown(at)) return false;
        if (!PropSpotFree(at, PropRadius(kind), out, fixed) || !land.Valid(at) || land.Slope(at) > maxSlope(kind)) return false;
        out.push_back({at, kind, static_cast<uint16_t>(rng.Below(0x10000))});
        return true;
    };
    auto around = [&](Vec2 c, float rMin, float rMax) {
        const float a = static_cast<float>(rng.Unit() * 6.283185307179586), r = rMin + (rMax - rMin) * static_cast<float>(rng.Unit());
        return Vec2{c.x + std::cos(a) * r, c.z + std::sin(a) * r};
    };
    const bool cliffs = features && !features->cliffs.empty();
    for (int attempt = 0; attempt < count * 40 && static_cast<int>(out.size()) < count; attempt++) {
        const double roll = rng.Unit();
        if (cliffs && roll < 0.2) {   // boulders strung along the foot of a cliff, with rocks that have rolled down between them
            const CliffFoot& f = features->cliffs[rng.Below(static_cast<uint32_t>(features->cliffs.size()))];
            const Vec2 along = {-f.uphill.z, f.uphill.x};
            const int boulders = 3 + static_cast<int>(rng.Below(3));
            float t = -0.5f * 190.0f * boulders;
            for (int k = 0; k < boulders; k++, t += 190.0f) {
                const float off = 20.0f + 50.0f * static_cast<float>(rng.Unit()), jitter = 40.0f * static_cast<float>(rng.Unit() - 0.5);
                add({f.pos.x + along.x * (t + jitter) - f.uphill.x * off, f.pos.z + along.z * (t + jitter) - f.uphill.z * off}, PropKind::Boulder);
                if (rng.Unit() < 0.7) add({f.pos.x + along.x * (t + 95.0f) - f.uphill.x * (30.0f + 90.0f * static_cast<float>(rng.Unit())),
                                           f.pos.z + along.z * (t + 95.0f) - f.uphill.z * (30.0f + 90.0f * static_cast<float>(rng.Unit()))}, PropKind::Rock);
            }
            continue;
        }
        const Vec2 centre = RandomPointIn(rng, map, valid, 0.97f);
        if (!land.Valid(centre) || land.Slope(centre) > 0.35f || inTown(centre)) continue;
        if (roll < 0.06) {   // a lone rock or bush
            add(centre, rng.Below(2) ? PropKind::Rock : PropKind::Bush);
        } else if (roll < 0.55) {   // a grove: bushes crowded together round one or two rocks
            const int bushes = 4 + static_cast<int>(rng.Below(4));
            for (int k = 0; k < bushes; k++) add(around(centre, 25.0f, 220.0f), PropKind::Bush);
            if (rng.Unit() < 0.5) add(around(centre, 40.0f, 140.0f), PropKind::Rock);
        } else {   // a rock field: a few boulders with smaller rocks about them, and sometimes a standing stone in the middle
            if (rng.Unit() < 0.7) add(centre, PropKind::Pillar);
            if (rng.Unit() < 0.3) add(around(centre, 120.0f, 240.0f), PropKind::Pillar);
            const int boulders = 1 + static_cast<int>(rng.Below(2)), rocks = 3 + static_cast<int>(rng.Below(3));
            for (int k = 0; k < boulders; k++) add(around(centre, 100.0f, 220.0f), PropKind::Boulder);
            for (int k = 0; k < rocks; k++) add(around(centre, 60.0f, 280.0f), PropKind::Rock);
        }
    }
    return out;
}

} // namespace royale
