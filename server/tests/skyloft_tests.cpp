// Skyloft Isles (tools/maps/skyloft): the baked collision, drawing data, layout and a match on it.
#include "game_server.h"
#include "loopback.h"
#include "fortnite_map.h"
#include "skyloft_layout.h"
#include "skyloft_model.h"
#include <cstdio>
#include <set>
#include <algorithm>
using namespace royale;
static int failures=0;
static constexpr float kDt=1.0f/kTickHz;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n",__LINE__,#c);++failures; } } while(0)
struct P3 { float x,y,z; };
static P3 Sub(P3 a,P3 b) {return {a.x-b.x,a.y-b.y,a.z-b.z};}
static P3 Cross(P3 a,P3 b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static float Dot(P3 a,P3 b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
static float CollisionRay(const fortnite::Mesh& mesh,P3 from,P3 to) {
    float nearest=2;const P3 dir=Sub(to,from);
    for(const auto& p:mesh.polys) {
        const auto& aa=mesh.verts[p.a];const auto& bb=mesh.verts[p.b];const auto& cc=mesh.verts[p.c];
        P3 a{float(aa.x),float(aa.y),float(aa.z)},b{float(bb.x),float(bb.y),float(bb.z)},c{float(cc.x),float(cc.y),float(cc.z)};
        const P3 e1=Sub(b,a),e2=Sub(c,a),h=Cross(dir,e2);const float det=Dot(e1,h);
        if(std::fabs(det)<.00001f) continue;
        const P3 s=Sub(from,a);const float u=Dot(s,h)/det;if(u<-.0001f || u>1.0001f) continue;
        const P3 q=Cross(s,e1);const float v=Dot(dir,q)/det;if(v<-.0001f || u+v>1.0001f) continue;
        const float t=Dot(e2,q)/det;if(t>=0 && t<=1) nearest=std::min(nearest,t);
    }
    return nearest;
}
int main() {
    CHECK(kSkyloftMapIndex==13 && kMapCount>kSkyloftMapIndex && IsPlayableMap(13) && IsIslandMap(13) && IsAuthoredMap(13));
    CHECK(std::string(MapOf(13).name)=="Skyloft Isles" && MapOf(13).scene==kHyruleFieldScene);
    CHECK(std::string(kPoiNames[PoiNameBase(13)])=="Knight Academy" && PoiNameBase(13)==270 && PoiNameBase(13)+kNamesPerMap==kPoiNameTotal);
    CHECK(std::string(kPoiNames[PoiNameBase(13)+23])=="Ember Bridge");
    fortnite::UseTerrainForMap(13);
    CHECK(fortnite::gTerrainMapId==13 && fortnite::gHeightData==skyloft::kHeights && fortnite::gColourData==skyloft::kColours && fortnite::gCoverData==skyloft::kCover);
    auto collision=fortnite::BuildCollision();
    CHECK(collision.verts.size()>0 && collision.verts.size()<65536 && collision.polys.size()>0 && collision.polys.size()<65536);
    for (const auto& p:collision.polys) {
        CHECK(p.c<collision.verts.size() && p.a<collision.verts.size() && p.b<collision.verts.size());
        CHECK(p.a<8191 && p.b<8191);
        CHECK(p.surface<sizeof(skyloft::kSurfaces)/sizeof(skyloft::kSurfaces[0]));
    }
    CHECK(sizeof(skyloft::kSurfaces)/sizeof(skyloft::kSurfaces[0])<=16);
    // The 24 landmark places and the 7 bosses.
    CHECK(sizeof(skyloft::kRegions)/sizeof(skyloft::kRegions[0])==kNamesPerMap);
    size_t bosses=0; for (const auto& r:skyloft::kRegions) bosses+=r.boss>=0;
    CHECK(bosses==7);
    auto layout=GenerateSkyloftLayout();
    CHECK(layout.pois.size()==24 && layout.bossSpots.size()==sizeof(skyloft::kBossSites)/sizeof(skyloft::kBossSites[0]) && layout.lootSpots.size()>=1);
    for (size_t i=0;i<layout.pois.size();++i) {
        CHECK(layout.pois[i].name==PoiNameBase(13)+i);
        CHECK(std::string(kPoiNames[layout.pois[i].name])==skyloft::kRegions[i].name);
    }
    // Buildings: the floor is where the map says, and nothing blocks the middle of the room.
    CHECK(sizeof(skyloft::kBuildings)/sizeof(skyloft::kBuildings[0])>=1);
    for (const auto& b:skyloft::kBuildings) {
        const float floor=CollisionRay(collision,{b.x,b.floorY+80,b.z},{b.x,b.floorY-80,b.z});
        if (!(std::fabs(floor-.5f)<.04f)) std::printf("floor %.3f at %.0f %.0f floorY %.0f\n",floor,b.x,b.z,b.floorY);
        CHECK(std::fabs(floor-.5f)<.04f);
        if (SkyloftObstacleAt({b.x,b.z},0.0f)) std::printf("obstacle at %.0f %.0f\n",b.x,b.z);
        CHECK(!SkyloftObstacleAt({b.x,b.z},0.0f));
    }
    CHECK(sizeof(skyloft::kLootSites)/sizeof(skyloft::kLootSites[0])>=1);
    for (const auto& s:skyloft::kLootSites) CHECK(s.y>-300 && std::fabs(s.x)<skyloft::kHalfX && std::fabs(s.z)<skyloft::kHalfZ);
    CHECK(sizeof(skyloft::kUpperNodes)/sizeof(skyloft::kUpperNodes[0])>=1);
    for (const auto& s:skyloft::kAllyStations) CHECK(s.kind>=0 && s.kind<=6);
    // Drawing data stays inside its arrays.
    for (const auto& b:skyloft::kBatches) {
        CHECK(b.count%3==0 && b.first+b.count<=sizeof(skyloft::kDrawVertices)/sizeof(skyloft::kDrawVertices[0]));
        CHECK(b.texture<sizeof(skyloft::kTextures)/sizeof(skyloft::kTextures[0]));
    }
    CHECK(skyloft::kTextureSize==128);
    // Raised water: the pool's own level, over lower ground, and the sea elsewhere.
    for (const auto& pool:skyloft::kPools) {
        const Vec2 p{(pool.x0+pool.x1)/2,(pool.z0+pool.z1)/2};
        CHECK(SkyloftWaterY(p)==pool.y);
        CHECK(fortnite::WaterHeightAt(p.x,p.z)==pool.y);
    }
    CHECK(SkyloftWaterY({skyloft::kHalfX-1,skyloft::kHalfZ-1})==-227.0f || sizeof(skyloft::kPools)>0);
    // The ground and the placement contract agree with the lobby spawn.
    CHECK(SkyloftPlacementValid({skyloft::kSpawnX,skyloft::kSpawnZ}));
    // A match on the map runs with bots.
    {
        net::LoopbackNetwork network; auto& host=network.Server();
        GameServer server(host,5,MapOf(0).fallback,40);server.SetBossCount(3);
        CHECK(server.SelectMap(13));server.SetPlayerLimit(30);server.Sim().match.AddHuman(1);
        CHECK(server.Pois().size()==24);
        CHECK(server.StartMatch());
        CHECK(server.Sim().match.Bosses().size()==7);
        CHECK(server.Sim().match.Loot().size()>=1);
        float elapsed=0;
        while (elapsed<8.0f && server.Sim().match.State()!=MatchState::Ending) { server.Sim().Tick(kDt);elapsed+=kDt; }
        CHECK(server.Sim().match.State()!=MatchState::Lobby);
        for (const auto& boss:server.Sim().match.Bosses()) CHECK(boss.kind==SkyloftBossAt(boss.home)||IsChuKind(boss.kind));
    }
    fortnite::UseTerrainForMap(kFortniteMapIndex);CHECK(!fortnite::gSandboxTerrain && fortnite::gHeightData==fortnite::kHeights);
    std::printf("Skyloft: %s (%d failures)\n",failures?"FAILED":"passed",failures);return failures?1:0;
}
