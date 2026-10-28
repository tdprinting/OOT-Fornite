#pragma once
#include "balance.h"
#include "rng.h"
#include "storm.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

// The Moonfall Bus's flight (docs/BATTLE_BUS.md section 4.2). One straight line across the map circle, chosen from the match seed on the server
// and sent to every client once (net::EvBusPath); after that every machine works out where the bus is from the time since the drop began, so
// nothing about the bus travels per tick. Header only: the server, the bots, the game and the tests all use it.
namespace royale {

struct BusPath {
    Vec2 start = {}, end = {};
    float altitude = kBusAltitude;
    float speed = kBusSpeed;
    float length = 0;
    Vec2 center = {};          // the map circle it was made for (the jump window is measured against it)
    float radius = 0;
    bool valid = false;
};

// Seconds from start to end.
inline float BusCrossSec(const BusPath& b) { return b.speed > 0 ? b.length / b.speed : 0.0f; }

// Where the bus is t seconds after the drop began (t is clamped to the line, so it waits at the start before and at the end after).
inline Vec2 BusPosAt(const BusPath& b, float t) {
    if (b.length <= 0) return b.start;
    const float k = std::clamp(t * b.speed / b.length, 0.0f, 1.0f);
    return {b.start.x + (b.end.x - b.start.x) * k, b.start.z + (b.end.z - b.start.z) * k};
}
// Unit direction of travel.
inline Vec2 BusDir(const BusPath& b) {
    const float dx = b.end.x - b.start.x, dz = b.end.z - b.start.z, l = std::hypot(dx, dz);
    return l > 0 ? Vec2{dx / l, dz / l} : Vec2{0, 1};
}
// Heading in radians, OoT style (0 = +z, growing towards +x).
inline float BusYaw(const BusPath& b) { const Vec2 d = BusDir(b); return std::atan2(d.x, d.z); }

// The stretch of the line inside the jump circle (kBusJumpOpen of the map radius) and the world square (kBusJumpWorldMax): the bus opens its
// doors at BusOpenSec and everyone still aboard is pushed off at BusCloseSec. Both are seconds since the drop began.
inline void BusWindow(const BusPath& b, float& open, float& close) {
    open = close = 0;
    if (b.length <= 0 || b.speed <= 0) return;
    const Vec2 d = BusDir(b);
    const float fx = b.start.x - b.center.x, fz = b.start.z - b.center.z;
    const float along = -(fx * d.x + fz * d.z);                          // distance along the line to the point nearest the centre
    const float off2 = fx * fx + fz * fz - along * along;                 // squared distance of the line from the centre
    const float r = b.radius * kBusJumpOpen;
    const float half = std::sqrt((std::max)(0.0f, r * r - off2));
    float lo = along - half, hi = along + half;                           // distances along the line
    const float s[2] = {b.start.x, b.start.z}, v[2] = {d.x, d.z};
    for (int k = 0; k < 2; ++k) {                                         // clip to the square, one axis at a time
        if (std::fabs(v[k]) < 1e-6f) { if (std::fabs(s[k]) > kBusJumpWorldMax) hi = lo; continue; }
        const float a = (-kBusJumpWorldMax - s[k]) / v[k], c = (kBusJumpWorldMax - s[k]) / v[k];
        lo = (std::max)(lo, (std::min)(a, c));
        hi = (std::min)(hi, (std::max)(a, c));
    }
    open = std::clamp(lo / b.speed, 0.0f, BusCrossSec(b));
    close = std::clamp(hi / b.speed, open, BusCrossSec(b));
}
inline float BusOpenSec(const BusPath& b) { float o, c; BusWindow(b, o, c); return o; }
inline float BusCloseSec(const BusPath& b) { float o, c; BusWindow(b, o, c); return c; }
// Over the island, with the doors open.
inline bool BusOverLand(const BusPath& b, float t) { float o, c; BusWindow(b, o, c); return t >= o && t <= c && c > o; }

// Seconds since the drop began at which the bus is nearest a point (clamped to the line).
inline float BusTimeNearest(const BusPath& b, Vec2 at) {
    if (b.speed <= 0) return 0;
    const Vec2 d = BusDir(b);
    const float along = (at.x - b.start.x) * d.x + (at.z - b.start.z) * d.z;
    return std::clamp(along, 0.0f, b.length) / b.speed;
}
// How far a point is from the line (the segment).
inline float BusLineDistance(const BusPath& b, Vec2 at) { return Distance(at, BusPosAt(b, BusTimeNearest(b, at))); }

// Build the line for a match. The seed picks a heading and a sideways offset (no further than kBusCorridor of the radius from the centre); the
// line is the chord across the circle at that offset, carried on kBusMargin past the shore at both ends. The speed fits the crossing to about 50 s,
// held between kBusMinSpeed and kBusSpeed, and raised if a huge map would otherwise take longer than kBusMaxCrossSec.
inline BusPath MakeBusPath(uint64_t seed, const Circle& map) {
    Rng rng(seed ^ 0x62757321ull);   // "bus!"
    const double bearing = rng.Unit() * 6.283185307179586;
    const float offset = static_cast<float>((rng.Unit() * 2.0 - 1.0) * kBusCorridor) * map.radius;
    const Vec2 d = {static_cast<float>(std::sin(bearing)), static_cast<float>(std::cos(bearing))};   // direction of travel
    const Vec2 side = {d.z, -d.x};
    const float half = std::sqrt((std::max)(0.0f, map.radius * map.radius - offset * offset)) + kBusMargin;
    const Vec2 mid = {map.center.x + side.x * offset, map.center.z + side.z * offset};
    BusPath b;
    b.start = {mid.x - d.x * half, mid.z - d.z * half};
    b.end = {mid.x + d.x * half, mid.z + d.z * half};
    b.length = half * 2.0f;
    b.speed = std::clamp(b.length / 50.0f, kBusMinSpeed, kBusSpeed);
    b.speed = (std::max)(b.speed, b.length / kBusMaxCrossSec);
    b.altitude = kBusAltitude;
    b.center = map.center;
    b.radius = map.radius;
    b.valid = true;
    return b;
}

// The same line rebuilt from what EvBusPath carries (the client's copy).
inline BusPath BusFromEnds(Vec2 start, Vec2 end, float altitude, float speed, const Circle& map) {
    BusPath b;
    b.start = start; b.end = end; b.altitude = altitude; b.speed = speed;
    b.length = Distance(start, end);
    b.center = map.center; b.radius = map.radius;
    b.valid = b.length > 0 && speed > 0;
    return b;
}

// The countdown's climb: from kBusClimbFrom to the cruising height, eased, over kCountdownSec (only drawn; the players aboard sit in their seats
// whatever the height).
inline float BusClimbAltitude(const BusPath& b, float countdownElapsed) {
    const float k = std::clamp(countdownElapsed / kCountdownSec, 0.0f, 1.0f);
    const float e = k * k * (3.0f - 2.0f * k);
    return kBusClimbFrom + (b.altitude - kBusClimbFrom) * e;
}

// Where things are on the bus, in its own frame: x to its right, y up from the floor, z forward (units). The benches are two rows of 16 facing
// forward, 54 apart, the rows 120 apart with an aisle between (docs/BATTLE_BUS.md section 2). These are the design's numbers; the model's exported
// Seat empties (shared/bus_geometry.h, once the model is built) are expected to match them.
constexpr int kBusSeatRows = 2, kBusSeatsPerRow = 16;
constexpr float kBusSeatPitch = 54.0f, kBusRowGap = 120.0f, kBusSeatHeight = 42.0f;
inline void BusSeatLocal(int seat, float* x, float* y, float* z) {
    const int s = std::clamp(seat, 0, kBusSeatRows * kBusSeatsPerRow - 1);
    const int row = s % kBusSeatRows, along = s / kBusSeatRows;            // seats fill front to back, left and right in turn
    *x = row == 0 ? -kBusRowGap * 0.5f : kBusRowGap * 0.5f;
    *y = kBusSeatHeight;
    *z = (static_cast<float>(kBusSeatsPerRow - 1) * 0.5f - static_cast<float>(along)) * kBusSeatPitch;
}
// The open left side, halfway along: where a jumper leaves from.
constexpr float kBusExitX = -230.0f, kBusExitY = 20.0f, kBusExitZ = 0.0f;
// The bus's frame in the world: its floor centre and heading, turned into world positions for local points.
inline void BusLocalToWorld(float yaw, float bx, float by, float bz, float lx, float ly, float lz, float* wx, float* wy, float* wz) {
    const float s = std::sin(yaw), c = std::cos(yaw);                     // forward is (s, c); right is (c, -s)
    *wx = bx + lz * s + lx * c;
    *wz = bz + lz * c - lx * s;
    *wy = by + ly;
}
// Where the bus is past either end of its line (it keeps flying on out to sea after the drop, for the people watching from the ground).
inline Vec2 BusPosUnclamped(const BusPath& b, float t) {
    const Vec2 d = BusDir(b);
    return {b.start.x + d.x * b.speed * t, b.start.z + d.z * b.speed * t};
}

// A jumper keeps the bus's speed for kBusExitPush seconds, fading out: how far that carries them along the line, t seconds after the jump.
inline float BusExitCarry(const BusPath& b, float t) {
    const float s = std::clamp(t, 0.0f, kBusExitPush);
    return b.speed * (s - s * s / (2.0f * kBusExitPush));
}

} // namespace royale
