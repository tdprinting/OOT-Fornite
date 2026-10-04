// RoyaleSession: the host/join layer the game's menu drives. Real UDP on localhost, two sessions in one process.
#include "RoyaleSession.h"
#include <chrono>
#include <cstdio>
#include <thread>

using namespace royale;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void Pump(std::vector<RoyaleSession*> sessions, float dt = 0.05f) {
    for (auto* s : sessions) s->Update(dt);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
}
template <class F>
static bool PumpUntil(std::vector<RoyaleSession*> sessions, F cond, float maxWall = 8) {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count() < maxWall) {
        if (cond()) return true;
        Pump(sessions);
    }
    return cond();
}

static void IdleSessionIsHarmless() {
    RoyaleSession s;
    s.Update(0.1f);
    s.SendLocalPose(1, 2, 3, 4, 5, 6);
    s.SetReady(true);
    s.ReportAttack(1, true);
    s.RequestPickup(0);
    s.RequestUsePotion();
    s.Leave();
    CHECK(!s.StartMatch());
    CHECK(s.Puppets().empty());
    HudState h = s.Hud();
    CHECK(h.mode == HudState::Mode::Idle && !h.connected && !h.status.empty());
}

static void HostCanStartAndBotsAppear() {
    RoyaleSession host, guest;
    std::string err;
    CHECK(host.Host(0, "Link", &err));
    CHECK(host.Hud().hostPort != 0);
    CHECK(guest.Join("127.0.0.1", host.Hud().hostPort, "Zelda", &err));
    CHECK(PumpUntil({&host, &guest}, [&] { return host.Joined() && guest.Joined() && guest.Hud().roster.size() == 2; }));
    CHECK(host.Hud().mode == HudState::Mode::Hosting && guest.Hud().mode == HudState::Mode::Joined);
    CHECK(host.Hud().humanCount == 2);
    CHECK(host.Hud().selfId != guest.Hud().selfId);

    CHECK(!guest.StartMatch());                       // only the host can start
    CHECK(host.StartMatch());
    CHECK(PumpUntil({&host, &guest}, [&] { return guest.Hud().state == MatchState::InMatch; }, 15));
    CHECK(PumpUntil({&host, &guest}, [&] { return guest.Hud().haveSelf && guest.Hud().alive > 20; }));

    // The guest sees the host and nearby bots as puppets, with names for humans and the right flags for bots.
    CHECK(PumpUntil({&host, &guest}, [&] { return !guest.Puppets().empty(); }));
    bool sawBot = false;
    for (auto& p : guest.Puppets()) sawBot |= p.isBot;
    CHECK(PumpUntil({&host, &guest}, [&] {
        for (auto& p : guest.Puppets()) if (p.isBot) return true;
        return false;
    }) || sawBot);
    CHECK(guest.Hud().alive == 32 || guest.Hud().alive > 20);
    CHECK(guest.Hud().safeZone.radius > 0 && guest.Hud().map.radius == kHyruleFieldMap.radius);

    // Poses travel between players.
    guest.SendLocalPose(kHyruleFieldMap.center.x + 12, 34, kHyruleFieldMap.center.z + 5, 77, 2, 0x51);
    // (the epoch bump at match start means the guest must have seen a snapshot first, which PumpUntil above ensured)
    bool seen = PumpUntil({&host, &guest}, [&] {
        // The game sends a pose every frame; a single one could be discarded by an epoch bump, so keep sending.
        guest.SendLocalPose(kHyruleFieldMap.center.x + 12, 34, kHyruleFieldMap.center.z + 5, 77, 2, 0x51);
        // Snapshots only carry the nearest players, so stand the host next to the guest.
        host.SendLocalPose(kHyruleFieldMap.center.x, 0, kHyruleFieldMap.center.z, 0, 0, 0x51);
        for (auto& p : host.Puppets()) if (p.id == guest.Hud().selfId) return std::abs(p.y - 34) < 0.5f && p.rot == 77 && p.anim == 2 && p.scene == 0x51;
        return false;
    });
    CHECK(seen);
}

static void BadAddressesAndPorts() {
    {
        RoyaleSession s;
        std::string err;
        CHECK(!s.Join("this-host-does-not-exist.invalid", 7777, "x", &err) && !err.empty());
        CHECK(s.GetMode() == RoyaleSession::Mode::Idle);
    }
    {
        RoyaleSession a, b;
        std::string err;
        CHECK(a.Host(0, "A", &err));
        CHECK(!b.Host(a.Hud().hostPort, "B", &err) && !err.empty());   // port already taken
        CHECK(b.GetMode() == RoyaleSession::Mode::Idle);
    }
    {   // Nobody home: the attempt ends with a readable reason instead of hanging.
        uint16_t port;
        { RoyaleSession tmp; tmp.Host(0, "T"); port = tmp.Hud().hostPort; }
        RoyaleSession s;
        CHECK(s.Join("127.0.0.1", port, "x"));
        CHECK(PumpUntil({&s}, [&] { return s.GetMode() == RoyaleSession::Mode::Idle; }, 10));
        CHECK(s.Hud().status == "Disconnected from host");
        s.ClearLastEnded();
        CHECK(s.Hud().status == "Not in a match");
    }
}

