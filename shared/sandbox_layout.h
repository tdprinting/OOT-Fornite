#pragma once
#include "poi.h"
#include "sandbox_terrain.h"

namespace royale {

// What stands on the Sandbox test map's ground (shared/sandbox_terrain.h): its zones as places (so the HUD and the map name them), and the props that
// the bots and the player climb, hide behind and smash. Everything is laid out by hand, so the same every time.
inline constexpr float kSandboxLootRadius = 760.0f;   // the loot plaza, from its middle (the zone's own radius is the plaza's reach)

inline Vec2 SandboxSpawn() { return {sandbox::kSpawnX, sandbox::kSpawnZ}; }
inline Circle SandboxLootRoom() { return {{sandbox::kZones[1].x, sandbox::kZones[1].z}, kSandboxLootRadius}; }
inline Vec2 SandboxZoneAt(int zone) { const sandbox::Zone& z = sandbox::kZones[(std::max)(0, (std::min)(zone, sandbox::kZoneCount - 1))]; return {z.x, z.z}; }
// Where the carts stand at the start: three in the lot and one by the spawn pad.
inline std::vector<Vec2> SandboxCartSpots() { return {{1500.0f, -1900.0f}, {1700.0f, -2250.0f}, {1900.0f, -1900.0f}, {350.0f, -700.0f}}; }

inline PoiLayout GenerateSandboxLayout(uint64_t seed) {
    PoiLayout out;
    Rng rng(seed ^ 0x73616E64ull);   // "sand"
    for (int i = 0; i < sandbox::kZoneCount; i++) {
        Poi p;
        p.name = static_cast<uint8_t>(kSandboxMapIndex * kNamesPerMap + i);
        p.center = {sandbox::kZones[i].x, sandbox::kZones[i].z};
        p.radius = sandbox::kZones[i].radius;
        out.pois.push_back(p);
    }
    auto add = [&](float x, float z, PropKind kind, uint16_t rot = 0) { out.props.push_back({{x, z}, kind, rot}); };

    // The block course: two flights of stone steps (60, 120, 180 tall, 150 wide, side by side), and a row of blocks with gaps to jump.
    for (int k = 0; k < 3; k++) add(150.0f + 150.0f * k, -1500.0f, k == 0 ? PropKind::PlatformLow : k == 1 ? PropKind::PlatformMid : PropKind::PlatformHigh);
    for (int k = 0; k < 6; k++) add(150.0f + 150.0f * k, -1750.0f, k < 2 ? PropKind::PlatformLow : k < 4 ? PropKind::PlatformMid : PropKind::PlatformHigh);
    for (int k = 0; k < 4; k++) add(-200.0f - 300.0f * k, -1500.0f, PropKind::PlatformMid);   // a gap of 150 between each

    // The cover yard: a ring of boulders (every shape), pillars in the middle and a short wall of blocks.
    for (int k = 0; k < 8; k++) {
        const float a = 6.2831853f * k / 8.0f;
        add(1000.0f + std::cos(a) * 420.0f, -250.0f + std::sin(a) * 420.0f, PropKind::Boulder, RotForShape(rng, k % kBoulderShapes));
    }
    add(940.0f, -250.0f, PropKind::Pillar); add(1060.0f, -250.0f, PropKind::Pillar); add(1000.0f, -190.0f, PropKind::Pillar);
    for (int k = 0; k < 4; k++) add(700.0f + 150.0f * k, 250.0f, PropKind::PlatformMid);

    // Things to smash, in rows by the spawn pad: rocks, bushes and pillars.
    for (int k = 0; k < 6; k++) add(-375.0f + 150.0f * k, -900.0f, PropKind::Rock, static_cast<uint16_t>(rng.Below(0x10000)));
    for (int k = 0; k < 6; k++) add(-375.0f + 150.0f * k, -1000.0f, PropKind::Bush, static_cast<uint16_t>(rng.Below(0x10000)));
    for (int k = 0; k < 3; k++) add(-150.0f + 150.0f * k, -1100.0f, PropKind::Pillar);

    // The boulder gallery north of the pad: one of each shape.
    for (int k = 0; k < kBoulderShapes; k++) add(-750.0f + 300.0f * k, 450.0f, PropKind::Boulder, RotForShape(rng, k));

    // The loot plaza's corner pillars.
    for (int k = 0; k < 4; k++) add(k % 2 ? -1800.0f : -2800.0f, k < 2 ? -200.0f : 800.0f, PropKind::Pillar);
    return out;
}

} // namespace royale
