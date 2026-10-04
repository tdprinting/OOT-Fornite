#pragma once
#include "loot.h"
#include <vector>

namespace royale {

// Scenery scattered over the map to make it denser to fight in: rocks and boulders to hide behind, bushes, stumps. Generated from
// the match seed so the host, every client and the bots' navigation grid agree on exactly where each one is. The game decides
// how to draw each kind; the server only needs the footprint (to keep bots from walking through them).
enum class PropKind : uint8_t { Rock, Boulder, Bush, Pillar, Roof, Count }; // Roof: a cottage roof (no collision), part of a building

struct Prop {
    Vec2 pos;
    PropKind kind;
    uint16_t rot; // binary angle, 0x10000 = a full turn
};

// Footprint radius in map units; 0 means bots can walk through it.
constexpr float PropRadius(PropKind k) {
    switch (k) {
        case PropKind::Rock: return 28.0f;
        case PropKind::Boulder: return 75.0f;
        case PropKind::Pillar: return 40.0f;
        default: return 0.0f;
    }
}

inline std::vector<Prop> GenerateProps(uint64_t seed, Circle map, int count, const PlacementFn& valid = nullptr) {
    Rng rng(seed ^ 0x70726F70ull); // "prop"
    std::vector<Prop> out;
    out.reserve(count);
    for (int n = 0; n < count; n++) {
        const uint32_t roll = rng.Below(100);
        const PropKind kind = roll < 45 ? PropKind::Rock : roll < 62 ? PropKind::Boulder : roll < 92 ? PropKind::Bush : PropKind::Pillar;
        const Vec2 at = RandomPointIn(rng, map, valid, 0.97f);
        if (valid && !valid(at)) continue; // RandomPointIn gives up after 40 tries; don't put scenery in the void
        out.push_back({at, kind, static_cast<uint16_t>(rng.Below(0x10000))});
    }
    return out;
}

} // namespace royale
