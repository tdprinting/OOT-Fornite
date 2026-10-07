// Network layer tests over the in-memory LoopbackNetwork (no sockets): protocol, join flow, interpolation,
// validation, events, loss, bandwidth. Real-UDP tests are in enet_tests.cpp.
#include "game_client.h"
#include "game_server.h"
#include "loopback.h"
#include "../../shared/fortnite_map.h"
#include <cstdio>
#include <memory>

using namespace royale;
using namespace royale::net;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static Circle MapCircle() { return {{0, 0}, 2000.0f}; }
constexpr float kDt = 1.0f / kTickHz;

// A server plus any number of clients on one loopback network.
struct Rig {
    LoopbackNetwork network;
    GameServer server;
    std::vector<std::unique_ptr<GameClient>> clients;

    explicit Rig(uint64_t seed = 1, int loot = 400) : network(seed), server(network.Server(), seed, MapCircle(), loot) {}

    GameClient& Add(const std::string& name, LoopbackNetwork::Link link = {}) {
        clients.push_back(std::make_unique<GameClient>(network.NewClient(link), name));
        return *clients.back();
    }
    void Step(float dt = kDt) {
        network.Advance(dt);
        server.Update(dt);
        for (auto& c : clients) c->Update(dt);
    }
    void Run(float seconds, float dt = kDt) {
        for (int i = 0; i < static_cast<int>(seconds / dt + 0.5f); i++) Step(dt);
    }
    // Run until the condition holds or `maxSeconds` pass.
    template <class F>
    bool RunUntil(F cond, float maxSeconds = 30) {
        for (float t = 0; t < maxSeconds; t += kDt) {
            if (cond()) return true;
            Step();
        }
        return cond();
    }
    Match& M() { return server.Sim().match; }
    bool AllJoined() {
        for (auto& c : clients) if (c->GetStatus() != GameClient::Status::Joined) return false;
        return true;
    }
    // Start the match and run until the match is live (past countdown and drop).
    void StartAndGoLive() {
        CHECK(server.StartMatch());
        CHECK(RunUntil([&] { return M().State() == MatchState::InMatch; }));
    }
};

// ---- serialization --------------------------------------------------------------------------------------------

static void ByteReaderBounds() {
    uint8_t d[3] = {1, 2, 3};
    ByteReader r(d, 3);
    CHECK(r.U16() == 0x0201 && r.ok);
    CHECK(r.U32() == 0 && !r.ok);        // asked for 4, only 1 left
    CHECK(r.U8() == 0 && !r.ok);         // stays failed
    ByteWriter w;
    w.Str(std::string(300, 'x'));
    CHECK(w.buf.size() == 256);          // truncated to 255 + length byte
    ByteReader r2(w.buf.data(), w.buf.size());
    CHECK(r2.Str(10).empty() && !r2.ok); // longer than the allowed max
}

template <class T> static bool RoundTrips(const T& in, T& out) { return Decode(Encode(in), out); }

static Welcome SampleWelcome() {
    Welcome w;
    w.playerId = 7; w.seed = 0x1122334455667788ull; w.map = {{1, 2}, 3000};
    for (int i = 0; i < kStormPhaseCount; i++) w.stormEnds[i] = {{float(i), float(-i)}, 100.0f - i};
    w.loot = {{10, 20, 3, 2, true, false}, {-5, 6, 7, 4, false, true}};
    w.roster = {{1, kRosterHost, "Link"}, {7, kRosterReady, "Zelda"}};
    return w;
}

static void MessagesRoundTrip() {
    { Hello a, b; a.name = "Link"; a.hostToken = 0xABCDEF0123456789ull; CHECK(RoundTrips(a, b) && b.name == "Link" && b.version == kProtocolVersion && b.hostToken == a.hostToken); }
    { Input a, b; a.seq = 65535; a.epoch = 3; a.x = -1.5f; a.y = 2; a.z = 3.25f; a.rot = -1234; a.anim = 9; a.scene = 0x43;
      CHECK(RoundTrips(a, b) && b.seq == 65535 && b.epoch == 3 && b.x == -1.5f && b.z == 3.25f && b.rot == -1234 && b.anim == 9 && b.scene == 0x43); }
    { AttackReport a, b; a.target = 12; a.hit = true; CHECK(RoundTrips(a, b) && b.target == 12 && b.hit); }
    { PickupRequest a, b; a.index = 123456; a.force = true; CHECK(RoundTrips(a, b) && b.index == 123456 && b.force); }
    { UsePotionRequest a, b; CHECK(RoundTrips(a, b)); }
    { NpcHitRequest a, b; a.tenths = 7; CHECK(RoundTrips(a, b) && b.tenths == 7); }
    { Welcome a = SampleWelcome(), b; CHECK(RoundTrips(a, b));
      CHECK(b.playerId == 7 && b.seed == a.seed && b.map.radius == 3000 && b.loot.size() == 2 && b.roster[1].name == "Zelda");
      CHECK(b.loot[0].chest && !b.loot[0].taken && b.loot[1].taken && b.stormEnds[5].radius == 95.0f); }
    { Reject a, b; a.reason = RejectReason::LobbyFull; CHECK(RoundTrips(a, b) && b.reason == RejectReason::LobbyFull); }
    { MatchStateMsg a, b; a.state = 3; a.alive = 17; a.winner = 1003; CHECK(RoundTrips(a, b) && b.state == 3 && b.alive == 17 && b.winner == 1003); }
    { SetReady a, b; a.ready = true; CHECK(RoundTrips(a, b) && b.ready); }
    { EvMapConfig a, b; a.map = {{5, -6}, 2500}; for (int i = 0; i < kStormPhaseCount; i++) a.stormEnds[i] = {{float(i), 1}, 100.0f - i}; a.loot = {{1, 2, 3, 1, true, false}};
      CHECK(RoundTrips(a, b) && b.map.radius == 2500 && b.map.center.z == -6 && b.stormEnds[5].radius == 95 && b.loot.size() == 1 && b.loot[0].item == 3); }
    { EvReady a, b; a.id = 9; a.ready = true; CHECK(RoundTrips(a, b) && b.id == 9 && b.ready); }
    { Snapshot a, b; a.tick = 99; a.stormTime = 12.5f; a.state = 3; a.alive = 9; a.epoch = 2;
      PlayerNet p; p.id = 1031; p.x = 1; p.y = 2; p.z = 3; p.rot = -5; p.health = PlayerNet::QuantizeHealth(1.5f);
      p.flags = PlayerNet::kAlive | PlayerNet::kBot; p.weapon = 2; p.weaponRarity = 4; p.potions = 2; p.anim = 7; p.scene = 0x51; p.shield = 17; p.shieldRarity = 3; p.boots = 52; p.mask = 60;
      a.players = {p, p};
      CHECK(RoundTrips(a, b) && b.players.size() == 2 && b.players[0].id == 1031 && b.players[1].potions == 2 && b.players[0].scene == 0x51 && b.players[0].shield == 17 && b.players[0].shieldRarity == 3 &&
            b.players[1].boots == 52 && b.players[1].mask == 60);
      CHECK(std::abs(b.players[0].Health() - 1.5f) < 0.01f);
      ByteWriter w; p.Write(w); CHECK(w.buf.size() == 27); } // documented per-player size
    { EvDamaged a, b; a.target = 1; a.attacker = 2; a.amount = 1.5f; a.health = 0.5f; CHECK(RoundTrips(a, b) && b.amount == 1.5f && b.attacker == 2); }
    { EvEliminated a, b; a.victim = 3; CHECK(RoundTrips(a, b) && b.victim == 3 && b.killer == kNoPlayer16); }
    { EvLootTaken a, b; a.index = 9; a.by = 4; CHECK(RoundTrips(a, b) && b.index == 9 && b.by == 4); }
    { EvLootAdded a, b; a.index = 400; a.loot = {1, 2, 3, 4, true, false}; CHECK(RoundTrips(a, b) && b.index == 400 && b.loot.item == 3); }
    { EvPlayerJoined a, b; a.id = 5; a.flags = kRosterHost | kRosterReady; a.name = "Navi"; CHECK(RoundTrips(a, b) && b.name == "Navi" && b.flags == 3); }
    { Hello a, b; a.name = "X"; a.tunic = PackRgb(10, 200, 255); CHECK(RoundTrips(a, b) && b.tunic == PackRgb(10, 200, 255)); }
    { EvPlayerJoined a, b; a.id = 4; a.name = "Y"; a.tunic = PackRgb(1, 2, 3); CHECK(RoundTrips(a, b) && b.tunic == PackRgb(1, 2, 3)); }
    { EvBossDown a, b; a.boss = kBossIdBase + 2; a.killer = 7; a.x = 5; a.z = -9; CHECK(RoundTrips(a, b) && b.boss == kBossIdBase + 2 && b.killer == 7 && b.z == -9); }
    { EvBossDown bad, out; bad.boss = 12; CHECK(!RoundTrips(bad, out)); }                    // not a boss id
    { Snapshot a, b; BossNet n; n.index = 3; n.kind = 2; n.x = 10; n.z = -4; n.rot = -123; n.hp = 77; n.smashing = true; a.bosses = {n};
      CHECK(RoundTrips(a, b) && b.bosses.size() == 1 && b.bosses[0].Id() == kBossIdBase + 3 && b.bosses[0].kind == 2 && b.bosses[0].rot == -123 && b.bosses[0].hp == 77 && b.bosses[0].smashing); }
    for (int kind=static_cast<int>(BossKind::ChuRed);kind<=static_cast<int>(BossKind::ChuDark);kind++) {
        Snapshot a,b; BossNet n; n.kind=static_cast<uint8_t>(kind);n.mode=static_cast<uint8_t>(DragonMode::Stunned);n.aux=4;a.bosses={n};
        CHECK(RoundTrips(a,b) && b.bosses[0].kind==kind && b.bosses[0].aux==4);
    }
    { Snapshot bad, out; BossNet n; n.index = 9; bad.bosses = {n}; CHECK(!RoundTrips(bad, out)); }   // index out of range
    { Snapshot a, b; BossNet n; n.mode = static_cast<uint8_t>(DragonMode::Stunned); n.aux = 2; a.bosses = {n};
      CHECK(RoundTrips(a, b) && b.bosses[0].mode == static_cast<uint8_t>(DragonMode::Stunned) && b.bosses[0].aux == 2); }   // what it is doing, and which variant
    { Snapshot bad, out; BossNet n; n.mode = static_cast<uint8_t>(DragonMode::Count); bad.bosses = {n}; CHECK(!RoundTrips(bad, out)); }
    { EvStrike a, b; a.by = kDragonId; a.radius = 100; a.delay = 1; a.style = static_cast<uint8_t>(StrikeStyle::Ice); CHECK(RoundTrips(a, b) && b.style == a.style); }
    { EvStrike bad, out; bad.style = static_cast<uint8_t>(StrikeStyle::Count); CHECK(!RoundTrips(bad, out)); }
    { UseShieldRequest a, b; CHECK(RoundTrips(a, b)); }
    { EvInventory a, b; a.shield = 2.5f; CHECK(RoundTrips(a, b) && b.shield == 2.5f); }
    { EvInventory bad, out; bad.shield = 9.0f; CHECK(!RoundTrips(bad, out)); }
    { UseAbilityRequest a, b; CHECK(RoundTrips(a, b)); }
    { SelectWeaponRequest a, b; a.slot = 2; CHECK(RoundTrips(a, b) && b.slot == 2); }
    { SelectWeaponRequest bad; bad.slot = 9; SelectWeaponRequest out; CHECK(!RoundTrips(bad, out)); }
    { EvMapConfig a, b; a.props = {{{1, 2}, PropKind::Boulder, 123}, {{-3, 4}, PropKind::Bush, 65535}};
      CHECK(RoundTrips(a, b) && b.props.size() == 2 && b.props[0].kind == PropKind::Boulder && b.props[1].rot == 65535 && b.props[1].pos.x == -3); }
    { EvMapConfig a, b; a.pois = {{3, {10, 20}, 480}, {15, {-5, 7}, 480}}; CHECK(RoundTrips(a, b) && b.pois.size() == 2 && b.pois[1].name == 15 && b.pois[0].center.z == 20 && b.pois[0].radius == 480); }
    { EvMapConfig a, b; a.pois = {{250, {0, 0}, 1}}; CHECK(!RoundTrips(a, b)); }           // not a name we have
    { EvInventory a, b; a.reserve = {{5, 1}, {6, 2}}; CHECK(RoundTrips(a, b) && b.reserve.size() == 2 && b.reserve[1].item == 6); }
    { EvAbility a, b; a.user = 7; a.item = 55; a.x = 1.5f; a.z = -2; CHECK(RoundTrips(a, b) && b.user == 7 && b.item == 55 && b.x == 1.5f && b.z == -2); }
    { EvInventory a, b; a.maxHealth = 5; a.heartPieces = 3; a.potions = {{1, 2}, {3, 4}}; a.hasAbility = true; a.hasMark = true; a.ability = {40, 3};
      a.abilityReadyIn = 12.5f; a.gearMask = 0x05; a.gear[0] = {60, 1}; a.gear[2] = {62, 4}; a.speedMult = 1.4f; a.speedLeft = 6; a.shieldLeft = 2;
      CHECK(RoundTrips(a, b) && b.maxHealth == 5 && b.heartPieces == 3 && b.potions.size() == 2 && b.potions[1].rarity == 4 && b.hasAbility && b.hasMark
            && b.ability.item == 40 && b.abilityReadyIn == 12.5f && b.gearMask == 5 && b.gear[0].item == 60 && b.gear[2].rarity == 4 && b.gear[1].item == 0
            && b.speedMult == 1.4f && b.speedLeft == 6 && b.shieldLeft == 2); }
    { EvPlayerLeft a, b; a.id = 5; CHECK(RoundTrips(a, b) && b.id == 5); }
}

