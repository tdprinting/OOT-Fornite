"""Compile production gear and grenade code with deterministic engine stubs (Windows)."""
from pathlib import Path
import subprocess
from test_war_table_matrix import function
ROOT = Path(__file__).resolve().parents[1]
players = (ROOT/'mod/Royale/features/Players.inc').read_text()
terrain = (ROOT/'mod/Royale/features/Terrain.inc').read_text()
projectiles = (ROOT/'mod/Royale/features/Projectiles.inc').read_text()
code = r'''
#include <cassert>
#include <cmath>
#include <vector>
#include <iostream>
#include "shared/combat.h"
#include "shared/skins.h"
using s8=int8_t; using u8=uint8_t; using s16=int16_t; using u16=uint16_t; using s32=int32_t;
namespace royale {
struct HudState {
 struct { int gearMask=0; struct {int item=0;} gear[kGearSlots]; } inv;
 ItemId weapon=ItemId::BasicSword, shield=ItemId::HylianShield;
 bool hasShield=true; float invulnLeft=0;
};
}
constexpr int PLAYER_SHIELD_NONE=0, PLAYER_SHIELD_DEKU=1, PLAYER_SHIELD_HYLIAN=2, PLAYER_SHIELD_MIRROR=3;
constexpr int PLAYER_BOOTS_KOKIRI=0, PLAYER_MASK_NONE=0;
constexpr int PLAYER_TUNIC_KOKIRI=0, PLAYER_TUNIC_GORON=1, PLAYER_TUNIC_ZORA=2;
constexpr int EQUIP_TYPE_SHIELD=1, EQUIP_TYPE_TUNIC=2, PLAYER_STATE1_SHIELDING=1;
void Player_Draw() {} void LocalLink_Draw() {}
struct Player { s8 currentShield=0,currentTunic=0; u8 currentMask=0; int heldItemAction=0,stateFlags1=0;
 struct { void (*draw)()=Player_Draw; } actor; };
int equipment[4]={}; int refreshes=0; float gSelfInvulnLeft=0;
void Inventory_ChangeEquipment(int type,u16 value) {equipment[type]=value;}
int Player_ActionToModelGroup(Player*,int) {return 0;}
void Player_SetModelGroup(Player*,int) {++refreshes;}
int PlayerBootsFor(royale::ItemId) {return 0;}
int PlayerMaskFor(royale::ItemId) {return 0;}
bool field=true;
bool InField() {return field;}
bool LiveAndAlive(const royale::HudState&) {return true;}
bool VictoryWalk(const royale::HudState&) {return false;}
struct LocalDress {bool on=false; s8 boots=0; u8 mask=0; royale::ItemId weapon=royale::ItemId::BasicSword;};
LocalDress gLocalDress;
bool gLocalMaskSaved=false; u8 gSavedLocalMask=PLAYER_MASK_NONE;
uint32_t gLocalTunic=royale::PackRgb(30,105,27);
struct Projectile {float x,y,z,vx,vy,vz,life,gravity,spin; uint32_t variant; bool explodes; royale::Rarity rarity;};
std::vector<Projectile> gProjectiles;
void* gPlayState=reinterpret_cast<void*>(1);
struct Vec3f {float x,y,z;};
constexpr int NA_SE_IT_ARROW_SHOT=1,NA_SE_IT_SLING_SHOT=2,NA_SE_IT_BOMB_IGNIT=3,NA_SE_IT_BOOMERANG_THROW=4;
float gSfxDefaultFreqAndVolScale=1,gSfxDefaultReverb=0;
void Audio_PlaySoundGeneral(u16,Vec3f*,int,float*,float*,float*) {}
'''
code += '\n'.join(function(players,n) for n in ('PlayerShieldFor','LocalWearTunic'))
code += '\n'.join(function(terrain,n) for n in ('WornGear','SyncLocalDress'))
code += function(projectiles,'SpawnProjectileFrom')
code += r'''
int main() {
 royale::HudState h; Player p;
 int slot=static_cast<int>(royale::GearSlot::Tunic);
 assert(LocalWearTunic(h)==gLocalTunic);
 h.inv.gearMask=1<<slot;
 for(auto item : {royale::ItemId::GoronTunic,royale::ItemId::ZoraTunic,royale::ItemId::KokiriTunic}) {
  h.inv.gear[slot].item=static_cast<int>(item); SyncLocalDress(&p,h);
  int expected=item==royale::ItemId::GoronTunic?1:item==royale::ItemId::ZoraTunic?2:0;
  assert(p.currentTunic==expected && equipment[EQUIP_TYPE_TUNIC]==expected+1);
  assert(LocalWearTunic(h)==(expected==1?royale::PackRgb(100,20,0):expected==2?royale::PackRgb(0,60,100):gLocalTunic));
 }
 for(auto shield : {royale::ItemId::DekuShield,royale::ItemId::HylianShield,royale::ItemId::MirrorShield}) {
  h.shield=shield; SyncLocalDress(&p,h); assert(p.currentShield==PlayerShieldFor(shield));
  equipment[EQUIP_TYPE_SHIELD]=0; SyncLocalDress(&p,h);
  assert(equipment[EQUIP_TYPE_SHIELD]==PlayerShieldFor(shield));
 }
 h.hasShield=false; p.stateFlags1=PLAYER_STATE1_SHIELDING; SyncLocalDress(&p,h);
 assert(p.currentShield==0 && p.stateFlags1==0 && equipment[EQUIP_TYPE_SHIELD]==0);
 for(s16 yaw : {s16(0),s16(16384),s16(-16384)}) {
  gProjectiles.clear(); SpawnProjectileFrom(royale::ItemId::ShockwaveGrenade,0,45,0,yaw,royale::Rarity::Legendary);
  assert(gProjectiles.size()==1); const auto& g=gProjectiles[0];
  assert(g.variant==10 && g.explodes && g.gravity>0 && g.life>0.7f);
  assert(std::fabs(std::hypot(g.vx,g.vz)*g.life-520)<1);
  assert(45+g.vy*g.life-g.gravity*g.life*g.life/2<5);
 }
 field=false; gProjectiles.clear(); SpawnProjectileFrom(royale::ItemId::ShockwaveGrenade,0,45,0,0,royale::Rarity::Common);
 assert(gProjectiles.empty());
 std::cout<<"Production gear colors, shield resync/removal, and grenade flight passed\n";
}
'''
build=ROOT/'war-table-compile/equipment-regression'; build.mkdir(parents=True,exist_ok=True)
(build/'test.cpp').write_text(code)
vc='C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat'
(build/'run.cmd').write_text('@echo off\ncall "'+vc+'" >nul\ncl /nologo /EHsc /std:c++20 /I"'+str(ROOT)+'" test.cpp /Fetest.exe >compile.log 2>&1\nif errorlevel 1 exit /b 1\ntest.exe\n')
r=subprocess.run(['cmd.exe','/c',str(build/'run.cmd')],cwd=build)
if r.returncode: print((build/'compile.log').read_text(errors='replace')[-4000:])
raise SystemExit(r.returncode)
