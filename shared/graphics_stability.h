#pragma once
#include <algorithm>
#include <cmath>
namespace royale::graphics {
struct Cloud { float x, y, z, fade; };
inline float Wrap(float x, float period) {
    x = std::fmod(x, period);
    return x < 0.0f ? x + period : x;
}
// Camera-relative vertices plus the camera translation produce world positions.
// Wrap only outside the visible radius, with a smooth fade before recycling.
inline Cloud CloudPosition(float hx, float hz, float height, float eyeX, float eyeY, float eyeZ, float driftX, float driftZ) {
    constexpr float box = 12000.0f;
    const float x = Wrap(hx * box - eyeX + driftX, box) - box * 0.5f;
    const float z = Wrap(hz * box - eyeZ + driftZ, box) - box * 0.5f;
    const float edge = std::clamp((5600.0f - std::hypot(x, z)) / 1400.0f, 0.0f, 1.0f);
    return {x, 2500.0f + 1800.0f * height - eyeY, z, edge * edge * (3.0f - 2.0f * edge)};
}
// The island's base mesh is flattened to the water level. Negative wave troughs
// must stay above that opaque sheet instead of alternating in front/behind it.
inline float WaterOffset(float swell, float push, float shore) {
    return std::max(0.75f, 3.0f + (swell + push) * shore);
}
// The drawn water's height above the level the game reports. `base` is where calm water sits (the island's sea bed is drawn well below its
// sheet, so the sheet rides at the level itself; on lakes and rivers the game draws its own water there, so ours rides a little over it) and
// `lowest` is how far under the level a trough or a swimmer's dip may go: on the island the bed is 40 units down, so dips show; over the game's own
// water they would vanish behind it, so they stop just above it.
inline float WaterSurfaceOffset(float swell, float push, float shore, float base, float lowest) {
    return std::max(lowest, base + (swell + push) * shore);
}
}
