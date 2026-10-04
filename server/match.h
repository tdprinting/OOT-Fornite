#pragma once
#include "../shared/balance.h"
#include "../shared/boss.h"
#include "../shared/combat.h"
#include "../shared/map.h"
#include "../shared/storm.h"
#include <algorithm>
#include <array>
#include <vector>

namespace royale {

struct Equipped {
    ItemId item;
    Rarity rarity;
};

enum class DamageKind : uint8_t { Normal, Storm, Fire, Explosion };

struct PlayerState {
    uint32_t id = 0;
    bool isBot = false;
    bool alive = true;
    float health = kMaxHealth; // hearts
    float armor = 0;           // the shield bar: soaks up damage before health does (not the storm), 0 to kMaxShield
    float maxHealth = kMaxHealth;
    int heartPieces = 0;
    Vec2 pos = {};
    // Pose data that the server only relays between clients and never simulates.
    float y = 0;
    int16_t rot = 0; // OoT binary angle: 0x10000 = 360 degrees
    uint8_t anim = 0;
    uint8_t scene = 0; // game scene the player is in (relayed). Bots are always in Hyrule Field.

    // ---- what the player carries
    Equipped weapon = {ItemId::DekuStick, Rarity::Common}; // starter weapon, like Fortnite's pickaxe
    bool hasShield = false;
    Equipped shield = {ItemId::DekuShield, Rarity::Common};
    std::vector<Equipped> potions;                         // consumables bag, at most kMaxPotions
    std::vector<Equipped> reserve;                         // backup weapons for the hotbar, at most kMaxReserveWeapons
    std::array<Equipped, kGearSlots> gear = {};            // one passive item per slot
    uint8_t gearMask = 0;                                  // bit n set when gear[n] is filled
    Equipped ability = {ItemId::DinsFire, Rarity::Common};
    bool hasAbility = false;
    float abilityReadyAt = 0;
    bool hasMark = false;                                  // Farore's Wind
    Vec2 mark = {};
    float markExpires = 0;

    // ---- timed effects; every "...Until" is a match-clock time
    float burnUntil = 0, burnDps = 0;
    uint32_t burnBy = 0xFFFFFFFFu;
    float stunUntil = 0, frozenUntil = 0;
    float invulnUntil = 0;
    float speedUntil = 0, speedMult = 1;
    float regenUntil = 0, regenRate = 0;
    float revealUntil = 0;
    float dmgTakenUntil = 0, dmgTakenMult = 1;

    float attackReadyAt = 0;
    int kills = 0;
    float damageDealt = 0;  // hearts of damage done to other players (storm and burn-out excluded)
    int chestsOpened = 0;
    int bossKills = 0;
    int placement = 0;      // 1 = winner; set when eliminated (number alive at that moment) or when the match ends
    bool dirty = true; // inventory or status changed since it was last sent to the owner
};

constexpr uint32_t kNoPlayer = 0xFFFFFFFFu;

// Things that happened inside the match since the last DrainEvents(); the network layer turns these into messages.
struct MatchEvent {
    enum class Type : uint8_t { Damaged, Eliminated, LootTaken, LootAdded, StateChanged, AbilityUsed, Teleported, Revived, BossDown, BossSpawned, Strike } type;
    uint32_t a = kNoPlayer; // Damaged: target | Eliminated: victim | LootTaken: taker | AbilityUsed: user | Teleported/Revived: player
    uint32_t b = kNoPlayer; // Damaged/Eliminated: attacker (kNoPlayer = storm, disconnect)
    float amount = 0;       // Damaged: hearts dealt
    float health = 0;       // Damaged: target's health afterwards
    size_t index = 0;       // LootTaken / LootAdded
    MatchState state = MatchState::Lobby; // StateChanged
    uint8_t item = 0;       // AbilityUsed: which ability
    float x = 0, z = 0;     // AbilityUsed: where the user stood | BossDown: where it fell (a = boss id, b = who landed the last hit)
};

struct LootEntry {
    LootSpawn spawn;
    bool taken = false;
};

struct MiniBoss {
    uint32_t id = 0;
    BossKind kind = BossKind::Stone;
    Vec2 home, pos;
    int16_t rot = 0;           // OoT binary angle, 0 = +z
    float health = 0, maxHealth = 0;
    bool alive = true;
    float attackReadyAt = 0;
    float lastSmashAt = -10;   // for the animation
    // The dragon only:
    float y = 0;               // height above the ground
    DragonMode mode = DragonMode::Patrol;
    float modeUntil = 0;
    Vec2 waypoint = {};
    float swoopReadyAt = 0;
    Vec2 swoopAt = {};
    uint32_t target = 0xFFFFFFFFu;
    float lostTargetAt = 0;
};

// A blast that lands at a marked spot a moment after it is announced (the dragon's fireballs, meteors and swoop).
struct Strike {
    Vec2 at;
    float radius = 0, damage = 0;
    float hitAt = 0;
    uint32_t by = 0;
    bool applied = false;
};

struct AttackResult {
    bool ok = false; // the attack was allowed and its cooldown started
    bool hit = false;
    float damage = 0;
    bool killed = false;
};

// What a player's gear adds up to right now.
struct GearTotals {
    float melee = 1, ranged = 1;
    float damageTaken = 1;
    float storm = 1, fire = 1, explosion = 1;
    float speed = 1;
    bool stunImmune = false;
};

inline GearTotals TotalsOf(const PlayerState& p) {
    GearTotals t;
    for (int slot = 0; slot < kGearSlots; slot++) {
        if (!(p.gearMask & (1 << slot))) continue;
        const GearDef g = GearOf(p.gear[slot].item);
        const Rarity r = p.gear[slot].rarity;
        t.melee *= Scaled(g.melee, r);
        t.ranged *= Scaled(g.ranged, r);
        t.damageTaken *= Scaled(g.damageTaken, r);
        t.storm *= Scaled(g.storm, r);
        t.fire *= Scaled(g.fire, r);
        t.explosion *= Scaled(g.explosion, r);
        t.speed *= Scaled(g.speed, r);
        t.stunImmune = t.stunImmune || g.stunImmune;
    }
    return t;
}

// The server-side match. No networking and no game code in here, so it runs inside the host's game, headless,
// or in unit tests. Feed it Tick(dt) at kTickHz and call the event methods as messages arrive.
class Match {
  public:
    static constexpr float kCountdownSec = royale::kCountdownSec;
    static constexpr float kDropSec = royale::kDropSec; // spawn protection
    static constexpr float kEndingSec = royale::kEndingSec;

    Match(uint64_t seed, Circle map, int lootCount = 400)
        : seed(seed), map(map), storm(seed, map), abilityRng(seed ^ 0x6162696Cull) {
        for (const LootSpawn& l : GenerateLoot(seed, map, lootCount, 0.15f)) loot.push_back({l, false});
    }

    // Reject positions that can't be walked at (set by the host once it has measured the real map). Used for spawn points
    // and for loot placed by RegenerateLoot.
    void SetPlacementValidator(PlacementFn fn) { placement = std::move(fn); }
    // Chests for the buildings and caves (see shared/poi.h), on top of the scattered ones.
    void SetLootSpots(std::vector<Vec2> spots) { lootSpots = std::move(spots); }
    void RegenerateLoot(int count, float chestFraction = 0.15f) {
        loot.clear();
        for (const LootSpawn& l : GenerateLoot(seed, map, count, chestFraction, placement)) loot.push_back({l, false});
        for (const LootSpawn& l : GenerateSpotLoot(seed, lootSpots)) loot.push_back({l, false});
    }

