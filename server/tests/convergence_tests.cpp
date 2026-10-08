#include "game_server.h"
#include "loopback.h"
#include "fortnite_map.h"
#include "convergence_model.h"
#include <cstdio>
#include <set>
#include <algorithm>
using namespace royale;
static int failures=0;
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
    CHECK(kMapCount==12 && kPlayableMapCount==9 && IsPlayableMap(kConvergenceMapIndex));
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
        CHECK(CollisionRay(collision,{b.x,b.floorY+80,b.z},{b.x+b.halfWidth+80,b.floorY+80,b.z})<=1);
        CHECK(CollisionRay(collision,{b.x,b.floorY+80,b.z},{b.x-b.halfWidth-80,b.floorY+80,b.z})<=1);
        CHECK(CollisionRay(collision,{b.x,b.floorY+80,b.z-b.halfDepth-60},{b.x,b.floorY+80,b.z+b.halfDepth+60})>1);
        const float floor=CollisionRay(collision,{b.x,b.floorY+80,b.z},{b.x,b.floorY-80,b.z});
        CHECK(std::fabs(floor-.5f)<.01f);
        CHECK(CollisionRay(collision,{b.x,b.floorY+900,b.z},{b.x,b.floorY+300,b.z})<=1);
    }
    const size_t colliderCount=sizeof(convergence::kPropColliders)/sizeof(convergence::kPropColliders[0]);
    CHECK(colliderCount>100);
    for(size_t i=0;i<colliderCount;++i) {
        const auto& c=convergence::kPropColliders[i];CHECK(c.x0<c.x1 && c.y0<c.y1 && c.z0<c.z1);
        const auto chosen=ConvergenceCollidersNear((c.x0+c.x1)*.5f,(c.y0+c.y1)*.5f-40,(c.z0+c.z1)*.5f,34);
        CHECK(std::find(chosen.begin(),chosen.end(),i)!=chosen.end());
        CHECK(chosen.size()<=34 && chosen.size()*12+1<=420 && chosen.size()*8+3<=420);
    }
    int trunks=0;
    for(const auto& c:convergence::kStaticFixtures) {
        if(std::string(c.name).find("Tree trunk")!=0) continue;
        ++trunks;
        CHECK(CollisionRay(collision,{float(c.x0-40),(c.y0+c.y1)*.5f,(c.z0+c.z1)*.5f},
            {float(c.x1+40),(c.y0+c.y1)*.5f,(c.z0+c.z1)*.5f})<=1);
    }
    CHECK(trunks>20);
    bool barrel=false,grave=false,bench=false,hearth=false;
    for(const auto& c:convergence::kPropColliders) {
        std::string name=c.name;
        barrel|=name.find("Storage barrel")!=std::string::npos;grave|=name.find("Grave marker")!=std::string::npos;
        bench|=name.find("Bench")!=std::string::npos;hearth|=name.find("Hearth")!=std::string::npos;
    }
    CHECK(barrel && grave && bench && hearth);
    float y=0;CHECK(fortnite::GroundHeight(-900,700,&y));
    CHECK(std::fabs(y-ConvergenceTerrainHeight({-900,700}))<.01f);
    CHECK(ConvergenceGroundHeight({-900,700})>y+15);
    CHECK(ConvergenceObstacleAt({-900-230,700-130})); // the bed footprint cannot be walked through
    for (const auto& batch:convergence::kBatches) {
        CHECK(batch.count%3==0 && batch.first+batch.count<=sizeof(convergence::kDrawVertices)/sizeof(convergence::kDrawVertices[0]));
        CHECK(batch.texture<sizeof(convergence::kTextures)/sizeof(convergence::kTextures[0]));
    }
    // Test the bytes the graphics uploader consumes, not host-endian numeric words.
    static_assert(sizeof(convergence::kTextures[0][0])==1,"RGBA16 uploads require explicit big-endian bytes");
    static_assert(sizeof(convergence::kTextures[0])==32*32*2,"one RGBA5551 texture is 2048 bytes");
    auto texel=[](size_t material,size_t pixel) {
        const uint8_t* bytes=convergence::kTextures[material]+pixel*2;
        return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0])<<8)|bytes[1]);
    };
    for (size_t material=0;material<sizeof(convergence::kTextures)/sizeof(convergence::kTextures[0]);++material) {
        for (size_t pixel=0;pixel<32*32;++pixel) CHECK((texel(material,pixel)&1)==1);
    }
    // Forest must upload green, wood brown and plaster warm, rather than neon swapped colours.
    auto average=[&](size_t material,int shift) {
        int total=0;for(size_t pixel=0;pixel<1024;++pixel) total+=(texel(material,pixel)>>shift)&31;
        return total;
    };
    CHECK(average(1,6)>average(1,11) && average(1,6)>average(1,1));
    CHECK(average(7,11)>average(7,6) && average(7,6)>average(7,1));
    CHECK(average(6,11)>average(6,6) && average(6,6)>average(6,1));
    // Area filtering must preserve the mortar line instead of sampling between seams.
    auto brightness=[](uint16_t c) { return ((c>>11)&31)+((c>>6)&31)+((c>>1)&31); };
    CHECK(brightness(texel(5,0))+5 < brightness(texel(5,4*32+4)));
    auto layout=GenerateConvergenceLayout();CHECK(layout.pois.size()==10 && layout.bossSpots.size()==7 && layout.lootSpots.size()>=30);
    for (int seed=1;seed<=8;++seed) {
        net::LoopbackNetwork network; auto& host=network.Server();
        GameServer server(host,seed,MapOf(0).fallback,40);server.SetBossCount(3);
        CHECK(server.SelectMap(kConvergenceMapIndex));
        server.SetPlayerLimit(2);server.Sim().match.AddHuman(1);
        CHECK(server.StartMatch());std::set<int> kinds;
        CHECK(server.Sim().match.Bosses().size()==7);
        for (const auto& b:server.Sim().match.Bosses()) {
            CHECK(b.kind==ConvergenceBossAt(b.home) || IsChuKind(b.kind));kinds.insert(static_cast<int>(b.kind));
            CHECK(ConvergenceDryGround(b.home) && !ConvergenceObstacleAt(b.home));
        }
        CHECK(kinds.size()>=3);
        for (const auto& l:server.Sim().match.Loot()) CHECK(ConvergenceDryGround(l.spawn.pos) && !ConvergenceObstacleAt(l.spawn.pos));
    }
    fortnite::UseTerrainForMap(kSandboxMapIndex);CHECK(fortnite::gSandboxTerrain);
    fortnite::UseTerrainForMap(kFortniteMapIndex);CHECK(!fortnite::gSandboxTerrain && fortnite::gHeightData==fortnite::kHeights);
    std::printf("Convergence: %s (%d failures)\n",failures?"FAILED":"passed",failures);return failures?1:0;
}
