// Deterministic helper simulation and wire validation. CHECK stays active in Release builds.
#include "match.h"
#include "protocol.h"
#include "game_client.h"
#include "game_server.h"
#include "loopback.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <set>

using namespace royale;
using namespace royale::net;

#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)
constexpr float dt = 0.05f;
static Circle Arena() { return {{0, 0}, 2000}; }

struct Fight {
    Match m{77, Arena(), 0};
    Fight() {
        CHECK(m.SetSandbox(true));
        CHECK(m.AddHuman(1));
        CHECK(m.Start());
        m.SandboxGod(false);
        CHECK(m.SandboxBoss(BossKind::Stone, {0, 0}));
        CHECK(m.Helpers().size() == kHelpersPerBoss);
        for (const auto& helper : m.Helpers()) {
            auto& h = const_cast<BossHelper&>(helper);
            h.alive = false; h.mode = BokoMode::Idle;
        }
        H().alive = true; H().home = H().pos = {0, 0}; H().readyAt = 0;
        B().mode = DragonMode::Stunned; B().modeUntil = 100000;
        P().pos = {1000, 0}; P().invulnUntil = 0;
        m.DrainEvents();
    }
    BossHelper& H() { return const_cast<BossHelper&>(m.Helpers().front()); }
    MiniBoss& B() { return const_cast<MiniBoss&>(m.Bosses().front()); }
    PlayerState& P() { return *m.Find(1); }
    void Run(float seconds) { for (int i = 0; i < static_cast<int>(seconds / dt + 0.5f); ++i) m.Tick(dt); }
    bool Until(BokoMode mode, float seconds = 4) {
        for (int i = 0; i < static_cast<int>(seconds / dt); ++i) {
            m.Tick(dt);
            if (H().mode == mode) return true;
        }
        return false;
    }
    void Commit(BokoMode mode, Vec2 target) {
        P().pos = target; H().target = P().id;
        CHECK(Until(mode));
        H().readyAt = 100000;
    }
};

static void SpawnAndIdentity() {
    Match a(41, Arena(), 0), b(41, Arena(), 0);
    for (auto* m : {&a, &b}) {
        CHECK(m->SetSoloTest(true)); CHECK(m->AddHuman(1));
        m->SetBossCount(2); m->SetBossSpots({{-600, 0}, {600, 0}}); CHECK(m->Start());
        CHECK(m->Helpers().size() == 2 * kHelpersPerBoss);
        CHECK(m->Alive() == 1 && m->Bosses().size() == 2);
    }
    std::set<uint32_t> ids;
    for (size_t i = 0; i < a.Helpers().size(); ++i) {
        const auto& h = a.Helpers()[i];
        CHECK(ids.insert(h.id).second && IsHelperId(h.id) && !IsBossId(h.id));
        CHECK(Distance(h.home, b.Helpers()[i].home) == 0);
        const auto* boss = a.FindBoss(h.boss); CHECK(boss);
        CHECK(Distance(h.home, boss->home) >= 170 && Distance(h.home, boss->home) <= 340);
        for (size_t j = 0; j < i; ++j) CHECK(Distance(h.home, a.Helpers()[j].home) >= 65);
    }
    // Placement restrictions apply even when no navigation grid has been installed.
    a.SetPlacementValidator([](Vec2) { return false; });
    a.SpawnBosses(); CHECK(a.Helpers().empty());
    Fight f;
    CHECK(f.m.SandboxBoss(BossKind::DragonForest, {500, 500}));
    CHECK(f.m.Helpers().size() == kHelpersPerBoss);
    f.B().alive = false;
    CHECK(f.m.SandboxBoss(BossKind::Stone, {300, 0}));
    CHECK(f.m.Helpers().size() == kHelpersPerBoss); // Reusing the boss slot replaces its old helpers.
    for (const auto& h : f.m.Helpers()) CHECK(h.alive && h.health == kBokoHealth);
    f.m.SandboxClearBosses(); CHECK(f.m.Helpers().empty());
}

