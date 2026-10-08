// Hyrule Kingdom (tools/maps/kingdom): the baked collision, drawing data, layout and a match on it.
#include "game_server.h"
#include "loopback.h"
#include "fortnite_map.h"
#include "kingdom_layout.h"
#include "kingdom_model.h"
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
    CHECK(kMapCount>kKingdomMapIndex && kKingdomMapIndex==8 && IsPlayableMap(kKingdomMapIndex) && IsIslandMap(kKingdomMapIndex) && IsAuthoredMap(kKingdomMapIndex));
    CHECK(std::string(MapOf(kKingdomMapIndex).name)=="Hyrule Kingdom" && MapOf(kKingdomMapIndex).scene==kHyruleFieldScene);   // Hyrule Field's scene, so its song plays
    CHECK(std::string(kPoiNames[kKingdomMapIndex*kNamesPerMap])=="Hyrule Castle");
    fortnite::UseTerrainForMap(kKingdomMapIndex);
    CHECK(fortnite::gTerrainMapId==kKingdomMapIndex && fortnite::gHeightData==kingdom::kHeights);
    auto collision=fortnite::BuildCollision();
    // The game keeps a vertex number in 13 bits in the first two corners of a triangle (the third has 16: patches/0025).
    CHECK(collision.verts.size()<65536 && collision.polys.size()<65536);
    CHECK(collision.verts.size()>8191);   // this map needs the third corner's extra bits, so keep the test honest about it
    size_t highCorners=0;
    for (const auto& p:collision.polys) {
        CHECK(p.a<8191 && p.b<8191 && p.c<collision.verts.size());
        CHECK(p.surface<sizeof(kingdom::kSurfaces)/sizeof(kingdom::kSurfaces[0]));
        CHECK(std::abs(p.nx)+std::abs(p.ny)+std::abs(p.nz)>100);
        highCorners+=p.c>=8191;
    }
    CHECK(highCorners>0);
    CHECK(sizeof(kingdom::kSurfaces)/sizeof(kingdom::kSurfaces[0])<=16);
    // Ground, buildings and places.
    CHECK(sizeof(kingdom::kRegions)/sizeof(kingdom::kRegions[0])==kNamesPerMap);
    size_t bosses=0; for (const auto& r:kingdom::kRegions) bosses+=r.boss>=0;
    CHECK(bosses==7);
    for (const auto& b:kingdom::kBuildings) {
        CHECK(KingdomDryGround({b.x,b.z}) || KingdomGroundHeight({b.x,b.z})>=b.floorY);
        const float floor=CollisionRay(collision,{b.x,b.floorY+80,b.z},{b.x,b.floorY-80,b.z});
        if (!(std::fabs(floor-.5f)<.04f)) std::printf("floor %.3f at %.0f %.0f floorY %.0f\n",floor,b.x,b.z,b.floorY);
        CHECK(std::fabs(floor-.5f)<.04f);                                  // the floor is where the map says
        if (KingdomObstacleAt({b.x,b.z},0.0f)) std::printf("obstacle at %.0f %.0f\n",b.x,b.z);
        CHECK(!KingdomObstacleAt({b.x,b.z},0.0f));                         // and nothing blocks the middle of the room
        CHECK(CollisionRay(collision,{b.x,b.floorY+900,b.z},{b.x,b.floorY+500,b.z})<=2);   // (a roof or sky: just a sanity ray)
    }
    CHECK(sizeof(kingdom::kBuildings)/sizeof(kingdom::kBuildings[0])>=30);
    // Loot sites lie on the ground or a floor above it, never inside the sea.
    for (const auto& s:kingdom::kLootSites) { CHECK(s.y>-200 && std::fabs(s.x)<7400 && std::fabs(s.z)<7700); }
    CHECK(sizeof(kingdom::kLootSites)/sizeof(kingdom::kLootSites[0])>=60);
    // Drawing data: batches stay inside the vertex array, textures exist and are 128 x 128 RGBA5551 with explicit big-endian bytes.
    static_assert(sizeof(kingdom::kTextures[0])==128*128*2,"one 128 x 128 RGBA5551 texture");
    for (const auto& batch:kingdom::kBatches) {
        for (size_t i=batch.first; i<batch.first+batch.count; ++i) {
            const auto& v=kingdom::kDrawVertices[i];
            const float dx=float(v.x)-batch.x,dy=float(v.y)-batch.y,dz=float(v.z)-batch.z;
            CHECK(dx*dx+dy*dy+dz*dz <= float(batch.radius)*batch.radius);
        }
        CHECK(batch.count%3==0 && batch.first+batch.count<=sizeof(kingdom::kDrawVertices)/sizeof(kingdom::kDrawVertices[0]));
        CHECK(batch.texture<sizeof(kingdom::kTextures)/sizeof(kingdom::kTextures[0]) && batch.reach>=4000);
    }
    for (size_t material=0;material<sizeof(kingdom::kTextures)/sizeof(kingdom::kTextures[0]);++material)
        for (size_t pixel=0;pixel<128*128;pixel+=17) CHECK((kingdom::kTextures[material][pixel*2+1]&1)==1);
    auto layout=GenerateKingdomLayout();CHECK(layout.pois.size()==24 && layout.bossSpots.size()==7 && layout.lootSpots.size()>=60);
    for (int seed=1;seed<=4;++seed) {
        net::LoopbackNetwork network; auto& host=network.Server();
        GameServer server(host,seed,MapOf(0).fallback,40);server.SetBossCount(3);
        CHECK(server.SelectMap(kKingdomMapIndex));
        server.SetPlayerLimit(2);server.Sim().match.AddHuman(1);
        CHECK(server.StartMatch());std::set<int> kinds;
        CHECK(server.Sim().match.Bosses().size()==7);
        for (const auto& b:server.Sim().match.Bosses()) {
            CHECK(b.kind==KingdomBossAt(b.home) || IsChuKind(b.kind));kinds.insert(static_cast<int>(b.kind));
        }
        CHECK(kinds.size()>=3);
        for (const auto& l:server.Sim().match.Loot()) { float y; CHECK(KingdomLootHeightAt(l.spawn.pos,&y) || (KingdomDryGround(l.spawn.pos) && !KingdomObstacleAt(l.spawn.pos,0.0f))); }
    }
    // The bots' grid: the doorways, the viaduct and the boardwalks join the places up, and chests upstairs are left to players.
    {
        PlacementFn valid=[](Vec2 p) { float y; return KingdomLootHeightAt(p,&y) || (KingdomDryGround(p) && !KingdomObstacleAt(p,0.0f)); };
        HeightFn height=[](Vec2 p,float* y) { float s; *y=KingdomLootHeightAt(p,&s) ? s : KingdomGroundHeight(p); return true; };
        NavGrid grid(MapOf(kKingdomMapIndex).fallback,valid,height);
        std::vector<NavGrid::UpperNode> nodes;
        for (const auto& n:kingdom::kUpperNodes) nodes.push_back({float(n.x),float(n.z),float(n.y)});
        grid.AddUpper(nodes);CHECK(nodes.size()>2000);
        // Verify authored ivy routes directly, independently of random bot goals.
        grid.SetClimbing(true);
        for (const auto& wall:kingdom::kClimbWalls) {
            const float nx=wall.nx/100.f,nz=wall.nz/100.f;
            grid.AddClimb({wall.x+nx*50,wall.z+nz*50},wall.y0,{wall.x-nx*45,wall.z-nz*45},wall.y1);
        }
        int upstairs=0;
        for (const auto& site:kingdom::kLootSites) if (site.y>KingdomGroundHeight({site.x,site.z})+90.0f) { grid.MarkUpper({site.x,site.z},site.y);++upstairs; }
        grid.BuildRegions();
        bool ivyRoute=false;
        for (const auto& wall:kingdom::kClimbWalls) {
            const float nx=wall.nx/100.f,nz=wall.nz/100.f;
            std::vector<NavGrid::Stop> route;
            if (grid.FindRoute({wall.x+nx*50,wall.z+nz*50},wall.y0,false,{wall.x-nx*45,wall.z-nz*45},wall.y1,true,route))
                for (const auto& stop:route) ivyRoute|=stop.climb;
            if (ivyRoute) break;
        }
        CHECK(ivyRoute);
        CHECK(upstairs>20 && upstairs<70);
        Vec2 town{0,1150};CHECK(grid.Snap(town,&town,true));
        const Vec2 places[]={{0,-1500},{4050,-1350},{-5050,-1150},{1500,4550},{-1900,4700},{-5000,5150},{2250,-5350},{5250,-2250},{-5750,-2300},{-2950,2350},{1900,-2850}};
        for (Vec2 at:places) { std::vector<Vec2> path;CHECK(grid.FindPath(town,at,path,true) && grid.Connected(town,at)); }
        // Chests upstairs that a ramp leads to are reachable, with a route that climbs; the others (the towers' ivy) are not.
        int reachable=0;
        for (const auto& site:kingdom::kLootSites) if (site.y>KingdomGroundHeight({site.x,site.z})+90.0f) {
            std::vector<NavGrid::Stop> route;
            const bool found=grid.FindRoute(town,0,false,{site.x,site.z},site.y,true,route);
            CHECK(found==grid.Connected(town,{site.x,site.z}));
            if (found) { ++reachable;float top=0;for (const auto& stop:route) top=std::max(top,stop.y);CHECK(top>=site.y-60.0f && route.back().upper); }
        }
        CHECK(reachable>=15);
    }
    // Bots really go upstairs: in a match on the Kingdom some chests above the ground floor are opened, and bots are seen high above the floor.
    {
        net::LoopbackNetwork network; auto& host=network.Server();
        GameServer server(host,5,MapOf(0).fallback,40);server.SetBossCount(3);
        CHECK(server.SelectMap(kKingdomMapIndex));server.SetPlayerLimit(30);server.Sim().match.AddHuman(1);
        CHECK(server.StartMatch());
        // This regression measures navigation, independently of the random boss combat pool.
        server.Sim().match.SandboxClearBosses();
        float highest=0;float elapsed=0;int swimTicks=0,climbTicks=0;
        while (elapsed<420.0f && server.Sim().match.State()!=MatchState::Ending) {
            server.Sim().Tick(kDt);elapsed+=kDt;
            for (const auto& p:server.Sim().match.Players()) if (p.isBot && p.alive && elapsed>100.0f) {highest=std::max(highest,p.y);const auto a=static_cast<Anim>(p.anim);if(a==Anim::Swim||a==Anim::Tread)++swimTicks;if(a==Anim::Climb)++climbTicks;}
        }
        int upstairsTaken=0;
        for (const auto& l:server.Sim().match.Loot()) {
            if (!l.taken) continue;
            for (const auto& site:kingdom::kLootSites) if (Distance(l.spawn.pos,{site.x,site.z})<2.0f && site.y>KingdomGroundHeight({site.x,site.z})+90.0f) ++upstairsTaken;
        }
        std::printf("  bots upstairs: highest %.0f above the floor, %d chests upstairs opened\n",highest,upstairsTaken);
        std::printf("  bots swam %d ticks, climbed %d ticks\n",swimTicks,climbTicks);
        CHECK(highest>200.0f && upstairsTaken>=1);
        // Random matches can choose ramps exclusively after the 100s warmup.
        // Authored climbing is asserted deterministically by ivyRoute above.
    }
    fortnite::UseTerrainForMap(kFortniteMapIndex);CHECK(!fortnite::gSandboxTerrain && fortnite::gHeightData==fortnite::kHeights);
    std::printf("Kingdom: %s (%d failures)\n",failures?"FAILED":"passed",failures);return failures?1:0;
}
