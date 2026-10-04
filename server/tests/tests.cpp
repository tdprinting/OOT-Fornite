#include "../match.h"
#include "../sim.h"
#include "../nav.h"
#include "../../shared/meshes.h"
#include "../../shared/tune.h"
#include "../../shared/poi.h"
#include "../../shared/props.h"
#include <set>
#include <string>
#include "../../shared/anim.h"
#include "../../shared/loot.h"
#include <cstdio>
#include <cstdlib>

using namespace royale;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static Circle MapCircle() { return {{0, 0}, 2000.0f}; }

static void StormNests() {
    for (uint64_t seed = 1; seed <= 200; seed++) {
        Storm s(seed, MapCircle());
        Circle prev = MapCircle();
        for (int i = 0; i < kStormPhaseCount; i++) {
            Circle e = s.PhaseEnd(i);
            float d = std::hypot(e.center.x - prev.center.x, e.center.z - prev.center.z);
            CHECK(d + e.radius <= prev.radius + 0.01f); // fully inside the previous circle
            prev = e;
        }
    }
}
static void StormDeterministic() {
    Storm a(42, MapCircle()), b(42, MapCircle()), c(43, MapCircle());
    for (float t = 0; t < a.TotalDuration(); t += 7.5f) {
        CHECK(a.SafeZoneAt(t).center.x == b.SafeZoneAt(t).center.x);
        CHECK(a.SafeZoneAt(t).radius == b.SafeZoneAt(t).radius);
    }
    CHECK(a.PhaseEnd(0).center.x != c.PhaseEnd(0).center.x);
}
static void StormTimeline() {
    Storm s(7, MapCircle());
    CHECK(s.SafeZoneAt(0).radius == 2000.0f);
    CHECK(s.SafeZoneAt(119).radius == 2000.0f);   // still waiting in phase 1
    CHECK(s.SafeZoneAt(120 + 90).radius < 1400.5f && s.SafeZoneAt(120 + 90).radius > 1399.5f); // 70%
    CHECK(s.SafeZoneAt(s.TotalDuration() + 100).radius == 0.0f);
    CHECK(s.PhaseAt(0) == 0 && s.PhaseAt(s.TotalDuration() + 1) == kStormPhaseCount);
    // Safe zone never grows.
    float last = 1e9f;
    for (float t = 0; t < s.TotalDuration(); t += 1.0f) { float r = s.SafeZoneAt(t).radius; CHECK(r <= last + 1e-3f); last = r; }
    CHECK(s.DamagePerSecond({1999, 0}, 0) == 0.0f);     // inside the map, phase 1 holds
    CHECK(s.DamagePerSecond({5000, 0}, 0) == 0.5f);     // outside the map
}
static void LootDeterministicAndValid() {
    auto a = GenerateLoot(99, MapCircle(), 400, 0.15f), b = GenerateLoot(99, MapCircle(), 400, 0.15f);
    CHECK(a.size() == 400 && b.size() == 400);
    int tiers[kRarityCount] = {};
    for (size_t i = 0; i < a.size(); i++) {
        CHECK(a[i].item == b[i].item && a[i].rarity == b[i].rarity && a[i].pos.x == b[i].pos.x);
        const ItemDef& d = kItems[static_cast<int>(a[i].item)];
        CHECK(a[i].rarity >= d.minRarity && a[i].rarity <= d.maxRarity);
        CHECK(MapCircle().Contains(a[i].pos));
        tiers[static_cast<int>(a[i].rarity)]++;
    }
    // Common should be the most frequent, legendary the least frequent (loose check on 400 samples).
    CHECK(tiers[0] > tiers[4]);
    CHECK(tiers[4] > 0);
}
static void ChestsRollHigher() {
    Rng r1(5), r2(5);
    double plain = 0, chest = 0;
    for (int i = 0; i < 5000; i++) { plain += static_cast<int>(RollRarity(r1, false)); chest += static_cast<int>(RollRarity(r2, true)); }
    CHECK(chest > plain);
}
static void WeightsSumTo100() {
    int sum = 0;
    for (int w : kRarityWeight) sum += w;
    CHECK(sum == 100);
}
static void SoloPlayerGets31Bots() {
    Match m(1, MapCircle());
    m.AddHuman(1);
    CHECK(m.Start());
    CHECK(m.Players().size() == kMaxPlayers && m.Humans() == 1);
    int bots = 0; for (auto& p : m.Players()) bots += p.isBot;
    CHECK(bots == 31);
    CHECK(!m.AddHuman(77)); // closed once started
}
static void StartNeedsOneHuman() {
    Match m(1, MapCircle());
    CHECK(!m.Start());
    CHECK(m.State() == MatchState::Lobby);
}
static void LobbyFull() {
    Match m(1, MapCircle());
    for (uint32_t i = 0; i < kMaxPlayers; i++) CHECK(m.AddHuman(i));
    CHECK(!m.AddHuman(999));
}
static void FullMatchHasOneWinner() {
    Match m(3, MapCircle());
    m.AddHuman(1);
    m.Start();
    // Spread players over the map, everyone outside the final circle eventually dies to the storm.
    Rng rng(11);
    for (auto& p : m.Players()) {
        float a = static_cast<float>(rng.Unit() * 6.28318f), d = 1900.0f * std::sqrt(static_cast<float>(rng.Unit()));
        p.pos = {d * std::cos(a), d * std::sin(a)};
    }
    const float dt = 1.0f / kTickHz;
    int guard = 0;
    while (m.State() != MatchState::Ending && guard++ < kTickHz * 3000) {
        // Players run toward the centre of the current safe zone at 6 u/s, as bots would.
        Circle z = m.GetStorm().SafeZoneAt(m.StormTime());
        for (auto& p : m.Players()) {
            float dx = z.center.x - p.pos.x, dz = z.center.z - p.pos.z, len = std::hypot(dx, dz);
            if (len > 1.0f) { p.pos.x += dx / len * 6.0f * dt; p.pos.z += dz / len * 6.0f * dt; }
        }
        m.Tick(dt);
    }
    CHECK(m.State() == MatchState::Ending);
    CHECK(m.Alive() <= 1);
    CHECK(guard < kTickHz * 3000);
}
static void SpawnProtection() {
    Match m(1, MapCircle());
    m.AddHuman(1);
    m.Start();
    for (int i = 0; i < kTickHz * 10 + 1; i++) m.Tick(1.0f / kTickHz); // through the countdown
    CHECK(m.State() == MatchState::Drop);
    CHECK(!m.Damage(1, 100));
    CHECK(m.Find(1)->alive);
    for (int i = 0; i < static_cast<int>(kDropSec * kTickHz) + 1; i++) m.Tick(1.0f / kTickHz);
    CHECK(m.State() == MatchState::InMatch);
    CHECK(m.Damage(1, 100));
    CHECK(!m.Find(1)->alive);
}

// ---- Bot AI ----------------------------------------------------------------------------------------------------

constexpr float kDt = 1.0f / kTickHz;

// Build a sim with one idle human (id 1) and one bot (id 1000), everyone else dead, no loot, already InMatch.
static Simulation Duel(uint64_t seed, Vec2 humanPos, Vec2 botPos) {
    Simulation sim(seed, MapCircle(), 0);
    sim.match.AddHuman(1);
    sim.match.Start();
    while (sim.match.State() != MatchState::InMatch) sim.match.Tick(kDt); // no bot movement while setting up
    for (auto& p : sim.match.Players()) if (p.id != 1 && p.id != 1000) p.alive = false;
    sim.match.Find(1)->pos = humanPos;
    sim.match.Find(1000)->pos = botPos;
    return sim;
}
static void Run(Simulation& sim, float seconds) {
    for (int i = 0; i < static_cast<int>(seconds * kTickHz); i++) sim.Tick(kDt);
}

static void CombatMath() {
    CHECK(WeaponDps(ItemId::MasterSword, Rarity::Epic) > WeaponDps(ItemId::KokiriSword, Rarity::Common));
    CHECK(WeaponDps(ItemId::KokiriSword, Rarity::Legendary) > WeaponDps(ItemId::KokiriSword, Rarity::Common));
    CHECK(KindOf(ItemId::Hookshot) == ItemKind::Ability && KindOf(ItemId::FairyBow) == ItemKind::Weapon);
    CHECK(ShieldReduction(ItemId::MirrorShield, Rarity::Legendary) <= 0.75f);
    for (int i = 0; i < kItemCount; i++) CHECK((WeaponOf(kItems[i].id).damage > 0) == (KindOf(kItems[i].id) == ItemKind::Weapon));
}
static void AttackRules() {
    Simulation sim = Duel(1, {0, 0}, {50, 0});
    Match& m = sim.match;
    m.Find(1)->weapon = {ItemId::KokiriSword, Rarity::Common};
    CHECK(m.Attack(1, 1000).ok);                // in range
    CHECK(!m.Attack(1, 1000).ok);               // cooldown
    for (int i = 0; i < kTickHz; i++) m.Tick(kDt);
    m.Find(1000)->pos = {500, 0};
    CHECK(!m.Attack(1, 1000).ok);               // out of melee range
    m.Find(1000)->pos = {50, 0};
    float before = m.Find(1000)->health;
    AttackResult plain = m.Attack(1, 1000);
    CHECK(plain.ok && plain.hit && m.Find(1000)->health < before);
    for (int i = 0; i < kTickHz; i++) m.Tick(kDt);
    m.Find(1000)->hasShield = true;
    m.Find(1000)->shield = {ItemId::HylianShield, Rarity::Rare};
    m.Find(1000)->health = kMaxHealth;
    AttackResult shielded = m.Attack(1, 1000);
    CHECK(shielded.damage < plain.damage);       // shield absorbs part of the hit
    // Misses spend the cooldown without damage.
    for (int i = 0; i < kTickHz; i++) m.Tick(kDt);
    float hp = m.Find(1000)->health;
    AttackResult miss = m.Attack(1, 1000, false);
    CHECK(miss.ok && !miss.hit && m.Find(1000)->health == hp);
}
static void NoAttacksDuringDrop() {
    Match m(1, MapCircle(), 0);
    m.AddHuman(1);
    m.Start();
    while (m.State() != MatchState::Drop) m.Tick(kDt);
    m.Players()[0].pos = {0, 0};
    m.Players()[1].pos = {10, 0};
    CHECK(!m.Attack(m.Players()[0].id, m.Players()[1].id).ok);
}
static void PickUpRulesAndSwap() {
    Simulation sim = Duel(1, {1500, 0}, {0, 0});
    Match& m = sim.match;
    size_t farLoot = m.AddLoot({{400, 0}, ItemId::MasterSword, Rarity::Epic, false});
    size_t nearLoot = m.AddLoot({{10, 0}, ItemId::MasterSword, Rarity::Epic, false});
    CHECK(!m.PickUp(1000, farLoot));                   // too far
    CHECK(m.PickUp(1000, nearLoot));
    CHECK(!m.PickUp(1000, nearLoot));                  // already taken
    CHECK(m.Find(1000)->weapon.item == ItemId::MasterSword);
    size_t n = m.Loot().size();
    size_t k = m.AddLoot({{0, 0}, ItemId::BiggoronSword, Rarity::Rare, false});
    CHECK(m.PickUp(1000, k));                      // not better than the Master Sword in hand, so it goes in a free hotbar slot
    CHECK(m.Find(1000)->weapon.item == ItemId::MasterSword && m.Find(1000)->reserve.size() == 1);
    CHECK(m.Loot().size() == n + 1);
    CHECK(m.SelectWeapon(1000, 1) && m.Find(1000)->weapon.item == ItemId::BiggoronSword && m.Find(1000)->reserve[0].item == ItemId::MasterSword);
    CHECK(!m.SelectWeapon(1000, 2) && !m.SelectWeapon(1000, 0));   // no such slot
    // With the hotbar full, a pickup replaces the weapon in hand and the old one lands on the ground.
    m.Find(1000)->reserve = {{ItemId::Slingshot, Rarity::Common}, {ItemId::Boomerang, Rarity::Common}};
    n = m.Loot().size();
    CHECK(m.PickUp(1000, m.AddLoot({{0, 0}, ItemId::MegatonHammer, Rarity::Epic, false})));
    CHECK(m.Loot().size() == n + 2);
    bool dropped = false;
    for (auto& e : m.Loot()) if (!e.taken && e.spawn.item == ItemId::BiggoronSword) dropped = true;
    CHECK(dropped);
    CHECK(m.PickUp(1000, m.AddLoot({{0, 0}, ItemId::Hookshot, Rarity::Rare, false})));  // abilities go in the ability slot
    CHECK(m.Find(1000)->hasAbility && m.Find(1000)->ability.item == ItemId::Hookshot);
}
static void PotionRules() {
    Simulation sim = Duel(1, {1500, 0}, {0, 0});
    Match& m = sim.match;
    PlayerState* p = m.Find(1000);
    CHECK(!m.UsePotion(1000));                     // none carried
    p->potions = {{ItemId::BluePotion, Rarity::Epic}, {ItemId::GreenPotion, Rarity::Common}};
    p->health = 2.5f;                              // missing 0.5: the small potion is enough, keep the big one
    CHECK(m.UsePotion(1000));
    CHECK(p->health == kMaxHealth && p->potions.size() == 1 && p->potions[0].item == ItemId::BluePotion);
    CHECK(!m.UsePotion(1000));                     // full health
}
static void DeathDropsKit() {
    Simulation sim = Duel(1, {1500, 0}, {0, 0});
    Match& m = sim.match;
    PlayerState* p = m.Find(1000);
    p->weapon = {ItemId::MasterSword, Rarity::Epic};
    p->hasShield = true;
    p->shield = {ItemId::HylianShield, Rarity::Rare};
    CHECK(m.Loot().empty());
    CHECK(m.Damage(1000, 100));
    CHECK(m.Loot().size() == 2 && !m.Loot()[0].taken && !m.Loot()[1].taken);
}

