// Feature smokes (docs/TEST_TOOL_DESIGN.md, layer 1): one scripted match per recent feature, with the real GameServer, real GameClients
// and real bots on the in-memory LoopbackNetwork. Sim time only (kTickHz), never the wall clock, so a run is the same every time for a seed.
// Smokes assert on what the clients see (ClientEvents, client queries); Match internals are touched only to force setup.
// Run: royale_feature_smoke [--list] [--seed N] [name...]   (no names = all; each smoke runs on seeds 1, 2, 3 unless --seed)
#include "game_client.h"
#include "game_server.h"
#include "loopback.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace royale;
using namespace royale::net;

static int failures = 0, known = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)
// A reported bug that is not fixed yet: printed every run, does not fail it. Remove the KNOWN when the bug is fixed (it then becomes a CHECK).
#define KNOWN(c, note) do { if (!(c)) { std::printf("KNOWN %s:%d  %s  -- %s\n", __FILE__, __LINE__, #c, note); known++; } } while (0)

constexpr float kDt = 1.0f / kTickHz;
constexpr uint64_t kHostToken = 0x5E1F7E57ull;
using Type = ClientEvent::Type;

// One host server, N scripted humans (clients[0] is the host's own client), the rest of the slots bots.
struct Rig {
    LoopbackNetwork network;
    GameServer server;
    std::vector<std::unique_ptr<GameClient>> clients;
    std::vector<std::vector<ClientEvent>> log;   // every event each client drained, in order
    std::vector<std::function<void(Rig&)>> scripts;
    float now = 0;   // sim seconds

    // `beforeJoin` runs on each client before it connects (SetSkin, SetTunic).
    Rig(uint64_t seed, int mapId, int humans, TeamMode mode = TeamMode::Solo, std::function<void(int, GameClient&)> beforeJoin = nullptr)
        : network(seed), server(network.Server(), seed, kHyruleFieldMap) {
        server.SetHostToken(kHostToken);
        server.SetBossCount(5);
        if (mapId != 0) server.SelectMap(mapId);
        if (mode != TeamMode::Solo) server.SetTeamMode(mode);
        for (int i = 0; i < humans; i++) {
            clients.push_back(std::make_unique<GameClient>(network.NewClient(), "Smoke" + std::to_string(i), i == 0 ? kHostToken : 0));
            if (beforeJoin) beforeJoin(i, *clients.back());
        }
        log.resize(clients.size());
        CHECK(Until([&] { for (auto& c : clients) if (c->GetStatus() != GameClient::Status::Joined) return false; return true; }, 10));
    }
    GameClient& C(int i) { return *clients[static_cast<size_t>(i)]; }
    Match& M() { return server.Sim().match; }
    // A point `dx, dz` from the middle of the map (positions forced in setup must be inside it, or the storm hurts).
    Vec2 At(float dx, float dz) const { const Circle c = MapOf(server.MapId()).fallback; return {c.center.x + dx, c.center.z + dz}; }
    void Tick() {
        network.Advance(kDt);
        server.Update(kDt);
        for (size_t i = 0; i < clients.size(); i++) {
            clients[i]->Update(kDt);
            for (auto& e : clients[i]->DrainEvents()) log[i].push_back(e);
        }
        for (auto& s : scripts) s(*this);
        now += kDt;
    }
    void Run(float seconds) { for (int i = 0; i < static_cast<int>(seconds * kTickHz + 0.5f); i++) Tick(); }
    template <class F> bool Until(F cond, float maxSec) {
        for (float t = 0; t < maxSec; t += kDt) { if (cond()) return true; Tick(); }
        return cond();
    }
    void Script(std::function<void(Rig&)> perTick) { scripts.push_back(std::move(perTick)); }
    int Count(int client, Type t) const { int n = 0; for (auto& e : log[static_cast<size_t>(client)]) n += e.type == t; return n; }
    // Start and run to InMatch with everybody put on the ground (for smokes that are not about the drop).
    bool StartAndLand() {
        if (!server.StartMatch()) return false;
        if (!Until([&] { return M().State() == MatchState::Drop; }, 20)) return false;
        M().LandEveryone();
        return Until([&] { return M().State() == MatchState::InMatch && C(0).State() == MatchState::InMatch; }, 120);
    }
};

