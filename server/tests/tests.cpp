#include "../match.h"
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

int main() {
    StormNests(); StormDeterministic(); StormTimeline(); LootDeterministicAndValid(); ChestsRollHigher();
    WeightsSumTo100(); SoloPlayerGets31Bots(); StartNeedsOneHuman(); LobbyFull(); FullMatchHasOneWinner(); SpawnProtection();
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("all tests passed\n");
    return 0;
}
