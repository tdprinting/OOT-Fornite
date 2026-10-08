#pragma once
#include "ally.h"
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

namespace poi_detail {
inline void Piece(PoiLayout& out, Rng& rng, PropKind kind, Vec2 local, float angle, Vec2 at, const PlacementFn& valid) {
    const Vec2 p = Rotated(local, angle, at);
    if (!valid || valid(p)) out.props.push_back({p, kind, static_cast<uint16_t>(rng.Below(0x10000))});
}
inline void Spot(PoiLayout& out, Vec2 local, float angle, Vec2 at, const PlacementFn& valid) {
    const Vec2 p = Rotated(local, angle, at);
    if (!valid || valid(p)) out.lootSpots.push_back(p);
}
// A square stepped mound of nine blocks: low corners, middle edges and a high middle, 450 across. The chest on the top is an Epic or better.
inline bool Ziggurat(PoiLayout& out, Vec2 at, const PlacementFn& valid) {
    const float s = kPlatformHalf * 2.0f;
    for (int ix = -1; ix <= 1; ix++) for (int iz = -1; iz <= 1; iz++) if (valid && !valid({at.x + ix * s, at.z + iz * s})) return false;
    for (int ix = -1; ix <= 1; ix++) for (int iz = -1; iz <= 1; iz++) {
        const int edge = (ix == 0) + (iz == 0);
        out.props.push_back({{at.x + ix * s, at.z + iz * s}, edge == 2 ? PropKind::PlatformHigh : edge == 1 ? PropKind::PlatformMid : PropKind::PlatformLow, 0});
    }
    out.sites.push_back({at, 2});
    return true;
}
} // namespace poi_detail

// ---- boulder formations -------------------------------------------------------------------------------------------------------
// Boulders gathered into shapes you notice from a distance and can use in a fight, instead of every one standing alone: a tumbled pile, a
// ridge with a gap to run through, a ring of standing stones with a chest in the middle, a line of flat table rocks to hop along, and a gate
// of two tall slabs. Each piece is an ordinary boulder (solid, and the bots path round it); its shape comes from its rotation (BoulderShape).
enum class Formation : uint8_t { Pile, Ridge, Ring, Steps, Gate, Count };
constexpr int kFormationCount = static_cast<int>(Formation::Count);
enum BoulderLook : int { kDome = 0, kSlab = 1, kTable = 2, kSplit = 3, kStack = 4, kHuddle = 5, kAnyShape = -1 };

namespace poi_detail {
inline void Stone(PoiLayout& out, Rng& rng, int shape, Vec2 local, float angle, Vec2 at, const PlacementFn& valid) {
    const Vec2 p = Rotated(local, angle, at);
    if (valid && !valid(p)) return;
    const int s = shape < 0 ? static_cast<int>(rng.Below(kBoulderShapes)) : shape;
    out.props.push_back({p, PropKind::Boulder, RotForShape(rng, s)});
}
// The map axis (AddClimb's `dir`) that points most directly away from `centre`, so a climb built at `from` runs outward, clear of the town.
inline int OutwardDir(Vec2 from, Vec2 centre) {
    const float dx = from.x - centre.x, dz = from.z - centre.z;
    return std::fabs(dx) >= std::fabs(dz) ? (dx >= 0 ? 0 : 2) : (dz >= 0 ? 1 : 3);
}
inline void Pebble(PoiLayout& out, Rng& rng, Vec2 local, float angle, Vec2 at, const PlacementFn& valid) {
    const Vec2 p = Rotated(local, angle, at);
    if (!valid || valid(p)) out.props.push_back({p, PropKind::Rock, static_cast<uint16_t>(rng.Below(0x10000))});
}
} // namespace poi_detail

// Builds one formation at `at`, turned by `angle`. A chest goes in the ring of standing stones (as a hideaway: a Rare or better).
inline void AddFormation(PoiLayout& out, Rng& rng, Formation what, Vec2 at, float angle, const PlacementFn& valid) {
    using namespace poi_detail;
    const float pi = 3.14159265f;
    switch (what) {
        case Formation::Pile: {    // a stack in the middle with boulders tumbled round it and loose rocks further out
            Stone(out, rng, kStack, {0, 0}, angle, at, valid);
            const int n = 3 + static_cast<int>(rng.Below(2));
            for (int i = 0; i < n; i++) {
                const float a = i * 2.0f * pi / n + rng.Unit() * 0.5f, d = 140.0f + 20.0f * static_cast<float>(rng.Unit());
                static const int shapes[3] = {kDome, kHuddle, kSplit};
                Stone(out, rng, shapes[rng.Below(3)], {std::cos(a) * d, std::sin(a) * d}, angle, at, valid);
            }
            for (int i = 0; i < 3; i++) { const float a = rng.Unit() * 2.0f * pi; Pebble(out, rng, {std::cos(a) * 270.0f, std::sin(a) * 270.0f}, angle, at, valid); }
            break;
        }
        case Formation::Ridge: {   // a curving line of slabs and split stones, with one gap to run through
            const int n = 7, gap = 2 + static_cast<int>(rng.Below(3));
            const float bend = (static_cast<float>(rng.Unit()) - 0.5f) * 0.0012f;
            for (int i = 0; i < n; i++) {
                if (i == gap) continue;
                const float x = (i - (n - 1) * 0.5f) * 150.0f;
                Stone(out, rng, i % 2 ? kSlab : (rng.Below(2) ? kSplit : kDome), {x, bend * x * x}, angle, at, valid);
            }
            Pebble(out, rng, {-540, 60}, angle, at, valid); Pebble(out, rng, {540, -60}, angle, at, valid);
            break;
        }
        case Formation::Ring: {    // standing stones round a chest, open on two sides
            const int n = 9;
            const int gapA = static_cast<int>(rng.Below(n)), gapB = (gapA + 4 + static_cast<int>(rng.Below(2))) % n;
            for (int i = 0; i < n; i++) {
                if (i == gapA || i == gapB) continue;
                const float a = i * 2.0f * pi / n;
                Stone(out, rng, kSlab, {std::cos(a) * 280.0f, std::sin(a) * 280.0f}, 0, at, valid);
            }
            if (!valid || valid(at)) out.sites.push_back({at, 1});
            break;
        }
        case Formation::Steps: {   // flat table rocks you can hop along, ending at a stack
            for (int i = 0; i < 4; i++) Stone(out, rng, kTable, {i * 175.0f - 260.0f, (i % 2 ? 70.0f : -70.0f)}, angle, at, valid);
            Stone(out, rng, kStack, {460, 0}, angle, at, valid);
            Pebble(out, rng, {-430, 40}, angle, at, valid);
            break;
        }
        case Formation::Gate: {    // two tall slabs framing a way through, with piled stones either side
            Stone(out, rng, kSlab, {-115, 0}, angle, at, valid); Stone(out, rng, kSlab, {115, 0}, angle, at, valid);
            Stone(out, rng, kHuddle, {-270, 40}, angle, at, valid); Stone(out, rng, kDome, {270, -30}, angle, at, valid);
            Stone(out, rng, kStack, {-400, -60}, angle, at, valid); Stone(out, rng, kSplit, {410, 50}, angle, at, valid);
            break;
        }
        default: break;
    }
}

// ---- outposts: designed stone structures out in the open --------------------------------------------------------------------
// Built from the climbing blocks (each 150 square, 60, 120 or 180 tall, edge to edge on the map's grid) so each one is a little climb with a
// purpose, rather than blocks dropped at random:
//   Lookout  - a square of four blocks winding up (low, mid, high) to a high corner with the prize, guarded by standing stones
//   Bridge   - a raised walkway: steps up at both ends to a three-block high span across the open, a sniper's perch with a chest in the middle
//   Arena    - a square ring of mid blocks round an open pit with a chest in it, two low steps to get up; fight in it or on the walls
//   Terraces - a broad stair three blocks wide, low to high, with slabs at its sides and the prize along the top
enum class Outpost : uint8_t { Lookout, Bridge, Arena, Terraces, Count };
constexpr int kOutpostCount = static_cast<int>(Outpost::Count);