// A human who rides the bus, jumps once the doors are over land, falls, lands and then stands where the server has them.
static void Dropper(Rig& rig, int i, float jumpAfterOpen = 0.0f) {
    auto openedAt = std::make_shared<float>(-1.0f);
    rig.Script([i, jumpAfterOpen, openedAt](Rig& r) {
        GameClient& c = r.C(i);
        const net::PlayerNet* me = c.Self();
        if (!me) return;
        if (c.State() == MatchState::Drop && c.Aboard()) {
            const bool open = BusOverLand(c.Bus(), c.BusTime());
            if (open && *openedAt < 0) *openedAt = r.now;
            const bool go = open && r.now - *openedAt >= jumpAfterOpen;
            c.SendInput(me->x, me->y, me->z, 0, static_cast<uint8_t>(go ? Anim::Freefall : Anim::BusRide));
        } else if (c.State() == MatchState::Drop || c.State() == MatchState::InMatch) {
            c.SendInput(me->x, 0, me->z, 0, static_cast<uint8_t>(Anim::Idle));   // down: the first non-air anim is the landing
        }
    });
}

static bool Saw(const std::vector<ClientEvent>& log, Type t, uint16_t id) { for (auto& e : log) if (e.type == t && e.id == id) return true; return false; }

// ---- the smokes --------------------------------------------------------------------------------------------------------------------------

// Host + 1 guest + 30 bots on Moonfall: the whole flow, ending with one winner.
static void Smoke_match_flow(uint64_t seed) {
    Rig rig(seed, kMoonfallMapIndex, 2);
    Dropper(rig, 0); Dropper(rig, 1);
    int lastAlive = 1000;
    bool monotone = true;
    const Circle area = MapOf(kMoonfallMapIndex).fallback;
    for (auto& l : rig.C(1).Loot())   // KNOWN: the chest on Death Mountain's summit (4700,-5000) is 6862 from the centre of a 6500 circle
        KNOWN(Distance({l.x, l.z}, area.center) <= area.radius + 1.0f, "Moonfall chest site outside the map circle starts in the storm");
    rig.Script([&](Rig& r) { if (r.M().State() == MatchState::InMatch) { monotone &= r.M().Alive() <= lastAlive; lastAlive = r.M().Alive(); } });
    CHECK(rig.server.StartMatch());
    CHECK(rig.Until([&] { return rig.C(0).State() == MatchState::Ending && !rig.C(0).Results().empty(); }, 900));
    rig.Run(1.0f);
    for (size_t i = 0; i < rig.clients.size(); i++) {
        std::vector<MatchState> seen;
        for (auto& e : rig.log[i]) if (e.type == Type::StateChanged) seen.push_back(e.state);
        CHECK(seen == (std::vector<MatchState>{MatchState::Countdown, MatchState::Drop, MatchState::InMatch, MatchState::Ending}));
    }
    int winners = 0;
    for (auto& r : rig.C(0).Results()) winners += r.placement == 1;
    CHECK(winners == 1 && rig.C(0).Winner() != kNoPlayer16 && rig.C(1).Winner() == rig.C(0).Winner());
    CHECK(monotone);
    CHECK(rig.now < 600);   // sim seconds (seeds 1-3 measured 314-441)
    CHECK(rig.C(0).DesyncCount() == 0 && rig.C(1).DesyncCount() == 0);
    CHECK(rig.server.GetStats().badPackets == 0);
}

