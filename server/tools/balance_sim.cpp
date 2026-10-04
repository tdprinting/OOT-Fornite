// Headless balance simulator: plays full bot-only matches on the real server logic (the same world generation the game uses) and prints how
// they went, so the numbers in shared/balance.h, items.h, combat.h and boss.h can be tuned against data instead of a feeling.
//   royale_balance [matches] [easy|normal|hard] [map 0-4] [players 2-32]
#include "game_server.h"
#include "loopback.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <numeric>
#include <string>

using namespace royale;
using royale::net::LoopbackNetwork;
static constexpr float kDt = 1.0f / kTickHz;

struct MatchStats {
    float duration = 0;            // seconds from the first moment of the fight to the end
    int kills = 0, stormDeaths = 0, burnDeaths = 0, bossDeaths = 0;
    float winnerHealth = 0;
    int chestsOpened = 0;
    int rupeesEarned = 0;
    bool dragonSpawned = false, dragonKilled = false;
    float dragonKilledAt = 0;
    int bossesKilled = 0;
    std::map<std::string, int> deathWeapons;   // weapon in hand at death
    std::map<std::string, int> winWeapon;
    float firstKillAt = 1e9f;
    int aliveAt60 = 0, aliveAt180 = 0;
};

static MatchStats PlayOne(uint64_t seed, BotDifficulty difficulty, int mapId, int players) {
    LoopbackNetwork network(seed);
    const Circle fallback = MapOf(mapId).fallback;
    GameServer server(network.Server(), seed, fallback, 150);
    server.SetBotDifficulty(difficulty);
    server.SetBossCount(5);
    server.SetPlayerLimit(players);
    server.SelectMap(mapId);
    const Circle real = {fallback.center, std::min(fallback.radius, MapOf(mapId).maxRadius * 0.95f)};
    PlacementFn valid = [real](Vec2 p) { return Distance(p, real.center) <= real.radius; };
    server.Reconfigure(real, valid, 150, seed * 977);
    Simulation& sim = server.Sim();
    sim.match.AddHuman(1);
    server.StartMatch();
    sim.match.Find(1)->alive = false;      // the placeholder human takes no part
    sim.match.Find(1)->placement = players;
    MatchStats st;
    const int humans = 1;
    (void)humans;
    float t = 0;
    float fightStart = -1;
    std::map<uint32_t, std::string> lastWeapon;
    int guard = 0;
    while (sim.match.State() != MatchState::Ending && guard++ < 20 * 60 * 40) {
        sim.Tick(kDt);
        t += kDt;
        if (sim.match.State() == MatchState::InMatch && fightStart < 0) fightStart = t;
        for (auto& p : sim.match.Players()) if (p.alive) lastWeapon[p.id] = NameOf(p.weapon.item);
        for (const MatchEvent& e : sim.match.DrainEvents()) {
            if (e.type == MatchEvent::Type::Eliminated && e.a != 1) {
                if (e.b == kNoPlayer) {
                    const PlayerState* v = sim.match.Find(e.a);
                    (void)v;
                    st.stormDeaths++;
                } else if (IsBossId(e.b)) {
                    st.bossDeaths++;
                } else {
                    st.kills++;
                    if (st.firstKillAt > 1e8f) st.firstKillAt = t - fightStart;
                }
                st.deathWeapons[lastWeapon[e.a]]++;
            }
            if (e.type == MatchEvent::Type::BossSpawned) st.dragonSpawned = true;
            if (e.type == MatchEvent::Type::BossDown) { st.bossesKilled++; if (IsBossId(e.a) && e.a == kDragonId) { st.dragonKilled = true; st.dragonKilledAt = t - fightStart; } }
        }
        const float since = fightStart < 0 ? 0 : t - fightStart;
        if (fightStart >= 0 && st.aliveAt60 == 0 && since >= 60.0f) st.aliveAt60 = sim.match.Alive();
        if (fightStart >= 0 && st.aliveAt180 == 0 && since >= 180.0f) st.aliveAt180 = sim.match.Alive();
    }
    st.duration = fightStart < 0 ? 0 : t - fightStart;
    for (auto& p : sim.match.Players()) {
        if (p.alive) { st.winnerHealth = p.health; st.winWeapon[lastWeapon[p.id]]++; }
        st.chestsOpened += p.chestsOpened;
        st.rupeesEarned += p.rupees;
    }
    return st;
}