// Builds one, turned to face `dir` (0-3, quarter turns on the map's grid). All of it has to stand on ground, or nothing is built.
inline bool AddOutpost(PoiLayout& out, Rng& rng, Outpost what, Vec2 at, int dir, const PlacementFn& valid) {
    struct Cell { int x, z; PropKind kind; };
    const PropKind L = PropKind::PlatformLow, M = PropKind::PlatformMid, H = PropKind::PlatformHigh;
    std::vector<Cell> cells;
    Vec2 prize = {0, 0};
    uint8_t bonus = 2;
    std::vector<std::pair<Vec2, int>> stones;   // grid-space position and shape
    switch (what) {
        case Outpost::Lookout:
            cells = {{0, 0, L}, {1, 0, M}, {1, 1, H}, {0, 1, H}};
            prize = {0, 1};
            stones = {{{-1.4f, -0.9f}, 1}, {{2.4f, -0.9f}, 1}, {{2.4f, 2.0f}, 4}, {{-1.4f, 2.0f}, 1}};
            break;
        case Outpost::Bridge:
            cells = {{-3, 0, L}, {-2, 0, M}, {-1, 0, H}, {0, 0, H}, {1, 0, H}, {2, 0, M}, {3, 0, L}};
            prize = {0, 0};
            stones = {{{0, -1.6f}, 3}, {{0, 1.6f}, 5}};
            break;
        case Outpost::Arena:
            for (int x = -1; x <= 2; x++) for (int z = -1; z <= 2; z++) if (x == -1 || x == 2 || z == -1 || z == 2) cells.push_back({x, z, M});
            cells.push_back({-2, -1, L}); cells.push_back({3, 2, L});
            prize = {0.5f, 0.5f};
            bonus = 1;
            break;
        default:   // terraces
            for (int x = -1; x <= 1; x++) { cells.push_back({x, 0, L}); cells.push_back({x, 1, M}); cells.push_back({x, 2, H}); }
            prize = {0, 2};
            stones = {{{-2.2f, 0.5f}, 1}, {{2.2f, 0.5f}, 1}, {{-2.2f, 2.2f}, 4}, {{2.2f, 2.2f}, 3}};
            break;
    }
    const float s = kPlatformHalf * 2.0f;
    auto place = [&](float gx, float gz) {   // grid to map, turned by dir
        float x = gx, z = gz;
        for (int k = 0; k < (dir & 3); k++) { const float t = x; x = -z; z = t; }
        return Vec2{at.x + x * s, at.z + z * s};
    };
    for (const Cell& c : cells) if (valid && !valid(place(static_cast<float>(c.x), static_cast<float>(c.z)))) return false;
    for (const Cell& c : cells) out.props.push_back({place(static_cast<float>(c.x), static_cast<float>(c.z)), c.kind, 0});
    for (const auto& st : stones) {
        const Vec2 p = place(st.first.x, st.first.z);
        if (!valid || valid(p)) out.props.push_back({p, PropKind::Boulder, RotForShape(rng, st.second)});
    }
    out.sites.push_back({place(prize.x, prize.z), bonus});
    return true;
}

// ---- the kinds of town ----------------------------------------------------------------------------------------------------------
// Every place on a map is one of these, dealt out so that neighbours differ, and each has something worth the trip: a climb or a stepped
// mound with an Epic-or-better chest on top, chests indoors, and (at the fort, quarry and ruins) a mini boss standing guard.
constexpr float kTownRadius = 500.0f;   // a town's area: its buildings and cave (its climb stands at the edge, up to 300 further out)
enum class TownKind : uint8_t { Hamlet, Fort, Quarry, Shrine, Ruins, Camp, Count };
constexpr int kTownKindCount = static_cast<int>(TownKind::Count);
inline const char* TownKindName(TownKind k) {
    static const char* names[kTownKindCount] = {"hamlet", "fort", "quarry", "shrine", "ruins", "camp"};
    return names[static_cast<int>(k) % kTownKindCount];
}

inline void BuildTown(PoiLayout& out, Rng& rng, TownKind kind, Vec2 c, float base, const PlacementFn& valid) {
    using namespace poi_detail;
    const float pi = 3.14159265f;
    auto local = [&](float x, float z) { return Rotated({x, z}, base, c); };
    switch (kind) {
        case TownKind::Hamlet: {   // two cottages facing a village green with a well post and hedges, a climb behind
            AddHouse(out, rng, local(-225, -60), base, valid);
            AddHouse(out, rng, local(225, -60), base, valid);
            Piece(out, rng, PropKind::Pillar, {0, 210}, base, c, valid);   // the well
            for (int i = 0; i < 6; i++) Piece(out, rng, PropKind::Bush, {-300.0f + i * 120.0f, 330.0f}, base, c, valid);
            AddClimb(out, local(-120, -380), OutwardDir(local(-120, -380), c), valid);
            Spot(out, {0, 150}, base, c, valid);
            break;
        }
        case TownKind::Fort: {     // a square stockade with a gate front and back, a climb to the lookout inside, and a guard
            const float half = 290.0f, step = 72.0f;
            for (float t = -half; t <= half + 0.1f; t += step) {
                for (int side = -1; side <= 1; side += 2) {
                    if (std::fabs(t) > 75.0f) Piece(out, rng, PropKind::Pillar, {t, side * half}, base, c, valid);   // front and back, gates in the middle
                    if (std::fabs(t) < half - 1.0f) Piece(out, rng, PropKind::Pillar, {side * half, t}, base, c, valid);
                }
            }
            for (int k = 0; k < 4; k++) Stone(out, rng, kStack, {(k & 1 ? 1 : -1) * (half + 95.0f), (k & 2 ? 1 : -1) * (half + 95.0f)}, base, c, valid);   // corner bastions
            AddClimb(out, {c.x - kPlatformHalf * 2.0f, c.z}, 0, valid);   // across the middle along the map's x axis, so it fits inside however the fort is turned
            out.bossSpots.push_back({c.x, c.z + 200.0f});
            Spot(out, {-200, 180}, base, c, valid); Spot(out, {200, 180}, base, c, valid); Spot(out, {200, -200}, base, c, valid); Spot(out, {-200, -200}, base, c, valid);
            break;
        }
        case TownKind::Quarry: {   // a boulder cave with a guard, a ridge of stones behind it, a pile and table rocks to hop across
            AddCave(out, rng, local(0, -60), base + pi, valid);
            AddFormation(out, rng, Formation::Ridge, local(0, -480), base, valid);
            AddFormation(out, rng, Formation::Pile, local(-380, 240), base, valid);
            for (int i = 0; i < 3; i++) Stone(out, rng, kTable, {120.0f + i * 150.0f, 160.0f + (i % 2) * 90.0f}, base, c, valid);
            AddClimb(out, local(380, -40), OutwardDir(local(380, -40), c), valid);
            Spot(out, {200, 330}, base, c, valid); Spot(out, {-120, 380}, base, c, valid);
            break;
        }
        case TownKind::Shrine: {   // a stepped mound with the prize on top, a ring of standing stones round it and hedges outside
            Ziggurat(out, c, valid);
            const int n = 12;
            for (int i = 0; i < n; i++) {
                if (i == 0 || i == n / 2) continue;   // two ways in
                const float a = base + i * 2.0f * pi / n;
                Stone(out, rng, kSlab, {std::cos(a) * 430.0f, std::sin(a) * 430.0f}, 0, c, valid);
            }
            for (int i = 0; i < 8; i++) { const float a = base + (i + 0.5f) * pi / 4.0f; Piece(out, rng, PropKind::Bush, {std::cos(a) * 530.0f, std::sin(a) * 530.0f}, 0, c, valid); }
            Spot(out, {350, 0}, base, c, valid); Spot(out, {-350, 0}, base, c, valid);   // in the two ways in
            Spot(out, {0, 340}, base, c, valid); Spot(out, {0, -340}, base, c, valid);
            break;
        }
        case TownKind::Ruins: {    // broken walls round a graveyard, fallen boulders, a climb up the old tower, and something guarding it
            AddRuins(out, rng, local(0, -260), base, valid);
            AddRuins(out, rng, local(-270, 120), base + pi * 0.5f, valid);
            for (int row = 0; row < 2; row++) for (int col = 0; col < 4; col++) Piece(out, rng, PropKind::Pillar, {-60.0f + col * 110.0f, 80.0f + row * 150.0f}, base, c, valid);
            Stone(out, rng, kSplit, {330, -60}, base, c, valid); Stone(out, rng, kHuddle, {-120, 380}, base, c, valid);
            AddClimb(out, local(260, -340), OutwardDir(local(260, -340), c), valid);
            out.bossSpots.push_back(local(100, 0));
            Spot(out, {380, 200}, base, c, valid);
            break;
        }
        default: {                 // camp: a cottage, a cave in the hillside and a fence of posts with a climb at the end
            AddHouse(out, rng, c, base, valid);
            const float a1 = base + 1.1f + static_cast<float>(rng.Unit()) * 0.8f;
            AddCave(out, rng, {c.x + std::cos(a1) * 330.0f, c.z + std::sin(a1) * 330.0f}, a1 + pi, valid);
            for (int i = 0; i < 5; i++) { const float a = a1 + pi * 0.65f + i * 0.22f; Piece(out, rng, PropKind::Pillar, {std::cos(a) * 360.0f, std::sin(a) * 360.0f}, 0, c, valid); }
            const Vec2 climb = {c.x + std::cos(base + 3.3f) * 420.0f, c.z + std::sin(base + 3.3f) * 420.0f};
            AddClimb(out, climb, OutwardDir(climb, c), valid);
            break;
        }
    }
}

