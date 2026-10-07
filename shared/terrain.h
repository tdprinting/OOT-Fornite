#pragma once
#include "loot.h"
#include <cmath>
#include <functional>
#include <vector>

namespace royale {

// The floor height under a point, as the host's game measures it (the scene's own floor, without the mod's scenery). False where there is none.
using HeightFn = std::function<bool(Vec2, float*)>;

// What the placement rules (props.h, poi.h, placement.h) ask of the ground: is there floor, is it level, where does it climb. Built from the same
// two probes the bots' navigation grid uses, so the chests, boulders and camps agree with where a player can really stand. With no height probe
// the ground is taken as flat.
class Ground {
  public:
    Ground() = default;
    Ground(PlacementFn valid, HeightFn height) : valid_(std::move(valid)), height_(std::move(height)) {}

    bool Valid(Vec2 p) const { return !valid_ || valid_(p); }
    bool HasHeights() const { return static_cast<bool>(height_); }
    bool Height(Vec2 p, float* y) const {
        if (!height_) { *y = 0.0f; return true; }
        return height_(p, y);
    }
    // The steepest rise per unit run between p and four points `step` away (0.3 is a gentle hill, 1 a 45 degree bank). 9 when there is no
    // floor at p or right beside it: the edge of the world, a cliff into the void.
    float Slope(Vec2 p, float step = 60.0f) const {
        if (!height_) return 0.0f;
        float centre;
        if (!height_(p, &centre)) return 9.0f;
        float worst = 0.0f;
        const Vec2 around[4] = {{p.x + step, p.z}, {p.x - step, p.z}, {p.x, p.z + step}, {p.x, p.z - step}};
        for (const Vec2& q : around) {
            float y;
            if (!height_(q, &y)) return 9.0f;
            worst = (std::max)(worst, std::fabs(y - centre) / step);
        }
        return worst;
    }
    // Floor to stand on that is not too steep.
    bool Level(Vec2 p, float maxSlope) const { return Valid(p) && Slope(p) <= maxSlope; }

  private:
    PlacementFn valid_;
    HeightFn height_;
};

// Landmarks of the ground itself, found once per match: the foot of a cliff or steep bank (boulders belong along it and a chest tucked in at the
// base), and lookouts (level ground that drops away on every side, where the best chests sit).
struct CliffFoot {
    Vec2 pos;       // level ground at the foot
    Vec2 uphill;    // unit vector from there up the slope
};
struct TerrainFeatures {
    std::vector<CliffFoot> cliffs;
    std::vector<Vec2> lookouts;
};

inline TerrainFeatures FindTerrainFeatures(const Circle& map, const Ground& ground) {
    TerrainFeatures out;
    if (!ground.HasHeights()) return out;
    const float cell = 140.0f, ring = 200.0f, pi = 3.14159265f;
    const int n = static_cast<int>(map.radius * 2.0f / cell);
    for (int j = 0; j <= n; j++) {
        for (int i = 0; i <= n; i++) {
            const Vec2 p = {map.center.x - map.radius + i * cell, map.center.z - map.radius + j * cell};
            if (Distance(p, map.center) > map.radius * 0.94f || !ground.Valid(p)) continue;
            float h;
            if (!ground.Height(p, &h)) continue;
            float up = -1.0e9f;
            Vec2 upDir{0, 0};
            bool allLower = true, ok = true;
            for (int k = 0; k < 8 && ok; k++) {
                const Vec2 d = {std::cos(k * pi / 4.0f), std::sin(k * pi / 4.0f)};
                float hq;
                if (!ground.Height({p.x + d.x * ring, p.z + d.z * ring}, &hq)) { ok = false; break; }
                if (hq - h > up) { up = hq - h; upDir = d; }
                if (hq - h > -70.0f) allLower = false;
            }
            if (!ok) continue;
            const bool cliff = up > 120.0f, lookout = allLower;
            if (!cliff && !lookout) continue;
            if (ground.Slope(p) > 0.3f) continue;
            if (cliff) {
                // Walk to the base of the wall: as far up as it stays gentle.
                Vec2 foot = p;
                for (float t = 20.0f; t <= ring; t += 20.0f) {
                    const Vec2 q = {p.x + upDir.x * t, p.z + upDir.z * t};
                    if (!ground.Valid(q) || ground.Slope(q, 40.0f) > 0.45f) break;
                    foot = q;
                }
                bool near = false;
                for (const CliffFoot& c : out.cliffs) if (Distance(c.pos, foot) < 300.0f) { near = true; break; }
                if (!near) out.cliffs.push_back({foot, upDir});
            } else {
                bool near = false;
                for (const Vec2& q : out.lookouts) if (Distance(q, p) < 800.0f) { near = true; break; }
                if (!near) out.lookouts.push_back(p);
            }
        }
    }
    return out;
}

} // namespace royale