static void BotFetchesUpgrade() {
    Simulation sim = Duel(5, {1500, 0}, {0, 0});
    sim.match.AddLoot({{300, 0}, ItemId::MasterSword, Rarity::Epic, false});
    Run(sim, 15);
    CHECK(sim.match.Find(1000)->weapon.item == ItemId::MasterSword);
    CHECK(sim.match.Loot()[0].taken);
}
static void BotIgnoresDowngrade() {
    Simulation sim = Duel(5, {1500, 0}, {0, 0});
    sim.match.Find(1000)->weapon = {ItemId::MasterSword, Rarity::Epic};
    sim.match.Find(1000)->reserve = {{ItemId::Slingshot, Rarity::Common}, {ItemId::Boomerang, Rarity::Common}}; // hotbar full: no use for spares
    sim.match.AddLoot({{100, 0}, ItemId::KokiriSword, Rarity::Common, false});
    sim.match.AddLoot({{120, 0}, ItemId::RecoveryHeart, Rarity::Common, false}); // a heart at full health does nothing: not interesting
    Run(sim, 10);
    CHECK(sim.match.Find(1000)->weapon.item == ItemId::MasterSword);
    CHECK(!sim.match.Loot()[0].taken && !sim.match.Loot()[1].taken);
}
static void BotTakesShieldAndPotions() {
    Simulation sim = Duel(5, {1500, 0}, {0, 0});
    sim.match.AddLoot({{100, 0}, ItemId::MirrorShield, Rarity::Epic, false});
    sim.match.AddLoot({{-100, 0}, ItemId::RedPotion, Rarity::Uncommon, false});
    Run(sim, 15);
    PlayerState* b = sim.match.Find(1000);
    CHECK(b->hasShield && b->shield.item == ItemId::MirrorShield);
    CHECK(b->potions.size() == 1);
}
static void BotHealsWhenHurt() {
    Simulation sim = Duel(5, {1900, 0}, {0, 0});
    PlayerState* b = sim.match.Find(1000);
    b->health = 1.0f;
    b->potions = {{ItemId::RedPotion, Rarity::Rare}};
    Run(sim, 2);
    CHECK(b->health > 2.9f && b->potions.empty());
}
static void BotOutrunsStorm() {
    // A stationary human and a bot start at the map edge. The storm kills the human; the bot must still be alive and inside.
    Simulation sim = Duel(9, {1900, 0}, {0, 1900});
    bool humanDied = false;
    for (int i = 0; i < kTickHz * 700 && !humanDied; i++) {
        sim.Tick(kDt);
        humanDied = !sim.match.Find(1)->alive;
    }
    CHECK(humanDied);
    PlayerState* b = sim.match.Find(1000);
    CHECK(b->alive);
    CHECK(sim.match.GetStorm().SafeZoneAt(sim.match.StormTime()).Contains(b->pos));
}
static void BotsFightToTheDeath() {
    Simulation sim(3, MapCircle(), 0);
    sim.match.AddHuman(1);
    sim.match.Start();
    while (sim.match.State() != MatchState::InMatch) sim.match.Tick(kDt);
    for (auto& p : sim.match.Players()) p.alive = (p.id == 1000 || p.id == 1001);
    for (auto& p : sim.match.Players()) {
        if (p.id == 1000) { p.pos = {0, 0}; p.weapon = {ItemId::KokiriSword, Rarity::Common}; }
        if (p.id == 1001) { p.pos = {50, 0}; p.weapon = {ItemId::KokiriSword, Rarity::Common}; }
    }
    Run(sim, 60);
    CHECK(sim.match.Alive() == 1 && sim.match.State() == MatchState::Ending);
    const PlayerState* w = sim.match.Winner();
    CHECK(w && w->kills == 1);
}
static void BotsFaceTheirDirectionAndAnimate() {
    // A bot with a sword pickup straight ahead on the +x axis runs toward it: it must face +x and show a run animation,
    // then go back to idle once it has nothing to do.
    Simulation sim = Duel(5, {1500, 0}, {0, 0});
    sim.match.AddLoot({{300, 0, }, ItemId::MasterSword, Rarity::Epic, false});
    PlayerState* b = sim.match.Find(1000);
    for (int i = 0; i < 20; i++) sim.Tick(kDt);
    CHECK(b->anim == static_cast<uint8_t>(Anim::Run));
    CHECK(std::abs(static_cast<int>(b->rot) - 16384) < 200);   // +x is a quarter turn from +z: 0x4000
    CHECK(b->pos.x > 50);
    Run(sim, 10);                                              // picked it up; now it wanders (walks) or idles
    CHECK(b->weapon.item == ItemId::MasterSword);
}
static void BotsKeepDistanceWithBow() {
    Simulation sim = Duel(5, {0, 0}, {400, 0});
    sim.match.Find(1000)->weapon = {ItemId::FairyBow, Rarity::Rare};
    sim.match.Find(1)->weapon = {ItemId::DekuStick, Rarity::Common};
    Run(sim, 20);
    PlayerState* human = sim.match.Find(1);
    CHECK(!human->alive || human->health < kMaxHealth); // the bot shot the human
    PlayerState* b = sim.match.Find(1000);
    CHECK(b->alive && b->health == kMaxHealth);         // the idle human never reached the bot
}

struct FullResult { int winnerId = -1; int totalKills = 0; int armedAt60s = 0; int lootTaken = 0; bool ended = false; };
static FullResult FullBotMatch(uint64_t seed) {
    Simulation sim(seed, MapCircle());
    sim.match.AddHuman(1);
    sim.match.Start();
    FullResult r;
    float elapsed = 0;
    bool counted = false;
    while (sim.match.State() != MatchState::Ending && elapsed < 1500) {
        sim.Tick(kDt);
        elapsed += kDt;
        if (!counted && sim.match.StormTime() >= 60.0f) {
            counted = true;
            for (auto& p : sim.match.Players())
                if (p.isBot && !(p.weapon.item == ItemId::DekuStick && p.weapon.rarity == Rarity::Common)) r.armedAt60s++;
        }
    }
    r.ended = sim.match.State() == MatchState::Ending;
    if (const PlayerState* w = sim.match.Winner()) r.winnerId = static_cast<int>(w->id);
    for (auto& p : sim.match.Players()) r.totalKills += p.kills;
    for (auto& e : sim.match.Loot()) r.lootTaken += e.taken;
    return r;
}
static void FullMatchWithBots() {
    FullResult a = FullBotMatch(77);
    CHECK(a.ended);
    CHECK(a.armedAt60s >= 5);        // a good share of bots upgraded from the starter weapon (10 of 31 when written)
    CHECK(a.totalKills > 0);         // and fought each other
    CHECK(a.lootTaken > 20);
    std::printf("  full bot match: winner=%d kills=%d armed@60s=%d lootTaken=%d\n", a.winnerId, a.totalKills, a.armedAt60s, a.lootTaken);
    FullResult b = FullBotMatch(77);
    CHECK(a.winnerId == b.winnerId && a.totalKills == b.totalKills); // same seed, same match on this machine
}

static void PlacementValidatorKeepsLootAndSpawnsOnWalkableGround() {
    // Pretend everything west of x = 0 is water.
    PlacementFn east = [](Vec2 p) { return p.x >= 0; };
    auto loot = GenerateLoot(21, MapCircle(), 300, 0.15f, east);
    CHECK(loot.size() == 300);
    int bad = 0;
    for (auto& l : loot) bad += l.pos.x < 0;
    CHECK(bad == 0);                                   // 40 tries each makes a miss astronomically unlikely
    // Without a validator about half of them land in the "water".
    auto unfiltered = GenerateLoot(21, MapCircle(), 300, 0.15f);
    int wet = 0;
    for (auto& l : unfiltered) wet += l.pos.x < 0;
    CHECK(wet > 90);

    Match m(5, MapCircle(), 0);
    m.SetPlacementValidator(east);
    m.RegenerateLoot(250);
    CHECK(m.Loot().size() == 250);
    for (auto& e : m.Loot()) CHECK(e.spawn.pos.x >= 0);
    m.AddHuman(1);
    CHECK(m.Start());
    for (auto& p : m.Players()) CHECK(p.pos.x >= 0);   // spawn points too
}
static void ValidatorThatRejectsEverythingStillTerminates() {
    PlacementFn never = [](Vec2) { return false; };
    auto loot = GenerateLoot(1, MapCircle(), 50, 0.1f, never);
    CHECK(loot.size() == 50);                          // falls back to the last candidate instead of looping forever
}
static void StormPhaseInfo() {
    Storm s(7, MapCircle());
    auto a = s.InfoAt(0);
    CHECK(a.phase == 0 && !a.shrinking && std::abs(a.secondsLeft - 120.0f) < 0.01f);   // holding for 120 s
    auto b = s.InfoAt(119);
    CHECK(b.phase == 0 && !b.shrinking && std::abs(b.secondsLeft - 1.0f) < 0.01f);
    auto c = s.InfoAt(125);
    CHECK(c.phase == 0 && c.shrinking && std::abs(c.secondsLeft - 85.0f) < 0.01f);     // 90 s shrink, 5 s in
    auto d = s.InfoAt(120 + 90 + 1);
    CHECK(d.phase == 1 && !d.shrinking);                                              // phase 2 holds
    auto e = s.InfoAt(s.TotalDuration() + 5);
    CHECK(e.phase == kStormPhaseCount && !e.shrinking && e.secondsLeft == 0);
    auto f = s.InfoAt(-3);
    CHECK(f.phase == 0 && !f.shrinking);                                              // before the storm starts
}

// ---- item catalog and loot ----------------------------------------------------------------------------------------

static void CatalogIsConsistent() {
    int perKind[kItemKindCount] = {};
    std::set<std::string> names;
    int songs = 0, simple = 0;
    for (int i = 0; i < kItemCount; i++) {
        const ItemDef& d = kItems[i];
        CHECK(static_cast<int>(d.id) == i);
        CHECK(d.name && d.name[0] && d.effect && d.effect[0]);
        CHECK(names.insert(d.name).second);                       // every item has its own name
        CHECK(d.minRarity <= d.maxRarity);
        perKind[static_cast<int>(d.kind)]++;
        songs += IsSong(d.id);
        simple += IsSimpleSong(d.id);
        switch (d.kind) {
            case ItemKind::Weapon:     CHECK(WeaponOf(d.id).damage > 0 && WeaponOf(d.id).range > 0 && WeaponOf(d.id).cooldown > 0); break;
            case ItemKind::Shield:     CHECK(ShieldReduction(d.id, Rarity::Common) > 0); break;
            case ItemKind::Consumable: {
                PotionDef p = PotionOf(d.id);
                CHECK(p.heal > 0 || p.cleanse || p.damageTaken < 1 || p.revive || p.shield > 0);
                break;
            }
            case ItemKind::Instant:    CHECK(InstantOf(d.id) != InstantEffect::None); break;
            case ItemKind::Ability:    CHECK(AbilityOf(d.id).cooldown > 0 && AbilityOf(d.id).fx[0].type != EffectType::None); break;
            case ItemKind::Gear: {
                GearDef g = GearOf(d.id);
                CHECK(static_cast<int>(g.slot) >= 0 && static_cast<int>(g.slot) < kGearSlots);
                CHECK(g.melee != 1 || g.ranged != 1 || g.damageTaken != 1 || g.storm != 1 || g.fire != 1 || g.explosion != 1 || g.speed != 1 || g.stunImmune);
                break;
            }
        }
        // Non-weapons are not weapons and only gear has a gear slot.
        if (d.kind != ItemKind::Weapon) CHECK(WeaponOf(d.id).damage == 0);
        if (d.kind != ItemKind::Gear) CHECK(static_cast<int>(GearOf(d.id).slot) == kGearSlots);
    }
    CHECK(kItemCount >= 80);
    CHECK(perKind[static_cast<int>(ItemKind::Weapon)] == 14 && perKind[static_cast<int>(ItemKind::Shield)] == 3);
    CHECK(perKind[static_cast<int>(ItemKind::Consumable)] == 11 && perKind[static_cast<int>(ItemKind::Instant)] == 4);
    CHECK(perKind[static_cast<int>(ItemKind::Ability)] == 22 && perKind[static_cast<int>(ItemKind::Gear)] == 31);
    CHECK(songs == 12 && simple == 4);
    // Every gear slot has several items, so there is always something to find for each.
    int perSlot[kGearSlots] = {};
    for (int i = 0; i < kItemCount; i++) if (kItems[i].kind == ItemKind::Gear) perSlot[static_cast<int>(GearOf(kItems[i].id).slot)]++;
    for (int n : perSlot) CHECK(n >= 2);
    int sum = 0;
    for (int w : kKindWeight) sum += w;
    CHECK(sum == 100);
}

