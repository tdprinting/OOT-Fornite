#pragma once
#include "../shared/protocol.h"
#include "../shared/transport.h"
#include "sim.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace royale {

// The host's server: owns the match and its bots, talks to clients through a Transport.
//
// Trust model: positions and "did my hit land" come from clients (OoT physics can't be reproduced server-side). The
// server validates what it can: movement speed is clamped, attacks need range and cooldown, pickups need proximity, and
// everything that matters (health, damage, loot, eliminations, storm) is decided here.
//
// Call Update(realDt) every frame. It runs the simulation in fixed 1/kTickHz steps and sends one snapshot per client per step.
class GameServer {
  public:
    struct Stats {
        uint64_t bytesOut = 0, packetsOut = 0;
        uint64_t badPackets = 0;      // undecodable, wrong state, or from a peer that never said Hello
        uint64_t staleInputs = 0;     // old seq or old teleport epoch
        uint64_t speedClamps = 0;     // position updates the server had to shorten
        uint64_t rejectedActions = 0; // attacks, pickups or potions the match refused
    };

    GameServer(net::Transport& transport, uint64_t seed, Circle map, int lootCount = 400)
        : link(transport), sim(seed, map, lootCount), mapCircle(map) {}

    // Host presses "Start". Needs at least one human in the lobby; the remaining slots fill with bots.
    bool StartMatch() {
        if (!sim.match.Start()) return false;
        // Start() teleports everybody to spawn points. Bump every epoch so inputs sent from the old positions are ignored.
        for (auto& c : clients) {
            c.epoch++;
            c.lastInputClock = clock;
        }
        return true;
    }

    void Update(float realDt) {
        PollNetwork();
        accumulator += realDt;
        int steps = 0;
        while (accumulator >= kStep && steps < 5) {
            accumulator -= kStep;
            steps++;
            Step();
        }
        if (steps == 5) accumulator = 0; // fell too far behind: drop the backlog instead of spiralling
        link.Flush();
    }

    Simulation& Sim() { return sim; }
    const Stats& GetStats() const { return stats; }
    uint32_t Tick() const { return tick; }
    int HumanCount() const { int n = 0; for (auto& c : clients) n += c.joined; return n; }
    // Player id for a connected peer, or kNoPlayer.
    uint32_t PlayerOfPeer(net::PeerId peer) const {
        const Client* c = FindByPeer(peer);
        return c && c->joined ? c->playerId : kNoPlayer;
    }

    // The most elapsed time a single input may be credited with. Clients send ~20 inputs a second, so a longer gap means a
    // stall or a lost packet, not permission to cross the map: without a cap, a player idle for 15 s could jump 7000+ units.
    static constexpr float kMaxInputGap = 0.5f;

  private:
    static constexpr float kStep = 1.0f / kTickHz;

    struct Client {
        net::PeerId peer = 0;
        bool joined = false;
        uint32_t playerId = 0;
        std::string name;
        uint8_t epoch = 0;
        uint16_t lastSeq = 0;
        bool hasSeq = false;
        float lastInputClock = 0;
    };

    Client* FindByPeer(net::PeerId peer) {
        for (auto& c : clients) if (c.peer == peer) return &c;
        return nullptr;
    }
    const Client* FindByPeer(net::PeerId peer) const {
        for (auto& c : clients) if (c.peer == peer) return &c;
        return nullptr;
    }
    Client* FindByPlayer(uint32_t id) {
        for (auto& c : clients) if (c.joined && c.playerId == id) return &c;
        return nullptr;
    }

    template <class T>
    void SendTo(const Client& c, const T& msg, bool reliable = true) {
        std::vector<uint8_t> bytes = net::Encode(msg);
        stats.bytesOut += bytes.size();
        stats.packetsOut++;
        link.Send(c.peer, bytes, reliable);
    }
    template <class T>
    void Broadcast(const T& msg) {
        for (auto& c : clients) if (c.joined) SendTo(c, msg);
    }

    void PollNetwork() {
        net::NetEvent ev;
        while (link.Poll(ev)) {
            switch (ev.type) {
                case net::NetEvent::Type::Connected: {
                    Client c;
                    c.peer = ev.peer;
                    clients.push_back(c);
                    break;
                }
                case net::NetEvent::Type::Disconnected:
                    OnDisconnected(ev.peer);
                    break;
                case net::NetEvent::Type::Data:
                    OnData(ev.peer, ev.data);
                    break;
            }
        }
    }

    void OnDisconnected(net::PeerId peer) {
        Client* c = FindByPeer(peer);
        if (!c) return;
        if (c->joined) {
            uint32_t id = c->playerId;
            c->joined = false;
            sim.match.RemovePlayer(id);
            net::EvPlayerLeft left;
            left.id = static_cast<uint16_t>(id);
            Broadcast(left);
        }
        clients.erase(std::remove_if(clients.begin(), clients.end(), [&](const Client& x) { return x.peer == peer; }), clients.end());
    }