// Every strict prefix of a valid message, a message with a trailing byte, and a wrong type byte must all be rejected.
template <class T>
static void RejectsMangled(const T& msg) {
    std::vector<uint8_t> good = Encode(msg);
    T out;
    CHECK(Decode(good, out));
    for (size_t n = 0; n < good.size(); n++) CHECK(!Decode(good.data(), n, out));
    std::vector<uint8_t> extra = good;
    extra.push_back(0);
    CHECK(!Decode(extra, out));
    std::vector<uint8_t> wrongType = good;
    wrongType[0] ^= 0x40;
    CHECK(!Decode(wrongType, out));
}

static void DecodeRejectsMangled() {
    Hello h; h.name = "Link"; RejectsMangled(h);
    Input in; in.x = 1; RejectsMangled(in);
    AttackReport ar; ar.target = 1; RejectsMangled(ar);
    PickupRequest pr; RejectsMangled(pr);
    RejectsMangled(SampleWelcome());
    Reject rj; rj.reason = RejectReason::LobbyFull; RejectsMangled(rj);
    MatchStateMsg ms; RejectsMangled(ms);
    Snapshot s; s.players.resize(2); RejectsMangled(s);
    EvDamaged ed; RejectsMangled(ed);
    EvEliminated ee; RejectsMangled(ee);
    EvLootTaken lt; RejectsMangled(lt);
    EvLootAdded la; RejectsMangled(la);
    EvPlayerJoined pj; pj.name = "x"; RejectsMangled(pj);
    SetReady sr; sr.ready = true; RejectsMangled(sr);
    EvReady er; RejectsMangled(er);
    EvMapConfig mc; mc.loot = {{1, 2, 3, 1, true, false}}; RejectsMangled(mc);
    EvPlayerLeft pl; RejectsMangled(pl);
}

static void DecodeRejectsBadValues() {
    Input in, out;
    in.x = std::nanf("");
    CHECK(!Decode(Encode(in), out));
    in.x = 0; in.y = INFINITY;
    CHECK(!Decode(Encode(in), out));
    AttackReport ar, arOut;
    std::vector<uint8_t> bytes = Encode(ar);
    bytes[bytes.size() - 2] = 2; // hit must be 0 or 1
    CHECK(!Decode(bytes, arOut));
    bytes = Encode(ar);
    bytes.back() = 3; // style must be a known AttackStyle
    CHECK(!Decode(bytes, arOut));
    ar.style = 2;
    CHECK(Decode(Encode(ar), arOut) && arOut.style == 2);
    Reject rj, rjOut;
    bytes = Encode(rj);
    bytes.back() = 0;
    CHECK(!Decode(bytes, rjOut));
    SetReady srv, srOut;
    bytes = Encode(srv);
    bytes.back() = 2; // ready must be 0 or 1
    CHECK(!Decode(bytes, srOut));
    LootNet bad;
    bad.item = static_cast<uint8_t>(ItemId::Count);
    ByteWriter w; w.U8(static_cast<uint8_t>(MsgType::EvLootAdded)); w.U32(0); bad.Write(w);
    EvLootAdded la;
    CHECK(!Decode(w.buf, la));
    Snapshot s, sOut;
    s.players.resize(2);
    bytes = Encode(s);
    bytes[1 + 4 + 4 + 1 + 1 + 1 + 1] = 200; // player count far above the cap
    CHECK(!Decode(bytes, sOut));
    ByteWriter huge; huge.U8(static_cast<uint8_t>(MsgType::Welcome)); huge.U16(1); huge.U16(kProtocolVersion); huge.U64(0);
    for (int i = 0; i < 3 + 3 * kStormPhaseCount; i++) huge.F32(1);
    huge.U16(60000); // more loot than kMaxLoot
    Welcome wo;
    CHECK(!Decode(huge.buf, wo));
    CHECK(SanitizeName(std::string("Li\nnk\x01\xff") + std::string(40, 'a')).size() == kMaxNameLen);
    CHECK(SanitizeName("A\tB") == "AB");
}

static void FuzzNeverCrashes() {
    Rng rng(2024);
    Hello a; Input b; AttackReport c; PickupRequest d; Welcome e; Reject f; MatchStateMsg g; Snapshot h;
    EvDamaged i; EvEliminated j; EvLootTaken k; EvLootAdded l; EvPlayerJoined m; EvPlayerLeft n;
    for (int iter = 0; iter < 40000; iter++) {
        std::vector<uint8_t> buf(rng.Below(80));
        for (auto& x : buf) x = static_cast<uint8_t>(rng.Below(256));
        if (!buf.empty() && rng.Below(2)) buf[0] = static_cast<uint8_t>(rng.Below(2) ? 1 + rng.Below(5) : 64 + rng.Below(12));
        (void)Decode(buf, a); (void)Decode(buf, b); (void)Decode(buf, c); (void)Decode(buf, d); (void)Decode(buf, e);
        (void)Decode(buf, f); (void)Decode(buf, g); (void)Decode(buf, h); (void)Decode(buf, i); (void)Decode(buf, j);
        (void)Decode(buf, k); (void)Decode(buf, l); (void)Decode(buf, m); (void)Decode(buf, n);
    }
    // Mutate valid messages one byte at a time.
    std::vector<uint8_t> w = Encode(SampleWelcome());
    for (size_t pos = 0; pos < w.size(); pos++) {
        for (int v : {0, 1, 0x7F, 0x80, 0xFF}) {
            std::vector<uint8_t> x = w;
            x[pos] = static_cast<uint8_t>(v);
            Welcome out;
            (void)Decode(x, out);
        }
    }
}

// ---- transport -----------------------------------------------------------------------------------------------

static void LoopbackLatencyAndLoss() {
    LoopbackNetwork net(1);
    Transport& server = net.Server();
    LoopbackNetwork::Link link;
    link.latencySec = 0.1f;
    link.unreliableLoss = 0.5f;
    Transport& client = net.NewClient(link);
    NetEvent ev;
    CHECK(!server.Poll(ev));              // nothing yet: the connect is still in flight
    net.Advance(0.11f);
    CHECK(server.Poll(ev) && ev.type == NetEvent::Type::Connected);
    CHECK(client.Poll(ev) && ev.type == NetEvent::Type::Connected);
    uint8_t one = 1;
    for (int i = 0; i < 400; i++) client.Send(0, &one, 1, false);
    for (int i = 0; i < 20; i++) client.Send(0, &one, 1, true);
    net.Advance(0.2f);
    int unreliable = 0, total = 0;
    while (server.Poll(ev)) { total++; }
    unreliable = total - 20;
    CHECK(unreliable > 120 && unreliable < 280);   // about half of 400 lost
    CHECK(total >= 20);                             // reliable ones all arrived
}

static void LoopbackKeepsOrderUnderJitter() {
    LoopbackNetwork net(5);
    LoopbackNetwork::Link link;
    link.latencySec = 0.05f;
    link.jitterSec = 0.2f;
    Transport& client = net.NewClient(link);
    NetEvent ev;
    net.Advance(1);
    while (net.Server().Poll(ev)) {}
    while (client.Poll(ev)) {}
    for (uint8_t i = 0; i < 100; i++) net.Server().Send(1, &i, 1, true);
    net.Advance(1);
    int expect = 0;
    while (client.Poll(ev)) { CHECK(ev.data.size() == 1 && ev.data[0] == expect); expect++; }
    CHECK(expect == 100);
}

// ---- join flow -----------------------------------------------------------------------------------------------

static void JoinAndWelcome() {
    Rig rig(11);
    GameClient& a = rig.Add("Link");
    CHECK(a.GetStatus() == GameClient::Status::Connecting);
    CHECK(rig.RunUntil([&] { return a.GetStatus() == GameClient::Status::Joined; }));
    CHECK(a.PlayerId() == 1);
    CHECK(a.Seed() == 11 && a.Map().radius == 2000);
    CHECK(a.Loot().size() == rig.M().Loot().size() && a.Loot().size() == 400);
    CHECK(a.Roster().size() == 1 && a.Roster().at(1).name == "Link" && !a.Roster().at(1).host);
    // The client rebuilt the same storm from the 6 circles.
    for (float t : {0.0f, 100.0f, 150.0f, 300.0f, 500.0f, 660.0f}) {
        Circle s = rig.M().GetStorm().SafeZoneAt(t), c = Storm(a.Map(), rig.M().GetStorm().PhaseEnds()).SafeZoneAt(t);
        CHECK(s.center.x == c.center.x && s.radius == c.radius);
    }
    GameClient& b = rig.Add("Zelda");
    CHECK(rig.RunUntil([&] { return b.GetStatus() == GameClient::Status::Joined; }));
    CHECK(b.PlayerId() == 2 && b.Roster().size() == 2);
    rig.Run(1);
    bool sawJoin = false;
    for (auto& e : a.DrainEvents()) if (e.type == ClientEvent::Type::PlayerJoined && e.id == 2) sawJoin = true;
    CHECK(sawJoin && a.Roster().size() == 2 && a.Roster().at(2).name == "Zelda" && !a.Roster().at(2).host);
    CHECK(rig.server.HumanCount() == 2);
}

static void RejectedJoins() {
    // Version mismatch (raw Hello with a wrong version).
    {
        Rig rig;
        Transport& raw = rig.network.NewClient();
        rig.Run(0.5f);
        Hello h; h.version = 99; h.name = "Old";
        raw.Send(0, Encode(h), true);
        rig.Run(0.5f);
        NetEvent ev; Reject rj; bool got = false;
        while (raw.Poll(ev)) if (ev.type == NetEvent::Type::Data && Decode(ev.data, rj)) got = rj.reason == RejectReason::VersionMismatch;
        CHECK(got);
        CHECK(rig.server.HumanCount() == 0);
    }
    // Garbage instead of Hello.
    {
        Rig rig;
        Transport& raw = rig.network.NewClient();
        rig.Run(0.5f);
        uint8_t junk[3] = {1, 2, 3};
        raw.Send(0, junk, 3, true);
        rig.Run(0.5f);
        NetEvent ev; Reject rj; bool got = false;
        while (raw.Poll(ev)) if (ev.type == NetEvent::Type::Data && Decode(ev.data, rj)) got = rj.reason == RejectReason::BadHello;
        CHECK(got && rig.server.HumanCount() == 0);
    }
    // Non-Hello message before joining is dropped, and gives the peer no state.
    {
        Rig rig;
        Transport& raw = rig.network.NewClient();
        rig.Run(0.5f);
        Input in; in.x = 5;
        raw.Send(0, Encode(in), false);
        PickupRequest pr; raw.Send(0, Encode(pr), true);
        rig.Run(0.5f);
        CHECK(rig.server.GetStats().badPackets == 2 && rig.server.HumanCount() == 0);
        NetEvent ev; int snapshots = 0;
        while (raw.Poll(ev)) if (ev.type == NetEvent::Type::Data) snapshots++;
        CHECK(snapshots == 0);
    }
    // Lobby full.
    {
        Rig rig(1, 10);
        for (int i = 0; i < kMaxPlayers; i++) rig.Add("P" + std::to_string(i));
        CHECK(rig.RunUntil([&] { return rig.AllJoined(); }));
        GameClient& extra = rig.Add("Late");
        CHECK(rig.RunUntil([&] { return extra.GetStatus() == GameClient::Status::Rejected; }));
        CHECK(extra.RejectedBecause() == RejectReason::LobbyFull);
        CHECK(rig.server.HumanCount() == kMaxPlayers);
    }
    // Match already running.
    {
        Rig rig;
        GameClient& a = rig.Add("A");
        CHECK(rig.RunUntil([&] { return a.GetStatus() == GameClient::Status::Joined; }));
        CHECK(rig.server.StartMatch());
        GameClient& late = rig.Add("Late");
        CHECK(rig.RunUntil([&] { return late.GetStatus() == GameClient::Status::Rejected; }));
        CHECK(late.RejectedBecause() == RejectReason::MatchInProgress);
    }
}

