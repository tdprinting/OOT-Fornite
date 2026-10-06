#pragma once
#include "fortnite_scenery.h"

namespace royale {
namespace fortnite {

// Standing water on the Fortnite Map: puddles where rain would collect, drawn all the time (the weather's own puddles come on top of them and grow
// in the rain). They lie in dips of level ground (lower than the ground round them), in the muddy dirt fields and swamp, and on the low ground
// beside the lakes and the river. Where each one is comes from the ground alone, so every player sees the same ones and nothing is sent.
constexpr float kPuddleCellSize = 190.0f;
constexpr float kPuddleReach = 1500.0f;

struct PuddlePiece {
    float x, z, y;
    float sx, sz;      // how the ground slopes under it, so the pool can lie along it
    float size;        // scale of the puddle mesh (1 is about 190 across)
    float yaw;
    uint32_t shape;    // 0..3, the mesh variant
};

inline bool PuddleIn(int cx, int cz, PuddlePiece* out) {
    const float x = (static_cast<float>(cx) + 0.2f + 0.6f * Scenery01(cx, cz, 41)) * kPuddleCellSize, z = (static_cast<float>(cz) + 0.2f + 0.6f * Scenery01(cx, cz, 42)) * kPuddleCellSize;
    float y;
    if (!GroundHeight(x, z, &y) || y < static_cast<float>(kWaterY) + 6.0f) return false;
    const Cover cover = CoverAt(x, z);
    if (cover != Cover::Meadow && cover != Cover::Dirt) return false;   // not on roads, in towns or under the woods
    if (GroundUp(x, z) < 0.985f) return false;                              // level ground only
    // Is it a dip? Compare with a ring of ground 190 away and with the ground right under the pool (it must lie flat there).
    float sum = 0.0f;
    for (int k = 0; k < 8; k++) {
        const float a = 0.785398f * static_cast<float>(k);
        float q;
        if (!GroundHeight(x + std::cos(a) * 190.0f, z + std::sin(a) * 190.0f, &q)) return false;
        sum += q;
    }
    const float dip = sum / 8.0f - y;
    float e, w, n, s;
    if (!GroundHeight(x + 55.0f, z, &e) || !GroundHeight(x - 55.0f, z, &w) || !GroundHeight(x, z + 55.0f, &n) || !GroundHeight(x, z - 55.0f, &s)) return false;
    if (std::fabs(e + w - 2.0f * y) > 6.0f || std::fabs(n + s - 2.0f * y) > 6.0f || std::fabs(e - w) > 30.0f || std::fabs(n - s) > 30.0f) return false;   // no bumps, no steep tilt
    const bool mud = cover == Cover::Dirt;
    const bool lowBank = y < static_cast<float>(kWaterY) + 150.0f;   // the ground beside the lakes and the river stays wet
    const float roll = Scenery01(cx, cz, 43);
    float chance = 0.0f;
    if (dip >= 1.5f) chance = 0.4f + (dip > 16.0f ? 0.3f : dip * 0.019f);   // rain collects in dips
    if (mud) chance = chance > 0.0f ? chance + 0.25f : (dip >= 0.5f ? 0.3f : 0.12f);   // fields and the swamp are boggy
    if (lowBank && chance < 0.3f) chance = 0.3f;
    if (roll >= chance) return false;
    *out = {x, z, y + 0.4f, (e - w) / 110.0f, (n - s) / 110.0f, 0.6f + (dip > 20.0f ? 20.0f : dip) * 0.045f + 0.5f * Scenery01(cx, cz, 44),
            Scenery01(cx, cz, 45) * 6.2831853f, SceneryHash(cx, cz, 46) % 4u};
    return true;
}

} // namespace fortnite
} // namespace royale