static void IdleDanceAndReturnHome() {
    Fight f;
    f.Run(dt); CHECK(f.H().mode == BokoMode::Dance);
    f.Run(1.9f); CHECK(f.H().mode == BokoMode::Idle);
    f.H().pos = {300, 0};
    f.Run(dt); CHECK(f.H().mode == BokoMode::Walk);
    f.Run(4); CHECK(Distance(f.H().pos, f.H().home) < 25);
    CHECK(f.H().target == kNoPlayer);
    CHECK(f.Until(BokoMode::Dance));
}

static void EveryAttackIsReachable() {
    for (int personality = 0; personality < kHelpersPerBoss; ++personality) {
        for (int variant = 0; variant < 3; ++variant) {
            Fight f;
            f.H().personality = f.H().moves = static_cast<uint8_t>(personality);
            const BokoMode expected = variant == 0 ? BokoMode::Swing : variant == 1 ? BokoMode::Jump : BokoMode::Throw;
            f.P().pos = {variant == 0 ? 50.0f : variant == 1 ? 150.0f : 450.0f, 0};
            CHECK(f.Until(expected));
            CHECK(f.H().moves > personality); // No player had to damage or kite it first.
        }
    }
}

static void SwingIsTelegraphedDirectionalAndDodged() {
    Fight f; f.Commit(BokoMode::Swing, {50, 0});
    const float health = f.P().health;
    f.Run(0.45f); CHECK(f.P().health == health);
    f.Run(0.1f); CHECK(f.P().health < health);
    const float after = f.P().health;
    f.Run(0.5f); CHECK(f.P().health == after);

    Fight dodge; dodge.Commit(BokoMode::Swing, {50, 0});
    dodge.P().rollUntil = 100;
    dodge.Run(1); CHECK(dodge.P().health == health);

    Fight behind; behind.Commit(BokoMode::Swing, {50, 0});
    behind.P().pos = {-50, 0}; behind.Run(1);
    CHECK(behind.P().health == health);
}

static void JumpLocksLandingAndStumblesOnMiss() {
    Fight hit; hit.Commit(BokoMode::Jump, {150, 0});
    const float health = hit.P().health;
    hit.Run(0.5f); CHECK(hit.H().y > 40 && hit.P().health == health);
    hit.Run(0.55f);
    CHECK(hit.P().health < health && hit.H().y == 0);
    CHECK(Distance(hit.H().pos, {150, 0}) < 0.1f);

    Fight miss; miss.Commit(BokoMode::Jump, {150, 0});
    miss.P().pos = {400, 0}; miss.Run(1.05f);
    CHECK(miss.H().mode == BokoMode::Recover && miss.H().y == 0);
    CHECK(miss.P().health == health && Distance(miss.H().pos, {150, 0}) < 0.1f);
}

static void RockHasFlightTimeAndLocksAim() {
    for (bool dodge : {false, true}) {
        Fight f; f.Commit(BokoMode::Throw, {400, 0});
        const float health = f.P().health;
        f.Run(0.65f); CHECK(!f.H().rockActive && f.P().health == health);
        f.Run(0.1f); CHECK(f.H().rockActive);
        CHECK(Distance(f.H().rockTo, {400, 0}) == 0);
        if (dodge) f.P().pos = {400, 100};
        f.Run(0.65f); CHECK(f.H().rockActive && f.P().health == health);
        f.Run(0.2f); CHECK(!f.H().rockActive);
        CHECK(dodge ? f.P().health == health : f.P().health < health);
        const float after = f.P().health;
        f.Run(1); CHECK(f.P().health == after);
    }
}

static void HitsInterruptWindupsAndRestartFlinches() {
    for (BokoMode mode : {BokoMode::Swing, BokoMode::Jump, BokoMode::Throw}) {
        Fight f;
        f.Commit(mode, {mode == BokoMode::Swing ? 50.0f : mode == BokoMode::Jump ? 150.0f : 400.0f, 0});
        f.P().pos = {10, 0};
        CHECK(f.m.Attack(1, f.H().id).hit);
        CHECK(f.H().mode == BokoMode::Hurt && f.H().alive);
        f.Run(0.15f);
        const uint8_t action = f.H().action;
        f.P().attackReadyAt = 0;
        CHECK(f.m.Attack(1, f.H().id).hit);
        CHECK(f.H().action == static_cast<uint8_t>(action + 1));
        CHECK(f.H().modeAt == f.m.Clock());
        f.H().readyAt = 100000;
        const float health = f.P().health;
        f.Run(1.8f);
        CHECK(f.P().health == health && !f.H().rockActive && f.H().y == 0);
    }
}