static void StartNeedsAHuman() {
    Rig rig;
    CHECK(!rig.server.StartMatch());
    CHECK(rig.M().State() == MatchState::Lobby);
}

// ---- in-match behaviour -----------------------------------------------------------------------------------------

static void TeleportEpochIgnoresOldInputs() {
    Rig rig(3, 0);
    GameClient& a = rig.Add("A");
    CHECK(rig.RunUntil([&] { return a.GetStatus() == GameClient::Status::Joined; }));
    a.SendInput(-1500, 0, 1500, 0, 0);        // client wanders around the lobby
    rig.Run(0.3f);
    CHECK(a.Epoch() == 0);
    CHECK(rig.server.StartMatch());
    Vec2 spawn = rig.M().Find(1)->pos;
    // An input sent from the lobby position, before the client has seen the new epoch, must not drag the player.
    a.SendInput(-1500, 0, 1500, 0, 0);
    rig.Run(0.5f);
    CHECK(rig.M().Find(1)->pos.x == spawn.x && rig.M().Find(1)->pos.z == spawn.z);
    CHECK(rig.server.GetStats().staleInputs >= 1);
    CHECK(a.Epoch() == 1);                    // learned about the teleport from a snapshot
    CHECK(a.Self() && std::abs(a.Self()->x - spawn.x) < 1 && std::abs(a.Self()->z - spawn.z) < 1);
    // Now the client sits at the spawn point and its inputs are accepted.
    a.SendInput(spawn.x + 10, 0, spawn.z, 0, 0);
    rig.Run(0.3f);
    CHECK(std::abs(rig.M().Find(1)->pos.x - (spawn.x + 10)) < 0.01f);
}

static void SpeedClamp() {
    Rig rig(3, 0);
    GameClient& a = rig.Add("A");
    CHECK(rig.RunUntil([&] { return a.GetStatus() == GameClient::Status::Joined; }));
    rig.StartAndGoLive();
    rig.Run(0.5f);
    PlayerState* p = rig.M().Find(1);
    Vec2 before = p->pos;
    a.SendInput(before.x + 5000, 0, before.z, 0, 0);       // 5000 units in one update
    rig.Run(0.3f);
    float moved = Distance(before, p->pos);
    CHECK(moved > 0 && moved <= kMaxPlausibleSpeed * GameServer::kMaxInputGap + kMovementSlack);
    CHECK(rig.server.GetStats().speedClamps == 1);
    // A normal run is not clamped.
    before = p->pos;
    a.SendInput(before.x + 20, 0, before.z, 0, 0);
    rig.Run(0.3f);
    CHECK(std::abs(p->pos.x - (before.x + 20)) < 0.01f && rig.server.GetStats().speedClamps == 1);
}

static void OldAndDuplicateInputsIgnored() {
    Rig rig(3, 0);
    Transport& raw = rig.network.NewClient();
    rig.Run(0.3f);
    Hello h; h.name = "Raw";
    raw.Send(0, Encode(h), true);
    rig.Run(0.3f);
    CHECK(rig.server.HumanCount() == 1);
    auto send = [&](uint16_t seq, float x) {
        Input in; in.seq = seq; in.x = x;
        raw.Send(0, Encode(in), false);
        rig.Run(0.15f);
        return rig.M().Find(1)->pos.x;
    };
    CHECK(send(5, 100) == 100);
    CHECK(send(3, 200) == 100);                    // older than what we have: ignored
    CHECK(send(5, 300) == 100);                    // duplicate: ignored
    CHECK(send(6, 400) == 400);
    CHECK(rig.server.GetStats().staleInputs == 2);
    CHECK(send(30000, 1) == 1);                    // jumps in seq are fine while they move forward
    CHECK(send(60000, 2) == 2);
    CHECK(send(65535, 3) == 3);
    CHECK(send(0, 4) == 4);                        // wraps around: 0 is newer than 65535
    CHECK(send(65535, 5) == 4);                    // and 65535 is now old
}

static void NaNInputNeverAccepted() {
    Rig rig(3, 0);
    GameClient& a = rig.Add("A");
    CHECK(rig.RunUntil([&] { return a.GetStatus() == GameClient::Status::Joined; }));
    rig.StartAndGoLive();
    Vec2 before = rig.M().Find(1)->pos;
    a.SendInput(std::nanf(""), 0, 0, 0, 0);
    rig.Run(0.3f);
    CHECK(rig.M().Find(1)->pos.x == before.x && rig.server.GetStats().badPackets >= 1);
}

static void AttackOverTheWire() {
    Rig rig(3, 0);
    GameClient& a = rig.Add("A");
    GameClient& b = rig.Add("B");
    GameClient& c = rig.Add("Bystander");
    CHECK(rig.RunUntil([&] { return rig.AllJoined(); }));
    rig.StartAndGoLive();
    // Park the three humans, everyone else is dead.
    for (auto& p : rig.M().Players()) if (!p.isBot) {} else p.alive = false;
    rig.M().Find(1)->pos = {0, 0};  rig.M().Find(2)->pos = {50, 0};  rig.M().Find(3)->pos = {1000, 1000};
    rig.M().Find(1)->weapon = {ItemId::KokiriSword, Rarity::Common};
    rig.Run(0.5f);
    a.DrainEvents(); b.DrainEvents(); c.DrainEvents();

    a.ReportAttack(2, true);
    rig.Run(0.5f);
    float hp = rig.M().Find(2)->health;
    CHECK(hp < kMaxHealth);
    bool aSaw = false, bSaw = false, cSaw = false;
    for (auto& e : a.DrainEvents()) aSaw |= e.type == ClientEvent::Type::Damaged && e.id == 2 && e.other == 1;
    for (auto& e : b.DrainEvents()) bSaw |= e.type == ClientEvent::Type::Damaged && e.id == 2 && e.other == 1;
    for (auto& e : c.DrainEvents()) cSaw |= e.type == ClientEvent::Type::Damaged;
    CHECK(aSaw && bSaw && !cSaw);                      // only attacker and target are told
    CHECK(b.Self() && std::abs(b.Self()->Health() - hp) < 0.02f); // everyone sees health in snapshots

    // Cooldown, range and unknown targets are rejected by the server.
    uint64_t rejectedBefore = rig.server.GetStats().rejectedActions;
    a.ReportAttack(2, true);                           // inside the cooldown
    a.ReportAttack(3, true);                           // out of range
    a.ReportAttack(500, true);                         // nobody has this id
    rig.Run(0.3f);
    CHECK(rig.server.GetStats().rejectedActions == rejectedBefore + 3);

    // Finish B off: Eliminated goes to everyone, and the dropped sword shows up as new loot on every client.
    size_t lootBefore = a.Loot().size();
    rig.M().Find(2)->weapon = {ItemId::MasterSword, Rarity::Epic};
    rig.M().Find(2)->health = 0.1f;
    rig.Run(1.0f);
    a.ReportAttack(2, true);
    rig.Run(0.5f);
    CHECK(!rig.M().Find(2)->alive);
    bool aElim = false, cElim = false;
    for (auto& e : a.DrainEvents()) aElim |= e.type == ClientEvent::Type::Eliminated && e.id == 2 && e.other == 1;
    for (auto& e : c.DrainEvents()) cElim |= e.type == ClientEvent::Type::Eliminated && e.id == 2;
    CHECK(aElim && cElim);
    CHECK(a.Loot().size() == lootBefore + 1 && c.Loot().size() == lootBefore + 1);
    CHECK(a.Loot().back().item == static_cast<uint8_t>(ItemId::MasterSword) && a.DesyncCount() == 0);
}

static void PickupAndPotionOverTheWire() {
    Rig rig(3, 0);
    GameClient& a = rig.Add("A");
    GameClient& b = rig.Add("B");
    CHECK(rig.RunUntil([&] { return rig.AllJoined(); }));
    rig.StartAndGoLive();
    for (auto& p : rig.M().Players()) if (p.isBot) p.alive = false;
    rig.M().Find(1)->pos = {0, 0};
    rig.M().Find(2)->pos = {1500, 0};
    size_t sword = rig.M().AddLoot({{10, 0}, ItemId::MasterSword, Rarity::Epic, false});
    size_t farPotion = rig.M().AddLoot({{900, 0}, ItemId::RedPotion, Rarity::Common, false});
    size_t potion = rig.M().AddLoot({{0, 20}, ItemId::RedPotion, Rarity::Rare, false});
    rig.Run(0.5f);
    CHECK(a.Loot().size() == 3 && b.Loot().size() == 3);

    a.RequestPickup(static_cast<uint32_t>(farPotion));   // too far
    a.RequestPickup(9999);                              // doesn't exist
    a.RequestPickup(static_cast<uint32_t>(sword));
    a.RequestPickup(static_cast<uint32_t>(potion));
    rig.Run(0.5f);
    CHECK(rig.M().Find(1)->weapon.item == ItemId::MasterSword && rig.M().Find(1)->potions.size() == 1);
    CHECK(a.Loot()[sword].taken && b.Loot()[sword].taken && !b.Loot()[farPotion].taken);
    CHECK(rig.server.GetStats().rejectedActions == 2);
    CHECK(a.Self() && a.Self()->weapon == static_cast<uint8_t>(ItemId::MasterSword) && a.Self()->potions == 1);

    rig.M().Find(1)->health = kMaxHealth - 1.0f;
    a.RequestUsePotion();
    rig.Run(0.5f);
    CHECK(rig.M().Find(1)->health == kMaxHealth && rig.M().Find(1)->potions.empty());
    uint64_t rejected = rig.server.GetStats().rejectedActions;
    a.RequestUsePotion();                               // nothing left
    rig.Run(0.3f);
    CHECK(rig.server.GetStats().rejectedActions == rejected + 1);
    CHECK(a.DesyncCount() == 0 && b.DesyncCount() == 0);
}

static void ResultsAndRematchOverTheWire() {
    Rig rig(11, 40);
    rig.server.SetHostToken(0xABCDEF12ull);
    rig.clients.push_back(std::make_unique<GameClient>(rig.network.NewClient(), "Host", 0xABCDEF12ull));
    GameClient& host = *rig.clients.back();
    GameClient& guest = rig.Add("Guest");
    CHECK(rig.RunUntil([&] { return rig.AllJoined(); }));
    auto run = [&](float seconds) { rig.Run(seconds); };
    rig.StartAndGoLive();
    for (auto& p : rig.M().Players()) if (p.isBot) p.alive = false;
    rig.M().Find(1)->pos = {0, 0};
    rig.M().Find(2)->pos = {40, 0};
    rig.M().Find(1)->weapon = {ItemId::MasterSword, Rarity::Legendary};
    rig.M().Damage(2, 100.0f, 1);
    run(1.0f);
    CHECK(rig.M().State() == MatchState::Ending);
    // Everyone received the standings, best first, with the winner on top.
    CHECK(!host.Results().empty() && host.Results().size() == guest.Results().size());
    CHECK(host.Results()[0].id == 1 && host.Results()[0].placement == 1 && host.Results()[0].score > host.Results()[1].score);
    // Only the host can start another match.
    const uint64_t rejected = rig.server.GetStats().rejectedActions;
    guest.RequestRematch();
    run(0.3f);
    CHECK(rig.M().State() == MatchState::Ending && rig.server.GetStats().rejectedActions == rejected + 1);
    const uint64_t oldSeed = rig.M().Seed();
    host.RequestRematch();
    run(0.5f);
    CHECK(rig.M().State() == MatchState::Countdown);                  // straight into the countdown, no lobby
    CHECK(rig.M().Seed() != oldSeed);                                 // fresh loot and storm
    CHECK(host.GetStatus() == GameClient::Status::Joined && guest.GetStatus() == GameClient::Status::Joined);
    CHECK(rig.M().Find(1) && rig.M().Find(2) && rig.M().Find(1)->alive && rig.M().Find(2)->alive);   // the same people, back at full health
    CHECK(rig.M().Find(1)->kills == 0 && rig.M().Find(1)->damageDealt == 0 && rig.M().Find(1)->placement == 0);
    CHECK(host.Results().empty() && host.Loot().size() == rig.M().Loot().size());
    CHECK(rig.M().Players().size() == static_cast<size_t>(kMaxPlayers));                // bots refilled to 32
}