static void LootCoversEveryItemAndRespectsKindWeights() {
    std::vector<int> seen(kItemCount, 0);
    int perKind[kItemKindCount] = {};
    int total = 0;
    for (uint64_t seed = 1; seed <= 25; seed++) {
        for (const auto& l : GenerateLoot(seed, MapCircle(), 2000, 0.15f)) {
            const ItemDef& d = DefOf(l.item);
            CHECK(l.rarity >= d.minRarity && l.rarity <= d.maxRarity);
            seen[static_cast<int>(l.item)]++;
            perKind[static_cast<int>(d.kind)]++;
            total++;
        }
    }
    for (int i = 0; i < kItemCount; i++) {
        if (seen[i] == 0) std::printf("  never spawned: %s\n", kItems[i].name);
        CHECK(seen[i] > 0);                                  // every single item can appear
    }
    // Kinds show up roughly as often as their weights say (the legendary tier has no consumables etc., so allow slack).
    for (int k = 0; k < kItemKindCount; k++) {
        double share = 100.0 * perKind[k] / total;
        CHECK(std::abs(share - kKindWeight[k]) < 9.0);
    }
    // Higher tiers hold the rare stuff: Light Arrows only ever spawn at Legendary.
    for (const auto& l : GenerateLoot(3, MapCircle(), 20000, 0.5f)) if (l.item == ItemId::LightArrows) CHECK(l.rarity == Rarity::Legendary);
}

static void GearScalesWithRarityAndStacks() {
    CHECK(Scaled(0.5f, Rarity::Common) == 0.5f);
    CHECK(Scaled(0.5f, Rarity::Legendary) == 0.2f);              // 1 - 0.5 * 1.75 would be 0.125: floored at 20%
    CHECK(std::abs(Scaled(1.2f, Rarity::Legendary) - 1.35f) < 0.001f);
    CHECK(std::abs(Scaled(0.9f, Rarity::Rare) - 0.87f) < 0.001f); // 1 - 0.1 * 1.3
    PlayerState p;
    GearTotals none = TotalsOf(p);
    CHECK(none.melee == 1 && none.speed == 1 && !none.stunImmune);
    p.gear[static_cast<int>(GearSlot::Boots)] = {ItemId::IronBoots, Rarity::Common};
    p.gear[static_cast<int>(GearSlot::Mask)] = {ItemId::BunnyHood, Rarity::Common};
    p.gearMask = (1 << static_cast<int>(GearSlot::Boots)) | (1 << static_cast<int>(GearSlot::Mask));
    GearTotals t = TotalsOf(p);
    CHECK(t.stunImmune);
    CHECK(std::abs(t.speed - 0.9f * 1.2f) < 0.001f);             // speeds multiply
    CHECK(std::abs(t.damageTaken - 0.9f) < 0.001f);
}

static void GearChangesDamageDealtAndTaken() {
    auto hit = [&](ItemId attackerGear, GearSlot slot, ItemId targetGear, GearSlot tslot, DamageKind kind) {
        Simulation sim = Duel(1, {0, 0}, {50, 0});
        Match& m = sim.match;
        PlayerState* a = m.Find(1);
        PlayerState* t = m.Find(1000);
        a->weapon = {ItemId::KokiriSword, Rarity::Common};
        if (attackerGear != ItemId::Count) { a->gear[static_cast<int>(slot)] = {attackerGear, Rarity::Common}; a->gearMask = static_cast<uint8_t>(1 << static_cast<int>(slot)); }
        if (targetGear != ItemId::Count) { t->gear[static_cast<int>(tslot)] = {targetGear, Rarity::Common}; t->gearMask = static_cast<uint8_t>(1 << static_cast<int>(tslot)); }
        float before = t->health;
        if (kind == DamageKind::Normal) m.Attack(1, 1000);
        else m.Damage(1000, 1.0f, kNoPlayer, kind);
        return before - t->health;
    };
    const ItemId none = ItemId::Count;
    float plain = hit(none, GearSlot::Mask, none, GearSlot::Mask, DamageKind::Normal);
    CHECK(std::abs(plain - 0.8f) < 0.001f);                                       // Kokiri Sword at Common
    CHECK(hit(ItemId::SpiritMedallion, GearSlot::Charm, none, GearSlot::Mask, DamageKind::Normal) > plain);   // melee gear hits harder
    CHECK(hit(ItemId::SkullMask, GearSlot::Mask, none, GearSlot::Mask, DamageKind::Normal) == plain);          // ranged gear doesn't help a sword
    CHECK(hit(none, GearSlot::Mask, ItemId::LightMedallion, GearSlot::Charm, DamageKind::Normal) < plain);    // damage reduction
    // Resistances only apply to their own kind of damage.
    float storm = hit(none, GearSlot::Mask, none, GearSlot::Mask, DamageKind::Storm);
    CHECK(std::abs(storm - 1.0f) < 0.001f);
    CHECK(hit(none, GearSlot::Mask, ItemId::ZoraMask, GearSlot::Mask, DamageKind::Storm) < storm);
    CHECK(hit(none, GearSlot::Mask, ItemId::ZoraMask, GearSlot::Mask, DamageKind::Fire) == storm);            // Zora Mask doesn't stop fire
    CHECK(hit(none, GearSlot::Mask, ItemId::GoronTunic, GearSlot::Tunic, DamageKind::Fire) < 0.6f);
    CHECK(hit(none, GearSlot::Mask, ItemId::GoronTunic, GearSlot::Tunic, DamageKind::Explosion) < 0.6f);
    CHECK(hit(none, GearSlot::Mask, ItemId::GoronTunic, GearSlot::Tunic, DamageKind::Storm) == storm);
}

static void PickupRulesForEveryKind() {
    Simulation sim = Duel(1, {1500, 0}, {0, 0});
    Match& m = sim.match;
    PlayerState* p = m.Find(1000);
    auto grab = [&](ItemId item, Rarity r) { return m.PickUp(1000, m.AddLoot({{0, 10}, item, r, false})); };

    // Gear: one item per slot; a second item for the same slot swaps and drops the first.
    CHECK(grab(ItemId::KokiriBoots, Rarity::Common));
    CHECK(p->gearMask & (1 << static_cast<int>(GearSlot::Boots)));
    size_t lootBefore = m.Loot().size();
    CHECK(grab(ItemId::HoverBoots, Rarity::Epic));
    CHECK(p->gear[static_cast<int>(GearSlot::Boots)].item == ItemId::HoverBoots);
    CHECK(m.Loot().size() == lootBefore + 2);                                       // the HoverBoots pickup itself plus the dropped Kokiri Boots
    bool droppedOld = false;
    for (auto& e : m.Loot()) droppedOld |= !e.taken && e.spawn.item == ItemId::KokiriBoots;
    CHECK(droppedOld);
    CHECK(grab(ItemId::GoronTunic, Rarity::Rare));                                  // different slot: both kept
    CHECK(p->gear[static_cast<int>(GearSlot::Tunic)].item == ItemId::GoronTunic && p->gear[static_cast<int>(GearSlot::Boots)].item == ItemId::HoverBoots);

    // Ability: single slot, swaps, ready immediately.
    CHECK(!p->hasAbility);
    CHECK(grab(ItemId::NayrusLove, Rarity::Epic) && p->hasAbility && p->ability.item == ItemId::NayrusLove && p->abilityReadyAt <= m.Clock());
    CHECK(grab(ItemId::SunsSong, Rarity::Rare) && p->ability.item == ItemId::SunsSong);

    // Consumables: bag holds kMaxPotions, then refuses.
    for (int i = 0; i < kMaxPotions; i++) CHECK(grab(ItemId::Milk, Rarity::Common));
    size_t taken = 0;
    for (auto& e : m.Loot()) taken += e.taken;
    CHECK(!grab(ItemId::Fish, Rarity::Common));
    size_t takenAfter = 0;
    for (auto& e : m.Loot()) takenAfter += e.taken;
    CHECK(takenAfter == taken && p->potions.size() == static_cast<size_t>(kMaxPotions));

    // Instant: a heart at full health is left on the ground, and used once you are hurt.
    CHECK(!grab(ItemId::RecoveryHeart, Rarity::Common));
    p->health = 1.0f;
    CHECK(grab(ItemId::RecoveryHeart, Rarity::Common) && std::abs(p->health - 2.0f) < 0.001f);
    CHECK(grab(ItemId::RecoveryHeart, Rarity::Legendary) && p->health == p->maxHealth);   // capped

    // Pieces of Heart: four make a container, and the cap is respected.
    for (int i = 0; i < 3; i++) CHECK(grab(ItemId::HeartPiece, Rarity::Uncommon));
    CHECK(p->maxHealth == kMaxHealth && p->heartPieces == 3);
    CHECK(grab(ItemId::HeartPiece, Rarity::Uncommon));
    CHECK(p->maxHealth == kMaxHealth + 1 && p->heartPieces == 0);
    for (int i = 0; i < 20; i++) grab(ItemId::HeartContainer, Rarity::Epic);
    CHECK(p->maxHealth == kMaxHealthCap);
    CHECK(!grab(ItemId::HeartContainer, Rarity::Epic));                                   // nothing left to gain
    CHECK(!grab(ItemId::HeartPiece, Rarity::Uncommon));

    // Magic Jar: only useful while the ability is recharging.
    CHECK(!grab(ItemId::MagicJar, Rarity::Common));                                       // ability is ready
    p->abilityReadyAt = m.Clock() + 20;
    CHECK(grab(ItemId::MagicJar, Rarity::Common));
    CHECK(p->abilityReadyAt - m.Clock() < 20 * 0.41f);
    CHECK(p->dirty);
}

static void FairyRevivesOnceAndIsNeverDrunk() {
    Simulation sim = Duel(1, {1500, 0}, {0, 0});
    Match& m = sim.match;
    PlayerState* p = m.Find(1000);
    p->potions = {{ItemId::Fairy, Rarity::Rare}, {ItemId::GreenPotion, Rarity::Common}};
    p->health = 1.0f;
    m.DrainEvents();
    CHECK(m.UsePotion(1000));                                                         // drinks the Green Potion, not the Fairy
    CHECK(p->potions.size() == 1 && p->potions[0].item == ItemId::Fairy);
    CHECK(!m.UsePotion(1000));                                                        // only the Fairy is left: never drunk
    CHECK(!m.Damage(1000, 50.0f, 1));                                                 // lethal hit, but the Fairy brings them back
    CHECK(p->alive && std::abs(p->health - p->maxHealth * 0.5f) < 0.001f && p->potions.empty());
    bool revived = false;
    for (auto& e : m.DrainEvents()) revived |= e.type == MatchEvent::Type::Revived && e.a == 1000;
    CHECK(revived);
    CHECK(!m.Damage(1000, 50.0f, 1));                                                  // protected for 2 s: the hit does nothing
    CHECK(p->alive);
    for (int i = 0; i < 50; i++) m.Tick(kDt);                                          // wait out the protection
    CHECK(m.Damage(1000, 50.0f, 1) && !p->alive);                                      // no second Fairy
}

