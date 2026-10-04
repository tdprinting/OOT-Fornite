#include "../match.h"
#include "../sim.h"
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
    for (int i = 0; i < kTickHz * 5 + 1; i++) m.Tick(1.0f / kTickHz);
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
    CHECK(KindOf(ItemId::Hookshot) == ItemKind::Utility && KindOf(ItemId::FairyBow) == ItemKind::Weapon);
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
    CHECK(m.PickUp(1000, k));                      // swap
    CHECK(m.Loot().size() == n + 2);               // the loot we added, plus the Master Sword we dropped
    bool master = false;
    for (auto& e : m.Loot()) if (!e.taken && e.spawn.item == ItemId::MasterSword) master = true;
    CHECK(master);                                 // old weapon is on the ground again
    CHECK(!m.PickUp(1000, m.AddLoot({{0, 0}, ItemId::Hookshot, Rarity::Rare, false}))); // utility not supported yet
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
    sim.match.AddLoot({{100, 0}, ItemId::KokiriSword, Rarity::Common, false});
    sim.match.AddLoot({{120, 0}, ItemId::Hookshot, Rarity::Epic, false}); // utility: not interesting
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

int main() {
    StormNests(); StormDeterministic(); StormTimeline(); LootDeterministicAndValid(); ChestsRollHigher();
    CombatMath(); AttackRules(); NoAttacksDuringDrop(); PickUpRulesAndSwap(); PotionRules(); DeathDropsKit();
    BotFetchesUpgrade(); BotIgnoresDowngrade(); BotTakesShieldAndPotions(); BotHealsWhenHurt(); BotOutrunsStorm(); BotsFightToTheDeath(); BotsFaceTheirDirectionAndAnimate(); BotsKeepDistanceWithBow(); FullMatchWithBots();
    WeightsSumTo100(); SoloPlayerGets31Bots(); StartNeedsOneHuman(); LobbyFull(); FullMatchHasOneWinner(); SpawnProtection();
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("all tests passed\n");
    return 0;
}
