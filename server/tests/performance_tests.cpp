#include "sim.h"
#include "frame_cache.h"
#include "water_sim.h"
#include <cstdio>
#include <cstdlib>
using namespace royale;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static void NavigationStorageAndBudget() {
    NavGrid grid({{0,0}, 6600}, [](Vec2 p) { return std::fabs(p.x) > 90; });
    NavGrid::SearchWorkspace work;
    NavGrid::SearchBudget budget;
    std::vector<Vec2> path;
    std::vector<NavGrid::Stop> route;
    CHECK(!grid.FindRoute({-3000,0},0,false,{3000,0},0,false,route,&work));
    auto* costs = work.cost.data(); auto* heap = work.heap.data();
    const auto generation = work.generation;
    CHECK(!grid.FindRoute({-3000,0},0,false,{3000,0},0,false,route,&work));
    CHECK(work.cost.data() == costs && work.heap.data() == heap);
    CHECK(work.generation == generation + 1);
    CHECK(grid.FindRoute({-3000,0},0,false,{-3000,3000},0,false,route,&work,&budget));
    CHECK(route.size() == 1 && work.expanded == 0 && budget.searches == 12);
    budget.expansions = 40;
    CHECK(!grid.FindPath({-3000,0},{3000,0},path,true,&work,&budget));
    CHECK(work.expanded == 40 && budget.expansions == 0 && work.deferred);
    CHECK(!grid.FindRoute({-3000,0},0,false,{3000,0},0,false,route,&work,&budget));
    CHECK(work.expanded == 0 && work.deferred);
    budget.Reset();
    CHECK(grid.FindPath({-3000,0},{-3000,3000},path,true,&work,&budget));
    // Stamps from a previous failed query cannot contaminate a new query.
    CHECK(path.size() == 1);
}

static void TerrainTransitions() {
    NavGrid::SearchWorkspace work;
    std::vector<NavGrid::Stop> route;
    NavGrid wet({{0,0},900}, [](Vec2 p) { return std::fabs(p.x) > 90; });
    wet.AddWater(0,40,[](Vec2 p) { return std::fabs(p.x) <= 90; });
    CHECK(wet.FindRoute({-400,0},0,false,{400,0},0,false,route,&work));
    CHECK(work.expanded > 0); // Water must not use the dry direct-route shortcut.
    NavGrid ledge({{0,0},900}, nullptr, [](Vec2 p,float* y) { *y=p.x > 0 ? 60.0f : 0; return true; });
    CHECK(ledge.FindRoute({-400,0},0,false,{400,0},0,false,route,&work));
    CHECK(work.expanded > 0 && route.size() > 1);
    NavGrid cliff({{0,0},900}, nullptr, [](Vec2 p,float* y) { *y=p.x > 0 ? 250.0f : 0; return true; });
    CHECK(!cliff.FindRoute({-400,0},0,false,{400,0},0,false,route,&work));
    cliff.SetClimbing(true);
    CHECK(cliff.FindRoute({-400,0},0,false,{400,0},0,false,route,&work));
    bool climbed = false; for (auto& stop : route) climbed |= stop.climb;
    CHECK(climbed);
}

static Simulation Duel() {
    Simulation sim(5, {{0,0},3000},0);
    sim.match.AddHuman(1); sim.match.Start();
    while (sim.match.State() != MatchState::InMatch) sim.match.Tick(0.05f);
    for (auto& p : sim.match.Players()) if (p.id != 1 && p.id != 1000) p.alive = false;
    sim.match.Find(1)->pos = {2000,0}; sim.match.Find(1000)->pos = {-400,0};
    sim.match.SetSupplyDrops(false);
    return sim;
}