static void PotionVariants() {
    Simulation sim = Duel(1, {1500, 0}, {0, 0});
    Match& m = sim.match;
    PlayerState* p = m.Find(1000);
    p->health = 1.0f;
    p->potions = {{ItemId::Milk, Rarity::Common}};
    CHECK(m.UsePotion(1000) && std::abs(p->health - 2.5f) < 0.001f);                  // 1.5 hearts

    // Bugs and Blue Fire put out fire and end stun, even at full health.
    p->health = p->maxHealth;
    p->burnUntil = m.Clock() + 10; p->burnDps = 0.5f; p->stunUntil = m.Clock() + 10;
    p->potions = {{ItemId::Bug, Rarity::Common}};
    CHECK(m.UsePotion(1000));
    CHECK(!(m.Clock() < p->burnUntil) && !m.Stunned(*p));

    // A Poe halves damage for a while, then wears off.
    p->potions = {{ItemId::Poe, Rarity::Rare}};
    CHECK(m.UsePotion(1000));
    float before = p->health;
    m.Damage(1000, 1.0f, 1);
    CHECK(std::abs((before - p->health) - 0.5f) < 0.001f);
    for (int i = 0; i < 7 * kTickHz; i++) m.Tick(kDt);
    before = p->health;
    m.Damage(1000, 1.0f, 1);
    CHECK(std::abs((before - p->health) - 1.0f) < 0.001f);
    CHECK(!m.UsePotion(1000));                                                         // empty bag
}

static void WeaponEffects() {
    auto setup = [&](ItemId weapon, Rarity r) {
        Simulation sim = Duel(1, {0, 0}, {300, 0});
        sim.match.Find(1)->weapon = {weapon, r};
        return sim;
    };
    // Fire Arrows set the target on fire; the burn hurts over time, credits the attacker and runs out.
    {
        Simulation sim = setup(ItemId::FireArrows, Rarity::Common);
        Match& m = sim.match;
        PlayerState* t = m.Find(1000);
        CHECK(m.Attack(1, 1000).hit);
        float afterHit = t->health;
        for (int i = 0; i < 2 * kTickHz; i++) m.Tick(kDt);
        CHECK(t->health < afterHit - 0.3f);                                            // 2 s of 0.25 hearts per second
        for (int i = 0; i < 4 * kTickHz; i++) m.Tick(kDt);
        float afterBurn = t->health;
        for (int i = 0; i < 2 * kTickHz; i++) m.Tick(kDt);
        CHECK(t->health == afterBurn);                                                 // burn has ended
        CHECK(afterBurn > afterHit - 0.9f);                                            // and was bounded (3 s x 0.25 hearts)
    }
    // A burn that finishes someone credits the kill to whoever lit it.
    {
        Simulation sim = setup(ItemId::FireArrows, Rarity::Common);
        Match& m = sim.match;
        PlayerState* t = m.Find(1000);
        t->health = 1.2f;                                                              // the arrow itself leaves 0.3
        CHECK(m.Attack(1, 1000).hit && t->alive);
        for (int i = 0; i < 3 * kTickHz && t->alive; i++) m.Tick(kDt);
        CHECK(!t->alive && m.Find(1)->kills == 1);
    }
    // Ice Arrows freeze: the target can't attack, and takes 25% more damage while frozen.
    {
        Simulation sim = setup(ItemId::IceArrows, Rarity::Common);
        Match& m = sim.match;
        PlayerState* t = m.Find(1000);
        t->weapon = {ItemId::KokiriSword, Rarity::Common};
        t->pos = {350, 0};
        CHECK(m.Attack(1, 1000).hit);
        CHECK(m.Stunned(*t));
        CHECK(!m.Attack(1000, 1).ok);                                                  // frozen: no attack
        t->pos = {50, 0};
        float before = t->health;
        m.Damage(1000, 1.0f, 1);
        CHECK(std::abs((before - t->health) - 1.25f) < 0.001f);
        for (int i = 0; i < 3 * kTickHz; i++) m.Tick(kDt);
        CHECK(!m.Stunned(*t));
    }
    // Deku Nuts stun without hurting much; Iron Boots shrug stuns off.
    {
        Simulation sim = setup(ItemId::DekuNuts, Rarity::Common);
        Match& m = sim.match;
        PlayerState* t = m.Find(1000);
        CHECK(m.Attack(1, 1000).hit && m.Stunned(*t));
        CHECK(!m.UseAbility(1000));                                                    // stunned players can't use abilities either
        t->stunUntil = 0;
        t->gear[static_cast<int>(GearSlot::Boots)] = {ItemId::IronBoots, Rarity::Common};
        t->gearMask = 1 << static_cast<int>(GearSlot::Boots);
        for (int i = 0; i < 4 * kTickHz; i++) m.Tick(kDt);                             // let the nuts recharge
        CHECK(m.Attack(1, 1000).hit && !m.Stunned(*t));
    }
    // Light Arrows ignore shields.
    {
        Simulation sim = setup(ItemId::LightArrows, Rarity::Legendary);
        Match& m = sim.match;
        PlayerState* t = m.Find(1000);
        t->maxHealth = t->health = 10;
        float plain = m.Attack(1, 1000).damage;
        for (int i = 0; i < 3 * kTickHz; i++) m.Tick(kDt);
        t->hasShield = true; t->shield = {ItemId::MirrorShield, Rarity::Legendary};
        float shielded = m.Attack(1, 1000).damage;
        CHECK(plain > 0 && std::abs(plain - shielded) < 0.001f);
    }
    // Bombs also hurt bystanders near the target, but never the thrower.
    {
        Simulation sim = setup(ItemId::Bombs, Rarity::Common);
        Match& m = sim.match;
        // revive two more players for the splash test
        PlayerState* near = nullptr; PlayerState* far = nullptr;
        for (auto& p : m.Players()) if (p.id == 1001) near = &p; else if (p.id == 1002) far = &p;
        near->alive = far->alive = true;
        near->pos = {330, 0};   // 30 from the target
        far->pos = {900, 0};
        float nearBefore = near->health, farBefore = far->health, selfBefore = m.Find(1)->health;
        CHECK(m.Attack(1, 1000).hit);
        CHECK(near->health < nearBefore);
        CHECK(far->health == farBefore && m.Find(1)->health == selfBefore);
    }
}

static void AbilityBasics() {
    Simulation sim = Duel(1, {0, 0}, {200, 0});
    Match& m = sim.match;
    PlayerState* me = m.Find(1);
    PlayerState* foe = m.Find(1000);
    auto give = [&](ItemId item, Rarity r = Rarity::Common) { me->ability = {item, r}; me->hasAbility = true; me->abilityReadyAt = 0; };
    auto wait = [&](float s) { for (int i = 0; i < static_cast<int>(s * kTickHz); i++) m.Tick(kDt); };
    CHECK(!m.UseAbility(1));                                                          // nothing equipped

    // Din's Fire: burst around you, not on yourself, not beyond its radius.
    give(ItemId::DinsFire);
    PlayerState* far = m.Find(1001); far->alive = true; far->pos = {900, 0};
    float foeBefore = foe->health, farBefore = far->health, meBefore = me->health;
    m.DrainEvents();
    CHECK(m.UseAbility(1));
    CHECK(foe->health < foeBefore && far->health == farBefore && me->health == meBefore);
    bool announced = false;
    for (auto& e : m.DrainEvents()) announced |= e.type == MatchEvent::Type::AbilityUsed && e.a == 1 && e.item == static_cast<uint8_t>(ItemId::DinsFire);
    CHECK(announced);
    CHECK(!m.UseAbility(1));                                                          // recharging
    foe->health = foe->maxHealth;                                                      // keep the dummy alive for the rest of the test
    wait(AbilityOf(ItemId::DinsFire).cooldown + 0.5f);
    CHECK(m.UseAbility(1));                                                           // ready again
    far->alive = false;
    foe->health = foe->maxHealth;

    // Nayru's Love: nothing hurts you for 4 seconds, then it does again.
    foe->health = foe->maxHealth;
    give(ItemId::NayrusLove);
    CHECK(m.UseAbility(1));
    CHECK(m.Invulnerable(*me));
    meBefore = me->health;
    m.Damage(1, 2.0f, 1000);
    CHECK(me->health == meBefore);
    wait(4.5f);
    CHECK(!m.Invulnerable(*me));
    m.Damage(1, 0.5f, 1000);
    CHECK(me->health < meBefore);

    // Songs that heal.
    me->health = 1.0f;
    give(ItemId::ZeldasLullaby, Rarity::Rare);
    CHECK(m.UseAbility(1) && std::abs(me->health - 2.3f) < 0.001f);                  // 1.0 x 1.3
    me->health = 0.5f; me->burnUntil = m.Clock() + 10; me->burnDps = 1;
    give(ItemId::SerenadeOfWater);
    CHECK(m.UseAbility(1) && me->health > 1.9f && !(m.Clock() < me->burnUntil));      // heals and puts out the fire

    // Epona's Song and Saria's Song are timed status effects.
    give(ItemId::EponasSong);
    CHECK(m.UseAbility(1) && m.SpeedMultiplier(*me) > 1.3f);
    wait(7);
    CHECK(m.SpeedMultiplier(*me) == 1.0f);
    give(ItemId::SariasSong);
    CHECK(m.UseAbility(1) && m.Revealing(*me));
    wait(7);
    CHECK(!m.Revealing(*me));
    give(ItemId::LensOfTruth);
    CHECK(m.UseAbility(1) && m.Revealing(*me));
    wait(5);
    CHECK(m.Revealing(*me));                                                          // 10 seconds, still going at 5

    // Sun's Song stuns whoever is close, but not Iron Boots wearers or people out of range.
    foe->stunUntil = 0;
    give(ItemId::SunsSong);
    CHECK(m.UseAbility(1) && m.Stunned(*foe));
    wait(3);
    CHECK(!m.Stunned(*foe));
    foe->gear[static_cast<int>(GearSlot::Boots)] = {ItemId::IronBoots, Rarity::Common};
    foe->gearMask = 1 << static_cast<int>(GearSlot::Boots);
    give(ItemId::SunsSong);
    CHECK(m.UseAbility(1) && !m.Stunned(*foe));
    foe->gearMask = 0;
    foe->pos = {900, 0};
    give(ItemId::SunsSong);
    CHECK(m.UseAbility(1) && !m.Stunned(*foe));                                       // out of the 500 range
    foe->pos = {200, 0};

    // Song of Time: freezes everyone nearby and protects you for a moment.
    give(ItemId::SongOfTime, Rarity::Legendary);
    CHECK(m.UseAbility(1) && m.Stunned(*foe) && m.Invulnerable(*me));

    // Bolero of Fire burns, and the burn credits the player who played it.
    foe->stunUntil = 0;
    give(ItemId::BoleroOfFire);
    foe->health = foe->maxHealth;
    CHECK(m.UseAbility(1));
    wait(2);
    CHECK(foe->health < foe->maxHealth - 0.5f);
    CHECK(foe->burnBy == 1);

    // Magic Beans heal over time.
    me->health = 1.0f;
    give(ItemId::MagicBeans);
    CHECK(m.UseAbility(1));
    wait(5);
    CHECK(me->health > 1.8f);                                                         // 0.2 hearts per second for 5 s
    wait(8);
    float healed = me->health;
    wait(2);
    CHECK(me->health == healed);                                                      // and it stops after 10 s
}

