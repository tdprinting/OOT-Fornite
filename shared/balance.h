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
constexpr float kLobbyAutoStartSec = 120.0f; // the lobby starts the match by itself after this long (the host can turn it off)
constexpr float kLobbyStartGraceSec = 30.0f; // if the host's game hasn't started it by then, the server does
constexpr float kMaxShield = 3.0f;   // the shield bar under the hearts, in hearts' worth of damage it soaks up (shown as 0 to 100)
constexpr int kMinPlayers = 2;        // the host's player-count slider runs from kMinPlayers to kMaxPlayers
constexpr int kMaxReserveWeapons = 2; // backup weapons carried besides the one in hand (the weapon part of the hotbar)
// Share of a player's items that is left on the ground when they are eliminated (money and ammo drop 60%).
constexpr float kDeathDropShare = 0.5f;

constexpr int kMaxProps = 1200;

// Supply drops: a crate with guaranteed Legendary loot is announced, lands a few seconds later in the part of the map that is still safe, and everybody races for it.
constexpr float kSupplyFirstSec = 55.0f;   // match time of the first drop
constexpr float kSupplyEverySec = 70.0f;
constexpr float kSupplyWarningSec = 7.0f;  // from the announcement to the crate landing

// Match points: damage is the main thing, kills and surviving longer add to it, a win is a big bonus.
constexpr int kPointsPerHeartOfDamage = 100;
constexpr int kPointsPerKill = 500;
constexpr int kPointsPerChest = 25;
constexpr int kPointsPerPlacementStep = 20; // (kMaxPlayers - placement) steps
constexpr int kPointsForWinning = 1000;
inline int ScorePoints(float damage, int kills, int chests, int placement) {
    int points = static_cast<int>(damage * kPointsPerHeartOfDamage + 0.5f) + kills * kPointsPerKill + chests * kPointsPerChest;
    if (placement > 0) points += (kMaxPlayers - placement) * kPointsPerPlacementStep;
    if (placement == 1) points += kPointsForWinning;
    return points;
}

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
    {40, 50, 0.60f, 0.5f},
    {30, 40, 0.38f, 1.0f},
    {25, 35, 0.22f, 1.5f},
    {20, 30, 0.12f, 2.0f},
    {15, 25, 0.05f, 3.0f},
    {0, 20, 0.00f, 5.0f},
}};

} // namespace royale
