#pragma once
#include "fortnite_scenery.h"
#include "placement.h"

namespace royale {
namespace fortnite {

// The Fortnite Map's own scenery as chest anchors: a chest under an oak on its rise, one in the lee of a cliff slab or a shore boulder. The pieces
// come from SceneryIn (the same function every client draws from), so a chest is always where an oak or cliff really stands. Oaks and boulders
// are solid, so they also go in the plan's solids and no chest is ever placed inside one.
inline void AddIslandAnchors(const Circle& map, std::vector<LootAnchor>& anchors, std::vector<Circle>& solids) {
    const int cx0 = static_cast<int>(std::floor((map.center.x - map.radius) / kSceneryCell)), cx1 = static_cast<int>(std::floor((map.center.x + map.radius) / kSceneryCell));
    const int cz0 = static_cast<int>(std::floor((map.center.z - map.radius) / kSceneryCell)), cz1 = static_cast<int>(std::floor((map.center.z + map.radius) / kSceneryCell));
    for (int cz = cz0; cz <= cz1; cz++) {
        for (int cx = cx0; cx <= cx1; cx++) {
            SceneryPiece piece;
            if (!SceneryIn(cx, cz, 1.0f, &piece)) continue;
            const Vec2 at = {piece.x, piece.z};
            if (Distance(at, map.center) > map.radius) continue;
            const float r = SceneryRadius(piece.kind, piece.scale);
            if (r > 0.0f) solids.push_back({at, r});
            const float dx = at.x - map.center.x, dz = at.z - map.center.z, d = (std::max)(1.0f, std::hypot(dx, dz));
            if (piece.kind == SceneryKind::Oak) anchors.push_back({at, AnchorKind::Scenery, {dx / d, dz / d}, r + 60.0f});
            else if (piece.kind == SceneryKind::Cliff) anchors.push_back({at, AnchorKind::CliffFoot, {std::sin(piece.yaw), std::cos(piece.yaw)}, 120.0f});   // cliffs face downhill
        }
    }
}

} // namespace fortnite
} // namespace royale
