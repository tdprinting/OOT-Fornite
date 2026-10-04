#pragma once
#include "../shared/protocol.h"
#include "../shared/transport.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

namespace royale {

// Something that happened that the game layer should react to (HUD, sound, effects).
struct ClientEvent {
    enum class Type : uint8_t { StateChanged, Damaged, Eliminated, LootTaken, LootAdded, PlayerJoined, PlayerLeft, ReadyChanged, MapChanged, InventoryChanged, AbilityUsed, BossDown, Strike, BossSpawned, PropBroken, SupplyDrop, WeatherChanged, AllyChanged, AllyAction } type;
    uint16_t id = 0;     // Damaged: target | Eliminated: victim | LootTaken: taker | PlayerJoined/Left: player
    uint16_t other = 0;  // Damaged: attacker | Eliminated: killer (kNoPlayer16 for storm or disconnect)
    float amount = 0;    // Damaged: hearts
    float health = 0;    // Damaged: what the target has left
    uint16_t count = 0;  // PropBroken: how many rupees or pieces of ammo were inside (item 255 = nothing)
    size_t index = 0;    // LootTaken / LootAdded
    MatchState state = MatchState::Lobby;
    bool ready = false;  // ReadyChanged
    uint8_t item = 0;    // PropBroken: what was inside | AbilityUsed: the ability (net::kRevivedItem means a Fairy brought someone back)
    float x = 0, z = 0;  // AbilityUsed: where the user was | Strike / BossSpawned / BossDown: where
};

// Everything the local player carries and the timed effects on them, as last reported by the server. The "left" values count down by
// the local clock between reports; use the accessors on GameClient rather than reading them raw.
struct InventoryInfo {
    float maxHealth = kMaxHealth;
    float shield = 0; // the shield bar, 0 to kMaxShield
    float magic = kMaxMagic; // the magic meter as of receivedAt
    float adultLeft = 0;     // Adult Power seconds left as of receivedAt
    int heartPieces = 0;
    int rupees = 0;
    std::array<uint8_t, kAmmoKinds> ammo = {};
    std::vector<net::ItemRef> potions;
    std::vector<net::ItemRef> reserve; // backup weapons (hotbar slots 2 and 3)
    bool hasAbility = false;
    net::ItemRef ability;
    float abilityReadyIn = 0;
    bool hasMark = false;
    uint8_t gearMask = 0;
    std::array<net::ItemRef, kGearSlots> gear = {};
    float invulnLeft = 0, speedLeft = 0, speedMult = 1, revealLeft = 0, stunLeft = 0, burnLeft = 0, regenLeft = 0, shieldLeft = 0;
    float receivedAt = 0;
};

// One entry of the lobby list: every human in the match, whether or not they are nearby.
struct RosterInfo {
    std::string name;
    bool host = false;
    bool ready = false;
    uint32_t tunic = SkinRgb(0);
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

    // `hostToken` is only passed by the hosting process for its own player (see GameServer::SetHostToken).
    GameClient(net::Transport& transport, std::string playerName, uint64_t hostToken = 0)
        : link(transport), name(std::move(playerName)), token(hostToken) {}

