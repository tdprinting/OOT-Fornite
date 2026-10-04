// Real-UDP tests: the same GameServer and GameClient as net_tests.cpp, but over ENet sockets on localhost.
#include "enet_transport.h"
#include "game_client.h"
#include "game_server.h"
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

using namespace royale;
using namespace royale::net;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static Circle MapCircle() { return {{0, 0}, 2000.0f}; }

// Game time advances in big steps (so a 15 s countdown takes well under a second of wall time) while ENet runs on real
// time. Each Step also sleeps a moment so packets actually make the round trip over the loopback interface.
struct UdpRig {
    std::unique_ptr<ENetTransport> hostTransport;
    std::unique_ptr<GameServer> server;
    std::vector<std::unique_ptr<ENetTransport>> clientTransports;
    std::vector<std::unique_ptr<GameClient>> clients;

    explicit UdpRig(int loot = 400) {
        std::string err;
        hostTransport = ENetTransport::Host(0, kMaxPlayers, &err);
        if (!hostTransport) { std::printf("cannot host: %s\n", err.c_str()); std::exit(2); }
        server = std::make_unique<GameServer>(*hostTransport, 5, MapCircle(), loot);
    }
    GameClient& Add(const std::string& name) {
        std::string err;
        clientTransports.push_back(ENetTransport::Connect("127.0.0.1", hostTransport->Port(), &err));
        if (!clientTransports.back()) { std::printf("cannot connect: %s\n", err.c_str()); std::exit(2); }
        clients.push_back(std::make_unique<GameClient>(*clientTransports.back(), name));
        return *clients.back();
    }
    void Step(float dt = 0.05f) {
        server->Update(dt);
        for (auto& c : clients) c->Update(dt);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    template <class F>
    bool RunUntil(F cond, float maxWallSeconds = 8) {
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count() < maxWallSeconds) {
            if (cond()) return true;
            Step();
        }
        return cond();
    }
    void RunFor(float wallSeconds) {
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count() < wallSeconds) Step();
    }
    Match& M() { return server->Sim().match; }
};

static void HostingTwiceOnOnePortFails() {
    std::string err;
    auto first = ENetTransport::Host(0, 4, &err);
    CHECK(first && first->Port() != 0);
    auto second = ENetTransport::Host(first->Port(), 4, &err);
    CHECK(!second && !err.empty());
    // After the first host goes away the port is free again.
    uint16_t port = first->Port();
    first.reset();
    auto third = ENetTransport::Host(port, 4, &err);
    CHECK(third != nullptr);
}

static void UnresolvableHostFails() {
    std::string err;
    auto t = ENetTransport::Connect("this-host-does-not-exist.invalid", 7777, &err);
    CHECK(!t && !err.empty());
}