    void Reject(Client& c, net::RejectReason why) {
        net::Reject r;
        r.reason = why;
        SendTo(c, r);
        link.Disconnect(c.peer);
    }

    void OnData(net::PeerId peer, const std::vector<uint8_t>& data) {
        Client* c = FindByPeer(peer);
        net::MsgType type;
        if (!c || !net::PeekType(data.data(), data.size(), type)) { stats.badPackets++; return; }

        if (!c->joined) {
            if (type == net::MsgType::Hello) HandleHello(*c, data);
            else stats.badPackets++;
            return;
        }
        switch (type) {
            case net::MsgType::Input: HandleInput(*c, data); break;
            case net::MsgType::AttackReport: {
                net::AttackReport m;
                if (!net::Decode(data, m)) { stats.badPackets++; break; }
                if (!sim.match.Attack(c->playerId, m.target, m.hit).ok) stats.rejectedActions++;
                break;
            }
            case net::MsgType::PickupRequest: {
                net::PickupRequest m;
                if (!net::Decode(data, m)) { stats.badPackets++; break; }
                if (!sim.match.PickUp(c->playerId, m.index)) stats.rejectedActions++;
                break;
            }
            case net::MsgType::UsePotionRequest: {
                net::UsePotionRequest m;
                if (!net::Decode(data, m)) { stats.badPackets++; break; }
                if (!sim.match.UsePotion(c->playerId)) stats.rejectedActions++;
                break;
            }
            default:
                stats.badPackets++;
                break;
        }
    }

    uint32_t NextPlayerId() {
        // Human ids are 1..999; bots use 1000 and up.
        for (uint32_t id = 1; id < 1000; id++) {
            if (!sim.match.Find(id)) return id;
        }
        return kNoPlayer;
    }

    void HandleHello(Client& c, const std::vector<uint8_t>& data) {
        net::Hello hello;
        if (!net::Decode(data, hello)) { Reject(c, net::RejectReason::BadHello); return; }
        if (hello.version != net::kProtocolVersion) { Reject(c, net::RejectReason::VersionMismatch); return; }
        if (sim.match.State() != MatchState::Lobby) { Reject(c, net::RejectReason::MatchInProgress); return; }
        uint32_t id = NextPlayerId();
        if (HumanCount() >= kMaxPlayers || id == kNoPlayer || !sim.match.AddHuman(id)) { Reject(c, net::RejectReason::LobbyFull); return; }

        c.joined = true;
        c.playerId = id;
        c.name = net::SanitizeName(hello.name);
        c.lastInputClock = clock;

        net::Welcome w;
        w.playerId = static_cast<uint16_t>(id);
        w.seed = sim.match.Seed();
        w.map = mapCircle;
        w.stormEnds = sim.match.GetStorm().PhaseEnds();
        for (const auto& l : sim.match.Loot()) w.loot.push_back(ToNet(l));
        for (const auto& o : clients) if (o.joined) w.roster.push_back({static_cast<uint16_t>(o.playerId), o.name});
        SendTo(c, w);

        net::EvPlayerJoined joined;
        joined.id = static_cast<uint16_t>(id);
        joined.name = c.name;
        for (auto& o : clients) if (o.joined && o.peer != c.peer) SendTo(o, joined);
    }

    void HandleInput(Client& c, const std::vector<uint8_t>& data) {
        net::Input in;
        if (!net::Decode(data, in)) { stats.badPackets++; return; }
        if (c.hasSeq && static_cast<int16_t>(in.seq - c.lastSeq) <= 0) { stats.staleInputs++; return; }
        c.lastSeq = in.seq;
        c.hasSeq = true;
        if (in.epoch != c.epoch) { stats.staleInputs++; return; }

        PlayerState* p = sim.match.Find(c.playerId);
        if (!p || !p->alive) return;
        Vec2 target = {in.x, in.z};
        if (sim.match.State() != MatchState::Lobby) {
            float elapsed = (std::min)(std::max(clock - c.lastInputClock, kStep), kMaxInputGap);
            float maxMove = kMaxPlausibleSpeed * elapsed + kMovementSlack;
            float d = Distance(p->pos, target);
            if (d > maxMove) {
                float k = maxMove / d;
                target = {p->pos.x + (target.x - p->pos.x) * k, p->pos.z + (target.z - p->pos.z) * k};
                stats.speedClamps++;
            }
        }
        c.lastInputClock = clock;
        p->pos = target;
        p->y = in.y;
        p->rot = in.rot;
        p->anim = in.anim;
    }

