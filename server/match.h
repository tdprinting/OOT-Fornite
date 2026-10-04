#pragma once
#include "../shared/balance.h"
#include "../shared/combat.h"
#include "../shared/storm.h"
#include <vector>

namespace royale {

enum class MatchState : uint8_t { Lobby, Countdown, Drop, InMatch, Ending };

struct Equipped {
    ItemId item;
    Rarity rarity;
};

struct PlayerState {
    uint32_t id = 0;
    bool isBot = false;
    bool alive = true;
    float health = kMaxHealth; // hearts
    Vec2 pos = {};
    Equipped weapon = {ItemId::DekuStick, Rarity::Common}; // starter weapon, like Fortnite's pickaxe
    bool hasShield = false;
    Equipped shield = {ItemId::DekuShield, Rarity::Common};
    std::vector<Equipped> potions;
    float attackReadyAt = 0;
    int kills = 0;
};

struct LootEntry {
    LootSpawn spawn;
    bool taken = false;
};

struct AttackResult {
    bool ok = false; // the attack was allowed and its cooldown started
    bool hit = false;
    float damage = 0;
    bool killed = false;
};

// The server-side match. No networking and no game code in here, so it runs inside the host's game, headless,
// or in unit tests. Feed it Tick(dt) at kTickHz and call the event methods as messages arrive.
class Match {
  public:
    static constexpr float kCountdownSec = 10.0f;
    static constexpr float kDropSec = 5.0f; // spawn invulnerability
    static constexpr float kEndingSec = 10.0f;

    Match(uint64_t seed, Circle map, int lootCount = 400) : seed(seed), map(map), storm(seed, map) {
        for (const LootSpawn& l : GenerateLoot(seed, map, lootCount, 0.15f)) loot.push_back({l, false});
    }

    // Add a human. Returns false if the lobby is full or the match already started.
    bool AddHuman(uint32_t id) {
        if (state != MatchState::Lobby || players.size() >= kMaxPlayers) return false;
        players.push_back(MakePlayer(id, false));
        return true;
    }