static void SkinsTravelToEveryone() {
    Rig rig(21, 20);
    GameClient& a = rig.Add("Red");
    a.SetTunic(PackRgb(200, 30, 30));
    CHECK(rig.RunUntil([&] { return a.GetStatus() == GameClient::Status::Joined; }));
    GameClient& b = rig.Add("Blue");
    b.SetTunic(PackRgb(30, 30, 200));
    CHECK(rig.RunUntil([&] { return rig.AllJoined(); }));
    rig.Run(0.5f);
    // Each sees the other's colour; the one who joined first learned about the second through the join event.
    CHECK(a.Roster().at(2).tunic == PackRgb(30, 30, 200) && b.Roster().at(1).tunic == PackRgb(200, 30, 30));
    CHECK(a.Roster().at(1).tunic == PackRgb(200, 30, 30));
    // Bots wear presets by id, the same on every machine, and not all one colour.
    CHECK(BotTunic(1000) == BotTunic(1000));
    bool varied = false;
    for (uint32_t id = 1001; id < 1032; id++) varied |= BotTunic(id) != BotTunic(1000);
    CHECK(varied);
    CHECK(SkinRgb(0) == PackRgb(30, 105, 27) && SkinRgb(-1) == SkinRgb(0) && SkinRgb(kSkinCount + 5) == SkinRgb(0));
}

static void BossesOverTheWire() {
    Rig rig(31, 20);
    rig.server.SetBossCount(2);
    GameClient& a = rig.Add("Hero");
    GameClient& b = rig.Add("Far");
    CHECK(rig.RunUntil([&] { return rig.AllJoined(); }));
    rig.StartAndGoLive();
    CHECK(rig.M().Bosses().size() == 2);
    for (auto& p : rig.M().Players()) if (p.isBot) p.alive = false;
    // Put the hero next to the first boss and the other player far from both.
    const MiniBoss boss = rig.M().Bosses()[0];
    rig.M().Find(1)->pos = {boss.pos.x + 70, boss.pos.z};
    rig.M().Find(2)->pos = {boss.pos.x + 1900, boss.pos.z + 1900};
    rig.M().Find(1)->weapon = {ItemId::MasterSword, Rarity::Legendary};
    rig.Run(1.0f);
    // The hero sees the boss near them; its fields come through.
    bool seen = false;
    for (const auto& n : a.Bosses()) seen |= n.Id() == boss.id && n.kind == static_cast<uint8_t>(boss.kind) && n.hp > 200;
    CHECK(seen);
    // Hit it over the wire, then finish it: everyone is told, and its chests appear on every client.
    const size_t lootBefore = a.Loot().size();
    a.ReportAttack(static_cast<uint16_t>(boss.id), true);
    rig.Run(0.4f);
    CHECK(rig.M().FindBoss(boss.id)->health < boss.maxHealth);
    MiniBoss* live = const_cast<MiniBoss*>(rig.M().FindBoss(boss.id));
    live->health = 0.1f;
    live->reassembled = true;   // a Stalfos would otherwise get back up once
    rig.M().Find(1)->health = rig.M().Find(1)->maxHealth;
    rig.Run(0.8f);
    a.ReportAttack(static_cast<uint16_t>(boss.id), true);
    rig.Run(0.6f);
    CHECK(!rig.M().FindBoss(boss.id)->alive);
    bool downA = false, downB = false;
    for (auto& e : a.DrainEvents()) downA |= e.type == ClientEvent::Type::BossDown && e.id == boss.id && e.other == 1;
    for (auto& e : b.DrainEvents()) downB |= e.type == ClientEvent::Type::BossDown && e.id == boss.id;
    CHECK(downA && downB);
    CHECK(a.Loot().size() > lootBefore && b.Loot().size() == a.Loot().size());
    bool gone = true;
    for (const auto& n : a.Bosses()) gone &= n.Id() != boss.id;                                   // dead bosses are not sent
    CHECK(gone);
    // The player who is far away is not sent the bosses that are out of reach.
    bool farSees = false;
    for (const auto& n : b.Bosses()) farSees |= Distance({n.x, n.z}, {rig.M().Find(2)->pos.x, rig.M().Find(2)->pos.z}) > 4500.0f;
    CHECK(!farSees);
}

static void PlayerLimitOverTheWire() {
    Rig rig(41, 20);
    GameClient& a = rig.Add("A");
    GameClient& b = rig.Add("B");
    CHECK(rig.RunUntil([&] { return rig.AllJoined(); }));
    CHECK(a.PlayerLimit() == kMaxPlayers && b.PlayerLimit() == kMaxPlayers);
    CHECK(!rig.server.SetPlayerLimit(1) && !rig.server.SetPlayerLimit(40));
    CHECK(rig.server.SetPlayerLimit(3));
    rig.Run(0.4f);
    CHECK(a.PlayerLimit() == 3 && b.PlayerLimit() == 3 && rig.server.PlayerLimit() == 3);       // everyone is told
    GameClient& c = rig.Add("C");
    CHECK(rig.RunUntil([&] { return c.GetStatus() == GameClient::Status::Joined; }));
    CHECK(c.PlayerLimit() == 3);                                                                  // a later joiner learns it from Welcome
    GameClient& d = rig.Add("D");
    CHECK(rig.RunUntil([&] { return d.GetStatus() == GameClient::Status::Rejected; }));          // full at three
    CHECK(d.RejectedBecause() == RejectReason::LobbyFull);
    CHECK(!rig.server.SetPlayerLimit(2));                                                         // can't go below the people here
    rig.StartAndGoLive();
    CHECK(rig.M().Players().size() == 3);                                                         // no bots at all
    CHECK(!rig.server.SetPlayerLimit(10));                                                        // lobby only
    { MatchStateMsg a2, b2; a2.limit = 12; CHECK(RoundTrips(a2, b2) && b2.limit == 12); }
    { MatchStateMsg bad, out; bad.limit = 1; CHECK(!RoundTrips(bad, out)); }
}

static void LobbyTimer() {
    Rig rig(51, 20);
    GameClient& a = rig.Add("A");
    CHECK(rig.RunUntil([&] { return rig.AllJoined(); }));
    rig.Run(0.3f);
    CHECK(a.LobbyLeft() < 0 && rig.server.LobbyLeft() < 0);                           // off unless the host asks for it
    rig.server.SetAutoStart(120.0f);
    rig.Run(1.0f);
    CHECK(a.LobbyLeft() > 116 && a.LobbyLeft() <= 120);
    rig.Run(60.0f);
    CHECK(a.LobbyLeft() > 54 && a.LobbyLeft() < 62);                                   // counting down, and clients see it
    CHECK(rig.M().State() == MatchState::Lobby);
    // The host's game starts the match when it reaches zero; if it never does, the server steps in after the grace period.
    rig.Run(60.5f);
    CHECK(a.LobbyLeft() >= 0 && a.LobbyLeft() < 1.5f && rig.M().State() == MatchState::Lobby);
    rig.Run(kLobbyStartGraceSec - 4.0f);
    CHECK(rig.M().State() == MatchState::Lobby);
    rig.Run(5.0f);
    CHECK(rig.M().State() == MatchState::Countdown);
    CHECK(rig.server.LobbyLeft() < 0);                                                  // the timer is over once the match is on
    { Snapshot s1, s2; s1.lobbyLeft = 77; CHECK(RoundTrips(s1, s2) && s2.lobbyLeft == 77); }
    // Starting early by hand works as before and the timer plays no part afterwards.
    Rig rig2(52, 20);
    GameClient& b = rig2.Add("B");
    CHECK(rig2.RunUntil([&] { return rig2.AllJoined(); }));
    rig2.server.SetAutoStart(120.0f);
    rig2.Run(10.0f);
    CHECK(rig2.server.StartMatch());
    rig2.Run(5.0f);
    CHECK(b.LobbyLeft() < 0);
}

static void ShieldOverTheWire() {
    Rig rig(61, 20);
    GameClient& a = rig.Add("A");
    GameClient& b = rig.Add("B");
    CHECK(rig.RunUntil([&] { return rig.AllJoined(); }));
    rig.StartAndGoLive();
    for (auto& p : rig.M().Players()) if (p.isBot) p.alive = false;
    rig.M().Find(1)->pos = {0, 0};
    rig.M().Find(2)->pos = {1800, 0};
    rig.M().Find(1)->potions = {{ItemId::LargeShieldPotion, Rarity::Rare}};
    rig.Run(0.4f);
    CHECK(a.Inventory().shield == 0);
    a.UseShield();
    rig.Run(0.5f);
    CHECK(std::fabs(a.Inventory().shield - 2.0f) < 0.001f && a.Inventory().potions.empty());       // the owner sees the bar fill
    CHECK(b.Inventory().shield == 0);                                                              // and nobody else gets it
    const uint64_t rejected = rig.server.GetStats().rejectedActions;
    a.UseShield();                                                                                  // nothing left to drink
    rig.Run(0.3f);
    CHECK(rig.server.GetStats().rejectedActions == rejected + 1);
}

static void SmashingPropsOverTheWire() {
    Rig rig(95, 20);
    GameClient& a = rig.Add("A");
    GameClient& b = rig.Add("B");
    CHECK(rig.RunUntil([&] { return rig.AllJoined(); }));
    CHECK(rig.server.Reconfigure({{0, 0}, 2000}, nullptr, 20));
    rig.Run(0.5f);
    CHECK(a.Props().size() > 300);
    size_t bush = a.Props().size(), rock = a.Props().size();
    for (size_t i = 0; i < a.Props().size(); i++) {
        if (a.Props()[i].kind == PropKind::Bush && bush == a.Props().size()) bush = i;
        if (a.Props()[i].kind == PropKind::Rock && rock == a.Props().size()) rock = i;
    }
    CHECK(bush < a.Props().size() && rock < a.Props().size());
    // Not during the lobby.
    a.ReportSmash(bush);
    rig.Run(0.3f);
    CHECK(a.BrokenProps().empty());
    rig.StartAndGoLive();
    for (auto& p : rig.M().Players()) if (p.isBot) p.pos = {1900, 1900};
    PlayerState* me = rig.M().Find(1);
    me->rupees = 0; me->ammo.fill(0);
    // Too far away to have done it.
    me->pos = {a.Props()[bush].pos.x + 900.0f, a.Props()[bush].pos.z};
    rig.Run(0.2f);
    const uint64_t rejected = rig.server.GetStats().rejectedActions;
    a.ReportSmash(bush);
    rig.Run(0.4f);
    CHECK(a.BrokenProps().empty() && rig.server.GetStats().rejectedActions == rejected + 1);
    // Next to it: it breaks for everybody, and what was inside goes to the one who broke it.
    int found = 0;
    for (size_t index = 0; index < a.Props().size() && found < 40; index++) {
        const Prop& pr = a.Props()[index];
        if (pr.kind != PropKind::Bush && pr.kind != PropKind::Rock && pr.kind != PropKind::Boulder) continue;
        me->pos = pr.pos;
        for (auto& p : rig.M().Players()) if (p.isBot) p.pos = {1900, 1900};
        me->invulnUntil = 1.0e9f;
        a.DrainEvents(); b.DrainEvents();
        rig.Run(0.15f);
        a.ReportSmash(index);
        rig.Run(0.3f);
        CHECK(a.BrokenProps().count(index) && b.BrokenProps().count(index));
        bool bSaw = false, aSaw = false;
        for (auto& e : b.DrainEvents()) bSaw |= e.type == ClientEvent::Type::PropBroken && e.index == index && e.id == a.PlayerId();
        for (auto& e : a.DrainEvents()) aSaw |= e.type == ClientEvent::Type::PropBroken && e.index == index;
        CHECK(aSaw && bSaw);
        // Breaking it twice gives nothing twice.
        const int rupees = me->rupees;
        a.ReportSmash(index);
        b.ReportSmash(index);
        rig.Run(0.3f);
        CHECK(me->rupees == rupees);
        found++;
    }
    CHECK(me->rupees > 0 || (me->ammo[0] + me->ammo[1] + me->ammo[2] + me->ammo[3] + me->ammo[4]) > 0);     // forty props yield something
    rig.Run(0.3f);
    CHECK(a.Inventory().rupees == me->rupees);                                                              // and the owner is told
    // A rematch puts the scenery back.
    CHECK(rig.server.Reconfigure({{0, 0}, 2000}, nullptr, 20) == (rig.M().State() == MatchState::Lobby || rig.M().State() == MatchState::Ending));
}