// The landmark in the middle of a map: a keep. A ring wall of posts and boulders (stacks and standing slabs) with four gates, two halls inside
// and the stepped mound of the keep itself in the middle with the best chest on top. `size` scales the ring for a small map, which leaves
// the halls out.
inline void BuildKeep(PoiLayout& out, Rng& rng, Vec2 c, float base, float size, const PlacementFn& valid) {
    using namespace poi_detail;
    const float pi = 3.14159265f, r = 600.0f * size;
    const int posts = static_cast<int>(36 * size) + 6;
    for (int i = 0; i < posts; i++) {
        const float a = i * 2.0f * pi / posts;
        if (std::fabs(std::cos(a * 2.0f)) < 0.22f) continue;   // the four gates, on the diagonals
        if (i % 4 == 0) Stone(out, rng, i % 8 == 0 ? kStack : kSlab, {std::cos(a) * r, std::sin(a) * r}, base, c, valid);
        else Piece(out, rng, PropKind::Pillar, {std::cos(a) * r, std::sin(a) * r}, base, c, valid);
    }
    if (size >= 0.9f) {   // the halls stand end-on to the mound, their doors facing along the ring
        AddHouse(out, rng, Rotated({-390, 0}, base, c), base + pi * 0.5f, valid);
        AddHouse(out, rng, Rotated({390, 0}, base, c), base - pi * 0.5f, valid);
    }
    Ziggurat(out, c, valid);
    for (int i = 0; i < 4; i++) Spot(out, {std::cos(i * pi * 0.5f + 0.7f) * 340.0f, std::sin(i * pi * 0.5f + 0.7f) * 340.0f}, base, c, valid);
}

// The kinds of town for `count` places, shuffled from the seed and dealt round so that neighbours (and the first few) all differ.
inline std::vector<TownKind> DealTownKinds(Rng& rng, int count) {
    std::vector<TownKind> deck;
    std::vector<TownKind> out;
    while (static_cast<int>(out.size()) < count) {
        if (deck.empty()) {
            for (int k = 0; k < kTownKindCount; k++) deck.push_back(static_cast<TownKind>(k));
            for (size_t i = deck.size(); i > 1; i--) std::swap(deck[i - 1], deck[rng.Below(static_cast<uint32_t>(i))]);
            if (!out.empty() && deck.back() == out.back()) std::swap(deck.front(), deck.back());   // no two in a row across decks
        }
        out.push_back(deck.back());
        deck.pop_back();
    }
    return out;
}

