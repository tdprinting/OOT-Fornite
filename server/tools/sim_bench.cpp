// Headless server tick benchmark: one parked human and 31 bots play a real match (the same set-up as tools/balance_sim.cpp) and every
// Simulation::Tick is timed and its heap allocations counted. Prints mean / p99 / max ms per tick and allocations per tick, plus a
// fingerprint of the match (winner, kills, loot taken, ticks) so a speed change can be checked to leave the match itself identical.
//   royale_sim_bench [map 0-N] [seconds of match time, default 240] [seed]
#include "game_server.h"
#include "loopback.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

static bool tracking = false;
static size_t allocations = 0;
void* operator new(size_t n) {
    if (tracking) ++allocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

using namespace royale;
using royale::net::LoopbackNetwork;

int main(int argc, char** argv) {
    const int mapId = argc > 1 ? ClampMap(std::atoi(argv[1])) : 0;
    const float seconds = argc > 2 ? static_cast<float>(std::atof(argv[2])) : 240.0f;
    const uint64_t seed = argc > 3 ? static_cast<uint64_t>(std::atoll(argv[3])) : 1031;
    constexpr float kDt = 1.0f / kTickHz;
    LoopbackNetwork network(seed);
    const Circle fallback = MapOf(mapId).fallback;
    GameServer server(network.Server(), seed, fallback, 150);
    server.SetBotDifficulty(BotDifficulty::Normal);
    server.SetBossCount(5);
    server.SetPlayerLimit(kMaxPlayers);
    server.SelectMap(mapId);
    const Circle real = {fallback.center, std::min(fallback.radius, MapOf(mapId).maxRadius * 0.95f)};
    PlacementFn valid = [real](Vec2 p) { return Distance(p, real.center) <= real.radius; };
    server.Sim() = Simulation(seed, real, 0);   // SelectMap seeds the layout from the clock: start again from a fixed seed so runs compare
    server.Reconfigure(real, valid, 150, 0);
    Simulation& sim = server.Sim();
    sim.match.AddHuman(1);
    server.StartMatch();
    sim.match.Find(1)->alive = false;   // the placeholder human takes no part
    sim.match.Find(1)->placement = kMaxPlayers;
    std::vector<double> ms;
    size_t allocs = 0;
    const int maxTicks = static_cast<int>(seconds * kTickHz);
    for (int i = 0; i < maxTicks && sim.match.State() != MatchState::Ending; i++) {
        allocations = 0;
        tracking = true;
        const auto t0 = std::chrono::steady_clock::now();
        sim.Tick(kDt);
        const auto t1 = std::chrono::steady_clock::now();
        tracking = false;
        allocs += allocations;
        ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        sim.match.DrainEvents();
    }
    double sum = 0;
    for (double v : ms) sum += v;
    std::vector<double> sorted = ms;
    std::sort(sorted.begin(), sorted.end());
    const double p99 = sorted.empty() ? 0 : sorted[std::min(sorted.size() - 1, sorted.size() * 99 / 100)];
    int kills = 0, alive = 0, loot = 0;
    float px = 0;
    for (auto& p : sim.match.Players()) { kills += p.kills; alive += p.alive; px += p.pos.x + p.pos.z; }
    for (auto& e : sim.match.Loot()) loot += e.taken;
    const PlayerState* w = sim.match.Winner();
    std::printf("map %d: %zu ticks, mean %.3f ms/tick, p99 %.3f ms, max %.3f ms, %.1f allocations/tick\n", mapId, ms.size(), ms.empty() ? 0 : sum / ms.size(), p99,
                sorted.empty() ? 0 : sorted.back(), ms.empty() ? 0 : double(allocs) / ms.size());
    std::printf("fingerprint: winner=%d kills=%d alive=%d lootTaken=%d posSum=%.6f\n", w ? static_cast<int>(w->id) : -1, kills, alive, loot, px);
}