    // Add a human. Returns false if the lobby is full or the match already started.
    // How many players the match has, bots included (2 to 32). Can't go below the people already in the lobby.
    bool SetPlayerLimit(int n) {
        if (state != MatchState::Lobby || n < kMinPlayers || n > kMaxPlayers || n < static_cast<int>(players.size())) return false;
        playerLimit = n;
        return true;
    }
    int PlayerLimit() const { return playerLimit; }

    bool AddHuman(uint32_t id) {
        if (state != MatchState::Lobby || static_cast<int>(players.size()) >= playerLimit) return false;
        players.push_back(MakePlayer(id, false));
        return true;
    }

    // Start with at least one human; every remaining slot up to kMaxPlayers is filled with a bot.
    bool Start() {
        if (state != MatchState::Lobby || players.empty()) return false;
        humans = static_cast<int>(players.size());
        uint32_t nextId = 1000;
        while (static_cast<int>(players.size()) < playerLimit) {
            players.push_back(MakePlayer(nextId++, true));
            players.back().scene = static_cast<uint8_t>(MapOf(mapId).scene);
        }
        Rng spawn(seed ^ 0x7370776Eull); // "spwn"
        for (auto& p : players) p.pos = RandomPointIn(spawn, map, placement, 0.9f);
        SpawnBosses();
        Enter(MatchState::Countdown);
        return true;
    }

    void Tick(float dt) {
        stateTime += dt;
        clock += dt;
        switch (state) {
            case MatchState::Countdown:
                if (stateTime >= kCountdownSec) Enter(MatchState::Drop);
                break;
            case MatchState::Drop:
                if (stateTime >= kDropSec) Enter(MatchState::InMatch);
                break;
            case MatchState::InMatch:
                stormTime += dt;
                for (auto& p : players) {
                    if (!p.alive) continue;
                    float dps = storm.DamagePerSecond(p.pos, stormTime);
                    if (dps > 0) Damage(p.id, dps * dt, kNoPlayer, DamageKind::Storm, false);
                    if (!p.alive) continue;
                    if (clock < p.burnUntil) Damage(p.id, p.burnDps * dt, p.burnBy, DamageKind::Fire, false);
                    if (!p.alive) continue;
                    if (clock < p.regenUntil && p.health < p.maxHealth) p.health = (std::min)(p.maxHealth, p.health + p.regenRate * dt);
                    if (p.hasMark && clock >= p.markExpires) { p.hasMark = false; p.dirty = true; }
                }
                TickBosses(dt);
                if (Alive() <= 1) Enter(MatchState::Ending);
                break;
            case MatchState::Ending:
            case MatchState::Lobby:
                break;
        }
    }

    // Returns true if the player was eliminated by this damage. Ignored during the drop (spawn protection) and while the
    // player is invulnerable. The player's gear, status effects and (for storm, fire and explosions) the matching resistances
    // are applied here. A Fairy in the bag turns a killing blow into a revival. `announce` false suppresses the Damaged event
    // (used for the storm and burning, which tick every frame).
    bool Damage(uint32_t id, float hearts, uint32_t attacker = kNoPlayer, DamageKind kind = DamageKind::Normal, bool announce = true) {
        PlayerState* p = Find(id);
        if (!p || !p->alive || state == MatchState::Drop || hearts <= 0) return false;
        if (clock < p->invulnUntil) return false;

        const GearTotals g = TotalsOf(*p);
        float mult = g.damageTaken;
        if (kind == DamageKind::Storm) mult *= g.storm;
        else if (kind == DamageKind::Fire) mult *= g.fire;
        else if (kind == DamageKind::Explosion) mult *= g.explosion;
        if (clock < p->dmgTakenUntil) mult *= p->dmgTakenMult;
        if (clock < p->frozenUntil && kind != DamageKind::Storm) mult *= 1.25f; // frozen targets are brittle
        hearts *= mult;

        // The shield bar takes the hit first (the storm goes straight through it).
        float soaked = 0;
        if (kind != DamageKind::Storm && p->armor > 0) {
            soaked = (std::min)(p->armor, hearts);
            p->armor -= soaked;
            hearts -= soaked;
            p->dirty = true;
        }
        const float dealt = soaked + (std::min)(hearts, p->health);
        p->health -= hearts;
        bool killed = p->health <= 0;
        if (killed && TryFairy(*p)) killed = false;
        if (killed) p->health = 0;
        if (attacker != kNoPlayer && attacker != id && kind != DamageKind::Storm) {
            if (PlayerState* a = Find(attacker)) a->damageDealt += dealt;
        }
        // Storm ticks are not announced: health travels in snapshots, only the elimination is an event.
        if (attacker != kNoPlayer && announce) {
            MatchEvent e{MatchEvent::Type::Damaged};
            e.a = id; e.b = attacker; e.amount = hearts + soaked; e.health = p->health;
            events.push_back(e);
        }
        if (killed) Eliminate(*p, attacker);
        return killed;
    }

    // A player left (closed the game, lost connection). In the lobby they just vanish; in a match they are eliminated.
    void RemovePlayer(uint32_t id) {
        if (state == MatchState::Lobby) {
            for (size_t i = 0; i < players.size(); i++) {
                if (players[i].id == id) { players.erase(players.begin() + static_cast<long>(i)); return; }
            }
            return;
        }
        PlayerState* p = Find(id);
        if (p && p->alive) { p->health = 0; Eliminate(*p, kNoPlayer); }
    }

    std::vector<MatchEvent> DrainEvents() {
        std::vector<MatchEvent> out;
        out.swap(events);
        return out;
    }

    // Attack with the attacker's equipped weapon. `hit` is the outcome of the accuracy roll (bots roll it themselves,
    // for humans the client reports it). A miss still spends the cooldown. Range and cooldown are checked here, and a stunned
    // or frozen attacker can't attack at all. The weapon's own effect (burn, freeze, stun, shield piercing, explosion) applies on a hit.
    AttackResult Attack(uint32_t attackerId, uint32_t targetId, bool hit = true) {
        AttackResult r;
        if (state != MatchState::InMatch) return r;
        if (IsBossId(targetId)) return AttackBoss(attackerId, targetId, hit);
        PlayerState* a = Find(attackerId);
        PlayerState* t = Find(targetId);
        if (!a || !t || a == t || !a->alive || !t->alive) return r;
        WeaponStats w = WeaponOf(a->weapon.item);
        if (w.damage <= 0 || clock < a->attackReadyAt) return r;
        if (clock < a->stunUntil || clock < a->frozenUntil) return r;
        if (Distance(a->pos, t->pos) > w.range * 1.1f) return r;
        a->attackReadyAt = clock + w.cooldown;
        r.ok = true;
        if (!hit) return r;

        const GearTotals ag = TotalsOf(*a);
        const float base = w.damage * RarityScale(a->weapon.rarity) * (w.ranged ? ag.ranged : ag.melee);
        float reduction = 0.0f;
        if (w.effect != WeaponEffect::PierceShield && t->hasShield) reduction = ShieldReduction(t->shield.item, t->shield.rarity);
        r.damage = base * (1.0f - reduction);
        r.hit = true;
        r.killed = Damage(targetId, r.damage, attackerId, w.splashRadius > 0 ? DamageKind::Explosion : DamageKind::Normal);

        if (!r.killed && t->alive) {
            const bool immune = TotalsOf(*t).stunImmune;
            const float seconds = w.effectSeconds * RarityScale(a->weapon.rarity);
            switch (w.effect) {
                case WeaponEffect::Burn:
                    t->burnUntil = clock + seconds;
                    t->burnDps = (std::max)(clock < t->burnUntil ? t->burnDps : 0.0f, w.effectAmount * RarityScale(a->weapon.rarity));
                    t->burnBy = attackerId;
                    t->dirty = true;
                    break;
                case WeaponEffect::Freeze:
                    if (!immune) { t->frozenUntil = (std::max)(t->frozenUntil, clock + seconds); t->dirty = true; }
                    break;
                case WeaponEffect::Stun:
                    if (!immune) { t->stunUntil = (std::max)(t->stunUntil, clock + seconds); t->dirty = true; }
                    break;
                default:
                    break;
            }
        }
        if (w.splashRadius > 0) {
            const Vec2 centre = t->pos;
            for (auto& o : players) {
                if (!o.alive || o.id == attackerId || o.id == targetId) continue;
                if (Distance(o.pos, centre) <= w.splashRadius) Damage(o.id, base * 0.6f, attackerId, DamageKind::Explosion);
            }
        }
        return r;
    }

