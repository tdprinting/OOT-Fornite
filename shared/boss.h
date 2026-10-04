#pragma once
#include "balance.h"
#include <cstdint>

namespace royale {

// Mini bosses: big golems that guard the caves (and wild spots) of the map. They chase and smash players who come close, take damage
// like anyone else, and drop several Epic and Legendary chests when they fall. The server runs them; clients just draw what they are told.
// Seven mini bosses (a golem each, coloured for the places they guard) and five dragons, one per kind of place: the major boss of a map.
enum class BossKind : uint8_t { Stone, Lava, Frost, Moss, Tide, Shade, Dune, DragonFire, DragonWater, DragonForest, DragonShadow, DragonSand, Count };
constexpr int kMiniBossKindCount = 7;
constexpr bool IsDragonKind(BossKind k) { return static_cast<int>(k) >= static_cast<int>(BossKind::DragonFire); }

struct BossDef {
    const char* name;
    float health;        // hearts of damage to bring it down
    float damage;        // hearts per smash
    float cooldown;      // seconds between smashes
    float speed;         // units per second (a running player is 100)
    float scale;         // how big it is drawn, 1 = the standard golem
    int drops;           // chests dropped on death
};

constexpr BossDef kBossDefs[] = {
    {"Stone Moan Golem", 16.0f, 0.8f, 1.5f, 60.0f, 1.0f, 3},
    {"Lava Java Golem", 22.0f, 1.0f, 1.4f, 68.0f, 1.15f, 4},
    {"Frost Lost Golem", 28.0f, 1.2f, 1.3f, 75.0f, 1.3f, 5},
    {"Mossy Glossy Golem", 20.0f, 0.9f, 1.5f, 62.0f, 1.1f, 3},
    {"Tidal Idol Golem", 24.0f, 1.0f, 1.4f, 70.0f, 1.2f, 4},
    {"Shade Parade Golem", 26.0f, 1.1f, 1.3f, 72.0f, 1.25f, 4},
    {"Dune Tune Golem", 22.0f, 1.0f, 1.4f, 68.0f, 1.15f, 4},
    // The major bosses: damage is per attack; they fly at 150 and have their own attacks (see below).
    {"Scorch Torch Dragon", 80.0f, 1.0f, 3.0f, 150.0f, 2.4f, 9},
    {"Tidal Bridal Leviathan", 80.0f, 1.0f, 3.0f, 150.0f, 2.4f, 9},
    {"Gnarly Barley Wyvern", 80.0f, 1.0f, 3.0f, 150.0f, 2.4f, 9},
    {"Gloom Doom Wraith", 80.0f, 1.0f, 3.0f, 150.0f, 2.4f, 9},
    {"Dusty Crusty Drake", 80.0f, 1.0f, 3.0f, 150.0f, 2.4f, 9},
};
constexpr int kBossKindCount = sizeof(kBossDefs) / sizeof(kBossDefs[0]);
constexpr BossDef BossOf(BossKind k) { return kBossDefs[static_cast<int>(k) < kBossKindCount ? static_cast<int>(k) : 0]; }

constexpr uint32_t kBossIdBase = 5000;       // boss ids live above every player id (humans 1..999, bots 1000..)
constexpr int kMaxBosses = 8;
constexpr float kBossAggroRange = 450.0f;    // how close a player has to be to be noticed
constexpr float kBossLeash = 1100.0f;        // how far from its home it will chase before giving up
constexpr float kBossReach = 105.0f;         // how far it reaches with a smash
constexpr float kBossBodyRadius = 70.0f;     // players hit it from this much further than a plain range check
constexpr int kPointsPerBossKill = 400;      // for landing the last hit

constexpr bool IsBossId(uint32_t id) { return id >= kBossIdBase && id < kBossIdBase + kMaxBosses; }

// The major boss, a dragon themed for the map (fire, water, forest, shadow or sand; they fight alike). It spawns halfway through the match (if the host leaves that on), flies around the safe zone, and fights with:
//   fire breath  a cone in front of it, burning anyone in it,
//   fireballs    ground strikes that land at a marked spot a moment later (and meteors when it is hurt): step out of the circle,
//   a swoop      a dive at someone, after which it lands and is vulnerable to everything for a few seconds.
// While it is in the air only ranged weapons reach it.
constexpr uint32_t kDragonId = kBossIdBase + kMaxBosses - 1;
constexpr float kDragonAltitude = 380.0f;       // cruising height above the ground
constexpr float kDragonAirborneAbove = 140.0f;  // above this only ranged weapons can hit it
constexpr float kDragonAggroRange = 1300.0f;
constexpr float kDragonBreathRange = 650.0f;
constexpr float kDragonBreathHalfAngle = 0.5f;  // radians either side of where it faces
constexpr float kDragonBreathSeconds = 1.8f;
constexpr float kDragonBreathDps = 0.8f;        // hearts a second to anyone in the cone
constexpr float kDragonStrikeRadius = 150.0f;
constexpr float kDragonStrikeDelay = 1.3f;      // seconds between the warning circle and the blast
constexpr float kDragonStrikeDamage = 1.3f;
constexpr float kDragonLandedSeconds = 5.0f;
constexpr float kDragonBodyRadius = 150.0f;
enum class DragonMode : uint8_t { Patrol, Chase, Breath, Cast, Swoop, Landed, Climb };

} // namespace royale
