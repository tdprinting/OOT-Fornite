#pragma once
#include "fortnite_scenery.h"

namespace royale {
namespace fortnite {

// Where water stands on the Fortnite Map all the time (the ground patches of shared/ground_patches.h put a puddle on a seed there that is always
// shown, the rain makes more and grows them). Standing water collects in dips of level ground, in the muddy dirt fields and swamp, and on the low
// ground beside the lakes and the river. How wet a spot is comes from the ground alone, so every player sees the same ones and nothing is sent.
// Returns the chance (0 when the ground there is no place for water: sea, roads, towns, woods, steep or bumpy ground) that a seed at (x, z) is a
// standing puddle.
inline float PuddleWetness(float x, float z) {
    float y;
    if (!GroundHeight(x, z, &y) || y < static_cast<float>(kWaterY) + 6.0f) return 0.0f;
    const Cover cover = CoverAt(x, z);
    if (cover != Cover::Meadow && cover != Cover::Dirt) return 0.0f;   // not on roads, in towns or under the woods
    if (GroundUp(x, z) < 0.985f) return 0.0f;                              // level ground only
    // Is it a dip? Compare with a ring of ground 190 away and with the ground right under the pool (it must lie flat there).
    float sum = 0.0f;
    for (int k = 0; k < 8; k++) {
        const float a = 0.785398f * static_cast<float>(k);
        float q;
        if (!GroundHeight(x + std::cos(a) * 190.0f, z + std::sin(a) * 190.0f, &q)) return 0.0f;
        sum += q;
    }
    const float dip = sum / 8.0f - y;
    float e, w, n, s;
    if (!GroundHeight(x + 55.0f, z, &e) || !GroundHeight(x - 55.0f, z, &w) || !GroundHeight(x, z + 55.0f, &n) || !GroundHeight(x, z - 55.0f, &s)) return 0.0f;
    if (std::fabs(e + w - 2.0f * y) > 6.0f || std::fabs(n + s - 2.0f * y) > 6.0f || std::fabs(e - w) > 30.0f || std::fabs(n - s) > 30.0f) return 0.0f;   // no bumps, no steep tilt
    const bool mud = cover == Cover::Dirt;
    const bool lowBank = y < static_cast<float>(kWaterY) + 150.0f;   // the ground beside the lakes and the river stays wet
    float chance = 0.0f;
    if (dip >= 1.5f) chance = 0.4f + (dip > 16.0f ? 0.3f : dip * 0.019f);   // rain collects in dips
    if (mud) chance = chance > 0.0f ? chance + 0.25f : (dip >= 0.5f ? 0.3f : 0.12f);   // fields and the swamp are boggy
    if (lowBank && chance < 0.3f) chance = 0.3f;
    return chance;
}

} // namespace fortnite
} // namespace royale