    // Pick up loot entry `index`. Weapons, shields, abilities and gear swap with what the player already has in that slot (the old
    // one drops on the ground). Consumables need room in the bag. Instant items are used on the spot and stay on the ground when they
    // would do nothing (a heart at full health).
    // Would taking this be an improvement? Walking over loot only picks up what passes this; anything else needs the player to ask
    // (force), so a Common stick doesn't replace your Epic sword just because you walked past it.
    static bool WorthTaking(const PlayerState& p, const LootSpawn& s) {
        switch (KindOf(s.item)) {
            case ItemKind::Weapon: {
                if (WeaponDps(s.item, s.rarity) > WeaponDps(p.weapon.item, p.weapon.rarity) * 1.05f) return true; // an upgrade
                if (static_cast<int>(p.reserve.size()) >= kMaxReserveWeapons) return false;                  // no room for a spare
                if (s.item == p.weapon.item && s.rarity == p.weapon.rarity) return false;
                for (const Equipped& e : p.reserve) if (e.item == s.item && e.rarity == s.rarity) return false;
                return true;                                                                                 // fills an empty hotbar slot
            }
            case ItemKind::Shield: return !p.hasShield || ShieldReduction(s.item, s.rarity) > ShieldReduction(p.shield.item, p.shield.rarity) + 0.01f;
            case ItemKind::Consumable:
                if (PotionOf(s.item).revive) { for (const Equipped& e : p.potions) if (PotionOf(e.item).revive) return false; }
                return static_cast<int>(p.potions.size()) < kMaxPotions;
            case ItemKind::Instant: return true; // refused by UseInstant when it would do nothing
            case ItemKind::Ability: return !p.hasAbility;
            case ItemKind::Gear: {
                const int slot = static_cast<int>(GearOf(s.item).slot);
                return slot >= 0 && slot < kGearSlots && (!(p.gearMask & (1 << slot)) || static_cast<int>(s.rarity) > static_cast<int>(p.gear[slot].rarity));
            }
        }
        return false;
    }

    // `force` = the player asked for this item (or it is a bot, which already decided); false = they just walked over it.
    // Swap the weapon in hand with backup slot 1..kMaxReserveWeapons. Costs a short delay before the next swing.
    bool SelectWeapon(uint32_t id, int slot) {
        PlayerState* p = Find(id);
        if (!p || !p->alive || (state != MatchState::Drop && state != MatchState::InMatch)) return false;
        if (slot < 1 || slot > static_cast<int>(p->reserve.size())) return false;
        std::swap(p->weapon, p->reserve[static_cast<size_t>(slot - 1)]);
        p->attackReadyAt = (std::max)(p->attackReadyAt, clock + 0.35f);
        p->dirty = true;
        return true;
    }

    bool PickUp(uint32_t id, size_t index, bool force = true) {
        PlayerState* p = Find(id);
        if (!p || !p->alive || (state != MatchState::Drop && state != MatchState::InMatch)) return false;
        if (index >= loot.size() || loot[index].taken) return false;
        const LootSpawn s = loot[index].spawn;
        if (Distance(p->pos, s.pos) > kPickupRange * (s.container ? 2.0f : 1.5f)) return false; // chests are solid, so you open them from a step away
        if (s.container && !force) return false; // chests have to be opened on purpose
        if (!force && !WorthTaking(*p, s)) return false;
        switch (KindOf(s.item)) {
            case ItemKind::Weapon: {
                const bool upgrade = WeaponDps(s.item, s.rarity) > WeaponDps(p->weapon.item, p->weapon.rarity) * 1.05f;
                const bool room = static_cast<int>(p->reserve.size()) < kMaxReserveWeapons;
                if (upgrade) {                       // the better weapon goes in hand; the old one becomes a backup if there is room
                    if (room && !IsStarter(p->weapon)) p->reserve.push_back(p->weapon); else DropEquipment(*p, p->weapon);
                    p->weapon = {s.item, s.rarity};
                } else if (room) {                   // not better, but there is a free hotbar slot
                    p->reserve.push_back({s.item, s.rarity});
                } else {                             // hotbar full: replace the weapon in hand
                    DropEquipment(*p, p->weapon);
                    p->weapon = {s.item, s.rarity};
                }
                break;
            }
            case ItemKind::Shield:
                if (p->hasShield) DropEquipment(*p, p->shield);
                p->shield = {s.item, s.rarity};
                p->hasShield = true;
                break;
            case ItemKind::Consumable:
                if (static_cast<int>(p->potions.size()) >= kMaxPotions) return false;
                p->potions.push_back({s.item, s.rarity});
                break;
            case ItemKind::Instant:
                if (!UseInstant(*p, s.item, s.rarity)) return false;
                break;
            case ItemKind::Ability:
                if (p->hasAbility) DropEquipment(*p, p->ability);
                p->ability = {s.item, s.rarity};
                p->hasAbility = true;
                p->abilityReadyAt = clock; // ready straight away
                p->hasMark = false;
                break;
            case ItemKind::Gear: {
                const int slot = static_cast<int>(GearOf(s.item).slot);
                if (slot < 0 || slot >= kGearSlots) return false;
                if (p->gearMask & (1 << slot)) DropEquipment(*p, p->gear[slot]);
                p->gear[slot] = {s.item, s.rarity};
                p->gearMask = static_cast<uint8_t>(p->gearMask | (1 << slot));
                break;
            }
        }
        p->dirty = true;
        if (s.container) p->chestsOpened++;
        loot[index].taken = true;
        MatchEvent e{MatchEvent::Type::LootTaken};
        e.a = id; e.index = index;
        events.push_back(e);
        return true;
    }