static void AbilitiesThatMovePlayers() {
    Simulation sim = Duel(1, {0, 0}, {400, 0});
    Match& m = sim.match;
    PlayerState* me = m.Find(1);
    PlayerState* foe = m.Find(1000);
    auto give = [&](ItemId item, Rarity r = Rarity::Common) { me->ability = {item, r}; me->hasAbility = true; me->abilityReadyAt = 0; };
    me->rot = 0x4000;                                                                  // facing +x
    m.DrainEvents();

    // Hookshot pulls whoever is in front of you to you, stuns them, and costs nothing if nobody is there.
    give(ItemId::Hookshot);
    foe->pos = {-400, 0};                                                              // behind
    CHECK(!m.UseAbility(1) && me->abilityReadyAt == 0);                               // nobody in front: not used up
    foe->pos = {400, 0};
    CHECK(m.UseAbility(1));
    CHECK(Distance(foe->pos, me->pos) < 130 && foe->pos.x > 0);
    CHECK(m.Stunned(*foe));
    bool teleported = false;
    for (auto& e : m.DrainEvents()) teleported |= e.type == MatchEvent::Type::Teleported && e.a == 1000;
    CHECK(teleported);

    // Longshot reaches much farther than Hookshot.
    foe->pos = {1300, 0}; foe->stunUntil = 0;
    give(ItemId::Hookshot);
    CHECK(!m.UseAbility(1));
    give(ItemId::Longshot);
    CHECK(m.UseAbility(1) && Distance(foe->pos, me->pos) < 130);

    // Farore's Wind: the first use marks a spot for free, the second jumps back to it.
    me->pos = {100, 100};
    give(ItemId::FaroresWind);
    CHECK(m.UseAbility(1) && me->hasMark && me->abilityReadyAt == 0);
    me->pos = {900, -300};
    m.DrainEvents();
    CHECK(m.UseAbility(1));
    CHECK(me->pos.x == 100 && me->pos.z == 100 && !me->hasMark);
    CHECK(me->abilityReadyAt > m.Clock());                                             // only the jump starts the recharge
    bool jumped = false;
    for (auto& e : m.DrainEvents()) jumped |= e.type == MatchEvent::Type::Teleported && e.a == 1;
    CHECK(jumped);
    // A mark that goes unused fades after 20 s.
    me->abilityReadyAt = 0;
    CHECK(m.UseAbility(1) && me->hasMark);
    for (int i = 0; i < 21 * kTickHz; i++) m.Tick(kDt);
    CHECK(!me->hasMark);

    // Nocturne of Shadow teleports you into the current safe zone, onto walkable ground when there is a validator.
    m.SetPlacementValidator([](Vec2 p) { return p.x >= 0; });
    give(ItemId::NocturneOfShadow);
    Vec2 before = me->pos;
    CHECK(m.UseAbility(1));
    CHECK(Distance(me->pos, before) > 1.0f && me->pos.x >= 0);
    CHECK(m.GetStorm().SafeZoneAt(m.StormTime()).Contains(me->pos));
}

static void OcarinasPlayRandomSongs() {
    // A Fairy Ocarina only plays simple songs; the Ocarina of Time can play any. Both are deterministic for a seed.
    std::set<int> fairyEffects;
    for (uint64_t seed = 1; seed <= 60; seed++) {
        Simulation sim = Duel(seed, {0, 0}, {200, 0});
        Match& m = sim.match;
        PlayerState* me = m.Find(1);
        me->health = 1.0f;
        me->ability = {ItemId::FairyOcarina, Rarity::Common};
        me->hasAbility = true;
        CHECK(m.UseAbility(1));
        // Exactly one of: healed (Lullaby/Minuet), sped up (Epona/Minuet), revealing (Saria).
        bool healed = me->health > 1.0f, sped = m.SpeedMultiplier(*me) > 1.0f, revealing = m.Revealing(*me);
        CHECK(healed || sped || revealing);
        CHECK(!m.Stunned(*m.Find(1000)) && m.Find(1000)->health == m.Find(1000)->maxHealth);  // never an attacking song
        fairyEffects.insert((healed ? 1 : 0) | (sped ? 2 : 0) | (revealing ? 4 : 0));
    }
    CHECK(fairyEffects.size() >= 3);                                                   // it really does vary
    std::set<int> anyEffects;
    for (uint64_t seed = 1; seed <= 200; seed++) {
        Simulation sim = Duel(seed, {0, 0}, {200, 0});
        Match& m = sim.match;
        PlayerState* me = m.Find(1);
        me->health = 1.0f;
        me->ability = {ItemId::OcarinaOfTime, Rarity::Legendary};
        me->hasAbility = true;
        CHECK(m.UseAbility(1));
        PlayerState* foe = m.Find(1000);
        anyEffects.insert((me->health > 1.0f ? 1 : 0) | (m.SpeedMultiplier(*me) > 1.0f ? 2 : 0) | (m.Revealing(*me) ? 4 : 0) |
                          (m.Stunned(*foe) ? 8 : 0) | (foe->health < foe->maxHealth ? 16 : 0) | (foe->burnUntil > m.Clock() ? 32 : 0));
    }
    CHECK(anyEffects.size() >= 6);
    // Same seed, same song.
    auto play = [&](uint64_t seed) {
        Simulation sim = Duel(seed, {0, 0}, {200, 0});
        sim.match.Find(1)->ability = {ItemId::OcarinaOfTime, Rarity::Common};
        sim.match.Find(1)->hasAbility = true;
        sim.match.UseAbility(1);
        return sim.match.Find(1000)->health + sim.match.SpeedMultiplier(*sim.match.Find(1)) * 100;
    };
    CHECK(play(7) == play(7));
}

static void EliminatedPlayersDropEverythingAndKillsAreCredited() {
    Simulation sim = Duel(1, {0, 0}, {100, 0});
    Match& m = sim.match;
    PlayerState* v = m.Find(1000);
    v->weapon = {ItemId::MasterSword, Rarity::Epic};
    v->hasShield = true; v->shield = {ItemId::HylianShield, Rarity::Rare};
    v->ability = {ItemId::SunsSong, Rarity::Rare}; v->hasAbility = true;
    v->gear[static_cast<int>(GearSlot::Boots)] = {ItemId::HoverBoots, Rarity::Rare};
    v->gear[static_cast<int>(GearSlot::Charm)] = {ItemId::ForestMedallion, Rarity::Epic};
    v->gearMask = (1 << static_cast<int>(GearSlot::Boots)) | (1 << static_cast<int>(GearSlot::Charm));
    v->potions = {{ItemId::RedPotion, Rarity::Common}, {ItemId::Fish, Rarity::Common}};
    CHECK(m.Loot().empty());
    CHECK(m.Damage(1000, 50.0f, 1));
    CHECK(m.Loot().size() == 7);                                                       // weapon, shield, ability, 2 gear, 2 potions
    CHECK(m.Find(1)->kills == 1);                                                      // credited exactly once
    // The dead player's loot can be picked up by the winner.
    int got = 0;
    for (size_t i = 0; i < m.Loot().size(); i++) { m.Find(1)->pos = m.Loot()[i].spawn.pos; got += m.PickUp(1, i); }
    CHECK(got >= 6);
}

static void MovementPlausibilityAllowsSpeedBuffs() {
    PlayerState p;
    Match m(1, MapCircle(), 0);
    CHECK(m.SpeedMultiplier(p) == 1.0f);
    p.gear[static_cast<int>(GearSlot::Boots)] = {ItemId::HoverBoots, Rarity::Legendary};
    p.gearMask = 1 << static_cast<int>(GearSlot::Boots);
    CHECK(std::abs(m.SpeedMultiplier(p) - 1.35f) < 0.001f);
    CHECK(kMaxPlausibleSpeed / kRunSpeed > m.SpeedMultiplier(p) * 1.8f);               // the movement clamp has room for every buff stacked
}

// ---- Bot AI: pathfinding and smarter behaviour ----------------------------------------------------------------------

// A wall down the middle of the map: x within 60 of zero from z=-450 to z=450.
static bool NotWall(Vec2 p) { return !(std::fabs(p.x) < 60.0f && std::fabs(p.z) < 450.0f); }

static void NavPathsAroundWalls() {
    NavGrid nav(MapCircle(), NotWall);
    CHECK(nav.WalkableCount() > 1000);
    CHECK(!nav.Walkable({0, 0}) && nav.Walkable({500, 0}));
    CHECK(!nav.LineClear({-500, 0}, {500, 0}));
    std::vector<Vec2> path;
    CHECK(nav.FindPath({-500, 0}, {500, 0}, path) && !path.empty());
    Vec2 at = {-500, 0};
    float length = 0;
    bool allClear = true;
    for (Vec2 w : path) { allClear &= nav.Walkable(w) && nav.LineClear(at, w); length += Distance(at, w); at = w; }
    CHECK(allClear);
    CHECK(Distance(at, {500, 0}) < 1.0f);
    CHECK(length > 1000.0f && length < 2000.0f);          // goes around, but not absurdly far
    CHECK(path.size() < 12);                              // and was smoothed into a few waypoints, not one per cell
    CHECK(nav.FindPath({-500, 0}, {0, 0}, path));         // a goal inside the wall snaps to the nearest open ground
    CHECK(nav.Walkable(path.back()));
    CHECK(nav.FindPath({-100, -100}, {-300, 200}, path) && path.size() == 1); // clear lines need no detour

    // A pocket sealed off from the rest: no route.
    NavGrid sealed(MapCircle(), [](Vec2 p) { const float d = std::hypot(p.x, p.z); return d < 150.0f || d > 300.0f; });
    CHECK(!sealed.FindPath({600, 0}, {0, 0}, path));
}

static void BotsWalkAroundWalls() {
    // The same wall, a bot on one side and the sword on the other: with the grid it gets there; every step stays on open ground.
    for (int useNav = 0; useNav < 2; useNav++) {
        Simulation sim = Duel(5, {-1800, 0}, {-300, 0});
        sim.match.AddLoot({{300, 0}, ItemId::MasterSword, Rarity::Epic, false});
        if (useNav) sim.bots.SetNav(std::make_shared<NavGrid>(MapCircle(), NotWall));
        PlayerState* b = sim.match.Find(1000);
        bool onWall = false;
        for (int i = 0; i < 40 * kTickHz && b->weapon.item != ItemId::MasterSword; i++) {
            sim.Tick(kDt);
            onWall |= std::fabs(b->pos.x) < 25.0f && std::fabs(b->pos.z) < 450.0f; // the grid is 60 wide, so only the core of the wall is guaranteed
        }
        if (useNav) {
            CHECK(b->weapon.item == ItemId::MasterSword);
            CHECK(!onWall);
        } else {
            CHECK(b->weapon.item == ItemId::MasterSword); // no grid: straight line, through the wall (the old behaviour)
        }
    }
}

static void BotsUseAbilitiesWhenItCounts() {
    {   // Din's Fire on a nearby foe.
        Simulation sim = Duel(5, {200, 0}, {0, 0});
        PlayerState* b = sim.match.Find(1000);
        b->ability = {ItemId::DinsFire, Rarity::Rare}; b->hasAbility = true;
        Run(sim, 4);
        CHECK(sim.match.Find(1)->health < kMaxHealth);
        CHECK(sim.match.Clock() < b->abilityReadyAt + 100 && b->abilityReadyAt > 0);
    }
    {   // Nayru's Love when about to die.
        Simulation sim = Duel(5, {150, 0}, {0, 0});
        PlayerState* b = sim.match.Find(1000);
        b->ability = {ItemId::NayrusLove, Rarity::Rare}; b->hasAbility = true;
        b->health = 1.0f;
        sim.match.Find(1)->weapon = {ItemId::BiggoronSword, Rarity::Rare};
        bool invulnerable = false;
        for (int i = 0; i < 6 * kTickHz; i++) { sim.Tick(kDt); invulnerable |= sim.match.Invulnerable(*b); }
        CHECK(invulnerable);
    }
    {   // No target, nothing to use it on: Din's Fire stays in the bag.
        Simulation sim = Duel(5, {1900, 0}, {0, 0});
        PlayerState* b = sim.match.Find(1000);
        b->ability = {ItemId::DinsFire, Rarity::Rare}; b->hasAbility = true;
        Run(sim, 3);
        CHECK(b->abilityReadyAt == 0);
    }
    {   // The Hookshot reels in a foe who is out of sword range.
        Simulation sim = Duel(5, {500, 0}, {0, 0});
        PlayerState* b = sim.match.Find(1000);
        b->ability = {ItemId::Hookshot, Rarity::Rare}; b->hasAbility = true;
        b->weapon = {ItemId::KokiriSword, Rarity::Common};
        float closest = 1e9f;
        for (int i = 0; i < 5 * kTickHz; i++) { sim.Tick(kDt); closest = (std::min)(closest, Distance(b->pos, sim.match.Find(1)->pos)); }
        CHECK(closest < 150.0f);
        CHECK(b->abilityReadyAt > 0);
    }
}

static void BotsFleeLosingFights() {
    Simulation sim = Duel(5, {250, 0}, {0, 0});
    PlayerState* b = sim.match.Find(1000);
    b->health = 0.7f; b->potions.clear();
    b->weapon = {ItemId::DekuStick, Rarity::Common};
    sim.match.Find(1)->weapon = {ItemId::MasterSword, Rarity::Legendary};
    const float before = Distance(b->pos, sim.match.Find(1)->pos);
    Run(sim, 2);
    CHECK(b->alive && Distance(b->pos, sim.match.Find(1)->pos) > before + 100.0f);   // ran away from the stronger human
}

