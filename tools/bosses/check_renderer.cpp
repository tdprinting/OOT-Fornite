// Compile and exercise the real draw function with Shipwright's display-list macros.
// Stub world/matrix allocation only; this checks texture dimensions, pointers and batches.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
#include "boss.h"
#include "morrow_model.h"
#define _LANGUAGE_C
#define F3DEX_GBI_2
#include <libultraship/libultra/gbi.h>
using BK=royale::BossKind;
using BM=royale::DragonMode;
struct Actor { struct {struct {float x=0,y=0,z=0;} pos;} world; };
struct State {void* gfxCtx=nullptr;};
struct PlayState {State state;};
struct BossActor {int kind=0,mode=0,aux=0;float moved=0,smashAge=10,modeAge=0,hurtAge=10,flashAge=10,flashLen=0;int16_t rot=0;float hp=1,lastHp=1,x=0,z=0;bool modeFresh=false,morrowAlerted=false;};
bool gHitFlash=true;
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

using s16=int16_t;using u8=uint8_t;
namespace GfxLayerId {constexpr int Characters=0;}
struct GfxLayer {GfxLayer(PlayState*,int) {}};
void* FrameAlloc(PlayState*,size_t bytes) {return Graph_Alloc(nullptr,bytes);}
void Matrix_RotateX(float,int) {}
#include "RoyaleMorrow.h"
void PlayMorrowSound(int,float,float) {}
int main() {
 Actor actor;PlayState play;BossActor boss;boss.kind=static_cast<int>(BK::Hollowbell);
 for(int mode=0;mode<static_cast<int>(BM::Count);mode++) {
  output=commands;boss.mode=mode;Morrow_Update(&play,boss);Morrow_Draw(&actor,&play,boss);
  if(output>=commands+3000 || allocated.size()!=std::size(royale::morrow_model::kCorners)) return 1;
  for(Gfx* g=commands;g<output;g++) if((g->words.w0>>24)==G_VTX) {
   const auto n=(g->words.w0>>12)&0xff; if(n>32 || n%3) return 2;
  }
 }
 std::puts("Morrow draw: every mode, valid display-list batches and vertex allocation");
}