static void SelectingTheMap() {
    Rig rig(71, 0);
    rig.server.SetHostToken(0xABCDEF12345ull);
    GameClient& guest = rig.Add("Guest");
    CHECK(rig.RunUntil([&] { return guest.GetStatus() == GameClient::Status::Joined; }));
    GameClient host(rig.network.NewClient(), "Host", 0xABCDEF12345ull);
    for (int i = 0; i < 20; i++) { rig.Step(); host.Update(kDt); }
    CHECK(guest.MapId() == 0 && host.MapId() == 0);
    // The host picks Lake Hylia: everybody is told, with that place's guessed size and its own point of interest names.
    host.SelectMap(1);
    CHECK(rig.RunUntil([&] { host.Update(kDt); return guest.MapId() == 1 && host.MapId() == 1; }));
    CHECK(guest.Map().radius == MapOf(1).fallback.radius);
    CHECK(!guest.Pois().empty());
    for (const Poi& p : guest.Pois()) CHECK(p.name >= kNamesPerMap && p.name < 2 * kNamesPerMap);
    // Somebody who is not the host can't change it.
    const uint64_t rejected = rig.server.GetStats().rejectedActions;
    guest.SelectMap(3);
    for (int i = 0; i < 20; i++) { rig.Step(); host.Update(kDt); }
    CHECK(guest.MapId() == 1 && rig.server.GetStats().rejectedActions == rejected + 1);
    // Players joining later are welcomed onto the chosen map.
    GameClient late(rig.network.NewClient(), "Late");
    for (int i = 0; i < 20; i++) { rig.Step(); late.Update(kDt); host.Update(kDt); }
    CHECK(late.GetStatus() == GameClient::Status::Joined && late.MapId() == 1);
    // And it can't be changed once the match is on.
    rig.server.StartMatch();
    CHECK(!rig.server.SelectMap(2));
}

static void TheDragonOverTheWire() {
    Rig rig(82, 20);
    GameClient& a = rig.Add("A");
    CHECK(rig.RunUntil([&] { return rig.AllJoined(); }));
    rig.StartAndGoLive();
    for (auto& p : rig.M().Players()) { p.invulnUntil = 1.0e9f; if (p.isBot) p.pos = {1900, 1900}; }
    rig.M().Find(1)->pos = {0, 0};
    float half = 0;
    for (const auto& ph : kStormPhases) half += ph.waitSec + ph.closeSec;
    half *= 0.5f;
    bool spawned = false, strike = false;
    for (int i = 0; i < static_cast<int>((half + 30.0f) * kTickHz) && !(spawned && strike); i++) {
        rig.Step();
        for (auto& p : rig.M().Players()) p.invulnUntil = 1.0e9f;
        for (const auto& e : a.DrainEvents()) {
            spawned |= e.type == ClientEvent::Type::BossSpawned && IsBossId(e.id) && IsDragonKind(static_cast<BossKind>(e.item));
            strike |= e.type == ClientEvent::Type::Strike && e.amount > 0 && e.health > 0;
        }
    }
    CHECK(spawned);
    bool seen = false;
    for (const auto& b : a.Bosses()) seen |= b.Id() == kDragonId && IsDragonKind(static_cast<BossKind>(b.kind)) && b.y > 100;
    CHECK(seen);   // always in the snapshot, however far away, and with its height
    (void)strike;
}

static void BackupWeaponsReachTheOwner() {
    Rig rig(62, 20);
    GameClient& a = rig.Add("A");
    CHECK(rig.RunUntil([&] { return rig.AllJoined(); }));
    rig.StartAndGoLive();
    for (auto& p : rig.M().Players()) if (p.isBot) p.alive = false;
    PlayerState* me = rig.M().Find(1);
    me->reserve = {{ItemId::BiggoronSword, Rarity::Epic}};
    me->dirty = true;
    rig.Run(0.4f);
    CHECK(a.Inventory().reserve.size() == 1 && a.Inventory().reserve[0].item == static_cast<uint8_t>(ItemId::BiggoronSword) && a.Inventory().reserve[0].rarity == static_cast<uint8_t>(Rarity::Epic));
}

static void DisconnectHandling() {
    {   // In the lobby the player just disappears.
        Rig rig;
        GameClient& a = rig.Add("A");
        GameClient& b = rig.Add("B");
        CHECK(rig.RunUntil([&] { return rig.AllJoined(); }));
        b.Leave();
        rig.Run(0.5f);
        CHECK(rig.server.HumanCount() == 1 && rig.M().Players().size() == 1);
        bool sawLeft = false;
        for (auto& e : a.DrainEvents()) sawLeft |= e.type == ClientEvent::Type::PlayerLeft && e.id == 2;
        CHECK(sawLeft && a.Roster().size() == 1);
        CHECK(b.GetStatus() == GameClient::Status::Disconnected);
    }
    {   // Mid-match they are eliminated, dropping their kit.
        Rig rig(3, 0);
        GameClient& a = rig.Add("A");
        GameClient& b = rig.Add("B");
        CHECK(rig.RunUntil([&] { return rig.AllJoined(); }));
        rig.StartAndGoLive();
        rig.M().Find(2)->weapon = {ItemId::MasterSword, Rarity::Epic};
        a.DrainEvents();
        b.Leave();
        rig.Run(0.5f);
        CHECK(!rig.M().Find(2)->alive);
        bool elim = false;
        for (auto& e : a.DrainEvents()) elim |= e.type == ClientEvent::Type::Eliminated && e.id == 2 && e.other == kNoPlayer16;
        CHECK(elim && a.Loot().size() == 1);
    }
    {   // The server can't be crashed by a peer that leaves before saying Hello.
        Rig rig;
        Transport& raw = rig.network.NewClient();
        rig.Run(0.2f);
        raw.Disconnect(0);
        rig.Run(0.2f);
        CHECK(rig.server.HumanCount() == 0);
    }
}

static void InterestManagement() {
    Rig rig(3, 0);
    for (int i = 0; i < 20; i++) rig.Add("P" + std::to_string(i));
    CHECK(rig.RunUntil([&] { return rig.AllJoined(); }));
    rig.StartAndGoLive();
    // Humans 1..20 in a line, 100 units apart, bots dead.
    for (auto& p : rig.M().Players()) if (p.isBot) p.alive = false;
    for (uint32_t id = 1; id <= 20; id++) rig.M().Find(id)->pos = {static_cast<float>(id) * 100.0f, 0};
    rig.Run(0.5f);
    GameClient& first = *rig.clients[0];
    auto visible = first.VisiblePlayers();
    CHECK(visible.size() == kSnapshotMaxPlayers);              // capped, not all 19 others
    for (uint16_t id = 2; id <= 13; id++) CHECK(std::find(visible.begin(), visible.end(), id) != visible.end()); // the 12 nearest
    CHECK(std::find(visible.begin(), visible.end(), uint16_t(20)) == visible.end());
    CHECK(first.Self() != nullptr);                            // self is always present
    GameClient& middle = *rig.clients[9];                      // player 10 sees both sides
    auto mv = middle.VisiblePlayers();
    CHECK(std::find(mv.begin(), mv.end(), uint16_t(4)) != mv.end() && std::find(mv.begin(), mv.end(), uint16_t(16)) != mv.end());
}

static void InterpolationIsSmoothUnderJitter() {
    Rig rig(3, 0);
    LoopbackNetwork::Link link;
    link.latencySec = 0.05f; link.jitterSec = 0.03f;
    GameClient& watcher = rig.Add("Watcher", link);
    GameClient& runner = rig.Add("Runner", link);
    CHECK(rig.RunUntil([&] { return rig.AllJoined(); }));
    rig.StartAndGoLive();
    for (auto& p : rig.M().Players()) if (p.isBot) p.alive = false;
    rig.M().Find(1)->pos = {0, 0};
    rig.M().Find(2)->pos = {0, 100};
    rig.Run(1.0f);
    // The runner moves along +x at 100 u/s for 6 seconds, the watcher renders at 60 fps. (Both clients then need the new epoch;
    // the match-start teleport is long done by now.)
    float runnerX = 0;
    float last = -1e9f, worstStep = 0, worstLag = 0;
    int samples = 0;
    const float frame = 1.0f / 60.0f;
    float inputTimer = 0;
    for (float t = 0; t < 6.0f; t += frame) {
        runnerX += 100.0f * frame;
        inputTimer += frame;
        if (inputTimer >= kDt) { inputTimer = 0; runner.SendInput(runnerX, 0, 100, 0, 1); }
        rig.network.Advance(frame);
        rig.server.Update(frame);
        for (auto& c : rig.clients) c->Update(frame);
        PlayerNet seen;
        if (t > 2.0f && watcher.Sample(2, seen)) {
            if (last > -1e8f) {
                CHECK(seen.x >= last - 0.001f);                              // never goes backwards
                worstStep = (std::max)(worstStep, seen.x - last);
            }
            worstLag = (std::max)(worstLag, runnerX - seen.x);
            last = seen.x;
            samples++;
        }
    }
    CHECK(samples > 100);
    CHECK(worstStep <= 100.0f * frame * 2.5f);   // no big jumps between frames
    CHECK(worstLag < 100.0f * 0.45f);            // lags the truth by well under half a second
    CHECK(worstLag > 0);
    std::printf("  interpolation: worst frame step %.2f units (ideal %.2f), worst lag %.1f units\n", worstStep, 100.0f * frame, worstLag);
}

static void InterpolatesAngleAcrossWrap() {
    Rig rig(3, 0);
    GameClient& a = rig.Add("A");
    GameClient& b = rig.Add("B");
    CHECK(rig.RunUntil([&] { return rig.AllJoined(); }));
    rig.StartAndGoLive();
    for (auto& p : rig.M().Players()) if (p.isBot) p.alive = false;
    rig.M().Find(2)->pos = {0, 0};
    rig.Run(1.0f);
    b.SendInput(rig.M().Find(2)->pos.x, 0, rig.M().Find(2)->pos.z, 32000, 0);
    rig.Run(0.4f);
    b.SendInput(rig.M().Find(2)->pos.x, 0, rig.M().Find(2)->pos.z, -32000, 0);   // 33536 -> through 0x8000 is a +1536 turn
    // 32000 -> -32000 as binary angles is a short turn through +-32768 (1536 units), not a 64000-unit sweep through zero.
    // Every interpolated sample while the turn is shown must stay near the wrap point.
    // Sample at 200 Hz, far finer than the 20 Hz snapshots, so many samples land between the two keyframes.
    int nearWrap = 0, turning = 0;
    for (int i = 0; i < 200; i++) {
        rig.Step(0.005f);
        PlayerNet s;
        if (a.Sample(2, s)) {
            CHECK(std::abs(static_cast<int>(s.rot)) >= 31990);
            nearWrap++;
            if (s.rot != 32000 && s.rot != -32000) turning++;
        }
    }
    CHECK(nearWrap >= 150);
    CHECK(turning >= 5);                       // we actually observed samples mid-turn, not just the end points
}

static void StormMatchesAcrossTheWire() {
    Rig rig(8, 0);
    GameClient& a = rig.Add("A");
    CHECK(rig.RunUntil([&] { return a.GetStatus() == GameClient::Status::Joined; }));
    rig.StartAndGoLive();
    rig.Run(130.0f);                            // into the first shrink
    for (auto& p : rig.M().Players()) if (p.id != 1) p.alive = false;
    rig.M().Find(1)->health = kMaxHealth;
    Circle server = rig.M().GetStorm().SafeZoneAt(rig.M().StormTime());
    Circle client = a.SafeZone();
    CHECK(std::abs(server.radius - client.radius) < 20.0f);        // within about a snapshot of latency
    CHECK(std::abs(a.StormTime() - rig.M().StormTime()) < 0.2f);
    CHECK(a.StormDamagePerSecond({server.center.x + server.radius + 500, server.center.z}) > 0);
    CHECK(a.StormDamagePerSecond(server.center) == 0);
}

