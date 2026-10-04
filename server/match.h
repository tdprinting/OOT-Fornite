#pragma once
#include "../shared/balance.h"
#include "../shared/storm.h"
#include <vector>

namespace royale {

enum class MatchState : uint8_t { Lobby, Countdown, Drop, InMatch, Ending };

struct PlayerState {
    uint32_t id = 0;
    bool isBot = false;
    bool alive = true;
    float health = 3.0f; // hearts
    Vec2 pos = {};
};

// The server-side match. No networking and no game code in here, so it runs inside the host's game, headless,
// or in unit tests. Feed it Tick(dt) at kTickHz and call the event methods as messages arrive.
class Match {
  public:
    static constexpr float kCountdownSec = 10.0f;
    static constexpr float kDropSec = 5.0f; // spawn invulnerability
    static constexpr float kEndingSec = 10.0f;

    Match(uint64_t seed, Circle map) : seed(seed), map(map), storm(seed, map) {}

    // Add a human. Returns false if the lobby is full or the match already started.
    bool AddHuman(uint32_t id) {
        if (state != MatchState::Lobby || players.size() >= kMaxPlayers) return false;
        players.push_back(PlayerState{id, false});
        return true;
    }

    // Start with at least one human; every remaining slot up to kMaxPlayers is filled with a bot.
    bool Start() {
        if (state != MatchState::Lobby || players.empty()) return false;
        humans = static_cast<int>(players.size());
        uint32_t nextId = 1000;
        while (players.size() < kMaxPlayers) players.push_back(PlayerState{nextId++, true});
        state = MatchState::Countdown;
        stateTime = 0;
        return true;
    }

    void Tick(float dt) {
        stateTime += dt;
        switch (state) {
            case MatchState::Countdown:
                if (stateTime >= kCountdownSec) Enter(MatchState::Drop);
                break;
            case MatchState::Drop:
                if (stateTime >= kDropSec) Enter(MatchState::InMatch);
                break;
            case MatchState::InMatch:
                stormTime += dt;
                for (auto& p : players) {
                    if (!p.alive) continue;
                    float dps = storm.DamagePerSecond(p.pos, stormTime);
                    if (dps > 0) Damage(p.id, dps * dt);
                }
                if (Alive() <= 1) Enter(MatchState::Ending);
                break;
            case MatchState::Ending:
            case MatchState::Lobby:
                break;
        }
    }

    // Returns true if the player was eliminated by this damage. Ignored during the drop (spawn protection).
    bool Damage(uint32_t id, float hearts) {
        PlayerState* p = Find(id);
        if (!p || !p->alive || state == MatchState::Drop || hearts <= 0) return false;
        p->health -= hearts;
        if (p->health <= 0) {
            p->health = 0;
            p->alive = false;
            return true;
        }
        return false;
    }

    int Alive() const {
        int n = 0;
        for (const auto& p : players) n += p.alive;
        return n;
    }
    // Only meaningful once the match is Ending.
    const PlayerState* Winner() const {
        if (state != MatchState::Ending) return nullptr;
        for (const auto& p : players) if (p.alive) return &p;
        return nullptr;
    }

    PlayerState* Find(uint32_t id) {
        for (auto& p : players) if (p.id == id) return &p;
        return nullptr;
    }

    MatchState State() const { return state; }
    int Humans() const { return humans; }
    float StormTime() const { return stormTime; }
    const Storm& GetStorm() const { return storm; }
    const std::vector<PlayerState>& Players() const { return players; }
    std::vector<PlayerState>& Players() { return players; }
    uint64_t Seed() const { return seed; }

  private:
    void Enter(MatchState s) { state = s; stateTime = 0; }

    uint64_t seed;
    Circle map;
    Storm storm;
    MatchState state = MatchState::Lobby;
    float stateTime = 0, stormTime = 0;
    int humans = 0;
    std::vector<PlayerState> players;
};

} // namespace royale