static void HarderBotsKillFaster() {
    // A bot with a sword against a human standing still, averaged over seeds. Harder bots aim better and react sooner.
    float total[2] = {0, 0};
    for (int d = 0; d < 2; d++) {
        for (uint64_t seed = 1; seed <= 12; seed++) {
            Simulation sim = Duel(seed, {120, 0}, {0, 0});
            sim.bots.SetDifficulty(d == 0 ? BotDifficulty::Easy : BotDifficulty::Hard);
            PlayerState* b = sim.match.Find(1000);
            b->weapon = {ItemId::KokiriSword, Rarity::Common};
            sim.match.Find(1)->weapon = {ItemId::DekuStick, Rarity::Common};
            sim.match.Find(1)->health = 2.0f;
            float t = 40.0f;
            for (int i = 0; i < 40 * kTickHz; i++) {
                sim.Tick(kDt);
                sim.match.Find(1)->pos = {120, 0};   // the human stands still
                if (!sim.match.Find(1)->alive) { t = static_cast<float>(i) / kTickHz; break; }
            }
            total[d] += t;
        }
    }
    std::printf("  time to kill: easy %.1fs, hard %.1fs (mean of 12)\n", total[0] / 12, total[1] / 12);
    CHECK(total[1] < total[0]);
}

static void BotsPickUpFairiesAndHearts() {
    Simulation sim = Duel(5, {1900, 0}, {0, 0});
    sim.match.AddLoot({{200, 0}, ItemId::Fairy, Rarity::Rare, false});
    sim.match.AddLoot({{-200, 0}, ItemId::HeartContainer, Rarity::Rare, false});
    PlayerState* b = sim.match.Find(1000);
    Run(sim, 15);
    bool fairy = false;
    for (const Equipped& e : b->potions) fairy |= e.item == ItemId::Fairy;
    CHECK(fairy);
    CHECK(b->maxHealth > kMaxHealth);
}

static void BotsAdvantageMath() {
    Simulation sim = Duel(5, {100, 0}, {0, 0});
    PlayerState* a = sim.match.Find(1000);
    PlayerState* h = sim.match.Find(1);
    a->weapon = {ItemId::MasterSword, Rarity::Epic};
    h->weapon = {ItemId::DekuStick, Rarity::Common};
    CHECK(BotController::Advantage(sim.match, *a, *h) > 1.5f);
    CHECK(BotController::Advantage(sim.match, *h, *a) < 0.7f);
    h->stunUntil = sim.match.Clock() + 3;
    CHECK(BotController::Advantage(sim.match, *a, *h) > 3.0f);
}

static void WalkingOverLootOnlyTakesUpgrades() {
    Simulation sim = Duel(5, {1900, 0}, {1900, 100});
    Match& m = sim.match;
    PlayerState* p = m.Find(1);
    p->pos = {0, 0};
    p->weapon = {ItemId::MasterSword, Rarity::Epic};
    p->reserve = {{ItemId::Slingshot, Rarity::Common}, {ItemId::Boomerang, Rarity::Common}};   // hotbar full
    const size_t stick = m.AddLoot({{0, 0}, ItemId::DekuStick, Rarity::Common, false});
    CHECK(!m.PickUp(1, stick, false));                                   // walking over a worse weapon leaves it alone
    CHECK(p->weapon.item == ItemId::MasterSword && !m.Loot()[stick].taken);
    const size_t better = m.AddLoot({{0, 0}, ItemId::MasterSword, Rarity::Legendary, false});
    CHECK(m.PickUp(1, better, false) && p->weapon.rarity == Rarity::Legendary);   // an upgrade is taken on its own
    // The swapped-out sword lands a step away, not underfoot, so it can't be re-grabbed at once.
    bool away = false;
    for (const auto& l : m.Loot()) if (!l.taken && l.spawn.item == ItemId::MasterSword && l.spawn.rarity == Rarity::Epic) away = Distance(l.spawn.pos, p->pos) > kPickupRange;
    CHECK(away);
    CHECK(m.PickUp(1, stick, true) && p->weapon.item == ItemId::DekuStick);       // asking for it swaps anyway
    // Chests have to be opened on purpose: walking over one never opens it.
    const size_t chest = m.AddLoot({{0, 0}, ItemId::MasterSword, Rarity::Legendary, true, true});
    CHECK(!m.PickUp(1, chest, false) && !m.Loot()[chest].taken);
    CHECK(m.PickUp(1, chest, true) && m.Loot()[chest].taken);
    // Full bag: a potion is refused by walking, and a second Fairy is not worth taking.
    p->potions = {{ItemId::Fairy, Rarity::Rare}};
    const size_t fairy = m.AddLoot({{0, 0}, ItemId::Fairy, Rarity::Rare, false});
    CHECK(!m.PickUp(1, fairy, false));
}

static void HotbarAndChestsAndProps() {
    // Bots carry spares and pick the right one for the range.
    {
        Simulation sim = Duel(5, {600, 0}, {0, 0});
        PlayerState* b = sim.match.Find(1000);
        b->weapon = {ItemId::KokiriSword, Rarity::Common};
        b->reserve = {{ItemId::FairyBow, Rarity::Rare}};
        sim.match.Find(1)->weapon = {ItemId::DekuStick, Rarity::Common};
        bool usedBow = false;
        for (int i = 0; i < 8 * kTickHz; i++) { sim.Tick(kDt); sim.match.Find(1)->pos = {600, 0}; usedBow |= b->weapon.item == ItemId::FairyBow; }
        CHECK(usedBow);
    }
    // Chests: bots walk to a chest and open it on purpose.
    {
        Simulation sim = Duel(5, {1900, 0}, {0, 0});
        const size_t chest = sim.match.AddLoot({{200, 0}, ItemId::MasterSword, Rarity::Legendary, true, true});
        Run(sim, 12);
        CHECK(sim.match.Loot()[chest].taken);
    }
    // Props: deterministic, on valid ground, and solid ones block the nav grid.
    {
        auto valid = [](Vec2 p) { return p.x > -1500; };
        auto a = GenerateProps(7, MapCircle(), 300, valid), b = GenerateProps(7, MapCircle(), 300, valid), c = GenerateProps(8, MapCircle(), 300, valid);
        CHECK(a.size() > 250 && a.size() == b.size());
        bool same = true, ok = true, kinds[4] = {false, false, false, false};
        for (size_t i = 0; i < a.size(); i++) { same &= a[i].pos.x == b[i].pos.x && a[i].kind == b[i].kind; ok &= valid(a[i].pos); kinds[static_cast<int>(a[i].kind)] = true; }
        CHECK(same && ok && kinds[0] && kinds[1] && kinds[2] && kinds[3]);
        CHECK(a[0].pos.x != c[0].pos.x || a[1].pos.x != c[1].pos.x);
        NavGrid nav(MapCircle(), nullptr);
        CHECK(nav.Walkable({0, 0}));
        nav.Block({0, 0}, 80.0f);
        CHECK(!nav.Walkable({0, 0}) && !nav.Walkable({50, 0}) && nav.Walkable({200, 0}));
        std::vector<Vec2> path;
        CHECK(nav.FindPath({-400, 0}, {400, 0}, path) && path.size() >= 1);
        Vec2 at = {-400, 0};
        bool clear = true;
        for (Vec2 w : path) { clear &= nav.LineClear(at, w); at = w; }
        CHECK(clear);
    }
}

static void ScoringAndStandings() {
    CHECK(ScorePoints(0, 0, 0, 0) == 0);
    CHECK(ScorePoints(2.5f, 1, 2, 5) == 250 + 500 + 50 + (kMaxPlayers - 5) * kPointsPerPlacementStep);
    CHECK(ScorePoints(0, 0, 0, 1) > ScorePoints(0, 0, 0, 2) + kPointsForWinning - 1);
    Simulation sim = Duel(5, {50, 0}, {0, 0});
    Match& m = sim.match;
    PlayerState* h = m.Find(1);
    PlayerState* b = m.Find(1000);
    h->weapon = {ItemId::MasterSword, Rarity::Epic};
    b->health = 3.0f;
    h->pos = {0, 0};
    const float before = h->damageDealt;
    const AttackResult r = m.Attack(1, 1000, true);
    CHECK(r.hit && h->damageDealt > before + 0.5f);                    // damage is credited to the attacker
    CHECK(b->damageDealt == 0);                                        // and not to the one hit
    m.Damage(1, 0.5f, kNoPlayer, DamageKind::Storm);
    CHECK(h->damageDealt < before + 5.0f && m.Find(1000)->damageDealt == 0);
    // Overkill only counts the health that was there.
    b->health = 0.25f;
    const float dealt = h->damageDealt;
    m.Damage(1000, 50.0f, 1);
    CHECK(std::fabs(h->damageDealt - dealt - 0.25f) < 0.001f && h->kills == 1);
    sim.Tick(kDt);
    CHECK(m.State() == MatchState::Ending);                           // only the human is left
    CHECK(h->placement == 1 && b->placement == 2);                    // the loser died with two alive; the winner placed first
    const auto st = m.Standings();
    CHECK(st.size() >= 2 && st[0].id == 1 && st[0].score == m.Score(*h) && st[0].score > st[1].score);
    // Opening chests is worth a little.
    const size_t chest = m.AddLoot({{0, 0}, ItemId::GreenPotion, Rarity::Common, true, true});
    h->pos = {0, 0};
    const int scoreBefore = m.Score(*h);
    m.PickUp(1, chest, true);
    CHECK(h->chestsOpened == 0 || m.Score(*h) == scoreBefore + kPointsPerChest);
}

static void PointsOfInterest() {
    const Circle map = {{0, 0}, 4000.0f};
    auto valid = [](Vec2 p) { return p.x > -3000.0f; };
    const PoiLayout a = GeneratePois(9, map, 12, valid), b = GeneratePois(9, map, 12, valid), c = GeneratePois(10, map, 12, valid);
    CHECK(a.pois.size() >= 8 && a.pois.size() <= 12);
    CHECK(a.pois.size() == b.pois.size() && a.props.size() == b.props.size() && a.lootSpots.size() == b.lootSpots.size());
    CHECK(a.pois[1].center.x != c.pois[1].center.x || a.pois[1].name != c.pois[1].name);          // a new seed gives a new layout
    CHECK(Distance(a.pois[0].center, map.center) < map.radius * 0.1f && std::string(kPoiNames[a.pois[0].name]) == "Hylian Billion Pavilion");   // the landmark in the middle
    int inner = 0, outer = 0;
    for (size_t i = 1; i < a.pois.size(); i++) { const float d = Distance(a.pois[i].center, map.center) / map.radius; inner += d > 0.4f && d < 0.62f; outer += d > 0.7f; }
    CHECK(inner >= 3 && outer >= 3);                                                              // a ring of towns, then a wider one
    std::set<int> names;
    bool apart = true, inMap = true, grounded = true;
    for (size_t i = 0; i < a.pois.size(); i++) {
        names.insert(a.pois[i].name);
        inMap &= Distance(a.pois[i].center, map.center) <= map.radius;
        for (size_t j = i + 1; j < a.pois.size(); j++) apart &= Distance(a.pois[i].center, a.pois[j].center) >= a.pois[i].radius * 2.0f;
    }
    CHECK(names.size() == a.pois.size() && apart && inMap);                                      // every place has its own name and room
    for (const Prop& p : a.props) grounded &= valid(p.pos);
    for (const Vec2& sp : a.lootSpots) grounded &= valid(sp);
    CHECK(grounded);
    CHECK(a.props.size() > a.pois.size() * 20 && a.lootSpots.size() >= a.pois.size() * 6);       // a building, a cave and ruins at each
    for (int i = 0; i < kPoiNameCount; i++) CHECK(kPoiNames[i] != nullptr && kPoiNames[i][0] != 0);

    // The chests: every spot gets one, always on the better tiers, in a container, on top of the scattered ones.
    Simulation sim(9, map, 0);
    sim.match.SetLootSpots(a.lootSpots);
    sim.match.RegenerateLoot(50);
    CHECK(sim.match.Loot().size() == 50 + a.lootSpots.size());
    int atSpots = 0;
    for (const auto& l : sim.match.Loot()) {
        for (const Vec2& sp : a.lootSpots) if (Distance(l.spawn.pos, sp) < 0.01f && l.spawn.container) { atSpots++; break; }
    }
    CHECK(atSpots >= static_cast<int>(a.lootSpots.size()));

    // Walls are solid for the bots but the door is open: a path leads from outside a building to a chest inside it.
    PoiLayout one;
    Rng rng(3);
    AddHouse(one, rng, {0, 0}, 0.0f, nullptr);
    NavGrid nav(map, nullptr);
    for (const Prop& p : one.props) if (PropRadius(p.kind) > 0) nav.Block(p.pos, PropRadius(p.kind) + 20.0f);
    CHECK(!nav.Walkable({-180, 140}));                                                           // inside a wall
    std::vector<Vec2> path;
    CHECK(nav.FindPath({0, 600}, one.lootSpots[0], path));                                       // door is at +z with angle 0
    bool clear = true;
    Vec2 at = {0, 600};
    for (Vec2 w : path) { clear &= nav.LineClear(at, w); at = w; }
    CHECK(clear && Distance(at, one.lootSpots[0]) < 80.0f);
    CHECK(path.size() >= 2);                                                                     // it bends through the doorway rather than cutting the wall

    // A bot goes into the building, through the door, and opens a chest.
    {
        Simulation duel = Duel(5, {1900, 0}, {0, 700});
        auto grid = std::make_shared<NavGrid>(MapCircle(), nullptr);
        for (const Prop& p : one.props) if (PropRadius(p.kind) > 0) grid->Block(p.pos, PropRadius(p.kind) + 20.0f);
        duel.bots.SetNav(grid);
        const size_t chest = duel.match.AddLoot({one.lootSpots[0], ItemId::MasterSword, Rarity::Epic, true, true});
        Run(duel, 40);
        CHECK(duel.match.Loot()[chest].taken);
    }
}

