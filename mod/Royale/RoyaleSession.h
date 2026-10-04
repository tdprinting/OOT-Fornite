#pragma once
// Everything about hosting or joining a Royale match that does NOT touch game code: transports, the host's server, the
// local client, and plain-data views for the game layer to render. Kept free of game headers so it builds and is unit-tested
// anywhere (see server/tests/session_tests.cpp); RoyaleMod.cpp is the thin layer that connects it to the engine.
#include "enet_transport.h"
#include "game_client.h"
#include "game_server.h"
#include "map.h"
#include "names.h"
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
    ItemId weapon = ItemId::BasicSword;
    Rarity weaponRarity = Rarity::Common;
    uint32_t tunic = SkinRgb(0); // 0xRRGGBB, the colour this player chose (bots wear a preset by id)
};

// One line of the lobby list.
struct RosterRow {
    uint16_t id = 0;
    std::string name;
    bool host = false;
    bool ready = false;
    bool self = false;
};

// One line of the end-of-match standings.
struct ResultsRow {
    uint16_t id = 0;
    std::string name;
    bool self = false, isBot = false;
    int placement = 0, kills = 0, chests = 0, score = 0;
    float damage = 0;
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
    int stormPhase = 0;                  // 0-based; kStormPhaseCount once the storm has finished
    bool stormShrinking = false;
    float stormSecondsLeft = 0;          // until the hold ends / the shrink ends
    ItemId weapon = ItemId::BasicSword;   // what the local player holds (from the server)
    Rarity weaponRarity = Rarity::Common;
    bool hasShield = false;
    ItemId shield = ItemId::DekuShield;
    Rarity shieldRarity = Rarity::Common;
    float selfX = 0, selfZ = 0;          // the server's view of where you are
    float maxHealth = kMaxHealth;        // grows with Heart Containers
    InventoryInfo inv;                   // bag, ability, gear and timed effects (raw; use the *Left fields below)
    float abilityReadyIn = 0;            // seconds until the ability can be used again (0 = ready)
    float magic = kMaxMagic;             // the magic meter now (0 to kMaxMagic); abilities spend it
    float invulnLeft = 0, speedLeft = 0, revealLeft = 0, stunLeft = 0, burnLeft = 0, shieldLeft = 0;
    float speedMult = 1;                 // movement speed multiplier from gear and songs (1 = normal)
    std::vector<RosterRow> roster;
    uint16_t hostPort = 0;
    int humanCount = 0;           // people in the lobby (hosts count the server's view)
    int botSlots = 0;             // empty slots the host's Start will fill with bots
    bool isHost = false;
    bool selfReady = false;
    float countdownLeft = 0;      // seconds until the drop, while the state is Countdown
    int playerLimit = kMaxPlayers; // players in the match, bots included (the host's slider)
    int mapId = 0;                 // which place the match is played in (shared/map.h)
    int rupees = 0;
    Weather weather;               // the sky right now (season, kind, strength)
    float weatherSecondsLeft = 0;  // until the next spell
    std::array<int, kAmmoKinds> ammo = {};
    float lobbyLeft = -1;          // seconds until the lobby starts the match by itself; -1 when there is no timer
    uint16_t winnerId = 0xFFFF;   // once the match has ended
    std::string winnerName;       // "You" is left to the UI; bots have names from shared/names.h
    std::vector<ResultsRow> results; // standings, best score first, once the match has ended
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
        server->SetBotDifficulty(botDifficulty);
        server->SetBossCount(5);
        server->SetAutoStart(autoStart);
        if (playerLimit != kMaxPlayers) server->SetPlayerLimit(playerLimit);
        server->SetMajorBoss(majorBoss);
        server->SetWeatherOptions(weatherOptions);
        if (selectedMap != 0) server->SelectMap(selectedMap);
        // A secret only this process knows: the server uses it to recognise the host's own player.
        uint64_t token = (static_cast<uint64_t>(rd()) << 32) ^ rd();
        if (token == 0) token = 1;
        server->SetHostToken(token);
        clientTransport = net::ENetTransport::Connect("127.0.0.1", hostTransport->Port(), &err);
        if (!clientTransport) { Leave(); Fail(error, err); return false; }
        client = std::make_unique<GameClient>(*clientTransport, playerName, token);
        client->SetTunic(tunic);
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
        client->SetTunic(tunic);
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

    // Host only, lobby only: rebuild the world on the measured map (see GameServer::Reconfigure).
    bool ConfigureMap(Circle map, PlacementFn valid = nullptr) { return mode == Mode::Hosting && server && server->Reconfigure(map, std::move(valid), 150, GameServer::FreshSeedOffset()); }

    // The tunic colour (0xRRGGBB) the other players see you in. Set before hosting or joining.
    void SetTunic(uint32_t rgb) { tunic = rgb; }
    // Host only, in the lobby: how many players the match has, bots included (2 to 32). Returns false if that isn't allowed right now.
    // Host only: the lobby starts the match by itself after this many seconds; 0 turns the timer off.
    void SetAutoStart(float seconds) { autoStart = seconds; if (server) server->SetAutoStart(seconds); }
    bool SetPlayerLimit(int n) { playerLimit = n; return server ? server->SetPlayerLimit(n) : false; }
    // Host: where the match is played, and whether the dragon turns up halfway through. Both are remembered for the next lobby too.
    void ReportPropSmashed(size_t index) { if (Joined()) client->ReportSmash(index); }
    bool SelectMap(int id) { selectedMap = ClampMap(id); return server ? server->SelectMap(selectedMap) : false; }
    void SetMajorBoss(bool on) { majorBoss = on; if (server) server->SetMajorBoss(on); }
    void SetWeatherOptions(const WeatherOptions& o) { weatherOptions = o; if (server) server->SetWeatherOptions(o); }
    void SetBotDifficulty(BotDifficulty d) { botDifficulty = d; if (server) server->SetBotDifficulty(d); }

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
    void RequestPickup(uint32_t lootIndex, bool force = false) { if (Joined()) client->RequestPickup(lootIndex, force); }
    void RequestUsePotion() { if (Joined()) client->RequestUsePotion(); }
    void UseAbility() { if (Joined()) client->UseAbility(); }
    void UseShield() { if (Joined()) client->UseShield(); }
    void SelectWeapon(int slot) { if (Joined()) client->SelectWeapon(slot); }
    // Host only: another match right away with everyone who is connected.
    void RequestPlayAgain() { if (Joined()) client->RequestRematch(); }

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
            s.name = it != client->Roster().end() ? it->second.name : (p.flags & net::PlayerNet::kBot ? BotName(id) : "Player " + std::to_string(id));
            s.tunic = it != client->Roster().end() ? it->second.tunic : BotTunic(id);
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

