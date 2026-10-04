#pragma once
#include "map.h"
#include "props.h"
#include <vector>

namespace royale {

// Points of interest: named places with a stone building, a small cave and some ruins, where most of the chests are. The layout comes
// from the match seed (like the scenery), the host sends it to everyone, and the pieces are ordinary props, so the bots' navigation
// grid treats the walls as solid and the buildings can be walked into through their doors.
struct Poi {
    uint8_t name = 0; // index into kPoiNames
    Vec2 center;
    float radius = 0;
};

struct PoiLayout {
    std::vector<Poi> pois;
    std::vector<Prop> props;     // walls, cave rocks and ruins
    std::vector<Vec2> lootSpots; // where the chests go
    std::vector<Vec2> bossSpots; // the caves: where a mini boss may stand guard
    std::vector<ChestSite> sites; // climbs and hideaways (see AddClimb, GenerateWilds)
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
    out.bossSpots.push_back(poi_detail::Rotated({0, -10}, angle, at));
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

// A staircase of three platforms side by side along one of the map's axes (dir 0..3), each a step higher than the last, with a chest on the top
// one. Needs jumping and clambering to reach: the point of it. Bots can't climb, so they leave these to the players.
inline void AddClimb(PoiLayout& out, Vec2 at, int dir, const PlacementFn& valid) {
    static const float dx[4] = {1, 0, -1, 0}, dz[4] = {0, 1, 0, -1};
    const PropKind steps[3] = {PropKind::PlatformLow, PropKind::PlatformMid, PropKind::PlatformHigh};
    Vec2 pieces[3];
    for (int i = 0; i < 3; i++) {
        pieces[i] = {at.x + dx[dir & 3] * kPlatformHalf * 2.0f * i, at.z + dz[dir & 3] * kPlatformHalf * 2.0f * i};
        if (valid && !valid(pieces[i])) return;                                  // the whole thing has to stand on ground
    }
    for (int i = 0; i < 3; i++) out.props.push_back({pieces[i], steps[i], 0});
    out.sites.push_back({pieces[2], 2});
}

// Climbs standing alone in the open and chests hidden behind big rocks, spread so no two are close together. `props` is the map's scenery so far
// (the boulders to hide behind come from there); `taken` the chest spots already used, which new ones keep their distance from.
inline void GenerateWilds(PoiLayout& out, uint64_t seed, Circle map, const std::vector<Prop>& props, const std::vector<Vec2>& taken, int climbs, int hideaways,
                          const PlacementFn& valid = nullptr) {
    Rng rng(seed ^ 0x77696C64ull); // "wild"
    std::vector<Vec2> spots = taken;
    for (const ChestSite& s : out.sites) spots.push_back(s.pos);
    const float apart = (std::max)(380.0f, map.radius * 0.1f);
    auto far = [&](Vec2 p) {
        for (const Vec2& q : spots) if (Distance(p, q) < apart) return false;
        for (const Poi& poi : out.pois) if (Distance(p, poi.center) < poi.radius * 0.8f) return false;
        return true;
    };
    for (int n = 0, placed = 0; n < climbs * 12 && placed < climbs; n++) {
        const Vec2 at = RandomPointIn(rng, map, valid, 0.85f);
        if (!far(at) || Distance(at, map.center) > map.radius - 500.0f) continue;
        const size_t before = out.props.size();
        AddClimb(out, at, static_cast<int>(rng.Below(4)), valid);
        if (out.props.size() == before) continue;
        if (!far(out.sites.back().pos)) { out.props.resize(before); out.sites.pop_back(); continue; }   // the chest on top has to be well apart too
        spots.push_back(out.sites.back().pos);
        placed++;
    }
    // Hideaways: a chest tucked behind a boulder, on the side facing away from the middle of the map.
    std::vector<size_t> boulders;
    for (size_t i = 0; i < props.size(); i++) if (props[i].kind == PropKind::Boulder) boulders.push_back(i);
    for (size_t i = boulders.size(); i > 1; i--) std::swap(boulders[i - 1], boulders[rng.Below(static_cast<uint32_t>(i))]);
    int placed = 0;
    for (size_t index : boulders) {
        if (placed >= hideaways) break;
        const Prop& b = props[index];
        const float dx = b.pos.x - map.center.x, dz = b.pos.z - map.center.z, d = (std::max)(1.0f, std::hypot(dx, dz));
        const Vec2 at = {b.pos.x + dx / d * 130.0f, b.pos.z + dz / d * 130.0f};
        if (Distance(at, map.center) > map.radius - 60.0f || (valid && !valid(at)) || !far(at)) continue;
        out.sites.push_back({at, 1});
        spots.push_back(at);
        placed++;
    }
}

inline PoiLayout GeneratePois(uint64_t seed, Circle map, int count, const PlacementFn& valid = nullptr, int mapId = 0) {
    PoiLayout out;
    Rng rng(seed ^ 0x706F69ull); // "poi"
    // The names, shuffled for this match.
    const int nameBase = ClampMap(mapId) * kNamesPerMap;
    uint8_t names[kNamesPerMap];
    for (int i = 0; i < kNamesPerMap; i++) names[i] = static_cast<uint8_t>(nameBase + i);
    for (int i = kNamesPerMap - 1; i > 1; i--) std::swap(names[i], names[1 + rng.Below(static_cast<uint32_t>(i))]); // name 0 stays the landmark's

    // Laid out the way battle royale maps are: a big landmark in the middle, a ring of towns around it and a wider ring near the edge,
    // each with room around it so there are open stretches (full of rocks to hide behind) between them to run across.
    const float poiRadius = (std::max)(260.0f, (std::min)(480.0f, map.radius * 0.13f)); // small places get smaller towns
    const float minGap = (std::max)(poiRadius * 2.2f, map.radius * 0.26f);
    const int wanted = (std::min)(count, kNamesPerMap);
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
            AddClimb(out, {c.x + std::cos(base + 3.3f) * 400.0f, c.z + std::sin(base + 3.3f) * 400.0f}, static_cast<int>(rng.Below(4)), valid);
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