    // Use one item from the bag: the potion that restores the missing health with the least waste when hurt (or the biggest if none
    // is enough); otherwise something that cures fire or stun; otherwise a Poe. A Fairy is never used this way.
    bool UsePotion(uint32_t id) {
        PlayerState* p = Find(id);
        if (!p || !p->alive || p->potions.empty()) return false;
        if (state != MatchState::Drop && state != MatchState::InMatch) return false;

        int best = -1;
        const float missing = p->maxHealth - p->health;
        if (missing > 0.01f) {
            for (size_t i = 0; i < p->potions.size(); i++) {
                const PotionDef d = PotionOf(p->potions[i].item);
                if (d.revive || d.heal <= 0) continue;
                if (best < 0) { best = static_cast<int>(i); continue; }
                float hi = PotionHeal(p->potions[i].item, p->potions[i].rarity);
                float hb = PotionHeal(p->potions[best].item, p->potions[best].rarity);
                bool iEnough = hi >= missing, bEnough = hb >= missing;
                if ((iEnough && !bEnough) || (iEnough == bEnough && (iEnough ? hi < hb : hi > hb))) best = static_cast<int>(i);
            }
        }
        const bool afflicted = clock < p->burnUntil || clock < p->stunUntil || clock < p->frozenUntil;
        for (size_t i = 0; best < 0 && afflicted && i < p->potions.size(); i++) {
            if (PotionOf(p->potions[i].item).cleanse) best = static_cast<int>(i);
        }
        for (size_t i = 0; best < 0 && i < p->potions.size(); i++) {
            const PotionDef d = PotionOf(p->potions[i].item);
            if (!d.revive && d.damageTaken < 1.0f) best = static_cast<int>(i);
        }
        if (best < 0) return false;

        const Equipped used = p->potions[best];
        const PotionDef d = PotionOf(used.item);
        p->health = (std::min)(p->maxHealth, p->health + PotionHeal(used.item, used.rarity));
        if (d.cleanse) Cleanse(*p);
        if (d.damageTaken < 1.0f) {
            p->dmgTakenUntil = clock + d.seconds;
            p->dmgTakenMult = d.damageTaken;
        }
        p->potions.erase(p->potions.begin() + best);
        p->dirty = true;
        return true;
    }

    // Drink a shield potion: the one that fills the bar best without wasting much (the biggest that fits, or the smallest if none does).
    // A small potion only works while the bar is below half; the big one fills it. Fails if the bag has none or the bar is already full enough.
    bool UseShield(uint32_t id) {
        PlayerState* p = Find(id);
        if (!p || !p->alive || (state != MatchState::Drop && state != MatchState::InMatch)) return false;
        int best = -1;
        float bestFit = 0;
        for (size_t i = 0; i < p->potions.size(); i++) {
            const PotionDef d = PotionOf(p->potions[i].item);
            if (d.shield <= 0 || p->armor >= d.shieldCap - 0.01f) continue;
            const float room = d.shieldCap - p->armor;
            const float fit = d.shield <= room ? d.shield : -d.shield; // fits entirely: prefer the largest; otherwise the smallest
            if (best < 0 || (fit > 0 && (bestFit < 0 || fit > bestFit)) || (fit < 0 && bestFit < 0 && fit > bestFit)) { best = static_cast<int>(i); bestFit = fit; }
        }
        if (best < 0) return false;
        const PotionDef d = PotionOf(p->potions[static_cast<size_t>(best)].item);
        p->armor = (std::min)(d.shieldCap, p->armor + d.shield);
        p->potions.erase(p->potions.begin() + best);
        p->dirty = true;
        return true;
    }

    // Use the ability slot. Fails (and costs nothing) if there is no ability, it is still recharging, the player is stunned, or the
    // ability needs a target that isn't there (Hookshot with nobody in front). Farore's Wind marks a spot the first time and jumps back
    // to it the second time.
    bool UseAbility(uint32_t id) {
        PlayerState* p = Find(id);
        if (!p || !p->alive || state != MatchState::InMatch || !p->hasAbility) return false;
        if (clock < p->abilityReadyAt || clock < p->stunUntil || clock < p->frozenUntil) return false;
        const ItemId item = p->ability.item;
        bool startsCooldown = true;
        if (!RunAbility(*p, item, p->ability.rarity, &startsCooldown)) return false;
        if (startsCooldown) p->abilityReadyAt = clock + AbilityOf(item).cooldown;
        p->dirty = true;
        MatchEvent e{MatchEvent::Type::AbilityUsed};
        e.a = id; e.item = static_cast<uint8_t>(item); e.x = p->pos.x; e.z = p->pos.z;
        events.push_back(e);
        return true;
    }

    float SpeedMultiplier(const PlayerState& p) const {
        return TotalsOf(p).speed * (clock < p.speedUntil ? p.speedMult : 1.0f);
    }
    bool Revealing(const PlayerState& p) const { return clock < p.revealUntil; }
    bool Stunned(const PlayerState& p) const { return clock < p.stunUntil || clock < p.frozenUntil; }
    bool Invulnerable(const PlayerState& p) const { return clock < p.invulnUntil; }

    const std::vector<LootEntry>& Loot() const { return loot; }
    size_t AddLoot(const LootSpawn& l) {
        loot.push_back({l, false});
        MatchEvent e{MatchEvent::Type::LootAdded};
        e.index = loot.size() - 1;
        events.push_back(e);
        return loot.size() - 1;
    }
    void ClearLoot() { loot.clear(); }
    float Clock() const { return clock; }

    int Alive() const {
        int n = 0;
        for (const auto& p : players) n += p.alive;
        return n;
    }
    // Only meaningful once the match is Ending.
    int Score(const PlayerState& p) const { return ScorePoints(p.damageDealt, p.kills, p.chestsOpened, p.placement) + p.bossKills * kPointsPerBossKill; }

    struct Standing { uint32_t id; bool isBot; int placement, kills, chests, score; float damage; };
    // Everyone's results, best score first (ties broken by placement).
    std::vector<Standing> Standings() const {
        std::vector<Standing> out;
        for (const auto& p : players) out.push_back({p.id, p.isBot, p.placement, p.kills, p.chestsOpened, Score(p), p.damageDealt});
        std::sort(out.begin(), out.end(), [](const Standing& a, const Standing& b) {
            if (a.score != b.score) return a.score > b.score;
            return (a.placement ? a.placement : 99) < (b.placement ? b.placement : 99);
        });
        return out;
    }

    // ---- mini bosses --------------------------------------------------------------------------------------------------

    // Where bosses may stand guard (the caves of the points of interest). Any shortfall is filled with spots on open ground.
    void SetBossSpots(std::vector<Vec2> spots) { bossSpots = std::move(spots); }
    void SetMapId(int id) { mapId = ClampMap(id); }
    int MapId() const { return mapId; }
    void SetBossCount(int n) { bossCount = (std::max)(0, (std::min)(n, kMaxBosses)); }
    const std::vector<MiniBoss>& Bosses() const { return bosses; }
    const MiniBoss* FindBoss(uint32_t id) const {
        for (const auto& b : bosses) if (b.id == id) return &b;
        return nullptr;
    }
    static const char* BossName(const MiniBoss& b) { return BossOf(b.kind).name; }

    void SpawnBosses() {
        bosses.clear();
        Rng rng(seed ^ 0x626F7373ull); // "boss"
        std::vector<Vec2> spots = bossSpots;
        for (size_t i = spots.size(); i > 1; i--) std::swap(spots[i - 1], spots[rng.Below(static_cast<uint32_t>(i))]);
        while (static_cast<int>(bosses.size()) < bossCount) {
            Vec2 at = {};
            bool found = false;
            if (!spots.empty()) {
                at = spots.back();
                spots.pop_back();
                found = true;
            } else {
                for (int attempt = 0; attempt < 60 && !found; attempt++) {
                    at = RandomPointIn(rng, map, placement, 0.8f);
                    found = !placement || placement(at);
                    for (const auto& other : bosses) if (Distance(other.home, at) < 1200.0f) found = false;
                }
            }
            if (!found) break;
            MiniBoss b;
            b.id = kBossIdBase + static_cast<uint32_t>(bosses.size());
            b.kind = MapOf(mapId).minis[rng.Below(2)];   // the ones that suit this place
            b.home = b.pos = at;
            b.maxHealth = b.health = BossOf(b.kind).health;
            bosses.push_back(b);
        }
    }

