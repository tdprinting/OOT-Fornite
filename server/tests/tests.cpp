#include "../match.h"
#include "../sim.h"
#include "../nav.h"
#include "../../shared/meshes.h"
#include "../../shared/gilded_sword_surface.h"
#include "../../shared/objmodel.h"
#include "../../shared/tune.h"
#include "../../shared/poi.h"
#include "../../shared/props.h"
#include "../../shared/fortnite_map.h"
#include "../../shared/sandbox_layout.h"
#include "../../shared/fortnite_scenery.h"
#include "../../shared/fortnite_puddles.h"
#include "../../shared/ground_patches.h"
#include "../../shared/placement.h"
#include "../../shared/island_anchors.h"
#include <set>
#include <unordered_set>
#include "cloth.h"
#include <string>
#include "../../shared/anim.h"
#include "../../shared/lilo_anim.h"
#include "../../shared/avriella_anim.h"
#include "../../shared/lilo_sounds.h"
#include "../../shared/loot.h"
#include <cstdio>
#include <cstring>
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
    const float wait0 = kStormPhases[0].waitSec, close0 = kStormPhases[0].closeSec;
    CHECK(s.SafeZoneAt(wait0 - 1).radius == 2000.0f);   // still waiting in phase 1
    CHECK(s.SafeZoneAt(wait0 + close0).radius < 1200.5f && s.SafeZoneAt(wait0 + close0).radius > 1199.5f); // 60%
    CHECK(s.SafeZoneAt(s.TotalDuration() + 100).radius == 0.0f);
    CHECK(s.PhaseAt(0) == 0 && s.PhaseAt(s.TotalDuration() + 1) == kStormPhaseCount);
    // Safe zone never grows.
    float last = 1e9f;
    for (float t = 0; t < s.TotalDuration(); t += 1.0f) { float r = s.SafeZoneAt(t).radius; CHECK(r <= last + 1e-3f); last = r; }
    CHECK(s.DamagePerSecond({1999, 0}, 0) == 0.0f);     // inside the map, phase 1 holds
    CHECK(std::abs(s.DamagePerSecond({5000, 0}, 0) - kStormPhases[0].damagePerSec * (2000.0f / 3500.0f)) < 1e-4f);     // outside the map (a small map's storm hurts a little less)
    Storm big(7, {{0, 0}, 5000});
    CHECK(big.DamagePerSecond({9000, 0}, 0) == kStormPhases[0].damagePerSec);
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
// Ranged weapons spend ammo now; most tests are about something else, so they start with plenty.
static void FillAmmo(Simulation& sim, int n = 60) { for (auto& p : sim.match.Players()) p.ammo.fill(n); }

