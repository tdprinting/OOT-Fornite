#pragma once
#include "balance.h"
#include <cstdint>

namespace royale {

// Mini bosses: big golems that guard the caves (and wild spots) of the map. They chase and smash players who come close, take damage
// like anyone else, and drop several Epic and Legendary chests when they fall. The server runs them; clients just draw what they are told.
enum class BossKind : uint8_t { Stone, Lava, Frost, Count };

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

} // namespace royale