    void Update(float dt) {
        localClock += dt;
        net::NetEvent ev;
        while (link.Poll(ev)) {
            switch (ev.type) {
                case net::NetEvent::Type::Connected: {
                    net::Hello h;
                    h.name = name;
                    h.hostToken = token;
                    h.tunic = tunic;
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
    void SendInput(float x, float y, float z, int16_t rot, uint8_t anim, uint8_t scene = 0) {
        if (status != Status::Joined) return;
        net::Input in;
        in.seq = ++inputSeq;
        in.epoch = epoch;
        in.x = x; in.y = y; in.z = z; in.rot = rot; in.anim = anim; in.scene = scene;
        Send(in, false);
    }
    void ReportAttack(uint16_t target, bool hit) { net::AttackReport m; m.target = target; m.hit = hit; SendIfJoined(m); }
    // `force` false = auto-pickup while walking (upgrades only); true = the player pressed the swap button.
    void RequestPickup(uint32_t index, bool force = false) { net::PickupRequest m; m.index = index; m.force = force; SendIfJoined(m); }
    void RequestUsePotion() { SendIfJoined(net::UsePotionRequest{}); }
    // Use the ability slot. The server may refuse (recharging, stunned, no target); the inventory update tells you what happened.
    void UseAbility() { SendIfJoined(net::UseAbilityRequest{}); }
    void HireAlly(int index) { net::HireAllyRequest m; m.index = static_cast<uint8_t>(index); SendIfJoined(m); }   // next to a free ally, with the rupees
    // Drink a shield potion.
    void UseShield() { SendIfJoined(net::UseShieldRequest{}); }
    // Swap the weapon in hand with backup slot 1 or 2.
    void SelectWeapon(int slot) { net::SelectWeaponRequest m; m.slot = static_cast<uint8_t>(slot); SendIfJoined(m); }
    // The mini bosses in the latest snapshot (the ones near you).
    const std::vector<net::BossNet>& Bosses() const { return bosses; }
    // The hireable allies in the latest snapshot (the ones near you, and yours wherever they are).
    const std::vector<net::AllyNet>& Allies() const { return allies; }
    // The replay of the match that just ended (valid once all of it has arrived).
    const Replay& GetReplay() const { return replay; }
    bool ReplayComplete() const { return replay.Valid() && replayGot >= replay.frames.size(); }
    // Players in the match, bots included.
    int PlayerLimit() const { return playerLimit; }
    int MapId() const { return mapId; }
    // Seconds until the lobby starts the match by itself (as of the last snapshot), or -1 if there is no timer.
    float LobbyLeft() const { return lobbyLeftAtSnapshot == 255 ? -1.0f : (std::max)(0.0f, static_cast<float>(lobbyLeftAtSnapshot) - (localClock - snapshotArrival)); }
    const std::vector<Prop>& Props() const { return props; }
    const std::vector<Poi>& Pois() const { return pois; }
    // Host only (the server ignores anyone else): start another match with everyone who is connected.
    void RequestRematch() { SendIfJoined(net::RematchRequest{}); }
    void ReportSmash(size_t index) { net::PropSmashRequest m; m.index = static_cast<uint16_t>(index); SendIfJoined(m); } // I broke a rock or cut a bush
    const std::set<size_t>& BrokenProps() const { return brokenProps; }
    // The magic meter now: what the server said, plus the refill since then.
    float MagicNow() const { return (std::min)(kMaxMagic, inventory.magic + (std::max)(0.0f, localClock - inventory.receivedAt) * kMagicRegenPerSec); }
    const Weather& CurrentWeather() const { return weather; }
    float WeatherSecondsLeft() const { return (std::max)(0.0f, weatherSeconds - (localClock - weatherAt)); }
    void SelectMap(int id) { net::SelectMapRequest m; m.map = static_cast<uint8_t>(id); SendIfJoined(m); } // host only, lobby only
    const std::vector<net::ResultRow>& Results() const { return results; }
    // Lobby only: tell everyone you are (not) ready. The server ignores this once the match has started.
    void SetReady(bool ready) { net::SetReady m; m.ready = ready; SendIfJoined(m); }
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
    const std::map<uint16_t, RosterInfo>& Roster() const { return roster; }
    // The tunic colour sent in Hello. Set it before the connection comes up.
    void SetTunic(uint32_t rgb) { tunic = rgb; }
    const InventoryInfo& Inventory() const { return inventory; }
    // Seconds left on a timed value from the inventory, counting down since it arrived.
    float Left(float secondsAtReceipt) const { return (std::max)(0.0f, secondsAtReceipt - (localClock - inventory.receivedAt)); }
    float AbilityReadyIn() const { return Left(inventory.abilityReadyIn); }

    // Who won, once the match is Ending (net::kNoPlayer16 if nobody or not over yet).
    uint16_t Winner() const { return winner; }
    // Seconds since the match state last changed, by the local clock (drives the lobby countdown).
    float StateElapsed() const { return localClock - stateSince; }
    uint64_t Seed() const { return seed; }
    uint32_t LastSnapshotTick() const { return lastTick; }
    uint64_t DesyncCount() const { return desyncs; }

    // Storm time as best estimated now (the server's value at the last snapshot plus the time since).
    float StormTime() const { return haveSnapshot ? stormTimeAtSnapshot + (localClock - snapshotArrival) : 0.0f; }
    Circle SafeZone() const { return storm ? storm->SafeZoneAt(StormTime()) : map; }
    // Phase, whether the zone is shrinking, and seconds until that changes. Valid once joined.
    const Storm* GetStorm() const { return storm.get(); }
    Storm::PhaseInfo StormInfo() const { return storm ? storm->InfoAt(StormTime()) : Storm::PhaseInfo{0, false, 0.0f}; }
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

    void SetState(MatchState next) {
        if (next != state) stateSince = localClock;
        state = next;
    }

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
                SetState(static_cast<MatchState>(m.state));
                alive = m.alive;
                winner = m.winner;
                playerLimit = m.limit;
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
                e.id = m.target; e.other = m.attacker; e.amount = m.amount; e.health = m.health;
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
                roster[m.id] = RosterInfo{m.name, (m.flags & net::kRosterHost) != 0, (m.flags & net::kRosterReady) != 0, m.tunic};
                ClientEvent e{ClientEvent::Type::PlayerJoined};
                e.id = m.id;
                events.push_back(e);
                break;
            }
            case net::MsgType::EvReady: {
                net::EvReady m;
                if (!net::Decode(data, m)) break;
                auto it = roster.find(m.id);
                if (it != roster.end()) it->second.ready = m.ready;
                ClientEvent e{ClientEvent::Type::ReadyChanged};
                e.id = m.id;
                e.ready = m.ready;
                events.push_back(e);
                break;
            }
            case net::MsgType::EvInventory: {
                net::EvInventory m;
                if (!net::Decode(data, m)) break;
                inventory.rupees = m.rupees; inventory.ammo = m.ammo;
                inventory.maxHealth = m.maxHealth; inventory.shield = m.shield; inventory.magic = m.magic; inventory.adultLeft = m.adultLeft; inventory.heartPieces = m.heartPieces; inventory.potions = m.potions; inventory.reserve = m.reserve;
                inventory.hasAbility = m.hasAbility; inventory.ability = m.ability; inventory.abilityReadyIn = m.abilityReadyIn;
                inventory.hasMark = m.hasMark; inventory.gearMask = m.gearMask; inventory.gear = m.gear;
                inventory.invulnLeft = m.invulnLeft; inventory.speedLeft = m.speedLeft; inventory.speedMult = m.speedMult;
                inventory.revealLeft = m.revealLeft; inventory.stunLeft = m.stunLeft; inventory.burnLeft = m.burnLeft;
                inventory.regenLeft = m.regenLeft; inventory.shieldLeft = m.shieldLeft;
                inventory.receivedAt = localClock;
                ClientEvent e{ClientEvent::Type::InventoryChanged};
                events.push_back(e);
                break;
            }
            case net::MsgType::EvResults: {
                net::EvResults m;
                if (!net::Decode(data, m)) break;
                results = m.rows;
                break;
            }
            case net::MsgType::EvWeather: {
                net::EvWeather m;
                if (!net::Decode(data, m)) break;
                weather = Weather{static_cast<Season>(m.season), static_cast<Sky>(m.sky), m.intensity};
                weatherSeconds = m.seconds;
                weatherAt = localClock;
                ClientEvent e{ClientEvent::Type::WeatherChanged};
                e.item = m.sky; e.count = m.intensity; e.health = m.seconds; e.id = m.season;
                events.push_back(e);
                break;
            }
            case net::MsgType::EvAlly: {
                net::EvAlly m;
                if (!net::Decode(data, m)) break;
                ClientEvent e{ClientEvent::Type::AllyChanged};
                e.index = m.index; e.id = m.owner; e.item = m.status;
                events.push_back(e);
                break;
            }
            case net::MsgType::EvAllyAction: {
                net::EvAllyAction m;
                if (!net::Decode(data, m)) break;
                ClientEvent e{ClientEvent::Type::AllyAction};
                e.index = m.index; e.id = m.target; e.x = m.x; e.z = m.z;
                events.push_back(e);
                break;
            }
            case net::MsgType::EvReplayHeader: {
                net::EvReplayHeader m;
                if (!net::Decode(data, m)) break;
                replay = Replay{};
                replay.ids = m.ids;
                replay.kills = m.kills;
                replay.frames.assign(m.frames, std::vector<int16_t>(m.ids.size() * 2, 0));
                replayGot = 0;
                break;
            }
            case net::MsgType::EvReplayChunk: {
                net::EvReplayChunk m;
                if (!net::Decode(data, m)) break;
                if (m.players != replay.ids.size()) break;
                for (size_t i = 0; i < m.frames.size() && m.first + i < replay.frames.size(); i++) { replay.frames[m.first + i] = m.frames[i]; replayGot++; }
                break;
            }
            case net::MsgType::EvSupplyDrop: {
                net::EvSupplyDrop m;
                if (!net::Decode(data, m)) break;
                ClientEvent e{ClientEvent::Type::SupplyDrop};
                e.x = m.x; e.z = m.z; e.health = m.delay;
                events.push_back(e);
                break;
            }
            case net::MsgType::EvPropBroken: {
                net::EvPropBroken m;
                if (!net::Decode(data, m)) break;
                brokenProps.insert(m.index);
                ClientEvent e{ClientEvent::Type::PropBroken};
                e.index = m.index; e.id = m.by; e.item = m.item; e.count = m.amount;
                events.push_back(e);
                break;
            }
            case net::MsgType::EvStrike: {
                net::EvStrike m;
                if (!net::Decode(data, m)) break;
                ClientEvent e{ClientEvent::Type::Strike};
                e.id = m.by; e.x = m.x; e.z = m.z; e.amount = m.radius; e.health = m.delay; e.item = m.style;
                events.push_back(e);
                break;
            }
            case net::MsgType::EvBossSpawn: {
                net::EvBossSpawn m;
                if (!net::Decode(data, m)) break;
                ClientEvent e{ClientEvent::Type::BossSpawned};
                e.id = m.boss; e.item = m.kind; e.x = m.x; e.z = m.z;
                events.push_back(e);
                break;
            }
            case net::MsgType::EvBossDown: {
                net::EvBossDown m;
                if (!net::Decode(data, m)) break;
                ClientEvent e{ClientEvent::Type::BossDown};
                e.id = m.boss; e.other = m.killer; e.x = m.x; e.z = m.z;
                events.push_back(e);
                break;
            }
            case net::MsgType::EvAbility: {
                net::EvAbility m;
                if (!net::Decode(data, m)) break;
                ClientEvent e{ClientEvent::Type::AbilityUsed};
                e.id = m.user; e.item = m.item; e.x = m.x; e.z = m.z;
                events.push_back(e);
                break;
            }
            case net::MsgType::EvMapConfig: {
                net::EvMapConfig m;
                if (!net::Decode(data, m)) break;
                map = m.map;
                mapId = m.mapId;
                brokenProps.clear();
                storm = std::make_unique<Storm>(m.map, m.stormEnds);
                loot = m.loot;
                props = m.props;
                pois = m.pois;
                results.clear();
                ClientEvent e{ClientEvent::Type::MapChanged};
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
        props = w.props;
        playerLimit = w.limit;
        mapId = w.mapId;
        pois = w.pois;
        roster.clear();
        for (const auto& r : w.roster) {
            roster[r.id] = RosterInfo{r.name, (r.flags & net::kRosterHost) != 0, (r.flags & net::kRosterReady) != 0, r.tunic};
        }
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
        lobbyLeftAtSnapshot = s.lobbyLeft;
        SetState(static_cast<MatchState>(s.state));
        alive = s.alive;
        epoch = s.epoch;

        bosses = s.bosses;
        allies = s.allies;
        bossesAt = localClock;
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
    uint64_t token = 0;
    Status status = Status::Connecting;
    net::RejectReason rejectReason = net::RejectReason::BadHello;
    uint16_t playerId = 0;
    uint64_t seed = 0;
    Circle map;
    int mapId = 0;
    std::set<size_t> brokenProps;   // props somebody has smashed this match
    Weather weather;                // the sky, as last announced by the server
    float weatherSeconds = 0, weatherAt = 0;
    std::unique_ptr<Storm> storm;
    std::vector<net::LootNet> loot;
    std::map<uint16_t, RosterInfo> roster;
    uint32_t tunic = SkinRgb(0);
    InventoryInfo inventory;
    std::vector<net::BossNet> bosses;
    std::vector<net::AllyNet> allies;
    Replay replay;
    size_t replayGot = 0;
    float bossesAt = 0;
    int playerLimit = kMaxPlayers;
    uint8_t lobbyLeftAtSnapshot = 255;
    std::vector<Prop> props;
    std::vector<Poi> pois;
    std::vector<net::ResultRow> results;
    std::map<uint16_t, Remote> players;
    std::vector<ClientEvent> events;
    MatchState state = MatchState::Lobby;
    uint16_t winner = net::kNoPlayer16;
    float stateSince = 0;
    int alive = 0;
    uint8_t epoch = 0;
    uint16_t inputSeq = 0;
    bool haveSnapshot = false;
    uint32_t lastTick = 0;
    float localClock = 0, serverOffset = 0, snapshotArrival = 0, stormTimeAtSnapshot = 0;
    uint64_t desyncs = 0;
};

} // namespace royale