    AttackResult AttackBoss(uint32_t attackerId, uint32_t bossId, bool hit) {
        AttackResult r;
        PlayerState* a = Find(attackerId);
        MiniBoss* b = nullptr;
        for (auto& x : bosses) if (x.id == bossId) b = &x;
        if (!a || !b || !a->alive || !b->alive) return r;
        const WeaponStats w = WeaponOf(a->weapon.item);
        if (w.damage <= 0 || clock < a->attackReadyAt || clock < a->stunUntil || clock < a->frozenUntil) return r;
        const bool dragon = IsDragonKind(b->kind);
        const bool airborne = dragon && b->y > kDragonAirborneAbove;
        if (airborne && !w.ranged) return r;                                           // up in the sky: only arrows and the like reach it
        if (Distance(a->pos, b->pos) > w.range * 1.1f + (dragon ? kDragonBodyRadius : kBossBodyRadius)) return r; // it is big: you can hit it from further off
        a->attackReadyAt = clock + w.cooldown;
        r.ok = true;
        if (!hit) return r;
        const GearTotals ag = TotalsOf(*a);
        r.damage = w.damage * RarityScale(a->weapon.rarity) * (w.ranged ? ag.ranged : ag.melee);
        if (dragon) r.damage *= airborne ? 0.75f : (b->mode == DragonMode::Landed ? 1.25f : 1.0f); // landed: it is dazed and takes extra
        r.hit = true;
        const float dealt = (std::min)(r.damage, b->health);
        b->health -= r.damage;
        a->damageDealt += dealt;
        b->target = attackerId; // whoever hurts it is who it comes for
        b->lostTargetAt = clock;
        MatchEvent e{MatchEvent::Type::Damaged};
        e.a = bossId; e.b = attackerId; e.amount = r.damage; e.health = (std::max)(0.0f, b->health);
        events.push_back(e);
        if (b->health <= 0) {
            r.killed = true;
            KillBoss(*b, *a);
        }
        return r;
    }

    void KillBoss(MiniBoss& b, PlayerState& killer) {
        b.alive = false;
        b.health = 0;
        killer.bossKills++;
        killer.dirty = true;
        // Several chests on the best tiers, in a ring around where it fell.
        Rng rng(seed ^ (static_cast<uint64_t>(b.id) * 0x9E3779B97F4A7C15ull) ^ static_cast<uint64_t>(clock * 1000.0f));
        const int drops = BossOf(b.kind).drops;
        for (int i = 0; i < drops; i++) {
            const Rarity tier = rng.Unit() < 0.4 ? Rarity::Legendary : Rarity::Epic;
            ItemId item;
            if (!PickItem(rng, tier, &item)) item = ItemId::MasterSword;
            Rarity t = tier;
            if (t < DefOf(item).minRarity) t = DefOf(item).minRarity;
            if (t > DefOf(item).maxRarity) t = DefOf(item).maxRarity;
            const float angle = 6.2831853f * i / drops + static_cast<float>(rng.Unit()) * 0.5f;
            Vec2 at = {b.pos.x + std::cos(angle) * 130.0f, b.pos.z + std::sin(angle) * 130.0f};
            if (Distance(at, map.center) > map.radius || (placement && !placement(at))) at = b.pos;
            AddLoot({at, item, t, true, true});
        }
        if (IsDragonKind(b.kind)) { // the big one also leaves heart containers
            for (int i = 0; i < 2; i++) {
                const float angle = 6.2831853f * (i + 0.5f) / 2.0f + 0.4f;
                Vec2 at = {b.pos.x + std::cos(angle) * 230.0f, b.pos.z + std::sin(angle) * 230.0f};
                if (Distance(at, map.center) > map.radius || (placement && !placement(at))) at = b.pos;
                LootSpawn heart = {at, ItemId::HeartContainer, Rarity::Legendary, true, true};
                heart.special = true;
                AddLoot(heart);
            }
        }
        MatchEvent e{MatchEvent::Type::BossDown};
        e.a = b.id; e.b = killer.id; e.x = b.pos.x; e.z = b.pos.z;
        events.push_back(e);
    }

    void SetMajorBoss(bool on) { majorBoss = on; }
    bool MajorBossEnabled() const { return majorBoss; }
    const std::vector<Strike>& Strikes() const { return strikes; }

    // The fire dragon arrives halfway through the storm timeline, somewhere inside the safe zone, and everybody is told.
    void MaybeSpawnDragon() {
        if (!majorBoss || dragonSpawned || stormTime < DragonSpawnTime()) return;
        dragonSpawned = true;
        Rng rng(seed ^ 0x647261676Full); // "drago"
        const Circle zone = storm.SafeZoneAt(stormTime);
        Vec2 at = zone.center;
        for (int attempt = 0; attempt < 40; attempt++) {
            const Vec2 c = RandomPointIn(rng, zone, placement, 0.7f);
            at = c;
            if (!placement || placement(c)) break;
        }
        MiniBoss d;
        d.id = kDragonId;
        d.kind = MapOf(mapId).major;
        d.home = d.pos = at;
        d.maxHealth = d.health = BossOf(d.kind).health;
        d.y = kDragonAltitude;
        d.waypoint = at;
        d.swoopReadyAt = clock + 8.0f;
        bosses.push_back(d);
        MatchEvent e{MatchEvent::Type::BossSpawned};
        e.a = d.id; e.x = at.x; e.z = at.z;
        events.push_back(e);
    }
    float DragonSpawnTime() const {
        float total = 0;
        for (const auto& ph : kStormPhases) total += ph.waitSec + ph.closeSec;
        return total * 0.5f;
    }

    void AddStrike(Vec2 at, float radius, float damage, float delay, uint32_t by) {
        Strike s;
        s.at = at; s.radius = radius; s.damage = damage; s.hitAt = clock + delay; s.by = by;
        strikes.push_back(s);
        MatchEvent e{MatchEvent::Type::Strike};
        e.a = by; e.x = at.x; e.z = at.z; e.amount = radius; e.health = delay;
        events.push_back(e);
    }

    // The fire dragon sets people alight; the others (water, forest, shadow, sand) leave their victims stunned for a moment instead.
    void ApplyElement(PlayerState& p, uint32_t by, float seconds, float dps) {
        const MiniBoss* src = FindBoss(by);
        if (src && src->kind != BossKind::DragonFire) { p.stunUntil = (std::max)(p.stunUntil, clock + 0.5f + seconds * 0.15f); p.dirty = true; }
        else ApplyBurn(p, by, seconds, dps);
    }

    void ApplyBurn(PlayerState& p, uint32_t by, float seconds, float dps) {
        p.burnUntil = (std::max)(p.burnUntil, clock + seconds);
        p.burnDps = (std::max)(clock < p.burnUntil ? p.burnDps : 0.0f, dps);
        p.burnBy = by;
        p.dirty = true;
    }

