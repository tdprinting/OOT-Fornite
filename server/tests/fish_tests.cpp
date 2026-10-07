#include "../../shared/lobby_fish.h"
#include "../../shared/lobby_fish_model.h"
#include "../../shared/lobby_reef_geometry.h"
#include <cstdio>
#include <cstdlib>

using namespace royale::reef;
void Require(bool ok, const char* why) { if (!ok) { std::fprintf(stderr,"Fish test: %s\n",why); std::exit(1); } }
int main() {
    Aquarium tank;
    bool cleaned = false, approached = false;
    // Fifteen minutes with an approaching/departing visitor: no escapes, bad poses or jitter from frame-sized turns.
    for (int tick=0;tick<18000;++tick) {
        const auto before=tank.fish;
        tank.Step(0.05f,{40,0,tick%600 < 300 ? -250.0f : -1200.0f});
        for (int i=0;i<kFishCount;++i) {
            const Fish& f=tank.fish[i];
            Require(std::isfinite(f.pos.x) && std::isfinite(f.pos.y) && std::isfinite(f.pos.z),"finite positions");
            Require(std::fabs(f.pos.x)<=110 && f.pos.y>=78 && f.pos.y<=135 && std::fabs(f.pos.z)<=40,"inside swimming bounds");
            Require(Length(Sub(f.pos,before[i].pos))<4.5f,"smooth motion including contact correction");
            for (int j=i+1;j<kFishCount;++j) {
                if (Length(Sub(f.pos,tank.fish[j].pos))+0.03f<FishRadius(f)+FishRadius(tank.fish[j]))
                    std::fprintf(stderr,"tick %d pair %d,%d distance %.5f required %.5f positions (%.2f,%.2f,%.2f) (%.2f,%.2f,%.2f)\n",tick,i,j,Length(Sub(f.pos,tank.fish[j].pos)),FishRadius(f)+FishRadius(tank.fish[j]),f.pos.x,f.pos.y,f.pos.z,tank.fish[j].pos.x,tank.fish[j].pos.y,tank.fish[j].pos.z);
                Require(Length(Sub(f.pos,tank.fish[j].pos))+0.03f>=FishRadius(f)+FishRadius(tank.fish[j]),"fish bodies and fins cannot overlap");
            }
            const auto* mesh=f.species ? kCleanerWrasse : kClownfish;
            const size_t n=f.species ? sizeof(kCleanerWrasse)/sizeof(*kCleanerWrasse) : sizeof(kClownfish)/sizeof(*kClownfish);
            for (size_t v=0;v<n;++v) {
                Point p=PoseVertex({mesh[v].x,mesh[v].y,mesh[v].z},mesh[v].part,f.swim,f.cleaning);
                // Circumscribed radius includes yaw/pitch: the full fish fits within tank glass and water, not just its origin.
                const float radius=Length(p)*f.size;
                Require(radius<52,"mesh fits designed clearance");
                const float y=std::cos(f.pitch)*p.y+std::sin(f.pitch)*p.z;
                const float z=-std::sin(f.pitch)*p.y+std::cos(f.pitch)*p.z;
                const Point world={f.pos.x+(std::cos(f.yaw)*p.x+std::sin(f.yaw)*z)*f.size,
                    f.pos.y+y*f.size,f.pos.z+(-std::sin(f.yaw)*p.x+std::cos(f.yaw)*z)*f.size};
                Require(std::fabs(world.x)<160 && world.y>34 && world.y<181 && std::fabs(world.z)<86,"animated fish never clip glass or water surface");
            }
            cleaned |= f.cleaning;
        }
        for (int i=0;i<kCrabCount;++i) {
            const auto& c=tank.crabs[i];
            Require(std::isfinite(c.yaw) && std::fabs(c.pos.x)<=130 && c.pos.y==32 && std::fabs(c.pos.z)<=56,"crabs stay on sand");
            for (int j=i+1;j<kCrabCount;++j)
                Require(Length(Sub(c.pos,tank.crabs[j].pos))+0.05f>=32*(c.size+tank.crabs[j].size),"crabs cannot overlap");
            for (const auto& v:kHermitCrab) {
                const Point p=PoseVertex({v.x,v.y,v.z},v.part,c.swim,c.cleaning);
                Require(Length(p)<=32,"crab separation sphere encloses animated legs and claws");
                Require(p.y>=0,"crab legs stay above the sand");
            }
        }
        approached |= tank.fish[0].pos.z<-35;
    }
    Require(cleaned,"cleaners actually reach a cleaning visit");
    Require(approached,"curious clown comes to front");
    // Deliberately overlapping fish, including at the wall, must be separated by actual bounds, not just future steering.
    for (Point origin : {Point{0,110,0},Point{110,135,40},Point{-110,78,-40}}) {
        tank.Reset();for (auto& f:tank.fish) f.pos=origin;
        tank.SeparateFish();
        for (int i=0;i<kFishCount;++i) for (int j=i+1;j<kFishCount;++j)
            Require(Length(Sub(tank.fish[i].pos,tank.fish[j].pos))+0.03f>=FishRadius(tank.fish[i])+FishRadius(tank.fish[j]),"coincident fish recover at tank corners");
    }
    // Every collision face is outward-facing, non-degenerate, and every box edge has two incident triangles.
    int edges[8][8]={};
    for (const auto& t:kTankCollisionTriangles) {
        const auto a=kTankCollisionVertices[t[0]],b=kTankCollisionVertices[t[1]],c=kTankCollisionVertices[t[2]];
        const auto u=Sub(b,a),v=Sub(c,a);
        const Point n={u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
        const Point center={(a.x+b.x+c.x)/3,(a.y+b.y+c.y)/3-kTankHeight*0.5f,(a.z+b.z+c.z)/3};
        Require(n.x*center.x+n.y*center.y+n.z*center.z>0,"outward collision winding");
        for (int k=0;k<3;++k) { const int x=std::min(t[k],t[(k+1)%3]),y=std::max(t[k],t[(k+1)%3]);++edges[x][y]; }
    }
    for (int x=0;x<8;++x) for (int y=x+1;y<8;++y) Require(edges[x][y]==0 || edges[x][y]==2,"closed watertight tank collision");
    Require(kTankScale==0.5f,"tank reduced by half");
    tank.Reset();
    Require(tank.time==0 && tank.fish[1].species==0 && tank.fish[4].species==1,"reset restores species");
    tank.Step(NAN,{0,0,0});
    Require(tank.time==0,"reject invalid dt");
    Require(sizeof(kClownfish)/sizeof(*kClownfish)%3==0 && sizeof(kCleanerWrasse)/sizeof(*kCleanerWrasse)%3==0,"complete triangles");
    std::puts("lobby fish tests passed");
}