static void BotsAbandonUnreachableLoot() {
    auto sim = Duel();
    auto grid = std::make_shared<NavGrid>(Circle{{0,0},3000}, [](Vec2 p) { return std::fabs(p.x) > 90; });
    // No regions: deliberately let the planner discover failure rather than prefiltering it.
    sim.bots.SetNav(grid); sim.match.SetNav(grid);
    const size_t blocked = sim.match.AddLoot({{300,0},ItemId::GildedSword,Rarity::Legendary,false});
    sim.Tick(0.05f);
    CHECK(sim.match.Navigation().budget.searches < 12);
    const size_t reachable = sim.match.AddLoot({{-650,0},ItemId::MasterSword,Rarity::Epic,false});
    int searches = 0;
    for (int i = 0; i < 60; ++i) {
        sim.Tick(0.05f); searches += 12 - sim.match.Navigation().budget.searches;
    }
    CHECK(!sim.match.Loot()[blocked].taken);
    CHECK(sim.match.Loot()[reachable].taken);
    CHECK(searches < 15); // A failed route must not be searched every 20Hz tick.
}

static void AllyCannotStrikeThroughCover() {
    auto sim = Duel();
    auto grid = std::make_shared<NavGrid>(Circle{{0,0},3000}, nullptr);
    grid->Block({0,0},90,500); sim.match.SetNav(grid);
    auto& a = sim.match.MutableAllies()[0];
    a.owner = 1; a.alive = true; a.pos = {-180,0}; a.attackReadyAt = 0;
    auto* target = sim.match.Find(1000); target->pos = {180,0};
    const float health = target->health;
    CHECK(!sim.match.AllyStrike(a,target->id));
    CHECK(target->health == health && a.attackReadyAt == 0);
    target->pos = {2800,0};
    CHECK(!sim.match.AllyStrike(a,target->id)); // Authoritative range, independent of AI caller.
}

static void RevealedBotsStillRespectCover() {
    auto sim = Duel();
    auto grid = std::make_shared<NavGrid>(Circle{{0,0},3000}, nullptr);
    grid->Block({0,0},90,500); sim.bots.SetNav(grid);
    auto* bot = sim.match.Find(1000); auto* foe = sim.match.Find(1);
    bot->pos = {-180,0}; foe->pos = {180,0};
    bot->weapon = {ItemId::FairyBow,Rarity::Epic}; bot->ammo.fill(60);
    bot->hasAbility = false; bot->revealUntil = sim.match.Clock()+10;
    const auto ammo = bot->ammo;
    const float ready = bot->attackReadyAt;
    for (int i = 0; i < 40; ++i) {
        sim.bots.Step(sim.match, 0); // No movement: keep the terrain obstruction in place.
        sim.match.Tick(0.05f);
    }
    CHECK(bot->ammo == ammo && bot->attackReadyAt == ready);
}

static void CacheAndRippleSleep() {
    FrameCache<float,8> cache;
    int queries = 0; float out = 0;
    auto query = [&] { return float(++queries); };
    cache.BeginFrame(1,1);
    CHECK(cache.Get(10,20,&out,query) && out == 1);
    CHECK(cache.Get(10,20,&out,query) && queries == 1);
    CHECK(!cache.Get(11,20,&out,query));
    cache.BeginFrame(2,1); cache.Invalidate();
    CHECK(cache.Get(10,20,&out,query) && out == 2);
    cache.BeginFrame(30,1);
    CHECK(cache.Get(10,20,&out,query) && out == 3);
    water::RippleField field;
    field.Recenter(0,0); CHECK(field.Sleeping());
    field.Step(0.1f); CHECK(field.Sleeping() && field.Activity() == 0);
    field.Impulse(50000,50000,10,20); CHECK(field.Sleeping());
    field.Impulse(0,0,5,20); CHECK(!field.Sleeping());
    for (int i = 0; i < 3600 && !field.Sleeping(); ++i) field.Step(1.0f/60);
    CHECK(field.Sleeping() && field.Energy() == 0);
    field.Impulse(0,0,3,20); CHECK(!field.Sleeping());
    field.Clear(); CHECK(field.Sleeping());
}
int main() {
    NavigationStorageAndBudget(); TerrainTransitions(); BotsAbandonUnreachableLoot();
    AllyCannotStrikeThroughCover(); RevealedBotsStillRespectCover(); CacheAndRippleSleep();
    std::printf("performance regressions: %d failures\n", failures);
    return failures ? 1 : 0;
}