// The Moonfall Bus: line once per client, horn heard, guest jumps 1 s after the doors open, host never jumps and is pushed off as the
// bus leaves the island (BusCloseSec; the design said BusCrossSec, the rule in Match::TickBus is the island's far shore).
static void Smoke_bus(uint64_t seed) {
    Rig rig(seed, kMoonfallMapIndex, 2);
    Dropper(rig, 1, 1.0f);
    bool honked = false, aboardAtDrop = false, checkedDrop = false, hurtAboard = false;
    rig.Script([&](Rig& r) {
        GameClient& g = r.C(1);
        if (!honked && g.Aboard() && g.Self() && r.M().State() == MatchState::Drop) {   // the Dropper sends BusRide this tick; the horn replaces it
            g.SendInput(g.Self()->x, g.Self()->y, g.Self()->z, 0, static_cast<uint8_t>(Anim::BusHorn));
            honked = true;
        }
        if (!checkedDrop && r.C(1).State() == MatchState::Drop) { checkedDrop = true; aboardAtDrop = r.C(1).Aboard() && r.C(0).Aboard(); }
        PlayerState* host = r.M().Find(r.C(0).PlayerId());
        if (host && host->aboard && r.M().State() == MatchState::Drop) {   // nothing hurts you on the bus
            const float before = host->health;
            r.M().Damage(host->id, 1.0f, kNoPlayer);
            hurtAboard |= host->health != before;
        }
    });
    CHECK(rig.server.StartMatch());
    CHECK(rig.Until([&] { return rig.M().State() == MatchState::Drop; }, 20));
    const BusPath line = rig.M().Bus();
    CHECK(line.valid);
    // The guest jumped (1 s after the doors opened) and is off; the host never jumped and is pushed off at the far shore.
    CHECK(rig.Until([&] { return !rig.C(1).Aboard() && rig.M().State() == MatchState::Drop; }, 120));
    CHECK(rig.M().BusTime() < BusCloseSec(line));   // the guest's own jump, not the push as the bus leaves the island
    CHECK(rig.Until([&] { const PlayerState* h = rig.M().Find(rig.C(0).PlayerId()); return h && !h->aboard; }, BusCrossSec(line) + 5));
    CHECK(rig.M().State() == MatchState::Drop && std::fabs(rig.M().BusTime() - BusCloseSec(line)) <= 2 * kDt);   // pushed off then, not before
    rig.Run(1.0f);
    CHECK(aboardAtDrop);
    CHECK(!hurtAboard);
    for (size_t i = 0; i < rig.clients.size(); i++) CHECK(rig.Count(static_cast<int>(i), Type::BusPath) == 1);
    CHECK(Saw(rig.log[0], Type::BusHorn, rig.C(1).PlayerId()));
    CHECK(rig.server.GetStats().badPackets == 0);
}

// Duos: no teammate damage, knocked not eliminated, a 3 s revive, pings to the team only, pads announced.
static void Smoke_teams(uint64_t seed) {
    Rig rig(seed, 0, 3, TeamMode::Duos);   // host + A on one team; the stranger is with a bot
    GameClient& a = rig.C(0);
    GameClient& b = rig.C(1);
    GameClient& stranger = rig.C(2);
    rig.server.SetBossCount(0);   // a quiet field: a hit on the reviver cancels a revive (by design)
    CHECK(rig.StartAndLand());
    rig.Run(1.0f);
    const uint16_t aId = a.PlayerId(), bId = b.PlayerId();
    CHECK(a.Teammate(bId) && b.Teammate(aId) && !a.Teammate(stranger.PlayerId()));
    PlayerState* pa = rig.M().Find(aId);
    PlayerState* pb = rig.M().Find(bId);
    PlayerState* ps = rig.M().Find(stranger.PlayerId());
    if (!pa || !pb || !ps) { CHECK(!"players found"); return; }
    for (auto& p : rig.M().Players()) if (p.isBot) p.alive = false;   // bots out of the way (the stranger's team is still standing)
    pa->pos = rig.At(0, 0); pb->pos = rig.At(40, 0); ps->pos = rig.At(-1500.0f, -1500.0f);
    pa->invulnUntil = pb->invulnUntil = 0;
    pa->attackReadyAt = 0;
    rig.Run(0.5f);
    // A hits its teammate: nothing.
    const float bBefore = pb->health;
    a.ReportAttack(bId, true);
    rig.Run(0.5f);
    CHECK(pb->health == bBefore);
    for (auto& e : rig.log[1]) CHECK(!(e.type == Type::Damaged && e.id == bId && e.amount > 0));
    // B to zero: knocked, not out.
    rig.M().Damage(bId, 50.0f, ps->id);
    rig.Run(0.5f);
    CHECK(pb->alive && pb->knocked);
    CHECK(Saw(rig.log[0], Type::Knocked, bId) && !Saw(rig.log[0], Type::Eliminated, bId));
    for (int i = 0; i < static_cast<int>(3.6f * kTickHz); i++) { if (i % 2 == 0) a.HoldRevive(bId, true); rig.Tick(); }
    a.HoldRevive(bId, false);
    rig.Run(0.5f);
    CHECK(!pb->knocked && Saw(rig.log[1], Type::TeamRevived, bId));
    // A ping reaches the team, not the stranger.
    { const Vec2 p = rig.At(100.0f, 100.0f); a.SendPing(PingKind::Place, p.x, p.z); }
    rig.Run(0.5f);
    CHECK(Saw(rig.log[0], Type::Pinged, aId) && Saw(rig.log[1], Type::Pinged, aId) && !Saw(rig.log[2], Type::Pinged, aId));
    CHECK(rig.Count(0, Type::PadsChanged) >= 1 && a.LaunchPads().size() == rig.M().Pads().size() && !rig.M().Pads().empty());
    CHECK(rig.server.GetStats().badPackets == 0);
}

