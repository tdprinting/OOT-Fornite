#pragma once
#include "../shared/protocol.h"
#include "../shared/transport.h"
#include <cmath>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace royale {

// Something that happened that the game layer should react to (HUD, sound, effects).
struct ClientEvent {
    enum class Type : uint8_t { StateChanged, Damaged, Eliminated, LootTaken, LootAdded, PlayerJoined, PlayerLeft } type;
    uint16_t id = 0;     // Damaged: target | Eliminated: victim | LootTaken: taker | PlayerJoined/Left: player
    uint16_t other = 0;  // Damaged: attacker | Eliminated: killer (kNoPlayer16 for storm or disconnect)
    float amount = 0;    // Damaged: hearts
    size_t index = 0;    // LootTaken / LootAdded
    MatchState state = MatchState::Lobby;
};

// Client side of the protocol: connects, mirrors the server's world, and smooths remote players for rendering.
//
// Remote players are drawn kInterpDelay behind the newest snapshot, interpolated between the two snapshots that
// bracket that time. The local player is NOT predicted here: the game moves Link as usual, calls SendInput, and may
// compare against Self() (the server's view) to detect a correction after a clamp or teleport.
class GameClient {
  public:
    enum class Status : uint8_t { Connecting, Joined, Rejected, Disconnected };
    static constexpr float kInterpDelay = 0.1f; // seconds; two snapshots at 20 Hz
    static constexpr size_t kHistoryMax = 40;

    GameClient(net::Transport& transport, std::string playerName) : link(transport), name(std::move(playerName)) {}

    void Update(float dt) {
        localClock += dt;
        net::NetEvent ev;
        while (link.Poll(ev)) {
            switch (ev.type) {
                case net::NetEvent::Type::Connected: {
                    net::Hello h;
                    h.name = name;
                    Send(h);
                    break;
                }
                case net::NetEvent::Type::Disconnected:
                    if (status != Status::Rejected) status = Status::Disconnected;
                    break;
                case net::NetEvent::Type::Data:
                    OnData(ev.data);
                    break;
            }
        }
        link.Flush();
    }

    // ---- what the game sends ----
    void SendInput(float x, float y, float z, int16_t rot, uint8_t anim) {
        if (status != Status::Joined) return;
        net::Input in;
        in.seq = ++inputSeq;
        in.epoch = epoch;
        in.x = x; in.y = y; in.z = z; in.rot = rot; in.anim = anim;
        Send(in, false);
    }
    void ReportAttack(uint16_t target, bool hit) { net::AttackReport m; m.target = target; m.hit = hit; SendIfJoined(m); }
    void RequestPickup(uint32_t index) { net::PickupRequest m; m.index = index; SendIfJoined(m); }
    void RequestUsePotion() { SendIfJoined(net::UsePotionRequest{}); }
    void Leave() { link.Disconnect(0); }

    // ---- what the game reads ----
    Status GetStatus() const { return status; }
    net::RejectReason RejectedBecause() const { return rejectReason; }
    uint16_t PlayerId() const { return playerId; }
    MatchState State() const { return state; }
    int AliveCount() const { return alive; }
    uint8_t Epoch() const { return epoch; }
    const Circle& Map() const { return map; }
    const std::vector<net::LootNet>& Loot() const { return loot; }
    const std::map<uint16_t, std::string>& Roster() const { return roster; }
    uint64_t Seed() const { return seed; }
    uint32_t LastSnapshotTick() const { return lastTick; }
    uint64_t DesyncCount() const { return desyncs; }

    // Storm time as best estimated now (the server's value at the last snapshot plus the time since).
    float StormTime() const { return haveSnapshot ? stormTimeAtSnapshot + (localClock - snapshotArrival) : 0.0f; }
    Circle SafeZone() const { return storm ? storm->SafeZoneAt(StormTime()) : map; }
    float StormDamagePerSecond(Vec2 p) const { return storm ? storm->DamagePerSecond(p, StormTime()) : 0.0f; }

