#pragma once
#include "balance.h"
#include "rng.h"
#include <array>
#include <cmath>

namespace royale {

struct Vec2 {
    float x = 0, z = 0;
};

inline float Distance(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.z - b.z); }

struct Circle {
    Vec2 center;
    float radius = 0;
    bool Contains(Vec2 p) const {
        float dx = p.x - center.x, dz = p.z - center.z;
        return dx * dx + dz * dz <= radius * radius;
    }
};

// The storm is a pure function of (seed, map, time), so it needs no per-tick network traffic. Each phase's final circle
// lies fully inside the previous one. The SERVER is the source of truth: because std::sin/cos/sqrt are not guaranteed
// to be bit-identical across platforms (x86 Windows vs ARM Android), the server sends the 6 phase circles to clients at
// match start (see PhaseEnd) and clients interpolate from those, rather than re-deriving them from the seed.
class Storm {
  public:
    // Server: generate the phase circles from the match seed.
    Storm(uint64_t seed, Circle map) {
        Rng rng(seed);
        Circle cur = map;
        std::array<Circle, kStormPhaseCount> ends;
        for (int i = 0; i < kStormPhaseCount; i++) {
            Circle next;
            next.radius = map.radius * kStormPhases[i].endRadiusFrac;
            // Pick a centre such that `next` fits inside `cur`: |c' - c| <= r - r'.
            float maxOffset = cur.radius - next.radius;
            float angle = static_cast<float>(rng.Unit() * 6.283185307179586);
            float dist = maxOffset * std::sqrt(static_cast<float>(rng.Unit()));
            next.center = {cur.center.x + dist * std::cos(angle), cur.center.z + dist * std::sin(angle)};
            ends[i] = next;
            cur = next;
        }
        Init(map, ends);
    }

    // Client: rebuild the storm from the circles the server sent in Welcome.
    Storm(Circle map, const std::array<Circle, kStormPhaseCount>& phaseEnds) { Init(map, phaseEnds); }

    const std::array<Circle, kStormPhaseCount>& PhaseEnds() const { return end; }

    // Index of the phase active at time t (seconds since the storm started), or kStormPhaseCount once finished.
    int PhaseAt(float t) const {
        for (int i = 0; i < kStormPhaseCount; i++) {
            if (t < phaseStart[i] + kStormPhases[i].waitSec + kStormPhases[i].closeSec) {
                return i;
            }
        }
        return kStormPhaseCount;
    }

    Circle SafeZoneAt(float t) const {
        if (t < 0) {
            return start[0];
        }
        int p = PhaseAt(t);
        if (p >= kStormPhaseCount) {
            return end[kStormPhaseCount - 1];
        }
        float local = t - phaseStart[p];
        const auto& def = kStormPhases[p];
        if (local <= def.waitSec) {
            return start[p];
        }
        float k = (local - def.waitSec) / def.closeSec;
        return {{start[p].center.x + (end[p].center.x - start[p].center.x) * k,
                 start[p].center.z + (end[p].center.z - start[p].center.z) * k},
                start[p].radius + (end[p].radius - start[p].radius) * k};
    }

    // Hearts per second the storm deals to a player standing at `p` at time t.
    float DamagePerSecond(Vec2 p, float t) const {
        int phase = PhaseAt(t);
        int idx = phase >= kStormPhaseCount ? kStormPhaseCount - 1 : phase;
        return SafeZoneAt(t).Contains(p) ? 0.0f : kStormPhases[idx].damagePerSec;
    }

    float TotalDuration() const { return total; }
    const Circle& PhaseEnd(int i) const { return end[i]; }

  private:
    void Init(Circle map, const std::array<Circle, kStormPhaseCount>& ends) {
        end = ends;
        Circle cur = map;
        float t = 0;
        for (int i = 0; i < kStormPhaseCount; i++) {
            start[i] = cur;
            cur = end[i];
            phaseStart[i] = t;
            t += kStormPhases[i].waitSec + kStormPhases[i].closeSec;
        }
        total = t;
    }

    std::array<Circle, kStormPhaseCount> start, end;
    float phaseStart[kStormPhaseCount];
    float total;
};

} // namespace royale