// Player skins (protocol 44): a valid skin travels to everyone; an invalid one becomes Link.
static void Smoke_skins(uint64_t seed) {
    Rig rig(seed, 0, 3, TeamMode::Solo, [](int i, GameClient& c) { if (i == 1) c.SetSkin(1); if (i == 2) c.SetSkin(200); });
    rig.Run(0.5f);
    const uint16_t hd = rig.C(1).PlayerId(), bad = rig.C(2).PlayerId();
    for (auto& c : rig.clients) {
        CHECK(c->Roster().count(hd) && c->Roster().at(hd).skin == 1);
        CHECK(c->Roster().count(bad) && c->Roster().at(bad).skin == 0);
        for (auto& [id, r] : c->Roster()) CHECK(ValidSkinModel(r.skin));
    }
    CHECK(Saw(rig.log[0], Type::PlayerJoined, hd) && Saw(rig.log[0], Type::PlayerJoined, bad));
    CHECK(rig.server.GetStats().badPackets == 0);
}

// Peter then Master Chief hired over the wire; Chief shoots a bot.
static void Smoke_allies_chief_peter(uint64_t seed) {
    Rig rig(seed, 0, 1);
    GameClient& me = rig.C(0);
    CHECK(rig.StartAndLand());
    rig.Run(1.0f);
    PlayerState* self = rig.M().Find(me.PlayerId());
    if (!self || rig.M().Allies().size() < 7) { CHECK(!"self and seven allies"); return; }
    self->rupees = 999; self->dirty = true;
    CHECK(rig.Until([&] { return me.Inventory().rupees == 999; }, 5));
    std::vector<int> hired;
    for (int index : {static_cast<int>(AllyKind::Peter), static_cast<int>(AllyKind::Chief)}) {
        self->pos = rig.M().Allies()[static_cast<size_t>(index)].pos;
        me.HireAlly(index);
        CHECK(rig.Until([&] { for (auto& a : me.Allies()) if (a.index == index && a.owner == me.PlayerId()) return true; return false; }, 5));
    }
    for (auto& e : rig.log[0]) if (e.type == Type::AllyChanged && e.id == me.PlayerId()) hired.push_back(static_cast<int>(e.index));
    CHECK(hired == (std::vector<int>{static_cast<int>(AllyKind::Peter), static_cast<int>(AllyKind::Chief)}));
    // Put a bot in front of the Chief and wait for a shot.
    const AllyState* chief = nullptr;
    for (auto& a : rig.M().Allies()) if (a.kind == AllyKind::Chief) chief = &a;
    uint32_t botId = kNoPlayer;
    for (auto& p : rig.M().Players()) if (p.isBot && p.alive) { botId = p.id; break; }
    bool shot = false;
    for (float t = 0; t < 30 && !shot && chief && botId != kNoPlayer; t += kDt) {
        if (PlayerState* bot = rig.M().Find(botId)) { bot->pos = {chief->pos.x + 300.0f, chief->pos.z}; bot->invulnUntil = 0; }
        rig.Tick();
        for (auto& e : rig.log[0]) shot |= e.type == Type::AllyAction && e.index == static_cast<size_t>(AllyKind::Chief);
    }
    CHECK(shot);
    CHECK(rig.server.GetStats().badPackets == 0);
}

