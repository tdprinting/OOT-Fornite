// OOT Royale: connects the game to RoyaleSession (host/join/network), shows other players as Link puppets, and provides the
// "Battle Royale" menu with a lobby and a waiting room.
//
// STATUS: compiled in CI, NOT yet run in the game. Written against Waterdish/Shipwright-Android c9d8f4a (Shipwright 9.0.2)
// plus patches/0001 (ShouldActorInit hook). Copy into the fork with scripts/link_mod.*.
//
// Include order matters: our headers first, because the game's headers define short macro names (MIN, MAX, ABS, ...).
#include "RoyaleSession.h"
#include "war_table.h"
#include "war_table_assets.h"
#include "anim.h"
#include "cloth.h"
#include "build_version.h"
#include "lilo_anim.h"
#include "lilo_sounds.h"
#include "avriella_sounds.h"
#include "avriella_toys.h"
#include "avriella_toy_sounds.h"
#include "avriella_anim.h"
#include "maya_anim.h"
#include "maya_sounds.h"
#include "maya_phone.h"
#include "cart_model.h"
#include "chuchu_model.h"
#include "gilded_sword_icon.h"
#include "gilded_sword_surface.h"
#include "logo_data.h"
#include "fortnite_map.h"
#include "convergence_layout.h"
#include "convergence_model.h"
#include "kingdom_layout.h"
#include "kingdom_model.h"
#include "kingdom_surface_maps.h"
#include "riftlands_layout.h"
#include "riftlands_model.h"
namespace royale { namespace riftlands {
using kingdom::kSurfaceSize; using kingdom::kSurfaceClasses; using kingdom::kSurfaceUnits;
using kingdom::kSurfaceNormal; using kingdom::kSurfaceBump;
} }
#include "fortnite_puddles.h"
#include "ground_patches.h"
#include "lobby_fish.h"
#include "lobby_pets.h"
#include "lobby_fish_model.h"
#include "lobby_reef_geometry.h"
#include "fortnite_scenery.h"
#include "map.h"
#include "meshes.h"
#include "names.h"
#include "objmodel.h"
#include "skins.h"
#include "sky_model.h"
#include "graphics_stability.h"
#include "graphics_layers.h"
#include "frame_cache.h"
#include "water_sim.h"
#include "water_look.h"
#include "item_surface_maps.h"
#include "dynamic_shadows.h"
#include <libultraship/surface_map.h>
#include "tune.h"
#include "basic_pitch.h"
#include "oot_arrange.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <new>
#include <set>
#include <thread>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <deque>
#include <filesystem>
#include <random>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <sstream>
#include <cstdarg>
#ifdef __ANDROID__
#include <csignal>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <unwind.h>
#include <sys/syscall.h>
#include <jni.h>
#include "port/mobile/MobileImpl.h"
#endif