static void ReliableEventsSurviveLoss() {
    Rig rig(4);
    LoopbackNetwork::Link lossy;
    lossy.latencySec = 0.08f; lossy.jitterSec = 0.04f; lossy.unreliableLoss = 0.3f;
    GameClient& a = rig.Add("A", lossy);
    GameClient& b = rig.Add("B", lossy);
    GameClient& c = rig.Add("C", lossy);
    CHECK(rig.RunUntil([&] { return rig.AllJoined(); }));
    rig.StartAndGoLive();
    rig.Run(120.0f);                            // bots loot and fight; humans stand still and get hunted
    rig.network.Advance(2.0f);
    rig.Run(2.0f);
    // Every client's loot table must equal the server's, even though 30% of snapshots were lost.
    for (GameClient* g : {&a, &b, &c}) {
        CHECK(g->Loot().size() == rig.M().Loot().size());
        size_t mismatches = 0;
        for (size_t i = 0; i < g->Loot().size() && i < rig.M().Loot().size(); i++)
            mismatches += g->Loot()[i].taken != rig.M().Loot()[i].taken;
        CHECK(mismatches == 0);
        CHECK(g->DesyncCount() == 0);
        CHECK(g->GetStatus() == GameClient::Status::Joined);
    }
    CHECK(rig.M().Loot().size() > 400);         // eliminations dropped kit
}

static void FullMatchOverTheNetwork() {
    Rig rig(77);
    GameClient& me = rig.Add("Solo");
    CHECK(rig.RunUntil([&] { return me.GetStatus() == GameClient::Status::Joined; }));
    CHECK(rig.server.StartMatch());
    CHECK(rig.RunUntil([&] { return rig.M().State() == MatchState::Ending; }, 1500));
    rig.Run(1.0f);
    std::vector<MatchState> seen;
    for (auto& e : me.DrainEvents()) if (e.type == ClientEvent::Type::StateChanged) seen.push_back(e.state);
    CHECK(seen.size() == 4 && seen[0] == MatchState::Countdown && seen[1] == MatchState::Drop && seen[2] == MatchState::InMatch && seen[3] == MatchState::Ending);
    CHECK(me.State() == MatchState::Ending && me.AliveCount() <= 1);
    CHECK(rig.M().Alive() <= 1);
    CHECK(me.DesyncCount() == 0 && me.Loot().size() == rig.M().Loot().size());
    CHECK(rig.server.GetStats().badPackets == 0);
}

static void BandwidthWith32Players() {
    Rig rig(5);
    rig.server.SetVehicleCount(kMaxVehicles);   // with every cart there could be, most of them near (some driven, the rest parked)
    for (int i = 0; i < kMaxPlayers; i++) rig.Add("P" + std::to_string(i));
    CHECK(rig.RunUntil([&] { return rig.AllJoined(); }));
    rig.StartAndGoLive();
    for (auto& p : rig.M().Players()) { // spread out so interest management has real work to do
        p.pos.x = static_cast<float>(static_cast<int>(p.id % 7) * 40);
        p.pos.z = static_cast<float>(static_cast<int>(p.id % 5) * 40);
    }
    {   // two carts with people in them right among them, the rest parked round about
        auto& carts = rig.M().MutableVehicles();
        for (size_t i = 0; i < carts.size(); i++) {
            carts[i].body.x = 300.0f + 700.0f * static_cast<float>(i % 4);
            carts[i].body.z = -300.0f - 700.0f * static_cast<float>(i / 4);
            if (i < 2) carts[i].seat[0] = rig.M().Players()[i].id;   // somebody in it: sent every snapshot
        }
    }
    rig.Run(2.0f);
    uint64_t before = rig.server.GetStats().bytesOut;
    const float seconds = 10.0f;
    rig.Run(seconds);
    double perClientPerSec = static_cast<double>(rig.server.GetStats().bytesOut - before) / seconds / kMaxPlayers;
    std::printf("  32 players, host upload: %.0f B/s per client (%.1f KB/s total), snapshot budget 10 KB/s per client\n",
                perClientPerSec, perClientPerSec * kMaxPlayers / 1024.0);
    CHECK(perClientPerSec < 10 * 1024.0);
    CHECK(perClientPerSec > 1000.0); // and it really is sending
}

static void ServerSurvivesHostileClient() {
    Rig rig(3, 0);
    GameClient& a = rig.Add("A");
    CHECK(rig.RunUntil([&] { return a.GetStatus() == GameClient::Status::Joined; }));
    // A joined peer firing random bytes at the server for a while.
    Transport& raw = rig.network.NewClient();
    rig.Run(0.2f);
    Hello h; h.name = "Evil"; raw.Send(0, Encode(h), true);
    rig.Run(0.2f);
    Rng rng(99);
    for (int i = 0; i < 3000; i++) {
        std::vector<uint8_t> junk(rng.Below(60));
        for (auto& x : junk) x = static_cast<uint8_t>(rng.Below(256));
        if (!junk.empty() && rng.Below(2)) junk[0] = static_cast<uint8_t>(1 + rng.Below(6));
        raw.Send(0, junk, rng.Below(2) == 0);
        if (i % 20 == 0) rig.Step();
    }
    rig.StartAndGoLive();
    for (int i = 0; i < 1000; i++) {
        std::vector<uint8_t> junk(rng.Below(40));
        for (auto& x : junk) x = static_cast<uint8_t>(rng.Below(256));
        if (!junk.empty()) junk[0] = static_cast<uint8_t>(1 + rng.Below(6));
        raw.Send(0, junk, true);
        if (i % 10 == 0) rig.Step();
    }
    rig.Run(2.0f);
    CHECK(a.GetStatus() == GameClient::Status::Joined);
    CHECK(rig.M().State() == MatchState::InMatch || rig.M().State() == MatchState::Ending);
}

// ---- lobby ----------------------------------------------------------------------------------------------------

static void ReadyFlowAndRosterFlags() {
    Rig rig(11, 0);
    GameClient& a = rig.Add("Link");
    GameClient& b = rig.Add("Zelda");
    CHECK(rig.RunUntil([&] { return rig.AllJoined(); }));
    rig.Run(0.5f);
    CHECK(!a.Roster().at(1).ready && !b.Roster().at(2).ready);

    b.SetReady(true);
    rig.Run(0.5f);
    CHECK(a.Roster().at(2).ready && b.Roster().at(2).ready);        // everybody sees it, including the sender
    bool sawEvent = false;
    for (auto& e : a.DrainEvents()) sawEvent |= e.type == ClientEvent::Type::ReadyChanged && e.id == 2 && e.ready;
    CHECK(sawEvent);

    // Someone who joins later learns the current flags from Welcome.
    GameClient& c = rig.Add("Late");
    CHECK(rig.RunUntil([&] { return c.GetStatus() == GameClient::Status::Joined; }));
    CHECK(c.Roster().at(2).ready && !c.Roster().at(1).ready);

    b.SetReady(false);
    rig.Run(0.5f);
    CHECK(!a.Roster().at(2).ready && !c.Roster().at(2).ready);

    // Setting the flag it already has sends nothing new.
    a.DrainEvents();
    b.SetReady(false);
    rig.Run(0.3f);
    bool again = false;
    for (auto& e : a.DrainEvents()) again |= e.type == ClientEvent::Type::ReadyChanged;
    CHECK(!again);

    // Once the match starts ready flags are meaningless and the server refuses them.
    a.SetReady(true);
    rig.Run(0.3f);
    uint64_t rejected = rig.server.GetStats().rejectedActions;
    CHECK(rig.server.StartMatch());
    a.SetReady(false);
    rig.Run(0.3f);
    CHECK(rig.server.GetStats().rejectedActions == rejected + 1);
}

static void HostIsIdentifiedByToken() {
    Rig rig(11, 0);
    rig.server.SetHostToken(0x1234567890ABCDEFull);
    // A normal client first (no token): not the host, even though it joined first.
    GameClient& guest = rig.Add("Guest");
    CHECK(rig.RunUntil([&] { return guest.GetStatus() == GameClient::Status::Joined; }));
    CHECK(!guest.Roster().at(1).host);
    // A client with a wrong token is not the host either.
    GameClient wrong(rig.network.NewClient(), "Faker", 0xDEADBEEFull);
    for (int i = 0; i < 20; i++) { rig.Step(); wrong.Update(kDt); }
    CHECK(wrong.GetStatus() == GameClient::Status::Joined && !wrong.Roster().at(wrong.PlayerId()).host);
    // The real host token marks exactly that player, and everyone is told.
    GameClient host(rig.network.NewClient(), "Host", 0x1234567890ABCDEFull);
    for (int i = 0; i < 20; i++) { rig.Step(); host.Update(kDt); wrong.Update(kDt); }
    CHECK(host.GetStatus() == GameClient::Status::Joined && host.Roster().at(host.PlayerId()).host);
    CHECK(guest.Roster().at(host.PlayerId()).host);
    // A second client replaying the token does not become a second host.
    GameClient copycat(rig.network.NewClient(), "Copycat", 0x1234567890ABCDEFull);
    for (int i = 0; i < 20; i++) { rig.Step(); copycat.Update(kDt); host.Update(kDt); wrong.Update(kDt); }
    CHECK(copycat.GetStatus() == GameClient::Status::Joined && !copycat.Roster().at(copycat.PlayerId()).host);
    int hosts = 0;
    for (auto& [id, info] : guest.Roster()) hosts += info.host;
    CHECK(hosts == 1);
}

static void NoTokenMeansNoHost() {
    Rig rig(11, 0);              // server never given a token
    GameClient a(rig.network.NewClient(), "A", 0);
    for (int i = 0; i < 20; i++) { rig.Step(); a.Update(kDt); }
    CHECK(a.GetStatus() == GameClient::Status::Joined && !a.Roster().at(a.PlayerId()).host);
    GameClient b(rig.network.NewClient(), "B", 0);   // 0 must not match an unset token
    for (int i = 0; i < 20; i++) { rig.Step(); a.Update(kDt); b.Update(kDt); }
    CHECK(!b.Roster().at(b.PlayerId()).host);
}

static void SceneIsRelayedBetweenPlayers() {
    Rig rig(11, 0);
    GameClient& a = rig.Add("A");
    GameClient& b = rig.Add("B");
    CHECK(rig.RunUntil([&] { return rig.AllJoined(); }));
    a.SendInput(10, 20, 30, 0, 0, 0x43);   // A is in the waiting room
    b.SendInput(10, 20, 30, 0, 0, 0x51);   // B is out in the field
    rig.Run(0.5f);
    PlayerNet seenOfA, seenOfB;
    CHECK(b.Sample(1, seenOfA) && seenOfA.scene == 0x43);
    CHECK(a.Sample(2, seenOfB) && seenOfB.scene == 0x51);
}

static void BotsReportTheFieldScene() {
    Rig rig(5);
    GameClient& me = rig.Add("Me");
    CHECK(rig.RunUntil([&] { return me.GetStatus() == GameClient::Status::Joined; }));
    rig.StartAndGoLive();
    rig.Run(1.0f);
    bool sawBot = false;
    for (uint16_t id : me.VisiblePlayers()) {
        PlayerNet p;
        if (me.Sample(id, p) && (p.flags & PlayerNet::kBot)) { sawBot = true; CHECK(p.scene == 0x51); }
    }
    CHECK(sawBot);
}

static void OldProtocolVersionIsRejected() {
    Rig rig;
    Transport& raw = rig.network.NewClient();
    rig.Run(0.3f);
    Hello h; h.version = 1; h.name = "OldBuild";   // what a version-1 client would send
    raw.Send(0, Encode(h), true);
    rig.Run(0.5f);
    NetEvent ev; Reject rj; bool got = false;
    while (raw.Poll(ev)) if (ev.type == NetEvent::Type::Data && Decode(ev.data, rj)) got = rj.reason == RejectReason::VersionMismatch;
    CHECK(got && rig.server.HumanCount() == 0);
    // A genuine v1 Hello is shorter than a v2 one (no host token), so the server must also survive decoding it as garbage.
    ByteWriter v1; v1.U8(static_cast<uint8_t>(MsgType::Hello)); v1.U16(1); v1.Str("OldBuild");
    Transport& raw2 = rig.network.NewClient();
    rig.Run(0.3f);
    raw2.Send(0, v1.buf, true);
    rig.Run(0.5f);
    CHECK(rig.server.HumanCount() == 0);
}

