#pragma once
// Hireable allies: four people wandering the map who will join whoever pays them, follow that player and fight for them until the player is
// eliminated (then they are free to hire again) or the storm gets them. Their pay is in rupees, which come from bushes, rocks and the pockets
// of eliminated players.
#include "storm.h"
#include <cstdint>

namespace royale {

enum class AllyKind : uint8_t { Kokiri, Zora, Goron, Gerudo, Count };
constexpr int kAllyCount = static_cast<int>(AllyKind::Count);
constexpr int kMaxAlliesPerPlayer = 2;
constexpr float kHireRange = 230.0f;       // how close you have to be to hire one
constexpr float kAllyFollowDistance = 190.0f;
constexpr float kAllyLeash = 1500.0f;      // further than this from its owner and an ally hurries back (and is brought close beyond twice that)

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
};
constexpr const AllyDef& AllyOf(AllyKind k) { return kAllyDefs[static_cast<int>(k) % kAllyCount]; }

struct AllyState {
    uint8_t index = 0;                 // 0 to kAllyCount - 1, which is also its kind
    AllyKind kind = AllyKind::Kokiri;
    Vec2 pos;
    int16_t rot = 0;
    float health = 3.0f;
    bool alive = true;
    uint32_t owner = 0xFFFFFFFFu;      // the player it works for, or kNoPlayer (0xFFFFFFFF) while it is free to hire
    float attackReadyAt = 0, healReadyAt = 0;
    float actUntil = 0;                // just attacked: shown as swinging or shooting until then
    bool moving = false;
    bool Hired() const { return owner != 0xFFFFFFFFu; }
};

} // namespace royale
