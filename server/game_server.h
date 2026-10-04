#pragma once
#include <random>
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
        : link(transport), sim(seed, map, lootCount), mapCircle(map) { sim.match.SetMajorBoss(majorBoss); }
    // How many mini bosses a match has (0 to 8). Survives Reconfigure.
    void SetBossCount(int n) { bossCount = n; sim.match.SetBossCount(n); }
    // The map's dragon arrives halfway through the storm timeline. Survives Reconfigure.
    void SetMajorBoss(bool on) { majorBoss = on; sim.match.SetMajorBoss(on); }
    // The host's player-count slider: how many players the match has, bots included. Lobby only, and never fewer than the people here.
    // Smaller matches also get fewer towns and bosses, so the map isn't mostly empty. Everyone is told.
    bool SetPlayerLimit(int n) {
        if (!sim.match.SetPlayerLimit(n)) return false;
        playerLimit = n;
        poiCount = (std::max)(4, (std::min)(12, n / 2 + 2));
        if (bossCount > 0) { bossCount = (std::max)(0, (std::min)(5, n / 6)); sim.match.SetBossCount(bossCount); }
        net::MatchStateMsg m;
        m.state = static_cast<uint8_t>(sim.match.State());
        m.alive = static_cast<uint8_t>(sim.match.Alive());
        m.limit = static_cast<uint8_t>(n);
        Broadcast(m);
        return true;
    }
    int PlayerLimit() const { return playerLimit; }
    // The lobby starts the match by itself after this many seconds (0 turns it off). The clock starts when the first player is here.
    void SetAutoStart(float seconds) { autoStartSec = seconds; if (seconds <= 0) lobbyElapsed = 0; }
    float AutoStartSeconds() const { return autoStartSec; }
    // Seconds left on that timer, or a negative number when it is off or the match has begun.
    float LobbyLeft() const { return (autoStartSec > 0 && sim.match.State() == MatchState::Lobby) ? (std::max)(0.0f, autoStartSec - lobbyElapsed) : -1.0f; }

    // The hosting process generates a random token and gives it both to the server (here) and to its own client's Hello, so
    // the server can tell which connected player is the host without trusting addresses or join order.
    void SetHostToken(uint64_t token) { hostToken = token; }

    // The host measured the real playable area and wants the lobby's world rebuilt on it: new map circle, loot and storm placed only
    // on positions `valid` accepts (e.g. "there is floor here"). Lobby only; players, ready flags and connections are kept.
    // Everyone is sent the new map, storm circles and loot in one reliable message.
    bool Reconfigure(Circle map, PlacementFn valid = nullptr, int lootCount = 150, uint64_t seedOffset = 0) {
        if ((sim.match.State() != MatchState::Lobby && sim.match.State() != MatchState::Ending) || map.radius <= 0) return false;
        lastValid = valid;
        lastLootCount = lootCount;
        lootCount = static_cast<int>(static_cast<float>(lootCount) * (std::max)(1.0f, (std::min)(1.9f, (map.radius * map.radius) / (4800.0f * 4800.0f))));   // a huge map gets more chests, so they are still found
        const uint64_t baseSeed = sim.match.Seed() + seedOffset;
        // Prefer a storm whose six circle centres are all on walkable ground; give up after 200 tries and take the last one.
        uint64_t seed = baseSeed;
        for (uint64_t k = 0; k < 200; k++) {
            seed = baseSeed + k * 7919;
            Storm candidate(seed, map);
            bool ok = true;
            if (valid) for (int i = 0; i < kStormPhaseCount; i++) ok = ok && valid(candidate.PhaseEnd(i).center);
            if (ok) break;
        }
        std::vector<uint32_t> humans;
        for (const auto& p : sim.match.Players()) humans.push_back(p.id);
        sim = Simulation(seed, map, 0);
        sim.bots.SetDifficulty(botDifficulty);
        sim.match.SetPlacementValidator(valid);
        // Scenery: the same list goes to every client, and the bots' navigation grid treats the solid ones as obstacles.
        sim.match.SetMapId(mapId);
        PoiLayout layout = GeneratePois(seed, map, poiCount, valid, mapId);
        pois = layout.pois;
        const float areaShare = (std::min)(1.9f, (map.radius * map.radius) / (4000.0f * 4000.0f));   // a small map gets fewer rocks and bushes, a big one more
        const int sceneryWanted = (std::max)(220, static_cast<int>(static_cast<float>(propCount) * (std::min)(1.8f, areaShare * 1.4f)));
        // The towns, walls and climbs come first and must all fit, so the scenery gets what is left of the budget.
        // Loose scenery stays off the towns' streets.
        const std::vector<Circle> clearings = PoiClearings(layout.pois);
        props = GenerateProps(seed, map, (std::min)(sceneryWanted, (std::max)(120, kMaxProps - static_cast<int>(layout.props.size()) - 320)), valid, &clearings);
        // Boulder formations, climbs out in the open and chests hidden behind boulders, spread out over the whole map.
        GenerateWilds(layout, seed, map, props, layout.lootSpots, 4 + static_cast<int>(map.radius / 700.0f), 12 + static_cast<int>(map.radius / 250.0f), valid,
                      2 + static_cast<int>(map.radius / 650.0f));
        props.insert(props.end(), layout.props.begin(), layout.props.end()); // the buildings, caves and climbs are scenery too
        if (props.size() > static_cast<size_t>(kMaxProps)) props.resize(kMaxProps);
        broken.assign(props.size(), false);
        sim.match.SetLootSpots(layout.lootSpots);
        sim.match.SetChestSites(layout.sites);
        sim.match.SetBossSpots(layout.bossSpots);
        sim.match.SetAllySpots(GenerateAllySpots(seed, map, layout.pois, valid));
        // A small map cannot hold five mini bosses: about one for every 1700 units of radius squared.
        sim.match.SetBossCount((std::min)(bossCount, (std::max)(1, static_cast<int>(map.radius * map.radius / (1700.0f * 1700.0f)))));
        sim.match.SetMajorBoss(majorBoss);
        sim.match.SetWeatherOptions(weatherOptions);
        sim.match.SetPlayerLimit(playerLimit);
        if (valid) {
            auto grid = std::make_shared<NavGrid>(map, valid);
            for (const Prop& p : props) if (PropRadius(p.kind) > 0) grid->Block(p.pos, PropRadius(p.kind) + 20.0f);
            sim.bots.SetNav(grid);
        }
        sim.match.RegenerateLoot(lootCount);
        for (uint32_t id : humans) sim.match.AddHuman(id);
        mapCircle = map;

        net::EvMapConfig cfg;
        cfg.mapId = static_cast<uint8_t>(mapId);
        cfg.map = map;
        cfg.stormEnds = sim.match.GetStorm().PhaseEnds();
        for (const auto& l : sim.match.Loot()) cfg.loot.push_back(ToNet(l));
        cfg.props = props;
        cfg.pois = pois;
        Broadcast(cfg);
        return true;
    }

    // Results screen: everyone still connected plays again on the same map with fresh loot, scenery and storm, with no trip back to
    // the lobby. Only meaningful once the match has ended.
    // A different storm, loot layout and spawn spread every match.
    static uint64_t FreshSeedOffset() {
        std::random_device rd;
        return (static_cast<uint64_t>(rd()) << 32) ^ rd() ^ 0x9E3779B97F4A7C15ull;
    }

    // A player broke a rock or cut a bush. The first report for a prop wins; it must be a breakable kind, and the player close enough to have done
    // it. What was inside is rolled by the match from the seed and the prop's number, so it is the same however the report arrives.
    bool SmashProp(uint32_t playerId, size_t index) {
        if (index >= props.size() || broken.size() != props.size() || broken[index]) return false;
        const Prop& prop = props[index];
        if (prop.kind != PropKind::Rock && prop.kind != PropKind::Boulder && prop.kind != PropKind::Bush) return false;
        const PlayerState* p = sim.match.Find(playerId);
        if (!p || !p->alive || (sim.match.State() != MatchState::InMatch && sim.match.State() != MatchState::Drop)) return false;
        if (Distance(p->pos, prop.pos) > kSmashReach) return false;
        broken[index] = true;
        const Match::PropDrop drop = sim.match.GrantPropLoot(playerId, prop.kind, index);
        net::EvPropBroken ev;
        ev.index = static_cast<uint16_t>(index);
        ev.by = static_cast<uint16_t>(playerId);
        if (drop.any) { ev.item = static_cast<uint8_t>(drop.item); ev.amount = static_cast<uint16_t>(drop.amount); }
        Broadcast(ev);
        return true;
    }
    static constexpr float kSmashReach = 280.0f;

    // The host chooses where the match is played (lobby only). Everyone is sent a rebuilt world on that map's guessed size; the host's game
    // measures the real scene when the match starts and rebuilds it once more.
    bool SelectMap(int id) {
        if (sim.match.State() != MatchState::Lobby || id < 0 || id >= kMapCount) return false;
        mapId = id;
        mapCircle = MapOf(id).fallback;
        return Reconfigure(mapCircle, nullptr, lastLootCount, FreshSeedOffset());
    }
    int MapId() const { return mapId; }

    bool PlayAgain() {
        if (sim.match.State() != MatchState::Ending) return false;
        if (!Reconfigure(mapCircle, lastValid, lastLootCount, FreshSeedOffset())) return false;
        return StartMatch();
    }

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
    const std::vector<Poi>& Pois() const { return pois; }
    // How well bots play. Survives Reconfigure (which rebuilds the simulation).
    void SetBotDifficulty(BotDifficulty d) { botDifficulty = d; sim.bots.SetDifficulty(d); }
    // The host's weather choices; they apply to the next match (and survive Reconfigure).
    void SetWeatherOptions(const WeatherOptions& o) { weatherOptions = o; sim.match.SetWeatherOptions(o); }
    const WeatherOptions& GetWeatherOptions() const { return weatherOptions; }
    BotDifficulty GetBotDifficulty() const { return botDifficulty; }
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
        uint32_t tunic = SkinRgb(0);
        uint8_t epoch = 0;
        uint16_t lastSeq = 0;
        bool hasSeq = false;
        float lastInputClock = 0;
        bool isHost = false;
        bool ready = false;
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
                if (!sim.match.PickUp(c->playerId, m.index, m.force)) stats.rejectedActions++;
                break;
            }
            case net::MsgType::PropSmashRequest: {
                net::PropSmashRequest m;
                if (!net::Decode(data, m)) { stats.badPackets++; break; }
                if (!SmashProp(c->playerId, m.index)) stats.rejectedActions++;
                break;
            }
            case net::MsgType::SelectMapRequest: {
                net::SelectMapRequest m;
                if (!net::Decode(data, m)) { stats.badPackets++; break; }
                if (!c->isHost || !SelectMap(m.map)) stats.rejectedActions++;
                break;
            }
            case net::MsgType::RematchRequest: {
                net::RematchRequest m;
                if (!net::Decode(data, m)) { stats.badPackets++; break; }
                if (!c->isHost || !PlayAgain()) stats.rejectedActions++;
                break;
            }
            case net::MsgType::UseShieldRequest: {
                net::UseShieldRequest m;
                if (!net::Decode(data, m)) { stats.badPackets++; break; }
                if (!sim.match.UseShield(c->playerId)) stats.rejectedActions++;
                break;
            }
            case net::MsgType::SelectWeaponRequest: {
                net::SelectWeaponRequest m;
                if (!net::Decode(data, m)) { stats.badPackets++; break; }
                if (!sim.match.SelectWeapon(c->playerId, m.slot)) stats.rejectedActions++;
                break;
            }
            case net::MsgType::HireAllyRequest: {
                net::HireAllyRequest m;
                if (!net::Decode(data, m)) { stats.badPackets++; break; }
                if (!sim.match.HireAlly(c->playerId, m.index)) stats.rejectedActions++;
                break;
            }
            case net::MsgType::UseAbilityRequest: {
                net::UseAbilityRequest m;
                if (!net::Decode(data, m)) { stats.badPackets++; break; }
                if (!sim.match.UseAbility(c->playerId)) stats.rejectedActions++;
                break;
            }
            case net::MsgType::SetReady: {
                net::SetReady m;
                if (!net::Decode(data, m)) { stats.badPackets++; break; }
                // Ready flags only mean something while the lobby is open.
                if (sim.match.State() != MatchState::Lobby) { stats.rejectedActions++; break; }
                if (c->ready != m.ready) {
                    c->ready = m.ready;
                    net::EvReady ev;
                    ev.id = static_cast<uint16_t>(c->playerId);
                    ev.ready = m.ready;
                    Broadcast(ev);
                }
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

    static uint8_t RosterFlags(const Client& c) {
        return static_cast<uint8_t>((c.isHost ? net::kRosterHost : 0) | (c.ready ? net::kRosterReady : 0));
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
        if (HumanCount() >= playerLimit || id == kNoPlayer || !sim.match.AddHuman(id)) { Reject(c, net::RejectReason::LobbyFull); return; }

        c.joined = true;
        c.playerId = id;
        c.tunic = hello.tunic;
        c.name = net::SanitizeName(hello.name);
        if (c.name.empty()) c.name = "Player " + std::to_string(id);
        bool haveHost = false;
        for (const auto& o : clients) haveHost |= o.joined && o.isHost;
        c.isHost = hostToken != 0 && hello.hostToken == hostToken && !haveHost;
        c.lastInputClock = clock;

        net::Welcome w;
        w.playerId = static_cast<uint16_t>(id);
        w.seed = sim.match.Seed();
        w.mapId = static_cast<uint8_t>(mapId);
        w.limit = static_cast<uint8_t>(playerLimit);
        w.map = mapCircle;
        w.stormEnds = sim.match.GetStorm().PhaseEnds();
        for (const auto& l : sim.match.Loot()) w.loot.push_back(ToNet(l));
        w.props = props;
        w.pois = pois;
        for (const auto& o : clients) if (o.joined) w.roster.push_back({static_cast<uint16_t>(o.playerId), RosterFlags(o), o.name, o.tunic});
        SendTo(c, w);

        net::EvPlayerJoined joined;
        joined.id = static_cast<uint16_t>(id);
        joined.flags = RosterFlags(c);
        joined.name = c.name;
        joined.tunic = c.tunic;
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
        if (in.anim == static_cast<uint8_t>(Anim::Roll) && sim.match.CanRoll(*p)) sim.match.StartRoll(p->id); // a human's roll counts from the moment it starts
        p->scene = in.scene;
    }

    static net::LootNet ToNet(const LootEntry& l) {
        net::LootNet n;
        n.x = l.spawn.pos.x; n.z = l.spawn.pos.z;
        n.item = static_cast<uint8_t>(l.spawn.item);
        n.rarity = static_cast<uint8_t>(l.spawn.rarity);
        n.chest = l.spawn.container; // the client draws these as treasure chests
        n.taken = l.taken;
        n.special = l.spawn.special;
        n.supply = l.spawn.supply;
        n.amount = l.spawn.amount;
        return n;
    }

    void Step() {
        if (sim.match.State() == MatchState::Lobby && autoStartSec > 0 && HumanCount() >= 1) {
            lobbyElapsed += kStep;
            // The host's game normally starts the match when the timer runs out (it has to measure the map first). If that never
            // happens, the server does it with the map it has.
            if (lobbyElapsed >= autoStartSec + kLobbyStartGraceSec) StartMatch();
        }
        sim.Tick(kStep);
        clock += kStep;
        tick++;
        BroadcastEvents();
        SendInventories();
        SendSnapshots();
    }

    void BroadcastEvents() {
        for (const MatchEvent& e : sim.match.DrainEvents()) {
            switch (e.type) {
                case MatchEvent::Type::StateChanged: {
                    net::MatchStateMsg m;
                    m.state = static_cast<uint8_t>(e.state);
                    m.alive = static_cast<uint8_t>(sim.match.Alive());
                    m.limit = static_cast<uint8_t>(playerLimit);
                    if (const PlayerState* w = sim.match.Winner()) m.winner = static_cast<uint16_t>(w->id);
                    Broadcast(m);
                    if (e.state == MatchState::Ending) {
                        net::EvResults res;
                        for (const auto& s : sim.match.Standings()) {
                            net::ResultRow row;
                            row.id = static_cast<uint16_t>(s.id);
                            row.placement = static_cast<uint8_t>(s.placement);
                            row.kills = static_cast<uint8_t>((std::min)(s.kills, 255));
                            row.chests = static_cast<uint8_t>((std::min)(s.chests, 255));
                            row.damageTenths = static_cast<uint16_t>((std::min)(s.damage * 10.0f, 65535.0f));
                            row.score = static_cast<uint32_t>(s.score);
                            res.rows.push_back(row);
                        }
                        Broadcast(res);
                        SendReplay();
                    }
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
                case MatchEvent::Type::AbilityUsed: {
                    net::EvAbility a;
                    a.user = static_cast<uint16_t>(e.a);
                    a.item = e.item;
                    a.x = e.x;
                    a.z = e.z;
                    Broadcast(a);
                    break;
                }
                case MatchEvent::Type::BossDown: {
                    net::EvBossDown d;
                    d.boss = static_cast<uint16_t>(e.a);
                    d.killer = static_cast<uint16_t>(e.b);
                    d.x = e.x; d.z = e.z;
                    Broadcast(d);
                    break;
                }
                case MatchEvent::Type::Strike: {
                    net::EvStrike st;
                    st.by = static_cast<uint16_t>(e.a);
                    st.x = e.x; st.z = e.z; st.radius = e.amount; st.delay = e.health;
                    Broadcast(st);
                    break;
                }
                case MatchEvent::Type::AllyChanged: {
                    net::EvAlly a;
                    a.index = static_cast<uint8_t>(e.a); a.owner = e.b == kNoPlayer ? net::kNoPlayer16 : static_cast<uint16_t>(e.b); a.status = e.item;
                    Broadcast(a);
                    break;
                }
                case MatchEvent::Type::AllyAction: {
                    net::EvAllyAction a;
                    a.index = static_cast<uint8_t>(e.a); a.target = e.b == kNoPlayer ? net::kNoPlayer16 : static_cast<uint16_t>(e.b); a.x = e.x; a.z = e.z;
                    Broadcast(a);
                    break;
                }
                case MatchEvent::Type::Weather: {
                    net::EvWeather w;
                    w.season = static_cast<uint8_t>(e.a); w.sky = e.item; w.intensity = static_cast<uint8_t>(e.amount); w.seconds = e.health;
                    Broadcast(w);
                    break;
                }
                case MatchEvent::Type::SupplyDrop: {
                    net::EvSupplyDrop sd;
                    sd.x = e.x; sd.z = e.z; sd.delay = e.health;
                    Broadcast(sd);
                    break;
                }
                case MatchEvent::Type::BossSpawned: {
                    net::EvBossSpawn sp;
                    sp.boss = static_cast<uint16_t>(e.a);
                    const MiniBoss* b = sim.match.FindBoss(e.a);
                    sp.kind = b ? static_cast<uint8_t>(b->kind) : static_cast<uint8_t>(BossKind::DragonFire);
                    sp.x = e.x; sp.z = e.z;
                    Broadcast(sp);
                    break;
                }
                case MatchEvent::Type::Revived: {
                    net::EvAbility a;
                    a.user = static_cast<uint16_t>(e.a);
                    a.item = net::kRevivedItem;
                    if (const PlayerState* p = sim.match.Find(e.a)) { a.x = p->pos.x; a.z = p->pos.z; }
                    Broadcast(a);
                    break;
                }
                case MatchEvent::Type::Teleported: {
                    // The server moved this player (Hookshot pull, Farore's Wind, Nocturne). Bumping the epoch makes the owner's game
                    // jump there too, and makes the server ignore the position the client reports until it has seen the move.
                    if (Client* t = FindByPlayer(e.a)) { t->epoch++; t->lastInputClock = clock; }
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

    // The replay of the match, to everybody: a header, then the frames a few at a time.
    void SendReplay() {
        const Replay& rp = sim.match.GetReplay();
        if (!rp.Valid()) return;
        net::EvReplayHeader head;
        head.frames = static_cast<uint16_t>(rp.frames.size());
        head.ids = rp.ids;
        for (const ReplayKill& k : rp.kills) if (k.frame < head.frames) head.kills.push_back(k);
        Broadcast(head);
        const size_t perChunk = (std::max<size_t>)(1, 1200 / ((std::max<size_t>)(1, rp.ids.size()) * 4));
        for (size_t first = 0; first < rp.frames.size(); first += perChunk) {
            net::EvReplayChunk c;
            c.first = static_cast<uint16_t>(first);
            c.players = static_cast<uint8_t>(rp.ids.size());
            for (size_t i = first; i < rp.frames.size() && i < first + perChunk; i++) c.frames.push_back(rp.frames[i]);
            Broadcast(c);
        }
    }

    // Each player's own inventory goes to them (and nobody else) whenever it changes.
    void SendInventories() {
        const float now = sim.match.Clock();
        for (auto& c : clients) {
            if (!c.joined) continue;
            PlayerState* p = sim.match.Find(c.playerId);
            if (!p || !p->dirty) continue;
            p->dirty = false;
            net::EvInventory inv;
            inv.maxHealth = p->maxHealth;
            inv.heartPieces = static_cast<uint8_t>(p->heartPieces);
            inv.rupees = static_cast<uint16_t>((std::min)(p->rupees, 65535));
            for (int k = 0; k < kAmmoKinds; k++) inv.ammo[static_cast<size_t>(k)] = static_cast<uint8_t>((std::min)(p->ammo[static_cast<size_t>(k)], 255));
            for (const Equipped& e : p->potions) inv.potions.push_back({static_cast<uint8_t>(e.item), static_cast<uint8_t>(e.rarity)});
            for (const Equipped& e : p->reserve) inv.reserve.push_back({static_cast<uint8_t>(e.item), static_cast<uint8_t>(e.rarity)});
            inv.hasAbility = p->hasAbility;
            inv.ability = {static_cast<uint8_t>(p->ability.item), static_cast<uint8_t>(p->ability.rarity)};
            inv.abilityReadyIn = (std::max)(0.0f, p->abilityReadyAt - now);
            inv.hasMark = p->hasMark;
            inv.gearMask = p->gearMask;
            for (int i = 0; i < kGearSlots; i++) inv.gear[i] = {static_cast<uint8_t>(p->gear[i].item), static_cast<uint8_t>(p->gear[i].rarity)};
            inv.invulnLeft = (std::max)(0.0f, p->invulnUntil - now);
            inv.speedLeft = (std::max)(0.0f, p->speedUntil - now);
            inv.speedMult = p->speedMult;
            inv.revealLeft = (std::max)(0.0f, p->revealUntil - now);
            inv.stunLeft = (std::max)(0.0f, (std::max)(p->stunUntil, p->frozenUntil) - now);
            inv.burnLeft = (std::max)(0.0f, p->burnUntil - now);
            inv.regenLeft = (std::max)(0.0f, p->regenUntil - now);
            inv.shield = p->armor;
            inv.magic = sim.match.MagicNow(*p);
            inv.adultLeft = (std::max)(0.0f, p->adultUntil - now);
            inv.shieldLeft = (std::max)(0.0f, p->dmgTakenUntil - now);
            SendTo(c, inv);
        }
    }

    net::PlayerNet ToNet(const PlayerState& p) const {
        net::PlayerNet n;
        n.id = static_cast<uint16_t>(p.id);
        n.x = p.pos.x; n.z = p.pos.z; n.y = p.y;
        n.rot = p.rot;
        n.health = net::PlayerNet::QuantizeHealth(p.health);
        n.flags = static_cast<uint8_t>((p.alive ? net::PlayerNet::kAlive : 0) | (p.hasShield ? net::PlayerNet::kShield : 0) |
                                       (p.isBot ? net::PlayerNet::kBot : 0) | (sim.match.Clock() < p.adultUntil ? net::PlayerNet::kAdult : 0));
        n.weapon = static_cast<uint8_t>(p.weapon.item);
        n.weaponRarity = static_cast<uint8_t>(p.weapon.rarity);
        n.potions = static_cast<uint8_t>(p.potions.size());
        n.anim = p.anim;
        n.scene = p.scene;
        n.shield = static_cast<uint8_t>(p.shield.item);
        n.shieldRarity = static_cast<uint8_t>(p.shield.rarity);
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
            { const float left = LobbyLeft(); s.lobbyLeft = left < 0 ? 255 : static_cast<uint8_t>((std::min)(254.0f, std::ceil(left))); }
            s.players.push_back(ToNet(*self));

            std::vector<std::pair<float, const PlayerState*>> nearby;
            for (const auto& o : players) {
                if (o.id == self->id || !o.alive) continue;
                nearby.push_back({Distance(self->pos, o.pos), &o});
            }
            // Normally just the nearest few; while a Lens of Truth or Saria's Song is active, everybody.
            size_t keep = (std::min)(nearby.size(), sim.match.Revealing(*self) ? static_cast<size_t>(kMaxPlayers) : net::kSnapshotMaxPlayers);
            std::partial_sort(nearby.begin(), nearby.begin() + static_cast<long>(keep), nearby.end(),
                              [](const auto& a, const auto& b) { return a.first < b.first || (a.first == b.first && a.second->id < b.second->id); });
            for (size_t i = 0; i < keep; i++) s.players.push_back(ToNet(*nearby[i].second));
            for (const MiniBoss& b : sim.match.Bosses()) {
                if (!b.alive || (!IsDragonKind(b.kind) && Distance(self->pos, b.pos) > 4500.0f)) continue; // only the ones that could matter to this player (the dragon is always visible)
                net::BossNet n;
                n.index = static_cast<uint8_t>(b.id - kBossIdBase);
                n.kind = static_cast<uint8_t>(b.kind);
                n.x = b.pos.x; n.z = b.pos.z; n.rot = b.rot;
                n.hp = static_cast<uint8_t>((std::max)(0.0f, (std::min)(255.0f, b.health / b.maxHealth * 255.0f + 0.5f)));
                n.smashing = sim.match.Clock() - b.lastSmashAt < 0.4f;
                n.y = static_cast<int16_t>(std::lround((std::max)(0.0f, (std::min)(b.y, 3000.0f))));
                n.mode = static_cast<uint8_t>(b.mode);
                s.bosses.push_back(n);
            }
            for (const AllyState& a : sim.match.Allies()) {
                if (!a.alive || (a.owner != c.playerId && Distance(self->pos, a.pos) > 4500.0f)) continue;
                net::AllyNet n;
                n.index = a.index; n.kind = static_cast<uint8_t>(a.kind);
                n.x = a.pos.x; n.z = a.pos.z; n.rot = a.rot;
                n.hp = static_cast<uint8_t>((std::max)(0.0f, (std::min)(255.0f, a.health / AllyOf(a.kind).maxHealth * 255.0f + 0.5f)));
                n.owner = a.Hired() ? static_cast<uint16_t>(a.owner) : net::kNoPlayer16;
                n.flags = static_cast<uint8_t>((a.moving ? 1 : 0) | (sim.match.Clock() < a.actUntil ? 2 : 0));
                s.allies.push_back(n);
            }
            SendTo(c, s, false);
        }
    }

    net::Transport& link;
    Simulation sim;
    BotDifficulty botDifficulty = BotDifficulty::Normal;
    WeatherOptions weatherOptions;
    int mapId = 0;
    bool majorBoss = true;
    std::vector<Prop> props;
    std::vector<bool> broken;   // which props have been smashed, by prop index
    std::vector<Poi> pois;
    int propCount = 560;
    int poiCount = 12;
    int bossCount = 0;
    int playerLimit = kMaxPlayers;
    float autoStartSec = 0;
    float lobbyElapsed = 0;
    PlacementFn lastValid;
    int lastLootCount = 150;
    Circle mapCircle;
    std::vector<Client> clients;
    Stats stats;
    float accumulator = 0;
    float clock = 0;
    uint32_t tick = 0;
    uint64_t hostToken = 0;
};

} // namespace royale
