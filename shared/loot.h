#pragma once
#include "balance.h"
#include "items.h"
#include "rng.h"
#include "storm.h"
#include <functional>
#include <vector>

namespace royale {

struct LootSpawn {
    Vec2 pos;
    ItemId item;
    Rarity rarity;
    bool fromChest;           // rolled on the higher chest tiers
    bool container = false;   // shown as a treasure chest that has to be opened; false for items dropped by players
    bool special = false;     // a heart container chest: extra rare, drawn differently
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

// Pick an item for a rolled tier: first a kind (weighted, among kinds that have something at this tier), then one of that kind's
// eligible items. Returns false if nothing at all can spawn at the tier.
inline bool PickItem(Rng& rng, Rarity tier, ItemId* out) {
    int eligibleCount[kItemKindCount] = {};
    for (int i = 0; i < kItemCount; i++) {
        if (tier >= kItems[i].minRarity && tier <= kItems[i].maxRarity) eligibleCount[static_cast<int>(kItems[i].kind)]++;
    }
    int totalWeight = 0;
    for (int k = 0; k < kItemKindCount; k++) if (eligibleCount[k] > 0) totalWeight += kKindWeight[k];
    if (totalWeight == 0) return false;
    int roll = static_cast<int>(rng.Below(static_cast<uint32_t>(totalWeight)));
    int kind = 0;
    for (int k = 0; k < kItemKindCount; k++) {
        if (eligibleCount[k] == 0) continue;
        if (roll < kKindWeight[k]) { kind = k; break; }
        roll -= kKindWeight[k];
    }
    int pick = static_cast<int>(rng.Below(static_cast<uint32_t>(eligibleCount[kind])));
    for (int i = 0; i < kItemCount; i++) {
        if (static_cast<int>(kItems[i].kind) != kind || tier < kItems[i].minRarity || tier > kItems[i].maxRarity) continue;
        if (pick-- == 0) { *out = kItems[i].id; return true; }
    }
    return false;
}

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
        Rarity tier = RollRarity(rng, chest);
        ItemId item;
        if (!PickItem(rng, tier, &item)) item = static_cast<ItemId>(rng.Below(kItemCount));
        // An item can only exist within its own tier range; clamp so a roll never produces an invalid (item, tier) pair.
        if (tier < DefOf(item).minRarity) tier = DefOf(item).minRarity;
        if (tier > DefOf(item).maxRarity) tier = DefOf(item).maxRarity;
        out.push_back({RandomPointIn(rng, map, valid), item, tier, chest, true});
    }
    return out;
}

// One chest per spot (the buildings and caves of the points of interest), always on the better chest tiers.
inline std::vector<LootSpawn> GenerateSpotLoot(uint64_t seed, const std::vector<Vec2>& spots) {
    Rng rng(seed ^ 0x73706F74ull); // "spot"
    std::vector<LootSpawn> out;
    for (const Vec2& at : spots) {
        Rarity tier = RollRarity(rng, true);
        ItemId item;
        if (!PickItem(rng, tier, &item)) item = static_cast<ItemId>(rng.Below(kItemCount));
        if (tier < DefOf(item).minRarity) tier = DefOf(item).minRarity;
        if (tier > DefOf(item).maxRarity) tier = DefOf(item).maxRarity;
        out.push_back({at, item, tier, true, true});
    }
    return out;
}

} // namespace royale