    // Start with at least one human; every remaining slot up to kMaxPlayers is filled with a bot.
    bool Start() {
        if (state != MatchState::Lobby || players.empty()) return false;
        humans = static_cast<int>(players.size());
        uint32_t nextId = 1000;
        while (players.size() < kMaxPlayers) players.push_back(MakePlayer(nextId++, true));
        Rng spawn(seed ^ 0x7370776Eull); // "spwn"
        for (auto& p : players) {
            float a = static_cast<float>(spawn.Unit() * 6.283185307179586);
            float d = map.radius * 0.9f * std::sqrt(static_cast<float>(spawn.Unit()));
            p.pos = {map.center.x + d * std::cos(a), map.center.z + d * std::sin(a)};
        }
        state = MatchState::Countdown;
        stateTime = 0;
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
                    if (dps > 0) Damage(p.id, dps * dt);
                }
                if (Alive() <= 1) Enter(MatchState::Ending);
                break;
            case MatchState::Ending:
            case MatchState::Lobby:
                break;
        }
    }

    // Returns true if the player was eliminated by this damage. Ignored during the drop (spawn protection).
    bool Damage(uint32_t id, float hearts) {
        PlayerState* p = Find(id);
        if (!p || !p->alive || state == MatchState::Drop || hearts <= 0) return false;
        p->health -= hearts;
        if (p->health <= 0) {
            p->health = 0;
            p->alive = false;
            DropKit(*p);
            return true;
        }
        return false;
    }

    // Attack with the attacker's equipped weapon. `hit` is the outcome of the accuracy roll (bots roll it themselves,
    // for humans the client reports it). A miss still spends the cooldown. Range and cooldown are checked here.
    AttackResult Attack(uint32_t attackerId, uint32_t targetId, bool hit = true) {
        AttackResult r;
        if (state != MatchState::InMatch) return r;
        PlayerState* a = Find(attackerId);
        PlayerState* t = Find(targetId);
        if (!a || !t || a == t || !a->alive || !t->alive) return r;
        WeaponStats w = WeaponOf(a->weapon.item);
        if (w.damage <= 0 || clock < a->attackReadyAt) return r;
        if (Distance(a->pos, t->pos) > w.range * 1.1f) return r;
        a->attackReadyAt = clock + w.cooldown;
        r.ok = true;
        if (!hit) return r;
        float reduction = t->hasShield ? ShieldReduction(t->shield.item, t->shield.rarity) : 0.0f;
        r.damage = w.damage * static_cast<float>(kRarityMultiplier[static_cast<int>(a->weapon.rarity)]) * (1.0f - reduction);
        r.hit = true;
        r.killed = Damage(targetId, r.damage);
        if (r.killed) a->kills++;
        return r;
    }

    // Pick up loot entry `index`. Weapons and shields swap with what the player holds (the old one drops on the ground).
    bool PickUp(uint32_t id, size_t index) {
        PlayerState* p = Find(id);
        if (!p || !p->alive || (state != MatchState::Drop && state != MatchState::InMatch)) return false;
        if (index >= loot.size() || loot[index].taken) return false;
        const LootSpawn s = loot[index].spawn;
        if (Distance(p->pos, s.pos) > kPickupRange * 1.5f) return false;
        switch (KindOf(s.item)) {
            case ItemKind::Weapon:
                DropEquipment(*p, p->weapon);
                p->weapon = {s.item, s.rarity};
                break;
            case ItemKind::Shield:
                if (p->hasShield) DropEquipment(*p, p->shield);
                p->shield = {s.item, s.rarity};
                p->hasShield = true;
                break;
            case ItemKind::Potion:
                if (static_cast<int>(p->potions.size()) >= kMaxPotions) return false;
                p->potions.push_back({s.item, s.rarity});
                break;
            case ItemKind::Utility:
                return false;
        }
        loot[index].taken = true;
        return true;
    }

    // Drink the potion that restores the missing health with the least waste (or the biggest if none is enough).
    bool UsePotion(uint32_t id) {
        PlayerState* p = Find(id);
        if (!p || !p->alive || p->potions.empty() || p->health >= kMaxHealth) return false;
        if (state != MatchState::Drop && state != MatchState::InMatch) return false;
        float missing = kMaxHealth - p->health;
        size_t best = 0;
        for (size_t i = 1; i < p->potions.size(); i++) {
            float hi = PotionHeal(p->potions[i].item, p->potions[i].rarity);
            float hb = PotionHeal(p->potions[best].item, p->potions[best].rarity);
            bool iEnough = hi >= missing, bEnough = hb >= missing;
            if ((iEnough && !bEnough) || (iEnough == bEnough && (iEnough ? hi < hb : hi > hb))) best = i;
        }
        p->health = std::min(kMaxHealth, p->health + PotionHeal(p->potions[best].item, p->potions[best].rarity));
        p->potions.erase(p->potions.begin() + static_cast<long>(best));
        return true;
    }

    const std::vector<LootEntry>& Loot() const { return loot; }
    size_t AddLoot(const LootSpawn& l) { loot.push_back({l, false}); return loot.size() - 1; }
    void ClearLoot() { loot.clear(); }
    float Clock() const { return clock; }

    int Alive() const {
        int n = 0;
        for (const auto& p : players) n += p.alive;
        return n;
    }
    // Only meaningful once the match is Ending.
    const PlayerState* Winner() const {
        if (state != MatchState::Ending) return nullptr;
        for (const auto& p : players) if (p.alive) return &p;
        return nullptr;
    }

    PlayerState* Find(uint32_t id) {
        for (auto& p : players) if (p.id == id) return &p;
        return nullptr;
    }

    MatchState State() const { return state; }
    int Humans() const { return humans; }
    float StormTime() const { return stormTime; }
    const Storm& GetStorm() const { return storm; }
    const std::vector<PlayerState>& Players() const { return players; }
    std::vector<PlayerState>& Players() { return players; }
    uint64_t Seed() const { return seed; }

  private:
    void Enter(MatchState s) { state = s; stateTime = 0; }

    static PlayerState MakePlayer(uint32_t id, bool isBot) {
        PlayerState p;
        p.id = id;
        p.isBot = isBot;
        return p;
    }

    static bool IsStarter(const Equipped& e) { return e.item == ItemId::DekuStick && e.rarity == Rarity::Common; }
    void DropEquipment(const PlayerState& p, const Equipped& e) {
        if (!IsStarter(e)) loot.push_back({{p.pos, e.item, e.rarity, false}, false});
    }
    // Eliminated players leave their weapon and shield behind for others (the Skulltula pile in the design doc).
    void DropKit(const PlayerState& p) {
        DropEquipment(p, p.weapon);
        if (p.hasShield) DropEquipment(p, p.shield);
    }

    uint64_t seed;
    Circle map;
    Storm storm;
    MatchState state = MatchState::Lobby;
    float stateTime = 0, stormTime = 0, clock = 0;
    int humans = 0;
    std::vector<PlayerState> players;
    std::vector<LootEntry> loot;
};

} // namespace royale
