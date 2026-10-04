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
    s.SendLocalPose(1, 2, 3, 4, 5);
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
    guest.SendLocalPose(kHyruleFieldMap.center.x + 12, 34, kHyruleFieldMap.center.z + 5, 77, 2);
    // (the epoch bump at match start means the guest must have seen a snapshot first, which PumpUntil above ensured)
    bool seen = PumpUntil({&host, &guest}, [&] {
        for (auto& p : host.Puppets()) if (p.id == guest.Hud().selfId) return std::abs(p.y - 34) < 0.5f && p.rot == 77 && p.anim == 2;
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

int main() {
    IdleSessionIsHarmless(); HostCanStartAndBotsAppear(); BadAddressesAndPorts(); LateJoinerSeesWhy();
    HostLeavingEndsGuestSession(); CanHostAgainAfterLeaving();
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("all session tests passed\n");
    return 0;
}
