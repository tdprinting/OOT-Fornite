#include "../game_server.h"
#include "../../shared/loopback.h"
#include "../../shared/riftlands_model.h"
#include <cstdio>
#include <set>
#include <string>
#include <deque>
using namespace royale;
int failures=0;
#define CHECK(x) do { if(!(x)) { std::printf("FAIL %d: %s\n",__LINE__,#x);++failures; } } while(0)

int main() {
    CHECK(kRiftlandsMapIndex==11 && kKingdomMapIndex==8 && kMainBossArenaIndex==10);
    CHECK(IsPlayableMap(11) && IsAuthoredMap(11) && IsIslandMap(11));
    CHECK(std::string(MapOf(11).name)=="Hyrule Riftlands");
    auto layout=GenerateRiftlandsLayout();
    CHECK(layout.pois.size()==24 && layout.lootSpots.size()==84 && layout.bossSpots.size()==7);
    for(size_t i=0;i<layout.pois.size();++i) {
        CHECK(layout.pois[i].name==PoiNameBase(11)+i);
        CHECK(std::string(kPoiNames[layout.pois[i].name])==riftlands::kRegions[i].name);
    }
    fortnite::UseTerrainForMap(11);
    CHECK(fortnite::gHeightData==riftlands::kHeights);
    auto collision=fortnite::BuildCollision();
    CHECK(collision.verts.size()<65536 && collision.polys.size()<65536);
    for(const auto& p:collision.polys) {
        CHECK(p.a<8191 && p.b<8191 && p.c<collision.verts.size());
        CHECK(p.surface<std::size(riftlands::kSurfaces));
        const float length=std::sqrt(float(p.nx)*p.nx+float(p.ny)*p.ny+float(p.nz)*p.nz);
        CHECK(length>32760 && length<32775);
    }
    CHECK(std::size(riftlands::kSurfaces)<=16);
    for(const auto& b:riftlands::kBatches) {
        CHECK(b.count%3==0 && b.first+b.count<=std::size(riftlands::kDrawVertices));
        CHECK(b.texture<std::size(riftlands::kTextures));
        for(size_t i=b.first;i<b.first+b.count;++i) {
            const auto& v=riftlands::kDrawVertices[i];
            const float d=std::sqrt(float(v.x-b.x)*(v.x-b.x)+float(v.y-b.y)*(v.y-b.y)+float(v.z-b.z)*(v.z-b.z));
            CHECK(d<=b.radius+2);
        }
    }
    for(const auto& pool:riftlands::kPools) {
        Vec2 p{(pool.x0+pool.x1)/2,(pool.z0+pool.z1)/2};
        CHECK(RiftlandsWaterY(p)==pool.y && RiftlandsTerrainHeight(p)<pool.y-30);
        CHECK(!RiftlandsDryGround(p));
    }
    for(const auto& w:riftlands::kWalkways) {
        const float run=Distance({w.x0,w.z0},{w.x1,w.z1});
        if(run<=1||std::fabs(w.y1-w.y0)/run>.8f)std::printf("steep walkway %.0f %.0f -> %.0f %.0f: %.2f\n",w.x0,w.z0,w.x1,w.z1,std::fabs(w.y1-w.y0)/run);
        CHECK(run>1 && std::fabs(w.y1-w.y0)/run<=.8f);
    }
    PlacementFn valid=[](Vec2 p) { return RiftlandsDryGround(p)&&!RiftlandsObstacleAt(p,0); };
    HeightFn height=[](Vec2 p,float* y) { *y=RiftlandsGroundHeight(p);return true; };
    NavGrid grid(MapOf(11).fallback,valid,height);
    std::vector<NavGrid::UpperNode> nodes;
    for(const auto& n:riftlands::kUpperNodes)nodes.push_back({float(n.x),float(n.z),float(n.y)});
    grid.AddUpper(nodes);grid.BuildRegions();
    // Check ordinary dry routes to POIs from the inner bypass; doors and essential
    // loot must not require swimming, jumping or a newly added traversal feature.
    int reachable=0;
    for(size_t i=0;i<8;++i) {
        const auto& r=riftlands::kRegions[i]; const auto& at=riftlands::kAccessSites[i];
        std::vector<Vec2> path;
        bool ok=grid.FindPath({0,1500},{at.x,at.z},path);
        std::printf("route %s: %s (%zu nodes)\n",r.name,ok?"yes":"no",path.size());
        reachable+=ok;
    }
    CHECK(reachable==8);
    for(const auto& door:riftlands::kDoors) {
        const riftlands::Building* room=nullptr;
        for(const auto& b:riftlands::kBuildings)
            if(std::fabs(b.floorY-door.y)<3 && RiftlandsInBuilding(b,{door.x,door.z},3)) {
                if(!room || Distance({b.x,b.z},{door.x,door.z})<Distance({room->x,room->z},{door.x,door.z}))room=&b;
            }
        CHECK(room!=nullptr);
        if(!room)continue;
        const float len=Distance({room->x,room->z},{door.x,door.z});
        const Vec2 dir{(room->x-door.x)/len,(room->z-door.z)/len};
        const Vec2 inside{door.x+dir.x*65,door.z+dir.z*65},outside{door.x-dir.x*65,door.z-dir.z*65};
        if(!grid.LineClear(inside,outside)) std::printf("blocked door %s at %.0f %.0f\n",door.house,door.x,door.z);
        CHECK(grid.LineClear(inside,outside));
    }
    int groundReachable=0,upperReachable=0;
    for(size_t i=0;i<std::size(riftlands::kLootSites);++i) {
        const auto& site=riftlands::kLootSites[i];
        if(i<72) {
            std::vector<Vec2> path;
            bool ok=grid.FindPath({0,1500},{site.x,site.z},path);
            if(!ok)std::printf("unreachable starter/secondary %zu at %.0f %.0f\n",i,site.x,site.z);
            groundReachable+=ok;
        } else {
            std::vector<NavGrid::Stop> route;
            bool ok=grid.FindRoute({0,1500},RiftlandsGroundHeight({0,1500}),false,{site.x,site.z},site.y,true,route);
            if(!ok)std::printf("unreachable upper-room %zu at %.0f %.0f\n",i,site.x,site.z);
            upperReachable+=ok;
        }
    }
    CHECK(groundReachable==72 && upperReachable==12);
    for(const auto& s:riftlands::kAllyStations) {if(!valid({s.x,s.z}))std::printf("invalid ally %.0f %.0f\n",s.x,s.z);CHECK(valid({s.x,s.z}));}
    for(const auto& s:riftlands::kCartStations) {if(!valid({s.x,s.z}))std::printf("invalid cart %.0f %.0f\n",s.x,s.z);CHECK(valid({s.x,s.z}));}
    for(uint64_t seed:{1ull,19ull,101ull}) {
        net::LoopbackNetwork network;auto& host=network.Server();
        GameServer server(host,seed,MapOf(0).fallback,84);CHECK(server.SelectMap(11));
        CHECK(server.Pois().size()==24);
        server.SetBossCount(3);server.SetPlayerLimit(2);server.Sim().match.AddHuman(1);CHECK(server.StartMatch());
        CHECK(server.Sim().match.Bosses().size()==7);
        for(const auto& boss:server.Sim().match.Bosses()) CHECK(boss.kind==RiftlandsBossAt(boss.home)||IsChuKind(boss.kind));
        CHECK(server.Sim().match.Loot().size()==84);
        for(const auto& circle:server.Sim().match.GetStorm().PhaseEnds())CHECK(valid(circle.center));
        std::printf("seed %llu: %zu chests\n",static_cast<unsigned long long>(seed),server.Sim().match.Loot().size());
        for(const auto& item:server.Sim().match.Loot()) {
            float y;
            CHECK(RiftlandsLootHeightAt(item.spawn.pos,&y)||valid(item.spawn.pos));
        }
    }
    fortnite::UseTerrainForMap(8);CHECK(fortnite::gHeightData==kingdom::kHeights);
    fortnite::UseTerrainForMap(7);CHECK(fortnite::gHeightData==convergence::kHeights);
    std::printf("Riftlands: %s (%d failures)\n",failures?"FAILED":"passed",failures);
    return failures?1:0;
}