// Skyloft Isles (map 13): selected by the host, loot on the map, bots alive and fighting.
static void Smoke_map_skyloft(uint64_t seed) {
    Rig rig(seed, 0, 1);
    rig.C(0).SelectMap(kSkyloftMapIndex);
    CHECK(rig.Until([&] { return rig.C(0).MapId() == kSkyloftMapIndex; }, 5));
    CHECK(rig.Count(0, Type::MapChanged) >= 1);
    const Circle area = MapOf(kSkyloftMapIndex).fallback;
    CHECK(!rig.C(0).Loot().empty());
    // KNOWN: on Skyloft some authored chest sites (the Lumpy Pumpkin islet, Beedle's Perch) lie outside the map circle {0,0} r 6500, so they
    // start inside the storm. Fix is a map decision (grow the circle or drop those sites); see docs/TEST_TOOL_DESIGN.md "Findings".
    int outside = 0;
    for (auto& l : rig.C(0).Loot()) outside += Distance({l.x, l.z}, area.center) > area.radius + 1.0f;
    KNOWN(outside == 0, "Skyloft chest sites outside the map circle start in the storm");
    CHECK(rig.StartAndLand());
    CHECK(rig.M().Alive() == kMaxPlayers);
    int start = 0;
    for (auto& p : rig.M().Players()) start += p.isBot && p.alive;
    CHECK(start == kMaxPlayers - 1);
    rig.Run(240.0f);
    CHECK(kMaxPlayers - rig.M().Alive() >= 10);
    CHECK(rig.server.GetStats().badPackets == 0);
}

static bool IsShockwave(const ClientEvent& e, uint16_t by) {
    return e.type == Type::Strike && e.id == by && e.item == static_cast<uint8_t>(StrikeStyle::Shockwave);
}

