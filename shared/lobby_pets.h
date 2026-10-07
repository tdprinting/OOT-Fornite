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
// Things the three pets do together, only while all three are out (Royale.LobbyAllPets). One event every so often, each with its own formation.
enum class Trio { None, Conga, Tag, Circle, Nap, Dance };
constexpr float kTrioCycle=130.0f, kTrioLength=14.0f;
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
    // Which group event is on, and how many seconds into it (-1 between events).
    Trio Event(float* into=nullptr) const {
        const float t=std::fmod(time,kTrioCycle);
        for(int i=0;i<5;++i) {
            const float start=10.0f+i*25.0f;
            if(t>=start && t<start+kTrioLength) { if(into)*into=t-start; return Trio(i+1); }
        }
        if(into)*into=-1;
        return Trio::None;
    }
    // Where each pet (0 Lilo, 1 Avriella, 2 Maya) wants to be during an event.
    Point TrioGoal(Trio e,int pet,float into) const {
        const float tau=6.2831853f;
        switch(e) {
            case Trio::Conga: {   // Lilo leads a conga line round a wide loop, Avriella then Maya behind her
                const float a=into*.55f-pet*.0f-(pet==0?0.0f:pet==1?.55f:1.1f);
                return {center.x+std::sin(a)*105,center.z+std::cos(a)*105};
            }
            case Trio::Tag: {     // Maya chases Lilo round the circle; Avriella cheers from the middle
                if(pet==1)return {center.x,center.z};
                const float a=into*(pet==0?.95f:.8f)-(pet==2?.6f:0.0f);
                return {center.x+std::sin(a)*95,center.z+std::cos(a)*95};
            }
            case Trio::Circle: case Trio::Nap: {
                const float r=e==Trio::Circle ? 56.0f : 26.0f,a=tau*pet/3.0f+.5f;
                return {center.x+std::sin(a)*r,center.z+std::cos(a)*r};
            }
            case Trio::Dance: {   // a row of dancers
                return {center.x+(pet-1)*44.0f,center.z+10};
            }
            default: return Goal(pet);
        }
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
