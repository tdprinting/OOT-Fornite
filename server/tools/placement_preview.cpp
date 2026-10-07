// Lays out a map's scenery and chests exactly as a match does (GameServer::Reconfigure) and writes what it made as a text dump, which
// tools/placement_preview.py draws. Prints how well the chests are placed: how many have something to stand next to, and how many are on
// steep ground or inside a rock. Run: royale_placement_preview <map 0-7> <seed> <out.txt>
// The Fortnite Map, the sandbox and Convergence use their real ground. The five Ocarina of Time scenes only have a floor inside the game, so
// they get a stand-in (a ridge, a plateau and some rolling ground) with the scene's own circle: it shows the rules, not the real scene.
#include "game_server.h"
#include "loopback.h"
#include "../../shared/fortnite_map.h"
#include "../../shared/fortnite_scenery.h"
#include "../../shared/terrain.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
using namespace royale;
namespace fn = royale::fortnite;
using royale::net::LoopbackNetwork;

static float StandIn(Vec2 p, const Circle& c) {
    const float x = p.x - c.center.x, z = p.z - c.center.z, r = c.radius;
    float h = 45.0f * std::sin(x / (r * 0.18f)) * std::sin(z / (r * 0.14f));
    const float ridge = (x * 0.6f + z * 0.8f) - r * 0.25f;                       // a long escarpment: 260 high, climbed over about 150 units
    h += 260.0f * (std::clamp(ridge / 150.0f, 0.0f, 1.0f));
    const float d = std::hypot(x + r * 0.45f, z - r * 0.35f);                      // a plateau with steep sides
    h += 220.0f * (1.0f - std::clamp((d - r * 0.16f) / 120.0f, 0.0f, 1.0f));
    return h;
}

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: %s <map 0-7> <seed> <out.txt>\n", argv[0]); return 1; }
    const int mapId = std::atoi(argv[1]);
    const uint64_t seed = std::strtoull(argv[2], nullptr, 10);
    const Circle map = MapOf(mapId).fallback;
    PlacementFn valid;
    HeightFn height;
    if (mapId == kConvergenceMapIndex) {
        valid = [](Vec2 p) { return ConvergenceDryGround(p) && !ConvergenceObstacleAt(p); };
        height = [](Vec2 p, float* y) { *y = ConvergenceGroundHeight(p); return true; };
    } else if (IsIslandMap(mapId)) {
        fn::UseTerrainForMap(mapId);
        valid = [](Vec2 p) { float y; return fn::GroundHeight(p.x, p.z, &y) && y > fn::kWaterY + 10.0f && fn::GroundUp(p.x, p.z) >= 0.8f; };
        height = [](Vec2 p, float* y) { return fn::GroundHeight(p.x, p.z, y); };
    } else {
        valid = [map](Vec2 p) { return Distance(p, map.center) < map.radius; };
        height = [map](Vec2 p, float* y) { *y = StandIn(p, map); return true; };
    }
    LoopbackNetwork network(seed);
    GameServer server(network.Server(), seed, map, 0);
    if (mapId == kSandboxMapIndex) { server.SetSandbox(true); server.SetSoloTest(true); }   // the test map can only be picked once it is switched on
    if (!server.SelectMap(mapId)) { std::fprintf(stderr, "map %d can't be selected\n", mapId); return 1; }
    if (!server.Reconfigure(map, valid, 150, 0, height)) { std::fprintf(stderr, "Reconfigure refused\n"); return 1; }
    const Ground ground(valid, height);
    const auto& props = server.Props();
    std::FILE* f = std::fopen(argv[3], "w");
    if (!f) return 1;
    std::fprintf(f, "MAP %g %g %g\n", map.center.x, map.center.z, map.radius);
    const float step = map.radius / 80.0f;
    const int n = static_cast<int>(map.radius * 2.0f / step) + 1;
    std::fprintf(f, "GRID %g %g %g %d %d\n", map.center.x - map.radius, map.center.z - map.radius, step, n, n);
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            float y;
            const Vec2 p = {map.center.x - map.radius + i * step, map.center.z - map.radius + j * step};
            std::fprintf(f, "%d ", height(p, &y) && valid(p) ? static_cast<int>(y) : -9999);
        }
        std::fprintf(f, "\n");
    }
    for (const Poi& p : server.Pois()) std::fprintf(f, "POI %g %g %g\n", p.center.x, p.center.z, p.radius);
    for (const Prop& p : props) std::fprintf(f, "PROP %d %g %g\n", static_cast<int>(p.kind), p.pos.x, p.pos.z);
    // How well placed is each chest? Beside scenery (a boulder, rock, standing stone or thicket within 150), in the open, on a slope, in a solid.
    int total = 0, beside = 0, open = 0, steep = 0, inside = 0, floating = 0, index = 0;
    // The scattered chests come first in the match's list (see Match::RegenerateLoot), then the towns' and the climbs'.
    const int scattered = static_cast<int>(server.Sim().match.ScatteredLoot());
    int scatterOpen = 0, scatterInside = 0, scatterSteep = 0, scatterTotal = 0;
    for (const auto& e : server.Sim().match.Loot()) {
        const bool scatter = index++ < scattered;
        const Vec2 at = e.spawn.pos;
        if (!e.spawn.container) continue;
        total++;
        int bushes = 0;
        bool near = false, in = false;
        for (const Prop& p : props) {
            const float d = Distance(at, p.pos), r = PropRadius(p.kind) * (p.kind == PropKind::Boulder ? BoulderScale(p.rot) : 1.0f);
            if (r > 0 && d < r) in = true;
            if (p.kind == PropKind::Bush) { if (d < 200.0f) bushes++; }
            else if (!IsPlatform(p.kind) && p.kind != PropKind::Roof && d < r + 150.0f) near = true;
            else if (IsPlatform(p.kind) && d < 400.0f) near = true;
        }
        for (const Poi& poi : server.Pois()) if (Distance(at, poi.center) < poi.radius + 20.0f) near = true;
        if (mapId == kConvergenceMapIndex) {   // its scenery is authored geometry, not props
            for (const auto& b : convergence::kBuildings) if (std::fabs(at.x - b.x) < b.halfWidth + 150.0f && std::fabs(at.z - b.z) < b.halfDepth + 150.0f) near = true;
            for (const auto& o : convergence::kObstacles) if (at.x > o.x0 - 150.0f && at.x < o.x1 + 150.0f && at.z > o.z0 - 150.0f && at.z < o.z1 + 150.0f) near = true;
        }
        if (mapId == kFortniteMapIndex) {      // the island's own oaks, cliff slabs and boulders (shared/fortnite_scenery.h)
            for (int cz = static_cast<int>(std::floor((at.z - 150.0f) / fn::kSceneryCell)); cz <= static_cast<int>(std::floor((at.z + 150.0f) / fn::kSceneryCell)); cz++)
                for (int cx = static_cast<int>(std::floor((at.x - 150.0f) / fn::kSceneryCell)); cx <= static_cast<int>(std::floor((at.x + 150.0f) / fn::kSceneryCell)); cx++) {
                    fn::SceneryPiece piece;
                    if (!fn::SceneryIn(cx, cz, 1.0f, &piece)) continue;
                    if ((piece.kind == fn::SceneryKind::Oak || piece.kind == fn::SceneryKind::Cliff || piece.kind == fn::SceneryKind::Boulder) && Distance(at, {piece.x, piece.z}) < 170.0f) near = true;
                    if (fn::SceneryRadius(piece.kind, piece.scale) > 0 && Distance(at, {piece.x, piece.z}) < fn::SceneryRadius(piece.kind, piece.scale)) in = true;
                }
        }
        float y;
        if (!valid(at) || !height(at, &y)) floating++;
        const bool slope = ground.Slope(at) > 0.3f;
        beside += near || bushes >= 3;
        open += !(near || bushes >= 3);
        steep += slope;
        inside += in;
        if (scatter) { scatterTotal++; scatterOpen += !(near || bushes >= 3); scatterInside += in; scatterSteep += slope; }
        std::fprintf(f, "CHEST %g %g %d %d\n", at.x, at.z, static_cast<int>(e.spawn.rarity), (near || bushes >= 3) ? 1 : 0);
    }
    std::fclose(f);
    std::printf("map %d (%s) seed %llu: %zu props, %zu places, %d chests: %d beside scenery or in a town, %d out in the open, %d on slopes over 0.3, %d inside a solid, %d without floor\n",
                mapId, MapOf(mapId).name, static_cast<unsigned long long>(seed), props.size(), server.Pois().size(), total, beside, open, steep, inside, floating);
    std::printf("   scattered chests: %d, of them %d out in the open, %d on slopes, %d inside a solid\n", scatterTotal, scatterOpen, scatterSteep, scatterInside);
    return 0;
}