    void TickStrikes() {
        for (auto& s : strikes) {
            if (s.applied || clock < s.hitAt) continue;
            s.applied = true;
            for (auto& p : players) {
                if (!p.alive || clock < p.invulnUntil || Distance(p.pos, s.at) > s.radius) continue;
                Damage(p.id, s.damage, s.by, DamageKind::Fire);
                if (p.alive) ApplyElement(p, s.by, 3.0f, 0.3f);
            }
        }
        strikes.erase(std::remove_if(strikes.begin(), strikes.end(), [&](const Strike& s) { return s.applied && clock > s.hitAt + 1.0f; }), strikes.end());
    }

    void TickDragon(MiniBoss& b, float dt) {
        const BossDef def = BossOf(b.kind);
        auto face = [&](Vec2 to) { b.rot = static_cast<int16_t>(static_cast<int32_t>(std::atan2(to.x - b.pos.x, to.z - b.pos.z) * (32768.0f / 3.14159265358979f))); };
        auto fly = [&](Vec2 to, float speed) {
            const float dx = to.x - b.pos.x, dz = to.z - b.pos.z, d = std::hypot(dx, dz);
            if (d > 1.0f) { const float step = (std::min)(d, speed * dt); b.pos.x += dx / d * step; b.pos.z += dz / d * step; }
            return d;
        };
        auto climbTo = [&](float alt, float rate) { b.y += (std::max)(-rate * dt, (std::min)(rate * dt, alt - b.y)); };
        // Pick the nearest living player in range as the target.
        PlayerState* target = nullptr;
        float best = kDragonAggroRange;
        for (auto& p : players) {
            if (!p.alive || clock < p.invulnUntil) continue;
            const float d = Distance(p.pos, b.pos);
            if (d < best) { best = d; target = &p; }
        }
        if (target) { b.target = target->id; b.lostTargetAt = clock; } else b.target = kNoPlayer;
        const float hpFrac = b.health / b.maxHealth;

        switch (b.mode) {
            case DragonMode::Landed:
                climbTo(0.0f, 300.0f);
                if (clock >= b.modeUntil) b.mode = DragonMode::Climb;
                return;
            case DragonMode::Climb:
                climbTo(kDragonAltitude, 160.0f);
                if (b.y >= kDragonAltitude - 5.0f) b.mode = DragonMode::Chase;
                return;
            case DragonMode::Swoop: {
                const float t = (std::min)(1.0f, 1.0f - (b.modeUntil - clock) / kDragonStrikeDelay);
                b.y = kDragonAltitude * (1.0f - t);
                fly(b.swoopAt, def.speed * 2.2f);
                face(b.swoopAt);
                if (clock >= b.modeUntil) { // it hits the ground and sits there, dazed
                    b.pos = b.swoopAt;
                    b.y = 0;
                    b.mode = DragonMode::Landed;
                    b.modeUntil = clock + kDragonLandedSeconds;
                    b.lastSmashAt = clock;
                }
                return;
            }
            case DragonMode::Breath:
                b.lastSmashAt = clock; // keeps the animation going
                if (target) face(target->pos);
                for (auto& p : players) {
                    if (!p.alive || clock < p.invulnUntil) continue;
                    const float dx = p.pos.x - b.pos.x, dz = p.pos.z - b.pos.z, d = std::hypot(dx, dz);
                    if (d > kDragonBreathRange) continue;
                    float off = std::atan2(dx, dz) - static_cast<float>(b.rot) * (3.14159265f / 32768.0f);
                    while (off > 3.14159265f) off -= 6.2831853f;
                    while (off < -3.14159265f) off += 6.2831853f;
                    if (std::fabs(off) > kDragonBreathHalfAngle) continue;
                    Damage(p.id, kDragonBreathDps * dt, b.id, DamageKind::Fire, false);
                    if (p.alive) ApplyElement(p, b.id, 2.5f, 0.3f);
                }
                if (clock >= b.modeUntil) b.mode = DragonMode::Chase;
                return;
            case DragonMode::Cast:
                if (target) face(target->pos);
                if (clock >= b.modeUntil) b.mode = DragonMode::Chase;
                return;
            default: break;
        }
        climbTo(kDragonAltitude + 30.0f * std::sin(clock * 1.6f), 120.0f);
        if (!target) { // nobody near: cruise between spots in the safe zone
            b.mode = DragonMode::Patrol;
            if (fly(b.waypoint, 110.0f) < 90.0f) {
                Rng rng(seed ^ static_cast<uint64_t>(clock * 1000.0f) ^ 0x70617472ull);
                b.waypoint = RandomPointIn(rng, storm.SafeZoneAt(stormTime), placement, 0.8f);
            }
            face(b.waypoint);
            return;
        }
        b.mode = DragonMode::Chase;
        const float d = Distance(b.pos, target->pos);
        face(target->pos);
        if (d > 480.0f) fly(target->pos, def.speed);              // close in to about 480 units and hover
        if (clock < b.attackReadyAt) return;
        Rng rng(seed ^ static_cast<uint64_t>(clock * 977.0f) ^ b.id);
        if (clock >= b.swoopReadyAt && rng.Unit() < 0.4) {         // dive at them
            b.mode = DragonMode::Swoop;
            b.modeUntil = clock + kDragonStrikeDelay;
            b.swoopAt = target->pos;
            b.swoopReadyAt = clock + 15.0f;
            b.attackReadyAt = clock + 2.0f;
            AddStrike(b.swoopAt, 140.0f, 1.6f, kDragonStrikeDelay, b.id);
        } else if (d < kDragonBreathRange * 0.9f) {                // close: breathe fire
            b.mode = DragonMode::Breath;
            b.modeUntil = clock + kDragonBreathSeconds;
            b.attackReadyAt = clock + def.cooldown + kDragonBreathSeconds;
        } else {                                                    // far: fireballs, and meteors when it is hurt
            b.mode = DragonMode::Cast;
            b.modeUntil = clock + 1.0f;
            b.attackReadyAt = clock + def.cooldown + 1.0f;
            AddStrike(target->pos, kDragonStrikeRadius, kDragonStrikeDamage, kDragonStrikeDelay, b.id);
            const int extra = hpFrac < 0.5f ? 6 : 2;
            for (int i = 0; i < extra; i++) {
                const float a = static_cast<float>(rng.Unit() * 6.2831853), dist = 80.0f + static_cast<float>(rng.Unit()) * (hpFrac < 0.5f ? 520.0f : 260.0f);
                AddStrike({target->pos.x + std::cos(a) * dist, target->pos.z + std::sin(a) * dist}, kDragonStrikeRadius * 0.8f, kDragonStrikeDamage * 0.8f, kDragonStrikeDelay + 0.1f * i, b.id);
            }
        }
    }