    // Things that happened since the last call (players joining, ready changes, state changes, eliminations...).
    std::vector<ClientEvent> DrainEvents() { return client ? client->DrainEvents() : std::vector<ClientEvent>(); }

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
        {
            auto info = client->StormInfo();
            h.stormPhase = info.phase;
            h.stormShrinking = info.shrinking;
            h.stormSecondsLeft = info.secondsLeft;
        }
        for (const auto& [id, info] : client->Roster()) {
            RosterRow row;
            row.id = id; row.name = info.name; row.host = info.host; row.ready = info.ready; row.self = id == client->PlayerId();
            h.isHost = h.isHost || (row.self && row.host);
            h.selfReady = h.selfReady || (row.self && row.ready);
            h.roster.push_back(std::move(row));
        }
        h.humanCount = static_cast<int>(h.roster.size());
        h.playerLimit = client->PlayerLimit();
        h.mapId = client->MapId();
        h.weather = client->CurrentWeather();
        h.weatherSecondsLeft = client->WeatherSecondsLeft();
        h.rupees = client->Inventory().rupees;
        for (int k = 0; k < kAmmoKinds; k++) h.ammo[static_cast<size_t>(k)] = client->Inventory().ammo[static_cast<size_t>(k)];
        h.lobbyLeft = h.state == MatchState::Lobby ? client->LobbyLeft() : -1.0f;
        h.botSlots = (std::max)(0, h.playerLimit - h.humanCount);
        if (h.state == MatchState::Countdown) h.countdownLeft = (std::max)(0.0f, kCountdownSec - client->StateElapsed());
        for (const net::ResultRow& r : client->Results()) {
            ResultsRow row;
            row.id = r.id;
            row.isBot = r.id >= 1000;
            row.self = r.id == client->PlayerId();
            auto who = client->Roster().find(r.id);
            row.name = who != client->Roster().end() ? who->second.name : BotName(r.id);
            row.placement = r.placement; row.kills = r.kills; row.chests = r.chests; row.score = static_cast<int>(r.score);
            row.damage = r.damageTenths / 10.0f;
            h.results.push_back(std::move(row));
        }
        h.inv = client->Inventory();
        h.maxHealth = h.inv.maxHealth;
        h.abilityReadyIn = client->AbilityReadyIn();
        h.magic = client->MagicNow();
        h.invulnLeft = client->Left(h.inv.invulnLeft);
        h.speedLeft = client->Left(h.inv.speedLeft);
        h.revealLeft = client->Left(h.inv.revealLeft);
        h.stunLeft = client->Left(h.inv.stunLeft);
        h.burnLeft = client->Left(h.inv.burnLeft);
        h.shieldLeft = client->Left(h.inv.shieldLeft);
        h.speedMult = 1.0f;
        for (int slot = 0; slot < kGearSlots; slot++) {
            if (h.inv.gearMask & (1 << slot)) {
                h.speedMult *= Scaled(GearOf(static_cast<ItemId>(h.inv.gear[slot].item)).speed, static_cast<Rarity>(h.inv.gear[slot].rarity));
            }
        }
        if (h.speedLeft > 0) h.speedMult *= h.inv.speedMult;
        h.winnerId = client->Winner();
        if (h.winnerId != net::kNoPlayer16) {
            auto w = client->Roster().find(h.winnerId);
            h.winnerName = w != client->Roster().end() ? w->second.name : BotName(h.winnerId);
        }
        if (const net::PlayerNet* self = client->Self()) {
            h.haveSelf = true;
            h.selfHealth = self->Health();
            h.selfAlive = self->flags & net::PlayerNet::kAlive;
            h.potions = self->potions;
            h.weapon = static_cast<ItemId>(self->weapon);
            h.weaponRarity = static_cast<Rarity>(self->weaponRarity);
            h.hasShield = (self->flags & net::PlayerNet::kShield) != 0;
            h.shield = static_cast<ItemId>(self->shield);
            h.shieldRarity = static_cast<Rarity>(self->shieldRarity);
            h.selfX = self->x;
            h.selfZ = self->z;
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

    BotDifficulty botDifficulty = BotDifficulty::Normal;
    uint32_t tunic = SkinRgb(0);
    int playerLimit = kMaxPlayers;
    int selectedMap = 0;
    bool majorBoss = true;
    WeatherOptions weatherOptions;
    float autoStart = kLobbyAutoStartSec;
    Mode mode = Mode::Idle;
    // Order matters: clients are destroyed before the transports they use.
    std::unique_ptr<net::ENetTransport> hostTransport;
    std::unique_ptr<GameServer> server;
    std::unique_ptr<net::ENetTransport> clientTransport;
    std::unique_ptr<GameClient> client;
    std::string lastEnded;
};

} // namespace royale
