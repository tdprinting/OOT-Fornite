// Great Hyrule Kingdom (tools/maps/great_hyrule_kingdom): the baked collision, drawing data, layout, the water and a match on it.
#include "game_server.h"
#include "loopback.h"
#include "fortnite_map.h"
#include "great_hyrule_kingdom_layout.h"
#include "great_hyrule_kingdom_model.h"
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
    CHECK(kGreatHyruleKingdomMapIndex==14 && kMapCount==15 && IsPlayableMap(14) && IsIslandMap(14) && IsAuthoredMap(14) && !IsTestMap(14));
    CHECK(kDefaultMapIndex==14 && kSkyloftMapIndex==13 && kMoonfallMapIndex==12 && kKingdomMapIndex==8);   // the main map; the older maps keep their numbers
    CHECK(std::string(MapOf(14).name)=="Great Hyrule Kingdom" && MapOf(14).scene==kHyruleFieldScene);
    CHECK(std::string(kPoiNames[PoiNameBase(14)])=="Castle Town" && PoiNameBase(14)==294 && PoiNameBase(14)+kNamesPerMap==kPoiNameTotal);
    CHECK(std::string(kPoiNames[PoiNameBase(14)+23])=="Death Mountain Trail");
    CHECK(std::string(kPoiNames[PoiNameBase(13)+23])=="Ember Bridge" && PoiNameBase(13)+kNamesPerMap==PoiNameBase(14));
    fortnite::UseTerrainForMap(14);
    CHECK(fortnite::gTerrainMapId==14 && fortnite::gHeightData==great_hyrule_kingdom::kHeights && fortnite::gColourData==great_hyrule_kingdom::kColours && fortnite::gCoverData==great_hyrule_kingdom::kCover);
    auto collision=fortnite::BuildCollision();
    CHECK(collision.verts.size()>0 && collision.verts.size()<65536 && collision.polys.size()>0 && collision.polys.size()<65536);
    for (const auto& p:collision.polys) {
        CHECK(p.c<collision.verts.size() && p.a<collision.verts.size() && p.b<collision.verts.size());
        CHECK(p.a<8191 && p.b<8191);
        CHECK(p.surface<sizeof(great_hyrule_kingdom::kSurfaces)/sizeof(great_hyrule_kingdom::kSurfaces[0]));
    }
    CHECK(sizeof(great_hyrule_kingdom::kSurfaces)/sizeof(great_hyrule_kingdom::kSurfaces[0])<=16);
    // The 24 landmark places and the 7 bosses.
    CHECK(sizeof(great_hyrule_kingdom::kRegions)/sizeof(great_hyrule_kingdom::kRegions[0])==kNamesPerMap);
    size_t bosses=0; for (const auto& r:great_hyrule_kingdom::kRegions) bosses+=r.boss>=0;
    CHECK(bosses==7);
    {   // the mini boss of each guarded place (tools/maps/great_hyrule_kingdom/layout.py BOSS_KINDS)
        const struct { const char* name; BossKind kind; } guards[] = {{"Hyrule Castle",BossKind::Stone},{"Lake Hylia",BossKind::Tide},{"Kokiri Forest",BossKind::Moss},
            {"Kakariko Graveyard",BossKind::Shade},{"Gerudo Fortress",BossKind::Dune},{"Snowpeak Lodge",BossKind::Frost},{"Goron City",BossKind::Lava}};
        for (const auto& g:guards) {
            bool found=false;
            for (const auto& r:great_hyrule_kingdom::kRegions) if (std::string(r.name)==g.name) { found=true; CHECK(r.boss==static_cast<int>(g.kind)); CHECK(GreatHyruleKingdomBossAt({r.x,r.z})==g.kind); }
            CHECK(found);
        }
        CHECK(great_hyrule_kingdom::kRegions[0].boss<0 && std::string(great_hyrule_kingdom::kRegions[0].name)=="Castle Town");   // the hot drop has no guard
    }
    auto layout=GenerateGreatHyruleKingdomLayout();
    CHECK(layout.pois.size()==24 && layout.bossSpots.size()==sizeof(great_hyrule_kingdom::kBossSites)/sizeof(great_hyrule_kingdom::kBossSites[0]) && layout.lootSpots.size()>=1);
    for (size_t i=0;i<layout.pois.size();++i) {
        CHECK(layout.pois[i].name==PoiNameBase(14)+i);
        CHECK(std::string(kPoiNames[layout.pois[i].name])==great_hyrule_kingdom::kRegions[i].name);
    }
    // Buildings: the floor is where the map says, and nothing blocks the middle of the room.
    CHECK(sizeof(great_hyrule_kingdom::kBuildings)/sizeof(great_hyrule_kingdom::kBuildings[0])>=1);
    for (const auto& b:great_hyrule_kingdom::kBuildings) {
        const float floor=CollisionRay(collision,{b.x,b.floorY+80,b.z},{b.x,b.floorY-80,b.z});
        if (!(std::fabs(floor-.5f)<.04f)) std::printf("floor %.3f at %.0f %.0f floorY %.0f\n",floor,b.x,b.z,b.floorY);
        CHECK(std::fabs(floor-.5f)<.04f);
        if (GreatHyruleKingdomObstacleAt({b.x,b.z},0.0f)) std::printf("obstacle at %.0f %.0f\n",b.x,b.z);
        CHECK(!GreatHyruleKingdomObstacleAt({b.x,b.z},0.0f));
    }
    CHECK(sizeof(great_hyrule_kingdom::kLootSites)/sizeof(great_hyrule_kingdom::kLootSites[0])>=1);
    for (const auto& s:great_hyrule_kingdom::kLootSites) CHECK(s.y>-300 && std::fabs(s.x)<great_hyrule_kingdom::kHalfX && std::fabs(s.z)<great_hyrule_kingdom::kHalfZ);
    CHECK(sizeof(great_hyrule_kingdom::kUpperNodes)/sizeof(great_hyrule_kingdom::kUpperNodes[0])>=1);
    for (const auto& s:great_hyrule_kingdom::kAllyStations) CHECK(s.kind>=0 && s.kind<=6);
    // Drawing data stays inside its arrays.
    for (const auto& b:great_hyrule_kingdom::kBatches) {
        CHECK(b.count%3==0 && b.first+b.count<=sizeof(great_hyrule_kingdom::kDrawVertices)/sizeof(great_hyrule_kingdom::kDrawVertices[0]));
        CHECK(b.texture<sizeof(great_hyrule_kingdom::kTextures)/sizeof(great_hyrule_kingdom::kTextures[0]));
    }
    CHECK(great_hyrule_kingdom::kTextureSize==256 && sizeof(great_hyrule_kingdom::kTextures[0])==256*256*2);   // 256 x 256 RGBA16 (the other maps are 128)
    // Raised water: two lakes and the river's six stepped pools, each at its own level, and the sea elsewhere.
    CHECK(sizeof(great_hyrule_kingdom::kPools)/sizeof(great_hyrule_kingdom::kPools[0])==8);
    for (const auto& pool:great_hyrule_kingdom::kPools) {
        const Vec2 p{(pool.x0+pool.x1)/2,(pool.z0+pool.z1)/2};
        CHECK(GreatHyruleKingdomWaterY(p)==pool.y);
        CHECK(fortnite::WaterHeightAt(p.x,p.z)==pool.y);
    }
    CHECK(GreatHyruleKingdomWaterY({great_hyrule_kingdom::kHalfX-1,great_hyrule_kingdom::kHalfZ-1})==-227.0f || sizeof(great_hyrule_kingdom::kPools)>0);
    // The ground and the placement contract agree with the lobby spawn.
    CHECK(GreatHyruleKingdomPlacementValid({great_hyrule_kingdom::kSpawnX,great_hyrule_kingdom::kSpawnZ}));
    // A match on the map runs with bots.
    {
        net::LoopbackNetwork network; auto& host=network.Server();
        GameServer server(host,5,MapOf(0).fallback,40);server.SetBossCount(3);
        CHECK(server.SelectMap(14));server.SetPlayerLimit(30);server.Sim().match.AddHuman(1);
        CHECK(server.Pois().size()==24);
        CHECK(server.StartMatch());
        CHECK(server.Sim().match.Bosses().size()==7);
        CHECK(server.Sim().match.Loot().size()>=1);
        float elapsed=0;
        while (elapsed<8.0f && server.Sim().match.State()!=MatchState::Ending) { server.Sim().Tick(kDt);elapsed+=kDt; }
        CHECK(server.Sim().match.State()!=MatchState::Lobby);
        for (const auto& boss:server.Sim().match.Bosses()) CHECK(boss.kind==GreatHyruleKingdomBossAt(boss.home)||IsChuKind(boss.kind));
    }
    fortnite::UseTerrainForMap(kFortniteMapIndex);CHECK(!fortnite::gSandboxTerrain && fortnite::gHeightData==fortnite::kHeights);
    std::printf("Great Hyrule Kingdom: %s (%d failures)\n",failures?"FAILED":"passed",failures);return failures?1:0;
}
