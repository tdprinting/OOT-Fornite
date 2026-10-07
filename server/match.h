#pragma once
#include "../shared/balance.h"
#include "../shared/ally.h"
#include "../shared/anim.h"
#include "../shared/boss.h"
#include "../shared/combat.h"
#include "../shared/map.h"
#include "../shared/convergence_data.h"
#include "../shared/placement.h"
#include "../shared/props.h"
#include "../shared/replay.h"
#include "../shared/storm.h"
#include "../shared/vehicle.h"
#include "../shared/weather.h"
#include "nav.h"
#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <vector>

namespace royale {

struct Equipped {
    ItemId item;
    Rarity rarity;
};

enum class DamageKind : uint8_t { Normal, Storm, Fire, Explosion, Gas };

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
    Equipped weapon = {ItemId::BasicSword, Rarity::Common}; // starter weapon, like Fortnite's pickaxe: a weak sword that never runs out
    int rupees = 0;                                        // money: hire helpers
    std::array<int, kAmmoKinds> ammo = {};                 // arrows, seeds, bombs, bombchus, nuts
    bool hasShield = false;
    Equipped shield = {ItemId::DekuShield, Rarity::Common};
    std::vector<Equipped> potions;                         // consumables bag, at most kMaxPotions
    std::vector<Equipped> reserve;                         // backup weapons for the hotbar, at most kMaxReserveWeapons
    std::array<Equipped, kGearSlots> gear = {};            // one passive item per slot
    uint8_t gearMask = 0;                                  // bit n set when gear[n] is filled
    Equipped ability = {ItemId::DinsFire, Rarity::Common};
    bool hasAbility = false;
    float abilityReadyAt = 0;
    float adultUntil = 0;                                  // Adult Power: bigger, stronger, tougher and faster until then
    float magic = kMaxMagic;                               // the magic meter as of magicStamp (it refills by itself; see Match::MagicNow)
    float magicStamp = 0;
    bool hasMark = false;                                  // Farore's Wind
    Vec2 mark = {};
    float markExpires = 0;

    // ---- timed effects; every "...Until" is a match-clock time
    float burnUntil = 0, burnDps = 0;
    float gasTime = 0;   // seconds spent in Lilo's cloud (builds while inside, drains outside); it hurts once it passes kFartGraceSeconds
    uint32_t burnBy = 0xFFFFFFFFu;
    float stunUntil = 0, frozenUntil = 0;
    float invulnUntil = 0;
    float npcHitReadyAt = 0;   // a villager can land another blow after this (match clock)
    float speedUntil = 0, speedMult = 1;
    float regenUntil = 0, regenRate = 0;
    float revealUntil = 0;
    float dmgTakenUntil = 0, dmgTakenMult = 1;

    float attackReadyAt = 0;
    float rollUntil = 0;     // a dodge roll in progress: attacks aimed at a rolling player miss
    float rollReadyAt = 0;   // the next roll can start at this time
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
    enum class Type : uint8_t { Damaged, Eliminated, LootTaken, LootAdded, StateChanged, AbilityUsed, Teleported, Revived, BossDown, BossSpawned, Strike, SupplyDrop, Weather, AllyChanged, AllyAction } type;
    uint32_t a = kNoPlayer; // Damaged: target | Eliminated: victim | LootTaken: taker | AbilityUsed: user | Teleported/Revived: player
    uint32_t b = kNoPlayer; // Damaged/Eliminated: attacker (kNoPlayer = storm, disconnect)
    float amount = 0;       // Damaged: hearts dealt
    float health = 0;       // Damaged: target's health afterwards
    size_t index = 0;       // LootTaken / LootAdded
    MatchState state = MatchState::Lobby; // StateChanged
    uint8_t item = 0;       // AbilityUsed: which ability
    float x = 0, z = 0;     // AbilityUsed: where the user stood | BossDown: where it fell (a = boss id, b = who landed the last hit)
    // AllyChanged: a = ally index, b = its owner (kNoPlayer when free), item = 0 hired / 1 released / 2 fell. AllyAction: a = ally index, b = target id, x/z = target.
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
    float windupUntil = -1;    // a blow is being wound up (it lands then, on whoever is still in front of it)
    float y = 0;               // height above the ground (the major boss flies; a mini boss leaps)
    DragonMode mode = DragonMode::Patrol;
    float modeUntil = 0;
    uint8_t aux = 0;           // which variant of the current move (for the client: which hand, fire or ice)
    int moves = 0;             // special moves made so far (they take turns)
    float specialReadyAt = 0;  // its own special move is ready again
    Vec2 from = {}, to = {};   // the line of a leap, a charge or a hidden trip
    float moveStart = 0;
    float chargeSpeed = 0;
    DragonMode next = DragonMode::Chase;   // what it does when the current move ends
    float nextSeconds = 0;                 // ...for how long, when that is a daze
    std::vector<uint32_t> hitThisMove;     // who a charge has already hit
    std::vector<Vec2> path;                // its way around walls (with a navigation grid)
    size_t pathIdx = 0;
    float repathAt = 0;
    Vec2 pathGoal = {};
    bool chainAfterSlam = false;           // Morpha: the core drops out after the swing
    Vec2 chargeThrough = {};               // Phantom Ganon: where you stood when he went into his portal
    float trail = 0;                       // the Magma Dodongo: distance rolled since the last patch of fire
    bool reassembled = false;              // the Stalfos has already pulled itself back together once
    // The major boss only:
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
    bool lightning = false;   // a bolt from a thunderstorm: plain damage, no burning
    StrikeStyle style = StrikeStyle::Fire;   // what it does besides the damage (and how it looks)
};

struct AttackResult {
    bool ok = false; // the attack was allowed and its cooldown started
    bool hit = false;
    float damage = 0;
    bool killed = false;
    bool dodged = false; // the target was rolling: the attack was spent but missed
    bool blocked = false; // the target took it on a raised shield
    int extraHits = 0;    // a spin attack: everyone else it caught
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
    // How many of the chests RegenerateLoot made are scattered ones (the rest are the towns' and the climbs'); they come first in Loot().
    size_t ScatteredLoot() const { return loot.size() - (std::min)(loot.size(), lootSpots.size() + chestSites.size()); }
    // Chests on climbs and in hideaways (see shared/poi.h): better loot, further apart.
    void SetChestSites(std::vector<ChestSite> sites) { chestSites = std::move(sites); }
    // Where the scattered chests may go (shared/placement.h): at camps, boulders, thickets, cliffs and the map's own scenery, on level ground. Without a
    // plan (the unit tests' bare matches) they fall back to random points on valid ground.
    void SetLootPlan(LootPlan plan) { lootPlan = std::make_shared<LootPlan>(std::move(plan)); }
    void RegenerateLoot(int count, float chestFraction = 0.15f) {
        loot.clear();
        // The scattered chests keep their distance from every building, climb and hideaway chest and from each other.
        std::vector<Vec2> taken = lootSpots;
        for (const ChestSite& s : chestSites) taken.push_back(s.pos);
        if (lootPlan) {
            for (const LootSpawn& l : GenerateAnchoredLoot(seed, *lootPlan, count * 7 / 10, chestFraction, &taken, (std::max)(450.0f, map.radius * 0.08f))) loot.push_back({l, false});
        } else {
            for (const LootSpawn& l : GenerateLoot(seed, map, count, chestFraction, placement, &taken, map.radius * 0.11f)) loot.push_back({l, false});
        }
        for (const LootSpawn& l : GenerateSpotLoot(seed, lootSpots)) loot.push_back({l, false});
        Rng siteRng(seed ^ 0x73697465ull); // "site"
        const size_t firstSite = loot.size();
        for (const ChestSite& s : chestSites) loot.push_back({SiteChest(siteRng, s.pos, s.bonus), false});
        // Some Rare chests hold a Piece of Heart (four make a heart), so more health in a match is within reach.
        Rng pieceRng(seed ^ 0x7069656365ull);   // "piece"
        for (LootEntry& e : loot) {
            LootSpawn& l = e.spawn;
            if (l.container && !l.special && l.rarity == Rarity::Rare && pieceRng.Unit() < kRareChestHeartPieceChance) l.item = ItemId::HeartPiece;
        }
        // A few of the hideaways and climbs hold a Heart Container instead: pink chests, one more heart for whoever finds one.
        std::vector<size_t> good;
        for (size_t i = 0; i < chestSites.size(); i++) if (chestSites[i].bonus >= 1) good.push_back(firstSite + i);
        const int hearts = (std::min)(static_cast<int>(good.size()), 2 + static_cast<int>(map.radius / 3200.0f));
        Rng heartRng(seed ^ 0x6865617274ull);   // "heart"
        for (int n = 0; n < hearts; n++) {
            const size_t pick = heartRng.Below(static_cast<uint32_t>(good.size()));
            LootSpawn& l = loot[good[pick]].spawn;
            l.item = ItemId::HeartContainer; l.rarity = Rarity::Legendary; l.fromChest = true; l.container = true; l.special = true;
            good.erase(good.begin() + static_cast<long>(pick));
        }
    }

    // Add a human. Returns false if the lobby is full or the match already started.
    // How many players the match has, bots included (2 to 32). Can't go below the people already in the lobby.
    bool SetPlayerLimit(int n) {
        if (state != MatchState::Lobby || n < kMinPlayers || n > kMaxPlayers || n < static_cast<int>(players.size())) return false;
        playerLimit = n;
        return true;
    }
    int PlayerLimit() const { return playerLimit; }
    // Test mode: Start() adds no bots, and the match does not end just because one player is left (it ends when nobody is). Everything else
    // (storm, loot, bosses, supply drops, allies, weather) runs as in a real match. Lobby only.
    bool SetSoloTest(bool on) { if (state != MatchState::Lobby) return false; soloTest = on; return true; }
    bool SoloTest() const { return soloTest; }

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
        while (!soloTest && !sandbox && static_cast<int>(players.size()) < playerLimit) {
            players.push_back(MakePlayer(nextId++, true));
            players.back().scene = static_cast<uint8_t>(MapOf(mapId).scene);
        }
        Rng spawn(seed ^ 0x7370776Eull); // "spwn"
        for (auto& p : players) p.pos = RandomPointIn(spawn, map, placement, 0.9f);
        if (sandbox) for (auto& p : players) p.pos = sandboxSpawn;
        SpawnBosses();
        SpawnAllies();
        SpawnVehicles();
        nextSupplyAt = kSupplyFirstSec; supplyCount = 0; pendingSupply.clear();
        for (auto& pl : players) { pl.magic = kMaxMagic; pl.magicStamp = clock; }
        replay = Replay{}; replayNextAt = 0;
        for (const auto& pl : players) replay.ids.push_back(static_cast<uint16_t>(pl.id));
        spell = -1; boltCount = 0; weather = Weather{PickSeason(wopt, seed), Sky::Clear, 0};
        if (sandbox) {   // the test map has no countdown and no drop: you are on the ground and the match is on
            stormTime = 0;
            Enter(MatchState::InMatch);
            SandboxWeather(Season::Summer, Sky::Clear, 0);
            return true;
        }
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
                TickWeather();
                if (stateTime >= kDropSec) Enter(MatchState::InMatch);
                break;
            case MatchState::InMatch:
                if (!sandbox || sandboxStormRuns) stormTime += dt;
                TickReplay();
                TickWeather();
                TickSupplyDrops();
                for (auto& p : players) {
                    if (!p.alive) continue;
                    float dps = sandbox && !sandboxStormRuns ? 0.0f : storm.DamagePerSecond(p.pos, stormTime);
                    if (dps > 0) Damage(p.id, dps * dt, kNoPlayer, DamageKind::Storm, false);
                    if (!p.alive) continue;
                    if (clock < p.burnUntil) Damage(p.id, p.burnDps * dt, p.burnBy, DamageKind::Fire, false);
                    if (!p.alive) continue;
                    TickGas(p, dt);
                    if (!p.alive) continue;
                    if (clock < p.regenUntil && p.health < p.maxHealth) p.health = (std::min)(p.maxHealth, p.health + p.regenRate * dt);
                    if (p.hasMark && clock >= p.markExpires) { p.hasMark = false; p.dirty = true; }
                }
                TickBosses(dt);
                TickVehicles(dt);
                if (!sandbox && Alive() <= (soloTest ? 0 : 1)) Enter(MatchState::Ending);
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
        if (sandbox && sandboxGod && !p->isBot) return false;   // the test map: nothing hurts you until you switch it off

        const GearTotals g = TotalsOf(*p);
        float mult = g.damageTaken;
        if (kind == DamageKind::Storm) mult *= g.storm;
        else if (kind == DamageKind::Fire) mult *= g.fire;
        else if (kind == DamageKind::Explosion) mult *= g.explosion;
        if (clock < p->dmgTakenUntil) mult *= p->dmgTakenMult;
        if (clock < p->adultUntil) mult *= kAdultTaken;
        if (clock < p->frozenUntil && kind != DamageKind::Storm) mult *= 1.25f; // frozen targets are brittle
        if (kind != DamageKind::Storm) mult *= IsBossId(attacker) ? kBossDamageScale : attacker == kNoPlayer ? kHazardDamageScale : kPlayerDamageScale;
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

