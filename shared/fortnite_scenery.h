#pragma once
#include "fortnite_map.h"
#include <cmath>
#include <cstdint>

namespace royale {
namespace fortnite {

// The Fortnite Map's Hyrule Field scenery (meshes.h, MeshKind::Scenery): lone oaks on the hilltops, flowering hedges along the edges of the
// woods and the towns' paving, drifts of flowers along the roads and in the meadows, cattails at the water, boulders at the shore, strata cliffs
// where the ground is steep and snow-capped crags on the highest ground. Where each one stands comes from the ground alone (height, slope, what the
// texture shows), so every player sees the same island without anything being sent; the game layer only adds the checks that need the match (the
// towns and props already on the ground) and draws them. Pure geometry and hashing: no game types, so the tests can check it.
enum class SceneryKind : uint8_t { Oak, Hedge, Boulder, Cliff, Crag, FlowersWhite, FlowersPink, Reeds, Count };   // the meshes.h item numbers, in order
constexpr float kSceneryCell = 150.0f;

struct SceneryPiece {
    float x, z;
    float y;       // the ground under it
    float scale;
    float yaw;     // cliffs face downhill
    SceneryKind kind;
};

// How far from the player each kind is drawn (and stands in the way): the big things show from afar, the small ones only close up.
constexpr float SceneryReach(SceneryKind k) {
    switch (k) {
        case SceneryKind::Crag: case SceneryKind::Cliff: return 3200.0f;
        case SceneryKind::Oak: return 2600.0f;
        case SceneryKind::Boulder: return 2000.0f;
        case SceneryKind::Hedge: return 1500.0f;
        default: return 1100.0f;
    }
}
constexpr float kSceneryMaxReach = 3200.0f;

inline uint32_t SceneryHash(int a, int b, int salt) {
    uint32_t h = static_cast<uint32_t>(a) * 374761393u + static_cast<uint32_t>(b) * 668265263u + static_cast<uint32_t>(salt) * 2246822519u + 0x1F123BB5u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
inline float Scenery01(int a, int b, int salt) { return static_cast<float>(SceneryHash(a, b, salt) & 0xFFFF) / 65535.0f; }

// The piece standing in cell (cx, cz), if any. `density` is the player's foliage option (about 0 to 1.3): fewer things on a slow phone.
inline bool SceneryIn(int cx, int cz, float density, SceneryPiece* out) {
    const float x = (static_cast<float>(cx) + 0.15f + 0.7f * Scenery01(cx, cz, 1)) * kSceneryCell, z = (static_cast<float>(cz) + 0.15f + 0.7f * Scenery01(cx, cz, 2)) * kSceneryCell;
    float y;
    if (!GroundHeight(x, z, &y) || y < static_cast<float>(kWaterY) + 2.0f) return false;
    const Cover cover = CoverAt(x, z);
    if (cover == Cover::Paving || cover == Cover::Water) return false;   // the towns have their own scenery
    const float dense = density < 0.0f ? 0.0f : density > 1.3f ? 1.3f : density;
    const float roll = Scenery01(cx, cz, 3), size = Scenery01(cx, cz, 4), turn = Scenery01(cx, cz, 5) * 6.2831853f;
    const float wl = static_cast<float>(kWaterY);
    auto at = [&](float dx, float dz) { float q; return GroundHeight(x + dx, z + dz, &q) ? q : -1.0e9f; };
    auto cov = [&](float dx, float dz) { return CoverAt(x + dx, z + dz); };
    *out = {x, z, y, 1.0f, turn, SceneryKind::Count};
    auto make = [&](SceneryKind k, float scale, float yaw) { *out = {x, z, y, scale, yaw, k}; return true; };

    const float up = GroundUp(x, z);
    // The water's edge (the texture's water, or ground that drops below the sea): cattails on the low banks, now and then a boulder.
    {
        const float er = 130.0f;
        const float lowest = std::fmin(std::fmin(at(er, 0), at(-er, 0)), std::fmin(at(0, er), at(0, -er)));
        const bool nearWater = (lowest < wl && lowest > -1.0e8f) || cov(er, 0) == Cover::Water || cov(-er, 0) == Cover::Water || cov(0, er) == Cover::Water || cov(0, -er) == Cover::Water;
        if (nearWater && up > 0.7f) {
            if (y < wl + 130.0f && roll < 0.45f * dense) return make(SceneryKind::Reeds, 0.9f + 0.4f * size, turn);
            if (roll > 0.9f && roll < 0.9f + 0.07f * dense) return make(SceneryKind::Boulder, 0.8f + 0.5f * size, turn);
            if (up >= 0.84f) return false;
        }
    }

    // Steep ground: cliffs facing downhill, with a boulder where the slope runs out.
    if (up < 0.84f) {
        const float r = 60.0f, hE = at(r, 0), hW = at(-r, 0), hN = at(0, r), hS = at(0, -r);
        const float gx = (hE - hW), gz = (hN - hS);   // uphill is (gx, gz): the cliff looks the other way
        if (hE < -1.0e8f || hW < -1.0e8f || hN < -1.0e8f || hS < -1.0e8f) return false;
        if (roll < 0.62f * dense) return make(SceneryKind::Cliff, 0.75f + 0.4f * size, std::atan2(-gx, -gz));
        if (roll < 0.76f * dense) return make(SceneryKind::Boulder, 0.8f + 0.5f * size, turn);
        return false;
    }

    // The highest ground: pink-lit snowy crags (the art's far mountains), more of them the higher it goes.
    if (y > 130.0f && cover != Cover::Woods && Scenery01(cx / 2, cz / 2, 9) < 0.55f && roll < (y > 200.0f ? 0.4f : 0.14f) * dense) return make(SceneryKind::Crag, 0.8f + 0.7f * size, turn);

    // Hilltops: a lone oak, as in the field's painted tree on its rise.
    const float hr = 130.0f;
    const float rise[4] = {at(hr, 0), at(-hr, 0), at(0, hr), at(0, -hr)};
    const bool top = y + 2.0f >= std::fmax(std::fmax(rise[0], rise[1]), std::fmax(rise[2], rise[3])) && y > std::fmin(std::fmin(rise[0], rise[1]), std::fmin(rise[2], rise[3])) + 10.0f;   // a rise, not just flat ground
    if (cover == Cover::Meadow && top && y > wl + 25.0f && roll < 0.45f * dense) return make(SceneryKind::Oak, 0.85f + 0.35f * size, turn);

    if (cover == Cover::Woods) return roll < 0.1f * dense ? make(SceneryKind::Hedge, 0.8f + 0.5f * size, turn) : false;   // undergrowth

    // Edges: hedges where the meadow meets the woods or a town, flowers along the dirt roads.
    const float e = 75.0f;
    const bool nearWoods = cov(e, 0) == Cover::Woods || cov(-e, 0) == Cover::Woods || cov(0, e) == Cover::Woods || cov(0, -e) == Cover::Woods;
    const bool nearTown = cov(e, 0) == Cover::Paving || cov(-e, 0) == Cover::Paving || cov(0, e) == Cover::Paving || cov(0, -e) == Cover::Paving;
    const bool nearRoad = cover == Cover::Dirt || cov(e, 0) == Cover::Dirt || cov(-e, 0) == Cover::Dirt || cov(0, e) == Cover::Dirt || cov(0, -e) == Cover::Dirt;
    if (nearTown && roll < 0.5f * dense) return make(SceneryKind::Hedge, 0.9f + 0.4f * size, turn);
    if (nearWoods && roll < 0.38f * dense) return make(SceneryKind::Hedge, 0.8f + 0.5f * size, turn);
    const SceneryKind flowers = Scenery01(cx / 3, cz / 3, 7) < 0.5f ? SceneryKind::FlowersWhite : SceneryKind::FlowersPink;
    if (nearRoad && roll < 0.6f * dense) return make(Scenery01(cx, cz, 8) < 0.5f ? SceneryKind::FlowersWhite : SceneryKind::FlowersPink, 1.3f + 0.7f * size, turn);

    // Meadows: drifts of flowers in patches (blocks of cells), a lone oak or a boulder now and then.
    if (cover == Cover::Meadow) {
        if (Scenery01(cx / 3, cz / 3, 6) < 0.36f && roll < 0.48f * dense) return make(flowers, 1.3f + 0.7f * size, turn);
        if (roll > 1.0f - 0.006f * dense) return make(SceneryKind::Oak, 0.85f + 0.35f * size, turn);
    }
    return false;
}

// Does a piece stand in the player's way? Oaks and boulders do (a circle on the ground, up to `top` high); the rest are walked through.
inline float SceneryRadius(SceneryKind k, float scale) {
    switch (k) {
        case SceneryKind::Oak: return 22.0f * scale + 10.0f;
        case SceneryKind::Boulder: return 58.0f * scale;
        default: return 0.0f;
    }
}
inline float SceneryTop(SceneryKind k, float scale) { return k == SceneryKind::Oak ? 200.0f * scale : k == SceneryKind::Boulder ? 76.0f * scale : 0.0f; }

} // namespace fortnite
} // namespace royale