#include "soh/ShipInit.hpp"
#include "soh/ActorDB.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/nametag.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/Notification/Notification.h"
#include "soh/SohGui/SohGui.hpp"
#include "soh/SohGui/SohMenu.h"
#include "soh/cvar_prefixes.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/SaveManager.h"
#include "soh/OTRGlobals.h"
#include <SDL2/SDL.h>
#include <imgui.h>
#include <libultraship/libultraship.h>
#include <spdlog/spdlog.h>
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "src/overlays/actors/ovl_En_Box/z_en_box.h" // the treasure chest actor
#include "src/overlays/actors/ovl_En_Bom/z_en_bom.h" // the bomb (its fuse)
#include "src/overlays/actors/ovl_En_Bom_Chu/z_en_bom_chu.h" // the bombchu (its fuse)
#include "src/overlays/actors/ovl_Magic_Fire/z_magic_fire.h" // Din's Fire (its collider and screen tint, for other players' casts)
#include "src/overlays/effects/ovl_Effect_Ss_HitMark/z_eff_ss_hitmark.h" // the game's hit sparks (a blow ringing off a shield)
#include "objects/gameplay_keep/gameplay_keep.h" // Link's animation assets (gPlayerAnim_*)
#include "objects/object_zo/object_zo.h"           // the Zora NPC: skeleton, animations, eyes
#include "objects/object_km1/object_km1.h"         // the Kokiri NPC
#include "objects/object_os_anime/object_os_anime.h" // the Kokiri animations
#include "objects/object_oF1d_map/object_oF1d_map.h" // the Goron NPC
#include "objects/object_ge1/object_ge1.h"         // the Gerudo NPC
#include "objects/object_dodongo/object_dodongo.h" // the Dodongo (a mini boss)
#include "objects/object_ik/object_ik.h"           // the Iron Knuckle (a mini boss)
#include "objects/object_wf/object_wf.h"           // the Wolfos (a mini boss)
#include "objects/object_sk2/object_sk2.h"         // the Stalfos (a mini boss)
#include "objects/object_fd2/object_fd2.h"         // Volvagia (Death Mountain Crater's major boss)
#include "objects/object_zf/object_zf.h"           // the Lizalfos (a mini boss)
#include "objects/object_bigokuta/object_bigokuta.h" // the Big Octo (a mini boss)
#include "objects/object_dh/object_dh.h"           // the Dead Hand and its hands (a mini boss)
#include "objects/object_mo/object_mo.h"           // Morpha (Lake Hylia's major boss)
#include "objects/object_gnd/object_gnd.h"         // Phantom Ganon (Hyrule Field's major boss)
#include "objects/object_sst/object_sst.h"         // Bongo Bongo (Kakariko's major boss)
#include "objects/object_tw/object_tw.h"           // Twinrova (Desert Colossus's major boss)
#include "regs.h"                                // WREG, for the game's own minimap switch
#include "textures/map_grand_static/map_grand_static.h"   // the game's own overworld minimaps
extern PlayState* gPlayState;
extern int gPauseLinkFrameBuffer;
float OTRGetAspectRatio(void);
void FileChoose_LoadGame(GameState* state);
void Player_DrawPauseImpl(PlayState*, void*, void*, SkelAnime*, Vec3f*, Vec3s*, f32, s32, s32, s32, s32,
                          s32, s32, Vec3f*, Vec3f*, f32, void*, void*);
// the game's font loader (audio_load.c; it returns the font's data, used here only as "did it load"), for playing the music folder's songs
void* AudioLoad_SyncLoadFont(u32 fontId);
extern char** sequenceMap;

void Player_UseItem(PlayState* play, Player* player, s32 item);
s8 Player_ItemToItemAction(s32 item);
void Player_Draw(Actor* actor, PlayState* play);
extern f32 gRoyaleRunSpeedScale;   // Link's top run speed multiplier (patches/0011); sprinting raises it
extern s32 gRoyaleNoUseOnTakeOut;   // 1 = taking an item out never uses it too (patches/0023); the use comes from pressing B
extern s32 gRoyaleNoAimView;    // 1 = bow, slingshot, boomerang and hookshot ready and fire in place, never the first-person aiming view (patches/0019)
extern EffectSsInfo sEffectSsInfo;   // the game's particle table (z_effect_soft_sprite.c): explosions and fire give off light
extern f32 gRoyaleCamLift;   // how far the main camera's view is lifted (patches/0020); raised while you ride a cart
void FrameInterpolation_RecordOpenChild(const void* a, int b);
void FrameInterpolation_RecordCloseChild(void);
}

// The waiting room scene id lives in shared/map.h (no game headers there); make sure it still matches the engine.
static_assert(SCENE_KOKIRI_FOREST==royale::lobby::kAreas[1].scene && ENTR_KOKIRI_FOREST_0_1==royale::lobby::kAreas[1].entrance,"Kokiri waiting route");
static_assert(SCENE_LON_LON_RANCH==royale::lobby::kAreas[2].scene && ENTR_LON_LON_RANCH_0_1==royale::lobby::kAreas[2].entrance,"Ranch waiting route");
static_assert(SCENE_KAKARIKO_VILLAGE==royale::lobby::kAreas[3].scene && ENTR_KAKARIKO_VILLAGE_0_1==royale::lobby::kAreas[3].entrance,"Kakariko waiting route");
static_assert(SCENE_LAKE_HYLIA==royale::lobby::kAreas[4].scene && ENTR_LAKE_HYLIA_0_1==royale::lobby::kAreas[4].entrance,"Lake waiting route");
static_assert(SCENE_TEMPLE_OF_TIME == royale::kWaitingRoomScene, "update kWaitingRoomScene in shared/map.h");
static_assert(SCENE_HYRULE_FIELD == royale::kHyruleFieldScene, "update kHyruleFieldScene in shared/map.h");
static_assert(SCENE_HYRULE_FIELD == royale::kMaps[0].scene && SCENE_LAKE_HYLIA == royale::kMaps[1].scene && SCENE_KAKARIKO_VILLAGE == royale::kMaps[2].scene &&
              SCENE_DEATH_MOUNTAIN_CRATER == royale::kMaps[3].scene && SCENE_DESERT_COLOSSUS == royale::kMaps[4].scene, "update the scene numbers in shared/map.h");

