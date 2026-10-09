#include "game_server.h"
#include "loopback.h"
#include "fortnite_map.h"
#include <cstdio>
using namespace royale;
int failures=0;
#define CHECK(c) do{if(!(c)){std::printf("FAIL %d: %s\n",__LINE__,#c);++failures;}}while(0)
int main() {
    for(int map : {kMiniBossArenaIndex,kMainBossArenaIndex}) {
        CHECK(IsTestMap(map) && IsBossArena(map) && IsIslandMap(map) && !IsPlayableMap(map));
        fortnite::UseTerrainForMap(map);
        CHECK(fortnite::gTerrainMapId==map && fortnite::gSandboxTerrain);
        for(float x=-2800;x<=2800;x+=200) for(float z=-2800;z<=2800;z+=200) {
            float y=999;CHECK(fortnite::GroundHeight(x,z,&y) && y==0 && !fortnite::IsWaterAt(x,z));
        }
        float wall=0;CHECK(fortnite::GroundHeight(map==kMainBossArenaIndex?4900.0f:3900.0f,0,&wall) && wall>500);
        net::LoopbackNetwork network;GameServer server(network.Server(),42,MapOf(0).fallback,10);
        CHECK(!server.SelectMap(map));server.SetSandbox(true);CHECK(server.SelectMap(map));
        server.Sim().match.AddHuman(1);CHECK(server.StartMatch());auto& m=server.Sim().match;
        CHECK(server.Sandbox() && m.State()==MatchState::InMatch && m.Players().size()==1 && m.Bosses().empty());
        CHECK(server.Props().empty());
        CHECK(Distance(m.Find(1)->pos,arena::kSpawn)<1);
        CHECK(!m.Loot().empty());
        for(const auto& l:m.Loot()) CHECK(Distance(l.spawn.pos,arena::kArmory)<=kSandboxLootRadius);
        for(int i=0;i<kBossKindCount;i++) {
            const auto kind=static_cast<BossKind>(i);
            if(IsMajorKind(kind)!=(map==kMainBossArenaIndex)) continue;
            CHECK(m.SandboxBossEncounter(kind,1));CHECK(m.Bosses().size()==1 && m.Bosses()[0].kind==kind);
            CHECK(Distance(m.Bosses()[0].home,arena::kBoss)<1);
            CHECK(!m.SandboxGodOn() && m.Bosses()[0].target==1);
            // Starting a fight must allow real boss damage, even from the distant spawn.
            for(int tick=0;tick<kTickHz*30 && m.Find(1)->health==m.Find(1)->maxHealth;tick++) m.Tick(1.0f/kTickHz);
            CHECK(m.Find(1)->health<m.Find(1)->maxHealth);
            auto* p=m.Find(1);p->alive=false;p->health=0;p->burnUntil=m.Clock()+20;
            CHECK(m.SandboxBossEncounter(kind,1));
            CHECK(p->alive && p->health==p->maxHealth && p->burnUntil<=m.Clock());
        }
        CHECK(!m.SandboxBossEncounter(map==kMainBossArenaIndex?BossKind::Stone:BossKind::DragonFire,1));
        CHECK(m.Bosses().size()==1 && m.SandboxClearArena(1) && m.Bosses().empty());
        for(int t=0;t<20;t++) m.Tick(1.0f/kTickHz);
        CHECK(m.State()==MatchState::InMatch && m.StormTime()==0);
    }
    fortnite::UseTerrainForMap(kFortniteMapIndex);
    std::printf("Boss arenas: %s\n",failures?"FAILED":"passed");return failures?1:0;
}
