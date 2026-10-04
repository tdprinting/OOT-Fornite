#pragma once
// Everything about hosting or joining a Royale match that does NOT touch game code: transports, the host's server, the
// local client, and plain-data views for the game layer to render. Kept free of game headers so it builds and is unit-tested
// anywhere (see server/tests/session_tests.cpp); RoyaleMod.cpp is the thin layer that connects it to the engine.
#include "enet_transport.h"
#include "game_client.h"
#include "game_server.h"
#include "map.h"
#include <algorithm>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace royale {

// One remote player (human or bot) as the renderer should draw it right now.
struct PuppetState {
    uint16_t id = 0;
    std::string name;
    float x = 0, y = 0, z = 0;
    int16_t rot = 0;
    uint8_t anim = 0;
    uint8_t scene = 0; // game scene this player is in; only draw them if it matches yours
    bool alive = true;
    bool isBot = false;
    float health = kMaxHealth;
    ItemId weapon = ItemId::DekuStick;
    Rarity weaponRarity = Rarity::Common;
};

// One line of the lobby list.
struct RosterRow {
    uint16_t id = 0;
    std::string name;
    bool host = false;
    bool ready = false;
    bool self = false;
};

struct HudState {
    enum class Mode : uint8_t { Idle, Hosting, Joined } mode = Mode::Idle;
    bool connected = false;      // joined the host's lobby/match
    std::string status;          // human-readable, for the menu
    MatchState state = MatchState::Lobby;
    int alive = 0;
    uint16_t selfId = 0;
    bool haveSelf = false;
    float selfHealth = kMaxHealth;
    bool selfAlive = true;
    int potions = 0;
    Circle map;
    Circle safeZone;
    float stormDamagePerSecond = 0;      // at the local player's position
    std::vector<RosterRow> roster;
    uint16_t hostPort = 0;
    int humanCount = 0;           // people in the lobby (hosts count the server's view)
    int botSlots = 0;             // empty slots the host's Start will fill with bots
    bool isHost = false;
    bool selfReady = false;
    float countdownLeft = 0;      // seconds until the drop, while the state is Countdown
    uint16_t winnerId = 0xFFFF;   // once the match has ended
    std::string winnerName;       // "You" is left to the UI; bots are named "Bot N"
};

class RoyaleSession {
  public:
    enum class Mode : uint8_t { Idle, Hosting, Joined };

    // Start a match server on `port` and join it as the host's own player (through 127.0.0.1, like everyone else).
    bool Host(uint16_t port, const std::string& playerName, std::string* error = nullptr) {
        Leave();
        std::string err;
        hostTransport = net::ENetTransport::Host(port, kMaxPlayers, &err);
        if (!hostTransport) { Fail(error, err); return false; }
        std::random_device rd;
        uint64_t seed = (static_cast<uint64_t>(rd()) << 32) ^ rd();
        server = std::make_unique<GameServer>(*hostTransport, seed, kHyruleFieldMap);
        // A secret only this process knows: the server uses it to recognise the host's own player.
        uint64_t token = (static_cast<uint64_t>(rd()) << 32) ^ rd();
        if (token == 0) token = 1;
        server->SetHostToken(token);
        clientTransport = net::ENetTransport::Connect("127.0.0.1", hostTransport->Port(), &err);
        if (!clientTransport) { Leave(); Fail(error, err); return false; }
        client = std::make_unique<GameClient>(*clientTransport, playerName, token);
        mode = Mode::Hosting;
        return true;
    }

    // Join someone else's match.
    bool Join(const std::string& address, uint16_t port, const std::string& playerName, std::string* error = nullptr) {
        Leave();
        std::string err;
        clientTransport = net::ENetTransport::Connect(address, port, &err);
        if (!clientTransport) { Fail(error, err); return false; }
        client = std::make_unique<GameClient>(*clientTransport, playerName);
        mode = Mode::Joined;
        return true;
    }

    // Close everything. Safe to call at any time, including when idle.
    void Leave() {
        if (client) client->Leave();
        client.reset();
        clientTransport.reset();
        server.reset();
        hostTransport.reset();
        mode = Mode::Idle;
    }

    // Host presses Start. Needs at least one human in the lobby; the rest of the 32 slots fill with bots.
    bool StartMatch() { return mode == Mode::Hosting && server && server->StartMatch(); }

    // Call once per rendered frame with the real elapsed time. The server steps itself at a fixed 20 Hz inside.
    void Update(float dt) {
        if (server) server->Update(dt);
        if (client) client->Update(dt);
        if (client && client->GetStatus() == GameClient::Status::Disconnected) {
            // Host vanished or we were kicked: tear down so the menu offers Host/Join again.
            lastEnded = "Disconnected from host";
            Leave();
        } else if (client && client->GetStatus() == GameClient::Status::Rejected) {
            lastEnded = RejectText(client->RejectedBecause());
            Leave();
        }
    }