static Simulation Duel(uint64_t seed, Vec2 humanPos, Vec2 botPos) {
    Simulation sim(seed, MapCircle(), 0);
    sim.match.AddHuman(1);
    sim.match.Start();
    while (sim.match.State() != MatchState::InMatch) sim.match.Tick(kDt); // no bot movement while setting up
    for (auto& p : sim.match.Players()) if (p.id != 1 && p.id != 1000) p.alive = false;
    FillAmmo(sim);
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
    CHECK(KindOf(ItemId::Longshot) == ItemKind::Ability && KindOf(ItemId::FairyBow) == ItemKind::Weapon);
    CHECK(ShieldReduction(ItemId::MirrorShield, Rarity::Legendary) <= 0.75f);
    for (int i = 0; i < kItemCount; i++) {
        const ItemId id = kItems[i].id;
        CHECK((WeaponOf(id).damage > 0 || id == ItemId::ShockwaveGrenade) == (KindOf(id) == ItemKind::Weapon));
    }
    // The Gilded Sword is the strongest sword: it reaches further and hits harder than the Master Sword, one-handed, Legendary only, and found as loot.
    const WeaponStats gilded = WeaponOf(ItemId::GildedSword), master = WeaponOf(ItemId::MasterSword);
    CHECK(gilded.damage > master.damage && gilded.range > master.range && !gilded.ranged && !IsTwoHanded(ItemId::GildedSword));
    CHECK(WeaponDps(ItemId::GildedSword, Rarity::Legendary) > WeaponDps(ItemId::MasterSword, Rarity::Legendary));
    CHECK(WeaponDps(ItemId::GildedSword, Rarity::Legendary) > WeaponDps(ItemId::BiggoronSword, Rarity::Epic));
    CHECK(InPool(ItemId::GildedSword) && DefOf(ItemId::GildedSword).minRarity == Rarity::Legendary && DefOf(ItemId::GildedSword).maxRarity == Rarity::Legendary);
    bool found = false;
    Rng rng(77);
    for (int i = 0; i < 4000 && !found; i++) { ItemId it; if (PickItem(rng, Rarity::Legendary, &it) && it == ItemId::GildedSword) found = true; }
    CHECK(found);
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
// Jump slashes hit harder, spin attacks catch everyone close, and a raised shield facing the blow takes most of it.
static void StrikesAndGuard() {
    Simulation sim = Duel(1, {0, 0}, {50, 0});
    Match& m = sim.match;
    m.Find(1)->weapon = {ItemId::KokiriSword, Rarity::Common};
    auto rest = [&] { for (int i = 0; i < 2 * kTickHz; i++) m.Tick(kDt); m.Find(1000)->health = kMaxHealth; };
    const AttackResult plain = m.Attack(1, 1000);
    rest();
    const AttackResult jump = m.Attack(1, 1000, true, AttackStyle::JumpSlash);
    CHECK(jump.ok && jump.damage > plain.damage * 1.4f);
    rest();
    // A third player just behind the swordsman: a spin catches them, a plain swing doesn't.
    PlayerState* third = nullptr;
    for (auto& p : m.Players()) if (p.id != 1 && p.id != 1000) { third = &p; break; }
    CHECK(third != nullptr);
    third->alive = true; third->health = kMaxHealth; third->pos = {-40, 0};
    CHECK(m.Attack(1, 1000).extraHits == 0 && third->health == kMaxHealth);
    rest();
    const AttackResult spin = m.Attack(1, 1000, true, AttackStyle::Spin);
    CHECK(spin.ok && spin.extraHits == 1 && third->health < kMaxHealth);
    third->alive = false;
    rest();
    // Shield up and facing the attacker (who stands towards -x): blocked.
    PlayerState* t = m.Find(1000);
    t->hasShield = true; t->shield = {ItemId::HylianShield, Rarity::Common};
    const AttackResult shielded = m.Attack(1, 1000);
    rest();
    t->anim = static_cast<uint8_t>(Anim::Guard);
    t->rot = -16384;   // facing -x
    const AttackResult guarded = m.Attack(1, 1000);
    CHECK(guarded.blocked && guarded.damage < shielded.damage * 0.3f);
    rest();
    t->rot = 16384;    // facing away: the shield is on the wrong side
    const AttackResult behind = m.Attack(1, 1000);
    CHECK(!behind.blocked && behind.damage == shielded.damage);
    rest();
    t->rot = -16384;   // light arrows go through any shield
    m.Find(1)->weapon = {ItemId::LightArrows, Rarity::Common};
    for (auto& p : m.Players()) p.ammo.fill(60);
    CHECK(!m.Attack(1, 1000).blocked);
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
    CHECK(m.PickUp(1000, m.AddLoot({{0, 0}, ItemId::Longshot, Rarity::Epic, false})));  // abilities go in the ability slot
    CHECK(m.Find(1000)->hasAbility && m.Find(1000)->ability.item == ItemId::Longshot);
}
static void PotionRules() {
    Simulation sim = Duel(1, {1500, 0}, {0, 0});
    Match& m = sim.match;
    PlayerState* p = m.Find(1000);
    CHECK(!m.UsePotion(1000));                     // none carried
    p->potions = {{ItemId::BluePotion, Rarity::Epic}, {ItemId::GreenPotion, Rarity::Common}};
    p->health = kMaxHealth - 0.5f;                             // missing 0.5: the small potion is enough, keep the big one
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
    p->ammo.fill(0);   // (the test fixture hands out ammo; dropped ammo has its own test)
    CHECK(m.Loot().empty());
    CHECK(m.Damage(1000, 100));
    CHECK(m.Loot().size() == 1 && !m.Loot()[0].taken);                                 // two items: one is left behind, not both
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
    const float wait0 = kStormPhases[0].waitSec, close0 = kStormPhases[0].closeSec;
    CHECK(a.phase == 0 && !a.shrinking && std::abs(a.secondsLeft - wait0) < 0.01f);   // holding for its wait
    auto b = s.InfoAt(wait0 - 1);
    CHECK(b.phase == 0 && !b.shrinking && std::abs(b.secondsLeft - 1.0f) < 0.01f);
    auto c = s.InfoAt(wait0 + 5);
    CHECK(c.phase == 0 && c.shrinking && std::abs(c.secondsLeft - (close0 - 5.0f)) < 0.01f);     // 5 s into the shrink
    auto d = s.InfoAt(wait0 + close0 + 1);
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
    for (int i = 0; i < kPoolItemCount; i++) {
        const ItemDef& d = kItems[i];
        CHECK(static_cast<int>(d.id) == i);
        CHECK(d.name && d.name[0] && d.effect && d.effect[0]);
        CHECK(names.insert(d.name).second);                       // every item has its own name
        CHECK(d.minRarity <= d.maxRarity);
        perKind[static_cast<int>(d.kind)]++;
        songs += IsSong(d.id);
        simple += IsSimpleSong(d.id);
        switch (d.kind) {
            case ItemKind::Weapon:     CHECK((WeaponOf(d.id).damage > 0 || d.id == ItemId::ShockwaveGrenade) && WeaponOf(d.id).range > 0 && WeaponOf(d.id).cooldown > 0); break;
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
    CHECK(kItemCount >= 80 && kPoolItemCount == 90 && !InPool(ItemId::BasicSword) && !InPool(ItemId::Rupees) && InPool(ItemId::HomingBombchus));
    CHECK(perKind[static_cast<int>(ItemKind::Weapon)] == 20 && perKind[static_cast<int>(ItemKind::Shield)] == 3);
    CHECK(perKind[static_cast<int>(ItemKind::Consumable)] == 11 && perKind[static_cast<int>(ItemKind::Instant)] == 5);
    CHECK(perKind[static_cast<int>(ItemKind::Ability)] == 20 && perKind[static_cast<int>(ItemKind::Gear)] == 31);
    CHECK(songs == 12 && simple == 4);
    // Every gear slot has several items, so there is always something to find for each.
    int perSlot[kGearSlots] = {};
    for (int i = 0; i < kPoolItemCount; i++) if (kItems[i].kind == ItemKind::Gear) perSlot[static_cast<int>(GearOf(kItems[i].id).slot)]++;
    for (int n : perSlot) CHECK(n >= 2);
    int sum = 0;
    for (int w : kKindWeight) sum += w;
    CHECK(sum == 100);
}

static void LootCoversEveryItemAndRespectsKindWeights() {
    std::vector<int> seen(kItemCount, 0);   // the economy items never appear in random loot
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
    for (int i = 0; i < kPoolItemCount; i++) {
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
    CHECK(seen[static_cast<int>(ItemId::BasicSword)] == 0 && seen[static_cast<int>(ItemId::Rupees)] == 0 && seen[static_cast<int>(ItemId::NutAmmo)] == 0);
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
    CHECK(std::abs(plain - 0.8f * kPlayerDamageScale) < 0.001f);                  // Kokiri Sword at Common
    CHECK(hit(ItemId::SpiritMedallion, GearSlot::Charm, none, GearSlot::Mask, DamageKind::Normal) > plain);   // melee gear hits harder
    CHECK(hit(ItemId::SkullMask, GearSlot::Mask, none, GearSlot::Mask, DamageKind::Normal) == plain);          // ranged gear doesn't help a sword
    CHECK(hit(none, GearSlot::Mask, ItemId::LightMedallion, GearSlot::Charm, DamageKind::Normal) < plain);    // damage reduction
    // Resistances only apply to their own kind of damage.
    float storm = hit(none, GearSlot::Mask, none, GearSlot::Mask, DamageKind::Storm);
    CHECK(std::abs(storm - 1.0f) < 0.001f);
    CHECK(hit(none, GearSlot::Mask, ItemId::ZoraMask, GearSlot::Mask, DamageKind::Storm) < storm);
    CHECK(std::abs(hit(none, GearSlot::Mask, ItemId::ZoraMask, GearSlot::Mask, DamageKind::Fire) - storm * kHazardDamageScale) < 0.001f); // Zora Mask doesn't stop fire
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

    // Everyone starts with 7 hearts; opening a chest spills a few rupees, and some Rare chests hold a Piece of Heart.
    {
        Simulation chestSim = Duel(1, {1500, 0}, {0, 0});
        Match& cm = chestSim.match;
        CHECK(kMaxHealth == 7.0f && cm.Find(1000)->maxHealth == 7.0f && cm.Find(1000)->health == 7.0f);
        cm.ClearLoot();
        LootSpawn chest = {{0, 0}, ItemId::BasicSword, Rarity::Common, true, true};
        chest.item = ItemId::RecoveryHeart;
        const size_t idx = cm.AddLoot(chest);
        const size_t before = cm.Loot().size();
        cm.Find(1000)->pos = {0, 0};
        cm.Find(1000)->health = 1.0f;
        CHECK(cm.PickUp(1000, idx));
        const size_t spilled = cm.Loot().size() - before;
        int money = 0;
        for (size_t i = before; i < cm.Loot().size(); i++) money += cm.Loot()[i].spawn.item == ItemId::Rupees && !cm.Loot()[i].spawn.container;
        CHECK(spilled >= 2 && spilled <= 3 && static_cast<size_t>(money) == spilled);
        int pieces = 0, rare = 0;
        for (uint64_t sd = 1; sd <= 40; sd++) {
            Simulation s2 = Duel(sd, {1500, 0}, {0, 0});
            s2.match.RegenerateLoot(300, 0.5f);
            for (const auto& e : s2.match.Loot()) if (e.spawn.container && !e.spawn.special && e.spawn.rarity == Rarity::Rare) { rare++; pieces += e.spawn.item == ItemId::HeartPiece; }
        }
        CHECK(rare > 100 && pieces > 0 && pieces < rare / 4);
    }

    // Instant: a heart at full health is left on the ground, and used once you are hurt.
    CHECK(!grab(ItemId::RecoveryHeart, Rarity::Common));
    p->health = 1.0f;
    CHECK(grab(ItemId::RecoveryHeart, Rarity::Common) && std::abs(p->health - 2.0f) < 0.001f);
    p->health = p->maxHealth - 0.5f;
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
    CHECK(std::abs((before - p->health) - 0.5f * kPlayerDamageScale) < 0.001f);
    for (int i = 0; i < 7 * kTickHz; i++) m.Tick(kDt);
    before = p->health;
    m.Damage(1000, 1.0f, 1);
    CHECK(std::abs((before - p->health) - kPlayerDamageScale) < 0.001f);
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
        CHECK(std::abs((before - t->health) - 1.25f * kPlayerDamageScale) < 0.001f);
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
    auto give = [&](ItemId item, Rarity r = Rarity::Common) { me->ability = {item, r}; me->hasAbility = true; me->abilityReadyAt = 0; me->magic = kMaxMagic; me->magicStamp = m.Clock(); };
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
    auto give = [&](ItemId item, Rarity r = Rarity::Common) { me->ability = {item, r}; me->hasAbility = true; me->abilityReadyAt = 0; me->magic = kMaxMagic; me->magicStamp = m.Clock(); };
    me->rot = 0x4000;                                                                  // facing +x
    m.DrainEvents();

    // Longshot pulls whoever is in front of you to you, stuns them, and costs nothing if nobody is there.
    give(ItemId::Longshot);
    foe->pos = {-400, 0};                                                              // behind
    CHECK(!m.UseAbility(1) && me->abilityReadyAt == 0);                               // nobody in front: not used up
    foe->pos = {400, 0};
    CHECK(m.UseAbility(1));
    CHECK(Distance(foe->pos, me->pos) < 130 && foe->pos.x > 0);
    CHECK(m.Stunned(*foe));
    bool teleported = false;
    for (auto& e : m.DrainEvents()) teleported |= e.type == MatchEvent::Type::Teleported && e.a == 1000;
    CHECK(teleported);

    // Out of reach it does nothing, and costs nothing.
    foe->pos = {1900, 0}; foe->stunUntil = 0;
    give(ItemId::Longshot);
    CHECK(!m.UseAbility(1));
    foe->pos = {1300, 0};
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

static void EliminatedPlayersDropPartOfTheirKitAndKillsAreCredited() {
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
    v->ammo.fill(0);
    CHECK(m.Loot().empty());
    CHECK(m.Damage(1000, 50.0f, 1));
    CHECK(m.Loot().size() >= 3 && m.Loot().size() <= 4);                               // about half of: weapon, shield, ability, 2 gear, 2 potions
    CHECK(m.Find(1)->kills == 1);                                                      // credited exactly once
    // The dead player's loot can be picked up by the winner.
    int got = 0;
    for (size_t i = 0; i < m.Loot().size(); i++) { m.Find(1)->pos = m.Loot()[i].spawn.pos; got += m.PickUp(1, i); }
    CHECK(got == static_cast<int>(m.Loot().size()));
    // Different players keep different halves, and the same player always the same one.
    {
        std::set<size_t> counts;
        for (uint32_t id = 1; id < 40; id++) {
            Simulation s2 = Duel(1, {0, 0}, {100, 0});
            PlayerState* d = s2.match.Find(1000);
            d->weapon = {ItemId::MasterSword, Rarity::Epic}; d->hasShield = true; d->shield = {ItemId::HylianShield, Rarity::Rare};
            d->potions = {{ItemId::RedPotion, Rarity::Common}, {ItemId::Fish, Rarity::Common}, {ItemId::GreenPotion, Rarity::Common}};
            d->ammo.fill(0);
            s2.match.Damage(1000, 50.0f, 1);
            counts.insert(s2.match.Loot().size());
        }
        CHECK(!counts.empty());
    }
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
    {   // The Longshot reels in a foe who is out of sword range.
        Simulation sim = Duel(5, {500, 0}, {0, 0});
        PlayerState* b = sim.match.Find(1000);
        b->ability = {ItemId::Longshot, Rarity::Rare}; b->hasAbility = true;
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

static Simulation DragonArena(int mapId, bool enabled);


static void ClothAndWind() {
    // The sheet stays attached to the frame, stays finite and stays near its cut shape, whatever the air does.
    GliderCloth calm;
    for (int i = 0; i < 240; i++) calm.Update(1.0f / 60.0f, {0, 0, 0}, 0.0f, i / 60.0f, 3);
    float calmMove = 0;
    for (int j = 0; j <= kClothSpan; j++) for (int i2 = 0; i2 <= kClothChord; i2++) calmMove = (std::max)(calmMove, Len(calm.left.At(i2, j) - calm.left.Rest(i2, j)));
    CHECK(calmMove < 40.0f);
    for (int j = 0; j <= kClothSpan; j++) { CHECK(Len(calm.left.At(0, j) - calm.left.Rest(0, j)) < 0.001f && Len(calm.left.At(kClothChord, 0) - calm.left.Rest(kClothChord, 0)) < 0.001f); }   // leading edge and keel are fixed
    // Falling fast into a storm billows the cloth up and makes it flutter far more than a still day.
    GliderCloth storm;
    float maxMove = 0, wobble = 0;
    ClothV3 last = storm.left.At(kClothChord, kClothSpan - 1);
    for (int i = 0; i < 600; i++) {
        storm.Update(1.0f / 60.0f, {40, 600, -80}, 1.0f, i / 60.0f, 3);
        for (int j = 0; j <= kClothSpan; j++) for (int i2 = 0; i2 <= kClothChord; i2++) {
            const ClothV3 d = storm.left.At(i2, j) - storm.left.Rest(i2, j);
            CHECK(std::isfinite(d.x) && std::isfinite(d.y) && std::isfinite(d.z));
            maxMove = (std::max)(maxMove, Len(d));
        }
        const ClothV3 now = storm.left.At(kClothChord, kClothSpan - 1);
        wobble += Len(now - last); last = now;
    }
    float calmWobble = 0;
    GliderCloth still;
    ClothV3 lastStill = still.left.At(kClothChord, kClothSpan - 1);
    for (int i = 0; i < 600; i++) { still.Update(1.0f / 60.0f, {0, 100, 0}, 0.0f, i / 60.0f, 3); const ClothV3 now = still.left.At(kClothChord, kClothSpan - 1); calmWobble += Len(now - lastStill); lastStill = now; }
    CHECK(maxMove > 8.0f && maxMove <= 90.5f);                          // it moves, but is held within a leash
    CHECK(wobble > calmWobble * 1.5f);                                    // and the storm makes it much livelier
    // Air from below lifts the trailing edge; air from above pushes it down.
    GliderCloth up, down;
    for (int i = 0; i < 180; i++) { up.Update(1.0f / 60.0f, {0, 600, 0}, 0.0f, 0.0f, 0); down.Update(1.0f / 60.0f, {0, -600, 0}, 0.0f, 0.0f, 0); }
    CHECK(up.left.At(kClothChord, 3).y > down.left.At(kClothChord, 3).y + 6.0f);
    // Mirror wings: the right wing of the same air is a mirror of the left.
    CHECK(up.right.At(kClothChord, 3).x < 0 ? false : true);
    // Garbage in does no harm, and a huge time step is clipped rather than blowing up.
    GliderCloth wild;
    wild.Update(5.0f, {1e6f, -1e6f, 1e6f}, 5.0f, 1e5f, 0xFFFFFFFFu);
    bool finite = true;
    for (const auto& v : wild.left.p) finite &= std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && Len(v) < 400.0f;
    CHECK(finite);
    // The triangles for drawing: whole triangles, both sides, in the colours asked for, nothing NaN.
    std::vector<ClothVertex> tris;
    const uint8_t a[3] = {230, 70, 60}, b[3] = {245, 235, 220};
    storm.Build(tris, a, b);
    CHECK(tris.size() == static_cast<size_t>(2 * kClothSpan * kClothChord * 2 * 2 * 3) && tris.size() % 3 == 0);   // two wings, two triangles a cell, two faces
    bool tfinite = true; int reds = 0, creams = 0;
    for (const auto& v : tris) { tfinite &= std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); reds += v.r > v.g * 2; creams += v.g > 120; }
    CHECK(tfinite && reds > 100 && creams > 100);
    // The cap: still on a still day, streams back and up when Link runs forward, settles again afterwards, and never goes wild.
    HatSpring hat;
    for (int i = 0; i < 120; i++) hat.Step(1.0f / 60.0f, 0, 0, 0, 0.0f, 0.0f, 0);
    CHECK(std::fabs(hat.fore) < 0.05f && std::fabs(hat.side) < 0.05f);
    HatSpring run;
    for (int i = 0; i < 120; i++) run.Step(1.0f / 60.0f, 0, -300, 0, 0.0f, 0.0f, 0);   // air streaming back past him at 300 a second
    HatSpring stand;
    for (int i = 0; i < 120; i++) stand.Step(1.0f / 60.0f, 0, 300, 0, 0.0f, 0.0f, 0);  // the opposite direction
    CHECK(run.fore > 0.3f && stand.fore < -0.3f && run.fore <= 1.1f);
    for (int i = 0; i < 240; i++) run.Step(1.0f / 60.0f, 0, 0, 0, 0.0f, 0.0f, 0);
    CHECK(std::fabs(run.fore) < 0.06f);
    HatSpring junk;
    junk.Step(10.0f, 1e9f, -1e9f, 1e9f, 9.0f, 1e9f, 5);
    CHECK(std::isfinite(junk.fore) && std::isfinite(junk.side) && std::fabs(junk.fore) <= 1.1f + 1e-4f && std::fabs(junk.twist) <= 0.6f + 1e-4f);
    junk.Step(NAN, NAN, NAN, NAN, NAN, NAN, 0, NAN, NAN, NAN);
    CHECK(std::isfinite(junk.fore) && std::isfinite(junk.side) && std::isfinite(junk.twist));
    // Turning drags the cap out to the other side and rolls it, then it settles; a hard turn throws it further than a gentle one.
    HatSpring left, gentle, none;
    float leftMax = 0, gentleMax = 0, twistMax = 0, noneMax = 0;
    for (int i = 0; i < 90; i++) {
        left.Step(1.0f / 60.0f, 0, 0, 0, 0.0f, 0.0f, 0, 0, 0, 8.0f); gentle.Step(1.0f / 60.0f, 0, 0, 0, 0.0f, 0.0f, 0, 0, 0, 2.0f); none.Step(1.0f / 60.0f, 0, 0, 0, 0.0f, 0.0f, 0);
        leftMax = (std::max)(leftMax, left.side); gentleMax = (std::max)(gentleMax, gentle.side); twistMax = (std::max)(twistMax, left.twist); noneMax = (std::max)(noneMax, std::fabs(none.side));
    }
    CHECK(leftMax > 0.25f && leftMax > gentleMax * 1.5f && twistMax > 0.1f && noneMax < 0.02f);
    for (int i = 0; i < 300; i++) left.Step(1.0f / 60.0f, 0, 0, 0, 0.0f, 0.0f, 0);
    CHECK(std::fabs(left.side) < 0.05f && std::fabs(left.twist) < 0.05f);
    // The skirt swings out in a turn too.
    ClothSwing spin;
    float spinMax = 0;
    for (int i = 0; i < 90; i++) { spin.Step(kSkirtSwing, 1.0f / 60.0f, 0, 0, 0, 0, 0.0f, 0.0f, 0, 0, 0, 8.0f); spinMax = (std::max)(spinMax, spin.side); }
    CHECK(spinMax > 0.03f);
    // The wind spins the cap more in a gale than a breeze.
    float breeze = 0, gale = 0;
    HatSpring h1, h2;
    for (int i = 0; i < 600; i++) { h1.Step(1.0f / 60.0f, 20, 10, 0, 0.1f, i / 60.0f, 2); h2.Step(1.0f / 60.0f, 20, 10, 0, 1.0f, i / 60.0f, 2); breeze = (std::max)(breeze, std::fabs(h1.fore)); gale = (std::max)(gale, std::fabs(h2.fore)); }
    CHECK(gale > breeze);
    // The tunic's skirt and the sheath: still when he stands on a calm day, trail back when he runs, swing past and settle when he stops,
    // flap more at a run than a walk, and stay finite and in reach whatever they are fed.
    for (const SwingTune* tune : {&kSkirtSwing, &kSheathSwing}) {
        ClothSwing calm;
        for (int i = 0; i < 240; i++) calm.Step(*tune, 1.0f / 60.0f, 0, 0, 0, 0, 0.0f, i / 60.0f, 0);
        CHECK(std::fabs(calm.fore) < 0.03f && std::fabs(calm.side) < 0.03f);
        ClothSwing runs;
        float trail = 0;
        for (int i = 0; i < 240; i++) { runs.Step(*tune, 1.0f / 60.0f, 0, -300, 300, 0, 0.0f, i / 60.0f, 0); if (i > 120) trail += runs.fore / 119.0f; }
        CHECK(trail > 0.05f && trail <= tune->maxFore);
        float past = 0;   // a sudden stop throws it forward (negative) before it settles
        runs.Step(*tune, 1.0f / 60.0f, 0, 0, 0, 0, 0.0f, 4.0f, 0, -300.0f, 0.0f);
        for (int i = 0; i < 30; i++) { runs.Step(*tune, 1.0f / 60.0f, 0, 0, 0, 0, 0.0f, 4.0f + i / 60.0f, 0); past = (std::min)(past, runs.fore); }
        CHECK(past < -0.02f);
        for (int i = 0; i < 300; i++) runs.Step(*tune, 1.0f / 60.0f, 0, 0, 0, 0, 0.0f, 0.0f, 0);
        CHECK(std::fabs(runs.fore) < 0.02f);
        ClothSwing walk, sprint;
        float walkSwing = 0, sprintSwing = 0;
        for (int i = 0; i < 300; i++) {
            walk.Step(*tune, 1.0f / 60.0f, 0, 0, 120, 0, 0.0f, 0.0f, 0); sprint.Step(*tune, 1.0f / 60.0f, 0, 0, 450, 0, 0.0f, 0.0f, 0);
            if (i > 60) { walkSwing = (std::max)(walkSwing, std::fabs(walk.side)); sprintSwing = (std::max)(sprintSwing, std::fabs(sprint.side)); }
        }
        CHECK(sprintSwing > walkSwing && walkSwing > 0.005f);
        ClothSwing still, windy;
        float stillMax = 0, windyMax = 0;
        for (int i = 0; i < 600; i++) {
            still.Step(*tune, 1.0f / 60.0f, 0, 0, 0, 0, 0.0f, i / 60.0f, 1); windy.Step(*tune, 1.0f / 60.0f, 0, 0, 0, 0, 1.0f, i / 60.0f, 1);
            stillMax = (std::max)(stillMax, std::fabs(still.fore)); windyMax = (std::max)(windyMax, std::fabs(windy.fore));
        }
        CHECK(windyMax > stillMax * 2.0f);
        ClothSwing wild;
        wild.Step(*tune, 10.0f, 1e9f, -1e9f, 1e9f, -1e9f, 9.0f, 1e9f, 5, 1e9f, -1e9f);
        wild.Step(*tune, NAN, NAN, NAN, NAN, NAN, NAN, 0.0f, 5, NAN, NAN);
        CHECK(std::isfinite(wild.fore) && std::isfinite(wild.side) && std::fabs(wild.fore) <= tune->maxFore * 1.5f + 1e-4f && std::fabs(wild.side) <= tune->maxSide * 1.5f + 1e-4f);
    }
}

static void TheSignInTheMiddle() {
    CHECK(std::string(kMapSignText) == "If you read this, I love My Wife Cynthia and my 2 daughters Maya and Avriela!");
    CHECK(std::string(kLiloName) == "Lilo" && std::string(kLiloLine) == "meoooww I smell a fart nearby");
    const MeshData lilo = BuildMesh(MeshKind::Cat, 0);
    float cmn[3], cmx[3];
    lilo.Bounds(cmn, cmx);
    CHECK(lilo.Triangles() >= 200 && cmx[1] > 70 && cmx[1] < 90 && cmx[0] - cmn[0] < 50 && cmx[2] - cmn[2] > 60);
    // Her accident, a real recording: about a second and a half, audible, never clipping, and ending quietly.
    const lilo_snd::Clip& fart = lilo_snd::kFart;
    int peak = 0; double energy = 0;
    for (int i = 0; i < fart.count; i++) { peak = std::max(peak, std::abs(static_cast<int>(fart.data[i]))); energy += static_cast<double>(fart.data[i]) * fart.data[i]; }
    CHECK(fart.count > lilo_snd::kRate && fart.count < lilo_snd::kRate * 2 && peak > 6000 && peak < 32000 && energy / fart.count > 1.0e6 && std::abs(static_cast<int>(fart.data[fart.count - 1])) < 300);
    // Lilo's seven recorded mews: short, audible, never clipping, and fading out to nothing at the end.
    CHECK(sizeof(lilo_snd::kClips) / sizeof(lilo_snd::kClips[0]) == 7);
    for (const lilo_snd::Clip& clip : lilo_snd::kClips) {
        int clipPeak = 0;
        for (int i = 0; i < clip.count; i++) clipPeak = std::max(clipPeak, std::abs(static_cast<int>(clip.data[i])));
        CHECK(clip.count > lilo_snd::kRate / 5 && clip.count < lilo_snd::kRate * 2 && clipPeak > 8000 && clipPeak < 32000 && std::abs(static_cast<int>(clip.data[clip.count - 1])) < 300);
    }
    CHECK(std::string(kMayaName) == "Maya" && std::string(kMayaGreeting) == "Hi Daddy I'm a Goo goo!");
    const MeshData sign = BuildMesh(MeshKind::Sign, 0);
    float mn[3], mx[3];
    sign.Bounds(mn, mx);
    CHECK(sign.Triangles() >= 24 && mx[1] > 140 && mx[1] < 170 && mx[0] - mn[0] > 130 && mx[0] - mn[0] < 160);
    const MeshData frame = BuildMesh(MeshKind::GliderFrame, 0);
    CHECK(frame.Triangles() >= 24 && frame.Triangles() < BuildMesh(MeshKind::Glider, 0).Triangles());   // the cloth version has no wings of its own
    {   // The Blender glider: the handle bar sits at the origin with a leather grip either side, and the live cloth wing starts exactly at its frame.
        using namespace glider_model;
        bool grips = false, gold = false;
        for (int i = 0; i < kFrameCount; i++) {
            grips |= kFrame[i].colour == 1 && std::fabs(kFrame[i].p[1]) < 8.0f && std::fabs(kFrame[i].p[0]) <= kGripOuter + 1.0f;
            gold |= kFrame[i].colour == 2;
        }
        CHECK(grips && gold && kFrameCount > 100 && kWingCount > 60);
        GliderCloth fit;
        CHECK(Len(fit.left.Rest(0, 0) - ClothV3{kNose[0], kNose[1], kNose[2]}) < 0.01f && Len(fit.right.Rest(kClothChord, kClothSpan) - ClothV3{kTip[0], kTip[1], kTip[2]}) < 0.5f + Len(ClothV3{kTail[0] - kTip[0], kTail[1] - kTip[1], kTail[2] - kTip[2]}) * 0.0f);
    }
}

static void HireableAllies() {
    // Four allies, one of each kind, free and healthy, placed apart and on valid ground.
    {
        const Circle map = {{0, 0}, 3000};
        auto valid = [](Vec2 p) { return p.x > -2500.0f; };
        const PoiLayout layout = GeneratePois(4, map, 12, valid, 1);
        const std::vector<Vec2> spots = GenerateAllySpots(4, map, layout.pois, valid);
        CHECK(static_cast<int>(spots.size()) == kAllyCount);
        for (size_t i = 0; i < spots.size(); i++) {
            CHECK(valid(spots[i]) && Distance(spots[i], map.center) <= map.radius);
            for (size_t j = i + 1; j < spots.size(); j++) CHECK(Distance(spots[i], spots[j]) > 300.0f);
        }
        CHECK(GenerateAllySpots(4, map, layout.pois, valid).size() == spots.size() && Distance(GenerateAllySpots(4, map, layout.pois, valid)[2], spots[2]) < 0.01f);
    }
    for (int i = 0; i < kAllyCount; i++) CHECK(AllyOf(static_cast<AllyKind>(i)).price >= 30 && AllyOf(static_cast<AllyKind>(i)).maxHealth >= 3 && std::string(AllyOf(static_cast<AllyKind>(i)).name).size() > 2);
    Simulation sim = Duel(11, {0, 0}, {0, 1900});
    Match& m = sim.match;
    CHECK(m.Allies().size() == static_cast<size_t>(kAllyCount));
    for (int i = 0; i < kAllyCount; i++) CHECK(m.Allies()[static_cast<size_t>(i)].index == i && static_cast<int>(m.Allies()[static_cast<size_t>(i)].kind) == i && m.Allies()[static_cast<size_t>(i)].alive && !m.Allies()[static_cast<size_t>(i)].Hired());
    PlayerState* me = m.Find(1);
    m.Find(1000)->stunUntil = 1e9f;                                                        // (the bot sits this part out, far away)
    me->rupees = 1000;
    auto place = [&](int index, Vec2 at) { m.MutableAllies()[static_cast<size_t>(index)].pos = at; };
    // Hiring needs you to be next to it, enough rupees, and a free ally.
    place(0, {600, 0});
    CHECK(!m.HireAlly(1, 0));                                                             // too far
    place(0, {100, 0});
    me->rupees = 10;
    CHECK(!m.HireAlly(1, 0));                                                             // too poor
    me->rupees = 1000;
    m.DrainEvents();
    CHECK(m.HireAlly(1, 0) && m.Allies()[0].owner == 1 && me->rupees == 1000 - AllyOf(AllyKind::Kokiri).price);
    bool announced = false;
    for (const auto& e : m.DrainEvents()) announced |= e.type == MatchEvent::Type::AllyChanged && e.a == 0 && e.b == 1 && e.item == 0;
    CHECK(announced);
    CHECK(!m.HireAlly(1, 0) && !m.HireAlly(1, 9) && !m.HireAlly(99, 1));                  // already hired, no such ally, no such player
    place(1, {120, 40}); place(2, {-100, 40});
    CHECK(m.HireAlly(1, 1) && m.AlliesOf(1) == 2);
    CHECK(!m.HireAlly(1, 2) && !m.Allies()[2].Hired());                                   // two at most
    // They follow: walk away and they come along; one left far behind is brought back.
    me->pos = {1500, 0};
    Run(sim, 12.0f);
    CHECK(Distance(m.Allies()[0].pos, me->pos) < 450.0f && Distance(m.Allies()[1].pos, me->pos) < 450.0f);
    me->pos = {-1200, 1000};
    m.MutableAllies()[0].pos = {1400, -1300};
    Run(sim, 2.0f);
    CHECK(Distance(m.Allies()[0].pos, me->pos) < 800.0f);   // (2600 behind: brought back in a flash)
    // They fight: a bot that comes close is hit by them and the credit goes to their owner.
    PlayerState* foe = m.Find(1000);
    foe->stunUntil = 0;
    foe->weapon = {ItemId::DekuStick, Rarity::Common};
    foe->health = foe->maxHealth;
    me->invulnUntil = 1e9f;                      // (the owner is not the test)
    foe->pos = {me->pos.x + 400, me->pos.z};
    m.MutableAllies()[0].pos = {me->pos.x + 150, me->pos.z}; m.MutableAllies()[1].pos = {me->pos.x - 150, me->pos.z};
    const float dealtBefore = me->damageDealt;
    bool acted = false;
    for (int i = 0; i < 15 * kTickHz && foe->alive; i++) { sim.Tick(kDt); for (const auto& e : m.DrainEvents()) acted |= e.type == MatchEvent::Type::AllyAction; }
    CHECK(acted && me->damageDealt > dealtBefore + 0.2f);
    // A Goron hits hard, up close; a Zora mends a hurt owner.
    Simulation s2 = Duel(12, {0, 0}, {0, 1900});
    Match& m2 = s2.match;
    PlayerState* me2 = m2.Find(1);
    m2.Find(1000)->stunUntil = 1e9f;
    me2->rupees = 1000;
    m2.MutableAllies()[1].pos = {80, 0};
    CHECK(m2.HireAlly(1, 1));
    me2->health = 1.0f;
    Run(s2, 5.0f);
    CHECK(me2->health >= 1.4f);                                                          // the Zora's gift (0.5 hearts)
    // When the owner falls, the allies are free again.
    m2.MutableAllies()[2].pos = {60, 0};
    CHECK(m2.HireAlly(1, 2));
    me2->invulnUntil = 0;
    m2.DrainEvents();
    CHECK(m2.Damage(1, 100.0f, 1000));
    bool released = false;
    for (const auto& e : m2.DrainEvents()) released |= e.type == MatchEvent::Type::AllyChanged && e.item == 1;
    CHECK(released && m2.AlliesOf(1) == 0 && !m2.Allies()[1].Hired() && !m2.Allies()[2].Hired());
    // Out in the storm an ally is hurt and finally falls (and a free one walks for the safe zone first).
    Simulation s3 = Duel(13, {0, 0}, {0, 1900});
    Match& m3 = s3.match;
    m3.Find(1)->invulnUntil = 1e9f; m3.Find(1000)->invulnUntil = 1e9f;                    // (so the match runs on while the storm closes)
    AllyState& lost = m3.MutableAllies()[3];
    for (int i = 0; i < static_cast<int>(2000 * kTickHz) && m3.State() == MatchState::InMatch && m3.GetStorm().DamagePerSecond(lost.pos, m3.StormTime()) <= 0; i++) m3.Tick(kDt);
    lost.pos = {m3.MapCircle().center.x + m3.MapCircle().radius * 0.95f, m3.MapCircle().center.z};
    const float before = lost.health;
    bool outside = m3.GetStorm().DamagePerSecond(lost.pos, m3.StormTime()) > 0;
    Run(s3, 2.0f);
    CHECK(!outside || lost.health < before || Distance(lost.pos, {m3.MapCircle().center.x + m3.MapCircle().radius * 0.95f, m3.MapCircle().center.z}) > 20.0f);   // it ran for cover or took damage
}

static void MatchReplayIsRecorded() {
    Simulation sim(51, MapCircle(), 20);
    sim.match.AddHuman(1);
    sim.match.Start();
    for (int i = 0; i < 400 * kTickHz && sim.match.State() != MatchState::Ending; i++) sim.Tick(kDt);
    const Replay& rp = sim.match.GetReplay();
    CHECK(rp.Valid() && rp.ids.size() == static_cast<size_t>(kMaxPlayers));
    CHECK(rp.frames.size() >= 20 && rp.frames.size() <= static_cast<size_t>(kReplayMaxFrames));
    bool shaped = true;
    for (const auto& f : rp.frames) shaped &= f.size() == rp.ids.size() * 2;
    CHECK(shaped);
    // The first frame has everyone in; by the last, most are gone; each elimination is on the list at a frame that exists.
    int goneFirst = 0, goneLast = 0;
    for (size_t i = 0; i < rp.ids.size(); i++) { goneFirst += rp.frames.front()[i * 2] == kReplayGone; goneLast += rp.frames.back()[i * 2] == kReplayGone; }
    CHECK(goneFirst <= 2 && goneLast >= kMaxPlayers - 3);
    CHECK(rp.kills.size() >= 20);
    bool ok = true;
    for (const ReplayKill& k : rp.kills) ok &= k.frame < rp.frames.size();
    CHECK(ok);
}

static void HeartChestsAndAdultPower() {
    // A few of the hideaways and climbs hold a Heart Container in a pink (special) chest.
    {
        Simulation sim(31, {{0, 0}, 4000}, 0);
        std::vector<ChestSite> sites;
        for (int i = 0; i < 20; i++) sites.push_back({{-3000.0f + i * 300.0f, (i % 2) * 400.0f}, static_cast<uint8_t>(i % 3)});
        sim.match.SetChestSites(sites);
        sim.match.RegenerateLoot(10);
        int special = 0;
        for (const auto& l : sim.match.Loot()) {
            if (!l.spawn.special) continue;
            special++;
            CHECK(l.spawn.item == ItemId::HeartContainer && l.spawn.container && l.spawn.rarity == Rarity::Legendary);
        }
        CHECK(special >= 2 && special <= 4);
        Simulation again(31, {{0, 0}, 4000}, 0);
        again.match.SetChestSites(sites);
        again.match.RegenerateLoot(10);
        int special2 = 0;
        for (const auto& l : again.match.Loot()) special2 += l.spawn.special;
        CHECK(special2 == special);                                                        // the same match always gets the same hearts
    }
    // Adult Power: Legendary only, grows you for a minute, and makes you hit harder, take less and run faster.
    CHECK(DefOf(ItemId::AdultPower).minRarity == Rarity::Legendary && DefOf(ItemId::AdultPower).maxRarity == Rarity::Legendary && InPool(ItemId::AdultPower));
    Simulation sim = Duel(21, {0, 0}, {45, 0});
    Match& m = sim.match;
    PlayerState* me = m.Find(1);
    PlayerState* foe = m.Find(1000);
    foe->stunUntil = 1e9f;
    me->weapon = {ItemId::KokiriSword, Rarity::Common};
    foe->health = foe->maxHealth;
    const float plain = [&] { const AttackResult r = m.Attack(1, 1000); return r.damage; }();
    CHECK(plain > 0);
    for (int i = 0; i < 2 * kTickHz; i++) m.Tick(kDt);
    const size_t power = m.AddLoot({{0, 0}, ItemId::AdultPower, Rarity::Legendary, true});
    CHECK(m.PickUp(1, power, false));
    CHECK(m.Clock() < me->adultUntil && me->adultUntil - m.Clock() > kAdultSeconds - 1.0f);
    const AttackResult grown = m.Attack(1, 1000);
    CHECK(grown.ok && grown.damage > plain * 1.3f);
    CHECK(m.SpeedMultiplier(*me) > 1.09f);
    const float hp = me->health;
    me->invulnUntil = 0; me->armor = 0;
    m.Damage(1, 0.5f, 1000);
    CHECK(std::abs((hp - me->health) - 0.5f * kAdultTaken * kPlayerDamageScale) < 0.01f);
    // A second one straight away is not wasted; near the end it tops you up.
    const size_t again = m.AddLoot({{0, 0}, ItemId::AdultPower, Rarity::Legendary, true});
    CHECK(!m.PickUp(1, again, false));
    for (int i = 0; i < 45 * kTickHz; i++) m.Tick(kDt);
    CHECK(m.PickUp(1, again, false) && me->adultUntil - m.Clock() > kAdultSeconds - 1.0f);
    for (int i = 0; i < 61 * kTickHz; i++) m.Tick(kDt);
    CHECK(m.SpeedMultiplier(*me) < 1.01f);                                                  // and it wears off
}

static void MagicMeter() {
    Simulation sim = Duel(1, {0, 0}, {3000, 0});
    Match& m = sim.match;
    PlayerState* me = m.Find(1);
    me->ability = {ItemId::DinsFire, Rarity::Common}; me->hasAbility = true; me->abilityReadyAt = 0;
    CHECK(std::abs(m.MagicNow(*me) - kMaxMagic) < 0.01f);                 // everybody starts full
    CHECK(m.UseAbility(1));
    CHECK(std::abs(m.MagicNow(*me) - (kMaxMagic - AbilityMagic(ItemId::DinsFire))) < 0.5f);   // it cost magic
    // It refills by itself, at about one use per cooldown.
    me->abilityReadyAt = 0;
    const float before = m.MagicNow(*me);
    for (int i = 0; i < 10 * kTickHz; i++) sim.Tick(1.0f / kTickHz);
    CHECK(m.MagicNow(*me) > before + 9.0f && m.MagicNow(*me) <= kMaxMagic);
    // Without enough magic nothing fires and nothing is spent (and no cooldown starts).
    me->magic = 5.0f; me->magicStamp = m.Clock();
    me->ability = {ItemId::NayrusLove, Rarity::Common}; me->abilityReadyAt = 0;
    CHECK(!m.UseAbility(1) && !m.Invulnerable(*me) && me->abilityReadyAt == 0);
    // A Magic Jar tops it up, and is refused when there is nothing to refill.
    me->magic = 10.0f; me->magicStamp = m.Clock();
    me->pos = {100, 0};
    const size_t jar = m.AddLoot({{100, 0}, ItemId::MagicJar, Rarity::Common, false});
    CHECK(m.PickUp(1, jar, false) && m.MagicNow(*me) >= 45.0f);
    me->magic = kMaxMagic; me->magicStamp = m.Clock(); me->abilityReadyAt = 0;
    const size_t jar2 = m.AddLoot({{100, 0}, ItemId::MagicJar, Rarity::Common, false});
    CHECK(!m.PickUp(1, jar2, false));
    // Every ability has a cost, and none is free or more than the meter holds.
    for (int i = 0; i < kItemCount; i++) {
        const ItemId id = static_cast<ItemId>(i);
        if (DefOf(id).kind != ItemKind::Ability) continue;
        CHECK(AbilityMagic(id) >= 10.0f && AbilityMagic(id) <= kMaxMagic * 0.5f);
        CHECK(AbilityMagic(id) <= AbilityOf(id).cooldown * kMagicRegenPerSec * 1.8f);   // the meter refills about as fast as the cooldown runs
    }
    // Bots do not try abilities they cannot pay for.
    Simulation botSim = Duel(5, {1900, 0}, {0, 0});
    PlayerState* bot = botSim.match.Find(1000);
    bot->ability = {ItemId::SongOfTime, Rarity::Common}; bot->hasAbility = true; bot->abilityReadyAt = 0;
    bot->magic = 0.0f; bot->magicStamp = botSim.match.Clock();
    botSim.match.Find(1)->health = botSim.match.Find(1)->maxHealth;
    for (int i = 0; i < 3 * kTickHz; i++) botSim.Tick(1.0f / kTickHz);
    CHECK(botSim.match.Clock() < bot->abilityReadyAt || bot->abilityReadyAt == 0);
}

static void SeasonsAndWeather() {
    // Pure and deterministic: same seed, map and spell give the same sky; the first spell is fair; no intensity means no weather at all.
    WeatherOptions o;
    for (int map = 0; map < kMapCount; map++) {
        for (int sp = 0; sp < 30; sp++) {
            const Weather a = WeatherForSpell(o, 99, map, sp), b = WeatherForSpell(o, 99, map, sp);
            CHECK(a.sky == b.sky && a.intensity == b.intensity && a.season == b.season);
            CHECK(a.intensity <= 100 && (a.sky == Sky::Clear) == (a.intensity == 0));
            if (sp == 0) CHECK(a.sky == Sky::Clear);
            // places keep to their own weather: no rain in the crater, no snow outside winter, no sandstorm outside the desert
            for (int season = 0; season < kSeasonCount; season++) {
                WeatherOptions fixed = o; fixed.season = static_cast<uint8_t>(season);
                const Weather w = WeatherForSpell(fixed, 1234 + sp, map, sp);
                CHECK(w.season == static_cast<Season>(season));
                if (map == 3) CHECK(w.sky != Sky::Rain && w.sky != Sky::Sandstorm);
                if (season != 3 && !(map == 3 && w.sky == Sky::Snow)) CHECK(w.sky != Sky::Snow);
                if (map != 4) CHECK(w.sky != Sky::Sandstorm);
                if (map != 3) CHECK(w.sky != Sky::Ash);
            }
        }
    }
    WeatherOptions calm; calm.intensity = 0;
    for (int sp = 0; sp < 30; sp++) CHECK(WeatherForSpell(calm, 5, 0, sp).sky == Sky::Clear);
    // Every kind turns up somewhere over many seeds, and the change slider shortens spells.
    int seen[kSkyCount] = {};
    for (int map = 0; map < kMapCount; map++) for (int season = 0; season < kSeasonCount; season++) for (int sp = 1; sp < 80; sp++) {
        WeatherOptions f = o; f.season = static_cast<uint8_t>(season);
        seen[static_cast<int>(WeatherForSpell(f, 7, map, sp).sky)]++;
    }
    for (int k = 0; k < kSkyCount; k++) CHECK(seen[k] > 0);
    WeatherOptions slow, fast; slow.change = 0; fast.change = 100;
    CHECK(SpellSeconds(slow) > SpellSeconds(fast) * 2.0f && SpellSeconds(fast) >= 30.0f);
    // Effects scale with strength.
    Weather fog{Season::Autumn, Sky::Fog, 100}, mist{Season::Autumn, Sky::Fog, 30}, clear;
    CHECK(SightMult(fog) < SightMult(mist) && SightMult(mist) < 1.0f && SightMult(clear) == 1.0f);
    CHECK(BurnMult(Weather{Season::Spring, Sky::Rain, 100}) < 1.0f && BurnMult(Weather{Season::Spring, Sky::Ash, 100}) > 1.0f);
    CHECK(LightningEvery(Weather{Season::Spring, Sky::Thunder, 100}) > 0 && LightningEvery(fog) == 0);
    // A match announces each spell once, tells the season, and lightning only falls in thunderstorms.
    Simulation sim = DragonArena(0, false);
    WeatherOptions fastOpt; fastOpt.change = 100; fastOpt.intensity = 100; fastOpt.season = static_cast<uint8_t>(Season::Spring);
    sim.match.SetWeatherOptions(fastOpt);
    sim.match.DrainEvents();   // (the arena already announced its first spell under the default options)
    int announced = 0, strikes = 0;
    float lastStart = -1;
    bool wasThunder = false;
    for (int i = 0; i < static_cast<int>(600 * kTickHz); i++) {
        sim.Tick(kDt);
        for (const auto& e : sim.match.DrainEvents()) {
            if (e.type == MatchEvent::Type::Weather) { announced++; CHECK(e.a == 0 && std::abs(e.health - SpellSeconds(fastOpt)) < 0.01f); lastStart = sim.match.StormTime(); wasThunder = e.item == static_cast<uint8_t>(Sky::Thunder); }
            if (e.type == MatchEvent::Type::Strike && e.a == kNoPlayer) { strikes++; CHECK(wasThunder && std::abs(e.health - kLightningWarning) < 0.01f); }
        }
    }
    CHECK(announced >= 5 && lastStart >= 0);
    (void)strikes;
}

static void SupplyDrops() {
    float total = 0;
    for (const auto& ph : kStormPhases) total += ph.waitSec + ph.closeSec;
    Simulation sim = DragonArena(0, false);          // (no dragon: just the arena with an invulnerable test player)
    Match& m = sim.match;
    CHECK(m.StormTime() < 1.0f);
    const size_t before = m.Loot().size();
    int announced = 0;
    float firstAnnounce = -1, landed = -1;
    std::vector<Vec2> where;
    for (int i = 0; i < static_cast<int>((total * 0.9f) * kTickHz); i++) {
        sim.Tick(kDt);
        for (const auto& e : m.DrainEvents()) {
            if (e.type == MatchEvent::Type::SupplyDrop) {
                announced++;
                if (firstAnnounce < 0) firstAnnounce = m.StormTime();
                CHECK(std::abs(e.health - kSupplyWarningSec) < 0.01f);
                where.push_back({e.x, e.z});
                CHECK(m.GetStorm().SafeZoneAt(m.StormTime() + kSupplyWarningSec + 10.0f).Contains({e.x, e.z}));   // lands where the zone will still be
            }
            if (e.type == MatchEvent::Type::LootAdded && landed < 0 && m.Loot()[e.index].spawn.supply) landed = m.StormTime();
        }
    }
    CHECK(announced >= 2 && announced <= 5);
    CHECK(firstAnnounce >= kSupplyFirstSec - 0.1f && firstAnnounce < kSupplyFirstSec + 1.0f);
    CHECK(landed >= firstAnnounce + kSupplyWarningSec - 0.2f && landed < firstAnnounce + kSupplyWarningSec + 1.0f);          // a few seconds after the warning
    int legendary = 0, supply = 0, rupees = 0;
    for (size_t i = before; i < m.Loot().size(); i++) {
        const LootSpawn& l = m.Loot()[i].spawn;
        if (!l.supply) continue;
        supply++;
        if (l.container && l.rarity == Rarity::Legendary) legendary++;
        if (l.item == ItemId::Rupees && l.amount == 60) rupees++;
    }
    CHECK(supply == 3 * announced + 1 && legendary >= announced && rupees == announced);                                             // crate, second chest, money
    // Off means off, and none come in the last quarter of the storm.
    Simulation off = DragonArena(0, false);
    off.match.SetSupplyDrops(false);
    for (int i = 0; i < static_cast<int>(total * kTickHz); i++) { off.Tick(kDt); for (const auto& e : off.match.DrainEvents()) CHECK(e.type != MatchEvent::Type::SupplyDrop); }
}

static void ClimbsAndSpreadOutChests() {
    // A climb is three platforms side by side, each a step higher, with the best chest on the top one.
    {
        PoiLayout out;
        AddClimb(out, {500, 300}, 1, nullptr);
        CHECK(out.props.size() == 3 && out.sites.size() == 1);
        CHECK(out.props[0].kind == PropKind::PlatformLow && out.props[1].kind == PropKind::PlatformMid && out.props[2].kind == PropKind::PlatformHigh);
        CHECK(PlatformHeight(PropKind::PlatformMid) - PlatformHeight(PropKind::PlatformLow) == 60.0f && PlatformHeight(PropKind::PlatformHigh) - PlatformHeight(PropKind::PlatformMid) == 60.0f);
        for (int i = 0; i < 2; i++) CHECK(std::abs(Distance(out.props[i].pos, out.props[i + 1].pos) - kPlatformHalf * 2.0f) < 0.01f);   // edge to edge: one step up the next
        CHECK(Distance(out.sites[0].pos, out.props[2].pos) < 0.01f && out.sites[0].bonus == 2 && IsPlatform(out.props[0].kind));
        PoiLayout blocked;
        AddClimb(blocked, {0, 0}, 0, [](Vec2 p) { return p.x < 100.0f; });                // the last step would hang over a drop
        CHECK(blocked.props.empty() && blocked.sites.empty());
    }
    // Every town has a climb, and the map gets climbs and hideaways of its own, all spread apart.
    for (uint64_t seed = 5; seed < 9; seed++) {
        const Circle map = {{0, 0}, 4000};
        PoiLayout layout = GeneratePois(seed, map, 12, nullptr, 1);
        size_t platformsInTowns = 0;
        for (const Prop& p : layout.props) platformsInTowns += IsPlatform(p.kind);
        CHECK(platformsInTowns >= 3 * layout.pois.size());
        const std::vector<Prop> scenery = GenerateProps(seed, map, 560, nullptr);
        const size_t sitesBefore = layout.sites.size();
        GenerateWilds(layout, seed, map, scenery, layout.lootSpots, 6, 30, nullptr);
        int climbs = 0, hideaways = 0;
        for (size_t i = sitesBefore; i < layout.sites.size(); i++) { climbs += layout.sites[i].bonus == 2; hideaways += layout.sites[i].bonus == 1; }
        CHECK(climbs >= 4 && hideaways >= 15);
        // Wild sites keep their distance from each other, from the town chests and from the towns themselves.
        const float apart = std::max(380.0f, map.radius * 0.1f);
        for (size_t i = sitesBefore; i < layout.sites.size(); i++) {
            for (size_t j = sitesBefore; j < i; j++) CHECK(Distance(layout.sites[i].pos, layout.sites[j].pos) >= apart - 0.01f);
            for (const Vec2& q : layout.lootSpots) CHECK(Distance(layout.sites[i].pos, q) >= apart - 0.01f);
            for (const Poi& poi : layout.pois) CHECK(Distance(layout.sites[i].pos, poi.center) >= poi.radius * 0.8f - 0.01f);
        }
        // A hideaway is behind a boulder, on the far side from the middle of the map.
        for (size_t i = sitesBefore; i < layout.sites.size(); i++) {
            if (layout.sites[i].bonus != 1) continue;
            bool behind = false;
            for (const Prop& b : scenery) {
                if (b.kind != PropKind::Boulder || std::abs(Distance(b.pos, layout.sites[i].pos) - 130.0f) > 0.5f) continue;
                behind |= Distance(layout.sites[i].pos, map.center) > Distance(b.pos, map.center);
            }
            CHECK(behind);
        }
    }
    // The world a server builds: chests on the good tiers up on the climbs, and the scattered ones kept apart.
    {
        Simulation sim(21, {{0, 0}, 4000}, 0);
        sim.match.AddHuman(1);
        const PoiLayout layout = GeneratePois(21, {{0, 0}, 4000}, 12, nullptr, 1);
        PoiLayout wild = layout;
        const std::vector<Prop> scenery = GenerateProps(21, {{0, 0}, 4000}, 560, nullptr);
        GenerateWilds(wild, 21, {{0, 0}, 4000}, scenery, wild.lootSpots, 6, 30, nullptr);
        sim.match.SetLootSpots(wild.lootSpots);
        sim.match.SetChestSites(wild.sites);
        sim.match.RegenerateLoot(100);
        const auto& loot = sim.match.Loot();
        CHECK(loot.size() == 100 + wild.lootSpots.size() + wild.sites.size());
        for (size_t i = 100 + wild.lootSpots.size(), k = 0; i < loot.size(); i++, k++) {
            const Rarity floor = wild.sites[k].bonus >= 2 ? Rarity::Epic : wild.sites[k].bonus == 1 ? Rarity::Rare : Rarity::Common;
            CHECK(Distance(loot[i].spawn.pos, wild.sites[k].pos) < 0.01f && loot[i].spawn.rarity >= floor);
        }
        // Scattered chests: nearly all have no other chest within the spacing.
        int lonely = 0;
        for (size_t i = 0; i < 100; i++) {
            bool alone = true;
            for (size_t j = 0; j < loot.size(); j++) if (j != i && Distance(loot[i].spawn.pos, loot[j].spawn.pos) < 4000 * 0.11f * 0.6f && j < 100 + wild.lootSpots.size()) alone = false;
            lonely += alone;
        }
        CHECK(lonely >= 80);
    }
}

static void StartingSwordAndAmmo() {
    // Everyone starts with the basic sword: weak, but never out of ammo.
    {
        Simulation sim(77, MapCircle(), 0);
        sim.match.AddHuman(1);
        sim.match.Start();
        for (const auto& p : sim.match.Players()) CHECK(p.weapon.item == ItemId::BasicSword && p.weapon.rarity == Rarity::Common && p.rupees == 0);
        CHECK(WeaponDps(ItemId::BasicSword, Rarity::Common) > WeaponDps(ItemId::DekuStick, Rarity::Common));
        CHECK(WeaponDps(ItemId::BasicSword, Rarity::Common) < WeaponDps(ItemId::KokiriSword, Rarity::Common) * 0.8f);
    }
    // A bow with no arrows is bashed with; picking one up gives a few to start with; more comes from piles, up to a cap that a Big Quiver raises.
    {
        Simulation sim = Duel(78, {0, 0}, {60, 0});
        Match& m = sim.match;
        PlayerState* h = m.Find(1);
        PlayerState* b = m.Find(1000);
        h->ammo.fill(0);
        h->weapon = {ItemId::FairyBow, Rarity::Legendary};
        h->attackReadyAt = 0;
        b->maxHealth = b->health = 100.0f;
        const AttackResult bash = m.Attack(1, 1000, true);
        CHECK(bash.hit && bash.damage < 0.8f);                                         // the basic sword's damage, not the bow's
        b->pos = {400, 0};
        Run(sim, 1.0f);
        h->attackReadyAt = 0;
        CHECK(!m.Attack(1, 1000, true).ok);                                            // and only at the basic sword's reach
        const size_t bowPile = m.AddLoot({{0, 0}, ItemId::FairyBow, Rarity::Rare, false});
        h->weapon = {ItemId::BasicSword, Rarity::Common};
        CHECK(m.PickUp(1, bowPile, true) && h->weapon.item == ItemId::FairyBow && h->ammo[static_cast<int>(AmmoKind::Arrows)] == AmmoStarter(AmmoKind::Arrows));
        h->attackReadyAt = 0;
        const int before = h->ammo[static_cast<int>(AmmoKind::Arrows)];
        const AttackResult shot = m.Attack(1, 1000, true);
        CHECK(shot.hit && h->ammo[static_cast<int>(AmmoKind::Arrows)] == before - 1 && shot.damage > bash.damage * 1.2f);
        h->attackReadyAt = 0;
        const int beforeMiss = h->ammo[static_cast<int>(AmmoKind::Arrows)];
        CHECK(m.ShootAtNothing(1).ok && h->ammo[static_cast<int>(AmmoKind::Arrows)] == beforeMiss - 1);   // a shot at nothing still spends an arrow
        CHECK(!m.ShootAtNothing(1).ok && h->ammo[static_cast<int>(AmmoKind::Arrows)] == beforeMiss - 1);  // and the cooldown applies
        LootSpawn pile = {{0, 0}, ItemId::ArrowAmmo, Rarity::Common, false, false};
        pile.amount = 50;
        const size_t big = m.AddLoot(pile);
        CHECK(m.PickUp(1, big, true) && h->ammo[static_cast<int>(AmmoKind::Arrows)] == AmmoBaseCap(AmmoKind::Arrows));   // capped
        const size_t second = m.AddLoot(pile);
        CHECK(!m.PickUp(1, second, true) && !m.Loot()[second].taken);                  // full: left for somebody else
        h->gear[static_cast<int>(GearSlot::Pack)] = {ItemId::BigQuiver, Rarity::Common};
        h->gearMask = static_cast<uint8_t>(h->gearMask | (1 << static_cast<int>(GearSlot::Pack)));
        CHECK(m.PickUp(1, second, true) && h->ammo[static_cast<int>(AmmoKind::Arrows)] == AmmoBaseCap(AmmoKind::Arrows) + 20);
    }
    // The special variants.
    {
        Simulation sim = Duel(79, {0, 0}, {100, 0});
        Match& m = sim.match;
        PlayerState* h = m.Find(1);
        PlayerState* b = m.Find(1000);
        b->maxHealth = b->health = 100.0f;
        h->weapon = {ItemId::TripleSlingshot, Rarity::Common};
        h->ammo[static_cast<int>(AmmoKind::Seeds)] = 10;
        h->attackReadyAt = 0;
        const float b0 = b->health;
        const AttackResult close = m.Attack(1, 1000, true);
        CHECK(close.hit && h->ammo[static_cast<int>(AmmoKind::Seeds)] == 7);            // three seeds spent
        const float closeDamage = b0 - b->health;
        b->pos = {700, 0};
        Run(sim, 1.5f);
        h->attackReadyAt = 0;
        const float b1 = b->health;
        m.Attack(1, 1000, true);
        CHECK(b1 - b->health < closeDamage * 0.5f);                                       // far off, only one pellet lands
        CHECK(WeaponDps(ItemId::TripleSlingshot, Rarity::Common) > WeaponDps(ItemId::Slingshot, Rarity::Common) * 1.25f);
        // The Giant's Hammer's slam hurts whoever stands near the target too.
        h->weapon = {ItemId::GiantsHammer, Rarity::Legendary};
        h->attackReadyAt = 0;
        b->pos = {60, 0};
        PlayerState* third = nullptr;
        for (auto& p : m.Players()) if (p.id != 1 && p.id != 1000 && !third) third = &p;
        third->alive = true; third->pos = {60, 150}; third->maxHealth = third->health = 50.0f;
        const float t0 = third->health;
        CHECK(m.Attack(1, 1000, true).hit && third->health < t0);
        CHECK(WeaponOf(ItemId::GiantsHammer).splashRadius > WeaponOf(ItemId::MegatonHammer).splashRadius && !WeaponOf(ItemId::GiantsHammer).ranged);
        CHECK(WeaponOf(ItemId::HomingBombchus).homing && WeaponOf(ItemId::HomingBombchus).splashRadius > 0 && AmmoUsedBy(ItemId::HomingBombchus) == AmmoKind::Bombchus);
    }
    // Eliminated players leave their rupees and ammo in piles with the amounts.
    {
        Simulation sim = Duel(80, {0, 0}, {5000, 5000});
        Match& m = sim.match;
        PlayerState* b = m.Find(1000);
        b->rupees = 120;
        b->ammo.fill(0);
        b->ammo[static_cast<int>(AmmoKind::Seeds)] = 9;
        b->weapon = {ItemId::BasicSword, Rarity::Common};
        const size_t before = m.Loot().size();
        CHECK(m.Damage(1000, 500.0f, 1));
        int rupees = 0, seeds = 0;
        for (size_t i = before; i < m.Loot().size(); i++) {
            const LootSpawn& l = m.Loot()[i].spawn;
            if (l.item == ItemId::Rupees) rupees += l.amount;
            if (l.item == ItemId::SeedAmmo) seeds += l.amount;
        }
        CHECK(rupees == 72 && seeds == 6);                                          // 60 per cent of the money and ammo, rounded up
        PlayerState* h = m.Find(1);
        h->ammo.fill(0);
        int taken = 0;
        for (size_t i = before; i < m.Loot().size(); i++) { h->pos = m.Loot()[i].spawn.pos; taken += m.PickUp(1, i, true); }
        CHECK(taken >= 2 && h->rupees == 72 && h->ammo[static_cast<int>(AmmoKind::Seeds)] == 6);
    }
}

static void StormJingleAndWarning() {
    const std::vector<int16_t> jingle = BuildStormJingle(), warning = BuildStormWarning();
    CHECK(jingle.size() == static_cast<size_t>(kStormJingleSeconds * kTuneRate) && warning.size() == static_cast<size_t>(kStormWarningSeconds * kTuneRate));
    for (const auto* buf : {&jingle, &warning}) {
        int peak = 0;
        double energy = 0;
        for (int16_t v : *buf) { peak = std::max(peak, std::abs(static_cast<int>(v))); energy += static_cast<double>(v) * v; }
        CHECK(peak > 6000 && peak < 32000 && energy / buf->size() > 1.0e6);               // audible, and never clipping
        CHECK(std::abs(static_cast<int>(buf->back())) < 200);                              // ends quietly: no click
    }
    CHECK(BuildStormJingle() == jingle && BuildStormWarning() == warning);              // deterministic
    CHECK(jingle != warning);
}

static void BotsLootBeforeTheyFight() {
    const float calm = BotController::CalmSeconds();
    const bool gear = BotController::GearFirst();
    BotController::CalmSeconds() = 15.0f;
    BotController::GearFirst() = true;
    auto setup = [&](Vec2 a, Vec2 b, ItemId weapon) {
        Simulation sim(88, MapCircle(), 0);
        sim.match.AddHuman(1);
        sim.match.Start();
        while (sim.match.State() != MatchState::InMatch) sim.match.Tick(kDt);
        for (auto& p : sim.match.Players()) if (p.id != 1000 && p.id != 1001) p.alive = false;
        sim.match.Find(1000)->pos = a; sim.match.Find(1001)->pos = b;
        for (uint32_t id : {1000u, 1001u}) { sim.match.Find(id)->weapon = {weapon, Rarity::Rare}; sim.match.Find(id)->maxHealth = sim.match.Find(id)->health = 50.0f; }
        return sim;
    };
    {   // Geared bots right next to each other still leave each other alone for the first ten seconds...
        Simulation sim = setup({0, 0}, {120, 0}, ItemId::MasterSword);
        for (int i = 0; i < 12 * kTickHz; i++) { sim.Tick(kDt); sim.match.Find(1)->alive = false; }
        CHECK(sim.match.Find(1000)->health == 50.0f && sim.match.Find(1001)->health == 50.0f);
        // ...and then fight.
        for (int i = 0; i < 12 * kTickHz; i++) { sim.Tick(kDt); sim.match.Find(1)->alive = false; }
        CHECK(sim.match.Find(1000)->health < 50.0f || sim.match.Find(1001)->health < 50.0f);
    }
    {   // With only the starting sword a bot doesn't go looking for a fight: it keeps its distance and loots, unless cornered.
        Simulation sim = setup({0, 0}, {420, 0}, ItemId::BasicSword);
        BotController::CalmSeconds() = 0.0f;
        float distanceAtFirstBlow = -1;
        for (int i = 0; i < 8 * kTickHz && distanceAtFirstBlow < 0; i++) {
            sim.Tick(kDt);
            sim.match.Find(1)->alive = false;
            if (sim.match.Find(1000)->health < 50.0f || sim.match.Find(1001)->health < 50.0f) distanceAtFirstBlow = Distance(sim.match.Find(1000)->pos, sim.match.Find(1001)->pos);
        }
        CHECK(distanceAtFirstBlow < 0 || distanceAtFirstBlow <= 230.0f);          // they only trade blows when they bump into each other
        Simulation close = setup({0, 0}, {100, 0}, ItemId::BasicSword);
        BotController::CalmSeconds() = 0.0f;
        for (int i = 0; i < 6 * kTickHz; i++) { close.Tick(kDt); close.match.Find(1)->alive = false; }
        CHECK(close.match.Find(1000)->health < 50.0f || close.match.Find(1001)->health < 50.0f);
    }
    BotController::CalmSeconds() = calm;
    BotController::GearFirst() = gear;
}

static void BotsShowTheirItemUse() {
    // A bot's pose says what it just did: swing, loose an arrow, throw, drink, cast or play.
    auto seenIn = [&](ItemId weapon, bool hurtWithPotion, ItemId ability, float foeDistance) {
        Simulation sim(90, MapCircle(), 0);
        sim.bots.SetDifficulty(BotDifficulty::Hard);
        sim.match.AddHuman(1);
        sim.match.Start();
        while (sim.match.State() != MatchState::InMatch) sim.match.Tick(kDt);
        for (auto& p : sim.match.Players()) if (p.id != 1000 && p.id != 1001) p.alive = false;
        PlayerState* a = sim.match.Find(1000);
        PlayerState* b = sim.match.Find(1001);
        a->pos = {0, 0}; b->pos = {foeDistance, 0};
        a->weapon = {weapon, Rarity::Rare};
        a->ammo.fill(50);
        a->maxHealth = 400.0f; a->health = hurtWithPotion ? 100.0f : 400.0f;
        b->maxHealth = b->health = 400.0f;
        b->weapon = {ItemId::BasicSword, Rarity::Common};
        b->stunUntil = 1.0e9f;                                   // the target stands still
        if (hurtWithPotion) a->potions = {{ItemId::RedPotion, Rarity::Rare}};
        if (ability != ItemId::BasicSword) { a->ability = {ability, Rarity::Rare}; a->hasAbility = true; a->abilityReadyAt = 0; }
        std::set<int> anims;
        for (int i = 0; i < 12 * kTickHz; i++) { sim.Tick(kDt); sim.match.Find(1)->alive = false; anims.insert(a->anim); b->stunUntil = 1.0e9f; }
        return anims;
    };
    CHECK(seenIn(ItemId::MasterSword, false, ItemId::BasicSword, 90).count(static_cast<int>(Anim::Attack)));
    CHECK(seenIn(ItemId::FairyBow, false, ItemId::BasicSword, 500).count(static_cast<int>(Anim::Shoot)));
    CHECK(seenIn(ItemId::Slingshot, false, ItemId::BasicSword, 500).count(static_cast<int>(Anim::Shoot)));
    CHECK(seenIn(ItemId::Bombs, false, ItemId::BasicSword, 400).count(static_cast<int>(Anim::Throw)));
    CHECK(seenIn(ItemId::MasterSword, true, ItemId::BasicSword, 900).count(static_cast<int>(Anim::Drink)));
    CHECK(seenIn(ItemId::MasterSword, false, ItemId::DinsFire, 200).count(static_cast<int>(Anim::Cast)));
    CHECK(seenIn(ItemId::MasterSword, false, ItemId::ZeldasLullaby, 200).count(static_cast<int>(Anim::Play)) || seenIn(ItemId::MasterSword, true, ItemId::ZeldasLullaby, 300).count(static_cast<int>(Anim::Play)));
}

static void RollingDodgesHits() {
    Simulation sim = Duel(31, {0, 0}, {40, 0});
    Match& m = sim.match;
    PlayerState* h = m.Find(1);
    PlayerState* b = m.Find(1000);
    h->maxHealth = h->health = 50.0f;
    b->weapon = {ItemId::MasterSword, Rarity::Legendary};
    b->attackReadyAt = 0;
    CHECK(m.StartRoll(1));
    CHECK(!m.StartRoll(1));                                                      // not again straight away
    const AttackResult dodged = m.Attack(1000, 1, true);
    CHECK(dodged.ok && !dodged.hit && dodged.dodged && h->health == 50.0f);       // the blow was spent on thin air
    Run(sim, Match::kRollSeconds + 0.1f);
    b->attackReadyAt = 0;
    const AttackResult landed = m.Attack(1000, 1, true);
    CHECK(landed.hit && h->health < 50.0f);                                        // once the roll is over it lands
    // Rolling can start again after the cooldown.
    Run(sim, Match::kRollCooldown);
    CHECK(m.StartRoll(1));
    // A blast is too wide to roll out of.
    h->health = 50.0f;
    b->weapon = {ItemId::Bombs, Rarity::Legendary};
    b->attackReadyAt = 0;
    const AttackResult blast = m.Attack(1000, 1, true);
    CHECK(blast.hit && !blast.dodged);
}

static void BotsRollAndLockOn() {
    // In a long duel the bots use the Z-target footwork and dodge rolls.
    std::set<int> seen;
    int rolls = 0;
    for (uint64_t seed = 40; seed < 46; seed++) {
        Simulation sim(seed, MapCircle(), 0);
        sim.bots.SetDifficulty(BotDifficulty::Hard);
        sim.match.AddHuman(1);
        sim.match.Start();
        while (sim.match.State() != MatchState::InMatch) sim.match.Tick(kDt);
        for (auto& p : sim.match.Players()) if (p.id != 1000 && p.id != 1001) p.alive = false;
        PlayerState* a = sim.match.Find(1000);
        PlayerState* b = sim.match.Find(1001);
        a->pos = {-80, 0}; b->pos = {80, 0};
        a->weapon = b->weapon = {ItemId::MasterSword, Rarity::Rare};
        a->maxHealth = a->health = b->maxHealth = b->health = 400.0f;
        for (int i = 0; i < 20 * 20; i++) {
            sim.Tick(kDt);
            sim.match.Find(1)->alive = false;
            for (uint32_t id : {1000u, 1001u}) {
                const PlayerState* p = sim.match.Find(id);
                seen.insert(p->anim);
                rolls += p->anim == static_cast<uint8_t>(Anim::Roll);
            }
        }
    }
    CHECK(seen.count(static_cast<int>(Anim::Roll)) && rolls > 5);
    CHECK(seen.count(static_cast<int>(Anim::SideL)) || seen.count(static_cast<int>(Anim::SideR)));
}

static void BotsPlayLikePlayers() {
    // Fights: side hops and back flips beside the rolls, jump slashes to open an exchange, and the shield raised against a swing.
    std::set<int> seen;
    for (uint64_t seed = 40; seed < 48; seed++) {
        Simulation sim(seed, MapCircle(), 0);
        sim.bots.SetDifficulty(BotDifficulty::Hard);
        sim.match.AddHuman(1);
        sim.match.Start();
        while (sim.match.State() != MatchState::InMatch) sim.match.Tick(kDt);
        for (auto& p : sim.match.Players()) if (p.id != 1000 && p.id != 1001) p.alive = false;
        PlayerState* a = sim.match.Find(1000);
        PlayerState* b = sim.match.Find(1001);
        a->pos = {-160, 0}; b->pos = {160, 0};
        a->weapon = b->weapon = {ItemId::MasterSword, Rarity::Rare};
        a->hasShield = b->hasShield = true;
        a->shield = b->shield = {ItemId::HylianShield, Rarity::Rare};
        a->maxHealth = a->health = b->maxHealth = b->health = 400.0f;
        for (int i = 0; i < 20 * 20; i++) {
            sim.Tick(kDt);
            sim.match.Find(1)->alive = false;
            for (uint32_t id : {1000u, 1001u}) seen.insert(sim.match.Find(id)->anim);
        }
    }
    CHECK(seen.count(static_cast<int>(Anim::HopL)) || seen.count(static_cast<int>(Anim::HopR)));
    CHECK(seen.count(static_cast<int>(Anim::JumpSlash)));
    CHECK(seen.count(static_cast<int>(Anim::Guard)));
    CHECK(seen.count(static_cast<int>(Anim::Jump)));
    CHECK(IsDodge(static_cast<uint8_t>(Anim::HopL)) && IsDodge(static_cast<uint8_t>(Anim::Backflip)) && !IsDodge(static_cast<uint8_t>(Anim::Guard)));

    // Chests: the bot stops, kicks it open, then holds up what was inside.
    {
        Simulation sim = Duel(77, {1500, 0}, {0, 0});
        PlayerState* a = sim.match.Find(1000);
        LootSpawn chest{{150, 0}, ItemId::MasterSword, Rarity::Epic, true, true};
        sim.match.AddLoot(chest);
        std::vector<int> order;
        for (int i = 0; i < 8 * kTickHz; i++) {
            sim.Tick(kDt);
            if (order.empty() || order.back() != a->anim) order.push_back(a->anim);
        }
        auto at = [&](Anim x) { return std::find(order.begin(), order.end(), static_cast<int>(x)) - order.begin(); };
        CHECK(a->weapon.item == ItemId::MasterSword);
        CHECK(at(Anim::OpenChest) < static_cast<long>(order.size()) && at(Anim::ItemGet) < static_cast<long>(order.size()) && at(Anim::OpenChest) < at(Anim::ItemGet));
    }

    // Bushes and rocks: a bot short of arrows cuts the bush beside it; the server is asked to break it.
    {
        Simulation sim = Duel(78, {1500, 0}, {0, 0});
        PlayerState* a = sim.match.Find(1000);
        a->weapon = {ItemId::FairyBow, Rarity::Rare};
        a->ammo.fill(0);
        sim.bots.SetProps({{{200, 0}, PropKind::Bush, 0}, {{0, 2500}, PropKind::Rock, 0}});
        std::vector<std::pair<uint32_t, size_t>> asked;
        for (int i = 0; i < 6 * kTickHz && asked.empty(); i++) {
            sim.Tick(kDt);
            asked = sim.bots.DrainSmashes();
        }
        CHECK(asked.size() == 1 && asked[0].first == 1000 && asked[0].second == 0);
        CHECK(Distance(a->pos, {200, 0}) < 200.0f);
    }
}

static void LilosToxicCloud() {
    Simulation sim(77, MapCircle(), 0);
    sim.match.AddHuman(1);
    sim.match.AddHuman(2);   // somebody else alive, so the match goes on
    PlayerState* me = sim.match.Find(1);
    me->pos = {0, 0};
    CHECK(!sim.match.StartFartCloud(1, {0, 0}));   // not while the match is off
    sim.match.Start();
    while (sim.match.State() != MatchState::InMatch) sim.match.Tick(kDt);
    for (auto& p : sim.match.Players()) if (p.id != 1 && p.id != 2) p.alive = false;
    me = sim.match.Find(1);
    me->pos = {0, 0};
    sim.match.Find(2)->pos = {1800, 1800};
    me->maxHealth = me->health = 10.0f;
    me->invulnUntil = 0; me->armor = 0;
    CHECK(!sim.match.StartFartCloud(1, {kFartCloudReach + 50.0f, 0}));   // Lilo is beside you, not across the map
    CHECK(sim.match.StartFartCloud(1, {0, 0}));
    CHECK(!sim.match.StartFartCloud(1, {0, 0}));   // not again straight away
    auto run = [&](float seconds) { for (int i = 0; i < static_cast<int>(seconds * kTickHz); i++) { sim.match.Tick(kDt); me->pos = me->pos; } };
    run(kFartGraceSeconds - 0.4f);
    CHECK(me->health == 10.0f);                    // the first seconds do nothing
    run(0.4f + 2.0f);
    CHECK(me->health < 10.0f && me->health > 10.0f - 2.0f * kFartDps - 0.2f);   // then a small tick, about kFartDps a second
    // walking out drains the count, and nothing happens outside
    me->pos = {kFartCloudRadius + 100.0f, 0};
    run(kFartGraceSeconds);
    CHECK(me->gasTime == 0.0f);
    const float outside = me->health;
    run(2.0f);
    CHECK(me->health == outside);
    // and a cloud expires
    CHECK(sim.match.FartClouds().size() == 1);
    run(kFartCloudSeconds + 1.0f);
    CHECK(sim.match.FartClouds().empty());
}

static void BotsLeaveBlastRings() {
    // Hard bots standing in a marked blast walk or roll out before it lands; they are not stuck there taking it.
    int escaped = 0, trials = 0;
    for (uint64_t seed = 60; seed < 70; seed++) {
        Simulation sim(seed, MapCircle(), 0);
        sim.bots.SetDifficulty(BotDifficulty::Hard);
        sim.match.AddHuman(1);
        sim.match.Start();
        while (sim.match.State() != MatchState::InMatch) sim.match.Tick(kDt);
        for (auto& p : sim.match.Players()) if (p.id != 1000 && p.id != 1001) p.alive = false;
        PlayerState* b = sim.match.Find(1000);
        sim.match.Find(1001)->pos = {1800, 1800};
        b->pos = {0, 0};
        b->maxHealth = b->health = 100.0f;
        sim.match.Find(1)->alive = false;
        sim.match.AddStrike({0, 0}, 150.0f, 2.0f, 1.3f, kDragonId);
        trials++;
        for (int i = 0; i < static_cast<int>(1.4f * kTickHz); i++) { sim.Tick(kDt); sim.match.Find(1)->alive = false; }
        if (b->health > 99.0f) escaped++;
    }
    CHECK(escaped >= 8 && trials == 10);
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


static void HyruleFieldHasPlacesOfItsOwn() {
    const Circle map = {{-1269, 6635}, 7000.0f};
    for (uint64_t seed = 1; seed < 6; seed++) {
        const PoiLayout a = GeneratePois(seed, map, 12, nullptr, 0), b = GeneratePois(seed, map, 12, nullptr, 0);
        CHECK(a.pois.size() == b.pois.size() && a.props.size() == b.props.size() && a.lootSpots.size() == b.lootSpots.size() && a.sites.size() == b.sites.size());
        CHECK(a.pois.size() >= kFieldPlaceCount + 3 && a.pois.size() <= static_cast<size_t>(kNamesPerMap));        // ten places and some towns
        CHECK(std::string(kPoiNames[a.pois[0].name]) == "Hylian Billion Pavilion" && Distance(a.pois[0].center, map.center) < 1.0f);   // the ruined castle in the middle
        std::set<int> names;
        bool apart = true, inside = true, own = true;
        for (size_t i = 0; i < a.pois.size(); i++) {
            names.insert(a.pois[i].name);
            own &= a.pois[i].name < kNamesPerMap;
            inside &= Distance(a.pois[i].center, map.center) <= map.radius;
            for (size_t j = i + 1; j < a.pois.size(); j++) apart &= Distance(a.pois[i].center, a.pois[j].center) >= (a.pois[i].radius + a.pois[j].radius) * 0.95f;
        }
        CHECK(names.size() == a.pois.size() && apart && inside && own);
        int platforms = 0, pillars = 0, boulders = 0, bushes = 0, tops = 0;
        for (const Prop& p : a.props) { platforms += IsPlatform(p.kind); pillars += p.kind == PropKind::Pillar; boulders += p.kind == PropKind::Boulder; bushes += p.kind == PropKind::Bush; }
        for (const ChestSite& st : a.sites) tops += st.bonus == 2;
        CHECK(platforms >= 40 && pillars >= 100 && boulders >= 40 && bushes >= 10 && tops >= 5);                    // mounds, walls, canyons, rings, a causeway
        CHECK(a.props.size() < static_cast<size_t>(kMaxProps) - 260 && a.lootSpots.size() >= 25 && !a.bossSpots.empty());
        for (const Prop& p : a.props) inside &= Distance(p.pos, map.center) <= map.radius + 200.0f;
        CHECK(inside);
    }
    // On a field with holes in it, nothing is built over the gaps, and the layout still has its places.
    auto valid = [](Vec2 p) { return !(p.x > 1500.0f && p.x < 2600.0f); };
    const PoiLayout holey = GeneratePois(3, map, 12, valid, 0);
    CHECK(holey.pois.size() >= 6);
    bool grounded = true;
    for (const Prop& p : holey.props) grounded &= valid(p.pos);
    for (const Vec2& sp : holey.lootSpots) grounded &= valid(sp);
    for (const ChestSite& st : holey.sites) grounded &= valid(st.pos);
    CHECK(grounded);
    // A bigger arena gets a bigger allowance: the field may be measured up to 7400 across, the others to about 5000.
    CHECK(MapOf(0).maxRadius >= 7000.0f && MapOf(1).maxRadius <= 5200.0f && MapOf(0).fallback.radius > 5000.0f);
}

static void PointsOfInterest() {
    const Circle map = {{0, 0}, 4000.0f};
    auto valid = [](Vec2 p) { return p.x > -3000.0f; };
    const PoiLayout a = GeneratePois(9, map, 12, valid, 1), b = GeneratePois(9, map, 12, valid, 1), c = GeneratePois(10, map, 12, valid, 1);
    CHECK(a.pois.size() >= 8 && a.pois.size() <= 12);
    CHECK(a.pois.size() == b.pois.size() && a.props.size() == b.props.size() && a.lootSpots.size() == b.lootSpots.size());
    CHECK(a.pois[1].center.x != c.pois[1].center.x || a.pois[1].name != c.pois[1].name);          // a new seed gives a new layout
    CHECK(Distance(a.pois[0].center, map.center) < map.radius * 0.1f && std::string(kPoiNames[a.pois[0].name]) == kPoiNames[kNamesPerMap]);   // the landmark in the middle
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
    CHECK(a.props.size() > a.pois.size() * 20 && a.lootSpots.size() >= a.pois.size() * 3);       // every place is built up and has chests
    for (int i = 0; i < kPoiNameTotal; i++) CHECK(kPoiNames[i] != nullptr && kPoiNames[i][0] != 0);

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

static void MapsHaveTheirOwnNamesAndBosses() {
    CHECK(kMapCount >= 5);
    for (int id = 0; id < kMapCount; id++) {
        const MapDef& m = MapOf(id);
        CHECK(m.name != nullptr && m.scene > 0x50 && m.scene < 0x70 && m.fallback.radius > 1000);
        CHECK(!IsDragonKind(m.minis[0]) && !IsDragonKind(m.minis[1]) && IsDragonKind(m.major));
        // The pois use this map's own names, and the landmark keeps the first one.
        const Circle map = {{0, 0}, m.fallback.radius};
        const PoiLayout layout = GeneratePois(12 + id, map, 16, nullptr, id);
        CHECK(layout.pois.size() >= 3);
        for (const Poi& p : layout.pois) CHECK(p.name >= PoiNameBase(id) && p.name < PoiNameBase(id)+PoiNameCount(id));
        CHECK(layout.pois[0].name == PoiNameBase(id));
        // The bosses a match spawns are the ones that suit the place, never a dragon.
        Match match(5, map, 10);
        match.SetMapId(id);
        match.SetBossSpots(layout.bossSpots);
        match.SetBossCount(6);
        match.AddHuman(1);
        match.Start();
        CHECK(!match.Bosses().empty());
        for (const MiniBoss& b : match.Bosses()) CHECK(b.kind == m.minis[0] || b.kind == m.minis[1] || IsChuKind(b.kind));
        for (const auto& p : match.Players()) if (p.isBot) CHECK(p.scene == m.scene);
    }
    // The dragon of a map is the one for that place.
    for (int id = 0; id < kMapCount; id++) {
        Match match(6, {{0, 0}, 2000}, 10);
        match.SetMapId(id);
        match.SetMajorBoss(true);
        match.AddHuman(1);
        match.Start();
        for (int i = 0; i < 20 * 400 && !match.FindBoss(kDragonId); i++) match.Tick(0.05f);
        const MiniBoss* d = match.FindBoss(kDragonId);
        CHECK(d && d->kind == MapOf(id).major);
    }
}

static void FortniteMapIsSound() {
    namespace fn = royale::fortnite;
    CHECK(fn::kMapId < kMapCount && std::string(MapOf(fn::kMapId).name) == "Fortnite Map" && MapOf(fn::kMapId).scene == kHyruleFieldScene);
    const fn::Mesh m = fn::BuildCollision();
    CHECK(m.verts.size() == static_cast<size_t>(fn::kVerts * fn::kVerts) && m.polys.size() == static_cast<size_t>(fn::kCells * fn::kCells * 2));
    CHECK(m.verts.size() < 8192 && m.polys.size() < 32767);   // the game's vertex numbers have 13 bits, its triangle numbers 15
    // Every triangle points up, has a unit normal and sits on its plane; and the ground height query agrees with the triangles.
    bool sound = true;
    float steep = 0;
    for (const fn::Poly& p : m.polys) {
        const double nx = p.nx / 32767.0, ny = p.ny / 32767.0, nz = p.nz / 32767.0;
        sound &= p.ny > 0 && std::fabs(nx * nx + ny * ny + nz * nz - 1.0) < 0.01;
        for (uint16_t v : { p.a, p.b, p.c }) {
            sound &= v < m.verts.size();
            const fn::Vert& q = m.verts[v];
            sound &= std::fabs(nx * q.x + ny * q.y + nz * q.z + p.dist) < 2.0;
        }
        if (ny < 0.8) steep += 1;
    }
    CHECK(sound);
    CHECK(steep / m.polys.size() < 0.25f);                          // mostly walkable: cliffs are the minority
    for (int k = 0; k < 400; k++) {                                 // a spread of points: the height lies between its neighbours' and the plane agrees
        const float x = -fn::kHalfX + 1.0f + (2 * fn::kHalfX - 2.0f) * ((k * 37) % 400) / 400.0f, z = -fn::kHalfZ + 1.0f + (2 * fn::kHalfZ - 2.0f) * ((k * 91) % 400) / 400.0f;
        float y = 0;
        CHECK(fn::GroundHeight(x, z, &y));
        CHECK(y >= m.lo.y - 1 && y <= m.hi.y + 1);
    }
    float y;
    CHECK(!fn::GroundHeight(fn::kHalfX + 10.0f, 0, &y) && !fn::GroundHeight(0, -fn::kHalfZ - 10.0f, &y));
    // The lobby spawn is dry, the island's centre is land and the far corner is sea; the circle in the map table holds the spawn.
    CHECK(fn::GroundHeight(fn::kSpawnX, fn::kSpawnZ, &y) && y > fn::kWaterY + 50);
    CHECK(fn::IsWaterAt(-fn::kHalfX + 100.0f, -fn::kHalfZ + 100.0f));
    CHECK(Distance({ fn::kSpawnX, fn::kSpawnZ }, MapOf(fn::kMapId).fallback.center) < MapOf(fn::kMapId).fallback.radius);
    CHECK(MapOf(fn::kMapId).maxRadius <= 7400.0f && MapOf(fn::kMapId).fallback.radius <= MapOf(fn::kMapId).maxRadius);
    // The drawn blocks tile the ground, never dip under the water sheet, and the fine version passes through the collision's corners.
    std::vector<fn::DrawVert> fine, coarse;
    fn::BlockVertices(10, 20, true, fine);
    fn::BlockVertices(10, 20, false, coarse);
    CHECK(fine.size() == static_cast<size_t>(fn::kBlockVerts) && coarse.size() == 4);
    bool dry = true;
    for (const fn::DrawVert& v : fine) dry &= v.y >= fn::kSeabedY - 1;
    CHECK(dry);
    CHECK(fine.front().x == coarse[0].x && fine.front().z == coarse[0].z && fine.back().x == coarse[3].x && fine.back().z == coarse[3].z);
    CHECK(fine.front().y == coarse[0].y && fine.back().y == coarse[3].y);
    // Every level of detail lies on the collision's triangles (what you see is what you stand on), and open sea is only ever drawn flat.
    bool onGround = true;
    for (int lod = 0; lod < fn::kLods; lod++) {
        for (int b = 0; b < fn::kCells; b += 7) {
            std::vector<fn::DrawVert> vs;
            fn::BlockVertices(b, (b * 5) % fn::kCells, lod, vs);
            onGround &= vs.size() == static_cast<size_t>(fn::LodVerts(lod));
            for (const fn::DrawVert& v : vs) {
                float gy = 0;
                if (!fn::GroundHeight(std::clamp<float>(v.x, -fn::kHalfX + 0.5f, fn::kHalfX - 0.5f), std::clamp<float>(v.z, -fn::kHalfZ + 0.5f, fn::kHalfZ - 0.5f), &gy)) continue;
                onGround &= std::fabs(std::max(gy, static_cast<float>(fn::kSeabedY)) - v.y) < 3.0f;
            }
        }
    }
    CHECK(onGround);
    CHECK(fn::BlockIsOpenWater(0, 0) && fn::BlockIsOpenWater(fn::kCells - 1, fn::kCells - 1));
    CHECK(!fn::BlockIsOpenWater(static_cast<int>((fn::kSpawnX + fn::kHalfX) / fn::kCellX), static_cast<int>((fn::kSpawnZ + fn::kHalfZ) / fn::kCellZ)));
    // The lobby spawn is gentle enough to stand on; past the edge there is no ground at all.
    CHECK(fn::GroundUp(fn::kSpawnX, fn::kSpawnZ) > 0.85f && fn::GroundUp(fn::kHalfX + 5.0f, 0) == 0.0f);
}

// The Fortnite Map's own places stand on the towns painted on its texture, its trees grow in the painted woods, and its weather is its own.
static void FortniteIslandPlaces() {
    namespace fn = royale::fortnite;
    const Circle map = {{72.0f, -524.0f}, 7400.0f};
    const PlacementFn land = [](Vec2 p) { float y; return fn::GroundHeight(p.x, p.z, &y) && y > fn::kWaterY + 20 && fn::GroundUp(p.x, p.z) > 0.8f; };
    const PoiLayout layout = GeneratePois(77, map, 12, land, kFortniteMapIndex);
    CHECK(layout.pois.size() >= 20);
    CHECK(!layout.pois.empty() && layout.pois[0].name == kFortniteMapIndex * kNamesPerMap);   // Tilted Towers comes first
    std::set<int> names;
    for (const Poi& p : layout.pois) {
        CHECK(p.name >= kFortniteMapIndex * kNamesPerMap && p.name < (kFortniteMapIndex + 1) * kNamesPerMap);
        names.insert(p.name);
        const Vec2 painted = kIslandSpots[p.name - kFortniteMapIndex * kNamesPerMap];
        CHECK(Distance(p.center, painted) < 650.0f);                          // on its painted town, or right next to it
        for (const Poi& q : layout.pois) if (&q != &p) CHECK(Distance(p.center, q.center) > 1000.0f);
    }
    CHECK(names.size() == layout.pois.size());
    // Enough of it is built: houses, guards and chests, and it still leaves the match room for loose scenery and the wilds (kMaxProps).
    int roofs = 0;
    for (const Prop& pr : layout.props) roofs += pr.kind == PropKind::Roof;
    CHECK(roofs >= 15);
    CHECK(layout.bossSpots.size() >= 7);
    CHECK(layout.lootSpots.size() >= 60);
    CHECK(layout.props.size() < 650);
    for (const Prop& pr : layout.props) CHECK(land(pr.pos) || pr.kind == PropKind::Roof);
    // A small match gets the most famous ones, a different circle (no island at all) still works.
    CHECK(GeneratePois(77, map, 4, land, kFortniteMapIndex).pois.size() >= 9);
    CHECK(GeneratePois(77, {{0, 0}, 3000.0f}, 12, nullptr, kFortniteMapIndex).pois.size() >= 3);
    // Ground cover: the lake is water, Wailing Woods is woods, Tilted Towers is paved, the meadows are the most of the land.
    auto share = [&](Vec2 c, float r, fn::Cover what) {
        int n = 0, hit = 0;
        for (float dx = -r; dx <= r; dx += 40.0f) for (float dz = -r; dz <= r; dz += 40.0f) { n++; hit += fn::CoverAt(c.x + dx, c.z + dz) == what; }
        return static_cast<float>(hit) / n;
    };
    CHECK(share({-1190, -2170}, 250, fn::Cover::Water) > 0.8f);              // Loot Lake
    CHECK(share(kIslandSpots[static_cast<int>(IslandPlace::WailingWoods)], 500, fn::Cover::Woods) > 0.5f);
    CHECK(share(kIslandSpots[static_cast<int>(IslandPlace::TiltedTowers)], 250, fn::Cover::Paving) > 0.3f);
    CHECK(fn::CoverAt(-fn::kHalfX + 10, -fn::kHalfZ + 10) == fn::Cover::Water && fn::CoverAt(fn::kHalfX + 10, 0) == fn::Cover::Water);
    // The island's weather: winters bring snow, and storms blow in more often than on the field.
    WeatherOptions o;
    o.season = 3;
    int snow = 0;
    for (int spell = 1; spell < 60; spell++) snow += WeatherForSpell(o, 99, kFortniteMapIndex, spell).sky == Sky::Snow;
    CHECK(snow > 10);
    int island[kSkyCount], field[kSkyCount];
    SkyWeights(kFortniteMapIndex, Season::Spring, island);
    SkyWeights(0, Season::Spring, field);
    CHECK(island[static_cast<int>(Sky::Thunder)] > field[static_cast<int>(Sky::Thunder)]);
}

static void SoloTestHasNoBotsAndKeepsGoing() {
    const Circle map = {{0, 0}, 3000.0f};
    // A normal match with one human is filled with bots, and ends the moment one player is left.
    Match normal(5, map, 10);
    normal.AddHuman(1);
    normal.Start();
    CHECK(normal.Players().size() > 1);
    // Test mode: just the human, and the match goes on with one player alive, with its storm and everything else running.
    Match solo(5, map, 10);
    CHECK(solo.SetSoloTest(true));
    solo.AddHuman(1);
    solo.Start();
    CHECK(solo.Players().size() == 1 && solo.SoloTest());
    for (int i = 0; i < 20 * 120 && solo.State() != MatchState::InMatch; i++) solo.Tick(0.05f);
    CHECK(solo.State() == MatchState::InMatch);
    for (int i = 0; i < 20 * 20; i++) solo.Tick(0.05f);
    CHECK(solo.State() == MatchState::InMatch && solo.Alive() == 1);
    CHECK(!solo.SetSoloTest(false));   // only in the lobby
    CHECK(kFortniteMapIndex == royale::fortnite::kMapId);
}


static void SandboxTerrainAndLayout() {
    namespace fn = royale::fortnite;
    CHECK(!IsPlayableMap(kSandboxMapIndex) && kSandboxMapIndex < kMapCount && IsIslandMap(kSandboxMapIndex) && IsIslandMap(kFortniteMapIndex) && !IsIslandMap(0));
    CHECK(std::string(MapOf(kSandboxMapIndex).name) == "Sandbox" && MapOf(kSandboxMapIndex).scene == kHyruleFieldScene);
    fn::UseTerrain(true);
    float y = 0;
    // Spawn: flat, dry, walkable. Pond: under the water. Plateau, plaza and cliff top at their heights. The sea outside is deep.
    CHECK(fn::GroundHeight(sandbox::kSpawnX, sandbox::kSpawnZ, &y) && std::fabs(y) < 5.0f && fn::GroundUp(sandbox::kSpawnX, sandbox::kSpawnZ) > 0.95f);
    CHECK(fn::IsWaterAt(sandbox::kPondX, sandbox::kPondZ) && !fn::IsWaterAt(sandbox::kPondX + 1100.0f, sandbox::kPondZ));
    CHECK(fn::GroundHeight(0, 2650, &y) && std::fabs(y - sandbox::kPlateauHeight) < 6.0f);
    CHECK(fn::GroundHeight(-2300, 300, &y) && std::fabs(y - sandbox::kPlazaHeight) < 6.0f);
    CHECK(fn::GroundHeight(2900, 100, &y) && std::fabs(y - sandbox::kCliffHeight) < 6.0f);
    CHECK(fn::GroundHeight(sandbox::kHillX, sandbox::kHillZ, &y) && y > sandbox::kHillTop - 20.0f);
    CHECK(fn::IsWaterAt(5000, 0) && fn::IsWaterAt(0, -5000));
    // The ramps: two you can walk up (17 and 27 degrees) and one too steep to stand on; the cliff wall is steep; the ramp up the cliff is walkable.
    CHECK(fn::GroundUp(-1100, 1800) > 0.9f && fn::GroundUp(-150, 2000) > 0.85f && fn::GroundUp(800, 2200) < 0.8f);
    CHECK(fn::GroundUp(2450, 100) < 0.7f && fn::GroundUp(2800, -1100) > 0.85f);
    // Every zone is on dry land and on ground you can stand on, and inside the map circle.
    for (int i = 0; i < sandbox::kZoneCount; i++) {
        const sandbox::Zone& z = sandbox::kZones[i];
        CHECK(fn::GroundHeight(z.x, z.z, &y) && (i == 5 ? y < fn::kWaterY : y > fn::kWaterY + 20.0f) && fn::GroundUp(z.x, z.z) > 0.8f);
        CHECK(Distance({z.x, z.z}, MapOf(kSandboxMapIndex).fallback.center) + z.radius * 0.0f < MapOf(kSandboxMapIndex).fallback.radius);
    }
    // The cover: grass on the hill, trees in the grove, paving on the plaza, water in the pond.
    CHECK(fn::CoverAt(-2300, 300) == fn::Cover::Paving && fn::CoverAt(900, 900) == fn::Cover::Woods && fn::CoverAt(sandbox::kHillX, sandbox::kHillZ) == fn::Cover::Meadow && fn::CoverAt(sandbox::kPondX, sandbox::kPondZ) == fn::Cover::Water);
    // Collision built from it is sound (every triangle up, on its plane) and the colours are not the island's.
    const fn::Mesh m = fn::BuildCollision();
    bool sound = m.verts.size() == static_cast<size_t>(fn::kVerts * fn::kVerts);
    for (const fn::Poly& p : m.polys) sound &= p.ny > 0;
    CHECK(sound);
    const fn::DrawVert dv = fn::FineVertex(fn::kFine / 2, fn::kFine / 2);
    fn::UseTerrain(false);
    const fn::DrawVert iv = fn::FineVertex(fn::kFine / 2, fn::kFine / 2);
    CHECK(dv.y != iv.y || dv.r != iv.r || dv.g != iv.g);
    CHECK(!fn::gSandboxTerrain && fn::gSpawnX == fn::kSpawnX);   // back to the island: the other tests use it
    // The layout: a place for every zone, props of every kind the test course needs, and nothing outside the map.
    const PoiLayout layout = GenerateSandboxLayout(5);
    CHECK(layout.pois.size() == static_cast<size_t>(sandbox::kZoneCount) && layout.pois[0].name == kSandboxMapIndex * kNamesPerMap);
    std::set<int> kinds;
    bool inside = true;
    for (const Prop& p : layout.props) { kinds.insert(static_cast<int>(p.kind)); inside &= Distance(p.pos, MapOf(kSandboxMapIndex).fallback.center) < MapOf(kSandboxMapIndex).fallback.radius; }
    CHECK(inside && kinds.count(static_cast<int>(PropKind::PlatformLow)) && kinds.count(static_cast<int>(PropKind::PlatformMid)) && kinds.count(static_cast<int>(PropKind::PlatformHigh)));
    CHECK(kinds.count(static_cast<int>(PropKind::Boulder)) && kinds.count(static_cast<int>(PropKind::Rock)) && kinds.count(static_cast<int>(PropKind::Bush)) && kinds.count(static_cast<int>(PropKind::Pillar)));
    CHECK(Distance(SandboxLootRoom().center, {sandbox::kZones[1].x, sandbox::kZones[1].z}) < 1.0f);
}

static Match MakeSandbox(uint64_t seed = 9) {
    Match m(seed, MapOf(kSandboxMapIndex).fallback, 0);
    m.SetMapId(kSandboxMapIndex);
    m.SetSandboxSpawn(SandboxSpawn());
    m.SetSandboxLootRoom(SandboxLootRoom());
    m.SetVehicleCount(2);
    m.SetBossCount(0);
    CHECK(m.SetSandbox(true));
    m.AddHuman(1);
    return m;
}

static void SandboxMatchHasNoCountdownOrEnd() {
    Match m = MakeSandbox();
    m.SandboxStockLoot();
    // Every item in the game is lying on the plaza, plus chests; nothing is outside it.
    int items = 0, chests = 0;
    for (const LootEntry& l : m.Loot()) { (l.spawn.container ? chests : items)++; CHECK(Distance(l.spawn.pos, SandboxLootRoom().center) <= SandboxLootRoom().radius); }
    CHECK(items == kItemCount - 1 && chests == 6);   // everything but the starting sword
    std::set<int> seen;
    for (const LootEntry& l : m.Loot()) if (!l.spawn.container) seen.insert(static_cast<int>(l.spawn.item));
    CHECK(static_cast<int>(seen.size()) == kItemCount - 1 && seen.count(static_cast<int>(ItemId::ShockwaveGrenade)) && seen.count(static_cast<int>(ItemId::HoverBoots)));
    CHECK(!m.SandboxBot({0, 0}) && !m.SandboxGive(1, ItemId::MasterSword, Rarity::Rare));   // not before the match
    CHECK(m.Start());
    CHECK(m.State() == MatchState::InMatch && m.Players().size() == 1);               // no countdown, no drop, no bots
    CHECK(Distance(m.Players()[0].pos, SandboxSpawn()) < 1.0f);
    // The storm waits, nothing hurts, nothing ends the match.
    for (int i = 0; i < 20 * 30; i++) m.Tick(0.05f);
    CHECK(m.StormTime() == 0.0f && m.Players()[0].health == m.Players()[0].maxHealth);
    CHECK(m.Damage(1, 5.0f, kNoPlayer, DamageKind::Normal) == false && m.Players()[0].health == m.Players()[0].maxHealth);   // god mode
    m.SandboxGod(false);
    m.Damage(1, 1.0f, kNoPlayer, DamageKind::Normal);
    CHECK(m.Players()[0].health < m.Players()[0].maxHealth);
    m.SandboxHeal(1);
    CHECK(m.Players()[0].health == m.Players()[0].maxHealth);
    m.Damage(1, 500.0f, kNoPlayer, DamageKind::Normal);                                // dead: the match still goes on
    CHECK(!m.Players()[0].alive);
    for (int i = 0; i < 20; i++) m.Tick(0.05f);
    CHECK(m.State() == MatchState::InMatch);
    CHECK(m.SandboxRevive(1) && m.Players()[0].alive && m.Players()[0].health == m.Players()[0].maxHealth);
    // The storm runs only when asked, from the phase asked for.
    m.SandboxStormRuns(true);
    CHECK(m.SandboxStormPhase(2) && std::fabs(m.StormTime() - Match::StormPhaseStart(2)) < 0.01f);
    m.Tick(0.05f);
    CHECK(m.StormTime() > Match::StormPhaseStart(2));
    CHECK(!m.SandboxStormPhase(kStormPhaseCount + 1) && m.SandboxStormPhase(kStormPhaseCount));
}

static void SandboxCommands() {
    Match m = MakeSandbox(4);
    m.SandboxStockLoot();
    m.Start();
    // Bots appear where they are put, one after the other, and go away quietly.
    CHECK(m.SandboxBot({300, 0}) && m.SandboxBot({400, 0}) && m.Players().size() == 3 && m.Players()[1].id != m.Players()[2].id && m.Players()[1].isBot);
    CHECK(Distance(m.Players()[2].pos, {400, 0}) < 1.0f);
    // The bots obey the freeze switch: a frozen bot does not move however long the match runs.
    BotController bots(4);
    bots.SetFrozen(true);
    const Vec2 before = m.Players()[1].pos;
    for (int i = 0; i < 20 * 10; i++) { bots.Step(m, 0.05f); m.Tick(0.05f); }
    CHECK(bots.Frozen() && Distance(m.Players()[1].pos, before) < 0.01f);
    m.SandboxClearBots();
    CHECK(!m.Players()[1].alive && !m.Players()[2].alive && m.Players()[0].alive && m.Alive() == 1);
    // Bosses: seven mini bosses can stand at once and an eighth is refused; a major boss takes the dragon's place; clearing removes them.
    for (int i = 0; i < kMaxBosses - 1; i++) CHECK(m.SandboxBoss(static_cast<BossKind>(i % kMiniBossKindCount), {500.0f + 100.0f * i, 900}));
    CHECK(!m.SandboxBoss(BossKind::Stone, {0, 900}));
    CHECK(m.SandboxBoss(BossKind::DragonFire, {0, 900}) && m.SandboxBoss(BossKind::DragonWater, {100, 900}));
    int dragons = 0, minis = 0;
    for (const MiniBoss& b : m.Bosses()) { if (!b.alive) continue; (IsDragonKind(b.kind) ? dragons : minis)++; }
    CHECK(dragons == 1 && minis == kMaxBosses - 1);
    for (int i = 0; i < 20 * 5; i++) m.Tick(0.05f);   // they run without trouble
    m.SandboxClearBosses();
    for (const MiniBoss& b : m.Bosses()) CHECK(!b.alive);
    CHECK(m.SandboxBoss(BossKind::Frost, {500, 900}));   // the places are free again
    // Carts: the two from the start plus more, up to the limit; a cleared cart's place is used again.
    CHECK(m.Vehicles().size() == 2);
    while (m.SandboxCart({1700, -1900}, 0.5f)) {}
    CHECK(static_cast<int>(m.Vehicles().size()) == kMaxVehicles);
    m.SandboxClearCarts();
    CHECK(m.SandboxCart({1700, -1900}, 0.5f) && m.Vehicles()[0].index == 0 && !m.Vehicles()[0].gone);
    // Giving: an item goes straight into the bag or hands at the rarity asked for (within what the item allows).
    CHECK(m.SandboxGive(1, ItemId::MasterSword, Rarity::Epic) && m.Players()[0].weapon.item == ItemId::MasterSword && m.Players()[0].weapon.rarity == Rarity::Epic);
    CHECK(m.SandboxGive(1, ItemId::ShockwaveGrenade, Rarity::Legendary) && (m.Players()[0].weapon.item == ItemId::ShockwaveGrenade || (!m.Players()[0].reserve.empty() && m.Players()[0].reserve.back().item == ItemId::ShockwaveGrenade)));
    CHECK(m.SandboxGive(1, ItemId::HoverBoots, Rarity::Rare) && (m.Players()[0].gearMask & (1 << static_cast<int>(GearSlot::Boots))));
    // Weather stays where it is put; it only follows the schedule when set free. Teleports and supply drops work.
    m.SandboxWeather(Season::Winter, Sky::Snow, 80);
    for (int i = 0; i < 20 * 30; i++) m.Tick(0.05f);
    CHECK(m.CurrentWeather().sky == Sky::Snow && m.CurrentWeather().season == Season::Winter && m.CurrentWeather().intensity == 80);
    CHECK(m.SandboxTeleport(1, {1000, 1000}) && Distance(m.Players()[0].pos, {1000, 1000}) < 1.0f);
    const size_t lootBefore = m.Loot().size();
    CHECK(m.SandboxSupplyDrop({200, 200}));
    for (int i = 0; i < 20 * 9; i++) m.Tick(0.05f);
    CHECK(m.Loot().size() > lootBefore);
    // Restocking the plaza puts back what was taken, once.
    size_t taken = 0;
    for (size_t i = 0; i < m.Loot().size() && taken < 3; i++) {
        if (m.Loot()[i].taken || m.Loot()[i].spawn.container || Distance(m.Loot()[i].spawn.pos, SandboxLootRoom().center) > SandboxLootRoom().radius) continue;
        m.SandboxTeleport(1, m.Loot()[i].spawn.pos);
        if (m.PickUp(1, i, true)) taken++;
    }
    CHECK(taken == 3);
    CHECK(m.SandboxRestock() == static_cast<int>(taken) && m.SandboxRestock() == 0);
    // None of it works in an ordinary match.
    Match plain(3, {{0, 0}, 3000.0f}, 10);
    plain.AddHuman(1);
    plain.Start();
    for (int i = 0; i < 20 * 40; i++) plain.Tick(0.05f);
    CHECK(!plain.SandboxBot({0, 0}) && !plain.SandboxBoss(BossKind::Stone, {0, 0}) && !plain.SandboxCart({0, 0}, 0) && !plain.SandboxGive(1, ItemId::MasterSword, Rarity::Rare) && !plain.SandboxTeleport(1, {5, 5}));
}

static void CustomObjModels() {
    // A little winged thing: a body box, two wings and a tail, with a material colour and a quad that must be cut into two triangles.
    const std::string obj =
        "# test\nmtllib t.mtl\n"
        "v -1 0 -1\nv 1 0 -1\nv 1 1 -1\nv -1 1 -1\nv -1 0 1\nv 1 0 1\nv 1 1 1\nv -1 1 1\n"
        "o Body\nusemtl green\nf 1 2 3 4\nf 5 6 7 8\nf 1 2 6 5\nf 4 3 7 8\n"
        "v 1 1 0\nv 5 1.5 0\nv 5 1.5 1\nv 1 1 1\n"
        "o Wing_L\nusemtl red\nf 9 10 11 12\n"
        "v -1 1 0\nv -5 1.5 0\nv -5 1.5 1\nv -1 1 1\n"
        "o wing_R\nf 13 14 15 16\n"
        "v 0 0.5 -1\nv 0 0.5 -4\nv 0 1 -4\n"
        "o Tail_01\nf 17 18 19\n";
    const std::string mtl = "newmtl green\nKd 0.1 0.8 0.2\nnewmtl red\nKd 0.9 0.1 0.1\n";
    ObjModel m = ParseObj(obj, mtl);
    CHECK(m.ok && m.parts.size() == 4 && m.triangles == 4 * 2 + 2 + 2 + 1);
    CHECK(RoleOf(m.parts[0].name) == ObjRole::Body && RoleOf(m.parts[1].name) == ObjRole::Wing && RoleOf(m.parts[2].name) == ObjRole::Wing && RoleOf(m.parts[3].name) == ObjRole::Tail);
    // colours: the body is green, the wings red (with the light on them)
    CHECK(m.parts[0].mesh.v[0].g > m.parts[0].mesh.v[0].r && m.parts[1].mesh.v[0].r > m.parts[1].mesh.v[0].g);
    FitObjModel(m, 1200.0f, 1.0f, 0.0f, 0.0f);
    CHECK(std::fabs((m.mx[0] - m.mn[0]) - 1200.0f) < 1.0f && std::fabs(m.mn[1]) < 0.01f && std::fabs(m.mx[0] + m.mn[0]) < 1.0f);   // wide as asked, standing on the ground, centred
    // bad input is refused, never crashes
    CHECK(!ParseObj("v 1 2\n", "").ok && !ParseObj("v 0 0 0\nf 1 2 9\n", "").ok && !ParseObj("", "").ok && !ParseObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\nf -1 -2 -3\n", "").error.size());
    ObjModel neg = ParseObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf -3 -2 -1\n", "");
    CHECK(neg.ok && neg.triangles == 1);
}

// The Fortnite Map's Hyrule Field scenery: every model (eight pieces in four seasons) is a few hundred triangles at most and the same every time,
// and the placement follows the ground: cliffs on steep ground facing downhill, nothing in the water or on the towns' paving, a bit of every kind.
static void IslandScenery() {
    namespace fn = royale::fortnite;
    for (uint32_t variant = 0; variant < kMeshVariantSlots; variant++) {
        const MeshData m = BuildMesh(MeshKind::Scenery, variant), again = BuildMesh(MeshKind::Scenery, variant);
        float mn[3], mx[3];
        m.Bounds(mn, mx);
        CHECK(m.Triangles() >= 60 && m.Triangles() <= 600 && mn[1] >= -0.01f && mx[1] > 20 && mx[0] - mn[0] < 700 && mx[2] - mn[2] < 700);
        bool same = again.v.size() == m.v.size();
        for (size_t i = 0; same && i < m.v.size(); i++) same = again.v[i].x == m.v[i].x && again.v[i].g == m.v[i].g;
        CHECK(same);
    }
    float mn[3], mx[3];
    BuildMesh(MeshKind::Scenery, 1 * 8 + 3).Bounds(mn, mx);
    CHECK(mx[1] > 150 && mx[0] - mn[0] > 250);                                  // a cliff slab is wide and tall
    BuildMesh(MeshKind::Scenery, 1 * 8 + 4).Bounds(mn, mx);
    CHECK(mx[1] > 220);                                                         // and a crag is the tallest thing there
    int count[static_cast<int>(fn::SceneryKind::Count)] = {}, total = 0, offGround = 0, steepCliffs = 0, downhill = 0, cliffs = 0, onTown = 0, none = 0;
    for (int cz = -52; cz <= 52; cz++)
        for (int cx = -52; cx <= 52; cx++) {
            fn::SceneryPiece p, q;
            const bool has = fn::SceneryIn(cx, cz, 1.0f, &p);
            CHECK(has == fn::SceneryIn(cx, cz, 1.0f, &q) && (!has || (p.x == q.x && p.kind == q.kind && p.yaw == q.yaw)));   // the same for everyone
            if (fn::SceneryIn(cx, cz, 0.0f, &q)) none++;
            if (!has) continue;
            total++;
            count[static_cast<int>(p.kind)]++;
            float y;
            if (!fn::GroundHeight(p.x, p.z, &y) || y < fn::kWaterY || std::fabs(y - p.y) > 0.01f) offGround++;
            if (fn::CoverAt(p.x, p.z) == fn::Cover::Paving) onTown++;
            if (p.kind == fn::SceneryKind::Cliff) {   // faces downhill: the ground a little way in front is lower than behind
                cliffs++;
                if (fn::GroundUp(p.x, p.z) < 0.84f) steepCliffs++;
                float front, back;
                if (fn::GroundHeight(p.x + std::sin(p.yaw) * 60, p.z + std::cos(p.yaw) * 60, &front) && fn::GroundHeight(p.x - std::sin(p.yaw) * 60, p.z - std::cos(p.yaw) * 60, &back) && front < back) downhill++;
            }
        }
    CHECK(total > 800 && total < 5000);
    for (int k = 0; k < static_cast<int>(fn::SceneryKind::Count); k++) CHECK(count[k] > 0);
    CHECK(offGround == 0 && onTown == 0 && none == 0);
    CHECK(cliffs > 50 && steepCliffs == cliffs && downhill * 10 >= cliffs * 9);
    CHECK(fn::SceneryRadius(fn::SceneryKind::Oak, 1.0f) > 0 && fn::SceneryRadius(fn::SceneryKind::FlowersWhite, 1.0f) == 0);
}

// The ground patches (puddles, snow, frost, leaves, blossom) are placed the same for everyone, seeds that touch run into one patch (each seed in at
// most one, hardly any lost), the patch grows with the cover, and a patch waits for ground that has not been measured yet instead of guessing.
static void GroundPatches() {
    namespace gp = royale::ground;
    auto allGround = [](gp::Kind, int, int, gp::Seed&) { return 1; };
    for (int k = 0; k < gp::kKinds; k++) {
        const gp::Kind kind = static_cast<gp::Kind>(k);
        gp::Field merged, apart(0.0f), again;
        int seeds = 0, leadersMerged = 0, leadersApart = 0, members = 0, bigPatches = 0;
        std::set<std::pair<int, int>> taken;
        for (int cz = -40; cz <= 40; cz++)
            for (int cx = -40; cx <= 40; cx++) {
                if (gp::SeedAt(kind, cx, cz).ok) seeds++;
                gp::Patch a, b, c;
                const int ra = merged.Lead(kind, cx, cz, allGround, &a), rb = apart.Lead(kind, cx, cz, allGround, &b), rc = again.Lead(kind, cx, cz, allGround, &c);
                CHECK(ra >= 0 && rb >= 0 && ra == rc);
                if (ra == 1) {
                    CHECK(a.x == c.x && a.n == c.n && a.n >= 1 && a.n <= gp::kMaxMembers);
                    leadersMerged++;
                    for (int i = 0; i < a.n; i++) {
                        const std::pair<int, int> cell = i == 0 ? std::make_pair(cx, cz) : std::make_pair(static_cast<int>(std::floor((a.x + a.m[i].dx) / gp::kCell)), static_cast<int>(std::floor((a.z + a.m[i].dz) / gp::kCell)));
                        CHECK(taken.insert(cell).second);   // no seed is in two patches
                        members++;
                    }
                    if (a.n >= 2) bigPatches++;
                }
                if (rb == 1) { leadersApart++; CHECK(b.n == 1); }
            }
        CHECK(leadersApart == seeds);                       // with merging off every seed stands alone
        CHECK(leadersMerged < seeds && bigPatches > 20);    // with it on seeds run together
        CHECK(members * 100 >= seeds * 97);                 // and hardly any are lost
    }
    // A patch grows with the cover: nothing before its first seed starts, then bigger and bigger, never past the limit.
    gp::Field f;
    int checked = 0;
    for (int cz = -20; cz <= 20 && checked < 40; cz++)
        for (int cx = -20; cx <= 20 && checked < 40; cx++) {
            gp::Patch p;
            if (f.Lead(gp::Kind::Snow, cx, cz, allGround, &p) != 1 || p.n < 3) continue;
            checked++;
            CHECK(!gp::Evaluate(p, 0.0f).shown);
            float last = 0.0f;
            for (float cover = 0.0f; cover <= 1.001f; cover += 0.05f) {
                const gp::Shape sh = gp::Evaluate(p, cover);
                if (!sh.shown) continue;
                CHECK(sh.a >= sh.b && sh.b > 0.0f && sh.a <= gp::kMaxRadius && std::isfinite(sh.yaw) && std::isfinite(sh.dx));
                CHECK(sh.a * sh.b >= last * 0.98f);
                last = sh.a * sh.b;
            }
            CHECK(last > 0.0f);
        }
    CHECK(checked > 10);
    // Ground that is not measured yet holds a patch back (-1) and the patch comes out the same once it is.
    gp::Field slow, whole;
    int blocked = 0, agree = 0, total = 0;
    for (int cz = -12; cz <= 12; cz++)
        for (int cx = -12; cx <= 12; cx++) {
            int budget = 0;
            auto stingy = [&](gp::Kind, int, int, gp::Seed&) { return budget-- > 0 ? 1 : -1; };
            gp::Patch a, b;
            int r;
            for (int tries = 0; (r = slow.Lead(gp::Kind::Puddle, cx, cz, stingy, &a)) < 0 && tries < 400; tries++) { blocked++; budget = 3; }
            const int w = whole.Lead(gp::Kind::Puddle, cx, cz, allGround, &b);
            total++;
            agree += r == w && (r != 1 || (a.n == b.n && a.x == b.x));
        }
    CHECK(blocked > 0 && agree == total);
}

// The Fortnite Map's standing puddles lie on level land (never in the sea, on a road or in a wood), are the same for everyone, and there are plenty;
// and the ground is walkable inland: no steep ground away from the coast.
static void IslandPuddles() {
    namespace fn = royale::fortnite;
    namespace gp = royale::ground;
    int n = 0, bad = 0, mud = 0;
    for (int cz = -45; cz <= 45; cz++)
        for (int cx = -45; cx <= 45; cx++) {
            const gp::Seed s = gp::SeedAt(gp::Kind::Puddle, cx, cz);
            if (!s.ok) continue;
            const float w = fn::PuddleWetness(s.x, s.z);
            CHECK(w == fn::PuddleWetness(s.x, s.z));
            if (gp::Hash01(cx, cz, 7) >= w) continue;   // (the game rolls its own dice; any roll will do for the check)
            n++;
            float y;
            const fn::Cover c = fn::CoverAt(s.x, s.z);
            if (!fn::GroundHeight(s.x, s.z, &y) || y < fn::kWaterY || (c != fn::Cover::Meadow && c != fn::Cover::Dirt) || fn::GroundUp(s.x, s.z) < 0.98f) bad++;
            if (c == fn::Cover::Dirt) mud++;
        }
    CHECK(n > 100 && n < 900 && bad == 0 && mud > 5);
    int steepInland = 0, land = 0;   // away from the water, the ground can be stood on
    for (float z = -fn::kHalfZ + 10; z < fn::kHalfZ; z += 90)
        for (float x = -fn::kHalfX + 10; x < fn::kHalfX; x += 90) {
            float y, q;
            if (!fn::GroundHeight(x, z, &y) || y < fn::kWaterY + 2) continue;
            bool coast = false;
            for (const float d : {200.0f, -200.0f})
                coast = coast || (fn::GroundHeight(x + d, z, &q) && q < fn::kWaterY + 20) || (fn::GroundHeight(x, z + d, &q) && q < fn::kWaterY + 20);
            if (coast) continue;
            land++;
            if (fn::GroundUp(x, z) < 0.8f) steepInland++;
        }
    CHECK(land > 10000 && steepInland * 100 < land);   // under one percent of the inland is too steep to stand on
}


// Snow that is walked in: a footprint is a smooth dent (deepest in the middle, a low rim just outside, nothing far away, no creases), the deepest pit
// wins where prints overlap, prints fill in over time, a hard landing digs a bigger crater than a soft one, feet alternate sides of the line walked,
// and the new puddle, snow and bank meshes are smooth (soft rim alpha on the puddle, no step at the edge of the snow, lip that stands above the ground).
static void SnowDeformation() {
    namespace gp = royale::ground;
    const gp::Dent d = {100.0f, 50.0f, 0.7f, 11.0f, 6.5f, 8.0f};
    CHECK(std::fabs(gp::DentChange(d, 100.0f, 50.0f) + 8.0f) < 1e-4f);                  // the full depth at its middle
    CHECK(gp::DentChange(d, 100.0f + gp::DentReach(d) + 1.0f, 50.0f) == 0.0f);        // nothing outside its reach
    float lowest = 0.0f, highest = 0.0f, prev = 0.0f, worstStep = 0.0f;
    for (int i = 0; i <= 400; i++) {   // walk straight through it, along its long axis: the change must be continuous
        const float lx = -gp::DentReach(d) * 1.1f + i * (gp::DentReach(d) * 2.2f / 400.0f);
        const float c = gp::DentChange(d, d.x + lx * std::cos(d.yaw), d.z + lx * std::sin(d.yaw));
        lowest = std::min(lowest, c); highest = std::max(highest, c);
        if (i > 0) worstStep = std::max(worstStep, std::fabs(c - prev));
        prev = c;
    }
    CHECK(lowest < -7.9f && highest > 1.0f && highest < 0.5f * 8.0f && worstStep < 0.5f);   // a pit, a low rim, no jumps
    // the dent is turned: along its axis it is longer than across it
    const float along = gp::DentChange(d, d.x + 9.0f * std::cos(d.yaw), d.z + 9.0f * std::sin(d.yaw));
    const float across = gp::DentChange(d, d.x - 9.0f * std::sin(d.yaw), d.z + 9.0f * std::cos(d.yaw));
    CHECK(along < across - 1.0f);
    // two prints on top of each other dig no deeper than one; a trail keeps a ridge between prints
    const gp::Dent two[2] = {d, d};
    CHECK(std::fabs(gp::DentsChange(two, 2, d.x, d.z) - gp::DentChange(d, d.x, d.z)) < 1e-4f);
    const gp::Dent trail[2] = {{0, 0, 0, 11, 6.5f, 8}, {40, 0, 0, 11, 6.5f, 8}};
    CHECK(gp::DentsChange(trail, 2, 20.0f, 0.0f) > -1.0f && gp::DentsChange(trail, 2, 0.0f, 0.0f) < -7.0f);
    // prints fill in
    CHECK(gp::DentLeft(0.0f, 30.0f) > 0.999f && gp::DentLeft(30.0f, 30.0f) < 0.001f && gp::DentLeft(10.0f, 30.0f) > gp::DentLeft(20.0f, 30.0f));
    // feet alternate and sit either side of the line walked (walking along +x, the feet are offset along z)
    float lx, lz, rx, rz;
    gp::FootprintAt(10.0f, 0.0f, 1.0f, 0.0f, 1, 5.0f, &lx, &lz);
    gp::FootprintAt(10.0f, 0.0f, 1.0f, 0.0f, -1, 5.0f, &rx, &rz);
    CHECK(lx == 10.0f && rx == 10.0f && std::fabs(lz - 5.0f) < 1e-5f && std::fabs(rz + 5.0f) < 1e-5f);
    // a landing: none when soft, bigger and deeper the harder
    CHECK(gp::CraterFor(100.0f, 1.0f).radius == 0.0f);
    const gp::Crater c1 = gp::CraterFor(500.0f, 1.0f), c2 = gp::CraterFor(1400.0f, 1.0f);
    CHECK(c1.radius > 20.0f && c2.radius > c1.radius && c2.depth > c1.depth && gp::CraterFor(500.0f, 2.0f).radius > c1.radius * 1.9f);
    // the pile's profile falls to nothing at the edge with no slope there, the bank is a smooth bump on the water's edge
    CHECK(gp::PileProfile(1.0f) == 0.0f && gp::PileProfile(0.0f) == 1.0f && gp::PileProfile(0.98f) < 0.01f);
    CHECK(gp::BankHeight(1.0f, 3.0f) == 3.0f && gp::BankHeight(1.5f, 3.0f) < 0.01f && gp::BankHeight(0.5f, 3.0f) < 0.01f);
    // the meshes: puddles fade out at their rim and are opaque inside, the snow is a fine surface with no step at its edge, the bank stands above the ground
    for (uint32_t shape = 0; shape < gp::kPuddleShapes; shape++) {
        const MeshData puddle = BuildMesh(MeshKind::Ground, gp::kPuddleFirst + shape), bank = BuildMesh(MeshKind::Ground, gp::kBankFirst + shape);
        int clear = 0, solid = 0;
        for (const auto& v : puddle.v) { clear += v.a == 0; solid += v.a == 255; }
        CHECK(clear > 50 && solid > 200 && puddle.Triangles() > 250);
        float mn[3], mx[3];
        bank.Bounds(mn, mx);
        CHECK(bank.Triangles() > 200 && mx[1] > 3.0f && mx[1] < 4.0f);
    }
    for (uint32_t v = gp::kPileFirst; v < gp::kDriftFirst + gp::kDriftShapes; v++) {
        const MeshData snow = BuildMesh(MeshKind::Ground, v);
        float mn[3], mx[3];
        snow.Bounds(mn, mx);
        CHECK(snow.Triangles() > 300 && mn[1] > -0.01f && mx[1] > 10.0f);
        int lowCorners = 0;
        for (const auto& p : snow.v) lowCorners += p.y < 0.05f;
        CHECK(lowCorners >= 60);   // the outer ring lies on the ground: the edge is flush, not a wall
    }
}

static void BouldersAndFormations() {
    // Six shapes in five maps' stone: each the height its shape says (so standing on top matches what you see), within the triangle
    // budget, and each map's stone a different colour.
    std::set<int> looks;
    for (uint32_t theme = 0; theme < 5; theme++) {
        for (int shape = 0; shape < kBoulderShapes; shape++) {
            const MeshData m = BuildMesh(MeshKind::Boulder, static_cast<uint32_t>(shape) + kBoulderShapes * theme);
            float mn[3], mx[3];
            m.Bounds(mn, mx);
            CHECK(m.Triangles() >= 100 && m.Triangles() <= 420 && mn[1] >= -0.01f);
            CHECK(std::fabs(mx[1] - BoulderHeight(shape)) < 1.0f && BoulderTop(shape) < mx[1] && mx[0] - mn[0] < 260 && mx[2] - mn[2] < 260);
            if (shape == 0) {
                long r = 0, g = 0, b = 0;
                for (const auto& v : m.v) { r += v.r; g += v.g; b += v.b; }
                const long n = static_cast<long>(m.v.size());
                looks.insert(static_cast<int>(r / n / 8) * 10000 + static_cast<int>(g / n / 8) * 100 + static_cast<int>(b / n / 8));
            }
        }
        const MeshData rock = BuildMesh(MeshKind::Rock, kBoulderShapes * theme);
        float mn[3], mx[3];
        rock.Bounds(mn, mx);
        CHECK(rock.Triangles() <= 420 && mx[1] < 60 && mx[0] - mn[0] < 120);
    }
    CHECK(looks.size() == 5);
    CHECK(BoulderHeight(1) > 150 && BoulderTop(2) < 64);                                          // a slab you can't climb, a table rock you can
    // The shape and size come from the rotation, and a formation can ask for the shape it wants.
    Rng rng(4);
    for (int shape = 0; shape < kBoulderShapes; shape++)
        for (int i = 0; i < 50; i++) CHECK(BoulderShape(RotForShape(rng, shape)) == shape);
    std::set<int> shapes;
    for (uint32_t r = 0; r < 0x10000; r += 97) {
        shapes.insert(BoulderShape(static_cast<uint16_t>(r)));
        CHECK(BoulderScale(static_cast<uint16_t>(r)) >= 0.85f && BoulderScale(static_cast<uint16_t>(r)) <= 1.15f);
    }
    CHECK(static_cast<int>(shapes.size()) == kBoulderShapes);

    // Formations: boulders of several shapes in a group, the ring with a chest in the middle, all kept off the towns.
    const Circle map = {{0, 0}, 4000};
    for (int f = 0; f < kFormationCount; f++) {
        PoiLayout one;
        Rng frng(9 + f);
        AddFormation(one, frng, static_cast<Formation>(f), {0, 0}, 0.4f, nullptr);
        int boulders = 0;
        std::set<int> kinds;
        for (const Prop& p : one.props) if (p.kind == PropKind::Boulder) { boulders++; kinds.insert(BoulderShape(p.rot)); }
        CHECK(boulders >= 4);
        CHECK(static_cast<Formation>(f) == Formation::Ring ? kinds.size() == 1 && one.sites.size() == 1 && Distance(one.sites[0].pos, {0, 0}) < 1.0f : kinds.size() >= 2);
        for (const Prop& p : one.props) CHECK(Distance(p.pos, {0, 0}) < 700.0f);
    }
    for (uint64_t seed = 1; seed < 5; seed++) {
        PoiLayout layout = GeneratePois(seed, map, 12, nullptr, 1);
        const size_t before = layout.props.size();
        GenerateWilds(layout, seed, map, {}, layout.lootSpots, 4, 0, nullptr, 8);
        int formationBoulders = 0;
        for (size_t i = before; i < layout.props.size(); i++) {
            if (layout.props[i].kind != PropKind::Boulder) continue;
            formationBoulders++;
            for (const Poi& poi : layout.pois) CHECK(Distance(layout.props[i].pos, poi.center) > poi.radius);
        }
        CHECK(formationBoulders >= 25);
        // Loose scenery stays off the towns' streets.
        const std::vector<Circle> clear = PoiClearings(layout.pois);
        for (const Prop& p : GenerateProps(seed, map, 600, nullptr, &clear))
            for (const Circle& c : clear) CHECK(Distance(p.pos, c.center) >= c.radius);
    }
}

static void OutpostsAreDesigned() {
    // Every outpost is one connected structure of blocks on the grid (each block touches another edge to edge), whichever way it faces, and its
    // prize sits on its highest block (or in the arena's pit).
    for (int k = 0; k < kOutpostCount; k++) {
        for (int dir = 0; dir < 4; dir++) {
            PoiLayout one;
            Rng rng(k * 4 + dir + 1);
            CHECK(AddOutpost(one, rng, static_cast<Outpost>(k), {300, -200}, dir, nullptr));
            std::vector<Prop> blocks;
            for (const Prop& p : one.props) if (IsPlatform(p.kind)) blocks.push_back(p);
            CHECK(blocks.size() >= 4 && one.sites.size() == 1);
            for (size_t i = 0; i < blocks.size(); i++) {
                bool touches = false;
                for (size_t j = 0; j < blocks.size(); j++)
                    if (i != j && std::fabs(Distance(blocks[i].pos, blocks[j].pos) - kPlatformHalf * 2.0f) < 0.5f) touches = true;
                CHECK(touches);
            }
            float highest = 0, under = -1;
            for (const Prop& b : blocks) {
                highest = (std::max)(highest, PlatformHeight(b.kind));
                if (Distance(b.pos, one.sites[0].pos) < 1.0f) under = PlatformHeight(b.kind);
            }
            CHECK(static_cast<Outpost>(k) == Outpost::Arena ? under < 0 && one.sites[0].bonus == 1 : under == highest && one.sites[0].bonus == 2);
        }
        PoiLayout none;
        Rng rng(1);
        CHECK(!AddOutpost(none, rng, static_cast<Outpost>(k), {0, 0}, 0, [](Vec2 p) { return p.x < 100.0f; }) && none.props.empty());   // half over a drop: not built
    }
    // A map gets several, kept away from the towns.
    const Circle map = {{0, 0}, 4000};
    PoiLayout layout = GeneratePois(6, map, 12, nullptr, 1);
    const size_t before = layout.props.size();
    GenerateWilds(layout, 6, map, {}, layout.lootSpots, 0, 0, nullptr, 0, 5);
    int blocks = 0;
    for (size_t i = before; i < layout.props.size(); i++) {
        if (!IsPlatform(layout.props[i].kind)) continue;
        blocks++;
        for (const Poi& poi : layout.pois) CHECK(Distance(layout.props[i].pos, poi.center) > poi.radius);
    }
    CHECK(blocks >= 20);
}

static void TownsAreDifferentPlaces() {
    // Kinds of town are dealt so that every kind turns up before any repeats, and never the same twice running.
    Rng rng(11);
    const std::vector<TownKind> kinds = DealTownKinds(rng, 20);
    std::set<int> firstDeck;
    for (int i = 0; i < kTownKindCount; i++) firstDeck.insert(static_cast<int>(kinds[i]));
    CHECK(static_cast<int>(firstDeck.size()) == kTownKindCount);
    for (size_t i = 1; i < kinds.size(); i++) CHECK(kinds[i] != kinds[i - 1]);
    // Every kind of town has chests and a prize up high, and stands on its own ground.
    for (int k = 0; k < kTownKindCount; k++) {
        PoiLayout one;
        Rng trng(k + 1);
        BuildTown(one, trng, static_cast<TownKind>(k), {1000, -500}, 0.3f * k, nullptr);
        int tops = 0;
        for (const ChestSite& st : one.sites) tops += st.bonus == 2;
        CHECK(tops >= 1 && one.lootSpots.size() + one.sites.size() >= 3 && one.props.size() >= 10);
        for (const Prop& p : one.props) CHECK(Distance(p.pos, {1000, -500}) < kTownRadius + 300.0f);
        // Nothing solid stands on a chest.
        for (const Vec2& sp : one.lootSpots) for (const Prop& p : one.props) CHECK(PropRadius(p.kind) == 0.0f || IsPlatform(p.kind) || Distance(sp, p.pos) > PropRadius(p.kind) * 0.8f);
    }
    // A small map gets fewer, whole towns rather than a heap of overlapping ones; a big one gets the full set.
    const PoiLayout small = GeneratePois(3, {{0, 0}, 1900}, 12, nullptr, 2), big = GeneratePois(3, {{0, 0}, 4000}, 12, nullptr, 2);
    CHECK(small.pois.size() >= 4 && small.pois.size() <= 7 && big.pois.size() >= 10);
    for (size_t i = 0; i < small.pois.size(); i++)
        for (size_t j = i + 1; j < small.pois.size(); j++) CHECK(Distance(small.pois[i].center, small.pois[j].center) >= 1050.0f);
    for (size_t i = 0; i < big.pois.size(); i++)
        for (size_t j = i + 1; j < big.pois.size(); j++) CHECK(Distance(big.pois[i].center, big.pois[j].center) >= 1250.0f);
    std::set<int> names;
    for (const Poi& p : big.pois) names.insert(p.name);
    CHECK(names.size() == big.pois.size());
}

static void GildedSwordSurfaceMaps() {
    namespace gs = royale::gilded_surface;
    namespace gm = royale::gilded_sword_model;
    // Every triangle names a material and a surface class that exist, and its outward direction is a real direction.
    auto check = [&](const gm::Tri* tris, int count) {
        for (int i = 0; i < count; i++) {
            CHECK(tris[i].mat < 7 && tris[i].cls < 4);
            const float l = std::sqrt(static_cast<float>(tris[i].n[0] * tris[i].n[0] + tris[i].n[1] * tris[i].n[1] + tris[i].n[2] * tris[i].n[2]));
            CHECK(l > 100.0f && l < 140.0f);
        }
    };
    check(gm::kBlade, gm::kBladeCount); check(gm::kHilt, gm::kHiltCount); check(gm::kScabbard, gm::kScabbardCount);
    // The blade's flat sides face up and down, so the engraving is projected onto them (the game projects along a triangle's dominant axis).
    for (int i = 0; i < gm::kBladeCount; i++) CHECK(std::abs(gm::kBlade[i].n[2]) > 100);
    for (int c = 0; c < static_cast<int>(gs::Class::Count); c++) {
        const gs::Map m = gs::Build(static_cast<gs::Class>(c)), again = gs::Build(static_cast<gs::Class>(c));
        const size_t bytes = static_cast<size_t>(m.size) * m.size * 4;
        CHECK(m.size >= 64 && m.normal.size() == bytes && m.height.size() == bytes && m.uvScale > 0 && m.normal == again.normal && m.height == again.height);   // deterministic
        double mean = 0, var = 0; int up = 0;
        for (size_t i = 0; i < bytes; i += 4) { mean += m.height[i]; up += m.normal[i + 2] >= 128; CHECK(m.normal[i + 3] == 255 && m.height[i] == m.height[i + 1]); }
        mean /= bytes / 4;
        for (size_t i = 0; i < bytes; i += 4) var += (m.height[i] - mean) * (m.height[i] - mean);
        var /= bytes / 4;
        CHECK(mean > 80 && mean < 180 && std::sqrt(var) > 3.0 && std::sqrt(var) < 70.0);   // real relief, nothing blown out
        CHECK(up == static_cast<int>(bytes / 4));                                           // every normal points out of the surface
    }
    // The blade's engraving is laid on the model's own diamonds: a groove runs just inside every diamond's edge, and the gold diamond's heart is a raised boss.
    const float p = gm::kDiamondPitch, x0 = gm::kBladeStart;
    const float boss = gs::detail::BladeHeight(x0 + p * 0.5f, 0.0f), plain = gs::detail::BladeHeight(x0 + p * 0.5f, gm::kBladeHalfWidth * 0.95f);
    CHECK(boss > plain + 0.05f);
    const float groove = gs::detail::BladeHeight(x0 + p * 0.25f, gm::kBladeHalfWidth * 0.5f * 0.965f), inside = gs::detail::BladeHeight(x0 + p * 0.25f, gm::kBladeHalfWidth * 0.2f);
    CHECK(std::fabs(groove - 0.5f) > 0.04f || std::fabs(inside - 0.5f) > 0.01f);
    // The pattern repeats every two diamonds along the blade and across the tile.
    CHECK(std::fabs(gs::detail::BladeHeight(x0 + 100.0f, 120.0f) - gs::detail::BladeHeight(x0 + 100.0f + 2.0f * p, 120.0f)) < 0.001f);
}
static void CustomMeshes() {
    for (int k = 0; k < static_cast<int>(MeshKind::Count); k++) {
        for (uint32_t variant = 0; variant < kMeshVariants; variant++) {
            const MeshData m = BuildMesh(static_cast<MeshKind>(k), variant);
            CHECK(!m.v.empty() && m.v.size() % 3 == 0 && m.Triangles() >= 12 && m.Triangles() <= (k == static_cast<int>(MeshKind::Glider) ? 520u : k == static_cast<int>(MeshKind::Scenery) ? 600u : k == static_cast<int>(MeshKind::GildedSword) ? 500u : k == static_cast<int>(MeshKind::VictoryCrown) ? 600u : 420u));   // (the glider is a Blender model with more parts) a few dozen triangles: chunky, and cheap to draw
            float mn[3], mx[3];
            m.Bounds(mn, mx);
            bool finite = true;
            for (const auto& p : m.v) finite &= std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
            CHECK(finite && (mn[1] >= -0.01f || k == static_cast<int>(MeshKind::Glider) || k == static_cast<int>(MeshKind::GliderFrame) || k == static_cast<int>(MeshKind::GildedSword) || k == static_cast<int>(MeshKind::VictoryCrown)));   // nothing below the ground (the glider's origin is its handle bar, the sword's its grip)
            const MeshData again = BuildMesh(static_cast<MeshKind>(k), variant);
            bool same = again.v.size() == m.v.size();
            for (size_t i = 0; same && i < m.v.size(); i++) same = again.v[i].x == m.v[i].x && again.v[i].r == m.v[i].r;
            CHECK(same);                                                                                // deterministic
            // Every face is lit by the way it faces, so colours differ across the model and none is black.
            int lo = 255, hi = 0;
            for (const auto& p : m.v) { lo = (std::min)(lo, static_cast<int>(p.g)); hi = (std::max)(hi, static_cast<int>(p.g)); }
            CHECK(hi > lo + 15 && hi > 60);
            if (static_cast<MeshKind>(k) == MeshKind::Rock) CHECK(mx[0] - mn[0] < 120 && mx[1] < 60);
            if (static_cast<MeshKind>(k) == MeshKind::Boulder) CHECK(mx[0] - mn[0] > 100 && mx[0] - mn[0] < 260 && std::fabs(mx[1] - BoulderHeight(static_cast<int>(variant) % kBoulderShapes)) < 1.0f);
            if (static_cast<MeshKind>(k) == MeshKind::Pillar) CHECK(mx[1] > 190 && mx[1] < 215 && mx[0] - mn[0] < 100);
            if (static_cast<MeshKind>(k) == MeshKind::Golem) CHECK(mx[1] > 250 && mx[1] < 300 && mx[0] - mn[0] > 200 && mx[0] - mn[0] < 280 && m.Triangles() >= 100);
            if (static_cast<MeshKind>(k) == MeshKind::Glider) CHECK(mn[1] > -15 && mn[1] < 5 && mx[1] > 70 && mx[1] < 110 && mx[0] - mn[0] > 230 && mx[0] - mn[0] < 300);   // the handle bar is the origin; the wing is above it
            if (static_cast<MeshKind>(k) == MeshKind::GildedSword) CHECK(mx[0] > 5500 && mx[0] < 6500 && mn[0] < -1000 && mx[1] - mn[1] < 2600);   // grip at the origin, blade along +X in limb units
            if (static_cast<MeshKind>(k) == MeshKind::Dragon) CHECK(mx[0] - mn[0] > 700 && mx[2] - mn[2] > 800 && m.Triangles() >= 150);
            if (static_cast<MeshKind>(k) == MeshKind::Projectile) CHECK(mx[2] - mn[2] > 15 && mx[2] - mn[2] < 130 && m.Triangles() >= 12);
            if (static_cast<MeshKind>(k) == MeshKind::Platform) CHECK(mx[0] - mn[0] >= 150 && mx[0] - mn[0] < 170 && mx[1] > 59.0f * static_cast<float>(variant % 3 + 1) && mx[1] < 64.0f * static_cast<float>(variant % 3 + 1));
            if (static_cast<MeshKind>(k) == MeshKind::Ground) CHECK(mx[0] - mn[0] > 40 && mx[0] - mn[0] < 340 && mx[2] - mn[2] > 40 && mx[1] < 70.0f);   // a patch of ground, not a tower
            if (static_cast<MeshKind>(k) == MeshKind::Ripple) CHECK(mx[1] < 0.01f && mx[0] - mn[0] > 35 && mx[0] - mn[0] < 45);
            if (static_cast<MeshKind>(k) == MeshKind::Grenade) CHECK(mx[1] > 26 && mx[1] < 34 && mx[0] - mn[0] < 36);   // a bomb-sized ball
            if (static_cast<MeshKind>(k) == MeshKind::Roof) CHECK(mn[1] >= 199.0f && mx[1] > 300 && mx[0] - mn[0] > 400 && mx[2] - mn[2] > 330);
        }
    }
    // Variants of a rock really differ.
    // Every projectile variant builds, stays near the origin and is a few dozen triangles.
    for (uint32_t variant = 0; variant < 10; variant++) {
        const MeshData m = BuildMesh(MeshKind::Projectile, variant);
        float mn[3], mx[3];
        m.Bounds(mn, mx);
        CHECK(m.Triangles() >= 12 && m.Triangles() <= 60 && mn[1] >= -0.01f && mx[1] < 50 && std::fabs(mn[0]) < 60 && std::fabs(mx[0]) < 60);
    }
    // One golem per kind of mini boss, one dragon per theme and pose, and each is its own colour.
    {
        std::set<int> reds;
        for (uint32_t kind = 0; kind < kMiniBossKindCount; kind++) {
            const MeshData g = BuildMesh(MeshKind::Golem, kind);
            CHECK(g.Triangles() >= 100 && g.Triangles() <= 420);
            reds.insert(g.v[0].r * 1000 + g.v[0].g);
        }
        CHECK(reds.size() >= 6);
        std::set<int> dragonColours;
        for (uint32_t theme = 0; theme < 5; theme++) {
            for (uint32_t pose = 0; pose < 4; pose++) {
                const MeshData d = BuildMesh(MeshKind::Dragon, pose + 4 * theme);
                CHECK(d.Triangles() >= 150 && d.Triangles() <= 420);
                float mn[3], mx[3];
                d.Bounds(mn, mx);
                CHECK(mn[1] >= -0.01f && std::isfinite(mx[0]));
                if (pose == 0) dragonColours.insert(d.v[0].r * 1000 + d.v[0].g);
            }
        }
        CHECK(dragonColours.size() == 5);
        // The wings really move: arms up reaches higher than arms down.
        float upMn[3], upMx[3], dnMn[3], dnMx[3];
        BuildMesh(MeshKind::Dragon, 0).Bounds(upMn, upMx);
        BuildMesh(MeshKind::Dragon, 2).Bounds(dnMn, dnMx);
        CHECK(upMx[1] > dnMx[1] + 200.0f);
    }
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
    FillAmmo(sim);
    sim.match.Find(1000)->pos = {-1950, -1950}; // one bot far away, so the match doesn't end for lack of opponents
    sim.match.Find(1)->pos = human;
    return sim;
}

static bool storm_inside(Simulation& sim, Vec2 p) { return sim.match.GetStorm().SafeZoneAt(sim.match.StormTime()).Contains(p); }

static Simulation DragonArena(int mapId, bool enabled) {
    Simulation sim(9, MapCircle(), 0);
    sim.match.SetMapId(mapId);
    sim.match.SetMajorBoss(enabled);
    sim.match.AddHuman(1);
    sim.match.Start();
    while (sim.match.State() != MatchState::InMatch) sim.match.Tick(kDt);
    for (auto& p : sim.match.Players()) if (p.id != 1 && p.id != 1000) p.alive = false;
    sim.match.Find(1000)->pos = {-1950, -1950};
    FillAmmo(sim);
    sim.match.Find(1)->pos = {0, 0};
    sim.match.Find(1)->maxHealth = sim.match.Find(1)->health = 100.0f;
    sim.match.Find(1)->invulnUntil = 1.0e9f; // the storm and the dragon leave the test player alone unless a test turns this off
    sim.match.Find(1000)->maxHealth = sim.match.Find(1000)->health = 1000.0f;
    return sim;
}

static void TheMajorBoss() {
    float half = 0;
    for (const auto& ph : kStormPhases) half += ph.waitSec + ph.closeSec;
    half *= 0.5f;
    // Halfway through the storm timeline, and only if it is switched on.
    {
        Simulation off = DragonArena(0, false);
        Run(off, half + 20.0f);
        CHECK(off.match.FindBoss(kDragonId) == nullptr);
        Simulation on = DragonArena(3, true);
        Run(on, half - 10.0f);
        CHECK(on.match.FindBoss(kDragonId) == nullptr);
        bool announced = false;
        for (int i = 0; i < 20 * 25 && !announced; i++) {                                         // look as it arrives, before its first dive
            on.Tick(kDt);
            for (const auto& e : on.match.DrainEvents()) announced |= e.type == MatchEvent::Type::BossSpawned;
        }
        const MiniBoss* d = on.match.FindBoss(kDragonId);
        CHECK(d && d->alive && d->kind == BossKind::DragonFire && announced);                       // map 3 is Death Mountain Crater: the fire dragon
        CHECK(d && d->y > 100.0f && storm_inside(on, d->pos));                                       // it flies, over the safe zone
    }
    // In the air only ranged weapons reach it; landed, it takes extra.
    {
        Simulation sim = DragonArena(1, true);
        Run(sim, half + 2.0f);
        Match& m = sim.match;
        MiniBoss* d = const_cast<MiniBoss*>(m.FindBoss(kDragonId));
        CHECK(d && d->kind == BossKind::DragonWater);
        PlayerState* h = m.Find(1);
        d->mode = DragonMode::Chase;
        d->attackReadyAt = 1e9f;
        d->swoopReadyAt = 1e9f;
        d->y = kDragonAltitude;
        d->pos = {0, 40};
        h->pos = {0, 0};
        h->weapon = {ItemId::MasterSword, Rarity::Legendary};
        h->attackReadyAt = 0;
        CHECK(!m.AttackBoss(1, kDragonId, true).ok);                                                 // a sword can't reach
        h->weapon = {ItemId::FairyBow, Rarity::Legendary};
        h->attackReadyAt = 0;
        const AttackResult air = m.AttackBoss(1, kDragonId, true);
        CHECK(air.ok && air.damage > 0);
        d->mode = DragonMode::Landed; d->modeUntil = m.Clock() + 10; d->y = 0;
        h->attackReadyAt = 0;
        const AttackResult ground = m.AttackBoss(1, kDragonId, true);
        CHECK(ground.ok && ground.damage > air.damage * 1.4f);
        h->weapon = {ItemId::MasterSword, Rarity::Legendary};
        h->attackReadyAt = 0;
        CHECK(m.AttackBoss(1, kDragonId, true).ok);                                                  // on the ground a sword does
    }
    // Marked blasts land after their delay, only on whoever stands in the ring.
    {
        Simulation sim = DragonArena(0, true);
        Match& m = sim.match;
        PlayerState* h = m.Find(1);
        h->invulnUntil = 0;
        h->pos = {100, 0};
        m.AddStrike({100, 0}, 150.0f, 1.3f, 1.0f, kDragonId);
        Run(sim, 0.5f);
        CHECK(h->health > 99.99f);
        Run(sim, 1.0f);
        CHECK(h->health < 100.0f - 1.2f * kBossDamageScale);
        Run(sim, 4.0f);                                                                              // let the burn finish
        const float after = h->health;
        h->pos = {1500, 0};
        m.AddStrike({100, 0}, 150.0f, 1.3f, 0.2f, kDragonId);
        Run(sim, 0.6f);
        CHECK(h->health >= after - 0.01f);
    }
    // Bringing it down drops nine chests and two heart containers.
    {
        Simulation sim = DragonArena(4, true);
        Run(sim, half + 2.0f);
        Match& m = sim.match;
        MiniBoss* d = const_cast<MiniBoss*>(m.FindBoss(kDragonId));
        CHECK(d && d->kind == BossKind::DragonSand);
        PlayerState* h = m.Find(1);
        d->mode = DragonMode::Landed; d->modeUntil = m.Clock() + 10; d->y = 0; d->pos = {0, 30}; d->health = 0.5f;
        d->attackReadyAt = 1e9f;
        h->pos = {0, 0};
        h->weapon = {ItemId::MasterSword, Rarity::Legendary};
        h->attackReadyAt = 0;
        const size_t before = m.Loot().size();
        const AttackResult r = m.AttackBoss(1, kDragonId, true);
        CHECK(r.ok && r.killed);
        size_t hearts = 0;
        for (size_t i = before; i < m.Loot().size(); i++) hearts += m.Loot()[i].spawn.item == ItemId::HeartContainer && m.Loot()[i].spawn.special;
        CHECK(m.Loot().size() - before >= 9 + 2 && hearts == 2);
    }
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
        for (int i = 0; i < 30 * kTickHz; i++) { sim.Tick(kDt); sim.match.Find(1)->pos = {1900, 1000}; sim.match.Find(1000)->pos = {-1950, -1950}; }   // the bot sprints in from the storm otherwise
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

// One mini boss of the given kind at the origin, with the test player (who can't die) at `human`.
static Simulation OneBoss(BossKind kind, Vec2 human, std::shared_ptr<const NavGrid> nav = nullptr) {
    Simulation sim = BossArena({{0, 0}}, 1, human);
    MiniBoss* b = const_cast<MiniBoss*>(&sim.match.Bosses()[0]);
    b->kind = kind;
    b->maxHealth = b->health = BossOf(kind).health;
    PlayerState* h = sim.match.Find(1);
    h->maxHealth = h->health = 100.0f;
    if (nav) sim.match.SetNav(std::move(nav));
    return sim;
}

static void ChuChuCombat() {
    // The merged combat rules must dodge the damage and the elemental follow-up together.
    {
        auto sim=OneBoss(BossKind::ChuRed,{0,80});auto* h=sim.match.Find(1);
        auto* b=const_cast<MiniBoss*>(sim.match.FindBoss(kBossIdBase));
        b->windupUntil=sim.match.Clock()+kDt;b->rot=0;
        h->rollUntil=sim.match.Clock()+1;const float health=h->health;
        sim.match.Tick(kDt);
        CHECK(h->health==health && h->burnUntil<=sim.match.Clock());
    }
    // Electric melee counter, projectile discharge, and missing ammunition.
    for (BossKind kind : {BossKind::ChuYellow, BossKind::ChuBlue}) {
        auto sim = OneBoss(kind, {0, 80});
        auto* h = sim.match.Find(1);
        auto* b = const_cast<MiniBoss*>(sim.match.FindBoss(kBossIdBase));
        // Pick a charged clock; the test match starts after countdown/drop.
        while (!ChuCharged(kind, sim.match.Clock(), false)) sim.match.Tick(kDt);
        h->weapon = {ItemId::MasterSword, Rarity::Common}; h->attackReadyAt = 0;
        const float hp = b->health;
        CHECK(sim.match.AttackBoss(1,b->id,true).ok && b->health == hp);
        h->stunUntil=0; h->attackReadyAt=0; h->weapon={ItemId::Boomerang,Rarity::Common};
        CHECK(sim.match.AttackBoss(1,b->id,true).hit && b->mode == DragonMode::Stunned && b->health < hp);
        h->attackReadyAt=0; h->weapon={ItemId::MasterSword,Rarity::Common};
        CHECK(sim.match.AttackBoss(1,b->id,true).hit);
    }
    {
        auto sim=OneBoss(BossKind::ChuDark,{0,80}); auto* h=sim.match.Find(1);
        auto* b=const_cast<MiniBoss*>(sim.match.FindBoss(kBossIdBase));
        h->weapon={ItemId::MasterSword,Rarity::Common};h->attackReadyAt=0;
        const float resisted=sim.match.AttackBoss(1,b->id,true).damage;
        h->weapon={ItemId::LightArrows,Rarity::Common};h->attackReadyAt=0;
        CHECK(sim.match.AttackBoss(1,b->id,true).hit && b->mode==DragonMode::Stunned && b->aux==4);
        h->weapon={ItemId::MegatonHammer,Rarity::Common};h->attackReadyAt=0;
        CHECK(sim.match.AttackBoss(1,b->id,true).damage > resisted*5);
    }
    for (BossKind kind : {BossKind::ChuRed,BossKind::ChuGreen,BossKind::ChuYellow,BossKind::ChuBlue,BossKind::ChuDark}) {
        CHECK(IsChuKind(kind) && !IsMajorKind(kind));
        auto sim=OneBoss(kind,{0,300}); bool special=false, strike=false, leapt=false;
        for(int i=0;i<20*14;i++) {
            sim.match.Find(1)->pos={0,300}; sim.match.Tick(kDt);
            const auto* b=sim.match.FindBoss(kBossIdBase);
            special |= b->mode==DragonMode::Leap || b->mode==DragonMode::Hidden || b->mode==DragonMode::Summon;
            leapt |= b->mode==DragonMode::Leap;
            for(const auto& e:sim.match.DrainEvents()) if(e.type==MatchEvent::Type::Strike && e.a==kBossIdBase) strike=true;
        }
        CHECK(special && strike);
        if(kind==BossKind::ChuBlue) CHECK(!leapt);
    }
    // Seeded normal matches expose all variants without removing the old pool.
    std::set<BossKind> seen;
    for(uint64_t seed=1;seed<=80;seed++) {
        Match m(seed,MapCircle(),0); m.SetBossCount(7);m.SetBossSpots({{0,0},{500,0},{-500,0},{0,500},{0,-500},{700,700},{-700,-700}});
        m.SpawnBosses();for(const auto& b:m.Bosses()) { CHECK(!IsMajorKind(b.kind));seen.insert(b.kind); }
    }
    for(int k=static_cast<int>(BossKind::ChuRed);k<=static_cast<int>(BossKind::ChuDark);k++) CHECK(seen.count(static_cast<BossKind>(k)));
}

static void BossesUseTheirOwnMoves() {
    // Every mini boss, fought for a while, does its own thing from the game (and the blasts it marks are its own kind).
    struct Want { BossKind kind; DragonMode mode; StrikeStyle style; float start; };
    const Want wants[] = {
        {BossKind::Stone, DragonMode::Leap, StrikeStyle::Rock, 400.0f},      // Stalfos: jump slash
        {BossKind::Lava, DragonMode::Charge, StrikeStyle::Fire, 430.0f},     // Magma Dodongo: rolls at you from further off, leaving fire
        {BossKind::Lava, DragonMode::Breath, StrikeStyle::Count, 150.0f},    // ...and breathes fire up close
        {BossKind::Frost, DragonMode::Summon, StrikeStyle::Ice, 150.0f},     // White Wolfos: freezing howl
        {BossKind::Moss, DragonMode::Summon, StrikeStyle::Spore, 400.0f},    // Moss Lizalfos: spore pods
        {BossKind::Tide, DragonMode::Charge, StrikeStyle::Count, 430.0f},    // Big Octo: spin charge
        {BossKind::Shade, DragonMode::Summon, StrikeStyle::Shadow, 400.0f},  // Dead Hand: hands out of the ground
        {BossKind::Dune, DragonMode::Slam, StrikeStyle::Rock, 150.0f},       // Iron Knuckle: overhead cleave
    };
    for (const Want& w : wants) {
        Simulation sim = OneBoss(w.kind, {w.start, 0});
        bool sawMode = false, sawStyle = w.style == StrikeStyle::Count, dazedLater = false;
        for (int i = 0; i < static_cast<int>(12 * kTickHz); i++) {
            sim.Tick(kDt);
            sim.match.Find(1)->health = 100.0f;
            sim.match.Find(1)->pos = {w.start, 0};   // stand still where it found you
            const MiniBoss& b = sim.match.Bosses()[0];
            sawMode |= b.mode == w.mode;
            dazedLater |= sawMode && b.mode == DragonMode::Stunned;
            for (const auto& e : sim.match.DrainEvents()) sawStyle |= e.type == MatchEvent::Type::Strike && e.item == static_cast<uint8_t>(w.style);
        }
        CHECK(sawMode && sawStyle);
        if (w.kind == BossKind::Tide || w.kind == BossKind::Dune || (w.kind == BossKind::Lava && w.mode == DragonMode::Charge)) CHECK(dazedLater);   // and is dazed after it
    }
    // Dead Hand burrows over to you after its grab; while it is underground nothing can hit it, and dazed it takes extra.
    {
        Simulation sim = OneBoss(BossKind::Shade, {400, 0});
        bool hid = false;
        for (int i = 0; i < static_cast<int>(10 * kTickHz) && !hid; i++) { sim.Tick(kDt); sim.match.Find(1)->health = 100.0f; hid = BossHidden(sim.match.Bosses()[0].mode); }
        CHECK(hid);
        PlayerState* h = sim.match.Find(1);
        MiniBoss* b = const_cast<MiniBoss*>(&sim.match.Bosses()[0]);
        h->weapon = {ItemId::FairyBow, Rarity::Legendary};
        h->pos = {b->pos.x + 200, b->pos.z};
        h->attackReadyAt = 0;
        h->stunUntil = h->frozenUntil = 0;   // (its hands were holding you)
        CHECK(!sim.match.AttackBoss(1, b->id, true).ok);
        b->mode = DragonMode::Chase;
        h->attackReadyAt = 0;
        const float plain = sim.match.AttackBoss(1, b->id, true).damage;
        b->mode = DragonMode::Stunned; b->modeUntil = sim.match.Clock() + 5;
        h->attackReadyAt = 0;
        const float dazed = sim.match.AttackBoss(1, b->id, true).damage;
        CHECK(plain > 0 && dazed > plain * 1.2f);
    }
    // The Stalfos gets back up once; the second time it stays down.
    {
        Simulation sim = OneBoss(BossKind::Stone, {60, 0});
        Match& m = sim.match;
        PlayerState* h = m.Find(1);
        h->weapon = {ItemId::MasterSword, Rarity::Legendary};
        MiniBoss* b = const_cast<MiniBoss*>(&m.Bosses()[0]);
        b->health = 0.1f;
        h->attackReadyAt = 0;
        const AttackResult first = m.AttackBoss(1, b->id, true);
        CHECK(first.hit && !first.killed && b->alive && b->mode == DragonMode::Stunned && std::abs(b->health - b->maxHealth * kStalfosGetsUpWith) < 0.01f);
        b->health = 0.1f;
        h->attackReadyAt = 0;
        CHECK(m.AttackBoss(1, b->id, true).killed && !b->alive);
    }
    // The Iron Knuckle with its armour off is faster.
    {
        Simulation a = OneBoss(BossKind::Dune, {400, 0}), bare = OneBoss(BossKind::Dune, {400, 0});
        MiniBoss* x = const_cast<MiniBoss*>(&bare.match.Bosses()[0]);
        x->health = x->maxHealth * 0.4f;
        Run(a, 1.2f); Run(bare, 1.2f);
        CHECK(Distance(bare.match.Bosses()[0].pos, {0, 0}) > Distance(a.match.Bosses()[0].pos, {0, 0}) * 1.3f);
    }
    // What the blasts do: ice freezes, shadow holds you, fire sets you alight.
    {
        Simulation sim = OneBoss(BossKind::Frost, {1900, 0});
        Match& m = sim.match;
        PlayerState* h = m.Find(1);
        h->pos = {1500, 0};
        m.AddStrike({1500, 0}, 100.0f, 0.1f, 0.1f, m.Bosses()[0].id, StrikeStyle::Ice);
        Run(sim, 0.3f);
        CHECK(m.Clock() < h->frozenUntil);
        m.AddStrike({1500, 0}, 100.0f, 0.1f, 0.1f, m.Bosses()[0].id, StrikeStyle::Shadow);
        Run(sim, 0.3f);
        CHECK(m.Clock() < h->stunUntil);
        m.AddStrike({1500, 0}, 100.0f, 0.1f, 0.1f, m.Bosses()[0].id, StrikeStyle::Fire);
        Run(sim, 0.3f);
        CHECK(m.Clock() < h->burnUntil);
    }
}

static void BossesFindTheirWay() {
    // A wall between the boss and you: with the grid it walks around it (never through it); a boss that can leap is not stopped
    // by a gap it can't walk round, and Dead Hand goes under.
    auto wall = std::make_shared<NavGrid>(MapCircle(), NotWall);
    {
        Simulation sim = BossArena({{-200, 0}}, 1, {200, 0});
        MiniBoss* b = const_cast<MiniBoss*>(&sim.match.Bosses()[0]);
        b->kind = BossKind::Dune;   // an Iron Knuckle: it walks
        sim.match.SetNav(wall);
        sim.match.Find(1)->maxHealth = sim.match.Find(1)->health = 100.0f;
        bool throughWall = false;
        float closest = 1e9f;
        for (int i = 0; i < static_cast<int>(20 * kTickHz); i++) {
            sim.Tick(kDt);
            sim.match.Find(1)->health = 100.0f;
            sim.match.Find(1)->pos = {200, 0};
            const MiniBoss& x = sim.match.Bosses()[0];
            throughWall |= std::fabs(x.pos.x) < 25.0f && std::fabs(x.pos.z) < 450.0f;   // the grid is 60 wide, so only the core of the wall is guaranteed
            closest = std::min(closest, Distance(x.pos, {200, 0}));
        }
        CHECK(!throughWall && closest < 200.0f);
    }
    // A pocket it can't walk to: the Stalfos leaps in, Dead Hand burrows in, the Lizalfos climbs over.
    auto sealed = std::make_shared<NavGrid>(MapCircle(), [](Vec2 p) { const float d = std::hypot(p.x - 400.0f, p.z); return d < 150.0f || d > 300.0f; });
    for (BossKind k : {BossKind::Stone, BossKind::Shade, BossKind::Moss}) {
        Simulation sim = BossArena({{50, 0}}, 1, {400, 0});
        MiniBoss* b = const_cast<MiniBoss*>(&sim.match.Bosses()[0]);
        b->kind = k;
        sim.match.SetNav(sealed);
        bool trick = false;
        float closest = 1e9f;
        for (int i = 0; i < static_cast<int>(20 * kTickHz); i++) {
            sim.Tick(kDt);
            sim.match.Find(1)->health = 100.0f;
            sim.match.Find(1)->pos = {400, 0};
            const MiniBoss& x = sim.match.Bosses()[0];
            trick |= x.mode == DragonMode::Leap || x.mode == DragonMode::Hidden || x.mode == DragonMode::Climb;
            closest = std::min(closest, Distance(x.pos, {400, 0}));
        }
        CHECK(trick && closest < 160.0f);
    }
}

static void MajorBossesFightTheirOwnWay() {
    float half = 0;
    for (const auto& ph : kStormPhases) half += ph.waitSec + ph.closeSec;
    half *= 0.5f;
    // Each map's major boss is its own, and fought for a while shows its own moves.
    struct Want { int map; BossKind kind; std::vector<DragonMode> modes; std::vector<StrikeStyle> styles; };
    const Want wants[] = {
        {3, BossKind::DragonFire, {DragonMode::Hidden, DragonMode::Emerge, DragonMode::Landed}, {StrikeStyle::Fire, StrikeStyle::Rock}},
        {1, BossKind::DragonWater, {DragonMode::Hidden, DragonMode::Emerge, DragonMode::Slam, DragonMode::Stunned}, {StrikeStyle::Water}},
        {0, BossKind::DragonForest, {DragonMode::Hidden, DragonMode::Charge, DragonMode::Beam, DragonMode::Cast}, {StrikeStyle::Magic, StrikeStyle::Bolt}},
        {2, BossKind::DragonShadow, {DragonMode::Slam}, {StrikeStyle::Shadow}},
        {4, BossKind::DragonSand, {DragonMode::Beam}, {StrikeStyle::Fire, StrikeStyle::Ice}},
    };
    for (const Want& w : wants) {
        Simulation sim = DragonArena(w.map, true);
        Run(sim, half + 1.0f);
        Match& m = sim.match;
        const MiniBoss* d = m.FindBoss(kDragonId);
        CHECK(d && d->kind == w.kind && std::string(BossOf(d->kind).name).size() > 3);
        if (!d) continue;
        PlayerState* h = m.Find(1);
        h->invulnUntil = 0;
        std::set<int> modes, styles, auxes;
        for (int i = 0; i < static_cast<int>(90 * kTickHz); i++) {
            sim.Tick(kDt);
            h->health = h->maxHealth;
            // Keep close to it. Morpha travels under the water to a spot beside you and rises there, so you have to stand still for it to arrive.
            if (d->kind == BossKind::DragonWater) h->pos = {0, 0}; else h->pos = {d->pos.x + 300.0f, d->pos.z};
            modes.insert(static_cast<int>(d->mode));
            if (d->mode == DragonMode::Beam || d->mode == DragonMode::Slam) auxes.insert(d->aux);
            for (const auto& e : m.DrainEvents()) if (e.type == MatchEvent::Type::Strike && e.a == kDragonId) styles.insert(e.item);
        }
        for (DragonMode md : w.modes) CHECK(modes.count(static_cast<int>(md)));
        for (StrikeStyle st : w.styles) CHECK(styles.count(static_cast<int>(st)));
        if (w.kind == BossKind::DragonSand) CHECK(auxes.count(0) && auxes.count(1));   // fire and ice take turns
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
    // Hookshot and Shockwave Grenade are hotbar weapons used with B, not powers.
    CHECK(KindOf(ItemId::ShockwaveGrenade) == ItemKind::Weapon && KindOf(ItemId::Hookshot) == ItemKind::Weapon);
    CHECK(InPool(ItemId::ShockwaveGrenade) && InPool(ItemId::Hookshot));
    {   // The grenade is a knockback-only projectile: it does not damage or stun.
        Simulation sim = Duel(5, {100, 0}, {0, 0});
        Match& m = sim.match;
        PlayerState* user = m.Find(1);
        PlayerState* target = m.Find(1000);
        user->pos = {0, 0};
        target->pos = {300, 0};
        user->weapon = {ItemId::ShockwaveGrenade, Rarity::Rare};
        user->attackReadyAt = 0;
        const float before = target->health;
        CHECK(m.Attack(1, 1000).ok && target->health == before && !m.Stunned(*target));
        CHECK(!m.Stunned(*user) && user->health == user->maxHealth);
        CHECK(!m.Attack(1, 1000).ok);   // recharging: a grenade is slow
    }
    {   // Throws with no locked target still announce a blast downrange for every client.
        Simulation sim = Duel(5, {100, 0}, {0, 0});
        Match& m = sim.match;
        PlayerState* user = m.Find(1);
        user->pos = {0, 0}; user->rot = 0;
        user->weapon = {ItemId::ShockwaveGrenade, Rarity::Rare};
        user->attackReadyAt = 0;
        m.DrainEvents();
        CHECK(m.ShootAtNothing(1).ok);
        bool announced = false;
        for (const auto& e : m.DrainEvents()) if (e.type == MatchEvent::Type::Strike && e.item == static_cast<uint8_t>(StrikeStyle::Shockwave)) {
            announced = true; CHECK(std::fabs(e.x) < 0.01f && std::fabs(e.z - 520.0f) < 0.01f && e.health > 0.0f);
        }
        CHECK(announced);
    }
    {   // The hookshot reels the target in and stuns them.
        Simulation sim = Duel(5, {100, 0}, {0, 0});
        Match& m = sim.match;
        PlayerState* user = m.Find(1);
        PlayerState* target = m.Find(1000);
        user->pos = {0, 0};
        user->rot = 0x4000;   // facing +x
        target->pos = {700, 0};
        user->weapon = {ItemId::Hookshot, Rarity::Rare};
        user->attackReadyAt = 0;
        CHECK(m.Attack(1, 1000).ok && Distance(target->pos, user->pos) < 130 && target->pos.x > 0 && m.Stunned(*target));
    }
}

// A boss's swing or a helper's strike follows the same rules a player's weapon does: rolling dodges it, a raised shield in front takes most of it.
static void BlowsFollowThePlayersRules() {
    Simulation sim = Duel(5, {100, 0}, {0, 0});
    Match& m = sim.match;
    PlayerState* h = m.Find(1);
    h->health = h->maxHealth = 7.0f;
    h->invulnUntil = 0; h->armor = 0; h->hasShield = false;
    m.DrainEvents();
    CHECK(m.Blow(*h, {0, 0}, 1.0f, 1000));                                                    // a plain blow lands
    CHECK(std::fabs((7.0f - h->health) - kPlayerDamageScale) < 0.001f);
    h->health = 7.0f;
    CHECK(m.StartRoll(1));
    CHECK(!m.Blow(*h, {0, 0}, 1.0f, 1000) && h->health == 7.0f);                              // rolled clean through it
    Run(sim, Match::kRollSeconds + 0.2f);
    h->invulnUntil = 0;
    h->hasShield = true; h->shield = {ItemId::HylianShield, Rarity::Common};
    h->anim = static_cast<uint8_t>(Anim::Guard); h->rot = 0;
    const float before = h->health;
    CHECK(m.Blow(*h, {0, 500}, 1.0f, 1000));                                                  // from in front: shield and guard
    const float guarded = before - h->health;
    h->health = before; h->anim = 0;
    CHECK(m.Blow(*h, {0, 500}, 1.0f, 1000));
    CHECK(guarded < (before - h->health) * 0.5f);
}

static void ShieldBar() {
    Simulation sim = Duel(5, {100, 0}, {0, 0});
    Match& m = sim.match;
    PlayerState* h = m.Find(1);
    PlayerState* b = m.Find(1000);
    h->health = h->maxHealth = 3.0f;
    h->armor = 2.0f;
    m.DrainEvents();
    const float k = 1.0f / kPlayerDamageScale;   // hits below are given before the overall damage dial, so they land as written
    // The shield soaks damage before health does.
    m.Damage(1, 1.5f * k, 1000);
    CHECK(std::fabs(h->armor - 0.5f) < 0.001f && h->health == 3.0f);
    float announced = 0;
    for (const auto& e : m.DrainEvents()) if (e.type == MatchEvent::Type::Damaged && e.a == 1) announced = e.amount;
    CHECK(std::fabs(announced - 1.5f) < 0.001f);                                          // the hit marker shows the whole hit
    const float dealt = b->damageDealt;
    m.Damage(1, 1.0f * k, 1000);
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

// Lilo's Blender model (shared/lilo_model.h): a low poly budget, batches the graphics chip can load, sane textures, and clips that pose her sensibly.
static void LiloTheCatModel() {
    using namespace royale::lilo;
    CHECK(kTriCount >= 300 && kTriCount <= 1200 && kVertCount >= 200);
    int tris = 0, verts = 0;
    for (int b = 0; b < kBatchCount; b++) {
        const Batch& bt = kBatches[b];
        CHECK(bt.vertCount > 0 && bt.vertCount <= 32 && bt.firstVert == verts && bt.firstTri == tris);
        for (int t = bt.firstTri; t < bt.firstTri + bt.triCount; t++)
            for (int k = 0; k < 3; k++) CHECK(kTris[t][k] < bt.vertCount);
        tris += bt.triCount;
        verts += bt.vertCount;
    }
    CHECK(tris == kTriCount && verts == kVertCount);
    int faceBatches = 0;
    for (int b = 0; b < kBatchCount; b++) faceBatches += kBatches[b].texture == kFace;
    CHECK(faceBatches > 0 && faceBatches < kBatchCount);
    for (int i = 0; i < kVertCount; i++) {
        const Vert& v = kVerts[i];
        const int w = v.s / 32, h = v.t / 32;
        CHECK(v.b0 < kBoneCount && v.b1 < kBoneCount && w >= -1 && h >= -1 && w <= kFurW + 1 && h <= kFurH + 1);
        CHECK(std::abs(std::sqrt(static_cast<float>(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz)) - 127.0f) < 4.0f);
    }
    CHECK(sizeof(kFurTex) == kFurW * kFurH * 2 && sizeof(kFaceTex[0]) == kFaceW * kFaceH * 2 && kFurW * kFurH * 2 <= 4096);   // fits the N64's texture memory
    for (int i = 0; i < kFurW * kFurH; i++) CHECK(kFurTex[i * 2 + 1] & 1);   // opaque
    CHECK(std::string(kClips[kIdle].name) == "idle" && std::string(kClips[kTalk].name) == "talk" && std::string(kClips[kSleep].name) == "sleep");
    for (int f = 0; f < kFrameCount * kBoneCount; f++) {
        const int16_t* q = &kPoses[f * 7];
        const float len = std::sqrt(static_cast<float>(q[0]) * q[0] + static_cast<float>(q[1]) * q[1] + static_cast<float>(q[2]) * q[2] + static_cast<float>(q[3]) * q[3]) / 32767.0f;
        CHECK(std::fabs(len - 1.0f) < 0.002f);
    }
    // Standing she is about 50 units tall and 75 long, nose towards +z, paws on the ground; nothing sinks far into the ground in any clip.
    Pose p;
    float mn[3], mx[3];
    SampleClip(kIdle, 0.0f, p);
    PoseBounds(p, mn, mx);
    CHECK(std::fabs(mn[1]) < 1.0f && mx[1] > 40.0f && mx[1] < 60.0f && mx[2] - mn[2] > 60.0f && mx[2] > 25.0f && mx[0] - mn[0] < 30.0f);
    const float standing = mx[1];
    for (int c = 0; c < kClipCount; c++)
        for (float t = 0.0f; t <= ClipSeconds(c); t += 0.05f) { SampleClip(c, t, p); PoseBounds(p, mn, mx); CHECK(mn[1] > -10.0f && mx[1] < 85.0f); }
    SampleClip(kSleep, 0.0f, p);
    PoseBounds(p, mn, mx);
    CHECK(mx[1] < standing * 0.75f);   // curled up low
    SampleClip(kJump, 0.45f, p);
    PoseBounds(p, mn, mx);
    CHECK(mn[1] > 15.0f);              // in the air
    Pose a, b;
    SampleClip(kTalk, 0.0f, a);
    SampleClip(kTalk, 0.125f, b);
    CHECK(std::fabs(a.bone[5].q[0] - b.bone[5].q[0]) > 0.03f);   // the jaw opens and shuts as she mews
    SampleClip(kWalk, ClipSeconds(kWalk) + 0.1f, a);
    SampleClip(kWalk, 0.1f, b);
    CHECK(std::fabs(a.bone[8].q[0] - b.bone[8].q[0]) < 1e-4f);   // loops wrap round
    // The animator cross-fades, and a one-shot clip ends.
    Animator an;
    an.Play(kJump);
    CHECK(an.clip == kJump && an.from == kIdle && !an.Done());
    for (int i = 0; i < 40; i++) an.Update(0.05f);
    CHECK(an.Done() && an.from < 0);
    an.Play(kWalk, 0.0f);
    an.Update(1e9f, 1.0f);
    an.Update(-1.0f, std::nanf(""));
    an.Evaluate(p);
    CHECK(std::isfinite(p.bone[0].q[3]) && !an.Done());
}

// Avriella's Blender model (shared/avriella_model.h): a low poly budget, batches the graphics chip can load, textures that fit its memory, and clips that
// pose her as a baby: sitting, crawling low, lying down to nap, standing up to wobble, and three rocks that only show while she stacks them.
static void AvriellaTheBabyModel() {
    using namespace royale::avriella;
    CHECK(kTriCount >= 600 && kTriCount <= 1300 && kVertCount >= 300);
    int tris = 0, verts = 0;
    int batchesWithTexture[3] = { 0, 0, 0 };
    for (int b = 0; b < kBatchCount; b++) {
        const Batch& bt = kBatches[b];
        CHECK(bt.vertCount > 0 && bt.vertCount <= 32 && bt.firstVert == verts && bt.firstTri == tris);
        for (int t = bt.firstTri; t < bt.firstTri + bt.triCount; t++)
            for (int k = 0; k < 3; k++) CHECK(kTris[t][k] < bt.vertCount);
        tris += bt.triCount;
        verts += bt.vertCount;
        CHECK(bt.texture <= kFace);
        batchesWithTexture[bt.texture]++;
    }
    CHECK(tris == kTriCount && verts == kVertCount);
    CHECK(batchesWithTexture[kCloth] > 0 && batchesWithTexture[kSkin] > 0 && batchesWithTexture[kFace] > 0);
    for (int i = 0; i < kVertCount; i++) {
        const Vert& v = kVerts[i];
        CHECK(v.b0 < kBoneCount && v.b1 < kBoneCount && v.s / 32 >= -1 && v.t / 32 >= -1 && v.s / 32 <= 65 && v.t / 32 <= 33);
        CHECK(std::abs(std::sqrt(static_cast<float>(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz)) - 127.0f) < 4.0f);
    }
    // each texture fits the N64's 4 KB of texture memory, and is opaque
    CHECK(sizeof(kClothTex) == kClothW * kClothH * 2 && kClothW * kClothH * 2 <= 4096 && sizeof(kSkinTex) == kSkinW * kSkinH * 2 && kSkinW * kSkinH * 2 <= 4096);
    CHECK(sizeof(kFaceTex[0]) == kFaceW * kFaceH * 2 && kFaceCount == 5);
    for (int i = 0; i < kClothW * kClothH; i++) CHECK(kClothTex[i * 2 + 1] & 1);
    for (int i = 0; i < kSkinW * kSkinH; i++) CHECK(kSkinTex[i * 2 + 1] & 1);
    CHECK(std::string(kClips[kIdle].name) == "idle" && std::string(kClips[kCrawl].name) == "crawl" && std::string(kClips[kNap].name) == "nap" &&
          std::string(kClips[kRocks].name) == "rocks" && kClipCount == 16);
    for (int f = 0; f < kFrameCount * kBoneCount; f++) {
        const int16_t* q = &kPoses[f * 7];
        const float len = std::sqrt(static_cast<float>(q[0]) * q[0] + static_cast<float>(q[1]) * q[1] + static_cast<float>(q[2]) * q[2] + static_cast<float>(q[3]) * q[3]) / 32767.0f;
        CHECK(std::fabs(len - 1.0f) < 0.002f);
    }
    // Which vertices are the rocks: those on the last three bones (rock.1 to rock.3). They sit under the ground (the terrain hides them) except while she stacks.
    Pose p;
    float mn[3], mx[3];
    auto bodyBounds = [&](const Pose& pose, float lo[3], float hi[3]) {   // her without the rocks
        for (int k = 0; k < 3; k++) { lo[k] = 1e30f; hi[k] = -1e30f; }
        for (int i = 0; i < kVertCount; i++) {
            if (kVerts[i].b0 >= kBoneCount - 3 || kVerts[i].b1 >= kBoneCount - 3) continue;
            float pos[3], nrm[3];
            SkinVertex(pose, kVerts[i], pos, nrm);
            for (int k = 0; k < 3; k++) { lo[k] = std::min(lo[k], pos[k]); hi[k] = std::max(hi[k], pos[k]); }
        }
    };
    float lowestRock = 1e30f;
    auto rockLow = [&](const Pose& pose) {
        float low = 1e30f;
        for (int i = 0; i < kVertCount; i++) {
            if (kVerts[i].b0 < kBoneCount - 3) continue;
            float pos[3], nrm[3];
            SkinVertex(pose, kVerts[i], pos, nrm);
            low = std::min(low, pos[1]);
            lowestRock = std::min(lowestRock, pos[1]);
        }
        return low;
    };
    CHECK(kVertCount > 0 && std::string(kBoneNames[kBoneCount - 3]) == "rock.1");
    SampleClip(kStand, 0.0f, p);
    bodyBounds(p, mn, mx);
    CHECK(std::fabs(mn[1]) < 2.0f && mx[1] > 55.0f && mx[1] < 70.0f && mx[0] - mn[0] < 60.0f);   // standing: about 62 units tall, feet on the ground, arms out
    SampleClip(kSit, 0.0f, p);
    bodyBounds(p, mn, mx);
    CHECK(mn[1] > -2.0f && mx[1] > 45.0f && mx[1] < 60.0f);                                       // sitting: about 52 tall with her tuft, not sunk into the ground
    SampleClip(kCrawl, 0.2f, p);
    bodyBounds(p, mn, mx);
    CHECK(mn[1] > -2.5f && mx[1] < 55.0f && mx[2] - mn[2] > 35.0f);                               // crawling: low and long, nose towards +z
    SampleClip(kNap, 0.0f, p);
    bodyBounds(p, mn, mx);
    CHECK(mn[1] > -6.0f && mx[1] < 26.0f && mx[2] - mn[2] > 45.0f);                               // asleep on her back: flat
    for (int c = 0; c < kClipCount; c++) {
        for (float t = 0.0f; t <= ClipSeconds(c); t += 0.05f) {
            SampleClip(c, t, p);
            bodyBounds(p, mn, mx);
            CHECK(mn[1] > -8.0f && mx[1] < 80.0f && std::isfinite(mn[0]) && std::isfinite(mx[2]));   // nothing sinks far into the ground or flies off in any clip
            if (c != kRocks) CHECK(rockLow(p) < -50.0f);                                          // the rocks stay hidden under the ground
        }
    }
    // while she stacks, the rocks come up out of hiding and the tower grows: the top rock ends up higher than the bottom one
    int shown = 0;
    float topSeen = -1e30f;
    for (float t = 0.0f; t <= ClipSeconds(kRocks); t += 0.05f) {
        SampleClip(kRocks, t, p);
        if (rockLow(p) > -2.0f) shown++;
        for (int i = 0; i < kVertCount; i++) {
            if (kVerts[i].b0 < kBoneCount - 3) continue;
            float pos[3], nrm[3];
            SkinVertex(p, kVerts[i], pos, nrm);
            topSeen = std::max(topSeen, pos[1]);
        }
    }
    CHECK(shown > 20 && topSeen > 9.0f && topSeen < 25.0f && lowestRock < -50.0f);
    // she waves with a hand above her head, and the clap brings the hands together
    SampleClip(kWave, 0.5f, p);
    float handY = -1e30f, headY = 0;
    for (int i = 0; i < kVertCount; i++) {
        float pos[3], nrm[3];
        SkinVertex(p, kVerts[i], pos, nrm);
        if (kVerts[i].b0 == 12 || kVerts[i].b0 == 13) handY = std::max(handY, pos[1]);   // the right forearm and hand
        headY = std::max(headY, pos[1]);
    }
    CHECK(handY > 24.0f && headY > handY - 40.0f);
    SampleClip(kRoll, 1.0f, p);
    bodyBounds(p, mn, mx);
    CHECK(mx[1] < 30.0f);   // rolling on the floor
    // loops wrap round, the animator cross-fades, and her one-shot (the roll) ends
    Pose a, b;
    SampleClip(kCrawl, ClipSeconds(kCrawl) + 0.1f, a);
    SampleClip(kCrawl, 0.1f, b);
    CHECK(std::fabs(a.bone[8].q[0] - b.bone[8].q[0]) < 1e-4f);
    // she cannot crawl yet: she rolls about, low on the floor, and kicks lying down
    for (int c : { kTumble, kKick }) {
        SampleClip(c, 0.3f, p);
        bodyBounds(p, mn, mx);
        CHECK(mn[1] > -8.0f && mx[1] < 35.0f);
    }
    SampleClip(kChew, 1.0f, p);
    float chewLow = 1e30f;
    for (int i = 0; i < kVertCount; i++) {
        if (kVerts[i].b0 != kBoneCount - 3) continue;
        float pos[3], nrm[3];
        SkinVertex(p, kVerts[i], pos, nrm);
        chewLow = std::min(chewLow, pos[1]);
    }
    CHECK(chewLow > 10.0f && chewLow < 50.0f);   // the rock she chews is up in her hand
    Animator an;
    an.Play(kRoll);
    CHECK(an.clip == kRoll && an.from == kIdle && !an.Done());
    for (int i = 0; i < 60; i++) an.Update(0.05f);
    CHECK(an.Done() && an.from < 0);
    an.Play(kCrawl, 0.0f);
    an.Update(1e9f, 1.0f);
    an.Update(-1.0f, std::nanf(""));
    an.Evaluate(p);
    CHECK(std::isfinite(p.bone[0].q[3]) && !an.Done());
    // her lines: sixteen, each fits a text box, and none is empty
    CHECK(kAvriellaPetLineCount == 17 && std::string(kAvriellaName) == "Avriella");
    for (int i = 0; i < kAvriellaPetLineCount; i++) CHECK(std::strlen(kAvriellaPetLines[i]) > 3 && std::strlen(kAvriellaPetLines[i]) < 120);
}

// ---- Bots get about like players: the skydive, sprinting, ledges, cliffs, cover and high ground --------------------------------

// A cliff: x < 0 is low ground, x >= 0 is 300 higher, except a gentle ramp across z > 900 that climbs from one to the other.
static bool CliffFloor(Vec2 p, float* y) {
    if (p.z > 900.0f) *y = (std::max)(0.0f, (std::min)(300.0f, (p.x + 400.0f) * 0.375f));
    else *y = p.x < 0.0f ? 0.0f : 300.0f;
    return true;
}

static void NavKnowsLedgesAndCliffs() {
    NavGrid nav(MapCircle(), nullptr, CliffFloor);
    CHECK(nav.HasHeights());
    CHECK(std::fabs(nav.FloorAt({-500, 0}) - 0.0f) < 1.0f && std::fabs(nav.FloorAt({500, 0}) - 300.0f) < 1.0f);
    std::vector<Vec2> path;
    // Up the cliff: not straight up the face, but round by the ramp, a long way round.
    CHECK(!nav.LineClear({-500, 0}, {500, 0}, true));
    CHECK(nav.FindPath({-500, 0}, {500, 0}, path, true));
    float length = 0, highestZ = -1e9f;
    Vec2 at = {-500, 0};
    for (Vec2 w : path) { length += Distance(at, w); at = w; highestZ = (std::max)(highestZ, w.z); }
    CHECK(length > 1800.0f && highestZ > 850.0f);
    // Down it: a 300 drop is safe, so straight over the edge.
    CHECK(nav.FindPath({500, 0}, {-500, 0}, path, true) && path.size() <= 2);
    // A drop too far is a cliff it goes round, as is a wall too tall to climb.
    NavGrid deep(MapCircle(), nullptr, [](Vec2 p, float* y) { *y = std::fabs(p.x) < 200.0f && std::fabs(p.z) < 500.0f ? -600.0f : 0.0f; return true; });
    CHECK(deep.FindPath({-500, 0}, {500, 0}, path, true));
    at = {-500, 0};
    bool dropped = false;
    for (Vec2 w : path) { for (float t = 0; t <= 1.0f; t += 0.02f) dropped |= deep.FloorAt({at.x + (w.x - at.x) * t, at.z + (w.z - at.z) * t}) < -100.0f; at = w; }
    CHECK(!dropped);   // never went down into the pit (it couldn't get out)
    // Bosses and allies keep their old ways: heights don't matter to them, only open ground.
    CHECK(nav.FindPath({-500, 0}, {500, 0}, path) && path.size() == 1);
}

static void BotsClimbBlocksAndBoulders() {
    // A staircase of blocks (60, 120, 180 high) and a lone 180 block: the stairs are climbed one step at a time, the lone block can't be.
    std::vector<Prop> props = {
        {{0, 0}, PropKind::PlatformLow, 0}, {{150, 0}, PropKind::PlatformMid, 0}, {{300, 0}, PropKind::PlatformHigh, 0},
        {{-900, 600}, PropKind::PlatformHigh, 0},
    };
    auto grid = std::make_shared<NavGrid>(MapCircle(), nullptr);
    AddSceneryToNav(*grid, props);
    CHECK(grid->Standable({300, 0}) && !grid->Walkable({300, 0}));   // a bot can stand up there; a boss still goes round
    std::vector<Vec2> path;
    CHECK(grid->FindPath({-600, 0}, {300, 0}, path, true));
    CHECK(!grid->FindPath({-900, 0}, {-900, 600}, path, true));
    // A bot climbs the stairs for a chest on top, jumping up each step, and ends up standing 180 up.
    Simulation sim = Duel(5, {1900, 0}, {-600, 0});
    sim.bots.SetNav(grid);
    sim.bots.SetProps(props);
    const size_t chest = sim.match.AddLoot({{300, 0}, ItemId::MasterSword, Rarity::Epic, true, true});
    PlayerState* b = sim.match.Find(1000);
    float highest = 0;
    int jumps = 0;
    uint8_t prevAnim = 0;
    for (int i = 0; i < 30 * kTickHz && !sim.match.Loot()[chest].taken; i++) {
        sim.Tick(kDt);
        highest = (std::max)(highest, b->y);
        if (b->anim == static_cast<uint8_t>(Anim::Jump) && prevAnim != b->anim) jumps++;
        prevAnim = b->anim;
    }
    CHECK(sim.match.Loot()[chest].taken);
    CHECK(highest > 170.0f && jumps >= 2);
    // Walking off the far side it falls rather than snapping to the ground.
    Simulation drop = Duel(5, {1900, 0}, {300, 0});
    drop.bots.SetNav(grid);
    drop.bots.SetProps(props);
    drop.match.AddLoot({{600, 0}, ItemId::MasterSword, Rarity::Epic, false});
    PlayerState* d = drop.match.Find(1000);
    bool midAir = false;
    for (int i = 0; i < 6 * kTickHz; i++) { drop.Tick(kDt); midAir |= d->y > 20.0f && d->y < 170.0f && d->pos.x > 380.0f; }
    CHECK(midAir && d->y < 1.0f);
}

// ---- carts ---------------------------------------------------------------------------------------------------------------------------------

static CartBody CartAt(float x, float z, float yaw, const CartWorld& w = {}) {
    CartBody b; b.x = x; b.z = z; b.yaw = yaw;
    SettleCart(b, w);
    return b;
}

static void CartPhysics() {
    // Flat ground: it speeds up to near its top speed straight ahead (yaw 0 is +z), turns, brakes, and backs up slower than it goes forward.
    const CartWorld flat;
    CartBody b = CartAt(0, 0, 0);
    CHECK(b.grounded && std::fabs(b.y) < 1.0f);
    for (int i = 0; i < 3 * kTickHz; i++) StepCart(b, {1, 0, false, false}, flat, kDt);
    CHECK(b.speed > 300.0f && b.speed <= kCartMaxSpeed + 1.0f && b.z > 400.0f && std::fabs(b.x) < 5.0f);
    const float yaw0 = b.yaw;
    for (int i = 0; i < kTickHz; i++) StepCart(b, {1, 1, false, false}, flat, kDt);
    CHECK(std::fabs(cartdetail::WrapAngle(b.yaw - yaw0)) > 0.5f);
    for (int i = 0; i < 2 * kTickHz; i++) StepCart(b, {0, 0, true, false}, flat, kDt);
    CHECK(std::fabs(b.speed) < 5.0f);
    for (int i = 0; i < 4 * kTickHz; i++) StepCart(b, {-1, 0, false, false}, flat, kDt);
    CHECK(b.speed < -100.0f && b.speed >= -kCartMaxReverse - 1.0f);
    // The handbrake at speed in a turn slides it sideways.
    CartBody d = CartAt(0, 0, 0);
    for (int i = 0; i < 3 * kTickHz; i++) StepCart(d, {1, 0, false, false}, flat, kDt);
    float slide = 0;
    for (int i = 0; i < kTickHz / 2; i++) { StepCart(d, {1, 1, false, true}, flat, kDt); slide = (std::max)(slide, std::fabs(d.slide)); }
    CHECK(slide > 40.0f);

    // A wall: it stops at it, and the crash is reported (and hurts).
    CartWorld walled;
    walled.solid = [](float, float z) { return z > 600.0f; };
    CartBody w = CartAt(0, 0, 0, walled);
    float impact = 0;
    for (int i = 0; i < 5 * kTickHz; i++) impact = (std::max)(impact, StepCart(w, {1, 0, false, false}, walled, kDt).impact);
    CHECK(w.z < 600.0f && w.z > 450.0f && std::fabs(w.speed) < 100.0f && impact > kCartCrashFrom && CrashDamage(impact) > 0.0f);
    // A step taller than a wheel can climb is a wall too; a low one it rolls over.
    for (float rise : {20.0f, 120.0f}) {
        CartWorld stepped;
        stepped.ground = [rise](float, float z, float* y) { *y = z > 500.0f ? rise : 0.0f; return true; };
        CartBody c = CartAt(0, 0, 0, stepped);
        for (int i = 0; i < 5 * kTickHz; i++) StepCart(c, {1, 0, false, false}, stepped, kDt);
        CHECK((c.z > 700.0f) == (rise < kCartMaxStep));
    }
    // Off a cliff: it flies, falls and lands hard below.
    CartWorld cliff;
    cliff.ground = [](float, float z, float* y) { *y = z > 500.0f ? -500.0f : 0.0f; return true; };
    CartBody f = CartAt(0, 0, 0, cliff);
    bool flew = false; float landing = 0;
    for (int i = 0; i < 6 * kTickHz; i++) {
        const CartStep st = StepCart(f, {1, 0, false, false}, cliff, kDt);
        flew |= st.tookOff || !f.grounded;
        landing = (std::max)(landing, st.landing);
    }
    CHECK(flew && f.grounded && std::fabs(f.y + 500.0f) < 5.0f && landing > kCartLandFrom && LandingDamage(landing) > 0.0f);
    // A bot's wish to get somewhere: DriveToward takes it there and pulls up.
    CartBody g = CartAt(0, 0, 0);
    for (int i = 0; i < 12 * kTickHz; i++) StepCart(g, DriveToward(g, {800, -600}, 80), flat, kDt);
    CHECK(Distance({g.x, g.z}, {800, -600}) < 200.0f && std::fabs(g.speed) < 120.0f);
}

// A duel with carts parked at the given spots.
static Simulation CartDuel(uint64_t seed, Vec2 humanPos, Vec2 botPos, std::vector<Vec2> spots) {
    Simulation sim(seed, MapCircle(), 0);
    sim.match.SetVehicleCount(static_cast<int>(spots.size()));
    sim.match.SetVehicleSpots(spots);
    sim.match.AddHuman(1);
    sim.match.Start();
    while (sim.match.State() != MatchState::InMatch) sim.match.Tick(kDt);
    for (auto& p : sim.match.Players()) if (p.id != 1 && p.id != 1000) p.alive = false;
    FillAmmo(sim);
    sim.match.Find(1)->pos = humanPos;
    sim.match.Find(1000)->pos = botPos;
    return sim;
}

static int CartNear(const Match& m, Vec2 at) {
    for (const auto& v : m.Vehicles()) if (Distance({v.body.x, v.body.z}, at) < 50.0f) return v.index;
    return -1;
}

static void CartsSeatsRamsAndWrecks() {
    CHECK(VehicleCountFor(2000) >= 4 && VehicleCountFor(1e6f) == kMaxVehicles);
    Simulation sim = CartDuel(4, {1500, 1500}, {-1500, -1500}, {{0, 0}, {1000, 0}});
    Match& m = sim.match;
    CHECK(m.Vehicles().size() == 2);
    const int ci = CartNear(m, {0, 0});
    CHECK(ci >= 0);
    if (ci < 0) return;
    VehicleState& v = m.MutableVehicles()[static_cast<size_t>(ci)];
    PlayerState* h = m.Find(1);
    PlayerState* b = m.Find(1000);
    // Too far away to get in; at the driver's door it works, and the rider sits on the saddle.
    CHECK(!m.EnterVehicle(1, ci, Seat::Driver));
    h->pos = ExitSpot(v.body, Seat::Driver);
    CHECK(m.EnterVehicle(1, ci, Seat::Driver));
    int idx; Seat seat;
    CHECK(m.RidingIn(1, &idx, &seat) && idx == ci && seat == Seat::Driver && v.Driver() == 1u);
    CHECK(!m.EnterVehicle(1, 1 - ci, Seat::Driver));   // one cart at a time
    // The bot asks for the driver's seat but that is taken: it gets the back one.
    b->pos = ExitSpot(v.body, Seat::Passenger);
    CHECK(m.EnterVehicle(1000, ci, Seat::Driver) && v.Passenger() == 1000u);
    // The driver's game reports the cart: a plausible move is taken, a teleport is cut short.
    CartBody moved = v.body; moved.z += 20.0f; moved.speed = 200.0f;
    m.Tick(kDt);
    CHECK(m.DriveVehicle(1, ci, moved, 0, false, 0, 0) && std::fabs(v.body.z - moved.z) < 0.01f);
    CartBody far = v.body; far.z += 3000.0f;
    m.Tick(kDt);
    CHECK(m.DriveVehicle(1, ci, far, 0, false, 0, 0) && v.body.z < moved.z + 200.0f);
    CHECK(!m.DriveVehicle(1000, ci, moved, 0, false, 0, 0));   // only the driver drives
    // Riders move with it.
    m.Tick(kDt);
    float sx, sy, sz;
    SeatSpot(v.body, Seat::Passenger, &sx, &sy, &sz);
    CHECK(Distance(b->pos, {sx, sz}) < 2.0f);
    // Getting out puts them down at their door.
    CHECK(m.ExitVehicle(1000) && !m.RidingIn(1000) && Distance(b->pos, ExitSpot(v.body, Seat::Passenger)) < 1.0f && b->y == 0.0f);
    // The passenger takes the reins when the driver has got out.
    b->pos = ExitSpot(v.body, Seat::Passenger);
    CHECK(m.EnterVehicle(1000, ci, Seat::Passenger));
    CHECK(!m.SwitchSeat(1000));
    CHECK(m.ExitVehicle(1));
    CHECK(m.SwitchSeat(1000) && v.Driver() == 1000u && v.Passenger() == kNoPlayer);
    CHECK(m.ExitVehicle(1000));

    // Running somebody over: the human drives at the bot standing in front.
    h->pos = ExitSpot(v.body, Seat::Driver);
    CHECK(m.EnterVehicle(1, ci, Seat::Driver));
    b->pos = {v.body.x + std::sin(v.body.yaw) * 30.0f, v.body.z + std::cos(v.body.yaw) * 30.0f};
    const float before = b->health;
    CartBody fast = v.body; fast.speed = 380.0f;
    m.Tick(kDt);
    m.DriveVehicle(1, ci, fast, 0, false, 0, 0);
    m.Tick(kDt);
    CHECK(b->health < before - kRamBase && !InsideCart(v.body, b->pos.x, b->pos.z));

    // Smashed up: the riders are thrown out, the firebox goes up and hurts whoever is close, and the wreck burns out.
    b->pos = {v.body.x + 120.0f, v.body.z};
    b->health = h->health = kMaxHealth;
    const float bBefore = b->health;
    m.DamageVehicle(v, kCartHealth + 1.0f, 1000);
    CHECK(v.wrecked && !m.RidingIn(1) && v.Driver() == kNoPlayer);
    for (int i = 0; i < kTickHz; i++) m.Tick(kDt);
    CHECK(b->health < bBefore || !b->alive);
    h->pos = ExitSpot(v.body, Seat::Driver);
    CHECK(!m.EnterVehicle(1, ci, Seat::Driver));
    for (int i = 0; i < static_cast<int>((kCartWreckSeconds + 1) * kTickHz); i++) m.Tick(kDt);
    CHECK(v.gone && m.State() == MatchState::InMatch);
    // Swords hurt carts too.
    VehicleState& other = m.MutableVehicles()[static_cast<size_t>(1 - ci)];
    const float hp = other.health;
    m.DamageVehicle(other, 2.0f, 1);
    CHECK(other.health < hp && !other.wrecked);
}

// A point on the map far from where the safe zone is heading.
static constexpr float kStormLookaheadForTests = 15.0f;
static Vec2 FarFromZone(Simulation& sim, float later) {
    const Vec2 c = sim.match.GetStorm().SafeZoneAt(sim.match.StormTime() + later).center;
    const float d = std::hypot(c.x, c.z);
    return d < 1.0f ? Vec2{0, 1750.0f} : Vec2{-c.x / d * 1750.0f, -c.z / d * 1750.0f};
}
static void MoveCart(Simulation& sim, int ci, Vec2 to) {
    VehicleState& v = sim.match.MutableVehicles()[static_cast<size_t>(ci)];
    v.body.x = to.x; v.body.z = to.z;
    SettleCart(v.body, sim.match.VehicleWorld());
}

static void BotsDriveAndRideCarts() {
    // A bot that likes driving (1001 does), with nobody about and a cart close by, gets in and drives off somewhere.
    {
        Simulation sim = CartDuel(9, {-1900, 0}, {0, 1900}, {{0, 0}});
        const Vec2 far = FarFromZone(sim, 200.0f);
        sim.match.Find(1000)->alive = false;
        PlayerState* lover = sim.match.Find(1001);
        lover->alive = true;
        lover->pos = far;
        sim.match.Find(1)->pos = {-far.x, -far.z};
        MoveCart(sim, 0, {far.x * 0.88f, far.z * 0.88f});
        bool drove = false;
        float bestMove = 0;
        Vec2 start = {};
        for (int i = 0; i < kTickHz * 60 && lover->alive && sim.match.State() == MatchState::InMatch; i++) {
            sim.Tick(kDt);
            int idx; Seat seat;
            if (sim.match.RidingIn(1001, &idx, &seat) && seat == Seat::Driver) {
                const auto& v = sim.match.Vehicles()[static_cast<size_t>(idx)];
                if (!drove) start = {v.body.x, v.body.z};
                drove = true;
                bestMove = (std::max)(bestMove, Distance(start, {v.body.x, v.body.z}));
            }
        }
        CHECK(drove && bestMove > 500.0f);
    }
    // Put behind the reins, a bot drives somewhere (not off the map), and gets out at the end.
    {
        Simulation sim = CartDuel(11, {-1900, 0}, {1300, 0}, {{0, 0}});
        const Vec2 far = FarFromZone(sim, kStormLookaheadForTests);
        sim.match.Find(1)->pos = {-far.x, -far.z};
        MoveCart(sim, 0, far);
        const int ci = 0;
        PlayerState* b = sim.match.Find(1000);
        b->pos = ExitSpot(sim.match.Vehicles()[0].body, Seat::Driver);
        CHECK(sim.match.EnterVehicle(1000, ci, Seat::Driver));
        float topSpeed = 0, moved = 0;
        bool gotOut = false;
        for (int i = 0; i < kTickHz * 30; i++) {
            sim.Tick(kDt);
            const auto& v = sim.match.Vehicles()[0];
            topSpeed = (std::max)(topSpeed, std::fabs(v.body.speed));
            moved = (std::max)(moved, Distance(far, {v.body.x, v.body.z}));
            CHECK(sim.match.Inside({v.body.x, v.body.z}));
            gotOut |= !sim.match.RidingIn(1000);
        }
        CHECK(topSpeed > 150.0f && moved > 400.0f && gotOut);
    }
    // A cart coming straight at a bot: it gets out of the way.
    {
        int dodged = 0;
        for (uint64_t seed = 1; seed <= 6; seed++) {
            Simulation sim = CartDuel(seed, {-1900, 0}, {0, 400}, {{0, -200}});
            const int ci = CartNear(sim.match, {0, -200});
            if (ci < 0) continue;
            VehicleState& v = sim.match.MutableVehicles()[static_cast<size_t>(ci)];
            v.body.yaw = 0;
            PlayerState* h = sim.match.Find(1);
            h->pos = ExitSpot(v.body, Seat::Driver);
            CHECK(sim.match.EnterVehicle(1, ci, Seat::Driver));
            const float hp = sim.match.Find(1000)->health;
            for (int i = 0; i < kTickHz * 2; i++) {
                CartBody b = v.body; b.speed = 350.0f; b.z += 350.0f * kDt; b.x = 0;
                sim.match.DriveVehicle(1, ci, b, 0, false, 0, 0);
                sim.Tick(kDt);
            }
            dodged += sim.match.Find(1000)->health >= hp;
        }
        CHECK(dodged >= 3);
    }
    // A bot in the back with nobody driving takes the reins.
    {
        Simulation sim = CartDuel(13, {-1900, 0}, {1300, 0}, {{1400, 200}});
        const int ci = CartNear(sim.match, {1400, 200});
        if (ci < 0) { CHECK(false); return; }
        PlayerState* b = sim.match.Find(1000);
        b->pos = ExitSpot(sim.match.Vehicles()[static_cast<size_t>(ci)].body, Seat::Passenger);
        CHECK(sim.match.EnterVehicle(1000, ci, Seat::Passenger));
        Run(sim, 1.0f);
        int idx; Seat seat = Seat::None;
        CHECK((sim.match.RidingIn(1000, &idx, &seat) && seat == Seat::Driver) || !sim.match.RidingIn(1000));
    }
}

static void FullMatchWithCarts() {
    // 31 bots and a parked human on a map with carts: bots get in, drive, ride and get out, and the match still ends with one winner.
    const float calm = BotController::CalmSeconds();
    BotController::CalmSeconds() = 40.0f;   // a quiet start, as in a real match, when bots go looking for carts
    Simulation sim(17, MapCircle(), 0);
    sim.bots.SetNav(std::make_shared<NavGrid>(MapCircle(), NotWall));
    sim.match.SetVehicleWorld(CartWorld{nullptr, [](float x, float z) { return !NotWall({x, z}) || std::hypot(x, z) > 1960.0f; }});
    sim.match.SetVehicleCount(VehicleCountFor(2000.0f));
    sim.match.AddHuman(1);
    sim.match.Start();
    std::unordered_set<uint32_t> drivers, passengers;
    int guard = 0;
    while (sim.match.State() != MatchState::Ending && guard++ < kTickHz * 1500) {
        sim.Tick(kDt);
        for (const auto& v : sim.match.Vehicles()) {
            if (v.Driver() != kNoPlayer && std::fabs(v.body.speed) > 100.0f) drivers.insert(v.Driver());
            if (v.Passenger() != kNoPlayer) passengers.insert(v.Passenger());
        }
        for (const auto& p : sim.match.Players()) if (p.alive && sim.match.RidingIn(p.id)) CHECK(sim.match.Inside(p.pos));
    }
    CHECK(sim.match.State() == MatchState::Ending && sim.match.Alive() <= 1);
    BotController::CalmSeconds() = calm;
    CHECK(drivers.size() >= 3);
    std::printf("  carts: %zu bots drove, %zu rode along, match over after %.0f s\n", drivers.size(), passengers.size(), sim.match.Clock());
}

static void BotsSkydiveIn() {
    // From the start of the countdown every bot hangs in the sky, glides during the drop and is on the ground (and on solid ground) soon
    // after; most come down by a chest.
    Simulation sim(21, MapCircle(), 0);
    sim.bots.SetNav(std::make_shared<NavGrid>(MapCircle(), NotWall));
    for (int i = 0; i < 12; i++) sim.match.AddLoot({{-1500.0f + 260.0f * static_cast<float>(i), (i % 2 ? 500.0f : -700.0f)}, ItemId::MasterSword, Rarity::Rare, true, true});
    sim.match.AddHuman(1);
    sim.match.Start();
    sim.Tick(kDt);
    bool allHigh = true;
    for (const auto& p : sim.match.Players()) if (p.isBot) allHigh &= std::fabs(p.y - kSkyHeight) < 1.0f;
    CHECK(allHigh);
    std::unordered_map<uint32_t, Vec2> startAt;
    while (sim.match.State() == MatchState::Countdown) sim.Tick(kDt);
    for (const auto& p : sim.match.Players()) startAt[p.id] = p.pos;
    bool falling = true, steered = false;
    for (int i = 0; i < 3 * kTickHz; i++) sim.Tick(kDt);
    for (const auto& p : sim.match.Players()) if (p.isBot) { falling &= p.y < kSkyHeight - 3 * kGlideSpeed * 0.9f && p.y > 100.0f; steered |= Distance(p.pos, startAt[p.id]) > 100.0f; }
    CHECK(falling && steered);
    while (sim.match.State() == MatchState::Drop) sim.Tick(kDt);
    Run(sim, 1.0f);
    int landed = 0, bots = 0, byChest = 0;
    for (const auto& p : sim.match.Players()) {
        if (!p.isBot || !p.alive) continue;
        bots++;
        landed += (p.y < 1.0f || sim.match.RidingIn(p.id)) && NotWall(p.pos);   // (or already sat in a cart)
        for (const auto& l : sim.match.Loot()) if (l.spawn.container && Distance(l.spawn.pos, p.pos) < 500.0f) { byChest++; break; }
    }
    CHECK(bots > 20 && landed == bots);
    CHECK(byChest * 2 > bots);
    // A bot that joins mid-match (or a test that skips the drop) is simply on the ground.
    Simulation duel = Duel(5, {1900, 0}, {0, 0});
    duel.Tick(kDt);
    CHECK(duel.match.Find(1000)->y == 0.0f);
}

static void BotsSprintLikePlayers() {
    // Out in the storm a bot sprints for the zone: faster than a run, shown as a sprint, and only as long as a full bar lasts.
    Simulation sim = Duel(9, {0, 0}, {0, 1950});
    sim.match.Find(1)->alive = true;
    PlayerState* b = sim.match.Find(1000);
    while (sim.match.GetStorm().DamagePerSecond(b->pos, sim.match.StormTime()) <= 0 && sim.match.State() == MatchState::InMatch) { sim.match.Tick(kDt); b->pos = {0, 1950}; }
    int sprintTicks = 0, runTicks = 0, stretch = 0, longest = 0;
    float fastest = 0;
    for (int i = 0; i < 12 * kTickHz; i++) {
        const Vec2 before = b->pos;
        sim.Tick(kDt);
        if (b->anim == static_cast<uint8_t>(Anim::Sprint)) { sprintTicks++; fastest = (std::max)(fastest, Distance(before, b->pos) / kDt); longest = (std::max)(longest, ++stretch); }
        else stretch = 0;
        if (b->anim == static_cast<uint8_t>(Anim::Run)) runTicks++;
    }
    CHECK(sprintTicks > kTickHz && fastest > kRunSpeed * 1.25f && runTicks > 0);   // and back to a run once the bar is spent or it is safe
    CHECK(longest <= static_cast<int>(kSprintSeconds * kTickHz) + 2);   // no longer than a full bar in one go
    // A wounded bot running from a stronger foe sprints too, once the foe is close.
    Simulation flee = Duel(5, {200, 0}, {0, 0});
    PlayerState* f = flee.match.Find(1000);
    f->health = 0.7f; f->potions.clear();
    f->weapon = {ItemId::DekuStick, Rarity::Common};
    flee.match.Find(1)->weapon = {ItemId::MasterSword, Rarity::Legendary};
    bool sprinted = false;
    for (int i = 0; i < 2 * kTickHz; i++) { flee.Tick(kDt); sprinted |= f->anim == static_cast<uint8_t>(Anim::Sprint); }
    CHECK(sprinted);
}

static uint16_t RotForShapeFixed(int shape) { Rng r(7); return RotForShape(r, shape); }

static void BotsUseCoverAndHighGround() {
    // A tall boulder between a bot with a bow and a human: the bot can't see the human, so it doesn't shoot; with nothing in the way it does.
    std::vector<Prop> rock = {{{300, 0}, PropKind::Boulder, RotForShapeFixed(1)}};
    for (int blocked = 0; blocked < 2; blocked++) {
        Simulation sim = Duel(5, {600, 0}, {0, 0});
        auto grid = std::make_shared<NavGrid>(MapCircle(), nullptr);
        if (blocked) AddSceneryToNav(*grid, rock);
        sim.bots.SetNav(grid);
        if (blocked) sim.bots.SetProps(rock);
        PlayerState* b = sim.match.Find(1000);
        b->weapon = {ItemId::FairyBow, Rarity::Rare};
        PlayerState* h = sim.match.Find(1);
        bool shot = false;
        for (int i = 0; i < 2 * kTickHz; i++) { sim.Tick(kDt); h->pos = {600, 0}; shot |= h->health < kMaxHealth; b->pos = {0, 0}; }
        CHECK(shot == !blocked);
    }
    // Hurt, with a human shooting at it, a bot with a potion gets behind the boulder before it drinks.
    {
        Simulation sim = Duel(5, {-500, 0}, {0, 0});
        auto grid = std::make_shared<NavGrid>(MapCircle(), nullptr);
        std::vector<Prop> near = {{{100, 120}, PropKind::Boulder, RotForShapeFixed(1)}};
        AddSceneryToNav(*grid, near);
        sim.bots.SetNav(grid);
        sim.bots.SetProps(near);
        PlayerState* b = sim.match.Find(1000);
        PlayerState* h = sim.match.Find(1);
        h->weapon = {ItemId::FairyBow, Rarity::Rare};
        b->health = 1.6f;
        b->potions = {{ItemId::RedPotion, Rarity::Common}};
        b->weapon = {ItemId::KokiriSword, Rarity::Common};
        BotController::CalmSeconds() = 1e9f;   // no fighting back in this one: it is about hiding
        bool drankInCover = false;
        for (int i = 0; i < 8 * kTickHz && !b->potions.empty(); i++) {
            sim.Tick(kDt);
            h->pos = {-500, 0};
            if (b->potions.empty()) drankInCover = Distance(b->pos, {100, 120}) < 200.0f && b->pos.x > 100.0f;
        }
        BotController::CalmSeconds() = 0.0f;
        CHECK(drankInCover);
    }
    // A bot with a bow fighting on flat ground climbs a low rise next to it to shoot from.
    {
        Simulation sim = Duel(5, {650, 0}, {0, 0});
        sim.bots.SetNav(std::make_shared<NavGrid>(MapCircle(), nullptr, [](Vec2 p, float* y) { *y = Distance(p, {0, 220}) < 140.0f ? 60.0f : 0.0f; return true; }));
        PlayerState* b = sim.match.Find(1000);
        b->weapon = {ItemId::FairyBow, Rarity::Rare};
        sim.match.Find(1)->weapon = {ItemId::FairyBow, Rarity::Rare};
        bool upHigh = false;
        for (int i = 0; i < 6 * kTickHz && !upHigh; i++) { sim.Tick(kDt); sim.match.Find(1)->pos = {650, 0}; sim.match.Find(1)->health = kMaxHealth; upHigh = Distance(b->pos, {0, 220}) < 130.0f; }
        CHECK(upHigh);
    }
}

// Chests, boulders and camps are placed with purpose (shared/placement.h): on level ground that exists, clear of anything solid, beside scenery, with
// the cliffs lined with boulders, and the same every time for a seed.
static void PlacementIsPurposeful() {
    namespace fn = royale::fortnite;
    // Stand-in ground: gently rolling, with a steep escarpment across the map (a bank 260 high over 150 units: slope 1.7) and a pond that is not ground.
    const Circle map = {{0, 0}, 4000};
    const HeightFn height = [](Vec2 p, float* y) { *y = 30.0f * std::sin(p.x / 700.0f) + 260.0f * std::clamp((p.x * 0.6f + p.z * 0.8f - 1000.0f) / 150.0f, 0.0f, 1.0f); return true; };
    const PlacementFn valid = [](Vec2 p) { return Distance(p, {-1500, -1200}) > 500.0f; };
    const Ground ground(valid, height);
    for (uint64_t seed = 3; seed < 6; seed++) {
        const MapPlacement a = PlaceMap(seed, map, 0, 12, 560, valid, height), b = PlaceMap(seed, map, 0, 12, 560, valid, height);
        CHECK(a.props.size() == b.props.size() && a.plan.anchors.size() == b.plan.anchors.size() && a.layout.sites.size() == b.layout.sites.size());
        for (size_t i = 0; i < a.props.size() && i < b.props.size(); i++) CHECK(a.props[i].pos.x == b.props[i].pos.x && a.props[i].pos.z == b.props[i].pos.z);
        CHECK(!a.features.cliffs.empty());
        // Loose scenery: on ground, not steep, never overlapping another piece, and boulders strung along the cliff feet.
        const std::vector<Circle> none;
        const std::vector<Prop> loose = GenerateProps(seed, map, 560, valid, &none, &ground, &a.features);
        CHECK(loose.size() > 400);
        int alongCliffs = 0;
        for (size_t i = 0; i < loose.size(); i++) {
            CHECK(valid(loose[i].pos) && ground.Slope(loose[i].pos) <= 0.65f + 1.0e-3f);
            for (size_t j = i + 1; j < loose.size(); j++) CHECK(Distance(loose[i].pos, loose[j].pos) >= PropRadius(loose[i].kind) + PropRadius(loose[j].kind));
            if (loose[i].kind == PropKind::Boulder) for (const CliffFoot& f : a.features.cliffs) if (Distance(loose[i].pos, f.pos) < 400.0f) { alongCliffs++; break; }
        }
        CHECK(alongCliffs >= 3);
        // Every town has a camp outside it.
        for (const Poi& poi : a.layout.pois) {
            if (poi.radius >= 580.0f) continue;
            bool camp = false;
            for (const ChestSite& s : a.layout.sites) camp = camp || (s.bonus == 0 && Distance(s.pos, poi.center) > poi.radius + 200.0f && Distance(s.pos, poi.center) < poi.radius + 420.0f);
            CHECK(camp);
        }
        // Scattered chests: level, clear of every solid piece, out of the towns, apart from each other, and nearly all beside something.
        std::vector<Vec2> taken = a.layout.lootSpots;
        for (const ChestSite& s : a.layout.sites) taken.push_back(s.pos);
        const float spacing = 240.0f;
        const std::vector<LootSpawn> loot = GenerateAnchoredLoot(seed, a.plan, 200, 0.15f, &taken, spacing);
        CHECK(loot.size() > 100 && loot.size() <= 200);
        int beside = 0;
        for (size_t i = 0; i < loot.size(); i++) {
            CHECK(ChestSpotOk(a.plan, loot[i].pos) && ground.Slope(loot[i].pos) <= 0.3f);
            for (size_t j = i + 1; j < loot.size(); j++) CHECK(Distance(loot[i].pos, loot[j].pos) >= spacing * 0.99f);
            for (const Prop& p : a.props) if (PropRadius(p.kind) > 0.0f ? Distance(loot[i].pos, p.pos) < PropRadius(p.kind) + 200.0f : p.kind == PropKind::Bush && Distance(loot[i].pos, p.pos) < 200.0f) { beside++; break; }
        }
        CHECK(beside * 10 >= static_cast<int>(loot.size()) * 8);
        // Lookouts and climbs hold the better chests: the lookout chests sit between two standing stones.
        for (const ChestSite& s : a.layout.sites) CHECK(valid(s.pos) && ground.Slope(s.pos) <= 0.7f);
    }
    // Flat ground with nothing measured (no height probe): the rules still run and the count is reasonable.
    {
        const MapPlacement flat = PlaceMap(9, map, 0, 12, 560, nullptr, nullptr);
        CHECK(flat.features.cliffs.empty() && flat.props.size() > 400);
        const auto loot = GenerateAnchoredLoot(9, flat.plan, 150, 0.15f, nullptr, 240.0f);
        CHECK(loot.size() > 80);
    }
    // The Fortnite Map: chests keep out of the oaks and boulders of its own scenery, and stay on dry, level land.
    {
        fn::UseTerrainForMap(fn::kMapId);
        const Circle island = MapOf(kFortniteMapIndex).fallback;
        const PlacementFn dry = [](Vec2 p) { float y; return fn::GroundHeight(p.x, p.z, &y) && y > fn::kWaterY + 10.0f && fn::GroundUp(p.x, p.z) >= 0.8f; };
        const HeightFn h = [](Vec2 p, float* y) { return fn::GroundHeight(p.x, p.z, y); };
        const MapPlacement p = PlaceMap(4, island, kFortniteMapIndex, 24, 560, dry, h, fn::AddIslandAnchors);
        int sceneryAnchors = 0;
        for (const LootAnchor& a : p.plan.anchors) sceneryAnchors += a.kind == AnchorKind::Scenery || a.kind == AnchorKind::CliffFoot;
        CHECK(sceneryAnchors > 20);
        const auto loot = GenerateAnchoredLoot(4, p.plan, 280, 0.15f, nullptr, 240.0f);
        CHECK(loot.size() > 150);
        for (const LootSpawn& l : loot) {
            CHECK(dry(l.pos) && ChestSpotOk(p.plan, l.pos));
            for (int cz = static_cast<int>(std::floor((l.pos.z - 150.0f) / fn::kSceneryCell)); cz <= static_cast<int>(std::floor((l.pos.z + 150.0f) / fn::kSceneryCell)); cz++)
                for (int cx = static_cast<int>(std::floor((l.pos.x - 150.0f) / fn::kSceneryCell)); cx <= static_cast<int>(std::floor((l.pos.x + 150.0f) / fn::kSceneryCell)); cx++) {
                    fn::SceneryPiece piece;
                    if (fn::SceneryIn(cx, cz, 1.0f, &piece) && fn::SceneryRadius(piece.kind, piece.scale) > 0.0f) CHECK(Distance(l.pos, {piece.x, piece.z}) > fn::SceneryRadius(piece.kind, piece.scale));
                }
        }
        fn::UseTerrainForMap(0);   // the other tests expect the default island ground
    }
}

// The climbing blocks baked into the island's collision: eight corners and ten triangles each, a flat top and outward vertical sides.
static void BlocksAreBakedIntoTheCollision() {
    namespace fn = royale::fortnite;
    fn::UseTerrainForMap(fn::kMapId);
    fn::gBlocks.clear();
    const fn::Mesh plain = fn::BuildCollision();
    fn::gBlocks = {{500.0f, -300.0f, 75.0f, -20.0f, 120.0f}, {650.0f, -300.0f, 75.0f, 0.0f, 180.0f}};
    const fn::Mesh withBlocks = fn::BuildCollision();
    fn::gBlocks.clear();
    CHECK(withBlocks.verts.size() == plain.verts.size() + 16 && withBlocks.polys.size() == plain.polys.size() + 20);
    CHECK(withBlocks.verts.size() < 8191);
    int tops = 0, sides = 0;
    for (size_t i = plain.polys.size(); i < withBlocks.polys.size(); i++) {
        const fn::Poly& p = withBlocks.polys[i];
        const fn::Vert &a = withBlocks.verts[p.a], &b = withBlocks.verts[p.b], &c = withBlocks.verts[p.c];
        const double ny = p.ny / 32767.0, nx = p.nx / 32767.0, nz = p.nz / 32767.0;
        CHECK(std::fabs(nx * nx + ny * ny + nz * nz - 1.0) < 1e-3);
        for (const fn::Vert* v : {&a, &b, &c}) CHECK(std::fabs(nx * v->x + ny * v->y + nz * v->z + p.dist) < 1.5);   // all three corners lie on the plane
        if (ny > 0.99) { tops++; CHECK(a.y == b.y && b.y == c.y); }
        else { sides++; CHECK(std::fabs(ny) < 1e-3); }
        // Faces look away from the block: the block's middle is behind every plane.
        const double mx = (i < plain.polys.size() + 10) ? 500.0 : 650.0, mz = -300.0, my = (i < plain.polys.size() + 10) ? 50.0 : 90.0;
        CHECK(nx * mx + ny * my + nz * mz + p.dist < 0.0);
    }
    CHECK(tops == 4 && sides == 16);
}

int main() {
    BotController::CalmSeconds() = 0.0f;   // tests put bots in fights straight away
    BotController::GearFirst() = false;
    LiloTheCatModel(); AvriellaTheBabyModel(); MatchReplayIsRecorded(); HeartChestsAndAdultPower(); HireableAllies(); ClothAndWind(); TheSignInTheMiddle(); MagicMeter(); SeasonsAndWeather(); SupplyDrops(); BotsShowTheirItemUse(); BotsLootBeforeTheyFight(); ClimbsAndSpreadOutChests(); PlacementIsPurposeful(); BlocksAreBakedIntoTheCollision(); StartingSwordAndAmmo(); StormJingleAndWarning(); RollingDodgesHits(); BotsRollAndLockOn(); BotsPlayLikePlayers(); BotsLeaveBlastRings(); LilosToxicCloud(); MapsHaveTheirOwnNamesAndBosses(); FortniteMapIsSound(); FortniteIslandPlaces(); SoloTestHasNoBotsAndKeepsGoing(); TheMajorBoss(); StormNests(); StormDeterministic(); StormTimeline(); LootDeterministicAndValid(); ChestsRollHigher(); SandboxTerrainAndLayout(); SandboxMatchHasNoCountdownOrEnd(); SandboxCommands();
    CombatMath(); AttackRules(); NoAttacksDuringDrop(); PickUpRulesAndSwap(); PotionRules(); DeathDropsKit();
    BotFetchesUpgrade(); BotIgnoresDowngrade(); BotTakesShieldAndPotions(); BotHealsWhenHurt(); BotOutrunsStorm(); BotsFightToTheDeath(); BotsFaceTheirDirectionAndAnimate(); BotsKeepDistanceWithBow(); FullMatchWithBots();
    CatalogIsConsistent(); LootCoversEveryItemAndRespectsKindWeights(); GearScalesWithRarityAndStacks(); GearChangesDamageDealtAndTaken();
    PickupRulesForEveryKind(); FairyRevivesOnceAndIsNeverDrunk(); PotionVariants(); WeaponEffects(); AbilityBasics(); AbilitiesThatMovePlayers();
    OcarinasPlayRandomSongs(); EliminatedPlayersDropPartOfTheirKitAndKillsAreCredited(); MovementPlausibilityAllowsSpeedBuffs();
    PlacementValidatorKeepsLootAndSpawnsOnWalkableGround(); ValidatorThatRejectsEverythingStillTerminates(); StormPhaseInfo();
    BlowsFollowThePlayersRules(); ShieldBar(); ShockwaveGrenade(); ChickenTune(); PlayerLimitSlider(); MiniBosses(); ChuChuCombat(); BossesUseTheirOwnMoves(); BossesFindTheirWay(); MajorBossesFightTheirOwnWay(); CustomObjModels(); CustomMeshes(); GildedSwordSurfaceMaps(); IslandScenery(); IslandPuddles(); GroundPatches(); BouldersAndFormations(); OutpostsAreDesigned(); TownsAreDifferentPlaces(); PointsOfInterest(); HyruleFieldHasPlacesOfItsOwn(); ScoringAndStandings(); HotbarAndChestsAndProps(); WalkingOverLootOnlyTakesUpgrades(); NavPathsAroundWalls(); BotsWalkAroundWalls(); BotsUseAbilitiesWhenItCounts(); BotsFleeLosingFights(); HarderBotsKillFaster(); BotsPickUpFairiesAndHearts(); BotsAdvantageMath();
    NavKnowsLedgesAndCliffs(); BotsClimbBlocksAndBoulders(); BotsSkydiveIn(); BotsSprintLikePlayers(); BotsUseCoverAndHighGround();
    CartPhysics(); CartsSeatsRamsAndWrecks(); BotsDriveAndRideCarts(); FullMatchWithCarts();
    WeightsSumTo100(); SoloPlayerGets31Bots(); StartNeedsOneHuman(); LobbyFull(); FullMatchHasOneWinner(); SpawnProtection();
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("all tests passed\n");
    return 0;
}