    static net::LootNet ToNet(const LootEntry& l) {
        net::LootNet n;
        n.x = l.spawn.pos.x; n.z = l.spawn.pos.z;
        n.item = static_cast<uint8_t>(l.spawn.item);
        n.rarity = static_cast<uint8_t>(l.spawn.rarity);
        n.chest = l.spawn.fromChest;
        n.taken = l.taken;
        return n;
    }

    void Step() {
        sim.Tick(kStep);
        clock += kStep;
        tick++;
        BroadcastEvents();
        SendSnapshots();
    }

    void BroadcastEvents() {
        for (const MatchEvent& e : sim.match.DrainEvents()) {
            switch (e.type) {
                case MatchEvent::Type::StateChanged: {
                    net::MatchStateMsg m;
                    m.state = static_cast<uint8_t>(e.state);
                    m.alive = static_cast<uint8_t>(sim.match.Alive());
                    Broadcast(m);
                    break;
                }
                case MatchEvent::Type::Damaged: {
                    net::EvDamaged d;
                    d.target = static_cast<uint16_t>(e.a);
                    d.attacker = static_cast<uint16_t>(e.b);
                    d.amount = e.amount;
                    d.health = e.health;
                    // Only the two people involved care; everyone else sees the health bar move in snapshots.
                    if (Client* t = FindByPlayer(e.a)) SendTo(*t, d);
                    if (Client* a = FindByPlayer(e.b)) SendTo(*a, d);
                    break;
                }
                case MatchEvent::Type::Eliminated: {
                    net::EvEliminated el;
                    el.victim = static_cast<uint16_t>(e.a);
                    el.killer = e.b == kNoPlayer ? net::kNoPlayer16 : static_cast<uint16_t>(e.b);
                    Broadcast(el);
                    break;
                }
                case MatchEvent::Type::LootTaken: {
                    net::EvLootTaken t;
                    t.index = static_cast<uint32_t>(e.index);
                    t.by = static_cast<uint16_t>(e.a);
                    Broadcast(t);
                    break;
                }
                case MatchEvent::Type::LootAdded: {
                    net::EvLootAdded a;
                    a.index = static_cast<uint32_t>(e.index);
                    a.loot = ToNet(sim.match.Loot()[e.index]);
                    Broadcast(a);
                    break;
                }
            }
        }
    }

    static net::PlayerNet ToNet(const PlayerState& p) {
        net::PlayerNet n;
        n.id = static_cast<uint16_t>(p.id);
        n.x = p.pos.x; n.z = p.pos.z; n.y = p.y;
        n.rot = p.rot;
        n.health = net::PlayerNet::QuantizeHealth(p.health);
        n.flags = static_cast<uint8_t>((p.alive ? net::PlayerNet::kAlive : 0) | (p.hasShield ? net::PlayerNet::kShield : 0) |
                                       (p.isBot ? net::PlayerNet::kBot : 0));
        n.weapon = static_cast<uint8_t>(p.weapon.item);
        n.weaponRarity = static_cast<uint8_t>(p.weapon.rarity);
        n.potions = static_cast<uint8_t>(p.potions.size());
        n.anim = p.anim;
        return n;
    }

    // Interest management: each client gets itself plus the nearest few living players, not all 32.
    void SendSnapshots() {
        const auto& players = sim.match.Players();
        for (auto& c : clients) {
            if (!c.joined) continue;
            const PlayerState* self = sim.match.Find(c.playerId);
            if (!self) continue;
            net::Snapshot s;
            s.tick = tick;
            s.stormTime = sim.match.StormTime();
            s.state = static_cast<uint8_t>(sim.match.State());
            s.alive = static_cast<uint8_t>(sim.match.Alive());
            s.epoch = c.epoch;
            s.players.push_back(ToNet(*self));

            std::vector<std::pair<float, const PlayerState*>> nearby;
            for (const auto& o : players) {
                if (o.id == self->id || !o.alive) continue;
                nearby.push_back({Distance(self->pos, o.pos), &o});
            }
            size_t keep = (std::min)(nearby.size(), net::kSnapshotMaxPlayers);
            std::partial_sort(nearby.begin(), nearby.begin() + static_cast<long>(keep), nearby.end(),
                              [](const auto& a, const auto& b) { return a.first < b.first || (a.first == b.first && a.second->id < b.second->id); });
            for (size_t i = 0; i < keep; i++) s.players.push_back(ToNet(*nearby[i].second));
            SendTo(c, s, false);
        }
    }

    net::Transport& link;
    Simulation sim;
    Circle mapCircle;
    std::vector<Client> clients;
    Stats stats;
    float accumulator = 0;
    float clock = 0;
    uint32_t tick = 0;
};

} // namespace royale