static void ConnectingToNobodyEndsInDisconnected() {
    uint16_t freePort;
    {
        auto h = ENetTransport::Host(0, 1);
        freePort = h->Port();
    }
    auto t = ENetTransport::Connect("127.0.0.1", freePort);
    CHECK(t != nullptr);
    GameClient client(*t, "Lonely");
    auto start = std::chrono::steady_clock::now();
    bool failed = false;
    while (std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count() < 10) {
        client.Update(0.02f);
        if (client.GetStatus() == GameClient::Status::Disconnected) { failed = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CHECK(failed);
    CHECK(client.GetStatus() == GameClient::Status::Disconnected);
}

static void JoinPlayAndLeaveOverUdp() {
    UdpRig rig;
    GameClient& a = rig.Add("Link");
    GameClient& b = rig.Add("Zelda");
    CHECK(rig.RunUntil([&] { return a.GetStatus() == GameClient::Status::Joined && b.GetStatus() == GameClient::Status::Joined; }));
    CHECK(a.PlayerId() != b.PlayerId() && a.PlayerId() >= 1 && b.PlayerId() >= 1);
    // The Welcome carries all 400 loot entries (about 4.5 KB): it has to survive ENet's fragmentation intact.
    CHECK(a.Loot().size() == 400 && b.Loot().size() == 400);
    for (size_t i = 0; i < 400; i++) {
        const auto& s = rig.M().Loot()[i].spawn;
        CHECK(a.Loot()[i].x == s.pos.x && a.Loot()[i].item == static_cast<uint8_t>(s.item));
    }
    CHECK(rig.RunUntil([&] { return a.Roster().size() == 2 && b.Roster().size() == 2; }));
    CHECK(rig.RunUntil([&] { return a.LastSnapshotTick() > 3; }));        // snapshots are flowing in the lobby

    // Start, wait for the match to go live (virtual time).
    CHECK(rig.server->StartMatch());
    CHECK(rig.RunUntil([&] { return rig.M().State() == MatchState::InMatch; }, 15));
    CHECK(rig.RunUntil([&] { return a.State() == MatchState::InMatch && a.Self() && b.Epoch() == 1; }));
    for (auto& p : rig.M().Players()) if (p.isBot) p.alive = false;
    uint16_t ia = a.PlayerId(), ib = b.PlayerId();
    rig.M().Find(ia)->pos = {0, 0};
    rig.M().Find(ib)->pos = {40, 0};
    rig.M().Find(ia)->weapon = {ItemId::MasterSword, Rarity::Epic};
    rig.M().Find(ib)->health = 0.5f;
    rig.RunFor(0.3f);

    // Movement flows client to server and back out to the other client over UDP.
    a.SendInput(10, 0, 0, 100, 3);
    CHECK(rig.RunUntil([&] { return rig.M().Find(ia)->pos.x == 10 && rig.M().Find(ia)->rot == 100; }));
    PlayerNet seen;
    CHECK(rig.RunUntil([&] { return b.Sample(ia, seen) && std::abs(seen.x - 10) < 0.5f; }));

    // A hit goes through, kills, and the elimination reaches both players.
    a.ReportAttack(ib, true);
    CHECK(rig.RunUntil([&] { return !rig.M().Find(ib)->alive; }));
    bool aElim = false, bElim = false;
    CHECK(rig.RunUntil([&] {
        for (auto& e : a.DrainEvents()) aElim |= e.type == ClientEvent::Type::Eliminated && e.id == ib && e.other == ia;
        for (auto& e : b.DrainEvents()) bElim |= e.type == ClientEvent::Type::Eliminated && e.id == ib;
        return aElim && bElim;
    }));
    CHECK(rig.RunUntil([&] { return rig.M().State() == MatchState::Ending; }));   // only A alive: match over
    CHECK(rig.RunUntil([&] { return a.State() == MatchState::Ending; }));
    CHECK(a.Loot().size() == rig.M().Loot().size());                               // dropped kit synced

    // A leaves: the server removes the player.
    a.Leave();
    CHECK(rig.RunUntil([&] { return a.GetStatus() == GameClient::Status::Disconnected; }));
    CHECK(rig.RunUntil([&] { return rig.server->HumanCount() == 1; }));
    CHECK(rig.server->GetStats().badPackets == 0);
}

static void RejectDeliveredBeforeDisconnect() {
    UdpRig rig(10);
    GameClient& a = rig.Add("A");
    CHECK(rig.RunUntil([&] { return a.GetStatus() == GameClient::Status::Joined; }));
    CHECK(rig.server->StartMatch());
    GameClient& late = rig.Add("Late");
    CHECK(rig.RunUntil([&] { return late.GetStatus() == GameClient::Status::Rejected; }));
    CHECK(late.RejectedBecause() == RejectReason::MatchInProgress);   // the Reject got through before the link closed
    CHECK(rig.RunUntil([&] { return rig.server->HumanCount() == 1; }));
}

static void ThirtyTwoPlayersOverUdp() {
    UdpRig rig(100);
    for (int i = 0; i < kMaxPlayers; i++) rig.Add("P" + std::to_string(i));
    bool all = rig.RunUntil([&] {
        for (auto& c : rig.clients) if (c->GetStatus() != GameClient::Status::Joined) return false;
        return true;
    }, 15);
    CHECK(all);
    CHECK(rig.server->HumanCount() == kMaxPlayers);
    CHECK(rig.server->StartMatch());
    CHECK(rig.RunUntil([&] { return rig.M().State() == MatchState::InMatch; }, 20));
    for (auto& p : rig.M().Players()) { p.pos.x = static_cast<float>(p.id % 8) * 30; p.pos.z = static_cast<float>(p.id % 4) * 30; }
    rig.RunFor(1.0f);
    int withNeighbours = 0;
    for (auto& c : rig.clients) if (c->VisiblePlayers().size() == kSnapshotMaxPlayers) withNeighbours++;
    CHECK(withNeighbours == kMaxPlayers);
    CHECK(rig.server->GetStats().badPackets == 0);
}

int main() {
    HostingTwiceOnOnePortFails(); UnresolvableHostFails(); ConnectingToNobodyEndsInDisconnected();
    JoinPlayAndLeaveOverUdp(); RejectDeliveredBeforeDisconnect(); ThirtyTwoPlayersOverUdp();
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("all UDP tests passed\n");
    return 0;
}