    // An angered villager hit this player. A few hits a second at most, and only while the match is on.
    bool NpcHit(uint32_t id, float hearts) {
        PlayerState* p = Find(id);
        if (!p || !p->alive || (state != MatchState::InMatch && state != MatchState::Drop)) return false;
        if (clock < p->npcHitReadyAt) return false;
        p->npcHitReadyAt = clock + 0.6f;
        Damage(id, (std::min)(hearts, 2.0f), kNoPlayer, DamageKind::Normal, false);
        return true;
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
    // A dodge roll. Bots call this when they decide to roll; for humans the game does the rolling and the server notices the roll animation in
    // their input (see GameServer). It lasts a third of a second and can't be repeated for a while. Hits that would land while it lasts miss.
    static constexpr float kRollSeconds = 0.35f, kRollCooldown = 1.1f;
    static bool HasAmmo(const PlayerState& p, ItemId weapon) {
        const AmmoKind k = AmmoUsedBy(weapon);
        return k == AmmoKind::None || p.ammo[static_cast<int>(k)] > 0;
    }
    // The stats of the weapon in hand right now (a bow with no arrows is bashed with like the basic sword).
    static WeaponStats StatsOf(const PlayerState& p) { return ActiveWeapon(p.weapon.item, HasAmmo(p, p.weapon.item)); }
    static int AmmoCapOf(const PlayerState& p, AmmoKind k) {
        const bool pack = (p.gearMask & (1 << static_cast<int>(GearSlot::Pack))) != 0;
        return AmmoCap(k, pack, pack ? p.gear[static_cast<int>(GearSlot::Pack)].item : ItemId::BasicSword);
    }
    // Picking up a bow, the slingshot or something to throw gives enough to start with if you have none.
    static void GiveStarterAmmo(PlayerState& p, ItemId weapon) {
        const AmmoKind k = AmmoUsedBy(weapon);
        if (k == AmmoKind::None) return;
        int& n = p.ammo[static_cast<int>(k)];
        n = (std::max)(n, (std::min)(AmmoStarter(k), AmmoCapOf(p, k)));
    }
    void SpendAmmo(PlayerState& p, ItemId weapon) {
        const AmmoKind k = AmmoUsedBy(weapon);
        if (k == AmmoKind::None) return;
        int& n = p.ammo[static_cast<int>(k)];
        n = (std::max)(0, n - WeaponOf(weapon).shots); // a volley spends one for each pellet
        p.dirty = true;
    }
    // How many pellets of a volley reach a target `dist` away: all of them close up, fewer further off.
    static int Pellets(const WeaponStats& w, float dist) { return (std::max)(1, w.shots - (dist >= 250.0f ? 1 : 0) - (dist >= 500.0f ? 1 : 0)); }

    bool StartRoll(uint32_t id) {
        PlayerState* p = Find(id);
        if (!p || !p->alive || (state != MatchState::InMatch && state != MatchState::Drop) || clock < p->rollReadyAt || Stunned(*p)) return false;
        p->rollUntil = clock + kRollSeconds;
        p->rollReadyAt = clock + kRollCooldown;
        return true;
    }
    bool Rolling(const PlayerState& p) const { return clock < p.rollUntil; }
    bool CanRoll(const PlayerState& p) const { return p.alive && clock >= p.rollReadyAt && !Stunned(p); }

    // Whether `t` has its shield up towards `from` against this weapon.
    static bool Guards(const PlayerState& t, Vec2 from, const WeaponStats& w) {
        if (w.effect == WeaponEffect::PierceShield || (w.splashRadius > 0 && w.ranged)) return false;
        return Guards(t, from);
    }
    static bool Guards(const PlayerState& t, Vec2 from) {
        if (t.anim != static_cast<uint8_t>(Anim::Guard) || !t.hasShield || IsTwoHanded(t.weapon.item)) return false;
        const float face = static_cast<float>(t.rot) * (3.14159265f / 32768.0f);
        float off = std::atan2(from.x - t.pos.x, from.z - t.pos.z) - face;
        while (off > 3.14159265f) off -= 6.2831853f;
        while (off < -3.14159265f) off += 6.2831853f;
        return std::fabs(off) <= kGuardHalfAngle;
    }

    // A blow from something that is not a player's weapon (a boss's swing or charge, a helper's strike) follows the same rules a player's does, so a
    // fight feels the same whoever it is against: a roll goes through it, a shield's own worth comes off it, and a raised shield facing it takes most of
    // what is left. Returns true if it hurt (false: dodged). The ones that are explosions, breath or magic don't use this (shields don't stop those).
    bool Blow(PlayerState& victim, Vec2 from, float hearts, uint32_t attacker) {
        if (!victim.alive || clock < victim.rollUntil) return false;
        float amount = hearts;
        if (victim.hasShield) amount *= 1.0f - ShieldReduction(victim.shield.item, victim.shield.rarity);
        if (Guards(victim, from)) amount *= 1.0f - kGuardBlock;
        Damage(victim.id, amount, attacker, DamageKind::Normal);
        return true;
    }

    // A shot, throw or lob that had nothing in reach (the arrow, seed, bomb or bombchu still flies on the player's own screen): it spends the ammo and the
    // cooldown like any other, so the count under the hotbar goes down however it ends. Weapons that use no ammo are not affected.
    AttackResult ShootAtNothing(uint32_t attackerId) {
        AttackResult r;
        if (state != MatchState::InMatch) return r;
        PlayerState* a = Find(attackerId);
        if (!a || !a->alive || AmmoUsedBy(a->weapon.item) == AmmoKind::None || !HasAmmo(*a, a->weapon.item)) return r;
        const WeaponStats w = ActiveWeapon(a->weapon.item, true);
        if (w.damage <= 0 || clock < a->attackReadyAt || Stunned(*a)) return r;
        a->attackReadyAt = clock + w.cooldown;
        SpendAmmo(*a, a->weapon.item);
        r.ok = true;
        return r;
    }

    AttackResult Attack(uint32_t attackerId, uint32_t targetId, bool hit = true, AttackStyle style = AttackStyle::Normal) {
        AttackResult r;
        if (state != MatchState::InMatch) return r;
        if (IsBossId(targetId)) return AttackBoss(attackerId, targetId, hit);
        if (IsVehicleId(targetId)) return AttackVehicle(attackerId, targetId, hit);
        PlayerState* a = Find(attackerId);
        PlayerState* t = Find(targetId);
        if (!a || !t || a == t || !a->alive || !t->alive) return r;
        const bool hadAmmo = HasAmmo(*a, a->weapon.item);
        WeaponStats w = ActiveWeapon(a->weapon.item, hadAmmo);
        if (w.damage <= 0 || clock < a->attackReadyAt) return r;
        if (clock < a->stunUntil || clock < a->frozenUntil) return r;
        if (Distance(a->pos, t->pos) > w.range * 1.1f) return r;
        if (w.ranged) style = AttackStyle::Normal;   // only blades and hammers jump slash and spin
        a->attackReadyAt = clock + w.cooldown * (style == AttackStyle::Spin ? kSpinRecovery : style == AttackStyle::JumpSlash ? kJumpSlashRecovery : 1.0f);
        r.ok = true;
        if (hadAmmo) SpendAmmo(*a, a->weapon.item);
        if (!hit) return r;
        if (clock < t->rollUntil && w.splashRadius <= 0) { r.dodged = true; return r; } // rolled out of the way (blasts are too wide to roll out of)

        const GearTotals ag = TotalsOf(*a);
        const float base = w.damage * static_cast<float>(Pellets(w, Distance(a->pos, t->pos))) * (hadAmmo ? RarityScale(a->weapon.rarity) : 1.0f) * (w.ranged ? ag.ranged : ag.melee) * (clock < a->adultUntil ? kAdultDamage : 1.0f) *
                           (style == AttackStyle::JumpSlash ? kJumpSlashDamage : 1.0f);
        auto dealt = [&](const PlayerState& victim, float amount, bool* blocked) {
            float reduction = 0.0f;
            if (w.effect != WeaponEffect::PierceShield && victim.hasShield) reduction = ShieldReduction(victim.shield.item, victim.shield.rarity);
            *blocked = Guards(victim, a->pos, w);
            return amount * (1.0f - reduction) * (*blocked ? 1.0f - kGuardBlock : 1.0f);
        };
        r.damage = dealt(*t, base, &r.blocked);
        r.hit = true;
        r.killed = Damage(targetId, r.damage, attackerId, w.splashRadius > 0 && w.ranged ? DamageKind::Explosion : DamageKind::Normal);
        if (style == AttackStyle::Spin) {   // the spin catches everyone else within reach too
            const float reach = w.range * 1.1f + kSpinReachBonus;
            for (auto& o : players) {
                if (!o.alive || o.id == attackerId || o.id == targetId || Distance(o.pos, a->pos) > reach || clock < o.rollUntil || clock < o.invulnUntil) continue;
                bool blocked = false;
                const float amount = dealt(o, base, &blocked);
                Damage(o.id, amount, attackerId, DamageKind::Normal);
                r.extraHits++;
            }
        }

        if (!r.killed && t->alive && !r.blocked) {   // a shield taken hit doesn't burn, freeze or stun
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
                    if (w.splashRadius > 0 && !immune) {   // a shockwave stuns everyone it reaches, not only the one it was thrown at
                        for (auto& o : players) {
                            if (!o.alive || o.id == attackerId || o.id == targetId || Distance(o.pos, t->pos) > w.splashRadius || TotalsOf(o).stunImmune) continue;
                            o.stunUntil = (std::max)(o.stunUntil, clock + seconds);
                            o.dirty = true;
                        }
                    }
                    break;
                case WeaponEffect::Pull: {   // the chain reels them in to just in front of you
                    const float facing = static_cast<float>(a->rot) * (3.14159265f / 32768.0f);
                    Teleport(*t, {a->pos.x + std::sin(facing) * 110.0f, a->pos.z + std::cos(facing) * 110.0f});
                    if (!immune) { t->stunUntil = (std::max)(t->stunUntil, clock + seconds); t->dirty = true; }
                    break;
                }
                default:
                    break;
            }
        }
        if (w.splashRadius > 0) {
            const Vec2 centre = t->pos;
            for (auto& o : players) {
                if (!o.alive || o.id == attackerId || o.id == targetId) continue;
                if (Distance(o.pos, centre) <= w.splashRadius) Damage(o.id, base * 0.6f, attackerId, w.ranged ? DamageKind::Explosion : DamageKind::Normal);
            }
            for (auto& v : vehicles) if (!v.wrecked && !v.gone && Distance({v.body.x, v.body.z}, centre) <= w.splashRadius + kCartHitRadius * 0.5f) DamageVehicle(v, base * 0.9f, attackerId);
        }
        return r;
    }

    // What is inside a rock or bush, rolled from the seed and the prop's number: nothing, rupees or ammo. Granted straight to whoever broke it.
    struct PropDrop { bool any = false; ItemId item = ItemId::Rupees; int amount = 0; };
    PropDrop GrantPropLoot(uint32_t playerId, PropKind kind, size_t index) {
        PropDrop d;
        PlayerState* p = Find(playerId);
        if (!p || !p->alive) return d;
        Rng rng(seed ^ (static_cast<uint64_t>(index) * 0x9E3779B97F4A7C15ull) ^ 0x736D617368ull); // "smash"
        const uint32_t r = rng.Below(100);
        auto give = [&](ItemId item, int amount) { d.any = true; d.item = item; d.amount = amount; };
        switch (kind) {
            case PropKind::Bush:
                if (r < 40) give(ItemId::Rupees, 1);
                else if (r < 48) give(ItemId::Rupees, 5);
                else if (r < 58) give(ItemId::ArrowAmmo, 3);
                else if (r < 66) give(ItemId::SeedAmmo, 5);
                else if (r < 70) give(ItemId::BombAmmo, 1);
                else if (r < 74) give(ItemId::NutAmmo, 2);
                break;
            case PropKind::Rock:
                if (r < 28) give(ItemId::Rupees, 5);
                else if (r < 38) give(ItemId::Rupees, 20);
                else if (r < 52) give(ItemId::ArrowAmmo, 5);
                else if (r < 62) give(ItemId::SeedAmmo, 8);
                else if (r < 72) give(ItemId::BombAmmo, 2);
                else if (r < 78) give(ItemId::BombchuAmmo, 2);
                else if (r < 84) give(ItemId::NutAmmo, 3);
                break;
            case PropKind::Boulder:
                if (r < 30) give(ItemId::Rupees, 20);
                else if (r < 42) give(ItemId::Rupees, 50);
                else if (r < 58) give(ItemId::ArrowAmmo, 10);
                else if (r < 66) give(ItemId::SeedAmmo, 12);
                else if (r < 76) give(ItemId::BombAmmo, 3);
                else if (r < 84) give(ItemId::BombchuAmmo, 3);
                else if (r < 90) give(ItemId::NutAmmo, 5);
                else give(ItemId::Rupees, 5);
                break;
            default: break;
        }
        if (!d.any) return d;
        if (d.item == ItemId::Rupees) {
            p->rupees += d.amount;
        } else {
            const AmmoKind k = AmmoGivenBy(d.item);
            int& n = p->ammo[static_cast<int>(k)];
            n = (std::min)(AmmoCapOf(*p, k), n + d.amount);
        }
        p->dirty = true;
        return d;
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
                if (InstantOf(s.item) == InstantEffect::Rupees) {
                    p->rupees += (std::max)(1, static_cast<int>(s.amount));
                } else if (InstantOf(s.item) == InstantEffect::Ammo) {
                    const AmmoKind k = AmmoGivenBy(s.item);
                    int& n = p->ammo[static_cast<int>(k)];
                    const int cap = AmmoCapOf(*p, k);
                    if (n >= cap) return false;                               // already full: leave it for someone else
                    n = (std::min)(cap, n + (std::max)(1, static_cast<int>(s.amount)));
                } else if (!UseInstant(*p, s.item, s.rarity)) {
                    return false;
                }
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
        if (KindOf(s.item) == ItemKind::Weapon) {
            GiveStarterAmmo(*p, s.item);
            if (!p->reserve.empty()) GiveStarterAmmo(*p, p->reserve.back().item);
        }
        p->dirty = true;
        if (s.container) {
            p->chestsOpened++;
            SpillRupees(s.pos, index);
        }
        loot[index].taken = true;
        MatchEvent e{MatchEvent::Type::LootTaken};
        e.a = id; e.index = index;
        events.push_back(e);
        return true;
    }

