#pragma once
#include "bot.h"
#include "match.h"

namespace royale {

// A match plus the bots that play in it. The host's game loop (or a headless server) calls Tick(dt) at kTickHz.
class Simulation {
  public:
    Simulation(uint64_t seed, Circle map, int lootCount = 400) : match(seed, map, lootCount), bots(seed) {}

    void Tick(float dt) {
        bots.Step(match, dt);
        match.Tick(dt);
    }

    Match match;
    BotController bots;
};

} // namespace royale
