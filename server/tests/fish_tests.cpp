#include "../../shared/lobby_fish.h"
#include "../../shared/lobby_fish_model.h"
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
            Require(Length(Sub(f.pos,before[i].pos))<3,"smooth motion");
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
        approached |= tank.fish[0].pos.z<-35;
    }
    Require(cleaned,"cleaners actually reach a cleaning visit");
    Require(approached,"curious clown comes to front");
    tank.Reset();
    Require(tank.time==0 && tank.fish[1].species==0 && tank.fish[4].species==1,"reset restores species");
    tank.Step(NAN,{0,0,0});
    Require(tank.time==0,"reject invalid dt");
    Require(sizeof(kClownfish)/sizeof(*kClownfish)%3==0 && sizeof(kCleanerWrasse)/sizeof(*kCleanerWrasse)%3==0,"complete triangles");
    std::puts("lobby fish tests passed");
}
