#pragma once
// Hireable allies: seven people wandering the map who will join whoever pays them, follow that player and fight for them until the player is
// eliminated (then they are free to hire again) or the storm gets them. Their pay is in rupees, which come from bushes, rocks and the pockets
// of eliminated players.
#include "storm.h"
#include <cstdint>

namespace royale {

enum class AllyKind : uint8_t { Kokiri, Zora, Goron, Gerudo, Jones, Peter, Chief, Count };   // append only: indices are on the wire
constexpr int kAllyCount = static_cast<int>(AllyKind::Count);
constexpr int kMaxAlliesPerPlayer = 2;
constexpr float kHireRange = 230.0f;       // how close you have to be to hire one
constexpr float kAllyFollowDistance = 190.0f;
constexpr float kAllyLeash = 1500.0f;      // further than this from its owner and an ally hurries back (and is brought close beyond twice that)

// Private NPC equipment: these are deliberately not ItemIds, ammo, inventory or loot.
enum class JonesGun : uint8_t { Assault, Burst, Carbine, Scoped, Count };
struct JonesGunDef { float range,damage,cooldown; };
inline constexpr JonesGunDef kJonesGuns[] = {
    {700,.45f,1.10f}, {950,.60f,1.50f}, {400,.30f,.75f}, {1250,.85f,2.10f}
};
constexpr JonesGun JonesGunAt(float distance) {
    return distance<275?JonesGun::Carbine:distance<550?JonesGun::Assault:distance<850?JonesGun::Burst:JonesGun::Scoped;
}
// Jones and Master Chief carry the same private rifles (same rules, same meshes); everyone else carries none.
constexpr bool CarriesJonesGuns(AllyKind k) { return k==AllyKind::Jones || k==AllyKind::Chief; }
constexpr bool AllyWeaponValid(uint8_t kind, uint8_t weapon) {
    return kind<static_cast<uint8_t>(AllyKind::Count) && CarriesJonesGuns(static_cast<AllyKind>(kind))?weapon<static_cast<uint8_t>(JonesGun::Count):weapon==0;
}

struct AllyDef {
    const char* name;
    const char* title;     // what they do, for the label
    int price;             // rupees
    float maxHealth;       // hearts
    float damage;          // hearts per hit
    float range;           // how far it hits from
    float cooldown;        // seconds between hits
    float speed;           // units per second (Link runs 100)
    bool melee;
    float healEvery;       // a Zora mends its owner: seconds between, 0 = never
    float healAmount;      // hearts
};

constexpr AllyDef kAllyDefs[kAllyCount] = {
    {"Kokiri",  "the Slinger",    40,  3.0f, 0.30f, 650.0f, 1.10f, 120.0f, false,  0.0f, 0.0f},
    {"Zora",    "the Tidecaller", 70,  4.0f, 0.35f, 600.0f, 1.40f, 118.0f, false, 24.0f, 0.5f},
    {"Goron",   "the Brawler",    90,  8.0f, 1.10f, 150.0f, 1.70f, 100.0f, true,   0.0f, 0.0f},
    {"Gerudo",  "the Archer",    110,  4.0f, 0.55f, 950.0f, 1.60f, 128.0f, false,  0.0f, 0.0f},
    {"Jones",   "the Rift Scout",100,  5.0f, 0.45f, 1250.0f,1.30f, 125.0f, false,  0.0f, 0.0f},
    {"Peter",   "the Quahog Brawler",85,7.0f, 0.90f, 160.0f, 1.50f, 112.0f, true,   0.0f, 0.0f},   // big haymaker, slow; takes phone calls when idle
    {"Master Chief","the Spartan",140,6.0f, 0.45f, 1250.0f,1.30f, 118.0f, false,  0.0f, 0.0f},  // Jones's rifles; tougher and pricier, a touch slower
};
constexpr const AllyDef& AllyOf(AllyKind k) { return kAllyDefs[static_cast<int>(k) % kAllyCount]; }

struct AllyState {
    uint8_t index = 0;                 // 0 to kAllyCount - 1, which is also its kind
    AllyKind kind = AllyKind::Kokiri;
    Vec2 pos;
    int16_t rot = 0;
    float health = 3.0f;
    bool alive = true;
    float fellAt = -100.0f;             // match clock when it was struck down: clients get it with zero health for a moment so the fall plays
    uint32_t owner = 0xFFFFFFFFu;      // the player it works for, or kNoPlayer (0xFFFFFFFF) while it is free to hire
    float attackReadyAt = 0, healReadyAt = 0;
    float actUntil = 0;                // just attacked: shown as swinging or shooting until then
    bool moving = false;
    uint8_t weapon = 0; // JonesGun for Jones and Chief; always zero for the rest
    bool Hired() const { return owner != 0xFFFFFFFFu; }
};

} // namespace royale