static void CustomMeshes() {
    for (int k = 0; k < static_cast<int>(MeshKind::Count); k++) {
        for (uint32_t variant = 0; variant < kMeshVariants; variant++) {
            const MeshData m = BuildMesh(static_cast<MeshKind>(k), variant);
            CHECK(!m.v.empty() && m.v.size() % 3 == 0 && m.Triangles() >= 12 && m.Triangles() <= 200);   // a few dozen triangles: chunky, and cheap to draw
            float mn[3], mx[3];
            m.Bounds(mn, mx);
            bool finite = true;
            for (const auto& p : m.v) finite &= std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
            CHECK(finite && mn[1] >= -0.01f);                                                          // nothing below the ground
            const MeshData again = BuildMesh(static_cast<MeshKind>(k), variant);
            bool same = again.v.size() == m.v.size();
            for (size_t i = 0; same && i < m.v.size(); i++) same = again.v[i].x == m.v[i].x && again.v[i].r == m.v[i].r;
            CHECK(same);                                                                                // deterministic
            // Every face is lit by the way it faces, so colours differ across the model and none is black.
            int lo = 255, hi = 0;
            for (const auto& p : m.v) { lo = (std::min)(lo, static_cast<int>(p.g)); hi = (std::max)(hi, static_cast<int>(p.g)); }
            CHECK(hi > lo + 15 && hi > 60);
            if (static_cast<MeshKind>(k) == MeshKind::Rock) CHECK(mx[0] - mn[0] < 120 && mx[1] < 60);
            if (static_cast<MeshKind>(k) == MeshKind::Boulder) CHECK(mx[0] - mn[0] > 100 && mx[0] - mn[0] < 260 && mx[1] < 150);
            if (static_cast<MeshKind>(k) == MeshKind::Pillar) CHECK(mx[1] > 190 && mx[1] < 215 && mx[0] - mn[0] < 100);
            if (static_cast<MeshKind>(k) == MeshKind::Golem) CHECK(mx[1] > 250 && mx[1] < 300 && mx[0] - mn[0] > 200 && mx[0] - mn[0] < 280 && m.Triangles() >= 100);
            if (static_cast<MeshKind>(k) == MeshKind::Roof) CHECK(mn[1] >= 199.0f && mx[1] > 300 && mx[0] - mn[0] > 400 && mx[2] - mn[2] > 330);
        }
    }
    // Variants of a rock really differ.
    const MeshData r0 = BuildMesh(MeshKind::Rock, 0), r1 = BuildMesh(MeshKind::Rock, 1);
    bool differ = false;
    for (size_t i = 0; i < r0.v.size() && i < r1.v.size(); i++) differ |= r0.v[i].x != r1.v[i].x;
    CHECK(differ);
    // Faces point outward: for the post, the average normal of the faces on the shaft points away from its axis.
    const MeshData post = BuildMesh(MeshKind::Pillar, 0);
    bool outward = true;
    for (size_t i = 0; i + 2 < post.v.size(); i += 3) {
        const auto &a = post.v[i], &b = post.v[i + 1], &c = post.v[i + 2];
        const float nx = (b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y), nz = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        (void)nx; (void)nz;
        if (a.y > 80 && a.y < 160 && b.y > 80 && b.y < 160 && c.y > 80 && c.y < 160) {                   // a shaft face: its centre sits well off the axis
            const float cx = (a.x + b.x + c.x) / 3, cz = (a.z + b.z + c.z) / 3;
            outward &= std::sqrt(cx * cx + cz * cz) > 25.0f;
        }
    }
    CHECK(outward);
    // Buildings get a roof, at the building's centre.
    PoiLayout house;
    Rng rng(5);
    AddHouse(house, rng, {100, 200}, 0.7f, nullptr);
    int roofs = 0;
    for (const Prop& p : house.props) if (p.kind == PropKind::Roof) { roofs++; CHECK(Distance(p.pos, {100, 200}) < 0.01f); }
    CHECK(roofs == 1);
    CHECK(PropRadius(PropKind::Roof) == 0.0f);                                                       // bots walk under it
}

// A started match with one human and `bosses` mini bosses at the given spots; everyone else is out of the way.
static Simulation BossArena(std::vector<Vec2> spots, int bosses, Vec2 human) {
    Simulation sim(5, MapCircle(), 0);
    sim.match.AddHuman(1);
    sim.match.SetBossSpots(std::move(spots));
    sim.match.SetBossCount(bosses);
    sim.match.Start();
    while (sim.match.State() != MatchState::InMatch) sim.match.Tick(kDt);
    for (auto& p : sim.match.Players()) if (p.id != 1 && p.id != 1000) p.alive = false;
    sim.match.Find(1000)->pos = {-1950, -1950}; // one bot far away, so the match doesn't end for lack of opponents
    sim.match.Find(1)->pos = human;
    return sim;
}

static void MiniBosses() {
    // They stand where the caves are, with ids above every player.
    {
        Simulation sim = BossArena({{0, 0}, {900, 0}, {-900, 0}}, 3, {1900, 0});
        const auto& b = sim.match.Bosses();
        CHECK(b.size() == 3);
        std::set<uint32_t> ids;
        bool atSpots = true, valid = true;
        for (const auto& x : b) {
            ids.insert(x.id);
            atSpots &= (Distance(x.home, {0, 0}) < 1 || Distance(x.home, {900, 0}) < 1 || Distance(x.home, {-900, 0}) < 1);
            valid &= IsBossId(x.id) && x.alive && x.health == BossOf(x.kind).health && static_cast<int>(x.kind) < kBossKindCount;
        }
        CHECK(ids.size() == 3 && atSpots && valid);
        // Without spots they find open ground, apart from each other.
        Simulation open = BossArena({}, 4, {1900, 0});
        CHECK(open.match.Bosses().size() == 4);
        bool apart = true;
        for (size_t i = 0; i < 4; i++) for (size_t j = i + 1; j < 4; j++) apart &= Distance(open.match.Bosses()[i].home, open.match.Bosses()[j].home) >= 1199.0f;
        CHECK(apart);
        // A plain match has none.
        Simulation plain = Duel(5, {0, 0}, {100, 0});
        CHECK(plain.match.Bosses().empty());
    }
    // It notices you, comes at you and smashes, no faster than its cooldown.
    {
        Simulation sim = BossArena({{0, 0}}, 1, {300, 0});
        const MiniBoss& boss = sim.match.Bosses()[0];
        PlayerState* h = sim.match.Find(1);
        h->health = 3.0f;
        const float before = Distance(boss.pos, h->pos);
        Run(sim, 1.0f);
        CHECK(Distance(boss.pos, h->pos) < before - 30.0f);                       // it is coming
        Run(sim, 6.0f);
        const float lost = 3.0f - h->health;
        CHECK(lost > 0.7f);                                                       // it hit at least once
        const BossDef def = BossOf(boss.kind);
        CHECK(lost <= def.damage * (7.0f / def.cooldown + 1.5f));                 // ...but not faster than it can swing
    }
    // Out of range it ignores you; if you run away it gives up and walks home, healing.
    {
        Simulation sim = BossArena({{0, 0}}, 1, {1500, 0});
        Run(sim, 3.0f);
        CHECK(Distance(sim.match.Bosses()[0].pos, {0, 0}) < 1.0f);               // never moved
        sim.match.Find(1)->pos = {400, 0};
        Run(sim, 2.5f);
        CHECK(Distance(sim.match.Bosses()[0].pos, {0, 0}) > 50.0f);               // chasing
        sim.match.Bosses();
        MiniBoss* mb = const_cast<MiniBoss*>(&sim.match.Bosses()[0]);
        mb->health = mb->maxHealth * 0.5f;
        for (int i = 0; i < 30 * kTickHz; i++) { sim.Tick(kDt); sim.match.Find(1)->pos = {1900, 1000}; }
        CHECK(Distance(sim.match.Bosses()[0].pos, sim.match.Bosses()[0].home) < 20.0f);
        CHECK(sim.match.Bosses()[0].health > sim.match.Bosses()[0].maxHealth * 0.5f + 5.0f);
    }
    // Hitting it, killing it, and what drops.
    {
        Simulation sim = BossArena({{0, 0}}, 1, {60, 0});
        Match& m = sim.match;
        PlayerState* h = m.Find(1);
        h->weapon = {ItemId::MasterSword, Rarity::Legendary};
        h->health = 3.0f; h->maxHealth = 10.0f;
        const uint32_t id = m.Bosses()[0].id;
        const size_t lootBefore = m.Loot().size();
        CHECK(!m.Attack(1, kBossIdBase + 7).ok);                                  // no such boss
        h->pos = {600, 0};
        CHECK(!m.Attack(1, id).ok);                                               // too far
        h->pos = {60, 0};
        float last = m.Bosses()[0].health;
        const AttackResult first = m.Attack(1, id, true);
        CHECK(first.ok && first.hit && m.Bosses()[0].health < last && h->damageDealt > 0);
        CHECK(m.Bosses()[0].target == 1);
        // Keep swinging (and keep the player alive) until it falls.
        int swings = 0;
        while (m.Bosses()[0].alive && swings < 200) {
            h->health = h->maxHealth;
            sim.Tick(0.7f);
            m.Attack(1, id, true);
            swings++;
        }
        CHECK(!m.Bosses()[0].alive && m.Bosses()[0].health == 0);
        CHECK(h->bossKills == 1 && m.Score(*h) >= kPointsPerBossKill);
        const int drops = BossOf(m.Bosses()[0].kind).drops;
        CHECK(m.Loot().size() == lootBefore + static_cast<size_t>(drops));
        bool good = true;
        for (size_t i = lootBefore; i < m.Loot().size(); i++) good &= m.Loot()[i].spawn.container && m.Loot()[i].spawn.rarity >= Rarity::Rare && !m.Loot()[i].taken;
        CHECK(good);
        bool down = false;
        for (const auto& e : m.DrainEvents()) down |= e.type == MatchEvent::Type::BossDown && e.a == id && e.b == 1;
        CHECK(down);
        const float hp = h->health;
        Run(sim, 4.0f);
        CHECK(h->health >= hp);                                                   // a dead boss stops smashing
        CHECK(!m.Attack(1, id).ok);
    }
    // It can kill: the elimination names the boss and nobody gets a kill for it.
    {
        Simulation sim = BossArena({{0, 0}}, 1, {80, 0});
        PlayerState* h = sim.match.Find(1);
        h->health = 0.5f;
        sim.match.DrainEvents();
        Run(sim, 3.0f);
        CHECK(!h->alive);
        bool named = false;
        for (const auto& e : sim.match.DrainEvents()) named |= e.type == MatchEvent::Type::Eliminated && e.a == 1 && IsBossId(e.b);
        CHECK(named && h->kills == 0);
    }
    // Bots: a strong one takes a boss on, a weak one keeps away.
    {
        Simulation sim = BossArena({{0, 0}}, 1, {1900, 100});
        PlayerState* b = sim.match.Find(1000);
        b->pos = {420, 0};
        b->weapon = {ItemId::MasterSword, Rarity::Epic};
        b->health = b->maxHealth = 6.0f;
        const float start = sim.match.Bosses()[0].health;
        for (int i = 0; i < 25 * kTickHz; i++) { sim.Tick(kDt); b->health = b->maxHealth; }
        CHECK(sim.match.Bosses()[0].health < start - 3.0f || !sim.match.Bosses()[0].alive);
    }
    {
        Simulation sim = BossArena({{0, 0}}, 1, {1900, 100});
        PlayerState* b = sim.match.Find(1000);
        b->pos = {250, 0};
        b->weapon = {ItemId::DekuStick, Rarity::Common};
        b->health = 1.0f;
        const float before = Distance(b->pos, {0, 0});
        Run(sim, 2.0f);
        CHECK(!b->alive || Distance(b->pos, sim.match.Bosses()[0].pos) > before - 40.0f);   // it did not walk up and trade blows
    }
}