    // The server's latest view of the local player.
    const net::PlayerNet* Self() const {
        auto it = players.find(playerId);
        return it != players.end() && it->second.visible ? &it->second.latest : nullptr;
    }

    // Players currently in range (everyone in the newest snapshot except you).
    std::vector<uint16_t> VisiblePlayers() const {
        std::vector<uint16_t> out;
        for (const auto& [id, p] : players) if (p.visible && id != playerId) out.push_back(id);
        return out;
    }

    // Interpolated state of a player at the render time. False if that player isn't visible.
    bool Sample(uint16_t id, net::PlayerNet& out) const {
        auto it = players.find(id);
        if (it == players.end() || !it->second.visible || it->second.history.empty()) return false;
        const auto& h = it->second.history;
        float t = RenderServerTime();
        if (t <= h.front().t) { out = h.front().s; return true; }
        if (t >= h.back().t) { out = h.back().s; return true; }
        for (size_t i = 1; i < h.size(); i++) {
            if (h[i].t >= t) {
                const auto& a = h[i - 1];
                const auto& b = h[i];
                float k = (t - a.t) / (b.t - a.t);
                out = k < 0.5f ? a.s : b.s; // discrete fields (flags, weapon, health) from the nearer sample
                out.x = a.s.x + (b.s.x - a.s.x) * k;
                out.y = a.s.y + (b.s.y - a.s.y) * k;
                out.z = a.s.z + (b.s.z - a.s.z) * k;
                int16_t d = static_cast<int16_t>(static_cast<uint16_t>(b.s.rot) - static_cast<uint16_t>(a.s.rot)); // shortest way round
                out.rot = static_cast<int16_t>(static_cast<float>(a.s.rot) + static_cast<float>(d) * k);
                return true;
            }
        }
        out = h.back().s;
        return true;
    }

    std::vector<ClientEvent> DrainEvents() {
        std::vector<ClientEvent> out;
        out.swap(events);
        return out;
    }

  private:
    struct SampleAt {
        float t;
        net::PlayerNet s;
    };
    struct Remote {
        bool visible = false;
        net::PlayerNet latest;
        std::deque<SampleAt> history;
    };

    template <class T>
    void Send(const T& m, bool reliable = true) { link.Send(0, net::Encode(m), reliable); }
    template <class T>
    void SendIfJoined(const T& m) { if (status == Status::Joined) Send(m); }

    float RenderServerTime() const { return localClock + serverOffset - kInterpDelay; }