static void RewardsAreOnceAndNotEliminations() {
    Fight f;
    f.P().pos = {20, 0};
    f.H().health = 0.1f;
    const size_t loot = f.m.Loot().size();
    CHECK(f.m.Attack(1, f.H().id).killed);
    CHECK(f.m.Loot().size() == loot + 1 && f.m.Loot().back().spawn.item == ItemId::Rupees);
    CHECK(f.H().mode == BokoMode::Dead && !f.H().alive);
    CHECK(f.P().kills == 0 && f.P().bossKills == 0 && f.m.Alive() == 1);
    CHECK(std::fabs(f.P().damageDealt - 0.1f) < 0.0001f);
    CHECK(!f.m.Attack(1, f.H().id).ok && f.m.Loot().size() == loot + 1);
    for (const auto& e : f.m.DrainEvents()) CHECK(e.type != MatchEvent::Type::BossDown && e.type != MatchEvent::Type::Eliminated);
    f.Run(1.2f);
    CHECK(f.H().mode != BokoMode::Dead && !f.H().alive);
    CHECK(f.m.Loot().size() == loot + 1);
}

static void DeadBossCancelsCombatAndHelpersFlee() {
    for (BokoMode mode : {BokoMode::Swing, BokoMode::Jump, BokoMode::Throw, BokoMode::Hurt}) {
        Fight f;
        f.H().home = f.H().pos = {100, 0};
        f.H().mode = mode; f.H().target = 1; f.H().rockActive = true;
        f.H().rockTo = f.P().pos; f.H().rockAt = -10;
        f.B().alive = false;
        const float health = f.P().health;
        f.Run(0.1f);
        CHECK(f.H().mode == BokoMode::Flee && f.H().target == kNoPlayer && !f.H().rockActive);
        CHECK(f.H().pos.x > 100 && f.P().health == health);
        const float started = f.H().modeAt;
        f.P().pos = f.H().pos;
        CHECK(f.m.Attack(1, f.H().id).hit);
        CHECK(f.H().mode == BokoMode::Flee && f.H().modeAt == started && f.H().target == kNoPlayer);
        f.Run(8.1f);
        CHECK(!f.H().alive && f.m.Loot().empty() && f.P().kills == 0 && f.P().bossKills == 0);
    }
}

static void TargetSharingAndLeash() {
    Fight commanded;
    commanded.B().target = 1; commanded.P().pos = {600, 0};
    commanded.Run(dt);
    CHECK(commanded.H().target == 1 && commanded.H().mode == BokoMode::Alert);
    commanded.P().pos = {1000, 0}; commanded.Run(1);
    CHECK(commanded.H().target == kNoPlayer);
    Fight protectedPlayer;
    protectedPlayer.B().target = 1; protectedPlayer.P().pos = {50, 0}; protectedPlayer.P().invulnUntil = 100;
    protectedPlayer.Run(3);
    CHECK(protectedPlayer.H().target == kNoPlayer);
    Fight deadPlayer;
    deadPlayer.B().target = 1; deadPlayer.P().pos = {50, 0}; deadPlayer.P().alive = false;
    deadPlayer.Run(3);
    CHECK(deadPlayer.H().target == kNoPlayer);
}