static void LateJoinerSeesWhy() {
    RoyaleSession host, guest, late;
    CHECK(host.Host(0, "Host"));
    CHECK(guest.Join("127.0.0.1", host.Hud().hostPort, "Guest"));
    CHECK(PumpUntil({&host, &guest}, [&] { return host.Joined() && guest.Joined(); }));
    CHECK(host.StartMatch());
    CHECK(late.Join("127.0.0.1", host.Hud().hostPort, "Late"));
    CHECK(PumpUntil({&host, &guest, &late}, [&] { return late.GetMode() == RoyaleSession::Mode::Idle; }));
    CHECK(late.Hud().status == "Match already started");
}

static void HostLeavingEndsGuestSession() {
    RoyaleSession host, guest;
    CHECK(host.Host(0, "Host"));
    CHECK(guest.Join("127.0.0.1", host.Hud().hostPort, "Guest"));
    CHECK(PumpUntil({&host, &guest}, [&] { return host.Joined() && guest.Joined(); }));
    host.Leave();
    CHECK(host.GetMode() == RoyaleSession::Mode::Idle);
    CHECK(PumpUntil({&guest}, [&] { return guest.GetMode() == RoyaleSession::Mode::Idle; }, 12));
    CHECK(guest.Hud().status == "Disconnected from host");
}

static void CanHostAgainAfterLeaving() {
    RoyaleSession s;
    CHECK(s.Host(0, "One"));
    uint16_t first = s.Hud().hostPort;
    CHECK(PumpUntil({&s}, [&] { return s.Joined(); }));
    s.Leave();
    CHECK(s.Host(0, "Two"));
    CHECK(PumpUntil({&s}, [&] { return s.Joined(); }));
    CHECK(s.Hud().humanCount == 1 && first != 0);
    CHECK(s.Host(0, "Three"));          // Host() while hosting replaces the old session
    CHECK(PumpUntil({&s}, [&] { return s.Joined(); }));
}

static void LobbyRosterHostAndReady() {
    RoyaleSession host, guest;
    CHECK(host.Host(0, "Alice"));
    CHECK(guest.Join("127.0.0.1", host.Hud().hostPort, "Bob"));
    CHECK(PumpUntil({&host, &guest}, [&] { return host.Joined() && guest.Joined() && guest.Hud().roster.size() == 2; }));

    HudState h = host.Hud(), g = guest.Hud();
    CHECK(h.isHost && !g.isHost);
    CHECK(h.humanCount == 2 && h.botSlots == kMaxPlayers - 2);
    int hostRows = 0;
    for (auto& r : g.roster) {
        hostRows += r.host;
        if (r.self) CHECK(r.name == "Bob" && !r.host);
        else CHECK(r.name == "Alice" && r.host);
    }
    CHECK(hostRows == 1);
    CHECK(!g.selfReady);

    guest.SetReady(true);
    CHECK(PumpUntil({&host, &guest}, [&] {
        for (auto& r : host.Hud().roster) if (r.name == "Bob") return r.ready;
        return false;
    }));
    CHECK(guest.Hud().selfReady && !host.Hud().selfReady);
    guest.SetReady(false);
    CHECK(PumpUntil({&host, &guest}, [&] { return !guest.Hud().selfReady; }));
}

static void CountdownAndWinnerReachTheHud() {
    RoyaleSession host, guest;
    CHECK(host.Host(0, "Alice"));
    CHECK(guest.Join("127.0.0.1", host.Hud().hostPort, "Bob"));
    CHECK(PumpUntil({&host, &guest}, [&] { return host.Joined() && guest.Joined(); }));
    CHECK(host.Hud().countdownLeft == 0 && host.Hud().winnerName.empty());
    CHECK(host.StartMatch());
    CHECK(PumpUntil({&host, &guest}, [&] { return guest.Hud().state == MatchState::Countdown; }));
    float first = guest.Hud().countdownLeft;
    CHECK(first > 0 && first <= kCountdownSec);
    for (int i = 0; i < 40; i++) Pump({&host, &guest});            // 2 s of game time
    float later = guest.Hud().countdownLeft;
    CHECK(later < first && later >= 0);
    CHECK(PumpUntil({&host, &guest}, [&] { return guest.Hud().state == MatchState::InMatch; }, 15));
    CHECK(guest.Hud().countdownLeft == 0);                         // only meaningful during the countdown
}

static void LocalAddressesLookSane() {
    auto addrs = net::LocalIPv4Addresses();
    for (const auto& a : addrs) {
        unsigned b[4]; char extra;
        CHECK(std::sscanf(a.c_str(), "%u.%u.%u.%u%c", &b[0], &b[1], &b[2], &b[3], &extra) == 4);
        CHECK(b[0] != 127 && b[0] != 0 && !(b[0] == 169 && b[1] == 254));          // never loopback or link-local
    }
    // Private LAN addresses are listed before anything else.
    bool seenPublic = false;
    for (const auto& a : addrs) {
        unsigned x, y, z, w;
        std::sscanf(a.c_str(), "%u.%u.%u.%u", &x, &y, &z, &w);
        bool priv = x == 10 || (x == 192 && y == 168) || (x == 172 && y >= 16 && y <= 31);
        if (!priv) seenPublic = true;
        else CHECK(!seenPublic);
    }
    std::printf("  local addresses found: %zu\n", addrs.size());
}

int main() {
    IdleSessionIsHarmless(); HostCanStartAndBotsAppear(); BadAddressesAndPorts(); LateJoinerSeesWhy();
    HostLeavingEndsGuestSession(); CanHostAgainAfterLeaving(); LobbyRosterHostAndReady(); CountdownAndWinnerReachTheHud(); LocalAddressesLookSane();
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("all session tests passed\n");
    return 0;
}