namespace SohGui {
extern std::shared_ptr<SohMenu> mSohMenu;
}

// Set by the game's file select (patches/0008) every frame the "Battle Royale" quest option is on screen; our overlay then writes its subtitle.
int gRoyaleQuestLabel = 0;
extern "C" void Royale_ShowQuestLabel(void) {
    gRoyaleQuestLabel = 8;
}

// (defined in libultraship's gfx_pc.cpp; declared here, at file scope, because the mod's own code sits in an anonymous namespace)
struct GfxRenderingAPI* gfx_get_current_rendering_api();

namespace {

// OPEN_DISPS and CLOSE_DISPS declare these two functions inside the function that uses them. In an anonymous namespace that makes them
// *different* functions from the game's, which the linker then can't find. These are the missing definitions: they just call the real ones.
void FrameInterpolation_RecordOpenChild(const void* a, int b) { ::FrameInterpolation_RecordOpenChild(a, b); }
void FrameInterpolation_RecordCloseChild(void) { ::FrameInterpolation_RecordCloseChild(); }

// Stable child identities keep culled instances from interpolating into their neighbours.
struct DrawIdentity {
    DrawIdentity(const void* kind, int x, int z) {
        FrameInterpolation_RecordOpenChild(kind, x);
        FrameInterpolation_RecordOpenChild(kind, z);
    }
    ~DrawIdentity() {
        FrameInterpolation_RecordCloseChild();
        FrameInterpolation_RecordCloseChild();
    }
    DrawIdentity(const DrawIdentity&) = delete;
    DrawIdentity& operator=(const DrawIdentity&) = delete;
};

// A fixed place in the world near the camera that a world effect (fog banks, weather specks, wind streaks) is drawn from, with an interpolation
// identity of its own (shared/graphics_layers.h, AnchorNear): the effect's matrix stays put while the camera moves, so the frames the game blends in
// between do not drag its pieces along with the camera; crossing into the next cell starts a new identity, so that one frame is not blended.
struct WorldAnchor {
    royale::gfxlayers::Anchor at;
    DrawIdentity identity;
    WorldAnchor(const void* key, const Vec3f& eye) : at(royale::gfxlayers::AnchorNear(eye.x, eye.y, eye.z)), identity(key, at.cx * 1024 + at.cy, at.cz) {}
    Vec3f Origin() const { return { at.x, at.y, at.z }; }
};

royale::RoyaleSession gSession;
bool WarMenuOpen();
std::atomic<bool> gMenuAudioMuted{true};
void WarOpen();
void WarInput();
void WarGameUpdate();
void WarDraw(GameState* state);

#include "features/Core.inc"
#include "features/Players.inc"
#include "features/Ragdolls.inc"
#include "features/WorldObjects.inc"
#include "features/VictoryCrown.inc"
#include "features/GroundWeather.inc"
#include "features/Atmosphere.inc"
#include "features/Projectiles.inc"
#include "features/Hud.inc"
#include "features/OotVitals.inc"
#include "features/Effects.inc"
#include "features/EndMatch.inc"
#include "features/Vehicles.inc"
#include "features/MusicDiagnostics.inc"
#include "features/Scenery.inc"
#include "features/Water.inc"
#include "features/Terrain.inc"
#include "features/Lobby.inc"
#include "features/Maya.inc"
#include "features/LiloModel.inc"
#include "features/Avriella.inc"
#include "features/AvriellaToys.inc"
#include "features/Companions.inc"
#include "features/Equipment.inc"
#include "features/SceneHooks.inc"
#include "features/Menus.inc"
#include "features/Updater.inc"
#include "features/Lighting.inc"
#include "features/EngineBridge.inc"