static void WallsBlockSightAttacksAndMovement() {
    auto grid = std::make_shared<NavGrid>(Arena(), [](Vec2 p) { return !(p.x > 70 && p.x < 170 && std::fabs(p.z) < 160); });
    Fight unseen; unseen.m.SetNav(grid); unseen.P().pos = {240, 0}; unseen.B().target = 1;
    unseen.Run(3); CHECK(unseen.H().target == kNoPlayer);
    unseen.P().weapon = {ItemId::FairyBow, Rarity::Common}; unseen.P().ammo[static_cast<size_t>(AmmoKind::Arrows)] = 20;
    CHECK(!unseen.m.Attack(1, unseen.H().id).ok);

    Fight path; path.m.SetNav(grid); path.H().target = 1; path.P().pos = {300, 0}; path.H().readyAt = 100000;
    bool around = false;
    for (int i = 0; i < 160; ++i) {
        const Vec2 before = path.H().pos;
        path.m.Tick(dt);
        CHECK(grid->Walkable(path.H().pos) && grid->LineClear(before, path.H().pos));
        around |= std::fabs(path.H().pos.z) > 160;
    }
    if (!around || Distance(path.H().pos, path.P().pos) > kBokoReach)
        std::printf("Path ended at %.1f, %.1f; target %.1f, %.1f; waypoint %zu/%zu\n", path.H().pos.x, path.H().pos.z,
                    path.P().pos.x, path.P().pos.z, path.H().pathIdx, path.H().path.size());
    CHECK(around && Distance(path.H().pos, path.P().pos) <= kBokoReach);

    Fight jump; jump.Commit(BokoMode::Jump, {150, 0}); jump.m.SetNav(grid);
    jump.Run(1.05f);
    CHECK(grid->Walkable(jump.H().pos) && jump.H().pos.x < 150 && jump.P().health == kMaxHealth);

    Fight rock; rock.Commit(BokoMode::Throw, {400, 0}); rock.Run(0.75f);
    CHECK(rock.H().rockActive); rock.m.SetNav(grid); rock.Run(1);
    CHECK(rock.P().health == kMaxHealth);
}

static void SnapshotRoundTripAndMalformedHelpers() {
    Snapshot s, out;
    s.state = static_cast<uint8_t>(MatchState::InMatch); s.alive = 1;
    for (int i = 0; i < kMaxHelpers; ++i) {
        HelperNet h;
        h.index = static_cast<uint8_t>(i); h.boss = static_cast<uint8_t>(i / kHelpersPerBoss);
        h.mode = static_cast<uint8_t>(i % static_cast<int>(BokoMode::Count));
        h.personality = static_cast<uint8_t>(i % kHelpersPerBoss); h.action = 255;
        h.x = i * 20.5f; h.z = -i * 10.0f; h.age = 0.2f; h.y = 55;
        h.rock = i % 2; h.rockAge = 0.5f; h.fromX = -123; h.toZ = 456;
        s.helpers.push_back(h);
    }
    const auto bytes = Encode(s);
    CHECK(Decode(bytes, out) && out.helpers.size() == kMaxHelpers && out.alive == 1);
    for (size_t i = 0; i < s.helpers.size(); ++i) {
        const auto& a = s.helpers[i]; const auto& b = out.helpers[i];
        CHECK(a.Id() == b.Id() && a.boss == b.boss && a.mode == b.mode && a.action == b.action);
        CHECK(a.x == b.x && a.z == b.z && a.age == b.age && a.y == b.y && a.rock == b.rock);
        if (a.rock) CHECK(a.rockAge == b.rockAge && a.fromX == b.fromX && a.toZ == b.toZ);
    }
    for (size_t length = 0; length < bytes.size(); ++length) CHECK(!Decode(bytes.data(), length, out));
    auto trailing = bytes; trailing.push_back(0); CHECK(!Decode(trailing, out));
    s.helpers.push_back({}); CHECK(!Decode(Encode(s), out));
    s.helpers.resize(1);
    auto invalid = [&](HelperNet h) { s.helpers[0] = h; CHECK(!Decode(Encode(s), out)); };
    HelperNet h;
    h.index = kMaxHelpers; invalid(h); h = {};
    h.boss = kMaxBosses; invalid(h); h = {};
    h.kind = static_cast<uint8_t>(HelperKind::Count); invalid(h); h = {};
    h.mode = static_cast<uint8_t>(BokoMode::Count); invalid(h); h = {};
    h.personality = kHelpersPerBoss; invalid(h); h = {};
    h.x = std::numeric_limits<float>::quiet_NaN(); invalid(h); h = {};
    h.z = std::numeric_limits<float>::infinity(); invalid(h); h = {};
    h.age = -1; invalid(h); h = {};
    h.y = -1; invalid(h); h = {};
    h.rock = true; h.rockAge = 1; invalid(h);
    h.rockAge = 0; h.toX = std::numeric_limits<float>::infinity(); invalid(h);
    ByteWriter w; HelperNet{}.Write(w); w.buf.back() = 2;
    ByteReader r(w.buf.data(), w.buf.size()); CHECK(!h.Read(r));
}

