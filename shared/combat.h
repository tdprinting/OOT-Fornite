#pragma once
#include "balance.h"
#include "items.h"
#include "loot.h"
#include <algorithm>

// Combat numbers per item. Damage is in hearts before the rarity multiplier. All placeholders for playtesting.
namespace royale {

enum class WeaponEffect : uint8_t {
    None,
    Burn,         // sets the target on fire: effectAmount hearts per second for effectSeconds
    Freeze,       // target can't act for effectSeconds and takes 25% more damage while frozen
    Stun,         // target can't act for effectSeconds
    PierceShield, // ignores the target's shield
};

struct WeaponStats {
    float damage;   // hearts per hit at Common
    float range;    // units
    float cooldown; // seconds between attacks
    bool ranged;
    WeaponEffect effect = WeaponEffect::None;
    float effectSeconds = 0;
    float effectAmount = 0;
    float splashRadius = 0; // explosions also hurt everyone else this close to the target (not the attacker)
};

constexpr WeaponStats WeaponOf(ItemId id) {
    using E = WeaponEffect;
    switch (id) {
        case ItemId::DekuStick:     return {0.5f, 80, 0.7f, false};
        case ItemId::KokiriSword:   return {0.8f, 90, 0.6f, false};
        case ItemId::MasterSword:   return {1.5f, 100, 0.6f, false};
        case ItemId::BiggoronSword: return {2.0f, 120, 1.1f, false};
        case ItemId::MegatonHammer: return {2.5f, 110, 1.3f, false};
        case ItemId::Slingshot:     return {0.5f, 800, 0.8f, true};
        case ItemId::Boomerang:     return {0.6f, 500, 1.0f, true};
        case ItemId::FairyBow:      return {1.0f, 1200, 1.0f, true};
        case ItemId::Bombs:         return {1.8f, 600, 2.0f, true, E::None, 0, 0, 150};
        case ItemId::Bombchus:      return {1.5f, 900, 2.5f, true, E::None, 0, 0, 100};
        case ItemId::DekuNuts:      return {0.3f, 450, 3.0f, true, E::Stun, 2.0f};
        case ItemId::FireArrows:    return {0.9f, 1200, 1.2f, true, E::Burn, 3.0f, 0.25f};
        case ItemId::IceArrows:     return {0.8f, 1200, 1.4f, true, E::Freeze, 1.5f};
        case ItemId::LightArrows:   return {1.8f, 1500, 1.8f, true, E::PierceShield};
        default:                    return {0, 0, 1, false};
    }
}

inline float RarityScale(Rarity r) { return static_cast<float>(kRarityMultiplier[static_cast<int>(r)]); }

inline float WeaponDps(ItemId id, Rarity r) {
    WeaponStats w = WeaponOf(id);
    return w.damage > 0 ? w.damage * RarityScale(r) / w.cooldown : 0.0f;
}

// Fraction of incoming damage the shield absorbs, capped at 75%.
inline float ShieldReduction(ItemId id, Rarity r) {
    float base = id == ItemId::DekuShield ? 0.20f : id == ItemId::HylianShield ? 0.35f : id == ItemId::MirrorShield ? 0.45f : 0.0f;
    return (std::min)(0.75f, base * RarityScale(r));
}

inline float PotionHeal(ItemId id, Rarity r) { return PotionOf(id).heal * RarityScale(r); }

// What a gear item is worth at its rarity: each multiplier moves away from 1.0 by the rarity scale, so a Legendary piece does 1.75
// times as much as a Common one. Reductions never go below 20%.
inline float Scaled(float multiplier, Rarity r) {
    float v = 1.0f + (multiplier - 1.0f) * RarityScale(r);
    return multiplier < 1.0f ? (std::max)(0.2f, v) : v;
}

} // namespace royale
