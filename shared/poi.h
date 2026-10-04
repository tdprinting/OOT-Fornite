#pragma once
#include "props.h"
#include <vector>

namespace royale {

// Points of interest: named places with a stone building, a small cave and some ruins, where most of the chests are. The layout comes
// from the match seed (like the scenery), the host sends it to everyone, and the pieces are ordinary props, so the bots' navigation
// grid treats the walls as solid and the buildings can be walked into through their doors.
inline const char* const kPoiNames[] = {
    "Deku Dew Zoo",         "Goron Groove Lagoon",    "Zora Snore Shore",       "Gerudo Voodoo Rendezvous",
    "Kokiri Breezy Wheezy", "Hylian Billion Pavilion", "Skulltula Hullabaloo",  "Bombchu Kaboom Room",
    "Poe Show Shack",       "Octorok Rock Dock",       "Cucco Mucky Plucky",    "Navi Gravy Bay",
    "Ganon's Bacon Cabin",  "Moblin Cobblin' Wobblin'", "Tektite Tight Bite",   "Lon Lon Gone Wrong",
};
constexpr int kPoiNameCount = 16;

struct Poi {
    uint8_t name = 0; // index into kPoiNames
    Vec2 center;
    float radius = 0;
};

struct PoiLayout {
    std::vector<Poi> pois;
    std::vector<Prop> props;     // walls, cave rocks and ruins
    std::vector<Vec2> lootSpots; // where the chests go
};

namespace poi_detail {
inline Vec2 Rotated(Vec2 local, float angle, Vec2 origin) {
    const float c = std::cos(angle), s = std::sin(angle);
    return {origin.x + local.x * c - local.z * s, origin.z + local.x * s + local.z * c};
}
inline uint16_t BinAngle(float radians) { return static_cast<uint16_t>(static_cast<int32_t>(radians * (32768.0f / 3.14159265f)) & 0xFFFF); }
} // namespace poi_detail

// A walled courtyard with a door in the front wall.
inline void AddHouse(PoiLayout& out, Rng& rng, Vec2 at, float angle, const PlacementFn& valid) {
    const float w = 360.0f, d = 280.0f, step = 62.0f;
    auto piece = [&](float lx, float lz) {
        const Vec2 p = poi_detail::Rotated({lx, lz}, angle, at);
        if (!valid || valid(p)) out.props.push_back({p, PropKind::Pillar, static_cast<uint16_t>(rng.Below(0x10000))});
    };
    for (float t = -w * 0.5f; t <= w * 0.5f + 0.1f; t += step) {
        piece(t, -d * 0.5f);                       // back wall
        if (std::fabs(t) >= 100.0f) piece(t, d * 0.5f); // front wall, with a door gap in the middle
    }
    for (float t = -d * 0.5f + step; t < d * 0.5f - 1.0f; t += step) { piece(-w * 0.5f, t); piece(w * 0.5f, t); }
    // The roof stands on the posts. Its rotation is the building's, as the game's Y rotation turns the opposite way to ours.
    if (!valid || valid(at)) out.props.push_back({at, PropKind::Roof, poi_detail::BinAngle(-angle)});
    const Vec2 spots[] = {{-110, -70}, {110, -70}, {0, 20}, {-100, 70}};
    for (const Vec2& s : spots) out.lootSpots.push_back(poi_detail::Rotated(s, angle, at));
}

// A horseshoe of boulders, open at the front, with chests at the back.
inline void AddCave(PoiLayout& out, Rng& rng, Vec2 at, float angle, const PlacementFn& valid) {
    const float r = 175.0f;
    for (int i = 0; i < 8; i++) {
        const float a = -3.14159265f * 0.62f + (3.14159265f * 1.24f) * i / 7.0f; // wraps around the back
        const Vec2 p = poi_detail::Rotated({std::sin(a) * r, -std::cos(a) * r}, angle, at);
        if (!valid || valid(p)) out.props.push_back({p, PropKind::Boulder, static_cast<uint16_t>(rng.Below(0x10000))});
    }
    for (int side = -1; side <= 1; side += 2) { // standing stones guarding the mouth
        const Vec2 p = poi_detail::Rotated({side * 120.0f, r * 0.95f}, angle, at);
        if (!valid || valid(p)) out.props.push_back({p, PropKind::Pillar, static_cast<uint16_t>(rng.Below(0x10000))});
    }
    out.lootSpots.push_back(poi_detail::Rotated({-45, -85}, angle, at));
    out.lootSpots.push_back(poi_detail::Rotated({50, -60}, angle, at));
}

// Broken walls and a fallen boulder.
inline void AddRuins(PoiLayout& out, Rng& rng, Vec2 at, float angle, const PlacementFn& valid) {
    const float xs[] = {-170, -108, -46, 80, 142, 204};
    for (float x : xs) {
        const Vec2 p = poi_detail::Rotated({x, 0}, angle, at);
        if (!valid || valid(p)) out.props.push_back({p, PropKind::Pillar, static_cast<uint16_t>(rng.Below(0x10000))});
    }
    const Vec2 b = poi_detail::Rotated({0, 38}, angle, at);
    if (!valid || valid(b)) out.props.push_back({b, PropKind::Boulder, static_cast<uint16_t>(rng.Below(0x10000))});
    out.lootSpots.push_back(poi_detail::Rotated({-20, -70}, angle, at));
    out.lootSpots.push_back(poi_detail::Rotated({120, 60}, angle, at));
}

inline PoiLayout GeneratePois(uint64_t seed, Circle map, int count, const PlacementFn& valid = nullptr) {
    PoiLayout out;
    Rng rng(seed ^ 0x706F69ull); // "poi"
    // The names, shuffled for this match.
    uint8_t names[kPoiNameCount];
    for (int i = 0; i < kPoiNameCount; i++) names[i] = static_cast<uint8_t>(i);
    for (int i = kPoiNameCount - 1; i > 0; i--) std::swap(names[i], names[rng.Below(static_cast<uint32_t>(i + 1))]);

    // Laid out the way battle royale maps are: a big landmark in the middle, a ring of towns around it and a wider ring near the edge,
    // each with room around it so there are open stretches (full of rocks to hide behind) between them to run across.
    const float poiRadius = 480.0f;
    const float minGap = (std::max)(poiRadius * 2.2f, map.radius * 0.26f);
    const int wanted = (std::min)(count, kPoiNameCount);
    // The centre landmark keeps its name; the rest of the names are dealt out in shuffled order.
    for (int i = 0; i < kPoiNameCount; i++) if (names[i] == 5) std::swap(names[0], names[i]); // "Hylian Billion Pavilion"
    struct Slot { float ring; int index, of; float phase; };
    std::vector<Slot> slots = {{0.0f, 0, 1, 0.0f}};
    const float phase = static_cast<float>(rng.Unit() * 6.2831853);
    for (int i = 0; i < 5; i++) slots.push_back({0.50f, i, 5, phase});
    for (int i = 0; i < 6; i++) slots.push_back({0.80f, i, 6, phase + 0.5f});
    for (const Slot& slot : slots) {
        if (static_cast<int>(out.pois.size()) >= wanted) break;
        bool placed = false;
        for (int attempt = 0; attempt < 24 && !placed; attempt++) {
            const float angle = phase * 0.0f + slot.phase + slot.index * 6.2831853f / slot.of + static_cast<float>(rng.Unit() - 0.5) * 0.35f;
            const float dist = map.radius * slot.ring * (1.0f + static_cast<float>(rng.Unit() - 0.5) * 0.12f);
            const Vec2 c = {map.center.x + std::cos(angle) * dist, map.center.z + std::sin(angle) * dist};
            if (Distance(c, map.center) > map.radius - poiRadius * 1.1f) continue;
            bool ok = !valid || valid(c);
            for (int k = 0; ok && k < 8; k++) { // the ground under the whole area has to exist
                const float a = k * 0.785398f;
                if (valid && !valid({c.x + std::cos(a) * poiRadius * 0.7f, c.z + std::sin(a) * poiRadius * 0.7f})) ok = false;
            }
            for (const Poi& other : out.pois) if (Distance(other.center, c) < minGap) ok = false;
            if (!ok) continue;
            placed = true;
            Poi p;
            p.name = names[out.pois.size()];
            p.center = c;
            p.radius = poiRadius;
            out.pois.push_back(p);

            const float base = static_cast<float>(rng.Unit() * 6.2831853);
            AddHouse(out, rng, c, base, valid);
            const float a1 = base + 1.1f + static_cast<float>(rng.Unit()) * 0.8f;
            AddCave(out, rng, {c.x + std::cos(a1) * 310.0f, c.z + std::sin(a1) * 310.0f}, a1 + 3.14159265f, valid);
            const float a2 = a1 + 3.14159265f + static_cast<float>(rng.Unit() - 0.5) * 1.0f;
            AddRuins(out, rng, {c.x + std::cos(a2) * 300.0f, c.z + std::sin(a2) * 300.0f}, a2, valid);
            if (slot.ring == 0.0f) { // the landmark in the middle is bigger: a second building and more ruins
                AddHouse(out, rng, {c.x + std::cos(base + 2.1f) * 330.0f, c.z + std::sin(base + 2.1f) * 330.0f}, base + 0.8f, valid);
                AddRuins(out, rng, {c.x + std::cos(base + 4.2f) * 330.0f, c.z + std::sin(base + 4.2f) * 330.0f}, base + 2.3f, valid);
            }
        }
    }
    // Keep only the chest spots that are on real ground.
    std::vector<Vec2> spots;
    for (const Vec2& s : out.lootSpots) if (!valid || valid(s)) spots.push_back(s);
    out.lootSpots = spots;
    return out;
}

} // namespace royale