int main(int argc, char** argv) {
    const int matches = argc > 1 ? std::max(1, std::atoi(argv[1])) : 12;
    BotDifficulty difficulty = BotDifficulty::Normal;
    if (argc > 2) difficulty = !std::strcmp(argv[2], "easy") ? BotDifficulty::Easy : !std::strcmp(argv[2], "hard") ? BotDifficulty::Hard : BotDifficulty::Normal;
    const int mapId = argc > 3 ? ClampMap(std::atoi(argv[3])) : 0;
    const int players = argc > 4 ? std::clamp(std::atoi(argv[4]), kMinPlayers, kMaxPlayers) : kMaxPlayers;
    float total = 0, minD = 1e9f, maxD = 0, kills = 0, storm = 0, boss = 0, chests = 0, rupees = 0, first = 0, a60 = 0, a180 = 0;
    int dragonSpawn = 0, dragonKill = 0, draws = 0;
    std::map<std::string, int> deaths, wins;
    for (int i = 0; i < matches; i++) {
        const MatchStats s = PlayOne(1000 + static_cast<uint64_t>(i) * 31, difficulty, mapId, players);
        total += s.duration; minD = std::min(minD, s.duration); maxD = std::max(maxD, s.duration);
        kills += s.kills; storm += s.stormDeaths; boss += s.bossDeaths; chests += s.chestsOpened; rupees += s.rupeesEarned;
        first += s.firstKillAt > 1e8f ? s.duration : s.firstKillAt; a60 += s.aliveAt60; a180 += s.aliveAt180;
        dragonSpawn += s.dragonSpawned; dragonKill += s.dragonKilled;
        for (auto& [k, v] : s.deathWeapons) deaths[k] += v;
        for (auto& [k, v] : s.winWeapon) wins[k] += v;
        draws += s.winnerHealth <= 0;
    }
    const float n = static_cast<float>(matches);
    std::printf("map %s, %d players, %s bots, %d matches\n", MapOf(mapId).name, players, difficulty == BotDifficulty::Easy ? "easy" : difficulty == BotDifficulty::Hard ? "hard" : "normal", matches);
    std::printf("  match length    avg %.0f s  (min %.0f, max %.0f)   first kill after %.0f s\n", total / n, minD, maxD, first / n);
    std::printf("  alive           %.1f at 1:00, %.1f at 3:00 (of %d)\n", a60 / n, a180 / n, players - 1);
    std::printf("  eliminations    %.1f by players, %.1f by the storm, %.1f by mini bosses/dragon (per match)\n", kills / n, storm / n, boss / n);
    std::printf("  chests opened   %.0f per match, rupees held at the end %.0f\n", chests / n, rupees / n);
    std::printf("  dragon          spawned in %d of %d matches, killed in %d\n", dragonSpawn, matches, dragonKill);
    std::printf("  weapon at death:");
    std::vector<std::pair<int, std::string>> d;
    for (auto& [k, v] : deaths) d.push_back({v, k});
    std::sort(d.rbegin(), d.rend());
    for (size_t i = 0; i < d.size() && i < 8; i++) std::printf("  %s %.1f", d[i].second.c_str(), d[i].first / n);
    std::printf("\n  winning weapon: ");
    std::vector<std::pair<int, std::string>> w;
    for (auto& [k, v] : wins) w.push_back({v, k});
    std::sort(w.rbegin(), w.rend());
    for (size_t i = 0; i < w.size() && i < 6; i++) std::printf("  %s x%d", w[i].second.c_str(), w[i].first);
    std::printf("\n  matches with no survivor: %d\n", draws);
    return 0;
}
