#include "game_server.h"
#include "loopback.h"
#include "morrow_model.h"
#include "morrow_sounds.h"
#include <cstdio>
#include <cmath>
using namespace royale;
int failures=0;
#define CHECK(c) do{if(!(c)){std::printf("FAIL %d: %s\n",__LINE__,#c);++failures;}}while(0)
int main(){
 net::LoopbackNetwork network;GameServer server(network.Server(),42,MapOf(0).fallback,10);
 server.SetSandbox(true);CHECK(server.SelectMap(kMiniBossArenaIndex));auto& m=server.Sim().match;
 m.AddHuman(1);CHECK(server.StartMatch());CHECK(m.SandboxBossEncounter(BossKind::Hollowbell,1));
 auto& b=const_cast<MiniBoss&>(m.Bosses()[0]);auto& p=*m.Find(1);
 p.pos={b.pos.x,b.pos.z+300};b.rot=0;
 CHECK(m.StartMiniSpecial(b,p,300));CHECK(b.mode==DragonMode::Slam && b.aux==1);
 CHECK(m.Strikes().size()==3);
 const auto road=m.Strikes();
 for(int i=0;i<3;i++){CHECK(std::fabs(road[i].hitAt-m.Clock()-(1.1f+.4f*i))<.001f);CHECK(std::fabs(road[i].at.z-b.pos.z-(150+150*i))<.01f);}
 const Vec2 remembered=road[0].at;p.pos.x+=400;CHECK(Distance(m.Strikes()[0].at,remembered)==0);
 b.mode=DragonMode::Chase;CHECK(m.StartMiniSpecial(b,p,300));CHECK(b.aux==2);
 CHECK(m.Strikes().size()==6);CHECK(std::fabs(m.Strikes()[3].hitAt-m.Clock()-2.2f)<.001f);
 b.health=15;b.moves=3;b.mode=DragonMode::Chase;CHECK(m.StartMiniSpecial(b,p,300));CHECK(b.aux==3);
 CHECK(std::fabs(m.Strikes()[6].hitAt-m.Clock()-1.2f)<.001f);
 m.SetMapId(0);b.pos.x=b.home.x+kBossLeash+100;m.TickMini(b,.01f);CHECK(m.Strikes().empty() && b.mode==DragonMode::Patrol);
 b.pos=b.home;b.mode=DragonMode::Chase;CHECK(m.StartMiniSpecial(b,p,300));
 const auto loot=m.Loot().size();m.KillBoss(b,p);CHECK(!b.alive && m.Loot().size()==loot+4);
 for(const auto& s:m.Strikes()) CHECK(s.applied);
 CHECK(morrow_model::kTriangleCount<=5000);
 unsigned count=0;for(const auto& part:morrow_model::kParts){CHECK(part.first==count && part.count%3==0);count+=part.count;}
 CHECK(count==std::size(morrow_model::kCorners));
 for(const auto& clip:morrow_snd::kClips){CHECK(clip.count>10000);for(unsigned i=0;i<clip.count;i++) CHECK(std::abs(int(clip.data[i]))<29205);}
 std::printf("Morrow combat/model/audio: %s\n",failures?"FAILED":"passed");return failures?1:0;
}
