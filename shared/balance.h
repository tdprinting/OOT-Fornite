#pragma once
#include <array>
#include <cstdint>

// All tunable numbers live here. Values are placeholders from docs/DESIGN.md and expected to change in playtests.
namespace royale {

constexpr int kMaxPlayers = 32;
constexpr int kTickHz = 20;

enum class Rarity : uint8_t { Common, Uncommon, Rare, Epic, Legendary, Count };
constexpr int kRarityCount = static_cast<int>(Rarity::Count);

// Spawn weight in percent, sums to 100.
constexpr std::array<int, kRarityCount> kRarityWeight = {40, 28, 18, 10, 4};
// Damage / effect multiplier for an item of this tier.
constexpr std::array<double, kRarityCount> kRarityMultiplier = {1.0, 1.15, 1.3, 1.5, 1.75};

struct StormPhaseDef {
    float waitSec;           // storm holds still
    float closeSec;          // storm shrinks to endRadiusFrac
    float endRadiusFrac;     // fraction of the map radius at the end of the phase
    float damagePerSec;      // hearts per second outside the safe zone
};

constexpr int kStormPhaseCount = 6;
constexpr std::array<StormPhaseDef, kStormPhaseCount> kStormPhases = {{
    {120, 90, 0.70f, 0.5f},
    {90, 60, 0.45f, 1.0f},
    {60, 60, 0.25f, 1.0f},
    {45, 45, 0.12f, 2.0f},
    {30, 30, 0.05f, 3.0f},
    {0, 20, 0.00f, 5.0f},
}};

} // namespace royale