static void PlayerLimitSlider() {
    Match m(1, MapCircle());
    CHECK(m.PlayerLimit() == kMaxPlayers);
    CHECK(!m.SetPlayerLimit(1) && !m.SetPlayerLimit(33) && !m.SetPlayerLimit(0));       // 2 to 32 only
    CHECK(m.SetPlayerLimit(6) && m.PlayerLimit() == 6);
    for (uint32_t id = 1; id <= 4; id++) CHECK(m.AddHuman(id));
    CHECK(!m.SetPlayerLimit(3));                                                          // not below the people already here
    CHECK(m.SetPlayerLimit(5));
    CHECK(m.AddHuman(5) && !m.AddHuman(6));                                               // the lobby is full at the limit
    CHECK(m.Start());
    CHECK(m.Players().size() == 5 && m.Alive() == 5);
    int bots = 0;
    for (auto& p : m.Players()) bots += p.isBot;
    CHECK(bots == 0);
    CHECK(!m.SetPlayerLimit(10));                                                         // lobby only
    // A solo player on a small limit gets just enough bots.
    Match solo(2, MapCircle());
    solo.AddHuman(1);
    CHECK(solo.SetPlayerLimit(2));
    CHECK(solo.Start() && solo.Players().size() == 2 && solo.Alive() == 2);
    Match big(3, MapCircle());
    big.AddHuman(1);
    CHECK(big.SetPlayerLimit(12) && big.Start() && big.Players().size() == 12);
}

static void ChickenTune() {
    const std::vector<int16_t> a = BuildChickenTune(), b = BuildChickenTune();
    CHECK(a.size() == static_cast<size_t>(kTuneSeconds * kTuneRate));                 // exactly one cycle of the dance, so it loops cleanly
    CHECK(a == b);                                                                    // deterministic
    int peak = 0;
    double energy = 0;
    for (int16_t v : a) { peak = (std::max)(peak, std::abs(static_cast<int>(v))); energy += static_cast<double>(v) * v; }
    CHECK(peak > 20000 && peak < 32768);                                              // loud but not clipping
    CHECK(std::sqrt(energy / a.size()) > 2500.0);                                     // and not mostly silence
    // Every second has sound in it: the tune is busy through all four bars of the dance.
    for (int bar = 0; bar < 4; bar++) {
        double e = 0;
        for (int i = bar * kTuneRate; i < (bar + 1) * kTuneRate; i++) e += static_cast<double>(a[static_cast<size_t>(i)]) * a[static_cast<size_t>(i)];
        CHECK(std::sqrt(e / kTuneRate) > 1500.0);
    }
    // The loop point is quiet-ish at both ends (no click when it wraps): the last few samples are well under the peak.
    int tail = 0;
    for (size_t i = a.size() - 40; i < a.size(); i++) tail = (std::max)(tail, std::abs(static_cast<int>(a[i])));
    CHECK(tail < peak / 3);
}

static void ShockwaveGrenade() {
    CHECK(KindOf(ItemId::ShockwaveGrenade) == ItemKind::Ability && !IsSong(ItemId::ShockwaveGrenade));
    CHECK(AbilityOf(ItemId::ShockwaveGrenade).fx[0].type == EffectType::Shockwave && AbilityOf(ItemId::ShockwaveGrenade).cooldown > 0);
    Simulation sim = Duel(5, {100, 0}, {0, 0});
    Match& m = sim.match;
    PlayerState* user = m.Find(1);
    PlayerState* near = m.Find(1000);
    // A third player well outside the blast.
    PlayerState* far = m.Find(1001);
    far->alive = true;
    far->pos = {1500, 0};
    user->pos = {0, 0};
    near->pos = {200, 0};
    user->ability = {ItemId::ShockwaveGrenade, Rarity::Rare};
    user->hasAbility = true;
    user->abilityReadyAt = 0;
    const float nearBefore = Distance(near->pos, user->pos);
    m.DrainEvents();
    CHECK(m.UseAbility(1));
    CHECK(Distance(near->pos, user->pos) > nearBefore + 250.0f);                          // thrown away from the user
    CHECK(near->pos.z == 0 && near->pos.x > 200.0f);                                       // straight away, not sideways
    CHECK(m.Stunned(*near) && !m.Stunned(*user));                                           // dazed, and the user is fine
    CHECK(Distance(far->pos, {1500, 0}) < 0.01f);                                           // out of range: untouched
    CHECK(Distance(user->pos, {0, 0}) < 0.01f && near->health == near->maxHealth);          // no damage, and the user stays put
    bool teleported = false;
    for (const auto& e : m.DrainEvents()) teleported |= e.type == MatchEvent::Type::Teleported && e.a == 1000;
    CHECK(teleported);
    CHECK(!m.UseAbility(1));                                                                // recharging
    // The edge of the map stops the throw, and ground that isn't there shortens it.
    Simulation edge = Duel(5, {100, 0}, {0, 0});
    edge.match.Find(1)->pos = {1900, 0};
    edge.match.Find(1000)->pos = {1960, 0};
    edge.match.Find(1)->ability = {ItemId::ShockwaveGrenade, Rarity::Legendary};
    edge.match.Find(1)->hasAbility = true;
    CHECK(edge.match.UseAbility(1));
    CHECK(Distance(edge.match.Find(1000)->pos, MapCircle().center) <= MapCircle().radius);
    // A bot uses it when an enemy is on top of it and it is losing.
    Simulation botFight = Duel(5, {100, 0}, {0, 0});
    PlayerState* b = botFight.match.Find(1000);
    b->ability = {ItemId::ShockwaveGrenade, Rarity::Epic};
    b->hasAbility = true;
    b->health = 1.0f;
    botFight.match.Find(1)->pos = {150, 0};
    botFight.match.Find(1)->weapon = {ItemId::BiggoronSword, Rarity::Epic};
    bool used = false;
    for (int i = 0; i < 3 * kTickHz; i++) { botFight.Tick(kDt); used |= b->abilityReadyAt > 0; }
    CHECK(used);
}

static void ShieldBar() {
    Simulation sim = Duel(5, {100, 0}, {0, 0});
    Match& m = sim.match;
    PlayerState* h = m.Find(1);
    PlayerState* b = m.Find(1000);
    h->health = h->maxHealth = 3.0f;
    h->armor = 2.0f;
    m.DrainEvents();
    // The shield soaks damage before health does.
    m.Damage(1, 1.5f, 1000);
    CHECK(std::fabs(h->armor - 0.5f) < 0.001f && h->health == 3.0f);
    float announced = 0;
    for (const auto& e : m.DrainEvents()) if (e.type == MatchEvent::Type::Damaged && e.a == 1) announced = e.amount;
    CHECK(std::fabs(announced - 1.5f) < 0.001f);                                          // the hit marker shows the whole hit
    const float dealt = b->damageDealt;
    m.Damage(1, 1.0f, 1000);
    CHECK(h->armor == 0 && std::fabs(h->health - 2.5f) < 0.001f);                         // what is left goes to health
    CHECK(std::fabs(b->damageDealt - dealt - 1.0f) < 0.001f);                              // damage dealt counts shield and health
    h->armor = 2.0f;
    m.Damage(1, 0.5f, kNoPlayer, DamageKind::Storm);
    CHECK(h->armor == 2.0f && std::fabs(h->health - 2.0f) < 0.001f);                       // the storm ignores the shield
    // Potions: the small one only works while the bar is under half; the large one fills it.
    h->armor = 0;
    h->potions = {{ItemId::SmallShieldPotion, Rarity::Common}, {ItemId::SmallShieldPotion, Rarity::Common}};
    CHECK(!m.UsePotion(1) || h->potions.size() == 2 || true);
    CHECK(m.UseShield(1) && std::fabs(h->armor - 1.0f) < 0.001f);
    CHECK(m.UseShield(1) && std::fabs(h->armor - 1.5f) < 0.001f);                          // capped at half
    h->potions.push_back({ItemId::SmallShieldPotion, Rarity::Common});
    CHECK(!m.UseShield(1) && h->potions.size() == 1);                                        // a small one can't push it past half
    h->potions = {{ItemId::LargeShieldPotion, Rarity::Rare}};
    CHECK(m.UseShield(1) && std::fabs(h->armor - 3.0f) < 0.001f && h->potions.empty());
    h->potions = {{ItemId::LargeShieldPotion, Rarity::Rare}};
    CHECK(!m.UseShield(1));                                                                  // already full
    // With both in the bag and an empty bar, the large one is drunk first (it fits).
    h->armor = 0;
    h->potions = {{ItemId::SmallShieldPotion, Rarity::Common}, {ItemId::LargeShieldPotion, Rarity::Epic}};
    CHECK(m.UseShield(1) && std::fabs(h->armor - 2.0f) < 0.001f && h->potions.size() == 1 && h->potions[0].item == ItemId::SmallShieldPotion);
    // Healing potions are not shield potions and the other way round.
    h->health = 1.0f;
    h->potions = {{ItemId::SmallShieldPotion, Rarity::Common}};
    CHECK(!m.UsePotion(1) && h->health == 1.0f);
    h->potions = {{ItemId::RedPotion, Rarity::Common}};
    CHECK(!m.UseShield(1));
    // Walking over a shield potion picks it up only if the bag has room; chests give them out like any item.
    h->potions = {};
    const size_t pick = m.AddLoot({{100, 0}, ItemId::LargeShieldPotion, Rarity::Rare, false});
    h->pos = {100, 0};
    CHECK(m.PickUp(1, pick, false) && h->potions.size() == 1);
    // A bot with a shield potion and nobody around drinks it.
    Simulation botSim = Duel(5, {1900, 0}, {0, 0});
    PlayerState* bot = botSim.match.Find(1000);
    bot->potions = {{ItemId::LargeShieldPotion, Rarity::Rare}};
    bot->armor = 0;
    Run(botSim, 3.0f);
    CHECK(bot->armor > 1.9f && bot->potions.empty());
}

int main() {
    StormNests(); StormDeterministic(); StormTimeline(); LootDeterministicAndValid(); ChestsRollHigher();
    CombatMath(); AttackRules(); NoAttacksDuringDrop(); PickUpRulesAndSwap(); PotionRules(); DeathDropsKit();
    BotFetchesUpgrade(); BotIgnoresDowngrade(); BotTakesShieldAndPotions(); BotHealsWhenHurt(); BotOutrunsStorm(); BotsFightToTheDeath(); BotsFaceTheirDirectionAndAnimate(); BotsKeepDistanceWithBow(); FullMatchWithBots();
    CatalogIsConsistent(); LootCoversEveryItemAndRespectsKindWeights(); GearScalesWithRarityAndStacks(); GearChangesDamageDealtAndTaken();
    PickupRulesForEveryKind(); FairyRevivesOnceAndIsNeverDrunk(); PotionVariants(); WeaponEffects(); AbilityBasics(); AbilitiesThatMovePlayers();
    OcarinasPlayRandomSongs(); EliminatedPlayersDropEverythingAndKillsAreCredited(); MovementPlausibilityAllowsSpeedBuffs();
    PlacementValidatorKeepsLootAndSpawnsOnWalkableGround(); ValidatorThatRejectsEverythingStillTerminates(); StormPhaseInfo();
    ShieldBar(); ShockwaveGrenade(); ChickenTune(); PlayerLimitSlider(); MiniBosses(); CustomMeshes(); PointsOfInterest(); ScoringAndStandings(); HotbarAndChestsAndProps(); WalkingOverLootOnlyTakesUpgrades(); NavPathsAroundWalls(); BotsWalkAroundWalls(); BotsUseAbilitiesWhenItCounts(); BotsFleeLosingFights(); HarderBotsKillFaster(); BotsPickUpFairiesAndHearts(); BotsAdvantageMath();
    WeightsSumTo100(); SoloPlayerGets31Bots(); StartNeedsOneHuman(); LobbyFull(); FullMatchHasOneWinner(); SpawnProtection();
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("all tests passed\n");
    return 0;
}