// Out in the open between the towns: boulder formations, climbs standing alone and chests hidden behind big rocks, spread so no two are close
// together. `props` is the map's scenery so far (the boulders to hide behind come from there); `taken` the chest spots already used, which new
// ones keep their distance from.
inline void GenerateWilds(PoiLayout& out, uint64_t seed, Circle map, const std::vector<Prop>& props, const std::vector<Vec2>& taken, int climbs, int hideaways,
                          const PlacementFn& valid = nullptr, int formations = 0, int outposts = 0) {
    Rng rng(seed ^ 0x77696C64ull); // "wild"
    std::vector<Vec2> spots = taken;
    for (const ChestSite& s : out.sites) spots.push_back(s.pos);
    const float apart = (std::max)(380.0f, map.radius * 0.1f);
    auto far = [&](Vec2 p) {
        for (const Vec2& q : spots) if (Distance(p, q) < apart) return false;
        for (const Poi& poi : out.pois) if (Distance(p, poi.center) < poi.radius * 0.8f) return false;
        return true;
    };
    // Formations first: well clear of the towns (they have their own) and of each other, so each is a landmark of its own.
    std::vector<Vec2> rocks;
    int deck[kFormationCount] = {};
    for (int n = 0, placed = 0; n < formations * 16 && placed < formations; n++) {
        const Vec2 at = RandomPointIn(rng, map, valid, 0.88f);
        if (valid && !valid(at)) continue;
        bool ok = Distance(at, map.center) <= map.radius - 450.0f;
        for (const Poi& poi : out.pois) ok = ok && Distance(at, poi.center) > poi.radius + 650.0f;   // a formation reaches about 600 from its middle
        for (const Vec2& q : rocks) ok = ok && Distance(at, q) > (std::max)(1100.0f, map.radius * 0.22f);
        for (const Vec2& q : spots) ok = ok && Distance(at, q) > 420.0f;
        if (!ok) continue;
        if (placed % kFormationCount == 0) {   // dealt from a shuffled deck, so a map gets one of each before any repeats
            for (int i = 0; i < kFormationCount; i++) deck[i] = i;
            for (int i = kFormationCount - 1; i > 0; i--) std::swap(deck[i], deck[rng.Below(static_cast<uint32_t>(i + 1))]);
        }
        const Formation kind = static_cast<Formation>(deck[placed % kFormationCount]);
        const size_t sitesBefore = out.sites.size();
        AddFormation(out, rng, kind, at, static_cast<float>(rng.Unit() * 6.2831853), valid);
        for (size_t i = sitesBefore; i < out.sites.size(); i++) spots.push_back(out.sites[i].pos);
        rocks.push_back(at);
        placed++;
    }
    auto clearOfRocks = [&](Vec2 p) { for (const Vec2& q : rocks) if (Distance(p, q) < 650.0f) return false; return true; };
    // Outposts next: one of each design before any repeats, each well apart from the towns and formations.
    for (int n = 0, placed = 0; n < outposts * 16 && placed < outposts; n++) {
        const Vec2 at = RandomPointIn(rng, map, valid, 0.85f);
        if (!far(at) || !clearOfRocks(at) || Distance(at, map.center) > map.radius - 650.0f) continue;
        bool ok = true;
        for (const Poi& poi : out.pois) ok = ok && Distance(at, poi.center) > poi.radius + 600.0f;
        if (!ok) continue;
        const size_t before = out.props.size(), sitesBefore = out.sites.size();
        if (!AddOutpost(out, rng, static_cast<Outpost>((placed + static_cast<int>(seed % kOutpostCount)) % kOutpostCount), at, static_cast<int>(rng.Below(4)), valid)) continue;
        bool apartEnough = true;
        for (size_t i = sitesBefore; i < out.sites.size(); i++) apartEnough = apartEnough && far(out.sites[i].pos);
        if (!apartEnough) { out.props.resize(before); out.sites.resize(sitesBefore); continue; }
        for (size_t i = sitesBefore; i < out.sites.size(); i++) spots.push_back(out.sites[i].pos);
        rocks.push_back(at);
        placed++;
    }
    for (int n = 0, placed = 0; n < climbs * 12 && placed < climbs; n++) {
        const Vec2 at = RandomPointIn(rng, map, valid, 0.85f);
        if (!far(at) || !clearOfRocks(at) || Distance(at, map.center) > map.radius - 500.0f) continue;
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


// ---- Hyrule Field's own places ------------------------------------------------------------------------------------------------------
// The field is the biggest map and gets places of its own instead of the same town over and over: a ruined castle in the middle, a ranch, a long wall,
// a canyon, a stone circle, a graveyard and so on. Each is built from ordinary props (so bots path around them and clients draw them) plus our solid
// stone blocks, which is how the field's ground gets new hills, a raised causeway and a stepped mound that need jumping and climbing.

enum class FieldPlace : uint8_t { Castle, Ranch, GreatWall, Ravine, TemplePlaza, WindmillHill, Graveyard, StoneCircle, FairyGlade, Causeway, Count };
constexpr int kFieldPlaceCount = static_cast<int>(FieldPlace::Count);
// Where each place goes (angle in degrees, distance as a share of the map radius) and how big its area is.
struct FieldSlot { float angleDeg, ring, radius; };
constexpr FieldSlot kFieldSlots[kFieldPlaceCount] = {
    {0, 0.00f, 700}, {270, 0.55f, 520}, {20, 0.45f, 600}, {150, 0.65f, 560}, {60, 0.30f, 420},
    {200, 0.38f, 380}, {110, 0.42f, 400}, {320, 0.62f, 380}, {235, 0.82f, 360}, {90, 0.78f, 600},
};

inline void BuildFieldPlace(PoiLayout& out, Rng& rng, FieldPlace what, Vec2 at, float angle, const PlacementFn& valid) {
    using namespace poi_detail;
    const float pi = 3.14159265f;
    switch (what) {
        case FieldPlace::Castle: {   // a ring wall with four gates, two halls inside and the stepped mound of the keep in the middle
            const int posts = 36;
            for (int i = 0; i < posts; i++) {
                const float a = i * 2.0f * pi / posts;
                const float gate = std::fabs(std::sin(a * 2.0f));      // 0 at the four gates
                if (gate < 0.2f) continue;
                Piece(out, rng, i % 3 == 0 ? PropKind::Boulder : PropKind::Pillar, {std::cos(a) * 520.0f, std::sin(a) * 520.0f}, 0, at, valid);
            }
            AddHouse(out, rng, Rotated({-250, 0}, angle, at), angle + pi * 0.5f, valid);
            AddHouse(out, rng, Rotated({250, 0}, angle, at), angle - pi * 0.5f, valid);
            Ziggurat(out, at, valid);
            for (int i = 0; i < 4; i++) Spot(out, {std::cos(i * pi * 0.5f + 0.7f) * 330.0f, std::sin(i * pi * 0.5f + 0.7f) * 330.0f}, 0, at, valid);
            break;
        }
        case FieldPlace::Ranch: {    // a fenced paddock with a barn at one end and bushes (the hay) inside
            for (float x = -420.0f; x <= 420.1f; x += 105.0f) { Piece(out, rng, PropKind::Pillar, {x, -260}, angle, at, valid); if (std::fabs(x) > 120.0f) Piece(out, rng, PropKind::Pillar, {x, 260}, angle, at, valid); }
            for (float z = -155.0f; z <= 155.1f; z += 105.0f) { Piece(out, rng, PropKind::Pillar, {-420, z}, angle, at, valid); Piece(out, rng, PropKind::Pillar, {420, z}, angle, at, valid); }
            AddHouse(out, rng, Rotated({-250, 0}, angle, at), angle, valid);
            for (int i = 0; i < 10; i++) Piece(out, rng, PropKind::Bush, {60.0f + rng.Below(300), -200.0f + rng.Below(400)}, angle, at, valid);
            Spot(out, {330, -80}, angle, at, valid); Spot(out, {330, 90}, angle, at, valid);
            break;
        }
        case FieldPlace::GreatWall: { // a long wall of boulders across the field with two gaps to run through, and a climb at one end
            for (float t = -1200.0f; t <= 1200.1f; t += 150.0f) {
                if (std::fabs(t - 450.0f) < 140.0f || std::fabs(t + 500.0f) < 140.0f) continue;
                Piece(out, rng, PropKind::Boulder, {t, 0}, angle, at, valid);
            }
            AddClimb(out, Rotated({-1330, 160}, angle, at), 0, valid);
            Spot(out, {-700, 150}, angle, at, valid); Spot(out, {800, -150}, angle, at, valid); Spot(out, {-100, 150}, angle, at, valid);
            break;
        }
        case FieldPlace::Ravine: {   // two rows of boulders with a corridor between them, a dead end with chests, and a guardian
            for (float t = -750.0f; t <= 750.1f; t += 150.0f) { Piece(out, rng, PropKind::Boulder, {t, -190}, angle, at, valid); Piece(out, rng, PropKind::Boulder, {t, 190}, angle, at, valid); }
            for (float z = -110.0f; z <= 110.1f; z += 110.0f) Piece(out, rng, PropKind::Boulder, {825, z}, angle, at, valid);
            Piece(out, rng, PropKind::Pillar, {-800, -80}, angle, at, valid); Piece(out, rng, PropKind::Pillar, {-800, 80}, angle, at, valid);
            out.bossSpots.push_back(Rotated({600, 0}, angle, at));
            Spot(out, {700, -60}, angle, at, valid); Spot(out, {640, 70}, angle, at, valid); Spot(out, {-300, 0}, angle, at, valid);
            break;
        }
        case FieldPlace::TemplePlaza: { // a ring of eight standing stones round a dais you have to climb
            for (int i = 0; i < 8; i++) Piece(out, rng, PropKind::Pillar, {std::cos(i * pi * 0.25f) * 260.0f, std::sin(i * pi * 0.25f) * 260.0f}, 0, at, valid);
            AddClimb(out, Rotated({-150, 0}, angle, at), (static_cast<int>(angle / (pi * 0.5f)) & 3), valid);
            Spot(out, {0, -170}, angle, at, valid); Spot(out, {0, 170}, angle, at, valid);
            break;
        }
        case FieldPlace::WindmillHill: { // a stepped hill, with ruined walls round the foot
            Ziggurat(out, at, valid);
            for (int i = 0; i < 6; i++) { const float a = i * pi / 3.0f + 0.3f; Piece(out, rng, PropKind::Pillar, {std::cos(a) * 340.0f, std::sin(a) * 340.0f}, 0, at, valid); }
            Spot(out, {0, 300}, angle, at, valid); Spot(out, {0, -300}, angle, at, valid);
            break;
        }
        case FieldPlace::Graveyard: { // rows of tombstones with a ruined chapel in the middle and something that guards it
            for (int row = 0; row < 3; row++) for (int col = 0; col < 5; col++) {
                if (row == 1 && col >= 1 && col <= 3) continue;
                Piece(out, rng, PropKind::Pillar, {-300.0f + col * 150.0f, -200.0f + row * 200.0f}, angle, at, valid);
            }
            AddRuins(out, rng, at, angle, valid);
            out.bossSpots.push_back(at);
            Spot(out, {-220, -100}, angle, at, valid); Spot(out, {220, 100}, angle, at, valid);
            break;
        }
        case FieldPlace::StoneCircle: { // ten great stones round a chest, with two gaps in the ring
            for (int i = 0; i < 10; i++) { if (i == 2 || i == 7) continue; Piece(out, rng, PropKind::Boulder, {std::cos(i * pi * 0.2f) * 300.0f, std::sin(i * pi * 0.2f) * 300.0f}, 0, at, valid); }
            if (!valid || valid(at)) out.sites.push_back({at, 1});
            break;
        }
        case FieldPlace::FairyGlade: { // a ring of bushes round a single standing stone
            for (int i = 0; i < 14; i++) Piece(out, rng, PropKind::Bush, {std::cos(i * pi / 7.0f) * 230.0f, std::sin(i * pi / 7.0f) * 230.0f}, 0, at, valid);
            Piece(out, rng, PropKind::Pillar, {0, 0}, angle, at, valid);
            Spot(out, {60, 60}, angle, at, valid); Spot(out, {-70, -40}, angle, at, valid);
            break;
        }
        case FieldPlace::Causeway: { // a raised walk of blocks with posts along both sides: a high road across the open field
            const float s = kPlatformHalf * 2.0f;
            bool ok = true;
            for (int i = -3; i <= 3; i++) if (valid && !valid(Rotated({i * s, 0}, angle, at))) ok = false;
            if (ok) {
                for (int i = -3; i <= 3; i++) out.props.push_back({Rotated({i * s, 0}, angle, at), i == -3 || i == 3 ? PropKind::PlatformMid : PropKind::PlatformLow, 0});
                for (int i = -3; i <= 3; i += 2) { Piece(out, rng, PropKind::Pillar, {i * s, -150}, angle, at, valid); Piece(out, rng, PropKind::Pillar, {i * s, 150}, angle, at, valid); }
                out.sites.push_back({Rotated({3 * s, 0}, angle, at), 2});
            }
            Spot(out, {-300, 250}, angle, at, valid); Spot(out, {300, -250}, angle, at, valid);
            break;
        }
        default: break;
    }
}

// The field's layout: ten places of its own at fixed spots on the map, then ordinary towns (a house, a cave, ruins, a climb) in the gaps, each
// kept apart from everything else. A place that will not fit on the ground is skipped, so a strangely shaped field still gets a sensible layout.
inline PoiLayout GenerateFieldPois(uint64_t seed, Circle map, int towns, const PlacementFn& valid) {
    PoiLayout out;
    Rng rng(seed ^ 0x6669656C64ull);   // "field"
    // names: the first of the field's list is the castle; the places have names of their own (kFieldNames) and the towns share the rest
    static const uint8_t kFieldNames[kFieldPlaceCount] = {0, 15, 16, 17, 18, 19, 8, 20, 11, 21};
    bool used[kNamesPerMap] = {};
    for (uint8_t n : kFieldNames) used[n] = true;
    std::vector<uint8_t> townNames;
    for (int i = 0; i < kNamesPerMap; i++) if (!used[i]) townNames.push_back(static_cast<uint8_t>(i));
    for (size_t i = townNames.size(); i > 1; i--) std::swap(townNames[i - 1], townNames[rng.Below(static_cast<uint32_t>(i))]);
    auto groundUnder = [&](Vec2 c, float r) {
        if (!valid) return true;
        if (!valid(c)) return false;
        for (int k = 0; k < 8; k++) { const float a = k * 0.785398f; if (!valid({c.x + std::cos(a) * r * 0.7f, c.z + std::sin(a) * r * 0.7f})) return false; }
        return true;
    };
    auto apart = [&](Vec2 c, float r) {
        for (const Poi& o : out.pois) if (Distance(o.center, c) < (o.radius + r) * 1.05f) return false;
        return Distance(c, map.center) <= map.radius - r * 0.9f;
    };
    const float scale = (std::min)(1.0f, map.radius / 7000.0f);   // a smaller circle squeezes the same places closer
    const float pi = 3.14159265f;
    for (int i = 0; i < kFieldPlaceCount; i++) {
        const FieldSlot& slot = kFieldSlots[i];
        const float r = slot.radius * (0.55f + 0.45f * scale);
        for (int attempt = 0; attempt < 14; attempt++) {
            const float shift = attempt == 0 ? 0.0f : (attempt % 2 ? 1.0f : -1.0f) * 0.12f * ((attempt + 1) / 2);
            const float a = slot.angleDeg * pi / 180.0f + shift;
            const float ring = slot.ring * (attempt < 3 ? 1.0f : 0.9f);
            const Vec2 c = {map.center.x + std::cos(a) * map.radius * ring, map.center.z + std::sin(a) * map.radius * ring};
            if (!groundUnder(c, r) || !apart(c, r)) continue;
            Poi p; p.name = kFieldNames[i]; p.center = c; p.radius = r;
            out.pois.push_back(p);
            // long places run across the line from the middle; the rest face a random way
            const float facing = (i == static_cast<int>(FieldPlace::GreatWall) || i == static_cast<int>(FieldPlace::Causeway)) ? a + pi * 0.5f : static_cast<float>(rng.Unit() * 6.2831853);
            BuildFieldPlace(out, rng, static_cast<FieldPlace>(i), c, facing, valid);
            break;
        }
    }
    // Towns in the gaps: a wide ring, then anywhere there is room. Each is a different kind of place (BuildTown).
    const float townRadius = kTownRadius;
    const std::vector<TownKind> kinds = DealTownKinds(rng, towns);
    for (int n = 0, placed = 0; n < 90 && placed < towns && placed < static_cast<int>(townNames.size()); n++) {
        const float a = static_cast<float>(rng.Unit() * 6.2831853), d = map.radius * (0.35f + 0.55f * static_cast<float>(rng.Unit()));
        const Vec2 c = {map.center.x + std::cos(a) * d, map.center.z + std::sin(a) * d};
        if (!groundUnder(c, townRadius) || !apart(c, townRadius + 160.0f)) continue;
        Poi p; p.name = townNames[static_cast<size_t>(placed)]; p.center = c; p.radius = townRadius;
        out.pois.push_back(p);
        BuildTown(out, rng, kinds[static_cast<size_t>(placed)], c, static_cast<float>(rng.Unit() * 6.2831853), valid);
        placed++;
    }
    std::vector<Vec2> spots;
    for (const Vec2& sp : out.lootSpots) if (!valid || valid(sp)) spots.push_back(sp);
    out.lootSpots = spots;
    return out;
}

// Where the four allies wait: spread across the middle rings of the map, well apart from each other and from the towns (so you have to go and find them).
inline std::vector<Vec2> GenerateAllySpots(uint64_t seed, Circle map, const std::vector<Poi>& pois, const PlacementFn& valid = nullptr) {
    Rng rng(seed ^ 0x616C6C79ull ^ 0x73706F74ull);   // "ally" "spot"
    std::vector<Vec2> out;
    const float apart = (std::max)(500.0f, map.radius * 0.42f);
    for (int attempt = 0; attempt < 600 && static_cast<int>(out.size()) < kAllyCount; attempt++) {
        const float a = static_cast<float>(rng.Unit() * 6.2831853), d = map.radius * (0.25f + 0.5f * static_cast<float>(rng.Unit()));
        const Vec2 at = {map.center.x + std::cos(a) * d, map.center.z + std::sin(a) * d};
        if (valid && !valid(at)) continue;
        bool ok = true;
        for (const Vec2& q : out) if (Distance(q, at) < apart * (attempt < 300 ? 1.0f : 0.6f)) ok = false;
        for (const Poi& p : pois) if (Distance(p.center, at) < p.radius * 0.9f) ok = false;
        if (ok) out.push_back(at);
    }
    while (static_cast<int>(out.size()) < kAllyCount) out.push_back(RandomPointIn(rng, map, valid, 0.6f));   // a small or strange map: anywhere will do
    return out;
}

// ---- the Fortnite Map's own places ---------------------------------------------------------------------------------------------------
// The island's texture has its towns painted on (shared/fortnite_map.h): Tilted Towers' grey blocks, Pleasant Park round its green, the three
// sheds of Dusty Depot, the farms, the swamp, the dark mass of Wailing Woods. Each named place is built on the spot where the picture has it,
// with buildings and features made from ordinary props (so bots path round them and every client draws the same), and the toughest ones have a
// mini boss standing guard. The places are in the order of their names (kPoiNames, the Fortnite Map's list), most famous first: a small match
// gets the first few, a full one all of them.
enum class IslandPlace : uint8_t {
    TiltedTowers, PleasantPark, RetailRow, SaltySprings, LootLake, DustyDepot, GreasyGrove, LonelyLodge, SnobbyShores, ShiftyShafts, FlushFactory,
    FatalFields, LuckyLanding, HauntedHills, JunkJunction, MoistyMire, AnarchyAcres, WailingWoods, TomatoTown, ParadisePalms, RiskyReels, LazyLinks,
    FrostyFlights, SweatySands, Count
};
constexpr int kIslandPlaceCount = static_cast<int>(IslandPlace::Count);
static_assert(kIslandPlaceCount == kNamesPerMap, "one place for every name on the Fortnite Map");
// Where each one is painted, in map units (the texture's pixel times the island's size, see scripts/make_fortnite_map.py; pixel in the comment).
constexpr Vec2 kIslandSpots[kIslandPlaceCount] = {
    {-2092, -444},  {-3467, -3454}, {3373, 196},    {550, 1099},    // Tilted Towers (735, 965), Pleasant Park (545, 565), Retail Row (1490, 1050), Salty Springs (1100, 1170)
    {-174, -2513},  {984, -1008},   {-4227, 1437},  {5255, -1761},  // Loot Lake's east shore (1000, 690), Dusty Depot (1160, 890), Greasy Grove (440, 1215), Lonely Lodge (1750, 790)
    {-6218, -1234}, {-4046, -519},  {-2418, 5313},  {1274, 3394},   // Snobby Shores (165, 860), Shifty Shafts (465, 955), Flush Factory (690, 1730), Fatal Fields (1200, 1475)
    {116, 5915},    {-5241, -4733}, {-4698, -6013}, {4459, 4033},   // Lucky Landing (1040, 1810), Haunted Hills (300, 395), Junk Junction (375, 225), Moisty Mire (1640, 1560)
    {-174, -4545},  {4170, -3266},  {2070, -3078},  {3518, 3206},   // Anarchy Acres (1000, 420), Wailing Woods (1600, 590), Tomato Town (1310, 615), Paradise Palms (1510, 1450)
    {5473, 1099},   {3011, -4696},  {-2201, 1701},  {3011, -1761},  // Risky Reels (1780, 1170), Lazy Links (1440, 400), Frosty Flights (720, 1250), Sweaty Sands (1440, 790)
};
constexpr float kIslandPlaceRadius = 520.0f;

inline void BuildIslandPlace(PoiLayout& out, Rng& rng, IslandPlace what, Vec2 c, float base, const PlacementFn& valid) {
    using namespace poi_detail;
    const float pi = 3.14159265f;
    auto local = [&](float x, float z) { return Rotated({x, z}, base, c); };
    auto ring = [&](PropKind kind, int n, float r, float phase) {   // n pieces round a circle
        for (int i = 0; i < n; i++) { const float a = phase + i * 2.0f * pi / n; Piece(out, rng, kind, {std::cos(a) * r, std::sin(a) * r}, base, c, valid); }
    };
    auto guard = [&](float x, float z) { const Vec2 p = local(x, z); if (!valid || valid(p)) out.bossSpots.push_back(p); };
    auto climb = [&](float x, float z) { const Vec2 p = local(x, z); AddClimb(out, p, OutwardDir(p, c), valid); };
    switch (what) {
        case IslandPlace::TiltedTowers:   // a tight block of tall buildings round a stepped clock tower, and something nasty in the square
            AddHouse(out, rng, local(-230, -150), base, valid);
            AddHouse(out, rng, local(230, -150), base, valid);
            Ziggurat(out, local(0, 260), valid);
            climb(-430, 230);
            guard(0, 60);
            Spot(out, {-330, 120}, base, c, valid); Spot(out, {330, 120}, base, c, valid);
            break;
        case IslandPlace::PleasantPark:   // two houses facing a park: a gazebo post ringed by hedges, a climb behind
            AddHouse(out, rng, local(-260, -210), base, valid);
            AddHouse(out, rng, local(260, -210), base, valid);
            Piece(out, rng, PropKind::Pillar, {0, 160}, base, c, valid);
            for (int i = 0; i < 6; i++) { const float a = i * pi / 3.0f; Piece(out, rng, PropKind::Bush, {std::cos(a) * 150.0f, 160.0f + std::sin(a) * 150.0f}, base, c, valid); }
            climb(0, -430);
            Spot(out, {0, 60}, base, c, valid); Spot(out, {0, 300}, base, c, valid);
            break;
        case IslandPlace::RetailRow:      // a row of shops along a street lit by lamp posts, crates (table rocks) out front
            AddHouse(out, rng, local(-210, -160), base, valid);
            AddHouse(out, rng, local(210, -160), base, valid);
            for (int i = 0; i < 4; i++) Piece(out, rng, PropKind::Pillar, {-330.0f + i * 220.0f, 70}, base, c, valid);
            Stone(out, rng, kTable, {-120, 200}, base, c, valid); Stone(out, rng, kTable, {150, 230}, base, c, valid);
            climb(0, 400);
            Spot(out, {0, 130}, base, c, valid);
            break;
        case IslandPlace::SaltySprings:   // a house, the ruins of its neighbour and a garden
            AddHouse(out, rng, local(0, -170), base, valid);
            AddRuins(out, rng, local(0, 230), base, valid);
            for (int i = 0; i < 4; i++) Piece(out, rng, PropKind::Bush, {-300.0f + i * 200.0f, 60}, base, c, valid);
            climb(400, 0);
            break;
        case IslandPlace::LootLake:       // the lake house on the shore, rocks at the water's edge and something lurking
            AddHouse(out, rng, c, base, valid);
            for (int i = 0; i < 3; i++) Stone(out, rng, i == 1 ? kHuddle : kDome, {-240.0f + i * 240.0f, 300}, base, c, valid);
            guard(0, -300);
            Spot(out, {300, -200}, base, c, valid);
            break;
        case IslandPlace::DustyDepot:     // two big sheds, crates between them and a lookout climb
            AddHouse(out, rng, local(-220, 0), base + pi * 0.5f, valid);
            AddHouse(out, rng, local(220, 0), base + pi * 0.5f, valid);
            Stone(out, rng, kTable, {0, -240}, base, c, valid); Stone(out, rng, kTable, {0, 240}, base, c, valid);
            climb(-150, 420);
            guard(0, 0);
            break;
        case IslandPlace::GreasyGrove:    // a diner with its hedges and a well post
            AddHouse(out, rng, c, base, valid);
            for (int i = 0; i < 5; i++) Piece(out, rng, PropKind::Bush, {-260.0f + i * 130.0f, 280}, base, c, valid);
            Piece(out, rng, PropKind::Pillar, {320, -60}, base, c, valid);
            climb(-380, -200);
            Spot(out, {0, 200}, base, c, valid);
            break;
        case IslandPlace::LonelyLodge:    // the lodge and its tall wooden lookout in the woods
            AddHouse(out, rng, local(0, -260), base, valid);
            AddOutpost(out, rng, Outpost::Lookout, local(0, 200), static_cast<int>(rng.Below(4)), valid);
            Spot(out, {-300, 120}, base, c, valid);
            break;
        case IslandPlace::SnobbyShores:   // a villa on the cliffs with its stepped garden
            AddHouse(out, rng, local(0, -200), base, valid);
            Ziggurat(out, local(0, 220), valid);
            break;
        case IslandPlace::ShiftyShafts:   // the mine: a cave in the hillside, a ridge of spoil and whatever lives in the dark
            AddCave(out, rng, c, base, valid);
            AddFormation(out, rng, Formation::Ridge, local(0, -400), base, valid);
            Spot(out, {250, 260}, base, c, valid);
            break;
        case IslandPlace::FlushFactory:   // a walled yard round the factory, a guard inside
            for (float t = -300.0f; t <= 300.1f; t += 150.0f) {
                Piece(out, rng, PropKind::Pillar, {t, -300}, base, c, valid);
                if (std::fabs(t) > 100.0f) Piece(out, rng, PropKind::Pillar, {t, 300}, base, c, valid);   // the gate
                if (std::fabs(t) < 299.0f) { Piece(out, rng, PropKind::Pillar, {-300, t}, base, c, valid); Piece(out, rng, PropKind::Pillar, {300, t}, base, c, valid); }
            }
            AddHouse(out, rng, local(0, -60), base, valid);
            guard(0, 200);
            break;
        case IslandPlace::FatalFields:    // the farm: a barn, a paddock fence and haystacks (bushes)
            AddHouse(out, rng, local(-260, 0), base, valid);
            for (int i = 0; i < 4; i++) { Piece(out, rng, PropKind::Pillar, {60.0f + i * 110.0f, -220}, base, c, valid); Piece(out, rng, PropKind::Pillar, {60.0f + i * 110.0f, 220}, base, c, valid); }
            for (int i = 0; i < 6; i++) Piece(out, rng, PropKind::Bush, {110.0f + (i % 3) * 110.0f, -90.0f + (i / 3) * 180.0f}, base, c, valid);
            Spot(out, {230, 0}, base, c, valid);
            break;
        case IslandPlace::LuckyLanding:   // a temple mound in a ring of standing stones
            Ziggurat(out, c, valid);
            for (int i = 0; i < 6; i++) { const float a = base + i * pi / 3.0f; Stone(out, rng, kSlab, {std::cos(a) * 340.0f, std::sin(a) * 340.0f}, 0, c, valid); }
            Spot(out, {380, 0}, base, c, valid); Spot(out, {-380, 0}, base, c, valid);
            break;
        case IslandPlace::HauntedHills:   // the graveyard: rows of tombstones round a ruined chapel, and the dead are restless
            for (int row = 0; row < 3; row++) for (int col = 0; col < 3; col++) if (row != 1 || col != 1) Piece(out, rng, PropKind::Pillar, {-220.0f + col * 220.0f, -220.0f + row * 220.0f}, base, c, valid);
            AddRuins(out, rng, local(0, 360), base, valid);
            guard(0, 0);
            Spot(out, {0, 110}, base, c, valid);
            break;
        case IslandPlace::JunkJunction:   // piles of junk (boulders) to climb over, with a guard in the scrap
            AddFormation(out, rng, Formation::Pile, local(-200, 0), base, valid);
            AddFormation(out, rng, Formation::Pile, local(260, 200), base + 1.0f, valid);
            climb(200, -320);
            guard(0, 150);
            Spot(out, {60, -60}, base, c, valid);
            break;
        case IslandPlace::MoistyMire:     // the swamp: reed clumps (bushes) and mossy stones, and something hiding in the grass
            for (int k = 0; k < 4; k++) {
                const float a = base + k * pi * 0.5f + 0.4f;
                for (int i = 0; i < 3; i++) Piece(out, rng, PropKind::Bush, {std::cos(a) * 280.0f + (i - 1) * 60.0f, std::sin(a) * 280.0f + (i % 2) * 50.0f}, 0, c, valid);
            }
            Stone(out, rng, kHuddle, {-150, 0}, base, c, valid); Stone(out, rng, kSplit, {160, 60}, base, c, valid);
            guard(0, -80);
            Spot(out, {0, 160}, base, c, valid);
            break;
        case IslandPlace::AnarchyAcres:   // a farmhouse, rows of crops (bushes) and a fence
            AddHouse(out, rng, local(0, -230), base, valid);
            for (int i = 0; i < 8; i++) Piece(out, rng, PropKind::Bush, {-300.0f + (i % 4) * 200.0f, 140.0f + (i / 4) * 150.0f}, base, c, valid);
            for (int i = 0; i < 4; i++) Piece(out, rng, PropKind::Pillar, {-420, -120.0f + i * 130.0f}, base, c, valid);
            break;
        case IslandPlace::WailingWoods:   // deep in the woods: a ring of hedges round a circle of standing stones, and its keeper
            ring(PropKind::Bush, 10, 330.0f, base);
            AddFormation(out, rng, Formation::Ring, c, base, valid);
            guard(0, 200);
            break;
        case IslandPlace::TomatoTown:     // what is left of the diner, a lamp post either side and a climb to the sign
            AddRuins(out, rng, c, base, valid);
            Piece(out, rng, PropKind::Pillar, {-280, 120}, base, c, valid); Piece(out, rng, PropKind::Pillar, {280, 120}, base, c, valid);
            climb(0, -320);
            Spot(out, {0, 200}, base, c, valid);
            break;
        case IslandPlace::ParadisePalms:  // a hotel by the pool (table rocks) and its hedges
            AddHouse(out, rng, local(0, -150), base, valid);
            Stone(out, rng, kTable, {-160, 220}, base, c, valid); Stone(out, rng, kTable, {160, 220}, base, c, valid);
            for (int i = 0; i < 3; i++) Piece(out, rng, PropKind::Bush, {-200.0f + i * 200.0f, 380}, base, c, valid);
            break;
        case IslandPlace::RiskyReels:     // the drive-in: a walled ring to fight in, with posts for the screen
            AddOutpost(out, rng, Outpost::Arena, c, static_cast<int>(rng.Below(4)), valid);
            Piece(out, rng, PropKind::Pillar, {-150, -380}, base, c, valid); Piece(out, rng, PropKind::Pillar, {150, -380}, base, c, valid);
            break;
        case IslandPlace::LazyLinks:      // the golf course: a flag post on a green ringed by bushes, and a sand trap (table rock)
            Piece(out, rng, PropKind::Pillar, {0, 0}, base, c, valid);
            ring(PropKind::Bush, 6, 220.0f, base + 0.3f);
            Stone(out, rng, kTable, {320, 120}, base, c, valid);
            Spot(out, {90, 40}, base, c, valid);
            break;
        case IslandPlace::FrostyFlights:  // the hangar and its raised runway
            AddHouse(out, rng, local(0, -260), base, valid);
            AddOutpost(out, rng, Outpost::Bridge, local(0, 230), 0, valid);
            break;
        default:                          // Sweaty Sands: a beach house, its hedges and a climb
            AddHouse(out, rng, c, base, valid);
            for (int i = 0; i < 4; i++) Piece(out, rng, PropKind::Bush, {-240.0f + i * 160.0f, 260}, base, c, valid);
            climb(380, -100);
            break;
    }
}

// The Fortnite Map's layout: its places, as many as the match wants (most famous first), each on its painted spot or as near it as there is
// ground. A place that won't fit is left out, so a different circle (the tests use one) still gets a sensible map.
inline PoiLayout GenerateIslandPois(uint64_t seed, Circle map, int count, const PlacementFn& valid) {
    PoiLayout out;
    Rng rng(seed ^ 0x69736C65ull);   // "isle"
    const int wanted = (std::max)(10, (std::min)(kIslandPlaceCount, count * 2));
    for (int i = 0; i < wanted; i++) {
        const Vec2 spot = kIslandSpots[i];
        Vec2 c = spot;
        bool ok = false;
        for (int k = 0; k < 25 && !ok; k++) {   // the spot itself, then rings round it
            const float a = (k - 1) % 8 * 0.785398f, d = k == 0 ? 0.0f : 120.0f * (1 + (k - 1) / 8);
            c = {spot.x + std::cos(a) * d, spot.z + std::sin(a) * d};
            ok = (!valid || valid(c)) && Distance(c, map.center) <= map.radius - 150.0f;
        }
        if (!ok) continue;
        Poi p;
        p.name = static_cast<uint8_t>(kFortniteMapIndex * kNamesPerMap + i);
        p.center = c;
        p.radius = kIslandPlaceRadius;
        out.pois.push_back(p);
        BuildIslandPlace(out, rng, static_cast<IslandPlace>(i), c, static_cast<float>(rng.Unit() * 6.2831853), valid);
    }
    std::vector<Vec2> spots;
    for (const Vec2& sp : out.lootSpots) if (!valid || valid(sp)) spots.push_back(sp);
    out.lootSpots = spots;
    return out;
}

inline PoiLayout GeneratePois(uint64_t seed, Circle map, int count, const PlacementFn& valid = nullptr, int mapId = 0) {
    if(IsBossArena(mapId)) {
        PoiLayout arena;
        arena.pois={ {static_cast<uint8_t>(PoiNameBase(mapId)),{0,0},1500},
                     {static_cast<uint8_t>(PoiNameBase(mapId)+1),{0,-2400},760},
                     {static_cast<uint8_t>(PoiNameBase(mapId)+2),{0,-900},650} };
        return arena;
    }
    if (ClampMap(mapId) == kFortniteMapIndex) return GenerateIslandPois(seed, map, count, valid);   // the island's places are painted on it
    if (ClampMap(mapId) == 0) return GenerateFieldPois(seed, map, (std::max)(3, (std::min)(8, count - 4)), valid);   // Hyrule Field has places of its own
    PoiLayout out;
    Rng rng(seed ^ 0x706F69ull); // "poi"
    // The names, shuffled for this match.
    const int nameBase = PoiNameBase(ClampMap(mapId));
    uint8_t names[kNamesPerMap];
    for (int i = 0; i < kNamesPerMap; i++) names[i] = static_cast<uint8_t>(nameBase + i);
    for (int i = kNamesPerMap - 1; i > 1; i--) std::swap(names[i], names[1 + rng.Below(static_cast<uint32_t>(i))]); // name 0 stays the landmark's

    // Laid out the way battle royale maps are: a keep in the middle, a ring of towns around it and a wider ring near the edge, each a
    // different kind of place (BuildTown) with room around it, so there are open stretches (with boulder formations) to run across between
    // them. A town is about a thousand across whatever the map, so a small map gets fewer of them rather than a pile of overlapping ones.
    const float poiRadius = kTownRadius;
    const float minGap = (std::max)((std::min)(1250.0f, map.radius * 0.57f), map.radius * 0.26f);
    const int wanted = (std::min)(count, kNamesPerMap);
    const float keepSize = (std::max)(0.6f, (std::min)(1.0f, map.radius / 3000.0f));
    const std::vector<TownKind> kinds = DealTownKinds(rng, wanted);
    struct Slot { float ring; int index, of; float phase; };
    std::vector<Slot> slots = {{0.0f, 0, 1, 0.0f}};
    const float phase = static_cast<float>(rng.Unit() * 6.2831853);
    for (int i = 0; i < 5; i++) slots.push_back({0.58f, i, 5, phase});
    for (int i = 0; i < 6; i++) slots.push_back({0.80f, i, 6, phase + 0.5f});
    for (const Slot& slot : slots) {
        if (static_cast<int>(out.pois.size()) >= wanted) break;
        bool placed = false;
        for (int attempt = 0; attempt < 24 && !placed; attempt++) {
            const float angle = slot.phase + slot.index * 6.2831853f / slot.of + static_cast<float>(rng.Unit() - 0.5) * 0.35f;
            const float dist = map.radius * slot.ring * (1.0f + static_cast<float>(rng.Unit() - 0.5) * 0.12f);
            const Vec2 c = {map.center.x + std::cos(angle) * dist, map.center.z + std::sin(angle) * dist};
            if (slot.ring > 0.0f && Distance(c, map.center) > map.radius - poiRadius * 1.1f) continue;
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
            p.radius = slot.ring == 0.0f ? 600.0f * keepSize : poiRadius;
            const float base = static_cast<float>(rng.Unit() * 6.2831853);
            if (slot.ring == 0.0f) BuildKeep(out, rng, c, base, keepSize, valid);
            else BuildTown(out, rng, kinds[out.pois.size()], c, base, valid);
            out.pois.push_back(p);
        }
    }
    // Keep only the chest spots that are on real ground.
    std::vector<Vec2> spots;
    for (const Vec2& s : out.lootSpots) if (!valid || valid(s)) spots.push_back(s);
    out.lootSpots = spots;
    return out;
}

// Loose scenery (GenerateProps) keeps out of the places, so a town isn't cluttered with stray rocks and bushes on its streets.
inline std::vector<Circle> PoiClearings(const std::vector<Poi>& pois) {
    std::vector<Circle> out;
    for (const Poi& p : pois) out.push_back({p.center, p.radius + 120.0f});
    return out;
}

// A camp: a chest set in a loose ring of rocks and bushes a little way outside a town, open on the town's side, so a place has a stash just beyond
// its edge instead of chests lying in the open. `perTown` is how many each place gets; a camp needs level ground, clear of everything built and
// well apart from any other chest. They are ordinary props, so the bots path round them.
inline void AddCamps(PoiLayout& out, uint64_t seed, Circle map, const Ground& ground, int perTown = 1) {
    Rng rng(seed ^ 0x63616D70ull); // "camp"
    const float pi = 3.14159265f;
    std::vector<Vec2> chests = out.lootSpots;
    for (const ChestSite& s : out.sites) chests.push_back(s.pos);
    for (const Poi& poi : out.pois) {
        if (poi.radius >= 580.0f) continue;   // the keep in the middle has its own mound
        for (int made = 0, tries = 0; made < perTown && tries < 24; tries++) {
            const float a = static_cast<float>(rng.Unit() * 2.0 * pi), d = poi.radius + 230.0f + 160.0f * static_cast<float>(rng.Unit());
            const Vec2 at = {poi.center.x + std::cos(a) * d, poi.center.z + std::sin(a) * d};
            if (Distance(at, map.center) > map.radius - 400.0f || !ground.Level(at, 0.2f)) continue;
            bool ok = true;
            for (int k = 0; ok && k < 6; k++) ok = ground.Level({at.x + std::cos(k * pi / 3.0f) * 120.0f, at.z + std::sin(k * pi / 3.0f) * 120.0f}, 0.45f);
            for (const Poi& other : out.pois) ok = ok && Distance(at, other.center) > other.radius + 170.0f;
            for (const Vec2& q : chests) ok = ok && Distance(at, q) > 380.0f;
            for (const Prop& p : out.props) ok = ok && Distance(at, p.pos) > PropRadius(p.kind) + 140.0f;
            if (!ok) continue;
            // The ring is open towards the town: rocks and bushes on the far half only.
            for (int k = 0; k < 5; k++) {
                const float turn = a + pi * (0.35f + 1.3f * static_cast<float>(k) / 4.0f), r = (k % 2 == 0) ? 95.0f : 130.0f;
                out.props.push_back({{at.x + std::cos(turn) * r, at.z + std::sin(turn) * r}, k % 2 == 0 ? PropKind::Rock : PropKind::Bush,
                                     static_cast<uint16_t>(rng.Below(0x10000))});
            }
            out.sites.push_back({at, 0});
            chests.push_back(at);
            made++;
        }
    }
}

} // namespace royale