    void OnData(const std::vector<uint8_t>& data) {
        net::MsgType type;
        if (!net::PeekType(data.data(), data.size(), type)) return;
        // Until the server accepts us, only Welcome or Reject mean anything.
        if (status == Status::Connecting && type != net::MsgType::Welcome && type != net::MsgType::Reject) return;
        switch (type) {
            case net::MsgType::Welcome: OnWelcome(data); break;
            case net::MsgType::Reject: {
                net::Reject r;
                if (net::Decode(data, r)) { rejectReason = r.reason; status = Status::Rejected; }
                break;
            }
            case net::MsgType::MatchStateMsg: {
                net::MatchStateMsg m;
                if (!net::Decode(data, m)) break;
                state = static_cast<MatchState>(m.state);
                alive = m.alive;
                ClientEvent e{ClientEvent::Type::StateChanged};
                e.state = state;
                events.push_back(e);
                break;
            }
            case net::MsgType::Snapshot: OnSnapshot(data); break;
            case net::MsgType::EvDamaged: {
                net::EvDamaged m;
                if (!net::Decode(data, m)) break;
                ClientEvent e{ClientEvent::Type::Damaged};
                e.id = m.target; e.other = m.attacker; e.amount = m.amount;
                events.push_back(e);
                break;
            }
            case net::MsgType::EvEliminated: {
                net::EvEliminated m;
                if (!net::Decode(data, m)) break;
                ClientEvent e{ClientEvent::Type::Eliminated};
                e.id = m.victim; e.other = m.killer;
                events.push_back(e);
                break;
            }
            case net::MsgType::EvLootTaken: {
                net::EvLootTaken m;
                if (!net::Decode(data, m)) break;
                if (m.index < loot.size()) loot[m.index].taken = true; else desyncs++;
                ClientEvent e{ClientEvent::Type::LootTaken};
                e.id = m.by; e.index = m.index;
                events.push_back(e);
                break;
            }
            case net::MsgType::EvLootAdded: {
                net::EvLootAdded m;
                if (!net::Decode(data, m)) break;
                if (m.index == loot.size()) loot.push_back(m.loot);
                else if (m.index < loot.size()) loot[m.index] = m.loot;
                else { desyncs++; break; }
                ClientEvent e{ClientEvent::Type::LootAdded};
                e.index = m.index;
                events.push_back(e);
                break;
            }
            case net::MsgType::EvPlayerJoined: {
                net::EvPlayerJoined m;
                if (!net::Decode(data, m)) break;
                roster[m.id] = m.name;
                ClientEvent e{ClientEvent::Type::PlayerJoined};
                e.id = m.id;
                events.push_back(e);
                break;
            }
            case net::MsgType::EvPlayerLeft: {
                net::EvPlayerLeft m;
                if (!net::Decode(data, m)) break;
                roster.erase(m.id);
                ClientEvent e{ClientEvent::Type::PlayerLeft};
                e.id = m.id;
                events.push_back(e);
                break;
            }
            default:
                break;
        }
    }

    void OnWelcome(const std::vector<uint8_t>& data) {
        net::Welcome w;
        if (status != Status::Connecting || !net::Decode(data, w) || w.version != net::kProtocolVersion) return;
        playerId = w.playerId;
        seed = w.seed;
        map = w.map;
        storm = std::make_unique<Storm>(w.map, w.stormEnds);
        loot = w.loot;
        roster.clear();
        for (const auto& r : w.roster) roster[r.id] = r.name;
        status = Status::Joined;
    }

    void OnSnapshot(const std::vector<uint8_t>& data) {
        net::Snapshot s;
        if (!net::Decode(data, s)) return;
        if (haveSnapshot && s.tick <= lastTick) return; // stale
        float serverTime = static_cast<float>(s.tick) / kTickHz;
        float offset = serverTime - localClock;
        serverOffset = haveSnapshot ? serverOffset + (offset - serverOffset) * 0.1f : offset;
        haveSnapshot = true;
        lastTick = s.tick;
        snapshotArrival = localClock;
        stormTimeAtSnapshot = s.stormTime;
        state = static_cast<MatchState>(s.state);
        alive = s.alive;
        epoch = s.epoch;

        for (auto& [id, p] : players) p.visible = false;
        for (const auto& pn : s.players) {
            Remote& r = players[pn.id];
            r.visible = true;
            r.latest = pn;
            r.history.push_back({serverTime, pn});
            while (r.history.size() > kHistoryMax) r.history.pop_front();
        }
    }

    net::Transport& link;
    std::string name;
    Status status = Status::Connecting;
    net::RejectReason rejectReason = net::RejectReason::BadHello;
    uint16_t playerId = 0;
    uint64_t seed = 0;
    Circle map;
    std::unique_ptr<Storm> storm;
    std::vector<net::LootNet> loot;
    std::map<uint16_t, std::string> roster;
    std::map<uint16_t, Remote> players;
    std::vector<ClientEvent> events;
    MatchState state = MatchState::Lobby;
    int alive = 0;
    uint8_t epoch = 0;
    uint16_t inputSeq = 0;
    bool haveSnapshot = false;
    uint32_t lastTick = 0;
    float localClock = 0, serverOffset = 0, snapshotArrival = 0, stormTimeAtSnapshot = 0;
    uint64_t desyncs = 0;
};

} // namespace royale
