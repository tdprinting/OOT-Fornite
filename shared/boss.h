#pragma once
#include "balance.h"
#include <cstdint>

namespace royale {

// The bosses are the game's own. Seven mini bosses guard the caves (and wild spots) of the map: the game's enemies, each in its own colours
// with moves and a twist the original never had. They chase and fight players who come close, take damage like anyone else, and drop several
// Epic and Legendary chests when they fall. Each place also has one major boss, the dungeon boss that belongs there, who arrives halfway
// through the match. The server runs them; clients just draw what they are told.
//
// The enum names are the old ones (they are the numbers that travel over the network); what each one is now:
//   Stone  Stalfos (gets back up once)           Lava   Magma Dodongo (rolls, leaving fire)   Frost  White Wolfos (freezing howl)
//   Moss   Moss Lizalfos (hides in the grass)    Tide   Big Octo (spinning charge)            Shade  Dead Hand (grabs, burrows)
//   Dune   Iron Knuckle (its armour breaks off at half health and it gets fast)
//   DragonFire Volvagia   DragonWater Morpha      DragonForest Phantom Ganon   DragonShadow Bongo Bongo   DragonSand Twinrova
// Append new kinds so the existing major boss IDs and authored region IDs stay stable.
enum class BossKind : uint8_t { Stone, Lava, Frost, Moss, Tide, Shade, Dune, DragonFire, DragonWater, DragonForest, DragonShadow, DragonSand, ChuRed, ChuGreen, ChuYellow, ChuBlue, ChuDark, Count };
constexpr int kMiniBossKindCount = 12;
inline constexpr BossKind kMiniBossKinds[kMiniBossKindCount] = {
    BossKind::Stone, BossKind::Lava, BossKind::Frost, BossKind::Moss, BossKind::Tide, BossKind::Shade, BossKind::Dune,
    BossKind::ChuRed, BossKind::ChuGreen, BossKind::ChuYellow, BossKind::ChuBlue, BossKind::ChuDark
};
constexpr bool IsChuKind(BossKind k) { return k >= BossKind::ChuRed && k <= BossKind::ChuDark; }
constexpr bool ChuCharged(BossKind k, float clock, bool dazed) {
    return !dazed && (k == BossKind::ChuYellow || k == BossKind::ChuBlue) && static_cast<int>(clock * 0.5f) % 3 != 2;
}
// "Dragon" kinds are the major bosses (they all fly, or float, or hide; only Volvagia is still a dragon).
constexpr bool IsDragonKind(BossKind k) { return k >= BossKind::DragonFire && k <= BossKind::DragonSand; }
constexpr bool IsMajorKind(BossKind k) { return IsDragonKind(k); }

// How a boss gets around the map when the straight way is blocked (a wall, a cliff, water, a gap between roofs). Mini bosses walk
// the navigation grid like the bots do and fall back on their own trick when there is no path; the major bosses travel their own way.
enum class Traverse : uint8_t {
    Leap,      // jumps the gap (Stalfos, White Wolfos, Iron Knuckle)
    Climb,     // crawls straight over walls and cliffs (the Lizalfos)
    Swim,      // goes straight through water (Big Octo)
    Burrow,    // sinks into the ground and comes up somewhere else (Dead Hand, Volvagia)
    Roll,      // curls up and rolls over whatever is in the way (the Magma Dodongo)
    Submerge,  // slides away out of sight under the water (Morpha)
    Warp,      // rides through a portal (Phantom Ganon)
    Vanish,    // fades into shadow and reappears (Bongo Bongo)
    Fly,       // flies straight there, fast (Twinrova on her brooms)
};

struct BossDef {
    const char* name;
    const char* title;   // the line under the name, as on the game's title cards
    float health;        // hearts of damage to bring it down
    float damage;        // hearts per blow
    float cooldown;      // seconds between attacks
    float speed;         // units per second (a running player is 100)
    float scale;         // how big it is drawn (1 = the standard mini boss size)
    int drops;           // chests dropped on death
    Traverse traverse;
    float altitude;      // major bosses: cruising height above the ground (0 for the ones on foot)
};

constexpr BossDef kBossDefs[] = {
    {"Stalfos", "Bones That Won't Stay Down", 16.0f, 0.8f, 1.7f, 66.0f, 1.0f, 3, Traverse::Leap, 0.0f},
    {"Magma Dodongo", "Molten Roller of the Crater", 22.0f, 1.0f, 1.6f, 55.0f, 1.15f, 4, Traverse::Roll, 0.0f},
    {"White Wolfos", "Howler of the Ice Cavern", 28.0f, 1.2f, 1.5f, 80.0f, 1.3f, 5, Traverse::Leap, 0.0f},
    {"Moss Lizalfos", "Lurker in the Grass", 20.0f, 0.9f, 1.7f, 70.0f, 1.1f, 3, Traverse::Climb, 0.0f},
    {"Big Octo", "Spinning Terror of the Deep", 24.0f, 1.0f, 1.6f, 66.0f, 1.2f, 4, Traverse::Swim, 0.0f},
    {"Dead Hand", "Horror at the Bottom of the Well", 26.0f, 1.1f, 1.5f, 48.0f, 1.25f, 4, Traverse::Burrow, 0.0f},
    {"Iron Knuckle", "Golden Guard of the Colossus", 22.0f, 1.0f, 1.6f, 52.0f, 1.15f, 4, Traverse::Leap, 0.0f},
    // The major bosses: damage is per attack; they have their own attacks (see below).
    {"Volvagia", "Subterranean Lava Dragon", 80.0f, 1.0f, 3.4f, 135.0f, 2.4f, 9, Traverse::Burrow, 380.0f},
    {"Morpha", "Giant Aquatic Amoeba", 80.0f, 1.0f, 3.4f, 110.0f, 2.4f, 9, Traverse::Submerge, 70.0f},
    {"Phantom Ganon", "Evil Spirit from Beyond", 80.0f, 1.0f, 3.4f, 140.0f, 2.4f, 9, Traverse::Warp, 360.0f},
    {"Bongo Bongo", "Phantom Shadow Beast", 80.0f, 1.0f, 3.4f, 120.0f, 2.4f, 9, Traverse::Vanish, 260.0f},
    {"Twinrova", "Sorceress Sisters", 80.0f, 1.0f, 3.4f, 160.0f, 2.4f, 9, Traverse::Fly, 430.0f},
    {"Red ChuChu", "Blazing Jelly Giant", 20.0f, 0.8f, 1.5f, 65.0f, 1.15f, 3, Traverse::Leap, 0.0f},
    {"Green ChuChu", "Vanishing Forest Jelly", 22.0f, 0.8f, 1.6f, 72.0f, 1.15f, 3, Traverse::Burrow, 0.0f},
    {"Yellow ChuChu", "Crackling Jelly Giant", 24.0f, 0.9f, 1.7f, 62.0f, 1.2f, 4, Traverse::Leap, 0.0f},
    {"Blue ChuChu", "Stormwater Jelly Giant", 26.0f, 1.0f, 1.8f, 55.0f, 1.2f, 4, Traverse::Swim, 0.0f},
    {"Dark ChuChu", "Petrifying Shadow Jelly", 24.0f, 0.9f, 1.8f, 58.0f, 1.2f, 4, Traverse::Burrow, 0.0f},
};
constexpr int kBossKindCount = sizeof(kBossDefs) / sizeof(kBossDefs[0]);
static_assert(kBossKindCount == static_cast<int>(BossKind::Count), "one definition per kind of boss");
constexpr BossDef BossOf(BossKind k) { return kBossDefs[static_cast<int>(k) < kBossKindCount ? static_cast<int>(k) : 0]; }

constexpr uint32_t kBossIdBase = 5000;       // boss ids live above every player id (humans 1..999, bots 1000..)
constexpr int kMaxBosses = 8;
constexpr float kBossAggroRange = 450.0f;    // how close a player has to be to be noticed
constexpr float kBossLeash = 1100.0f;        // how far from its home it will chase before giving up
constexpr float kBossReach = 105.0f;         // how far it reaches with a smash
constexpr float kBossWindupSeconds = 0.7f;   // it rears back this long before a blow lands (the animation shows it): time to roll or raise a shield
constexpr float kBossInterruptShare = 0.08f; // a blow this share of its health (a Master Sword hit is 9% of a Stalfos) knocks a mini boss out of its wind-up
constexpr float kBossBodyRadius = 70.0f;     // players hit it from this much further than a plain range check
constexpr int kPointsPerBossKill = 400;      // for landing the last hit

constexpr bool IsBossId(uint32_t id) { return id >= kBossIdBase && id < kBossIdBase + kMaxBosses; }

// The major boss is the dungeon boss of the place. It arrives halfway through the match (if the host leaves that on) over the safe zone.
// While it is up in the air only ranged weapons reach it; while it is hidden (underground, under water, in shadow, in a portal) nothing does;
// when it is down or dazed (Landed, Stunned) everything does, and it takes extra. Their attacks:
//   Volvagia       fire breath, fireballs (meteors when hurt), a dive; it burrows into the ground and bursts out under you with falling rocks.
//   Morpha         slides around under the water; its tentacle rises next to you, swings, and throws up geysers; then the core is exposed.
//   Phantom Ganon  energy-ball volleys, lightning from his spear, and a charge out of a portal; he crosses the map through portals.
//   Bongo Bongo    hand slams, a clap, a drum beat of shockwaves, a head charge; he vanishes into shadow and reappears beside you.
//   Twinrova       fire and ice beams in turn (fire burns, ice freezes), a fire-and-ice ring when hurt, a broom dive.
constexpr uint32_t kDragonId = kBossIdBase + kMaxBosses - 1;
constexpr float kDragonAltitude = 380.0f;       // cruising height above the ground (Volvagia; the others have their own in kBossDefs)
constexpr float kDragonAirborneAbove = 140.0f;  // above this only ranged weapons can hit it
constexpr float kDragonAggroRange = 1300.0f;
constexpr float kDragonBreathRange = 650.0f;
constexpr float kDragonBreathHalfAngle = 0.5f;  // radians either side of where it faces
constexpr float kDragonBreathSeconds = 1.8f;
constexpr float kDragonBreathDps = 0.6f;        // hearts a second to anyone in the cone
constexpr float kDragonStrikeRadius = 150.0f;
constexpr float kDragonStrikeDelay = 1.3f;      // seconds between the warning circle and the blast
constexpr float kDragonStrikeDamage = 1.0f;
constexpr float kDragonLandedSeconds = 5.0f;
constexpr float kDragonBodyRadius = 150.0f;
constexpr float kBossStunnedTakes = 1.25f;      // a boss that is down or dazed takes this much more
constexpr float kStalfosGetsUpWith = 0.3f;      // the Stalfos pulls itself back together once, with this much of its health
constexpr float kIronKnuckleBareSpeed = 1.6f;   // the Iron Knuckle without its armour: this much faster on its feet...
constexpr float kIronKnuckleBareSwing = 0.7f;   // ...and swinging this much more often

// What a boss is doing. The first seven are the dragon's original modes; mini bosses use the same list (Patrol when idle, Chase otherwise).
enum class DragonMode : uint8_t {
    Patrol, Chase, Breath, Cast, Swoop, Landed, Climb,
    Hidden,   // travelling out of sight: underground, under water, in shadow, through a portal. Nothing can hit it
    Emerge,   // bursting back out
    Slam,     // a big blow wound up (hand slam, clap, axe cleave, tentacle swing): it lands at the marked spots
    Beam,     // a ranged blast (Twinrova's fire or ice, Phantom Ganon's volley)
    Charge,   // rushing along a line (King Dodongo's roll, Big Octo's spin, Phantom Ganon's charge, Twinrova's broom dash)
    Leap,     // jumping (over a gap, or onto someone)
    Summon,   // eggs, grasping hands, geysers, a howl, a drum beat
    Stunned,  // dazed after a big move: now is the time to hit it
    Count
};
using BossMode = DragonMode;
constexpr bool BossHidden(DragonMode m) { return m == DragonMode::Hidden; }
constexpr bool BossDazed(DragonMode m) { return m == DragonMode::Landed || m == DragonMode::Stunned; }

// How a marked blast looks and what it does to whoever it catches (besides the damage).
enum class StrikeStyle : uint8_t {
    Fire,     // sets you alight
    Bolt,     // lightning: plain damage
    Ice,      // freezes you for a moment
    Water,    // a geyser: knocks you off your feet for a moment
    Shadow,   // grasping hands, shadow slams: hold you for a moment
    Rock,     // falling rocks, a landing, a shockwave: plain damage
    Magic,    // Phantom Ganon's energy: stuns briefly
    Spore,    // the Lizalfos's spore pods bursting: plain damage
    Shockwave, // a purple blast that launches players away from its center
    Count
};

constexpr StrikeStyle ChuStyle(BossKind k) {
    return k == BossKind::ChuRed ? StrikeStyle::Fire : k == BossKind::ChuGreen ? StrikeStyle::Spore :
           k == BossKind::ChuYellow || k == BossKind::ChuBlue ? StrikeStyle::Bolt : StrikeStyle::Shadow;
}

} // namespace royale