    // Game to server.
    void SendLocalPose(float x, float y, float z, int16_t rot, uint8_t anim, uint8_t scene) { if (Joined()) client->SendInput(x, y, z, rot, anim, scene); }
    // Lobby only.
    void SetReady(bool ready) { if (Joined()) client->SetReady(ready); }
    void ReportAttack(uint16_t target, bool hit) { if (Joined()) client->ReportAttack(target, hit); }
    void RequestPickup(uint32_t lootIndex) { if (Joined()) client->RequestPickup(lootIndex); }
    void RequestUsePotion() { if (Joined()) client->RequestUsePotion(); }

    // Server to game.
    std::vector<PuppetState> Puppets() const {
        std::vector<PuppetState> out;
        if (!Joined()) return out;
        for (uint16_t id : client->VisiblePlayers()) {
            net::PlayerNet p;
            if (!client->Sample(id, p)) continue;
            PuppetState s;
            s.id = id;
            auto it = client->Roster().find(id);
            s.name = it != client->Roster().end() ? it->second.name : (p.flags & net::PlayerNet::kBot ? "Bot " + std::to_string(id) : "Player " + std::to_string(id));
            s.x = p.x; s.y = p.y; s.z = p.z; s.rot = p.rot; s.anim = p.anim; s.scene = p.scene;
            s.alive = p.flags & net::PlayerNet::kAlive;
            s.isBot = p.flags & net::PlayerNet::kBot;
            s.health = p.Health();
            s.weapon = static_cast<ItemId>(p.weapon);
            s.weaponRarity = static_cast<Rarity>(p.weaponRarity);
            out.push_back(std::move(s));
        }
        return out;
    }

    HudState Hud() const {
        HudState h;
        h.mode = mode == Mode::Hosting ? HudState::Mode::Hosting : mode == Mode::Joined ? HudState::Mode::Joined : HudState::Mode::Idle;
        h.hostPort = hostTransport ? hostTransport->Port() : 0;
        if (!client) {
            h.status = lastEnded.empty() ? "Not in a match" : lastEnded;
            return h;
        }
        h.connected = client->GetStatus() == GameClient::Status::Joined;
        h.status = h.connected ? "Connected" : "Connecting...";
        h.state = client->State();
        h.alive = client->AliveCount();
        h.selfId = client->PlayerId();
        h.map = client->Map();
        h.safeZone = client->SafeZone();
        for (const auto& [id, info] : client->Roster()) {
            RosterRow row;
            row.id = id; row.name = info.name; row.host = info.host; row.ready = info.ready; row.self = id == client->PlayerId();
            h.isHost = h.isHost || (row.self && row.host);
            h.selfReady = h.selfReady || (row.self && row.ready);
            h.roster.push_back(std::move(row));
        }
        h.humanCount = static_cast<int>(h.roster.size());
        h.botSlots = kMaxPlayers - h.humanCount;
        if (h.state == MatchState::Countdown) h.countdownLeft = (std::max)(0.0f, kCountdownSec - client->StateElapsed());
        h.winnerId = client->Winner();
        if (h.winnerId != net::kNoPlayer16) {
            auto w = client->Roster().find(h.winnerId);
            h.winnerName = w != client->Roster().end() ? w->second.name : "Bot " + std::to_string(h.winnerId - 999);
        }
        if (const net::PlayerNet* self = client->Self()) {
            h.haveSelf = true;
            h.selfHealth = self->Health();
            h.selfAlive = self->flags & net::PlayerNet::kAlive;
            h.potions = self->potions;
            h.stormDamagePerSecond = client->StormDamagePerSecond({self->x, self->z});
        }
        return h;
    }

    Mode GetMode() const { return mode; }
    bool Joined() const { return client && client->GetStatus() == GameClient::Status::Joined; }
    GameClient* Client() { return client.get(); }
    GameServer* Server() { return server.get(); }
    // Why the last session ended, shown in the menu (empty if it did not end abnormally).
    const std::string& LastEnded() const { return lastEnded; }
    void ClearLastEnded() { lastEnded.clear(); }

    ~RoyaleSession() { Leave(); }

  private:
    static void Fail(std::string* error, const std::string& why) { if (error) *error = why; }
    static std::string RejectText(net::RejectReason r) {
        switch (r) {
            case net::RejectReason::VersionMismatch: return "Host runs a different version of the mod";
            case net::RejectReason::LobbyFull: return "Lobby is full";
            case net::RejectReason::MatchInProgress: return "Match already started";
            default: return "Host refused the connection";
        }
    }

    Mode mode = Mode::Idle;
    // Order matters: clients are destroyed before the transports they use.
    std::unique_ptr<net::ENetTransport> hostTransport;
    std::unique_ptr<GameServer> server;
    std::unique_ptr<net::ENetTransport> clientTransport;
    std::unique_ptr<GameClient> client;
    std::string lastEnded;
};

} // namespace royale
