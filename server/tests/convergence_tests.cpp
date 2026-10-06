#include "game_server.h"
#include "loopback.h"
#include "fortnite_map.h"
#include "convergence_model.h"
#include <cstdio>
#include <set>
using namespace royale;
static int failures=0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n",__LINE__,#c);++failures; } } while(0)
int main() {
    CHECK(kMapCount==8 && kPlayableMapCount==7 && IsPlayableMap(kConvergenceMapIndex));
    CHECK(!IsPlayableMap(kSandboxMapIndex) && kSandboxMapIndex==6 && kFortniteMapIndex==5);
    fortnite::UseTerrainForMap(kConvergenceMapIndex);
    auto collision=fortnite::BuildCollision();
    CHECK(collision.verts.size()<8192 && collision.polys.size()<65536);
    for (const auto& p:collision.polys) {
        CHECK(p.a<collision.verts.size() && p.b<collision.verts.size() && p.c<collision.verts.size());
        CHECK(std::abs(p.nx)+std::abs(p.ny)+std::abs(p.nz)>100);
    }
    CHECK(ConvergenceDryGround({0,-600}) && !ConvergenceObstacleAt({0,-600}));
    CHECK(!ConvergenceDryGround({4900,-1300}));
    // Both market-house doorways and the interior central aisle must be passable.
    for (float z=400;z<=1000;z+=20) CHECK(!ConvergenceObstacleAt({-900,z}));
    CHECK(ConvergenceObstacleAt({-1300,700}));
    for (const auto& b:convergence::kBuildings) {
        for (float z=b.z-b.halfDepth-70;z<=b.z+b.halfDepth+70;z+=20) {
            CHECK(!ConvergenceObstacleAt({b.x,z}));
            CHECK(ConvergenceDryGround({b.x,z}));
        }
    }
    float y=0;CHECK(fortnite::GroundHeight(-900,700,&y));
    CHECK(std::fabs(y-ConvergenceGroundHeight({-900,700}))<.01f);
    for (const auto& batch:convergence::kBatches) {
        CHECK(batch.count%3==0 && batch.first+batch.count<=sizeof(convergence::kDrawVertices)/sizeof(convergence::kDrawVertices[0]));
        CHECK(batch.texture<sizeof(convergence::kTextures)/sizeof(convergence::kTextures[0]));
    }
    auto layout=GenerateConvergenceLayout();CHECK(layout.pois.size()==10 && layout.bossSpots.size()==7 && layout.lootSpots.size()>=30);
    for (int seed=1;seed<=8;++seed) {
        net::LoopbackNetwork network; auto& host=network.Server();
        GameServer server(host,seed,MapOf(0).fallback,40);server.SetBossCount(3);
        CHECK(server.SelectMap(kConvergenceMapIndex));
        server.SetPlayerLimit(2);server.Sim().match.AddHuman(1);
        CHECK(server.StartMatch());std::set<int> kinds;
        CHECK(server.Sim().match.Bosses().size()==7);
        for (const auto& b:server.Sim().match.Bosses()) {
            CHECK(b.kind==ConvergenceBossAt(b.home));kinds.insert(static_cast<int>(b.kind));
            CHECK(ConvergenceDryGround(b.home) && !ConvergenceObstacleAt(b.home));
        }
        CHECK(kinds.size()==7);
        for (const auto& l:server.Sim().match.Loot()) CHECK(ConvergenceDryGround(l.spawn.pos) && !ConvergenceObstacleAt(l.spawn.pos));
    }
    fortnite::UseTerrainForMap(kSandboxMapIndex);CHECK(fortnite::gSandboxTerrain);
    fortnite::UseTerrainForMap(kFortniteMapIndex);CHECK(!fortnite::gSandboxTerrain && fortnite::gHeightData==fortnite::kHeights);
    std::printf("Convergence: %s (%d failures)\n",failures?"FAILED":"passed",failures);return failures?1:0;
}
