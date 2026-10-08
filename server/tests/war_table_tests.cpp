#include "war_table.h"
#include <cstdlib>
#include <iostream>
#include <set>
using namespace royale::wartable;
static void Check(bool ok,const char* message) {if(!ok){std::cerr<<message<<'\n';std::exit(1);}}
int main() {
    Check(ActivePage(false,false,false,false,true)==Page::Play,"Startup always reaches Play");
    Check(ActivePage(false,true,false,false,true)==Page::Lobby,"Pending joins retain cancellation");
    Check(ActivePage(true,false,true,false,true)==Page::Lobby,"Lobby has its own actions");
    Check(ActivePage(true,false,false,false,true)==Page::Pause,"Alive players get the BR pause menu");
    Check(ActivePage(true,false,false,false,false)==Page::Death,"Eliminated players get Spectate and Leave");
    Check(ActivePage(true,false,false,true,false)==Page::Results,"Results take priority over elimination");
    Check(!ShowDeathMenu(true,true,true,1.79)&&ShowDeathMenu(true,true,true,1.8),"Death menu waits for the ragdoll moment");
    Check(!ShowDeathMenu(true,false,true,8)&&!ShowDeathMenu(true,true,false,8),"Spectating or ended matches do not reopen the death menu");
    const auto base=Loadout(royale::ItemId::MasterSword,royale::Rarity::Common,true,1,1,1,0,false);
    Check(std::fabs(base.attack-1.5f*royale::kPlayerDamageScale)<.001f&&base.defense==0,"Pause attack uses normal player damage");
    const auto empowered=Loadout(royale::ItemId::MasterSword,royale::Rarity::Legendary,true,1.2f,1,.9f,.35f,true);
    Check(empowered.attack>base.attack&&empowered.defense>50,"Rarity, gear, shield and adult buffs are reflected");
    const auto empty=Loadout(royale::ItemId::FairyBow,royale::Rarity::Legendary,false,1,1,1,0,false);
    Check(std::fabs(empty.attack-royale::WeaponOf(royale::ItemId::BasicSword).damage*royale::kPlayerDamageScale)<.001f,"Empty ranged weapons show the basic melee fallback without rarity");
    std::set<int> maps;
    int current=0;
    for(int i=0;i<royale::kPlayableMapCount;++i){maps.insert(current);current=CycleMap(current,1);}
    Check(current==0&&maps.size()==royale::kPlayableMapCount,"Every playable map must be reachable exactly once");
    Check(!maps.count(royale::kSandboxMapIndex),"Sandbox belongs in Practice, not the competitive picker");
    Check(maps.count(royale::kConvergenceMapIndex),"Convergence must be available");
    Check(CycleMap(CycleMap(0,-1),1)==0,"Map navigation must wrap in both directions");
    for(int bits=0;bits<32;++bits) {
        bool joined=bits&1,host=bits&2,lobby=bits&4,player=bits&8,pending=bits&16;
        Check(CanStart(joined,host,lobby,player,pending)==(bits==15),"Only a connected host in the lobby can start once");
    }
    uint16_t port=0;
    Check(ValidPort("1024",&port)&&port==1024,"Lowest supported port");
    Check(ValidPort("65535",&port)&&port==65535,"Highest supported port");
    for(const char* s:{"","0","1023","65536","-1","99999999999","12x4"," 1234"})Check(!ValidPort(s,&port),"Reject unsafe port");
    Check(ValidAddress("192.168.1.23")&&ValidAddress("friend.local"),"LAN and DNS names are accepted");
    Check(!ValidAddress("")&&!ValidAddress("host name")&&!ValidAddress("host/../../"),"Malformed addresses rejected");
    std::vector<Widget> buttons={{1,{0,0,100,25},"one","",true},{2,{110,0,100,25},"two","",false},{3,{0,40,100,25},"three","",true},{4,{110,40,100,25},"four","",true}};
    Check(Neighbor(buttons,0,0,1)==2,"Down follows the same column");
    Check(Neighbor(buttons,0,1,0)==3,"Navigation skips disabled actions");
    Check(Neighbor(buttons,2,1,0)==3,"Right reaches adjacent control");
    Check(Neighbor(buttons,3,1,0)==3,"Focus remains valid at edge");
    Repeat repeat;
    Check(repeat.Tick(1,.05f),"First stick direction responds immediately");
    Check(!repeat.Tick(1,.05f),"Holding does not repeat every frame");
    Check(repeat.Tick(-1,.05f),"Reversal responds immediately");
    repeat.Tick(0,.05f);Check(repeat.Tick(-1,.05f),"Release resets repeat delay");
    float a=0,b=0;
    for(int i=0;i<20;++i)a=Approach(a,100,.05f,false);
    for(int i=0;i<60;++i)b=Approach(b,100,1.0f/60,false);
    Check(std::fabs(a-b)<.01f,"Animation is independent of update frequency");
    Check(Approach(0,100,.01f,true)==100,"Reduced motion snaps immediately");
    for(auto size:{std::pair<float,float>{1920,1080},{1280,800},{640,480},{3840,1080}}) {
        auto c=Canvas::Fit(size.first,size.second);
        Check(std::fabs(c.X(size.first/2)-320)<.001f&&std::fabs(c.Y(size.second/2)-180)<.001f,"Pointer and native canvas align across aspect ratios");
        Check(c.left>=0&&c.top>=0,"Canvas stays inside screen");
    }
    std::cout<<"War Table navigation, permissions, validation and animation tests passed\n";
}