static void HelpersReplicateToBothClientsAndDisappear() {
    LoopbackNetwork network(83);
    GameServer server(network.Server(), 83, Arena(), 0);
    CHECK(server.Sim().match.SetSandbox(true));
    GameClient a(network.NewClient(), "Link"), b(network.NewClient(), "Zelda");
    auto run = [&](float seconds) {
        for (int i = 0; i < static_cast<int>(seconds / dt + 0.5f); ++i) {
            network.Advance(dt); server.Update(dt); a.Update(dt); b.Update(dt);
        }
    };
    run(0.5f);
    CHECK(a.GetStatus() == GameClient::Status::Joined && b.GetStatus() == GameClient::Status::Joined);
    CHECK(server.StartMatch());
    Match& m = server.Sim().match;
    CHECK(m.SandboxBoss(BossKind::Stone, {0, 0}));
    auto& boss = const_cast<MiniBoss&>(m.Bosses().front());
    boss.mode = DragonMode::Stunned; boss.modeUntil = 100000;
    auto& helper = const_cast<BossHelper&>(m.Helpers().front());
    helper.pos = {20, 0}; helper.health = 0.1f;
    helper.mode = BokoMode::Throw; helper.modeAt = m.Clock(); helper.action = 37; helper.resolved = true;
    helper.rockActive = true; helper.rockAt = m.Clock(); helper.rockFrom = helper.pos; helper.rockTo = {400, 0};
    const uint16_t id = static_cast<uint16_t>(helper.id);
    run(0.2f);
    for (const auto* client : {&a, &b}) {
        CHECK(client->Helpers().size() == kHelpersPerBoss);
        const auto& n = client->Helpers().front();
        CHECK(n.Id() == id && n.action == 37 && n.mode == static_cast<uint8_t>(BokoMode::Throw));
        CHECK(n.rock && n.rockAge > 0 && n.toX == 400 && n.fromX == 20);
    }
    a.ReportAttack(id, true); run(0.2f);
    for (const auto* client : {&a, &b}) {
        CHECK(client->Helpers().front().Id() == id);
        CHECK(client->Helpers().front().mode == static_cast<uint8_t>(BokoMode::Dead));
        CHECK(client->Helpers().front().hp == 0 && !client->Helpers().front().rock);
    }
    CHECK(m.Alive() == 2 && m.Find(1)->kills == 0 && m.Find(1)->bossKills == 0);
    run(1.2f);
    for (const auto* client : {&a, &b}) {
        CHECK(client->Helpers().size() == kHelpersPerBoss - 1);
        for (const auto& n : client->Helpers()) CHECK(n.Id() != id);
    }
    boss.alive = false; run(0.2f);
    for (const auto* client : {&a, &b})
        for (const auto& n : client->Helpers()) CHECK(n.mode == static_cast<uint8_t>(BokoMode::Flee) && !n.rock);
    run(8.2f);
    CHECK(a.Helpers().empty() && b.Helpers().empty());
}

int main() {
    SpawnAndIdentity();
    IdleDanceAndReturnHome();
    EveryAttackIsReachable();
    SwingIsTelegraphedDirectionalAndDodged();
    JumpLocksLandingAndStumblesOnMiss();
    RockHasFlightTimeAndLocksAim();
    HitsInterruptWindupsAndRestartFlinches();
    RewardsAreOnceAndNotEliminations();
    DeadBossCancelsCombatAndHelpersFlee();
    TargetSharingAndLeash();
    WallsBlockSightAttacksAndMovement();
    SnapshotRoundTripAndMalformedHelpers();
    HelpersReplicateToBothClientsAndDisappear();
    std::puts("Bokoblin AI, combat, lifecycle, navigation, and protocol tests passed.");
}
