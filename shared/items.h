#pragma once
#include "balance.h"
#include <array>
#include <cstddef>
#include <cstdint>

// The item catalog: every Ocarina of Time item that can mean something in a battle royale, with what it does here.
//
// Not included, because they have no meaning in a match: rupees and wallets, dungeon keys, boss keys, maps and compasses, Gold
// Skulltula tokens, the trading-quest items (Cucco, eggs, letters, Cojiro...), the empty bottle, ammo (every weapon has unlimited
// ammo), the Triforce, and the Stone of Agony and Gerudo Card.
//
// Six kinds of item:
//   Weapon      held in the single weapon slot; picking one up swaps it with the old one (which drops).
//   Shield      the single shield slot; absorbs a share of incoming damage.
//   Consumable  goes in the bag (3 slots); D-pad Down uses one. Fairy is never drunk: it revives you once if you would die.
//   Instant     used the moment it is picked up (hearts, magic jar). Left on the ground if it would do nothing.
//   Ability     the single ability slot (spells, songs, hookshots...); D-pad Up uses it, then it needs time to recharge.
//   Gear        passive. Seven slots (tunic, boots, gauntlets, mask, scale, pack, charm), one item each.
// Everything scales with rarity: Legendary versions are 1.75 times as strong as Common ones.
namespace royale {

enum class ItemId : uint8_t {
    // ---- weapons
    DekuStick, KokiriSword, MasterSword, BiggoronSword, MegatonHammer,
    Slingshot, FairyBow, Boomerang, Bombs, Bombchus, DekuNuts, FireArrows, IceArrows, LightArrows,
    // ---- shields
    DekuShield, HylianShield, MirrorShield,
    // ---- consumables (bottle contents and potions)
    GreenPotion, RedPotion, BluePotion, Fairy, Milk, Fish, BlueFire, Bug, Poe, SmallShieldPotion, LargeShieldPotion,
    // ---- instant
    RecoveryHeart, HeartPiece, HeartContainer, MagicJar, AdultPower,
    // ---- abilities
    DinsFire, FaroresWind, NayrusLove, Hookshot, Longshot, LensOfTruth, MagicBeans, FairyOcarina, OcarinaOfTime,
    ZeldasLullaby, EponasSong, SariasSong, SunsSong, SongOfTime, SongOfStorms, MinuetOfForest, BoleroOfFire,
    SerenadeOfWater, NocturneOfShadow, RequiemOfSpirit, PreludeOfLight, ShockwaveGrenade,
    // ---- gear
    KokiriTunic, GoronTunic, ZoraTunic,
    KokiriBoots, IronBoots, HoverBoots,
    GoronBracelet, SilverGauntlets, GoldenGauntlets,
    KeatonMask, SkullMask, SpookyMask, BunnyHood, GoronMask, ZoraMask, GerudoMask, MaskOfTruth,
    SilverScale, GoldenScale,
    BigQuiver, BulletBag, BombBag,
    ForestMedallion, FireMedallion, WaterMedallion, SpiritMedallion, ShadowMedallion, LightMedallion,
    KokiriEmerald, GoronRuby, ZoraSapphire,
    // ---- special variants of weapons
    TripleSlingshot, GiantsHammer, HomingBombchus,
    // ---- the starting sword and the economy: never found as random loot (see InPool), given at the start, found in rocks and bushes, and
    // dropped by players who are eliminated
    BasicSword, Rupees, ArrowAmmo, SeedAmmo, BombAmmo, BombchuAmmo, NutAmmo,
    Count
};
constexpr int kItemCount = static_cast<int>(ItemId::Count);
constexpr int kPoolItemCount = static_cast<int>(ItemId::BasicSword); // the items that can turn up in random loot are the ones before the economy items
constexpr bool InPool(ItemId id) { return static_cast<int>(id) < kPoolItemCount; }

enum class ItemKind : uint8_t { Weapon, Shield, Consumable, Instant, Ability, Gear };
constexpr int kItemKindCount = 6;

enum class GearSlot : uint8_t { Tunic, Boots, Gauntlets, Mask, Scale, Pack, Charm, Count };
constexpr int kGearSlots = static_cast<int>(GearSlot::Count);

struct ItemDef {
    ItemId id;
    const char* name;
    ItemKind kind;
    Rarity minRarity; // lowest tier this item can spawn at
    Rarity maxRarity; // highest tier
    const char* effect; // one line for menus and labels
};

// Short names for the table below. Deliberately not single letters: game and Windows headers define macros like those.
namespace tbl {
constexpr Rarity rC = Rarity::Common, rU = Rarity::Uncommon, rR = Rarity::Rare, rE = Rarity::Epic, rL = Rarity::Legendary;
constexpr ItemKind kWeapon = ItemKind::Weapon, kShield = ItemKind::Shield, kConsumable = ItemKind::Consumable,
                   kInstant = ItemKind::Instant, kAbility = ItemKind::Ability, kGear = ItemKind::Gear;
} // namespace tbl
using namespace tbl;

// One entry per ItemId, in the same order (checked below).
constexpr ItemDef kItems[] = {
    // weapons
    {ItemId::DekuStick, "Deku Stick", kWeapon, rC, rU, "Weak melee"},
    {ItemId::KokiriSword, "Kokiri Sword", kWeapon, rC, rR, "Fast melee"},
    {ItemId::MasterSword, "Master Sword", kWeapon, rE, rL, "Strong melee"},
    {ItemId::BiggoronSword, "Biggoron's Sword", kWeapon, rR, rE, "Heavy melee, long reach"},
    {ItemId::MegatonHammer, "Megaton Hammer", kWeapon, rE, rL, "Slow, huge melee hit"},
    {ItemId::Slingshot, "Slingshot", kWeapon, rC, rR, "Weak, long-range"},
    {ItemId::FairyBow, "Fairy Bow", kWeapon, rR, rL, "Strong, long-range"},
    {ItemId::Boomerang, "Boomerang", kWeapon, rU, rR, "Mid-range, quick"},
    {ItemId::Bombs, "Bombs", kWeapon, rU, rE, "Thrown, explodes on everyone near the target"},
    {ItemId::Bombchus, "Bombchus", kWeapon, rR, rE, "Long-range explosive"},
    {ItemId::DekuNuts, "Deku Nuts", kWeapon, rC, rR, "Flash: stuns the target for 2 seconds"},
    {ItemId::FireArrows, "Fire Arrows", kWeapon, rR, rE, "Sets the target on fire"},
    {ItemId::IceArrows, "Ice Arrows", kWeapon, rR, rE, "Freezes the target: it can't act and takes extra damage"},
    {ItemId::LightArrows, "Light Arrows", kWeapon, rL, rL, "Huge damage that ignores shields"},
    // shields
    {ItemId::DekuShield, "Deku Shield", kShield, rC, rR, "Absorbs a little damage"},
    {ItemId::HylianShield, "Hylian Shield", kShield, rU, rE, "Absorbs a fair amount of damage"},
    {ItemId::MirrorShield, "Mirror Shield", kShield, rE, rL, "Absorbs a lot of damage"},
    // consumables
    {ItemId::GreenPotion, "Green Potion", kConsumable, rC, rR, "Heals 1 heart"},
    {ItemId::RedPotion, "Red Potion", kConsumable, rU, rE, "Heals 2 hearts"},
    {ItemId::BluePotion, "Blue Potion", kConsumable, rR, rL, "Heals 3 hearts"},
    {ItemId::Fairy, "Fairy", kConsumable, rU, rL, "Revives you once if you would die"},
    {ItemId::Milk, "Lon Lon Milk", kConsumable, rC, rU, "Heals 1.5 hearts"},
    {ItemId::Fish, "Fish", kConsumable, rC, rC, "Heals half a heart"},
    {ItemId::BlueFire, "Blue Fire", kConsumable, rU, rR, "Heals half a heart and puts out fire"},
    {ItemId::Bug, "Bugs", kConsumable, rC, rU, "Heals a little and cures fire and stun"},
    {ItemId::Poe, "Poe", kConsumable, rR, rE, "Take half damage for 6 seconds"},
    {ItemId::SmallShieldPotion, "Small Shield Potion", kConsumable, rC, rU, "Adds a third of a shield bar, up to half of it"},
    {ItemId::LargeShieldPotion, "Large Shield Potion", kConsumable, rR, rL, "Adds two thirds of a shield bar, up to all of it"},
    // instant
    {ItemId::RecoveryHeart, "Recovery Heart", kInstant, rC, rU, "Heals 1 heart on the spot"},
    {ItemId::HeartPiece, "Piece of Heart", kInstant, rU, rR, "Four make a Heart Container"},
    {ItemId::HeartContainer, "Heart Container", kInstant, rE, rL, "+1 maximum heart and heals it"},
    {ItemId::MagicJar, "Magic Jar", kInstant, rC, rR, "Refills your magic and recharges your ability"},
    {ItemId::AdultPower, "Adult Power", kInstant, rL, rL, "Grow into adult Link for a minute: hit harder, take less damage, run faster"},
    // abilities
    {ItemId::DinsFire, "Din's Fire", kAbility, rR, rL, "Fire burst around you"},
    {ItemId::FaroresWind, "Farore's Wind", kAbility, rE, rL, "Mark a spot, then jump back to it"},
    {ItemId::NayrusLove, "Nayru's Love", kAbility, rE, rL, "Invulnerable for 4 seconds"},
    {ItemId::Hookshot, "Hookshot", kAbility, rR, rE, "Pull the player in front of you to you"},
    {ItemId::Longshot, "Longshot", kAbility, rE, rL, "Pull from much farther away"},
    {ItemId::LensOfTruth, "Lens of Truth", kAbility, rU, rE, "See every player for 10 seconds"},
    {ItemId::MagicBeans, "Magic Beans", kAbility, rC, rR, "Heal over time for 10 seconds"},
    {ItemId::FairyOcarina, "Fairy Ocarina", kAbility, rC, rU, "Plays a random simple song"},
    {ItemId::OcarinaOfTime, "Ocarina of Time", kAbility, rL, rL, "Plays a random song from the whole list"},
    {ItemId::ZeldasLullaby, "Zelda's Lullaby", kAbility, rU, rE, "Heals 1 heart"},
    {ItemId::EponasSong, "Epona's Song", kAbility, rU, rR, "Run faster for 6 seconds"},
    {ItemId::SariasSong, "Saria's Song", kAbility, rU, rR, "See every player for 6 seconds"},
    {ItemId::SunsSong, "Sun's Song", kAbility, rR, rE, "Stuns everyone close to you"},
    {ItemId::SongOfTime, "Song of Time", kAbility, rL, rL, "Freezes everyone nearby while you can't be hurt"},
    {ItemId::SongOfStorms, "Song of Storms", kAbility, rR, rE, "Lightning strikes everyone near you"},
    {ItemId::MinuetOfForest, "Minuet of Forest", kAbility, rU, rR, "Run faster and heal half a heart"},
    {ItemId::BoleroOfFire, "Bolero of Fire", kAbility, rR, rE, "Sets everyone near you on fire"},
    {ItemId::SerenadeOfWater, "Serenade of Water", kAbility, rR, rE, "Heals 1.5 hearts and puts out fire"},
    {ItemId::NocturneOfShadow, "Nocturne of Shadow", kAbility, rR, rE, "Vanish and reappear somewhere else"},
    {ItemId::RequiemOfSpirit, "Requiem of Spirit", kAbility, rR, rE, "Stuns and hurts everyone near you"},
    {ItemId::PreludeOfLight, "Prelude of Light", kAbility, rR, rE, "Heals 1 heart and protects you briefly"},
    {ItemId::ShockwaveGrenade, "Shockwave Grenade", kAbility, rU, rL, "Launches you high into the air; no fall damage until you land"},
    // gear: tunics
    {ItemId::KokiriTunic, "Kokiri Tunic", kGear, rC, rU, "Plain: slightly less damage taken"},
    {ItemId::GoronTunic, "Goron Tunic", kGear, rU, rE, "Half damage from fire and explosions"},
    {ItemId::ZoraTunic, "Zora Tunic", kGear, rU, rE, "Less storm damage"},
    // boots
    {ItemId::KokiriBoots, "Kokiri Boots", kGear, rC, rU, "Plain: a little faster"},
    {ItemId::IronBoots, "Iron Boots", kGear, rU, rE, "Can't be stunned or frozen, slower"},
    {ItemId::HoverBoots, "Hover Boots", kGear, rR, rL, "Run noticeably faster"},
    // gauntlets
    {ItemId::GoronBracelet, "Goron's Bracelet", kGear, rC, rR, "+10% melee damage"},
    {ItemId::SilverGauntlets, "Silver Gauntlets", kGear, rU, rE, "+20% melee damage"},
    {ItemId::GoldenGauntlets, "Golden Gauntlets", kGear, rR, rL, "+35% melee damage"},
    // masks
    {ItemId::KeatonMask, "Keaton Mask", kGear, rC, rU, "A little less storm damage"},
    {ItemId::SkullMask, "Skull Mask", kGear, rC, rR, "+10% ranged damage"},
    {ItemId::SpookyMask, "Spooky Mask", kGear, rC, rR, "8% less damage taken"},
    {ItemId::BunnyHood, "Bunny Hood", kGear, rU, rE, "Run 20% faster"},
    {ItemId::GoronMask, "Goron Mask", kGear, rC, rR, "Less fire and explosion damage"},
    {ItemId::ZoraMask, "Zora Mask", kGear, rC, rR, "Less storm damage"},
    {ItemId::GerudoMask, "Gerudo Mask", kGear, rC, rR, "+10% melee damage"},
    {ItemId::MaskOfTruth, "Mask of Truth", kGear, rU, rE, "+5% ranged damage and 5% less damage taken"},
    // scales
    {ItemId::SilverScale, "Silver Scale", kGear, rC, rU, "A little less storm damage"},
    {ItemId::GoldenScale, "Golden Scale", kGear, rU, rR, "Less storm damage"},
    // packs
    {ItemId::BigQuiver, "Big Quiver", kGear, rU, rE, "+15% ranged damage"},
    {ItemId::BulletBag, "Bullet Bag", kGear, rC, rR, "+10% ranged damage"},
    {ItemId::BombBag, "Bomb Bag", kGear, rC, rR, "+10% ranged damage"},
    // charms (medallions and spiritual stones)
    {ItemId::ForestMedallion, "Forest Medallion", kGear, rR, rL, "Run 12% faster"},
    {ItemId::FireMedallion, "Fire Medallion", kGear, rR, rL, "60% less fire damage"},
    {ItemId::WaterMedallion, "Water Medallion", kGear, rR, rL, "40% less storm damage"},
    {ItemId::SpiritMedallion, "Spirit Medallion", kGear, rR, rL, "+20% melee damage"},
    {ItemId::ShadowMedallion, "Shadow Medallion", kGear, rR, rL, "+20% ranged damage"},
    {ItemId::LightMedallion, "Light Medallion", kGear, rR, rL, "12% less damage taken"},
    {ItemId::KokiriEmerald, "Kokiri's Emerald", kGear, rE, rL, "5% less damage taken, a little faster"},
    {ItemId::GoronRuby, "Goron's Ruby", kGear, rE, rL, "Half the explosion damage, 40% less fire damage"},
    {ItemId::ZoraSapphire, "Zora's Sapphire", kGear, rE, rL, "25% less storm damage"},
    // special weapon variants
    {ItemId::TripleSlingshot, "Triple Slingshot", kWeapon, rU, rE, "Fires three seeds at once: close up they all land"},
    {ItemId::GiantsHammer, "Giant's Hammer", kWeapon, rE, rL, "A huge slow slam that hits everyone around the target"},
    {ItemId::HomingBombchus, "Homing Bombchus", kWeapon, rE, rL, "Purple bombchus that chase their target down"},
    // starting sword and economy
    {ItemId::BasicSword, "Basic Sword", kWeapon, rC, rC, "Your starting sword: weak, but it never runs out"},
    {ItemId::Rupees, "Rupees", kInstant, rC, rC, "Money: hire helpers who follow and fight for you"},
    {ItemId::ArrowAmmo, "Arrows", kInstant, rC, rC, "Ammo for the bow and the elemental arrows"},
    {ItemId::SeedAmmo, "Deku Seeds", kInstant, rC, rC, "Ammo for the slingshot"},
    {ItemId::BombAmmo, "Bombs (ammo)", kInstant, rC, rC, "Ammo for thrown bombs"},
    {ItemId::BombchuAmmo, "Bombchus (ammo)", kInstant, rC, rC, "Ammo for bombchus"},
    {ItemId::NutAmmo, "Deku Nuts (ammo)", kInstant, rC, rC, "Ammo for deku nuts"},
};
static_assert(sizeof(kItems) / sizeof(kItems[0]) == static_cast<size_t>(kItemCount), "item table must have one entry per ItemId");

constexpr bool ItemTableInOrder() {
    for (int i = 0; i < kItemCount; i++) {
        if (static_cast<int>(kItems[i].id) != i) return false;
        if (kItems[i].minRarity > kItems[i].maxRarity) return false;
    }
    return true;
}
static_assert(ItemTableInOrder(), "kItems must be in ItemId order with min <= max rarity");

constexpr const ItemDef& DefOf(ItemId id) { return kItems[static_cast<int>(id)]; }
constexpr ItemKind KindOf(ItemId id) { return DefOf(id).kind; }
constexpr const char* NameOf(ItemId id) { return DefOf(id).name; }

// How often each kind shows up in loot, as a percentage. Picked before the item, so the huge gear and ability lists don't drown out weapons.
constexpr std::array<int, kItemKindCount> kKindWeight = {26, 8, 20, 8, 14, 24};

// ---- gear -------------------------------------------------------------------------------------------------------------------

// Multipliers at Common. Rarity scales how far each moves from 1.0 (see GearTotals).
struct GearDef {
    GearSlot slot;
    float melee = 1, ranged = 1;   // damage you deal
    float damageTaken = 1;         // all damage you take
    float storm = 1, fire = 1, explosion = 1; // extra multipliers for those damage types
    float speed = 1;               // movement speed
    bool stunImmune = false;
};

constexpr GearDef GearOf(ItemId id) {
    using S = GearSlot;
    switch (id) {
        case ItemId::KokiriTunic:     return {S::Tunic, 1, 1, 0.97f};
        case ItemId::GoronTunic:      return {S::Tunic, 1, 1, 1, 1, 0.5f, 0.5f};
        case ItemId::ZoraTunic:       return {S::Tunic, 1, 1, 0.95f, 0.7f};
        case ItemId::KokiriBoots:     return {S::Boots, 1, 1, 1, 1, 1, 1, 1.03f};
        case ItemId::IronBoots:       return {S::Boots, 1, 1, 0.9f, 1, 1, 1, 0.9f, true};
        case ItemId::HoverBoots:      return {S::Boots, 1, 1, 1, 1, 1, 1, 1.2f};
        case ItemId::GoronBracelet:   return {S::Gauntlets, 1.1f};
        case ItemId::SilverGauntlets: return {S::Gauntlets, 1.2f};
        case ItemId::GoldenGauntlets: return {S::Gauntlets, 1.35f};
        case ItemId::KeatonMask:      return {S::Mask, 1, 1, 1, 0.9f};
        case ItemId::SkullMask:       return {S::Mask, 1, 1.1f};
        case ItemId::SpookyMask:      return {S::Mask, 1, 1, 0.92f};
        case ItemId::BunnyHood:       return {S::Mask, 1, 1, 1, 1, 1, 1, 1.2f};
        case ItemId::GoronMask:       return {S::Mask, 1, 1, 1, 1, 0.8f, 0.7f};
        case ItemId::ZoraMask:        return {S::Mask, 1, 1, 1, 0.85f};
        case ItemId::GerudoMask:      return {S::Mask, 1.1f};
        case ItemId::MaskOfTruth:     return {S::Mask, 1, 1.05f, 0.95f};
        case ItemId::SilverScale:     return {S::Scale, 1, 1, 1, 0.95f};
        case ItemId::GoldenScale:     return {S::Scale, 1, 1, 1, 0.9f};
        case ItemId::BigQuiver:       return {S::Pack, 1, 1.15f};
        case ItemId::BulletBag:       return {S::Pack, 1, 1.1f};
        case ItemId::BombBag:         return {S::Pack, 1, 1.1f};
        case ItemId::ForestMedallion: return {S::Charm, 1, 1, 1, 1, 1, 1, 1.12f};
        case ItemId::FireMedallion:   return {S::Charm, 1, 1, 1, 1, 0.4f};
        case ItemId::WaterMedallion:  return {S::Charm, 1, 1, 1, 0.6f};
        case ItemId::SpiritMedallion: return {S::Charm, 1.2f};
        case ItemId::ShadowMedallion: return {S::Charm, 1, 1.2f};
        case ItemId::LightMedallion:  return {S::Charm, 1, 1, 0.88f};
        case ItemId::KokiriEmerald:   return {S::Charm, 1, 1, 0.95f, 1, 1, 1, 1.05f};
        case ItemId::GoronRuby:       return {S::Charm, 1, 1, 1, 1, 0.6f, 0.5f};
        case ItemId::ZoraSapphire:    return {S::Charm, 1, 1, 1, 0.75f};
        default:                      return {S::Count};
    }
}

// ---- consumables and instant items -----------------------------------------------------------------------------------------

struct PotionDef {
    float heal = 0;          // hearts at Common
    bool cleanse = false;    // puts out fire and ends stun
    float damageTaken = 1;   // temporary damage multiplier (Poe)
    float seconds = 0;       // how long that lasts
    bool revive = false;     // Fairy: used automatically on a lethal hit, never drunk
    float shield = 0;        // shield bar points added (in hearts' worth of damage), drunk with its own button
    float shieldCap = 0;     // it can't raise the shield bar above this
};

constexpr PotionDef PotionOf(ItemId id) {
    switch (id) {
        case ItemId::GreenPotion: return {1.0f};
        case ItemId::RedPotion:   return {2.0f};
        case ItemId::BluePotion:  return {3.0f};
        case ItemId::Fairy:       return {0, false, 1, 0, true};
        case ItemId::Milk:        return {1.5f};
        case ItemId::Fish:        return {0.5f};
        case ItemId::BlueFire:    return {0.5f, true};
        case ItemId::Bug:         return {0.3f, true};
        case ItemId::Poe:         return {0, false, 0.5f, 6.0f};
        case ItemId::SmallShieldPotion: return {0, false, 1, 0, false, 1.0f, 1.5f};
        case ItemId::LargeShieldPotion: return {0, false, 1, 0, false, 2.0f, 3.0f};
        default:                  return {};
    }
}

enum class InstantEffect : uint8_t { None, Heart, HeartPiece, HeartContainer, MagicJar, AdultPower, Rupees, Ammo };
constexpr InstantEffect InstantOf(ItemId id) {
    switch (id) {
        case ItemId::RecoveryHeart: return InstantEffect::Heart;
        case ItemId::HeartPiece: return InstantEffect::HeartPiece;
        case ItemId::HeartContainer: return InstantEffect::HeartContainer;
        case ItemId::MagicJar: return InstantEffect::MagicJar;
        case ItemId::AdultPower: return InstantEffect::AdultPower;
        case ItemId::Rupees: return InstantEffect::Rupees;
        case ItemId::ArrowAmmo: case ItemId::SeedAmmo: case ItemId::BombAmmo: case ItemId::BombchuAmmo: case ItemId::NutAmmo: return InstantEffect::Ammo;
        default: return InstantEffect::None;
    }
}

// ---- ammo --------------------------------------------------------------------------------------------------------------------
// Bows, the slingshot and the thrown weapons spend one piece of ammo per shot. Picking up one of them gives you a few to start with; more
// comes from rocks, bushes and eliminated players. With none left a weapon is just something to hit with (see ActiveWeapon in combat.h).
enum class AmmoKind : uint8_t { Arrows, Seeds, Bombs, Bombchus, Nuts, None };
constexpr int kAmmoKinds = 5;
constexpr AmmoKind AmmoUsedBy(ItemId weapon) {
    switch (weapon) {
        case ItemId::FairyBow: case ItemId::FireArrows: case ItemId::IceArrows: case ItemId::LightArrows: return AmmoKind::Arrows;
        case ItemId::Slingshot: case ItemId::TripleSlingshot: return AmmoKind::Seeds;
        case ItemId::Bombs: return AmmoKind::Bombs;
        case ItemId::Bombchus: case ItemId::HomingBombchus: return AmmoKind::Bombchus;
        case ItemId::DekuNuts: return AmmoKind::Nuts;
        default: return AmmoKind::None;
    }
}
constexpr AmmoKind AmmoGivenBy(ItemId pickup) {
    switch (pickup) {
        case ItemId::ArrowAmmo: return AmmoKind::Arrows;
        case ItemId::SeedAmmo: return AmmoKind::Seeds;
        case ItemId::BombAmmo: return AmmoKind::Bombs;
        case ItemId::BombchuAmmo: return AmmoKind::Bombchus;
        case ItemId::NutAmmo: return AmmoKind::Nuts;
        default: return AmmoKind::None;
    }
}
constexpr ItemId AmmoItem(AmmoKind k) {
    switch (k) {
        case AmmoKind::Arrows: return ItemId::ArrowAmmo;
        case AmmoKind::Seeds: return ItemId::SeedAmmo;
        case AmmoKind::Bombs: return ItemId::BombAmmo;
        case AmmoKind::Bombchus: return ItemId::BombchuAmmo;
        default: return ItemId::NutAmmo;
    }
}
constexpr const char* AmmoName(AmmoKind k) {
    switch (k) {
        case AmmoKind::Arrows: return "Arrows";
        case AmmoKind::Seeds: return "Seeds";
        case AmmoKind::Bombs: return "Bombs";
        case AmmoKind::Bombchus: return "Bombchus";
        case AmmoKind::Nuts: return "Nuts";
        default: return "";
    }
}
constexpr int AmmoStarter(AmmoKind k) { return k == AmmoKind::Arrows ? 12 : k == AmmoKind::Seeds ? 15 : k == AmmoKind::Nuts ? 6 : 4; }
constexpr int AmmoBaseCap(AmmoKind k) { return k == AmmoKind::Arrows ? 20 : k == AmmoKind::Seeds ? 20 : k == AmmoKind::Nuts ? 10 : 8; }
// The pack in your pack slot raises the cap for what it holds: the Big Quiver for arrows, the Bullet Bag for seeds, the Bomb Bag for bombs and bombchus.
constexpr int AmmoCap(AmmoKind k, bool hasPack, ItemId pack) {
    int cap = AmmoBaseCap(k);
    if (!hasPack) return cap;
    if (pack == ItemId::BigQuiver && k == AmmoKind::Arrows) cap += 20;
    if (pack == ItemId::BulletBag && k == AmmoKind::Seeds) cap += 20;
    if (pack == ItemId::BombBag && (k == AmmoKind::Bombs || k == AmmoKind::Bombchus)) cap += 8;
    return cap;
}

constexpr float kMaxHealthCap = 10.0f; // hearts, however many containers you find
constexpr int kHeartPiecesPerContainer = 4;

// ---- abilities -------------------------------------------------------------------------------------------------------------

enum class EffectType : uint8_t {
    None,
    AoeDamage,     // radius, amount hearts to everyone else in range
    Heal,          // amount hearts
    Invulnerable,  // seconds
    SpeedBoost,    // seconds, amount = speed multiplier
    RevealAll,     // seconds: every player is included in your snapshots
    StunNearby,    // radius, seconds
    PullTarget,    // radius = range, seconds = stun after the pull
    MarkAndReturn, // Farore's Wind
    RandomTeleport,
    BurnNearby,    // radius, amount = hearts per second, seconds
    Regen,         // amount = hearts per second, seconds
    Cleanse,
    RandomSong,    // radius: 0 = simple songs only, 1 = any song
    Launch,        // a mobility blast: throws the user high into the air (the game does that; see LaunchSelf in the mod), with no fall damage until
                   // they land and for `seconds` after. amount = how far a bot is carried (bots move on flat ground, so the server moves them).
};

struct Effect {
    EffectType type = EffectType::None;
    float radius = 0;
    float amount = 0;
    float seconds = 0;
};

struct AbilityDef {
    float cooldown = 0; // seconds
    std::array<Effect, 3> fx = {};
};

constexpr AbilityDef AbilityOf(ItemId id) {
    using T = EffectType;
    switch (id) {
        case ItemId::DinsFire:         return {14, {{{T::AoeDamage, 350, 1.6f, 0}}}};
        case ItemId::FaroresWind:      return {25, {{{T::MarkAndReturn, 0, 0, 20}}}};
        case ItemId::NayrusLove:       return {28, {{{T::Invulnerable, 0, 0, 4}}}};
        case ItemId::Hookshot:         return {10, {{{T::PullTarget, 900, 0, 1.0f}}}};
        case ItemId::Longshot:         return {8, {{{T::PullTarget, 1500, 0, 1.2f}}}};
        case ItemId::LensOfTruth:      return {30, {{{T::RevealAll, 0, 0, 10}}}};
        case ItemId::MagicBeans:       return {40, {{{T::Regen, 0, 0.2f, 10}}}};
        case ItemId::FairyOcarina:     return {20, {{{T::RandomSong, 0}}}};
        case ItemId::OcarinaOfTime:    return {15, {{{T::RandomSong, 1}}}};
        case ItemId::ZeldasLullaby:    return {20, {{{T::Heal, 0, 1.0f, 0}}}};
        case ItemId::EponasSong:       return {22, {{{T::SpeedBoost, 0, 1.4f, 6}}}};
        case ItemId::SariasSong:       return {24, {{{T::RevealAll, 0, 0, 6}}}};
        case ItemId::SunsSong:         return {22, {{{T::StunNearby, 500, 0, 2.5f}}}};
        case ItemId::SongOfTime:       return {45, {{{T::StunNearby, 900, 0, 1.5f}, {T::Invulnerable, 0, 0, 1.5f}}}};
        case ItemId::SongOfStorms:     return {20, {{{T::AoeDamage, 600, 1.4f, 0}}}};
        case ItemId::MinuetOfForest:   return {22, {{{T::SpeedBoost, 0, 1.25f, 4}, {T::Heal, 0, 0.5f, 0}}}};
        case ItemId::BoleroOfFire:     return {22, {{{T::BurnNearby, 400, 0.4f, 4}}}};
        case ItemId::SerenadeOfWater:  return {24, {{{T::Heal, 0, 1.5f, 0}, {T::Cleanse}}}};
        case ItemId::NocturneOfShadow: return {30, {{{T::RandomTeleport}}}};
        case ItemId::RequiemOfSpirit:  return {26, {{{T::StunNearby, 700, 0, 1.0f}, {T::AoeDamage, 700, 0.6f, 0}}}};
        case ItemId::PreludeOfLight:   return {26, {{{T::Heal, 0, 1.0f, 0}, {T::Invulnerable, 0, 0, 1.5f}}}};
        case ItemId::ShockwaveGrenade: return {12, {{{T::Launch, 0, 520, 1.0f}}}};
        default:                       return {};
    }
}

// What using an ability costs from the magic meter (kMaxMagic is 100): the strong ones cost the most.
constexpr float AbilityMagic(ItemId id) {
    switch (id) {
        case ItemId::DinsFire: return 30;        case ItemId::FaroresWind: return 20;     case ItemId::NayrusLove: return 40;
        case ItemId::Hookshot: return 10;        case ItemId::Longshot: return 12;        case ItemId::LensOfTruth: return 25;
        case ItemId::MagicBeans: return 20;      case ItemId::FairyOcarina: return 15;    case ItemId::OcarinaOfTime: return 15;
        case ItemId::ZeldasLullaby: return 20;   case ItemId::EponasSong: return 15;      case ItemId::SariasSong: return 20;
        case ItemId::SunsSong: return 25;        case ItemId::SongOfTime: return 45;      case ItemId::SongOfStorms: return 30;
        case ItemId::MinuetOfForest: return 20;  case ItemId::BoleroOfFire: return 25;    case ItemId::SerenadeOfWater: return 25;
        case ItemId::NocturneOfShadow: return 20; case ItemId::RequiemOfSpirit: return 35; case ItemId::PreludeOfLight: return 30;
        case ItemId::ShockwaveGrenade: return 20;
        default: return 0;
    }
}

constexpr bool IsSong(ItemId id) { return id >= ItemId::ZeldasLullaby && id <= ItemId::PreludeOfLight; }
// The simple songs a Fairy Ocarina can play.
constexpr bool IsSimpleSong(ItemId id) {
    return id == ItemId::ZeldasLullaby || id == ItemId::EponasSong || id == ItemId::SariasSong || id == ItemId::MinuetOfForest;
}

} // namespace royale
