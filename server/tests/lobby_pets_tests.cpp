#include "lobby_pets.h"
#include <cassert>
#include <cstdio>
int main() {
    namespace L=royale::lobby;
    assert(L::WaitingArea(-1).scene==0x43);
    assert(L::WaitingArea(1).scene==0x55);
    assert(L::WaitingArea(100).scene==0x57);
    assert(!L::IsWaitingScene(0));
    for(auto& a:L::kAreas) assert(L::IsWaitingScene(a.scene));
    L::Group g;g.Step({0,0},.05f);auto anchor=g.center;
    for(int i=0;i<400;++i)g.Step({float(i%100),20},.05f);
    assert(L::Distance(anchor,g.center)==0); // Small steps do not drag the play group around.
    bool seen[6]={};
    for(int i=0;i<900;++i) {
        g.Step({0,0},.05f);seen[int(g.Phase())]=true;
        for(int p=0;p<3;++p)assert(L::Distance(g.Goal(p),g.center)<90);
        assert(L::Distance(g.Goal(0),g.Goal(1))>60);
    }
    for(bool phase:seen)assert(phase);
    for(int i=0;i<30;++i)g.Step({1000,1000},.05f);
    assert(L::Distance(anchor,g.center)==0); // Brief excursions do not cancel play.
    for(int i=0;i<20;++i)g.Step({1000,1000},.05f);
    assert(L::Distance(g.center,{1000,1000})<150);
    // Group events: every one happens within a cycle, never overlaps another, and keeps the pets near the group and apart.
    g=L::Group{};g.Step({0,0},.05f);bool ev[6]={};
    for(int i=0;i<L::kTrioCycle*20;++i) {
        g.Step({0,0},.05f);float into;auto e=g.Event(&into);ev[int(e)]=true;
        if(e==L::Trio::None){assert(into<0);continue;}
        assert(into>=0 && into<=L::kTrioLength+.01f);
        for(int p=0;p<3;++p){assert(L::Distance(g.TrioGoal(e,p,into),g.center)<130);}
        if(e==L::Trio::Circle||e==L::Trio::Nap||e==L::Trio::Dance)for(int a=0;a<3;++a)for(int b=a+1;b<3;++b)assert(L::Distance(g.TrioGoal(e,a,into),g.TrioGoal(e,b,into))>30);
    }
    for(bool seenEvent:ev)assert(seenEvent);
    g=L::Group{};assert(!g.ready);
    puts("Lobby areas and coordinated pet behavior passed.");
}