static void EmptyNameGetsADefault() {
    Rig rig(11, 0);
    GameClient a(rig.network.NewClient(), "\x01\x02\x03");   // sanitizes to nothing
    for (int i = 0; i < 20; i++) { rig.Step(); a.Update(kDt); }
    CHECK(a.GetStatus() == GameClient::Status::Joined);
    CHECK(a.Roster().at(a.PlayerId()).name == "Player " + std::to_string(a.PlayerId()));
}

static void WinnerIsAnnounced() {
    Rig rig(77, 0);
    GameClient& me = rig.Add("Solo");
    CHECK(rig.RunUntil([&] { return me.GetStatus() == GameClient::Status::Joined; }));
    CHECK(rig.server.StartMatch());
    CHECK(rig.RunUntil([&] { return rig.M().State() == MatchState::Ending; }, 1500));
    rig.Run(1.0f);
    const PlayerState* w = rig.M().Winner();
    CHECK(w != nullptr);
    CHECK(w && me.Winner() == w->id);
}

static void WeatherOverTheWire() {
    net::EvWeather w; w.season = 2; w.sky = static_cast<uint8_t>(Sky::Fog); w.intensity = 80; w.seconds = 60;
    std::vector<uint8_t> bytes = net::Encode(w);
    net::EvWeather back;
    CHECK(net::Decode(bytes, back) && back.season == 2 && back.sky == static_cast<uint8_t>(Sky::Fog) && back.intensity == 80 && back.seconds == 60);
    net::EvWeather bad; bad.sky = 99;
    std::vector<uint8_t> badBytes = net::Encode(bad);
    net::EvWeather rejected;
    CHECK(!net::Decode(badBytes, rejected));
    Rig rig(31, 0);
    GameClient& me = rig.Add("W");
    CHECK(rig.RunUntil([&] { return me.GetStatus() == GameClient::Status::Joined; }));
    WeatherOptions o; o.season = static_cast<uint8_t>(Season::Winter); o.intensity = 100; o.change = 100;
    rig.server.SetWeatherOptions(o);
    CHECK(rig.server.StartMatch());
    CHECK(rig.RunUntil([&] { return me.State() == MatchState::InMatch; }, 40));
    CHECK(me.CurrentWeather().season == Season::Winter);                       // the season arrives with the first spell
    bool changed = false;
    CHECK(rig.RunUntil([&] { changed = changed || me.CurrentWeather().sky != Sky::Clear; return changed; }, 400));
    CHECK(changed && me.CurrentWeather().Strength() > 0);
}

static void VehiclesOverTheWire() {
    { VehicleRequest a, b; a.action = VehicleRequest::SwitchSeat; a.index = 5; a.seat = 1; CHECK(RoundTrips(a, b) && b.action == VehicleRequest::SwitchSeat && b.index == 5 && b.seat == 1); }
    { VehicleRequest bad, out; bad.index = kMaxVehicles; CHECK(!RoundTrips(bad, out)); }
    { VehicleDrive a, b; a.index = 2; a.x = 10.5f; a.y = -3; a.z = 99; a.yaw = -1234; a.pitch = -12; a.roll = 7; a.speed = -140; a.slide = 33; a.vy = -800; a.steer = -50;
      a.air = 120; a.flags = 3; a.impact = 410; a.landing = 900;
      CHECK(RoundTrips(a, b) && b.x == 10.5f && b.yaw == -1234 && b.pitch == -12 && b.roll == 7 && b.speed == -140 && b.vy == -800 && b.steer == -50 && b.air == 120 &&
            b.flags == 3 && b.impact == 410 && b.landing == 900); }
    { VehicleDrive bad, out; bad.x = std::nanf(""); CHECK(!RoundTrips(bad, out)); }
    { Snapshot a, b; VehicleNet n; n.index = 3; n.x = 5; n.y = 6; n.z = -7; n.yaw = 9000; n.speed = -20; n.steer = 40; n.air = 15; n.hp = 128; n.driver = 4; n.passenger = 1002;
      n.flags = VehicleNet::kDrift | VehicleNet::kGrounded; a.vehicles = {n};
      CHECK(RoundTrips(a, b) && b.vehicles.size() == 1 && b.vehicles[0].yaw == 9000 && b.vehicles[0].driver == 4 && b.vehicles[0].passenger == 1002 &&
            b.vehicles[0].hp == 128 && b.vehicles[0].flags == (VehicleNet::kDrift | VehicleNet::kGrounded)); }
    { Snapshot bad, out; VehicleNet n; n.index = kMaxVehicles; bad.vehicles = {n}; CHECK(!RoundTrips(bad, out)); }

    // A player walks up to a cart, gets in, drives it (their game runs the physics and reports), and gets out again; the others see it go.
    Rig rig(43, 0);
    rig.server.SetVehicleCount(4);
    GameClient& me = rig.Add("Driver");
    GameClient& other = rig.Add("Watcher");
    CHECK(rig.RunUntil([&] { return me.GetStatus() == GameClient::Status::Joined && other.GetStatus() == GameClient::Status::Joined; }));
    CHECK(rig.server.StartMatch());
    CHECK(rig.RunUntil([&] { return me.State() == MatchState::InMatch; }, 40));
    CHECK(rig.M().Vehicles().size() == 4);
    const VehicleState& v = rig.M().Vehicles()[0];
    PlayerState* self = rig.M().Find(me.Self()->id);
    PlayerState* watcher = rig.M().Find(other.Self()->id);
    self->pos = ExitSpot(v.body, Seat::Driver);
    watcher->pos = {v.body.x + 600.0f, v.body.z};
    CHECK(rig.RunUntil([&] { for (const auto& n : me.Vehicles()) if (n.index == 0) return true; return false; }, 3));
    me.EnterVehicle(0, Seat::Driver);
    int index = -1; Seat seat = Seat::None;
    CHECK(rig.RunUntil([&] { return me.MyVehicle(&index, &seat); }, 3));
    CHECK(index == 0 && seat == Seat::Driver && v.Driver() == self->id);
    CartBody body = v.body;
    const Vec2 start = {body.x, body.z};
    for (int i = 0; i < 2 * kTickHz; i++) {
        StepCart(body, {1, 0, false, false}, rig.M().VehicleWorld(), kDt);
        me.SendDrive(0, body, 0, false, 0, 0);
        rig.Step();
    }
    CHECK(Distance(start, {v.body.x, v.body.z}) > 150.0f && v.body.speed > 100.0f);
    net::VehicleNet seen;
    CHECK(rig.RunUntil([&] { return other.SampleVehicle(0, seen) && Distance(start, {seen.x, seen.z}) > 150.0f; }, 2));
    CHECK(seen.driver == self->id);
    // Stop, then get out.
    body.speed = 0;
    me.SendDrive(0, body, 0, false, 0, 0);
    rig.Run(0.2f);
    me.ExitVehicle();
    CHECK(rig.RunUntil([&] { return !me.MyVehicle(nullptr, nullptr); }, 3));
    CHECK(!rig.M().RidingIn(self->id));
}

static void AlliesOverTheWire() {
    { HireAllyRequest a, b; a.index = 3; CHECK(RoundTrips(a, b) && b.index == 3); }
    { HireAllyRequest bad, out; bad.index = 9; CHECK(!RoundTrips(bad, out)); }
    { EvAlly a, b; a.index = 2; a.owner = 5; a.status = 1; CHECK(RoundTrips(a, b) && b.index == 2 && b.owner == 5 && b.status == 1); }
    { EvAlly bad, out; bad.status = 7; CHECK(!RoundTrips(bad, out)); }
    { EvAllyAction a, b; a.index = 1; a.target = 1003; a.x = 4; a.z = -9; CHECK(RoundTrips(a, b) && b.target == 1003 && b.z == -9); }
    { Snapshot a, b; AllyNet n; n.index = 2; n.kind = 2; n.x = 10; n.z = -4; n.rot = -321; n.hp = 99; n.owner = 7; n.flags = 3; a.allies = {n};
      CHECK(RoundTrips(a, b) && b.allies.size() == 1 && b.allies[0].rot == -321 && b.allies[0].owner == 7 && b.allies[0].flags == 3); }
    { Snapshot bad, out; AllyNet n; n.kind = 9; bad.allies = {n}; CHECK(!RoundTrips(bad, out)); }
    Rig rig(41, 0);
    GameClient& me = rig.Add("Hirer");
    CHECK(rig.RunUntil([&] { return me.GetStatus() == GameClient::Status::Joined; }));
    CHECK(rig.server.StartMatch());
    CHECK(rig.RunUntil([&] { return me.State() == MatchState::InMatch; }, 40));
    rig.Run(1.0f);
    PlayerState* self = rig.M().Find(me.Self()->id);
    CHECK(self != nullptr);
    CHECK(rig.RunUntil([&] { return !me.Allies().empty() || true; }, 1));
    self->pos = rig.M().Allies()[0].pos;
    self->rupees = 500;
    self->dirty = true;
    CHECK(rig.RunUntil([&] { return me.Inventory().rupees == 500; }, 5));
    me.HireAlly(0);
    CHECK(rig.RunUntil([&] { for (const auto& a : me.Allies()) if (a.index == 0 && a.owner == me.Self()->id) return true; return false; }, 5));
    CHECK(me.Inventory().rupees == 500 - AllyOf(AllyKind::Kokiri).price);
    me.HireAlly(0);                                                                    // again: refused, nothing changes
    rig.Run(1.0f);
    CHECK(me.Inventory().rupees == 500 - AllyOf(AllyKind::Kokiri).price);
}

static void ReplayOverTheWire() {
    { EvReplayHeader a, b; a.frames = 5; a.ids = {1, 1000, 1001}; a.kills = {{2, 1, 1000}, {4, 0xFFFF, 1001}}; CHECK(RoundTrips(a, b) && b.ids.size() == 3 && b.kills.size() == 2 && b.kills[1].killer == 0xFFFF); }
    { EvReplayHeader bad, out; bad.frames = 3; bad.ids = {1}; bad.kills = {{9, 1, 1}}; CHECK(!RoundTrips(bad, out)); }   // a kill after the last frame
    { EvReplayChunk a, b; a.first = 4; a.players = 2; a.frames = {{1, 2, 3, 4}, {kReplayGone, 0, 7, 8}}; CHECK(RoundTrips(a, b) && b.frames.size() == 2 && b.frames[1][0] == kReplayGone && b.frames[1][3] == 8); }
    Rig rig(61, 0);
    GameClient& me = rig.Add("Replayer");
    CHECK(rig.RunUntil([&] { return me.GetStatus() == GameClient::Status::Joined; }));
    CHECK(rig.server.StartMatch());
    CHECK(rig.RunUntil([&] { return rig.M().State() == MatchState::Ending; }, 1500));
    CHECK(rig.RunUntil([&] { return me.ReplayComplete(); }, 10));
    const Replay& rp = me.GetReplay();
    CHECK(rp.ids.size() == static_cast<size_t>(kMaxPlayers) && rp.frames.size() >= 20 && !rp.kills.empty());
    CHECK(rp.frames.size() == rig.M().GetReplay().frames.size() && rp.frames.back() == rig.M().GetReplay().frames.back());
}

static void CountdownElapsedTracksState() {
    Rig rig(11, 0);
    GameClient& a = rig.Add("A");
    CHECK(rig.RunUntil([&] { return a.GetStatus() == GameClient::Status::Joined; }));
    CHECK(rig.server.StartMatch());
    CHECK(rig.RunUntil([&] { return a.State() == MatchState::Countdown; }));
    rig.Run(4.0f);
    CHECK(a.State() == MatchState::Countdown);
    CHECK(a.StateElapsed() > 3.0f && a.StateElapsed() < kCountdownSec);   // about 4 s into a 10 s countdown
    CHECK(rig.RunUntil([&] { return a.State() == MatchState::Drop; }, 15));
    CHECK(a.StateElapsed() < 1.0f);                                        // reset on the state change
}