    // An opened chest spills a few green and blue rupees around it, to pick up (money for hiring helpers).
    void SpillRupees(Vec2 at, size_t index) {
        Rng r(seed ^ 0x72757065ull ^ (static_cast<uint64_t>(index) * 0x9E3779B97F4A7C15ull));   // "rupe"
        const int n = kChestRupeesMin + static_cast<int>(r.Below(static_cast<uint32_t>(kChestRupeesMax - kChestRupeesMin + 1)));
        for (int i = 0; i < n; i++) {
            const float angle = 6.2831853f * (static_cast<float>(i) + static_cast<float>(r.Unit()) * 0.6f) / static_cast<float>(n);
            Vec2 to = {at.x + std::cos(angle) * 55.0f, at.z + std::sin(angle) * 55.0f};
            if (Distance(to, map.center) > map.radius || (placement && !placement(to))) to = at;
            LootSpawn money = {to, ItemId::Rupees, Rarity::Common, false, false};
            money.amount = r.Unit() < 0.7 ? 1 : 5;
            AddLoot(money);
        }
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


    // ---- allies (shared/ally.h): four people to hire. Their behaviour is in BotController::StepAllies; the rules of hiring are here. ----
    void PushEvent(const MatchEvent& e) { events.push_back(e); }
    // The replay (shared/replay.h): where everyone was every kReplayStepSec of the match, and who eliminated whom.
    const Replay& GetReplay() const { return replay; }
    void TickReplay() {
        if (stormTime < replayNextAt || static_cast<int>(replay.frames.size()) >= kReplayMaxFrames) return;
        replayNextAt = stormTime + kReplayStepSec;
        std::vector<int16_t> f;
        for (const auto& p : players) {
            f.push_back(p.alive ? static_cast<int16_t>((std::max)(-30000.0f, (std::min)(30000.0f, p.pos.x))) : kReplayGone);
            f.push_back(static_cast<int16_t>((std::max)(-30000.0f, (std::min)(30000.0f, p.pos.z))));
        }
        replay.frames.push_back(std::move(f));
    }
    void SetAllySpots(std::vector<Vec2> spots) { allySpots = std::move(spots); }
    const std::vector<AllyState>& Allies() const { return allies; }
    std::vector<AllyState>& MutableAllies() { return allies; }
    int AlliesOf(uint32_t player) const { int n = 0; for (const auto& a : allies) n += a.alive && a.owner == player; return n; }
    void SpawnAllies() {
        allies.clear();
        Rng rng(seed ^ 0x616C6C79ull);   // "ally"
        for (int i = 0; i < kAllyCount; i++) {
            AllyState a;
            a.index = static_cast<uint8_t>(i);
            a.kind = static_cast<AllyKind>(i);
            a.health = AllyOf(a.kind).maxHealth;
            if (static_cast<size_t>(i) < allySpots.size()) a.pos = allySpots[static_cast<size_t>(i)];
            else a.pos = RandomPointIn(rng, map, placement, 0.7f);
            a.rot = static_cast<int16_t>(rng.Below(65536) - 32768);
            allies.push_back(a);
        }
    }
    // Pay an ally to join you. The player must be standing next to it, have the rupees and have a free place (two allies at most).
    bool HireAlly(uint32_t playerId, int index) {
        PlayerState* p = Find(playerId);
        if (!p || !p->alive || (state != MatchState::InMatch && state != MatchState::Drop) || index < 0 || index >= static_cast<int>(allies.size())) return false;
        AllyState& a = allies[static_cast<size_t>(index)];
        const AllyDef& def = AllyOf(a.kind);
        if (!a.alive || a.Hired() || p->rupees < def.price || AlliesOf(playerId) >= kMaxAlliesPerPlayer) return false;
        if (Distance(p->pos, a.pos) > kHireRange * 1.15f) return false;
        p->rupees -= def.price;
        p->dirty = true;
        a.owner = playerId;
        a.attackReadyAt = clock + 1.0f;
        a.healReadyAt = clock + (def.healEvery > 0 ? 4.0f : 0.0f);   // a Zora can mend you almost at once, then settles to its rhythm
        MatchEvent e{MatchEvent::Type::AllyChanged};
        e.a = a.index; e.b = playerId; e.item = 0;
        events.push_back(e);
        return true;
    }
    void ReleaseAlly(AllyState& a, bool fell) {
        a.owner = kNoPlayer;
        if (fell) a.alive = false;
        MatchEvent e{MatchEvent::Type::AllyChanged};
        e.a = a.index; e.b = kNoPlayer; e.item = fell ? 2 : 1;
        events.push_back(e);
    }
    // An ally's hit on a player or boss, credited to the ally's owner. Returns true if it landed.
    bool AllyStrike(AllyState& a, uint32_t targetId) {
        const AllyDef& def = AllyOf(a.kind);
        PlayerState* owner = Find(a.owner);
        if (!owner || !owner->alive || clock < a.attackReadyAt) return false;
        a.attackReadyAt = clock + def.cooldown;
        a.actUntil = clock + 0.45f;
        MatchEvent act{MatchEvent::Type::AllyAction};
        act.a = a.index; act.b = targetId;
        if (IsBossId(targetId)) {
            for (auto& b : bosses) {
                if (b.id != targetId || !b.alive) continue;
                if ((IsDragonKind(b.kind) && b.y > kDragonAirborneAbove && def.melee) || BossHidden(b.mode)) return false;
                act.x = b.pos.x; act.z = b.pos.z; events.push_back(act);
                const float dmg = def.damage * (IsDragonKind(b.kind) ? 0.7f : 1.0f);
                const float dealt = (std::min)(dmg, b.health);
                b.health -= dmg;
                owner->damageDealt += dealt;
                b.target = owner->id; b.lostTargetAt = clock;
                MatchEvent e{MatchEvent::Type::Damaged};
                e.a = targetId; e.b = owner->id; e.amount = dmg; e.health = (std::max)(0.0f, b.health);
                events.push_back(e);
                if (b.health <= 0 && !StalfosGetsUp(b)) KillBoss(b, *owner);
                return true;
            }
            return false;
        }
        PlayerState* t = Find(targetId);
        if (!t || !t->alive || t->id == a.owner) return false;
        act.x = t->pos.x; act.z = t->pos.z; events.push_back(act);
        if (clock < t->invulnUntil) return true;                              // protected: the swing still happened
        Blow(*t, a.pos, def.damage, owner->id);                               // a roll dodges it, a shield and a guard take their share
        return true;
    }

    // ---- carts (shared/vehicle.h): anyone can get in one, drive it, ride along, run people over with it or smash it up. ----------------------
    // The ground and walls the server drives carts on: the host builds it from the bots' navigation grid. Without one, flat open ground.
    void SetVehicleWorld(CartWorld w) { vehicleWorld = std::move(w); }
    // Somewhere a person can stand (inside the map and not in a wall), for putting riders down and pushing people out of a cart's way.
    bool OpenGround(Vec2 at) const { return Inside(at) && !(vehicleWorld.solid && vehicleWorld.solid(at.x, at.z)); }
    // Good places for carts (the edges of the towns); more are found at random if these run out. And how many there are.
    void SetVehicleSpots(std::vector<Vec2> spots) { vehicleSpots = std::move(spots); }
    void SetVehicleCount(int n) { vehicleCount = (std::max)(0, (std::min)(n, kMaxVehicles)); }
    int VehicleCount() const { return vehicleCount; }
    const CartWorld& VehicleWorld() const { return vehicleWorld; }
    const std::vector<VehicleState>& Vehicles() const { return vehicles; }
    std::vector<VehicleState>& MutableVehicles() { return vehicles; }
    VehicleState* FindVehicle(int index) { return index >= 0 && index < static_cast<int>(vehicles.size()) ? &vehicles[static_cast<size_t>(index)] : nullptr; }
    const VehicleState* FindVehicle(int index) const { return index >= 0 && index < static_cast<int>(vehicles.size()) ? &vehicles[static_cast<size_t>(index)] : nullptr; }
    // Which cart and seat a player is in. False if they are on foot.
    bool RidingIn(uint32_t id, int* index = nullptr, Seat* seat = nullptr) const {
        if (id == kNoPlayer) return false;
        for (const auto& v : vehicles) {
            for (int s = 0; s < 2; s++) {
                if (v.seat[s] != id) continue;
                if (index) *index = v.index;
                if (seat) *seat = static_cast<Seat>(s);
                return true;
            }
        }
        return false;
    }

    // Park the carts: on fairly flat open ground with room round them, apart from each other, the given spots first.
    void SpawnVehicles() {
        vehicles.clear();
        if (vehicleCount <= 0) return;
        Rng rng(seed ^ 0x63617274ull);   // "cart"
        std::vector<Vec2> spots = vehicleSpots;
        for (size_t i = spots.size(); i > 1; i--) std::swap(spots[i - 1], spots[rng.Below(static_cast<uint32_t>(i))]);
        auto place = [&](Vec2 at) {
            if (!Inside(at)) return false;
            for (const auto& v : vehicles) if (Distance({v.body.x, v.body.z}, at) < 650.0f) return false;
            for (int t = 0; t < 4; t++) {
                const float yaw = static_cast<float>(rng.Unit() * 6.283185307179586 - 3.141592653589793);
                const cartdetail::Footing f = cartdetail::FootingAt(vehicleWorld, at.x, at.z, yaw);
                if (!f.any || std::fabs(f.pitch) > 0.2f || std::fabs(f.roll) > 0.2f || !cartdetail::Fits(vehicleWorld, at.x, at.z, yaw, f.y)) continue;
                VehicleState v;
                v.index = static_cast<uint8_t>(vehicles.size());
                v.body.x = at.x; v.body.z = at.z; v.body.yaw = yaw;
                SettleCart(v.body, vehicleWorld);
                vehicles.push_back(v);
                return true;
            }
            return false;
        };
        for (Vec2 at : spots) { if (static_cast<int>(vehicles.size()) >= vehicleCount) break; place(at); }
        for (int tries = 0; tries < 400 && static_cast<int>(vehicles.size()) < vehicleCount; tries++) place(RandomPointIn(rng, map, placement, 0.85f));
    }

    // Get in. The wanted seat if it is free, else the other one. You must be on foot, standing by the cart, and it must not be speeding past.
    bool EnterVehicle(uint32_t playerId, int index, Seat want) {
        PlayerState* p = Find(playerId);
        VehicleState* v = FindVehicle(index);
        if (!p || !v || !p->alive || v->wrecked || v->gone || state != MatchState::InMatch || Stunned(*p) || RidingIn(playerId)) return false;
        if (std::fabs(v->body.speed) > 200.0f) return false;
        const Seat order[2] = {want == Seat::Passenger ? Seat::Passenger : Seat::Driver, want == Seat::Passenger ? Seat::Driver : Seat::Passenger};
        for (Seat s : order) {
            uint32_t& who = v->seat[static_cast<int>(s)];
            if (who != kNoPlayer) continue;
            if (Distance(p->pos, ExitSpot(v->body, s)) > kEnterRange && Distance(p->pos, {v->body.x, v->body.z}) > kEnterRange) continue;
            who = playerId;
            if (s == Seat::Driver) { v->lastDriver = playerId; v->reportAt = clock; v->controls = {}; }
            PlaceRider(*v, *p, s);
            return true;
        }
        return false;
    }
    // Move to the cart's other seat (the passenger takes the reins when the driver has got out).
    bool SwitchSeat(uint32_t playerId) {
        int index; Seat s;
        if (!RidingIn(playerId, &index, &s)) return false;
        VehicleState& v = vehicles[static_cast<size_t>(index)];
        const int other = s == Seat::Driver ? 1 : 0;
        if (v.seat[other] != kNoPlayer || v.wrecked) return false;
        v.seat[other] = playerId;
        v.seat[static_cast<int>(s)] = kNoPlayer;
        if (other == 0) { v.lastDriver = playerId; v.reportAt = clock; v.controls = {}; }
        if (PlayerState* p = Find(playerId)) PlaceRider(v, *p, static_cast<Seat>(other));
        return true;
    }
    // Get out, onto the ground beside your seat.
    bool ExitVehicle(uint32_t playerId) {
        int index; Seat s;
        if (!RidingIn(playerId, &index, &s)) return false;
        VehicleState& v = vehicles[static_cast<size_t>(index)];
        v.seat[static_cast<int>(s)] = kNoPlayer;
        if (PlayerState* p = Find(playerId)) {
            Vec2 to = ExitSpot(v.body, s);
            if (!OpenGround(to)) { const Vec2 other = ExitSpot(v.body, s == Seat::Driver ? Seat::Passenger : Seat::Driver); to = OpenGround(other) ? other : Vec2{v.body.x, v.body.z}; }
            p->pos = to;
            p->y = p->isBot ? 0.0f : v.body.y;
        }
        return true;
    }

    // A human driver's game reports where the cart is (it runs the physics against the real ground). The move is checked like a player's.
    bool DriveVehicle(uint32_t playerId, int index, const CartBody& reported, float air, bool drift, float impact, float landing) {
        VehicleState* v = FindVehicle(index);
        if (!v || v->wrecked || v->gone || v->Driver() != playerId || state != MatchState::InMatch) return false;
        if (!std::isfinite(reported.x) || !std::isfinite(reported.z) || !std::isfinite(reported.y)) return false;
        const float elapsed = (std::min)(0.5f, (std::max)(clock - v->reportAt, 0.05f));
        const float maxMove = kCartMaxSpeed * 1.5f * elapsed + 60.0f;
        CartBody b = reported;
        const float d = std::hypot(b.x - v->body.x, b.z - v->body.z);
        if (d > maxMove) { const float k = maxMove / d; b.x = v->body.x + (b.x - v->body.x) * k; b.z = v->body.z + (b.z - v->body.z) * k; }
        b.speed = std::clamp(b.speed, -kCartMaxReverse * 1.5f, kCartMaxSpeed * 1.4f);
        v->body = b;
        v->air = (std::max)(0.0f, air);
        v->drift = drift;
        v->reportAt = clock;
        CartStep st;
        st.impact = (std::min)(impact, 900.0f);
        st.landing = (std::min)(landing, 3000.0f);
        ApplyCartStep(*v, st);
        return true;
    }

    // Hit a cart with your weapon. Its riders are not hurt (unless it is a blast).
    AttackResult AttackVehicle(uint32_t attackerId, uint32_t vehicleId, bool hit) {
        AttackResult r;
        PlayerState* a = Find(attackerId);
        VehicleState* v = FindVehicle(static_cast<int>(vehicleId - kVehicleIdBase));
        if (!a || !a->alive || !v || v->wrecked || v->gone || v->seat[0] == attackerId || v->seat[1] == attackerId) return r;
        const bool hadAmmo = HasAmmo(*a, a->weapon.item);
        const WeaponStats w = ActiveWeapon(a->weapon.item, hadAmmo);
        const Vec2 at = {v->body.x, v->body.z};
        if (w.damage <= 0 || clock < a->attackReadyAt || Stunned(*a)) return r;
        if (Distance(a->pos, at) > w.range * 1.1f + kCartHitRadius) return r;
        a->attackReadyAt = clock + w.cooldown;
        r.ok = true;
        if (hadAmmo) SpendAmmo(*a, a->weapon.item);
        if (!hit) return r;
        const float base = w.damage * static_cast<float>(Pellets(w, Distance(a->pos, at))) * (hadAmmo ? RarityScale(a->weapon.rarity) : 1.0f) *
                           (w.ranged ? TotalsOf(*a).ranged : TotalsOf(*a).melee) * (clock < a->adultUntil ? kAdultDamage : 1.0f);
        const float dmg = base * (w.splashRadius > 0 ? 1.5f : 1.0f) * (a->weapon.item == ItemId::MegatonHammer || a->weapon.item == ItemId::GiantsHammer ? 1.6f : 1.0f);
        r.hit = true;
        r.damage = dmg;
        MatchEvent e{MatchEvent::Type::Damaged};
        e.a = v->Id(); e.b = attackerId; e.amount = dmg; e.health = (std::max)(0.0f, v->health - dmg);
        events.push_back(e);
        if (w.splashRadius > 0) {
            for (auto& o : players) {
                if (o.alive && o.id != attackerId && Distance(o.pos, at) <= w.splashRadius) Damage(o.id, base * 0.6f, attackerId, DamageKind::Explosion);
            }
        }
        DamageVehicle(*v, dmg, attackerId);
        r.killed = v->wrecked;
        return r;
    }
    void DamageVehicle(VehicleState& v, float amount, uint32_t by) {
        if (v.wrecked || v.gone || !(amount > 0)) return;
        v.health -= amount;
        if (by != kNoPlayer) v.lastHitBy = by;
        if (v.health <= 0) WreckVehicle(v);
    }
    // Wrecked: the riders are thrown off, the firebox goes up, and what is left burns for a while.
    void WreckVehicle(VehicleState& v) {
        v.health = 0;
        v.wrecked = true;
        v.wreckedAt = clock;
        v.body.speed *= 0.3f;
        for (int s = 0; s < 2; s++) if (v.seat[s] != kNoPlayer) ExitVehicle(v.seat[s]);
        AddStrike({v.body.x, v.body.z}, kCartBlastRadius, kCartBlastDamage, 0.0f, v.lastHitBy, StrikeStyle::Fire);
    }

    void TickVehicles(float dt) {
        for (auto& v : vehicles) {
            if (v.gone) continue;
            if (v.Occupied() || v.wrecked || !v.body.grounded || std::fabs(v.body.speed) > 1.0f) v.busyAt = clock;
            if (v.wrecked) {
                if (clock - v.wreckedAt > kCartWreckSeconds) v.gone = true;
                else if (!v.body.grounded || std::fabs(v.body.speed) > 1.0f) StepCart(v.body, CartControls{}, vehicleWorld, dt);   // it rolls to a stop
                continue;
            }
            for (auto& who : v.seat) { const PlayerState* r = Find(who); if (who != kNoPlayer && (!r || !r->alive)) who = kNoPlayer; }
            const PlayerState* driver = Find(v.Driver());
            const bool human = driver && !driver->isBot && clock - v.reportAt < 1.0f;   // its driver's game moves it
            if (!human) {
                const CartControls c = driver && driver->isBot ? v.controls : CartControls{};
                const bool still = v.body.grounded && std::fabs(v.body.speed) < 1.0f && std::fabs(v.body.slide) < 1.0f && c.throttle == 0.0f;
                if (!still) ApplyCartStep(v, StepCart(v.body, c, vehicleWorld, dt));
                v.drift = c.handbrake;
                v.air = 0.0f;
                if (!v.body.grounded) { const cartdetail::Footing f = cartdetail::FootingAt(vehicleWorld, v.body.x, v.body.z, v.body.yaw); if (f.any) v.air = (std::max)(0.0f, v.body.y - f.y); }
            }
            if (v.wrecked) continue;
            for (int s = 0; s < 2; s++) if (PlayerState* r = Find(v.seat[s])) PlaceRider(v, *r, static_cast<Seat>(s));
            RamPlayers(v);
        }
        BumpCarts();
    }

  private:
    // A rider sits on the saddle: the server's idea of where they are follows the cart (bots' heights are above the floor, people's are world heights).
    void PlaceRider(const VehicleState& v, PlayerState& p, Seat s) {
        float x, y, z;
        SeatSpot(v.body, s, &x, &y, &z);
        p.pos = {x, z};
        p.y = p.isBot ? (std::max)(0.0f, y - v.body.y + v.air) : y;
        p.rot = YawToBinang(v.body.yaw);
    }
    // Crash and landing damage from a step of the physics, to the cart and (for the hard ones) the riders.
    void ApplyCartStep(VehicleState& v, const CartStep& st) {
        const float cartHurt = CrashDamage(st.impact) + LandingDamage(st.landing);
        const float riderHurt = RiderCrashDamage(st.impact) + LandingDamage(st.landing) * 0.25f;
        if (riderHurt > 0.0f) for (uint32_t id : v.seat) if (id != kNoPlayer) Damage(id, riderHurt, kNoPlayer, DamageKind::Normal, false);
        if (cartHurt > 0.0f) DamageVehicle(v, cartHurt, kNoPlayer);
    }
    // Anyone on foot in the way of a moving cart is run over: hurt by how fast it was going, credited to whoever is (or last was) driving it.
    void RamPlayers(VehicleState& v) {
        const float speed = std::hypot(v.body.speed, v.body.slide);
        if (speed < kRamMinSpeed) return;
        const uint32_t by = v.Driver() != kNoPlayer ? v.Driver() : v.lastDriver;
        for (auto& p : players) {
            if (!p.alive || p.id == v.seat[0] || p.id == v.seat[1] || RidingIn(p.id) || !InsideCart(v.body, p.pos.x, p.pos.z, 14.0f)) continue;
            if (p.isBot ? p.y > 70.0f : std::fabs(p.y - v.body.y) > 90.0f) continue;   // up on a block or a roof: it passes underneath
            if (!v.CanRam(p.id, clock)) continue;
            v.NoteRam(p.id, clock);
            Damage(p.id, RamDamage(speed), by == p.id ? kNoPlayer : by, DamageKind::Normal);
            DamageVehicle(v, kRamCartDamage, kNoPlayer);
            v.body.speed *= 0.82f;
            if (p.isBot && p.alive) {   // knocked aside, out of its path
                const float lx = std::cos(v.body.yaw), lz = -std::sin(v.body.yaw);
                const float side = (p.pos.x - v.body.x) * lx + (p.pos.z - v.body.z) * lz >= 0 ? 1.0f : -1.0f;
                const Vec2 to = {p.pos.x + lx * side * 80.0f, p.pos.z + lz * side * 80.0f};
                if (OpenGround(to)) p.pos = to;
            }
            if (v.wrecked) return;
        }
    }
    // Carts that run into each other: pushed apart, both hurt by how fast they met. A cart its driver's game moves isn't pushed by the server
    // (that game treats the others as walls), but it is still hurt.
    void BumpCarts() {
        for (size_t i = 0; i < vehicles.size(); i++) {
            for (size_t j = i + 1; j < vehicles.size(); j++) {
                VehicleState& a = vehicles[i];
                VehicleState& b = vehicles[j];
                if (a.gone || b.gone) continue;
                const float dx = b.body.x - a.body.x, dz = b.body.z - a.body.z, d = std::hypot(dx, dz);
                const float reach = (cart::kMaxZ - cart::kMinZ) * 0.5f + 18.0f;
                if (d >= reach || d < 1e-3f) continue;
                const float nx = dx / d, nz = dz / d;
                const float va = (std::sin(a.body.yaw) * a.body.speed) * nx + (std::cos(a.body.yaw) * a.body.speed) * nz;
                const float vb = (std::sin(b.body.yaw) * b.body.speed) * nx + (std::cos(b.body.yaw) * b.body.speed) * nz;
                const float closing = va - vb;
                if (closing > 40.0f && !a.wrecked && !b.wrecked) {
                    const float hurt = CrashDamage(closing) * 0.6f;
                    DamageVehicle(a, hurt, b.Driver());
                    DamageVehicle(b, hurt, a.Driver());
                    a.body.speed *= 0.4f; b.body.speed *= 0.4f;
                }
                const auto movedByServer = [&](const VehicleState& v) { const PlayerState* dr = Find(v.Driver()); return !(dr && !dr->isBot && clock - v.reportAt < 1.0f); };
                const float push = reach - d;
                const bool ma = movedByServer(a), mb = movedByServer(b);
                const float sa = ma && mb ? 0.5f : (ma ? 1.0f : 0.0f), sb = ma && mb ? 0.5f : (mb ? 1.0f : 0.0f);
                a.body.x -= nx * push * sa; a.body.z -= nz * push * sa;
                b.body.x += nx * push * sb; b.body.z += nz * push * sb;
            }
        }
    }

  public:
    // Use the ability slot. Fails (and costs nothing) if there is no ability, it is still recharging, the player is stunned, or the
    // ability needs a target that isn't there (Hookshot with nobody in front). Farore's Wind marks a spot the first time and jumps back
    // to it the second time.
    // Magic: the stored value plus what has refilled since it was stored.
    float MagicNow(const PlayerState& p) const { return (std::min)(kMaxMagic, p.magic + (std::max)(0.0f, clock - p.magicStamp) * kMagicRegenPerSec); }
    void AddMagic(PlayerState& p, float amount) { p.magic = (std::max)(0.0f, (std::min)(kMaxMagic, MagicNow(p) + amount)); p.magicStamp = clock; p.dirty = true; }

    bool UseAbility(uint32_t id) {
        PlayerState* p = Find(id);
        if (!p || !p->alive || state != MatchState::InMatch || !p->hasAbility) return false;
        if (clock < p->abilityReadyAt || clock < p->stunUntil || clock < p->frozenUntil) return false;
        const ItemId item = p->ability.item;
        const float cost = AbilityMagic(item);
        if (MagicNow(*p) + 0.001f < cost) return false;   // not enough magic
        bool startsCooldown = true;
        if (!RunAbility(*p, item, p->ability.rarity, &startsCooldown)) return false;
        if (startsCooldown) { p->abilityReadyAt = clock + AbilityOf(item).cooldown; AddMagic(*p, -cost); }
        p->dirty = true;
        MatchEvent e{MatchEvent::Type::AbilityUsed};
        e.a = id; e.item = static_cast<uint8_t>(item); e.x = p->pos.x; e.z = p->pos.z;
        events.push_back(e);
        return true;
    }

    float SpeedMultiplier(const PlayerState& p) const {
        return TotalsOf(p).speed * (clock < p.speedUntil ? p.speedMult : 1.0f) * (clock < p.adultUntil ? kAdultSpeed : 1.0f);
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
            if (mapId == kConvergenceMapIndex) {
                for (const auto& region : convergence::kRegions)
                    if (region.boss >= 0 && Distance(at,{region.x,region.z}) < 1.0f) b.kind = static_cast<BossKind>(region.boss);
            }
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
        const bool hadAmmo = HasAmmo(*a, a->weapon.item);
        const WeaponStats w = ActiveWeapon(a->weapon.item, hadAmmo);
        if (w.damage <= 0 || clock < a->attackReadyAt || clock < a->stunUntil || clock < a->frozenUntil) return r;
        const bool dragon = IsDragonKind(b->kind);
        const bool airborne = dragon && b->y > kDragonAirborneAbove;
        if (BossHidden(b->mode)) return r;                                             // underground, under water, in shadow: nothing reaches it
        if (airborne && !w.ranged) return r;                                           // up in the sky: only arrows and the like reach it
        if (Distance(a->pos, b->pos) > w.range * 1.1f + (dragon ? kDragonBodyRadius : kBossBodyRadius)) return r; // it is big: you can hit it from further off
        a->attackReadyAt = clock + w.cooldown;
        r.ok = true;
        if (hadAmmo) SpendAmmo(*a, a->weapon.item);
        if (!hit) return r;
        const GearTotals ag = TotalsOf(*a);
        r.damage = w.damage * static_cast<float>(Pellets(w, Distance(a->pos, b->pos))) * (hadAmmo ? RarityScale(a->weapon.rarity) : 1.0f) * (w.ranged ? ag.ranged : ag.melee) * (clock < a->adultUntil ? kAdultDamage : 1.0f);
        if (BossDazed(b->mode)) r.damage *= kBossStunnedTakes;                        // down or dazed: it takes extra
        else if (airborne) r.damage *= 0.75f;
        r.hit = true;
        const float dealt = (std::min)(r.damage, b->health);
        b->health -= r.damage;
        a->damageDealt += dealt;
        b->target = attackerId; // whoever hurts it is who it comes for
        b->lostTargetAt = clock;
        if (b->windupUntil >= 0.0f && r.damage >= b->maxHealth * kBossInterruptShare && !IsDragonKind(b->kind)) {   // a heavy blow knocks a mini boss out of its wind-up
            b->windupUntil = -1.0f;
            b->attackReadyAt = (std::max)(b->attackReadyAt, clock + 0.8f);
        }
        MatchEvent e{MatchEvent::Type::Damaged};
        e.a = bossId; e.b = attackerId; e.amount = r.damage; e.health = (std::max)(0.0f, b->health);
        events.push_back(e);
        if (b->health <= 0 && !StalfosGetsUp(*b)) {
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

    // Supply drops: announced a few seconds ahead somewhere in the zone that will still be safe, then a crate of guaranteed Legendary loot (and a
    // second chest and a pile of rupees) lands there. Not in the last phases, where there is no room left to race for it.
    void SetSupplyDrops(bool on) { supplyDrops = on; }
    void TickSupplyDrops() {
        if (!supplyDrops) return;
        float total = 0;
        for (const auto& ph : kStormPhases) total += ph.waitSec + ph.closeSec;
        if (stormTime >= nextSupplyAt && stormTime < total * 0.75f) {
            nextSupplyAt += kSupplyEverySec;
            Rng rng(seed ^ (static_cast<uint64_t>(supplyCount++) * 0x9E3779B97F4A7C15ull) ^ 0x737570ull); // "sup"
            const Circle zone = storm.SafeZoneAt(stormTime + kSupplyWarningSec + 10.0f);
            Vec2 at = RandomPointIn(rng, zone, placement, 0.8f);
            pendingSupply.push_back({at, clock + kSupplyWarningSec});
            MatchEvent e{MatchEvent::Type::SupplyDrop};
            e.x = at.x; e.z = at.z; e.health = kSupplyWarningSec;
            events.push_back(e);
        }
        for (size_t i = 0; i < pendingSupply.size();) {
            if (clock < pendingSupply[i].landAt) { i++; continue; }
            const Vec2 at = pendingSupply[i].pos;
            Rng rng(seed ^ (static_cast<uint64_t>(i + supplyCount) * 0xD1B54A32D192ED03ull) ^ 0x6372617465ull); // "crate"
            LootSpawn crate = SiteChest(rng, at, 3);
            crate.supply = true;
            AddLoot(crate);
            LootSpawn second = SiteChest(rng, {at.x + 70.0f, at.z + 40.0f}, 2);
            second.supply = true;
            AddLoot(second);
            LootSpawn money = {{at.x - 70.0f, at.z + 40.0f}, ItemId::Rupees, Rarity::Common, false, false};
            money.amount = 60;
            money.supply = true;
            AddLoot(money);
            pendingSupply.erase(pendingSupply.begin() + static_cast<long>(i));
        }
    }

    // Weather: see shared/weather.h. The sky changes spell by spell as the match goes on; thunderstorms throw lightning at the players.
    void SetWeatherOptions(const WeatherOptions& o) { wopt = o; }
    const WeatherOptions& GetWeatherOptions() const { return wopt; }
    const Weather& CurrentWeather() const { return weather; }
    void TickWeather() {
        const int sp = SpellIndex(wopt, stormTime);
        if (sp != spell && !(sandbox && !sandboxWeatherFree)) {
            spell = sp;
            weather = WeatherForSpell(wopt, seed, mapId, sp);
            MatchEvent e{MatchEvent::Type::Weather};
            e.a = static_cast<uint32_t>(weather.season); e.item = static_cast<uint8_t>(weather.sky); e.amount = static_cast<float>(weather.intensity);
            e.health = SpellSeconds(wopt);
            events.push_back(e);
            nextBolt = clock + 4.0f;
        }
        const float every = LightningEvery(weather);
        if (state != MatchState::InMatch || every <= 0.0f || clock < nextBolt) return;
        Rng rng(seed ^ (static_cast<uint64_t>(++boltCount) * 0xA24BAED4963EE407ull) ^ 0x626F6C74ull);   // "bolt"
        nextBolt = clock + every * (0.6f + 0.8f * static_cast<float>(rng.Unit()));
        std::vector<const PlayerState*> alive;
        for (const auto& p : players) if (p.alive) alive.push_back(&p);
        if (alive.empty()) return;
        const PlayerState* victim = alive[rng.Below(static_cast<uint32_t>(alive.size()))];
        const float ang = static_cast<float>(rng.Unit()) * 6.2831853f, off = static_cast<float>(rng.Unit()) * 320.0f;
        Strike s;
        s.at = {victim->pos.x + std::cos(ang) * off, victim->pos.z + std::sin(ang) * off};
        s.radius = kLightningRadius; s.damage = kLightningDamage; s.hitAt = clock + kLightningWarning; s.by = kNoPlayer; s.lightning = true; s.style = StrikeStyle::Bolt;
        strikes.push_back(s);
        MatchEvent e{MatchEvent::Type::Strike};
        e.a = kNoPlayer; e.x = s.at.x; e.z = s.at.z; e.amount = s.radius; e.health = kLightningWarning; e.item = static_cast<uint8_t>(StrikeStyle::Bolt);
        events.push_back(e);
    }

    void SetMajorBoss(bool on) { majorBoss = on; }
    bool MajorBossEnabled() const { return majorBoss; }
    const std::vector<Strike>& Strikes() const { return strikes; }

    // ---- Lilo's toxic cloud
    struct FartCloud { Vec2 at; float until; uint32_t by; };
    const std::vector<FartCloud>& FartClouds() const { return fartClouds; }
    // A player's Lilo made a cloud at `at`. Only while the match is on, from a living player standing near it, at most one every few seconds.
    bool StartFartCloud(uint32_t playerId, Vec2 at) {
        const PlayerState* p = Find(playerId);
        if (!p || !p->alive || state != MatchState::InMatch || Distance(p->pos, at) > kFartCloudReach) return false;
        if (fartClouds.size() >= 24) return false;
        auto last = lastFartCloud.find(playerId);
        if (last != lastFartCloud.end() && clock - last->second < kFartCloudCooldown) return false;
        lastFartCloud[playerId] = clock;
        fartClouds.push_back({at, clock + kFartCloudSeconds, playerId});
        return true;
    }
    static bool InFartCloud(const std::vector<FartCloud>& clouds, Vec2 p, float now) {
        for (const FartCloud& c : clouds) if (now < c.until && Distance(p, c.at) < kFartCloudRadius) return true;
        return false;
    }
    void TickGas(PlayerState& p, float dt) {
        fartClouds.erase(std::remove_if(fartClouds.begin(), fartClouds.end(), [&](const FartCloud& c) { return clock >= c.until; }), fartClouds.end());
        if (InFartCloud(fartClouds, p.pos, clock)) p.gasTime += dt;
        else p.gasTime = (std::max)(0.0f, p.gasTime - dt * 2.0f);
        if (p.gasTime > kFartGraceSeconds) Damage(p.id, kFartDps * dt, kNoPlayer, DamageKind::Gas, false);
    }
    // The walkability grid the host's game measured (the same one the bots use). Bosses path around walls, water and cliffs with it, and use
    // their own way across when there is no path. Without one (the tests, a plain match) they walk straight.
    void SetNav(std::shared_ptr<const NavGrid> grid) { nav = std::move(grid); }

    // The major boss arrives halfway through the storm timeline, somewhere inside the safe zone, and everybody is told.
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
        d.y = BossOf(d.kind).altitude;
        d.waypoint = at;
        d.swoopReadyAt = clock + 8.0f;
        d.specialReadyAt = clock + 20.0f;   // it shows itself off in the air before its first big move
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

    // What a boss's blasts do when nothing more particular is asked: Volvagia's burn, the others' hold you for a moment.
    StrikeStyle StyleOf(uint32_t by) const {
        const MiniBoss* src = FindBoss(by);
        if (!src) return StrikeStyle::Fire;
        switch (src->kind) {
            case BossKind::DragonWater: case BossKind::Tide: return StrikeStyle::Water;
            case BossKind::DragonForest: return StrikeStyle::Magic;
            case BossKind::DragonShadow: case BossKind::Shade: return StrikeStyle::Shadow;
            case BossKind::Frost: return StrikeStyle::Ice;
            case BossKind::Moss: return StrikeStyle::Spore;
            case BossKind::Stone: case BossKind::Dune: return StrikeStyle::Rock;
            default: return StrikeStyle::Fire;
        }
    }

    void AddStrike(Vec2 at, float radius, float damage, float delay, uint32_t by) { AddStrike(at, radius, damage, delay, by, StyleOf(by)); }
    void AddStrike(Vec2 at, float radius, float damage, float delay, uint32_t by, StrikeStyle style) {
        Strike s;
        s.at = at; s.radius = radius; s.damage = damage; s.hitAt = clock + delay; s.by = by; s.style = style;
        strikes.push_back(s);
        MatchEvent e{MatchEvent::Type::Strike};
        e.a = by; e.x = at.x; e.z = at.z; e.amount = radius; e.health = delay; e.item = static_cast<uint8_t>(style);
        events.push_back(e);
    }

    // What a blast does to whoever it catches, on top of the damage.
    void ApplyStyle(PlayerState& p, const Strike& s) {
        auto hold = [&](float seconds) {
            if (TotalsOf(p).stunImmune) return;
            p.stunUntil = (std::max)(p.stunUntil, clock + seconds);
            p.dirty = true;
        };
        switch (s.style) {
            case StrikeStyle::Fire: ApplyBurn(p, s.by, 3.0f, 0.3f); break;
            case StrikeStyle::Ice: if (!TotalsOf(p).stunImmune) { p.frozenUntil = (std::max)(p.frozenUntil, clock + 1.2f); p.dirty = true; } break;
            case StrikeStyle::Water: hold(0.6f); break;
            case StrikeStyle::Shadow: hold(0.95f); break;
            case StrikeStyle::Magic: hold(0.5f); break;
            default: break;
        }
    }

    void ApplyBurn(PlayerState& p, uint32_t by, float seconds, float dps) {
        seconds *= BurnMult(weather);   // rain puts fires out, ash storms feed them
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
                if (clock < p.rollUntil && !s.lightning && s.style != StrikeStyle::Bolt && s.by != kNoPlayer) continue;   // a well-timed roll goes through a marked blast
                if (s.lightning || s.style == StrikeStyle::Bolt) { Damage(p.id, s.damage, s.by, DamageKind::Normal); continue; }
                const DamageKind kind = s.style == StrikeStyle::Fire ? DamageKind::Fire : (s.style == StrikeStyle::Rock ? DamageKind::Explosion : DamageKind::Normal);
                Damage(p.id, s.damage, s.by, kind);
                if (p.alive) ApplyStyle(p, s);
            }
            for (auto& v : vehicles) {   // blasts break carts too (a wreck's own blast can set off the next one)
                if (v.wrecked || v.gone || Distance({v.body.x, v.body.z}, s.at) > s.radius + kCartHitRadius * 0.5f) continue;
                DamageVehicle(v, s.damage * (s.style == StrikeStyle::Bolt ? 1.0f : 1.6f), s.by);
            }
        }
        strikes.erase(std::remove_if(strikes.begin(), strikes.end(), [&](const Strike& s) { return s.applied && clock > s.hitAt + 1.0f; }), strikes.end());
    }

    // ---- how bosses move ----------------------------------------------------------------------------------------------------------

    static int16_t FaceRot(Vec2 from, Vec2 to) { return static_cast<int16_t>(static_cast<int32_t>(std::atan2(to.x - from.x, to.z - from.z) * (32768.0f / 3.14159265358979f))); }
    // How far p is from straight ahead of the boss, in radians (0 = dead ahead).
    static float OffFacing(const MiniBoss& b, Vec2 p) {
        float off = std::atan2(p.x - b.pos.x, p.z - b.pos.z) - static_cast<float>(b.rot) * (3.14159265f / 32768.0f);
        while (off > 3.14159265f) off -= 6.2831853f;
        while (off < -3.14159265f) off += 6.2831853f;
        return off;
    }
    static Vec2 Ahead(const MiniBoss& b, float dist, float turn = 0.0f) {
        const float a = static_cast<float>(b.rot) * (3.14159265f / 32768.0f) + turn;
        return {b.pos.x + std::sin(a) * dist, b.pos.z + std::cos(a) * dist};
    }
    static bool Step(Vec2& pos, Vec2 to, float dist) {
        const float dx = to.x - pos.x, dz = to.z - pos.z, d = std::hypot(dx, dz);
        if (d <= dist || d < 0.001f) { pos = to; return true; }
        pos.x += dx / d * dist; pos.z += dz / d * dist;
        return false;
    }
    bool Inside(Vec2 p) const { return Distance(p, map.center) <= map.radius && (!placement || placement(p)); }
    Vec2 SnapToGround(Vec2 p) const {
        Vec2 out = p;
        if (nav && nav->Snap(p, &out)) return out;
        return p;
    }

    // Walks a boss toward `goal` at `speed`: straight when the way is clear, along a path around walls when it is not, and with its own trick
    // (a leap, a climb, a swim, a burrow, a roll) when there is no path at all.
    void BossWalk(MiniBoss& b, Vec2 goal, float speed, float dt) {
        const BossDef def = BossOf(b.kind);
        Vec2 next = goal;
        float pace = speed;
        b.aux = 0;
        if (nav && Distance(b.pos, goal) > NavGrid::kCell) {
            const bool onGround = nav->Walkable(b.pos);
            Vec2 from = b.pos;
            if (!onGround) nav->Snap(b.pos, &from);   // just off the edge of the grid (a corner, a landing): reckon from the nearest open cell
            if (!onGround && def.traverse == Traverse::Climb) { b.mode = DragonMode::Climb; pace = speed * 0.75f; }   // up the wall, straight on
            else if (!onGround && def.traverse == Traverse::Swim) { b.aux = 1; pace = speed * 0.85f; }                 // through the water, straight on
            else if (!nav->LineClear(from, goal)) {
                if (clock >= b.repathAt || Distance(b.pathGoal, goal) > 150.0f) {
                    b.repathAt = clock + 0.7f;
                    b.pathGoal = goal;
                    b.pathIdx = 0;
                    if (!nav->FindPath(b.pos, goal, b.path)) b.path.clear();
                }
                while (b.pathIdx < b.path.size() && Distance(b.pos, b.path[b.pathIdx]) < NavGrid::kCell * 0.6f) b.pathIdx++;
                if (b.pathIdx < b.path.size()) next = b.path[b.pathIdx];
                else if (CrossGap(b, goal)) return;
            }
        }
        if (b.mode == DragonMode::Climb && (!nav || nav->Walkable(b.pos))) b.mode = DragonMode::Chase;
        b.rot = FaceRot(b.pos, next);
        Step(b.pos, next, pace * dt);
    }

    // No path to where it wants to be: its own way across. Returns true if it started something (a leap, a burrow, a roll).
    bool CrossGap(MiniBoss& b, Vec2 goal) {
        const BossDef def = BossOf(b.kind);
        const float d = Distance(b.pos, goal);
        switch (def.traverse) {
            case Traverse::Leap:
                if (d > 750.0f) return false;
                StartLeap(b, SnapToGround(goal), false);
                return true;
            case Traverse::Burrow:
                StartBurrow(b, SnapToGround(goal), 0.6f + d / (def.speed * 2.2f));
                return true;
            case Traverse::Roll:
                if (d > 1000.0f) return false;
                StartCharge(b, goal, 430.0f, DragonMode::Stunned, 1.2f);
                return true;
            case Traverse::Climb:
                b.mode = DragonMode::Climb;   // over the wall, straight at it
                return false;
            default:
                return false;   // swimmers just go straight
        }
    }

    void StartLeap(MiniBoss& b, Vec2 to, bool attack) {
        const float d = Distance(b.pos, to);
        b.mode = DragonMode::Leap;
        b.from = b.pos; b.to = to;
        b.moveStart = clock;
        b.modeUntil = clock + 0.45f + d / 900.0f;
        b.aux = attack ? 1 : 0;
        b.rot = FaceRot(b.pos, to);
        b.next = DragonMode::Chase;
        b.lastSmashAt = clock;
    }
    void StartBurrow(MiniBoss& b, Vec2 to, float seconds) {
        b.mode = DragonMode::Hidden;
        b.from = b.pos; b.to = to;
        b.moveStart = clock;
        b.modeUntil = clock + seconds;
    }
    void StartCharge(MiniBoss& b, Vec2 to, float speed, DragonMode after, float afterSeconds) {
        b.mode = DragonMode::Charge;
        b.from = b.pos; b.to = to;
        b.moveStart = clock;
        b.chargeSpeed = speed;
        b.modeUntil = clock + Distance(b.pos, to) / speed + 0.2f;
        b.rot = FaceRot(b.pos, to);
        b.next = after; b.nextSeconds = afterSeconds;
        b.hitThisMove.clear();
        b.lastSmashAt = clock;
    }
    // A boss doing something that takes a while and then moves on to `after` (for `afterSeconds`, when that is a daze).
    void StartMove(MiniBoss& b, DragonMode mode, float seconds, DragonMode after = DragonMode::Chase, float afterSeconds = 0.0f, uint8_t aux = 0) {
        b.mode = mode;
        b.moveStart = clock;
        b.modeUntil = clock + seconds;
        b.next = after; b.nextSeconds = afterSeconds;
        b.aux = aux;
        b.lastSmashAt = clock;
    }
    void FinishMove(MiniBoss& b) {
        const DragonMode after = b.next;
        b.next = DragonMode::Chase;
        b.mode = after;
        b.moveStart = clock;
        b.modeUntil = clock + b.nextSeconds;
        b.nextSeconds = 0.0f;
        if (after == DragonMode::Chase || after == DragonMode::Climb) b.aux = 0;
    }
    // Everyone within `radius` of the boss gets hit once per move (a roll, a spin, a charge).
    void BodyHits(MiniBoss& b, float radius, float damage, float stun) {
        for (auto& p : players) {
            if (!p.alive || clock < p.invulnUntil || Distance(p.pos, b.pos) > radius) continue;
            if (std::find(b.hitThisMove.begin(), b.hitThisMove.end(), p.id) != b.hitThisMove.end()) continue;
            b.hitThisMove.push_back(p.id);
            if (!Blow(p, b.pos, damage, b.id)) continue;                  // rolled clean through it
            if (p.alive && stun > 0 && !TotalsOf(p).stunImmune) { p.stunUntil = (std::max)(p.stunUntil, clock + stun); p.dirty = true; }
        }
    }
    // Damage to everyone in a cone in front of the boss, every tick (breath).
    void ConeHits(MiniBoss& b, float range, float halfAngle, float dps, float dt, bool burn) {
        for (auto& p : players) {
            if (!p.alive || clock < p.invulnUntil) continue;
            if (Distance(p.pos, b.pos) > range || std::fabs(OffFacing(b, p.pos)) > halfAngle) continue;
            Damage(p.id, dps * dt, b.id, burn ? DamageKind::Fire : DamageKind::Normal, false);
            if (p.alive && burn) ApplyBurn(p, b.id, 2.5f, 0.3f);
        }
    }

    // ---- mini bosses ------------------------------------------------------------------------------------------------------------

    // The Iron Knuckle's armour comes off at half health.
    static bool ArmourOff(const MiniBoss& b) { return b.kind == BossKind::Dune && b.health < b.maxHealth * 0.5f; }
    // A blow that would finish the Stalfos the first time only knocks it to pieces: it lies there a moment and pulls itself back together.
    // Returns true if that is what happened (and it is not dead).
    bool StalfosGetsUp(MiniBoss& b) {
        if (b.kind != BossKind::Stone || b.reassembled || b.health > 0) return false;
        b.reassembled = true;
        b.health = b.maxHealth * kStalfosGetsUpWith;
        b.windupUntil = -1.0f;
        StartMove(b, DragonMode::Stunned, 2.2f, DragonMode::Chase, 0.0f, 3);   // aux 3: in pieces on the ground
        b.y = 0;
        return true;
    }

    // Moves that take a while: carried on every tick until they finish. Returns false when the boss is free to choose again.
    bool TickMiniMove(MiniBoss& b, float dt) {
        const BossDef def = BossOf(b.kind);
        switch (b.mode) {
            case DragonMode::Leap: {
                const float span = (std::max)(0.05f, b.modeUntil - b.moveStart);
                const float t = (std::min)(1.0f, (clock - b.moveStart) / span);
                b.pos = {b.from.x + (b.to.x - b.from.x) * t, b.from.z + (b.to.z - b.from.z) * t};
                b.y = std::sin(t * 3.14159265f) * (110.0f + Distance(b.from, b.to) * 0.18f);
                if (t >= 1.0f) { b.y = 0; b.aux = 0; FinishMove(b); }
                return true;
            }
            case DragonMode::Charge: {
                const Vec2 was = b.pos;
                const bool done = Step(b.pos, b.to, b.chargeSpeed * dt);
                BodyHits(b, 95.0f * def.scale, def.damage, b.kind == BossKind::Tide ? 0.7f : 0.4f);
                if (b.kind == BossKind::Lava) {   // the Magma Dodongo's roll leaves a trail of fire
                    b.trail += Distance(was, b.pos);
                    if (b.trail > 110.0f) { b.trail = 0.0f; AddStrike(was, 75.0f, def.damage * 0.35f, 0.35f, b.id, StrikeStyle::Fire); }
                }
                if (done || clock >= b.modeUntil) FinishMove(b);
                return true;
            }
            case DragonMode::Hidden: {
                const float left = (std::max)(0.05f, b.modeUntil - clock);
                Step(b.pos, b.to, Distance(b.pos, b.to) / left * dt);
                if (clock >= b.modeUntil) {
                    b.pos = b.to;
                    StartMove(b, DragonMode::Emerge, 0.6f);
                    AddStrike(b.pos, 105.0f, def.damage, 0.5f, b.id, StrikeStyle::Shadow);   // it bursts up out of the ground under you
                }
                return true;
            }
            case DragonMode::Breath:
                ConeHits(b, 400.0f, 0.55f, def.damage * 0.55f, dt, true);
                b.lastSmashAt = clock;
                if (clock >= b.modeUntil) FinishMove(b);
                return true;
            case DragonMode::Slam: case DragonMode::Summon: case DragonMode::Emerge: case DragonMode::Stunned:
                if (clock >= b.modeUntil) FinishMove(b);
                return true;
            default:
                return false;
        }
    }

    // Each mini boss's own move, from what it does in the game. Returns true if it started one.
    bool StartMiniSpecial(MiniBoss& b, const PlayerState& target, float d) {
        const BossDef def = BossOf(b.kind);
        Rng rng(seed ^ (static_cast<uint64_t>(clock * 977.0f) * 0x9E3779B97F4A7C15ull) ^ b.id);
        const Vec2 at = target.pos;
        b.rot = FaceRot(b.pos, at);
        bool started = false;
        float busy = 0.0f;
        switch (b.kind) {
            case BossKind::Stone:   // Stalfos: a jump slash, landing where you stand
                if (d > 170.0f && d < 520.0f) {
                    StartLeap(b, SnapToGround(at), true);
                    AddStrike(b.to, 110.0f, def.damage, b.modeUntil - clock, b.id, StrikeStyle::Rock);
                    busy = b.modeUntil - clock; started = true;
                }
                break;
            case BossKind::Lava:    // Magma Dodongo: fire breath up close, a roll from further off that leaves fire behind (and dizzy after)
                if (d < 220.0f) { StartMove(b, DragonMode::Breath, 1.6f, DragonMode::Chase); busy = 1.6f; started = true; }
                else if (d < 950.0f) {
                    Vec2 to = {b.pos.x + (at.x - b.pos.x) / d * (d + 260.0f), b.pos.z + (at.z - b.pos.z) / d * (d + 260.0f)};
                    if (!Inside(to)) to = at;
                    StartCharge(b, to, 430.0f, DragonMode::Stunned, 1.6f);
                    busy = b.modeUntil - clock + 1.6f; started = true;
                }
                break;
            case BossKind::Frost:   // White Wolfos: an icy howl that freezes everyone close, or a pounce
                if (d < 250.0f) {
                    StartMove(b, DragonMode::Summon, 0.75f);
                    AddStrike(b.pos, 240.0f, def.damage * 0.7f, 0.7f, b.id, StrikeStyle::Ice);
                    busy = 0.75f; started = true;
                } else if (d < 480.0f) {
                    StartLeap(b, SnapToGround(at), true);
                    AddStrike(b.to, 95.0f, def.damage, b.modeUntil - clock, b.id, StrikeStyle::Ice);
                    busy = b.modeUntil - clock; started = true;
                }
                break;
            case BossKind::Moss:    // Moss Lizalfos: lobs spore pods that burst around you, then hops in for a slash
                if (d < 650.0f) {
                    StartMove(b, DragonMode::Summon, 1.0f, DragonMode::Chase);
                    for (int i = 0; i < 3; i++) {
                        const float a = static_cast<float>(rng.Unit()) * 6.2831853f, r = i == 0 ? 0.0f : 90.0f + static_cast<float>(rng.Unit()) * 90.0f;
                        AddStrike({at.x + std::cos(a) * r, at.z + std::sin(a) * r}, 95.0f, def.damage * 0.8f, 1.4f + 0.3f * i, b.id, StrikeStyle::Spore);
                    }
                    busy = 1.0f; started = true;
                }
                break;
            case BossKind::Tide:    // Big Octo: a spinning charge through you, dizzy after
                if (d > 150.0f && d < 800.0f) {
                    Vec2 to = {b.pos.x + (at.x - b.pos.x) / d * (d + 200.0f), b.pos.z + (at.z - b.pos.z) / d * (d + 200.0f)};
                    if (!Inside(to)) to = at;
                    StartCharge(b, to, 380.0f, DragonMode::Stunned, 1.2f);
                    b.aux = 2;
                    busy = b.modeUntil - clock + 1.2f; started = true;
                }
                break;
            case BossKind::Shade:   // Dead Hand: hands grab you out of the ground, then it burrows over to bite
                if (d < 600.0f) {
                    StartMove(b, DragonMode::Summon, 1.0f, DragonMode::Hidden, 0.9f);
                    AddStrike(at, 80.0f, def.damage * 0.6f, 1.0f, b.id, StrikeStyle::Shadow);
                    for (int i = 0; i < 3; i++) {
                        const float a = 2.0943951f * i + static_cast<float>(rng.Unit());
                        AddStrike({at.x + std::cos(a) * 140.0f, at.z + std::sin(a) * 140.0f}, 75.0f, def.damage * 0.6f, 1.0f, b.id, StrikeStyle::Shadow);
                    }
                    b.from = b.pos; b.to = SnapToGround({at.x + (b.pos.x - at.x) / (std::max)(d, 1.0f) * 70.0f, at.z + (b.pos.z - at.z) / (std::max)(d, 1.0f) * 70.0f});
                    b.moveStart = clock;
                    busy = 2.6f; started = true;
                }
                break;
            case BossKind::Dune:    // Iron Knuckle: an overhead cleave that sends a shockwave along the sand; the axe sticks (not once the armour is off)
                if (d < 260.0f) {
                    StartMove(b, DragonMode::Slam, ArmourOff(b) ? 0.7f : 1.0f, ArmourOff(b) ? DragonMode::Chase : DragonMode::Stunned, 1.3f);
                    AddStrike(Ahead(b, 120.0f), 140.0f, def.damage * 1.5f, 1.0f, b.id, StrikeStyle::Rock);
                    AddStrike(Ahead(b, 270.0f), 105.0f, def.damage * 0.7f, 1.2f, b.id, StrikeStyle::Rock);
                    AddStrike(Ahead(b, 410.0f), 105.0f, def.damage * 0.7f, 1.4f, b.id, StrikeStyle::Rock);
                    busy = 2.3f; started = true;
                }
                break;
            default: break;
        }
        if (started) {
            b.specialReadyAt = clock + busy + 7.0f + static_cast<float>(rng.Unit()) * 3.0f;   // a breather after each trick
            b.attackReadyAt = (std::max)(b.attackReadyAt, clock + busy + def.cooldown * 0.5f);
            b.moves++;
        }
        return started;
    }

    void TickMini(MiniBoss& b, float dt) {
        const BossDef def = BossOf(b.kind);
        if (TickMiniMove(b, dt)) return;
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
            if (target) b.specialReadyAt = (std::max)(b.specialReadyAt, clock + 1.5f);   // it closes in a little before its first trick
        }
        if (target) { b.target = target->id; b.lostTargetAt = clock; }
        else if (clock - b.lostTargetAt > 4.0f) b.target = kNoPlayer;

        if (b.windupUntil >= 0.0f) {   // winding up a blow: feet planted, then it strikes everyone still in front of it
            if (clock >= b.windupUntil) {
                b.windupUntil = -1.0f;
                for (auto& p : players) {
                    if (!p.alive || clock < p.invulnUntil) continue;
                    if (Distance(p.pos, b.pos) > kBossReach + 40.0f || std::fabs(OffFacing(b, p.pos)) > 1.25f) continue;
                    Blow(p, b.pos, def.damage, b.id);
                }
            }
            return;
        }
        if (target && Distance(b.pos, b.home) <= kBossLeash + 200.0f) {
            const float d = Distance(b.pos, target->pos);
            if (b.mode == DragonMode::Patrol) b.mode = DragonMode::Chase;
            if (clock >= b.specialReadyAt && clock >= b.attackReadyAt && StartMiniSpecial(b, *target, d)) return;
            if (d > kBossReach * 0.75f) BossWalk(b, target->pos, def.speed * (ArmourOff(b) ? kIronKnuckleBareSpeed : 1.0f), dt);
            b.rot = FaceRot(b.pos, target->pos);
            if (d <= kBossReach + 20.0f && clock >= b.attackReadyAt) {
                b.attackReadyAt = clock + def.cooldown * (ArmourOff(b) ? kIronKnuckleBareSwing : 1.0f);
                b.lastSmashAt = clock;
                b.windupUntil = clock + kBossWindupSeconds;   // it rears back first: that half second is the player's chance to roll away
            }
        } else {
            // Lost them (or they ran too far): walk home and recover.
            b.target = kNoPlayer;
            if (Distance(b.pos, b.home) > 8.0f) BossWalk(b, b.home, def.speed * 0.8f, dt);
            if (b.mode != DragonMode::Climb) b.mode = DragonMode::Patrol;
            b.health = (std::min)(b.maxHealth, b.health + 1.2f * dt);
        }
    }