    void TickBosses(float dt) {
        MaybeSpawnDragon();
        TickStrikes();
        for (auto& b : bosses) {
            if (!b.alive) continue;
            if (IsDragonKind(b.kind)) { TickDragon(b, dt); continue; }
            const BossDef def = BossOf(b.kind);
            // Notice the nearest player in range (the current target is kept while it stays in reach).
            PlayerState* target = Find(b.target);
            if (target && (!target->alive || Distance(target->pos, b.home) > kBossLeash + kBossAggroRange)) target = nullptr;
            if (!target) {
                float best = kBossAggroRange;
                for (auto& p : players) {
                    if (!p.alive || clock < p.invulnUntil) continue;
                    const float d = Distance(p.pos, b.pos);
                    if (d < best) { best = d; target = &p; }
                }
            }
            if (target) { b.target = target->id; b.lostTargetAt = clock; }
            else if (clock - b.lostTargetAt > 4.0f) b.target = kNoPlayer;

            if (target && Distance(b.pos, b.home) <= kBossLeash + 200.0f) {
                const float dx = target->pos.x - b.pos.x, dz = target->pos.z - b.pos.z;
                const float d = std::hypot(dx, dz);
                b.rot = static_cast<int16_t>(static_cast<int32_t>(std::atan2(dx, dz) * (32768.0f / 3.14159265358979f)));
                if (d > kBossReach * 0.75f) {
                    const float step = (std::min)(d, def.speed * dt);
                    b.pos.x += dx / d * step;
                    b.pos.z += dz / d * step;
                }
                if (d <= kBossReach + 20.0f && clock >= b.attackReadyAt) {
                    b.attackReadyAt = clock + def.cooldown;
                    b.lastSmashAt = clock;
                    Damage(target->id, def.damage, b.id);
                }
            } else {
                // Lost them (or they ran too far): walk home and recover.
                b.target = kNoPlayer;
                const float dx = b.home.x - b.pos.x, dz = b.home.z - b.pos.z;
                const float d = std::hypot(dx, dz);
                if (d > 8.0f) {
                    const float step = (std::min)(d, def.speed * 0.8f * dt);
                    b.pos.x += dx / d * step;
                    b.pos.z += dz / d * step;
                    b.rot = static_cast<int16_t>(static_cast<int32_t>(std::atan2(dx, dz) * (32768.0f / 3.14159265358979f)));
                }
                b.health = (std::min)(b.maxHealth, b.health + 1.2f * dt);
            }
        }
    }

    const PlayerState* Winner() const {
        if (state != MatchState::Ending) return nullptr;
        for (const auto& p : players) if (p.alive) return &p;
        return nullptr;
    }

    PlayerState* Find(uint32_t id) {
        for (auto& p : players) if (p.id == id) return &p;
        return nullptr;
    }
    const PlayerState* Find(uint32_t id) const {
        for (const auto& p : players) if (p.id == id) return &p;
        return nullptr;
    }

    MatchState State() const { return state; }
    int Humans() const { return humans; }
    float StormTime() const { return stormTime; }
    float StateTime() const { return stateTime; }
    const Storm& GetStorm() const { return storm; }
    const Circle& MapCircle() const { return map; }
    const std::vector<PlayerState>& Players() const { return players; }
    std::vector<PlayerState>& Players() { return players; }
    uint64_t Seed() const { return seed; }

  private:
    void Enter(MatchState s) {
        if (s == MatchState::Ending) {
            for (auto& p : players) if (p.alive) p.placement = 1;
        }
        state = s;
        stateTime = 0;
        MatchEvent e{MatchEvent::Type::StateChanged};
        e.state = s;
        events.push_back(e);
    }

    static PlayerState MakePlayer(uint32_t id, bool isBot) {
        PlayerState p;
        p.id = id;
        p.isBot = isBot;
        if (isBot) p.scene = static_cast<uint8_t>(kHyruleFieldScene);
        return p;
    }

    static bool IsStarter(const Equipped& e) { return e.item == ItemId::DekuStick && e.rarity == Rarity::Common; }
    void DropEquipment(const PlayerState& p, const Equipped& e) {
        // Put it down a step in front of the player, so it isn't underfoot (and re-grabbed) the instant it lands.
        const float facing = static_cast<float>(p.rot) * (3.14159265f / 32768.0f);
        Vec2 at = {p.pos.x + std::sin(facing) * 90.0f, p.pos.z + std::cos(facing) * 90.0f};
        if (Distance(at, map.center) > map.radius || (placement && !placement(at))) at = p.pos;
        if (!IsStarter(e)) AddLoot({at, e.item, e.rarity, false});
    }

    // Eliminated players leave everything they carry behind for others (the Skulltula pile in the design doc).
    void Eliminate(PlayerState& p, uint32_t killer) {
        p.placement = Alive(); // counted while this player still is
        p.alive = false;
        p.health = 0;
        DropKit(p);
        if (killer != kNoPlayer && killer != p.id) {
            if (PlayerState* k = Find(killer)) k->kills++;
        }
        MatchEvent e{MatchEvent::Type::Eliminated};
        e.a = p.id; e.b = killer;
        events.push_back(e);
    }
    void DropKit(const PlayerState& p) {
        DropEquipment(p, p.weapon);
        for (const Equipped& spare : p.reserve) DropEquipment(p, spare);
        if (p.hasShield) DropEquipment(p, p.shield);
        if (p.hasAbility) DropEquipment(p, p.ability);
        for (int slot = 0; slot < kGearSlots; slot++) {
            if (p.gearMask & (1 << slot)) DropEquipment(p, p.gear[slot]);
        }
        for (const Equipped& potion : p.potions) DropEquipment(p, potion);
    }

    // A Fairy in the bag brings a dying player back with half their hearts and a moment of safety.
    bool TryFairy(PlayerState& p) {
        for (size_t i = 0; i < p.potions.size(); i++) {
            if (!PotionOf(p.potions[i].item).revive) continue;
            p.potions.erase(p.potions.begin() + static_cast<long>(i));
            p.health = (std::max)(1.0f, p.maxHealth * 0.5f);
            p.invulnUntil = clock + 2.0f;
            Cleanse(p);
            p.dirty = true;
            MatchEvent e{MatchEvent::Type::Revived};
            e.a = p.id;
            events.push_back(e);
            return true;
        }
        return false;
    }

    static void Cleanse(PlayerState& p) {
        p.burnUntil = 0;
        p.stunUntil = 0;
        p.frozenUntil = 0;
        p.dirty = true;
    }

    // Instant items: returns false (and the item stays where it is) if using it now would do nothing.
    bool UseInstant(PlayerState& p, ItemId item, Rarity rarity) {
        switch (InstantOf(item)) {
            case InstantEffect::Heart:
                if (p.health >= p.maxHealth) return false;
                p.health = (std::min)(p.maxHealth, p.health + RarityScale(rarity));
                return true;
            case InstantEffect::HeartPiece:
                if (p.maxHealth >= kMaxHealthCap) return false;
                if (++p.heartPieces >= kHeartPiecesPerContainer) {
                    p.heartPieces = 0;
                    p.maxHealth = (std::min)(kMaxHealthCap, p.maxHealth + 1.0f);
                    p.health = (std::min)(p.maxHealth, p.health + 1.0f);
                }
                return true;
            case InstantEffect::HeartContainer:
                if (p.maxHealth >= kMaxHealthCap && p.health >= p.maxHealth) return false;
                p.maxHealth = (std::min)(kMaxHealthCap, p.maxHealth + 1.0f);
                p.health = (std::min)(p.maxHealth, p.health + 1.0f);
                return true;
            case InstantEffect::MagicJar:
                if (!p.hasAbility || clock >= p.abilityReadyAt) return false;
                p.abilityReadyAt = clock + (p.abilityReadyAt - clock) * 0.4f;
                return true;
            default:
                return false;
        }
    }

    // Everyone alive other than `self`, within `radius` of `self`.
    template <class F>
    void ForOthersNear(const PlayerState& self, float radius, F fn) {
        for (auto& o : players) {
            if (!o.alive || o.id == self.id) continue;
            if (Distance(o.pos, self.pos) <= radius) fn(o);
        }
    }

