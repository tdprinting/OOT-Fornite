#include "../match.h"
#include "../sim.h"
#include "../nav.h"
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
                CHECK(p.heal > 0 || p.cleanse || p.damageTaken < 1 || p.revive);
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
    CHECK(perKind[static_cast<int>(ItemKind::Consumable)] == 9 && perKind[static_cast<int>(ItemKind::Instant)] == 4);
    CHECK(perKind[static_cast<int>(ItemKind::Ability)] == 21 && perKind[static_cast<int>(ItemKind::Gear)] == 31);
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

int main() {
    StormNests(); StormDeterministic(); StormTimeline(); LootDeterministicAndValid(); ChestsRollHigher();
    CombatMath(); AttackRules(); NoAttacksDuringDrop(); PickUpRulesAndSwap(); PotionRules(); DeathDropsKit();
    BotFetchesUpgrade(); BotIgnoresDowngrade(); BotTakesShieldAndPotions(); BotHealsWhenHurt(); BotOutrunsStorm(); BotsFightToTheDeath(); BotsFaceTheirDirectionAndAnimate(); BotsKeepDistanceWithBow(); FullMatchWithBots();
    CatalogIsConsistent(); LootCoversEveryItemAndRespectsKindWeights(); GearScalesWithRarityAndStacks(); GearChangesDamageDealtAndTaken();
    PickupRulesForEveryKind(); FairyRevivesOnceAndIsNeverDrunk(); PotionVariants(); WeaponEffects(); AbilityBasics(); AbilitiesThatMovePlayers();
    OcarinasPlayRandomSongs(); EliminatedPlayersDropEverythingAndKillsAreCredited(); MovementPlausibilityAllowsSpeedBuffs();
    PlacementValidatorKeepsLootAndSpawnsOnWalkableGround(); ValidatorThatRejectsEverythingStillTerminates(); StormPhaseInfo();
    HotbarAndChestsAndProps(); WalkingOverLootOnlyTakesUpgrades(); NavPathsAroundWalls(); BotsWalkAroundWalls(); BotsUseAbilitiesWhenItCounts(); BotsFleeLosingFights(); HarderBotsKillFaster(); BotsPickUpFairiesAndHearts(); BotsAdvantageMath();
    WeightsSumTo100(); SoloPlayerGets31Bots(); StartNeedsOneHuman(); LobbyFull(); FullMatchHasOneWinner(); SpawnProtection();
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("all tests passed\n");
    return 0;
}