    // ---- the major bosses ---------------------------------------------------------------------------------------------------------

    void TickDragon(MiniBoss& b, float dt) {
        const BossDef def = BossOf(b.kind);
        const float cruise = def.altitude;
        auto face = [&](Vec2 to) { b.rot = FaceRot(b.pos, to); };
        auto fly = [&](Vec2 to, float speed) {
            const float d = Distance(b.pos, to);
            Step(b.pos, to, speed * dt);
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
        Rng rng(seed ^ static_cast<uint64_t>(clock * 977.0f) ^ b.id);

        switch (b.mode) {
            case DragonMode::Landed:
                climbTo(0.0f, 300.0f);
                if (clock >= b.modeUntil) b.mode = DragonMode::Climb;
                return;
            case DragonMode::Stunned:   // dazed where it is (Morpha's core lies exposed on the ground)
                climbTo(b.kind == BossKind::DragonWater ? 40.0f : 0.0f, 300.0f);
                if (clock >= b.modeUntil) { b.mode = b.kind == BossKind::DragonWater ? DragonMode::Hidden : DragonMode::Climb; b.modeUntil = clock + 1.5f; b.from = b.to = b.pos; b.moveStart = clock; }
                return;
            case DragonMode::Climb:
                climbTo(cruise, 160.0f);
                if (std::fabs(b.y - cruise) < 5.0f) b.mode = DragonMode::Chase;
                return;
            case DragonMode::Swoop: {   // a dive at someone (Volvagia's swoop, Bongo Bongo's head charge, Twinrova's broom dive)
                const float t = (std::min)(1.0f, 1.0f - (b.modeUntil - clock) / kDragonStrikeDelay);
                b.y = cruise * (1.0f - t);
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
                    if (Distance(p.pos, b.pos) > kDragonBreathRange || std::fabs(OffFacing(b, p.pos)) > kDragonBreathHalfAngle) continue;
                    Damage(p.id, kDragonBreathDps * dt, b.id, DamageKind::Fire, false);
                    if (p.alive) ApplyBurn(p, b.id, 2.5f, 0.3f);
                }
                if (clock >= b.modeUntil) b.mode = DragonMode::Chase;
                return;
            case DragonMode::Cast: case DragonMode::Slam: case DragonMode::Beam: case DragonMode::Summon:
                if (target) face(target->pos);
                if (clock >= b.modeUntil) FinishMove(b);
                return;
            case DragonMode::Hidden: {   // underground, under water, in shadow or in a portal, on its way somewhere
                climbTo(HiddenAltitude(b.kind), 600.0f);
                if (b.kind == BossKind::DragonWater) {   // Morpha follows you about under the water and comes up when it is ready
                    if (target) b.to = MorphaSurfaceSpot(b, target->pos);
                    const bool there = Step(b.pos, b.to, def.speed * dt);
                    if (there && target && clock >= b.attackReadyAt) {
                        b.attackReadyAt = clock + def.cooldown + 0.8f + 1.5f + 3.0f;
                        Emerge(b, target, rng);
                    } else if (there && !target) {
                        b.to = PatrolSpot();
                    }
                    return;
                }
                const float left = (std::max)(0.05f, b.modeUntil - clock);
                Step(b.pos, b.to, (std::max)(def.speed * 0.6f, Distance(b.pos, b.to) / left) * dt);
                if (clock >= b.modeUntil) Emerge(b, target, rng);
                return;
            }
            case DragonMode::Emerge:
                climbTo(EmergeAltitude(b.kind), 500.0f);
                if (clock >= b.modeUntil) FinishMove(b);
                return;
            case DragonMode::Charge: {   // Phantom Ganon's charge out of a portal, Twinrova's broom dash
                climbTo(b.kind == BossKind::DragonForest ? 100.0f : cruise, 400.0f);
                const bool done = Step(b.pos, b.to, b.chargeSpeed * dt);
                face(b.to);
                if (b.kind == BossKind::DragonForest) BodyHits(b, 140.0f, 1.3f, 0.5f);
                if (done || clock >= b.modeUntil) FinishMove(b);
                return;
            }
            default: break;
        }

        // Morpha lives under the water: when it is not doing something it is sliding about out of sight.
        if (b.kind == BossKind::DragonWater) {
            b.mode = DragonMode::Hidden;
            b.from = b.pos;
            b.moveStart = clock;
            b.to = target ? MorphaSurfaceSpot(b, target->pos) : PatrolSpot();
            return;
        }

        climbTo(cruise + 30.0f * std::sin(clock * 1.6f), 120.0f);
        if (!target) { // nobody near: cruise between spots in the safe zone
            b.mode = DragonMode::Patrol;
            if (fly(b.waypoint, 110.0f) < 90.0f) b.waypoint = PatrolSpot();
            face(b.waypoint);
            return;
        }
        b.mode = DragonMode::Chase;
        const float d = Distance(b.pos, target->pos);
        face(target->pos);
        // Getting to you: each one its own way.
        if (d > 850.0f && clock >= b.attackReadyAt) {
            const float a = static_cast<float>(rng.Unit()) * 6.2831853f;
            const Vec2 near = SnapToGround({target->pos.x + std::cos(a) * 380.0f, target->pos.z + std::sin(a) * 380.0f});
            if (b.kind == BossKind::DragonForest || b.kind == BossKind::DragonShadow) {   // through a portal / into the shadows, and out beside you
                StartBurrow(b, Inside(near) ? near : target->pos, 1.1f);
                b.next = DragonMode::Chase;
                return;
            }
            if (b.kind == BossKind::DragonSand) {   // a broom dash
                StartCharge(b, Inside(near) ? near : target->pos, def.speed * 3.0f, DragonMode::Chase, 0.0f);
                b.aux = 2;
                return;
            }
        }
        if (d > 480.0f) fly(target->pos, def.speed);              // close in to about 480 units and hover
        if (clock < b.attackReadyAt) return;
        switch (b.kind) {
            case BossKind::DragonForest: PhantomGanonAttack(b, *target, d, hpFrac, rng); break;
            case BossKind::DragonShadow: BongoAttack(b, *target, d, hpFrac, rng); break;
            case BossKind::DragonSand: TwinrovaAttack(b, *target, d, hpFrac, rng); break;
            default: VolvagiaAttack(b, *target, d, hpFrac, rng); break;
        }
    }

    static float HiddenAltitude(BossKind k) { return k == BossKind::DragonForest || k == BossKind::DragonShadow ? BossOf(k).altitude : 0.0f; }
    static float EmergeAltitude(BossKind k) { return k == BossKind::DragonWater ? BossOf(k).altitude : (k == BossKind::DragonFire ? 60.0f : BossOf(k).altitude); }
    Vec2 PatrolSpot() {
        Rng rng(seed ^ static_cast<uint64_t>(clock * 1000.0f) ^ 0x70617472ull);
        return RandomPointIn(rng, storm.SafeZoneAt(stormTime), placement, 0.8f);
    }
    // Where Morpha's tentacle comes up: beside you, on its side.
    Vec2 MorphaSurfaceSpot(const MiniBoss& b, Vec2 at) const {
        const float d = (std::max)(1.0f, Distance(b.pos, at));
        Vec2 p = {at.x + (b.pos.x - at.x) / d * 230.0f, at.z + (b.pos.z - at.z) / d * 230.0f};
        return Inside(p) ? p : at;
    }

    // It comes back out at the end of a hidden trip.
    void Emerge(MiniBoss& b, PlayerState* target, Rng& rng) {
        b.pos = b.to;
        switch (b.kind) {
            case BossKind::DragonFire:   // Volvagia bursts out of the ground, rocks come down, and it lies there for a moment
                StartMove(b, DragonMode::Emerge, 0.7f, DragonMode::Landed, 3.0f);
                for (int i = 0; i < 6; i++) {
                    const float a = static_cast<float>(rng.Unit()) * 6.2831853f, r = 160.0f + static_cast<float>(rng.Unit()) * 380.0f;
                    AddStrike({b.pos.x + std::cos(a) * r, b.pos.z + std::sin(a) * r}, 95.0f, 0.7f, 1.0f + 0.25f * i, b.id, StrikeStyle::Rock);
                }
                break;
            case BossKind::DragonWater: {   // Morpha's tentacle rises, swings at you, and (when hurt) throws up geysers; then the core is out
                StartMove(b, DragonMode::Emerge, 0.8f, DragonMode::Slam, 1.5f);
                AddStrike(b.pos, 120.0f, 1.0f, 0.7f, b.id, StrikeStyle::Water);
                if (target) {
                    b.rot = FaceRot(b.pos, target->pos);
                    for (int i = 0; i < 3; i++) AddStrike(Ahead(b, 180.0f + 110.0f * i, (i - 1) * 0.35f), 100.0f, 1.0f, 1.6f + 0.15f * i, b.id, StrikeStyle::Water);
                    if (b.health < b.maxHealth * 0.5f) {
                        for (int i = 0; i < 5; i++) {
                            const float a = static_cast<float>(rng.Unit()) * 6.2831853f, r = 60.0f + static_cast<float>(rng.Unit()) * 320.0f;
                            AddStrike({target->pos.x + std::cos(a) * r, target->pos.z + std::sin(a) * r}, 90.0f, 0.8f, 1.9f + 0.2f * i, b.id, StrikeStyle::Water);
                        }
                    }
                }
                b.chainAfterSlam = true;
                break;
            }
            default:   // Phantom Ganon out of his portal (into his charge), Bongo Bongo out of the shadows
                StartMove(b, DragonMode::Emerge, 0.5f, b.next == DragonMode::Charge ? DragonMode::Charge : DragonMode::Chase);
                break;
        }
    }

    void VolvagiaAttack(MiniBoss& b, PlayerState& target, float d, float hpFrac, Rng& rng) {
        const BossDef def = BossOf(b.kind);
        if (clock >= b.specialReadyAt && rng.Unit() < 0.35) {      // down into the ground, and up under you
            StartBurrow(b, SnapToGround(target.pos), 2.0f);
            b.specialReadyAt = clock + 18.0f;
            b.attackReadyAt = clock + 2.0f + 0.7f + 3.0f + def.cooldown;
            AddStrike(b.to, 170.0f, 1.4f, 2.0f, b.id, StrikeStyle::Fire);
            return;
        }
        if (clock >= b.swoopReadyAt && rng.Unit() < 0.4) {         // dive at them
            b.mode = DragonMode::Swoop;
            b.modeUntil = clock + kDragonStrikeDelay;
            b.swoopAt = target.pos;
            b.swoopReadyAt = clock + 15.0f;
            b.attackReadyAt = clock + 2.0f;
            AddStrike(b.swoopAt, 140.0f, 1.6f, kDragonStrikeDelay, b.id);
        } else if (d < kDragonBreathRange * 0.9f) {                // close: breathe fire
            b.mode = DragonMode::Breath;
            b.modeUntil = clock + kDragonBreathSeconds;
            b.attackReadyAt = clock + def.cooldown + kDragonBreathSeconds;
        } else {                                                    // far: fireballs, and meteors when it is hurt
            StartMove(b, DragonMode::Cast, 1.0f);
            b.attackReadyAt = clock + def.cooldown + 1.0f;
            AddStrike(target.pos, kDragonStrikeRadius, kDragonStrikeDamage, kDragonStrikeDelay, b.id);
            const int extra = hpFrac < 0.5f ? 6 : 2;
            for (int i = 0; i < extra; i++) {
                const float a = static_cast<float>(rng.Unit() * 6.2831853), dist = 80.0f + static_cast<float>(rng.Unit()) * (hpFrac < 0.5f ? 520.0f : 260.0f);
                AddStrike({target.pos.x + std::cos(a) * dist, target.pos.z + std::sin(a) * dist}, kDragonStrikeRadius * 0.8f, kDragonStrikeDamage * 0.8f, kDragonStrikeDelay + 0.1f * i, b.id);
            }
        }
    }

    void PhantomGanonAttack(MiniBoss& b, PlayerState& target, float d, float hpFrac, Rng& rng) {
        const BossDef def = BossOf(b.kind);
        (void)d;
        if (clock >= b.specialReadyAt) {   // into a portal, out of another one across from you, and a charge straight through
            const float a = static_cast<float>(rng.Unit()) * 6.2831853f;
            Vec2 start = {target.pos.x + std::cos(a) * 750.0f, target.pos.z + std::sin(a) * 750.0f};
            if (!Inside(start)) start = {target.pos.x - std::cos(a) * 750.0f, target.pos.z - std::sin(a) * 750.0f};
            if (!Inside(start)) start = b.pos;
            StartBurrow(b, start, 1.0f);
            b.next = DragonMode::Charge;
            b.chargeThrough = target.pos;
            b.specialReadyAt = clock + 16.0f;
            b.attackReadyAt = clock + 4.5f + def.cooldown;
            return;
        }
        if ((b.moves++ & 1) == 0) {   // a volley of energy balls, one after the other
            StartMove(b, DragonMode::Beam, 1.4f, DragonMode::Chase, 0.0f, 0);
            const int balls = hpFrac < 0.5f ? 5 : 3;
            for (int i = 0; i < balls; i++) {
                const float a = static_cast<float>(rng.Unit()) * 6.2831853f, r = i == 0 ? 0.0f : static_cast<float>(rng.Unit()) * 160.0f;
                AddStrike({target.pos.x + std::cos(a) * r, target.pos.z + std::sin(a) * r}, 110.0f, 0.8f, 1.0f + 0.32f * i, b.id, StrikeStyle::Magic);
            }
        } else {                      // the spear raised: lightning comes down around you
            StartMove(b, DragonMode::Cast, 1.2f, DragonMode::Chase, 0.0f, 1);
            for (int i = 0; i < 4; i++) {
                const float a = 1.5707963f * i + static_cast<float>(rng.Unit()) * 0.8f, r = i == 0 ? 0.0f : 150.0f + static_cast<float>(rng.Unit()) * 120.0f;
                AddStrike({target.pos.x + std::cos(a) * r, target.pos.z + std::sin(a) * r}, 120.0f, 1.0f, 1.2f + 0.12f * i, b.id, StrikeStyle::Bolt);
            }
        }
        b.attackReadyAt = clock + def.cooldown + 1.4f;
    }

    void BongoAttack(MiniBoss& b, PlayerState& target, float d, float hpFrac, Rng& rng) {
        const BossDef def = BossOf(b.kind);
        if (clock >= b.swoopReadyAt && rng.Unit() < 0.3) {   // the head charges down at you, and lies there with its eye open
            b.mode = DragonMode::Swoop;
            b.modeUntil = clock + kDragonStrikeDelay;
            b.swoopAt = target.pos;
            b.swoopReadyAt = clock + 16.0f;
            b.attackReadyAt = clock + 2.0f;
            AddStrike(b.swoopAt, 150.0f, 1.6f, kDragonStrikeDelay, b.id, StrikeStyle::Shadow);
            return;
        }
        const int pick = static_cast<int>(rng.Below(hpFrac < 0.5f ? 4 : 3));
        if (pick < 2) {   // one hand comes down on you
            StartMove(b, DragonMode::Slam, 1.1f, DragonMode::Chase, 0.0f, static_cast<uint8_t>(b.moves++ & 1));
            AddStrike(target.pos, 140.0f, 1.2f, 1.0f, b.id, StrikeStyle::Shadow);
        } else if (pick == 2) {   // both hands together: a clap
            StartMove(b, DragonMode::Slam, 1.4f, DragonMode::Chase, 0.0f, 2);
            AddStrike(target.pos, 210.0f, 1.8f, 1.3f, b.id, StrikeStyle::Shadow);
        } else {   // a drum beat: shockwaves go out around it
            StartMove(b, DragonMode::Summon, 1.8f);
            for (int i = 0; i < 8; i++) {
                const float a = 0.785398f * i, r = 220.0f + 60.0f * (i & 1);
                AddStrike({b.pos.x + std::cos(a) * r, b.pos.z + std::sin(a) * r}, 120.0f, 0.7f, 0.6f + 0.15f * i, b.id, StrikeStyle::Rock);
            }
            AddStrike(target.pos, 120.0f, 0.7f, 1.8f, b.id, StrikeStyle::Rock);
        }
        (void)d;
        b.attackReadyAt = clock + def.cooldown + 1.2f;
    }

    void TwinrovaAttack(MiniBoss& b, PlayerState& target, float d, float hpFrac, Rng& rng) {
        const BossDef def = BossOf(b.kind);
        if (clock >= b.swoopReadyAt && rng.Unit() < 0.3) {   // a dive on her brooms
            b.mode = DragonMode::Swoop;
            b.modeUntil = clock + kDragonStrikeDelay;
            b.swoopAt = target.pos;
            b.swoopReadyAt = clock + 15.0f;
            b.attackReadyAt = clock + 2.0f;
            AddStrike(b.swoopAt, 140.0f, 1.6f, kDragonStrikeDelay, b.id, (b.moves & 1) ? StrikeStyle::Ice : StrikeStyle::Fire);
            return;
        }
        if (hpFrac < 0.5f && clock >= b.specialReadyAt) {   // both at once: a ring of fire and ice around you
            StartMove(b, DragonMode::Summon, 1.6f, DragonMode::Chase, 0.0f, 3);
            for (int i = 0; i < 8; i++) {
                const float a = 0.785398f * i;
                AddStrike({target.pos.x + std::cos(a) * 280.0f, target.pos.z + std::sin(a) * 280.0f}, 120.0f, 0.9f, 1.3f, b.id, (i & 1) ? StrikeStyle::Ice : StrikeStyle::Fire);
            }
            AddStrike(target.pos, 130.0f, 1.0f, 1.7f, b.id, rng.Unit() < 0.5 ? StrikeStyle::Ice : StrikeStyle::Fire);
            b.specialReadyAt = clock + 14.0f;
            b.attackReadyAt = clock + def.cooldown + 1.6f;
            return;
        }
        // Koume's fire and Kotake's ice, in turn: a beam that scorches (or freezes) a line along the ground through you.
        const bool ice = (b.moves++ & 1) != 0;
        StartMove(b, DragonMode::Beam, 1.3f, DragonMode::Chase, 0.0f, ice ? 1 : 0);
        const float len = (std::max)(d, 1.0f);
        const float dx = (target.pos.x - b.pos.x) / len, dz = (target.pos.z - b.pos.z) / len;
        for (int i = 0; i < 6; i++) {
            const float along = len - 240.0f + 120.0f * i;
            const Vec2 at = {b.pos.x + dx * along, b.pos.z + dz * along};
            AddStrike(at, 100.0f, 0.8f, 0.9f + 0.08f * i, b.id, ice ? StrikeStyle::Ice : StrikeStyle::Fire);
        }
        b.attackReadyAt = clock + def.cooldown + 1.3f;
    }

    void TickBosses(float dt) {
        MaybeSpawnDragon();
        TickStrikes();
        for (auto& b : bosses) {
            if (!b.alive) continue;
            if (IsDragonKind(b.kind)) {
                const DragonMode before = b.mode;
                TickDragon(b, dt);
                // Phantom Ganon came out of his portal: now the charge through where you stood.
                if (b.kind == BossKind::DragonForest && before == DragonMode::Emerge && b.mode == DragonMode::Charge && b.moveStart == clock) {
                    const float d = (std::max)(1.0f, Distance(b.pos, b.chargeThrough));
                    Vec2 to = {b.chargeThrough.x + (b.chargeThrough.x - b.pos.x) / d * 500.0f, b.chargeThrough.z + (b.chargeThrough.z - b.pos.z) / d * 500.0f};
                    if (!Inside(to)) to = b.chargeThrough;
                    StartCharge(b, to, 650.0f, DragonMode::Climb, 0.0f);
                }
                // Morpha's swing is over: the core drops out, exposed.
                if (b.kind == BossKind::DragonWater && before == DragonMode::Slam && b.mode != DragonMode::Slam && b.chainAfterSlam) {
                    b.chainAfterSlam = false;
                    b.mode = DragonMode::Stunned;
                    b.modeUntil = clock + 3.0f;
                }
                continue;
            }
            TickMini(b, dt);
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

    // ---- the Sandbox test map (shared/sandbox_terrain.h) -------------------------------------------------------------------------------------
    // A match with no countdown, no drop, no end and a frozen storm, so every feature can be tried alone. Everything below only works in one (the host's
    // game sends these as buttons). The switches default to the quiet setting: you cannot be hurt, the storm waits, the weather stays where you set it.
    bool SetSandbox(bool on) { if (state != MatchState::Lobby) return false; sandbox = on; return true; }
    bool Sandbox() const { return sandbox; }
    void SetSandboxSpawn(Vec2 at) { sandboxSpawn = at; }
    void SetSandboxLootRoom(Circle room) { sandboxRoom = room; }
    void SandboxGod(bool on) { sandboxGod = on; }
    void SandboxStormRuns(bool on) { sandboxStormRuns = on; }
    void SandboxWeatherFree(bool on) { sandboxWeatherFree = on; }
    bool SandboxGodOn() const { return sandboxGod; }
    bool SandboxStormOn() const { return sandboxStormRuns; }
    bool SandboxWeatherIsFree() const { return sandboxWeatherFree; }
    bool SandboxLive() const { return sandbox && state == MatchState::InMatch; }

    // Seconds into the storm timeline at which a phase begins (kStormPhaseCount: when it is all over).
    static float StormPhaseStart(int phase) {
        float t = 0;
        for (int i = 0; i < phase && i < kStormPhaseCount; i++) t += kStormPhases[static_cast<size_t>(i)].waitSec + kStormPhases[static_cast<size_t>(i)].closeSec;
        return t;
    }
    // Jump the storm to the start of a phase (the circles are the real ones; run it with SandboxStormRuns).
    bool SandboxStormPhase(int phase) {
        if (!SandboxLive() || phase < 0 || phase > kStormPhaseCount) return false;
        stormTime = StormPhaseStart(phase);
        return true;
    }
    // Set the sky and keep it there (unless SandboxWeatherFree lets the schedule take over again).
    void SandboxWeather(Season season, Sky sky, int intensity) {
        weather = Weather{season, sky, static_cast<uint8_t>((std::max)(0, (std::min)(100, intensity)))};
        MatchEvent e{MatchEvent::Type::Weather};
        e.a = static_cast<uint32_t>(weather.season); e.item = static_cast<uint8_t>(weather.sky); e.amount = static_cast<float>(weather.intensity);
        e.health = SpellSeconds(wopt);
        events.push_back(e);
        nextBolt = clock + 4.0f;
    }
    // A supply drop at a spot (it is announced and lands as in a real match).
    bool SandboxSupplyDrop(Vec2 at) {
        if (!SandboxLive()) return false;
        pendingSupply.push_back({at, clock + kSupplyWarningSec});
        MatchEvent e{MatchEvent::Type::SupplyDrop};
        e.x = at.x; e.z = at.z; e.health = kSupplyWarningSec;
        events.push_back(e);
        return true;
    }
    bool SandboxTeleport(uint32_t id, Vec2 to) {
        PlayerState* p = Find(id);
        if (!SandboxLive() || !p || !p->alive) return false;
        Teleport(*p, to);
        return true;
    }
    void SandboxHeal(uint32_t id) {
        PlayerState* p = Find(id);
        if (!SandboxLive() || !p || !p->alive) return;
        p->health = p->maxHealth;
        p->armor = kMaxShield;
        p->magic = kMaxMagic; p->magicStamp = clock;
        p->abilityReadyAt = 0;
        for (int k = 0; k < kAmmoKinds; k++) p->ammo[static_cast<size_t>(k)] = (std::max)(p->ammo[static_cast<size_t>(k)], AmmoCapOf(*p, static_cast<AmmoKind>(k)));
        Cleanse(*p);
    }
    bool SandboxRevive(uint32_t id) {
        PlayerState* p = Find(id);
        if (!SandboxLive() || !p || p->alive) return false;
        p->alive = true; p->health = p->maxHealth; p->placement = 0;
        p->invulnUntil = clock + 2.0f;
        p->dirty = true;
        MatchEvent e{MatchEvent::Type::Revived};
        e.a = p->id;
        events.push_back(e);
        return true;
    }
    // Put an item straight into a player's hands or bag, as if they had found it (at the rarity asked for, within what the item can be).
    bool SandboxGive(uint32_t id, ItemId item, Rarity rarity) {
        PlayerState* p = Find(id);
        if (!SandboxLive() || !p || !p->alive || static_cast<int>(item) >= kItemCount) return false;
        rarity = (std::max)(DefOf(item).minRarity, (std::min)(DefOf(item).maxRarity, rarity));
        LootSpawn l = {p->pos, item, rarity, false, false};
        if (item == ItemId::Rupees) l.amount = 100;
        else if (item >= ItemId::ArrowAmmo) l.amount = 30;
        const size_t index = AddLoot(l);
        return PickUp(id, index, true);
    }
    // A cart standing at a spot (a gone one's place is used again).
    bool SandboxCart(Vec2 at, float yaw) {
        if (!SandboxLive()) return false;
        size_t slot = vehicles.size();
        for (size_t i = 0; i < vehicles.size(); i++) if (vehicles[i].gone) { slot = i; break; }
        if (slot >= static_cast<size_t>(kMaxVehicles)) return false;
        VehicleState v;
        v.index = static_cast<uint8_t>(slot);
        v.body.x = at.x; v.body.z = at.z; v.body.yaw = yaw;
        SettleCart(v.body, vehicleWorld);
        v.busyAt = clock;
        if (slot == vehicles.size()) vehicles.push_back(v); else vehicles[slot] = v;
        return true;
    }
    void SandboxClearCarts() { for (auto& v : vehicles) { v.gone = true; for (auto& who : v.seat) who = kNoPlayer; } }
    // A bot standing at a spot (they play as in a match unless the bot brain is frozen: BotController::SetFrozen).
    bool SandboxBot(Vec2 at) {
        if (!SandboxLive() || static_cast<int>(players.size()) >= kMaxPlayers) return false;
        uint32_t id = 1000;
        for (const auto& p : players) if (p.isBot) id = (std::max)(id, p.id + 1);
        PlayerState bot = MakePlayer(id, true);
        bot.scene = static_cast<uint8_t>(MapOf(mapId).scene);
        bot.pos = at;
        players.push_back(bot);
        return true;
    }
    void SandboxClearBots() { for (auto& p : players) if (p.isBot && p.alive) { p.alive = false; p.health = 0; } }
    // A boss at a spot: a mini boss takes a free place among the first seven, a major boss is the dragon's one place (a second one replaces the first).
    bool SandboxBoss(BossKind kind, Vec2 at) {
        if (!SandboxLive()) return false;
        const bool major = IsDragonKind(kind);
        uint32_t id = kDragonId;
        if (!major) {
            id = 0;
            for (int i = 0; i < kMaxBosses - 1 && id == 0; i++) {
                bool used = false;
                for (const auto& b : bosses) used = used || (b.id == kBossIdBase + static_cast<uint32_t>(i) && b.alive);
                if (!used) id = kBossIdBase + static_cast<uint32_t>(i);
            }
            if (id == 0) return false;
        }
        bosses.erase(std::remove_if(bosses.begin(), bosses.end(), [&](const MiniBoss& b) { return b.id == id; }), bosses.end());
        MiniBoss b;
        b.id = id; b.kind = kind;
        b.home = b.pos = at;
        b.maxHealth = b.health = BossOf(kind).health;
        if (major) {
            b.y = BossOf(kind).altitude;
            b.waypoint = at;
            b.swoopReadyAt = clock + 8.0f;
            b.specialReadyAt = clock + 6.0f;
        }
        bosses.push_back(b);
        MatchEvent e{MatchEvent::Type::BossSpawned};
        e.a = b.id; e.x = at.x; e.z = at.z;
        events.push_back(e);
        return true;
    }
    void SandboxClearBosses() { for (auto& b : bosses) { b.alive = false; b.health = 0; } }
    // The loot plaza: every item in the game lying in rows, and chests along the back. Called when the map is built.
    void SandboxStockLoot() {
        loot.clear();
        sandboxRestocked.clear();
        const Vec2 c = sandboxRoom.center;
        const int cols = 11;
        const float spacing = 100.0f;
        int n = 0;
        for (int i = 0; i < kItemCount; i++) {
            const ItemId item = static_cast<ItemId>(i);
            if (item == ItemId::BasicSword) continue;
            LootSpawn l = {{c.x - (cols - 1) * 0.5f * spacing + static_cast<float>(n % cols) * spacing, c.z - 500.0f + static_cast<float>(n / cols) * spacing},
                           item, DefOf(item).maxRarity, false, false};
            if (item == ItemId::Rupees) l.amount = 100;
            else if (item >= ItemId::ArrowAmmo) l.amount = 30;
            loot.push_back({l, false});
            n++;
        }
        Rng rng(seed ^ 0x73626F78ull);   // "sbox"
        static const Rarity tiers[5] = {Rarity::Common, Rarity::Uncommon, Rarity::Rare, Rarity::Epic, Rarity::Legendary};
        for (int k = 0; k < 5; k++) {   // one chest of each rarity
            ItemId item;
            if (!PickItem(rng, tiers[k], &item)) item = ItemId::MasterSword;
            Rarity t = (std::max)(DefOf(item).minRarity, (std::min)(DefOf(item).maxRarity, tiers[k]));
            loot.push_back({{{c.x - 400.0f + 200.0f * static_cast<float>(k), c.z + 540.0f}, item, t, true, true}, false});
        }
        LootSpawn heart = {{c.x + 520.0f, c.z + 540.0f}, ItemId::HeartContainer, Rarity::Legendary, true, true};
        heart.special = true;
        loot.push_back({heart, false});
    }
    // Put back everything on the plaza that has been picked up (the ones still lying there are left alone).
    int SandboxRestock() {
        if (!SandboxLive()) return 0;
        sandboxRestocked.resize(loot.size(), 0);
        int added = 0;
        const size_t had = loot.size();
        for (size_t i = 0; i < had; i++) {
            if (!loot[i].taken || sandboxRestocked[i] || Distance(loot[i].spawn.pos, sandboxRoom.center) > sandboxRoom.radius) continue;
            sandboxRestocked[i] = 1;
            AddLoot(loot[i].spawn);
            added++;
        }
        sandboxRestocked.resize(loot.size(), 0);
        return added;
    }

  private:
    void Enter(MatchState s) {
        if (s == MatchState::Ending) {
            for (auto& p : players) if (p.alive) p.placement = 1;
        }
        state = s;
        stateTime = 0;
        fartClouds.clear();
        lastFartCloud.clear();
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

    static bool IsStarter(const Equipped& e) { return e.item == ItemId::BasicSword && e.rarity == Rarity::Common; }
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
        for (auto& ally : allies) if (ally.owner == p.id && ally.alive) ReleaseAlly(ally, false);   // their allies are free to hire again
        for (auto& v : vehicles) for (auto& who : v.seat) if (who == p.id) who = kNoPlayer;      // and their seat in a cart is empty
        if (killer != kNoPlayer && killer != p.id) {
            if (PlayerState* k = Find(killer)) k->kills++;
        }
        if (!replay.frames.empty() || state == MatchState::InMatch) replay.kills.push_back({static_cast<uint16_t>((std::max)(0, static_cast<int>(replay.frames.size()) - 1)), killer == kNoPlayer ? static_cast<uint16_t>(0xFFFF) : static_cast<uint16_t>(killer), static_cast<uint16_t>(p.id)});
        MatchEvent e{MatchEvent::Type::Eliminated};
        e.a = p.id; e.b = killer;
        events.push_back(e);
    }
    // Only part of the kit is left behind: a random half of the items (at least one, never all of them when there is more than one) and
    // about 60 per cent of the money and ammo. The rest goes with the player.
    void DropKit(const PlayerState& p) {
        std::vector<const Equipped*> kit;
        auto add = [&](const Equipped& e) { if (!IsStarter(e)) kit.push_back(&e); };
        add(p.weapon);
        for (const Equipped& spare : p.reserve) add(spare);
        if (p.hasShield) add(p.shield);
        if (p.hasAbility) add(p.ability);
        for (int slot = 0; slot < kGearSlots; slot++) if (p.gearMask & (1 << slot)) add(p.gear[slot]);
        for (const Equipped& potion : p.potions) add(potion);
        Rng rng(seed ^ ((static_cast<uint64_t>(p.id) + 1) * 0x9E3779B97F4A7C15ull) ^ 0x64726F70ull);   // "drop"
        for (size_t i = kit.size(); i > 1; i--) std::swap(kit[i - 1], kit[rng.Below(static_cast<uint32_t>(i))]);
        if (!kit.empty()) {
            const int n = static_cast<int>(kit.size());
            int keep = static_cast<int>(std::lround(n * kDeathDropShare + (rng.Unit() - 0.5)));
            keep = (std::max)(1, (std::min)(keep, n > 1 ? n - 1 : 1));
            for (int i = 0; i < keep; i++) DropEquipment(p, *kit[static_cast<size_t>(i)]);
        }
        // Their money and ammo too, in a few piles so it isn't one jackpot: rupees in lumps of up to 50, each kind of ammo in one pile.
        const float facing = static_cast<float>(p.rot) * (3.14159265f / 32768.0f);
        int pile = 0;
        auto spot = [&]() {
            const float a = facing + 1.2f + 0.9f * static_cast<float>(pile++);
            Vec2 at = {p.pos.x + std::sin(a) * 110.0f, p.pos.z + std::cos(a) * 110.0f};
            if (Distance(at, map.center) > map.radius || (placement && !placement(at))) at = p.pos;
            return at;
        };
        for (int left = ShareOf(p.rupees); left > 0 && pile < 8;) {
            const int lump = (std::min)(left, 50);
            LootSpawn l = {spot(), ItemId::Rupees, Rarity::Common, false, false};
            l.amount = static_cast<uint16_t>(lump);
            AddLoot(l);
            left -= lump;
        }
        for (int k = 0; k < kAmmoKinds; k++) {
            if (p.ammo[static_cast<size_t>(k)] <= 0) continue;
            LootSpawn l = {spot(), AmmoItem(static_cast<AmmoKind>(k)), Rarity::Common, false, false};
            l.amount = static_cast<uint16_t>((std::max)(1, ShareOf(p.ammo[static_cast<size_t>(k)])));
            AddLoot(l);
        }
    }
    static int ShareOf(int amount) { return (amount * 60 + 99) / 100; }

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
            case InstantEffect::AdultPower:
                if (clock + 20.0f < p.adultUntil) return false;   // already grown: don't waste it (it can be topped up near the end)
                p.adultUntil = clock + kAdultSeconds;
                p.dirty = true;
                return true;
            case InstantEffect::MagicJar: {
                const bool needsMagic = MagicNow(p) < kMaxMagic - 0.5f, recharging = p.hasAbility && clock < p.abilityReadyAt;
                if (!needsMagic && !recharging) return false;
                AddMagic(p, 35.0f + 15.0f * RarityScale(rarity));
                if (recharging) p.abilityReadyAt = clock + (p.abilityReadyAt - clock) * 0.4f;
                return true;
            }
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
        for (auto& v : vehicles) for (auto& who : v.seat) if (who == p.id) who = kNoPlayer;   // a warp or a pull takes you out of a cart
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
                case EffectType::Launch: {
                    // A mobility blast. A player's own game throws them into the air and keeps them safe from the landing (the server only knows
                    // x and z). Bots move on flat ground, so the server carries a bot through the air instead: away from the nearest enemy, or
                    // the way it faces when nobody is close.
                    if (!p.isBot) { did = true; break; }
                    const float facing = static_cast<float>(p.rot) * (3.14159265f / 32768.0f);
                    float dx = std::sin(facing), dz = std::cos(facing);
                    const PlayerState* foe = nullptr;
                    float foeDist = 800.0f;
                    for (const auto& o : players) {
                        if (!o.alive || o.id == p.id) continue;
                        const float d = Distance(o.pos, p.pos);
                        if (d < foeDist) { foeDist = d; foe = &o; }
                    }
                    if (foe && foeDist > 1.0f) { dx = (p.pos.x - foe->pos.x) / foeDist; dz = (p.pos.z - foe->pos.z) / foeDist; }
                    const float carry = fx.amount * s;
                    Vec2 to = {p.pos.x + dx * carry, p.pos.z + dz * carry};
                    const float off = Distance(to, map.center);
                    if (off > map.radius - 20.0f) to = {map.center.x + (to.x - map.center.x) / off * (map.radius - 20.0f), map.center.z + (to.z - map.center.z) / off * (map.radius - 20.0f)};
                    if (placement && !placement(to)) to = {p.pos.x + dx * carry * 0.4f, p.pos.z + dz * carry * 0.4f}; // no ground there: a shorter hop
                    if (!placement || placement(to)) Teleport(p, to);
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
    bool soloTest = false;
    std::vector<PlayerState> players;
    std::vector<LootEntry> loot;
    std::vector<MatchEvent> events;
    PlacementFn placement;
    std::vector<Vec2> lootSpots;
    std::vector<Vec2> bossSpots;
    std::vector<MiniBoss> bosses;
    std::vector<Strike> strikes;
    std::vector<FartCloud> fartClouds;
    std::map<uint32_t, float> lastFartCloud;   // when each player last started a cloud
    std::shared_ptr<const NavGrid> nav;
    std::vector<AllyState> allies;
    std::vector<VehicleState> vehicles;
    std::vector<Vec2> vehicleSpots;
    CartWorld vehicleWorld;
    int vehicleCount = 0;    // the host's game sets it (GameServer); plain matches and most tests have none
    Replay replay;
    float replayNextAt = 0;
    std::vector<Vec2> allySpots;
    bool majorBoss = false;
    bool dragonSpawned = false;
    std::vector<ChestSite> chestSites;
    std::shared_ptr<LootPlan> lootPlan;
    bool supplyDrops = true;
    WeatherOptions wopt;
    Weather weather;
    int spell = -1;
    float nextBolt = 0;
    int boltCount = 0;
    float nextSupplyAt = kSupplyFirstSec;
    int supplyCount = 0;
    struct PendingSupply { Vec2 pos; float landAt; };
    std::vector<PendingSupply> pendingSupply;
    int bossCount = 0;
    int mapId = 0;           // which place this is (shared/map.h): decides the bosses
    bool sandbox = false, sandboxGod = true, sandboxStormRuns = false, sandboxWeatherFree = false;
    Vec2 sandboxSpawn = {};
    Circle sandboxRoom = {};
    std::vector<uint8_t> sandboxRestocked;   // loot entries already put back by SandboxRestock
    int playerLimit = kMaxPlayers; // the host's game turns bosses on (GameServer::SetBossCount); plain matches and the tests have none
};

} // namespace royale