    void Teleport(PlayerState& p, Vec2 to) {
        p.pos = to;
        p.dirty = true;
        MatchEvent e{MatchEvent::Type::Teleported};
        e.a = p.id;
        events.push_back(e);
    }

    // The nearest living player in front of `p` (within a 40 degree cone) and `range`. Null if nobody.
    PlayerState* TargetInFront(const PlayerState& p, float range) {
        const float facing = static_cast<float>(p.rot) * (3.14159265f / 32768.0f);
        PlayerState* best = nullptr;
        float bestDist = range;
        for (auto& o : players) {
            if (!o.alive || o.id == p.id) continue;
            const float dx = o.pos.x - p.pos.x, dz = o.pos.z - p.pos.z;
            const float d = std::sqrt(dx * dx + dz * dz);
            if (d > bestDist) continue;
            float off = std::atan2(dx, dz) - facing;
            while (off > 3.14159265f) off -= 6.2831853f;
            while (off < -3.14159265f) off += 6.2831853f;
            if (std::fabs(off) > 0.70f) continue;
            best = &o;
            bestDist = d;
        }
        return best;
    }

    // Carry out one ability. Returns false if it could not do anything it needs to (no target for a pull).
    bool RunAbility(PlayerState& p, ItemId item, Rarity rarity, bool* startsCooldown) {
        const AbilityDef def = AbilityOf(item);
        const float s = RarityScale(rarity);
        bool did = false;
        for (const Effect& fx : def.fx) {
            switch (fx.type) {
                case EffectType::None:
                    break;
                case EffectType::AoeDamage: {
                    const DamageKind kind = item == ItemId::DinsFire ? DamageKind::Fire : DamageKind::Normal;
                    ForOthersNear(p, fx.radius, [&](PlayerState& o) { Damage(o.id, fx.amount * s, p.id, kind); });
                    did = true;
                    break;
                }
                case EffectType::Heal:
                    p.health = (std::min)(p.maxHealth, p.health + fx.amount * s);
                    did = true;
                    break;
                case EffectType::Invulnerable:
                    p.invulnUntil = (std::max)(p.invulnUntil, clock + (std::min)(8.0f, fx.seconds * s));
                    did = true;
                    break;
                case EffectType::SpeedBoost:
                    p.speedUntil = clock + fx.seconds * s;
                    p.speedMult = (std::min)(1.8f, 1.0f + (fx.amount - 1.0f) * s);
                    did = true;
                    break;
                case EffectType::RevealAll:
                    p.revealUntil = clock + fx.seconds * s;
                    did = true;
                    break;
                case EffectType::StunNearby:
                    ForOthersNear(p, fx.radius, [&](PlayerState& o) {
                        if (TotalsOf(o).stunImmune) return;
                        o.stunUntil = (std::max)(o.stunUntil, clock + fx.seconds * s);
                        o.dirty = true;
                    });
                    did = true;
                    break;
                case EffectType::PullTarget: {
                    PlayerState* t = TargetInFront(p, fx.radius * (0.8f + 0.2f * s));
                    if (!t) break;
                    const float facing = static_cast<float>(p.rot) * (3.14159265f / 32768.0f);
                    Teleport(*t, {p.pos.x + std::sin(facing) * 110.0f, p.pos.z + std::cos(facing) * 110.0f});
                    if (!TotalsOf(*t).stunImmune) {
                        t->stunUntil = (std::max)(t->stunUntil, clock + fx.seconds * s);
                        t->dirty = true;
                    }
                    did = true;
                    break;
                }
                case EffectType::MarkAndReturn:
                    if (p.hasMark && clock < p.markExpires) {
                        Teleport(p, p.mark);
                        p.hasMark = false;
                    } else {
                        p.hasMark = true;
                        p.mark = p.pos;
                        p.markExpires = clock + fx.seconds;
                        *startsCooldown = false; // marking is free; the jump back starts the recharge
                    }
                    did = true;
                    break;
                case EffectType::RandomTeleport: {
                    const Circle zone = storm.SafeZoneAt(stormTime);
                    Teleport(p, RandomPointIn(abilityRng, zone.radius > 0 ? zone : map, placement, 0.8f));
                    did = true;
                    break;
                }
                case EffectType::BurnNearby:
                    ForOthersNear(p, fx.radius, [&](PlayerState& o) {
                        o.burnUntil = clock + fx.seconds * s;
                        o.burnDps = (std::max)(clock < o.burnUntil ? o.burnDps : 0.0f, fx.amount * s);
                        o.burnBy = p.id;
                        o.dirty = true;
                    });
                    did = true;
                    break;
                case EffectType::Regen:
                    p.regenUntil = clock + fx.seconds;
                    p.regenRate = fx.amount * s;
                    did = true;
                    break;
                case EffectType::Cleanse:
                    Cleanse(p);
                    did = true;
                    break;
                case EffectType::Shockwave: {
                    // Everyone near is thrown straight away from the user (the server moves them; their game follows), then left dazed.
                    ForOthersNear(p, fx.radius, [&](PlayerState& o) {
                        float dx = o.pos.x - p.pos.x, dz = o.pos.z - p.pos.z;
                        float len = std::hypot(dx, dz);
                        if (len < 1.0f) { dx = 1.0f; dz = 0.0f; len = 1.0f; }
                        const float push = fx.amount * s;
                        Vec2 to = {o.pos.x + dx / len * push, o.pos.z + dz / len * push};
                        const float off = Distance(to, map.center);
                        if (off > map.radius - 20.0f) to = {map.center.x + (to.x - map.center.x) / off * (map.radius - 20.0f), map.center.z + (to.z - map.center.z) / off * (map.radius - 20.0f)};
                        if (placement && !placement(to)) to = {o.pos.x + dx / len * push * 0.4f, o.pos.z + dz / len * push * 0.4f}; // no ground there: a shorter throw
                        Teleport(o, to);
                        if (!TotalsOf(o).stunImmune) { o.stunUntil = (std::max)(o.stunUntil, clock + fx.seconds * s); o.dirty = true; }
                    });
                    did = true;
                    break;
                }
                case EffectType::RandomSong: {
                    ItemId songs[kItemCount];
                    int n = 0;
                    for (int i = 0; i < kItemCount; i++) {
                        const ItemId candidate = kItems[i].id;
                        if (!IsSong(candidate)) continue;
                        if (fx.radius < 0.5f && !IsSimpleSong(candidate)) continue;
                        songs[n++] = candidate;
                    }
                    if (n == 0) break;
                    bool ignored = true;
                    did = RunAbility(p, songs[abilityRng.Below(static_cast<uint32_t>(n))], rarity, &ignored) || did;
                    break;
                }
            }
        }
        return did;
    }

    uint64_t seed;
    Circle map;
    Storm storm;
    Rng abilityRng;
    MatchState state = MatchState::Lobby;
    float stateTime = 0, stormTime = 0, clock = 0;
    int humans = 0;
    std::vector<PlayerState> players;
    std::vector<LootEntry> loot;
    std::vector<MatchEvent> events;
    PlacementFn placement;
    std::vector<Vec2> lootSpots;
    std::vector<Vec2> bossSpots;
    std::vector<MiniBoss> bosses;
    std::vector<Strike> strikes;
    bool majorBoss = false;
    bool dragonSpawned = false;
    int bossCount = 0;
    int mapId = 0;           // which place this is (shared/map.h): decides the bosses
    int playerLimit = kMaxPlayers; // the host's game turns bosses on (GameServer::SetBossCount); plain matches and the tests have none
};

} // namespace royale