// Shockwave Grenade: one marked Strike; after the fuse a bot next to the blast is thrown, one at 3x the radius is not; nobody is hurt.
static void Smoke_shockwave(uint64_t seed) {
    Rig rig(seed, 0, 2);
    GameClient& g = rig.C(1);
    rig.server.SetBossCount(0);   // only our grenade's strike
    CHECK(rig.StartAndLand());
    rig.Run(0.5f);
    PlayerState* me = rig.M().Find(g.PlayerId());
    PlayerState *nearBot = nullptr, *farBot = nullptr;
    for (auto& p : rig.M().Players()) if (p.isBot && p.alive) { if (!nearBot) nearBot = &p; else if (!farBot) { farBot = &p; break; } }
    if (!me || !nearBot || !farBot) { CHECK(!"players found"); return; }
    for (auto& p : rig.M().Players()) if (p.isBot && &p != nearBot && &p != farBot) p.pos = rig.At(-1500.0f, -1500.0f);
    me->pos = rig.At(0, 0); me->rot = 0; me->weapon = {ItemId::ShockwaveGrenade, Rarity::Rare}; me->attackReadyAt = 0; me->dirty = true;
    const float radius = WeaponOf(ItemId::ShockwaveGrenade).splashRadius;
    const Vec2 at = rig.At(0, kShockwaveSelfThrow);
    rig.Run(0.2f);
    const size_t mark = rig.log[1].size();
    g.ReportAttack(kNoPlayer16, false);
    // Hold the bots in place each tick (they walk); a shove is a jump of many units in one tick.
    const float nearHealth = nearBot->health, farHealth = farBot->health;
    float throwAt = -1, shovedAt = -1, farMoved = 0;
    for (int i = 0; i < static_cast<int>((kShockwaveFuse + 1.0f) * kTickHz); i++) {
        const Vec2 n0 = {at.x + 1.0f, at.z}, f0 = {at.x + radius * 3.0f, at.z};
        if (shovedAt < 0) nearBot->pos = n0;
        farBot->pos = f0;
        rig.Tick();
        if (throwAt < 0) for (size_t k = mark; k < rig.log[1].size(); k++) if (IsShockwave(rig.log[1][k], g.PlayerId())) throwAt = rig.now;
        if (shovedAt < 0 && Distance(nearBot->pos, n0) > kShockwaveBotShove * 0.3f) shovedAt = rig.now;
        farMoved = (std::max)(farMoved, Distance(farBot->pos, f0));
    }
    int strikes = 0;
    for (size_t k = mark; k < rig.log[1].size(); k++) if (IsShockwave(rig.log[1][k], g.PlayerId())) {
        strikes++;
        CHECK(std::fabs(rig.log[1][k].health - kShockwaveFuse) < 1e-3f && rig.log[1][k].amount == radius);
        CHECK(std::fabs(rig.log[1][k].x - at.x) < 1.0f && std::fabs(rig.log[1][k].z - at.z) < 1.0f);
    }
    CHECK(strikes == 1 && throwAt >= 0);
    CHECK(shovedAt >= 0);
    // The blast lands a fuse after the throw (the client hears of the throw one network hop after the server made it).
    if (shovedAt >= 0 && throwAt >= 0) CHECK(std::fabs((shovedAt - throwAt) - kShockwaveFuse) <= 3 * kDt);
    CHECK(farMoved < 30.0f);
    CHECK(nearBot->health == nearHealth && farBot->health == farHealth && me->health == me->maxHealth);
    CHECK(rig.server.GetStats().badPackets == 0);
}

// ---- harness --------------------------------------------------------------------------------------------------------------------------------
struct Smoke { const char* name; void (*fn)(uint64_t); };
static const Smoke kSmokes[] = {
    {"match_flow", Smoke_match_flow}, {"bus", Smoke_bus}, {"teams", Smoke_teams}, {"skins", Smoke_skins},
    {"allies_chief_peter", Smoke_allies_chief_peter}, {"map_skyloft", Smoke_map_skyloft}, {"shockwave", Smoke_shockwave},
};

int main(int argc, char** argv) {
    std::vector<std::string> names;
    std::vector<uint64_t> seeds = {1, 2, 3};
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--list")) { for (auto& s : kSmokes) std::printf("%s\n", s.name); return 0; }
        if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) { seeds = {std::strtoull(argv[++i], nullptr, 10)}; continue; }
        names.push_back(argv[i]);
    }
    for (auto& n : names) {
        bool known = false;
        for (auto& s : kSmokes) known |= n == s.name;
        if (!known) { std::printf("FAIL smoke.%s  unknown smoke (--list)\n", n.c_str()); return 1; }
    }
    int failed = 0;
    for (auto& s : kSmokes) {
        if (!names.empty() && std::find(names.begin(), names.end(), s.name) == names.end()) continue;
        const int before = failures, knownBefore = known;
        const auto t0 = std::chrono::steady_clock::now();
        std::string seedList;
        for (uint64_t seed : seeds) {
            const int f = failures;
            s.fn(seed);
            if (failures != f) std::printf("  (seed %llu)\n", static_cast<unsigned long long>(seed));
            seedList += (seedList.empty() ? "" : ",") + std::to_string(seed);
        }
        const long ms = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
        if (failures != before) { std::printf("FAIL smoke.%s  seeds %s  (%ld ms)\n", s.name, seedList.c_str(), ms); failed++; }
        else if (known != knownBefore) std::printf("PASS smoke.%s  seeds %s  (%ld ms)  known issue x%d\n", s.name, seedList.c_str(), ms, known - knownBefore);
        else std::printf("PASS smoke.%s  seeds %s  (%ld ms)\n", s.name, seedList.c_str(), ms);
        std::fflush(stdout);
    }
    return failed ? 1 : 0;
}

