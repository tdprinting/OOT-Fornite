#pragma once
#include <array>
#include <cstdint>

// All tunable numbers live here. Values are placeholders from docs/DESIGN.md and expected to change in playtests.
namespace royale {

constexpr int kMaxPlayers = 32;
constexpr int kTickHz = 20;

// Match timing, shared so clients can show the same countdown the server runs.
constexpr float kCountdownSec = 10.0f; // from Start to the drop
// The drop has no fixed length any more: it lasts while the Moonfall Bus crosses the island and everybody comes down (see the bus block below).
constexpr float kEndingSec = 10.0f;

// Lilo's accidents: a toxic green cloud that lingers where she made it. Stand in it for kFartGraceSeconds without leaving and it starts to hurt
// (kFartDps hearts a second); step out and the count drains away twice as fast as it built up. The server runs it (and tells everybody where the
// cloud is); every client draws it and warns the player standing in it with the same numbers.
constexpr float kFartCloudRadius = 260.0f;
constexpr float kFartCloudSeconds = 10.0f;
constexpr float kFartGraceSeconds = 3.0f;
constexpr float kFartDps = 0.3f;
constexpr float kFartCloudCooldown = 8.0f;   // one player can start a cloud this often
constexpr float kFartCloudReach = 450.0f;    // and only within this far of where they are (Lilo is right beside them)

// World units are OoT units. Link runs about 100 units/s.
constexpr float kRunSpeed = 100.0f;
constexpr float kPickupRange = 50.0f;

// The skydive at the start of the match (players and bots alike): you hang this high above the ground during the countdown, then fall
// during the drop, steering as you go. Holding Z dives faster.
constexpr float kSkyHeight = 3000.0f;   // units above the ground you start
constexpr float kGlideSpeed = 190.0f;   // units per second falling normally (the drop lasts 18 s)
constexpr float kDiveSpeed = 380.0f;    // diving
constexpr float kAirSpeed = 130.0f;     // steering speed
constexpr float kLateFallSpeed = 700.0f;   // still in the air when the drop ends: down you come

// The Moonfall Bus (docs/BATTLE_BUS.md, shared/bus.h): everyone starts the match aboard. It flies one straight line across the map, decided by the
// seed; you jump when you like once it is over the island, free-fall, open the glider and land. Whoever is still aboard when it leaves the island is
// pushed off. Players are safe while aboard, while falling, and for kLandedGraceSec after touching down.
constexpr float kBusAltitude = 3200.0f;      // how high it flies (world height; Moonfall's peaks are under 2000)
constexpr float kBusSpeed = 300.0f;          // units per second: a big map takes about 50 s to cross, as in Fortnite
constexpr float kBusMinSpeed = 120.0f;       // small maps slow it down, so a crossing is never short
constexpr float kBusMaxCrossSec = 60.0f;     // and a huge map speeds it up, so no crossing takes longer than this
constexpr float kBusMargin = 1500.0f;        // the line starts and ends this far outside the map circle
constexpr float kBusCorridor = 0.45f;        // the line passes the centre no further away than this share of the map radius
constexpr float kBusJumpOpen = 0.92f;        // jumping is allowed while the bus is inside this share of the map radius (over land)
constexpr float kBusClimbFrom = 600.0f;      // during the countdown the bus climbs from this height to kBusAltitude (only drawn)
constexpr float kFreefallSpeed = kDiveSpeed; // falling after the jump, before the glider opens
constexpr float kAutoGlideHeight = 600.0f;   // the glider opens by itself this high above the ground
constexpr float kBusExitPush = 1.0f;         // seconds a jumper keeps the bus's speed, so a jump carries you on ahead of it
constexpr float kLandedGraceSec = 2.0f;      // protection ends this long after touching down
constexpr float kDropExtraSec = 25.0f;       // the drop ends at the latest this long after the bus leaves the island
constexpr float kLandedDeadlineSec = 8.0f;   // anyone still not down this long into the match loses the protection anyway
constexpr float kBusHornCooldown = 1.5f;     // one honk per player this often

// Sprinting: faster running that drains a stamina bar, which refills after a short rest.
constexpr float kSprintMult = 1.35f;        // run speed while sprinting
constexpr float kSprintSeconds = 6.0f;      // a full bar lasts this long
constexpr float kStaminaRefill = 4.0f;      // seconds from empty to full once resting
constexpr float kStaminaRest = 1.0f;        // pause after sprinting before the bar starts to refill
constexpr float kSprintMinStamina = 0.15f;  // too winded to start below this
constexpr float kMaxHealth = 7.0f; // hearts everyone starts a match with (Heart Containers and Pieces still add up to kMaxHealthCap)
constexpr float kRareChestHeartPieceChance = 0.10f; // a Rare chest holds a Piece of Heart instead of its roll this often
constexpr int kChestRupeesMin = 2, kChestRupeesMax = 3; // rupees (green or blue) that spill out of an opened chest
constexpr int kPotionSlots = 5;   // the bag holds this many kinds of consumable; each kind stacks (see PotionStackMax in items.h), like Fortnite's med kits and shield potions
constexpr int kMaxPotions = 30;   // most single potions carried in all (kPotionSlots stacks of up to six)
constexpr int kWalletMax = 1000;  // the most rupees you can carry
constexpr float kLobbyAutoStartSec = 120.0f; // the lobby starts the match by itself after this long (the host can turn it off)
constexpr float kLobbyStartGraceSec = 30.0f; // if the host's game hasn't started it by then, the server does
constexpr float kMaxShield = 3.0f;   // the shield bar under the hearts, in hearts' worth of damage it soaks up (shown as 0 to 100)
constexpr int kMinPlayers = 2;        // the host's player-count slider runs from kMinPlayers to kMaxPlayers
constexpr int kMaxReserveWeapons = 2; // backup weapons carried besides the one in hand (the weapon part of the hotbar)
// Share of a player's items that is left on the ground when they are eliminated (money and ammo drop 60%).
constexpr float kDeathDropShare = 0.5f;

// Magic: abilities spend it, it comes back slowly by itself (about one use per cooldown), and Magic Jars refill it.
constexpr float kMaxMagic = 100.0f;
constexpr float kMagicRegenPerSec = 1.2f;

// Adult Power (a very rare Legendary find): you grow into adult Link for a while.
constexpr float kAdultSeconds = 60.0f;
constexpr float kAdultDamage = 1.4f;     // damage dealt
constexpr float kAdultTaken = 0.8f;      // damage taken
constexpr float kAdultSpeed = 1.1f;
constexpr float kAdultScale = 1.35f;     // how much bigger Link is drawn

constexpr int kMaxProps = 4000;   // was 1200 (a prop number is 16 bit on the wire, so 65535 is the ceiling)

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

// Overall damage dials, applied in Match::Damage on top of everything else. 1.0 = the raw numbers in combat.h, boss.h, vehicle.h.
constexpr float kPlayerDamageScale = 0.85f; // anything a player or bot does to another player (weapons, splash, burns, spells, allies, carts they drive)
constexpr float kBossDamageScale = 0.75f;   // mini bosses and the major boss
constexpr float kHazardDamageScale = 0.75f; // nobody's fault: lightning, crashes, Lilo's cloud (not the storm, which has its own table below)

constexpr int kStormPhaseCount = 6;
constexpr std::array<StormPhaseDef, kStormPhaseCount> kStormPhases = {{
    // Time to lose all 7 starting hearts standing outside (big map): 35 s, 17 s, 10 s, 7 s, 4.7 s, 3.5 s. Early storm is a nudge you can
    // run out of; late storm still kills, but a potion buys you real time. The shrinks are a little slower than before so there is time to run.
    {55, 65, 0.60f, 0.2f},
    {45, 55, 0.38f, 0.4f},
    {35, 45, 0.22f, 0.7f},
    {28, 38, 0.12f, 1.0f},
    {20, 32, 0.05f, 1.5f},
    {0, 25, 0.00f, 2.0f},
}};

} // namespace royale
