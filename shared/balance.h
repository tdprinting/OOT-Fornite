#pragma once
#include <array>
#include <cstdint>

// All tunable numbers live here. Values are placeholders from docs/DESIGN.md and expected to change in playtests.
namespace royale {

constexpr int kMaxPlayers = 32;
constexpr int kTickHz = 20;

// Match timing, shared so clients can show the same countdown the server runs.
constexpr float kCountdownSec = 10.0f; // from Start to the drop
constexpr float kDropSec = 18.0f;      // the skydive: spawn protection while everyone falls from the sky
constexpr float kEndingSec = 10.0f;

// World units are OoT units. Link runs about 100 units/s.
constexpr float kRunSpeed = 100.0f;
constexpr float kPickupRange = 50.0f;
constexpr float kMaxHealth = 3.0f; // hearts
constexpr int kMaxPotions = 3;

// Anti-cheat plausibility limit for client-reported movement: rolls, Epona, Hookshot and Longshot pulls are all faster
// than running, so allow several times run speed. Faster than this in one update is clamped by the server.
constexpr float kMaxPlausibleSpeed = kRunSpeed * 5.0f;
constexpr float kMovementSlack = 100.0f; // units of free movement per update, for network jitter

enum class MatchState : uint8_t { Lobby, Countdown, Drop, InMatch, Ending };

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