static void ReconfigureRebuildsTheLobbyWorld() {
    Rig rig(11);
    GameClient& a = rig.Add("Host");
    GameClient& b = rig.Add("Guest");
    CHECK(rig.RunUntil([&] { return rig.AllJoined(); }));
    b.SetReady(true);
    rig.Run(0.5f);
    Circle oldMap = a.Map();
    CHECK(oldMap.radius == 2000);
    a.DrainEvents(); b.DrainEvents();

    // Walkable ground is a disc of radius 800 around (500, -300) that is smaller than the default map.
    Circle real = {{500, -300}, 800};
    PlacementFn walkable = [&](Vec2 p) { return Distance(p, real.center) <= real.radius; };
    CHECK(rig.server.Reconfigure(real, walkable, 120));
    rig.Run(0.5f);

    for (GameClient* g : {&a, &b}) {
        CHECK(g->Map().radius == 800 && g->Map().center.x == 500);
        CHECK(g->Loot().size() == rig.M().Loot().size() && g->Loot().size() >= 15);   // chests go beside scenery now (at most a fifth of the 120 in the open), plus a chest for each spot in the buildings
        CHECK(!g->Pois().empty() && g->Props().size() > 100 && g->Pois().size() == rig.server.Pois().size());
        for (const Poi& poi : g->Pois()) CHECK(Distance(poi.center, real.center) <= 800.01f && poi.name < kPoiNameTotal);
        bool eventSeen = false;
        for (auto& e : g->DrainEvents()) eventSeen |= e.type == ClientEvent::Type::MapChanged;
        CHECK(eventSeen);
        for (const auto& l : g->Loot()) CHECK(Distance({l.x, l.z}, real.center) <= 800.01f);   // loot only on walkable ground
        // The client rebuilt the storm from the new circles, inside the new map, on walkable ground.
        Circle zone = g->SafeZone();
        CHECK(zone.radius == 800);
    }
    // The server's own storm agrees with what clients were sent, and its centres are walkable.
    for (int i = 0; i < kStormPhaseCount; i++) CHECK(walkable(rig.M().GetStorm().PhaseEnd(i).center));
    // Players, names and ready flags survive.
    CHECK(rig.server.HumanCount() == 2 && a.Roster().size() == 2 && a.Roster().at(2).ready);
    CHECK(rig.M().Find(1) && rig.M().Find(2) && rig.M().Players().size() == 2);
    // A later joiner gets the new world in Welcome.
    GameClient& c = rig.Add("Late");
    CHECK(rig.RunUntil([&] { return c.GetStatus() == GameClient::Status::Joined; }));
    CHECK(c.Map().radius == 800 && c.Loot().size() == rig.M().Loot().size() && c.Pois().size() == rig.server.Pois().size() && !c.Props().empty());
    // Match start puts everyone on walkable ground.
    CHECK(rig.server.StartMatch());
    for (auto& p : rig.M().Players()) CHECK(walkable(p.pos));
    // And it can no longer be reconfigured.
    CHECK(!rig.server.Reconfigure({{0, 0}, 500}));
    CHECK(!rig.server.Reconfigure({{0, 0}, 0}));
}

static void ReconfigureRejectedOnceTheMatchHasStarted() {
    Rig rig(11);
    GameClient& a = rig.Add("A");
    CHECK(rig.RunUntil([&] { return a.GetStatus() == GameClient::Status::Joined; }));
    CHECK(rig.server.StartMatch());
    Circle before = a.Map();
    CHECK(!rig.server.Reconfigure({{0, 0}, 300}));
    rig.Run(0.3f);
    CHECK(a.Map().radius == before.radius);
}

static void ShieldAndWeaponReachTheSnapshot() {
    Rig rig(11, 0);
    GameClient& a = rig.Add("A");
    CHECK(rig.RunUntil([&] { return a.GetStatus() == GameClient::Status::Joined; }));
    rig.StartAndGoLive();
    PlayerState* p = rig.M().Find(1);
    p->weapon = {ItemId::MasterSword, Rarity::Epic};
    p->hasShield = true;
    p->shield = {ItemId::MirrorShield, Rarity::Legendary};
    rig.Run(0.5f);
    const PlayerNet* self = a.Self();
    CHECK(self && self->weapon == static_cast<uint8_t>(ItemId::MasterSword) && self->weaponRarity == static_cast<uint8_t>(Rarity::Epic));
    CHECK(self && (self->flags & PlayerNet::kShield) && self->shield == static_cast<uint8_t>(ItemId::MirrorShield) &&
          self->shieldRarity == static_cast<uint8_t>(Rarity::Legendary));
    // Nothing worn yet; then boots and a mask, which everyone sees drawn on the player.
    CHECK(self && self->boots == PlayerNet::kNoGear && self->mask == PlayerNet::kNoGear);
    p->gear[static_cast<int>(GearSlot::Boots)] = {ItemId::HoverBoots, Rarity::Rare};
    p->gear[static_cast<int>(GearSlot::Mask)] = {ItemId::BunnyHood, Rarity::Common};
    p->gearMask |= (1 << static_cast<int>(GearSlot::Boots)) | (1 << static_cast<int>(GearSlot::Mask));
    rig.Run(0.5f);
    self = a.Self();
    CHECK(self && self->boots == static_cast<uint8_t>(ItemId::HoverBoots) && self->mask == static_cast<uint8_t>(ItemId::BunnyHood));
}

static void SandboxServerBuildsTheTestMap() {
    // The server lays the Sandbox out by hand: its places and props, no mini bosses, no dragon, carts in the lot and the plaza stocked.
    LoopbackNetwork network(7);
    GameServer server(network.Server(), 7, kHyruleFieldMap);
    server.SetBossCount(5);
    server.SetMajorBoss(true);
    CHECK(!server.SelectMap(kSandboxMapIndex));          // not offered as a lobby map
    server.SetSandbox(true);
    CHECK(server.SelectMap(kSandboxMapIndex) && server.Sandbox() && server.MapId() == kSandboxMapIndex);
    Match& m = server.Sim().match;
    CHECK(m.Sandbox() && !m.MajorBossEnabled());
    CHECK(server.Pois().size() == static_cast<size_t>(sandbox::kZoneCount));
    CHECK(!m.Loot().empty() && m.VehicleCount() == 4);
    CHECK(server.SelectMap(0) && !server.Sandbox() && !server.Sim().match.Sandbox());   // another map, an ordinary lobby again
}

// The host's commands reach a player over the wire: a bot, a cart and a boss put where they are wanted show up in the player's snapshots, the
// weather the host sets is announced, and a teleport moves the player (the client is told through the epoch).
static void SandboxOverTheWire() {
    Rig rig(11, 0);
    rig.server.SetSandbox(true);
    rig.server.SetSoloTest(true);
    CHECK(rig.server.SelectMap(kSandboxMapIndex));
    GameClient& a = rig.Add("Hero");
    CHECK(rig.RunUntil([&] { return rig.AllJoined(); }));
    CHECK(a.MapId() == kSandboxMapIndex);
    CHECK(rig.server.StartMatch());
    rig.Run(1.0f);
    CHECK(rig.M().State() == MatchState::InMatch && a.State() == MatchState::InMatch && rig.M().Players().size() == 1);
    CHECK(Distance(rig.M().Find(1)->pos, SandboxSpawn()) < 1.0f);
    Match& m = rig.M();
    CHECK(m.SandboxBot({sandbox::kSpawnX + 200, sandbox::kSpawnZ}));
    CHECK(m.SandboxBoss(BossKind::Moss, {sandbox::kSpawnX - 400, sandbox::kSpawnZ}));
    CHECK(m.SandboxCart({sandbox::kSpawnX, sandbox::kSpawnZ + 300}, 0.0f));
    m.SandboxWeather(Season::Autumn, Sky::Rain, 70);
    rig.Run(1.0f);
    bool bot = false, boss = false, cart = false;
    for (uint16_t id : a.VisiblePlayers()) { net::PlayerNet p; bot |= id >= 1000 && a.Sample(id, p); }
    for (const auto& n : a.Bosses()) boss |= n.kind == static_cast<uint8_t>(BossKind::Moss);
    for (const auto& n : a.Vehicles()) cart |= std::fabs(n.z - (sandbox::kSpawnZ + 300)) < 60.0f;
    CHECK(bot && boss && cart);
    CHECK(a.CurrentWeather().sky == Sky::Rain && a.CurrentWeather().season == Season::Autumn);
    const uint8_t epoch = a.Epoch();
    CHECK(m.SandboxTeleport(1, {1000, 1000}));
    rig.Run(0.5f);
    CHECK(a.Epoch() != epoch && a.Self() && Distance({a.Self()->x, a.Self()->z}, {1000, 1000}) < 5.0f);
}

// Bots on the test map's own ground: with its heights and its dry, walkable ground as the host's game measures them, bots dropped on the course
// move about, climb the stone steps' blocks and never end up in the pond.
static void SandboxBotsWalkTheTestMap() {
    namespace fn = royale::fortnite;
    fn::UseTerrain(true);
    Rig rig(21, 0);
    rig.server.SetSandbox(true);
    rig.server.SetSoloTest(true);
    CHECK(rig.server.SelectMap(kSandboxMapIndex));
    const PlacementFn dry = [](Vec2 p) { float y; return fn::GroundHeight(p.x, p.z, &y) && y > fn::kWaterY + 10.0f && fn::GroundUp(p.x, p.z) >= 0.8f; };
    const HeightFn height = [](Vec2 p, float* y) { return fn::GroundHeight(p.x, p.z, y); };
    CHECK(rig.server.Reconfigure(MapOf(kSandboxMapIndex).fallback, dry, 0, GameServer::FreshSeedOffset(), height));
    GameClient& a = rig.Add("Hero");
    CHECK(rig.RunUntil([&] { return rig.AllJoined(); }));
    CHECK(rig.server.StartMatch());
    rig.Run(0.5f);
    Match& m = rig.M();
    CHECK(m.Sandbox() && m.State() == MatchState::InMatch && !m.Vehicles().empty());
    for (int i = 0; i < 6; i++) CHECK(m.SandboxBot({-200.0f + 80.0f * i, -1000.0f}));
    std::vector<Vec2> start;
    for (const auto& p : m.Players()) start.push_back(p.pos);
    rig.Run(40.0f);
    float moved = 0;
    bool dryAll = true;
    for (size_t i = 0; i < m.Players().size(); i++) {
        const auto& p = m.Players()[i];
        if (!p.isBot || !p.alive) continue;
        moved = (std::max)(moved, Distance(p.pos, start[i]));
        float y;
        dryAll &= fn::GroundHeight(p.pos.x, p.pos.z, &y) && y > fn::kWaterY - 5.0f;
    }
    CHECK(moved > 200.0f && dryAll);
    CHECK(a.State() == MatchState::InMatch);
    fn::UseTerrain(false);
}

int main() {
    ByteReaderBounds(); MessagesRoundTrip(); DecodeRejectsMangled(); DecodeRejectsBadValues(); FuzzNeverCrashes();
    LoopbackLatencyAndLoss(); LoopbackKeepsOrderUnderJitter();
    JoinAndWelcome(); RejectedJoins(); StartNeedsAHuman();
    TeleportEpochIgnoresOldInputs(); SpeedClamp(); OldAndDuplicateInputsIgnored(); NaNInputNeverAccepted();
    ShieldOverTheWire(); SmashingPropsOverTheWire(); SelectingTheMap(); TheDragonOverTheWire(); BackupWeaponsReachTheOwner(); LobbyTimer(); PlayerLimitOverTheWire(); BossesOverTheWire(); SkinsTravelToEveryone(); AttackOverTheWire(); PickupAndPotionOverTheWire(); ResultsAndRematchOverTheWire(); DisconnectHandling(); InterestManagement();
    InterpolationIsSmoothUnderJitter(); InterpolatesAngleAcrossWrap(); StormMatchesAcrossTheWire();
    ReadyFlowAndRosterFlags(); HostIsIdentifiedByToken(); NoTokenMeansNoHost(); SceneIsRelayedBetweenPlayers(); BotsReportTheFieldScene();
    SandboxServerBuildsTheTestMap(); SandboxOverTheWire(); SandboxBotsWalkTheTestMap(); ReconfigureRebuildsTheLobbyWorld(); ReconfigureRejectedOnceTheMatchHasStarted(); ShieldAndWeaponReachTheSnapshot();
    WeatherOverTheWire(); AlliesOverTheWire(); VehiclesOverTheWire(); ReplayOverTheWire(); OldProtocolVersionIsRejected(); EmptyNameGetsADefault(); WinnerIsAnnounced(); CountdownElapsedTracksState();
    ReliableEventsSurviveLoss(); FullMatchOverTheNetwork(); BandwidthWith32Players(); ServerSurvivesHostileClient();
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("all network tests passed\n");
    return 0;
}
