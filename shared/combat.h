#pragma once
#include "balance.h"
#include "loot.h"
#include <algorithm>

// Combat numbers per item. Damage is in hearts before the rarity multiplier. All placeholders for playtesting.
namespace royale {

enum class ItemKind : uint8_t { Weapon, Shield, Potion, Utility };

struct WeaponStats {
    float damage;   // hearts per hit at Common
    float range;    // units
    float cooldown; // seconds between attacks
    bool ranged;
};

constexpr ItemKind KindOf(ItemId id) {
    switch (id) {
        case ItemId::DekuStick: case ItemId::KokiriSword: case ItemId::MasterSword: case ItemId::BiggoronSword:
        case ItemId::MegatonHammer: case ItemId::Slingshot: case ItemId::FairyBow: case ItemId::Boomerang:
        case ItemId::Bombs: case ItemId::Bombchus: case ItemId::DinsFire:
            return ItemKind::Weapon;
        case ItemId::DekuShield: case ItemId::HylianShield: case ItemId::MirrorShield:
            return ItemKind::Shield;
        case ItemId::GreenPotion: case ItemId::RedPotion: case ItemId::BluePotion:
            return ItemKind::Potion;
        default:
            return ItemKind::Utility; // Hookshot, Longshot, Farore's Wind, Nayru's Love: not handled yet
    }
}

constexpr WeaponStats WeaponOf(ItemId id) {
    switch (id) {
        case ItemId::DekuStick:     return {0.5f, 80, 0.7f, false};
        case ItemId::KokiriSword:   return {0.8f, 90, 0.6f, false};
        case ItemId::MasterSword:   return {1.5f, 100, 0.6f, false};
        case ItemId::BiggoronSword: return {2.0f, 120, 1.1f, false};
        case ItemId::MegatonHammer: return {2.5f, 110, 1.3f, false};
        case ItemId::Slingshot:     return {0.5f, 800, 0.8f, true};
        case ItemId::Boomerang:     return {0.6f, 500, 1.0f, true};
        case ItemId::FairyBow:      return {1.0f, 1200, 1.0f, true};
        case ItemId::Bombs:         return {1.8f, 600, 2.0f, true};
        case ItemId::Bombchus:      return {1.5f, 900, 2.5f, true};
        case ItemId::DinsFire:      return {1.2f, 300, 3.0f, true};
        default:                    return {0, 0, 1, false};
    }
}

inline float WeaponDps(ItemId id, Rarity r) {
    WeaponStats w = WeaponOf(id);
    return w.damage > 0 ? w.damage * static_cast<float>(kRarityMultiplier[static_cast<int>(r)]) / w.cooldown : 0.0f;
}

// Fraction of incoming damage the shield absorbs, capped at 75%.
inline float ShieldReduction(ItemId id, Rarity r) {
    float base = id == ItemId::DekuShield ? 0.20f : id == ItemId::HylianShield ? 0.35f : id == ItemId::MirrorShield ? 0.45f : 0.0f;
    return std::min(0.75f, base * static_cast<float>(kRarityMultiplier[static_cast<int>(r)]));
}

inline float PotionHeal(ItemId id, Rarity r) {
    float base = id == ItemId::GreenPotion ? 1.0f : id == ItemId::RedPotion ? 2.0f : id == ItemId::BluePotion ? 3.0f : 0.0f;
    return base * static_cast<float>(kRarityMultiplier[static_cast<int>(r)]);
}

} // namespace royale
