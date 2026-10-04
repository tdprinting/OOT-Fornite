#pragma once
#include "balance.h"
#include "rng.h"
#include "storm.h"
#include <functional>
#include <vector>

namespace royale {

enum class ItemId : uint8_t {
    DekuStick, KokiriSword, MasterSword, BiggoronSword, MegatonHammer,
    Slingshot, FairyBow, Boomerang, Hookshot, Longshot,
    Bombs, Bombchus,
    DinsFire, FaroresWind, NayrusLove,
    DekuShield, HylianShield, MirrorShield,
    GreenPotion, RedPotion, BluePotion,
    Count
};

struct ItemDef {
    ItemId id;
    const char* name;
    Rarity minRarity; // lowest tier this item can spawn at
    Rarity maxRarity; // highest tier
};

// Tier ranges follow the examples in docs/DESIGN.md section 4.3. Same item, different tier: damage and effect scale by
// kRarityMultiplier[tier].
constexpr ItemDef kItems[] = {
    {ItemId::DekuStick, "Deku Stick", Rarity::Common, Rarity::Uncommon},
    {ItemId::KokiriSword, "Kokiri Sword", Rarity::Common, Rarity::Rare},
    {ItemId::MasterSword, "Master Sword", Rarity::Epic, Rarity::Legendary},
    {ItemId::BiggoronSword, "Biggoron's Sword", Rarity::Rare, Rarity::Epic},
    {ItemId::MegatonHammer, "Megaton Hammer", Rarity::Epic, Rarity::Legendary},
    {ItemId::Slingshot, "Slingshot", Rarity::Common, Rarity::Rare},
    {ItemId::FairyBow, "Fairy Bow", Rarity::Rare, Rarity::Legendary},
    {ItemId::Boomerang, "Boomerang", Rarity::Uncommon, Rarity::Rare},
    {ItemId::Hookshot, "Hookshot", Rarity::Rare, Rarity::Epic},
    {ItemId::Longshot, "Longshot", Rarity::Epic, Rarity::Legendary},
    {ItemId::Bombs, "Bombs", Rarity::Uncommon, Rarity::Epic},
    {ItemId::Bombchus, "Bombchus", Rarity::Rare, Rarity::Epic},
    {ItemId::DinsFire, "Din's Fire", Rarity::Epic, Rarity::Legendary},
    {ItemId::FaroresWind, "Farore's Wind", Rarity::Legendary, Rarity::Legendary},
    {ItemId::NayrusLove, "Nayru's Love", Rarity::Legendary, Rarity::Legendary},
    {ItemId::DekuShield, "Deku Shield", Rarity::Common, Rarity::Rare},
    {ItemId::HylianShield, "Hylian Shield", Rarity::Uncommon, Rarity::Epic},
    {ItemId::MirrorShield, "Mirror Shield", Rarity::Epic, Rarity::Legendary},
    {ItemId::GreenPotion, "Green Potion", Rarity::Common, Rarity::Rare},
    {ItemId::RedPotion, "Red Potion", Rarity::Uncommon, Rarity::Epic},
    {ItemId::BluePotion, "Blue Potion", Rarity::Rare, Rarity::Legendary},
};
constexpr int kItemCount = sizeof(kItems) / sizeof(kItems[0]);
static_assert(kItemCount == static_cast<int>(ItemId::Count), "item table must cover every ItemId");

struct LootSpawn {
    Vec2 pos;
    ItemId item;
    Rarity rarity;
    bool fromChest;
};

inline Rarity RollRarity(Rng& rng, bool chest) {
    int roll = static_cast<int>(rng.Below(100));
    int acc = 0;
    int tier = kRarityCount - 1;
    for (int i = 0; i < kRarityCount; i++) {
        acc += kRarityWeight[i];
        if (roll < acc) {
            tier = i;
            break;
        }
    }
    // Chests shift one tier up.
    if (chest && tier < kRarityCount - 1) {
        tier++;
    }
    return static_cast<Rarity>(tier);
}

// Generate the match's loot from the seed. Every item is picked among those whose tier range contains the rolled tier,
// and, if none match, the roll is clamped to the closest tier an item supports, so a roll never produces an invalid
// (item, tier) pair.
// `valid`, when given, rejects positions the map can't actually be walked at (e.g. no floor there); up to 40 positions are tried
// per item before giving up and using the last one.
using PlacementFn = std::function<bool(Vec2)>;

inline Vec2 RandomPointIn(Rng& rng, const Circle& map, const PlacementFn& valid, float radiusFraction = 1.0f) {
    Vec2 p{};
    for (int attempt = 0; attempt < 40; attempt++) {
        float angle = static_cast<float>(rng.Unit() * 6.283185307179586);
        float dist = map.radius * radiusFraction * std::sqrt(static_cast<float>(rng.Unit()));
        p = {map.center.x + dist * std::cos(angle), map.center.z + dist * std::sin(angle)};
        if (!valid || valid(p)) break;
    }
    return p;
}

inline std::vector<LootSpawn> GenerateLoot(uint64_t seed, Circle map, int count, float chestFraction,
                                           const PlacementFn& valid = nullptr) {
    Rng rng(seed ^ 0x6C6F6F74ull); // "loot"
    std::vector<LootSpawn> out;
    out.reserve(count);
    for (int n = 0; n < count; n++) {
        bool chest = rng.Unit() < chestFraction;
        Rarity rolled = RollRarity(rng, chest);

        int candidates[kItemCount];
        int numCandidates = 0;
        for (int i = 0; i < kItemCount; i++) {
            if (rolled >= kItems[i].minRarity && rolled <= kItems[i].maxRarity) {
                candidates[numCandidates++] = i;
            }
        }
        const ItemDef& item = numCandidates > 0 ? kItems[candidates[rng.Below(numCandidates)]]
                                                : kItems[rng.Below(kItemCount)];
        Rarity tier = rolled;
        if (tier < item.minRarity) tier = item.minRarity;
        if (tier > item.maxRarity) tier = item.maxRarity;

        out.push_back({RandomPointIn(rng, map, valid), item.id, tier, chest});
    }
    return out;
}

} // namespace royale
