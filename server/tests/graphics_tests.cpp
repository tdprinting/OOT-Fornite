#include "graphics_stability.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
using namespace royale::graphics;
void require(bool ok) { if (!ok) { std::cerr << "graphics regression failed\n"; std::exit(1); } }
bool close(float a, float b) { return std::fabs(a - b) < 0.01f; }
int main() {
    const auto a = CloudPosition(.6f,.7f,.5f,0,100,0,25,40);
    const auto b = CloudPosition(.6f,.7f,.5f,180,800,-130,25,40);
    require(close(a.x,b.x+180) && close(a.y+100,b.y+800) && close(a.z,b.z-130));
    const auto wind = CloudPosition(.6f,.7f,.5f,0,100,0,45,30);
    require(close(wind.x-a.x,20) && close(wind.z-a.z,-10));
    require(close(Wrap(-1,12000),11999));
    const auto before = CloudPosition(0,.5f,.5f,-.01f,0,0,0,0);
    const auto after = CloudPosition(0,.5f,.5f,.01f,0,0,0,0);
    require(before.fade == 0 && after.fade == 0);
    for (int swell=-100; swell<=100; ++swell) {
        require(WaterOffset(float(swell),-20,1) >= .75f);
        require(close(WaterOffset(float(swell),-20,0),3));
    }
    require(close(WaterOffset(5,2,.5f),6.5f));
    std::cout << "graphics regression tests passed\n";
}
