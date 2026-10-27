#pragma once
#include "box_index.h"
#include "hyrule_third_storm_ground.h"
#include "poi.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>
namespace royale {
// Hyrule Before the Third Storm (map 15, tools/maps/hyrule_third_storm): the whole kingdom built in Blender, baked by export.py. Nothing is
// scattered over it. Read docs/HYRULE_THIRD_STORM.md. The map spans -32400..32400 (36 units a metre), so every test here uses
// hyrule_third_storm::kHalf, never the Fortnite island's kHalfX.

inline bool HyruleThirdStormOnMap(Vec2 p) { return hyrule_third_storm::OnMap(p.x, p.z); }
inline float HyruleThirdStormGroundHeight(Vec2 p) { return hyrule_third_storm::GroundY(p.x, p.z); }
inline bool HyruleThirdStormHasFloor(Vec2 p) { return hyrule_third_storm::HasFloor(p.x, p.z); }
// The terrain height for the minimap and weather: the same grid.
inline float HyruleThirdStormTerrainHeight(Vec2 p) { return hyrule_third_storm::GroundY(p.x, p.z); }
inline float HyruleThirdStormWaterY(Vec2 p) { return hyrule_third_storm::WaterY(p.x, p.z); }
inline bool HyruleThirdStormInBuilding(const hyrule_third_storm::Building& b, Vec2 p, float grow = 0.0f) {
    return std::fabs(p.x - b.x) <= b.halfWidth + grow && std::fabs(p.z - b.z) <= b.halfDepth + grow;
}
inline bool HyruleThirdStormLootHeightAt(Vec2 p, float* y) {
    for (const auto& s : hyrule_third_storm::kLootSites)
        if (std::fabs(p.x - s.x) < 2.0f && std::fabs(p.z - s.z) < 2.0f) { if (y) *y = s.y; return true; }
    return false;
}
inline std::vector<size_t> HyruleThirdStormCollidersNear(float x, float y, float z, size_t limit) {
    std::vector<std::pair<float, size_t>> near;
    y += 40.0f;
    for (size_t i = 0; i < sizeof(hyrule_third_storm::kPropColliders) / sizeof(hyrule_third_storm::kPropColliders[0]); ++i) {
        const auto& c = hyrule_third_storm::kPropColliders[i];
        if (std::fabs(x - c.x0) > 1200.0f && std::fabs(x - c.x1) > 1200.0f) continue;   // most are far away: skip the full test
        const float dx = std::max({c.x0 - x, 0.0f, x - c.x1}), dy = std::max({c.y0 - y, 0.0f, y - c.y1}), dz = std::max({c.z0 - z, 0.0f, z - c.z1});
        const float d = dx * dx + dy * dy + dz * dz;
        if (d <= 900.0f * 900.0f) near.push_back({d, i});
    }
    std::sort(near.begin(), near.end());
    std::vector<size_t> result;
    for (size_t i = 0; i < std::min(limit, near.size()); ++i) result.push_back(near[i].second);
    return result;
}
// Somewhere you can stand: a floor, not under water, not marked too steep.
inline bool HyruleThirdStormDryGround(Vec2 p) {
    if (!hyrule_third_storm::HasFloor(p.x, p.z) || (hyrule_third_storm::FlagsAt(p.x, p.z) & 3)) return false;
    return HyruleThirdStormGroundHeight(p) > HyruleThirdStormWaterY(p) + 15.0f;
}
// A prop's box (a tree trunk, a cart, a crate stack) stands at p.
inline bool HyruleThirdStormObstacleAt(Vec2 p, float margin = 22.0f) {
    static const BoxIndex<hyrule_third_storm::Obstacle> index(hyrule_third_storm::kObstacles);   // built on first use; the same answer as testing every box
    return index.Any(p.x, p.z, margin);
}
// Shared host/server placement contract: chest sites carry their own floor; other points need dry, open ground.
inline bool HyruleThirdStormPlacementValid(Vec2 p) {
    float y;
    if (HyruleThirdStormLootHeightAt(p, &y)) return y > HyruleThirdStormWaterY(p) + 15.0f;
    return HyruleThirdStormDryGround(p) && !HyruleThirdStormObstacleAt(p, 0.0f);
}
inline PoiLayout GenerateHyruleThirdStormLayout(const PlacementFn& valid = nullptr) {
    PoiLayout out;
    int i = 0;
    for (const auto& r : hyrule_third_storm::kRegions)
        out.pois.push_back({static_cast<uint16_t>(PoiNameBase(kHyruleThirdStormMapIndex) + i++), {r.x, r.z}, 2400.0f});
    for (const auto& b : hyrule_third_storm::kBossSites) out.bossSpots.push_back({b.x, b.z});
    for (const auto& p : hyrule_third_storm::kLootSites) {
        Vec2 at{p.x, p.z};
        if (!valid || valid(at)) out.lootSpots.push_back(at);
    }
    return out;
}
inline BossKind HyruleThirdStormBossAt(Vec2 at) {
    for (const auto& r : hyrule_third_storm::kBossSites)
        if (r.boss >= 0 && Distance(at, {r.x, r.z}) < 1.0f) return static_cast<BossKind>(r.boss);
    return BossKind::Stone;
}
}  // namespace royale
