// Compile and exercise the real draw function with Shipwright's display-list macros.
// Stub world/matrix allocation only; this checks texture dimensions, pointers and batches.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
#include "boss.h"
#include "chuchu_model.h"
#define _LANGUAGE_C
#define F3DEX_GBI_2
#include <libultraship/libultra/gbi.h>
using BK=royale::BossKind;
using BM=royale::DragonMode;
struct Actor { struct {struct {float x=0,y=0,z=0;} pos;} world; };
struct State {void* gfxCtx=nullptr;};
struct PlayState {State state;};
struct BossActor {int kind=0,mode=0,aux=0;float moved=0,smashAge=10,modeAge=0,hurtAge=10;int16_t rot=0;};
BK KindOf(const BossActor& b) {return static_cast<BK>(b.kind);}
BM ModeOf(const BossActor& b) {return static_cast<BM>(b.mode);}
float drawTime=.35f;
float BossTime(PlayState*) {return drawTime;}
std::vector<Vtx> allocated;
void* Graph_Alloc(void*,size_t bytes) {allocated.resize(bytes/sizeof(Vtx));return allocated.data();}
Gfx commands[3000];Gfx* output;
void gSPVertex(Gfx* pkt,uintptr_t v,int n,int v0) {__gSPVertex(pkt,v,n,v0);}
#define OPEN_DISPS(ctx) do { Gfx*& POLY_OPA_DISP=output
#define CLOSE_DISPS(ctx) } while(0)
void Gfx_SetupDL_25Opa(void*) {}
constexpr int MTXMODE_NEW=0,MTXMODE_APPLY=1;
void Matrix_Translate(float,float,float,int) {}
void Matrix_RotateY(float,int) {}
void Matrix_Scale(float,float,float,int) {}
Mtx matrix;
#define MATRIX_NEWMTX(ctx) (&matrix)
#include "RoyaleChuChu.h"
int main() {
    Actor actor;PlayState play;BossActor boss;
    constexpr size_t corners=sizeof(royale::chu::kCorners)/sizeof(royale::chu::kCorners[0]);
    for(int kind=static_cast<int>(BK::ChuRed);kind<=static_cast<int>(BK::ChuDark);kind++) {
        for(int mode=0;mode<static_cast<int>(BM::Count);mode++) {
            output=commands;boss.kind=kind;boss.mode=mode;boss.aux=4;
            ChuChu_Draw(&actor,&play,boss);
            if(allocated.size()!=corners || output>=commands+3000) return 1;
            for(const auto& v:allocated) if(v.v.ob[1]<0 || v.v.ob[1]>300) return 2;
            bool uploaded=false, oneCycle=false, noPalette=false;
            for(Gfx* g=commands;g<output;g++) {
                if((g->words.w0>>24)==G_SETOTHERMODE_H) {
                    if(g->words.w1==G_CYC_1CYCLE) oneCycle=true;
                    if(g->words.w1==G_TT_NONE) noPalette=true;
                }
                if((g->words.w0>>24)==G_LOADTILE) {
                    const int w=((g->words.w1>>12)&4095)/4+1,h=(g->words.w1&4095)/4+1;
                    if(w!=256 || h!=256) return 3;
                    uploaded=true;
                }
            }
            if(!oneCycle || !noPalette) return 5;
            if(!(kind==static_cast<int>(BK::ChuDark) && mode==static_cast<int>(BM::Stunned)) && !uploaded) return 4;
        }
    }
    boss.kind=static_cast<int>(BK::ChuRed);boss.mode=static_cast<int>(BM::Patrol);
    drawTime=.35f;output=commands;ChuChu_Draw(&actor,&play,boss);auto first=allocated;
    drawTime=1.1f;output=commands;ChuChu_Draw(&actor,&play,boss);
    bool flowed=false,shimmered=false;
    for(size_t i=0;i<first.size();i++) {
        flowed |= first[i].v.tc[0]!=allocated[i].v.tc[0] || first[i].v.tc[1]!=allocated[i].v.tc[1];
        shimmered |= first[i].v.cn[0]!=allocated[i].v.cn[0];
    }
    if(!flowed || !shimmered) return 6;
    std::puts("ChuChu renderer: all five variants/modes; full 256x256 uploads and valid vertices");
}
