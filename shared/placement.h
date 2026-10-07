#pragma once
#include "poi.h"
#include "props.h"
#include "terrain.h"
#include <algorithm>
#include <functional>
#include <vector>

namespace royale {

// Where things go, and why. Chests, boulders and the rest used to be dropped on random points, which put chests in the middle of empty fields,
// on cliffs, against walls and inside rocks. Now every one is tied to something a player can see:
//   - camps (poi.h AddCamps): a chest in a ring of rocks and bushes just outside each town;
//   - boulder nooks: a chest tucked against the far side of a boulder;
//   - groves: a chest in the middle of a thicket of bushes;
//   - cliff feet: a chest where a cliff or steep bank meets level ground, with boulders strung along it (props.h);
//   - lookouts: the summits and plateau edges, the best chests, flanked by two standing stones;
//   - on the Fortnite Map, the oaks and cliff slabs of its own scenery (AddIslandAnchors below).
// And every chest has to be on floor that exists and is level, clear of anything solid, away from the map's edge and out of the towns, which have
// their own chests indoors. All of it is a pure function of the seed, the map and the two ground probes, so the preview tool (server/tools) and the tests
// run it without the game.
enum class AnchorKind : uint8_t { Boulder, Grove, CliffFoot, Scenery };

struct LootAnchor {
    Vec2 pos;
    AnchorKind kind;
    Vec2 away{0, 0};   // the side to tuck the chest to, away from the middle of the thing (unit vector; zero means anywhere round it)
    float reach = 70;  // how far from `pos` the chest sits (the radius of the thing plus a little)
};

struct LootPlan {
    Circle map;
    Ground ground;
    std::vector<LootAnchor> anchors;
    std::vector<Circle> solids;   // what can't be stood in: rocks, boulders, pillars, platforms
    std::vector<Circle> towns;    // chests belong indoors here
};

inline void AddPropSolids(const std::vector<Prop>& props, std::vector<Circle>& out) {
    for (const Prop& p : props) {
        float r = PropRadius(p.kind);
        if (r <= 0.0f) continue;
        if (p.kind == PropKind::Boulder) r *= BoulderScale(p.rot);
        out.push_back({p.pos, r});
    }
}

// Is this a place a chest can stand? Floor under it that is level, clear of anything solid, inside the map and not on a town's streets.
inline bool ChestSpotOk(const LootPlan& plan, Vec2 p, float solidMargin = 45.0f) {
    if (Distance(p, plan.map.center) > plan.map.radius - 150.0f || !plan.ground.Level(p, 0.3f)) return false;
    for (const Circle& t : plan.towns) if (Distance(p, t.center) < t.radius) return false;
    for (const Circle& s : plan.solids) if (Distance(p, s.center) < s.radius + solidMargin) return false;
    return true;
}

// The best chest spot in a ring round an anchor: level and clear, on the anchor's far side when it has one, and in the lee of other scenery.
inline bool FindNook(const LootPlan& plan, Rng& rng, const LootAnchor& a, Vec2* out) {
    float best = -1.0e9f;
    bool found = false;
    const float pi = 3.14159265f;
    for (int k = 0; k < 16; k++) {
        const float angle = (k + static_cast<float>(rng.Unit())) * 2.0f * pi / 16.0f;
        const Vec2 d = {std::cos(angle), std::sin(angle)};
        for (int ring = 0; ring < 3; ring++) {
            const float r = a.reach + 35.0f * ring;
            const Vec2 p = {a.pos.x + d.x * r, a.pos.z + d.z * r};
            if (!ChestSpotOk(plan, p)) continue;
            int shelter = 0;
            for (const Circle& s : plan.solids) if (Distance(p, s.center) < 220.0f) shelter++;
            const float score = (d.x * a.away.x + d.z * a.away.z) * 1.0f + 0.6f * static_cast<float>(rng.Unit()) + 0.12f * (std::min)(shelter, 4) - 0.1f * ring;
            if (score > best) { best = score; *out = p; found = true; }
        }
    }
    return found;
}

// Anchors from the scenery (boulders, thickets, cliff feet). `extra` adds a map's own (the Fortnite Map's oaks and cliff slabs).
using AnchorExtraFn = std::function<void(const Circle& map, std::vector<LootAnchor>& anchors, std::vector<Circle>& solids)>;

inline LootPlan MakeLootPlan(Circle map, const Ground& ground, const std::vector<Prop>& props, const std::vector<Poi>& pois, const TerrainFeatures& features,
                             const AnchorExtraFn& extra = nullptr) {
    LootPlan plan;
    plan.map = map;
    plan.ground = ground;
    AddPropSolids(props, plan.solids);
    for (const Poi& p : pois) plan.towns.push_back({p.center, p.radius * 0.95f});
    if (extra) extra(map, plan.anchors, plan.solids);
    for (const Prop& p : props) {
        if (p.kind != PropKind::Boulder) continue;
        const float dx = p.pos.x - map.center.x, dz = p.pos.z - map.center.z, d = (std::max)(1.0f, std::hypot(dx, dz));
        plan.anchors.push_back({p.pos, AnchorKind::Boulder, {dx / d, dz / d}, PropRadius(p.kind) * BoulderScale(p.rot) + 50.0f});
    }
    for (const CliffFoot& f : features.cliffs) plan.anchors.push_back({f.pos, AnchorKind::CliffFoot, {-f.uphill.x, -f.uphill.z}, 60.0f});
    // Groves: where four or more bushes stand within 230 of each other, one anchor at their middle (the thickest first, none within 350 of another).
    std::vector<size_t> bushes;
    for (size_t i = 0; i < props.size(); i++) if (props[i].kind == PropKind::Bush) bushes.push_back(i);
    std::vector<std::pair<int, size_t>> thick;
    for (size_t i : bushes) {
        int n = 0;
        for (size_t j : bushes) if (Distance(props[i].pos, props[j].pos) < 230.0f) n++;
        if (n >= 4) thick.push_back({-n, i});
    }
    std::sort(thick.begin(), thick.end());
    std::vector<Vec2> groves;
    for (const auto& t : thick) {
        const Vec2 c = props[t.second].pos;
        bool apart = true;
        for (const Vec2& g : groves) if (Distance(c, g) < 350.0f) { apart = false; break; }
        if (!apart) continue;
        float sx = 0, sz = 0;
        int n = 0;
        for (size_t j : bushes) if (Distance(c, props[j].pos) < 230.0f) { sx += props[j].pos.x; sz += props[j].pos.z; n++; }
        groves.push_back(c);
        plan.anchors.push_back({{sx / n, sz / n}, AnchorKind::Grove, {0, 0}, 0.0f});
    }
    // Stone clusters: three or more rocks and standing stones within 260 of each other (a rock field, a ruin), a chest tucked in among them.
    std::vector<size_t> stones;
    for (size_t i = 0; i < props.size(); i++) if (props[i].kind == PropKind::Rock || props[i].kind == PropKind::Pillar) stones.push_back(i);
    std::vector<Vec2> fields;
    for (size_t i : stones) {
        int n = 0;
        float sx = 0, sz = 0;
        for (size_t j : stones) if (Distance(props[i].pos, props[j].pos) < 260.0f) { n++; sx += props[j].pos.x; sz += props[j].pos.z; }
        if (n < 3) continue;
        const Vec2 c = {sx / n, sz / n};
        bool apart = true;
        for (const Vec2& g : fields) if (Distance(c, g) < 400.0f) { apart = false; break; }
        if (!apart) continue;
        fields.push_back(c);
        plan.anchors.push_back({c, AnchorKind::Boulder, {0, 0}, 90.0f});
    }
    return plan;
}

// One rolled item at a spot (the same roll as GenerateLoot's).
inline LootSpawn RollSpawn(Rng& rng, Vec2 at, bool chest) {
    Rarity tier = RollRarity(rng, chest);
    ItemId item;
    if (!PickItem(rng, tier, &item)) item = static_cast<ItemId>(rng.Below(kPoolItemCount));
    if (tier < DefOf(item).minRarity) tier = DefOf(item).minRarity;
    if (tier > DefOf(item).maxRarity) tier = DefOf(item).maxRarity;
    return {at, item, tier, chest, true};
}

// The scattered chests, `count` of them, each at one of the plan's anchors (picked at random, the cliffs and thickets a little more often) and kept
// `spacing` apart from each other and `0.8 * spacing` from `avoid` (the chests of the towns, camps and climbs). If the anchors run out the rest go on
// any level, clear ground, still with the spacing, relaxed in steps so the count is met.
inline std::vector<LootSpawn> GenerateAnchoredLoot(uint64_t seed, const LootPlan& plan, int count, float chestFraction, const std::vector<Vec2>* avoid, float spacing) {
    Rng rng(seed ^ 0x616E6368ull); // "anch"
    std::vector<LootSpawn> out;
    out.reserve(count);
    auto weight = [](AnchorKind k) { return k == AnchorKind::CliffFoot ? 1.5f : k == AnchorKind::Grove ? 1.2f : k == AnchorKind::Scenery ? 0.9f : 1.0f; };
    // A weighted shuffle: each anchor's key is -ln(u) / weight, smallest first.
    std::vector<std::pair<float, size_t>> order;
    for (size_t i = 0; i < plan.anchors.size(); i++) order.push_back({-std::log((std::max)(1.0e-6, rng.Unit())) / weight(plan.anchors[i].kind), i});
    std::sort(order.begin(), order.end());
    auto apart = [&](Vec2 p, float gap) {
        if (avoid) for (const Vec2& q : *avoid) if (Distance(p, q) < gap * 0.8f) return false;
        for (const LootSpawn& l : out) if (Distance(p, l.pos) < gap) return false;
        return true;
    };
    auto put = [&](Vec2 p) { out.push_back(RollSpawn(rng, p, rng.Unit() < chestFraction)); };
    for (const auto& o : order) {
        if (static_cast<int>(out.size()) >= count) break;
        const LootAnchor& a = plan.anchors[o.second];
        Vec2 p;
        if (a.kind == AnchorKind::Grove) { p = a.pos; if (!ChestSpotOk(plan, p)) continue; }
        else if (!FindNook(plan, rng, a, &p)) continue;
        if (apart(p, spacing)) put(p);
    }
    // Chests out in the open are the exception: the anchors decide how many there are, and no more than a fifth of `count` more are added where the
    // scenery runs thin. (A map with little scenery gets fewer chests, not chests with nothing round them.)
    const int target = (std::min)(count, static_cast<int>(out.size()) + count / 5);
    for (float gap = spacing; static_cast<int>(out.size()) < target; gap *= 0.7f) {
        for (int tries = 0; tries < 400 && static_cast<int>(out.size()) < target; tries++) {
            const Vec2 p = RandomPointIn(rng, plan.map, nullptr, 0.9f);
            if (ChestSpotOk(plan, p) && apart(p, gap)) put(p);
        }
        if (gap < 40.0f) break;   // nowhere left (a tiny or mostly empty map): the count is not met rather than chests put on bad ground
    }
    return out;
}

// Everything a map's layout needs, in the order that makes it hang together: towns and their camps, loose scenery in clusters (aware of the
// cliffs), the wilds (formations, outposts, climbs, hideaways), lookouts, and then the plan the chests are placed from.
struct MapPlacement {
    PoiLayout layout;
    std::vector<Prop> props;   // all of the scenery: loose pieces and the layout's (what the clients draw and the bots avoid)
    LootPlan plan;
    TerrainFeatures features;
};

inline MapPlacement PlaceMap(uint64_t seed, Circle map, int mapId, int poiCount, int propCount, const PlacementFn& valid, const HeightFn& height,
                             const AnchorExtraFn& extra = nullptr) {
    MapPlacement out;
    const Ground ground(valid, height);
    out.features = FindTerrainFeatures(map, ground);
    // What the wilds may stand on: floor that is not a bank. (Towns keep the plain test: their places are fixed.)
    const PlacementFn gentle = [&ground](Vec2 p) { return ground.Level(p, 0.6f); };
    PoiLayout& layout = out.layout;
    layout = GeneratePois(seed, map, poiCount, valid, mapId);
    AddCamps(layout, seed, map, ground, 1);
    const float areaShare = (std::min)(1.9f, (map.radius * map.radius) / (4000.0f * 4000.0f));   // a small map gets fewer rocks and bushes, a big one more
    const int sceneryWanted = (std::max)(220, static_cast<int>(static_cast<float>(propCount) * (std::min)(1.8f, areaShare * 1.4f)));
    const std::vector<Circle> clearings = PoiClearings(layout.pois);
    std::vector<Prop> loose = GenerateProps(seed, map, (std::min)(sceneryWanted, (std::max)(120, kMaxProps - static_cast<int>(layout.props.size()) - 420)),
                                            valid, &clearings, &ground, &out.features, &layout.props);
    GenerateWilds(layout, seed, map, loose, layout.lootSpots, 4 + static_cast<int>(map.radius / 700.0f), 12 + static_cast<int>(map.radius / 250.0f), gentle,
                  2 + static_cast<int>(map.radius / 650.0f), 2 + static_cast<int>(map.radius / 1100.0f));
    // Lookouts: the best chests, on the summits, between two standing stones. A few per map, well apart from every other chest.
    {
        Rng rng(seed ^ 0x6C6F6F6Bull); // "look"
        std::vector<Vec2> chests = layout.lootSpots;
        for (const ChestSite& s : layout.sites) chests.push_back(s.pos);
        const int wanted = 2 + static_cast<int>(map.radius / 1500.0f);
        int made = 0;
        for (const Vec2& at : out.features.lookouts) {
            if (made >= wanted) break;
            bool ok = Distance(at, map.center) < map.radius - 300.0f;
            for (const Vec2& q : chests) ok = ok && Distance(at, q) > (std::max)(380.0f, map.radius * 0.1f);
            for (const Poi& poi : layout.pois) ok = ok && Distance(at, poi.center) > poi.radius + 100.0f;
            for (const Prop& q : layout.props) ok = ok && Distance(at, q.pos) > PropRadius(q.kind) + 120.0f;
            const Vec2 left = {at.x - 75.0f, at.z}, right = {at.x + 75.0f, at.z};
            ok = ok && ground.Level(left, 0.4f) && ground.Level(right, 0.4f);
            if (!ok) continue;
            layout.props.push_back({left, PropKind::Pillar, static_cast<uint16_t>(rng.Below(0x10000))});
            layout.props.push_back({right, PropKind::Pillar, static_cast<uint16_t>(rng.Below(0x10000))});
            layout.sites.push_back({at, 2});
            chests.push_back(at);
            made++;
        }
    }
    // Chests that landed on a bank or a cliff edge are dropped (the towns' own, indoors, only on the worst ground).
    auto keep = [&](Vec2 p, float steepest) { return ground.Slope(p) <= steepest; };
    layout.lootSpots.erase(std::remove_if(layout.lootSpots.begin(), layout.lootSpots.end(), [&](Vec2 p) { return !keep(p, 0.7f); }), layout.lootSpots.end());
    layout.sites.erase(std::remove_if(layout.sites.begin(), layout.sites.end(), [&](const ChestSite& s) { return !keep(s.pos, 0.7f); }), layout.sites.end());
    // Loose scenery gives way to everything built after it, and keeps a chest's spot free.
    std::vector<Vec2> chestAt = layout.lootSpots;
    for (const ChestSite& s : layout.sites) chestAt.push_back(s.pos);
    for (const Prop& q : loose) {
        bool ok = true;
        for (const Prop& b : layout.props) if (Distance(q.pos, b.pos) < PropRadius(q.kind) + PropRadius(b.kind) + 10.0f) { ok = false; break; }
        for (size_t i = 0; ok && i < chestAt.size(); i++) if (Distance(q.pos, chestAt[i]) < PropRadius(q.kind) + 60.0f) ok = false;
        if (ok) out.props.push_back(q);
    }
    out.props.insert(out.props.end(), layout.props.begin(), layout.props.end());   // the buildings, caves and climbs are scenery too
    if (out.props.size() > static_cast<size_t>(kMaxProps)) out.props.resize(kMaxProps);
    out.plan = MakeLootPlan(map, ground, out.props, layout.pois, out.features, extra);
    return out;
}

} // namespace royale
