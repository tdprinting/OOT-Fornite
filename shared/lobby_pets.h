#pragma once
#include <algorithm>
#include <cmath>

namespace royale::lobby {
struct Area { const char* name; int scene, entrance; };
inline constexpr Area kAreas[] = {
    {"Temple of Time", 0x43, 0x053}, {"Kokiri Forest", 0x55, 0x0EF},
    {"Lon Lon Ranch", 0x63, 0x158}, {"Kakariko Village", 0x52, 0x0DC},
    {"Lake Hylia", 0x57, 0x103},
};
inline constexpr int kAreaCount = sizeof(kAreas)/sizeof(kAreas[0]);
inline const Area& WaitingArea(int i) { return kAreas[std::clamp(i,0,kAreaCount-1)]; }
inline bool IsWaitingScene(int scene) { for (auto& a:kAreas) if(a.scene==scene)return true; return false; }
struct Point { float x=0,z=0; };
inline float Distance(Point a,Point b) { return std::hypot(a.x-b.x,a.z-b.z); }
enum class Activity { Greet, Chase, Drawing, Celebrate, Rest, Attention };
// One shared clock/anchor coordinates the group, rather than three independent followers.
struct Group {
    Point center; float time=0,farTime=0; bool ready=false;
    void Step(Point player,float dt) {
        if(!ready) { center={player.x+100,player.z+90}; ready=true; }
        time+=dt;
        farTime=Distance(player,center)>420 ? farTime+dt : 0;
        if(farTime>2) { center={player.x+100,player.z+90}; farTime=0; }
    }
    Activity Phase() const {
        const float t=std::fmod(time,44.0f);
        return t<4 ? Activity::Greet : t<14 ? Activity::Chase : t<24 ? Activity::Drawing :
               t<28 ? Activity::Celebrate : t<37 ? Activity::Rest : Activity::Attention;
    }
    Point Goal(int pet) const {
        // The cat circles the seated baby; Maya moves between the cat and her sister.
        if(Phase()==Activity::Chase && pet==0) {
            const float a=(time-4)*.75f; return {center.x+std::sin(a)*82,center.z+std::cos(a)*82};
        }
        if(pet==0)return {center.x-72,center.z+20};
        if(pet==1)return {center.x,center.z};
        return {center.x+74,center.z+30};
    }
};
}
