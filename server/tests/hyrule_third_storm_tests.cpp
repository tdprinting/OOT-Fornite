// Hyrule Before the Third Storm (map 15, tools/maps/hyrule_third_storm): its registration, the baked collision and drawing data, the floors
// under its chests, how far the bots can get round it, and full matches on it with bots (that they move, loot, never fall out of the world,
// and how long the server takes a tick). Prints a short report; the numbers are also the map's performance record.
#include "game_server.h"
#include "loopback.h"
#include "fortnite_map.h"
#include "hyrule_third_storm_layout.h"
#include "hyrule_third_storm_draw.h"
#include "engine_limits.h"
#include <chrono>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <algorithm>
using namespace royale;
namespace h = royale::hyrule_third_storm;
static int failures = 0;
static constexpr float kDt = 1.0f / kTickHz;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)
template <class T, size_t N> constexpr size_t Count(const T (&)[N]) { return N; }
static double Ms(std::chrono::steady_clock::time_point a) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count(); }

int main() {
    // ---- registration
    CHECK(kHyruleThirdStormMapIndex == 15 && kMapCount == 16 && IsPlayableMap(15) && IsIslandMap(15) && IsAuthoredMap(15) && !IsTestMap(15));
    CHECK(kDefaultMapIndex == kGreatHyruleKingdomMapIndex);   // the main map stays Great Hyrule Kingdom
    CHECK(std::string(MapOf(15).name) == "Hyrule Before the Third Storm" && MapOf(15).scene == kHyruleFieldScene);
    CHECK(PoiNameBase(15) == PoiNameBase(14) + kNamesPerMap && PoiNameBase(15) + kNamesPerMap == kPoiNameTotal);
    CHECK(std::string(kPoiNames[PoiNameBase(15)]) == "Castle Town" && std::string(kPoiNames[PoiNameBase(15) + 23]) == "Desert Pass Refuge");
    CHECK(Count(h::kRegions) == static_cast<size_t>(kNamesPerMap));
    for (int i = 0; i < kNamesPerMap; ++i) CHECK(std::string(kPoiNames[PoiNameBase(15) + i]) == h::kRegions[i].name);
    int guarded = 0;
    for (const auto& r : h::kRegions) guarded += r.boss >= 0;
    CHECK(guarded == 7 && h::kRegions[0].boss < 0 && Count(h::kBossSites) == 7);
    auto t0 = std::chrono::steady_clock::now();
    const h::GroundTables& G = h::Ground();
    const double groundMs = Ms(t0);
    t0 = std::chrono::steady_clock::now();
    const h::DrawTables& D = h::Draw();
    const double drawMs = Ms(t0);
    std::printf("  decoding the generated tables: ground %.0f ms, drawing %.0f ms\n", groundMs, drawMs);
    CHECK(G.ok && D.ok);
    fortnite::UseTerrainForMap(15);
    CHECK(fortnite::gTerrainMapId == 15 && fortnite::gHeightData == h::kHeights && fortnite::gColourData == G.colours.data() && fortnite::MapHalfX() == h::kHalf);

    // ---- collision: in the 16-bit world, every corner valid, the plane right (32-bit distance, patches/0040)
    t0 = std::chrono::steady_clock::now();
    const fortnite::Mesh mesh = fortnite::BuildCollision();
    const double buildMs = Ms(t0);
    { std::vector<h::Vertex> v; std::vector<h::Triangle> t; h::LoadCollision(v, t); CHECK(!t.empty() && mesh.verts.size() == v.size() && mesh.polys.size() == t.size()); }
    CHECK(Count(h::kSurfaces) <= 16);
    int64_t worstDist = 0; size_t badPlane = 0, farPlane = 0;
    for (const auto& p : mesh.polys) {
        CHECK(p.a < mesh.verts.size() && p.b < mesh.verts.size() && p.c < mesh.verts.size() && p.surface < Count(h::kSurfaces));
        const auto& A = mesh.verts[p.a];
        const double d = -(p.nx * double(A.x) + p.ny * double(A.y) + p.nz * double(A.z)) / 32767.0;
        if (std::fabs(d - p.dist) > 3.0) ++badPlane;
        if (std::abs(p.dist) > 32767) ++farPlane;
        worstDist = std::max<int64_t>(worstDist, std::abs(p.dist));
    }
    CHECK(badPlane == 0);
    CHECK(worstDist <= engine::kCollisionMaxPlaneDist);
    for (const auto& v : mesh.verts) CHECK(std::abs(v.x) <= engine::kWorldCoordMax && std::abs(v.z) <= engine::kWorldCoordMax);
    // how many lookup nodes the engine will need (a triangle is listed once per grid cell its box touches): must fit BgCheck_Allocate's room
    {
        const float cx = (mesh.hi.x - mesh.lo.x) / float(engine::kCollisionGridX), cy = std::max(1.0f, (mesh.hi.y - mesh.lo.y) / float(engine::kCollisionGridY)), cz = (mesh.hi.z - mesh.lo.z) / float(engine::kCollisionGridZ);
        size_t nodes = 0;
        for (const auto& p : mesh.polys) {
            const auto& a = mesh.verts[p.a]; const auto& b = mesh.verts[p.b]; const auto& c = mesh.verts[p.c];
            auto span = [](int lo, int hi, float o, float cell) { return static_cast<size_t>(std::floor((hi - o) / cell) - std::floor((lo - o) / cell) + 1); };
            nodes += span(std::min({a.x, b.x, c.x}), std::max({a.x, b.x, c.x}), mesh.lo.x, cx) * span(std::min({a.y, b.y, c.y}), std::max({a.y, b.y, c.y}), mesh.lo.y, cy) *
                     span(std::min({a.z, b.z, c.z}), std::max({a.z, b.z, c.z}), mesh.lo.z, cz);
        }
        std::printf("  collision: %zu vertices, %zu triangles (%zu need the 32-bit plane distance), about %zu lookup nodes of %zu; built in %.0f ms\n",
                    mesh.verts.size(), mesh.polys.size(), farPlane, nodes, engine::kCollisionMaxListNodes, buildMs);
        CHECK(nodes < engine::kCollisionMaxListNodes);
    }

    // ---- drawing data
    {
        size_t tris = 0, nearTris = 0;
        for (const auto& b : D.batches) {
            CHECK(b.texture < h::kTextureCount && b.cell < h::kCells * h::kCells && b.lod <= 1);
            uint32_t vo = b.firstVertex, io = b.firstIndex;
            for (uint32_t w = 0; w < b.windows; ++w) {
                const auto& win = D.windows[b.firstWindow + w];
                CHECK(win.vertices >= 3 && win.vertices <= engine::kRspMaxVertices && win.triangles >= 1);
                for (uint32_t k = 0; k < win.triangles * 3u; ++k) CHECK(D.index[io + k] < win.vertices);
                io += win.triangles * 3u; vo += win.vertices; tris += win.triangles; if (b.lod == 0) nearTris += win.triangles;
            }
            CHECK(vo <= D.verts.size() && io <= D.index.size());
        }
        std::printf("  drawing: %zu vertices, %zu triangles (%zu near, %zu far) in %zu batches, %d textures\n", D.verts.size(), tris, nearTris, tris - nearTris,
                    D.batches.size(), h::kTextureCount);
        CHECK(h::kFarClip <= engine::kFarClipMax && h::kNear > h::kCell);
    }

    // ---- floors: most of the map can be stood on; chests stand on a floor; water boxes fit the engine's s16 sizes
    {
        size_t floors = 0;
        for (int16_t w : G.walk) floors += w != h::kNoFloor;
        const double share = double(floors) / G.walk.size();
        size_t onFloor = 0;
        std::vector<std::string> buried;
        for (const auto& s : h::kLootSites) {   // a floor within one grid step at the chest's height (a chest stands against a wall)
            bool ok = false;
            for (int dz = -1; dz <= 1 && !ok; ++dz)
                for (int dx = -1; dx <= 1 && !ok; ++dx) {
                    const float x = s.x + dx * h::kStep, z = s.z + dz * h::kStep;
                    int i, j;
                    if (!h::GridPoint(x, z, &i, &j)) continue;
                    const int16_t g = G.walk[static_cast<size_t>(j) * h::kGrid + i];
                    ok = g != h::kNoFloor && s.y > g - 80.0f && s.y < g + 1000.0f;
                }
            onFloor += ok;
        }
        std::printf("  floors: %.1f%% of the map's grid points; %zu of %zu chest sites stand on a floor; %zu water boxes\n", 100.0 * share, onFloor, Count(h::kLootSites), Count(h::kPools));
        CHECK(share > 0.85);
        CHECK(Count(h::kLootSites) >= 200);
        CHECK(onFloor * 10 >= Count(h::kLootSites) * 8);
        for (const auto& p : h::kPools) CHECK(p.x1 - p.x0 <= 32767 && p.z1 - p.z0 <= 32767 && p.x1 > p.x0 && p.z1 > p.z0);
        CHECK(HyruleThirdStormPlacementValid({h::kSpawnX, h::kSpawnZ}));
        float y = 0; CHECK(fortnite::GroundHeight(h::kSpawnX, h::kSpawnZ, &y) && std::fabs(y - HyruleThirdStormGroundHeight({h::kSpawnX, h::kSpawnZ})) < 1.0f);
    }

    // ---- navigation: the grid the server builds for the bots, and where it gets them from the lobby
    {
        const Circle map = MapOf(15).fallback;
        t0 = std::chrono::steady_clock::now();
        NavGrid grid(map, HyruleThirdStormPlacementValid, [](Vec2 p, float* y) { float s; *y = HyruleThirdStormLootHeightAt(p, &s) ? s : HyruleThirdStormGroundHeight(p); return true; });
        for (const auto& pool : h::kPools) grid.AddWater(pool.y, 30.0f, [pool](Vec2 p) { return p.x >= pool.x0 && p.x <= pool.x1 && p.z >= pool.z0 && p.z <= pool.z1 && HyruleThirdStormGroundHeight(p) < pool.y - 15.0f; });
        grid.SetClimbing(true);
        std::vector<NavGrid::UpperNode> nodes;
        for (const auto& n : h::kUpperNodes) nodes.push_back({float(n.x), float(n.z), float(n.y)});
        grid.AddUpper(nodes);
        for (const auto& cw : h::kClimbWalls) {
            const float nx = cw.nx / 100.0f, nz = cw.nz / 100.0f;
            grid.AddClimb({cw.x + nx * 50.0f, cw.z + nz * 50.0f}, float(cw.y0), {cw.x - nx * 45.0f, cw.z - nz * 45.0f}, float(cw.y1));
        }
        grid.BuildRegions();
        const double navMs = Ms(t0);
        Vec2 lobby{h::kSpawnX, h::kSpawnZ};
        CHECK(grid.Snap(lobby, &lobby, true));
        int reached = 0; double pathMs = 0;
        std::vector<std::string> cut;
        for (const auto& r : h::kRegions) {
            Vec2 at{r.x, r.z};
            if (!grid.Snap(at, &at, true)) { cut.push_back(std::string(r.name) + " (no open ground at " + std::to_string(int(r.x)) + ", " + std::to_string(int(r.z)) + (HyruleThirdStormPlacementValid({r.x, r.z}) ? ", placement valid)" : ", placement invalid)")); continue; }
            std::vector<Vec2> path;
            t0 = std::chrono::steady_clock::now();
            const bool ok = grid.FindPath(lobby, at, path, true) && grid.Connected(lobby, at);
            pathMs += Ms(t0);
            if (ok) ++reached; else cut.push_back(r.name);
        }
        size_t chestsReached = 0;
        for (const auto& s : h::kLootSites) chestsReached += grid.Connected(lobby, {s.x, s.z});
        std::printf("  bots' grid: built in %.0f ms; from the lobby they reach %d of 24 places (%.1f ms a path) and %zu of %zu chest sites\n", navMs, reached,
                    pathMs / std::max(1, reached), chestsReached, Count(h::kLootSites));
        for (const auto& c : cut) std::printf("    cut off: %s\n", c.c_str());
        CHECK(reached >= 22);
        CHECK(chestsReached * 10 >= Count(h::kLootSites) * 7);
    }

    // ---- matches with bots: they spread out, loot, never fall out of the world, and the server keeps up
    for (int round = 0; round < 2; ++round) {
        net::LoopbackNetwork network; auto& host = network.Server();
        GameServer server(host, 5 + round * 7919, MapOf(0).fallback, 40); server.SetBossCount(3);
        t0 = std::chrono::steady_clock::now();
        CHECK(server.SelectMap(15));   // lays the map out: places, chests, the bots' grid
        const double startMs = Ms(t0);
        server.SetPlayerLimit(30); server.Sim().match.AddHuman(1);
        CHECK(server.StartMatch());
        CHECK(server.Pois().size() == 24 && server.Sim().match.Bosses().size() == 7 && server.Sim().match.Loot().size() >= 200);
        std::map<uint32_t, Vec2> landed, lastPos; std::map<uint32_t, float> lastMoveAt;
        float elapsed = 0; double tickMs = 0, worstTick = 0; int ticks = 0, stuckNow = 0, offMap = 0, slowTicks = 0;
        float travelled = 0;
        while (elapsed < 360.0f && server.Sim().match.State() != MatchState::Ending) {
            t0 = std::chrono::steady_clock::now();
            server.Sim().Tick(kDt);
            const double ms = Ms(t0); tickMs += ms; worstTick = std::max(worstTick, ms); ++ticks; slowTicks += ms > 50.0;
            elapsed += kDt;
            if (server.Sim().match.State() != MatchState::InMatch) continue;
            for (const auto& p : server.Sim().match.Players()) {
                if (!p.isBot || !p.alive) continue;
                if (!landed.count(p.id)) { landed[p.id] = p.pos; lastPos[p.id] = p.pos; lastMoveAt[p.id] = elapsed; continue; }
                const float d = Distance(p.pos, lastPos[p.id]);
                if (d > 150.0f) { travelled += d; lastPos[p.id] = p.pos; lastMoveAt[p.id] = elapsed; }
                // (the server does not simulate a bot's height, so only leaving the map's square can be checked here)
                if (!h::OnMap(p.pos.x, p.pos.z)) ++offMap;
            }
        }
        stuckNow = 0; int aliveBots = 0;
        for (const auto& p : server.Sim().match.Players())
            if (p.isBot && p.alive) { ++aliveBots; if (elapsed - lastMoveAt[p.id] > 60.0f) ++stuckNow; }
        size_t opened = 0;
        for (const auto& l : server.Sim().match.Loot()) opened += l.taken;
        std::printf("  match %d: laid out in %.0f ms; %.0f s played, %d bots alive, %d standing still over a minute; %.0f m walked by bots; %zu loot taken;"
                    " tick %.2f ms average, %.1f ms worst, %d over 50 ms; %d samples off the map\n",
                    round + 1, startMs, elapsed, aliveBots, stuckNow, travelled / 36.0f, opened, tickMs / std::max(1, ticks), worstTick, slowTicks, offMap);
        CHECK(server.Sim().match.State() != MatchState::Lobby);
        CHECK(offMap == 0);
        CHECK(slowTicks <= 3);
        CHECK(opened >= 20);
        CHECK(stuckNow * 4 <= std::max(1, aliveBots));
        CHECK(tickMs / std::max(1, ticks) < 8.0);   // a 20 Hz server has 50 ms a tick; the phone hosts too
        for (const auto& boss : server.Sim().match.Bosses()) {
            const bool dragon = boss.kind >= BossKind::DragonFire && boss.kind <= BossKind::DragonSand;   // the match's main boss, not a guard
            const bool ok = dragon || boss.kind == HyruleThirdStormBossAt(boss.home) || IsChuKind(boss.kind);
            if (!ok) std::printf("    boss %d at %.0f %.0f (site kind %d)\n", static_cast<int>(boss.kind), boss.home.x, boss.home.z, static_cast<int>(HyruleThirdStormBossAt(boss.home)));
            CHECK(ok);
        }
    }
    fortnite::UseTerrainForMap(kFortniteMapIndex); CHECK(!fortnite::gSandboxTerrain && fortnite::gHeightData == fortnite::kHeights && fortnite::MapHalfX() == fortnite::kHalfX);
    std::printf("Hyrule Before the Third Storm: %s (%d failures)\n", failures ? "FAILED" : "passed", failures);
    return failures ? 1 : 0;
}
