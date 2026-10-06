// OOT Royale: connects the game to RoyaleSession (host/join/network), shows other players as Link puppets, and provides the
// "Battle Royale" menu with a lobby and a waiting room.
//
// STATUS: compiled in CI, NOT yet run in the game. Written against Waterdish/Shipwright-Android c9d8f4a (Shipwright 9.0.2)
// plus patches/0001 (ShouldActorInit hook). Copy into the fork with scripts/link_mod.*.
//
// Include order matters: our headers first, because the game's headers define short macro names (MIN, MAX, ABS, ...).
#include "RoyaleSession.h"
#include "anim.h"
#include "cloth.h"
#include "build_version.h"
#include "lilo_anim.h"
#include "lilo_sounds.h"
#include "cart_model.h"
#include "logo_data.h"
#include "fortnite_map.h"
#include "map.h"
#include "meshes.h"
#include "names.h"
#include "objmodel.h"
#include "skins.h"
#include "tune.h"
#include "basic_pitch.h"
#include "oot_arrange.h"
#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
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
#include <SDL2/SDL.h>
#include <imgui.h>
#include <libultraship/libultraship.h>
#include <spdlog/spdlog.h>

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "src/overlays/actors/ovl_En_Box/z_en_box.h" // the treasure chest actor
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
// the game's font loader (audio_load.c; it returns the font's data, used here only as "did it load"), for playing the music folder's songs
void* AudioLoad_SyncLoadFont(u32 fontId);
extern char** sequenceMap;

void Player_UseItem(PlayState* play, Player* player, s32 item);
s8 Player_ItemToItemAction(s32 item);
void Player_Draw(Actor* actor, PlayState* play);
extern f32 gRoyaleRunSpeedScale;   // Link's top run speed multiplier (patches/0011); sprinting raises it
void FrameInterpolation_RecordOpenChild(const void* a, int b);
void FrameInterpolation_RecordCloseChild(void);
}

// The waiting room scene id lives in shared/map.h (no game headers there); make sure it still matches the engine.
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

royale::RoyaleSession gSession;

// ---- where is the local player? -----------------------------------------------------------------------------------------

bool InGame() {
    return gPlayState != nullptr && GET_PLAYER(gPlayState) != nullptr && gSaveContext.fileNum >= 0 && gSaveContext.fileNum <= 2 &&
           gSaveContext.gameMode == GAMEMODE_NORMAL;
}
int gMapId = 0;   // which place this match is played in (from the server, see HudState::mapId)
bool gFortniteScene = false;   // the scene now loaded is Hyrule Field with the Fortnite map's collision (see "the Fortnite map" below)
// State the cloth and weather code shares (the weather is drawn much further down; the glider and the cap need the wind early).
royale::MatchState gStateNow = royale::MatchState::Lobby;   // the match state as of this frame (the glider only shows during the skydive)
int gHatHookCalls = 0;       // how many times the game has asked us about the cap (shown in the menu, to prove the hook is wired)
float gHatLastSwing = 0.0f;  // the last swing applied, in degrees
int gGliderClothFrames = 0;  // frames the cloth glider was drawn
float gStormWeather = 0.0f;         // 0 to 1: how far you are into the storm's dark weather
double gBoltFlashUntil = 0;   // a lightning bolt has landed nearby: the screen flashes until then
float gWeatherBlend = 0.0f;
bool gSeasonAnnounced = false;
royale::Weather gWeatherShown;
float gWeatherDensity = 1.0f;       // the local option, 0 to 2
royale::Vec2 gSignPos = {};         // where the sign in the middle of the map stands (once found)
bool gSignKnown = false;
float gClothScale = 1.0f;           // the local option: cloth and wind physics on the cap, tunic, sheath and glider, 0 (off) to 2
bool gWindOn = true;                // the local option: a breeze that moves cloth, grass and trees (off: still air)
float gWindScale = 1.0f;            // how strong the breeze is, 0 to 2 (the slider)
bool gWindStreaks = true;           // the local option: streaks in the air that show which way the wind blows
bool gTornadoOn = false;            // the easter egg: a tornado wanders the map (local only, never saved)
int gMusicMode = 0;                 // the local option: match music, 0 the game's, 1 random songs from the music folder, 2 none

// The wind, worked out from the weather: a breeze always, more in rain, thunder, snow, ash and sand and in the storm itself, gusting and slowly
// turning. Returns the wind in world units per second; `strength` is 0 to 1.
// Hireable allies as drawn (the actors and their smoothing are with the other ally code, further down).
struct AllyActor {
    Actor* actor = nullptr;
    ActorFunc origDestroy = nullptr;
    int kind = 0;
    float x = 0, z = 0, tx = 0, tz = 0;
    int16_t rot = 0, trot = 0;
    float hp = 1.0f;
    uint16_t owner = royale::net::kNoPlayer16;
    float moved = 0;
    float actAge = 10.0f;      // seconds since it attacked or healed
    bool init = false;
    // The game's own NPC model: its skeleton and animation state (see AllyNpc below)
    SkelAnime sk = {};   // zeroed like an actor's memory: the game reads the old animation when it changes one
    Vec3s joint[32] = {};
    Vec3s morph[32] = {};
    bool skReady = false;
    const void* playing = nullptr;
    float phase = 0.0f;        // walking cycle
    float walkW = 0.0f;        // 0 standing to 1 walking
    float bob = 0.0f;
    bool wasActing = false;
};
std::unordered_map<uint8_t, AllyActor> gAllies;
std::unordered_map<const Actor*, uint8_t> gAllyOf;


void WindNow(float* wx, float* wz, float* strength) {
    if (!gWindOn || gWindScale <= 0.01f) { *wx = *wz = *strength = 0.0f; return; }
    const float t = static_cast<float>(ImGui::GetTime());
    float speed = 62.0f;   // a steady breeze even on a fine day
    const float amount = gWeatherBlend * gWeatherShown.Strength();
    switch (gWeatherShown.sky) {
        case royale::Sky::Rain: speed += 120.0f * amount; break;
        case royale::Sky::Thunder: speed += 230.0f * amount; break;
        case royale::Sky::Snow: speed += 90.0f * amount; break;
        case royale::Sky::Sandstorm: speed += 330.0f * amount; break;
        case royale::Sky::Ash: speed += 150.0f * amount; break;
        case royale::Sky::Fog: speed += 15.0f * amount; break;
        default: break;
    }
    speed += 170.0f * gStormWeather;
    speed *= 0.7f + 0.3f * std::sin(t * 0.9f) + 0.16f * std::sin(t * 2.3f + 1.0f) + 0.08f * std::sin(t * 5.1f + 2.0f);   // gusts come and go
    // Squalls: rain and thunder come in sudden surges of wind, rolling in every half minute or so.
    if (gWeatherShown.sky == royale::Sky::Rain || gWeatherShown.sky == royale::Sky::Thunder) {
        const float sq = std::max(0.0f, std::sin(t * 0.21f + 1.3f)), sq4 = sq * sq * sq * sq;
        speed += (gWeatherShown.sky == royale::Sky::Thunder ? 210.0f : 110.0f) * amount * sq4;
    }
    speed *= gWindScale;
    const float dir = 0.4f + t * 0.04f + static_cast<float>(static_cast<int>(gWeatherShown.season)) * 1.1f;
    *wx = std::cos(dir) * speed;
    *wz = std::sin(dir) * speed;
    *strength = std::clamp(speed / 260.0f, 0.0f, 1.0f);
}

const royale::MapDef& CurrentMap() { return royale::MapOf(gMapId); }
// The Fortnite map is played in Hyrule Field's scene, so being in that scene is not enough: it must be the version loaded with the island's collision
// (and the other way round for the real field). Outside a lobby nobody has picked a map, so the scene alone counts.
bool InField() {
    return InGame() && gPlayState->sceneNum == CurrentMap().scene &&
           (!gSession.Joined() || (gMapId == royale::fortnite::kMapId) == gFortniteScene);
}
bool InWaitingRoom() { return InGame() && gPlayState->sceneNum == SCENE_TEMPLE_OF_TIME; }

const char* SceneName(int scene) {
    static char other[24];
    for (int i = 0; i < royale::kMapCount; i++) if (scene == royale::kMaps[i].scene) return royale::kMaps[i].name;
    if (scene == SCENE_TEMPLE_OF_TIME) return "Temple of Time (waiting room)";
    std::snprintf(other, sizeof(other), "scene 0x%02X", scene);
    return other;
}

// Scene travel. Moves the player with the same mechanism the game's own warp console command uses.
int gTravelCooldown = 0; // game frames (20 per second) before another travel request is allowed

bool gOurTravel = false; // a scene change we asked for ourselves (see SealExits)
void Trace(const char* step);   // the crash breadcrumb trail, defined with the music code
// The feature that is running right now, for the crash report. Every feature's per-frame code starts with Feat("name"), so when the game
// dies the report names the last feature that started (a string literal, so the crash handler can read it safely).
const char* volatile gFeature = "(none yet)";
inline void Feat(const char* name) { gFeature = name; }

// ---- debug switches ----------------------------------------------------------------------------------------------------------
// A temporary Debug section in the Battle Royale menu: each newer feature can be switched off to find out which one causes a crash or glitch.
// They are all on by default and remembered between runs. Remove this section (and the DebugOn checks) once the features are trusted.
struct DebugSwitch { const char* key; const char* label; };
constexpr DebugSwitch kDebugSwitches[] = {
    { "Carts", "Lon Lon Buggy carts" },
    { "Weather", "Weather (the game's rain, snow, lightning, fog, sand)" },
    { "StormWall", "Storm wall" },
    { "Foliage", "Foliage, snow cover and puddles" },
    { "Cloth", "Cloth physics (caps, tunics, sheaths, gliders)" },
    { "Music", "Custom match and lobby music" },
    { "Terrain", "Custom rocks, trees and platforms" },
    { "TimeOfDay", "Time of day changes" },
    { "Allies", "Maya, Lilo, the cat pet and allies" },
    { "BossFx", "Boss effects in the world" },
    { "Loot", "Loot and chests in the world" },
    { "Props", "Map props" },
    { "Projectiles", "Arrows, bombs and chest reveals" },
    { "Minimap", "Minimap switching" },
    { "Wind", "Wind streaks in the air" },
    { "Tornado", "Tornado easter egg" },
    { "Ragdoll", "Ragdoll bodies: full-body joints and the lobby test ragdoll" },
};
constexpr int kDebugCount = static_cast<int>(sizeof(kDebugSwitches) / sizeof(kDebugSwitches[0]));
enum DebugId { kDbgCarts, kDbgWeather, kDbgStormWall, kDbgFoliage, kDbgCloth, kDbgMusic, kDbgTerrain, kDbgTimeOfDay, kDbgAllies, kDbgBossFx,
               kDbgLoot, kDbgProps, kDbgProjectiles, kDbgMinimap, kDbgWind, kDbgTornado, kDbgRagdoll };
static_assert(kDbgRagdoll + 1 == kDebugCount, "one switch per DebugId");
bool gDebugOn[kDebugCount];
bool gDebugLoaded = false;
void LoadDebugSwitches() {
    for (int i = 0; i < kDebugCount; i++) {
        const std::string key = std::string(CVAR_SETTING("Royale.Debug.")) + kDebugSwitches[i].key;
        gDebugOn[i] = CVarGetInteger(key.c_str(), 1) != 0;
    }
    gDebugLoaded = true;
}
inline bool DebugOn(int id) {
    if (!gDebugLoaded) LoadDebugSwitches();
    return gDebugOn[id];
}

bool TravelTo(int entrance) {
    if (!InGame() || gTravelCooldown > 0 || gPlayState->transitionTrigger != TRANS_TRIGGER_OFF) return false;
    gOurTravel = true;
    gPlayState->nextEntranceIndex = entrance;
    gPlayState->transitionTrigger = TRANS_TRIGGER_START;
    gPlayState->transitionType = TRANS_TYPE_FADE_BLACK;
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK;
    gTravelCooldown = 5 * royale::kTickHz;
    return true;
}
bool GoToWaitingRoom() { Trace("travel: to the waiting room"); return TravelTo(ENTR_TEMPLE_OF_TIME_ENTRANCE); }
int EntranceFor(int mapId) {
    switch (royale::ClampMap(mapId)) {
        case 1: return ENTR_LAKE_HYLIA_0_1;
        case 2: return ENTR_KAKARIKO_VILLAGE_0_1;
        case 3: return ENTR_DEATH_MOUNTAIN_CRATER_0_1;
        case 4: return ENTR_DESERT_COLOSSUS_0_4;
        default: return ENTR_HYRULE_FIELD_0_1;
    }
}
bool GoToField() { return TravelTo(EntranceFor(gMapId)); }

// Lobby rule: players may wait in either the waiting room or the field. Once the countdown starts everyone must be in the
// field, and the client takes them there by itself.
bool WantsWaitingRoom = false; // set when joining (if the player left the option on) or by the button
bool InPlayableScene(royale::MatchState state) {
    return InField() || (state == royale::MatchState::Lobby && InWaitingRoom());
}
bool MustBeInField(royale::MatchState state) {
    return state == royale::MatchState::Countdown || state == royale::MatchState::Drop || state == royale::MatchState::InMatch;
}

// ---- rarity presentation ------------------------------------------------------------------------------------------------

// Tiers wear the colours of Ocarina of Time's rupees (green, blue, red, purple, gold), the same rupee models loot is drawn with below.
struct Rgb { u8 r, g, b; };
constexpr Rgb kRarityRgb[royale::kRarityCount] = { { 96, 210, 84 }, { 72, 132, 250 }, { 236, 64, 52 }, { 186, 92, 236 }, { 250, 210, 40 } };

// Panels are the game's own dark, slightly warm text-box black rather than a modern navy.
ImU32 OotPanel(int alpha) { return IM_COL32(16, 12, 8, alpha); }

const char* RarityName(royale::Rarity r) {
    static const char* names[royale::kRarityCount] = { "Common", "Uncommon", "Rare", "Epic", "Legendary" };
    return names[static_cast<int>(r)];
}
Color_RGBA8 RarityColor(royale::Rarity r) {
    const Rgb& c = kRarityRgb[static_cast<int>(r)];
    return Color_RGBA8{ c.r, c.g, c.b, 255 };
}
ImVec4 RarityIm(royale::Rarity r) {
    const Rgb& c = kRarityRgb[static_cast<int>(r)];
    return ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f);
}
ImU32 RarityU32(royale::Rarity r) {
    const Rgb& c = kRarityRgb[static_cast<int>(r)];
    return IM_COL32(c.r, c.g, c.b, 255);
}
// Loot is drawn with the game's own rupee models, whose colours happen to line up with the tiers.
s16 RarityDropType(royale::Rarity r) {
    switch (r) {
        case royale::Rarity::Common: return ITEM00_RUPEE_GREEN;
        case royale::Rarity::Uncommon: return ITEM00_RUPEE_BLUE;
        case royale::Rarity::Rare: return ITEM00_RUPEE_RED;
        case royale::Rarity::Epic: return ITEM00_RUPEE_PURPLE;
        default: return ITEM00_RUPEE_ORANGE;
    }
}
// Dropped piles of money and ammo use the game's own models for them (rupee colour by how much; arrows, seeds, bombs and nuts as themselves).
s16 DropTypeFor(royale::ItemId item, royale::Rarity rarity, int amount) {
    using royale::ItemId;
    switch (item) {
        case ItemId::Rupees: return amount >= 50 ? ITEM00_RUPEE_PURPLE : amount >= 20 ? ITEM00_RUPEE_RED : amount >= 5 ? ITEM00_RUPEE_BLUE : ITEM00_RUPEE_GREEN;
        case ItemId::ArrowAmmo: return ITEM00_ARROWS_MEDIUM;
        case ItemId::SeedAmmo: return ITEM00_SEEDS;
        case ItemId::BombAmmo: return ITEM00_BOMBS_A;
        case ItemId::BombchuAmmo: return ITEM00_BOMBS_B;
        case ItemId::NutAmmo: return ITEM00_NUTS;
        default: return RarityDropType(rarity);
    }
}
const char* ItemName(royale::ItemId id) {
    return royale::kItems[static_cast<int>(id)].name;
}
std::string ItemLabel(royale::ItemId id, royale::Rarity r) {
    if (!royale::InPool(id) && id != royale::ItemId::BasicSword) return ItemName(id);   // money and ammo have no rarity
    return std::string(ItemName(id)) + " [" + RarityName(r) + "]";
}
// What a pile on the ground is called: "20 Rupees", "9 Deku Seeds", or the item and its rarity.
std::string LootLabel(const royale::net::LootNet& l) {
    const royale::ItemId id = static_cast<royale::ItemId>(l.item);
    if (royale::InstantOf(id) == royale::InstantEffect::Rupees || royale::InstantOf(id) == royale::InstantEffect::Ammo)
        return std::to_string(std::max<int>(1, l.amount)) + " " + ItemName(id);
    return ItemLabel(id, static_cast<royale::Rarity>(l.rarity));
}

// ---- the real map -----------------------------------------------------------------------------------------------------------

// Is there floor under (x, z)? Used to measure the map and to keep loot, spawns and storm centres on ground.
s32 gSolidBgId = -1;   // the collision slot holding the solid scenery (see "solid scenery" below), -1 when there is none

// On the Fortnite map the scene's ground is the island's own triangles (shared/fortnite_map.h), so its height, slope and water are worked out
// directly instead of with the game's raycasts: the same answers, far cheaper (the host asks thousands of times when it lays out a match).
bool OnIsland() { return gFortniteScene && gPlayState != nullptr && gPlayState->sceneNum == SCENE_HYRULE_FIELD; }

bool RawFloorAt(float x, float z, float* outY = nullptr) {   // the scene's own floor, without our climbing blocks
    if (!InField()) return false;
    if (OnIsland()) return royale::fortnite::GroundHeight(x, z, outY);
    Vec3f pos = { x, 4000.0f, z };
    for (int tries = 0; tries < 6; tries++) {
        CollisionPoly poly;
        s32 bgId = BGCHECK_SCENE;
        const float y = BgCheck_AnyRaycastFloor2(&gPlayState->colCtx, &poly, &bgId, &pos);
        if (y <= BGCHECK_Y_MIN + 1.0f) return false;
        if (gSolidBgId >= 0 && bgId == gSolidBgId) { pos.y = y - 2.0f; continue; }   // the top of a block or a boulder: look under it
        if (outY) *outY = y;
        return true;
    }
    return false;
}

// Floor that would load another scene (a doorway, a cave mouth, the edge of the field). Nothing is ever placed on or near it, so nobody
// spawns or finds a chest where the exit seal (SealExits) would shove them back. The ROM extractor's numbers (docs/MAPS.md) show how many
// of these each map has: Kakariko alone has nine loading zones and fourteen exit surfaces.
bool OnExitFloor(float x, float z) {
    if (!InField() || OnIsland()) return false;   // the island has no exits
    CollisionPoly poly;
    Vec3f pos = { x, 4000.0f, z };
    const float y = BgCheck_AnyRaycastFloor1(&gPlayState->colCtx, &poly, &pos);
    if (y <= BGCHECK_Y_MIN + 1.0f) return false;
    return SurfaceType_GetSceneExitIndex(&gPlayState->colCtx, &poly, BGCHECK_SCENE) != 0;
}
bool NearLoadingZone(float x, float z, float within) {
    if (!InField()) return false;
    const TransitionActorContext& t = gPlayState->transiActorCtx;
    for (int i = 0; i < t.numActors && t.list != nullptr; i++) {
        if (std::hypot(x - t.list[i].pos.x, z - t.list[i].pos.z) < within) return true;
    }
    return false;
}

// ---- climbing blocks ---------------------------------------------------------------------------------------------------------
// The scene's collision can't be changed, so the stone blocks of the climbs (shared/props.h) are solid in the mod's own terms: FloorAt knows
// their tops (chests and the ledge assist see them as floor), and ApplyPlatforms holds the player on top of them and out of their sides.
std::vector<size_t> gPlatformIdx;
const royale::Prop* gPlatformSrc = nullptr;
size_t gPlatformSrcCount = 0;
std::unordered_map<size_t, float> gPlatformBase;   // floor height under each block's middle

void RefreshPlatforms() {
    if (!gSession.Client()) { gPlatformIdx.clear(); gPlatformSrc = nullptr; gPlatformSrcCount = 0; return; }
    const auto& props = gSession.Client()->Props();
    if (props.data() == gPlatformSrc && props.size() == gPlatformSrcCount) return;
    gPlatformSrc = props.data();
    gPlatformSrcCount = props.size();
    gPlatformIdx.clear();
    gPlatformBase.clear();
    for (size_t i = 0; i < props.size(); i++) if (royale::IsPlatform(props[i].kind)) gPlatformIdx.push_back(i);
}

float PlatformBase(size_t i) {
    auto it = gPlatformBase.find(i);
    if (it != gPlatformBase.end()) return it->second;
    float y = 0;
    const auto& p = gSession.Client()->Props()[i];
    if (!RawFloorAt(p.pos.x, p.pos.z, &y)) return -1.0e9f;   // not measurable yet: don't remember it
    gPlatformBase[i] = y;
    return y;
}

// The top of the highest block covering (x, z), widened by `margin` all round.
bool PlatformTopAt(float x, float z, float* top, float margin = 0.0f) {
    RefreshPlatforms();
    bool found = false;
    for (size_t i : gPlatformIdx) {
        const royale::Prop& p = gSession.Client()->Props()[i];
        if (std::fabs(x - p.pos.x) > royale::kPlatformHalf + margin || std::fabs(z - p.pos.z) > royale::kPlatformHalf + margin) continue;
        const float base = PlatformBase(i);
        if (base < -1.0e8f) continue;
        const float t = base + royale::PlatformHeight(p.kind);
        if (!found || t > *top) { *top = t; found = true; }
    }
    return found;
}

bool FloorAt(float x, float z, float* outY = nullptr) {
    float raw = 0, top = 0;
    const bool haveRaw = RawFloorAt(x, z, &raw);
    if (!InField()) return false;
    if (PlatformTopAt(x, z, &top) && (!haveRaw || top > raw)) { if (outY) *outY = top; return true; }
    if (!haveRaw) return false;
    if (outY) *outY = raw;
    return true;
}

float gMedianFloorY = 0;   // heights far from this are cliffs or rooftops, not ground to place things on
bool gMapMeasured = false;
float gMeasuredRadius = 0;

// Water is no place for a chest or a spawn: the surface is above the floor under it.
bool UnderWater(float x, float z, float floorY) {
    if (OnIsland()) return std::fabs(x) < royale::fortnite::kHalfX && std::fabs(z) < royale::fortnite::kHalfZ && floorY + 15.0f < royale::fortnite::kWaterY;   // one sheet over it all
    float surface = 0;
    WaterBox* box = nullptr;
    return WaterBox_GetSurface1(gPlayState, &gPlayState->colCtx, x, z, &surface, &box) != 0 && surface > floorY + 15.0f;
}

// Ground that hurts or throws you out: lava and other damage floors, floors that put you back at the last door, bottomless drops, and slopes too steep to
// stand on. (The floor property numbers are the game's own, see FUNC_80041EA4_*.) Looks at the scene's own floor only: our blocks are always fine.
bool HazardFloorAt(float x, float z) {
    if (!InField()) return false;
    if (OnIsland()) return royale::fortnite::GroundUp(x, z) < 0.8f;   // off the island, or too steep to stand on
    CollisionPoly poly;
    s32 bgId = BGCHECK_SCENE;
    Vec3f pos = { x, 4000.0f, z };
    const float y = BgCheck_AnyRaycastFloor2(&gPlayState->colCtx, &poly, &bgId, &pos);
    if (y <= BGCHECK_Y_MIN + 1.0f) return true;   // nothing there at all
    if (gSolidBgId >= 0 && bgId == gSolidBgId) return false;
    const u32 property = func_80041EA4(&gPlayState->colCtx, &poly, bgId);
    if (property == FUNC_80041EA4_RESPAWN || property == FUNC_80041EA4_STOP || property == FUNC_80041EA4_VOID_OUT) return true;
    return poly.normal.y < COLPOLY_SNORMAL(0.8f);   // steeper than about 37 degrees: you slide off it
}

bool WalkableAt(royale::Vec2 p) {
    float y;
    return FloorAt(p.x, p.z, &y) && std::fabs(y - gMedianFloorY) <= 1200.0f && !UnderWater(p.x, p.z, y) && !OnExitFloor(p.x, p.z) && !NearLoadingZone(p.x, p.z, 380.0f) && !HazardFloorAt(p.x, p.z);
}

// Probe the floor on a grid across the whole scene and fit a circle around the part that has ground. Takes a few thousand
// raycasts (a few milliseconds), once per match.
bool MeasureField(royale::Circle* out) {
    if (!InField()) return false;
    std::vector<royale::Vec2> points;
    std::vector<float> heights;
    // The window is the scene's own collision bounds (the old fixed +-9000 box cut Hyrule Field, which runs from x -11000 to 6700 and z -1800 to 16500,
    // and put its centre in the wrong place). The step grows with the area so the number of probes stays about the same.
    const CollisionContext& cc = gPlayState->colCtx;
    const float x0 = std::max(-20000.0f, cc.minBounds.x), x1 = std::min(20000.0f, cc.maxBounds.x);
    const float z0 = std::max(-20000.0f, cc.minBounds.z), z1 = std::min(20000.0f, cc.maxBounds.z);
    const float step = std::max(300.0f, std::sqrt(std::max(1.0f, (x1 - x0) * (z1 - z0)) / 9000.0f));
    for (float x = x0; x <= x1; x += step) {
        for (float z = z0; z <= z1; z += step) {
            float y;
            if (FloorAt(x, z, &y) && !UnderWater(x, z, y) && !OnExitFloor(x, z)) { points.push_back({ x, z }); heights.push_back(y); }
        }
    }
    if (points.size() < 100) return false;

    std::vector<float> sorted = heights;
    std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
    gMedianFloorY = sorted[sorted.size() / 2];

    royale::Vec2 centre = { 0, 0 };
    std::vector<royale::Vec2> kept;
    for (size_t i = 0; i < points.size(); i++) {
        if (std::fabs(heights[i] - gMedianFloorY) > 1200.0f) continue;
        kept.push_back(points[i]);
        centre.x += points[i].x;
        centre.z += points[i].z;
    }
    if (kept.size() < 100) return false;
    centre.x /= kept.size();
    centre.z /= kept.size();
    std::vector<float> dist;
    for (const auto& p : kept) dist.push_back(royale::Distance(p, centre));
    std::sort(dist.begin(), dist.end());
    float radius = dist[static_cast<size_t>(dist.size() * 0.95f)] * 0.95f; // ignore stragglers, keep a margin
    // The Fortnite map is played on the whole island, coast to coast: its towns run right out to the cliffs (Junk Junction, Lucky Landing).
    if (gMapId == royale::fortnite::kMapId) radius = dist[std::min(dist.size() - 1, static_cast<size_t>(dist.size() * 0.99f))];
    radius = std::clamp(radius, 1500.0f, royale::MapOf(gMapId).maxRadius); // a smaller arena keeps the fights close and the storm in sight (Hyrule Field may be bigger)
    *out = { centre, radius };
    gMapMeasured = true;
    gMeasuredRadius = radius;
    return true;
}

// ---- puppets (other players drawn as Link) ---------------------------------------------------------------------------

uint16_t gSpawningPuppet = 0;                              // player id being spawned right now, 0 when none
std::unordered_map<const Actor*, uint16_t> gPuppetOf;      // actor -> player id
std::unordered_map<uint16_t, Actor*> gActorOf;             // player id -> actor
std::unordered_map<uint16_t, royale::PuppetState> gState;  // latest interpolated state per player id
std::unordered_map<const Actor*, const void*> gPlaying;    // animation each puppet is currently playing
std::unordered_map<const Actor*, std::string> gPlate;      // the nameplate text each puppet currently shows

// Floor height under (x, z), or `fallback` if the ray finds nothing.
float GroundY(PlayState* play, float x, float z, float fallback) {
    CollisionPoly poly;
    Vec3f pos = { x, 4000.0f, z };
    float y = BgCheck_AnyRaycastFloor1(&play->colCtx, &poly, &pos);
    return y > BGCHECK_Y_MIN + 1.0f ? y : fallback;
}

// Where a bot is drawn. The server moves bots in x and z and sends their height above the scene's own floor: 0 on the ground, the top of a
// climbing block or boulder they stand on, or how high they still are in the skydive. (The scene floor without our scenery, because the
// scenery near you is solid and would otherwise be counted twice.)
float BotY(PlayState* play, float x, float z, float lift) {
    float y = 0;
    if (!RawFloorAt(x, z, &y)) y = GroundY(play, x, z, lift);
    return y + lift;
}

// How a puppet holds the weapon the server says it has. Player_SetModelGroup and Player_Draw read the *local* equipped sword,
// so the matching item is swapped in around those calls.
struct Look {
    s32 modelGroup;
    s8 itemAction;
    u8 buttonItem;
};
Look LookFor(royale::ItemId weapon) {
    using royale::ItemId;
    switch (weapon) {
        case ItemId::BasicSword:
        case ItemId::KokiriSword: return { PLAYER_MODELGROUP_SWORD_AND_SHIELD, PLAYER_IA_SWORD_KOKIRI, ITEM_SWORD_KOKIRI };
        case ItemId::MasterSword: return { PLAYER_MODELGROUP_SWORD_AND_SHIELD, PLAYER_IA_SWORD_MASTER, ITEM_SWORD_MASTER };
        case ItemId::BiggoronSword: return { PLAYER_MODELGROUP_BGS, PLAYER_IA_SWORD_BIGGORON, ITEM_SWORD_BGS };
        case ItemId::GiantsHammer:
        case ItemId::MegatonHammer: return { PLAYER_MODELGROUP_HAMMER, PLAYER_IA_HAMMER, ITEM_HAMMER };
        case ItemId::FairyBow: return { PLAYER_MODELGROUP_BOW_SLINGSHOT, PLAYER_IA_BOW, ITEM_BOW };
        case ItemId::TripleSlingshot:
        case ItemId::Slingshot: return { PLAYER_MODELGROUP_BOW_SLINGSHOT, PLAYER_IA_SLINGSHOT, ITEM_SLINGSHOT };
        case ItemId::Boomerang: return { PLAYER_MODELGROUP_BOOMERANG, PLAYER_IA_BOOMERANG, ITEM_BOOMERANG };
        case ItemId::Hookshot: return { PLAYER_MODELGROUP_HOOKSHOT, PLAYER_IA_HOOKSHOT, ITEM_HOOKSHOT };
        case ItemId::Longshot: return { PLAYER_MODELGROUP_HOOKSHOT, PLAYER_IA_LONGSHOT, ITEM_LONGSHOT };
        case ItemId::FireArrows: case ItemId::IceArrows: case ItemId::LightArrows:
            return { PLAYER_MODELGROUP_BOW_SLINGSHOT, PLAYER_IA_BOW, ITEM_BOW };   // the elemental arrows are loosed from the real bow
        case ItemId::DekuStick: return { PLAYER_MODELGROUP_10, PLAYER_IA_DEKU_STICK, ITEM_STICK };
        case ItemId::Bombs: case ItemId::Bombchus: case ItemId::HomingBombchus:
            return { PLAYER_MODELGROUP_EXPLOSIVES, PLAYER_IA_BOMB, ITEM_BOMB };
        case ItemId::FairyOcarina: return { PLAYER_MODELGROUP_OCARINA, PLAYER_IA_OCARINA_FAIRY, ITEM_OCARINA_FAIRY };
        default:
            if (weapon == ItemId::OcarinaOfTime || (weapon >= ItemId::ZeldasLullaby && weapon <= ItemId::PreludeOfLight))
                return { PLAYER_MODELGROUP_OOT, PLAYER_IA_OCARINA_OF_TIME, ITEM_OCARINA_TIME };
            return { PLAYER_MODELGROUP_DEFAULT, PLAYER_IA_NONE, ITEM_NONE };
    }
}
// A bottle in the hand, coloured by what is in it, as Link holds one to drink.
Look BottleLook(royale::ItemId contents) {
    using royale::ItemId;
    s8 action = PLAYER_IA_BOTTLE_POTION_RED;
    switch (contents) {
        case ItemId::GreenPotion: action = PLAYER_IA_BOTTLE_POTION_GREEN; break;
        case ItemId::BluePotion: case ItemId::SmallShieldPotion: case ItemId::LargeShieldPotion: action = PLAYER_IA_BOTTLE_POTION_BLUE; break;
        case ItemId::Milk: action = PLAYER_IA_BOTTLE_MILK_FULL; break;
        case ItemId::Fish: action = PLAYER_IA_BOTTLE_FISH; break;
        case ItemId::BlueFire: action = PLAYER_IA_BOTTLE_FIRE; break;
        case ItemId::Bug: action = PLAYER_IA_BOTTLE_BUG; break;
        case ItemId::Poe: action = PLAYER_IA_BOTTLE_POE; break;
        case ItemId::Fairy: action = PLAYER_IA_BOTTLE_FAIRY; break;
        default: break;
    }
    return { PLAYER_MODELGROUP_BOTTLE, action, ITEM_BOTTLE };
}
// The shield, boots and mask a player has, as the game's own values for drawing them on Link.
s32 PlayerShieldFor(royale::ItemId id) {
    switch (id) {
        case royale::ItemId::DekuShield: return PLAYER_SHIELD_DEKU;
        case royale::ItemId::HylianShield: return PLAYER_SHIELD_HYLIAN;
        case royale::ItemId::MirrorShield: return PLAYER_SHIELD_MIRROR;
        default: return PLAYER_SHIELD_NONE;
    }
}
s32 PlayerBootsFor(royale::ItemId id) {
    switch (id) {
        case royale::ItemId::IronBoots: return PLAYER_BOOTS_IRON;
        case royale::ItemId::HoverBoots: return PLAYER_BOOTS_HOVER;
        default: return PLAYER_BOOTS_KOKIRI;
    }
}
u8 PlayerMaskFor(royale::ItemId id) {
    using royale::ItemId;
    switch (id) {
        case ItemId::KeatonMask: return PLAYER_MASK_KEATON;
        case ItemId::SkullMask: return PLAYER_MASK_SKULL;
        case ItemId::SpookyMask: return PLAYER_MASK_SPOOKY;
        case ItemId::BunnyHood: return PLAYER_MASK_BUNNY;
        case ItemId::GoronMask: return PLAYER_MASK_GORON;
        case ItemId::ZoraMask: return PLAYER_MASK_ZORA;
        case ItemId::GerudoMask: return PLAYER_MASK_GERUDO;
        case ItemId::MaskOfTruth: return PLAYER_MASK_TRUTH;
        default: return PLAYER_MASK_NONE;
    }
}
const royale::PuppetState* StateOf(const Actor* actor) {
    auto id = gPuppetOf.find(actor);
    if (id == gPuppetOf.end()) return nullptr;
    auto st = gState.find(id->second);
    return st == gState.end() ? nullptr : &st->second;
}

// The chicken dance, made from scratch: Link's standing animation with his limbs posed over it, in four one-second bars that repeat -
// beak (hands snapping open and shut in front), wings (elbows out, flapping), tail (shaking the hips while bobbing down) and a clap.
// `t` is seconds. Limb axes were worked out from the skeleton's layout, not from seeing it, so the amplitudes are modest.
float ChickenDanceBob(float t) {
    const float bar = std::fmod(t, 4.0f);
    return bar >= 2.0f && bar < 3.0f ? -std::fabs(std::sin(t * 12.0f)) * 9.0f : 0.0f; // squatting through the tail wiggle
}

void ApplyChickenDance(Player* p, float t) {
    constexpr float deg = 32768.0f / 180.0f;
    Vec3s* j = p->skelAnime.jointTable;
    const int bar = static_cast<int>(std::fmod(t, 4.0f));
    const float beat = t * 6.2831853f * 2.5f; // 2.5 flaps or snaps a second
    const float s = std::sin(beat);
    auto add = [&](int limb, float rx, float ry, float rz) {
        j[limb].x = static_cast<s16>(j[limb].x + rx * deg);
        j[limb].y = static_cast<s16>(j[limb].y + ry * deg);
        j[limb].z = static_cast<s16>(j[limb].z + rz * deg);
    };
    switch (bar) {
        case 0: { // beak: forearms up and forward, snapping
            const float snap = s > 0 ? 1.0f : 0.0f;
            add(PLAYER_LIMB_L_SHOULDER, 0, 0, 50.0f);
            add(PLAYER_LIMB_R_SHOULDER, 0, 0, -50.0f);
            add(PLAYER_LIMB_L_FOREARM, 0, 0, 55.0f - 40.0f * snap);
            add(PLAYER_LIMB_R_FOREARM, 0, 0, -55.0f + 40.0f * snap);
            break;
        }
        case 1: { // wings: arms out, elbows flapping
            add(PLAYER_LIMB_L_SHOULDER, 0, 0, 70.0f + 10.0f * s);
            add(PLAYER_LIMB_R_SHOULDER, 0, 0, -70.0f - 10.0f * s);
            add(PLAYER_LIMB_L_FOREARM, 0, 0, 40.0f * s);
            add(PLAYER_LIMB_R_FOREARM, 0, 0, -40.0f * s);
            break;
        }
        case 2: { // tail: hands tucked at the back, hips shaking
            add(PLAYER_LIMB_L_SHOULDER, 0, 0, 25.0f);
            add(PLAYER_LIMB_R_SHOULDER, 0, 0, -25.0f);
            add(PLAYER_LIMB_L_FOREARM, 0, 0, 70.0f);
            add(PLAYER_LIMB_R_FOREARM, 0, 0, -70.0f);
            add(PLAYER_LIMB_WAIST, 0, 28.0f * std::sin(t * 6.2831853f * 4.0f), 0);
            break;
        }
        default: { // clap: arms swinging in together
            const float clap = 0.5f + 0.5f * s;
            add(PLAYER_LIMB_L_SHOULDER, 0, 0, 20.0f + 35.0f * (1.0f - clap));
            add(PLAYER_LIMB_R_SHOULDER, 0, 0, -20.0f - 35.0f * (1.0f - clap));
            add(PLAYER_LIMB_L_FOREARM, 0, 0, 30.0f);
            add(PLAYER_LIMB_R_FOREARM, 0, 0, -30.0f);
            break;
        }
    }
}

std::unordered_map<uint16_t, int> gSwingFrames; // player id -> frames of slash animation left (set when they are seen hurting someone)
std::unordered_map<uint16_t, int> gFlinchFrames; // player id -> frames of a flinch left (set when they are seen getting hurt)
std::unordered_map<uint16_t, royale::ItemId> gLastAbility;   // player id -> the ability they last used (which spell or song a cast or a tune is)
std::unordered_map<uint16_t, double> gLastAbilityAt;         // and when

void SpawnProjectileFrom(royale::ItemId weapon, float x, float y, float z, s16 yaw); // below, with the other custom models

// ---- how other players move: the game's own animations, chained the way the game chains them -----------------------------------

#define RA(n) ((LinkAnimationHeader*)&gPlayerAnim_link_##n)

// How a weapon is held decides which of Link's move sets it uses (one-handed sword, two-handed sword, hammer, bow...).
enum class Grip : uint8_t { OneHand, TwoHand, Hammer, Bow, Hook, Boomerang, Explosive, Ocarina, Bare };
Grip GripOf(royale::ItemId w) {
    using royale::ItemId;
    switch (w) {
        case ItemId::BiggoronSword: return Grip::TwoHand;
        case ItemId::MegatonHammer: case ItemId::GiantsHammer: return Grip::Hammer;
        case ItemId::FairyBow: case ItemId::Slingshot: case ItemId::TripleSlingshot: case ItemId::FireArrows: case ItemId::IceArrows: case ItemId::LightArrows: return Grip::Bow;
        case ItemId::Hookshot: case ItemId::Longshot: return Grip::Hook;
        case ItemId::Boomerang: return Grip::Boomerang;
        case ItemId::Bombs: case ItemId::Bombchus: case ItemId::HomingBombchus: case ItemId::DekuNuts: return Grip::Explosive;
        default: break;
    }
    const Look look = LookFor(w);
    if (look.modelGroup == PLAYER_MODELGROUP_OCARINA || look.modelGroup == PLAYER_MODELGROUP_OOT) return Grip::Ocarina;
    if (look.modelGroup == PLAYER_MODELGROUP_SWORD_AND_SHIELD || look.modelGroup == PLAYER_MODELGROUP_10) return Grip::OneHand;
    return Grip::Bare;
}

// An action is up to three of the game's animations in a row: a slash and its recovery, a potion opened, drunk and finished, a spell
// gathered, loosed and ended. The last one loops (walking, holding a stance) or holds its final pose until the player does something else.
struct AnimStep { LinkAnimationHeader* anim; bool loop; };
struct AnimSeq {
    AnimStep step[3] = {};
    int count = 0;
    AnimSeq() = default;
    AnimSeq(LinkAnimationHeader* a, bool loop) { Add(a, loop); }
    AnimSeq& Add(LinkAnimationHeader* a, bool loop = false) { if (count < 3 && a != nullptr) step[count++] = { a, loop }; return *this; }
};

// Sprinting, seen on anyone: the run cycle sped up (Link's own follows his real speed) and small puffs of dust from the heels.
constexpr float kSprintAnimSpeed = 1.45f;
void SprintDust(PlayState* play, Actor* actor, u32 frame) {
    if (frame % 3 != 0) return;
    Vec3f at = actor->world.pos;
    const float back = actor->shape.rot.y * (3.14159265f / 32768.0f);
    at.x -= std::sin(back) * 12.0f;
    at.z -= std::cos(back) * 12.0f;
    Actor_SpawnFloorDustRing(play, actor, &at, 8.0f, 1, 4.0f, 70, 15, true);
}
// Actions are played once, from their first frame, each time one begins (see Puppet_Update).
bool OneShotAnim(uint8_t anim) {
    using royale::Anim;
    switch (static_cast<Anim>(anim)) {
        case Anim::Attack: case Anim::Shoot: case Anim::Throw: case Anim::Drink: case Anim::Play: case Anim::Cast: case Anim::Roll:
        case Anim::JumpSlash: case Anim::SpinAttack: case Anim::HopL: case Anim::HopR: case Anim::Backflip: case Anim::ItemGet:
        case Anim::OpenChest: case Anim::Jump: case Anim::Hurt: case Anim::Dead: case Anim::Guard: return true;
        default: return false;
    }
}

// The game's sword swings in the order a player strings them together: a slash, a slash from the other side, then a finishing blow
// (sometimes a stab). One-handed swords, the two-handed Biggoron's Sword and the hammer each have their own set.
AnimSeq SwingFor(Grip grip, int combo) {
    const int n = combo % 4;
    switch (grip) {
        case Grip::Hammer:
            return n % 2 == 0 ? AnimSeq(RA(hammer_hit), false).Add(RA(hammer_hit_end)) : AnimSeq(RA(hammer_side_hit), false).Add(RA(hammer_side_hit_end));
        case Grip::TwoHand: {
            static LinkAnimationHeader* const hit[4] = { RA(fighter_Lnormal_kiru), RA(fighter_LLside_kiru), RA(fighter_LRside_kiru_finsh), RA(fighter_Lpierce_kiru) };
            static LinkAnimationHeader* const end[4] = { RA(fighter_Lnormal_kiru_end), RA(fighter_LLside_kiru_end), RA(fighter_LRside_kiru_finsh_end), RA(fighter_Lpierce_kiru_end) };
            return AnimSeq(hit[n], false).Add(end[n]);
        }
        default: {
            static LinkAnimationHeader* const hit[4] = { RA(fighter_normal_kiru), RA(fighter_Lside_kiru), RA(fighter_Rside_kiru_finsh), RA(fighter_pierce_kiru) };
            static LinkAnimationHeader* const end[4] = { RA(fighter_normal_kiru_end), RA(fighter_Lside_kiru_end), RA(fighter_Rside_kiru_finsh_end), RA(fighter_pierce_kiru_end) };
            return AnimSeq(hit[n], false).Add(end[n]);
        }
    }
}

// Standing ready with what is in hand, and the lock-on footwork, both as the game shows them for that weapon.
LinkAnimationHeader* StanceFor(Grip grip) {
    switch (grip) {
        case Grip::Bow: return RA(bow_bow_wait);
        case Grip::Hook: return RA(hook_wait);
        case Grip::Boomerang: return RA(boom_throw_waitR);
        case Grip::TwoHand: case Grip::Hammer: return RA(fighter_waitR_long);
        default: return RA(anchor_waitR);
    }
}
LinkAnimationHeader* SideStepFor(Grip grip, bool left) {
    switch (grip) {
        case Grip::Bow: return RA(bow_side_walk);
        case Grip::Hook: return RA(hook_side_walk);
        case Grip::Boomerang: return left ? RA(boom_throw_side_walkL) : RA(boom_throw_side_walkR);
        case Grip::Explosive: return left ? RA(anchor_bom_side_walkL) : RA(anchor_bom_side_walkR);
        case Grip::TwoHand: case Grip::Hammer: return left ? RA(fighter_side_walkL_long) : RA(fighter_side_walkR_long);
        default: return left ? RA(anchor_side_walkL) : RA(anchor_side_walkR);
    }
}

// The spells and songs: which magic pose (fire, wind or soul) a spell is cast with.
AnimSeq CastFor(royale::ItemId ability) {
    using royale::ItemId;
    switch (ability) {
        case ItemId::DinsFire: case ItemId::BoleroOfFire: return AnimSeq(RA(magic_honoo1), false).Add(RA(magic_honoo2)).Add(RA(magic_honoo3));
        case ItemId::FaroresWind: case ItemId::NocturneOfShadow: case ItemId::EponasSong: case ItemId::MinuetOfForest:
            return AnimSeq(RA(magic_kaze1), false).Add(RA(magic_kaze2)).Add(RA(magic_kaze3));
        case ItemId::LensOfTruth: return AnimSeq(RA(normal_okarina_start), false);   // held up to the eye
        case ItemId::MagicBeans: return AnimSeq(RA(normal_put), false);               // planted at his feet
        default: return AnimSeq(RA(magic_tamashii1), false).Add(RA(magic_tamashii2)).Add(RA(magic_tamashii3));
    }
}

AnimSeq SeqFor(uint8_t anim, royale::ItemId weapon, int combo, royale::ItemId ability, Player* player) {
    using royale::Anim;
    using royale::ItemId;
    const Grip grip = GripOf(weapon);
    switch (static_cast<Anim>(anim)) {
        case Anim::Walk: return AnimSeq(RA(normal_walk), true);
        case Anim::Run: case Anim::Sprint: return AnimSeq(RA(normal_run), true);   // a sprint is the same run, played faster
        case Anim::Attack:
            if (grip == Grip::Bow || grip == Grip::Explosive || grip == Grip::Boomerang || grip == Grip::Hook || grip == Grip::Ocarina || grip == Grip::Bare)
                return AnimSeq(RA(fighter_normal_kiru), false).Add(RA(fighter_normal_kiru_end));   // bashing with whatever is in hand
            return SwingFor(grip, combo);
        case Anim::JumpSlash:
            if (grip == Grip::Hammer) return AnimSeq(RA(hammer_hit), false).Add(RA(hammer_hit_end));
            return AnimSeq(RA(fighter_Lpower_jump_kiru), false).Add(RA(fighter_Lpower_jump_kiru_hit)).Add(RA(fighter_Lpower_jump_kiru_end));
        case Anim::SpinAttack:
            if (grip == Grip::TwoHand || grip == Grip::Hammer) return AnimSeq(RA(fighter_Lrolling_kiru), false).Add(RA(fighter_Lrolling_kiru_end));
            return AnimSeq(RA(fighter_rolling_kiru), false).Add(RA(fighter_rolling_kiru_end));
        case Anim::Shoot:
            if (grip == Grip::Hook) return AnimSeq(RA(hook_shot_ready), false).Add(RA(hook_wait), true);
            return AnimSeq(RA(bow_bow_shoot), false).Add(RA(bow_bow_shoot_end)).Add(RA(bow_bow_wait), true);
        case Anim::Throw:
            if (ability == ItemId::Hookshot || ability == ItemId::Longshot) return AnimSeq(RA(hook_shot_ready), false).Add(RA(hook_wait), true);
            if (weapon == ItemId::Boomerang) return AnimSeq(RA(boom_throwR), false).Add(RA(boom_throw_wait2waitR)).Add(RA(boom_throw_waitR), true);
            return AnimSeq(RA(normal_throw), false);
        case Anim::Drink: return AnimSeq(RA(bottle_drink_demo_start), false).Add(RA(bottle_drink_demo_wait)).Add(RA(bottle_drink_demo_end));
        case Anim::Play: return AnimSeq(RA(normal_okarina_start), false).Add(RA(normal_okarina_swing), true);
        case Anim::Cast: return CastFor(ability);
        case Anim::Roll: return AnimSeq(RA(normal_landing_roll), false);
        case Anim::HopL: return AnimSeq(RA(fighter_Lside_jump), false).Add(RA(fighter_Lside_jump_end));
        case Anim::HopR: return AnimSeq(RA(fighter_Rside_jump), false).Add(RA(fighter_Rside_jump_end));
        case Anim::Backflip: return AnimSeq(RA(fighter_backturn_jump), false).Add(RA(fighter_backturn_jump_end));
        case Anim::Guard:
            if (grip == Grip::TwoHand || grip == Grip::Hammer) return AnimSeq(RA(fighter_defense_long), false).Add(RA(fighter_defense_long_wait), true);
            return AnimSeq(RA(anchor_waitR2defense), false).Add(RA(anchor_waitR_defense_wait), true);
        case Anim::ItemGet: return AnimSeq(RA(demo_get_itemB), false);
        case Anim::OpenChest: return AnimSeq(player != nullptr && player->ageProperties != nullptr ? player->ageProperties->unk_98 : RA(demo_Tbox_open), false);
        case Anim::Jump: return AnimSeq(RA(normal_run_jump), false).Add(RA(normal_landing));
        case Anim::Hurt: return AnimSeq(RA(normal_front_shit), false);
        case Anim::Dead: return AnimSeq(RA(normal_front_downA), false).Add(RA(normal_front_downB));
        case Anim::SideL: return AnimSeq(SideStepFor(grip, true), true);
        case Anim::SideR: return AnimSeq(SideStepFor(grip, false), true);
        case Anim::Back: return AnimSeq(grip == Grip::Bow || grip == Grip::Hook ? SideStepFor(grip, false) : RA(anchor_back_walk), true);
        case Anim::Stance: return AnimSeq(StanceFor(grip), true);
        case Anim::Emote1: return AnimSeq(RA(demo_bikkuri), false);     // startled: "Wow!"
        case Anim::Emote2: return AnimSeq(RA(demo_jibunmiru), false);   // looks at his own hands
        case Anim::Emote3: return AnimSeq(RA(demo_kaoage), false).Add(RA(demo_kaoage_wait), true);   // looks up
        case Anim::Emote4: return AnimSeq(RA(demo_kenmiru1), false).Add(RA(demo_kenmiru1_wait), true);   // admires a sword
        case Anim::Emote5: return AnimSeq(RA(normal_wait), true);       // the chicken dance poses the limbs itself (ApplyChickenDance)
        case Anim::Emote6: return AnimSeq(RA(demo_get_itemA), false);   // holds his prize high, the item-get pose
        case Anim::Emote7: return AnimSeq(RA(demo_kousan), false);      // gives up
        case Anim::Emote8: return AnimSeq(RA(demo_kakeyori_mimawasi), false).Add(RA(demo_kakeyori_wait), true);   // looks all around
        default: return AnimSeq(RA(normal_wait), true);                 // Idle and anything newer than this build
    }
}
// The first animation of an action (the corpses' emote doubles use it).
LinkAnimationHeader* AnimFor(uint8_t anim, royale::ItemId weapon = royale::ItemId::BasicSword, int combo = 0) {
    return SeqFor(anim, weapon, combo, royale::ItemId::DinsFire, nullptr).step[0].anim;
}

// ---- how other players sound: the game's own effects and Link's voice, from where they stand -----------------------------------

constexpr float kPuppetHearing = 1600.0f;   // sounds further off than this are not played at all (the game fades them with distance anyway)
int gPuppetSfxThisFrame = 0;               // a cap, so 31 players at once don't use up every sound channel
u32 gPuppetSfxFrame = 0;

bool PuppetAudible(const Actor* actor) {
    if (gPlayState == nullptr) return false;
    if (gPuppetSfxFrame != gPlayState->gameplayFrames) { gPuppetSfxFrame = gPlayState->gameplayFrames; gPuppetSfxThisFrame = 0; }
    if (gPuppetSfxThisFrame >= 6) return false;
    const Player* me = GET_PLAYER(gPlayState);
    const float dx = actor->world.pos.x - me->actor.world.pos.x, dz = actor->world.pos.z - me->actor.world.pos.z;
    return dx * dx + dz * dz < kPuppetHearing * kPuppetHearing;
}
void PuppetSfx(Actor* actor, u16 sfx) {
    if (!PuppetAudible(actor)) return;
    gPuppetSfxThisFrame++;
    Audio_PlaySoundGeneral(sfx, &actor->projectedPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}
// Link's voice: the child and adult voices are separate sounds, as in the game.
void PuppetVoice(Player* player, u16 sfx) { PuppetSfx(&player->actor, static_cast<u16>(sfx + player->ageProperties->unk_92)); }
// Footsteps, jumps and landings sound like the ground they are on (grass, dirt, stone, water...).
u16 PuppetFloorSfx(Player* player, u16 sfx) { return static_cast<u16>(sfx + player->floorSfxOffset + player->ageProperties->unk_94); }

void PuppetStep(Player* player, float speedPerFrame) {
    if (!PuppetAudible(&player->actor)) return;
    gPuppetSfxThisFrame++;
    func_800F4010(&player->actor.projectedPos, PuppetFloorSfx(player, NA_SE_PL_WALK_GROUND), speedPerFrame);
}

// A spell or a song's own sound when somebody uses it (the tune itself is played by PlaySongMelody).
u16 AbilitySfx(royale::ItemId id) {
    using royale::ItemId;
    switch (id) {
        case ItemId::DinsFire: case ItemId::BoleroOfFire: return NA_SE_PL_MAGIC_FIRE;
        case ItemId::FaroresWind: case ItemId::NocturneOfShadow: return NA_SE_PL_MAGIC_WIND_WARP;
        case ItemId::NayrusLove: case ItemId::PreludeOfLight: return NA_SE_PL_MAGIC_SOUL_NORMAL;
        case ItemId::Hookshot: case ItemId::Longshot: return NA_SE_IT_HOOKSHOT_CHAIN;
        case ItemId::ShockwaveGrenade: return NA_SE_IT_BOMB_EXPLOSION;
        case ItemId::MagicBeans: return NA_SE_PL_PLANT_GROW_UP;
        case ItemId::LensOfTruth: return NA_SE_PL_MAGIC_SOUL_FLASH;
        default: return NA_SE_PL_MAGIC_SOUL_BALL;
    }
}
u16 AbilityVoice(royale::ItemId id) {
    using royale::ItemId;
    switch (id) {
        case ItemId::DinsFire: case ItemId::BoleroOfFire: return NA_SE_VO_LI_MAGIC_ATTACK;
        case ItemId::FaroresWind: case ItemId::NocturneOfShadow: return NA_SE_VO_LI_MAGIC_FROL;
        case ItemId::NayrusLove: case ItemId::PreludeOfLight: return NA_SE_VO_LI_MAGIC_NALE;
        default: return 0;
    }
}

// The sound of an action starting: the swish of the swing and Link's shout, the bow string, the bottle, the roll...
void ActionSounds(Player* player, uint8_t anim, royale::ItemId weapon, int combo) {
    using royale::Anim;
    using royale::ItemId;
    const Grip grip = GripOf(weapon);
    switch (static_cast<Anim>(anim)) {
        case Anim::Attack:
            if (grip == Grip::Hammer) PuppetSfx(&player->actor, NA_SE_IT_HAMMER_SWING);
            else PuppetSfx(&player->actor, weapon == ItemId::MasterSword ? NA_SE_IT_MASTER_SWORD_SWING : grip == Grip::TwoHand ? NA_SE_IT_SWORD_SWING_HARD : NA_SE_IT_SWORD_SWING);
            PuppetVoice(player, combo % 4 == 2 || grip == Grip::Hammer || grip == Grip::TwoHand ? NA_SE_VO_LI_SWORD_L : NA_SE_VO_LI_SWORD_N);
            break;
        case Anim::JumpSlash:
            PuppetVoice(player, NA_SE_VO_LI_SWORD_L);
            PuppetSfx(&player->actor, PuppetFloorSfx(player, NA_SE_PL_JUMP));
            break;
        case Anim::SpinAttack:
            PuppetSfx(&player->actor, NA_SE_IT_ROLLING_CUT);
            PuppetVoice(player, NA_SE_VO_LI_SWORD_L);
            break;
        case Anim::Shoot:
            PuppetSfx(&player->actor, grip == Grip::Hook ? NA_SE_IT_HOOKSHOT_READY : weapon == ItemId::Slingshot || weapon == ItemId::TripleSlingshot ? NA_SE_IT_SLING_DRAW : NA_SE_IT_BOW_DRAW);
            break;
        case Anim::Throw: PuppetVoice(player, NA_SE_VO_LI_SWORD_N); PuppetSfx(&player->actor, NA_SE_PL_THROW); break;
        case Anim::Drink: PuppetSfx(&player->actor, NA_SE_PL_PUT_OUT_ITEM); break;
        case Anim::Roll: PuppetSfx(&player->actor, NA_SE_PL_ROLL); break;
        case Anim::HopL: case Anim::HopR: case Anim::Backflip: case Anim::Jump:
            PuppetVoice(player, NA_SE_VO_LI_AUTO_JUMP);
            PuppetSfx(&player->actor, PuppetFloorSfx(player, NA_SE_PL_JUMP));
            break;
        case Anim::Guard: PuppetSfx(&player->actor, NA_SE_IT_SHIELD_POSTURE); break;
        case Anim::OpenChest: PuppetSfx(&player->actor, NA_SE_EV_TBOX_OPEN); break;
        case Anim::ItemGet: PuppetSfx(&player->actor, NA_SE_SY_GET_ITEM); break;
        case Anim::Emote1: PuppetVoice(player, NA_SE_VO_LI_SURPRISE); break;
        case Anim::Hurt: PuppetVoice(player, NA_SE_VO_LI_DAMAGE_S); break;
        case Anim::Dead: PuppetVoice(player, NA_SE_VO_LI_DOWN); break;
        default: break;
    }
}

// Songs: when somebody near you plays one, you hear the real tune on the ocarina, one at a time.
int OcarinaSongOf(royale::ItemId id) {
    using royale::ItemId;
    switch (id) {
        case ItemId::ZeldasLullaby: return OCARINA_SONG_LULLABY;
        case ItemId::EponasSong: return OCARINA_SONG_EPONAS;
        case ItemId::SariasSong: return OCARINA_SONG_SARIAS;
        case ItemId::SunsSong: return OCARINA_SONG_SUNS;
        case ItemId::SongOfTime: return OCARINA_SONG_TIME;
        case ItemId::SongOfStorms: return OCARINA_SONG_STORMS;
        case ItemId::MinuetOfForest: return OCARINA_SONG_MINUET;
        case ItemId::BoleroOfFire: return OCARINA_SONG_BOLERO;
        case ItemId::SerenadeOfWater: return OCARINA_SONG_SERENADE;
        case ItemId::NocturneOfShadow: return OCARINA_SONG_NOCTURNE;
        case ItemId::RequiemOfSpirit: return OCARINA_SONG_REQUIEM;
        case ItemId::PreludeOfLight: return OCARINA_SONG_PRELUDE;
        case ItemId::FairyOcarina: { static const int simple[] = { OCARINA_SONG_LULLABY, OCARINA_SONG_EPONAS, OCARINA_SONG_SARIAS, OCARINA_SONG_SUNS, OCARINA_SONG_TIME, OCARINA_SONG_STORMS }; return simple[static_cast<int>(Rand_ZeroOne() * 5.99f)]; }
        case ItemId::OcarinaOfTime: return static_cast<int>(Rand_ZeroOne() * 11.99f);   // any of the twelve
        default: return -1;
    }
}
struct SongPlayback { bool on = false; double started = 0; };
SongPlayback gSongPlayback;
bool SongBlocked() {
    if (gPlayState == nullptr || gPlayState->pauseCtx.state != 0) return true;
    return (GET_PLAYER(gPlayState)->stateFlags2 & PLAYER_STATE2_OCARINA_PLAYING) != 0;   // you are playing the real ocarina yourself
}
void PlaySongMelody(int song) {
    if (song < 0 || gSongPlayback.on || SongBlocked()) return;
    Audio_OcaSetInstrument(1);
    Audio_OcaSetSongPlayback(static_cast<s8>(song + 1), 1);
    gSongPlayback = { true, ImGui::GetTime() };
}
void UpdateSongMelody() {
    if (!gSongPlayback.on) return;
    const double age = ImGui::GetTime() - gSongPlayback.started;
    const OcarinaStaff* staff = Audio_OcaGetDisplayingStaff();
    if ((age > 0.5 && staff != nullptr && staff->state == 0) || age > 8.0 || (gPlayState != nullptr && gPlayState->pauseCtx.state != 0)) {
        Audio_OcaSetSongPlayback(0, 0);
        Audio_OcaSetInstrument(0);
        gSongPlayback.on = false;
    }
}

// ---- the game's own spell and song effects, on whoever cast them ---------------------------------------------------------------

// Din's Fire, Farore's Wind, Nayru's Love and the ocarina's song swirls are the game's own actors. They are written for Link alone: every frame
// they find him at the head of the player list and follow him. To put one on another player, that player stands in at the head of the list
// while the effect sets itself up, updates and draws, so it follows them instead. Anything an effect would do to *your* game is undone for
// other players' casts: the clean-up that resets the magic meter (the server's), Din's Fire's red tint over the screen and its burning of
// whatever is near, and the Nayru's Love timer in the save (each diamond keeps its own, so it lasts as long as the server's protection).
enum class SpellKind : uint8_t { Fire, Wind, Love, Song };
struct SpellFx {
    Actor* actor = nullptr;
    uint16_t who = 0;       // the caster's player id (unused for your own)
    bool self = false;
    SpellKind kind = SpellKind::Fire;
    ActorFunc update = nullptr, draw = nullptr, destroy = nullptr;   // the effect's own
    int framesLeft = 0;     // Nayru's Love: game frames the diamond still stands
    int steps = 1;          // updates per frame: the song swirls run at double speed, so they don't hide a fight for five seconds
};
std::vector<SpellFx> gSpells;
constexpr int kLoveFrames = 4 * 20;   // Nayru's Love protects for 4 seconds (shared/items.h)
float gSelfInvulnLeft = 0.0f;         // seconds the server still protects you: your own diamond stands that long

SpellFx* SpellOf(const Actor* a) {
    for (SpellFx& f : gSpells) if (f.actor == a) return &f;
    return nullptr;
}
Actor* CasterOf(PlayState* play, const SpellFx& f) {
    if (f.self) return &GET_PLAYER(play)->actor;
    auto it = gActorOf.find(f.who);
    return it != gActorOf.end() ? it->second : nullptr;
}
// The caster at the head of the player list for as long as this lives.
struct CasterFirst {
    ActorListEntry* list;
    Actor* old;
    CasterFirst(PlayState* play, Actor* caster) : list(&play->actorCtx.actorLists[ACTORCAT_PLAYER]), old(list->head) {
        if (caster != nullptr) list->head = caster;
    }
    ~CasterFirst() { list->head = old; }
};
// What the diamond reads as the Nayru's Love timer: steady while it lasts, then the game's own flicker and fade over the last second, after
// which it ends itself (and gives the caster back the usual invincibility timer).
s16 LoveTimer(int framesLeft) { return static_cast<s16>(framesLeft > 20 ? 600 : 1200 - std::max(0, framesLeft)); }

void Spell_Draw(Actor* a, PlayState* play);
void Spell_Update(Actor* a, PlayState* play) {
    SpellFx* f = SpellOf(a);
    Actor* caster = f != nullptr ? CasterOf(play, *f) : nullptr;
    if (f == nullptr || caster == nullptr || f->update == nullptr || f->framesLeft < -40) { Actor_Kill(a); return; }
    if (f->kind == SpellKind::Love && f->self && gSelfInvulnLeft > 1.0f) f->framesLeft = std::max(f->framesLeft, static_cast<int>(gSelfInvulnLeft * 20.0f));
    const s16 love = gSaveContext.nayrusLoveTimer;
    {
        CasterFirst first(play, caster);
        for (int i = 0; i < f->steps; i++) {
            if (f->kind == SpellKind::Love) gSaveContext.nayrusLoveTimer = LoveTimer(f->framesLeft);
            f->update(a, play);
            if (a->update == nullptr) break;   // it ended itself
            // The effects change their own update and draw as they go (gathering, then expanding...): keep ours in front.
            if (a->update != Spell_Update) { f->update = a->update; a->update = Spell_Update; }
            if (a->draw != nullptr && a->draw != Spell_Draw) { f->draw = a->draw; a->draw = Spell_Draw; }
        }
    }
    gSaveContext.nayrusLoveTimer = love;
    if (f->kind == SpellKind::Love) f->framesLeft--;
    if (!f->self && f->kind == SpellKind::Fire) reinterpret_cast<MagicFire*>(a)->collider.base.atFlags &= ~AT_ON;   // the server decides who burns
}
void Spell_Draw(Actor* a, PlayState* play) {
    SpellFx* f = SpellOf(a);
    Actor* caster = f != nullptr ? CasterOf(play, *f) : nullptr;
    if (f == nullptr || caster == nullptr || f->draw == nullptr) return;
    if (!f->self && f->kind == SpellKind::Fire) reinterpret_cast<MagicFire*>(a)->screenTintIntensity = 0.0f;   // only the caster's screen glows
    const s16 love = gSaveContext.nayrusLoveTimer;
    if (f->kind == SpellKind::Love) gSaveContext.nayrusLoveTimer = LoveTimer(f->framesLeft);
    {
        CasterFirst first(play, caster);
        f->draw(a, play);
    }
    gSaveContext.nayrusLoveTimer = love;
}
void Spell_Destroy(Actor* a, PlayState* play) {
    SpellFx* f = SpellOf(a);
    if (f == nullptr) return;
    const auto magicState = gSaveContext.magicState, prevMagicState = gSaveContext.prevMagicState;
    const s16 love = gSaveContext.nayrusLoveTimer;
    if (f->destroy != nullptr) {
        CasterFirst first(play, CasterOf(play, *f));
        f->destroy(a, play);
    }
    gSaveContext.magicState = magicState;   // the effects reset the magic meter when they end; the meter is the server's
    gSaveContext.prevMagicState = prevMagicState;
    gSaveContext.nayrusLoveTimer = love;
    gSpells.erase(std::remove_if(gSpells.begin(), gSpells.end(), [a](const SpellFx& s) { return s.actor == a; }), gSpells.end());
}

// Spawns one of the game's spell or song effects on a caster (your own Link when `self`).
void SpawnSpell(PlayState* play, s16 actorId, s16 params, SpellKind kind, Actor* caster, uint16_t who, bool self) {
    if (play == nullptr || caster == nullptr || gSpells.size() >= 12) return;
    SpellFx f;
    f.who = who;
    f.self = self;
    f.kind = kind;
    f.framesLeft = kLoveFrames;
    f.steps = kind == SpellKind::Song ? 2 : 1;
    const s16 love = gSaveContext.nayrusLoveTimer;
    if (kind == SpellKind::Love) gSaveContext.nayrusLoveTimer = LoveTimer(f.framesLeft);   // straight to the diamond: the casting orb dims the whole scene
    Actor* a = nullptr;
    {
        CasterFirst first(play, caster);
        a = Actor_Spawn(&play->actorCtx, play, actorId, caster->world.pos.x, caster->world.pos.y, caster->world.pos.z, 0, 0, 0, params, true);
    }
    gSaveContext.nayrusLoveTimer = love;
    if (a == nullptr) return;
    f.actor = a;
    f.update = a->update;
    f.draw = a->draw;
    f.destroy = a->destroy;
    a->update = Spell_Update;
    if (a->draw != nullptr) a->draw = Spell_Draw;
    a->destroy = Spell_Destroy;
    gSpells.push_back(f);
}

// The swirl the game shows when Link plays a song on the ocarina, for the songs that have one (the same table the game's message code uses).
bool SongSwirl(royale::ItemId song, s16* actorId, s16* params) {
    using royale::ItemId;
    *params = 0;
    switch (song) {
        case ItemId::SariasSong: *actorId = ACTOR_OCEFF_WIPE3; return true;
        case ItemId::EponasSong: *actorId = ACTOR_OCEFF_WIPE2; return true;
        case ItemId::ZeldasLullaby: *actorId = ACTOR_OCEFF_WIPE; return true;
        case ItemId::SongOfTime: *actorId = ACTOR_OCEFF_WIPE; *params = 1; return true;
        // The warp songs, the Sun's Song and the Song of Storms do more than show a swirl in the game (a warp, a change of time, rain), so they
        // show the plain one the game uses for the scarecrow's song.
        case ItemId::MinuetOfForest: case ItemId::BoleroOfFire: case ItemId::SerenadeOfWater: case ItemId::RequiemOfSpirit:
        case ItemId::NocturneOfShadow: case ItemId::PreludeOfLight: case ItemId::SunsSong: case ItemId::SongOfStorms:
            *actorId = ACTOR_OCEFF_WIPE4; return true;
        default: return false;
    }
}

// What a spell, a song or a gadget looks like when somebody uses it. The spells are the game's own Din's Fire, Nayru's Love and Farore's Wind
// on whoever cast them, and a song you play yourself brings up the game's own swirl. The rest (and other players' songs, whose swirl fills
// the screen of whoever played them) are made from the game's own particle effects: its glitter, shock rings and explosions. `at` is the
// user's feet; `caster` is their actor.
void PowerFx(PlayState* play, royale::ItemId item, const Vec3f& at, bool self, Actor* caster, uint16_t who) {
    using royale::ItemId;
    auto ring = [&](Color_RGBA8 prim, Color_RGBA8 env, float radius, int count, float rise, s16 scale) {
        for (int i = 0; i < count; i++) {
            const float a = i * (6.2831853f / count);
            Vec3f pos = { at.x + std::sin(a) * radius, at.y + 6.0f + Rand_ZeroOne() * 12.0f, at.z + std::cos(a) * radius };
            Vec3f vel = { std::sin(a) * 0.6f, rise * (0.6f + Rand_ZeroOne()), std::cos(a) * 0.6f }, accel = { 0, 0.05f, 0 };
            EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, scale, 30);
        }
    };
    auto shock = [&](float y) { Vec3f pos = { at.x, at.y + y, at.z }, vel = { 0, 0, 0 }, accel = { 0, 0, 0 }; EffectSsBlast_SpawnWhiteShockwave(play, &pos, &vel, &accel); };
    auto spell = [&](s16 actorId, SpellKind kind) { SpawnSpell(play, actorId, 0, kind, caster, who, self); };
    if (self && (royale::IsSong(item) || item == ItemId::FairyOcarina || item == ItemId::OcarinaOfTime)) {
        s16 id = 0, params = 0;
        if (SongSwirl(item, &id, &params)) SpawnSpell(play, id, params, SpellKind::Song, caster, who, true);
    }
    switch (item) {
        case ItemId::DinsFire: spell(ACTOR_MAGIC_FIRE, SpellKind::Fire); break;
        case ItemId::NayrusLove: spell(ACTOR_MAGIC_DARK, SpellKind::Love); break;
        case ItemId::FaroresWind: spell(ACTOR_MAGIC_WIND, SpellKind::Wind); break;
        case ItemId::BoleroOfFire:
            ring({ 255, 200, 0, 255 }, { 255, 0, 0, 255 }, 60.0f, 20, 2.2f, 110); ring({ 255, 90, 0, 255 }, { 200, 0, 0, 255 }, 110.0f, 28, 1.4f, 90); shock(30.0f);
            break;
        case ItemId::MinuetOfForest: ring({ 200, 255, 120, 255 }, { 0, 200, 0, 255 }, 55.0f, 24, 2.6f, 90); break;
        case ItemId::SerenadeOfWater: ring({ 170, 230, 255, 255 }, { 0, 150, 255, 255 }, 55.0f, 24, 2.6f, 90); shock(20.0f); break;
        case ItemId::NocturneOfShadow: ring({ 230, 160, 255, 255 }, { 200, 50, 255, 255 }, 55.0f, 24, 2.6f, 90); break;
        case ItemId::RequiemOfSpirit: ring({ 255, 210, 120, 255 }, { 255, 150, 0, 255 }, 70.0f, 24, 2.0f, 100); shock(30.0f); break;
        case ItemId::PreludeOfLight: case ItemId::ZeldasLullaby: ring({ 255, 255, 220, 255 }, { 255, 230, 100, 255 }, 40.0f, 18, 2.8f, 100); break;
        case ItemId::SunsSong: ring({ 255, 255, 180, 255 }, { 255, 220, 0, 255 }, 80.0f, 28, 2.0f, 110); shock(40.0f); break;
        case ItemId::SongOfTime: ring({ 190, 255, 255, 255 }, { 80, 190, 240, 255 }, 80.0f, 28, 2.0f, 110); shock(40.0f); break;
        case ItemId::EponasSong: ring({ 255, 200, 140, 255 }, { 200, 120, 40, 255 }, 45.0f, 16, 1.6f, 80); break;
        case ItemId::SariasSong: ring({ 190, 255, 190, 255 }, { 0, 220, 100, 255 }, 45.0f, 16, 1.6f, 80); break;
        case ItemId::SongOfStorms:
            if (self) Environment_AddLightningBolts(play, 2);
            ring({ 255, 255, 255, 255 }, { 120, 120, 255, 255 }, 70.0f, 20, 2.4f, 90);
            break;
        case ItemId::ShockwaveGrenade:   // a purple blast at the feet that throws the user up
            ring({ 235, 190, 255, 255 }, { 160, 60, 255, 255 }, 30.0f, 16, 3.4f, 110);
            ring({ 210, 150, 255, 255 }, { 120, 40, 230, 255 }, 65.0f, 22, 1.2f, 90);
            shock(4.0f);
            break;
        case ItemId::MagicBeans: ring({ 180, 255, 140, 255 }, { 60, 200, 0, 255 }, 25.0f, 12, 2.0f, 80); break;
        case ItemId::LensOfTruth: ring({ 235, 190, 255, 255 }, { 150, 80, 220, 255 }, 25.0f, 12, 1.5f, 80); break;
        default: break;
    }
}

// Someone used an ability: the spell's sound and Link's shout from where they stand, and the tune if it was a song and they are close.
void AbilityFx(uint16_t who, royale::ItemId item, bool self, float x, float z) {
    gLastAbility[who] = item;
    gLastAbilityAt[who] = ImGui::GetTime();
    if (!InGame()) return;
    const bool song = royale::IsSong(item) || item == royale::ItemId::FairyOcarina || item == royale::ItemId::OcarinaOfTime;
    Player* me = GET_PLAYER(gPlayState);
    const float d = std::hypot(x - me->actor.world.pos.x, z - me->actor.world.pos.z);
    if (song && (self || d < 900.0f)) PlaySongMelody(OcarinaSongOf(item));
    if (self) return;   // your own spell already made its sound and flash when you used it
    auto a = gActorOf.find(who);
    if (a == gActorOf.end() || a->second == nullptr) return;
    PowerFx(gPlayState, item, a->second->world.pos, false, a->second, who);
    Player* p = (Player*)a->second;
    if (!song) PuppetSfx(&p->actor, AbilitySfx(item));
    if (const u16 v = AbilityVoice(item)) PuppetVoice(p, v);
}

// ---- the puppet itself ------------------------------------------------------------------------------------------------------------

// Each puppet's place in its current action, how fast it is really moving (to match its legs to it), and when it last made a sound.
struct PuppetMotion {
    uint8_t anim = 255;         // the action being shown
    AnimSeq seq;
    int step = 0;
    int combo = 0;
    royale::ItemId seqAbility = royale::ItemId::Count;
    float lastX = 0, lastZ = 0;
    bool havePos = false;
    float speed = 0;            // units per game frame, smoothed
    float prevFrame = 0;        // for footsteps
    int idleFrames = 0;         // standing about: now and then a look round
    float jumpT = -1;           // a bot's jump arc, 0..1 (the server only says which floor it is on, so the jump is drawn here)
    int floorCheck = 0;
    royale::ItemId heldWeapon = royale::ItemId::Count;
    int flinch = 0;
    // in a cart (PuppetRide): climbing on (0) or sat down (1), the gait shown, and where the saddle was (for climbing down)
    uint8_t rideStage = 0, rideGait = 0;
    royale::Seat rideSeat = royale::Seat::Driver;
    Vec3f rideAt = { 0, 0, 0 };
    s16 rideYaw = 0;
    int dismount = 0;
};
std::unordered_map<const Actor*, PuppetMotion> gMotion;
// Carts (the section "carts: the Lon Lon Buggy" below).
bool PuppetRide(PlayState* play, Player* player, PuppetMotion& m, uint16_t id);
bool CartInput(Input& in, const royale::HudState& hud);
std::string CartPromptText();
void DrawCartHud(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale);
void DrawCartsOnMap(ImDrawList* dl, float scale, const std::function<ImVec2(float, float)>& toMap, const std::function<bool(ImVec2)>& inside);
bool CartTargetAt(uint16_t id, float* x, float* z);

void StartStep(PlayState* play, Player* player, PuppetMotion& m, int step, float morph) {
    m.step = step;
    const AnimStep& st = m.seq.step[step];
    LinkAnimation_Change(play, &player->skelAnime, st.anim, 1.0f, 0.0f, Animation_GetLastFrame(st.anim), st.loop ? ANIMMODE_LOOP : ANIMMODE_ONCE, morph);
    m.prevFrame = 0;
}
void StartSeq(PlayState* play, Player* player, PuppetMotion& m, const AnimSeq& seq, float morph) {
    m.seq = seq;
    if (m.seq.count == 0) m.seq = AnimSeq(RA(normal_wait), true);
    StartStep(play, player, m, 0, morph);
}

// Which ground the puppet stands on, for its footsteps (checked a few times a second).
void UpdatePuppetFloor(PlayState* play, Player* player, PuppetMotion& m) {
    if (m.floorCheck-- > 0) return;
    m.floorCheck = 8;
    CollisionPoly poly;
    s32 bgId = BGCHECK_SCENE;
    Vec3f pos = { player->actor.world.pos.x, player->actor.world.pos.y + 40.0f, player->actor.world.pos.z };
    const float y = BgCheck_AnyRaycastFloor2(&play->colCtx, &poly, &bgId, &pos);
    player->floorSfxOffset = y > BGCHECK_Y_MIN + 1.0f ? SurfaceType_GetSfx(&play->colCtx, &poly, bgId) : 0;
}


void Puppet_Init(Actor* actor, PlayState* play) {
    Player* player = (Player*)actor;
    // Modeled on upstream Shipwright's Anchor DummyPlayer_Init (itself modeled on EnTorch2_Init and Player_Init).
    actor->room = -1;
    player->itemAction = player->heldItemAction = -1;
    player->heldItemId = ITEM_NONE;
    Player_UseItem(play, player, ITEM_NONE);
    Player_SetModelGroup(player, Player_ActionToModelGroup(player, player->heldItemAction));
    play->playerInit(player, play, gPlayerSkelHeaders[gSaveContext.linkAge]);

    // A puppet never swings a weapon, so don't let it hold one of the limited weapon-trail effect slots.
    Effect_Delete(play, player->meleeWeaponEffectIndex);
    player->meleeWeaponEffectIndex = TOTAL_EFFECT_COUNT;

    play->func_11D54(player, play);

    // Other players can be Z-targeted like an enemy, which is how duels work.
    actor->flags |= ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE;

}

// A small glow in the weapon's rarity colour around the hand that holds it: glints at the hand plus a faint halo, more of them the rarer it is,
// so you can size up what another player carries at a glance. Switch it off (or on for your own weapon) in the Royale menu.
bool HeldGlowOn(bool self);
void HeldGlow(PlayState* play, Player* player, royale::Rarity rarity, bool self) {
    if (!HeldGlowOn(self)) return;
    static const int kEvery[] = { 6, 5, 4, 3, 2 };
    if (play->gameplayFrames % kEvery[static_cast<int>(rarity)] != 0) return;
    const Vec3f& hand = player->bodyPartsPos[PLAYER_BODYPART_R_HAND];
    Vec3f pos = hand;
    pos.x += (Rand_ZeroOne() - 0.5f) * 14.0f;
    pos.y += (Rand_ZeroOne() - 0.5f) * 14.0f + 6.0f;
    pos.z += (Rand_ZeroOne() - 0.5f) * 14.0f;
    Vec3f vel = { 0.0f, 0.3f, 0.0f };
    Vec3f accel = { 0.0f, 0.0f, 0.0f };
    Color_RGBA8 prim = RarityColor(rarity);
    Color_RGBA8 env = prim;
    EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 40, 10);
}

bool HangingFromGlider(const royale::PuppetState* st, const Actor* actor, PlayState* play) {
    return st != nullptr && st->alive && (gStateNow == royale::MatchState::Countdown || gStateNow == royale::MatchState::Drop) &&
           actor->world.pos.y - GroundY(play, actor->world.pos.x, actor->world.pos.z, actor->world.pos.y - 1000.0f) > 120.0f;
}

// What is in someone's hand right now, as the game shows it: their weapon, except while they use something else the way Link does - the
// bottle while they drink, the ocarina while they play a song, the hookshot while they fire one. A thrown boomerang has left the hand.
Look ActionLook(uint8_t anim, royale::ItemId weapon, royale::ItemId ability, double abilityAge, royale::ItemId bottle) {
    using royale::Anim;
    using royale::ItemId;
    switch (static_cast<Anim>(anim)) {
        case Anim::Drink: return BottleLook(bottle);
        case Anim::Play:
            if (weapon == ItemId::FairyOcarina || ability == ItemId::FairyOcarina) return LookFor(ItemId::FairyOcarina);
            return LookFor(ItemId::OcarinaOfTime);
        case Anim::Throw:
            if ((ability == ItemId::Hookshot || ability == ItemId::Longshot) && abilityAge < 1.2) return LookFor(ability);
            if (weapon == ItemId::Boomerang) return LookFor(ItemId::Count);
            break;
        default: break;
    }
    return LookFor(weapon);
}
Look PuppetLook(const royale::PuppetState& s) {
    const auto ab = gLastAbility.find(s.id);
    const auto at = gLastAbilityAt.find(s.id);
    const royale::ItemId ability = ab != gLastAbility.end() ? ab->second : royale::ItemId::Count;
    const double age = at != gLastAbilityAt.end() ? ImGui::GetTime() - at->second : 1e9;
    return ActionLook(s.anim, s.weapon, ability, age, royale::ItemId::RedPotion);   // which potion others drink isn't sent: the red one
}

void Puppet_Update(Actor* actor, PlayState* play) {
    Feat("other players: update");
    Player* player = (Player*)actor;
    auto idIt = gPuppetOf.find(actor);
    auto stIt = idIt == gPuppetOf.end() ? gState.end() : gState.find(idIt->second);
    if (stIt == gState.end()) {
        Actor_Kill(actor);
        return;
    }
    const royale::PuppetState& s = stIt->second;

    actor->world.pos.x = s.x;
    actor->world.pos.z = s.z;
    // Bots are simulated in x and z with a height above the floor (BotY). Humans report their own height.
    actor->world.pos.y = s.isBot ? BotY(play, s.x, s.z, s.y) : s.y;
    actor->shape.rot.y = s.rot;
    actor->world.rot.y = s.rot;
    actor->shape.shadowAlpha = 255;
    actor->focus.pos = actor->world.pos;
    actor->focus.pos.y += 50.0f;

    player->currentTunic = PLAYER_TUNIC_KOKIRI; // the colour comes from the player's skin, not from whatever the local save wears

    {   // Adult Power: drawn bigger (and back to size afterwards), easing between the two
        const float target = 0.01f * (s.adult ? royale::kAdultScale : 1.0f);
        const float k = actor->scale.x + (target - actor->scale.x) * 0.15f;
        actor->scale.x = actor->scale.y = actor->scale.z = k;
    }

    // Hold what the server says this player holds, and wear their shield, boots and mask.
    player->currentBoots = PlayerBootsFor(s.boots);
    player->currentMask = PlayerMaskFor(s.mask);
    const s8 shield = PlayerShieldFor(s.shield);
    Look look = PuppetLook(s);
    if (player->modelGroup != look.modelGroup || player->heldItemAction != look.itemAction || player->currentShield != shield) {
        u8 original = gSaveContext.equips.buttonItems[0];
        gSaveContext.equips.buttonItems[0] = look.buttonItem;
        player->currentShield = shield;
        player->itemAction = player->heldItemAction = look.itemAction;
        Player_SetModelGroup(player, look.modelGroup);
        gSaveContext.equips.buttonItems[0] = original;
    }

    if (s.alive) HeldGlow(play, player, s.weaponRarity, false);

    // The nameplate shows the player's name, hearts left and weapon (colour coded by rarity), so you can size up everyone you see.
    {
        const int hearts = static_cast<int>(std::ceil(std::max(0.0f, s.health)));
        const std::string plate = s.name + "  " + std::to_string(hearts) + " hearts  " + ItemLabel(s.weapon, s.weaponRarity);
        std::string& shown = gPlate[actor];
        if (shown != plate) {
            NameTag_RemoveAllForActor(actor);
            NameTag_RegisterForActorWithOptions(actor, plate.c_str(), NameTagOptions{ "royale-puppet", 0, RarityColor(s.weaponRarity) });
            shown = plate;
        }
    }

    PuppetMotion& m = gMotion[actor];
    {   // how fast they really move (snapshots arrive in steps, so smoothed): the legs are matched to it
        if (m.havePos) {
            const float moved = std::hypot(s.x - m.lastX, s.z - m.lastZ);
            if (moved < 80.0f) m.speed = m.speed * 0.7f + moved * 0.3f;
        }
        m.lastX = s.x; m.lastZ = s.z; m.havePos = true;
    }
    UpdatePuppetFloor(play, player, m);
    if (m.heldWeapon != s.weapon) {   // a weapon swapped in: the game's draw sound
        if (m.heldWeapon != royale::ItemId::Count && s.alive) PuppetSfx(actor, NA_SE_PL_CHANGE_ARMS);
        m.heldWeapon = s.weapon;
    }

    {   // somebody has just loosed an arrow or thrown something: show it in flight
        static std::unordered_map<uint16_t, uint8_t> previous;
        uint8_t& before = previous[s.id];
        if (s.anim != before && s.alive && (s.anim == static_cast<uint8_t>(royale::Anim::Shoot) || s.anim == static_cast<uint8_t>(royale::Anim::Throw)))
            SpawnProjectileFrom(s.weapon, s.x, actor->world.pos.y + 45.0f, s.z, s.rot);
        before = s.anim;
    }
    if (s.alive && PuppetRide(play, player, m, s.id)) return;   // sat in a cart (or climbing down from one)
    if (HangingFromGlider(&s, actor, play)) {   // both hands up on the glider's bar (the game's ledge-hang pose)
        if (m.anim != 254) { StartSeq(play, player, m, AnimSeq(RA(normal_jump_climb_wait), true), -6.0f); m.anim = 254; }
        LinkAnimation_Update(play, &player->skelAnime);
        Vec3f ignored;
        SkelAnime_UpdateTranslation(&player->skelAnime, &ignored, actor->shape.rot.y);
        return;
    }

    // What to show. Mostly the player's own state; but when they are seen landing a blow (or taking one) and their state doesn't say so
    // yet, the swing (or the flinch) is shown anyway, so a fight always reads as one.
    using royale::Anim;
    uint8_t want = s.anim;
    bool forced = false;
    {
        auto sw = gSwingFrames.find(s.id);
        auto fl = gFlinchFrames.find(s.id);
        const bool busy = royale::IsStrike(s.anim) || royale::IsDodge(s.anim) || OneShotAnim(s.anim);
        if (sw != gSwingFrames.end() && sw->second > 0) {
            if (!busy) { want = static_cast<uint8_t>(Anim::Attack); forced = sw->second == 10; }
            sw->second--;
        } else if (fl != gFlinchFrames.end() && fl->second > 0) {
            if (!busy && s.alive) { want = static_cast<uint8_t>(Anim::Hurt); forced = fl->second == 8; }
            fl->second--;
        }
    }
    const auto ab = gLastAbility.find(s.id);
    royale::ItemId ability = ab != gLastAbility.end() ? ab->second : royale::ItemId::NayrusLove;
    {   // a throw is the hookshot only if they have just used one; otherwise it is whatever is in their hand
        auto at = gLastAbilityAt.find(s.id);
        if (want == static_cast<uint8_t>(royale::Anim::Throw) && (at == gLastAbilityAt.end() || ImGui::GetTime() - at->second > 1.2)) ability = royale::ItemId::Count;
    }
    // A spell's pose depends on which spell: if word of it arrives just after the pose began, start again with the right one.
    const bool respell = (want == static_cast<uint8_t>(Anim::Cast) || want == static_cast<uint8_t>(Anim::Throw)) && m.anim == want && m.seqAbility != ability && m.step == 0 &&
                         player->skelAnime.curFrame < 6.0f;
    // Standing about for a while: a look round, a stretch, as Link does when you leave the stick alone.
    bool fidget = false;
    if (want == static_cast<uint8_t>(Anim::Idle)) {
        if (++m.idleFrames > 110 + static_cast<int>(s.id % 7) * 20 && s.health > 1.0f) { fidget = true; m.idleFrames = 0; }
    } else {
        m.idleFrames = 0;
    }
    if (want != m.anim || forced || respell || fidget) {
        const bool strike = royale::IsStrike(want);
        if (strike && !respell) m.combo++;
        AnimSeq seq = SeqFor(want, s.weapon, m.combo, ability, player);
        if (fidget) {   // the game's own idle fidgets: a look round, a stretch, a practice swing, tugging the tunic, tapping a foot, the shield
            const Grip grip = GripOf(s.weapon);
            LinkAnimationHeader* const looks[6] = { RA(normal_wait_typeA_20f), RA(wait_typeD_20f), grip == Grip::TwoHand || grip == Grip::Hammer ? RA(wait_itemD2_20f) : RA(wait_itemD1_20f),
                                                    RA(wait_itemA_20f), RA(wait_itemB_20f), RA(wait_itemC_20f) };
            seq = AnimSeq(looks[(play->gameplayFrames / 7 + s.id) % 6], false).Add(RA(normal_wait), true);
        }
        if (want == static_cast<uint8_t>(Anim::Idle) && s.alive && s.health <= 1.0f) seq = AnimSeq(RA(wait_heat1_20f), false).Add(RA(wait_heat2_20f), true);   // nearly dead: doubled over, panting
        // Moving between walking, running and standing blends over a few frames; an action snaps in quickly, as the game does.
        const float morph = OneShotAnim(want) ? -3.0f : -6.0f;
        StartSeq(play, player, m, seq, morph);
        m.seqAbility = ability;
        if (!respell && !fidget && s.alive) ActionSounds(player, want, s.weapon, m.combo);
        m.jumpT = -1.0f;
        if (s.isBot && (want == static_cast<uint8_t>(Anim::Jump) || want == static_cast<uint8_t>(Anim::JumpSlash) || (royale::IsDodge(want) && want != static_cast<uint8_t>(Anim::Roll)))) m.jumpT = 0.0f;
        m.anim = want;
    }

    // Walking and running at the speed they really go, so the feet don't slide.
    const bool sprinting = want == static_cast<uint8_t>(Anim::Sprint);
    const bool walking = want == static_cast<uint8_t>(Anim::Walk) || want == static_cast<uint8_t>(Anim::Run) || want == static_cast<uint8_t>(Anim::SideL) ||
                         want == static_cast<uint8_t>(Anim::SideR) || want == static_cast<uint8_t>(Anim::Back) || sprinting;
    if (sprinting) {   // legs pump faster and dust kicks up, like the local player's sprint
        player->skelAnime.playSpeed = std::max(kSprintAnimSpeed, std::min(2.0f, m.speed / 5.5f));
        if (actor->bgCheckFlags & 1) SprintDust(play, actor, play->gameplayFrames);
    } else if (walking) {
        const float stride = want == static_cast<uint8_t>(Anim::Run) ? 5.5f : want == static_cast<uint8_t>(Anim::Walk) ? 2.4f : 3.0f;   // units per frame at normal speed
        player->skelAnime.playSpeed = std::clamp(m.speed / stride, 0.55f, 1.8f);
    }
    const bool finished = LinkAnimation_Update(play, &player->skelAnime);
    if (finished && !m.seq.step[m.step].loop && m.step + 1 < m.seq.count) {
        StartStep(play, player, m, m.step + 1, 0.0f);
        // Feet back on the ground after a hop, a flip or a jump; the jump slash coming down.
        if (s.alive && (royale::IsDodge(m.anim) || m.anim == static_cast<uint8_t>(Anim::Jump)) && m.anim != static_cast<uint8_t>(Anim::Roll) && m.step == 1)
            PuppetSfx(actor, PuppetFloorSfx(player, NA_SE_PL_LAND));
        if (s.alive && m.anim == static_cast<uint8_t>(Anim::JumpSlash) && m.step == 1) PuppetSfx(actor, NA_SE_IT_SWORD_SWING_HARD);
        if (s.alive && m.anim == static_cast<uint8_t>(Anim::Drink) && m.step == 1) PuppetVoice(player, NA_SE_VO_LI_DRINK);
    }
    // Footsteps: twice a stride, where the game puts them, sounding like the ground underfoot.
    if (walking && m.jumpT < 0.0f) {
        const float cycle = static_cast<float>(Animation_GetLastFrame(m.seq.step[m.step].anim)) + 1.0f;
        const float cur = player->skelAnime.curFrame;
        for (float at : { cycle * 10.0f / 29.0f, cycle * 24.0f / 29.0f }) {
            const bool crossed = cur >= m.prevFrame ? (m.prevFrame < at && cur >= at) : (at > m.prevFrame || at <= cur);
            if (crossed) PuppetStep(player, m.speed);
        }
        m.prevFrame = cur;
    }
    // Bots' jumps, hops and jump slashes are lifted into the air here (the server only moves them between floors).
    if (m.jumpT >= 0.0f) {
        const bool big = m.anim == static_cast<uint8_t>(Anim::Jump);
        const float seconds = big ? 0.55f : 0.4f, height = big ? 48.0f : m.anim == static_cast<uint8_t>(Anim::JumpSlash) ? 30.0f : 22.0f;
        m.jumpT += 1.0f / (seconds * royale::kTickHz);
        if (m.jumpT >= 1.0f) {
            m.jumpT = -1.0f;
            if (big && s.alive) PuppetSfx(actor, PuppetFloorSfx(player, NA_SE_PL_LAND));
        } else {
            actor->world.pos.y += 4.0f * height * m.jumpT * (1.0f - m.jumpT);
        }
    }
    if (s.anim == static_cast<uint8_t>(royale::Anim::Emote5)) {
        const float t = static_cast<float>(play->gameplayFrames) / royale::kTickHz;
        ApplyChickenDance(player, t);
        actor->world.pos.y += ChickenDanceBob(t);
    }
    // Cancel the animation's own root motion: the network decides where the puppet is.
    Vec3f ignored;
    SkelAnime_UpdateTranslation(&player->skelAnime, &ignored, actor->shape.rot.y);
}

// ---- character skins (tunic colour) -----------------------------------------------------------------------------------------

// The game colours Link's tunic from its cosmetic settings, so a player's skin is applied by setting those just for the moment their
// model is drawn, then putting the local player's own colour back. All three tunic colours are set so it doesn't matter which one is worn.
uint32_t gLocalTunic = royale::SkinRgb(0);
bool gTunicApplied = false;                      // we have taken over the cosmetic tunic settings
int gSavedTunicChanged[3] = {0, 0, 0};
Color_RGB8 gSavedTunicValue[3] = {};
const char* const kTunicChanged[3] = { CVAR_COSMETIC("Link.KokiriTunic.Changed"), CVAR_COSMETIC("Link.GoronTunic.Changed"), CVAR_COSMETIC("Link.ZoraTunic.Changed") };
const char* const kTunicValue[3] = { CVAR_COSMETIC("Link.KokiriTunic.Value"), CVAR_COSMETIC("Link.GoronTunic.Value"), CVAR_COSMETIC("Link.ZoraTunic.Value") };

void SetTunicCosmetics(uint32_t rgb) {
    const Color_RGB8 c = { royale::RgbR(rgb), royale::RgbG(rgb), royale::RgbB(rgb) };
    for (int i = 0; i < 3; i++) {
        CVarSetInteger(kTunicChanged[i], 1);
        CVarSetColor24(kTunicValue[i], c);
    }
}

// While you are in a match your own Link wears your chosen skin; leaving gives the player's own cosmetic settings back.
void ApplyLocalTunic(bool on) {
    if (on && !gTunicApplied) {
        for (int i = 0; i < 3; i++) {
            gSavedTunicChanged[i] = CVarGetInteger(kTunicChanged[i], 0);
            gSavedTunicValue[i] = CVarGetColor24(kTunicValue[i], Color_RGB8{ 30, 105, 27 });
        }
        gTunicApplied = true;
    }
    if (on) {
        SetTunicCosmetics(gLocalTunic);
    } else if (gTunicApplied) {
        for (int i = 0; i < 3; i++) {
            CVarSetInteger(kTunicChanged[i], gSavedTunicChanged[i]);
            CVarSetColor24(kTunicValue[i], gSavedTunicValue[i]);
        }
        gTunicApplied = false;
    }
}

void DrawGliderAt(PlayState* play, float x, float y, float z, s16 yaw, float roll, bool diving, uint32_t scheme, bool plain = false, const Player* hanger = nullptr); // with the other custom models, below

void Puppet_Draw(Actor* actor, PlayState* play) {
    Feat("other players: draw");
    // Player_Draw reads the local player's equipped item to pick the held model, so show the puppet's own.
    const royale::PuppetState* st = StateOf(actor);
    u8 original = gSaveContext.equips.buttonItems[0];
    gSaveContext.equips.buttonItems[0] = st ? PuppetLook(*st).buttonItem : ITEM_NONE;
    if (st && gTunicApplied) SetTunicCosmetics(st->tunic); // this player's own colour
    Player_Draw(actor, play);
    if (st && gTunicApplied) SetTunicCosmetics(gLocalTunic);
    gSaveContext.equips.buttonItems[0] = original;
    // Everyone who is still in the sky during the drop hangs from a glider.
    if (st && HangingFromGlider(st, actor, play)) {
        DrawGliderAt(play, actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, actor->shape.rot.y, 0.0f, false, st->id, st->isBot, (const Player*)actor);   // a bot's glider is the plain model: no cloth to simulate
    }
}

void ForgetCorpse(const Actor* actor); // defined with the other corpse code below

void Puppet_Destroy(Actor* actor, PlayState* play) {
    // The actor was spawned as ACTOR_PLAYER and re-labelled ACTOR_EN_OE2; restore the id so the actor database's
    // per-actor load counter is decremented for the right entry.
    actor->id = ACTOR_PLAYER;
    NameTag_RemoveAllForActor(actor);
    auto it = gPuppetOf.find(actor);
    if (it != gPuppetOf.end()) {
        auto a = gActorOf.find(it->second);
        if (a != gActorOf.end() && a->second == actor) gActorOf.erase(a);
        gPuppetOf.erase(it);
    }
    gPlaying.erase(actor);
    gMotion.erase(actor);
    gPlate.erase(actor);
    ForgetCorpse(actor);
}

void SpawnPuppet(const royale::PuppetState& s) {
    gSpawningPuppet = s.id;
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_PLAYER, s.x, s.y, s.z, 0, s.rot, 0, 0, false);
    gSpawningPuppet = 0;
    if (actor != nullptr) gActorOf[s.id] = actor;
}

// ---- falling limp: what is left of a player who was eliminated -------------------------------------------------------------------

// A ragdoll: the body is thrown by the blow, tumbles through the air, bounces, then rolls over and over along the ground (and down any slope)
// with its limbs flopping loose, until it comes to rest on its back or its front. It stays there for a couple of minutes, and anyone who walks
// into it shoves it along. It is a puppet actor that runs its own physics instead of following the network.
constexpr uint16_t kCorpseIdBase = 0xF000;
constexpr uint16_t kAllyIdBase = 0xE000;   // puppet ids from here up to the corpses are hireable allies (index = id - base)
constexpr float kCorpseSeconds = 150.0f;   // how long a body lies there
constexpr size_t kMaxCorpses = 16;         // more than this and the oldest one goes
constexpr float kBodyRadius = 8.0f;        // half the thickness of Link lying down: how far the body's middle is off the ground
// The loose joints of a body. The first nine are the original limbs (kept as they were); the rest are the extra points: spine, neck, pelvis,
// wrists and ankles. `parent` is the joint this one hangs from: it is dragged along when the parent swings and whips past it. `side` is -1 on
// Link's left, +1 on his right and in the middle; `limit` is how far (radians) it can bend; `reach` scales the angle added to the animation;
// `droop` is how much gravity pulls it when the body lies on its side; `stiff` is how hard it springs back.
struct RagJoint { int limb; int parent; float side; float reach; float limit; float droop; float stiff; };
constexpr int kOriginalJoints = 9;
constexpr int kJoints = 17;
const RagJoint kRagJoints[kJoints] = {
    { PLAYER_LIMB_HEAD,        10,  1.0f, 0.50f, 1.2f, 0.0f, 28.0f },
    { PLAYER_LIMB_L_SHOULDER,   9, -1.0f, 1.00f, 1.2f, 0.7f, 28.0f },
    { PLAYER_LIMB_R_SHOULDER,   9,  1.0f, 1.00f, 1.2f, 0.7f, 28.0f },
    { PLAYER_LIMB_L_FOREARM,    1, -1.0f, 0.90f, 1.2f, 0.7f, 28.0f },
    { PLAYER_LIMB_R_FOREARM,    2,  1.0f, 0.90f, 1.2f, 0.7f, 28.0f },
    { PLAYER_LIMB_L_THIGH,     12, -1.0f, 0.60f, 1.2f, 0.7f, 28.0f },
    { PLAYER_LIMB_R_THIGH,     12,  1.0f, 0.60f, 1.2f, 0.7f, 28.0f },
    { PLAYER_LIMB_L_SHIN,       5, -1.0f, 0.70f, 1.2f, 0.7f, 28.0f },
    { PLAYER_LIMB_R_SHIN,       6,  1.0f, 0.70f, 1.2f, 0.7f, 28.0f },
    { PLAYER_LIMB_UPPER,       11,  1.0f, 0.25f, 0.9f, 0.3f, 40.0f },   // chest
    { PLAYER_LIMB_COLLAR,       9,  1.0f, 0.30f, 0.9f, 0.3f, 34.0f },   // neck
    { PLAYER_LIMB_LOWER,       12,  1.0f, 0.20f, 0.8f, 0.2f, 44.0f },   // lower spine
    { PLAYER_LIMB_WAIST,       -1,  1.0f, 0.18f, 0.7f, 0.2f, 48.0f },   // pelvis
    { PLAYER_LIMB_L_HAND,       3, -1.0f, 0.80f, 1.2f, 0.7f, 22.0f },   // wrists
    { PLAYER_LIMB_R_HAND,       4,  1.0f, 0.80f, 1.2f, 0.7f, 22.0f },
    { PLAYER_LIMB_L_FOOT,       7, -1.0f, 0.60f, 1.0f, 0.7f, 22.0f },   // ankles
    { PLAYER_LIMB_R_FOOT,       8,  1.0f, 0.60f, 1.0f, 0.7f, 22.0f },
};
struct Corpse {
    Actor* actor = nullptr;
    royale::Vec2 vel = {};       // horizontal speed, units per second
    float vy = 0;                // vertical speed
    float spin = 0;              // radians per second around the vertical axis
    float roll = 0, rollVel = 0; // rolling over around the body's long axis (radians, radians per second)
    float age = 0;
    royale::ItemId weapon = royale::ItemId::DekuStick;
    uint32_t tunic = royale::SkinRgb(0);
    bool animStarted = false;
    float pitch = 0, pitchVel = 0;        // tumbling head over heels in the air (radians)
    royale::Vec2 lastVel = {};
    float lastVy = 0, lastRollVel = 0;
    float limb[kJoints][2] = {}, limbVel[kJoints][2] = {};   // loose joints: two swing angles each (radians)
    int bounces = 0;
    float still = 0;             // seconds it has lain still; a body at rest skips the ground checks
    bool dying = false;          // asked the game to remove it
    bool test = false;           // a lobby test ragdoll: never ages out, can be hit and grabbed
    bool held = false;           // being carried (the grab button is held)
    float hitCooldown = 0;
    bool pinned = false;         // an emote double: stands where the local player is and plays an emote, instead of falling
    int emote = 0;
};
std::unordered_map<uint16_t, Corpse> gCorpses;       // corpse id -> body
std::unordered_map<const Actor*, uint16_t> gCorpseOf;
uint16_t gNextCorpse = kCorpseIdBase;
std::unordered_map<uint16_t, royale::PuppetState> gLastSeen; // the last state of each living puppet, to see who just died
std::unordered_map<uint16_t, double> gFellAt;                // player id -> when their body was made, so one elimination makes one body

void ForgetCorpse(const Actor* actor) {
    auto corpse = gCorpseOf.find(actor);
    if (corpse != gCorpseOf.end()) { gCorpses.erase(corpse->second); gCorpseOf.erase(corpse); }
}

float WrapAngle(float a) {
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}

void Corpse_Update(Actor* actor, PlayState* play) {
    Feat("corpses: update");
    auto of = gCorpseOf.find(actor);
    if (of == gCorpseOf.end()) { Actor_Kill(actor); return; }
    Corpse& c = gCorpses[of->second];
    Player* player = reinterpret_cast<Player*>(actor);
    if (c.pinned) { // the emote double
        Player* local = GET_PLAYER(play);
        actor->world.pos = local->actor.world.pos;
        actor->shape.rot.y = local->actor.shape.rot.y;
        actor->world.rot.y = actor->shape.rot.y;
        actor->shape.shadowAlpha = 255;
        if (!c.animStarted) {
            LinkAnimation_PlayLoop(play, &player->skelAnime, AnimFor(royale::EmoteAnim(c.emote)));
            c.animStarted = true;
        }
        LinkAnimation_Update(play, &player->skelAnime);
        if (c.emote == royale::kChickenDanceEmote) {
            const float t = static_cast<float>(play->gameplayFrames) / royale::kTickHz;
            ApplyChickenDance(player, t);
            actor->world.pos.y += ChickenDanceBob(t);
        }
        Vec3f ignored;
        SkelAnime_UpdateTranslation(&player->skelAnime, &ignored, actor->shape.rot.y);
        return;
    }
    const float dt = 1.0f / royale::kTickHz;
    if (!c.test) c.age += dt;
    if (c.age > kCorpseSeconds) { c.dying = true; Actor_Kill(actor); return; }

    // Walking into a body shoves it (and sets it rolling). Not while invisible: a spectator stands on their own body.
    {
        Player* local = GET_PLAYER(play);
        const float dx = actor->world.pos.x - local->actor.world.pos.x, dz = actor->world.pos.z - local->actor.world.pos.z;
        const float d = std::hypot(dx, dz);
        if (!(local->stateFlags2 & PLAYER_STATE2_DISABLE_DRAW) && d < 30.0f && d > 0.01f && std::fabs(actor->world.pos.y - local->actor.world.pos.y) < 40.0f &&
            local->actor.speedXZ > 1.0f) {
            const float push = (30.0f - d) * 6.0f + local->actor.speedXZ * 12.0f;
            c.vel.x += dx / d * push * dt * 8.0f;
            c.vel.z += dz / d * push * dt * 8.0f;
            c.still = 0;
        }
        if (c.test) {   // the lobby test ragdoll can also be hit with a sword and carried (hold L)
            Feat("ragdoll: test dummy");
            c.hitCooldown -= dt;
            if (local->meleeWeaponState != 0 && c.hitCooldown <= 0.0f && d < 75.0f && d > 0.01f) {
                c.vel.x += dx / d * 320.0f; c.vel.z += dz / d * 320.0f;
                c.vy = std::max(c.vy, 260.0f);
                c.pitchVel += -4.0f; c.rollVel += (dx > 0 ? 3.0f : -3.0f);
                c.hitCooldown = 0.4f; c.bounces = 0; c.still = 0;
                for (auto& l : c.limbVel) { l[0] += (Rand_ZeroOne() - 0.5f) * 8.0f; l[1] += (Rand_ZeroOne() - 0.5f) * 8.0f; }
            }
            const bool grabbing = (gPlayState->state.input[0].cur.button & BTN_L) != 0 && d < (c.held ? 220.0f : 140.0f);
            c.held = grabbing;
            if (grabbing) {   // pulled to a spot in front of the player, hanging loose
                const float fy = local->actor.shape.rot.y * (3.14159265f / 32768.0f);
                const float tx = local->actor.world.pos.x + std::sin(fy) * 45.0f, tz = local->actor.world.pos.z + std::cos(fy) * 45.0f;
                const float ty = local->actor.world.pos.y + 35.0f;
                const float k = std::min(1.0f, 10.0f * dt);
                c.vel.x += ((tx - actor->world.pos.x) * 8.0f - c.vel.x) * k;
                c.vel.z += ((tz - actor->world.pos.z) * 8.0f - c.vel.z) * k;
                c.vy += ((ty - actor->world.pos.y) * 8.0f - c.vy) * k + 980.0f * dt;   // gravity is taken off again below
                c.pitchVel *= 0.9f; c.rollVel *= 0.9f; c.bounces = 0; c.still = 0;
            }
        }
    }

    const float yaw = actor->shape.rot.y * (3.14159265f / 32768.0f);
    const royale::Vec2 side = { std::cos(yaw), -std::sin(yaw) };   // the body's own left, which is the way it rolls
    const royale::Vec2 fwd = { std::sin(yaw), std::cos(yaw) };     // along the body, head to feet
    const bool resting = c.still > 2.0f;
    bool onGround = false;
    if (!resting) {
        // Gravity and a little air drag in the air; bounces that lose most of the energy; on the ground it rolls freely sideways
        // (rolling barely slows it) but drags along its length, and rolls down slopes.
        c.vy -= 980.0f * dt;
        c.vel.x *= 1.0f - 0.35f * dt; c.vel.z *= 1.0f - 0.35f * dt;
        actor->world.pos.x += c.vel.x * dt;
        actor->world.pos.z += c.vel.z * dt;
        actor->world.pos.y += c.vy * dt;
        const float ground = GroundY(play, actor->world.pos.x, actor->world.pos.z, actor->world.pos.y - 1.0f);
        const float lift = kBodyRadius * (1.0f - std::cos(c.roll));   // rolled onto its side or front, the middle of the body is higher
        if (actor->world.pos.y <= ground + lift) {
            actor->world.pos.y = ground + lift;
            onGround = true;
            if (c.vy < -120.0f && c.bounces < 4) {   // a bounce: it keeps a third of its height, the tumble changes, and the limbs fling
                c.vy = -c.vy * 0.34f;
                c.bounces++;
                c.vel.x *= 0.72f; c.vel.z *= 0.72f;
                c.pitchVel *= -0.45f;
                const float kick = (c.vel.x * side.x + c.vel.z * side.z) >= 0.0f ? 1.0f : -1.0f;
                c.vel.x += side.x * kick * 90.0f; c.vel.z += side.z * kick * 90.0f;   // landing knocks it over sideways
                for (auto& l : c.limbVel) { l[0] += (Rand_ZeroOne() - 0.5f) * 9.0f; l[1] += (Rand_ZeroOne() - 0.5f) * 9.0f; }
            } else {
                c.vy = 0;
                // Down the slope.
                const float gx = (GroundY(play, actor->world.pos.x + 10.0f, actor->world.pos.z, ground) - GroundY(play, actor->world.pos.x - 10.0f, actor->world.pos.z, ground)) / 20.0f;
                const float gz = (GroundY(play, actor->world.pos.x, actor->world.pos.z + 10.0f, ground) - GroundY(play, actor->world.pos.x, actor->world.pos.z - 10.0f, ground)) / 20.0f;
                if (std::hypot(gx, gz) > 0.08f && std::hypot(gx, gz) < 2.5f) {   // a real slope, not a wall or a ledge
                    c.vel.x -= gx * 520.0f * dt;
                    c.vel.z -= gz * 520.0f * dt;
                }
                float vs = c.vel.x * side.x + c.vel.z * side.z, vf = c.vel.x * fwd.x + c.vel.z * fwd.z;
                vs *= std::max(0.0f, 1.0f - (std::fabs(vs) > 40.0f ? 0.9f : 3.5f) * dt);   // rolling
                vf *= std::max(0.0f, 1.0f - (std::fabs(vf) > 60.0f ? 3.2f : 7.5f) * dt);   // sliding to a stop
                c.vel = { side.x * vs + fwd.x * vf, side.z * vs + fwd.z * vf };
                c.spin *= std::max(0.0f, 1.0f - 4.0f * dt);
                // Rolling without slipping: moving to its left turns it over that way.
                const float rolling = -vs / kBodyRadius;
                if (std::fabs(vs) > 12.0f) {
                    c.rollVel += (rolling - c.rollVel) * std::min(1.0f, 10.0f * dt);
                } else {   // nearly stopped: it flops down onto its back or its front, whichever is nearer
                    const float rest = std::round(c.roll / 3.14159265f) * 3.14159265f;
                    c.rollVel += (rest - c.roll) * 40.0f * dt;
                    c.rollVel *= std::max(0.0f, 1.0f - 6.0f * dt);
                }
            }
        }
        if (!onGround) c.rollVel *= 1.0f - 0.2f * dt;   // a sideways tumble keeps going through the air
        c.roll += c.rollVel * dt;
        if (c.roll > 6.2831853f || c.roll < -6.2831853f) c.roll = std::fmod(c.roll, 6.2831853f);

        // Head over heels while airborne; once down, the tumble eases out to lying flat.
        if (!onGround) { c.pitch += c.pitchVel * dt; }
        else { c.pitch = WrapAngle(c.pitch) * std::max(0.0f, 1.0f - 6.0f * dt); c.pitchVel *= std::max(0.0f, 1.0f - 6.0f * dt); }
        actor->shape.rot.y = static_cast<s16>(actor->shape.rot.y + static_cast<int>(c.spin * dt * (32768.0f / 3.14159265f)));
        actor->world.rot.y = actor->shape.rot.y;

        const bool moving = onGround ? (std::hypot(c.vel.x, c.vel.z) > 4.0f || std::fabs(c.rollVel) > 0.3f) : true;
        c.still = moving ? 0.0f : c.still + dt;
    }
    // The last few seconds it sinks out of sight.
    if (c.age > kCorpseSeconds - 3.0f) actor->world.pos.y -= 10.0f * dt;
    actor->shape.rot.x = static_cast<s16>(c.pitch * (32768.0f / 3.14159265f));
    actor->shape.rot.z = static_cast<s16>(c.roll * (32768.0f / 3.14159265f));
    actor->shape.shadowAlpha = 255;

    // Limp joints: they lag behind every change of speed and of roll (the blow, each bounce, each turn over), flop toward the ground when the body
    // lies on its side, and only weak springs pull them back. A joint is also dragged along by the one it hangs from, so a swinging shoulder whips
    // the forearm and the wrist after it. Without the Ragdoll debug switch only the original nine limbs move.
    const int joints = DebugOn(kDbgRagdoll) ? kJoints : kOriginalJoints;
    {
        Feat("ragdoll: joints");
        const float ax = (c.vel.x - c.lastVel.x) / dt, az = (c.vel.z - c.lastVel.z) / dt, ay = (c.vy - c.lastVy) / dt;
        const float ar = (c.rollVel - c.lastRollVel) / dt;
        c.lastVel = c.vel; c.lastVy = c.vy; c.lastRollVel = c.rollVel;
        const float kick = std::clamp((std::fabs(ax) + std::fabs(az) + std::fabs(ay) * 0.4f) * 0.0009f, 0.0f, 1.4f);
        const float droop = std::sin(c.roll) * 0.7f;   // gravity, sideways across the body
        float before[kJoints][2];                      // last frame's speeds, so the order of the joints does not matter
        for (int i = 0; i < joints; i++) { before[i][0] = c.limbVel[i][0]; before[i][1] = c.limbVel[i][1]; }
        for (int i = 0; i < joints; i++) {
            const RagJoint& jt = kRagJoints[i];
            const bool original = i < kOriginalJoints;
            for (int a = 0; a < 2; a++) {
                const float push = (a == 0 ? ay * 0.00032f : (ax * 0.0003f + az * 0.0003f - ar * 0.004f)) * jt.side + kick * (Rand_ZeroOne() - 0.5f) * 0.5f;
                c.limbVel[i][a] += push;
                if (!onGround && !resting) c.limbVel[i][a] += std::sin(c.age * (7.0f + i) + a) * 0.9f * dt * 20.0f;   // flailing through the air
                if (!original && jt.parent >= 0 && jt.parent < joints) c.limbVel[i][a] += before[jt.parent][a] * 5.0f * dt;
                const float target = a == 1 ? droop * (jt.droop / 0.7f) : 0.0f;
                c.limbVel[i][a] += (target - c.limb[i][a]) * jt.stiff * dt;
                c.limbVel[i][a] *= std::max(0.0f, 1.0f - 4.0f * dt);
                const float next = c.limb[i][a] + c.limbVel[i][a] * dt;
                if (std::fabs(next) > jt.limit) c.limbVel[i][a] *= -0.3f;   // hit the end of its range: it bounces back a little
                c.limb[i][a] = std::clamp(next, -jt.limit, jt.limit);
            }
        }
    }

    if (!c.animStarted) {
        LinkAnimation_PlayOnce(play, &player->skelAnime, (LinkAnimationHeader*)&gPlayerAnim_link_normal_back_downA); // knocked flat on the back
        c.animStarted = true;
    }
    LinkAnimation_Update(play, &player->skelAnime);
    {   // the loose joints, on top of the knocked-down pose
        Vec3s* j = player->skelAnime.jointTable;
        const float bin = 32768.0f / 3.14159265f;
        for (int i = 0; i < joints; i++) {
            const RagJoint& jt = kRagJoints[i];
            j[jt.limb].x = static_cast<s16>(j[jt.limb].x + c.limb[i][0] * jt.reach * bin);
            j[jt.limb].z = static_cast<s16>(j[jt.limb].z + c.limb[i][1] * jt.reach * bin);
        }
    }
    Vec3f ignored;
    SkelAnime_UpdateTranslation(&player->skelAnime, &ignored, actor->shape.rot.y);
}

void Corpse_Draw(Actor* actor, PlayState* play) {
    Feat("corpses: draw");
    auto of = gCorpseOf.find(actor);
    if (of == gCorpseOf.end()) return;
    const Corpse& c = gCorpses[of->second];
    const u8 original = gSaveContext.equips.buttonItems[0];
    gSaveContext.equips.buttonItems[0] = LookFor(c.weapon).buttonItem;
    if (gTunicApplied) SetTunicCosmetics(c.tunic);
    Player_Draw(actor, play);
    if (gTunicApplied) SetTunicCosmetics(gLocalTunic);
    gSaveContext.equips.buttonItems[0] = original;
}

// An emote: a copy of the local player's character stands where they are and plays the gesture, while the real one is hidden. Moving or attacking
// ends it. Returns the actor so the caller can remove it.
Actor* SpawnEmoteDouble(const royale::PuppetState& s, int emote) {
    if (gCorpses.size() >= kMaxCorpses + 2 || gPlayState == nullptr) return nullptr;
    const uint16_t id = gNextCorpse++;
    if (gNextCorpse < kCorpseIdBase) gNextCorpse = kCorpseIdBase;
    gSpawningPuppet = id;
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_PLAYER, s.x, s.y, s.z, 0, s.rot, 0, 0, false);
    gSpawningPuppet = 0;
    if (actor == nullptr) return nullptr;
    Corpse c;
    c.actor = actor;
    c.pinned = true;
    c.emote = emote;
    c.weapon = s.weapon;
    c.tunic = s.tunic;
    gCorpses[id] = c;
    gCorpseOf[actor] = id;
    return actor;
}

// The body of player `s`, thrown along (pushX, pushZ). Once per elimination: the elimination event and the puppet list can both report it.
void SpawnCorpse(const royale::PuppetState& s, float pushX, float pushZ) {
    if (gPlayState == nullptr) return;
    const double now = ImGui::GetTime();
    auto fell = gFellAt.find(s.id);
    if (fell != gFellAt.end() && now - fell->second < 10.0) return;
    gFellAt[s.id] = now;
    {   // room for one more: the oldest body goes
        size_t bodies = 0;
        Corpse* oldest = nullptr;
        for (auto& [cid, body] : gCorpses) {
            if (body.pinned || body.dying) continue;
            bodies++;
            if (oldest == nullptr || body.age > oldest->age) oldest = &body;
        }
        if (bodies >= kMaxCorpses && oldest != nullptr) { oldest->dying = true; Actor_Kill(oldest->actor); }
    }
    const uint16_t id = gNextCorpse++;
    if (gNextCorpse < kCorpseIdBase) gNextCorpse = kCorpseIdBase;
    gSpawningPuppet = id;
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_PLAYER, s.x, s.y, s.z, 0, s.rot, 0, 0, false);
    gSpawningPuppet = 0;
    if (actor == nullptr) return;
    actor->flags &= ~(ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE);   // a body can't be Z-targeted like a player
    Corpse c;
    c.actor = actor;
    const float len = std::max(0.001f, std::hypot(pushX, pushZ));
    const float yaw = s.rot * (3.14159265f / 32768.0f);
    const float sideways = (id & 1 ? 1.0f : -1.0f) * 110.0f;                   // and a little to one side, so it lands rolling
    c.vel = { pushX / len * 250.0f + std::cos(yaw) * sideways, pushZ / len * 250.0f - std::sin(yaw) * sideways };
    c.vy = 330.0f;
    c.pitchVel = -7.0f - (id % 3);                            // flips over backwards
    c.spin = (id & 1 ? 1.0f : -1.0f) * 2.0f;
    c.rollVel = -sideways / kBodyRadius * 0.5f;
    c.weapon = s.weapon;
    c.tunic = s.tunic;
    gCorpses[id] = c;
    gCorpseOf[actor] = id;
}

// ---- the lobby test ragdoll ---------------------------------------------------------------------------------------------------
// A dummy that looks like the local player, to try the ragdoll with while waiting: walk into it, hit it with the sword, or hold L to carry it.
// It never ages out and is not counted with the bodies of eliminated players.
constexpr size_t kMaxTestRagdolls = 3;

size_t CountTestRagdolls() {
    size_t n = 0;
    for (auto& [cid, body] : gCorpses) if (body.test && !body.dying) n++;
    return n;
}

void RemoveTestRagdolls() {
    for (auto& [cid, body] : gCorpses) if (body.test && !body.dying) { body.dying = true; Actor_Kill(body.actor); }
}

void FlingTestRagdolls() {
    for (auto& [cid, body] : gCorpses) {
        if (!body.test || body.dying) continue;
        body.vel = { (Rand_ZeroOne() - 0.5f) * 700.0f, (Rand_ZeroOne() - 0.5f) * 700.0f };
        body.vy = 420.0f + Rand_ZeroOne() * 200.0f;
        body.pitchVel = -9.0f + Rand_ZeroOne() * 4.0f;
        body.rollVel = (Rand_ZeroOne() - 0.5f) * 14.0f;
        body.spin = (Rand_ZeroOne() - 0.5f) * 8.0f;
        body.bounces = 0; body.still = 0;
        for (auto& l : body.limbVel) { l[0] += (Rand_ZeroOne() - 0.5f) * 12.0f; l[1] += (Rand_ZeroOne() - 0.5f) * 12.0f; }
    }
}

void SpawnTestRagdoll(const royale::HudState& hud) {
    Feat("ragdoll: spawn test dummy");
    if (gPlayState == nullptr || !InGame()) return;
    if (CountTestRagdolls() >= kMaxTestRagdolls) {   // the oldest one goes
        for (auto& [cid, body] : gCorpses) if (body.test && !body.dying) { body.dying = true; Actor_Kill(body.actor); break; }
    }
    const Player* self = GET_PLAYER(gPlayState);
    const float yaw = self->actor.shape.rot.y * (3.14159265f / 32768.0f);
    const uint16_t id = gNextCorpse++;
    if (gNextCorpse < kCorpseIdBase) gNextCorpse = kCorpseIdBase;
    const float x = self->actor.world.pos.x + std::sin(yaw) * 70.0f, z = self->actor.world.pos.z + std::cos(yaw) * 70.0f;
    gSpawningPuppet = id;
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_PLAYER, x, self->actor.world.pos.y + 60.0f, z, 0, self->actor.shape.rot.y + 0x8000, 0, 0, false);
    gSpawningPuppet = 0;
    if (actor == nullptr) return;
    actor->flags &= ~(ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE);
    Corpse c;
    c.actor = actor;
    c.test = true;
    c.weapon = hud.weapon;
    c.tunic = gLocalTunic;
    c.vy = 120.0f;
    c.pitchVel = -3.0f;
    gCorpses[id] = c;
    gCorpseOf[actor] = id;
}

// Make the world match the session: spawn missing puppets, drop ones that left, died, are out of range, or are in another scene.
void ReconcilePuppets(royale::MatchState state) {
    if (!gSession.Joined() || !InPlayableScene(state)) {
        for (auto& [id, actor] : gActorOf) Actor_Kill(actor);
        gState.clear();
        return;
    }
    std::vector<royale::PuppetState> desired = gSession.Puppets();
    std::unordered_map<uint16_t, bool> wanted;
    for (const auto& s : desired) {
        // Somebody who was standing a moment ago and is now eliminated falls over where they stood (pushed along the way they were facing back).
        auto last = gLastSeen.find(s.id);
        if (!s.alive && last != gLastSeen.end() && last->second.alive && s.scene == gPlayState->sceneNum) {
            const float a = last->second.rot * (3.14159265f / 32768.0f);
            SpawnCorpse(last->second, -std::sin(a), -std::cos(a));
        }
        gLastSeen[s.id] = s;
        if (!s.alive || s.scene != gPlayState->sceneNum) continue; // someone in the other room is not here with us
        wanted[s.id] = true;
        gState[s.id] = s;
        if (gActorOf.find(s.id) == gActorOf.end()) SpawnPuppet(s);
    }
    for (auto it = gActorOf.begin(); it != gActorOf.end();) {
        if (wanted.find(it->first) == wanted.end()) {
            Actor_Kill(it->second); // Puppet_Destroy erases the map entries on the next actor update
            gState.erase(it->first);
            it = gActorOf.erase(it);
        } else {
            ++it;
        }
    }
}

void KillAllEnemies() {
    if (gPlayState == nullptr) return;
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].head; a != nullptr; a = a->next) Actor_Kill(a);
}

// ---- loot on the ground ---------------------------------------------------------------------------------------------------

struct LootActor {
    Actor* actor = nullptr;
    float baseY = 0;
    int pickupCooldown = 0;
    bool chest = false;       // a treasure chest (opened with A) rather than an item on the ground
    bool opened = false;
    bool killing = false;     // asked the game to remove it; it goes when the game next updates actors
    bool big = false;         // Epic and Legendary chests are the big kind
    bool supply = false;      // from a supply drop
    bool special = false;     // a Heart Container chest: pink
    royale::Rarity rarity = royale::Rarity::Common;
    int gid = -1;             // the game's own model for this item (GetItemDrawID), or -1 to keep the stand-in's
    ActorFunc origUpdate = nullptr, origDestroy = nullptr;
};
std::unordered_map<size_t, LootActor> gLoot;        // loot index -> its actor
std::unordered_map<const Actor*, size_t> gLootOf;   // actor -> loot index
constexpr float kLootSpawnRadius = 1500.0f;          // only draw what is near you
constexpr size_t kMaxLootActors = 56;
constexpr float kLootPickupRange = 55.0f;            // the server allows 75 for items on the ground
constexpr float kChestOpenRange = 90.0f;             // and 100 for chests (they are solid, so you stop a little way off)
bool gSpawningLoot = false;                          // true while we spawn an item actor, so the "no stray item drops" rule lets it through

// ---- turning a world position into a spot on the screen ---------------------------------------------------------------------

bool WorldToScreen(float x, float y, float z, ImVec2* out) {
    if (gPlayState == nullptr) return false;
    Vec3f p = { x, y, z }, clip;
    f32 w = 0.0f;
    SkinMatrix_Vec3fMtxFMultXYZW(&gPlayState->viewProjectionMtxF, &p, &clip, &w);
    if (w <= 1.0f) return false; // behind the camera
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    out->x = (clip.x / w * 0.5f + 0.5f) * ds.x;
    out->y = (0.5f - clip.y / w * 0.5f) * ds.y;
    return true;
}

bool LiveAndAlive(const royale::HudState& h) {
    return gSession.Joined() && (h.state == royale::MatchState::Drop || h.state == royale::MatchState::InMatch) && h.haveSelf && h.selfAlive;
}

void Sparkle(PlayState* play, const Vec3f& at, royale::Rarity rarity); // below, with the chests
void DrawSign(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale);   // below, with the sign
void PlayOneShot(int kind);
void DrawGliderAim(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale); // below, with the skydive
void DrawLilo(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale);
void DrawFartReactions(ImDrawList* dl, ImFont* font, float scale);   // below, with Lilo
bool LiloNear();
void TalkToLilo();
void DrawMaya(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale);
bool MayaNear();
void TalkToMaya();
bool SignNear();
bool MessageBoxUp();
void PressedATalk();   // A next to Lilo, Maya or the sign: the game's own text box (see "talking")
void DrawAllyLabels(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const royale::HudState& h);   // below, with the allies
int NearbyFreeAlly();
void AllyActionFx(const royale::ClientEvent& e, const royale::HudState& h);

void Loot_Update(Actor* actor, PlayState* play) {
    auto idx = gLootOf.find(actor);
    if (idx == gLootOf.end()) {
        Actor_Kill(actor);
        return;
    }
    LootActor& la = gLoot[idx->second];
    // Spin and bob so it reads as something to grab.
    actor->shape.rot.y += 0x300;
    actor->world.pos.y = la.baseY + 22.0f + 5.0f * std::sin(static_cast<float>(play->gameplayFrames) * 0.1f);
    if (la.pickupCooldown > 0) la.pickupCooldown--;
    Sparkle(play, actor->world.pos, la.rarity);

    Player* player = GET_PLAYER(play);
    royale::HudState h = gSession.Hud();
    if (player == nullptr || la.pickupCooldown > 0 || !LiveAndAlive(h)) return;
    float dx = player->actor.world.pos.x - actor->world.pos.x, dz = player->actor.world.pos.z - actor->world.pos.z;
    float dy = player->actor.world.pos.y - la.baseY;
    if (dx * dx + dz * dz <= kLootPickupRange * kLootPickupRange && std::fabs(dy) < 100.0f) {
        gSession.RequestPickup(static_cast<uint32_t>(idx->second), false); // the server only takes upgrades this way
        la.pickupCooldown = royale::kTickHz; // ask again in a second if the server said no
    }
}

void Loot_Destroy(Actor* actor, PlayState* play) {
    NameTag_RemoveAllForActor(actor);
    auto idx = gLootOf.find(actor);
    if (idx != gLootOf.end()) {
        auto la = gLoot.find(idx->second);
        if (la != gLoot.end() && la->second.actor == actor) gLoot.erase(la);
        gLootOf.erase(idx);
    }
}

// ---- treasure chests ------------------------------------------------------------------------------------------------------

// The game's own chest actor, kept shut and inert until the server says it was opened, then handed back to the game to play its normal
// opening animation and sounds. (Treasure flags are scene-wide; ours is cleared again after every spawn so chests don't start open.)
constexpr int kChestFlag = 19;

void ClearChestFlag() {
    gPlayState->actorCtx.flags.chest &= ~(1u << kChestFlag);
}

// The game's own glint effect: a spark of the item's rarity colour drifting up, more often the rarer it is. Makes chests and dropped items
// catch the eye (this is the nearest thing to shiny materials the mod can do; there are no custom shaders).
void Sparkle(PlayState* play, const Vec3f& at, royale::Rarity rarity) {
    static const int kEvery[] = { 30, 20, 12, 8, 4 }; // frames between glints, Common to Legendary
    if (play->gameplayFrames % kEvery[static_cast<int>(rarity)] != 0) return;
    Vec3f pos = at;
    pos.x += (Rand_ZeroOne() - 0.5f) * 40.0f;
    pos.z += (Rand_ZeroOne() - 0.5f) * 40.0f;
    pos.y += 12.0f + Rand_ZeroOne() * 48.0f;
    Vec3f vel = { 0.0f, 0.6f + Rand_ZeroOne() * 0.6f, 0.0f };
    Vec3f accel = { 0.0f, 0.0f, 0.0f };
    Color_RGBA8 prim = RarityColor(rarity);
    Color_RGBA8 env = { 255, 255, 255, 255 };
    EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 90, 26);
}

void Chest_Update(Actor* actor, PlayState* play) {
    auto idx = gLootOf.find(actor);
    if (idx == gLootOf.end()) { Actor_Kill(actor); return; }
    LootActor& la = gLoot[idx->second];
    EnBox* box = reinterpret_cast<EnBox*>(actor);
    if (!la.opened) {
        box->alpha = 255; // the game's own update normally fades it in
        Sparkle(play, actor->world.pos, la.rarity);
        if (la.special && play->gameplayFrames % 3 == 0) {   // a Heart Container chest: pink hearts of light drifting up
            Vec3f pos = { actor->world.pos.x + (Rand_ZeroOne() - 0.5f) * 40.0f, actor->world.pos.y + 20.0f + Rand_ZeroOne() * 60.0f, actor->world.pos.z + (Rand_ZeroOne() - 0.5f) * 40.0f };
            Vec3f vel = { 0.0f, 0.9f + Rand_ZeroOne() * 0.6f, 0.0f }, accel = { 0.0f, 0.0f, 0.0f };
            Color_RGBA8 prim = { 255, 120, 190, 255 }, env = { 255, 40, 110, 255 };
            EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 110, 30);
        }
        if (la.supply) {   // a supply crate: a column of red and gold light
            for (int i = 0; i < 3; i++) {
                Vec3f pos = { actor->world.pos.x + (Rand_ZeroOne() - 0.5f) * 30.0f, actor->world.pos.y + Rand_ZeroOne() * 420.0f, actor->world.pos.z + (Rand_ZeroOne() - 0.5f) * 30.0f };
                Vec3f vel = { 0.0f, 1.0f, 0.0f }, accel = { 0.0f, 0.0f, 0.0f };
                Color_RGBA8 prim = { 255, static_cast<u8>(120 + Rand_ZeroOne() * 110), 40, 255 }, env = { 255, 40, 20, 255 };
                EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 40, 38);
            }
        }
        return;
    }
    la.origUpdate(actor, play);
}

void Chest_Destroy(Actor* actor, PlayState* play) {
    NameTag_RemoveAllForActor(actor);
    ActorFunc orig = nullptr;
    auto idx = gLootOf.find(actor);
    if (idx != gLootOf.end()) {
        auto la = gLoot.find(idx->second);
        if (la != gLoot.end()) { orig = la->second.origDestroy; if (la->second.actor == actor) gLoot.erase(la); }
        gLootOf.erase(idx);
    }
    if (orig) orig(actor, play); // removes the chest's collision
}

void OpenChest(LootActor& la) {
    if (la.opened) return;
    la.opened = true;
    EnBox* box = reinterpret_cast<EnBox*>(la.actor);
    box->unk_1F4 = la.big ? 1 : -1; // the game's "Link opened this" marker: 1 = the slow opening with golden light, -1 = quick
    NameTag_RemoveAllForActor(la.actor);
}

void SpawnChest(size_t index, const royale::net::LootNet& l, float groundY) {
    const royale::Rarity rarity = static_cast<royale::Rarity>(l.rarity);
    const bool big = rarity >= royale::Rarity::Epic;
    const s16 params = static_cast<s16>(((big ? ENBOX_TYPE_BIG_DEFAULT : ENBOX_TYPE_SMALL) << 12) | (GI_RUPEE_GREEN << 5) | kChestFlag);
    if (l.taken) Flags_SetTreasure(gPlayState, kChestFlag); else ClearChestFlag(); // opened chests are spawned already open
    const s16 rot = static_cast<s16>((index * 2654435761u) >> 16);
    gSpawningLoot = true;
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_BOX, l.x, groundY, l.z, 0, rot, 0, params, false);
    gSpawningLoot = false;
    ClearChestFlag();
    if (actor == nullptr) return;
    LootActor la;
    la.actor = actor; la.baseY = groundY; la.chest = true; la.opened = l.taken; la.big = big; la.rarity = rarity; la.supply = l.supply; la.special = l.special;
    la.origUpdate = actor->update;
    la.origDestroy = actor->destroy;
    gLoot[index] = la;
    gLootOf[actor] = index;
    actor->update = Chest_Update;
    actor->destroy = Chest_Destroy;
    if (!l.taken) {
        std::string label = l.special ? std::string("Heart Container Chest") : std::string(RarityName(rarity)) + " Chest";
        Color_RGBA8 labelColour = RarityColor(rarity);
        if (l.special) labelColour = { 255, 130, 190, 255 };
        NameTag_RegisterForActorWithOptions(actor, label.c_str(), NameTagOptions{ "royale-loot", static_cast<int16_t>(big ? 50 : 34), labelColour });
    }
}

// ---- scenery -----------------------------------------------------------------------------------------------------------------

// ---- our own models, drawn instead of the game's rocks -----------------------------------------------------------------------

// A mesh from shared/meshes.h turned into the game's vertex and display-list format. The vectors never grow after building, so the
// pointers inside the display list stay valid.
struct GpuMesh {
    std::vector<Vtx> vtx;
    std::vector<Gfx> dl;
};
GpuMesh gGpuMeshes[static_cast<int>(royale::MeshKind::Count)][royale::kMeshVariantSlots];
bool gGpuBuilt[static_cast<int>(royale::MeshKind::Count)][royale::kMeshVariantSlots] = {};

// Turns a triangle list into vertices and a display list. The vectors must not move afterwards: the display list points into them.
bool BuildGpuMesh(const royale::MeshData& data, GpuMesh& m) {
    if (data.v.empty()) return false;
    m.vtx.resize(data.v.size());
    for (size_t i = 0; i < data.v.size(); i++) {
        Vtx& v = m.vtx[i];
        v.v.ob[0] = static_cast<s16>(std::lround(data.v[i].x));
        v.v.ob[1] = static_cast<s16>(std::lround(data.v[i].y));
        v.v.ob[2] = static_cast<s16>(std::lround(data.v[i].z));
        v.v.flag = 0;
        v.v.tc[0] = v.v.tc[1] = 0;
        v.v.cn[0] = data.v[i].r;
        v.v.cn[1] = data.v[i].g;
        v.v.cn[2] = data.v[i].b;
        v.v.cn[3] = 255;
    }
    // The graphics chip takes up to 32 vertices at a time; each triangle has its own three, so ten triangles per batch.
    const size_t batches = (data.v.size() / 3 + 9) / 10;
    m.dl.assign(data.v.size() / 3 + batches + 1, Gfx{});
    Gfx* g = m.dl.data();
    for (size_t first = 0; first < data.v.size(); first += 30) {
        const size_t count = std::min<size_t>(30, data.v.size() - first);
        gSPVertex(g++, reinterpret_cast<uintptr_t>(&m.vtx[first]), static_cast<int>(count), 0);
        for (size_t t = 0; t + 2 < count; t += 3) gSP1Triangle(g++, static_cast<int>(t), static_cast<int>(t + 1), static_cast<int>(t + 2), 0);
    }
    gSPEndDisplayList(g++);
    m.dl.resize(static_cast<size_t>(g - m.dl.data())); // exactly what was written (the size only shrinks, so nothing moves)
    return true;
}

const GpuMesh* GpuMeshFor(royale::MeshKind kind, uint32_t variant) {
    const int k = static_cast<int>(kind);
    variant %= royale::kMeshVariantSlots;
    GpuMesh& m = gGpuMeshes[k][variant];
    if (gGpuBuilt[k][variant]) return &m;
    if (!BuildGpuMesh(royale::BuildMesh(kind, variant), m)) return nullptr;
    gGpuBuilt[k][variant] = true;
    return &m;
}

struct PropActor {
    Actor* actor = nullptr;
    ActorFunc origDestroy = nullptr;
    int meshKind = -1;     // royale::MeshKind drawn in place of the game's model, or -1 to leave the game's own
    uint32_t variant = 0;
    float scale = 1.0f;    // boulders come in sizes (royale::BoulderScale)
};
std::unordered_map<size_t, PropActor> gProps;        // prop index -> its actor
std::unordered_map<const Actor*, size_t> gPropOf;
std::unordered_set<size_t> gBrokenProps;              // rocks and bushes players smashed; they stay gone
std::unordered_set<size_t> gCulledProps;              // props we removed ourselves (far away), as opposed to smashed ones
constexpr float kPropSpawnRadius = 2200.0f;
constexpr float kTownSpawnRadius = 3400.0f;   // the pieces of a town (and the climbing blocks) show from further off, so you see a place before you reach it
constexpr size_t kMaxPropActors = 170;   // a town is a lot of wall pieces

void Prop_NoUpdate(Actor*, PlayState*) {} // the roof has no collision: never let the game's rock logic run on its stand-in

void Prop_DrawCustom(Actor* actor, PlayState* play) {
    auto idx = gPropOf.find(actor);
    if (idx == gPropOf.end()) return;
    auto pa = gProps.find(idx->second);
    if (pa == gProps.end() || pa->second.meshKind < 0) return;
    const GpuMesh* mesh = GpuMeshFor(static_cast<royale::MeshKind>(pa->second.meshKind), pa->second.variant);
    if (mesh == nullptr || mesh->dl.empty()) return;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(actor->shape.rot.y * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    if (pa->second.scale != 1.0f) Matrix_Scale(pa->second.scale, pa->second.scale, pa->second.scale, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK); // colours are baked into the vertices; draw both sides of every face
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(mesh->dl.data()));
    CLOSE_DISPS(play->state.gfxCtx);
}

bool CustomSceneryOn() {
    return CVarGetInteger(CVAR_SETTING("Royale.CustomScenery"), 1) != 0;
}
bool gSceneryWasCustom = true; // the setting the current props were spawned with

void Prop_Destroy(Actor* actor, PlayState* play) {
    ActorFunc orig = nullptr;
    auto idx = gPropOf.find(actor);
    if (idx != gPropOf.end()) {
        const size_t i = idx->second;
        auto pa = gProps.find(i);
        if (pa != gProps.end()) { orig = pa->second.origDestroy; gProps.erase(pa); }
        if (gCulledProps.erase(i) == 0) {                      // broken, not just put away because it is far off
            gBrokenProps.insert(i);
            if (gSession.Client() && !gSession.Client()->BrokenProps().count(i)) gSession.ReportPropSmashed(i);   // the server decides what was inside
        }
        gPropOf.erase(idx);
    }
    if (orig) orig(actor, play);
}

void SpawnProp(size_t index, const royale::Prop& p, float groundY) {
    int16_t id = ACTOR_EN_ISHI;
    int16_t params = 0;
    switch (p.kind) {
        case royale::PropKind::Rock: id = ACTOR_EN_ISHI; params = 0; break;
        case royale::PropKind::Boulder: id = ACTOR_EN_ISHI; params = 0x3CC1; break; // large rock; switch flag 0x3F so it is never "already smashed"
        case royale::PropKind::Pillar: id = ACTOR_EN_ISHI; params = 0x3CC1; break;
        case royale::PropKind::Bush: id = ACTOR_EN_KUSA; params = 0; break;
        case royale::PropKind::Roof: id = ACTOR_EN_ISHI; params = 0; break; // a stand-in actor to hang our roof model on
        case royale::PropKind::PlatformLow: case royale::PropKind::PlatformMid: case royale::PropKind::PlatformHigh: id = ACTOR_EN_ISHI; params = 0; break; // likewise for the climbing blocks
        default: return;
    }
    int meshKind = -1;
    if (royale::IsPlatform(p.kind)) meshKind = static_cast<int>(royale::MeshKind::Platform);   // the blocks only exist as our own model, whatever the scenery setting
    if (CustomSceneryOn()) {
        switch (p.kind) {
            case royale::PropKind::Rock: meshKind = static_cast<int>(royale::MeshKind::Rock); break;
            case royale::PropKind::Boulder: meshKind = static_cast<int>(royale::MeshKind::Boulder); break;
            case royale::PropKind::Pillar: meshKind = static_cast<int>(royale::MeshKind::Pillar); break;
            case royale::PropKind::Roof: meshKind = static_cast<int>(royale::MeshKind::Roof); break;
            default: break;
        }
    }
    if (p.kind == royale::PropKind::Roof && meshKind < 0) return; // a roof only exists as our own model
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, id, p.pos.x, groundY, p.pos.z, 0, static_cast<s16>(p.rot), 0, params, false);
    if (actor == nullptr) return; // the scene didn't have the object loaded for this one; skip it
    if (p.kind == royale::PropKind::Pillar && meshKind < 0) actor->scale.y *= 2.2f; // tall standing stones (the game's own rock, stretched)
    PropActor pa;
    pa.actor = actor;
    pa.origDestroy = actor->destroy;
    pa.meshKind = meshKind;
    pa.variant = royale::IsPlatform(p.kind) ? static_cast<uint32_t>(p.kind) - static_cast<uint32_t>(royale::PropKind::PlatformLow) : static_cast<uint32_t>(p.rot >> 4);
    if (royale::IsPlatform(p.kind)) pa.variant += 3u * static_cast<uint32_t>(CurrentMap().theme);   // blocks, posts and roofs in this map's stone and colours too
    if (p.kind == royale::PropKind::Pillar || p.kind == royale::PropKind::Roof) pa.variant = static_cast<uint32_t>(CurrentMap().theme);
    if (p.kind == royale::PropKind::Rock || p.kind == royale::PropKind::Boulder) {   // its shape, in this map's stone (meshes.h, StoneLook)
        pa.variant = static_cast<uint32_t>(royale::BoulderShape(p.rot) + royale::kBoulderShapes * static_cast<int>(CurrentMap().theme));
        if (p.kind == royale::PropKind::Boulder) pa.scale = royale::BoulderScale(p.rot);
    }
    gProps[index] = pa;
    gPropOf[actor] = index;
    actor->destroy = Prop_Destroy;
    if (meshKind >= 0) actor->draw = Prop_DrawCustom; // the game's rock stays as the solid part, unseen; our model is what you see
    switch (p.kind) { // a blob shadow under each, sized to the model
        case royale::PropKind::Rock: actor->shape.shadowScale = 26.0f; break;
        case royale::PropKind::Boulder: actor->shape.shadowScale = 75.0f * pa.scale; break;
        case royale::PropKind::Pillar: actor->shape.shadowScale = 40.0f; break;
        case royale::PropKind::Roof: actor->shape.shadowScale = 0.0f; break;
        default: if (royale::IsPlatform(p.kind)) actor->shape.shadowScale = 0.0f; break;
    }
    if (p.kind == royale::PropKind::Roof || royale::IsPlatform(p.kind)) {
        actor->update = Prop_NoUpdate;                // floating stand-in: no collision, no breaking
        actor->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED;
        actor->uncullZoneForward = 3000.0f; actor->uncullZoneScale = 1200.0f; actor->uncullZoneDownward = 1200.0f;
    }
}

void ClearProps() {
    for (auto& [idx, pa] : gProps) { gCulledProps.insert(idx); Actor_Kill(pa.actor); }
}

#include "RoyaleBosses.h"   // the bosses: mini bosses, the major boss of each map, their effects

// ---- the glider ---------------------------------------------------------------------------------------------------------------
// Everyone skydives under a striped glider. Other players' gliders are drawn along with them (Puppet_Draw); yours is a stand-in actor that
// follows you for the length of the fall, tilting as you steer and folding back when you dive.
float gGliderRoll = 0.0f;       // how far the local glider is banked
bool gGliderDiving = false;
Actor* gLocalGlider = nullptr;

// Each glider's canopy is a small piece of cloth (shared/cloth.h): it billows when air pushes up from below, ripples at the trailing edge and
// flaps harder in storms and wind. Simulated here per glider, from how it is moving through the air now.
struct GliderClothState {
    royale::GliderCloth cloth;
    float lx = 0, ly = 0, lz = 0;
    double lastT = 0;
    bool have = false;
};
std::unordered_map<uint32_t, GliderClothState> gGliderCloth;

void DrawGliderAt(PlayState* play, float x, float y, float z, s16 yaw, float roll, bool diving, uint32_t scheme, bool plain, const Player* hanger) {
    const bool cloth = gClothScale > 0.01f && !plain;
    if (cloth) gGliderClothFrames++;
    const GpuMesh* mesh = GpuMeshFor(cloth ? royale::MeshKind::GliderFrame : royale::MeshKind::Glider, cloth ? 0u : scheme);
    if (mesh == nullptr || mesh->dl.empty()) return;
    const float t = static_cast<float>(play->gameplayFrames) / royale::kTickHz;
    const double now = ImGui::GetTime();
    std::vector<royale::ClothVertex> tris;
    if (cloth) {
        if (gGliderCloth.size() > 48) gGliderCloth.clear();
        GliderClothState& st = gGliderCloth[scheme];
        if (!st.have) { st.lx = x; st.ly = y; st.lz = z; st.lastT = now; st.have = true; }
        const float dt = static_cast<float>(now - st.lastT);
        if (dt > 0.004f) {
            const float h = std::min(dt, 0.1f);
            const float vx = (x - st.lx) / dt, vy = (y - st.ly) / dt, vz = (z - st.lz) / dt;
            st.lx = x; st.ly = y; st.lz = z; st.lastT = now;
            float wx, wz, wind;
            WindNow(&wx, &wz, &wind);
            // The air as the glider sees it: wind minus its own movement, turned into the glider's frame (x to its left, z along its nose).
            const float ax = wx - vx, az = wz - vz, ay = -vy;
            const float yawRad = yaw * (3.14159265f / 32768.0f), c = std::cos(yawRad), sn = std::sin(yawRad);
            const royale::ClothV3 air = { (ax * c - az * sn) * gClothScale, ay * gClothScale, (ax * sn + az * c) * gClothScale };
            st.cloth.Update(h, air, wind * gClothScale, static_cast<float>(now), scheme);
        }
        static const uint8_t schemes[4][2][3] = { {{230, 70, 60}, {245, 235, 220}}, {{70, 130, 235}, {245, 220, 90}}, {{70, 190, 100}, {245, 245, 235}}, {{170, 90, 230}, {250, 210, 120}} };
        st.cloth.Build(tris, schemes[scheme % 4][0], schemes[scheme % 4][1]);
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    // The glider's origin is its handle bar, and the bar is put exactly where Link's two hands are (the game works out where each hand is as it
    // draws him), so he always holds the grips, whatever his size or pose. Banking, pitching and swaying all turn about the bar, so it stays in his hands.
    Vec3f grip = { x, y + 125.0f, z };   // (before he has been drawn once: roughly where hands hanging from a ledge are)
    if (hanger != nullptr) {
        const Vec3f& l = hanger->bodyPartsPos[PLAYER_BODYPART_L_HAND];
        const Vec3f& r = hanger->bodyPartsPos[PLAYER_BODYPART_R_HAND];
        const float mx = (l.x + r.x) * 0.5f, my = (l.y + r.y) * 0.5f, mz = (l.z + r.z) * 0.5f;
        // Trust the hands only if they are somewhere believable (near him and above his head's height): the pose may not have taken yet.
        if (std::isfinite(mx + my + mz) && std::fabs(mx - x) < 80.0f && std::fabs(mz - z) < 80.0f && my > y + 60.0f && my < y + 220.0f) grip = { mx, my, mz };
    }
    Matrix_Translate(grip.x, grip.y, grip.z, MTXMODE_NEW);
    Matrix_RotateY(yaw * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    Matrix_RotateZ(roll + std::sin(t * 3.1f + scheme) * 0.04f, MTXMODE_APPLY);               // gentle sway in the wind
    Matrix_RotateX((diving ? 0.5f : 0.1f) + std::sin(t * 2.3f + scheme * 1.7f) * 0.025f, MTXMODE_APPLY); // nose down for a dive
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(mesh->dl.data()));
    if (!tris.empty()) {   // the canopy: this frame's triangles, in memory the game hands out for one frame
        Vtx* v = static_cast<Vtx*>(Graph_Alloc(play->state.gfxCtx, tris.size() * sizeof(Vtx)));
        for (size_t i = 0; i < tris.size(); i++) {
            v[i].v.ob[0] = static_cast<s16>(std::lround(tris[i].x));
            v[i].v.ob[1] = static_cast<s16>(std::lround(tris[i].y));
            v[i].v.ob[2] = static_cast<s16>(std::lround(tris[i].z));
            v[i].v.flag = 0; v[i].v.tc[0] = v[i].v.tc[1] = 0;
            v[i].v.cn[0] = tris[i].r; v[i].v.cn[1] = tris[i].g; v[i].v.cn[2] = tris[i].b; v[i].v.cn[3] = 255;
        }
        for (size_t first = 0; first < tris.size(); first += 30) {
            const size_t count = std::min<size_t>(30, tris.size() - first);
            gSPVertex(POLY_OPA_DISP++, reinterpret_cast<uintptr_t>(&v[first]), static_cast<int>(count), 0);
            for (size_t k = 0; k + 2 < count; k += 3) gSP1Triangle(POLY_OPA_DISP++, static_cast<int>(k), static_cast<int>(k + 1), static_cast<int>(k + 2), 0);
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

void LocalGlider_Update(Actor* actor, PlayState* play) {
    Player* p = GET_PLAYER(play);
    actor->world.pos = p->actor.world.pos;
    actor->shape.rot.y = p->actor.shape.rot.y;
    actor->focus.pos = actor->world.pos;
}
void LocalGlider_Draw(Actor* actor, PlayState* play) {
    DrawGliderAt(play, actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, actor->shape.rot.y, gGliderRoll, gGliderDiving, 0, false, GET_PLAYER(play));
}
void LocalGlider_Destroy(Actor* actor, PlayState*) { if (gLocalGlider == actor) gLocalGlider = nullptr; }

void ReconcileLocalGlider(bool want) {
    if (want && gLocalGlider == nullptr && InField() && gPlayState != nullptr) {
        Player* p = GET_PLAYER(gPlayState);
        Actor* a = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ISHI, p->actor.world.pos.x, p->actor.world.pos.y, p->actor.world.pos.z, 0, p->actor.shape.rot.y, 0, 0, false);
        if (a == nullptr) return;
        a->update = LocalGlider_Update;
        a->draw = LocalGlider_Draw;
        a->destroy = LocalGlider_Destroy;
        a->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
        a->uncullZoneForward = 6000.0f; a->uncullZoneScale = 3000.0f; a->uncullZoneDownward = 3000.0f;
        a->shape.shadowScale = 0.0f;
        gLocalGlider = a;
    } else if (!want && gLocalGlider != nullptr) {
        Actor_Kill(gLocalGlider);
        gLocalGlider = nullptr;
    }
}

// ---- foliage, snow and puddles on the ground and the weather the game itself draws ------------------------------------------
// Patches of swaying grass, trees (a different set of leaves for each season) and, when it snows, mounds of snow that build up on the ground and
// slowly melt away afterwards are scattered around the player. Rain leaves puddles on level ground that grow while it pours, ripple with
// every drop and dry up slowly once it stops; rain also washes the snow away, and snow covers the puddles over. All of it is local scenery: where it stands is worked out from the map and a hash of
// each cell, so nothing is sent over the network, and it is only ever drawn near the player. Trees are solid (you walk around the trunk).
bool WaterAt(float x, float z, float floorY) { return UnderWater(x, z, floorY); }
float gFoliage = 1.0f;       // the local option, 0 (none) to 2
float gSnowCover = 0.0f;     // 0 bare ground to 1 deep snow
float gPuddleCover = 0.0f;   // 0 dry ground to 1 soaked (big puddles everywhere it is level)
float WeatherAmount();

uint32_t FloraHash(int a, int b, int salt) {
    uint32_t h = static_cast<uint32_t>(a) * 374761393u + static_cast<uint32_t>(b) * 668265263u + static_cast<uint32_t>(salt) * 2246822519u + 0x9E3779B9u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
float Flora01(int a, int b, int salt) { return static_cast<float>(FloraHash(a, b, salt) & 0xFFFF) / 65535.0f; }

struct FloraSpot { bool ok; float y; float sx = 0.0f, sz = 0.0f; };   // sx, sz: how the ground slopes (puddles lie along it)
std::unordered_map<uint64_t, FloraSpot> gFloraSpots;
int gFloraScene = -1;
int gFloraBudget = 0;

// Is there good ground at (x, z)? Cached per cell. `kind`: 0 grass, 1 a tree, 2 a snow mound, 3 a puddle, 4 a snow blanket, 5 small scenery
// (Decor), 6 a town's clutter (cell = the town and the piece).
// nullptr = not measured yet (the per-frame budget of measurements ran out).
const FloraSpot* FloraSpotAt(int kind, int cx, int cz, float x, float z) {
    const uint64_t key = (static_cast<uint64_t>(kind) << 58) | (static_cast<uint64_t>(cx + 65536) << 29) | static_cast<uint64_t>(cz + 65536);
    auto it = gFloraSpots.find(key);
    if (it != gFloraSpots.end()) return &it->second;
    if (gFloraBudget <= 0) return nullptr;
    gFloraBudget--;
    FloraSpot spot = { false, 0.0f };
    float y = 0, y2 = 0, y3 = 0;
    if (RawFloorAt(x, z, &y) && !WaterAt(x, z, y) && !OnExitFloor(x, z)) {
        spot.ok = true;
        spot.y = y;
        if (kind == 3) {   // puddles only lie on level, even ground: a gentle slope at most, and no bumps or edges across them
            float y4 = 0, y5 = 0;
            spot.ok = RawFloorAt(x + 45.0f, z, &y2) && RawFloorAt(x, z + 45.0f, &y3) && RawFloorAt(x - 45.0f, z, &y4) && RawFloorAt(x, z - 45.0f, &y5) &&
                      std::fabs(y2 - y4) < 14.0f && std::fabs(y3 - y5) < 14.0f && std::fabs(y2 + y4 - 2.0f * y) < 3.0f && std::fabs(y3 + y5 - 2.0f * y) < 3.0f;
            spot.sx = (y2 - y4) / 90.0f;
            spot.sz = (y3 - y5) / 90.0f;
        } else if (kind == 0 || kind == 1 || kind == 5 || kind == 6) {   // grass, trees and the small things stay off steep ground (and cliff edges)
            spot.ok = RawFloorAt(x + 45.0f, z, &y2) && RawFloorAt(x, z + 45.0f, &y3) && std::fabs(y2 - y) < 26.0f && std::fabs(y3 - y) < 26.0f;
        }
        if (spot.ok && kind == 1 && gSession.Client()) {   // a tree keeps clear of scenery, towns and loot sites
            for (const royale::Prop& p : gSession.Client()->Props())
                if (std::fabs(p.pos.x - x) < 140.0f && std::fabs(p.pos.z - z) < 140.0f) { spot.ok = false; break; }
            for (const royale::Poi& poi : gSession.Client()->Pois())
                if (std::hypot(poi.center.x - x, poi.center.z - z) < poi.radius * 0.7f + 140.0f) { spot.ok = false; break; }
        }
        if (spot.ok && kind == 6 && gSession.Client()) {   // a town's clutter stands clear of its walls, rocks and climbing blocks
            for (const royale::Prop& p : gSession.Client()->Props()) {
                const float clear = royale::IsPlatform(p.kind) ? 120.0f : royale::PropRadius(p.kind) + 35.0f;
                if (royale::PropRadius(p.kind) > 0 && std::fabs(p.pos.x - x) < clear && std::fabs(p.pos.z - z) < clear) { spot.ok = false; break; }
            }
        }
    }
    return &gFloraSpots.emplace(key, spot).first->second;
}

constexpr float kGrassCell = 95.0f, kTreeCell = 380.0f, kSnowCell = 125.0f, kBlanketCell = 210.0f, kPuddleCell = 260.0f, kDecorCell = 170.0f;
// The Fortnite Map's trees stand where its texture has them painted (fortnite::CoverAt): thick in the woods, a lone one here and there on the
// meadows, none on the roads or in the towns. Its cells are smaller, so the woods can be dense.
constexpr float kIslandTreeCell = 200.0f;
float TreeCell() { return OnIsland() ? kIslandTreeCell : kTreeCell; }

// Each map's plants: Hyrule Field, Lake Hylia and Kakariko keep the leafy trees and green grass (Kakariko's trees sparser), the desert has
// golden dry grass and palms gathered in oases, and Death Mountain has no grass, only a few dead, burnt trees.
int FloraTheme() { return static_cast<int>(CurrentMap().theme); }

struct TreeSpot { float x, y, z, scale, yaw; uint32_t variant; royale::MeshKind kind = royale::MeshKind::Tree; };
bool TreeIn(int cx, int cz, int season, TreeSpot* out) {
    if (OnIsland()) {
        const float x = (static_cast<float>(cx) + 0.12f + 0.76f * Flora01(cx, cz, 23)) * kIslandTreeCell, z = (static_cast<float>(cz) + 0.12f + 0.76f * Flora01(cx, cz, 24)) * kIslandTreeCell;
        const royale::fortnite::Cover cover = royale::fortnite::CoverAt(x, z);
        const float roll = Flora01(cx, cz, 22), dense = std::min(1.0f, gFoliage);
        if (cover == royale::fortnite::Cover::Woods ? roll > 0.35f + 0.5f * dense : cover != royale::fortnite::Cover::Meadow || roll > 0.03f * dense) return false;
        const FloraSpot* spot = FloraSpotAt(1, cx, cz, x, z);
        if (spot == nullptr || !spot->ok) return false;
        *out = { x, spot->y, z, 0.85f + 0.55f * Flora01(cx, cz, 25), Flora01(cx, cz, 26) * 6.2831853f, (FloraHash(cx, cz, 27) % 4) + 4u * static_cast<uint32_t>(season) };
        return true;
    }
    const int theme = FloraTheme();
    const float grove = theme == 4 ? 0.82f : theme == 3 ? 0.7f : theme == 2 ? 0.55f : 0.45f;   // the desert's oases and the mountain's few trees are rare
    if (Flora01(cx / 2, cz / 2, 21) < grove - 0.2f * std::min(1.0f, gFoliage)) return false;   // groves: whole blocks of cells are empty
    if (Flora01(cx, cz, 22) > (theme == 3 ? 0.35f : 0.62f)) return false;
    const float x = (static_cast<float>(cx) + 0.12f + 0.76f * Flora01(cx, cz, 23)) * kTreeCell, z = (static_cast<float>(cz) + 0.12f + 0.76f * Flora01(cx, cz, 24)) * kTreeCell;
    const FloraSpot* spot = FloraSpotAt(1, cx, cz, x, z);
    if (spot == nullptr || !spot->ok) return false;
    *out = { x, spot->y, z, 0.8f + 0.5f * Flora01(cx, cz, 25), Flora01(cx, cz, 26) * 6.2831853f, (FloraHash(cx, cz, 27) % 4) + 4u * static_cast<uint32_t>(season) };
    if (theme == 4) { out->kind = royale::MeshKind::ThemeTree; out->variant = FloraHash(cx, cz, 27) % 4; }          // palms
    if (theme == 3) { out->kind = royale::MeshKind::ThemeTree; out->variant = 4u + FloraHash(cx, cz, 27) % 4; }     // dead trees
    return true;
}

// The small things on the ground between the towns (meshes.h, Decor): which of the map's six is in a cell, or -1 for none. The first two of
// each map are the common ones; the rest turn up now and then, and each kind gathers in drifts (blocks of cells) rather than evenly.
int DecorIn(int cx, int cz) {
    if (Flora01(cx, cz, 81) > 0.34f * std::min(1.3f, gFoliage)) return -1;
    const float r = Flora01(cx / 3, cz / 3, 82) * 0.6f + Flora01(cx, cz, 83) * 0.4f;
    return r < 0.3f ? 0 : r < 0.55f ? 1 : r < 0.68f ? 2 : r < 0.8f ? 3 : r < 0.9f ? 4 : 5;
}

// The lived-in clutter round a town (meshes.h, Clutter): a few groups (a cart with hay, crates and barrels, pots by a lantern, a fire pit
// with its logs, a signpost at the edge), placed from the town's name so everyone sees the same.
struct ClutterPiece { float x, z, yaw; uint32_t variant; };
void TownClutter(const royale::Poi& poi, std::vector<ClutterPiece>& out) {
    static const uint32_t groups[5][3] = {{4, 3, 3}, {0, 1, 0}, {2, 5, 2}, {6, 0, 1}, {7, 1, 2}};
    const int n = 6;
    for (int g = 0; g < n; g++) {
        const float a = (g + 0.6f * Flora01(poi.name, g, 91)) * 6.2831853f / n, d = poi.radius * (0.35f + 0.5f * Flora01(poi.name, g, 92));
        const float gx = poi.center.x + std::cos(a) * d, gz = poi.center.z + std::sin(a) * d;
        const uint32_t* set = groups[FloraHash(poi.name, g, 93) % 5];
        for (int k = 0; k < 3; k++) {
            const float b = a + k * 2.1f + Flora01(g, k, 94), r = k == 0 ? 0.0f : 46.0f + 14.0f * Flora01(poi.name + k, g, 95);
            out.push_back({gx + std::cos(b) * r, gz + std::sin(b) * r, Flora01(poi.name, g * 3 + k, 96) * 6.2831853f, set[k]});
        }
    }
}

void DrawFloraMesh(PlayState* play, const GpuMesh* m, float x, float y, float z, float yaw, float tiltX, float tiltZ, float scale) {
    OPEN_DISPS(play->state.gfxCtx);
    Matrix_Translate(x, y, z, MTXMODE_NEW);
    Matrix_RotateY(yaw, MTXMODE_APPLY);
    Matrix_RotateX(tiltX, MTXMODE_APPLY);
    Matrix_RotateZ(tiltZ, MTXMODE_APPLY);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(m->dl.data()));
    CLOSE_DISPS(play->state.gfxCtx);
}

// Something lying flat on (possibly gently sloping) ground, drawn see-through with `alpha` (0-255). Expects the XLU setup DrawFlora makes.
void DrawGroundXlu(PlayState* play, const GpuMesh* m, float x, float y, float z, float sx, float sz, float yaw, float scale, int alpha) {
    OPEN_DISPS(play->state.gfxCtx);
    Matrix_Translate(x, y, z, MTXMODE_NEW);
    Matrix_RotateZ(std::atan(sx), MTXMODE_APPLY);    // lean along the slope, then turn about the ground's own up
    Matrix_RotateX(-std::atan(sz), MTXMODE_APPLY);
    Matrix_RotateY(yaw, MTXMODE_APPLY);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 255, 255, static_cast<u8>(std::clamp(alpha, 0, 255)));
    gSPDisplayList(POLY_XLU_DISP++, const_cast<Gfx*>(m->dl.data()));
    CLOSE_DISPS(play->state.gfxCtx);
}

int FloraSeason() { return gSession.Joined() ? (static_cast<int>(gWeatherShown.season) & 3) : 1; }

void DrawFlora(PlayState* play) {
    if (!InField() || gPlayState == nullptr) return;
    if (play->sceneNum != gFloraScene) { gFloraScene = play->sceneNum; gFloraSpots.clear(); gSnowCover = 0.0f; gPuddleCover = 0.0f; }
    {   // a new world from the host (new towns and scenery): forget which ground was clear of them
        static float lastSig = 0.0f;
        float sig = 0.0f;
        if (gSession.Client()) {
            const auto& pois = gSession.Client()->Pois();
            sig = static_cast<float>(pois.size()) + static_cast<float>(gSession.Client()->Props().size()) * 1000.0f + (pois.empty() ? 0.0f : pois[0].center.x * 0.37f + pois.back().center.z * 0.11f);
        }
        if (sig != lastSig) { lastSig = sig; gFloraSpots.clear(); }
    }
    const float dt = std::min(0.05f, ImGui::GetIO().DeltaTime);
    const int season = FloraSeason();
    const bool snowing = gWeatherShown.sky == royale::Sky::Snow && WeatherAmount() > 0.15f;
    const bool rainSky = gWeatherShown.sky == royale::Sky::Rain || gWeatherShown.sky == royale::Sky::Thunder;
    const float rainNow = std::max(rainSky ? WeatherAmount() : 0.0f, gStormWeather * 0.8f);   // the same rain DriveRealWeather lets fall
    const bool raining = rainNow > 0.12f;
    if (snowing) gSnowCover = std::min(1.0f, gSnowCover + dt / 45.0f * (0.5f + WeatherAmount()));
    else if (raining) gSnowCover = std::max(0.0f, gSnowCover - dt / 40.0f * (0.5f + rainNow));   // rain washes the snow away
    else gSnowCover = std::max(season == 3 ? 0.3f : 0.0f, gSnowCover - dt / 150.0f);   // it melts slowly (winter keeps a little)
    if (raining) gPuddleCover = std::min(1.0f, gPuddleCover + dt / 40.0f * (0.5f + rainNow));
    else gPuddleCover = std::max(0.0f, gPuddleCover - dt / (snowing ? 25.0f : 120.0f));   // they dry up slowly, or the snow covers them
    const bool snowOn = gSnowCover > 0.02f, puddlesOn = gPuddleCover > 0.02f;
    if (gFoliage <= 0.01f && !snowOn && !puddlesOn) return;

    Player* pl = GET_PLAYER(play);
    const float px = pl->actor.world.pos.x, pz = pl->actor.world.pos.z;
    const float t = static_cast<float>(ImGui::GetTime());
    float wx, wz, wind;
    WindNow(&wx, &wz, &wind);
    const float wl = std::max(1.0f, std::hypot(wx, wz)), dx = wx / wl, dz = wz / wl;
    gFloraBudget = 36;

    {
        OPEN_DISPS(play->state.gfxCtx);
        Gfx_SetupDL_25Opa(play->state.gfxCtx);
        gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
        gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
        CLOSE_DISPS(play->state.gfxCtx);
    }
    auto fade = [](float dist, float reach) { const float f = std::clamp((reach - dist) / (reach * 0.25f), 0.0f, 1.0f); return f * f * (3.0f - 2.0f * f); };
    // Nothing behind the camera is drawn: about half of everything near the player. `size` is how far the thing reaches from its spot.
    const Vec3f eye = play->view.eye;
    float vx = play->view.lookAt.x - eye.x, vz = play->view.lookAt.z - eye.z;
    const float vl = std::hypot(vx, vz), vy = std::fabs(play->view.lookAt.y - eye.y);
    const bool cull = vl > 1.0f && vl > vy * 0.5f;   // not when looking steeply down (the skydive): then all round is in view
    if (cull) { vx /= vl; vz /= vl; }
    auto inView = [&](float x, float z, float size) { return !cull || (x - eye.x) * vx + (z - eye.z) * vz > -size; };
    const bool island = OnIsland();

    if (snowOn) {   // mounds of snow, thicker the longer it has snowed
        const float reach = 1000.0f;
        const int c0x = static_cast<int>(std::floor((px - reach) / kSnowCell)), c1x = static_cast<int>(std::floor((px + reach) / kSnowCell));
        const int c0z = static_cast<int>(std::floor((pz - reach) / kSnowCell)), c1z = static_cast<int>(std::floor((pz + reach) / kSnowCell));
        for (int cz = c0z; cz <= c1z; cz++)
            for (int cx = c0x; cx <= c1x; cx++) {
                if (Flora01(cx, cz, 31) > gSnowCover * 0.92f) continue;
                const float x = (static_cast<float>(cx) + 0.2f + 0.6f * Flora01(cx, cz, 32)) * kSnowCell, z = (static_cast<float>(cz) + 0.2f + 0.6f * Flora01(cx, cz, 33)) * kSnowCell;
                const float d = std::hypot(x - px, z - pz);
                if (d > reach || !inView(x, z, 70.0f)) continue;
                const FloraSpot* spot = FloraSpotAt(2, cx, cz, x, z);
                if (spot == nullptr || !spot->ok) continue;
                const GpuMesh* m = GpuMeshFor(royale::MeshKind::SnowPatch, FloraHash(cx, cz, 34) % 4);
                if (m == nullptr || m->dl.empty()) continue;
                const float k = (0.75f + 0.5f * gSnowCover) * (0.85f + 0.5f * Flora01(cx, cz, 35)) * fade(d, reach);
                if (k > 0.02f) DrawFloraMesh(play, m, x, spot->y - 1.5f, z, Flora01(cx, cz, 36) * 6.2831853f, 0, 0, k);
            }
    }
    if (gSnowCover > 0.35f) {   // deep snow: broad, low blankets fill the ground between the mounds
        const float reach = 1000.0f, deep = (gSnowCover - 0.35f) / 0.65f;
        const int c0x = static_cast<int>(std::floor((px - reach) / kBlanketCell)), c1x = static_cast<int>(std::floor((px + reach) / kBlanketCell));
        const int c0z = static_cast<int>(std::floor((pz - reach) / kBlanketCell)), c1z = static_cast<int>(std::floor((pz + reach) / kBlanketCell));
        for (int cz = c0z; cz <= c1z; cz++)
            for (int cx = c0x; cx <= c1x; cx++) {
                if (Flora01(cx, cz, 71) > deep * 0.9f) continue;
                const float x = (static_cast<float>(cx) + 0.25f + 0.5f * Flora01(cx, cz, 72)) * kBlanketCell, z = (static_cast<float>(cz) + 0.25f + 0.5f * Flora01(cx, cz, 73)) * kBlanketCell;
                const float d = std::hypot(x - px, z - pz);
                if (d > reach || !inView(x, z, 120.0f)) continue;
                const FloraSpot* spot = FloraSpotAt(4, cx, cz, x, z);   // any ground, like the mounds, in cells of its own
                if (spot == nullptr || !spot->ok) continue;
                const GpuMesh* m = GpuMeshFor(royale::MeshKind::SnowPatch, 4u + FloraHash(cx, cz, 74) % 4);
                if (m == nullptr || m->dl.empty()) continue;
                const float k = (0.7f + 0.3f * deep) * (0.85f + 0.35f * Flora01(cx, cz, 75)) * fade(d, reach);
                if (k > 0.02f) DrawFloraMesh(play, m, x, spot->y - 1.0f, z, Flora01(cx, cz, 76) * 6.2831853f, 0, 0, k);
            }
    }

    if (gFoliage > 0.01f) {
        // grass: patches (blocks of cells that are grassy) of tufts, leaning and swaying in the wind
        const float reach = 700.0f + 650.0f * std::min(1.5f, gFoliage);
        const int c0x = static_cast<int>(std::floor((px - reach) / kGrassCell)), c1x = static_cast<int>(std::floor((px + reach) / kGrassCell));
        const int c0z = static_cast<int>(std::floor((pz - reach) / kGrassCell)), c1z = static_cast<int>(std::floor((pz + reach) / kGrassCell));
        Feat("wind on scenery");
        // With the wind on, gust fronts roll across the ground along the wind: the plants in a front bend further, then spring back.
        const float amp = 0.07f + 0.3f * wind, lean = 0.05f + 0.45f * wind;
        const float along = 0.0035f, front = t * (1.4f + 1.2f * wind);
        const int theme = FloraTheme();
        const uint32_t grassSeason = theme == 4 ? 2u : static_cast<uint32_t>(season);   // the desert's grass is dry and golden whatever the season
        const float grassy = theme == 4 ? 0.75f : theme == 2 ? 0.58f : 0.5f;           // and sparser, as is Kakariko's
        for (int cz = c0z; cz <= c1z && theme != 3; cz++)                               // none at all on Death Mountain
            for (int cx = c0x; cx <= c1x; cx++) {
                if (Flora01(cx / 6, cz / 6, 41) < (island ? 0.3f : grassy)) continue;               // not a grassy patch
                if (Flora01(cx, cz, 42) > 0.55f * std::min(1.2f, gFoliage) + 0.1f) continue;
                const float x = (static_cast<float>(cx) + 0.15f + 0.7f * Flora01(cx, cz, 43)) * kGrassCell, z = (static_cast<float>(cz) + 0.15f + 0.7f * Flora01(cx, cz, 44)) * kGrassCell;
                const float d = std::hypot(x - px, z - pz);
                if (d > reach || !inView(x, z, 40.0f)) continue;
                if (island) {   // the island's grass grows on its meadows and under its trees, not on the roads, the paving or the fields
                    const royale::fortnite::Cover cover = royale::fortnite::CoverAt(x, z);
                    if (cover != royale::fortnite::Cover::Meadow && cover != royale::fortnite::Cover::Woods) continue;
                }
                const FloraSpot* spot = FloraSpotAt(0, cx, cz, x, z);
                if (spot == nullptr || !spot->ok) continue;
                const GpuMesh* m = GpuMeshFor(royale::MeshKind::Grass, (FloraHash(cx, cz, 45) % 4) + 4u * grassSeason);
                if (m == nullptr || m->dl.empty()) continue;
                const float phase = t * (1.6f + 2.4f * wind) + x * 0.011f + z * 0.009f;
                const float gustFront = 0.55f + 0.45f * std::sin(front - (x * dx + z * dz) * along);
                const float a = lean * gustFront + std::sin(phase) * amp * (0.5f + 0.5f * gustFront) + std::sin(phase * 2.3f + 1.0f) * amp * 0.35f;
                const float k = 0.27f * (0.8f + 0.5f * Flora01(cx, cz, 46)) * fade(d, reach);
                if (k > 0.01f) DrawFloraMesh(play, m, x, spot->y - 1.0f, z, Flora01(cx, cz, 47) * 6.2831853f, dz * a, -dx * a, k);
            }

        // trees
        const float treeReach = (island ? 1500.0f : 1800.0f) + (island ? 1300.0f : 1800.0f) * std::min(1.5f, gFoliage);   // the island's woods are dense: not quite as far
        const float treeCell = TreeCell();
        const int t0x = static_cast<int>(std::floor((px - treeReach) / treeCell)), t1x = static_cast<int>(std::floor((px + treeReach) / treeCell));
        const int t0z = static_cast<int>(std::floor((pz - treeReach) / treeCell)), t1z = static_cast<int>(std::floor((pz + treeReach) / treeCell));
        const float tamp = 0.008f + 0.05f * wind;
        for (int cz = t0z; cz <= t1z; cz++)
            for (int cx = t0x; cx <= t1x; cx++) {
                TreeSpot tr;
                if (!TreeIn(cx, cz, season, &tr)) continue;
                const float d = std::hypot(tr.x - px, tr.z - pz);
                if (d > treeReach || !inView(tr.x, tr.z, 260.0f * tr.scale)) continue;
                const GpuMesh* m = GpuMeshFor(tr.kind, tr.variant);
                if (m == nullptr || m->dl.empty()) continue;
                const float gustFront = 0.55f + 0.45f * std::sin(front - (tr.x * dx + tr.z * dz) * along);
                const float a = tamp * std::sin(t * (1.1f + wind) + tr.x * 0.004f) * (0.6f + 0.4f * gustFront) + wind * 0.035f * gustFront;
                DrawFloraMesh(play, m, tr.x, tr.y - 2.0f, tr.z, tr.yaw, dz * a, -dx * a, tr.scale * std::max(0.01f, fade(d, treeReach)));
            }
    }

    if (gFoliage > 0.01f) {
        // the small things on the ground: flowers, ferns and logs in the field, reeds and driftwood by the lake, gourds and old fences in
        // Kakariko, Bomb Flowers and embers on the mountain, cacti and bones in the desert
        const int theme = FloraTheme();
        const float reach = 900.0f + 700.0f * std::min(1.5f, gFoliage);
        const int c0x = static_cast<int>(std::floor((px - reach) / kDecorCell)), c1x = static_cast<int>(std::floor((px + reach) / kDecorCell));
        const int c0z = static_cast<int>(std::floor((pz - reach) / kDecorCell)), c1z = static_cast<int>(std::floor((pz + reach) / kDecorCell));
        for (int cz = c0z; cz <= c1z; cz++)
            for (int cx = c0x; cx <= c1x; cx++) {
                const int item = DecorIn(cx, cz);
                if (item < 0) continue;
                const float x = (static_cast<float>(cx) + 0.15f + 0.7f * Flora01(cx, cz, 84)) * kDecorCell, z = (static_cast<float>(cz) + 0.15f + 0.7f * Flora01(cx, cz, 85)) * kDecorCell;
                const float d = std::hypot(x - px, z - pz);
                if (d > reach || !inView(x, z, 80.0f)) continue;
                if (island && royale::fortnite::CoverAt(x, z) != royale::fortnite::Cover::Meadow && royale::fortnite::CoverAt(x, z) != royale::fortnite::Cover::Woods) continue;
                const FloraSpot* spot = FloraSpotAt(5, cx, cz, x, z);
                if (spot == nullptr || !spot->ok) continue;
                const GpuMesh* m = GpuMeshFor(royale::MeshKind::Decor, static_cast<uint32_t>(item + 6 * theme));
                if (m == nullptr || m->dl.empty()) continue;
                const float k = (0.85f + 0.4f * Flora01(cx, cz, 86)) * fade(d, reach);
                if (k > 0.02f) DrawFloraMesh(play, m, x, spot->y - 1.0f, z, Flora01(cx, cz, 87) * 6.2831853f, 0, 0, k);
            }
        // the clutter of the people who live in the towns
        if (gSession.Client()) {
            const float townReach = 2200.0f;
            static std::vector<ClutterPiece> pieces;
            const auto& pois = gSession.Client()->Pois();
            for (size_t pi = 0; pi < pois.size(); pi++) {
                if (std::hypot(pois[pi].center.x - px, pois[pi].center.z - pz) > townReach + pois[pi].radius) continue;
                pieces.clear();
                TownClutter(pois[pi], pieces);
                for (size_t k = 0; k < pieces.size(); k++) {
                    const ClutterPiece& c = pieces[k];
                    const float d = std::hypot(c.x - px, c.z - pz);
                    if (d > townReach || !inView(c.x, c.z, 80.0f)) continue;
                    const FloraSpot* spot = FloraSpotAt(6, static_cast<int>(pi), static_cast<int>(k), c.x, c.z);
                    if (spot == nullptr || !spot->ok) continue;
                    const GpuMesh* m = GpuMeshFor(royale::MeshKind::Clutter, c.variant);
                    if (m == nullptr || m->dl.empty()) continue;
                    DrawFloraMesh(play, m, c.x, spot->y - 1.0f, c.z, c.yaw, 0, 0, std::max(0.01f, fade(d, townReach)));
                }
            }
        }
    }

    // The island's gusts: when the wind gets up (a storm blowing in, the storm itself, or any breezy autumn day) leaves are torn off its woods and
    // tumble past the player along the wind, red and gold in autumn, green the rest of the year. Each leaf lives a few seconds, then starts again
    // somewhere upwind; where they are comes from the time alone, so nothing is stored.
    const float gust = !island ? 0.0f : std::clamp(std::max({ (wind - 0.25f) * 1.6f, season == 2 ? 0.35f + wind : 0.0f, gStormWeather * 0.8f }), 0.0f, 1.0f);
    if (gust > 0.05f && gFoliage > 0.01f) {
        // Each leaf belongs to a square of ground near the player (so it stays put in the world as you run past), starting at a spot in it and
        // carried downwind for its few seconds.
        const float cellSize = 700.0f, life = 5.0f, speed = 120.0f + 320.0f * wind;
        const int perCell = static_cast<int>(7.0f * gust * std::min(1.3f, gFoliage));
        const int pcx = static_cast<int>(std::floor(px / cellSize)), pcz = static_cast<int>(std::floor(pz / cellSize));
        for (int ccz = pcz - 1; ccz <= pcz + 1; ccz++)
            for (int ccx = pcx - 1; ccx <= pcx + 1; ccx++)
                for (int j = 0; j < perCell; j++) {
                    const int id = (ccx * 31 + ccz) * 16 + j;
                    const float clock = t / life + Flora01(ccx * 16 + j, ccz, 101);
                    const int round = static_cast<int>(std::floor(clock));
                    const float age = clock - static_cast<float>(round);
                    const float sx = (static_cast<float>(ccx) + Flora01(id, round, 102)) * cellSize - dx * speed * life * 0.5f;
                    const float sz = (static_cast<float>(ccz) + Flora01(id, round, 103)) * cellSize - dz * speed * life * 0.5f;
                    const float flutter = std::sin(t * 3.1f + j) * 30.0f;
                    const float x = sx + dx * speed * age * life - dz * flutter, z = sz + dz * speed * age * life + dx * flutter;
                    if (std::hypot(x - px, z - pz) > 1000.0f || !inView(x, z, 20.0f)) continue;
                    float gy = 0;
                    if (!RawFloorAt(x, z, &gy)) continue;
                    const float y = std::max(gy, static_cast<float>(royale::fortnite::kWaterY)) + 25.0f + 140.0f * Flora01(id, round, 104) * (1.0f - age) + std::sin(t * 2.3f + j * 1.7f) * 18.0f;
                    const GpuMesh* m = GpuMeshFor(royale::MeshKind::LeafPile, season == 2 ? FloraHash(id, round, 105) % 2 : 3u);   // autumn's reds and golds, or a mix with green in it
                    if (m == nullptr || m->dl.empty()) continue;
                    const float k = 0.13f * std::min(1.0f, std::min(age, 1.0f - age) * 6.0f);
                    if (k > 0.004f) DrawFloraMesh(play, m, x, y, z, t * (1.3f + Flora01(id, 3, 106)) + j, std::sin(t * 4.0f + j) * 1.2f, std::cos(t * 3.3f + j * 0.7f) * 1.2f, k);
                }
    }

    if (puddlesOn) {   // puddles on level ground, see-through at the edge, growing with the rain; each drop that lands rings out across them
        const GpuMesh* ripple = GpuMeshFor(royale::MeshKind::Ripple, 0);
        {
            OPEN_DISPS(play->state.gfxCtx);
            Gfx_SetupDL_25Xlu(play->state.gfxCtx);
            gSPClearGeometryMode(POLY_XLU_DISP++, G_LIGHTING | G_CULL_BACK);
            gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, SHADE, 0, 0, 0, PRIMITIVE, 0, 0, 0, SHADE, 0, 0, 0, PRIMITIVE);   // vertex colour, our alpha
            CLOSE_DISPS(play->state.gfxCtx);
        }
        const float reach = 950.0f, rippleReach = 650.0f;
        const int c0x = static_cast<int>(std::floor((px - reach) / kPuddleCell)), c1x = static_cast<int>(std::floor((px + reach) / kPuddleCell));
        const int c0z = static_cast<int>(std::floor((pz - reach) / kPuddleCell)), c1z = static_cast<int>(std::floor((pz + reach) / kPuddleCell));
        for (int cz = c0z; cz <= c1z; cz++)
            for (int cx = c0x; cx <= c1x; cx++) {
                if (Flora01(cx, cz, 51) > 0.15f + 0.45f * gPuddleCover) continue;   // the first puddles show early, more join as it soaks in
                const float x = (static_cast<float>(cx) + 0.2f + 0.6f * Flora01(cx, cz, 52)) * kPuddleCell, z = (static_cast<float>(cz) + 0.2f + 0.6f * Flora01(cx, cz, 53)) * kPuddleCell;
                const float d = std::hypot(x - px, z - pz);
                if (d > reach || !inView(x, z, 90.0f)) continue;
                const FloraSpot* spot = FloraSpotAt(3, cx, cz, x, z);
                if (spot == nullptr || !spot->ok) continue;
                const GpuMesh* m = GpuMeshFor(royale::MeshKind::Puddle, FloraHash(cx, cz, 54) % 4 + (season == 3 ? 4u : 0u));   // frozen over in winter
                if (m == nullptr || m->dl.empty()) continue;
                const float size = (0.35f + 0.75f * gPuddleCover) * (0.7f + 0.6f * Flora01(cx, cz, 55));
                const float f = fade(d, reach);
                if (f < 0.02f) continue;
                const float yaw = Flora01(cx, cz, 56) * 6.2831853f;
                DrawGroundXlu(play, m, x, spot->y + 1.0f, z, spot->sx, spot->sz, yaw, size, static_cast<int>(215.0f * f * std::min(1.0f, gPuddleCover * 3.0f)));
                if (!raining || season == 3 || ripple == nullptr || ripple->dl.empty() || d > rippleReach) continue;
                const int rings = 1 + static_cast<int>(rainNow * 3.0f);
                for (int j = 0; j < rings; j++) {   // each ring grows and fades over 0.8 s, then starts again somewhere else on the puddle
                    const float phase = t / 0.8f + Flora01(cx, cz, 60 + j);
                    const int drop = static_cast<int>(std::floor(phase));
                    const float age = phase - static_cast<float>(drop);
                    const float ang = Flora01(cx * 31 + j, cz + drop, 61) * 6.2831853f, rr = 55.0f * size * std::sqrt(Flora01(cx + drop, cz * 17 + j, 62));
                    const float ox = std::cos(ang) * rr, oz = std::sin(ang) * rr;
                    DrawGroundXlu(play, ripple, x + ox, spot->y + 1.6f + spot->sx * ox + spot->sz * oz, z + oz, spot->sx, spot->sz, 0.0f,
                                  0.35f + 1.3f * age, static_cast<int>(190.0f * (1.0f - age) * f));
                }
            }
    }
}

// Trunks are solid: stand against one and you are pushed out of it (the same sort of local solidity the climbing blocks have).
void ApplyTrees(Player* player) {
    if (!InField() || gFoliage <= 0.01f) return;
    const int season = FloraSeason();
    const float px = player->actor.world.pos.x, pz = player->actor.world.pos.z;
    gFloraBudget = 6;
    const float cell = TreeCell();
    for (int cz = static_cast<int>(std::floor((pz - 160.0f) / cell)); cz <= static_cast<int>(std::floor((pz + 160.0f) / cell)); cz++)
        for (int cx = static_cast<int>(std::floor((px - 160.0f) / cell)); cx <= static_cast<int>(std::floor((px + 160.0f) / cell)); cx++) {
            TreeSpot tr;
            if (!TreeIn(cx, cz, season, &tr)) continue;
            const float r = 20.0f * tr.scale + 14.0f, ddx = px - tr.x, ddz = pz - tr.z, d = std::hypot(ddx, ddz);
            if (d < r && player->actor.world.pos.y < tr.y + 160.0f * tr.scale && d > 0.01f) {
                player->actor.world.pos.x = tr.x + ddx / d * r;
                player->actor.world.pos.z = tr.z + ddz / d * r;
            }
        }
}

// ---- the storm wall and the weather, in the world ------------------------------------------------------------------------------
// Where the safe zone's edge stands this frame, set by DriveStorm from the match state and drawn as a wall by DrawStormWall.
royale::Circle gWallZone;
float gWallAlpha = 0.0f;    // fades in when a match starts and out when it ends
float gWallBase = 0.0f;     // roughly where the ground is (the wall reaches well below and far above it)
bool gWallBaseKnown = false;

bool ScreenCovered();   // a menu or pause screen is over the game (defined with the screen drawing)

// Once per game tick: are you standing in the storm (alive, outside the safe zone), and where is the wall?
void DriveStorm(const royale::HudState& h) {
    const bool live = gSession.Joined() && InField() && (h.state == royale::MatchState::Drop || h.state == royale::MatchState::InMatch);
    const bool in = live && h.haveSelf && h.selfAlive && !ScreenCovered() && h.safeZone.radius > 0 && h.stormDamagePerSecond > 0;
    gStormWeather = std::clamp(gStormWeather + (in ? 0.06f : -0.09f), 0.0f, 1.0f);   // about a second to come and go
    const bool wall = live && h.safeZone.radius > 0 && !(h.haveSelf && !h.selfAlive) && !ScreenCovered();
    if (wall) gWallZone = h.safeZone;
    gWallAlpha = std::clamp(gWallAlpha + (wall ? 0.05f : -0.05f), 0.0f, 1.0f);
    if (!InField() || gPlayState == nullptr) { gWallBaseKnown = false; return; }
    Player* pl = GET_PLAYER(gPlayState);
    const float ground = pl->actor.floorHeight > BGCHECK_Y_MIN + 1.0f ? pl->actor.floorHeight : pl->actor.world.pos.y;
    gWallBase = gWallBaseKnown ? gWallBase + (ground - gWallBase) * 0.05f : ground;   // follows slowly, so it never jumps when you jump
    gWallBaseKnown = true;
}

// The edge of the safe zone as a wall of violet storm light all the way round, a little see-through, bright at the foot and fading out high
// up, with bands of light rolling along it. It is drawn without the game's distance fog, so it shows from anywhere on the map (out to the
// game's own draw distance), and the hills and buildings in front of it hide it as they should.
void DrawStormWall(PlayState* play) {
    if (gWallAlpha <= 0.0f || gWallZone.radius <= 0.0f || !gWallBaseKnown) return;
    const float r = gWallZone.radius;
    const float shrink = std::max(1.0f, r / 30000.0f);   // vertex positions are 16 bit: scale them down for a huge circle
    const int segs = std::clamp(static_cast<int>(r / 120.0f), 48, 160);
    const float t = static_cast<float>(ImGui::GetTime());
    constexpr int kRows = 4;
    const float heights[kRows] = { gWallBase - 3000.0f, gWallBase + 250.0f, gWallBase + 2600.0f, gWallBase + 7500.0f };
    const float alphas[kRows] = { 175.0f, 160.0f, 110.0f, 0.0f };
    Vtx* v = static_cast<Vtx*>(Graph_Alloc(play->state.gfxCtx, static_cast<size_t>(segs) * 2 * kRows * sizeof(Vtx)));
    if (v == nullptr) return;
    auto put = [&](Vtx& o, float ang, int row) {
        const float c = std::cos(ang), s = std::sin(ang);
        // Rolling bands of brighter light, running round the wall and drifting upward.
        const float band = 0.5f + 0.5f * std::sin(ang * 23.0f + t * 0.9f + row * 1.3f) * std::sin(ang * 7.0f - t * 0.55f + heights[row] * 0.0007f + t * 0.4f);
        const float k = 0.72f + 0.38f * band;
        o.v.ob[0] = static_cast<s16>(std::lround(c * r / shrink));
        o.v.ob[1] = static_cast<s16>(std::clamp(std::lround(heights[row] / shrink), -32000L, 32000L));
        o.v.ob[2] = static_cast<s16>(std::lround(s * r / shrink));
        o.v.flag = 0;
        o.v.tc[0] = o.v.tc[1] = 0;
        o.v.cn[0] = static_cast<u8>(std::min(255.0f, 150.0f * k + 20.0f * (row == 0)));
        o.v.cn[1] = static_cast<u8>(std::min(255.0f, 70.0f * k));
        o.v.cn[2] = static_cast<u8>(std::min(255.0f, 235.0f * k));
        o.v.cn[3] = static_cast<u8>(std::clamp(alphas[row] * (0.8f + 0.3f * band), 0.0f, 255.0f));
    };
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_XLU_DISP++, G_LIGHTING | G_CULL_BACK | G_CULL_FRONT | G_FOG);   // seen from either side, and not lost in the fog
    gDPSetRenderMode(POLY_XLU_DISP++, G_RM_PASS, G_RM_ZB_XLU_SURF2);
    gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, SHADE, SHADE, 0, PRIMITIVE, 0, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED);   // vertex colour, vertex alpha x our fade
    gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 255, 255, static_cast<u8>(255.0f * gWallAlpha));
    Matrix_Translate(gWallZone.center.x, 0.0f, gWallZone.center.z, MTXMODE_NEW);
    if (shrink > 1.0f) Matrix_Scale(shrink, shrink, shrink, MTXMODE_APPLY);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    for (int i = 0; i < segs; i++) {   // one strip of the wall: two columns of four, three quads high
        const float a0 = 6.2831853f * static_cast<float>(i) / segs, a1 = 6.2831853f * static_cast<float>(i + 1) / segs;
        Vtx* q = &v[i * 2 * kRows];
        for (int row = 0; row < kRows; row++) { put(q[row], a0, row); put(q[kRows + row], a1, row); }
        gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(q), 2 * kRows, 0);
        for (int row = 0; row + 1 < kRows; row++)
            gSP2Triangles(POLY_XLU_DISP++, row, kRows + row, kRows + row + 1, 0, row, kRows + row + 1, row + 1, 0);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// Ash and blowing sand: specks drifting through the air all around the camera, in the world (they pass in front of and behind things, and
// you fly through them), carried by the wind. Embers glow and rise; sand streaks along the wind. "Weather density" sets how many (0: none).
void DrawWeatherParticles(PlayState* play) {
    const royale::Sky sky = gWeatherShown.sky;
    if (sky != royale::Sky::Ash && sky != royale::Sky::Sandstorm) return;
    const float amount = WeatherAmount() * gWeatherDensity;
    if (amount <= 0.02f) return;
    const bool ash = sky == royale::Sky::Ash;
    const int n = std::min(ash ? 220 : 300, static_cast<int>((ash ? 150.0f : 220.0f) * amount));
    if (n <= 0) return;
    const Vec3f eye = play->view.eye;
    const float t = static_cast<float>(ImGui::GetTime());
    float wx, wz, wind;
    WindNow(&wx, &wz, &wind);
    const float wl = std::max(1.0f, std::hypot(wx, wz)), dx = wx / wl, dz = wz / wl;
    const float speed = ash ? 60.0f + 0.25f * wl : 380.0f + 0.9f * wl;
    constexpr float kBox = 1400.0f, kHalf = kBox * 0.5f, kTall = 900.0f;
    auto wrap = [](float x, float size) { x = std::fmod(x, size); return x < 0.0f ? x + size : x; };
    Vtx* v = static_cast<Vtx*>(Graph_Alloc(play->state.gfxCtx, static_cast<size_t>(n) * 8 * sizeof(Vtx)));
    if (v == nullptr) return;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_XLU_DISP++, G_LIGHTING | G_CULL_BACK | G_CULL_FRONT | G_FOG);
    gDPSetRenderMode(POLY_XLU_DISP++, G_RM_PASS, G_RM_ZB_XLU_SURF2);
    gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED);
    Matrix_Translate(eye.x, eye.y, eye.z, MTXMODE_NEW);   // the specks are placed around the camera
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    for (int i = 0; i < n; i++) {
        const float h1 = Flora01(i, 7, 201), h2 = Flora01(i, 11, 202), h3 = Flora01(i, 13, 203), depth = 0.6f + 0.8f * Flora01(i, 17, 204);
        // Each speck drifts along the wind (and up or down) through a box that moves with the camera, wrapping round its sides.
        const float drift = t * speed * depth;
        const float rise = ash ? t * (i % 3 == 0 ? 45.0f : -25.0f) * depth : std::sin(t * 2.0f + i) * 20.0f;
        float x = wrap(h1 * kBox + eye.x + dx * drift, kBox) - kHalf;
        float z = wrap(h2 * kBox + eye.z + dz * drift, kBox) - kHalf;
        float y = wrap(h3 * kTall + eye.y + rise, kTall) - kTall * 0.5f;
        if (ash) { x += std::sin(t * 0.8f + i) * 25.0f; z += std::cos(t * 0.7f + i * 1.7f) * 25.0f; }
        // Two crossed quads, so the speck looks the same from any side: a small square for ash, a thin streak along the wind for sand.
        float ax, ay, az, bx, by, bz, cx, cy, cz;
        if (ash) {
            const float s = (i % 3 == 0 ? 3.0f : 4.5f) * depth;
            ax = 0.0f; ay = s; az = 0.0f; bx = s; by = 0.0f; bz = 0.0f; cx = 0.0f; cy = 0.0f; cz = s;
        } else {
            const float len = 26.0f * depth, th = 1.6f * depth;
            ax = dx * len; ay = 0.0f; az = dz * len; bx = 0.0f; by = th; bz = 0.0f; cx = -dz * th; cy = 0.0f; cz = dx * th;
        }
        u8 r, g, b, a;
        if (ash && i % 3 == 0) { const float glow = 0.75f + 0.25f * std::sin(t * 6.0f + i); r = 255; g = static_cast<u8>(140 * glow); b = 40; a = 235; }   // a glowing ember
        else if (ash) { r = 120; g = 112; b = 108; a = 200; }
        else { r = 232; g = 196; b = 132; a = static_cast<u8>(150 + 60 * h3); }
        Vtx* q = &v[i * 8];
        const float corners[8][3] = { {x - ax - bx, y - ay - by, z - az - bz}, {x + ax - bx, y + ay - by, z + az - bz}, {x + ax + bx, y + ay + by, z + az + bz}, {x - ax + bx, y - ay + by, z - az + bz},
                                      {x - ax - cx, y - ay - cy, z - az - cz}, {x + ax - cx, y + ay - cy, z + az - cz}, {x + ax + cx, y + ay + cy, z + az + cz}, {x - ax + cx, y - ay + cy, z - az + cz} };
        for (int k = 0; k < 8; k++) {
            q[k].v.ob[0] = static_cast<s16>(std::lround(corners[k][0]));
            q[k].v.ob[1] = static_cast<s16>(std::lround(corners[k][1]));
            q[k].v.ob[2] = static_cast<s16>(std::lround(corners[k][2]));
            q[k].v.flag = 0; q[k].v.tc[0] = q[k].v.tc[1] = 0;
            q[k].v.cn[0] = r; q[k].v.cn[1] = g; q[k].v.cn[2] = b; q[k].v.cn[3] = a;
        }
    }
    for (int first = 0; first < n; first += 4) {   // four specks (32 vertices) at a time
        const int count = std::min(4, n - first);
        gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(&v[first * 8]), count * 8, 0);
        for (int k = 0; k < count; k++) {
            const int o = k * 8;
            gSP2Triangles(POLY_XLU_DISP++, o, o + 1, o + 2, 0, o, o + 2, o + 3, 0);
            gSP2Triangles(POLY_XLU_DISP++, o + 4, o + 5, o + 6, 0, o + 4, o + 6, o + 7, 0);
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);
}


// ---- wind streaks and the tornado --------------------------------------------------------------------------------------------------
// Wind streaks: thin pale lines that drift through the air round the camera along the wind, longer, quicker and more of them as it picks up
// (so a storm's squalls show), which is how you see which way it blows. Ash and sand have their own specks, so those skies skip this.
void DrawWindParticles(PlayState* play) {
    if (!gWindStreaks || !gWindOn || !InField()) return;
    if (gWeatherShown.sky == royale::Sky::Ash || gWeatherShown.sky == royale::Sky::Sandstorm) return;
    float wx, wz, wind;
    WindNow(&wx, &wz, &wind);
    if (wind < 0.04f) return;
    const int n = std::min(80, static_cast<int>((14.0f + 70.0f * wind) * std::min(1.5f, gWeatherDensity)));
    if (n <= 0) return;
    const Vec3f eye = play->view.eye;
    const float t = static_cast<float>(ImGui::GetTime());
    const float wl = std::max(1.0f, std::hypot(wx, wz)), dx = wx / wl, dz = wz / wl;
    constexpr float kBox = 1500.0f, kHalf = kBox * 0.5f, kTall = 650.0f;
    auto wrap = [](float x, float size) { x = std::fmod(x, size); return x < 0.0f ? x + size : x; };
    Vtx* v = static_cast<Vtx*>(Graph_Alloc(play->state.gfxCtx, static_cast<size_t>(n) * 4 * sizeof(Vtx)));
    if (v == nullptr) return;
    for (int i = 0; i < n; i++) {
        const float h1 = Flora01(i, 31, 301), h2 = Flora01(i, 37, 302), h3 = Flora01(i, 41, 303), depth = 0.6f + 0.8f * Flora01(i, 43, 304);
        const float drift = t * wl * 1.5f * depth;   // streaks outrun the wind a little so the direction reads at a glance
        const float x = wrap(h1 * kBox + eye.x + dx * drift, kBox) - kHalf;
        const float z = wrap(h2 * kBox + eye.z + dz * drift, kBox) - kHalf;
        const float y = wrap(h3 * kTall + eye.y * 0.0f, kTall) - kTall * 0.35f + std::sin(t * 1.7f + i) * 14.0f;
        const float len = (30.0f + 120.0f * wind) * depth, th = 1.8f * depth;
        const float fade = std::clamp(std::min(wrap(drift * 0.001f + h1, 1.0f), 1.0f - wrap(drift * 0.001f + h1, 1.0f)) * 5.0f, 0.0f, 1.0f);
        const u8 a = static_cast<u8>(std::clamp((55.0f + 110.0f * wind) * fade * (0.6f + 0.4f * depth), 0.0f, 220.0f));
        // A flat ribbon along the wind, tilted a little so it is seen from above and from the side.
        const float ax = dx * len, az = dz * len, bx = -dz * th, bz = dx * th;
        const float corners[4][3] = { {x - ax - bx, y, z - az - bz}, {x + ax - bx, y + th, z + az - bz}, {x + ax + bx, y + th, z + az + bz}, {x - ax + bx, y, z - az + bz} };
        for (int k = 0; k < 4; k++) {
            Vtx& o = v[i * 4 + k];
            o.v.ob[0] = static_cast<s16>(std::lround(corners[k][0])); o.v.ob[1] = static_cast<s16>(std::lround(corners[k][1])); o.v.ob[2] = static_cast<s16>(std::lround(corners[k][2]));
            o.v.flag = 0; o.v.tc[0] = o.v.tc[1] = 0;
            const bool tip = k == 1 || k == 2;   // the head of the streak is brighter than its tail
            o.v.cn[0] = 235; o.v.cn[1] = 242; o.v.cn[2] = 255; o.v.cn[3] = tip ? a : static_cast<u8>(a / 5);
        }
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_XLU_DISP++, G_LIGHTING | G_CULL_BACK | G_CULL_FRONT | G_FOG);
    gDPSetRenderMode(POLY_XLU_DISP++, G_RM_PASS, G_RM_ZB_XLU_SURF2);
    gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED);
    Matrix_Translate(eye.x, eye.y, eye.z, MTXMODE_NEW);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    for (int first = 0; first < n; first += 8) {   // eight streaks (32 vertices) at a time
        const int count = std::min(8, n - first);
        gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(&v[first * 4]), count * 4, 0);
        for (int k = 0; k < count; k++) gSP2Triangles(POLY_XLU_DISP++, k * 4, k * 4 + 1, k * 4 + 2, 0, k * 4, k * 4 + 2, k * 4 + 3, 0);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// The tornado: a funnel that wanders over the ground, drifting a little with the wind (and bigger in storms). It is local scenery for fun, so
// nothing about it is sent to anyone else: you see it, and only you are pulled in. Its physics: inside its reach Link is drawn toward the funnel,
// whirled round it (faster nearer the core) and lifted; high up he is flung out of the top. Its model is built fresh every frame (rings of
// the funnel, twisting and leaning, with a dust skirt at the foot), and loose leaves and grass whirl round it.
struct TornadoState {
    bool active = false;
    float x = 0, z = 0, ground = 0, heading = 0;
    int scene = -1;
};
TornadoState gTornado;
constexpr float kTornadoHeight = 900.0f, kTornadoReach = 460.0f;

float TornadoRadius(float h01) { return 38.0f + 270.0f * std::pow(h01, 1.7f); }   // narrow at the foot, wide at the top
float TornadoSway(float h01, float t, int axis) { return std::sin(t * (1.1f + 0.3f * axis) + h01 * 3.4f + axis * 2.0f) * 70.0f * h01 * h01; }

// Once per game tick (20 a second): it wanders, and if you are within reach it takes hold of you.
void UpdateTornado(Player* player) {
    if (!gTornadoOn || !InField() || gPlayState == nullptr) { gTornado.active = false; return; }
    Feat("tornado");
    const float dt = 1.0f / 20.0f, t = static_cast<float>(ImGui::GetTime());
    const float px = player->actor.world.pos.x, pz = player->actor.world.pos.z;
    if (!gTornado.active || gTornado.scene != gPlayState->sceneNum) {   // (re)spawn some way off, where there is ground
        gTornado.active = false;
        for (int tries = 0; tries < 24; tries++) {
            const float a = Flora01(tries, static_cast<int>(t), 311) * 6.2831853f, d = 900.0f + 500.0f * Flora01(tries, 7, 312);
            const float x = px + std::cos(a) * d, z = pz + std::sin(a) * d;
            float gy = 0;
            if (!RawFloorAt(x, z, &gy)) continue;
            gTornado = { true, x, z, gy, a + 3.14159f, gPlayState->sceneNum };
            break;
        }
        if (!gTornado.active) return;
    }
    float wx, wz, wind;
    WindNow(&wx, &wz, &wind);
    TornadoState& tn = gTornado;
    tn.heading += std::sin(t * 0.23f + tn.x * 0.001f) * 0.35f * dt;                    // a slow, drunken wander
    const float speed = 70.0f + 60.0f * wind;
    const float nx = tn.x + (std::cos(tn.heading) * speed + wx * 0.35f) * dt, nz = tn.z + (std::sin(tn.heading) * speed + wz * 0.35f) * dt;
    float gy = tn.ground;
    if (RawFloorAt(nx, nz, &gy) && std::fabs(gy - tn.ground) < 60.0f) { tn.x = nx; tn.z = nz; tn.ground = gy; }
    else tn.heading += 2.2f;   // a wall, a cliff or the water's edge: it turns away
    if (std::hypot(tn.x - px, tn.z - pz) > 6000.0f) tn.active = false;   // lost far behind: a new one will find you
    // Physics
    const float ddx = px - tn.x, ddz = pz - tn.z, d = std::max(1.0f, std::hypot(ddx, ddz));
    if (d > kTornadoReach) return;
    const float s = std::pow(1.0f - d / kTornadoReach, 1.4f);
    const float ix = -ddx / d, iz = -ddz / d;            // inward
    const float tx = -iz, tz = ix;                       // round it (counter-clockwise)
    Vec3f& pos = player->actor.world.pos;
    const float above = pos.y - tn.ground;
    const float pull = 220.0f * s, whirl = 150.0f + 520.0f * s;
    pos.x += (ix * pull + tx * whirl * s) * dt;
    pos.z += (iz * pull + tz * whirl * s) * dt;
    if (d < kTornadoReach * 0.6f && above < kTornadoHeight) {
        pos.y += 340.0f * s * dt;                                              // lifted up the funnel
        player->actor.velocity.y = std::max(player->actor.velocity.y, 70.0f * s);
    }
    if (above > kTornadoHeight * 0.7f) {                                       // flung out of the top
        pos.x -= ix * 700.0f * dt; pos.z -= iz * 700.0f * dt;
    }
}

void DrawTornado(PlayState* play) {
    if (!gTornado.active || !gTornadoOn || !InField() || gTornado.scene != play->sceneNum) return;
    Feat("tornado draw");
    const float t = static_cast<float>(ImGui::GetTime());
    const Vec3f eye = play->view.eye;
    if (std::hypot(gTornado.x - eye.x, gTornado.z - eye.z) > 5500.0f) return;
    constexpr int kRings = 10, kSegs = 14;
    Vtx* v = static_cast<Vtx*>(Graph_Alloc(play->state.gfxCtx, static_cast<size_t>(kSegs + 1) * kRings * sizeof(Vtx)));
    if (v == nullptr) return;
    for (int i = 0; i < kRings; i++) {
        const float h01 = static_cast<float>(i) / (kRings - 1), h = h01 * kTornadoHeight;
        const float r = TornadoRadius(h01), ox = TornadoSway(h01, t, 0), oz = TornadoSway(h01, t, 1);
        const float spin = t * (6.0f - 3.5f * h01) + h01 * 5.0f;   // the foot whirls fastest, the top lags: the funnel twists
        for (int j = 0; j <= kSegs; j++) {
            const float a = spin + 6.2831853f * j / kSegs;
            const float streak = 0.5f + 0.5f * std::sin(a * 3.0f + h01 * 9.0f - t * 5.0f);   // dark bands spiralling up it
            const float shade = 0.55f + 0.4f * streak;
            Vtx& o = v[i * (kSegs + 1) + j];
            o.v.ob[0] = static_cast<s16>(std::lround(ox + std::cos(a) * r)); o.v.ob[1] = static_cast<s16>(std::lround(h)); o.v.ob[2] = static_cast<s16>(std::lround(oz + std::sin(a) * r));
            o.v.flag = 0; o.v.tc[0] = o.v.tc[1] = 0;
            o.v.cn[0] = static_cast<u8>(150.0f * shade + 20.0f * (1.0f - h01)); o.v.cn[1] = static_cast<u8>(140.0f * shade + 12.0f * (1.0f - h01)); o.v.cn[2] = static_cast<u8>(130.0f * shade);
            o.v.cn[3] = static_cast<u8>(std::clamp((i == 0 ? 120.0f : 175.0f) * (1.0f - 0.55f * h01 * h01) * (0.7f + 0.3f * streak), 0.0f, 255.0f));
        }
    }
    OPEN_DISPS(play->state.gfxCtx);
    // Debris whirling round it: leaves and grass tufts climbing the funnel.
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    CLOSE_DISPS(play->state.gfxCtx);
    for (int i = 0; i < 16; i++) {
        const float life = std::fmod(t * (0.22f + 0.1f * Flora01(i, 3, 321)) + Flora01(i, 5, 322), 1.0f);
        const float h01 = life * 0.85f, r = TornadoRadius(h01) * (0.85f + 0.5f * Flora01(i, 9, 323));
        const float a = t * (5.0f - 2.5f * h01) + i * 0.39f;
        const GpuMesh* m = (i & 1) ? GpuMeshFor(royale::MeshKind::LeafPile, 3u) : GpuMeshFor(royale::MeshKind::Grass, static_cast<uint32_t>(i % 4) + 4u);
        if (m == nullptr || m->dl.empty()) continue;
        DrawFloraMesh(play, m, gTornado.x + TornadoSway(h01, t, 0) + std::cos(a) * r, gTornado.ground + h01 * kTornadoHeight, gTornado.z + TornadoSway(h01, t, 1) + std::sin(a) * r,
                      t * 3.0f + i, t * 2.0f + i, t * 2.6f, (i & 1) ? 0.16f : 0.3f);
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_XLU_DISP++, G_LIGHTING | G_CULL_BACK | G_CULL_FRONT | G_FOG);
    gDPSetRenderMode(POLY_XLU_DISP++, G_RM_PASS, G_RM_ZB_XLU_SURF2);
    gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED);
    Matrix_Translate(gTornado.x, gTornado.ground, gTornado.z, MTXMODE_NEW);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    for (int j = 0; j < kSegs; j++) {   // one strip of the funnel at a time: two columns of rings
        Vtx* strip = static_cast<Vtx*>(Graph_Alloc(play->state.gfxCtx, static_cast<size_t>(2 * kRings) * sizeof(Vtx)));
        if (strip == nullptr) break;
        for (int i = 0; i < kRings; i++) { strip[i] = v[i * (kSegs + 1) + j]; strip[kRings + i] = v[i * (kSegs + 1) + j + 1]; }
        gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(strip), 2 * kRings, 0);
        for (int i = 0; i + 1 < kRings; i++) gSP2Triangles(POLY_XLU_DISP++, i, kRings + i, kRings + i + 1, 0, i, kRings + i + 1, i + 1, 0);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// Rain and snow are the game's own: its rain streaks and its falling snow (the same flakes as its winter holiday mode), switched on and scaled with
// the server's spell of weather and the storm. So are the rest: thunderstorms and the storm zone bring the game's lightning (bolts on the horizon,
// the sky flashing), fog, sand, ash and the storm thicken the game's own distance fog and tint it, and a sandstorm brings the Haunted Wasteland's
// sandstorm. Everything is handed back to the game when the weather clears or the match ends.
void DriveRealWeather() {
    static bool rainManaged = false, snowManaged = false, lightningManaged = false, sandManaged = false, fogManaged = false;
    static float fogNearAdj = 0.0f, fogFarAdj = 0.0f, fogColAdj[3] = { 0.0f, 0.0f, 0.0f };
    if (!InField() || gPlayState == nullptr) {   // the scene is going (or gone): the next one starts from its own settings
        rainManaged = snowManaged = lightningManaged = sandManaged = fogManaged = false;
        fogNearAdj = fogFarAdj = fogColAdj[0] = fogColAdj[1] = fogColAdj[2] = 0.0f;
        return;
    }
    PlayState* play = gPlayState;
    const float w = WeatherAmount();
    const royale::Sky sky = gWeatherShown.sky;
    const bool rainSky = sky == royale::Sky::Rain || sky == royale::Sky::Thunder;
    const float rain = std::max(rainSky ? w : 0.0f, gStormWeather * 0.8f) * gWeatherDensity;
    const float snow = sky == royale::Sky::Snow ? w * gWeatherDensity : 0.0f;
    const int wantRain = std::clamp(static_cast<int>(rain * 70.0f), 0, 130);
    if (wantRain > 0 || (rainManaged && play->envCtx.unk_EE[1] > 0)) {
        if (!rainManaged) Trace("weather: rain on");
        const int cur = play->envCtx.unk_EE[1];
        play->envCtx.unk_EE[1] = static_cast<u8>(cur < wantRain ? std::min(wantRain, cur + 2) : std::max(wantRain, cur - 2));
        rainManaged = true;
    } else rainManaged = false;
    const int wantSnow = std::clamp(static_cast<int>(snow * 40.0f) & ~1, 0, 62);
    if (wantSnow > 0) {
        if (!snowManaged) Trace("weather: snow on");
        snowManaged = true;
        play->envCtx.unk_EE[3] = static_cast<u8>(wantSnow);
        static int tryFrame = 0;
        if (tryFrame++ % 40 == 0) Actor_Spawn(&play->actorCtx, play, ACTOR_OBJECT_KANKYO, 0, 0, 0, 0, 0, 0, 3, false);   // a second one removes itself
    } else if (snowManaged) {
        play->envCtx.unk_EE[3] = 0;   // the flakes thin out by themselves
        if (play->envCtx.unk_EE[2] == 0) snowManaged = false;
    }

    // Lightning: the game strikes at random every few seconds while it is on. It is turned off with "one last strike", which lets a flash
    // that is under way finish (switching straight off would leave the sky lit).
    const bool wantLightning = (sky == royale::Sky::Thunder && w > 0.25f) || gStormWeather > 0.5f;
    if (wantLightning) {
        if (!lightningManaged) Trace("weather: lightning on");
        play->envCtx.lightningMode = LIGHTNING_MODE_ON;
        lightningManaged = true;
    } else if (lightningManaged) {
        if (play->envCtx.lightningMode == LIGHTNING_MODE_ON) play->envCtx.lightningMode = LIGHTNING_MODE_LAST;
        lightningManaged = false;
    }

    // The desert's sandstorm, as in the Haunted Wasteland. "Weather density" 0 leaves it out.
    const bool wantSand = sky == royale::Sky::Sandstorm && w * gWeatherDensity > 0.2f;
    if (wantSand) {
        if (!sandManaged) Trace("weather: sandstorm on");
        if (play->envCtx.sandstormState == SANDSTORM_OFF || play->envCtx.sandstormState == SANDSTORM_DISSIPATE) play->envCtx.sandstormState = SANDSTORM_ACTIVE;
        sandManaged = true;
    } else if (sandManaged) {
        if (play->envCtx.sandstormState != SANDSTORM_OFF) play->envCtx.sandstormState = SANDSTORM_DISSIPATE;   // it fades out and switches itself off
        sandManaged = false;
    }

    // Fog, in the game's own fog: a fog "near" of 996 is no fog at all, lower brings it closer; "far" is where it is solid (and also how far the
    // game draws). Each kind of weather pulls both in and tints the fog its colour; the storm wins where it is thicker.
    float near = 996.0f, far = 12800.0f, amount = 0.0f;
    float col[3] = { 0.0f, 0.0f, 0.0f };
    auto want = [&](float a, float n, float f, int r, int g, int b) {
        if (a <= 0.0f) return;
        const float nn = 996.0f + (n - 996.0f) * a, ff = 12800.0f + (f - 12800.0f) * a;
        if (nn >= near && a <= amount) return;
        near = std::min(near, nn); far = std::min(far, ff);
        amount = std::max(amount, a);
        col[0] = static_cast<float>(r); col[1] = static_cast<float>(g); col[2] = static_cast<float>(b);
    };
    switch (sky) {
        case royale::Sky::Fog: want(w, 900.0f, 3200.0f, 205, 212, 218); break;
        case royale::Sky::Sandstorm: want(w, 930.0f, 4200.0f, 205, 165, 105); break;
        case royale::Sky::Ash: want(w, 960.0f, 6000.0f, 70, 52, 48); break;
        case royale::Sky::Thunder: want(w, 975.0f, 8000.0f, 60, 66, 84); break;
        case royale::Sky::Rain: want(w, 985.0f, 10000.0f, 110, 118, 130); break;
        case royale::Sky::Snow: want(w, 975.0f, 8000.0f, 220, 228, 240); break;
        default: break;
    }
    want(gStormWeather, 940.0f, 5000.0f, 70, 40, 120);   // inside the storm: violet murk
    const EnvLightSettings& base = play->envCtx.lightSettings;
    const float targetNear = amount > 0.0f ? std::min(0.0f, near - static_cast<float>(base.fogNear & 0x3FF)) : 0.0f;
    const float targetFar = amount > 0.0f ? std::min(0.0f, far - static_cast<float>(base.fogFar)) : 0.0f;
    if (amount > 0.0f || fogManaged) {
        if (!fogManaged) Trace("weather: fog on");
        auto toward = [](float cur, float target, float step) { return cur < target ? std::min(target, cur + step) : std::max(target, cur - step); };
        fogNearAdj = toward(fogNearAdj, targetNear, 1.5f);
        fogFarAdj = toward(fogFarAdj, targetFar, 220.0f);
        for (int i = 0; i < 3; i++) {
            const float target = amount > 0.0f ? (col[i] - static_cast<float>(base.fogColor[i])) * std::min(1.0f, amount * 1.2f) : 0.0f;
            fogColAdj[i] = toward(fogColAdj[i], target, 4.0f);
        }
        play->envCtx.adjFogNear = static_cast<s16>(std::lround(fogNearAdj));
        play->envCtx.adjFogFar = static_cast<s16>(std::lround(fogFarAdj));
        for (int i = 0; i < 3; i++) play->envCtx.adjFogColor[i] = static_cast<s16>(std::lround(fogColAdj[i]));
        fogManaged = amount > 0.0f || fogNearAdj != 0.0f || fogFarAdj != 0.0f || fogColAdj[0] != 0.0f || fogColAdj[1] != 0.0f || fogColAdj[2] != 0.0f;
    }
}

// ---- things in flight --------------------------------------------------------------------------------------------------------
// Every arrow, seed, bomb, bombchu and boomerang anybody looses is drawn in flight: when a puppet starts its shooting or throwing pose (or when
// you press B with something to shoot) a projectile leaves from there, along the way they face. One stand-in actor near the player draws them
// all with our own models. The damage itself is the server's business, so this is only for the eyes (and ears).
struct Projectile {
    float x, y, z, vx, vy, vz, life, gravity, spin;
    uint32_t variant;
    bool explodes;
};
std::vector<Projectile> gProjectiles;
Actor* gProjectileActor = nullptr;

// What came out of a chest you can see being opened: the item's real model floats up out of the chest over a few seconds, turning and
// glittering, so you can tell what it was before it is gone (drawn by the same stand-in actor as the projectiles).
struct Reveal { float x, y, z; int gid; float age; royale::Rarity rarity; };
std::vector<Reveal> gReveals;
constexpr float kRevealSeconds = 3.2f;

void SpawnProjectileFrom(royale::ItemId weapon, float x, float y, float z, s16 yaw) {
    using royale::ItemId;
    if (!InField() || gPlayState == nullptr) return;
    const royale::WeaponStats w = royale::WeaponOf(weapon);
    if (!w.ranged) return;
    uint32_t variant = 0;
    float speed = 1700.0f, gravity = 0.0f, spin = 0.0f;
    bool explodes = false;
    u16 sfx = NA_SE_IT_ARROW_SHOT;
    switch (weapon) {
        case ItemId::FireArrows: variant = 1; break;
        case ItemId::IceArrows: variant = 2; break;
        case ItemId::LightArrows: variant = 3; break;
        case ItemId::Slingshot: case ItemId::TripleSlingshot: variant = 4; speed = 1500.0f; sfx = NA_SE_IT_SLING_SHOT; break;
        case ItemId::Bombs: variant = 5; speed = 650.0f; gravity = 760.0f; explodes = true; spin = 6.0f; sfx = NA_SE_IT_BOMB_IGNIT; break;
        case ItemId::Bombchus: variant = 6; speed = 600.0f; explodes = true; sfx = NA_SE_IT_BOMB_IGNIT; break;
        case ItemId::HomingBombchus: variant = 7; speed = 700.0f; explodes = true; sfx = NA_SE_IT_BOMB_IGNIT; break;
        case ItemId::DekuNuts: variant = 8; speed = 1000.0f; spin = 9.0f; sfx = NA_SE_IT_SLING_SHOT; break;
        case ItemId::Boomerang: variant = 9; speed = 900.0f; spin = 20.0f; sfx = NA_SE_IT_BOOMERANG_THROW; break;
        default: break;
    }
    const float base = yaw * (3.14159265f / 32768.0f);
    const int count = weapon == ItemId::TripleSlingshot ? 3 : 1;
    for (int i = 0; i < count; i++) {
        const float a = base + (count == 1 ? 0.0f : (i - 1) * 0.11f);
        Projectile p = { x, y, z, std::sin(a) * speed, gravity > 0 ? 330.0f : 0.0f, std::cos(a) * speed, std::clamp(std::min(w.range, 1500.0f) / speed, 0.28f, 1.2f), gravity, spin, variant, explodes };
        if (gProjectiles.size() < 160) gProjectiles.push_back(p);
    }
    Vec3f at = { x, y, z };
    Audio_PlaySoundGeneral(sfx, &at, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

// Arrows, bombs, bombchus and boomerangs are drawn with the game's own models (the same display lists and skeleton the real arrow, bomb, bombchu and
// boomerang actors use), dressed the way the real actors draw them. The server still decides what they hit; these only fly for the eyes.
// Returns false for the kinds that keep our own little models (seeds and nuts).
bool DrawRealProjectile(PlayState* play, const Projectile& p) {
    const float horiz = std::hypot(p.vx, p.vz);
    const float yaw = std::atan2(p.vx, p.vz);
    const float pitchUp = std::atan2(p.vy, horiz);
    constexpr float kScale = 0.01f;   // the game's actors are drawn at a hundredth
    switch (p.variant) {
        case 0: case 1: case 2: case 3: {
            static SkelAnime arrow;
            static bool ready = false;
            if (!ready) {
                SkelAnime_Init(play, &arrow, (SkeletonHeader*)gArrowSkel, (AnimationHeader*)gArrow2Anim, nullptr, nullptr, 0);
                Animation_PlayOnce(&arrow, (AnimationHeader*)gArrow1Anim);
                SkelAnime_Update(&arrow);
                ready = true;
            }
            OPEN_DISPS(play->state.gfxCtx);
            Gfx_SetupDL_25Opa(play->state.gfxCtx);
            // Like the real arrow: yawed to its heading, then pitched about X by atan2(speed, -vy) (the model's long axis is Y).
            Matrix_Translate(p.x, p.y, p.z, MTXMODE_NEW);
            Matrix_RotateY(yaw, MTXMODE_APPLY);
            Matrix_RotateX(1.5707963f - pitchUp, MTXMODE_APPLY);
            Matrix_Scale(kScale, kScale, kScale, MTXMODE_APPLY);
            SkelAnime_DrawLod(play, arrow.skeleton, arrow.jointTable, nullptr, nullptr, nullptr, 0);
            CLOSE_DISPS(play->state.gfxCtx);
            if (p.variant != 0 && play->gameplayFrames % 2 == 0) {   // the elemental arrows leave a trail of sparks in their colour
                static const Color_RGBA8 prim[4] = { {255, 255, 255, 255}, {255, 170, 40, 255}, {170, 240, 255, 255}, {255, 255, 150, 255} };
                static const Color_RGBA8 env[4] = { {255, 255, 255, 255}, {255, 40, 0, 255}, {40, 120, 255, 255}, {255, 230, 80, 255} };
                Vec3f pos = { p.x, p.y, p.z }, vel = { 0.0f, 0.2f, 0.0f }, accel = { 0.0f, 0.0f, 0.0f };
                Color_RGBA8 pc = prim[p.variant], ec = env[p.variant];
                EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &pc, &ec, 120, 14);
            }
            return true;
        }
        case 5: {   // a bomb: the cap and the body, blinking, always facing the camera like the real one
            OPEN_DISPS(play->state.gfxCtx);
            Gfx_SetupDL_25Opa(play->state.gfxCtx);
            Matrix_Translate(p.x, p.y, p.z, MTXMODE_NEW);
            Matrix_ReplaceRotation(&play->billboardMtxF);
            Matrix_Scale(kScale, kScale, kScale, MTXMODE_APPLY);
            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gBombCapDL);
            Matrix_RotateZYX(0x4000, 0, 0, MTXMODE_APPLY);
            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            const int flash = static_cast<int>(120.0f + 120.0f * std::sin(p.spin * 3.0f));
            gDPPipeSync(POLY_OPA_DISP++);
            gDPSetEnvColor(POLY_OPA_DISP++, flash, 0, 40, 255);
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, flash, 0, 40, 255);
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gBombBodyDL);
            CLOSE_DISPS(play->state.gfxCtx);
            return true;
        }
        case 6: case 7: {   // a bombchu, blinking red like the real one
            OPEN_DISPS(play->state.gfxCtx);
            Gfx_SetupDL_25Opa(play->state.gfxCtx);
            const float blink = 0.5f + 0.5f * std::sin(p.spin * 4.0f);
            gDPSetEnvColor(POLY_OPA_DISP++, static_cast<int>(9.0f + blink * 209.0f), static_cast<int>(9.0f + blink * 34.0f), static_cast<int>(35.0f - blink * 35.0f), 255);
            Matrix_Translate(p.x, p.y, p.z, MTXMODE_NEW);
            Matrix_RotateY(yaw, MTXMODE_APPLY);
            Matrix_RotateX(-pitchUp, MTXMODE_APPLY);
            Matrix_Scale(kScale, kScale, kScale, MTXMODE_APPLY);
            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gBombchuDL);
            CLOSE_DISPS(play->state.gfxCtx);
            return true;
        }
        case 4: case 8: {   // a slingshot seed or a thrown deku nut: the game draws both as a spinning, pulsing glint (EnArrow_Draw)
            const bool seed = p.variant == 4;
            const u8 alpha = static_cast<u8>(Math_CosS(static_cast<s16>(p.spin * 5000.0f)) * 127.5f + 127.5f);
            const float scale = (seed ? 50.0f : 150.0f) * kScale;
            OPEN_DISPS(play->state.gfxCtx);
            Gfx_SetupDL_25Xlu2(play->state.gfxCtx);
            if (seed) {
                gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 255, 255, 255);
                gDPSetEnvColor(POLY_XLU_DISP++, 0, 255, 255, alpha);
            } else {
                gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 12, 0, 0, 255);
                gDPSetEnvColor(POLY_XLU_DISP++, 250, 250, 0, alpha);
            }
            Matrix_Translate(p.x, p.y, p.z, MTXMODE_NEW);
            Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
            Matrix_RotateZ(((play->gameplayFrames & 0xFF) * 4000) * (3.14159265f / 0x8000), MTXMODE_APPLY);
            Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gEffSparklesDL);
            CLOSE_DISPS(play->state.gfxCtx);
            return true;
        }
        case 9: {   // the boomerang, spinning flat as in the game
            OPEN_DISPS(play->state.gfxCtx);
            Gfx_SetupDL_25Opa(play->state.gfxCtx);
            Matrix_Translate(p.x, p.y, p.z, MTXMODE_NEW);
            Matrix_RotateY(yaw, MTXMODE_APPLY);
            Matrix_RotateZ(0x1F40 * (3.14159265f / 0x8000), MTXMODE_APPLY);
            Matrix_RotateY(p.spin * 5.0f, MTXMODE_APPLY);
            Matrix_Scale(kScale, kScale, kScale, MTXMODE_APPLY);
            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gBoomerangRefDL);
            CLOSE_DISPS(play->state.gfxCtx);
            return true;
        }
        default: return false;
    }
}

int GidFor(royale::ItemId id);
float GidScale(int gid);
constexpr int kGidGrenade = 1000;   // not one of the game's models: the Shockwave Grenade's own (shared/meshes.h), see DrawItemModel

// An item's model with the current matrix: the game's own (GetItem_Draw), or one of ours. Ours are built standing on y 0, so they are
// lifted to be centred like the game's.
void DrawItemModel(PlayState* play, int gid) {
    if (gid != kGidGrenade) { GetItem_Draw(play, static_cast<s16>(gid)); return; }
    const GpuMesh* mesh = GpuMeshFor(royale::MeshKind::Grenade, 0);
    if (mesh == nullptr || mesh->dl.empty()) return;
    Matrix_Push();
    Matrix_Translate(0.0f, -15.5f, 0.0f, MTXMODE_APPLY);
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);   // colours are baked into the vertices (the purple bands unshaded, so they glow)
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(mesh->dl.data()));
    CLOSE_DISPS(play->state.gfxCtx);
    Matrix_Pop();
}
void Sparkle(PlayState* play, const Vec3f& at, royale::Rarity rarity);

// What each other player last picked up, so it can be held up over their head while they show it off, as Link does.
struct HeldFind { int gid; double at; };
std::unordered_map<uint16_t, HeldFind> gLastFind;
constexpr double kHeldFindSeconds = 4.0;   // a find is only shown if the pose comes this soon after taking it

void DrawHeldFinds(PlayState* play) {
    const double now = ImGui::GetTime();
    for (const auto& [id, actor] : gActorOf) {
        auto f = gLastFind.find(id);
        auto st = gState.find(id);
        if (f == gLastFind.end() || st == gState.end() || actor == nullptr || f->second.gid < 0) continue;
        if (st->second.anim != static_cast<uint8_t>(royale::Anim::ItemGet) || now - f->second.at > kHeldFindSeconds) continue;
        const float size = actor->scale.y / 0.01f;   // grown players hold it higher
        const float k = GidScale(f->second.gid) * 0.032f * size;   // the size the chest reveal settles at
        OPEN_DISPS(play->state.gfxCtx);
        Matrix_Translate(actor->world.pos.x, actor->world.pos.y + 62.0f * size, actor->world.pos.z, MTXMODE_NEW);
        Matrix_RotateY(BINANG_TO_RAD(actor->shape.rot.y), MTXMODE_APPLY);
        Matrix_Scale(k, k, k, MTXMODE_APPLY);
        DrawItemModel(play, f->second.gid);
        CLOSE_DISPS(play->state.gfxCtx);
    }
}

void Projectile_Draw(Actor*, PlayState* play) {
    const float dt = std::min(0.05f, ImGui::GetIO().DeltaTime);
    Feat("draw: foliage and puddles");
    if (DebugOn(kDbgFoliage)) DrawFlora(play);
    Feat("draw: storm wall");
    if (DebugOn(kDbgStormWall)) DrawStormWall(play);
    Feat("draw: weather particles");
    if (DebugOn(kDbgWeather)) DrawWeatherParticles(play);
    Feat("draw: wind streaks");
    if (DebugOn(kDbgWind)) DrawWindParticles(play);
    Feat("draw: tornado");
    if (DebugOn(kDbgTornado)) DrawTornado(play);
    Feat("draw: held finds");
    DrawHeldFinds(play);
    Feat("draw: chest reveals and projectiles");
    if (!DebugOn(kDbgProjectiles)) return;
    for (size_t i = 0; i < gReveals.size();) {
        Reveal& r = gReveals[i];
        r.age += dt;
        if (r.age >= kRevealSeconds) { gReveals.erase(gReveals.begin() + static_cast<long>(i)); continue; }
        const float t = r.age / kRevealSeconds;
        const float ease = 1.0f - (1.0f - t) * (1.0f - t);          // quick at first, then settling
        const float grow = std::min(1.0f, r.age / 0.6f);
        const Vec3f at = { r.x, r.y + 10.0f + ease * 70.0f, r.z };
        Sparkle(play, at, r.rarity);
        const float k = GidScale(r.gid) * 0.0016f * (0.4f + 0.6f * grow) * 1.5f;
        OPEN_DISPS(play->state.gfxCtx);
        Matrix_Translate(at.x, at.y, at.z, MTXMODE_NEW);
        Matrix_RotateY(r.age * 2.4f, MTXMODE_APPLY);
        Matrix_Scale(k * 20.0f, k * 20.0f, k * 20.0f, MTXMODE_APPLY);
        DrawItemModel(play, r.gid);
        CLOSE_DISPS(play->state.gfxCtx);
        i++;
    }
    for (size_t i = 0; i < gProjectiles.size();) {
        Projectile& p = gProjectiles[i];
        p.x += p.vx * dt; p.y += p.vy * dt; p.z += p.vz * dt;
        p.vy -= p.gravity * dt;
        p.life -= dt;
        p.spin += dt * 14.0f;
        float floorY = p.y - 1.0f;
        const bool landed = RawFloorAt(p.x, p.z, &floorY) && p.y <= floorY + 4.0f;
        if (p.life <= 0.0f || landed) {
            if (p.explodes && gPlayState != nullptr) {
                Vec3f pos = { p.x, std::max(p.y, floorY) + 10.0f, p.z }, vel = { 0, 0, 0 }, accel = { 0, 0, 0 };
                EffectSsBomb2_SpawnLayered(play, &pos, &vel, &accel, 40, 10);
                Audio_PlaySoundGeneral(NA_SE_IT_BOMB_EXPLOSION, &pos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            }
            gProjectiles.erase(gProjectiles.begin() + static_cast<long>(i));
            continue;
        }
        if (DrawRealProjectile(play, p)) { i++; continue; }
        const GpuMesh* mesh = GpuMeshFor(royale::MeshKind::Projectile, p.variant);
        if (mesh != nullptr && !mesh->dl.empty()) {
            const float horiz = std::hypot(p.vx, p.vz);
            OPEN_DISPS(play->state.gfxCtx);
            Gfx_SetupDL_25Opa(play->state.gfxCtx);
            Matrix_Translate(p.x, p.y, p.z, MTXMODE_NEW);
            Matrix_RotateY(std::atan2(p.vx, p.vz), MTXMODE_APPLY);
            Matrix_RotateX(-std::atan2(p.vy, horiz), MTXMODE_APPLY);
            if (p.variant == 4 || p.variant == 5 || p.variant == 8 || p.variant == 9) Matrix_RotateZ(p.spin, MTXMODE_APPLY);
            Matrix_Scale(1.25f, 1.25f, 1.25f, MTXMODE_APPLY);
            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
            gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
            gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(mesh->dl.data()));
            CLOSE_DISPS(play->state.gfxCtx);
        }
        i++;
    }
}
void Projectile_Update(Actor* actor, PlayState* play) {
    Player* p = GET_PLAYER(play);
    actor->world.pos = p->actor.world.pos;   // stay near the player so the actor is never skipped
    actor->focus.pos = actor->world.pos;
}
void Projectile_Destroy(Actor* actor, PlayState*) { if (gProjectileActor == actor) gProjectileActor = nullptr; }

void ReconcileProjectileActor() {
    if (InField() && gProjectileActor == nullptr && gPlayState != nullptr) {
        Player* p = GET_PLAYER(gPlayState);
        Actor* a = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ISHI, p->actor.world.pos.x, p->actor.world.pos.y, p->actor.world.pos.z, 0, 0, 0, 0, false);
        if (a == nullptr) return;
        a->update = Projectile_Update;
        a->draw = Projectile_Draw;
        a->destroy = Projectile_Destroy;
        a->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
        a->uncullZoneForward = 9000.0f; a->uncullZoneScale = 9000.0f; a->uncullZoneDownward = 9000.0f;
        a->shape.shadowScale = 0.0f;
        gProjectileActor = a;
    } else if (!InField() && gProjectileActor != nullptr) {
        gProjectileActor = nullptr;   // the scene is changing: the actor goes with it
        gProjectiles.clear();
    }
}

// Which props are part of a place (inside a town's radius): they are spawned ahead of loose scenery and from further away.
std::vector<uint8_t> gPropInTown;
const royale::Prop* gTownSrc = nullptr;
size_t gTownSrcCount = 0, gTownPoiCount = 0;
void RefreshPropTowns(const std::vector<royale::Prop>& props) {
    const auto& pois = gSession.Client()->Pois();
    if (props.data() == gTownSrc && props.size() == gTownSrcCount && pois.size() == gTownPoiCount && gPropInTown.size() == props.size()) return;
    gTownSrc = props.data(); gTownSrcCount = props.size(); gTownPoiCount = pois.size();
    gPropInTown.assign(props.size(), 0);
    for (size_t i = 0; i < props.size(); i++) {
        if (royale::IsPlatform(props[i].kind) || props[i].kind == royale::PropKind::Roof) { gPropInTown[i] = 1; continue; }
        for (const royale::Poi& poi : pois)
            if (std::hypot(props[i].pos.x - poi.center.x, props[i].pos.z - poi.center.z) < poi.radius + 160.0f) { gPropInTown[i] = 1; break; }
    }
}

// Keep the nearest scenery alive around the player, the same for everyone because the list comes from the host. The game can only hold so
// many of our actors (kMaxPropActors), so they go to the props that matter most: the pieces of a town count as much nearer than they are, so a
// place is always whole (walls, roofs, climbs) before the loose rocks and bushes round it, and when the budget is full the farthest loose
// prop is put away to make room for a nearer one. (Before, props were taken in list order, and on the smaller maps the loose scenery used up
// the whole budget, so the towns never appeared at all.)
void ReconcileProps(const royale::HudState& hud) {
    const bool show = gSession.Joined() && InField() && hud.state != royale::MatchState::Lobby;
    if (!show) { ClearProps(); return; }
    const auto& props = gSession.Client()->Props();
    Player* player = GET_PLAYER(gPlayState);
    const float px = player->actor.world.pos.x, pz = player->actor.world.pos.z;
    if (CustomSceneryOn() != gSceneryWasCustom) { // the player flipped the setting: respawn everything the other way
        gSceneryWasCustom = CustomSceneryOn();
        ClearProps();
        return;
    }
    for (size_t broken : gSession.Client()->BrokenProps()) { // somebody else broke these: they are gone for everyone
        gBrokenProps.insert(broken);
        auto it = gProps.find(broken);
        if (it != gProps.end() && gCulledProps.insert(broken).second) Actor_Kill(it->second.actor);
    }
    RefreshPropTowns(props);
    // Rank everything in reach: distance squared, a town's pieces counted at about half their distance.
    struct Want { float key; size_t i; };
    static std::vector<Want> wants;
    static std::vector<uint8_t> wanted;
    wants.clear();
    wanted.assign(props.size(), 0);
    for (size_t i = 0; i < props.size(); i++) {
        if (gBrokenProps.count(i)) continue;
        const float dx = props[i].pos.x - px, dz = props[i].pos.z - pz, d2 = dx * dx + dz * dz;
        const bool town = gPropInTown[i] != 0;
        const float reach = town ? kTownSpawnRadius : kPropSpawnRadius;
        if (d2 > reach * reach) continue;
        wants.push_back({town ? d2 * 0.25f : d2, i});
    }
    const bool full = wants.size() > kMaxPropActors;
    if (full) {
        std::nth_element(wants.begin(), wants.begin() + kMaxPropActors, wants.end(), [](const Want& a, const Want& b) { return a.key < b.key; });
        wants.resize(kMaxPropActors);
    }
    for (const Want& w : wants) wanted[w.i] = 1;
    // Put away what is out of reach, and, when there are more wanted than the budget holds, whatever didn't make the cut.
    for (auto& [i, pa] : gProps) {
        if (i < wanted.size() && wanted[i]) continue;
        const float dx = i < props.size() ? props[i].pos.x - px : 1e9f, dz = i < props.size() ? props[i].pos.z - pz : 1e9f;
        const float reach = (i < gPropInTown.size() && gPropInTown[i]) ? kTownSpawnRadius : kPropSpawnRadius;
        if ((full || dx * dx + dz * dz > reach * reach * 1.4f) && gCulledProps.insert(i).second) Actor_Kill(pa.actor);
    }
    // Spawn the missing ones, nearest (by rank) first, a few a frame.
    std::sort(wants.begin(), wants.end(), [](const Want& a, const Want& b) { return a.key < b.key; });
    int spawned = 0;
    for (const Want& w : wants) {
        if (spawned >= 6 || gProps.size() >= kMaxPropActors) break;
        if (gProps.find(w.i) != gProps.end()) continue;
        float y;
        if (!RawFloorAt(props[w.i].pos.x, props[w.i].pos.z, &y)) continue;
        SpawnProp(w.i, props[w.i], y);
        spawned++;
    }
}

// The game's own 3D model for each item, so what lies on the ground is the real thing (a bow, a hookshot, a Zora tunic...) and not a
// stand-in gem. -1 means the game has no model for it and the stand-in stays.
int GidFor(royale::ItemId id) {
    using royale::ItemId;
    switch (id) {
        case ItemId::DekuStick: return GID_STICK;
        case ItemId::KokiriSword: case ItemId::MasterSword: case ItemId::BasicSword: return GID_SWORD_KOKIRI;
        case ItemId::BiggoronSword: return GID_SWORD_BGS;
        case ItemId::MegatonHammer: case ItemId::GiantsHammer: return GID_HAMMER;
        case ItemId::Slingshot: case ItemId::TripleSlingshot: return GID_SLINGSHOT;
        case ItemId::FairyBow: return GID_BOW;
        case ItemId::Boomerang: return GID_BOOMERANG;
        case ItemId::Bombs: case ItemId::BombAmmo: return GID_BOMB;
        case ItemId::ShockwaveGrenade: return kGidGrenade;
        case ItemId::Bombchus: case ItemId::HomingBombchus: case ItemId::BombchuAmmo: return GID_BOMBCHU;
        case ItemId::DekuNuts: case ItemId::NutAmmo: return GID_NUTS;
        case ItemId::FireArrows: return GID_ARROW_FIRE;
        case ItemId::IceArrows: return GID_ARROW_ICE;
        case ItemId::LightArrows: return GID_ARROW_LIGHT;
        case ItemId::ArrowAmmo: return GID_ARROWS_MEDIUM;
        case ItemId::SeedAmmo: return GID_SEEDS;
        case ItemId::DekuShield: return GID_SHIELD_DEKU;
        case ItemId::HylianShield: return GID_SHIELD_HYLIAN;
        case ItemId::MirrorShield: return GID_SHIELD_MIRROR;
        case ItemId::GreenPotion: case ItemId::LargeShieldPotion: return GID_POTION_GREEN;
        case ItemId::RedPotion: return GID_POTION_RED;
        case ItemId::BluePotion: case ItemId::SmallShieldPotion: return GID_POTION_BLUE;
        case ItemId::Fairy: return GID_FAIRY;
        case ItemId::Milk: return GID_MILK;
        case ItemId::Fish: return GID_FISH;
        case ItemId::BlueFire: return GID_BLUE_FIRE;
        case ItemId::Bug: return GID_BUG;
        case ItemId::Poe: return GID_POE;
        case ItemId::RecoveryHeart: return GID_HEART;
        case ItemId::HeartPiece: return GID_HEART_PIECE;
        case ItemId::HeartContainer: return GID_HEART_CONTAINER;
        case ItemId::MagicJar: return GID_MAGIC_LARGE;
        case ItemId::DinsFire: return GID_DINS_FIRE;
        case ItemId::FaroresWind: return GID_FARORES_WIND;
        case ItemId::NayrusLove: return GID_NAYRUS_LOVE;
        case ItemId::Hookshot: return GID_HOOKSHOT;
        case ItemId::Longshot: return GID_LONGSHOT;
        case ItemId::LensOfTruth: return GID_LENS;
        case ItemId::MagicBeans: return GID_BEAN;
        case ItemId::FairyOcarina: return GID_OCARINA_FAIRY;
        case ItemId::OcarinaOfTime: return GID_OCARINA_TIME;
        case ItemId::ZeldasLullaby: return GID_SONG_ZELDA;
        case ItemId::EponasSong: return GID_SONG_EPONA;
        case ItemId::SariasSong: return GID_SONG_SARIA;
        case ItemId::SunsSong: return GID_SONG_SUN;
        case ItemId::SongOfTime: return GID_SONG_TIME;
        case ItemId::SongOfStorms: return GID_SONG_STORM;
        case ItemId::MinuetOfForest: return GID_SONG_MINUET;
        case ItemId::BoleroOfFire: return GID_SONG_BOLERO;
        case ItemId::SerenadeOfWater: return GID_SONG_SERENADE;
        case ItemId::NocturneOfShadow: return GID_SONG_NOCTURNE;
        case ItemId::RequiemOfSpirit: return GID_SONG_REQUIEM;
        case ItemId::PreludeOfLight: return GID_SONG_PRELUDE;
        case ItemId::KokiriTunic: case ItemId::GoronTunic: return GID_TUNIC_GORON;
        case ItemId::ZoraTunic: return GID_TUNIC_ZORA;
        case ItemId::KokiriBoots: case ItemId::IronBoots: return GID_BOOTS_IRON;
        case ItemId::HoverBoots: return GID_BOOTS_HOVER;
        case ItemId::GoronBracelet: return GID_BRACELET;
        case ItemId::SilverGauntlets: return GID_GAUNTLETS_SILVER;
        case ItemId::GoldenGauntlets: return GID_GAUNTLETS_GOLD;
        case ItemId::KeatonMask: return GID_MASK_KEATON;
        case ItemId::SkullMask: return GID_MASK_SKULL;
        case ItemId::SpookyMask: return GID_MASK_SPOOKY;
        case ItemId::BunnyHood: return GID_MASK_BUNNY;
        case ItemId::GoronMask: return GID_MASK_GORON;
        case ItemId::ZoraMask: return GID_MASK_ZORA;
        case ItemId::GerudoMask: return GID_MASK_GERUDO;
        case ItemId::MaskOfTruth: return GID_MASK_TRUTH;
        case ItemId::SilverScale: return GID_SCALE_SILVER;
        case ItemId::GoldenScale: return GID_SCALE_GOLDEN;
        case ItemId::BigQuiver: return GID_QUIVER_50;
        case ItemId::BulletBag: return GID_BULLET_BAG_50;
        case ItemId::BombBag: return GID_BOMB_BAG_40;
        case ItemId::ForestMedallion: return GID_MEDALLION_FOREST;
        case ItemId::FireMedallion: return GID_MEDALLION_FIRE;
        case ItemId::WaterMedallion: return GID_MEDALLION_WATER;
        case ItemId::SpiritMedallion: return GID_MEDALLION_SPIRIT;
        case ItemId::ShadowMedallion: return GID_MEDALLION_SHADOW;
        case ItemId::LightMedallion: return GID_MEDALLION_LIGHT;
        case ItemId::KokiriEmerald: return GID_KOKIRI_EMERALD;
        case ItemId::GoronRuby: return GID_GORON_RUBY;
        case ItemId::ZoraSapphire: return GID_ZORA_SAPPHIRE;
        default: return -1;   // rupees keep the game's own rupee drawing
    }
}

// How big a model is drawn (the game's own item drops use 25; long things are drawn smaller so they do not tower over the player).
float GidScale(int gid) {
    switch (gid) {
        case GID_SWORD_KOKIRI: case GID_SWORD_BGS: case GID_HAMMER: case GID_BOW: case GID_HOOKSHOT: case GID_LONGSHOT: return 17.0f;
        case GID_TUNIC_GORON: case GID_TUNIC_ZORA: case GID_SHIELD_DEKU: case GID_SHIELD_HYLIAN: case GID_SHIELD_MIRROR: return 20.0f;
        default: return 25.0f;
    }
}

void Loot_Draw(Actor* actor, PlayState* play) {
    auto idx = gLootOf.find(actor);
    if (idx == gLootOf.end()) return;
    const LootActor& la = gLoot[idx->second];
    if (la.gid < 0) return;
    func_8002EBCC(actor, play, 0);   // the same highlights the game's own 3D drops get
    func_8002ED80(actor, play, 0);
    const float k = GidScale(la.gid);
    Matrix_Scale(k, k, k, MTXMODE_APPLY);
    DrawItemModel(play, la.gid);
}

void SpawnLoot(size_t index, const royale::net::LootNet& l, float groundY) {
    if (l.chest) { SpawnChest(index, l, groundY); return; } // generated loot is in chests; only dropped items lie on the ground
    royale::Rarity rarity = static_cast<royale::Rarity>(l.rarity);
    gSpawningLoot = true;
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ITEM00, l.x, groundY + 22.0f, l.z, 0, 0, 0, DropTypeFor(static_cast<royale::ItemId>(l.item), rarity, l.amount), false);
    gSpawningLoot = false;
    if (actor == nullptr) return;
    // Keep the game's rupee model and drawing, but replace its behaviour: no vanilla pickup, our own spin and server-checked grab.
    actor->update = Loot_Update;
    actor->destroy = Loot_Destroy;
    gLoot[index] = { actor, groundY, 0 };
    gLoot[index].rarity = rarity;
    gLoot[index].gid = GidFor(static_cast<royale::ItemId>(l.item));
    if (gLoot[index].gid >= 0) {   // the item's real model instead of the stand-in
        actor->draw = Loot_Draw;
        Actor_SetScale(actor, 0.03f);
    }
    gLootOf[actor] = index;
    std::string label = LootLabel(l);
    NameTag_RegisterForActorWithOptions(actor, label.c_str(), NameTagOptions{ "royale-loot", 26, RarityColor(rarity) });
}

void ClearLoot() {
    for (auto& [idx, la] : gLoot) Actor_Kill(la.actor);
}

// Keep a handful of actors alive for the loot near the player, and none for loot that is taken or far away.
void ReconcileLoot(const royale::HudState& hud) {
    bool show = gSession.Joined() && InField() &&
                (hud.state == royale::MatchState::Countdown || hud.state == royale::MatchState::Drop || hud.state == royale::MatchState::InMatch);
    if (!show) {
        ClearLoot();
        return;
    }
    const auto& loot = gSession.Client()->Loot();
    Player* player = GET_PLAYER(gPlayState);
    const float px = player->actor.world.pos.x, pz = player->actor.world.pos.z;

    for (auto it = gLoot.begin(); it != gLoot.end();) {
        const size_t idx = it->first;
        LootActor& la = it->second;
        const bool known = idx < loot.size();
        const float dx = known ? loot[idx].x - px : 1e9f, dz = known ? loot[idx].z - pz : 1e9f;
        const bool far = dx * dx + dz * dz > kLootSpawnRadius * kLootSpawnRadius * 1.5f;
        if (la.chest) {
            // Chests clean up their own entry when the game destroys them (they have collision to remove first).
            if (known && loot[idx].taken && !la.opened) {
                OpenChest(la);
                const int gid = GidFor(static_cast<royale::ItemId>(loot[idx].item));
                if (gid >= 0 && dx * dx + dz * dz < 1800.0f * 1800.0f && gReveals.size() < 8) gReveals.push_back({ la.actor->world.pos.x, la.actor->world.pos.y + 25.0f, la.actor->world.pos.z, gid, 0.0f, la.rarity });
            }
            if ((!known || far) && !la.killing) { la.killing = true; Actor_Kill(la.actor); }
            ++it;
        } else if (!known || loot[idx].taken || far) {
            Actor_Kill(la.actor);
            gLootOf.erase(la.actor);
            it = gLoot.erase(it);
        } else {
            ++it;
        }
    }

    if (gLoot.size() >= kMaxLootActors) return;
    int spawnedThisFrame = 0;
    for (size_t i = 0; i < loot.size() && spawnedThisFrame < 4 && gLoot.size() < kMaxLootActors; i++) {
        if ((loot[i].taken && !loot[i].chest) || gLoot.find(i) != gLoot.end()) continue; // opened chests stay, standing open
        float dx = loot[i].x - px, dz = loot[i].z - pz;
        if (dx * dx + dz * dz > kLootSpawnRadius * kLootSpawnRadius) continue;
        float y;
        if (!FloorAt(loot[i].x, loot[i].z, &y)) continue; // nothing to stand on there
        SpawnLoot(i, loot[i], y);
        spawnedThisFrame++;
    }
}

constexpr size_t kNoLoot = SIZE_MAX;

// The closest thing the player could take or open right now (an unopened chest within reach, or an item on the ground).
size_t NearestLootIndex() {
    if (!InField() || gSession.Client() == nullptr) return kNoLoot;
    const auto& loot = gSession.Client()->Loot();
    Player* pl = GET_PLAYER(gPlayState);
    size_t best = kNoLoot;
    float bestD = 1e18f;
    for (const auto& [idx, la] : gLoot) {
        if (idx >= loot.size() || loot[idx].taken || la.opened) continue;
        const float reach = la.chest ? kChestOpenRange : kLootPickupRange;
        const float dx = la.actor->world.pos.x - pl->actor.world.pos.x, dz = la.actor->world.pos.z - pl->actor.world.pos.z;
        const float d2 = dx * dx + dz * dz;
        if (d2 <= reach * reach && d2 < bestD && std::fabs(pl->actor.world.pos.y - la.baseY) < 120.0f) { bestD = d2; best = idx; }
    }
    return best;
}

struct PickupNote {
    std::string text;
    royale::Rarity rarity;
};
std::deque<PickupNote> gPickupLog;                    // newest first, shown (colour coded) in the menu
std::string gBannerText;                              // the big "you got" line above the hotbar
royale::Rarity gBannerRarity = royale::Rarity::Common;
double gBannerUntil = 0;

void NotePickup(const std::string& label, royale::Rarity rarity, bool fromChest) {
    gPickupLog.push_front({ (fromChest ? "Chest: " : "") + label, rarity });
    while (gPickupLog.size() > 10) gPickupLog.pop_back();
    gBannerText = label;
    gBannerRarity = rarity;
    gBannerUntil = ImGui::GetTime() + 2.5;
}

std::string ShortName(royale::ItemId id) {
    std::string n = ItemName(id);
    return n.size() > 15 ? n.substr(0, 14) + "." : n;
}

// ---- the always-on HUD ------------------------------------------------------------------------------------------------------

std::string ClockText(float seconds) {
    int s = static_cast<int>(std::ceil(std::max(0.0f, seconds)));
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

// Drawn straight onto the screen every frame, whether or not the menu is open: alive count, storm timer, a pointer to the
// safe zone, what you hold, and the big banners for countdown, elimination and results.
bool gSkydiving = false;   // falling in from the sky at the start of the match (see UpdateSkydive)
bool gDiveHeld = false;    // Z is held to dive. The game never sees that Z (see OnEmoteWheelInput), so it can't lock on and flatten the camera

// Sprinting, the Fortnite way: click the left stick while running and Link runs faster, draining a stamina bar under the magic meter.
// It stops when you let go of the stick, click again or run dry, and the bar refills after a short rest. The N64 pad has no stick
// click, so it is read from SDL directly. The game's own top run speed is raised (patches/0011), so Link's legs, footsteps and turning
// keep up with it; others see the Sprint pose (see ClassifyAnim). Purely local: the server trusts your position.
constexpr float kSprintMult = royale::kSprintMult;   // shared/balance.h: the bots sprint with the same numbers
constexpr float kSprintSeconds = royale::kSprintSeconds;
constexpr float kStaminaRefill = royale::kStaminaRefill;
constexpr float kStaminaRest = royale::kStaminaRest;
constexpr float kSprintMinStamina = royale::kSprintMinStamina;
float gStamina = 1.0f;
float gStaminaRestLeft = 0.0f;
bool gSprinting = false;
bool gSprintButtonWasDown = false;
double gStaminaFullAt = 0.0;                // when the bar last filled, so it can fade away

bool SprintButtonDown() {
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (!SDL_IsGameController(i)) continue;
        SDL_GameController* pad = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i));   // only pads the game already opened
        if (pad != nullptr && SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSTICK)) return true;
    }
    return false;
}

void UpdateSprint(Player* player, const royale::HudState& hud) {
    const float dt = 1.0f / royale::kTickHz;   // the game updates the player 20 times a second
    const bool down = SprintButtonDown();
    const bool clicked = down && !gSprintButtonWasDown;
    gSprintButtonWasDown = down;

    if (!LiveAndAlive(hud)) {   // a fresh bar for every match
        gStamina = 1.0f; gSprinting = false;
        return;
    }
    const Input& in = gPlayState->state.input[0];
    const bool stickHeld = std::hypot(static_cast<float>(in.cur.stick_x), static_cast<float>(in.cur.stick_y)) > 30.0f;
    const bool canSprint = InField() && !gSkydiving && hud.stunLeft <= 0 && (player->actor.bgCheckFlags & 1) &&
                           !(player->stateFlags1 & (PLAYER_STATE1_IN_WATER | PLAYER_STATE1_ON_HORSE | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_SHIELDING |
                                                    PLAYER_STATE1_CARRYING_ACTOR | PLAYER_STATE1_HANGING_OFF_LEDGE | PLAYER_STATE1_CLIMBING_LADDER));
    if (clicked) gSprinting = !gSprinting && canSprint && stickHeld && gStamina >= kSprintMinStamina;
    // Jumping off a ledge mid-sprint keeps it going; only the conditions that really end a run stop it.
    if (!stickHeld || gStamina <= 0.0f || hud.stunLeft > 0 || (player->stateFlags1 & (PLAYER_STATE1_IN_WATER | PLAYER_STATE1_ON_HORSE | PLAYER_STATE1_IN_CUTSCENE |
                                                                                          PLAYER_STATE1_HANGING_OFF_LEDGE | PLAYER_STATE1_CLIMBING_LADDER)))
        gSprinting = false;

    if (gSprinting) {
        if (std::fabs(player->linearVelocity) > 2.0f) gStamina = std::max(0.0f, gStamina - dt / kSprintSeconds);   // only running costs stamina
        gStaminaRestLeft = kStaminaRest;
    } else if (gStaminaRestLeft > 0.0f) {
        gStaminaRestLeft -= dt;
    } else if (gStamina < 1.0f) {
        gStamina = std::min(1.0f, gStamina + dt / kStaminaRefill);
        if (gStamina >= 1.0f) gStaminaFullAt = ImGui::GetTime();
    }
    gRoyaleRunSpeedScale = gSprinting ? kSprintMult : 1.0f;
    if (gSprinting && (player->actor.bgCheckFlags & 1) && std::fabs(player->linearVelocity) > 4.0f) SprintDust(gPlayState, &player->actor, gPlayState->gameplayFrames);
}

// A thin bar under the magic meter, the width of the hearts, like the shield and magic bars above it. No text. It only shows while you are using
// stamina and fades out a moment after it fills, so it stays off the screen the rest of the time.
void DrawStaminaBar(ImDrawList* dl, ImVec2 ds, const royale::HudState& h) {
    if (!LiveAndAlive(h) || !InField()) return;
    const double sinceFull = gStamina >= 1.0f ? ImGui::GetTime() - gStaminaFullAt : 0.0;
    const float alpha = static_cast<float>(std::clamp(1.0 - (sinceFull - 0.8) / 0.4, 0.0, 1.0));
    if (alpha <= 0.0f) return;
    auto a = [&](int v) { return static_cast<int>(v * alpha); };
    const float unit = ds.y / 240.0f;   // the game's own HUD is laid out on a 240 high screen
    const float bx = 30.0f * unit, by = 66.0f * unit, bw = std::max(3.0f, h.maxHealth) * 16.0f * unit, bh = 4.0f * unit;
    const float fill = std::clamp(gStamina, 0.0f, 1.0f);
    const bool winded = !gSprinting && fill < kSprintMinStamina;
    dl->AddRectFilled(ImVec2(bx - 2, by - 2), ImVec2(bx + bw + 2, by + bh + 2), IM_COL32(0, 0, 0, a(170)), 3.0f);
    dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), IM_COL32(58, 42, 16, a(200)), 2.0f);   // dark leather brown
    if (fill > 0.0f) {
        // Hylian hair gold (#F0DF57) from the art guide; Hylian crest red (#AD3725) while too winded to sprint.
        dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw * fill, by + bh), winded ? IM_COL32(173, 55, 37, a(255)) : IM_COL32(240, 223, 87, a(255)), 2.0f);
        dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw * fill, by + bh * 0.45f), IM_COL32(255, 248, 200, a(150)), 2.0f);
    }
}

struct SupplyMark { float x, z; double until; };
std::vector<SupplyMark> gSupplyMarks;   // announced crates, shown on the minimap until they have landed and been taken


// Spectating after you are eliminated: you can watch yourself (where you fell) or any other player who is still alive, and nobody else.
// D-pad Left/Right (or the < > buttons) switch between them; if the one you watch is eliminated the view moves on to the next living player.
constexpr uint16_t kSpectateSelf = 0xFFFF;
uint16_t gSpectateTarget = kSpectateSelf;

std::vector<uint16_t> SpectatableIds() {
    std::vector<uint16_t> ids = { kSpectateSelf };
    for (const auto& st : gSession.Puppets()) if (st.alive) ids.push_back(st.id);
    std::sort(ids.begin() + 1, ids.end());
    return ids;
}

void CycleSpectate(int dir) {
    const std::vector<uint16_t> ids = SpectatableIds();
    size_t at = 0;
    for (size_t i = 0; i < ids.size(); i++) if (ids[i] == gSpectateTarget) at = i;
    gSpectateTarget = ids[(at + ids.size() + (dir < 0 ? ids.size() - 1 : 1)) % ids.size()];
}

const royale::PuppetState* SpectateTarget() {
    if (gSpectateTarget == kSpectateSelf) return nullptr;
    for (const auto& st : gSession.Puppets()) if (st.id == gSpectateTarget && st.alive) { static royale::PuppetState keep; keep = st; return &keep; }
    return nullptr;
}

std::string SpectateName() {
    if (const royale::PuppetState* t = SpectateTarget()) return t->name.empty() ? "Link" : t->name;
    return "yourself";
}

// Is something covering the game right now (its own pause screen or the port's menu)? Then the storm and weather tints stay off the screen.
bool ScreenCovered() {
    if (gPlayState != nullptr && gPlayState->pauseCtx.state != 0) return true;
    return SohGui::mSohMenu && SohGui::mSohMenu->IsVisible();
}

// The storm. Its edge is a glowing wall standing in the world (DrawStormWall), so you can see it from across the map. Standing outside the safe
// zone you are in the storm itself: the sky goes dark (DriveTimeOfDay), the game's own rain, lightning and a violet fog close in
// (DriveRealWeather), and this adds only a faint violet tint on top. Nothing of it is drawn while you are dead or a menu covers the game.
void DrawStorm(ImDrawList* dl, ImVec2 ds, float scale, const royale::HudState& h) {
    (void)scale;
    const float w = gStormWeather;
    if (w <= 0.0f || !InField() || !h.selfAlive || ScreenCovered()) return;
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ds, IM_COL32(40, 16, 70, static_cast<int>(60 * w)), IM_COL32(40, 16, 70, static_cast<int>(60 * w)),
                                IM_COL32(70, 40, 110, static_cast<int>(35 * w)), IM_COL32(70, 40, 110, static_cast<int>(35 * w)));
}

// ---- seasons and weather ----------------------------------------------------------------------------------------------------
// What the sky is doing (the server's spell of weather). Rain, snow, lightning, fog and the desert sandstorm are the game's own effects, in the
// world (DriveRealWeather); ash and blowing sand are particles in the world too (DrawWeatherParticles). All that is left on the screen is a faint
// seasonal tint and the flash of a bolt landing next to you. Everything fades in and out as spells change.

float WeatherAmount() { return gWeatherBlend * gWeatherShown.Strength(); }

void DrawWeather(ImDrawList* dl, ImVec2 ds, float scale, const royale::HudState& h) {
    (void)scale;
    const bool live = InField() && gSession.Joined() && (h.state == royale::MatchState::Drop || h.state == royale::MatchState::InMatch);
    const bool active = live && h.weather.sky != royale::Sky::Clear && h.weather.intensity > 0;
    if (active) gWeatherShown = h.weather;
    gWeatherBlend = std::clamp(gWeatherBlend + (active ? 0.012f : -0.012f), 0.0f, 1.0f);
    if (live) gWeatherShown.season = h.weather.season;
    if (!live || ScreenCovered()) return;
    // The season colours the whole picture a little.
    static const ImU32 kSeasonTint[4] = { IM_COL32(150, 230, 140, 16), IM_COL32(255, 214, 120, 18), IM_COL32(255, 150, 60, 26), IM_COL32(190, 215, 255, 30) };
    dl->AddRectFilled(ImVec2(0, 0), ds, kSeasonTint[static_cast<int>(h.weather.season) & 3]);
    // A lightning bolt landing nearby lights up the whole screen.
    const double t = ImGui::GetTime();
    if (t < gBoltFlashUntil) dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(225, 232, 255, static_cast<int>(std::min(1.0, (gBoltFlashUntil - t) / 0.35) * 150.0)));
}

// Town names hanging over each point of interest, big enough to read from the sky while you skydive.
void DrawPoiLabels(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const royale::HudState& h) {
    if (!gSession.Client() || !InField()) return;
    (void)h;
    Player* pl = GET_PLAYER(gPlayState);
    const float reach = gSkydiving ? 14000.0f : 5000.0f;
    for (const royale::Poi& p : gSession.Client()->Pois()) {
        const float dx = p.center.x - pl->actor.world.pos.x, dz = p.center.z - pl->actor.world.pos.z;
        const float d = std::sqrt(dx * dx + dz * dz);
        if (d > reach) continue;
        float y = pl->actor.world.pos.y;
        FloorAt(p.center.x, p.center.z, &y);
        ImVec2 at;
        if (!WorldToScreen(p.center.x, y + 650.0f, p.center.z, &at)) continue;
        const int alpha = static_cast<int>(255.0f * std::min(1.0f, (reach - d) / (reach * 0.4f)));
        const float size = std::clamp(34.0f * scale * (2400.0f / (d + 1200.0f)), 15.0f * scale, 38.0f * scale);
        const char* name = royale::kPoiNames[p.name];
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, name);
        const ImVec2 pos(at.x - sz.x * 0.5f, at.y - sz.y * 0.5f);
        dl->AddText(font, size, ImVec2(pos.x + 2.0f, pos.y + 2.0f), IM_COL32(10, 14, 8, alpha), name);
        dl->AddText(font, size, pos, IM_COL32(255, 222, 110, alpha), name);
    }
}

bool MapOption(const char* name, bool fallback = true) {
    char key[64];
    std::snprintf(key, sizeof(key), CVAR_SETTING("Royale.%s"), name);
    return CVarGetInteger(key, fallback ? 1 : 0) != 0;
}

// The game's own overworld minimap for the scene (the light-blue map that sits in the corner in Ocarina of Time), read from the player's ROM
// like the rest of the game's art. Its 4-bit grey and alpha pixels are turned into white for ImGui and tinted when drawn; drawn smooth rather
// than in blocky pixels, since the minimap below shows it close up.
struct GameMinimap { bool tried = false, ok = false; int texId = -1; ImTextureID tex = nullptr; int w = 0, h = 0; };

const char* OverworldMinimapName(int index) {
    static const char* const kNames[] = { dgHyruleFieldMinimapTex, dgKakarikoVillageMinimapTex, dgGraveyardMinimapTex, dgZorasRiverMinimapTex,
                                          dgKokiriForestMinimapTex, dgSacredMeadowMinimapTex, dgLakeHyliaMinimapTex, dgZorasDomainMinimapTex,
                                          dgZorasFountainMinimapTex, dgGerudoValleyMinimapTex, dgHauntedWastelandMinimapTex, dgDesertColossusMinimapTex,
                                          dgGerudosFortessMinimapTex, dgLostWoodsMinimapTex, dgHyruleCastleAreaMinimapTex, dgDeathMountainTrailMinimapTex,
                                          dgDeathMountainCraterMinimapTex, dgGoronCityMinimapTex, dgLonLonRanchMinimapTex, dgOutsideGanonsCastleMinimapTex };
    return index >= 0 && index < static_cast<int>(sizeof(kNames) / sizeof(kNames[0])) ? kNames[index] : nullptr;
}

ImTextureID UploadRgba(const uint8_t* rgba, int w, int h, int* idInOut) {
    GfxRenderingAPI* api = gfx_get_current_rendering_api();
    if (api == nullptr) return nullptr;
    if (*idInOut < 0) *idInOut = static_cast<int>(api->new_texture());
    api->select_texture(0, *idInOut);
    api->set_sampler_parameters(0, true, 0, 0);
    api->upload_texture(rgba, w, h);
    return reinterpret_cast<ImTextureID>(static_cast<intptr_t>(*idInOut));
}

// The Fortnite map's picture for the minimap: the baked vertex colours of the island, upscaled smoothly by the graphics card.
ImTextureID FortniteMinimapTexture() {
    static int texId = -1;
    static ImTextureID tex = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        constexpr int n = royale::fortnite::kFine + 1;
        std::vector<uint8_t> rgba(static_cast<size_t>(n) * n * 4);
        for (int i = 0; i < n * n; i++) {
            rgba[i * 4 + 0] = royale::fortnite::kColours[i * 3 + 0];
            rgba[i * 4 + 1] = royale::fortnite::kColours[i * 3 + 1];
            rgba[i * 4 + 2] = royale::fortnite::kColours[i * 3 + 2];
            rgba[i * 4 + 3] = 255;
        }
        tex = UploadRgba(rgba.data(), n, n, &texId);
    }
    return tex;
}

const GameMinimap* GameMinimapFor(int sceneNum) {
    static GameMinimap maps[20];
    const int index = sceneNum - SCENE_HYRULE_FIELD;
    const char* name = OverworldMinimapName(index);
    if (name == nullptr) return nullptr;
    GameMinimap& m = maps[index];
    if (!m.tried) {
        m.tried = true;
        try {
            auto res = std::static_pointer_cast<Fast::Texture>(Ship::Context::GetInstance()->GetResourceManager()->LoadResource(name, true));
            if (res != nullptr && res->ImageData != nullptr && res->Type == Fast::TextureType::GrayscaleAlpha4bpp && res->Width > 0 && res->Height > 0 &&
                res->ImageDataSize >= static_cast<uint32_t>(res->Width * res->Height / 2)) {
                std::vector<uint8_t> rgba(static_cast<size_t>(res->Width) * res->Height * 4);
                for (int i = 0; i < res->Width * res->Height; i++) {
                    const uint8_t nib = (i & 1) ? (res->ImageData[i / 2] & 0xF) : (res->ImageData[i / 2] >> 4);
                    const uint8_t grey = static_cast<uint8_t>(((nib >> 1) & 7) * 255 / 7);
                    rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = grey;
                    rgba[i * 4 + 3] = (nib & 1) ? 255 : 0;
                }
                m.tex = UploadRgba(rgba.data(), res->Width, res->Height, &m.texId);
                m.w = res->Width;
                m.h = res->Height;
                m.ok = m.tex != nullptr;
            }
        } catch (...) {}
    }
    return m.ok ? &m : nullptr;
}

// Bottom left: a minimap that follows you. It shows the area around you on the game's own minimap for the scene, north up, with you in the
// middle as the game's yellow arrow: the storm in purple, the safe zone's edge, supply drops, mini bosses and other players. While you skydive
// it zooms out to show the whole map, so you can pick where to land.
void DrawMinimap(ImDrawList* dl, ImVec2 ds, float scale, const royale::HudState& h) {
    if (h.map.radius <= 0 || !InField()) return;
    Player* pl = GET_PLAYER(gPlayState);
    const float half = 86.0f * scale;
    const ImVec2 c(half + 22.0f * scale, ds.y - half - 30.0f * scale);
    const ImVec2 a(c.x - half, c.y - half), b(c.x + half, c.y + half);
    const float px = pl->actor.world.pos.x, pz = pl->actor.world.pos.z;

    // How much of the world fits across (half of it, in world units): close around you on foot, the whole map while skydiving.
    static float viewHalf = 4200.0f;
    const float want = gSkydiving ? std::max(4200.0f, h.map.radius * 1.15f) : 4200.0f;
    viewHalf += (want - viewHalf) * std::min(1.0f, ImGui::GetIO().DeltaTime * 3.0f);
    const float k = half / viewHalf;
    // North (the game's -Z) is up, as on the game's own minimap.
    auto toMap = [&](float x, float z) { return ImVec2(c.x + (x - px) * k, c.y + (z - pz) * k); };
    auto inside = [&](ImVec2 p) { return p.x > a.x && p.x < b.x && p.y > a.y && p.y < b.y; };
    auto pinned = [&](ImVec2 p) {   // something off the edge: pinned to the rim, on the line towards it
        const float m = 5.0f * scale, dx = p.x - c.x, dy = p.y - c.y, far = std::max(std::fabs(dx), std::fabs(dy));
        if (far <= half - m) return p;
        const float t = (half - m) / far;
        return ImVec2(c.x + dx * t, c.y + dy * t);
    };

    const float round = 12.0f * scale;
    dl->AddRectFilled(ImVec2(a.x + 3 * scale, a.y + 4 * scale), ImVec2(b.x + 3 * scale, b.y + 4 * scale), IM_COL32(0, 0, 0, 90), round);
    dl->AddRectFilled(a, b, IM_COL32(10, 16, 22, 170), round);
    dl->PushClipRect(ImVec2(a.x + 2 * scale, a.y + 2 * scale), ImVec2(b.x - 2 * scale, b.y - 2 * scale), true);

    // The game's minimap, placed with the game's own numbers for where it sits and how world positions land on it (z_map_exp.c's compass
    // icons): pixel u = (offsetX + x / scaleX) / 10 + 160 - minimapX, pixel v = 120 - (offsetY - z / scaleY) / 10 - minimapY.
    const int owIndex = gPlayState->sceneNum - SCENE_HYRULE_FIELD;
    const GameMinimap* gm = gFortniteScene ? nullptr : GameMinimapFor(gPlayState->sceneNum);
    if (gFortniteScene) {   // the island's own picture (its texture), laid over the whole map
        ImTextureID tex = FortniteMinimapTexture();
        if (tex != nullptr) dl->AddImage(tex, toMap(-royale::fortnite::kHalfX, -royale::fortnite::kHalfZ), toMap(royale::fortnite::kHalfX, royale::fortnite::kHalfZ), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 210));
    } else if (gm != nullptr && gMapData != nullptr && owIndex >= 0 && owIndex < 20) {
        const float sx = gMapData->owCompassInfo[owIndex][0], sz = gMapData->owCompassInfo[owIndex][1];
        const float ox = gMapData->owCompassInfo[owIndex][2], oy = gMapData->owCompassInfo[owIndex][3];
        const float mx = gMapData->owMinimapPosX[owIndex], my = gMapData->owMinimapPosY[owIndex];
        if (sx > 0 && sz > 0) {
            const float x0 = ((mx - 160.0f) * 10.0f - ox) * sx, x1 = x0 + gm->w * 10.0f * sx;
            const float z0 = (oy - (120.0f - my) * 10.0f) * sz, z1 = z0 + gm->h * 10.0f * sz;
            dl->AddImage(gm->tex, toMap(x0, z0), toMap(x1, z1), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(0, 255, 255, 190));   // the game's minimap colour
        }
    }

    // The storm: everything outside the safe zone, as a thick purple ring around it (clipped to the map), then the zone's white edge.
    const ImVec2 zc = toMap(h.safeZone.center.x, h.safeZone.center.z);
    const float zr = h.safeZone.radius * k;
    const float reach = std::sqrt((std::fabs(zc.x - c.x) + half) * (std::fabs(zc.x - c.x) + half) + (std::fabs(zc.y - c.y) + half) * (std::fabs(zc.y - c.y) + half));
    if (reach > zr) {
        const float thick = reach - zr + 4.0f * scale;
        dl->AddCircle(zc, zr + thick * 0.5f, IM_COL32(120, 50, 190, 120), 96, thick);
    }
    dl->AddCircle(zc, zr, IM_COL32(255, 255, 255, 235), 96, 2.0f * scale);

    const double now = ImGui::GetTime();
    {   // supply drops and helpers to hire: a pulsing star, pinned to the rim when off the map
        const float pulse = 0.75f + 0.25f * static_cast<float>(std::sin(now * 5.0));
        auto star = [&](float wx, float wz, ImU32 col) {
            const ImVec2 p = pinned(toMap(wx, wz));
            const float u = 7.0f * scale * pulse;
            dl->AddQuadFilled(ImVec2(p.x, p.y - u), ImVec2(p.x + u * 0.4f, p.y), ImVec2(p.x, p.y + u), ImVec2(p.x - u * 0.4f, p.y), col);
            dl->AddQuadFilled(ImVec2(p.x - u, p.y), ImVec2(p.x, p.y - u * 0.4f), ImVec2(p.x + u, p.y), ImVec2(p.x, p.y + u * 0.4f), col);
        };
        gSupplyMarks.erase(std::remove_if(gSupplyMarks.begin(), gSupplyMarks.end(), [&](const SupplyMark& m) { return now > m.until; }), gSupplyMarks.end());
        for (const SupplyMark& m : gSupplyMarks) star(m.x, m.z, IM_COL32(255, 160, 60, 255));
        for (const auto& [aid, al] : gAllies) if (al.owner == h.selfId) star(al.x, al.z, IM_COL32(120, 255, 150, 255));   // only your own helpers
        if (gSession.Client()) for (const auto& l : gSession.Client()->Loot()) if (l.supply && l.chest && !l.taken) star(l.x, l.z, IM_COL32(255, 220, 90, 255));
    }
    if (gSession.Client() && MapOption("MapEnemies")) {
        for (const auto& bn : gSession.Client()->Bosses()) {   // mini bosses: a purple diamond
            const ImVec2 p = toMap(bn.x, bn.z);
            if (!inside(p)) continue;
            const float u = 5.0f * scale;
            dl->AddQuadFilled(ImVec2(p.x, p.y - u), ImVec2(p.x + u, p.y), ImVec2(p.x, p.y + u), ImVec2(p.x - u, p.y), IM_COL32(190, 60, 255, 255));
            dl->AddQuad(ImVec2(p.x, p.y - u), ImVec2(p.x + u, p.y), ImVec2(p.x, p.y + u), ImVec2(p.x - u, p.y), IM_COL32(255, 255, 255, 230), 1.5f);
        }
    }
    DrawCartsOnMap(dl, scale, toMap, inside);
    for (const auto& st : gSession.Puppets()) {
        if (!st.alive || !MapOption(st.isBot ? "MapBots" : "MapPlayers")) continue;
        const ImVec2 p = toMap(st.x, st.z);
        if (!inside(p)) continue;
        dl->AddCircleFilled(p, 3.4f * scale, IM_COL32(0, 0, 0, 200));
        dl->AddCircleFilled(p, 2.6f * scale, st.isBot ? IM_COL32(255, 100, 100, 255) : IM_COL32(255, 170, 60, 255));
    }
    dl->PopClipRect();

    // You, in the middle: the game's own minimap arrow (yellow-green, as Minimap_DrawCompassIcons colours it), pointing the way Link faces.
    const float th = pl->actor.shape.rot.y * (3.14159265f / 32768.0f);
    const ImVec2 fwd(std::sin(th), std::cos(th)), side(-fwd.y, fwd.x);
    const float u = 7.0f * scale;
    const ImVec2 tip(c.x + fwd.x * u * 1.3f, c.y + fwd.y * u * 1.3f), l(c.x - fwd.x * u + side.x * u * 0.8f, c.y - fwd.y * u + side.y * u * 0.8f),
                 r(c.x - fwd.x * u - side.x * u * 0.8f, c.y - fwd.y * u - side.y * u * 0.8f);
    dl->AddTriangleFilled(tip, l, r, IM_COL32(200, 255, 0, 255));
    dl->AddTriangle(tip, l, r, IM_COL32(0, 0, 0, 220), 1.5f * scale);

    // The frame: the menus' gold rule, and N at the top.
    dl->AddRect(a, b, IM_COL32(176, 118, 24, 255), round, 0, 2.0f * scale);
    dl->AddRect(ImVec2(a.x + 4 * scale, a.y + 4 * scale), ImVec2(b.x - 4 * scale, b.y - 4 * scale), IM_COL32(255, 214, 90, 120), round * 0.7f, 0, 1.0f * scale);
    const ImVec2 nsz = ImGui::GetFont()->CalcTextSizeA(14.0f * scale, FLT_MAX, 0.0f, "N");
    const ImVec2 np(c.x - nsz.x * 0.5f, a.y - nsz.y * 0.5f);
    dl->AddCircleFilled(ImVec2(c.x, a.y), nsz.y * 0.7f, IM_COL32(16, 12, 8, 230));
    dl->AddText(ImGui::GetFont(), 14.0f * scale, np, IM_COL32(255, 222, 110, 255), "N");
}

// The game's own item art (the 32x32 icons from its resource archive) is used wherever the game has one for the item; the hand-drawn
// icons below are only the fallback for things the original never had an icon for (songs, loot like rupee piles, medallions).
const char* RealIconName(royale::ItemId id) {
    using royale::ItemId;
    switch (id) {
        case ItemId::DekuStick: return "gItemIconDekuStickTex";
        case ItemId::BasicSword: case ItemId::KokiriSword: return "gItemIconSwordKokiriTex";
        case ItemId::MasterSword: return "gItemIconSwordMasterTex";
        case ItemId::BiggoronSword: return "gItemIconSwordBiggoronTex";
        case ItemId::MegatonHammer: case ItemId::GiantsHammer: return "gItemIconHammerTex";
        case ItemId::Slingshot: case ItemId::TripleSlingshot: return "gItemIconSlingshotTex";
        case ItemId::FairyBow: return "gItemIconBowTex";
        case ItemId::Boomerang: return "gItemIconBoomerangTex";
        case ItemId::Bombs: case ItemId::BombAmmo: return "gItemIconBombTex";
        case ItemId::Bombchus: case ItemId::HomingBombchus: case ItemId::BombchuAmmo: return "gItemIconBombchuTex";
        case ItemId::DekuNuts: case ItemId::NutAmmo: return "gItemIconDekuNutTex";
        case ItemId::FireArrows: return "gItemIconArrowFireTex";
        case ItemId::IceArrows: return "gItemIconArrowIceTex";
        case ItemId::LightArrows: return "gItemIconArrowLightTex";
        case ItemId::ArrowAmmo: return "gItemIconBowTex";
        case ItemId::SeedAmmo: return "gItemIconDekuSeedsTex";
        case ItemId::DekuShield: return "gItemIconShieldDekuTex";
        case ItemId::HylianShield: return "gItemIconShieldHylianTex";
        case ItemId::MirrorShield: return "gItemIconShieldMirrorTex";
        case ItemId::GreenPotion: return "gItemIconBottlePotionGreenTex";
        case ItemId::RedPotion: return "gItemIconBottlePotionRedTex";
        case ItemId::BluePotion: return "gItemIconBottlePotionBlueTex";
        case ItemId::Fairy: return "gItemIconBottleFairyTex";
        case ItemId::Milk: return "gItemIconBottleMilkFullTex";
        case ItemId::Fish: return "gItemIconBottleFishTex";
        case ItemId::BlueFire: return "gItemIconBottleBlueFireTex";
        case ItemId::Bug: return "gItemIconBottleBugTex";
        case ItemId::Poe: return "gItemIconBottlePoeTex";
        case ItemId::SmallShieldPotion: case ItemId::LargeShieldPotion: return "gItemIconBottleEmptyTex";
        case ItemId::DinsFire: return "gItemIconDinsFireTex";
        case ItemId::FaroresWind: return "gItemIconFaroresWindTex";
        case ItemId::NayrusLove: return "gItemIconNayrusLoveTex";
        case ItemId::Hookshot: return "gItemIconHookshotTex";
        case ItemId::Longshot: return "gItemIconLongshotTex";
        case ItemId::LensOfTruth: return "gItemIconLensOfTruthTex";
        case ItemId::MagicBeans: return "gItemIconMagicBeanTex";
        case ItemId::FairyOcarina: return "gItemIconOcarinaFairyTex";
        case ItemId::OcarinaOfTime: return "gItemIconOcarinaOfTimeTex";
        case ItemId::KokiriTunic: return "gItemIconTunicKokiriTex";
        case ItemId::GoronTunic: return "gItemIconTunicGoronTex";
        case ItemId::ZoraTunic: return "gItemIconTunicZoraTex";
        case ItemId::KokiriBoots: return "gItemIconBootsKokiriTex";
        case ItemId::IronBoots: return "gItemIconBootsIronTex";
        case ItemId::HoverBoots: return "gItemIconBootsHoverTex";
        case ItemId::GoronBracelet: return "gItemIconGoronsBraceletTex";
        case ItemId::SilverGauntlets: return "gItemIconSilverGauntletsTex";
        case ItemId::GoldenGauntlets: return "gItemIconGoldenGauntletsTex";
        case ItemId::KeatonMask: return "gItemIconMaskKeatonTex";
        case ItemId::SkullMask: return "gItemIconMaskSkullTex";
        case ItemId::SpookyMask: return "gItemIconMaskSpookyTex";
        case ItemId::BunnyHood: return "gItemIconMaskBunnyHoodTex";
        case ItemId::GoronMask: return "gItemIconMaskGoronTex";
        case ItemId::ZoraMask: return "gItemIconMaskZoraTex";
        case ItemId::GerudoMask: return "gItemIconMaskGerudoTex";
        case ItemId::MaskOfTruth: return "gItemIconMaskTruthTex";
        case ItemId::SilverScale: return "gItemIconScaleSilverTex";
        case ItemId::GoldenScale: return "gItemIconScaleGoldenTex";
        case ItemId::BigQuiver: return "gItemIconQuiver50Tex";
        case ItemId::BulletBag: return "gItemIconBulletBag50Tex";
        case ItemId::BombBag: return "gItemIconBombBag40Tex";
        case ItemId::RecoveryHeart: return "gHeartFullTex";
        case ItemId::HeartPiece: return "gQuestIconHeartPieceTex";
        case ItemId::HeartContainer: return "gQuestIconHeartContainerTex";
        case ItemId::MagicJar: return "gQuestIconMagicJarSmallTex";
        case ItemId::Rupees: return "gRupeeGreenTex";
        case ItemId::ForestMedallion: return "gQuestIconMedallionForestTex";
        case ItemId::FireMedallion: return "gQuestIconMedallionFireTex";
        case ItemId::WaterMedallion: return "gQuestIconMedallionWaterTex";
        case ItemId::SpiritMedallion: return "gQuestIconMedallionSpiritTex";
        case ItemId::ShadowMedallion: return "gQuestIconMedallionShadowTex";
        case ItemId::LightMedallion: return "gQuestIconMedallionLightTex";
        case ItemId::KokiriEmerald: return "gQuestIconKokiriEmeraldTex";
        case ItemId::GoronRuby: return "gQuestIconGoronRubyTex";
        case ItemId::ZoraSapphire: return "gQuestIconZoraSapphireTex";
        case ItemId::ZeldasLullaby: case ItemId::EponasSong: case ItemId::SariasSong: case ItemId::SunsSong: case ItemId::SongOfTime: case ItemId::SongOfStorms:
        case ItemId::MinuetOfForest: case ItemId::BoleroOfFire: case ItemId::SerenadeOfWater: case ItemId::NocturneOfShadow: case ItemId::RequiemOfSpirit: case ItemId::PreludeOfLight:
            return "gSongNoteTex";   // the game's music note, tinted in each song's colour (see RealIconTint)
        default: return nullptr;
    }
}

// Songs share the game's note icon; each is tinted in the colour of its song.
ImU32 RealIconTint(royale::ItemId id) {
    using royale::ItemId;
    switch (id) {
        case ItemId::ZeldasLullaby: return IM_COL32(220, 150, 255, 255);
        case ItemId::EponasSong: return IM_COL32(230, 130, 90, 255);
        case ItemId::SariasSong: return IM_COL32(110, 240, 130, 255);
        case ItemId::SunsSong: return IM_COL32(255, 235, 110, 255);
        case ItemId::SongOfTime: return IM_COL32(120, 170, 255, 255);
        case ItemId::SongOfStorms: return IM_COL32(190, 200, 215, 255);
        case ItemId::MinuetOfForest: return IM_COL32(90, 220, 90, 255);
        case ItemId::BoleroOfFire: return IM_COL32(255, 90, 60, 255);
        case ItemId::SerenadeOfWater: return IM_COL32(80, 150, 255, 255);
        case ItemId::NocturneOfShadow: return IM_COL32(180, 90, 230, 255);
        case ItemId::RequiemOfSpirit: return IM_COL32(255, 160, 60, 255);
        case ItemId::PreludeOfLight: return IM_COL32(255, 245, 160, 255);
        default: return IM_COL32(255, 255, 255, 255);
    }
}

// Loads an icon on first use. nullptr (no icon, or not loadable) means "draw the fallback".
void* RealIcon(royale::ItemId id) {
    static std::unordered_map<std::string, bool> loaded;
    const char* name = RealIconName(id);
    if (name == nullptr) return nullptr;
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    auto it = loaded.find(name);
    if (it == loaded.end()) {
        bool ok = false;
        try {
            const std::string n = name;
            const std::string dir = n.rfind("gQuestIcon", 0) == 0 ? "icon_item_24_static" : (n.rfind("gHeart", 0) == 0 || n.rfind("gRupee", 0) == 0) ? "parameter_static" : "icon_item_static";
            const std::string path = "__OTR__textures/" + dir + "/" + n;
            auto res = Ship::Context::GetInstance()->GetResourceManager()->LoadResource(path, true);
            if (res != nullptr) {
                gui->LoadGuiTexture(std::string("royale:") + name, path, ImVec4(1, 1, 1, 1));
                ok = true;
            }
        } catch (...) {}
        it = loaded.emplace(name, ok).first;
    }
    return it->second ? (void*)gui->GetTextureByName(std::string("royale:") + name) : nullptr;
}

// ---- item icons ------------------------------------------------------------------------------------------------------------
// The game's own item icons live in its resource archive and aren't reachable from here, so every item gets a small hand-drawn
// icon built from lines and shapes, tinted by what the item is. `tier` is the rarity colour, used as the accent.
void DrawItemIcon(ImDrawList* dl, royale::ItemId id, ImVec2 c, float s, ImU32 tier) {
    using royale::ItemId;
    if (void* real = RealIcon(id)) {
        const float h = s * 0.5f;
        dl->AddImage(real, ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), ImVec2(0, 0), ImVec2(1, 1), RealIconTint(id));
        return;
    }
    const float u = s * 0.5f; // half the icon box
    auto P = [&](float x, float y) { return ImVec2(c.x + x * u, c.y + y * u); };
    // Colours from the official art: chrome steel with a cool tint, Triforce gold, Kokiri green, Hylian-shield blue, crest red, warm leather.
    const ImU32 steel = IM_COL32(200, 218, 232, 255), dark = IM_COL32(43, 32, 30, 255), wood = IM_COL32(134, 92, 45, 255), gold = IM_COL32(247, 214, 34, 255),
                red = IM_COL32(205, 55, 37, 255), green = IM_COL32(72, 166, 64, 255), blue = IM_COL32(70, 96, 200, 255), white = IM_COL32(240, 236, 222, 255),
                purple = IM_COL32(150, 70, 190, 255), orange = IM_COL32(238, 130, 40, 255), cyan = IM_COL32(110, 200, 220, 255);
    const float th = std::max(1.5f, s * 0.07f);
    auto sword = [&](ImU32 blade, ImU32 hilt, float len, float width) {
        dl->AddLine(P(-0.62f, 0.62f), P(0.62f - (1.0f - len) * 0.6f, -0.62f + (1.0f - len) * 0.6f), blade, th * width);
        dl->AddLine(P(-0.55f, 0.15f), P(-0.15f, 0.55f), hilt, th * 1.2f);   // crossguard
        dl->AddLine(P(-0.45f, 0.45f), P(-0.78f, 0.78f), hilt, th * 1.5f);   // grip
    };
    auto bottle = [&](ImU32 fill, bool big) {
        const float r = big ? 0.62f : 0.5f;
        dl->AddCircleFilled(P(0, 0.22f), u * r, fill, 20);
        dl->AddCircle(P(0, 0.22f), u * r, white, 20, th * 0.7f);
        dl->AddRectFilled(P(-0.17f, -0.62f), P(0.17f, -0.2f), steel);
        dl->AddRectFilled(P(-0.22f, -0.74f), P(0.22f, -0.58f), wood);
    };
    auto shieldShape = [&](ImU32 body, ImU32 trim) {
        const ImVec2 pts[5] = { P(-0.62f, -0.62f), P(0.62f, -0.62f), P(0.62f, 0.1f), P(0.0f, 0.82f), P(-0.62f, 0.1f) };
        dl->AddConvexPolyFilled(pts, 5, body);
        dl->AddPolyline(pts, 5, trim, ImDrawFlags_Closed, th);
    };
    auto heart = [&](ImU32 col, float k) {
        dl->AddCircleFilled(P(-0.3f * k, -0.2f * k), u * 0.42f * k, col, 16);
        dl->AddCircleFilled(P(0.3f * k, -0.2f * k), u * 0.42f * k, col, 16);
        dl->AddTriangleFilled(P(-0.68f * k, -0.02f * k), P(0.68f * k, -0.02f * k), P(0, 0.75f * k), col);
    };
    auto note = [&](ImU32 col) {
        dl->AddCircleFilled(P(-0.25f, 0.5f), u * 0.3f, col, 14);
        dl->AddLine(P(0.02f, 0.45f), P(0.02f, -0.6f), col, th * 1.2f);
        dl->AddLine(P(0.02f, -0.6f), P(0.5f, -0.35f), col, th * 1.6f);
    };
    auto orb = [&](ImU32 col) {
        dl->AddCircleFilled(c, u * 0.62f, col, 22);
        dl->AddCircle(c, u * 0.62f, white, 22, th * 0.8f);
        dl->AddCircleFilled(P(-0.2f, -0.2f), u * 0.18f, IM_COL32(255, 255, 255, 190), 12);
    };
    auto medal = [&](ImU32 col) {
        dl->AddCircleFilled(c, u * 0.62f, col, 22);
        dl->AddCircle(c, u * 0.62f, gold, 22, th * 1.4f);
        dl->AddCircleFilled(c, u * 0.2f, white, 12);
    };
    auto gem = [&](ImU32 col) {
        const ImVec2 pts[4] = { P(0, -0.75f), P(0.55f, 0), P(0, 0.75f), P(-0.55f, 0) };
        dl->AddConvexPolyFilled(pts, 4, col);
        dl->AddPolyline(pts, 4, white, ImDrawFlags_Closed, th * 0.8f);
    };
    auto mask = [&](ImU32 col) {
        dl->AddCircleFilled(c, u * 0.62f, col, 20);
        dl->AddCircleFilled(P(-0.24f, -0.12f), u * 0.13f, dark, 10);
        dl->AddCircleFilled(P(0.24f, -0.12f), u * 0.13f, dark, 10);
        dl->AddLine(P(-0.2f, 0.28f), P(0.2f, 0.28f), dark, th);
    };
    auto boot = [&](ImU32 col) {
        dl->AddRectFilled(P(-0.3f, -0.7f), P(0.2f, 0.3f), col);
        dl->AddRectFilled(P(-0.3f, 0.2f), P(0.7f, 0.7f), col, 4.0f);
        dl->AddRectFilled(P(-0.3f, 0.6f), P(0.7f, 0.75f), dark);
    };
    auto tunic = [&](ImU32 col) {
        const ImVec2 pts[8] = { P(-0.35f, -0.7f), P(0.35f, -0.7f), P(0.8f, -0.35f), P(0.55f, 0.0f), P(0.38f, -0.15f), P(0.38f, 0.7f), P(-0.38f, 0.7f), P(-0.38f, -0.15f) };
        dl->AddConvexPolyFilled(pts, 8, col);
        dl->AddTriangleFilled(P(-0.38f, -0.15f), P(-0.55f, 0.0f), P(-0.8f, -0.35f), col);
        dl->AddTriangleFilled(P(-0.35f, -0.7f), P(-0.8f, -0.35f), P(-0.38f, -0.15f), col);
        dl->AddLine(P(-0.38f, 0.3f), P(0.38f, 0.3f), gold, th);
    };
    auto arrow = [&](ImU32 tip) {
        dl->AddLine(P(-0.7f, 0.7f), P(0.6f, -0.6f), wood, th);
        dl->AddTriangleFilled(P(0.8f, -0.8f), P(0.28f, -0.5f), P(0.5f, -0.28f), tip);
        dl->AddLine(P(-0.7f, 0.7f), P(-0.4f, 0.7f), white, th);
        dl->AddLine(P(-0.7f, 0.7f), P(-0.7f, 0.4f), white, th);
    };

    switch (id) {
        case ItemId::DekuStick: dl->AddLine(P(-0.5f, 0.7f), P(0.5f, -0.7f), wood, th * 2.2f); dl->AddCircleFilled(P(0.55f, -0.75f), u * 0.18f, orange, 8); break;
        case ItemId::BasicSword: sword(IM_COL32(170, 178, 190, 255), wood, 0.7f, 1.2f); break;
        case ItemId::KokiriSword: sword(steel, green, 0.8f, 1.5f); break;
        case ItemId::MasterSword: sword(cyan, blue, 1.0f, 1.8f); dl->AddCircleFilled(P(-0.35f, 0.35f), u * 0.12f, gold, 8); break;
        case ItemId::BiggoronSword: sword(white, red, 1.0f, 2.8f); break;
        case ItemId::MegatonHammer:
            dl->AddLine(P(-0.6f, 0.7f), P(0.35f, -0.35f), wood, th * 1.8f);
            dl->AddRectFilled(P(0.0f, -0.8f), P(0.78f, -0.15f), IM_COL32(120, 125, 140, 255), 3.0f);
            dl->AddRect(P(0.0f, -0.8f), P(0.78f, -0.15f), steel, 3.0f, 0, th * 0.7f);
            break;
        case ItemId::Slingshot:
            dl->AddLine(P(0, 0.75f), P(0, 0.1f), wood, th * 1.8f);
            dl->AddLine(P(0, 0.1f), P(-0.55f, -0.65f), wood, th * 1.8f);
            dl->AddLine(P(0, 0.1f), P(0.55f, -0.65f), wood, th * 1.8f);
            dl->AddLine(P(-0.55f, -0.65f), P(0.55f, -0.65f), red, th * 0.8f);
            break;
        case ItemId::TripleSlingshot:
            dl->AddLine(P(0, 0.75f), P(0, 0.1f), wood, th * 1.8f);
            dl->AddLine(P(0, 0.1f), P(-0.6f, -0.55f), wood, th * 1.8f);
            dl->AddLine(P(0, 0.1f), P(0.6f, -0.55f), wood, th * 1.8f);
            for (int i = -1; i <= 1; i++) dl->AddCircleFilled(P(i * 0.3f, -0.62f - (i == 0 ? 0.15f : 0.0f)), u * 0.14f, orange, 8);
            break;
        case ItemId::GiantsHammer:
            dl->AddLine(P(-0.7f, 0.8f), P(0.3f, -0.3f), wood, th * 2.2f);
            dl->AddRectFilled(P(-0.15f, -0.95f), P(0.95f, -0.05f), gold, 3.0f);
            dl->AddRect(P(-0.15f, -0.95f), P(0.95f, -0.05f), IM_COL32(160, 110, 30, 255), 3.0f, 0, th);
            break;
        case ItemId::HomingBombchus:
            dl->AddRectFilled(P(-0.3f, -0.55f), P(0.3f, 0.5f), purple, u * 0.3f);
            dl->AddCircleFilled(P(-0.35f, -0.55f), u * 0.22f, IM_COL32(255, 200, 255, 255), 10);
            dl->AddCircleFilled(P(0.35f, -0.55f), u * 0.22f, IM_COL32(255, 200, 255, 255), 10);
            dl->AddLine(P(0, 0.5f), P(0.3f, 0.8f), orange, th);
            dl->AddCircle(c, u * 0.9f, IM_COL32(220, 150, 255, 200), 20, th * 0.8f);   // the ring says it seeks
            break;
        case ItemId::Rupees: gem(green); break;
        case ItemId::ArrowAmmo: for (int i = -1; i <= 1; i++) { dl->AddLine(P(-0.5f + i * 0.2f, 0.7f), P(0.4f + i * 0.2f, -0.6f), wood, th); dl->AddTriangleFilled(P(0.5f + i * 0.2f, -0.8f), P(0.2f + i * 0.2f, -0.55f), P(0.45f + i * 0.2f, -0.4f), steel); } break;
        case ItemId::SeedAmmo: dl->AddCircleFilled(P(-0.3f, 0.15f), u * 0.3f, IM_COL32(190, 150, 90, 255), 12); dl->AddCircleFilled(P(0.3f, -0.1f), u * 0.3f, IM_COL32(150, 190, 90, 255), 12); dl->AddCircleFilled(P(0.0f, 0.5f), u * 0.3f, orange, 12); break;
        case ItemId::BombAmmo:
            dl->AddCircleFilled(P(0, 0.2f), u * 0.58f, IM_COL32(35, 40, 55, 255), 20);
            dl->AddCircle(P(0, 0.2f), u * 0.58f, steel, 20, th * 0.7f);
            dl->AddLine(P(0.3f, -0.25f), P(0.55f, -0.55f), wood, th * 1.2f);
            dl->AddCircleFilled(P(0.6f, -0.62f), u * 0.14f, orange, 8);
            break;
        case ItemId::BombchuAmmo:
            dl->AddRectFilled(P(-0.3f, -0.55f), P(0.3f, 0.5f), red, u * 0.3f);
            dl->AddCircleFilled(P(-0.35f, -0.55f), u * 0.22f, white, 10);
            dl->AddCircleFilled(P(0.35f, -0.55f), u * 0.22f, white, 10);
            break;
        case ItemId::NutAmmo: dl->AddCircleFilled(c, u * 0.55f, wood, 18); dl->AddCircle(c, u * 0.55f, IM_COL32(95, 60, 30, 255), 18, th); dl->AddLine(P(0, -0.55f), P(0, -0.8f), green, th * 1.4f); break;
        case ItemId::FairyBow:
            dl->AddBezierQuadratic(P(-0.2f, -0.8f), P(0.95f, 0.0f), P(-0.2f, 0.8f), wood, th * 1.8f);
            dl->AddLine(P(-0.2f, -0.8f), P(-0.2f, 0.8f), white, th * 0.6f);
            dl->AddLine(P(-0.5f, 0.0f), P(0.7f, 0.0f), steel, th * 0.9f);
            break;
        case ItemId::FireArrows: arrow(IM_COL32(255, 120, 40, 255)); break;
        case ItemId::IceArrows: arrow(cyan); break;
        case ItemId::LightArrows: arrow(IM_COL32(255, 245, 150, 255)); break;
        case ItemId::Boomerang:
            dl->AddLine(P(-0.65f, -0.5f), P(0.0f, 0.6f), orange, th * 2.2f);
            dl->AddLine(P(0.0f, 0.6f), P(0.65f, -0.5f), orange, th * 2.2f);
            break;
        case ItemId::Bombs:
            dl->AddCircleFilled(P(0, 0.2f), u * 0.58f, IM_COL32(35, 40, 55, 255), 20);
            dl->AddCircle(P(0, 0.2f), u * 0.58f, steel, 20, th * 0.7f);
            dl->AddLine(P(0.3f, -0.25f), P(0.55f, -0.55f), wood, th * 1.2f);
            dl->AddCircleFilled(P(0.6f, -0.62f), u * 0.14f, orange, 8);
            break;
        case ItemId::Bombchus:
            dl->AddRectFilled(P(-0.3f, -0.55f), P(0.3f, 0.5f), red, u * 0.3f);
            dl->AddCircleFilled(P(-0.35f, -0.55f), u * 0.22f, white, 10);
            dl->AddCircleFilled(P(0.35f, -0.55f), u * 0.22f, white, 10);
            dl->AddLine(P(0, 0.5f), P(0.3f, 0.8f), orange, th);
            break;
        case ItemId::DekuNuts: dl->AddCircleFilled(c, u * 0.55f, wood, 18); dl->AddCircle(c, u * 0.55f, IM_COL32(95, 60, 30, 255), 18, th); dl->AddLine(P(0, -0.55f), P(0, -0.8f), green, th * 1.4f); break;
        case ItemId::DekuShield: shieldShape(wood, IM_COL32(95, 60, 30, 255)); break;
        case ItemId::HylianShield: shieldShape(blue, steel); dl->AddTriangleFilled(P(0, -0.3f), P(-0.25f, 0.2f), P(0.25f, 0.2f), gold); break;
        case ItemId::MirrorShield: shieldShape(steel, gold); dl->AddCircleFilled(P(0, -0.05f), u * 0.25f, red, 12); break;
        case ItemId::GreenPotion: bottle(green, false); break;
        case ItemId::RedPotion: bottle(red, false); break;
        case ItemId::BluePotion: bottle(blue, false); break;
        case ItemId::SmallShieldPotion: bottle(cyan, false); break;
        case ItemId::LargeShieldPotion: bottle(blue, true); dl->AddCircle(P(0, 0.22f), u * 0.34f, white, 16, th * 0.8f); break;
        case ItemId::Milk: bottle(white, false); break;
        case ItemId::Fish: bottle(orange, false); break;
        case ItemId::BlueFire: bottle(IM_COL32(90, 170, 255, 255), false); break;
        case ItemId::Bug: bottle(IM_COL32(140, 190, 60, 255), false); break;
        case ItemId::Poe: bottle(purple, false); break;
        case ItemId::Fairy:
            dl->AddCircleFilled(c, u * 0.22f, white, 12);
            dl->AddCircle(c, u * 0.34f, IM_COL32(255, 200, 255, 255), 14, th * 0.8f);
            dl->AddTriangleFilled(P(-0.1f, -0.1f), P(-0.8f, -0.55f), P(-0.45f, 0.2f), IM_COL32(190, 230, 255, 230));
            dl->AddTriangleFilled(P(0.1f, -0.1f), P(0.8f, -0.55f), P(0.45f, 0.2f), IM_COL32(190, 230, 255, 230));
            break;
        case ItemId::RecoveryHeart: heart(red, 1.0f); break;
        case ItemId::HeartPiece: heart(IM_COL32(240, 120, 150, 255), 0.85f); dl->AddLine(P(0, -0.5f), P(0, 0.7f), white, th * 0.7f); break;
        case ItemId::HeartContainer: heart(IM_COL32(255, 70, 110, 255), 1.1f); dl->AddCircle(c, u * 0.9f, gold, 22, th); break;
        case ItemId::MagicJar:
            dl->AddRectFilled(P(-0.42f, -0.35f), P(0.42f, 0.7f), green, 6.0f);
            dl->AddRectFilled(P(-0.5f, -0.6f), P(0.5f, -0.3f), IM_COL32(120, 120, 130, 255), 3.0f);
            break;
        case ItemId::DinsFire: orb(red); break;
        case ItemId::FaroresWind: orb(green); break;
        case ItemId::NayrusLove: orb(blue); break;
        case ItemId::Hookshot: case ItemId::Longshot:
            dl->AddLine(P(-0.7f, 0.7f), P(0.2f, -0.2f), id == ItemId::Longshot ? gold : steel, th * 1.6f);
            dl->AddTriangleFilled(P(0.8f, -0.8f), P(0.25f, -0.45f), P(0.45f, -0.25f), steel);
            dl->AddLine(P(-0.5f, 0.9f), P(-0.9f, 0.5f), wood, th * 1.6f);
            break;
        case ItemId::LensOfTruth:
            dl->AddCircleFilled(c, u * 0.7f, IM_COL32(60, 40, 120, 255), 22);
            dl->AddCircleFilled(c, u * 0.4f, white, 18);
            dl->AddCircleFilled(c, u * 0.2f, dark, 12);
            dl->AddCircle(c, u * 0.7f, gold, 22, th);
            break;
        case ItemId::MagicBeans: dl->AddCircleFilled(P(-0.2f, 0.1f), u * 0.34f, IM_COL32(190, 150, 90, 255), 14); dl->AddCircleFilled(P(0.25f, -0.15f), u * 0.3f, IM_COL32(150, 190, 90, 255), 14); break;
        case ItemId::FairyOcarina: case ItemId::OcarinaOfTime:
            dl->AddCircleFilled(c, u * 0.55f, id == ItemId::OcarinaOfTime ? blue : IM_COL32(110, 160, 220, 255), 20);
            dl->AddRectFilled(P(0.2f, -0.65f), P(0.75f, -0.1f), id == ItemId::OcarinaOfTime ? blue : IM_COL32(110, 160, 220, 255), 3.0f);
            for (int i = 0; i < 3; i++) dl->AddCircleFilled(P(-0.25f + i * 0.25f, 0.05f + (i == 1 ? -0.15f : 0.0f)), u * 0.09f, dark, 8);
            if (id == ItemId::OcarinaOfTime) dl->AddCircle(c, u * 0.55f, gold, 20, th * 0.8f);
            break;
        case ItemId::ZeldasLullaby: note(IM_COL32(240, 150, 200, 255)); break;
        case ItemId::EponasSong: note(orange); break;
        case ItemId::SariasSong: note(green); break;
        case ItemId::SunsSong: note(gold); break;
        case ItemId::SongOfTime: note(cyan); break;
        case ItemId::SongOfStorms: note(IM_COL32(150, 160, 190, 255)); break;
        case ItemId::MinuetOfForest: note(green); break;
        case ItemId::BoleroOfFire: note(red); break;
        case ItemId::SerenadeOfWater: note(blue); break;
        case ItemId::NocturneOfShadow: note(purple); break;
        case ItemId::RequiemOfSpirit: note(orange); break;
        case ItemId::PreludeOfLight: note(IM_COL32(255, 245, 150, 255)); break;
        case ItemId::ShockwaveGrenade: {   // a grey ball with glowing purple bands, like its model
            const ImU32 glow = IM_COL32(200, 120, 255, 255), halo = IM_COL32(170, 80, 255, 70);
            dl->AddCircleFilled(c, u * 0.78f, halo, 24);
            dl->AddCircleFilled(c, u * 0.62f, IM_COL32(150, 152, 162, 255), 24);
            dl->AddCircleFilled(P(-0.18f, -0.2f), u * 0.24f, IM_COL32(190, 192, 200, 255), 14);       // the shine
            dl->AddLine(P(-0.62f, 0), P(0.62f, 0), glow, th * 1.6f);                                    // the band round the middle
            dl->AddBezierQuadratic(P(0, -0.62f), P(0.42f, 0), P(0, 0.62f), glow, th * 1.4f, 12);       // and the ones over the top
            dl->AddBezierQuadratic(P(0, -0.62f), P(-0.42f, 0), P(0, 0.62f), glow, th * 1.4f, 12);
            dl->AddCircleFilled(P(0, -0.62f), u * 0.12f, IM_COL32(70, 72, 82, 255), 10);              // the cap
            dl->AddCircle(c, u * 0.62f, IM_COL32(60, 62, 72, 255), 24, th * 0.8f);
            break;
        }
        case ItemId::KokiriTunic: tunic(green); break;
        case ItemId::GoronTunic: tunic(red); break;
        case ItemId::ZoraTunic: tunic(blue); break;
        case ItemId::KokiriBoots: boot(wood); break;
        case ItemId::IronBoots: boot(IM_COL32(110, 115, 130, 255)); break;
        case ItemId::HoverBoots: boot(IM_COL32(150, 110, 70, 255)); dl->AddLine(P(-0.3f, 0.85f), P(0.7f, 0.85f), cyan, th * 1.4f); break;
        case ItemId::GoronBracelet: dl->AddCircle(c, u * 0.55f, IM_COL32(180, 130, 70, 255), 20, th * 2.4f); break;
        case ItemId::SilverGauntlets: dl->AddRectFilled(P(-0.55f, -0.45f), P(0.55f, 0.65f), steel, 6.0f); dl->AddRectFilled(P(-0.55f, -0.65f), P(0.55f, -0.35f), IM_COL32(150, 160, 175, 255), 3.0f); break;
        case ItemId::GoldenGauntlets: dl->AddRectFilled(P(-0.55f, -0.45f), P(0.55f, 0.65f), gold, 6.0f); dl->AddRectFilled(P(-0.55f, -0.65f), P(0.55f, -0.35f), IM_COL32(200, 150, 40, 255), 3.0f); break;
        case ItemId::KeatonMask: mask(IM_COL32(230, 170, 70, 255)); break;
        case ItemId::SkullMask: mask(white); break;
        case ItemId::SpookyMask: mask(IM_COL32(210, 90, 60, 255)); break;
        case ItemId::BunnyHood: mask(IM_COL32(245, 235, 240, 255)); dl->AddRectFilled(P(-0.4f, -0.95f), P(-0.2f, -0.4f), white, 4.0f); dl->AddRectFilled(P(0.2f, -0.95f), P(0.4f, -0.4f), white, 4.0f); break;
        case ItemId::GoronMask: mask(IM_COL32(190, 110, 60, 255)); break;
        case ItemId::ZoraMask: mask(IM_COL32(90, 150, 230, 255)); break;
        case ItemId::GerudoMask: mask(IM_COL32(230, 200, 90, 255)); break;
        case ItemId::MaskOfTruth: mask(IM_COL32(120, 90, 190, 255)); dl->AddCircle(P(0, -0.12f), u * 0.18f, gold, 10, th); break;
        case ItemId::SilverScale: dl->AddCircleFilled(c, u * 0.58f, steel, 20); dl->AddCircle(c, u * 0.58f, white, 20, th); break;
        case ItemId::GoldenScale: dl->AddCircleFilled(c, u * 0.58f, gold, 20); dl->AddCircle(c, u * 0.58f, white, 20, th); break;
        case ItemId::BigQuiver: dl->AddRectFilled(P(-0.3f, -0.1f), P(0.3f, 0.8f), wood, 4.0f); dl->AddLine(P(-0.12f, -0.1f), P(-0.12f, -0.8f), steel, th); dl->AddLine(P(0.12f, -0.1f), P(0.12f, -0.8f), steel, th); break;
        case ItemId::BulletBag: case ItemId::BombBag:
            dl->AddCircleFilled(P(0, 0.2f), u * 0.58f, id == ItemId::BombBag ? IM_COL32(60, 80, 140, 255) : IM_COL32(150, 110, 60, 255), 18);
            dl->AddRectFilled(P(-0.25f, -0.55f), P(0.25f, -0.2f), IM_COL32(190, 150, 90, 255), 3.0f);
            break;
        case ItemId::ForestMedallion: medal(green); break;
        case ItemId::FireMedallion: medal(red); break;
        case ItemId::WaterMedallion: medal(blue); break;
        case ItemId::SpiritMedallion: medal(orange); break;
        case ItemId::ShadowMedallion: medal(purple); break;
        case ItemId::LightMedallion: medal(IM_COL32(255, 240, 140, 255)); break;
        case ItemId::KokiriEmerald: gem(green); break;
        case ItemId::GoronRuby: gem(red); break;
        case ItemId::ZoraSapphire: gem(blue); break;
        default: dl->AddCircleFilled(c, u * 0.5f, tier, 16); break;
    }
}

// The item bar, like Fortnite's: weapons (the one in hand highlighted), shield, potions and your ability. D-pad Left cycles weapons;
// on a touch screen you can tap a slot.
void DrawHotbar(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const royale::HudState& h) {
    struct Slot { std::string title, sub; ImU32 border; bool filled, selected; float cooldown; int action; royale::ItemId icon; int ammo = -1; };
    const ImU32 grey = IM_COL32(120, 112, 100, 255);
    const royale::ItemId none = royale::ItemId::DekuStick;
    std::vector<Slot> slots;
    auto ammoOf = [&](royale::ItemId id) { const royale::AmmoKind k = royale::AmmoUsedBy(id); return k == royale::AmmoKind::None ? -1 : h.ammo[static_cast<size_t>(k)]; };
    slots.push_back({ ShortName(h.weapon), "", RarityU32(h.weaponRarity), true, true, 0.0f, 0, h.weapon, ammoOf(h.weapon) });
    for (int i = 0; i < royale::kMaxReserveWeapons; i++) {
        if (i < static_cast<int>(h.inv.reserve.size())) {
            const auto& r = h.inv.reserve[i];
            const royale::ItemId id = static_cast<royale::ItemId>(r.item);
            slots.push_back({ "", "", RarityU32(static_cast<royale::Rarity>(r.rarity)), true, false, 0.0f, i + 1, id, ammoOf(id) });
        } else {
            slots.push_back({ "", "", grey, false, false, 0.0f, 0, none });
        }
    }
    if (h.hasShield) slots.push_back({ "", "", RarityU32(h.shieldRarity), true, false, 0.0f, 0, h.shield });
    else slots.push_back({ "", "", grey, false, false, 0.0f, 0, none });
    if (!h.inv.potions.empty()) {
        const auto& p = h.inv.potions.front();
        const royale::ItemId id = static_cast<royale::ItemId>(p.item);
        slots.push_back({ "", "x" + std::to_string(h.inv.potions.size()), RarityU32(static_cast<royale::Rarity>(p.rarity)), true, false, 0.0f, 10, id });
    } else {
        slots.push_back({ "", "", grey, false, false, 0.0f, 10, none });
    }
    if (h.inv.hasAbility) {
        const royale::ItemId id = static_cast<royale::ItemId>(h.inv.ability.item);
        const float cd = royale::AbilityOf(id).cooldown;
        const bool lowMagic = h.magic + 0.001f < royale::AbilityMagic(id);
        slots.push_back({ "", h.abilityReadyIn > 0.05f ? ClockText(h.abilityReadyIn) : lowMagic ? "NO MAGIC" : "", RarityU32(static_cast<royale::Rarity>(h.inv.ability.rarity)), true, false,
                          cd > 0 ? std::min(1.0f, h.abilityReadyIn / cd) : 0.0f, 11, id });
    } else {
        slots.push_back({ "", "", grey, false, false, 0.0f, 11, none });
    }

    const float w = 84.0f * scale, hgt = 76.0f * scale, gap = 8.0f * scale;
    const float total = slots.size() * w + (slots.size() - 1) * gap;
    float x = (ds.x - total) * 0.5f;
    const float y = ds.y - hgt - 20.0f * scale;
    ImGuiIO& io = ImGui::GetIO();
    const bool tap = ImGui::IsMouseClicked(0) && !io.WantCaptureMouse;
    for (size_t i = 0; i < slots.size(); i++) {
        const Slot& sl = slots[i];
        const ImVec2 a(x, y), b(x + w, y + hgt);
        dl->AddRectFilled(a, b, OotPanel(195), 6.0f * scale);
        if (sl.filled) DrawItemIcon(dl, sl.icon, ImVec2((a.x + b.x) * 0.5f, a.y + hgt * 0.4f), hgt * 0.5f, sl.border);
        if (sl.cooldown > 0) dl->AddRectFilled(a, ImVec2(b.x, a.y + hgt * sl.cooldown), IM_COL32(0, 0, 0, 150), 6.0f * scale);
        dl->AddRect(a, b, sl.selected ? IM_COL32(255, 236, 120, 255) : sl.border, 6.0f * scale, 0, (sl.selected ? 4.0f : 2.5f) * scale);
        if (sl.filled) {   // which button uses the slot
            const int nres = royale::kMaxReserveWeapons;
            const char* hint = i == 0 ? "B" : static_cast<int>(i) <= nres ? "D-pad L/R" : static_cast<int>(i) == nres + 1 ? "C-Left" : static_cast<int>(i) == nres + 2 ? "D-pad Dn" : "D-pad Up";
            const float hs = 12.5f * scale;
            const ImVec2 hsz = font->CalcTextSizeA(hs, FLT_MAX, 0.0f, hint);
            dl->AddText(font, hs, ImVec2((a.x + b.x - hsz.x) * 0.5f + 1, b.y + 2 * scale + 1), IM_COL32(0, 0, 0, 220), hint);
            const ImU32 hintCol = i == 0 ? IM_COL32(100, 230, 110, 255) : static_cast<int>(i) == nres + 1 ? IM_COL32(255, 220, 40, 255) : IM_COL32(210, 205, 190, 235); // B green, C yellow, as on the N64 pad
            dl->AddText(font, hs, ImVec2((a.x + b.x - hsz.x) * 0.5f, b.y + 2 * scale), hintCol, hint);
        }
        const float ts = 11.5f * scale;
        dl->AddText(font, ts, ImVec2(a.x + 5 * scale, b.y - 17 * scale), IM_COL32(255, 255, 255, sl.filled ? 255 : 120), sl.title.c_str());
        dl->AddText(font, ts * 0.9f, ImVec2(a.x + 5 * scale, a.y + 3 * scale), sl.filled ? sl.border : grey, sl.sub.c_str());
        if (sl.filled && sl.ammo >= 0) { // how many shots are left
            const std::string n = "x" + std::to_string(sl.ammo);
            const ImVec2 nsz = font->CalcTextSizeA(ts * 1.15f, FLT_MAX, 0.0f, n.c_str());
            dl->AddText(font, ts * 1.15f, ImVec2(b.x - nsz.x - 5 * scale + 1, a.y + 4 * scale + 1), IM_COL32(0, 0, 0, 220), n.c_str());
            dl->AddText(font, ts * 1.15f, ImVec2(b.x - nsz.x - 5 * scale, a.y + 4 * scale), sl.ammo > 0 ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 90, 90, 255), n.c_str());
        }
        if (tap && io.MousePos.x >= a.x && io.MousePos.x <= b.x && io.MousePos.y >= a.y && io.MousePos.y <= b.y) {
            if (sl.action >= 1 && sl.action <= royale::kMaxReserveWeapons) gSession.SelectWeapon(sl.action);
            else if (sl.action == 10 && !h.inv.potions.empty()) gSession.RequestUsePotion();
            else if (sl.action == 11 && h.inv.hasAbility && h.abilityReadyIn <= 0.05f && h.magic + 0.001f >= royale::AbilityMagic(static_cast<royale::ItemId>(h.inv.ability.item))) gSession.UseAbility();
        }
        x += w + gap;
    }

    // Money, at the right end of the bar.
    {
        const std::string n = std::to_string(h.rupees);
        const float ts = 20.0f * scale;
        const ImVec2 nsz = font->CalcTextSizeA(ts, FLT_MAX, 0.0f, n.c_str());
        const float rx = (ds.x + total) * 0.5f, ry = y - 34.0f * scale;
        DrawItemIcon(dl, royale::ItemId::Rupees, ImVec2(rx - nsz.x - 22.0f * scale, ry + 12.0f * scale), 26.0f * scale, IM_COL32(110, 240, 130, 255));
        dl->AddText(font, ts, ImVec2(rx - nsz.x + 1, ry + 1), IM_COL32(0, 0, 0, 220), n.c_str());
        dl->AddText(font, ts, ImVec2(rx - nsz.x, ry), IM_COL32(150, 255, 160, 255), n.c_str());
    }

    // What you wear: one small icon per gear slot, just above the bar (tunic, boots, gauntlets, mask, scale, pack, charm).
    float gx = (ds.x - total) * 0.5f;
    const float gs = 30.0f * scale, gy = y - gs - 10.0f * scale;
    for (int i = 0; i < royale::kGearSlots; i++) {
        if (!(h.inv.gearMask & (1 << i))) continue;
        const royale::ItemId id = static_cast<royale::ItemId>(h.inv.gear[i].item);
        const ImU32 col = RarityU32(static_cast<royale::Rarity>(h.inv.gear[i].rarity));
        const ImVec2 a(gx, gy), b(gx + gs, gy + gs);
        dl->AddRectFilled(a, b, OotPanel(195), 5.0f * scale);
        DrawItemIcon(dl, id, ImVec2(gx + gs * 0.5f, gy + gs * 0.5f), gs * 0.72f, col);
        dl->AddRect(a, b, col, 5.0f * scale, 0, 2.0f * scale);
        gx += gs + 5.0f * scale;
    }
}

// ---- the emote wheel ---------------------------------------------------------------------------------------------------------------
// Hold C-Right and the wheel comes up in the middle of the screen: point the stick at an emote and let go of C-Right to do it (or press A).
// A quick tap of C-Right does the last one again; B closes it. By touch or mouse, tap EMOTE (bottom right), then tap an emote.
// While it is up the stick picks an emote instead of moving Link. It is drawn in the look of the game's pause menu: the text-box black,
// gold rules and the pulsing corner cursor.
struct EmoteWheel {
    bool open = false;
    bool byTouch = false;   // opened with the EMOTE button: stays up until an emote (or anywhere else) is tapped
    double openedAt = 0;
    int hover = -1;         // the emote pointed at, -1 for none yet
};
EmoteWheel gWheel;
int gLastEmote = 0;         // what a quick tap of C-Right does again
void StartEmote(int index, const royale::HudState& hud); // below, with the emote logic
bool CanEmote(const royale::HudState& hud);

void OpenEmoteWheel(bool byTouch) {
    gWheel = EmoteWheel{};
    gWheel.open = true;
    gWheel.byTouch = byTouch;
    gWheel.openedAt = ImGui::GetTime();
    Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}
void CloseEmoteWheel() { gWheel = EmoteWheel{}; }

// Which emote a direction points at: x to the right, y up. The emotes go clockwise from the top.
int WheelSliceAt(float x, float y) {
    float turns = std::atan2(x, y) / 6.2831853f;
    if (turns < 0.0f) turns += 1.0f;
    return static_cast<int>(turns * royale::kEmoteCount + 0.5f) % royale::kEmoteCount;
}

void WheelText(ImDrawList* dl, ImFont* font, float size, ImVec2 centre, ImU32 col, const char* text) {
    const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0, text);
    const ImVec2 at(centre.x - sz.x * 0.5f, centre.y - sz.y * 0.5f);
    const float o = std::max(1.5f, size * 0.06f);
    for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) if (dx || dy) dl->AddText(font, size, ImVec2(at.x + dx * o, at.y + dy * o), IM_COL32(20, 10, 0, 235), text);
    dl->AddText(font, size, at, col, text);
}

void DrawEmotes(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const royale::HudState& h) {
    if (!h.selfAlive || !InField()) { if (gWheel.open) CloseEmoteWheel(); return; }
    ImGuiIO& io = ImGui::GetIO();
    const bool tap = ImGui::IsMouseClicked(0) && !io.WantCaptureMouse;
    const ImU32 gold = IM_COL32(255, 214, 90, 255), goldDark = IM_COL32(176, 118, 24, 255);
    const ImU32 lit = IM_COL32(255, 236, 120, 255);
    bool used = false;   // a tap the wheel has dealt with

    // The EMOTE button, bottom right.
    {
        const float w = 128.0f * scale, hgt = 40.0f * scale;
        const ImVec2 a(ds.x - w - 18.0f * scale, ds.y - hgt - 24.0f * scale), b(a.x + w, a.y + hgt);
        dl->AddRectFilled(a, b, OotPanel(200), 6.0f * scale);
        dl->AddRect(a, b, gWheel.open ? lit : goldDark, 6.0f * scale, 0, 2.5f * scale);
        WheelText(dl, font, 16.0f * scale, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), IM_COL32(255, 255, 255, 255), "EMOTE");
        if (tap && io.MousePos.x >= a.x && io.MousePos.x <= b.x && io.MousePos.y >= a.y && io.MousePos.y <= b.y) {
            used = true;
            if (gWheel.open) CloseEmoteWheel();
            else if (CanEmote(h)) OpenEmoteWheel(true);
        }
    }
    if (!gWheel.open) return;

    const ImVec2 c(ds.x * 0.5f, ds.y * 0.5f);
    const float outer = 168.0f * scale, inner = 58.0f * scale, mid = (outer + inner) * 0.5f;
    const int n = royale::kEmoteCount;
    const float t = static_cast<float>(ImGui::GetTime() - gWheel.openedAt);
    const float grow = std::min(1.0f, t / 0.12f);   // pops open
    const float R = outer * (0.85f + 0.15f * grow), r = inner;

    // The mouse or a finger points at an emote too.
    const float mx = io.MousePos.x - c.x, my = io.MousePos.y - c.y, md = std::sqrt(mx * mx + my * my);
    if (gWheel.byTouch && md > r && md < R + 30.0f * scale) gWheel.hover = WheelSliceAt(mx, -my);

    // The ring: the text-box black, a gold rule round each edge, and spokes between the emotes.
    dl->AddCircleFilled(ImVec2(c.x + 5 * scale, c.y + 6 * scale), R, IM_COL32(0, 0, 0, 70), 64);
    for (int i = 0; i < n; i++) {
        const float a0 = (i - 0.5f) / n * 6.2831853f - 1.5707963f, a1 = (i + 0.5f) / n * 6.2831853f - 1.5707963f;
        dl->PathClear();
        dl->PathArcTo(c, R, a0, a1, 24);
        dl->PathArcTo(c, r, a1, a0, 12);
        dl->PathFillConvex(i == gWheel.hover ? IM_COL32(70, 52, 20, 235) : OotPanel(215));
    }
    dl->AddCircle(c, R, goldDark, 64, 2.0f * scale);
    dl->AddCircle(c, R - 5 * scale, (gold & 0x00FFFFFF) | 0x8C000000, 64, 1.0f * scale);
    dl->AddCircle(c, r, goldDark, 48, 2.0f * scale);
    for (int i = 0; i < n; i++) {
        const float a = (i + 0.5f) / n * 6.2831853f - 1.5707963f;
        dl->AddLine(ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r), ImVec2(c.x + std::cos(a) * (R - 5 * scale), c.y + std::sin(a) * (R - 5 * scale)),
                    (goldDark & 0x00FFFFFF) | 0xB4000000, 1.5f * scale);
    }
    // The names, and the pulsing corner cursor round the one pointed at.
    for (int i = 0; i < n; i++) {
        const float a = static_cast<float>(i) / n * 6.2831853f - 1.5707963f;
        const ImVec2 p(c.x + std::cos(a) * mid, c.y + std::sin(a) * mid);
        const bool sel = i == gWheel.hover;
        // Two short lines at most: the names are split at the first space when they are long.
        std::string name = royale::kEmoteNames[i];
        const size_t space = name.find(' ');
        const float size = 15.0f * scale;
        if (name.size() > 9 && space != std::string::npos) {
            WheelText(dl, font, size, ImVec2(p.x, p.y - size * 0.55f), sel ? IM_COL32(255, 255, 150, 255) : IM_COL32(236, 228, 190, 255), name.substr(0, space).c_str());
            WheelText(dl, font, size, ImVec2(p.x, p.y + size * 0.55f), sel ? IM_COL32(255, 255, 150, 255) : IM_COL32(236, 228, 190, 255), name.substr(space + 1).c_str());
        } else {
            WheelText(dl, font, size, p, sel ? IM_COL32(255, 255, 150, 255) : IM_COL32(236, 228, 190, 255), name.c_str());
        }
        if (sel) {
            const float pulse = 0.5f + 0.5f * std::sin(t * 6.0f);
            const float hw = 44.0f * scale + 3.0f * scale * pulse, hh = 24.0f * scale + 3.0f * scale * pulse, len = 10.0f * scale, w = 3.0f * scale;
            const ImU32 col = (lit & 0x00FFFFFF) | (static_cast<ImU32>(255 * (0.75f + 0.25f * pulse)) << 24);
            const ImVec2 q0(p.x - hw, p.y - hh), q1(p.x + hw, p.y + hh);
            dl->AddLine(q0, ImVec2(q0.x + len, q0.y), col, w); dl->AddLine(q0, ImVec2(q0.x, q0.y + len), col, w);
            dl->AddLine(ImVec2(q1.x, q0.y), ImVec2(q1.x - len, q0.y), col, w); dl->AddLine(ImVec2(q1.x, q0.y), ImVec2(q1.x, q0.y + len), col, w);
            dl->AddLine(ImVec2(q0.x, q1.y), ImVec2(q0.x + len, q1.y), col, w); dl->AddLine(ImVec2(q0.x, q1.y), ImVec2(q0.x, q1.y - len), col, w);
            dl->AddLine(q1, ImVec2(q1.x - len, q1.y), col, w); dl->AddLine(q1, ImVec2(q1.x, q1.y - len), col, w);
        }
    }
    // The middle: what will happen, and how.
    dl->AddCircleFilled(c, r - 3 * scale, OotPanel(230), 48);
    WheelText(dl, font, 17.0f * scale, ImVec2(c.x, c.y - 9 * scale), gold, "EMOTE");
    WheelText(dl, font, 12.0f * scale, ImVec2(c.x, c.y + 11 * scale), IM_COL32(236, 228, 190, 255), gWheel.byTouch ? "tap one" : "B: close");

    if (tap && !used) {
        if (md > r && md < R + 30.0f * scale) {
            const int pick = WheelSliceAt(mx, -my);
            CloseEmoteWheel();
            StartEmote(pick, h);
        } else {
            CloseEmoteWheel();   // a tap anywhere else puts it away
        }
    }
}

// The end-of-match standings.
// The replay of the match that just ended, as a top-down map at high speed beside the results: the storm closing in, everybody's dots, trails for you,
// red flashes where somebody was eliminated. It loops.
void DrawReplay(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const royale::HudState& h) {
    royale::GameClient* client = gSession.Client();
    if (client == nullptr || !client->ReplayComplete() || h.map.radius <= 0) return;
    const royale::Replay& rp = client->GetReplay();
    const float pw = std::min(ds.x * 0.9f, 640.0f * scale);
    const float room = (ds.x - pw) * 0.5f - 24.0f * scale;
    const float side = std::min(room, ds.y * 0.5f);
    if (side < 150.0f * scale) return;                                  // no room beside the results on a small screen
    const ImVec2 a(24.0f * scale, ds.y * 0.28f), b(a.x + side, a.y + side);
    dl->AddRectFilled(ImVec2(a.x - 8 * scale, a.y - 30 * scale), ImVec2(b.x + 8 * scale, b.y + 28 * scale), OotPanel(225), 10.0f * scale);
    dl->AddRect(ImVec2(a.x - 8 * scale, a.y - 30 * scale), ImVec2(b.x + 8 * scale, b.y + 28 * scale), IM_COL32(255, 210, 70, 255), 10.0f * scale, 0, 2.0f * scale);
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    const float k = side * 0.5f / h.map.radius * 0.97f;
    auto toPanel = [&](float x, float z) { return ImVec2(c.x + (x - h.map.center.x) * k, c.y - (z - h.map.center.z) * k); };
    const double cycle = rp.frames.size() * 0.12 + 2.5;
    const double t = std::fmod(ImGui::GetTime(), cycle);
    const float fpos = static_cast<float>(std::min(t / 0.12, static_cast<double>(rp.frames.size() - 1)));
    const int f0 = static_cast<int>(fpos);
    const int f1 = std::min<int>(f0 + 1, static_cast<int>(rp.frames.size()) - 1);
    const float mix = fpos - f0;
    dl->AddText(font, 20.0f * scale, ImVec2(a.x, a.y - 27 * scale), IM_COL32(255, 210, 70, 255), "REPLAY");
    // the map, the storm (purple outside the safe circle) and the safe circle
    dl->AddCircleFilled(c, h.map.radius * k, IM_COL32(110, 50, 170, 150), 72);
    if (const royale::Storm* storm = client->GetStorm()) {
        const royale::Circle safe = storm->SafeZoneAt(f0 * royale::kReplayStepSec);
        dl->AddCircleFilled(toPanel(safe.center.x, safe.center.z), safe.radius * k, IM_COL32(26, 52, 40, 255), 64);
        dl->AddCircle(toPanel(safe.center.x, safe.center.z), safe.radius * k, IM_COL32(255, 255, 255, 230), 64, 1.5f * scale);
    }
    dl->AddCircle(c, h.map.radius * k, IM_COL32(200, 160, 255, 255), 72, 1.5f * scale);
    int alive = 0;
    for (size_t i = 0; i < rp.ids.size(); i++) {
        const int16_t x0 = rp.frames[static_cast<size_t>(f0)][i * 2], x1 = rp.frames[static_cast<size_t>(f1)][i * 2];
        if (x0 == royale::kReplayGone) continue;
        alive++;
        const float z0 = rp.frames[static_cast<size_t>(f0)][i * 2 + 1], z1 = rp.frames[static_cast<size_t>(f1)][i * 2 + 1];
        const float px = x1 == royale::kReplayGone ? x0 : x0 + (x1 - x0) * mix, pz = x1 == royale::kReplayGone ? z0 : z0 + (z1 - z0) * mix;
        const bool self = rp.ids[i] == h.selfId, winner = rp.ids[i] == h.winnerId;
        if (self || winner) {   // a trail behind you and the winner
            ImVec2 prev = toPanel(px, pz);
            for (int back = 1; back <= 10 && f0 - back >= 0; back++) {
                const auto& fr = rp.frames[static_cast<size_t>(f0 - back)];
                if (fr[i * 2] == royale::kReplayGone) break;
                const ImVec2 q = toPanel(fr[i * 2], fr[i * 2 + 1]);
                dl->AddLine(prev, q, self ? IM_COL32(255, 220, 90, 190 - back * 15) : IM_COL32(120, 255, 150, 190 - back * 15), 2.0f * scale);
                prev = q;
            }
        }
        const ImU32 col = self ? IM_COL32(255, 220, 90, 255) : winner ? IM_COL32(120, 255, 150, 255) : rp.ids[i] >= 1000 ? IM_COL32(190, 190, 205, 255) : IM_COL32(255, 255, 255, 255);
        dl->AddCircleFilled(toPanel(px, pz), (self || winner ? 4.5f : 3.0f) * scale, col, 10);
    }
    // eliminations: a red ring that grows and fades where the player was last seen
    for (const royale::ReplayKill& kill : rp.kills) {
        const float age = fpos - kill.frame;
        if (age < 0.0f || age > 6.0f) continue;
        size_t col = rp.ids.size();
        for (size_t i = 0; i < rp.ids.size(); i++) if (rp.ids[i] == kill.victim) col = i;
        if (col == rp.ids.size()) continue;
        const int fr = std::max(0, static_cast<int>(kill.frame) - 1);
        const ImVec2 at = toPanel(rp.frames[static_cast<size_t>(fr)][col * 2], rp.frames[static_cast<size_t>(fr)][col * 2 + 1]);
        dl->AddCircle(at, (5.0f + age * 3.5f) * scale, IM_COL32(255, 70, 60, static_cast<int>(255 * (1.0f - age / 6.0f))), 16, 2.0f * scale);
    }
    char caption[48];
    const int secs = static_cast<int>(f0 * royale::kReplayStepSec);
    std::snprintf(caption, sizeof(caption), "%d:%02d    %d alive", secs / 60, secs % 60, alive);
    dl->AddText(font, 17.0f * scale, ImVec2(a.x, b.y + 5 * scale), IM_COL32(235, 235, 240, 255), caption);
}

void DrawResultsPanel(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const royale::HudState& h) {
    if (h.results.empty()) return;
    const float pw = std::min(ds.x * 0.9f, 640.0f * scale), rowH = 26.0f * scale;
    const int shown = std::min<int>(8, static_cast<int>(h.results.size()));
    const float ph = (shown + 4) * rowH + 70.0f * scale;
    const ImVec2 a((ds.x - pw) * 0.5f, ds.y * 0.28f), b(a.x + pw, a.y + ph);
    dl->AddRectFilled(a, b, OotPanel(225), 10.0f * scale);
    dl->AddRect(a, b, IM_COL32(255, 210, 70, 255), 10.0f * scale, 0, 3.0f * scale);
    auto put = [&](float x, float y, ImU32 col, float size, const std::string& t) { dl->AddText(font, size, ImVec2(x, y), col, t.c_str()); };
    float y = a.y + 10.0f * scale;
    put(a.x + 18 * scale, y, IM_COL32(255, 210, 70, 255), 24 * scale, "RESULTS");
    y += rowH * 1.3f;
    const float cName = a.x + 60 * scale, cKills = a.x + pw - 270 * scale, cDmg = a.x + pw - 190 * scale, cPts = a.x + pw - 90 * scale;
    const ImU32 head = IM_COL32(170, 170, 180, 255);
    put(a.x + 18 * scale, y, head, 15 * scale, "#"); put(cName, y, head, 15 * scale, "Player"); put(cKills, y, head, 15 * scale, "Kills");
    put(cDmg, y, head, 15 * scale, "Damage"); put(cPts, y, head, 15 * scale, "Points");
    y += rowH * 0.9f;
    bool selfShown = false;
    auto row = [&](int rank, const royale::ResultsRow& r) {
        const ImU32 col = r.self ? IM_COL32(120, 255, 140, 255) : IM_COL32(235, 235, 240, 255);
        put(a.x + 18 * scale, y, col, 17 * scale, std::to_string(rank));
        put(cName, y, col, 17 * scale, r.name + (r.placement == 1 ? "  (winner)" : ""));
        put(cKills, y, col, 17 * scale, std::to_string(r.kills));
        char dmg[16]; std::snprintf(dmg, sizeof(dmg), "%.1f", r.damage);
        put(cDmg, y, col, 17 * scale, dmg);
        put(cPts, y, col, 17 * scale, std::to_string(r.score));
        y += rowH;
    };
    for (int i = 0; i < shown; i++) { row(i + 1, h.results[i]); selfShown |= h.results[i].self; }
    if (!selfShown) {
        for (size_t i = 0; i < h.results.size(); i++) if (h.results[i].self) { put(a.x + 18 * scale, y, head, 15 * scale, "..."); y += rowH * 0.8f; row(static_cast<int>(i) + 1, h.results[i]); }
    }
    y += 6.0f * scale;
    const std::string footer = h.isHost ? "Press A to play again with everyone who is here" : "Waiting for the host to play again...";
    ImVec2 sz = font->CalcTextSizeA(18 * scale, FLT_MAX, 0.0f, footer.c_str());
    put(a.x + (pw - sz.x) * 0.5f, b.y - 32 * scale, IM_COL32(255, 236, 140, 255), 18 * scale, footer);
}

// The splash shown at the start of every match's countdown, in the look of OoT's title and file-select screens: deep blue-green
// night, gold lettering with a dark drop shadow, a gold Triforce and a double gold border. It covers the screen for the first
// kSplashSeconds of the countdown (fading in and out), which is also when everyone is being moved to Hyrule Field.
constexpr float kSplashSeconds = 5.0f;

// ---- the logo ---------------------------------------------------------------------------------------------------------------
// The picture in mod/Royale/logo_data.h (made from assets/logo.png by scripts/make_logo_assets.py) is turned into a texture the first time it is
// wanted, and drawn on the title screen, the file select screen, the match splash and the top of the menu.
// The game's own title screen (the Zelda logo that fades in when you press start) draws this picture instead of its own logo: patches/0010 calls here.
// Plain RGBA bytes, the same picture as everywhere else; returns null if there is none.
extern "C" const unsigned char* Royale_TitleLogoRGBA(int* width, int* height, int* destWidth, int* destHeight) {
    static std::vector<uint8_t> rgba;
    static bool built = false;
    if (!built) {
        built = true;
        if (royale::kLogoW > 0 && royale::kLogoH > 0 && royale::kLogoRuns > 0) {
            rgba.assign(static_cast<size_t>(royale::kLogoW) * royale::kLogoH * 4, 0);
            size_t px = 0;
            for (int r = 0; r < royale::kLogoRuns; r++) {
                const int count = royale::kLogoRle[r * 3], idx = royale::kLogoRle[r * 3 + 1], alpha = royale::kLogoRle[r * 3 + 2];
                for (int k = 0; k < count && px * 4 < rgba.size(); k++, px++) {
                    if (idx == 0) continue;
                    rgba[px * 4 + 0] = royale::kLogoPalette[(idx - 1) * 3 + 0];
                    rgba[px * 4 + 1] = royale::kLogoPalette[(idx - 1) * 3 + 1];
                    rgba[px * 4 + 2] = royale::kLogoPalette[(idx - 1) * 3 + 2];
                    rgba[px * 4 + 3] = static_cast<uint8_t>(alpha);
                }
            }
        }
    }
    if (rgba.empty()) return nullptr;
    *width = royale::kLogoW;
    *height = royale::kLogoH;
    *destHeight = 172;   // in the title's 320 x 240 picture
    *destWidth = static_cast<int>(172.0f * royale::kLogoW / royale::kLogoH + 0.5f);
    return rgba.data();
}

ImTextureID LogoTexture(ImVec2* size = nullptr) {
    static ImTextureID tex = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        std::vector<uint8_t> rgba(static_cast<size_t>(royale::kLogoW) * royale::kLogoH * 4, 0);
        size_t px = 0;
        for (int r = 0; r < royale::kLogoRuns; r++) {
            const int count = royale::kLogoRle[r * 3], idx = royale::kLogoRle[r * 3 + 1], alpha = royale::kLogoRle[r * 3 + 2];
            for (int k = 0; k < count && px * 4 < rgba.size(); k++, px++) {
                if (idx == 0) continue;
                rgba[px * 4 + 0] = royale::kLogoPalette[(idx - 1) * 3 + 0];
                rgba[px * 4 + 1] = royale::kLogoPalette[(idx - 1) * 3 + 1];
                rgba[px * 4 + 2] = royale::kLogoPalette[(idx - 1) * 3 + 2];
                rgba[px * 4 + 3] = static_cast<uint8_t>(alpha);
            }
        }
        GfxRenderingAPI* api = gfx_get_current_rendering_api();
        if (api != nullptr) {
            const int id = api->new_texture();
            api->select_texture(0, id);
            api->set_sampler_parameters(0, true, 0, 0);
            api->upload_texture(rgba.data(), royale::kLogoW, royale::kLogoH);
            tex = reinterpret_cast<ImTextureID>(static_cast<intptr_t>(id));   // (OpenGL: the texture id is the ImGui texture id)
        }
    }
    if (size) *size = ImVec2(static_cast<float>(royale::kLogoW), static_cast<float>(royale::kLogoH));
    return tex;
}

// The logo `width` wide with its top edge at `top`, centred on `cx`. Returns the height drawn (0 if there is no logo).
float DrawLogo(ImDrawList* dl, float cx, float top, float width, int alpha = 255, bool plaque = false) {
    ImVec2 sz;
    ImTextureID tex = LogoTexture(&sz);
    if (tex == nullptr || sz.x <= 0) return 0.0f;
    const float height = width * sz.y / sz.x;
    if (plaque) {   // the logo has black lettering: on a dark screen it sits on a pale rounded plaque
        const float pad = width * 0.04f;
        dl->AddRectFilled(ImVec2(cx - width * 0.5f - pad, top - pad), ImVec2(cx + width * 0.5f + pad, top + height + pad), IM_COL32(246, 241, 229, alpha * 235 / 255), pad * 1.6f);
    }
    dl->AddImage(tex, ImVec2(cx - width * 0.5f, top), ImVec2(cx + width * 0.5f, top + height), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, alpha));
    return height;
}

// The game's title screen and file select screen (nothing of the game is running yet): our logo takes the place of the 3D title on the first, and
// sits at the top of the second.
// Which of the game's own screens is running is read from the game state itself: its title (the Nintendo 64 logo) or its file select. The moment the
// game moves on to anything else (the game itself, the intro cutscene) the logo is gone.
void DrawTitleLogo() {
    if (InGame() || gGameState == nullptr) return;
    const int mode = gGameState->init == Title_Init ? GAMEMODE_TITLE_SCREEN : gGameState->init == FileChoose_Init ? GAMEMODE_FILE_SELECT : -1;
    if (mode < 0) return;
    ImVec2 sz;
    if (LogoTexture(&sz) == nullptr) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float aspect = sz.x / sz.y;
    static int lastMode = -1;
    static double shownAt = 0.0;
    if (mode != lastMode) { lastMode = mode; shownAt = ImGui::GetTime(); }
    if (mode == GAMEMODE_TITLE_SCREEN) {
        // nothing: the game's own title screen now draws the logo (patches/0010), fading in with its own fade
    } else {
        const float width = std::min(ds.x * 0.30f, ds.y * 0.30f * aspect);
        DrawLogo(dl, ds.x * 0.5f, ds.y * 0.025f, width, 245, true);
    }
    // the build number, small, bottom right
    const std::string version = std::string("Version ") + ROYALE_BUILD_VERSION;
    const float vs = std::clamp(ds.y / 720.0f, 0.8f, 2.2f) * 18.0f;
    const ImVec2 vsz = ImGui::GetFont()->CalcTextSizeA(vs, FLT_MAX, 0.0f, version.c_str());
    const ImU32 vcol = mode == GAMEMODE_TITLE_SCREEN ? IM_COL32(60, 50, 40, 255) : IM_COL32(255, 255, 255, 235);
    dl->AddText(ImGui::GetFont(), vs, ImVec2(ds.x - vsz.x - 14.0f, ds.y - vsz.y - 10.0f), vcol, version.c_str());
}

void DrawSplash(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, float secondsIn) {
    const float fadeIn = std::clamp(secondsIn / 0.6f, 0.0f, 1.0f);
    const float fadeOut = std::clamp((kSplashSeconds - secondsIn) / 0.6f, 0.0f, 1.0f);
    const float a = std::min(fadeIn, fadeOut);
    if (a <= 0.0f) return;
    auto A = [&](int v) { return static_cast<int>(v * a); };
    const ImU32 gold = IM_COL32(255, 214, 90, A(255)), goldDark = IM_COL32(176, 118, 24, A(255)), shadow = IM_COL32(8, 14, 10, A(235)),
                cream = IM_COL32(236, 228, 190, A(255));

    // Night sky: top and bottom of a vertical gradient, with a vignette.
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ds, IM_COL32(6, 22, 38, A(255)), IM_COL32(6, 22, 38, A(255)), IM_COL32(14, 54, 46, A(255)), IM_COL32(14, 54, 46, A(255)));
    for (int i = 0; i < 40; i++) { // a few stars, fixed positions
        const float sx = std::fmod(i * 197.0f + 41.0f, 1000.0f) / 1000.0f * ds.x, sy = std::fmod(i * 331.0f + 17.0f, 1000.0f) / 1000.0f * ds.y * 0.6f;
        dl->AddCircleFilled(ImVec2(sx, sy), (1.0f + (i % 3)) * scale * 0.8f, IM_COL32(255, 255, 230, A(60 + (i * 37) % 140)));
    }

    // Double gold border with diamond corners.
    const float m = 26 * scale;
    dl->AddRect(ImVec2(m, m), ImVec2(ds.x - m, ds.y - m), gold, 0, 0, 3.0f * scale);
    dl->AddRect(ImVec2(m + 9 * scale, m + 9 * scale), ImVec2(ds.x - m - 9 * scale, ds.y - m - 9 * scale), goldDark, 0, 0, 1.5f * scale);
    const ImVec2 corners[4] = { ImVec2(m, m), ImVec2(ds.x - m, m), ImVec2(m, ds.y - m), ImVec2(ds.x - m, ds.y - m) };
    for (const ImVec2& c : corners) {
        const float r = 9 * scale;
        dl->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), gold);
    }

    auto lettered = [&](float y, float size, ImU32 col, const char* text) {
        ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
        const float x = (ds.x - sz.x) * 0.5f;
        const float o = std::max(2.0f, size * 0.05f);
        for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) {
            if (dx || dy) dl->AddText(font, size, ImVec2(x + dx * o, y + dy * o), shadow, text);
        }
        dl->AddText(font, size, ImVec2(x + o * 1.6f, y + o * 1.6f), shadow, text);
        dl->AddText(font, size, ImVec2(x, y), col, text);
        return sz.y;
    };

    float y;
    ImVec2 logoSize;
    if (LogoTexture(&logoSize) != nullptr) {   // the logo, floating gently
        const float bob = std::sin(secondsIn * 2.0f) * 4.0f * scale;
        const float width = std::min(ds.x * 0.5f, ds.y * 0.5f * logoSize.x / logoSize.y);
        const float top = ds.y * 0.06f + bob;
        y = top + DrawLogo(dl, ds.x * 0.5f, top, width, A(255), true) + 24 * scale;
    } else {
        // The Triforce, three gold triangles with a dark gap in the middle, floating gently.
        const float bob = std::sin(secondsIn * 2.0f) * 4.0f * scale;
        const ImVec2 c(ds.x * 0.5f, ds.y * 0.27f + bob);
        const float t = 62 * scale; // half the width of the whole mark
        auto tri = [&](ImVec2 top, ImVec2 left, ImVec2 right) {
            dl->AddTriangleFilled(ImVec2(top.x + 3 * scale, top.y + 3 * scale), ImVec2(left.x + 3 * scale, left.y + 3 * scale), ImVec2(right.x + 3 * scale, right.y + 3 * scale), shadow);
            dl->AddTriangleFilled(top, left, right, gold);
            dl->AddTriangle(top, left, right, goldDark, 2.0f * scale);
        };
        const float h = t * 1.732f;                    // height of the whole mark
        const float half = t * 0.5f, hh = h * 0.5f;
        const ImVec2 apex(c.x, c.y - hh), bl(c.x - t, c.y + hh), br(c.x + t, c.y + hh), midL(c.x - half, c.y), midR(c.x + half, c.y), midB(c.x, c.y + hh);
        tri(apex, midL, midR);
        tri(midL, bl, midB);
        tri(midR, midB, br);

        y = ds.y * 0.27f + hh + 28 * scale;
        y += lettered(y, 58 * scale, gold, "OOT ROYALE") + 10 * scale;

    }
    // A thin gold rule with a diamond in the middle under the title.
    dl->AddLine(ImVec2(ds.x * 0.34f, y), ImVec2(ds.x * 0.66f, y), goldDark, 2.0f * scale);
    dl->AddQuadFilled(ImVec2(ds.x * 0.5f, y - 6 * scale), ImVec2(ds.x * 0.5f + 6 * scale, y), ImVec2(ds.x * 0.5f, y + 6 * scale), ImVec2(ds.x * 0.5f - 6 * scale, y), gold);
    y += 22 * scale;
    y += lettered(y, 34 * scale, cream, "Made by Tevin Dahl") + 26 * scale;
    y += lettered(y, 21 * scale, cream, "Thank you to my wife Cynthia for supporting my wildest dreams and hobbies :)");
}


void DrawQuestLabel() {
    if (gRoyaleQuestLabel <= 0) return;
    gRoyaleQuestLabel--;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float scale = std::clamp(ds.y / 720.0f, 0.8f, 2.2f);
    // The title screen is a 4:3 picture fitted to the window's height; its subtitle sits right of centre, a little below the middle.
    const float cx = ds.x * 0.5f + (246.0f / 320.0f - 0.5f) * ds.y * (4.0f / 3.0f), cy = ds.y * (196.0f / 240.0f);
    const char* text = "BATTLE ROYALE";
    const float size = 30.0f * scale;
    const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    const ImVec2 pos(cx - sz.x * 0.5f, cy - sz.y * 0.5f);
    for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) if (dx || dy) dl->AddText(font, size, ImVec2(pos.x + dx * 2.0f, pos.y + dy * 2.0f), IM_COL32(40, 20, 0, 255), text);
    dl->AddText(font, size, pos, IM_COL32(255, 214, 90, 255), text);
}

// ---- banners, chest-opening and boss effects --------------------------------------------------------------------------------
// A big line of text across the top for the moments that matter (a major boss arriving, what a chest gave you), and an item that floats up out of
// a chest you open. (The bosses' own effects are in RoyaleBosses.h.)
struct Banner { std::string text; ImU32 colour; double until; double start; };
std::vector<Banner> gBanners;
void ShowBanner(const std::string& text, ImU32 colour, float seconds = 2.6f) {
    const double now = ImGui::GetTime();
    gBanners.push_back({ text, colour, now + seconds, now });
    if (gBanners.size() > 2) gBanners.erase(gBanners.begin());
}

// Small "+5 Rupees" lines that float up on the right when something drops out of a rock or bush.
// The kill feed, top right: who got whom.
struct FeedLine { std::string text; ImU32 colour; double at; };
std::vector<FeedLine> gFeed;
void AddFeed(const std::string& text, ImU32 colour) {
    gFeed.push_back({ text, colour, ImGui::GetTime() });
    if (gFeed.size() > 4) gFeed.erase(gFeed.begin());
}
void DrawFeed(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale) {
    const double now = ImGui::GetTime();
    float y = 14.0f * scale;
    for (const FeedLine& f : gFeed) {
        const double age = now - f.at;
        if (age > 5.0) continue;
        const float a = static_cast<float>(std::min(1.0, (5.0 - age) / 1.0));
        const float size = 17.0f * scale;
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, f.text.c_str());
        const ImVec2 pos(ds.x - sz.x - 18.0f * scale, y);
        dl->AddRectFilled(ImVec2(pos.x - 8 * scale, pos.y - 2 * scale), ImVec2(pos.x + sz.x + 8 * scale, pos.y + sz.y + 2 * scale), IM_COL32(6, 12, 18, static_cast<int>(150 * a)), 5.0f * scale);
        dl->AddText(font, size, pos, (f.colour & 0x00FFFFFF) | (static_cast<ImU32>(255 * a) << 24), f.text.c_str());
        y += sz.y + 7.0f * scale;
    }
    gFeed.erase(std::remove_if(gFeed.begin(), gFeed.end(), [&](const FeedLine& f) { return now - f.at > 5.0; }), gFeed.end());
}

struct Gain { std::string text; ImU32 colour; double at; };
std::vector<Gain> gGains;
void ShowGain(const std::string& text, ImU32 colour) {
    gGains.push_back({ text, colour, ImGui::GetTime() });
    if (gGains.size() > 5) gGains.erase(gGains.begin());
}
void DrawGains(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale) {
    const double now = ImGui::GetTime();
    float y = ds.y * 0.42f;
    for (const Gain& g : gGains) {
        const double age = now - g.at;
        if (age > 1.8) continue;
        const float a = static_cast<float>(std::min(1.0, (1.8 - age) / 0.5));
        const float size = 22.0f * scale;
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, g.text.c_str());
        const ImVec2 pos(ds.x - sz.x - 24.0f * scale, y - static_cast<float>(age) * 26.0f * scale);
        dl->AddText(font, size, ImVec2(pos.x + 1.5f, pos.y + 1.5f), IM_COL32(0, 0, 0, static_cast<int>(220 * a)), g.text.c_str());
        dl->AddText(font, size, pos, (g.colour & 0x00FFFFFF) | (static_cast<ImU32>(255 * a) << 24), g.text.c_str());
        y += 28.0f * scale;
    }
    gGains.erase(std::remove_if(gGains.begin(), gGains.end(), [&](const Gain& g) { return now - g.at > 1.8; }), gGains.end());
}

struct PickupFx { royale::ItemId item; royale::Rarity rarity; float x, y, z; double at; };
std::vector<PickupFx> gPickupFx;


void DrawBanners(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale) {
    const double now = ImGui::GetTime();
    float y = ds.y * 0.27f;
    for (const Banner& b : gBanners) {
        const double age = now - b.start, left = b.until - now;
        if (left <= 0) continue;
        const float a = static_cast<float>(std::min(1.0, std::min(age / 0.2, left / 0.5)));
        const float size = (30.0f + 6.0f * static_cast<float>(std::max(0.0, 1.0 - age * 4.0))) * scale;
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, b.text.c_str());
        const ImVec2 pos((ds.x - sz.x) * 0.5f, y);
        const ImU32 col = (b.colour & 0x00FFFFFF) | (static_cast<ImU32>(255 * a) << 24);
        dl->AddRectFilled(ImVec2(pos.x - 20 * scale, pos.y - 6 * scale), ImVec2(pos.x + sz.x + 20 * scale, pos.y + sz.y + 6 * scale), IM_COL32(6, 12, 18, static_cast<int>(170 * a)), 8.0f * scale);
        dl->AddText(font, size, ImVec2(pos.x + 2, pos.y + 2), IM_COL32(0, 0, 0, static_cast<int>(230 * a)), b.text.c_str());
        dl->AddText(font, size, pos, col, b.text.c_str());
        y += sz.y + 16.0f * scale;
    }
    gBanners.erase(std::remove_if(gBanners.begin(), gBanners.end(), [&](const Banner& b) { return b.until <= now; }), gBanners.end());
}

// The item floats up out of the chest on a halo of its rarity colour, spinning its glow, then fades.
void DrawPickupFx(ImDrawList* dl, ImVec2 ds, float scale) {
    const double now = ImGui::GetTime();
    for (const PickupFx& f : gPickupFx) {
        const double age = now - f.at;
        if (age > 1.7) continue;
        ImVec2 at;
        if (!WorldToScreen(f.x, f.y + 50.0f + static_cast<float>(std::min(age, 1.0)) * 90.0f, f.z, &at)) continue;
        const float a = static_cast<float>(std::min(1.0, (1.7 - age) / 0.5));
        const float pop = static_cast<float>(std::min(1.0, age / 0.25)) * (1.0f + 0.12f * std::sin(static_cast<float>(age) * 14.0f));
        const Rgb& c = kRarityRgb[static_cast<int>(f.rarity)];
        const float R = 44.0f * scale * pop;
        dl->AddCircleFilled(at, R * 1.5f, IM_COL32(c.r, c.g, c.b, static_cast<int>(60 * a)), 28);
        for (int i = 0; i < 10; i++) { // rays turning slowly
            const float ang = i * 0.62832f + static_cast<float>(age) * 1.6f;
            dl->AddLine(ImVec2(at.x + std::cos(ang) * R * 0.9f, at.y + std::sin(ang) * R * 0.9f), ImVec2(at.x + std::cos(ang) * R * 1.8f, at.y + std::sin(ang) * R * 1.8f),
                        IM_COL32(c.r, c.g, c.b, static_cast<int>(150 * a)), 3.0f * scale);
        }
        dl->AddCircleFilled(at, R, OotPanel(static_cast<int>(210 * a)), 28);
        dl->AddCircle(at, R, IM_COL32(c.r, c.g, c.b, static_cast<int>(255 * a)), 28, 3.0f * scale);
        DrawItemIcon(dl, f.item, at, R * 1.25f, IM_COL32(c.r, c.g, c.b, 255));
    }
    gPickupFx.erase(std::remove_if(gPickupFx.begin(), gPickupFx.end(), [&](const PickupFx& f) { return now - f.at > 1.7; }), gPickupFx.end());
}

// ---- hit effects -------------------------------------------------------------------------------------------------------------
// When you hurt someone: a white X hit marker at the crosshair (red when it is a kill) and the damage floating up from them.
// When you are hurt: the screen edges flash red, a red arrow on a ring around the middle points at who hit you, and the damage shows.
struct FloatingNumber { float x, y, z, amount; double at; bool mine; };
std::vector<FloatingNumber> gFloatingNumbers;
struct IncomingHit { uint16_t from; double at; };
std::vector<IncomingHit> gIncomingHits;
double gHurtAt = -100.0, gHitMarkerAt = -100.0;
bool gHitMarkerKill = false;
float gHurtAmount = 0.0f;

bool KnownPosition(uint16_t id, float* x, float* z) {
    for (const auto& st : gSession.Puppets()) if (st.id == id) { *x = st.x; *z = st.z; return true; }
    if (gSession.Client() && royale::IsBossId(id)) for (const auto& bn : gSession.Client()->Bosses()) if (royale::kBossIdBase + bn.index == id) { *x = bn.x; *z = bn.z; return true; }
    if (CartTargetAt(id, x, z)) return true;
    return false;
}

void DrawHitEffects(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale) {
    const double now = ImGui::GetTime();
    if (gPlayState == nullptr || !InField()) { gFloatingNumbers.clear(); gIncomingHits.clear(); return; }
    Player* pl = GET_PLAYER(gPlayState);
    // red edges
    const double hurtAge = now - gHurtAt;
    if (hurtAge < 0.7) {
        const int a = static_cast<int>((1.0 - hurtAge / 0.7) * (150 + std::min(90.0f, gHurtAmount * 80.0f)));
        const ImU32 solid = IM_COL32(220, 20, 20, a), clear = IM_COL32(220, 20, 20, 0);
        const float e = std::min(ds.x, ds.y) * 0.22f;
        dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(ds.x, e), solid, solid, clear, clear);
        dl->AddRectFilledMultiColor(ImVec2(0, ds.y - e), ImVec2(ds.x, ds.y), clear, clear, solid, solid);
        dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(e, ds.y), solid, clear, clear, solid);
        dl->AddRectFilledMultiColor(ImVec2(ds.x - e, 0), ImVec2(ds.x, ds.y), clear, solid, solid, clear);
    }
    // arrows towards whoever hit you
    const ImVec2 centre(ds.x * 0.5f, ds.y * 0.5f);
    const float yaw = static_cast<float>(Camera_GetInputDirYaw(GET_ACTIVE_CAM(gPlayState))) * (3.14159265f / 32768.0f);
    for (const IncomingHit& hit : gIncomingHits) {
        const double age = now - hit.at;
        if (age > 1.6) continue;
        float ax, az;
        if (!KnownPosition(hit.from, &ax, &az)) continue;
        const float rel = std::atan2(ax - pl->actor.world.pos.x, az - pl->actor.world.pos.z) - yaw;
        const ImVec2 dir(std::sin(rel), -std::cos(rel)), side(-dir.y, dir.x);
        const float R = 150.0f * scale, w = 20.0f * scale, len = 34.0f * scale;
        const ImVec2 base(centre.x + dir.x * R, centre.y + dir.y * R);
        const int a = static_cast<int>(255.0 * (1.0 - age / 1.6));
        dl->AddTriangleFilled(ImVec2(base.x + dir.x * len, base.y + dir.y * len), ImVec2(base.x + side.x * w, base.y + side.y * w), ImVec2(base.x - side.x * w, base.y - side.y * w), IM_COL32(235, 30, 30, a));
    }
    gIncomingHits.erase(std::remove_if(gIncomingHits.begin(), gIncomingHits.end(), [&](const IncomingHit& h) { return now - h.at > 1.6; }), gIncomingHits.end());
    // hit marker
    const double markAge = now - gHitMarkerAt;
    if (markAge < 0.3) {
        const float k = static_cast<float>(1.0 - markAge / 0.3);
        const float in = (6.0f + (1.0f - k) * 5.0f) * scale, out = in + 11.0f * scale;
        const ImU32 col = gHitMarkerKill ? IM_COL32(255, 60, 60, static_cast<int>(255 * k)) : IM_COL32(255, 255, 255, static_cast<int>(255 * k));
        for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2) {
            dl->AddLine(ImVec2(centre.x + sx * in, centre.y + sy * in), ImVec2(centre.x + sx * out, centre.y + sy * out), IM_COL32(0, 0, 0, static_cast<int>(200 * k)), 5.0f * scale);
            dl->AddLine(ImVec2(centre.x + sx * in, centre.y + sy * in), ImVec2(centre.x + sx * out, centre.y + sy * out), col, 2.6f * scale);
        }
    }
    // floating numbers
    for (const FloatingNumber& f : gFloatingNumbers) {
        const double age = now - f.at;
        if (age > 1.0) continue;
        ImVec2 at;
        if (f.mine) { // taken from you: rises beside the crosshair
            at = ImVec2(centre.x + 60.0f * scale, centre.y + 20.0f * scale - static_cast<float>(age) * 50.0f * scale);
        } else if (!WorldToScreen(f.x, f.y + 70.0f + static_cast<float>(age) * 60.0f, f.z, &at)) {
            continue;
        }
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.1f", f.amount);
        const int a = static_cast<int>(255.0 * (1.0 - age));
        const float size = (f.mine ? 26.0f : 24.0f + std::min(14.0f, f.amount * 8.0f)) * scale;
        dl->AddText(font, size, ImVec2(at.x + 1.5f, at.y + 1.5f), IM_COL32(0, 0, 0, a), buf);
        dl->AddText(font, size, at, f.mine ? IM_COL32(255, 80, 80, a) : IM_COL32(255, 230, 90, a), buf);
    }
    gFloatingNumbers.erase(std::remove_if(gFloatingNumbers.begin(), gFloatingNumbers.end(), [&](const FloatingNumber& f) { return now - f.at > 1.0; }), gFloatingNumbers.end());
}

void DrawOverlay() {
    DrawTitleLogo();
    DrawQuestLabel();
    if (!gSession.Joined()) return;
    royale::HudState h = gSession.Hud();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float scale = std::clamp(ds.y / 720.0f, 0.8f, 2.2f);
    const ImU32 white = IM_COL32(255, 255, 255, 255), gold = IM_COL32(255, 210, 70, 255), red = IM_COL32(255, 90, 90, 255),
                green = IM_COL32(110, 230, 130, 255), grey = IM_COL32(190, 190, 190, 255);
    auto text = [&](float x, float y, ImU32 col, float size, const std::string& t) {
        dl->AddText(font, size, ImVec2(x + 1.5f, y + 1.5f), IM_COL32(0, 0, 0, 230), t.c_str());
        dl->AddText(font, size, ImVec2(x, y), col, t.c_str());
    };
    auto centered = [&](float y, ImU32 col, float size, const std::string& t) {
        ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, t.c_str());
        text((ds.x - sz.x) * 0.5f, y, col, size, t);
    };

    bool live = h.state == royale::MatchState::Drop || h.state == royale::MatchState::InMatch;

    const bool splashing = h.state == royale::MatchState::Countdown && h.countdownLeft > royale::kCountdownSec - kSplashSeconds;
    if (splashing) {
        DrawSplash(dl, font, ds, scale, royale::kCountdownSec - h.countdownLeft);
    } else if (h.state == royale::MatchState::Countdown) {
        centered(ds.y * 0.16f, gold, 46 * scale, "MATCH STARTS IN " + std::to_string(static_cast<int>(std::ceil(h.countdownLeft))));
        if (!InField()) centered(ds.y * 0.16f + 56 * scale, white, 24 * scale, "Heading to " + std::string(CurrentMap().name) + "...");
    }
    if (h.state == royale::MatchState::Ending) {
        centered(ds.y * 0.16f, gold, 46 * scale, h.winnerId == h.selfId && h.winnerId != royale::net::kNoPlayer16 ? "VICTORY ROYALE!" : "MATCH OVER");
        if (!h.winnerName.empty() && h.winnerId != h.selfId) centered(ds.y * 0.16f + 56 * scale, white, 26 * scale, "Winner: " + h.winnerName);
        centered(ds.y * 0.16f + 92 * scale, grey, 20 * scale, "Open the menu and choose Leave to go back");
    }
    if (live && h.haveSelf && !h.selfAlive) {
        centered(ds.y * 0.12f, red, 34 * scale, "ELIMINATED");
        centered(ds.y * 0.12f + 42 * scale, white, 22 * scale, "Watching " + SpectateName());
        ImGuiIO& io = ImGui::GetIO();
        const bool tap = ImGui::IsMouseClicked(0) && !io.WantCaptureMouse;
        for (int side = 0; side < 2; side++) {
            const float bw = 54.0f * scale, bh = 40.0f * scale;
            const ImVec2 a(side == 0 ? ds.x * 0.5f - 190.0f * scale : ds.x * 0.5f + 136.0f * scale, ds.y * 0.12f + 36.0f * scale), b(a.x + bw, a.y + bh);
            dl->AddRectFilled(a, b, OotPanel(205), 6.0f * scale);
            dl->AddRect(a, b, IM_COL32(255, 236, 120, 255), 6.0f * scale, 0, 2.0f * scale);
            dl->AddText(font, 24.0f * scale, ImVec2(a.x + 19 * scale, a.y + 7 * scale), IM_COL32(255, 255, 255, 255), side == 0 ? "<" : ">");
            if (tap && io.MousePos.x >= a.x && io.MousePos.x <= b.x && io.MousePos.y >= a.y && io.MousePos.y <= b.y) CycleSpectate(side == 0 ? -1 : 1);
        }
    }
    if (live && h.state == royale::MatchState::Drop && gSkydiving) centered(ds.y * 0.2f, green, 22 * scale, "Hold Z to dive   Stick to steer");
    if (live && gSkydiving) DrawGliderAim(dl, font, ds, scale);

    if (h.state == royale::MatchState::Ending) { DrawResultsPanel(dl, font, ds, scale, h); DrawReplay(dl, font, ds, scale, h); }

    DrawPickupFx(dl, ds, scale);
    DrawFeed(dl, font, ds, scale);
    DrawGains(dl, font, ds, scale);
    DrawHitEffects(dl, font, ds, scale);
    DrawBanners(dl, font, ds, scale);

    if (!live || !h.haveSelf) return;

    // What is at your feet: chests say how rare they are (not what is inside); items on the ground say what they are.
    // One line: a green A button, then what pressing it does.
    auto prompt = [&](ImU32 col, const std::string& t) {
        const float size = 24 * scale, btn = 13 * scale, gap = 8 * scale, y = ds.y * 0.66f;
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, t.c_str());
        const float x = (ds.x - sz.x - btn * 2 - gap) * 0.5f;
        const ImVec2 c(x + btn, y + sz.y * 0.5f);
        dl->AddCircleFilled(ImVec2(c.x + 1.5f, c.y + 1.5f), btn, IM_COL32(0, 0, 0, 200), 20);
        dl->AddCircleFilled(c, btn, IM_COL32(70, 200, 90, 255), 20);
        const ImVec2 asz = font->CalcTextSizeA(btn * 1.4f, FLT_MAX, 0.0f, "A");
        dl->AddText(font, btn * 1.4f, ImVec2(c.x - asz.x * 0.5f, c.y - asz.y * 0.5f), white, "A");
        text(x + btn * 2 + gap, y, col, size, t);
    };
    if (InField() && h.selfAlive && gSession.Client()) {
        const size_t near = NearestLootIndex();
        const auto& loot = gSession.Client()->Loot();
        if (near != kNoLoot && near < loot.size()) {
            const royale::Rarity r = static_cast<royale::Rarity>(loot[near].rarity);
            if (loot[near].chest) prompt(loot[near].special ? IM_COL32(255, 130, 190, 255) : RarityU32(r), loot[near].special ? std::string("Heart Container Chest") : std::string(RarityName(r)) + " Chest");
            else prompt(RarityU32(r), ItemLabel(static_cast<royale::ItemId>(loot[near].item), r));
        }
        else if (const std::string cart = CartPromptText(); !cart.empty()) {
            prompt(IM_COL32(255, 210, 140, 255), cart);
        } else if (const int ally = NearbyFreeAlly(); ally >= 0) {
            const royale::AllyDef& def = royale::kAllyDefs[ally];
            const bool afford = h.rupees >= def.price;
            prompt(afford ? IM_COL32(255, 222, 110, 255) : IM_COL32(255, 130, 120, 255), "Hire " + std::string(def.name) + " (" + std::to_string(def.price) + " rupees)");
        } else if (MessageBoxUp()) {
            // reading: the text box has the screen
        } else if (MayaNear()) {
            prompt(IM_COL32(255, 170, 215, 255), "Talk to Maya");
        } else if (LiloNear()) {
            prompt(IM_COL32(235, 235, 230, 255), "Talk to Lilo");
        } else if (SignNear()) {
            prompt(IM_COL32(255, 232, 160, 255), "Read the sign");
        }
    }
    if (ImGui::GetTime() < gBannerUntil) {
        const float fade = static_cast<float>(std::min(1.0, gBannerUntil - ImGui::GetTime()));
        ImU32 col = RarityU32(gBannerRarity);
        col = (col & 0x00FFFFFF) | (static_cast<ImU32>(255 * fade) << 24);
        centered(ds.y * 0.58f, col, 30 * scale, gBannerText);
    }
    DrawWeather(dl, ds, scale, h);
    DrawStorm(dl, ds, scale, h);
    DrawBossBars(dl, font, scale);
    DrawPoiLabels(dl, font, ds, scale, h);
    DrawSign(dl, font, ds, scale);
    DrawMaya(dl, font, ds, scale);
    DrawLilo(dl, font, ds, scale);
    DrawFartReactions(dl, font, scale);
    DrawAllyLabels(dl, font, ds, scale, h);
    DrawCartHud(dl, font, ds, scale);
    DrawMinimap(dl, ds, scale, h);
    DrawHotbar(dl, font, ds, scale, h);
    DrawEmotes(dl, font, ds, scale, h);

    // The shield bar, under the hearts: the game draws a row of hearts at the top left, and the bar runs as wide as that row and is filled by shield potions.
    {
        const float unit = ds.y / 240.0f;                                  // the game's own HUD is laid out on a 240 high screen
        const float bx = 30.0f * unit, by = 46.0f * unit, bw = std::max(3.0f, h.maxHealth) * 16.0f * unit, bh = 7.0f * unit;
        const float fill = std::clamp(h.inv.shield / royale::kMaxShield, 0.0f, 1.0f);
        dl->AddRectFilled(ImVec2(bx - 2, by - 2), ImVec2(bx + bw + 2, by + bh + 2), IM_COL32(0, 0, 0, 170), 3.0f);
        dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), IM_COL32(20, 30, 60, 200), 2.0f);
        if (fill > 0.0f) {
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw * fill, by + bh), IM_COL32(70, 150, 255, 255), 2.0f);
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw * fill, by + bh * 0.45f), IM_COL32(170, 215, 255, 180), 2.0f);
        }
        char label[24];
        std::snprintf(label, sizeof(label), "%d", static_cast<int>(std::lround(fill * 100.0f)));
        text(bx + bw + 8.0f * unit, by - 4.0f * unit, IM_COL32(150, 200, 255, 255), 14.0f * unit, label);
    }
    // The magic meter, a green bar under the shield bar: abilities spend it, it refills on its own, Magic Jars top it up.
    {
        const float unit = ds.y / 240.0f;
        const float bx = 30.0f * unit, by = 57.0f * unit, bw = std::max(3.0f, h.maxHealth) * 16.0f * unit, bh = 6.0f * unit;
        const float fill = std::clamp(h.magic / royale::kMaxMagic, 0.0f, 1.0f);
        const float need = h.inv.hasAbility ? royale::AbilityMagic(static_cast<royale::ItemId>(h.inv.ability.item)) / royale::kMaxMagic : 0.0f;
        dl->AddRectFilled(ImVec2(bx - 2, by - 2), ImVec2(bx + bw + 2, by + bh + 2), IM_COL32(0, 0, 0, 170), 3.0f);
        dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), IM_COL32(15, 40, 20, 200), 2.0f);
        if (fill > 0.0f) {
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw * fill, by + bh), fill >= need ? IM_COL32(60, 200, 90, 255) : IM_COL32(150, 170, 70, 255), 2.0f);
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw * fill, by + bh * 0.45f), IM_COL32(170, 255, 190, 170), 2.0f);
        }
        if (need > 0.0f) dl->AddLine(ImVec2(bx + bw * need, by - 1), ImVec2(bx + bw * need, by + bh + 1), IM_COL32(255, 255, 255, 200), 1.5f);   // what your ability costs
    }
    DrawStaminaBar(dl, ds, h);

    // Top right: the match at a glance (where the game's C buttons used to be; the hotbar at the bottom does their job now). Right-aligned,
    // under the safe-zone compass: how many are left, the zone timer and anything that is affecting you.
    {
        const float rx = ds.x - 16 * scale, line = 24 * scale;
        float y = 150 * scale;
        auto textR = [&](ImU32 col, float size, const std::string& t) {
            const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, t.c_str());
            text(rx - sz.x, y, col, size, t);
            y += size + 4 * scale;
        };
        textR(gold, 24 * scale, "ALIVE " + std::to_string(h.alive));
        if (h.stormPhase >= royale::kStormPhaseCount) textR(red, 20 * scale, "Final zone");
        else textR(h.stormShrinking || h.stormSecondsLeft <= 5.0f ? red : h.stormSecondsLeft <= 15.0f ? gold : white, 20 * scale, (h.stormShrinking ? "Closing " : "Zone ") + ClockText(h.stormSecondsLeft));
        // Only what is happening to you right now, short. Gear is already shown as icons above the item bar, the weather is on the screen itself.
        if (h.selfAlive) {
            if (h.adultLeft > 0.0f) textR(gold, 18 * scale, "Adult " + ClockText(h.adultLeft));
            if (h.invulnLeft > 0) textR(gold, 18 * scale, "Invulnerable " + ClockText(h.invulnLeft));
            if (h.speedLeft > 0) textR(green, 18 * scale, "Speed " + ClockText(h.speedLeft));
            if (h.revealLeft > 0) textR(green, 18 * scale, "Reveal " + ClockText(h.revealLeft));
            if (h.shieldLeft > 0) textR(green, 18 * scale, "Guard " + ClockText(h.shieldLeft));
            if (h.burnLeft > 0) textR(red, 18 * scale, "Burning");
            if (h.stunLeft > 0) textR(red, 18 * scale, "Stunned");
            if (h.inv.hasMark) textR(green, 16 * scale, "Farore's Wind set");
        }
    }

    // Top right: which way is the safe zone, relative to the way Link is facing.
    if (InField() && h.selfAlive) {
        Player* p = GET_PLAYER(gPlayState);
        float dx = h.safeZone.center.x - p->actor.world.pos.x, dz = h.safeZone.center.z - p->actor.world.pos.z;
        bool outside = std::sqrt(dx * dx + dz * dz) > h.safeZone.radius;
        float rel = std::atan2(dx, dz) - p->actor.shape.rot.y * (3.14159265f / 32768.0f);
        ImVec2 c(ds.x - 74 * scale, 84 * scale);
        float r = 34 * scale;
        auto pt = [&](float angle, float len) { return ImVec2(c.x + std::sin(angle) * len, c.y - std::cos(angle) * len); };
        dl->AddCircleFilled(c, r + 10 * scale, IM_COL32(0, 0, 0, 120));
        dl->AddTriangleFilled(pt(rel, r), pt(rel + 2.5f, r * 0.75f), pt(rel - 2.5f, r * 0.75f), outside ? red : green);
        if (outside) {
            ImVec2 sz = font->CalcTextSizeA(16 * scale, FLT_MAX, 0.0f, "SAFE ZONE");
            text(c.x - sz.x * 0.5f, c.y + r + 14 * scale, red, 16 * scale, "SAFE ZONE");
        }
    }
}

class RoyaleHudWindow : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void InitElement() override {}
    void UpdateElement() override {}
    void DrawElement() override {}
    void Draw() override {
        if (IsVisible()) DrawOverlay();
    }
};

// The overlay is a window with no frame of its own: it registers once, the first time the game loop runs (the GUI exists by then).
void EnsureHudWindow() {
    static bool done = false;
    if (done) return;
    done = true;
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    gui->AddGuiWindow(std::make_shared<RoyaleHudWindow>(CVAR_WINDOW("RoyaleHud"), true, "Royale HUD", ImVec2(0, 0)));
}

// ---- local player <-> session ----------------------------------------------------------------------------------------

// ---- emotes -------------------------------------------------------------------------------------------------------------------
// Your own Link does the emote himself: the game keeps running him as usual (standing still), and just before he is drawn his pose is
// replaced with the emote's frame. (It used to be done by a stand-in copy of Link while the real one was hidden, which could leave him
// invisible afterwards with only his shadow showing.)

struct EmoteState {
    int id = -1;               // which emote is playing, -1 for none
    double startAt = 0, endAt = 0;
    Player* player = nullptr;  // the Link whose drawing was taken over
};
EmoteState gEmote;

bool CanEmote(const royale::HudState& hud) { return LiveAndAlive(hud) && InField() && !gSkydiving; }

void LocalLink_Draw(Actor* actor, PlayState* play);
bool LocalDressOn();   // below, with the held weapon

// Gives Link back his own drawing (only if he is still the same Link: a scene change makes a new one).
void ReleaseEmoteDraw() {
    if (gPlayState == nullptr) return;
    Player* player = GET_PLAYER(gPlayState);
    if (player != nullptr && player->actor.draw == LocalLink_Draw) player->actor.draw = Player_Draw;
}

void StopEmote() {
    if (gEmote.id < 0) return;
    ReleaseEmoteDraw();
    gEmote = EmoteState{};
}

void StartEmote(int index, const royale::HudState& hud) {
    if (!CanEmote(hud) || index < 0 || index >= royale::kEmoteCount) return;
    StopEmote();
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr || (player->actor.draw != Player_Draw && player->actor.draw != LocalLink_Draw)) return;
    gEmote.id = index;
    gEmote.player = player;
    gEmote.startAt = ImGui::GetTime();
    gEmote.endAt = gEmote.startAt + (index == royale::kChickenDanceEmote ? royale::kChickenDanceSeconds : 3.4);
    gLastEmote = index;
    player->actor.draw = LocalLink_Draw;
}

// Any movement, attack or a few seconds ends an emote.
void UpdateEmote(Player* player, const royale::HudState& hud) {
    if (gEmote.id < 0) { if (player->actor.draw == LocalLink_Draw && !LocalDressOn()) player->actor.draw = Player_Draw; return; }
    const Input& in = gPlayState->state.input[0];
    const bool moved = std::fabs(static_cast<float>(in.cur.stick_x)) > 25.0f || std::fabs(static_cast<float>(in.cur.stick_y)) > 25.0f;
    if (!CanEmote(hud) || player != gEmote.player || ImGui::GetTime() > gEmote.endAt || moved ||
        (in.press.button & (BTN_B | BTN_A | BTN_Z | BTN_R | BTN_CUP | BTN_CLEFT | BTN_CDOWN | BTN_DDOWN | BTN_DUP))) {
        StopEmote();
    }
}

// Copies one frame of one of Link's animations straight into a joint table, the same way the game's own loader reads it (patched Ship of
// Harkinian reads the frame at once rather than by DMA), so it can be done while drawing, after the game has posed Link for the frame.
void LoadLinkFrame(LinkAnimationHeader* animation, int frame, int limbCount, Vec3s* out) {
    if (animation == nullptr) return;
    if (ResourceMgr_OTRSigCheck(reinterpret_cast<char*>(animation)) != 0)
        animation = reinterpret_cast<LinkAnimationHeader*>(ResourceMgr_LoadAnimByName(reinterpret_cast<const char*>(animation)));
    if (animation == nullptr) return;
    const LinkAnimationHeader* header = static_cast<const LinkAnimationHeader*>(SEGMENTED_TO_VIRTUAL(animation));
    char path[128];
    snprintf(path, sizeof(path), "misc/link_animetion/gPlayerAnimData_%06X",
             static_cast<unsigned>(reinterpret_cast<uintptr_t>(header->segment) - 0x07000000));
    const char* data = ResourceMgr_LoadPlayerAnimByName(path);
    if (data == nullptr) return;
    const size_t stride = sizeof(Vec3s) * limbCount + 2;   // every limb, then the eyes and mouth
    std::memcpy(out, data + stride * frame, stride);
}

// Poses Link `t` seconds into a sequence of animations: one after another (Link's animations step once per game tick), the last one
// looping or holding its final pose. He stays where he stands: only the height of the animation's root is used.
void PoseSeq(Player* player, const AnimSeq& seq, float t) {
    float f = t * royale::kTickHz;
    LinkAnimationHeader* anim = nullptr;
    int frame = 0;
    for (int i = 0; i < seq.count; i++) {
        const float len = static_cast<float>(Animation_GetLastFrame(seq.step[i].anim)) + 1.0f;
        if (seq.step[i].loop) { anim = seq.step[i].anim; frame = static_cast<int>(std::fmod(f, len)); break; }
        if (f < len || i == seq.count - 1) { anim = seq.step[i].anim; frame = static_cast<int>(std::min(f, len - 1.0f)); break; }
        f -= len;
    }
    if (anim == nullptr) return;
    Vec3s* j = player->skelAnime.jointTable;
    const s16 rootX = j[0].x, rootZ = j[0].z;
    LoadLinkFrame(anim, std::max(0, frame), player->skelAnime.limbCount, j);
    j[0].x = rootX;
    j[0].z = rootZ;
}
// Poses Link for the emote at this moment.
void PoseEmote(Player* player) {
    const float t = static_cast<float>(ImGui::GetTime() - gEmote.startAt);
    PoseSeq(player, SeqFor(royale::EmoteAnim(gEmote.id), royale::ItemId::BasicSword, 0, royale::ItemId::DinsFire, player), t);
    Vec3s* j = player->skelAnime.jointTable;
    if (gEmote.id == royale::kChickenDanceEmote) {
        ApplyChickenDance(player, t);
        if (player->actor.scale.y > 0.0f) j[0].y = static_cast<s16>(j[0].y + ChickenDanceBob(t) / player->actor.scale.y);
    }
}

void DrawLocalDressed(Player* player, PlayState* play, bool mayPose);   // below, with the held weapon
// Your own Link while you are in a match (and while you emote): drawn wearing what you have and holding what you use. See DrawLocalDressed.
void LocalLink_Draw(Actor* actor, PlayState* play) {
    Player* player = reinterpret_cast<Player*>(actor);
    if (player != GET_PLAYER(play)) { Player_Draw(actor, play); return; }
    const bool emote = gEmote.id >= 0 && gEmote.player == player;
    if (emote) PoseEmote(player);
    DrawLocalDressed(player, play, !emote);
}

// The wheel's controller side. Runs before the game reads the controller each frame, so while the wheel is up the stick and buttons
// choose an emote instead of moving Link.
void OnEmoteWheelInput() {
    if (gPlayState == nullptr || !gSession.Joined() || !InGame()) { if (gWheel.open) CloseEmoteWheel(); return; }
    Input& in = gPlayState->state.input[0];
    const royale::HudState hud = gSession.Hud();
    if (CartInput(in, hud)) { if (gWheel.open) CloseEmoteWheel(); return; }   // riding: the controller drives the cart
    // Skydiving: Z dives faster. It is taken off the controller before the game reads it, so Link doesn't Z-target (which parks the camera level
    // and hides the ground you are heading for); the camera stays free to look down at where you are going to land.
    gDiveHeld = gSkydiving && (in.cur.button & BTN_Z);
    if (gSkydiving) in.cur.button &= ~BTN_Z, in.press.button &= ~BTN_Z, in.rel.button &= ~BTN_Z;
    if (!gWheel.open) {
        if (!(in.press.button & BTN_CRIGHT) || !CanEmote(hud)) return;
        OpenEmoteWheel(false);
    }
    if (!CanEmote(hud) || (in.press.button & BTN_START)) { CloseEmoteWheel(); return; }
    if (in.press.button & BTN_B) {
        CloseEmoteWheel();
        Audio_PlaySoundGeneral(NA_SE_SY_FSEL_CLOSE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    } else {
        const float sx = in.cur.stick_x, sy = in.cur.stick_y;
        if (sx * sx + sy * sy > 35.0f * 35.0f) {   // the last emote pointed at stays chosen when the stick springs back
            const int slice = WheelSliceAt(sx, sy);
            if (slice != gWheel.hover)
                Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            gWheel.hover = slice;
        }
        int pick = -1;
        if (in.press.button & BTN_A) pick = gWheel.hover;
        if (!gWheel.byTouch && !(in.cur.button & BTN_CRIGHT)) {   // let go of C-Right
            if (gWheel.hover >= 0) pick = gWheel.hover;
            else if (ImGui::GetTime() - gWheel.openedAt < 0.3) pick = gLastEmote;   // a quick tap: the last one again
            else CloseEmoteWheel();
        }
        if (pick >= 0) { CloseEmoteWheel(); StartEmote(pick, hud); }
    }
    // This frame's buttons and stick were the wheel's.
    in.cur.button = in.press.button = in.rel.button = 0;
    in.cur.stick_x = in.cur.stick_y = in.rel.stick_x = in.rel.stick_y = in.press.stick_x = in.press.stick_y = 0;
}

// ---- the mod's own sounds, mixed into the game's audio ------------------------------------------------------------------------------
// Everything the mod plays (the music folder's songs, the chicken dance tune, the storm sounds, Lilo's mews and her accident) is mixed into each buffer the game's own
// audio thread makes (patch 0016 calls MixVoices for every buffer, 44.1 kHz stereo), each on a voice of its own, so they play together and none
// silences another or the game's sound. They used to be queued on extra SDL audio streams, which never open on Windows (the game uses WASAPI
// there and never starts SDL's audio) and may not on a phone: those sounds stayed silent.
enum Voice { kVoiceSong, kVoiceChicken, kVoiceOneShot, kVoiceCat, kVoiceCount };
struct MixVoiceState {
    std::shared_ptr<const std::vector<int16_t>> pcm;
    bool stereo = false, loop = false;
    int rate = 44100;
    double pos = 0;     // in frames of the voice's own rate
    float volume = 0.0f;
};
std::mutex gMixMutex;
MixVoiceState gVoices[kVoiceCount];
}   // namespace
extern "C" void (*gRoyaleAudioMix)(int16_t* buf, uint32_t frames);
namespace {
void MixVoices(int16_t* buf, uint32_t frames) {
    std::lock_guard<std::mutex> lock(gMixMutex);
    for (MixVoiceState& v : gVoices) {
        if (!v.pcm || v.volume <= 0.0f) continue;
        const std::vector<int16_t>& p = *v.pcm;
        const size_t ch = v.stereo ? 2 : 1, total = p.size() / ch;
        if (total == 0) continue;
        const double step = static_cast<double>(v.rate) / 44100.0;
        for (uint32_t i = 0; i < frames; i++) {
            size_t f = static_cast<size_t>(v.pos);
            if (f >= total) {
                if (!v.loop) { v.pcm.reset(); break; }
                v.pos = std::fmod(v.pos, static_cast<double>(total));
                f = static_cast<size_t>(v.pos);
            }
            const int l = static_cast<int>(p[f * ch] * v.volume), r = v.stereo ? static_cast<int>(p[f * ch + 1] * v.volume) : l;
            buf[2 * i] = static_cast<int16_t>(std::clamp(buf[2 * i] + l, -32768, 32767));
            buf[2 * i + 1] = static_cast<int16_t>(std::clamp(buf[2 * i + 1] + r, -32768, 32767));
            v.pos += step;
        }
    }
}
void StartVoice(Voice id, std::shared_ptr<const std::vector<int16_t>> pcm, bool stereo, int rate, bool loop, float volume) {
    gRoyaleAudioMix = MixVoices;
    std::lock_guard<std::mutex> lock(gMixMutex);
    MixVoiceState& v = gVoices[id];
    v.pcm = std::move(pcm); v.stereo = stereo; v.rate = rate; v.loop = loop; v.volume = volume; v.pos = 0;
}
void StopVoice(Voice id) {
    std::lock_guard<std::mutex> lock(gMixMutex);
    gVoices[id].pcm.reset();
}
void SetVoiceVolume(Voice id, float volume) {
    std::lock_guard<std::mutex> lock(gMixMutex);
    gVoices[id].volume = volume;
}
bool VoiceDone(Voice id) {
    std::lock_guard<std::mutex> lock(gMixMutex);
    return !gVoices[id].pcm;
}
// The game's own master volume (its default is 40) times the music volume.
float GameVolume(bool music) {
    const float master = static_cast<float>(CVarGetInteger(CVAR_SETTING("Volume.Master"), 40)) / 100.0f;
    const float sub = music ? static_cast<float>(CVarGetInteger(CVAR_SETTING("Volume.MainMusic"), 100)) / 100.0f : 1.0f;
    return std::clamp(master * sub, 0.0f, 1.0f);
}

// The chicken dance tune (shared/tune.h), played on a small audio device of its own beside the game's. You hear it when you do the dance, or
// when somebody doing it is near: louder the closer they are.
void StopChickenMusic() { StopVoice(kVoiceChicken); }

void UpdateChickenMusic() {
    float volume = 0.0f;
    if (gEmote.id == royale::kChickenDanceEmote) volume = 1.0f;
    else if (gPlayState != nullptr && InField() && GET_PLAYER(gPlayState) != nullptr) {
        Player* me = GET_PLAYER(gPlayState);
        for (const auto& [id, st] : gState) {
            if (!st.alive || st.anim != static_cast<uint8_t>(royale::Anim::Emote5)) continue;
            const float d = std::hypot(st.x - me->actor.world.pos.x, st.z - me->actor.world.pos.z);
            const float v = std::clamp(1.0f - d / 1800.0f, 0.0f, 1.0f);
            volume = std::max(volume, v * v * 0.8f);
        }
    }
    volume *= GameVolume(false) * 0.7f;
    if (volume < 0.01f) { StopChickenMusic(); return; }
    if (VoiceDone(kVoiceChicken)) {
        static const auto tune = std::make_shared<const std::vector<int16_t>>(royale::BuildChickenTune());
        StartVoice(kVoiceChicken, tune, false, royale::kTuneRate, true, volume);
    } else SetVoiceVolume(kVoiceChicken, volume);
}

// ---- carts: the Lon Lon Buggy -------------------------------------------------------------------------------------------------------
// A two-seat wooden buggy made in Blender in the N64 style (tools/cart/, assets/cart/cart.blend; shared/cart_model.h has the mesh, shared/vehicle.h the
// physics). The server parks them round the map and knows who sits where; whoever drives runs the physics in their own game against the real
// ground (so it rolls over every bump the game has) and reports the cart each tick. Carts driven by bots, or rolling with nobody at the reins, are
// moved by the server and drawn here on the real ground under their wheels.
//
// Controls. On foot next to a cart: A gets in (the driver's seat from its left, the back seat from its right). Driving: A goes, B brakes and then
// backs up, the stick steers, Z is the handbrake (slide round corners), R gets out. In the back seat: B and the C items still work (shoot from the
// saddle), A takes the reins if nobody is driving, R gets out.
namespace C = royale::cart;
constexpr float kSaddleDrop = 27.0f;   // Link's root sits this far under the top of the saddle (the game puts him 27 under Epona's rider point)
constexpr float kCartSub = 8.0f;       // the model goes to the graphics chip in 1/8 units, so its small parts keep their shape
constexpr float kCartHearing = 1800.0f;
constexpr float kSaddleClimbSeconds = 0.45f;   // the cart doesn't go until you are sat down

// The model, built once: the vertices with a fixed light baked into their colours (lit from above and a little in front, as the N64 games bake
// theirs), and one display list per rigid part, each loading the 64x32 texture.
struct CartGpu {
    std::vector<Vtx> vtx;
    std::vector<Gfx> dl[C::kPartCount];
    bool built = false;
};
CartGpu gCartGpu;

void BuildCartGpu() {
    if (gCartGpu.built) return;
    gCartGpu.vtx.resize(C::kVertCount);
    const float lx = 0.30f, ly = 0.86f, lz = 0.41f;
    for (int i = 0; i < C::kVertCount; i++) {
        const C::Vert& v = C::kVerts[i];
        const float nx = v.nx / 127.0f, ny = v.ny / 127.0f, nz = v.nz / 127.0f;
        const float lit = std::clamp(0.50f + 0.62f * std::max(0.0f, nx * lx + ny * ly + nz * lz) + 0.08f * ny, 0.0f, 1.0f);
        Vtx& o = gCartGpu.vtx[i];
        o.v.ob[0] = static_cast<s16>(std::lround(v.x * kCartSub));
        o.v.ob[1] = static_cast<s16>(std::lround(v.y * kCartSub));
        o.v.ob[2] = static_cast<s16>(std::lround(v.z * kCartSub));
        o.v.flag = 0;
        o.v.tc[0] = v.s;
        o.v.tc[1] = v.t;
        o.v.cn[0] = static_cast<u8>(255.0f * lit);
        o.v.cn[1] = static_cast<u8>(248.0f * lit);
        o.v.cn[2] = static_cast<u8>(236.0f * lit);
        o.v.cn[3] = 255;
    }
    for (int part = 0; part < C::kPartCount; part++) {
        std::vector<Gfx>& dl = gCartGpu.dl[part];
        size_t need = 16;
        for (int b = 0; b < C::kBatchCount; b++) if (C::kBatches[b].part == part) need += 2 + (C::kBatches[b].triCount + 1) / 2;
        dl.assign(need, Gfx{});
        Gfx* g = dl.data();
        gDPLoadTextureBlock(g++, C::kTex, G_IM_FMT_RGBA, G_IM_SIZ_16b, C::kTexW, C::kTexH, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP,
                            G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        for (int b = 0; b < C::kBatchCount; b++) {
            const C::Batch& bt = C::kBatches[b];
            if (bt.part != part) continue;
            gSPVertex(g++, reinterpret_cast<uintptr_t>(&gCartGpu.vtx[bt.firstVert]), bt.vertCount, 0);
            const int end = bt.firstTri + bt.triCount;
            int t = bt.firstTri;
            for (; t + 1 < end; t += 2)
                gSP2Triangles(g++, C::kTris[t][0], C::kTris[t][1], C::kTris[t][2], 0, C::kTris[t + 1][0], C::kTris[t + 1][1], C::kTris[t + 1][2], 0);
            if (t < end) gSP1Triangle(g++, C::kTris[t][0], C::kTris[t][1], C::kTris[t][2], 0);
        }
        gSPEndDisplayList(g++);
        dl.resize(static_cast<size_t>(g - dl.data()));   // only shrinks: nothing moves
    }
    gCartGpu.built = true;
}

// One cart as it is shown: smoothed from the snapshots (or, for the one you drive, your own physics), sat on the real ground, wheels turning.
struct CartView {
    Actor* actor = nullptr;
    ActorFunc origDestroy = nullptr;
    int index = -1;
    royale::net::VehicleNet net{};
    royale::CartBody body;           // as drawn
    float air = 0;
    bool drift = false;
    float spin[4] = {};              // each wheel's roll, radians
    float prevX = 0, prevZ = 0;
    bool placed = false;
    float hp = 1.0f;
    bool wrecked = false;
    double wreckedAt = 0, hurtAt = -10.0;
    uint16_t driver = royale::net::kNoPlayer16, passenger = royale::net::kNoPlayer16;
    float wasAir = 0;
};
std::map<int, CartView> gCarts;
std::unordered_map<const Actor*, int> gCartOf;

CartView* CartViewOf(int index) {
    auto it = gCarts.find(index);
    return it == gCarts.end() || it->second.actor == nullptr ? nullptr : &it->second;
}

// ---- the ground a cart drives on, in this game: the scene's floors (and our blocks and walls), its walls, water, rocks and the other carts.
float gCartRefY = 0;               // the cart's own height: the ground is looked for from a little above it (so a bridge overhead isn't the floor)
royale::Vec2 gCartFrom = {};       // the cart's middle: walls are looked for between it and each corner
int gCartSelf = -1;                // the cart being moved (it doesn't run into itself)
float gCartMapRadius = 0;
royale::Vec2 gCartMapCentre = {};

bool CartGroundAt(float x, float z, float* y) {
    if (!InField()) return false;
    CollisionPoly poly;
    s32 bgId = BGCHECK_SCENE;
    Vec3f pos = { x, gCartRefY + 160.0f, z };
    const float h = BgCheck_AnyRaycastFloor2(&gPlayState->colCtx, &poly, &bgId, &pos);
    if (h <= BGCHECK_Y_MIN + 1.0f) return false;
    *y = h;
    return true;
}
bool CartSolidAt(float x, float z) {
    if (!InField()) return true;
    if (gCartMapRadius > 0.0f && royale::Distance({ x, z }, gCartMapCentre) > gCartMapRadius + 400.0f) return true;   // the far edge of the world
    float gy = 0;
    if (CartGroundAt(x, z, &gy) && UnderWater(x, z, gy)) return true;
    {   // a wall between the middle of the cart and this corner, at bumper height
        Vec3f from = { gCartFrom.x, gCartRefY + 32.0f, gCartFrom.z }, to = { x, gCartRefY + 32.0f, z }, hit;
        CollisionPoly* poly = nullptr;
        s32 bgId = 0;
        if (BgCheck_EntityLineTest1(&gPlayState->colCtx, &from, &to, &hit, &poly, true, false, false, true, &bgId)) return true;
    }
    if (const royale::GameClient* c = gSession.Client()) {   // rocks, boulders and pillars (the blocks are solid ground already)
        const auto& props = c->Props();
        for (size_t i = 0; i < props.size(); i++) {
            const royale::Prop& p = props[i];
            if (p.kind != royale::PropKind::Rock && p.kind != royale::PropKind::Boulder && p.kind != royale::PropKind::Pillar) continue;
            if (gBrokenProps.count(i)) continue;
            const float r = royale::PropRadius(p.kind) * (p.kind == royale::PropKind::Boulder ? royale::BoulderScale(p.rot) : 1.0f);
            const float dx = x - p.pos.x, dz = z - p.pos.z;
            if (dx * dx + dz * dz < r * r) return true;
        }
    }
    for (const auto& [i, v] : gCarts) if (i != gCartSelf && v.actor != nullptr && royale::InsideCart(v.body, x, z, -4.0f)) return true;
    return false;
}
const royale::CartWorld& ClientCartWorld() {
    static const royale::CartWorld w{ [](float x, float z, float* y) { return CartGroundAt(x, z, y); }, [](float x, float z) { return CartSolidAt(x, z); } };
    return w;
}

// ---- your own ride ----------------------------------------------------------------------------------------------------------------
struct LocalRide {
    // the controller, read before the game sees it (and taken away from Link while you ride)
    float throttle = 0, steer = 0;
    bool brake = false, handbrake = false;
    bool wantExit = false, wantSwitch = false;
    // the seat
    bool seated = false;
    int index = -1;
    royale::Seat seat = royale::Seat::None;
    double mountAt = -10.0, dismountAt = -10.0;
    royale::Seat dismountSeat = royale::Seat::Driver;
    royale::CartBody lastBody;        // where the cart was when you got off (the climb down is shown from its saddle)
    royale::Vec2 exitTo = {};
    bool settleOnGround = false;
    // driving
    bool driving = false;
    royale::CartBody body;
    float air = 0;
    double enterAskedAt = -10.0;
    bool exitWhenStopped = false;
    float shake = 0;
    Player* player = nullptr;
};
LocalRide gRide;

void LocalRide_Draw(Actor* actor, PlayState* play);

// The climb into the saddle, the ride and the climb down: Link's own horse-riding animations (the game's, for Epona).
LinkAnimationHeader* MountAnim(royale::Seat s) { return s == royale::Seat::Passenger ? RA(uma_right_up) : RA(uma_left_up); }
LinkAnimationHeader* DismountAnim(royale::Seat s) { return s == royale::Seat::Passenger ? RA(uma_right_down) : RA(uma_left_down); }
float AnimSeconds(LinkAnimationHeader* a) { return (static_cast<float>(Animation_GetLastFrame(a)) + 1.0f) / royale::kTickHz; }

// Which cart (and seat) a player is on, and the saddle's place and the cart's tilt for drawing them there.
struct RiderPose {
    float x = 0, y = 0, z = 0;        // the top of the saddle
    s16 yaw = 0, pitch = 0, roll = 0;
    royale::Seat seat = royale::Seat::None;
    float steer = 0, speed = 0;
    int index = -1;
};
bool CartRiderPose(uint16_t id, RiderPose* out) {
    for (const auto& [i, v] : gCarts) {
        if (v.actor == nullptr || v.wrecked) continue;
        royale::Seat s = royale::Seat::None;
        if (v.driver == id) s = royale::Seat::Driver;
        else if (v.passenger == id) s = royale::Seat::Passenger;
        else continue;
        royale::SeatSpot(v.body, s, &out->x, &out->y, &out->z);
        out->yaw = royale::YawToBinang(v.body.yaw);
        out->pitch = royale::YawToBinang(-v.body.pitch);
        // leaning into the turn (and with the cart's own sway over bumps)
        const float lean = std::clamp(-v.body.speed * v.body.steer * 0.0011f, -0.22f, 0.22f);
        out->roll = royale::YawToBinang(v.body.roll + lean);
        out->seat = s;
        out->steer = v.body.steer;
        out->speed = v.body.speed;
        out->index = i;
        return true;
    }
    return false;
}

// ---- drawing ----------------------------------------------------------------------------------------------------------------------
void DrawCart(PlayState* play, const CartView& v) {
    BuildCartGpu();
    const royale::CartBody& b = v.body;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);   // the light is baked into the vertex colours; both sides of thin planks show
    // texture times the vertex colour, times a tint: charred black once wrecked, darker as it takes damage, a red flash when hit
    gDPSetCombineLERP(POLY_OPA_DISP++, TEXEL0, 0, SHADE, 0, 0, 0, 0, 1, COMBINED, 0, PRIMITIVE, 0, 0, 0, 0, 1);
    u8 r = 255, g = 255, bl = 255;
    if (v.wrecked) { r = 58; g = 48; bl = 42; }
    else {
        const float worn = 0.62f + 0.38f * std::clamp(v.hp, 0.0f, 1.0f);
        r = static_cast<u8>(255 * worn); g = static_cast<u8>(255 * worn); bl = static_cast<u8>(255 * worn);
        if (ImGui::GetTime() - v.hurtAt < 0.15) { r = 255; g = 120; bl = 110; }
    }
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, r, g, bl, 255);
    // the cart: yaw, then pitch (nose up is a turn about its left axis), then roll, as royale::CartToWorld does
    Matrix_Translate(b.x, b.y, b.z, MTXMODE_NEW);
    Matrix_RotateY(b.yaw, MTXMODE_APPLY);
    Matrix_RotateX(-b.pitch, MTXMODE_APPLY);
    Matrix_RotateZ(b.roll, MTXMODE_APPLY);
    for (int part = 0; part < C::kPartCount; part++) {
        Matrix_Push();
        const C::P3& pv = C::kPivot[part];
        Matrix_Translate(pv.x, pv.y, pv.z, MTXMODE_APPLY);
        if (part == C::kWheelFL || part == C::kWheelFR) Matrix_RotateY(b.steer, MTXMODE_APPLY);   // the front wheels steer
        if (part == C::kHandlebar) Matrix_RotateY(b.steer * 0.8f, MTXMODE_APPLY);
        if (part >= C::kWheelFL && part <= C::kWheelBR) Matrix_RotateX(v.spin[part - C::kWheelFL], MTXMODE_APPLY);   // and all four roll
        Matrix_Scale(1.0f / kCartSub, 1.0f / kCartSub, 1.0f / kCartSub, MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, gCartGpu.dl[part].data());
        Matrix_Pop();
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// ---- dust, smoke, sparks and fire, and how it sounds -------------------------------------------------------------------------------
bool CartAudible(const Actor* a) {
    if (gPlayState == nullptr) return false;
    const Player* me = GET_PLAYER(gPlayState);
    const float dx = a->world.pos.x - me->actor.world.pos.x, dz = a->world.pos.z - me->actor.world.pos.z;
    return dx * dx + dz * dz < kCartHearing * kCartHearing;
}
void CartSfx(Actor* a, u16 sfx) {
    if (CartAudible(a)) Audio_PlaySoundGeneral(sfx, &a->projectedPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}
Vec3f CartPoint(const royale::CartBody& b, const C::P3& p) {
    Vec3f out;
    royale::CartToWorld(b, p, &out.x, &out.y, &out.z);
    return out;
}
// A crash: splinters of light off the bumper and a wooden thump (a hard one rings the iron tyres).
void CartCrashFx(PlayState* play, CartView& v, float impact) {
    Vec3f at = CartPoint(v.body, { 0.0f, 30.0f, v.body.speed >= 0.0f ? C::kMaxZ : C::kMinZ });
    Vec3f vel = { 0, 2.0f, 0 }, accel = { 0, -0.4f, 0 };
    for (int i = 0; i < 3 + static_cast<int>(impact / 120.0f); i++) {
        Vec3f p = { at.x + Rand_CenteredFloat(30.0f), at.y + Rand_CenteredFloat(14.0f), at.z + Rand_CenteredFloat(30.0f) };
        Vec3f sv = { Rand_CenteredFloat(6.0f), 3.0f + Rand_ZeroOne() * 4.0f, Rand_CenteredFloat(6.0f) };
        EffectSsGSpk_SpawnRandColor(play, v.actor, &p, &sv, &accel, 60, 0);
    }
    EffectSsHitMark_SpawnFixedScale(play, 0, &at);
    (void)vel;
    CartSfx(v.actor, NA_SE_EV_WOOD_BOUND);
    if (impact > 300.0f) CartSfx(v.actor, NA_SE_EV_BLOCK_SHAKE);
}
void CartBlastFx(PlayState* play, CartView& v) {
    Vec3f at = { v.body.x, v.body.y + 40.0f, v.body.z }, zero = { 0, 0, 0 };
    EffectSsBomb2_SpawnLayered(play, &at, &zero, &zero, 120, 19);
    Vec3f low = { v.body.x, v.body.y + 4.0f, v.body.z };
    EffectSsBlast_SpawnWhiteShockwave(play, &low, &zero, &zero);
    for (int i = 0; i < 10; i++) {
        Vec3f p = { at.x + Rand_CenteredFloat(60.0f), at.y + Rand_CenteredFloat(30.0f), at.z + Rand_CenteredFloat(60.0f) };
        Vec3f sv = { Rand_CenteredFloat(10.0f), 6.0f + Rand_ZeroOne() * 6.0f, Rand_CenteredFloat(10.0f) }, acc = { 0, -0.6f, 0 };
        EffectSsGSpk_SpawnRandColor(play, v.actor, &p, &sv, &acc, 90, 0);
    }
    CartSfx(v.actor, NA_SE_IT_BOMB_EXPLOSION);
    CartSfx(v.actor, NA_SE_EV_WOODBOX_BREAK);
}
void CartEffects(PlayState* play, CartView& v) {
    const u32 frame = play->gameplayFrames;
    const royale::CartBody& b = v.body;
    const float speed = std::fabs(b.speed);
    const bool onGround = v.air < 3.0f;
    if (v.wrecked) {   // burning: flames on the deck, black smoke, the crackle of the fire
        if (frame % 2 == 0) {
            Vec3f p = CartPoint(b, { Rand_CenteredFloat(70.0f), 45.0f + Rand_ZeroOne() * 20.0f, Rand_CenteredFloat(120.0f) });
            Vec3f vel = { 0, 0.6f, 0 }, acc = { 0, 0.15f, 0 };
            EffectSsKFire_Spawn(play, &p, &vel, &acc, 18 + static_cast<s16>(Rand_ZeroOne() * 14.0f), 0);
        }
        if (frame % 3 == 0) {
            Vec3f p = CartPoint(b, { Rand_CenteredFloat(50.0f), 70.0f, Rand_CenteredFloat(90.0f) });
            Vec3f vel = { 0, 1.6f, 0 }, acc = { 0, 0.08f, 0 };
            Color_RGBA8 prim = { 40, 36, 34, 255 }, env = { 10, 10, 10, 255 };
            func_8002836C(play, &p, &vel, &acc, &prim, &env, 90, 24, 26);
        }
        if (CartAudible(v.actor)) func_8002F974(v.actor, NA_SE_EV_BURN_OUT - SFX_FLAG);
        return;
    }
    // The chimney: a puff of smoke every few frames (more and darker as the cart is smashed up), streaming back as it goes.
    const int every = v.hp < 0.35f ? 1 : (speed > 200.0f ? 3 : 5);
    if (frame % every == 0) {
        Vec3f p = CartPoint(b, C::kExhaust);
        const float fx = std::sin(b.yaw), fz = std::cos(b.yaw);
        Vec3f vel = { -fx * b.speed * 0.02f, 1.4f, -fz * b.speed * 0.02f }, acc = { 0, 0.05f, 0 };
        const u8 grey = v.hp < 0.35f ? 50 : 205;
        Color_RGBA8 prim = { grey, grey, static_cast<u8>(grey - 5), 255 }, env = { static_cast<u8>(grey * 0.6f), static_cast<u8>(grey * 0.6f), static_cast<u8>(grey * 0.6f), 255 };
        func_8002836C(play, &p, &vel, &acc, &prim, &env, v.hp < 0.35f ? 60 : 34, 14, 22);
    }
    // A damaged firebox spits sparks.
    if (v.hp < 0.35f && frame % 7 == 0) {
        Vec3f p = CartPoint(b, { 0.0f, 55.0f, C::kMinZ + 10.0f });
        Vec3f sv = { Rand_CenteredFloat(3.0f), 3.0f, Rand_CenteredFloat(3.0f) }, acc = { 0, -0.3f, 0 };
        EffectSsGSpk_SpawnRandColor(play, v.actor, &p, &sv, &acc, 40, 0);
    }
    // Dust off the wheels: kicked up behind the back wheels at speed, from all four in a slide, and a burst when it lands from a jump.
    if (onGround && (speed > 90.0f || v.drift) && frame % (v.drift ? 1 : 2) == 0) {
        for (int w = v.drift ? 0 : 2; w < 4; w++) {
            const C::P3& pv = C::kPivot[C::kWheelFL + w];
            Vec3f at = CartPoint(b, { pv.x, 0.0f, pv.z });
            const s16 scale = static_cast<s16>(std::min(130.0f, 50.0f + speed * 0.15f + (v.drift ? 30.0f : 0.0f)));
            v.actor->floorHeight = at.y;   // (the ring is put on the actor's floor)
            Actor_SpawnFloorDustRing(play, v.actor, &at, 6.0f, 0, 3.0f, scale, 14, true);
        }
    }
    if (v.wasAir > 25.0f && onGround) {
        for (int w = 0; w < 4; w++) {
            const C::P3& pv = C::kPivot[C::kWheelFL + w];
            Vec3f at = CartPoint(b, { pv.x, 0.0f, pv.z });
            v.actor->floorHeight = at.y;
            Actor_SpawnFloorDustRing(play, v.actor, &at, 14.0f, 2, 5.0f, 110, 18, true);
        }
        CartSfx(v.actor, NA_SE_EV_WOOD_BOUND);
    }
    v.wasAir = v.air;
    // The iron tyres on the ground, higher the faster it goes; a scrape in a slide.
    if (onGround && speed > 25.0f && CartAudible(v.actor)) func_800F436C(&v.actor->projectedPos, NA_SE_EV_STONE_ROLLING - SFX_FLAG, 0.55f + std::min(1.0f, speed / royale::kCartMaxSpeed) * 0.75f);
    if (onGround && v.drift && speed > 120.0f && frame % 4 == 0) CartSfx(v.actor, NA_SE_PL_SLIP);
}

// ---- the actors ---------------------------------------------------------------------------------------------------------------------
void Cart_Update(Actor* actor, PlayState* play) {
    auto of = gCartOf.find(actor);
    if (of == gCartOf.end()) { Actor_Kill(actor); return; }
    CartView& v = gCarts[of->second];
    const bool mine = gRide.driving && gRide.index == v.index;
    const float prevHp = v.hp;
    const bool wasWrecked = v.wrecked;
    royale::net::VehicleNet n;
    const bool sampled = gSession.Client() && gSession.Client()->SampleVehicle(v.index, n);
    if (sampled) {
        v.net = n;
        v.hp = n.hp / 255.0f;
        v.wrecked = (n.flags & royale::net::VehicleNet::kWrecked) != 0;
        v.driver = n.driver;
        v.passenger = n.passenger;
    }
    if (mine) {
        v.body = gRide.body;
        v.air = gRide.air;
        v.drift = gRide.handbrake && std::fabs(gRide.body.speed) > 60.0f;
    } else if (sampled) {
        royale::CartBody& b = v.body;
        b.x = n.x; b.z = n.z;
        b.yaw = royale::BinangToYaw(n.yaw);
        b.steer = n.steer / 100.0f;
        b.speed = n.speed;
        v.air = n.air;
        v.drift = (n.flags & royale::net::VehicleNet::kDrift) != 0;
        // sat on the real ground under its wheels (the server only knows its own rougher copy of the ground), tilted to it
        gCartRefY = v.placed ? b.y : n.y;
        const royale::cartdetail::Footing f = royale::cartdetail::FootingAt(ClientCartWorld(), b.x, b.z, b.yaw);
        const float groundY = f.any ? f.y : n.y - n.air;
        const float k = v.placed ? 0.35f : 1.0f;
        b.y = v.placed ? b.y + (groundY + n.air - b.y) * 0.6f : groundY + n.air;
        if (f.any) { b.pitch += (f.pitch - b.pitch) * k; b.roll += (f.roll - b.roll) * k; }
        b.grounded = n.air < 1.0f;
    }
    // the wheels roll by how far it has gone along its heading
    if (v.placed) {
        const float along = (v.body.x - v.prevX) * std::sin(v.body.yaw) + (v.body.z - v.prevZ) * std::cos(v.body.yaw);
        if (std::fabs(along) < 300.0f) for (float& s : v.spin) s = std::fmod(s + along / C::kWheelRadius, 6.2831853f);
    }
    v.prevX = v.body.x; v.prevZ = v.body.z;
    v.placed = true;
    actor->world.pos = { v.body.x, v.body.y, v.body.z };
    actor->shape.rot.y = actor->world.rot.y = royale::YawToBinang(v.body.yaw);
    actor->focus.pos = actor->world.pos;
    actor->focus.pos.y += 60.0f;
    actor->floorHeight = v.body.y - v.air;   // its shadow, on the ground under it
    if (v.wrecked && !wasWrecked) { v.wreckedAt = ImGui::GetTime(); CartBlastFx(play, v); }
    else if (v.hp < prevHp - 0.004f && !v.wrecked) {
        v.hurtAt = ImGui::GetTime();
        if (!mine) CartCrashFx(play, v, 200.0f);   // your own crashes are shown as they happen
    }
    CartEffects(play, v);
}
void Cart_Draw(Actor* actor, PlayState* play) {
    auto of = gCartOf.find(actor);
    if (of == gCartOf.end()) return;
    DrawCart(play, gCarts[of->second]);
}
void Cart_Destroy(Actor* actor, PlayState* play) {
    ActorFunc orig = nullptr;
    auto of = gCartOf.find(actor);
    if (of != gCartOf.end()) {
        auto v = gCarts.find(of->second);
        if (v != gCarts.end()) { orig = v->second.origDestroy; gCarts.erase(v); }
        gCartOf.erase(of);
    }
    if (orig) orig(actor, play);
}

// A stand-in actor for each cart in the snapshot (the ones near you, and yours), gone when it leaves view or burns out.
void ReconcileCarts(const royale::HudState& hud) {
    const bool show = gSession.Joined() && InField() && gSession.Client() &&
                      (hud.state == royale::MatchState::InMatch || hud.state == royale::MatchState::Ending);
    gCartMapRadius = hud.map.radius;
    gCartMapCentre = hud.map.center;
    std::unordered_set<int> wanted;
    if (show) {
        for (const royale::net::VehicleNet& n : gSession.Client()->Vehicles()) {
            wanted.insert(n.index);
            if (gCarts.count(n.index)) continue;
            Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ISHI, n.x, n.y, n.z, 0, n.yaw, 0, 0, false);
            if (actor == nullptr) continue;
            CartView v;
            v.actor = actor; v.origDestroy = actor->destroy; v.index = n.index; v.net = n;
            v.body.x = n.x; v.body.y = n.y; v.body.z = n.z; v.body.yaw = royale::BinangToYaw(n.yaw);
            v.hp = n.hp / 255.0f; v.wrecked = (n.flags & royale::net::VehicleNet::kWrecked) != 0;
            v.driver = n.driver; v.passenger = n.passenger;
            gCarts[n.index] = v;
            gCartOf[actor] = n.index;
            actor->update = Cart_Update;
            actor->draw = Cart_Draw;
            actor->destroy = Cart_Destroy;
            actor->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
            actor->uncullZoneForward = 5000.0f; actor->uncullZoneScale = 1500.0f; actor->uncullZoneDownward = 1500.0f;
            actor->shape.shadowScale = 70.0f;
            actor->shape.shadowAlpha = 160;
        }
    }
    for (auto& [i, v] : gCarts) if (!wanted.count(i) && v.actor) { Actor_Kill(v.actor); v.actor = nullptr; }
}

// The free seat of a cart you are standing by, if there is one (the seat whose side you are on, else the other).
bool NearbyCartSeat(int* index, royale::Seat* seat) {
    if (!InField() || gRide.seated) return false;
    Player* pl = GET_PLAYER(gPlayState);
    const royale::Vec2 me = { pl->actor.world.pos.x, pl->actor.world.pos.z };
    float best = royale::kEnterRange;
    bool found = false;
    for (const auto& [i, v] : gCarts) {
        if (v.actor == nullptr || v.wrecked || std::fabs(v.body.speed) > 200.0f) continue;
        if (std::fabs(pl->actor.world.pos.y - v.body.y) > 80.0f) continue;
        for (royale::Seat s : { royale::Seat::Driver, royale::Seat::Passenger }) {
            const bool free = (s == royale::Seat::Driver ? v.driver : v.passenger) == royale::net::kNoPlayer16;
            if (!free) continue;
            const float d = std::min(royale::Distance(me, royale::ExitSpot(v.body, s)), royale::Distance(me, { v.body.x, v.body.z }) + 20.0f);
            if (d < best) { best = d; *index = i; *seat = s; found = true; }
        }
    }
    return found;
}

// ---- the controller --------------------------------------------------------------------------------------------------------------
// Runs before the game reads the controller. Riding, it is all the cart's: Link stays sat down (the back seat keeps B and the C items to shoot with).
// On foot by a cart, A gets in instead of rolling.
bool CartInput(Input& in, const royale::HudState& hud) {
    if (!LiveAndAlive(hud) || !InField()) { gRide.wantExit = gRide.wantSwitch = false; return false; }
    if (!gRide.seated) {
        int index; royale::Seat seat;
        if ((in.press.button & BTN_A) && !MessageBoxUp() && NearestLootIndex() == kNoLoot && NearbyCartSeat(&index, &seat) &&
            ImGui::GetTime() - gRide.enterAskedAt > 0.4) {
            gSession.EnterVehicle(index, seat);
            gRide.enterAskedAt = ImGui::GetTime();
            in.press.button &= ~BTN_A; in.cur.button &= ~BTN_A;
        }
        return false;
    }
    const float sx = in.cur.stick_x / 70.0f;
    gRide.steer = std::fabs(sx) < 0.12f ? 0.0f : -std::clamp(sx, -1.0f, 1.0f);   // stick right turns right
    const bool driver = gRide.seat == royale::Seat::Driver;
    gRide.throttle = driver && (in.cur.button & BTN_A) ? 1.0f : 0.0f;
    gRide.brake = false;
    if (driver && (in.cur.button & BTN_B)) {
        if (gRide.body.speed > 15.0f) gRide.brake = true;   // braking first, then backing up
        else gRide.throttle = -1.0f;
    }
    gRide.handbrake = driver && (in.cur.button & BTN_Z);
    if (in.press.button & BTN_R) gRide.wantExit = true;
    if (!driver && (in.press.button & BTN_A)) gRide.wantSwitch = true;
    // Link himself doesn't move: the stick and the buttons were the cart's (the passenger keeps B and the C items).
    const u16 keep = driver ? BTN_START : static_cast<u16>(BTN_START | BTN_B | BTN_CLEFT | BTN_CDOWN | BTN_DLEFT | BTN_DRIGHT);
    in.cur.button &= keep; in.press.button &= keep; in.rel.button &= keep;
    in.cur.stick_x = in.cur.stick_y = in.rel.stick_x = in.rel.stick_y = in.press.stick_x = in.press.stick_y = 0;
    return true;
}

// ---- you, in the saddle -----------------------------------------------------------------------------------------------------------
// Link stands on the ground under the saddle (so the game keeps him on his feet and the camera follows the cart), and is drawn up on it: his draw
// is lifted to the saddle and tilted with the cart, and his pose is the horse-riding one (LocalRide_Draw).
void PlaceOnSeat(Player* player, const royale::CartBody& body, royale::Seat seat) {
    float sx, sy, sz;
    royale::SeatSpot(body, seat, &sx, &sy, &sz);
    gCartRefY = body.y;
    float gy = body.y;
    CartGroundAt(sx, sz, &gy);
    player->actor.world.pos = { sx, gy, sz };
    player->actor.prevPos = player->actor.world.pos;
    player->actor.velocity = { 0, 0, 0 };
    player->actor.speedXZ = 0;
    player->linearVelocity = 0;
    player->fallStartHeight = static_cast<s16>(gy);
    player->fallDistance = 0;
    const s16 yaw = royale::YawToBinang(body.yaw);
    player->actor.shape.rot.y = player->actor.world.rot.y = player->yaw = yaw;
    const float lean = std::clamp(-body.speed * body.steer * 0.0011f, -0.22f, 0.22f);
    player->actor.shape.rot.x = royale::YawToBinang(-body.pitch);
    player->actor.shape.rot.z = royale::YawToBinang(body.roll + lean);
    const float scale = std::max(0.001f, player->actor.scale.y);
    player->actor.shape.yOffset = (sy - kSaddleDrop - gy) / scale;
}
void LeaveSeatPose(Player* player) {
    player->actor.shape.rot.x = player->actor.shape.rot.z = 0;
    player->actor.shape.yOffset = 0;
    if (player->actor.draw == LocalRide_Draw) player->actor.draw = Player_Draw;
}

// On foot, a cart is solid: you are pushed out of it to its nearest side (as the rocks push you, ApplyRocks).
void PushOutOfCarts(Player* player) {
    Vec3f& p = player->actor.world.pos;
    for (const auto& [i, v] : gCarts) {
        if (v.actor == nullptr || p.y > v.body.y + 70.0f || p.y < v.body.y - 60.0f) continue;
        const royale::CartBody& b = v.body;
        const float cy = std::cos(b.yaw), sy = std::sin(b.yaw);
        const float dx = p.x - b.x, dz = p.z - b.z;
        const float lx = dx * cy - dz * sy, lz = dx * sy + dz * cy;   // in the cart's axes (as royale::InsideCart)
        const float m = 10.0f;
        const float x0 = C::kMinX - m, x1 = C::kMaxX + m, z0 = C::kMinZ - m, z1 = C::kMaxZ + m;
        if (lx <= x0 || lx >= x1 || lz <= z0 || lz >= z1) continue;
        float nx = lx, nz = lz;
        const float out[4] = { lx - x0, x1 - lx, lz - z0, z1 - lz };
        const int side = static_cast<int>(std::min_element(out, out + 4) - out);
        if (side == 0) nx = x0; else if (side == 1) nx = x1; else if (side == 2) nz = z0; else nz = z1;
        p.x = b.x + nx * cy + nz * sy;
        p.z = b.z - nx * sy + nz * cy;
    }
}

void UpdateLocalRide(Player* player, const royale::HudState& hud) {
    royale::GameClient* client = gSession.Client();
    const double now = ImGui::GetTime();
    int index = -1;
    royale::Seat seat = royale::Seat::None;
    const bool riding = client && LiveAndAlive(hud) && InField() && client->MyVehicle(&index, &seat);
    CartView* v = riding ? CartViewOf(index) : nullptr;
    if (gRide.player != player) { gRide.seated = false; gRide.driving = false; gRide.player = player; }   // a new scene, a new Link

    // Climbing down: Link is shown stepping off the saddle, then stands at the cart's side.
    if (!gRide.seated && gRide.settleOnGround) {
        if (now - gRide.dismountAt < AnimSeconds(DismountAnim(gRide.dismountSeat)) && LiveAndAlive(hud)) {
            PlaceOnSeat(player, gRide.lastBody, gRide.dismountSeat);
            return;
        }
        gRide.settleOnGround = false;
        float gy = player->actor.world.pos.y;
        gCartRefY = gRide.lastBody.y;
        CartGroundAt(gRide.exitTo.x, gRide.exitTo.z, &gy);
        player->actor.world.pos = { gRide.exitTo.x, gy + 2.0f, gRide.exitTo.z };
        player->actor.prevPos = player->actor.world.pos;
        player->fallStartHeight = static_cast<s16>(gy);
        LeaveSeatPose(player);
    }

    if (gRide.seated && (!riding || index != gRide.index)) {   // off (by choice, or thrown off a wreck)
        const bool thrown = !LiveAndAlive(hud) || (CartViewOf(gRide.index) && CartViewOf(gRide.index)->wrecked);
        gRide.lastBody = gRide.driving ? gRide.body : (CartViewOf(gRide.index) ? CartViewOf(gRide.index)->body : gRide.lastBody);
        gRide.dismountSeat = gRide.seat;
        gRide.exitTo = royale::ExitSpot(gRide.lastBody, gRide.seat);
        gRide.seated = false;
        gRide.driving = false;
        gRide.index = -1;
        gRide.seat = royale::Seat::None;
        gRide.dismountAt = thrown ? -10.0 : now;
        gRide.settleOnGround = true;
        Player_PlaySfx(&player->actor, NA_SE_PL_GET_OFF_HORSE);
        if (thrown) { player->actor.velocity.y = 8.0f; }
        return;
    }
    if (!riding || v == nullptr) { if (!gRide.seated && !gRide.settleOnGround && InField()) PushOutOfCarts(player); return; }

    if (!gRide.seated || seat != gRide.seat) {   // just got on, or moved over to the other seat
        if (!gRide.seated) { gRide.mountAt = now; Player_PlaySfx(&player->actor, NA_SE_PL_SIT_ON_HORSE); StopEmote(); }
        gRide.seated = true;
        gRide.index = index;
        gRide.seat = seat;
        gRide.driving = false;
        gRide.wantExit = gRide.wantSwitch = gRide.exitWhenStopped = false;
        gRide.settleOnGround = false;
    }
    player->actor.draw = LocalRide_Draw;

    if (gRide.wantExit) {
        gRide.wantExit = false;
        if (seat == royale::Seat::Driver && std::fabs(gRide.body.speed) > 60.0f) gRide.exitWhenStopped = true;   // pull up first
        else gSession.ExitVehicle();
    }
    if (gRide.wantSwitch) { gRide.wantSwitch = false; if (v->driver == royale::net::kNoPlayer16) gSession.SwitchSeat(); }

    if (seat == royale::Seat::Driver) {
        if (!gRide.driving) {   // take over from where it is shown
            gRide.driving = true;
            gRide.body = v->body;
            gRide.body.speed = v->net.speed;
            gRide.body.grounded = v->air < 1.0f;
        }
        royale::CartControls c;
        c.throttle = gRide.throttle; c.steer = gRide.steer; c.brake = gRide.brake; c.handbrake = gRide.handbrake;
        if (now - gRide.mountAt < kSaddleClimbSeconds) c = {};   // still climbing on
        if (gRide.exitWhenStopped) {   // R at speed: pull up, then get out
            c.throttle = 0; c.brake = true;
            if (std::fabs(gRide.body.speed) < 40.0f) { gSession.ExitVehicle(); gRide.exitWhenStopped = false; }
        }
        gCartRefY = gRide.body.y;
        gCartFrom = { gRide.body.x, gRide.body.z };
        gCartSelf = index;
        const royale::CartStep st = royale::StepCart(gRide.body, c, ClientCartWorld(), 1.0f / royale::kTickHz);
        gCartSelf = -1;
        gRide.air = 0.0f;
        if (!gRide.body.grounded) {
            gCartRefY = gRide.body.y;
            const royale::cartdetail::Footing f = royale::cartdetail::FootingAt(ClientCartWorld(), gRide.body.x, gRide.body.z, gRide.body.yaw);
            if (f.any) gRide.air = std::max(0.0f, gRide.body.y - f.y);
        }
        gSession.SendDrive(index, gRide.body, gRide.air, gRide.handbrake, st.impact, st.landing);
        if (st.impact > royale::kCartCrashFrom) { CartCrashFx(gPlayState, *v, st.impact); gRide.shake = std::min(1.0f, st.impact / 500.0f); }
        if (st.landing > 300.0f) gRide.shake = std::max(gRide.shake, std::min(1.0f, st.landing / 1200.0f));
        // The game's own rumble for a crash or a hard landing.
        if (gRide.shake > 0.05f) { func_800AA000(0.0f, static_cast<u8>(120 + 120 * gRide.shake), 10, 150); gRide.shake = 0; }
    } else {
        gRide.driving = false;
    }
    PlaceOnSeat(player, gRide.driving ? gRide.body : v->body, seat);
}

void PoseRider(Player* player) {
    const double now = ImGui::GetTime();
    LinkAnimationHeader* anim = RA(uma_wait_1);
    float f = 0;
    if (!gRide.seated && gRide.settleOnGround) {   // climbing down
        anim = DismountAnim(gRide.dismountSeat);
        f = static_cast<float>(now - gRide.dismountAt) * royale::kTickHz;
    } else if (now - gRide.mountAt < AnimSeconds(MountAnim(gRide.seat))) {   // climbing on
        anim = MountAnim(gRide.seat);
        f = static_cast<float>(now - gRide.mountAt) * royale::kTickHz;
    } else {
        // sat down: the game's riding idles; trotting along when it goes, a bouncier one flat out
        const float speed = gRide.driving ? std::fabs(gRide.body.speed) : (CartViewOf(gRide.index) ? std::fabs(CartViewOf(gRide.index)->body.speed) : 0.0f);
        anim = speed > 300.0f ? RA(uma_anim_slowrun) : speed > 60.0f ? RA(uma_anim_walk) : RA(uma_wait_1);
        const float len = static_cast<float>(Animation_GetLastFrame(anim)) + 1.0f;
        f = std::fmod(static_cast<float>(now) * royale::kTickHz * (speed > 60.0f ? std::clamp(speed / 250.0f, 0.6f, 1.6f) : 1.0f), len);
    }
    const float last = static_cast<float>(Animation_GetLastFrame(anim));
    LoadLinkFrame(anim, static_cast<int>(std::clamp(f, 0.0f, last)), player->skelAnime.limbCount, player->skelAnime.jointTable);
}
void LocalRide_Draw(Actor* actor, PlayState* play) {
    Player* player = reinterpret_cast<Player*>(actor);
    if (player == GET_PLAYER(play) && (gRide.seated || gRide.settleOnGround)) PoseRider(player);
    Player_Draw(actor, play);
}

// ---- other players in carts (Puppet_Update calls this) ----------------------------------------------------------------------------
// Returns true while the puppet is sat in a cart (or climbing down from one): it is placed and posed here.
bool PuppetRide(PlayState* play, Player* player, PuppetMotion& m, uint16_t id) {
    Actor* actor = &player->actor;
    RiderPose rp;
    if (CartRiderPose(id, &rp)) {
        const uint8_t key = static_cast<uint8_t>(240 + static_cast<int>(rp.seat));
        const float speed = std::fabs(rp.speed);
        const uint8_t gait = speed > 300.0f ? 2 : speed > 60.0f ? 1 : 0;
        auto gaitAnim = [](uint8_t g) { return g == 2 ? RA(uma_anim_slowrun) : g == 1 ? RA(uma_anim_walk) : RA(uma_wait_1); };
        if (m.anim != key) {   // just got on: the climb into the saddle first
            StartSeq(play, player, m, AnimSeq(MountAnim(rp.seat), false), -2.0f);
            m.anim = key;
            m.rideStage = 0;
            PuppetSfx(actor, NA_SE_PL_SIT_ON_HORSE);
        }
        const bool finished = LinkAnimation_Update(play, &player->skelAnime);
        if (m.rideStage == 0 && finished) {   // sat down: the riding idle (or trot) from here
            StartSeq(play, player, m, AnimSeq(gaitAnim(gait), true), -3.0f);
            m.rideStage = 1;
            m.rideGait = gait;
        } else if (m.rideStage == 1 && gait != m.rideGait) {
            StartSeq(play, player, m, AnimSeq(gaitAnim(gait), true), -6.0f);
            m.rideGait = gait;
        }
        if (m.rideStage == 1 && gait > 0) player->skelAnime.playSpeed = std::clamp(speed / 250.0f, 0.6f, 1.6f);
        actor->world.pos = { rp.x, rp.y - kSaddleDrop, rp.z };
        actor->shape.rot.y = actor->world.rot.y = rp.yaw;
        actor->shape.rot.x = rp.pitch;
        actor->shape.rot.z = rp.roll;
        actor->focus.pos = actor->world.pos;
        actor->focus.pos.y += 50.0f;
        m.rideSeat = rp.seat;
        m.rideAt = actor->world.pos;
        m.rideYaw = rp.yaw;
        m.dismount = 0;
        return true;   // (the animation's own root movement is kept: the climb up onto the saddle is the point of it)
    }
    if (m.anim == 240 || m.anim == 241) {   // just got off: climb down from where the saddle was
        StartSeq(play, player, m, AnimSeq(DismountAnim(m.rideSeat), false), -2.0f);
        m.anim = 239;
        m.dismount = static_cast<int>(AnimSeconds(DismountAnim(m.rideSeat)) * royale::kTickHz);
        PuppetSfx(actor, NA_SE_PL_GET_OFF_HORSE);
    }
    if (m.dismount > 0) {
        m.dismount--;
        LinkAnimation_Update(play, &player->skelAnime);
        actor->world.pos = m.rideAt;
        actor->shape.rot.y = actor->world.rot.y = m.rideYaw;
        actor->shape.rot.x = actor->shape.rot.z = 0;
        if (m.dismount == 0) m.anim = 255;   // back to whatever they are doing
        return true;
    }
    actor->shape.rot.x = actor->shape.rot.z = 0;
    return false;
}

// ---- the HUD: getting in, the controls while driving, the cart's health ------------------------------------------------------------
std::string CartPromptText() {
    int index; royale::Seat seat;
    if (!NearbyCartSeat(&index, &seat)) return {};
    return seat == royale::Seat::Driver ? "Drive the cart" : "Ride in the cart";
}
void DrawCartHud(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale) {
    if (!gRide.seated || !InField()) return;
    CartView* v = CartViewOf(gRide.index);
    if (v == nullptr) return;
    const float w = 180.0f * scale, hgt = 10.0f * scale;
    const ImVec2 a((ds.x - w) * 0.5f, ds.y - 118.0f * scale);
    const char* title = gRide.seat == royale::Seat::Driver ? "Lon Lon Buggy" : "Riding along";
    const float ts = 18.0f * scale;
    ImVec2 sz = font->CalcTextSizeA(ts, FLT_MAX, 0.0f, title);
    dl->AddText(font, ts, ImVec2((ds.x - sz.x) * 0.5f + 1.5f, a.y - ts - 2.5f * scale), IM_COL32(0, 0, 0, 220), title);
    dl->AddText(font, ts, ImVec2((ds.x - sz.x) * 0.5f, a.y - ts - 4.0f * scale), IM_COL32(255, 226, 150, 255), title);
    dl->AddRectFilled(ImVec2(a.x - 2, a.y - 2), ImVec2(a.x + w + 2, a.y + hgt + 2), IM_COL32(0, 0, 0, 190), 3.0f);
    const float hp = std::clamp(v->hp, 0.0f, 1.0f);
    const ImU32 col = hp > 0.5f ? IM_COL32(150, 110, 60, 255) : hp > 0.25f ? IM_COL32(230, 150, 50, 255) : IM_COL32(230, 60, 40, 255);
    dl->AddRectFilled(a, ImVec2(a.x + w * hp, a.y + hgt), col, 2.0f);
    const char* help = gRide.seat == royale::Seat::Driver ? "A go   B brake/back   Stick steer   Z slide   R get out"
                                                          : (v->driver == royale::net::kNoPlayer16 ? "A take the reins   R get out   B shoot" : "R get out   B shoot");
    const float hs = 15.0f * scale;
    sz = font->CalcTextSizeA(hs, FLT_MAX, 0.0f, help);
    dl->AddText(font, hs, ImVec2((ds.x - sz.x) * 0.5f + 1.0f, a.y + hgt + 5.0f * scale + 1.0f), IM_COL32(0, 0, 0, 220), help);
    dl->AddText(font, hs, ImVec2((ds.x - sz.x) * 0.5f, a.y + hgt + 5.0f * scale), IM_COL32(235, 235, 235, 255), help);
}
// On the minimap: a little brown cart for each one you can see (white edged when somebody is in it, grey when it is a wreck).
void DrawCartsOnMap(ImDrawList* dl, float scale, const std::function<ImVec2(float, float)>& toMap, const std::function<bool(ImVec2)>& inside) {
    for (const auto& [i, v] : gCarts) {
        if (v.actor == nullptr) continue;
        const ImVec2 p = toMap(v.body.x, v.body.z);
        if (!inside(p)) continue;
        const float u = 3.6f * scale;
        const bool used = v.driver != royale::net::kNoPlayer16 || v.passenger != royale::net::kNoPlayer16;
        dl->AddRectFilled(ImVec2(p.x - u, p.y - u * 0.7f), ImVec2(p.x + u, p.y + u * 0.7f), v.wrecked ? IM_COL32(90, 90, 90, 255) : IM_COL32(170, 110, 50, 255), 1.0f);
        dl->AddRect(ImVec2(p.x - u, p.y - u * 0.7f), ImVec2(p.x + u, p.y + u * 0.7f), used ? IM_COL32(255, 255, 255, 240) : IM_COL32(40, 25, 10, 230), 1.0f, 0, 1.3f);
    }
}
// A cart as something to hit with a sword or an arrow.
bool CartTargetAt(uint16_t id, float* x, float* z) {
    if (!royale::IsVehicleId(id)) return false;
    const CartView* v = CartViewOf(static_cast<int>(id - royale::kVehicleIdBase));
    if (v == nullptr) return false;
    *x = v->body.x; *z = v->body.z;
    return true;
}
struct CartTarget { uint16_t id; float x, z; };
std::vector<CartTarget> CartTargets() {
    std::vector<CartTarget> out;
    for (const auto& [i, v] : gCarts) {
        if (v.actor == nullptr || v.wrecked || gRide.index == i) continue;   // not the one you are sat in
        out.push_back({ static_cast<uint16_t>(royale::kVehicleIdBase + i), v.body.x, v.body.z });
    }
    return out;
}

void ForgetCarts() {
    gCarts.clear();
    gCartOf.clear();
    gRide.seated = gRide.driving = gRide.settleOnGround = false;
    gRide.index = -1;
    gRide.player = nullptr;
}

// ---- lobby music from a folder ----------------------------------------------------------------------------------------------
// Put .wav files in the "music" folder inside the game's data folder and they play in a shuffled loop while you wait in the lobby, then fade out when
// the countdown starts. (Only WAV is read: the game has no MP3/OGG decoder to hook into.) The folder is created on first use.
void Say(const std::string& text);
bool HeldGlowOn(bool self) {
    return self ? MapOption("HeldGlowSelf", false) : MapOption("HeldGlow", true);
}

struct LobbyMusic {
    std::vector<std::filesystem::path> tracks;
    std::vector<int16_t> pcm;   // a track just read, converted to the game's output format (stereo, 44.1 kHz), until it is handed to the mixer
    size_t next = 0;
    bool scanned = false;
    bool failed = false;
    bool playing = false;
    std::string nowPlaying;
    std::string status;         // shown in the menu: where the folder is, how many songs, and why one could not be read
};
LobbyMusic gLobbyMusic;


std::filesystem::path MusicFolder() { return std::filesystem::path(Ship::Context::GetPathRelativeToAppDirectory("music")); }

// A breadcrumb trail for crashes the phone gives no log for: each step is written to royale-trace.txt (next to the music folder) and closed
// again straight away, so the last line says how far the game got. Each run starts a new file; the one from the run before is kept as
// royale-trace-prev.txt so it is still there to read after the game is reopened.
char gTraceLast[160] = "(nothing yet)";   // the newest step, for the crash report

// Free memory in MB as Linux and Android report it, or -1. A phone that runs low closes the game with no message at all.
int FreeMemoryMb() {
    std::ifstream in("/proc/meminfo");
    std::string key;
    long kb = 0;
    std::string unit;
    while (in >> key >> kb >> unit) {
        if (key == "MemAvailable:") return static_cast<int>(kb / 1024);
    }
    return -1;
}

void Trace(const char* step) {
    static bool fresh = true;
    static std::string last;
    if (last == step) return;   // a step drawn every frame is written once
    last = step;
    std::snprintf(gTraceLast, sizeof(gTraceLast), "%s", step);
    const std::filesystem::path file(Ship::Context::GetPathRelativeToAppDirectory("royale-trace.txt"));
    if (fresh) {
        std::error_code ec;
        std::filesystem::rename(file, Ship::Context::GetPathRelativeToAppDirectory("royale-trace-prev.txt"), ec);
    }
    std::ofstream out(file, fresh ? std::ios::trunc : std::ios::app);
    fresh = false;
    if (out) {
        out << step;
        if (std::strncmp(step, "start:", 6) == 0) out << " (free memory " << FreeMemoryMb() << " MB)";
        out << "\n";
    }
}

// ---- crash report ---------------------------------------------------------------------------------------------------------------
// When the game dies from a native crash (a bad pointer, an abort), Android gives the player nothing to send. So a handler writes
// royale-crash.txt next to the music folder: which signal, where it faulted (library, offset and name for each step of the call stack) and the
// last step of the breadcrumb trail. The next time the Battle Royale menu opens it shows that text so it can be photographed, and the file stays
// until dismissed. The offsets are looked up against the symbols file that the game build publishes.
#ifdef __ANDROID__
char gCrashPath[600] = "";
struct sigaction gOldAction[6];
const int kCrashSignals[6] = { SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGTRAP };

struct CrashStack { void* pc[40]; int n; };
_Unwind_Reason_Code CrashUnwind(struct _Unwind_Context* ctx, void* arg) {
    CrashStack* st = static_cast<CrashStack*>(arg);
    const uintptr_t pc = _Unwind_GetIP(ctx);
    if (pc == 0) return _URC_NO_REASON;
    if (st->n >= 40) return _URC_END_OF_STACK;
    st->pc[st->n++] = reinterpret_cast<void*>(pc);
    return _URC_NO_REASON;
}
void CrashLine(int fd, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void CrashLine(int fd, const char* fmt, ...) {
    char buf[400];
    va_list args;
    va_start(args, fmt);
    const int n = std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n > 0) { const ssize_t ignored = write(fd, buf, std::min<int>(n, sizeof(buf) - 1)); (void)ignored; }
}
void CrashHandler(int sig, siginfo_t* info, void*) {
    static volatile sig_atomic_t busy = 0;
    if (!busy) {
        busy = 1;
        const int fd = open(gCrashPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            CrashLine(fd, "Version %s\nSignal %d code %d fault address %p thread %ld\nFeature running: %s\nLast step: %s\n", ROYALE_BUILD_VERSION, sig, info ? info->si_code : 0,
                      info ? info->si_addr : nullptr, static_cast<long>(syscall(SYS_gettid)), gFeature, gTraceLast);
            CrashStack st;
            st.n = 0;
            _Unwind_Backtrace(CrashUnwind, &st);
            for (int i = 0; i < st.n; i++) {
                Dl_info di;
                if (dladdr(st.pc[i], &di) && di.dli_fname != nullptr) {
                    const char* base = std::strrchr(di.dli_fname, '/');
                    CrashLine(fd, "#%d %s+0x%lx %s\n", i, base ? base + 1 : di.dli_fname,
                              static_cast<unsigned long>(reinterpret_cast<uintptr_t>(st.pc[i]) - reinterpret_cast<uintptr_t>(di.dli_fbase)), di.dli_sname ? di.dli_sname : "");
                } else {
                    CrashLine(fd, "#%d %p\n", i, st.pc[i]);
                }
            }
            close(fd);
        }
    }
    // Hand over to whoever was there before (Android's own crash reporter), so the game still ends the usual way.
    for (int i = 0; i < 6; i++) if (kCrashSignals[i] == sig) sigaction(sig, &gOldAction[i], nullptr);
    if (info != nullptr && info->si_code <= 0) raise(sig);   // sent by abort() and the like: send it again; a real fault just happens again on return
}
void InstallCrashReporter() {
    const std::string path = Ship::Context::GetPathRelativeToAppDirectory("royale-crash.txt");
    std::snprintf(gCrashPath, sizeof(gCrashPath), "%s", path.c_str());
    for (int i = 0; i < 6; i++) {
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = CrashHandler;
        sa.sa_flags = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        sigaction(kCrashSignals[i], &sa, &gOldAction[i]);
    }
}
#else
void InstallCrashReporter() {}
#endif

// A crash report from the last run, shown at the top of the Battle Royale menu until dismissed.
// Android's own record of why the last run ended (written by ExitReport.java at start), shown until dismissed.
void DrawExitReason() {
    static int state = 0;   // 0: not looked yet, 1: showing, 2: none or dismissed
    static std::string text;
    const std::filesystem::path file(Ship::Context::GetPathRelativeToAppDirectory("royale-exit-reason.txt"));
    if (state == 0) {
        std::ifstream in(file);
        std::stringstream ss;
        if (in) ss << in.rdbuf();
        text = ss.str();
        state = text.empty() ? 2 : 1;
    }
    if (state != 1) return;
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Android's note on why the game ended last time. Please send a photo of this to Claude:");
    ImGui::TextWrapped("%s", text.c_str());
    if (ImGui::Button("Dismiss Android note")) {
        std::error_code ec;
        std::filesystem::remove(file, ec);
        state = 2;
    }
    ImGui::Separator();
}

void DrawCrashReport() {
    static int state = 0;   // 0: not looked yet, 1: showing a crash, 2: none or dismissed, 3: showing a silent stop
    static std::string text;
    const std::filesystem::path file(Ship::Context::GetPathRelativeToAppDirectory("royale-crash.txt"));
    const std::filesystem::path prev(Ship::Context::GetPathRelativeToAppDirectory("royale-trace-prev.txt"));
    if (state == 0) {
        std::ifstream in(file);
        std::stringstream ss;
        if (in) ss << in.rdbuf();
        text = ss.str();
        state = text.empty() ? 2 : 1;
        if (state == 2) {
            // No crash report, but the last run's trail may end in the middle of a match start: the phone closed the game without a signal
            std::ifstream trail(prev);
            std::string line, lastLine;
            while (std::getline(trail, line)) if (!line.empty()) lastLine = line;
            if (std::strncmp(lastLine.c_str(), "start:", 6) == 0 && lastLine.find("match started") == std::string::npos) {
                text = lastLine;
                state = 3;
            }
        }
    }
    if (state == 1) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "The game closed unexpectedly last time. Please send a photo of this to Claude:");
        ImGui::TextWrapped("%s", text.c_str());
        if (ImGui::Button("Dismiss crash report")) {
            std::error_code ec;
            std::filesystem::remove(file, ec);
            state = 2;
        }
        ImGui::Separator();
    } else if (state == 3) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Last time the game stopped while starting a match, with no crash report (Android may have closed it, for example for memory). Please send a photo of this to Claude:");
        ImGui::TextWrapped("%s", text.c_str());
        if (ImGui::Button("Dismiss")) {
            std::error_code ec;
            std::filesystem::remove(prev, ec);
            state = 2;
        }
        ImGui::Separator();
    }
}

void QueueOotSongs();
// The menu is drawn on the render thread, but a scan also copies the game's soundfonts out of its audio data (CaptureOotFonts), which only the
// game thread may touch. So the menu asks for a scan with this flag and the game thread's frame update does it.
bool gScanRequested = false;
void ScanMusicFolder() {
    Trace("music scan: start");
    gLobbyMusic.tracks.clear();
    std::error_code ec;
    std::filesystem::create_directories(MusicFolder(), ec);
    for (const auto& e : std::filesystem::directory_iterator(MusicFolder(), ec)) {
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (e.is_regular_file(ec) && ext == ".wav") gLobbyMusic.tracks.push_back(e.path());
    }
    std::sort(gLobbyMusic.tracks.begin(), gLobbyMusic.tracks.end());
    std::mt19937 rng(std::random_device{}());
    std::shuffle(gLobbyMusic.tracks.begin(), gLobbyMusic.tracks.end(), rng);
    gLobbyMusic.next = 0;
    gLobbyMusic.scanned = true;
    gLobbyMusic.failed = false;
    gLobbyMusic.status = std::to_string(gLobbyMusic.tracks.size()) + " .wav song(s) found in " + MusicFolder().string();
    Trace("music scan: listed");
    QueueOotSongs();
    Trace("music scan: done");
}

// A WAV reader of our own. The SDL on the phone aborts the whole game on some perfectly good .wav files (a format chunk bigger than it
// expects, as in WAVE_FORMAT_EXTENSIBLE files, makes its fortified read stop the process), and a game that dies while loading a song is no use.
// This reads the chunks with every size checked against the file, understands 8, 16, 24 and 32-bit PCM and 32-bit float in mono or stereo (more
// channels: the first two), and gives back 16-bit samples the way SDL_LoadWAV does (free the result with SDL_FreeWAV). Returns nullptr and sets
// the SDL error text when the file is not usable.
Uint8* SafeLoadWav(const std::string& path, SDL_AudioSpec* spec, Uint8** audioBuf, Uint32* audioLen) {
    constexpr uintmax_t kMaxWavBytes = 400u * 1024u * 1024u;
    std::error_code ec;
    const uintmax_t fileSize = std::filesystem::file_size(path, ec);
    if (ec) { SDL_SetError("cannot open the file"); return nullptr; }
    if (fileSize < 44 || fileSize > kMaxWavBytes) { SDL_SetError("the file is empty or too big"); return nullptr; }
    std::vector<uint8_t> file(static_cast<size_t>(fileSize));
    {
        std::ifstream in(path, std::ios::binary);
        if (!in || !in.read(reinterpret_cast<char*>(file.data()), static_cast<std::streamsize>(file.size()))) { SDL_SetError("cannot read the file"); return nullptr; }
    }
    auto u16 = [&](size_t at) -> uint32_t { return at + 2 <= file.size() ? static_cast<uint32_t>(file[at] | (file[at + 1] << 8)) : 0u; };
    auto u32 = [&](size_t at) -> uint32_t { return at + 4 <= file.size() ? u16(at) | (u16(at + 2) << 16) : 0u; };
    if (std::memcmp(file.data(), "RIFF", 4) != 0 || std::memcmp(file.data() + 8, "WAVE", 4) != 0) { SDL_SetError("not a RIFF WAVE file"); return nullptr; }
    uint32_t tag = 0, channels = 0, rate = 0, bits = 0;
    size_t dataAt = 0, dataLen = 0;
    bool haveFmt = false, haveData = false;
    size_t pos = 12;
    while (pos + 8 <= file.size() && !haveData) {
        const size_t size = u32(pos + 4);
        const size_t body = pos + 8;
        const size_t avail = file.size() - body;
        if (std::memcmp(file.data() + pos, "fmt ", 4) == 0 && size >= 16 && avail >= 16) {
            tag = u16(body); channels = u16(body + 2); rate = u32(body + 4); bits = u16(body + 14);
            if (tag == 0xFFFE && size >= 26 && avail >= 26) tag = u16(body + 24);   // WAVE_FORMAT_EXTENSIBLE: the real format is in the sub-format
            haveFmt = true;
        } else if (std::memcmp(file.data() + pos, "data", 4) == 0) {
            dataAt = body; dataLen = std::min(size, avail); haveData = true;
        }
        const size_t next = body + size + (size & 1);
        if (next <= pos || next > file.size()) break;   // a chunk that claims to run past the end of the file ends the walk
        pos = next;
    }
    if (!haveFmt || !haveData) { SDL_SetError("no format or no sound data in the file"); return nullptr; }
    const bool pcm = tag == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
    const bool flt = tag == 3 && bits == 32;
    if (!pcm && !flt) { SDL_SetError("unsupported sample format"); return nullptr; }
    if (channels < 1 || channels > 8 || rate < 4000 || rate > 192000) { SDL_SetError("unsupported channel count or sample rate"); return nullptr; }
    const size_t bytesPer = bits / 8;
    const size_t frames = dataLen / (bytesPer * channels);
    if (frames == 0) { SDL_SetError("no sound data in the file"); return nullptr; }
    const uint32_t outChannels = channels >= 2 ? 2 : 1;
    const size_t outBytes = frames * outChannels * sizeof(int16_t);
    Uint8* out = static_cast<Uint8*>(SDL_malloc(outBytes));
    if (out == nullptr) { SDL_SetError("out of memory"); return nullptr; }
    int16_t* dst = reinterpret_cast<int16_t*>(out);
    for (size_t f = 0; f < frames; f++) {
        for (uint32_t c = 0; c < outChannels; c++) {
            const uint8_t* src = file.data() + dataAt + (f * channels + c) * bytesPer;
            int16_t v = 0;
            if (flt) {
                float x;
                std::memcpy(&x, src, 4);
                v = static_cast<int16_t>(std::clamp(x, -1.0f, 1.0f) * 32767.0f);
            } else if (bits == 8) v = static_cast<int16_t>((static_cast<int>(src[0]) - 128) << 8);
            else if (bits == 16) v = static_cast<int16_t>(src[0] | (src[1] << 8));
            else v = static_cast<int16_t>(src[bytesPer - 2] | (src[bytesPer - 1] << 8));   // 24 and 32-bit: the top 16 bits
            *dst++ = v;
        }
    }
    *spec = SDL_AudioSpec();
    spec->freq = static_cast<int>(rate);
    spec->format = AUDIO_S16SYS;
    spec->channels = static_cast<Uint8>(outChannels);
    spec->samples = 4096;
    *audioBuf = out;
    *audioLen = static_cast<Uint32>(outBytes);
    return out;
}

bool LoadNextTrack() {
    Trace("song load: start");
    LobbyMusic& m = gLobbyMusic;
    for (size_t tries = 0; tries < m.tracks.size(); tries++) {
        const std::filesystem::path& file = m.tracks[m.next++ % m.tracks.size()];
        SDL_AudioSpec spec = {};
        Uint8* buf = nullptr;
        Uint32 len = 0;
        if (SafeLoadWav(file.string(), &spec, &buf, &len) == nullptr) {
            m.status = "Could not read " + file.filename().string() + ": " + SDL_GetError() + " (use a 16-bit PCM .wav)";
            continue;
        }
        SDL_AudioCVT cvt;
        if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, 2, 44100) < 0) { SDL_FreeWAV(buf); continue; }
        if (!cvt.needed) {   // already 16-bit stereo at 44100 Hz: take it as it is, with no second and third copy of a big song in memory
            m.pcm.assign(reinterpret_cast<const int16_t*>(buf), reinterpret_cast<const int16_t*>(buf) + len / 2);
            SDL_FreeWAV(buf);
        } else {
            cvt.len = static_cast<int>(len);
            std::vector<Uint8> work(static_cast<size_t>(len) * (cvt.len_mult > 0 ? cvt.len_mult : 1));
            std::memcpy(work.data(), buf, len);
            SDL_FreeWAV(buf);
            cvt.buf = work.data();
            if (SDL_ConvertAudio(&cvt) < 0) continue;
            m.pcm.assign(reinterpret_cast<const int16_t*>(work.data()), reinterpret_cast<const int16_t*>(work.data()) + static_cast<size_t>(cvt.len_cvt) / 2);
        }
        m.nowPlaying = file.stem().string();
        Trace("song load: done");
        return !m.pcm.empty();
    }
    return false;
}

// ---- the music folder's songs, played with Ocarina of Time's own instruments ---------------------------------------------------------
// With the "OoT instruments" option on, each .wav in the music folder is turned into real OoT music in the background: Basic Pitch
// finds its notes, the drum finder its drums, and the game's own soundfonts are searched for the instruments that sound most like each
// part (the same steps as tools/song-to-oot.html). The result is a sequence in the game's own format, which the game's sound engine
// plays on its second music player, exactly like its own songs. Each song is converted once and kept in music/.oot (it holds only notes
// and the numbers of the game's soundfonts and instruments, nothing from the game's data). Until a song is ready, the original plays.
struct OotRawSample {
    std::vector<uint8_t> data;
    int codec = 0, order = 0, npred = 0, loopStart = -1, loopEnd = -1;
    std::vector<int16_t> book;
};
struct OotSong {
    enum State { Waiting, Working, Ready, Failed } state = Waiting;
    std::filesystem::path wav;
    royale::seq::Sequence seq;
    std::string why;                    // for Failed
};
struct OotMusic {
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    bool quit = false;
    bool captured = false;              // the soundfonts were copied out of the game (on the game thread)
    royale::music::Bank bank;           // owned by the worker once captured
    std::vector<OotRawSample> raw;
    std::vector<std::shared_ptr<OotSong>> songs;
    std::string status;                 // shown in the menu
    std::shared_ptr<OotSong> playing;   // on the game's second music player now
    float lastVolume = -1;
};
OotMusic gOot;
constexpr int kOotSeqPlayer = SEQ_PLAYER_BGM_SUB;
constexpr uint32_t kOotCacheVersion = 1;   // bump when the conversion changes, so old conversions are made again

// Off unless the player turns it on: the conversion copies every soundfont out of the game and runs a background worker, which is heavy for a phone.
bool OotInstrumentsOn() { return MapOption("OotConvert", false); }

// The game's music soundfonts (the ones its songs use, not the sound effects), copied out for the worker: every instrument and drum
// kit, with its samples still packed as the game keeps them. Runs once, on the game thread.
void CaptureOotFonts() {
    if (gOot.captured) return;
    std::set<int> fonts;
    for (size_t id = 2; id <= 108 && id < sequenceMapSize; id++) {
        if (!sequenceMap[id]) continue;
        const SequenceData sd = ResourceMgr_LoadSeqByName(sequenceMap[id]);
        for (int i = 0; i < sd.numFonts && i < 16; i++) fonts.insert(sd.fonts[i]);
    }
    royale::music::Bank bank;
    std::vector<OotRawSample> raw;
    std::unordered_map<const SoundFontSample*, int> seen;
    auto sampleOf = [&](const SoundFontSample* s) -> int {
        if (!s || !s->sampleAddr || s->size == 0) return -1;
        auto it = seen.find(s);
        if (it != seen.end()) return it->second;
        OotRawSample r;
        r.codec = s->codec;
        if (r.codec != CODEC_ADPCM && r.codec != CODEC_SMALL_ADPCM && r.codec != CODEC_S16 && r.codec != CODEC_S16_INMEMORY) return seen[s] = -1;
        if (r.codec == CODEC_ADPCM || r.codec == CODEC_SMALL_ADPCM) {
            if (!s->book || !s->book->book || s->book->order < 1 || s->book->order > 8 || s->book->npredictors < 1 || s->book->npredictors > 16) return seen[s] = -1;
            r.order = s->book->order; r.npred = s->book->npredictors;
            r.book.assign(s->book->book, s->book->book + 8 * r.order * r.npred);
        }
        r.data.assign(s->sampleAddr, s->sampleAddr + s->size);
        if (s->loop && s->loop->count != 0 && s->loop->end > s->loop->start + 8) { r.loopStart = static_cast<int>(s->loop->start); r.loopEnd = static_cast<int>(s->loop->end); }
        raw.push_back(std::move(r));
        return seen[s] = static_cast<int>(raw.size()) - 1;
    };
    auto envelope = [](royale::music::Zone& z, const AdsrEnvelope* e) {
        int16_t pairs[64] = {};
        int n = 0;
        if (e) for (; n < 32; n++) { pairs[2 * n] = e[n].delay; pairs[2 * n + 1] = e[n].arg; if (e[n].delay <= 0) { n++; break; } }
        royale::music::SetEnvelope(z, e ? pairs : nullptr, n);
    };
    for (int f : fonts) {
        if (f < 0 || f >= 256 || !fontMap[f]) continue;
        const SoundFont* sf = ResourceMgr_LoadAudioSoundFont(fontMap[f]);
        if (!sf) continue;
        for (int i = 0; i < sf->numInstruments && sf->instruments; i++) {
            const Instrument* ins = sf->instruments[i];
            if (!ins) continue;
            royale::music::Preset p;
            p.font = f; p.program = i;
            const int lo = ins->normalRangeLo, hi = ins->normalRangeHi, mo = royale::music::kMidiOffset;
            const int ranges[3][2] = {{lo > 0 ? 0 : -1, lo + mo - 1}, {lo + mo, hi + mo}, {hi < 127 ? hi + mo + 1 : -1, 127}};
            const SoundFontSound* sounds[3] = {&ins->lowNotesSound, &ins->normalNotesSound, &ins->highNotesSound};
            for (int slot = 0; slot < 3; slot++) {
                if (ranges[slot][0] < 0 || !sounds[slot]->sample) continue;
                royale::music::Zone z;
                z.lo = std::max(0, ranges[slot][0]); z.hi = std::min(127, ranges[slot][1]);
                if (z.lo > z.hi) continue;
                z.sample = sampleOf(sounds[slot]->sample);
                if (z.sample < 0) continue;
                z.tuning = sounds[slot]->tuning;
                envelope(z, ins->envelope);
                z.release = royale::music::ReleaseSeconds(ins->releaseRate);
                p.zones.push_back(z);
            }
            if (!p.zones.empty()) bank.presets.push_back(std::move(p));
        }
        royale::music::Preset kit;
        kit.font = f; kit.drums = true;
        for (int d = 0; d < sf->numDrums && sf->drums; d++) {
            const Drum* dr = sf->drums[d];
            const int key = d + royale::music::kMidiOffset;
            if (key > 127) break;
            if (!dr) continue;
            royale::music::Zone z;
            z.lo = z.hi = z.fixedKey = key;
            z.sample = sampleOf(dr->sound.sample);
            if (z.sample < 0) continue;
            z.tuning = dr->sound.tuning;
            envelope(z, dr->envelope);
            z.release = royale::music::ReleaseSeconds(dr->releaseRate);
            z.pan = (dr->pan - 64) / 64.0;
            kit.zones.push_back(z);
        }
        if (!kit.zones.empty()) bank.presets.push_back(std::move(kit));
    }
    std::lock_guard<std::mutex> lock(gOot.mutex);
    gOot.bank = std::move(bank);
    gOot.raw = std::move(raw);
    gOot.captured = true;
    gOot.wake.notify_all();
}

std::filesystem::path OotCachePath(const std::filesystem::path& wav) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(wav, ec);
    return MusicFolder() / ".oot" / (wav.stem().string() + "-" + std::to_string(ec ? 0 : size) + ".ootseq");
}
bool ReadOotCache(const std::filesystem::path& file, royale::seq::Sequence& s) {
    std::ifstream in(file, std::ios::binary);
    char magic[8] = {};
    uint32_t version = 0, nFonts = 0, size = 0;
    if (!in.read(magic, 8) || std::memcmp(magic, "OOTSEQ\0\0", 8) != 0) return false;
    if (!in.read(reinterpret_cast<char*>(&version), 4) || version != kOotCacheVersion) return false;
    if (!in.read(reinterpret_cast<char*>(&nFonts), 4) || nFonts > 16) return false;
    s.fonts.resize(nFonts);
    if (!in.read(reinterpret_cast<char*>(s.fonts.data()), nFonts) || !in.read(reinterpret_cast<char*>(&s.seconds), 8)) return false;
    if (!in.read(reinterpret_cast<char*>(&size), 4) || size == 0 || size > royale::seq::kMaxBytes) return false;
    s.data.resize(size);
    return static_cast<bool>(in.read(reinterpret_cast<char*>(s.data.data()), size)) && royale::seq::ParseSequence(s.data).ok;
}
void WriteOotCache(const std::filesystem::path& file, const royale::seq::Sequence& s) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary);
    const uint32_t version = kOotCacheVersion, nFonts = static_cast<uint32_t>(s.fonts.size()), size = static_cast<uint32_t>(s.data.size());
    out.write("OOTSEQ\0\0", 8);
    out.write(reinterpret_cast<const char*>(&version), 4);
    out.write(reinterpret_cast<const char*>(&nFonts), 4);
    out.write(reinterpret_cast<const char*>(s.fonts.data()), nFonts);
    out.write(reinterpret_cast<const char*>(&s.seconds), 8);
    out.write(reinterpret_cast<const char*>(&size), 4);
    out.write(reinterpret_cast<const char*>(s.data.data()), size);
}

// A .wav as mono 22050 Hz floats (what the note finder hears).
bool LoadWavMono22k(const std::filesystem::path& file, std::vector<float>& out, std::string& why) {
    SDL_AudioSpec spec = {};
    Uint8* buf = nullptr;
    Uint32 len = 0;
    if (SafeLoadWav(file.string(), &spec, &buf, &len) == nullptr) { why = SDL_GetError(); return false; }
    SDL_AudioCVT cvt;
    if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_F32SYS, 1, 22050) < 0) { SDL_FreeWAV(buf); why = SDL_GetError(); return false; }
    std::vector<Uint8> work(static_cast<size_t>(len) * static_cast<size_t>(cvt.len_mult > 0 ? cvt.len_mult : 1) + 16);
    std::memcpy(work.data(), buf, len);
    SDL_FreeWAV(buf);
    cvt.len = static_cast<int>(len);
    cvt.buf = work.data();
    if (cvt.needed && SDL_ConvertAudio(&cvt) < 0) { why = SDL_GetError(); return false; }
    const size_t bytes = cvt.needed ? static_cast<size_t>(cvt.len_cvt) : len;
    out.assign(bytes / sizeof(float), 0.0f);
    std::memcpy(out.data(), work.data(), out.size() * sizeof(float));
    return !out.empty();
}

// The worker: unpacks the samples once, then converts the songs one at a time, newest wishes first.
void OotWorker() {
    bool bankReady = false;
    for (;;) {
        std::shared_ptr<OotSong> song;
        {
            std::unique_lock<std::mutex> lock(gOot.mutex);
            gOot.wake.wait(lock, [] {
                if (gOot.quit) return true;
                if (!gOot.captured) return false;
                for (auto& s : gOot.songs) if (s->state == OotSong::Waiting) return true;
                return false;
            });
            if (gOot.quit) return;
            for (auto& s : gOot.songs) if (s->state == OotSong::Waiting) { song = s; break; }
            song->state = OotSong::Working;
        }
        auto say = [&](const std::string& text) { std::lock_guard<std::mutex> lock(gOot.mutex); gOot.status = text; };
        if (!bankReady) {
            say("Reading the game's instruments...");
            gOot.bank.samples.resize(gOot.raw.size());
            for (size_t i = 0; i < gOot.raw.size(); i++) {
                const OotRawSample& r = gOot.raw[i];
                royale::music::Sample& s = gOot.bank.samples[i];
                if (r.codec == CODEC_ADPCM || r.codec == CODEC_SMALL_ADPCM)
                    s.pcm = royale::music::DecodeVadpcm(r.data.data(), r.data.size(), r.book.data(), r.order, r.npred, r.codec == CODEC_SMALL_ADPCM);
                else { s.pcm.resize(r.data.size() / 2); std::memcpy(s.pcm.data(), r.data.data(), s.pcm.size() * 2); }
                if (r.loopStart >= 0 && r.loopStart < static_cast<int>(s.pcm.size())) { s.loopStart = r.loopStart; s.loopEnd = std::min(r.loopEnd, static_cast<int>(s.pcm.size())); }
                if (s.loopEnd <= s.loopStart + 8) s.loopStart = s.loopEnd = -1;
            }
            gOot.raw.clear();
            for (royale::music::Preset& p : gOot.bank.presets) royale::music::Analyze(gOot.bank, p);
            bankReady = true;
        }
        const std::string name = song->wav.stem().string();
        royale::seq::Sequence seq;
        std::string why;
        const std::filesystem::path cache = OotCachePath(song->wav);
        bool ok = ReadOotCache(cache, seq);
        if (!ok) {
            std::vector<float> y;
            if (!LoadWavMono22k(song->wav, y, why)) why = "could not read it (" + why + ")";
            else {
                bool quit = false;
                const royale::bp::Output o = royale::bp::Run(y.data(), y.size(), [&](float f) {
                    say("Turning " + name + " into OoT music: finding the notes " + std::to_string(static_cast<int>(f * 100)) + "%");
                    std::lock_guard<std::mutex> lock(gOot.mutex);
                    quit = gOot.quit;
                    return !quit;
                });
                if (quit) return;
                say("Turning " + name + " into OoT music: picking instruments");
                const std::vector<royale::music::DrumHit> hits = royale::music::TranscribeDrums(y.data(), y.size(), 22050);
                const std::vector<royale::music::Note> notes = royale::music::CleanNotes(royale::music::ToNotes(o.frames, o.onsets, 0.5, 0.3, 11, 11), hits);
                if (notes.empty()) why = "no notes were found in it";
                else {
                    const royale::music::Heard heard = royale::music::HearSong(y.data(), y.size(), 22050, notes, hits);
                    const royale::music::Arrangement a = royale::music::Arrange(gOot.bank, notes, hits, &heard, y.size() / 22050.0);
                    if (a.channels.empty()) why = "no instrument fits it";
                    else {
                        seq = royale::music::WriteArrangement(a);
                        ok = !seq.tooBig;
                        if (ok) WriteOotCache(cache, seq);
                        else why = "it is too long for the game";
                    }
                }
            }
        }
        std::lock_guard<std::mutex> lock(gOot.mutex);
        if (ok) { song->seq = std::move(seq); song->state = OotSong::Ready; gOot.status = name + " is ready to play with OoT instruments."; }
        else { song->state = OotSong::Failed; song->why = why; gOot.status = name + ": " + why + "; the original plays instead."; }
    }
}

// Queue every song in the folder (called after a scan). Starts the worker the first time.
void QueueOotSongs() {
    if (!OotInstrumentsOn()) return;
    CaptureOotFonts();
    std::lock_guard<std::mutex> lock(gOot.mutex);
    for (const auto& t : gLobbyMusic.tracks) {
        bool have = false;
        for (auto& s : gOot.songs) if (s->wav == t) { have = true; break; }
        if (!have) { auto s = std::make_shared<OotSong>(); s->wav = t; gOot.songs.push_back(s); }
    }
    if (!gOot.worker.joinable()) {
        gOot.worker = std::thread(OotWorker);
        std::atexit([] {
            { std::lock_guard<std::mutex> lock(gOot.mutex); gOot.quit = true; }
            gOot.wake.notify_all();
            if (gOot.worker.joinable()) gOot.worker.join();
        });
    }
    gOot.wake.notify_all();
}
std::shared_ptr<OotSong> ReadyOotSong(const std::filesystem::path& wav) {
    std::lock_guard<std::mutex> lock(gOot.mutex);
    for (auto& s : gOot.songs) if (s->wav == wav && s->state == OotSong::Ready) return s;
    return nullptr;
}

// Is our song still on the second music player? (The game may have stopped it, or put its own music there.)
bool OotSongPlaying() {
    if (!gOot.playing) return false;
    const SequencePlayer& sp = gAudioContext.seqPlayers[kOotSeqPlayer];
    return sp.enabled && sp.seqData == gOot.playing->seq.data.data();
}
void StopOotSong() {
    if (OotSongPlaying()) AudioSeq_SequencePlayerDisable(&gAudioContext.seqPlayers[kOotSeqPlayer]);
    gOot.playing = nullptr;
}
// Start a converted song on the game's second music player, the way the game starts its own (audio_load.c,
// AudioLoad_SyncInitSeqPlayerInternal), with no font list: the song names each soundfont itself. Runs on the game thread,
// between the sound engine's updates.
bool StartOotSong(const std::shared_ptr<OotSong>& song) {
    StopOotSong();
    for (uint8_t f : song->seq.fonts) if (!fontMap[f] || !AudioLoad_SyncLoadFont(f)) return false;
    SequencePlayer* sp = &gAudioContext.seqPlayers[kOotSeqPlayer];
    AudioSeq_SequencePlayerDisable(sp);
    AudioSeq_ResetSequencePlayer(sp);
    sp->seqId = NA_BGM_STAFF_4;
    sp->defaultFont = 0xFF;
    sp->seqData = song->seq.data.data();
    sp->scriptState.pc = sp->seqData;
    sp->scriptState.depth = 0;
    sp->delay = 0;
    sp->finished = 0;
    sp->playerIdx = kOotSeqPlayer;
    sp->enabled = 1;
    gOot.playing = song;
    gOot.lastVolume = -1;
    return true;
}
// The song follows the player's main music volume.
void KeepOotSongVolume() {
    if (!OotSongPlaying()) return;
    const float v = static_cast<float>(CVarGetInteger(CVAR_SETTING("Volume.MainMusic"), 100)) / 100.0f;
    if (v != gOot.lastVolume) { Audio_SetGameVolume(kOotSeqPlayer, v); gOot.lastVolume = v; }
}
std::string OotStatus() {
    std::lock_guard<std::mutex> lock(gOot.mutex);
    int ready = 0;
    for (auto& s : gOot.songs) ready += s->state == OotSong::Ready;
    return std::to_string(ready) + " of " + std::to_string(gOot.songs.size()) + " songs ready with OoT instruments. " + gOot.status;
}

// The music folder's songs play in the lobby (if that option is on) and, when "Match music" is set to Random, through the match too.
void UpdateLobbyMusic(bool inLobby, bool inMatchRandom = false) {
    LobbyMusic& m = gLobbyMusic;
    const bool want = ((inLobby && MapOption("LobbyMusic", true)) || inMatchRandom) && !m.failed;
    if (!want) {
        if (m.playing) StopVoice(kVoiceSong);
        m.playing = false;
        StopOotSong();
        if (!inLobby && !inMatchRandom) m.scanned = false; // pick up newly added songs next time
        return;
    }
    if (!m.scanned) ScanMusicFolder();
    if (m.tracks.empty()) return;
    // A song already turned into OoT music plays on the game's own sound engine; the others play as they are until they are ready.
    if (OotSongPlaying()) { if (OotInstrumentsOn()) { KeepOotSongVolume(); return; } StopOotSong(); }
    gOot.playing = nullptr;
    const bool done = VoiceDone(kVoiceSong);
    if (OotInstrumentsOn() && done) {
        for (size_t i = 0; i < m.tracks.size(); i++) {
            const size_t k = (m.next + i) % m.tracks.size();
            std::shared_ptr<OotSong> song = ReadyOotSong(m.tracks[k]);
            if (!song || !StartOotSong(song)) continue;
            m.next = k + 1;
            StopVoice(kVoiceSong);
            m.nowPlaying = m.tracks[k].stem().string();
            m.playing = true;
            KeepOotSongVolume();
            Say((inLobby ? "Lobby music: " : "Now playing: ") + m.nowPlaying + " (OoT instruments)");
            return;
        }
    }
    if (done) {
        if (!LoadNextTrack()) {
            m.failed = true;   // the game's own music comes back (DriveMatchMusic), and the menu says why
            if (m.status.rfind("Could not read", 0) != 0) m.status = "None of the songs in " + MusicFolder().string() + " could be played.";
            Say("Music folder: " + m.status);
            return;
        }
        StartVoice(kVoiceSong, std::make_shared<const std::vector<int16_t>>(std::move(m.pcm)), true, 44100, false, GameVolume(true));
        m.pcm.clear();
        Say((inLobby ? "Lobby music: " : "Now playing: ") + m.nowPlaying);
    } else {
        SetVoiceVolume(kVoiceSong, GameVolume(true));
    }
    m.playing = true;
}

// What the local player has just done with an item, held for a moment so that everyone sees the pose (potion, ocarina, bow, swing...).
int gActionFrames = 0;
royale::Anim gActionAnim = royale::Anim::Idle;
double gActionStart = 0;
royale::ItemId gActionItem = royale::ItemId::Count;   // the bottle being drunk or the ability being used: what Link holds for it (LocalLink_Draw)
void StartAction(royale::Anim pose, float seconds, royale::ItemId item = royale::ItemId::Count) {
    gActionAnim = pose;
    gActionFrames = std::max(1, static_cast<int>(seconds * royale::kTickHz));
    gActionStart = ImGui::GetTime();
    gActionItem = item;
}
royale::Anim PoseForWeapon(royale::ItemId weapon) {
    const royale::WeaponStats w = royale::WeaponOf(weapon);
    if (!w.ranged) return royale::Anim::Attack;
    const royale::AmmoKind a = royale::AmmoUsedBy(weapon);
    return (a == royale::AmmoKind::Arrows || a == royale::AmmoKind::Seeds) ? royale::Anim::Shoot : royale::Anim::Throw;
}

// Whether Link is playing a particular animation of the game's. The animation names are compared, not their addresses: every source file
// has its own copy of each name.
bool AnimIs(const void* playing, const char* name) {
    if (playing == nullptr) return false;
    const char* p = static_cast<const char*>(playing);
    return std::strncmp(p, "__OTR__", 7) == 0 && std::strcmp(p, name) == 0;
}

// What everybody else should see you doing. Your own Link runs the real game, so its moves are read straight off it: the Z-target side hops,
// back flips, jump slashes and spin attacks, the shield, the sword, jumps and the lock-on footwork.
uint8_t ClassifyAnim(Player* player) {
    using royale::Anim;
    static float lastX = 0, lastZ = 0;
    const float dx = player->actor.world.pos.x - lastX, dz = player->actor.world.pos.z - lastZ;
    lastX = player->actor.world.pos.x; lastZ = player->actor.world.pos.z;
    if (gEmote.id >= 0) return royale::EmoteAnim(gEmote.id); // others see the gesture
    if (player->stateFlags1 & PLAYER_STATE1_DEAD) return static_cast<uint8_t>(Anim::Dead);
    const void* a = player->skelAnime.animation;
    auto is = [&](const char* name) { return AnimIs(a, name); };
    if (is(gPlayerAnim_link_fighter_Lside_jump) || is(gPlayerAnim_link_fighter_Lside_jump_end)) return static_cast<uint8_t>(Anim::HopL);
    if (is(gPlayerAnim_link_fighter_Rside_jump) || is(gPlayerAnim_link_fighter_Rside_jump_end)) return static_cast<uint8_t>(Anim::HopR);
    if (is(gPlayerAnim_link_fighter_backturn_jump) || is(gPlayerAnim_link_fighter_backturn_jump_end)) return static_cast<uint8_t>(Anim::Backflip);
    if (is(gPlayerAnim_link_fighter_Lpower_jump_kiru) || is(gPlayerAnim_link_fighter_Lpower_jump_kiru_hit) || is(gPlayerAnim_link_fighter_jump_rollkiru) ||
        is(gPlayerAnim_link_fighter_jump_kiru_finsh))
        return static_cast<uint8_t>(Anim::JumpSlash);
    if (is(gPlayerAnim_link_fighter_rolling_kiru) || is(gPlayerAnim_link_fighter_Lrolling_kiru) || is(gPlayerAnim_link_fighter_Wrolling_kiru))
        return static_cast<uint8_t>(Anim::SpinAttack);
    if (gActionFrames > 0) return static_cast<uint8_t>(gActionAnim);
    if (player->stateFlags1 & PLAYER_STATE1_SHIELDING) return static_cast<uint8_t>(Anim::Guard);
    if (player->meleeWeaponState != 0) return static_cast<uint8_t>(Anim::Attack);   // swinging the real sword
    const bool ground = (player->actor.bgCheckFlags & 1) != 0;
    if (!ground && !gSkydiving && !(player->stateFlags1 & (PLAYER_STATE1_HANGING_OFF_LEDGE | PLAYER_STATE1_CLIMBING_LADDER | PLAYER_STATE1_IN_WATER | PLAYER_STATE1_CLIMBING_LEDGE)))
        return static_cast<uint8_t>(Anim::Jump);
    const float v = std::fabs(player->linearVelocity);
    const float runTop = gSprinting ? 7.5f * kSprintMult : 7.5f;
    if (is(gPlayerAnim_link_normal_landing_roll) || is(gPlayerAnim_link_normal_landing_roll_free) || (v > runTop && ground)) return static_cast<uint8_t>(Anim::Roll);   // a roll is faster than any run
    if (player->stateFlags1 & (PLAYER_STATE1_HOSTILE_LOCK_ON | PLAYER_STATE1_PARALLEL)) {
        // Z-targeting: the footwork depends on which way you move relative to where you face.
        if (dx * dx + dz * dz < 0.25f) return static_cast<uint8_t>(Anim::Stance);
        const float yaw = player->actor.shape.rot.y * (3.14159265f / 32768.0f);
        const float fwd = dx * std::sin(yaw) + dz * std::cos(yaw), right = -dx * std::cos(yaw) + dz * std::sin(yaw);
        if (std::fabs(fwd) >= std::fabs(right)) return static_cast<uint8_t>(fwd > 0 ? Anim::Run : Anim::Back);
        return static_cast<uint8_t>(right > 0 ? Anim::SideR : Anim::SideL);
    }
    if (v < 0.5f) return static_cast<uint8_t>(Anim::Idle);
    if (gSprinting && v >= 4.0f) return static_cast<uint8_t>(Anim::Sprint);
    return static_cast<uint8_t>(v < 4.0f ? Anim::Walk : Anim::Run);
}

uint8_t gLastEpoch = 0;
royale::MatchState gLastState = royale::MatchState::Lobby;
bool gWasJoined = false;
int gLastCountdownShown = -1;
int gAttackCooldown = 0;     // game frames until B may attack again
bool gPendingStart = false;  // the host pressed Start; waiting to be in Hyrule Field, measure the map, then begin
bool gSoloStartWanted = false;   // "Fortnite Map solo test" was pressed: start the match as soon as the host is in its own lobby
int gInFieldFrames = 0;      // frames spent in the field without a transition, so the scene's collision is ready
bool gSpectating = false;

// While a match is live the server's health replaces the save's. Keep the real values so leaving a match, or the match
// ending, doesn't leave the player's save file with 3 hearts.
bool gHealthOverridden = false;
s16 gSavedCapacity = 0, gSavedHealth = 0;
s16 gMatchHealth = 16;   // what the save's health should read this frame while the server owns it (never 0, see OnPlayerUpdate)
u8 gMatchSeqId = 0xFF, gMatchAmbienceId = 0xFF;   // the scene's music, to put back if the game's own death ever starts (see CancelGameDeath)

bool IsLive(const royale::HudState& h) {
    return h.state == royale::MatchState::Drop || h.state == royale::MatchState::InMatch;
}

void RestoreHealth() {
    if (!gHealthOverridden) return;
    gHealthOverridden = false;
    gSaveContext.healthCapacity = gSavedCapacity;
    // Come back alive: an eliminated player left health at 0.
    gSaveContext.health = gSavedHealth > 0 ? gSavedHealth : gSavedCapacity;
}

void Say(const std::string& text) {
    Notification::Emit({ .message = text });
}

// B attacks with the weapon the server says you hold; D-pad Down drinks a potion. Damage, range, cooldown and healing are
// decided by the server, so this only chooses a target: the nearest living player in front of Link and within weapon range.
int gCurrentPoi = -1;        // which point of interest the player is standing in, -1 for none

// "Entering ..." when you walk into a town.
void NoticePoi(Player* player, const royale::HudState& hud) {
    if (!gSession.Client() || !InField() || !(hud.state == royale::MatchState::Drop || hud.state == royale::MatchState::InMatch) || gSkydiving) { gCurrentPoi = -1; return; }
    int now = -1;
    for (const royale::Poi& p : gSession.Client()->Pois()) {
        const float dx = p.center.x - player->actor.world.pos.x, dz = p.center.z - player->actor.world.pos.z;
        if (dx * dx + dz * dz < p.radius * p.radius) { now = p.name; break; }
    }
    if (now != gCurrentPoi && now >= 0) Say(std::string("Entering ") + royale::kPoiNames[now]);
    gCurrentPoi = now;
}

int gNextWeaponSlot = 1;
int gLastWeaponSlot = 0;     // the reserve slot last swapped in (D-pad Left swaps it back)
int gJumpAssistFrames = 0;   // frames left in which a jump pulls you onto a ledge in front of you     // which backup slot D-pad Left swaps in next

// A swing that finds no player cuts the bush or breaks the rock in front of Link (a boulder needs something heavy or explosive).
// ---- angry villagers -------------------------------------------------------------------------------------------------------
// The scene's own people (the carpenters, say) stay in a match. Hit one by accident and they turn on you: they run at you and swing, and the server
// takes a little health for each blow. Leave them alone for a while (or get away from them) and they calm down and walk back to where they were.
struct AngryNpc {
    ActorFunc origUpdate = nullptr;
    ActorFunc origDestroy = nullptr;
    Vec3f home = { 0, 0, 0 };
    float timer = 0;      // seconds of anger left
    float cooldown = 0;   // until the next swing
    bool returning = false;
};
std::unordered_map<Actor*, AngryNpc> gAngry;
bool gAngryPlayerAlive = true;
constexpr float kNpcDt = 1.0f / 20.0f;          // the game runs its actors at 20 frames a second
constexpr float kNpcAngerSeconds = 25.0f;
constexpr float kNpcRunSpeed = 4.2f;            // units a frame: a little slower than Link running, so you can get away
constexpr float kNpcForgetDistance = 600.0f;    // out of sight, out of mind: anger runs down twice as fast beyond this

bool CanAnger(Actor* a) {
    if (a == nullptr || a->update == nullptr || a->category != ACTORCAT_NPC) return false;
    if (a->id == ACTOR_PLAYER || a->id == ACTOR_EN_OE2 || a->id == ACTOR_EN_ISHI) return false;   // players' puppets and our own stand-ins
    return gPuppetOf.find(a) == gPuppetOf.end();
}

void AngryNpc_Destroy(Actor* actor, PlayState* play) {
    auto it = gAngry.find(actor);
    ActorFunc orig = nullptr;
    if (it != gAngry.end()) { orig = it->second.origDestroy; gAngry.erase(it); }
    if (orig) orig(actor, play);
}

bool NpcStepTo(Actor* actor, PlayState* play, float tx, float tz, float speed) {
    const float dx = tx - actor->world.pos.x, dz = tz - actor->world.pos.z;
    const float d = std::hypot(dx, dz);
    const s16 yaw = static_cast<s16>(std::atan2(dx, dz) * (32768.0f / 3.14159265f));
    actor->world.rot.y = actor->shape.rot.y = yaw;
    if (d < 1.0f) return true;
    const float step = std::min(speed, d);
    Vec3f from = { actor->world.pos.x, actor->world.pos.y + 30.0f, actor->world.pos.z };
    Vec3f to = { actor->world.pos.x + dx / d * (step + 14.0f), actor->world.pos.y + 30.0f, actor->world.pos.z + dz / d * (step + 14.0f) };
    Vec3f hit;
    CollisionPoly* poly = nullptr;
    s32 bgId = 0;
    if (BgCheck_EntityLineTest1(&play->colCtx, &from, &to, &hit, &poly, true, false, false, true, &bgId)) return false;   // a wall: stay put
    actor->world.pos.x += dx / d * step;
    actor->world.pos.z += dz / d * step;
    actor->world.pos.y = GroundY(play, actor->world.pos.x, actor->world.pos.z, actor->world.pos.y);
    return false;
}

void AngryNpc_Update(Actor* actor, PlayState* play) {
    auto it = gAngry.find(actor);
    if (it == gAngry.end()) return;
    AngryNpc& a = it->second;
    if (a.origUpdate) a.origUpdate(actor, play);   // the villager's own animation keeps running; where it stands and faces is ours from here
    Player* player = GET_PLAYER(play);
    if (player == nullptr || !InField()) return;
    const float dx = player->actor.world.pos.x - actor->world.pos.x, dz = player->actor.world.pos.z - actor->world.pos.z;
    const float dist = std::hypot(dx, dz);
    a.cooldown = std::max(0.0f, a.cooldown - kNpcDt);
    if (!a.returning) {
        a.timer -= kNpcDt * (dist > kNpcForgetDistance || !gAngryPlayerAlive ? 2.0f : 1.0f);
        if (a.timer <= 0.0f) a.returning = true;
    }
    if (a.returning) {
        if (NpcStepTo(actor, play, a.home.x, a.home.z, kNpcRunSpeed * 0.6f) || std::hypot(a.home.x - actor->world.pos.x, a.home.z - actor->world.pos.z) < 20.0f) {
            actor->update = a.origUpdate;   // calm again: the game's own behaviour takes back over
            actor->destroy = a.origDestroy;
            gAngry.erase(it);
        }
        return;
    }
    if (dist > 60.0f) NpcStepTo(actor, play, player->actor.world.pos.x, player->actor.world.pos.z, kNpcRunSpeed);
    else actor->world.rot.y = actor->shape.rot.y = static_cast<s16>(std::atan2(dx, dz) * (32768.0f / 3.14159265f));
    if (dist <= 85.0f && a.cooldown <= 0.0f && gAngryPlayerAlive && std::fabs(player->actor.world.pos.y - actor->world.pos.y) < 70.0f) {
        a.cooldown = 1.0f;
        a.timer = std::max(a.timer, kNpcAngerSeconds * 0.6f);   // it is busy hitting you: it stays angry while it does
        gSession.ReportNpcHit(0.5f);
        Audio_PlaySoundGeneral(NA_SE_IT_HAMMER_HIT, &actor->projectedPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        Player_PlaySfx(&player->actor, NA_SE_VO_LI_DAMAGE_S + player->ageProperties->unk_92);
    }
}

void AngerNpc(Actor* actor) {
    auto it = gAngry.find(actor);
    if (it != gAngry.end()) { it->second.timer = kNpcAngerSeconds; it->second.returning = false; return; }
    AngryNpc a;
    a.origUpdate = actor->update;
    a.origDestroy = actor->destroy;
    a.home = actor->world.pos;
    a.timer = kNpcAngerSeconds;
    gAngry[actor] = a;
    actor->update = AngryNpc_Update;
    actor->destroy = AngryNpc_Destroy;
    Audio_PlaySoundGeneral(NA_SE_EN_STAL_WARAU, &actor->projectedPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

// A swing, spin or shot that found no player: did it hit a villager? (A bad aim at a carpenter makes an enemy of them.)
void HitNpcInFront(Player* player, const royale::WeaponStats& w) {
    if (!InField() || !gSession.Joined()) return;
    const float reach = w.ranged ? std::clamp(w.range, 150.0f, 500.0f) : std::clamp(w.range, 90.0f, 170.0f);
    const int cone = w.ranged ? 0x1000 : 0x2800;
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_NPC].head; a != nullptr; a = a->next) {
        if (!CanAnger(a) && gAngry.find(a) == gAngry.end()) continue;
        const float dx = a->world.pos.x - player->actor.world.pos.x, dz = a->world.pos.z - player->actor.world.pos.z;
        const float d = std::hypot(dx, dz);
        if (d > reach + 25.0f || std::fabs(a->world.pos.y - player->actor.world.pos.y) > 90.0f) continue;
        const s16 toTarget = static_cast<s16>(std::atan2(dx, dz) * (32768.0f / 3.14159265f));
        if (std::abs(static_cast<int>(static_cast<s16>(toTarget - player->actor.shape.rot.y))) > cone && d > 40.0f) continue;
        AngerNpc(a);
    }
}

void SmashPropInFront(Player* player, const royale::WeaponStats& w) {
    if (!gSession.Client()) return;
    const auto& props = gSession.Client()->Props();
    size_t best = props.size();
    float bestDist = 1e9f;
    for (const auto& [i, pa] : gProps) {
        if (i >= props.size() || !pa.actor) continue;
        const royale::PropKind kind = props[i].kind;
        if (kind != royale::PropKind::Rock && kind != royale::PropKind::Boulder && kind != royale::PropKind::Bush) continue;
        if (kind == royale::PropKind::Boulder && w.damage < 1.5f && w.splashRadius <= 0) continue;
        const float dx = props[i].pos.x - player->actor.world.pos.x, dz = props[i].pos.z - player->actor.world.pos.z;
        const float reach = royale::PropRadius(kind) + std::clamp(w.range, 90.0f, 170.0f);
        const float d = std::sqrt(dx * dx + dz * dz);
        if (d > reach) continue;
        const s16 toTarget = static_cast<s16>(std::atan2(dx, dz) * (32768.0f / 3.14159265f));
        if (std::abs(static_cast<int>(static_cast<s16>(toTarget - player->actor.shape.rot.y))) > 0x2800 && d > 50.0f) continue;
        if (d < bestDist) { bestDist = d; best = i; }
    }
    if (best >= props.size()) return;
    auto it = gProps.find(best);
    if (it == gProps.end()) return;
    float y = player->actor.world.pos.y;
    FloorAt(props[best].pos.x, props[best].pos.z, &y);
    SparkBurst(gPlayState, props[best].pos.x, y + 20.0f, props[best].pos.z, props[best].kind == royale::PropKind::Bush ? Color_RGBA8{ 90, 220, 90, 255 } : Color_RGBA8{ 190, 190, 180, 255 }, 8, 3.0f);
    Vec3f at = { props[best].pos.x, y + 20.0f, props[best].pos.z };
    Audio_PlaySoundGeneral(props[best].kind == royale::PropKind::Bush ? NA_SE_EV_PLANT_BROKEN : NA_SE_EV_ROCK_BROKEN, &at, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    Actor_Kill(it->second.actor);   // Prop_Destroy tells the server it was broken
}

// Hold the player on top of a climbing block (landing on it, walking along it) and out of its sides. Runs every frame in the player's update, after
// the game has settled Link on the scene's own floor.
// Drinking: which bottle Link takes out (the potion the server will most likely pick: the first that heals, or the first shield potion),
// how long it takes, and its sounds (Link's gulp and the hearts refilling).
constexpr float kDrinkSeconds = 1.5f;
royale::ItemId BottleToDrink(const royale::HudState& hud, bool shield) {
    for (const auto& pot : hud.inv.potions) {
        const royale::ItemId id = static_cast<royale::ItemId>(pot.item);
        const royale::PotionDef d = royale::PotionOf(id);
        if (shield ? d.shield > 0 : (!d.revive && d.heal > 0)) return id;
    }
    return shield ? royale::ItemId::BluePotion : royale::ItemId::RedPotion;
}
void DrinkSounds(Player* player) {
    Player_PlaySfx(&player->actor, static_cast<u16>(NA_SE_VO_LI_DRINK + player->ageProperties->unk_92));
    Audio_PlaySoundGeneral(NA_SE_SY_HP_RECOVER, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

// ---- solid scenery ------------------------------------------------------------------------------------------------------------
// The climbing blocks, rocks, boulders and standing stones are real ground: the game's own collision, the kind a moving platform or a chest
// has. One actor of ours holds a collision mesh built from the scenery near you (a box for each block, a stone shape for the rest), rebuilt
// as you move. Link's own movement then does the rest: he lands on them, walks and rolls across them, hops up small steps, grabs and climbs
// ledges, and stops at their sides, exactly as on the scene's own floor. The game only has room for so much of this kind of collision
// (shared with chests, gates and moving platforms), so only the nearest pieces are in it at a time; the far ones don't matter to you.
constexpr int kSolidMaxVtx = 420;
constexpr int kSolidMaxPoly = 420;
constexpr float kSolidRadius = 1500.0f;   // scenery within this of you is solid
Vec3s gSolidVtx[kSolidMaxVtx];
CollisionPoly gSolidPoly[kSolidMaxPoly];
SurfaceType gSolidSurfaces[2] = {
    { { 0, 2 } },                // stone underfoot
    { { 0, 2 | (1u << 17) } },   // stone the hookshot bites into (the blocks)
};
CollisionHeader gSolidHeader;
Actor* gSolidActor = nullptr;
bool gSolidFailed = false;            // the game had no free collision slot: fall back to ApplyPlatforms / ApplyRocks
std::vector<size_t> gSolidSet;         // the props in the mesh now
royale::Vec2 gSolidCentre = { 1e9f, 1e9f };
int gSolidAge = 0;

bool SolidActive() { return gSolidActor != nullptr && gSolidBgId >= 0; }

struct SolidBuilder {
    int nv = 0, np = 0;
    bool Room(int v, int p, int maxPoly) const { return nv + v <= kSolidMaxVtx && np + p <= maxPoly; }
    int V(float x, float y, float z) {
        gSolidVtx[nv] = { static_cast<s16>(std::lround(std::clamp(x, -32000.0f, 32000.0f))), static_cast<s16>(std::lround(std::clamp(y, -32000.0f, 32000.0f))),
                          static_cast<s16>(std::lround(std::clamp(z, -32000.0f, 32000.0f))) };
        return nv++;
    }
    // A triangle facing away from `inside` (the game works out the normal from the winding, so the winding is fixed to point outwards).
    void T(int a, int b, int c, const Vec3f& inside, u16 type, u16 xp) {
        const Vec3s &A = gSolidVtx[a], &B = gSolidVtx[b], &C = gSolidVtx[c];
        const float ux = B.x - A.x, uy = B.y - A.y, uz = B.z - A.z, vx = C.x - A.x, vy = C.y - A.y, vz = C.z - A.z;
        const float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        const float cx = (A.x + B.x + C.x) / 3.0f - inside.x, cy = (A.y + B.y + C.y) / 3.0f - inside.y, cz = (A.z + B.z + C.z) / 3.0f - inside.z;
        if (nx * cx + ny * cy + nz * cz < 0) std::swap(b, c);
        CollisionPoly& p = gSolidPoly[np++];
        p = {};
        p.type = type;
        p.flags_vIA = static_cast<u16>(a | (xp << 13));
        p.flags_vIB = static_cast<u16>(b);
        p.vIC = static_cast<u16>(c);
        p.normal = { 0, 0x7FFF, 0 };   // filled in properly by the game when it takes the mesh
    }
    // A stone: a ring of `sides` points round the foot, a smaller ring at the top, and a flat top to stand on. A box is the same with four sides.
    void Prism(float x, float z, float baseY, float topY, float footR, float topR, int sides, float turn, u16 type, u16 xp) {
        const int foot = nv;
        for (int i = 0; i < sides; i++) { const float a = turn + 6.2831853f * i / sides; V(x + std::sin(a) * footR, baseY, z + std::cos(a) * footR); }
        const int top = nv;
        for (int i = 0; i < sides; i++) { const float a = turn + 6.2831853f * i / sides; V(x + std::sin(a) * topR, topY, z + std::cos(a) * topR); }
        const Vec3f inside = { x, (baseY + topY) * 0.5f, z };
        for (int i = 0; i < sides; i++) {
            const int j = (i + 1) % sides;
            T(foot + i, foot + j, top + j, inside, type, xp);
            T(foot + i, top + j, top + i, inside, type, xp);
        }
        const Vec3f below = { x, topY - 50.0f, z };
        for (int i = 1; i + 1 < sides; i++) T(top, top + i, top + i + 1, below, type, xp);
    }
};
constexpr int PrismVtx(int sides) { return sides * 2; }
constexpr int PrismPoly(int sides) { return sides * 2 + sides - 2; }

int SolidSides(royale::PropKind k) { return royale::IsPlatform(k) ? 4 : k == royale::PropKind::Rock ? 6 : 8; }
bool IsSolidKind(royale::PropKind k) { return royale::IsPlatform(k) || k == royale::PropKind::Rock || k == royale::PropKind::Boulder || k == royale::PropKind::Pillar; }

// How many triangles the game still has room for, after everything else with this kind of collision (chests, gates, platforms of the scene).
int SolidPolyBudget() {
    const DynaCollisionContext& dyna = gPlayState->colCtx.dyna;
    int used = 0, usedVtx = 0;
    for (int i = 0; i < BG_ACTOR_MAX; i++) {
        if (i == gSolidBgId || !(dyna.bgActorFlags[i] & 1) || dyna.bgActors[i].colHeader == nullptr) continue;
        used += dyna.bgActors[i].colHeader->numPolygons;
        usedVtx += dyna.bgActors[i].colHeader->numVertices;
    }
    const int room = std::min({ dyna.polyListMax - used, dyna.vtxListMax - usedVtx, dyna.polyNodesMax - used }) - 64;   // a margin for things spawned this frame
    return std::clamp(room, 0, kSolidMaxPoly);
}

// Fill the mesh with the scenery nearest (x, z). Returns false if the same pieces are already in it.
bool BuildSolidMesh(float x, float z, bool force) {
    const auto& props = gSession.Client()->Props();
    std::vector<std::pair<float, size_t>> near;
    for (size_t i = 0; i < props.size(); i++) {
        const royale::Prop& p = props[i];
        if (!IsSolidKind(p.kind) || gBrokenProps.count(i)) continue;
        const float d = std::hypot(p.pos.x - x, p.pos.z - z);
        if (d > kSolidRadius) continue;
        auto pa = gProps.find(i);   // a rock someone has picked up and carried off is not where its footprint is any more
        if (pa != gProps.end() && pa->second.actor != nullptr && std::hypot(pa->second.actor->world.pos.x - p.pos.x, pa->second.actor->world.pos.z - p.pos.z) > 30.0f) continue;
        near.push_back({ d, i });
    }
    std::sort(near.begin(), near.end());
    const int budget = SolidPolyBudget();
    std::vector<size_t> chosen;
    int polys = 1, vtx = 3;   // the placeholder below
    for (const auto& [d, i] : near) {
        const int sides = SolidSides(props[i].kind);
        if (polys + PrismPoly(sides) > budget || vtx + PrismVtx(sides) > kSolidMaxVtx) break;
        if (PlatformBase(i) < -1.0e8f) continue;   // its ground isn't loaded yet
        polys += PrismPoly(sides);
        vtx += PrismVtx(sides);
        chosen.push_back(i);
    }
    std::sort(chosen.begin(), chosen.end());
    if (!force && chosen == gSolidSet) return false;
    gSolidSet = chosen;

    SolidBuilder b;
    // Always one triangle, so the mesh is never empty (the game divides by its vertex count): a sliver of wall far below the map, out of everyone's way.
    const float lowY = -31000.0f;
    const int a = b.V(x, lowY, z), c = b.V(x + 1.0f, lowY, z), e = b.V(x, lowY + 1.0f, z);
    b.T(a, c, e, { x, lowY, z + 1.0f }, 0, 1);
    for (size_t i : chosen) {
        const royale::Prop& p = props[i];
        const float base = PlatformBase(i);
        const int sides = SolidSides(p.kind);
        if (royale::IsPlatform(p.kind)) {   // a box, square to the map, sunk a little into the ground so a slope never leaves a gap under it
            const float h = royale::kPlatformHalf * 1.41421356f;
            b.Prism(p.pos.x, p.pos.z, base - 40.0f, base + royale::PlatformHeight(p.kind), h, h, 4, 0.78539816f, 1, 0);
        } else if (p.kind == royale::PropKind::Pillar) {
            b.Prism(p.pos.x, p.pos.z, base - 30.0f, base + 200.0f, royale::PropRadius(p.kind), royale::PropRadius(p.kind) * 0.85f, sides, 0.0f, 0, 1);
        } else {   // rocks and boulders: wide at the foot, a flat crown at the height you stand on (BoulderTop)
            const bool boulder = p.kind == royale::PropKind::Boulder;
            const float r = royale::PropRadius(p.kind) * (boulder ? royale::BoulderScale(p.rot) : 1.0f);
            const float top = boulder ? royale::BoulderTop(royale::BoulderShape(p.rot)) * royale::BoulderScale(p.rot) : 24.0f;
            b.Prism(p.pos.x, p.pos.z, base - 30.0f, base + top, r * 0.95f, r * 0.62f, sides, p.rot * (3.14159265f / 32768.0f), 0, 1);   // the camera passes through stones
        }
    }
    gSolidHeader = {};
    gSolidHeader.numVertices = static_cast<u16>(b.nv);
    gSolidHeader.vtxList = gSolidVtx;
    gSolidHeader.numPolygons = static_cast<u16>(b.np);
    gSolidHeader.polyList = gSolidPoly;
    gSolidHeader.surfaceTypeList = gSolidSurfaces;
    Vec3s lo = gSolidVtx[0], hi = gSolidVtx[0];
    for (int i = 1; i < b.nv; i++) {
        lo.x = std::min(lo.x, gSolidVtx[i].x); lo.y = std::min(lo.y, gSolidVtx[i].y); lo.z = std::min(lo.z, gSolidVtx[i].z);
        hi.x = std::max(hi.x, gSolidVtx[i].x); hi.y = std::max(hi.y, gSolidVtx[i].y); hi.z = std::max(hi.z, gSolidVtx[i].z);
    }
    gSolidHeader.minBounds = lo;
    gSolidHeader.maxBounds = hi;
    return true;
}

void Solid_Init(Actor* actor, PlayState* play) {
    Actor_SetScale(actor, 1.0f);
    actor->world.pos = actor->home.pos = { 0, 0, 0 };
    actor->shape.rot = actor->world.rot = { 0, 0, 0 };
    actor->shape.yOffset = 0.0f;
    DynaPolyActor* dyna = reinterpret_cast<DynaPolyActor*>(actor);
    DynaPolyActor_Init(dyna, 0);
    const Player* player = GET_PLAYER(play);
    BuildSolidMesh(player ? player->actor.world.pos.x : 0.0f, player ? player->actor.world.pos.z : 0.0f, true);
    dyna->bgId = DynaPoly_SetBgActor(play, &play->colCtx.dyna, actor, &gSolidHeader);
    if (dyna->bgId == BG_ACTOR_MAX) { gSolidFailed = true; gSolidBgId = -1; Actor_Kill(actor); return; }
    gSolidBgId = dyna->bgId;
}

void Solid_Destroy(Actor* actor, PlayState* play) {
    DynaPolyActor* dyna = reinterpret_cast<DynaPolyActor*>(actor);
    if (dyna->bgId >= 0 && dyna->bgId < BG_ACTOR_MAX) DynaPoly_DeleteBgActor(play, &play->colCtx.dyna, dyna->bgId);
    if (gSolidActor == actor) { gSolidActor = nullptr; gSolidBgId = -1; }
    gSolidSet.clear();
}

// The mesh only changes here, in the actor's own update: the game takes it in right after every actor has updated, in the same frame.
void Solid_Update(Actor* actor, PlayState* play) {
    if (!gSession.Client() || !InField()) return;
    const Player* player = GET_PLAYER(play);
    if (player == nullptr) return;
    const float x = player->actor.world.pos.x, z = player->actor.world.pos.z;
    if (++gSolidAge < 12 && std::hypot(x - gSolidCentre.x, z - gSolidCentre.z) < 150.0f) return;   // a few times a second, or sooner on the move
    gSolidAge = 0;
    gSolidCentre = { x, z };
    if (BuildSolidMesh(x, z, false)) play->colCtx.dyna.bitFlag |= DYNAPOLY_INVALIDATE_LOOKUP;
}

int SolidActorId() {
    static int id = -1;
    if (id < 0) {
        ActorDBInit init;
        init.name = "Royale_Solid";
        init.desc = "Battle royale scenery collision";
        init.category = ACTORCAT_BG;
        init.flags = ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
        init.objectId = OBJECT_GAMEPLAY_KEEP;
        init.instanceSize = sizeof(DynaPolyActor);
        init.init = Solid_Init;
        init.destroy = Solid_Destroy;
        init.update = Solid_Update;
        id = ActorDB::Instance->AddEntry(init).entry.id;
    }
    return id;
}

// Called every frame: the collision actor exists while you are in a match's field, and only then.
void EnsureSolidScenery() {
    const bool want = gSession.Client() != nullptr && InField() && gInFieldFrames > 20 && !gSession.Client()->Props().empty();
    if (want && gSolidActor == nullptr && !gSolidFailed) {
        gSolidCentre = { 1e9f, 1e9f };
        gSolidActor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, static_cast<s16>(SolidActorId()), 0, 0, 0, 0, 0, 0, 0, false);
        if (gSolidActor != nullptr && gSolidBgId < 0) gSolidActor = nullptr;   // it found no slot and removed itself
    } else if (!want && gSolidActor != nullptr) {
        Actor_Kill(gSolidActor);
        gSolidActor = nullptr;
        gSolidBgId = -1;
    }
}

// ---- the Fortnite map ---------------------------------------------------------------------------------------------------------
// The island (shared/fortnite_map.h) is played inside Hyrule Field's scene. patches/0013 lets us do three things there:
//   * swap the scene's collision for the island's triangles when the scene loads (Royale_CustomCollision, at the end of this file), so everything
//     the game does with the ground (walking, rolling, arrows, bombs, the camera, chests, spawns, the skydive's landing) works as on any map;
//   * not draw the field's rooms (Royale_HideRooms), and kill the scene's own actors (grass, rocks, trees: they stand on the old field's ground);
//   * and this actor draws the island in their place, with the texture baked into vertex colours (the game draws our meshes that way).
// Water is one big water box at the water level: lakes, rivers and the sea are the ground below it, and Link swims there.
struct FortniteGpu {
    static constexpr int kChunk = 4, kChunks = royale::fortnite::kCells / kChunk;   // blocks per chunk side; chunks per map side
    std::vector<Vtx> vtx[royale::fortnite::kLods];                            // [lod]: every block's vertices at that level of detail
    std::vector<Gfx> dl[royale::fortnite::kLods][kChunks * kChunks];          // one display list per chunk and level of detail
    float lo[kChunks * kChunks] = {}, hi[kChunks * kChunks] = {};             // each chunk's lowest and highest point, for culling
    bool built = false;
};
FortniteGpu gFortniteGpu;
Actor* gFortniteActor = nullptr;
bool gFortniteArrived = false;   // the local player has been put on the island since this scene loaded

void BuildFortniteGpu() {
    namespace fn = royale::fortnite;
    constexpr int kChunk = FortniteGpu::kChunk, kChunks = FortniteGpu::kChunks;
    FortniteGpu& g = gFortniteGpu;
    for (int lod = 0; lod < fn::kLods; lod++) g.vtx[lod].assign(static_cast<size_t>(fn::kCells) * fn::kCells * fn::LodVerts(lod), Vtx{});   // never resized again: the lists point into them
    std::fill(std::begin(g.lo), std::end(g.lo), 1.0e9f);
    std::fill(std::begin(g.hi), std::end(g.hi), -1.0e9f);
    std::vector<fn::DrawVert> tmp;
    for (int bj = 0; bj < fn::kCells; bj++) {
        for (int bi = 0; bi < fn::kCells; bi++) {
            for (int lod = 0; lod < fn::kLods; lod++) {
                tmp.clear();
                fn::BlockVertices(bi, bj, lod, tmp);
                Vtx* out = &g.vtx[lod][(static_cast<size_t>(bj) * fn::kCells + bi) * fn::LodVerts(lod)];
                for (size_t k = 0; k < tmp.size(); k++) {
                    out[k].v.ob[0] = tmp[k].x; out[k].v.ob[1] = tmp[k].y; out[k].v.ob[2] = tmp[k].z;
                    out[k].v.flag = 0;
                    out[k].v.tc[0] = out[k].v.tc[1] = 0;
                    out[k].v.cn[0] = tmp[k].r; out[k].v.cn[1] = tmp[k].g; out[k].v.cn[2] = tmp[k].b; out[k].v.cn[3] = 255;
                }
                if (lod == 0) {
                    const int c = (bj / kChunk) * kChunks + bi / kChunk;
                    for (const fn::DrawVert& v : tmp) { g.lo[c] = std::min<float>(g.lo[c], v.y); g.hi[c] = std::max<float>(g.hi[c], v.y); }
                }
            }
        }
    }
    for (int cj = 0; cj < kChunks; cj++) {
        for (int ci = 0; ci < kChunks; ci++) {
            for (int lod = 0; lod < fn::kLods; lod++) {
                std::vector<Gfx>& dl = g.dl[lod][cj * kChunks + ci];
                dl.assign(kChunk * kChunk * (1 + fn::kSub * fn::kSub * 2) + 1, Gfx{});
                Gfx* p = dl.data();
                for (int bj = cj * kChunk; bj < (cj + 1) * kChunk; bj++) {
                    for (int bi = ci * kChunk; bi < (ci + 1) * kChunk; bi++) {
                        const int use = fn::BlockIsOpenWater(bi, bj) ? fn::kLods - 1 : lod;   // open water is a flat square at any distance
                        const int n = fn::LodSquares(use), row = n + 1;
                        const Vtx* base = &g.vtx[use][(static_cast<size_t>(bj) * fn::kCells + bi) * fn::LodVerts(use)];
                        gSPVertex(p++, reinterpret_cast<uintptr_t>(base), fn::LodVerts(use), 0);
                        for (int b = 0; b < n; b++) {
                            for (int a = 0; a < n; a++) {
                                const int v = b * row + a;
                                gSP1Triangle(p++, v, v + 1, v + row + 1, 0);
                                gSP1Triangle(p++, v, v + row + 1, v + row, 0);
                            }
                        }
                    }
                }
                gSPEndDisplayList(p++);
                dl.resize(static_cast<size_t>(p - dl.data()));
            }
        }
    }
    g.built = true;
}

void FortniteTerrain_Init(Actor* actor, PlayState*) {
    Actor_SetScale(actor, 1.0f);
    actor->world.pos = actor->home.pos = { 0, 0, 0 };
}
void FortniteTerrain_Update(Actor*, PlayState*) {}
void FortniteTerrain_Destroy(Actor* actor, PlayState*) { if (gFortniteActor == actor) gFortniteActor = nullptr; }

// Draws only the chunks in front of the camera, each in the detail its distance calls for: full up close, half a little further, two triangles
// per square far away. Open sea costs two triangles a square whatever the distance.
void FortniteTerrain_Draw(Actor*, PlayState* play) {
    namespace fn = royale::fortnite;
    if (!gFortniteGpu.built) BuildFortniteGpu();
    constexpr int kChunk = FortniteGpu::kChunk, kChunks = FortniteGpu::kChunks;
    const Vec3f eye = play->view.eye, at = play->view.lookAt;
    float fx = at.x - eye.x, fy = at.y - eye.y, fz = at.z - eye.z;
    const float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (fl > 1.0f) { fx /= fl; fy /= fl; fz /= fl; } else { fx = 0; fy = -1; fz = 0; }
    const float flat = std::hypot(fx, fz);
    const float halfX = 0.5f * kChunk * fn::kCellX, halfZ = 0.5f * kChunk * fn::kCellZ;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(0.0f, 0.0f, 0.0f, MTXMODE_NEW);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);   // the colours are baked in; draw both sides
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    for (int cj = 0; cj < kChunks; cj++) {
        for (int ci = 0; ci < kChunks; ci++) {
            const int c = cj * kChunks + ci;
            const float cx = -fn::kHalfX + (ci + 0.5f) * kChunk * fn::kCellX, cz = -fn::kHalfZ + (cj + 0.5f) * kChunk * fn::kCellZ;
            const float cy = 0.5f * (gFortniteGpu.lo[c] + gFortniteGpu.hi[c]), halfY = 0.5f * (gFortniteGpu.hi[c] - gFortniteGpu.lo[c]);
            const float dx = cx - eye.x, dy = cy - eye.y, dz = cz - eye.z;
            const float r = std::sqrt(halfX * halfX + halfZ * halfZ + halfY * halfY) + 60.0f;   // the chunk's bounding sphere, with a margin
            const float along = dx * fx + dy * fy + dz * fz;
            if (along < -r) continue;                                                           // wholly behind the camera
            if (flat > 0.6f) {                                                                  // looking out rather than down: also skip what is far off to the side
                const float hx = fx / flat, hz = fz / flat, ahead = dx * hx + dz * hz, side = std::fabs(dx * hz - dz * hx);
                if (side - r > std::max(0.0f, ahead + r) * 2.4f) continue;
            }
            const float dist = std::hypot(dx, dz);
            const int lod = dist < 2600.0f ? 0 : dist < 5200.0f ? 1 : 2;
            gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(gFortniteGpu.dl[lod][c].data()));
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

int FortniteActorId() {
    static int id = -1;
    if (id < 0) {
        ActorDBInit init;
        init.name = "Royale_Terrain";
        init.desc = "Battle royale island";
        init.category = ACTORCAT_BG;
        init.flags = ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
        init.objectId = OBJECT_GAMEPLAY_KEEP;
        init.instanceSize = sizeof(Actor);
        init.init = FortniteTerrain_Init;
        init.destroy = FortniteTerrain_Destroy;
        init.update = FortniteTerrain_Update;
        init.draw = FortniteTerrain_Draw;
        id = ActorDB::Instance->AddEntry(init).entry.id;
    }
    return id;
}

// Called every frame: the island is drawn while we are in a scene loaded with its collision; the player is put on it once on arrival (the scene
// puts Link at the field's door, which is somewhere inside or under the island); and a lobby that changes between the field and the island
// reloads the scene, since the collision is chosen when the scene loads.
void DriveFortnite(Player* player, const royale::HudState& hud) {
    if (gPlayState == nullptr || !InGame()) return;
    const bool onIsland = gFortniteScene && gPlayState->sceneNum == SCENE_HYRULE_FIELD;
    if (onIsland && gFortniteActor == nullptr) {
        gFortniteActor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, static_cast<s16>(FortniteActorId()), 0, 0, 0, 0, 0, 0, 0, false);
    } else if (!onIsland && gFortniteActor != nullptr) {
        Actor_Kill(gFortniteActor);
        gFortniteActor = nullptr;
    }
    if (!gSession.Joined()) return;
    if (gPlayState->sceneNum == SCENE_HYRULE_FIELD && gFortniteScene != (gMapId == royale::fortnite::kMapId)) {
        GoToField();   // the host changed the map: load the scene again with the right ground
        return;
    }
    if (!onIsland) { gFortniteArrived = false; return; }
    if (gFortniteArrived || gPlayState->transitionTrigger != TRANS_TRIGGER_OFF) return;
    gFortniteArrived = true;
    // A little scatter, so a lobby's players don't all stand in one spot. Only in the lobby: once the match is on, the skydive places everyone.
    if (hud.state != royale::MatchState::Lobby || gSkydiving) return;
    namespace fn = royale::fortnite;
    const float a = static_cast<float>(std::rand() % 628) / 100.0f, r = static_cast<float>(std::rand() % 220);
    const float x = fn::kSpawnX + std::sin(a) * r, z = fn::kSpawnZ + std::cos(a) * r;
    float y = 0;
    if (!fn::GroundHeight(x, z, &y)) return;
    player->actor.world.pos = { x, y + 12.0f, z };
    player->actor.prevPos = player->actor.world.pos;
    player->actor.velocity = { 0, 0, 0 };
    player->actor.speedXZ = 0.0f;
    player->fallDistance = 0;
}

void ApplyPlatforms(Player* player) {
    if (!InField() || SolidActive()) return;
    RefreshPlatforms();
    if (gPlatformIdx.empty()) return;
    const auto& props = gSession.Client()->Props();
    const float px = player->actor.world.pos.x, pz = player->actor.world.pos.z;
    float py = player->actor.world.pos.y;
    // Walking: a grounded player steps up onto a block up to 64 tall without jumping (the real game's own step-up is much smaller), so the blocks
    // behave like stairs and terrain you can walk over; anything taller needs a jump.
    const float reach = ((player->actor.bgCheckFlags & 1) || player->actor.floorHeight >= py - 2.0f) ? 64.0f : 28.0f;
    for (size_t i : gPlatformIdx) {
        const royale::Prop& p = props[i];
        const float dx = px - p.pos.x, dz = pz - p.pos.z;
        if (std::fabs(dx) > royale::kPlatformHalf + 40.0f || std::fabs(dz) > royale::kPlatformHalf + 40.0f) continue;
        const float base = PlatformBase(i);
        if (base < -1.0e8f) continue;
        const float top = base + royale::PlatformHeight(p.kind);
        const bool onTop = std::fabs(dx) <= royale::kPlatformHalf && std::fabs(dz) <= royale::kPlatformHalf;
        if (onTop && py >= top - reach && player->actor.velocity.y <= 0.5f) {         // standing on it
            player->actor.world.pos.y = py = top;
            player->actor.velocity.y = 0.0f;
            player->actor.bgCheckFlags |= 1;
            player->actor.bgCheckFlags &= ~(2 | 4 | 8);   // not airborne, not against a wall: so no falling pose and no snagging on the block's edge
            player->actor.floorHeight = top;
            player->actor.gravity = player->actor.gravity > -0.1f ? player->actor.gravity : -1.0f;
        } else if (py < top - reach && std::fabs(dx) <= royale::kPlatformHalf + 16.0f && std::fabs(dz) <= royale::kPlatformHalf + 16.0f) {   // against its side: pushed out
            const float penX = royale::kPlatformHalf + 16.0f - std::fabs(dx), penZ = royale::kPlatformHalf + 16.0f - std::fabs(dz);
            if (penX < penZ) player->actor.world.pos.x += (dx >= 0 ? penX : -penX);
            else player->actor.world.pos.z += (dz >= 0 ? penZ : -penZ);
        }
    }
}

// Rocks, boulders and standing stones are solid in the mod's own terms too (the game's collision for the stand-in rock is smaller than the model you see,
// so you walked into the stone). You are pushed out of their sides, and can walk up onto the low ones (a rock, a boulder) the same way as onto the
// climbing blocks: step up by walking into them, or jump. Tall stones (the posts) are only walls.
void ApplyRocks(Player* player) {
    if (!InField() || !gSession.Client() || SolidActive()) return;
    const auto& props = gSession.Client()->Props();
    const float px = player->actor.world.pos.x, pz = player->actor.world.pos.z;
    const float py = player->actor.world.pos.y;
    const bool grounded = (player->actor.bgCheckFlags & 1) != 0;
    const float reach = grounded ? 64.0f : 28.0f;
    for (size_t i = 0; i < props.size(); i++) {
        const royale::Prop& p = props[i];
        if (p.kind != royale::PropKind::Rock && p.kind != royale::PropKind::Boulder && p.kind != royale::PropKind::Pillar) continue;
        const float dx = px - p.pos.x, dz = pz - p.pos.z;
        const bool boulder = p.kind == royale::PropKind::Boulder;
        const float radius = royale::PropRadius(p.kind) * (boulder ? royale::BoulderScale(p.rot) : 1.0f);
        if (std::fabs(dx) > radius + 40.0f || std::fabs(dz) > radius + 40.0f) continue;
        const float d = std::hypot(dx, dz);
        const float base = PlatformBase(i);
        if (base < -1.0e8f) continue;
        const float height = p.kind == royale::PropKind::Rock ? 24.0f : boulder ? royale::BoulderTop(royale::BoulderShape(p.rot)) * royale::BoulderScale(p.rot) : 200.0f;
        const float top = base + height;
        const float standR = radius * 0.62f;
        if (p.kind != royale::PropKind::Pillar && d <= standR && py >= top - reach && player->actor.velocity.y <= 0.5f) {   // on top
            player->actor.world.pos.y = top;
            player->actor.velocity.y = 0.0f;
            player->actor.bgCheckFlags |= 1;
            player->actor.floorHeight = top;
        } else if (d < radius && py < top - 4.0f && d > 0.01f) {   // against its side: out you go (and slide round it)
            const float push = radius - d;
            player->actor.world.pos.x += dx / d * push;
            player->actor.world.pos.z += dz / d * push;
        }
    }
}

// What a swing from Link would land on: the nearest other player or mini boss within reach and roughly in front of him.
// Returns 0 with *outDist left at 1e9 when there is none.
static uint16_t PickStrikeTarget(Player* player, float range, float* outDist) {
    uint16_t best = 0;
    float bestDist = 1e9f;
    for (const auto& [id, actor] : gActorOf) {
        auto st = gState.find(id);
        if (st == gState.end() || !st->second.alive) continue;
        float dx = st->second.x - player->actor.world.pos.x, dz = st->second.z - player->actor.world.pos.z;
        float d = std::sqrt(dx * dx + dz * dz);
        if (d > range * 1.05f) continue;
        // Within a 90 degree cone in front of Link (binary angle 0x2000 = 45 degrees).
        s16 toTarget = static_cast<s16>(std::atan2(dx, dz) * (32768.0f / 3.14159265f));
        s16 off = static_cast<s16>(toTarget - player->actor.shape.rot.y);
        if (std::abs(static_cast<int>(off)) > 0x2000 && d > 60.0f) continue;
        if (d < bestDist) { bestDist = d; best = id; }
    }
    // The mini bosses are big: they count from their edge, not their middle.
    if (gSession.Client()) {
        for (const auto& bn : gSession.Client()->Bosses()) {
            const float dx = bn.x - player->actor.world.pos.x, dz = bn.z - player->actor.world.pos.z;
            const float d = std::sqrt(dx * dx + dz * dz) - royale::kBossBodyRadius;
            if (d > range * 1.05f) continue;
            s16 toTarget = static_cast<s16>(std::atan2(dx, dz) * (32768.0f / 3.14159265f));
            s16 off = static_cast<s16>(toTarget - player->actor.shape.rot.y);
            if (std::abs(static_cast<int>(off)) > 0x2800 && d > 40.0f) continue;
            if (d < bestDist) { bestDist = d; best = static_cast<uint16_t>(bn.Id()); }
        }
    }
    // Carts: from their sides too (a cart is about a Link and a half wide and two long). People go first: a cart only counts when it is clearly nearer.
    for (const CartTarget& ct : CartTargets()) {
        const float dx = ct.x - player->actor.world.pos.x, dz = ct.z - player->actor.world.pos.z;
        const float d = std::sqrt(dx * dx + dz * dz) - royale::kCartHitRadius * 0.6f;
        if (d > range * 1.05f) continue;
        s16 toTarget = static_cast<s16>(std::atan2(dx, dz) * (32768.0f / 3.14159265f));
        s16 off = static_cast<s16>(toTarget - player->actor.shape.rot.y);
        if (std::abs(static_cast<int>(off)) > 0x2800 && d > 40.0f) continue;
        if (d + 30.0f < bestDist) { bestDist = std::max(0.0f, d); best = ct.id; }
    }
    *outDist = bestDist;
    return best;
}

// The game's own big sword moves (a jump slash: A while Z-targeting; a spin attack: hold and release B) count for their
// real worth: a jump slash hits harder and a spin catches everyone around you. Reported once, as each one starts.
static uint8_t gLastStrikeAnim = 0;
static void ReportStrikeMoves(Player* player, const royale::HudState& hud, uint8_t anim) {
    const uint8_t last = gLastStrikeAnim;
    gLastStrikeAnim = anim;
    if (anim == last) return;
    const bool jump = anim == static_cast<uint8_t>(royale::Anim::JumpSlash), spin = anim == static_cast<uint8_t>(royale::Anim::SpinAttack);
    if (!jump && !spin) return;
    const royale::WeaponStats w = royale::WeaponOf(hud.weapon);
    if (w.damage <= 0 || w.ranged) return;
    float dist = 1e9f;
    const uint16_t target = PickStrikeTarget(player, w.range, &dist);
    if (dist < 1e8f) gSession.ReportAttack(target, true, static_cast<uint8_t>(jump ? royale::AttackStyle::JumpSlash : royale::AttackStyle::Spin));
    else { HitNpcInFront(player, w); SmashPropInFront(player, w); }
    gAttackCooldown = std::max(gAttackCooldown, static_cast<int>(std::ceil(w.cooldown * royale::kTickHz)));
}

void HandleCombatInput(Player* player, const royale::HudState& hud) {
    if (gAttackCooldown > 0) gAttackCooldown--;
    if (!LiveAndAlive(hud) || !InField()) return;
    const Input& in = gPlayState->state.input[0];

    // C-Up: jump. In the air, if a ledge about knee to chest high is right in front of you, you are hauled up onto it.
    if ((in.press.button & BTN_CUP) && (player->actor.bgCheckFlags & 1) && !(player->stateFlags1 & (PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_GETTING_ITEM))) {
        player->actor.velocity.y = 11.5f;
        player->actor.bgCheckFlags &= ~1;
        gJumpAssistFrames = 14;
        Audio_PlaySoundGeneral(NA_SE_PL_JUMP, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }
    if (gJumpAssistFrames > 0) {
        gJumpAssistFrames--;
        const float yaw = player->actor.shape.rot.y * (3.14159265f / 32768.0f);
        const float fx = std::sin(yaw), fz = std::cos(yaw);
        float ledge = 0;
        float ground = GroundY(gPlayState, player->actor.world.pos.x, player->actor.world.pos.z, player->actor.world.pos.y - 1.0f);
        float standingOn = 0;
        if (PlatformTopAt(player->actor.world.pos.x, player->actor.world.pos.z, &standingOn) && player->actor.world.pos.y >= standingOn - 30.0f) ground = std::max(ground, standingOn);   // the step you are on counts as the ground
        for (float reach : { 28.0f, 48.0f }) {
            float y;
            if (FloorAt(player->actor.world.pos.x + fx * reach, player->actor.world.pos.z + fz * reach, &y) && y > ledge) ledge = y;
        }
        const float rise = ledge - player->actor.world.pos.y;
        if (ledge > 0 && rise > 6.0f && ledge - ground > 24.0f && ledge - ground < 105.0f && rise < 70.0f && player->actor.velocity.y < 6.5f) {
            player->actor.world.pos.x += fx * 5.0f;                     // up and over the edge
            player->actor.world.pos.z += fz * 5.0f;
            player->actor.velocity.y = std::max(player->actor.velocity.y, 7.0f);
            if (rise < 14.0f) player->actor.world.pos.y = ledge + 1.0f;  // close enough: on top
        }
    }

    if (in.press.button & BTN_DDOWN) {
        if (hud.potions > 0) { gSession.RequestUsePotion(); StartAction(royale::Anim::Drink, kDrinkSeconds, BottleToDrink(hud, false)); DrinkSounds(player); }
        else Say("No potions");
    }

    // C-Left: drink a shield potion (the bar under your hearts).
    if (in.press.button & BTN_CLEFT) {
        bool has = false;
        for (const auto& pot : hud.inv.potions) has |= royale::PotionOf(static_cast<royale::ItemId>(pot.item)).shield > 0;
        if (!has) Say("No shield potion");
        else if (hud.inv.shield >= royale::kMaxShield - 0.05f) Say("Your shield is full");
        else { gSession.UseShield(); StartAction(royale::Anim::Drink, kDrinkSeconds, BottleToDrink(hud, true)); DrinkSounds(player); }
    }

    // A: open the chest in front of you, take or swap the item on the ground, hire an ally or talk. Walking over an upgrade picks it up on its own.
    // While a text box is up, A belongs to it.
    if ((in.press.button & BTN_A) && !MessageBoxUp()) {
        const size_t target = NearestLootIndex();
        if (target != kNoLoot) gSession.RequestPickup(static_cast<uint32_t>(target), true);
        else {   // nothing to open or take: maybe somebody to hire
            const int ally = NearbyFreeAlly();
            if (ally >= 0) {
                const royale::AllyDef& def = royale::kAllyDefs[ally];
                if (hud.inv.rupees < def.price) Say(std::string("The ") + def.name + " wants " + std::to_string(def.price) + " rupees (you have " + std::to_string(hud.inv.rupees) + ")");
                else gSession.HireAlly(ally);
            } else {
                PressedATalk();
            }
        }
    }

    // D-pad Right: your next weapon; D-pad Left: back to the one before (like the bumpers on a hotbar). With spares B and C, the hand goes A, B, C, A...
    if (in.press.button & (BTN_DLEFT | BTN_DRIGHT)) {
        const int spares = static_cast<int>(hud.inv.reserve.size());
        if (spares == 0) {
            Say("No other weapon: open chests to find more");
        } else if (in.press.button & BTN_DRIGHT) {
            gNextWeaponSlot = gNextWeaponSlot > spares ? 1 : gNextWeaponSlot;
            gSession.SelectWeapon(gNextWeaponSlot);
            gLastWeaponSlot = gNextWeaponSlot;
            gNextWeaponSlot = gNextWeaponSlot >= spares ? 1 : gNextWeaponSlot + 1;
        } else {   // back: swap again with the slot we last swapped, which puts the earlier weapon back in hand
            const int back = gLastWeaponSlot >= 1 && gLastWeaponSlot <= spares ? gLastWeaponSlot : spares;
            gSession.SelectWeapon(back);
            gNextWeaponSlot = back;
        }
    }

    if (in.press.button & BTN_DUP) {
        if (!hud.inv.hasAbility) Say("No ability");
        else if (hud.abilityReadyIn > 0.05f) Say("Ability recharging: " + ClockText(hud.abilityReadyIn));
        else if (hud.magic + 0.001f < royale::AbilityMagic(static_cast<royale::ItemId>(hud.inv.ability.item))) Say("Not enough magic (" + std::to_string(static_cast<int>(hud.magic)) + " of " + std::to_string(static_cast<int>(royale::AbilityMagic(static_cast<royale::ItemId>(hud.inv.ability.item)))) + ")");
        else {
            gSession.UseAbility();
            const royale::ItemId ab = static_cast<royale::ItemId>(hud.inv.ability.item);
            StartAction(royale::IsSong(ab) || ab == royale::ItemId::FairyOcarina || ab == royale::ItemId::OcarinaOfTime ? royale::Anim::Play
                        : ab == royale::ItemId::ShockwaveGrenade || ab == royale::ItemId::Hookshot || ab == royale::ItemId::Longshot ? royale::Anim::Throw : royale::Anim::Cast,
                        royale::IsSong(ab) ? 1.5f : 0.9f, ab);
            PowerFx(gPlayState, ab, player->actor.world.pos, true, &player->actor, hud.selfId);
            Audio_PlaySoundGeneral(royale::IsSong(ab) || ab == royale::ItemId::FairyOcarina || ab == royale::ItemId::OcarinaOfTime ? NA_SE_PL_MAGIC_SOUL_BALL : AbilitySfx(ab),
                                   &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            if (const u16 v = AbilityVoice(ab)) Player_PlaySfx(&player->actor, static_cast<u16>(v + player->ageProperties->unk_92));
        }
    }

    if (!(in.press.button & BTN_B) || gAttackCooldown > 0) return;
    const royale::AmmoKind ammoKind = royale::AmmoUsedBy(hud.weapon);
    const bool hasAmmo = ammoKind == royale::AmmoKind::None || hud.ammo[static_cast<size_t>(ammoKind)] > 0;
    royale::WeaponStats w = royale::ActiveWeapon(hud.weapon, hasAmmo);   // no ammo: the weapon is only bashed with
    if (w.damage <= 0) return;
    if (!hasAmmo && ammoKind != royale::AmmoKind::None && gAttackCooldown <= 0) Say(std::string("Out of ") + royale::AmmoName(ammoKind) + ": bash them with it, or find more");
    gAttackCooldown = std::max(1, static_cast<int>(std::ceil(w.cooldown * royale::kTickHz)));

    float bestDist = 1e9f;
    const uint16_t best = PickStrikeTarget(player, w.range, &bestDist);
    // Show the swing or the shot: face what you are hitting, strike the pose, and loose the arrow or throw the bomb.
    s16 aim = player->actor.shape.rot.y;
    if (bestDist < 1e8f) {
        float tx = 0, tz = 0;
        if (KnownPosition(best, &tx, &tz)) aim = static_cast<s16>(std::atan2(tx - player->actor.world.pos.x, tz - player->actor.world.pos.z) * (32768.0f / 3.14159265f));
        player->actor.shape.rot.y = player->actor.world.rot.y = aim;
    }
    StartAction(hasAmmo ? PoseForWeapon(hud.weapon) : royale::Anim::Attack, 0.45f);
    // The swing, shot or throw (its model, sound and flight) is the game's own item code now, run by the item on the B button.
    if (bestDist < 1e8f) { gSession.ReportAttack(best, true); return; }
    HitNpcInFront(player, w);
    SmashPropInFront(player, w);
}

// The match-start skydive, as in Fortnite: you spawn high above your spawn point, hang there during the countdown, then fall
// during the drop. The stick steers, holding Z dives faster, and the ground ends it (anyone still airborne when the drop ends
// plummets). The server only knows x and z, and protects everyone for the whole drop, so this is all done on the client.
constexpr float kSkyHeight = royale::kSkyHeight;   // shared/balance.h: the bots skydive with the same numbers
constexpr float kGlideSpeed = royale::kGlideSpeed;
constexpr float kDiveSpeed = royale::kDiveSpeed;
constexpr float kAirSpeed = royale::kAirSpeed;
// Where you may touch down: floor that is not water, lava, a door, a cliff face or a bottomless drop, inside the circle the storm has left.
bool SafeLanding(float x, float z, const royale::HudState& hud) {
    float y;
    if (!FloorAt(x, z, &y) || std::fabs(y - gMedianFloorY) > 1200.0f || UnderWater(x, z, y) || OnExitFloor(x, z) || NearLoadingZone(x, z, 200.0f) || HazardFloorAt(x, z)) return false;
    if (hud.safeZone.radius > 0.0f && royale::Distance({ x, z }, hud.safeZone.center) > hud.safeZone.radius * 0.97f) return false;
    return true;
}

// The skydive is steered by you, but it never lets you land in the lava, the lake or a doorway: low enough to matter, the glider eases toward the nearest
// ground that is safe, and if you somehow end up over nothing at all you are put back above the nearest safe ground.
royale::Vec2 gLandTarget = {};
bool gHaveLandTarget = false;
int gLandCheckAge = 0;
bool FindSafeLanding(float x, float z, const royale::HudState& hud, royale::Vec2* out) {
    for (float r = 120.0f; r <= 3600.0f; r += 120.0f) {   // rings outward from where you are: the nearest safe spot wins
        const int n = std::max(8, static_cast<int>(r / 60.0f));
        for (int i = 0; i < n; i++) {
            const float a = 6.2831853f * i / n;
            const float tx = x + std::sin(a) * r, tz = z + std::cos(a) * r;
            if (SafeLanding(tx, tz, hud)) { *out = { tx, tz }; return true; }
        }
    }
    return false;
}
void SteerToSafeLanding(Player* player, const royale::HudState& hud, float dt) {
    const float px = player->actor.world.pos.x, pz = player->actor.world.pos.z;
    float ground = 0;
    const bool haveGround = FloorAt(px, pz, &ground);
    const float height = haveGround ? player->actor.world.pos.y - ground : 1.0e5f;
    if (haveGround && height > 1500.0f) { gHaveLandTarget = false; return; }   // high up: steer wherever you like
    if (++gLandCheckAge >= 6 || (haveGround ? false : !gHaveLandTarget)) {       // a few times a second
        gLandCheckAge = 0;
        gHaveLandTarget = !SafeLanding(px, pz, hud) && FindSafeLanding(px, pz, hud, &gLandTarget);
    }
    if (!haveGround && player->actor.world.pos.y < gMedianFloorY - 400.0f) {   // fell past the bottom of the map: back above the closest safe ground
        royale::Vec2 safe;
        if (FindSafeLanding(px, pz, hud, &safe) || FindSafeLanding(hud.safeZone.center.x, hud.safeZone.center.z, hud, &safe)) {
            player->actor.world.pos.x = safe.x; player->actor.world.pos.z = safe.z;
            player->actor.world.pos.y = gMedianFloorY + 500.0f;
        }
        return;
    }
    if (!gHaveLandTarget) return;
    const float dx = gLandTarget.x - px, dz = gLandTarget.z - pz;
    const float d = std::hypot(dx, dz);
    if (d < 20.0f) { gHaveLandTarget = false; return; }
    const float pull = std::min(d, kAirSpeed * 1.6f * dt);   // a firm drift: faster than you can steer away from it once you are low
    player->actor.world.pos.x += dx / d * pull;
    player->actor.world.pos.z += dz / d * pull;
}

void UpdateSkydive(Player* player, const royale::HudState& hud) {
    if (!gSkydiving) return;
    const bool phaseOk = hud.state == royale::MatchState::Countdown || hud.state == royale::MatchState::Drop || hud.state == royale::MatchState::InMatch;
    if (!phaseOk || !InField() || (hud.haveSelf && !hud.selfAlive)) { gSkydiving = false; return; }
    constexpr float dt = 1.0f / royale::kTickHz;
    const Input& in = gPlayState->state.input[0];

    // Steer relative to the camera, the way Link's stick normally works.
    const float sx = in.cur.stick_x, sy = in.cur.stick_y;
    const float mag = std::min(1.0f, std::sqrt(sx * sx + sy * sy) / 60.0f);
    if (mag > 0.1f) {
        const float yaw = static_cast<float>(Camera_GetInputDirYaw(GET_ACTIVE_CAM(gPlayState))) * (3.14159265f / 32768.0f);
        const float a = yaw + std::atan2(-sx, sy);
        player->actor.world.pos.x += std::sin(a) * kAirSpeed * mag * dt;
        player->actor.world.pos.z += std::cos(a) * kAirSpeed * mag * dt;
        player->actor.shape.rot.y = static_cast<s16>(a * (32768.0f / 3.14159265f));
        player->actor.world.rot.y = player->actor.shape.rot.y;
    }

    gGliderRoll += ((mag > 0.1f ? -sx / 80.0f * 0.5f : 0.0f) - gGliderRoll) * 0.12f;                  // bank into the turn
    gGliderDiving = hud.state == royale::MatchState::Drop && gDiveHeld;

    float fall = 0.0f;                                                          // hold in the sky during the countdown
    if (hud.state == royale::MatchState::Drop) fall = gDiveHeld ? kDiveSpeed : kGlideSpeed;
    else if (hud.state == royale::MatchState::InMatch) fall = royale::kLateFallSpeed;           // the drop is over: land now
    SteerToSafeLanding(player, hud, dt);
    const float ground = GroundY(gPlayState, player->actor.world.pos.x, player->actor.world.pos.z, -1.0e6f);
    float y = player->actor.world.pos.y - fall * dt;
    if (ground > -1.0e5f && y <= ground + 8.0f) {
        y = ground + 8.0f;
        gSkydiving = false;                                                       // touched down; normal gravity takes over
    }
    player->actor.world.pos.y = y;
    player->actor.prevPos = player->actor.world.pos;
    player->actor.velocity.y = 0.0f;
    player->actor.speedXZ = 0.0f;
    player->fallDistance = 0;                                                     // no landing damage or hard-landing stun
}

// The Shockwave Grenade, as in Fortnite: a blast under your feet throws you high into the air, and you take no fall damage (no hard landing,
// no stagger) until you land and for a moment after. The server only knows x and z, so this is all done here. Link is in his own jump the
// whole way, so the stick steers him in the air as in any jump, and walls and ledges work as they always do.
constexpr float kLaunchGraceSeconds = 1.0f;   // still safe this long after touching down (a bounce off a ledge, a slope into a drop)
int gLaunchFrames = 0;                        // frames since the launch; 0 = no launch going
bool gLaunchAirborne = false;                 // has left the ground
double gLaunchSafeUntil = 0;                  // set when you touch down

void LaunchSelf(Player* player, royale::Rarity rarity) {
    if (gSkydiving || player->stateFlags1 & (PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_DEAD)) return;
    const float height = 260.0f * royale::RarityScale(rarity);           // Uncommon about 300 units up (six Links), Legendary about 455
    const float g = player->actor.gravity < -0.3f ? -player->actor.gravity : 1.0f;
    player->actor.velocity.y = std::sqrt(2.0f * g * height);
    player->actor.world.pos.y += 2.0f;                                   // off the floor, so the game puts him in his jump next frame
    player->actor.bgCheckFlags &= ~1;
    player->fallStartHeight = static_cast<s16>(player->actor.world.pos.y);
    gLaunchFrames = 1;
    gLaunchAirborne = false;
    gLaunchSafeUntil = 0;
}

void UpdateLaunch(Player* player) {
    if (gLaunchFrames == 0) return;
    gLaunchFrames++;
    const bool grounded = (player->actor.bgCheckFlags & 1) || (player->stateFlags1 & PLAYER_STATE1_IN_WATER);
    if (!grounded) gLaunchAirborne = true;
    // The game measures a fall from the last height it saw you standing at. Moving that up with you means every landing is a short hop.
    player->fallStartHeight = static_cast<s16>(player->actor.world.pos.y);
    player->fallDistance = 0;
    const double now = ImGui::GetTime();
    if (!gLaunchAirborne) {
        if (gLaunchFrames > 10) gLaunchFrames = 0;                       // never got off the ground (hanging on a ledge, say): nothing to protect
        return;
    }
    if (!grounded && gLaunchSafeUntil == 0 && (gLaunchFrames & 1)) {     // a purple trail behind you on the way up and down
        Vec3f pos = { player->actor.world.pos.x, player->actor.world.pos.y + 10.0f, player->actor.world.pos.z }, vel = { 0, -0.5f, 0 }, accel = { 0, 0, 0 };
        Color_RGBA8 prim = { 235, 190, 255, 255 }, env = { 160, 60, 255, 255 };
        EffectSsKiraKira_SpawnDispersed(gPlayState, &pos, &vel, &accel, &prim, &env, 90, 20);
    }
    if (grounded && gLaunchSafeUntil == 0) gLaunchSafeUntil = now + kLaunchGraceSeconds;
    if ((gLaunchSafeUntil > 0 && now > gLaunchSafeUntil) || gLaunchFrames > 20 * 30) gLaunchFrames = 0;   // done (or something held you in the air for ages)
}

// Where you will land if you keep doing what you are doing: a ring on the ground at that spot (and a faint one straight below you), with how
// high you are. Steering moves the ring, holding Z pulls it closer, so you can pick a landing spot before you get there.
void DrawGliderAim(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale) {
    if (gPlayState == nullptr || !InField()) return;
    Player* player = GET_PLAYER(gPlayState);
    const Input& in = gPlayState->state.input[0];
    const float px = player->actor.world.pos.x, pz = player->actor.world.pos.z, py = player->actor.world.pos.y;
    const float below = GroundY(gPlayState, px, pz, -1.0e6f);
    if (below < -1.0e5f) return;
    const float sx = in.cur.stick_x, sy = in.cur.stick_y;
    const float mag = std::min(1.0f, std::sqrt(sx * sx + sy * sy) / 60.0f);
    const bool dropping = gStateNow == royale::MatchState::Drop;
    const float fall = dropping ? (gDiveHeld ? kDiveSpeed : kGlideSpeed) : royale::kLateFallSpeed;
    const float seconds = std::max(0.0f, (py - below)) / fall;
    float lx = px, lz = pz;
    if (mag > 0.1f) {   // the same steering the skydive uses, held for the rest of the fall
        const float yaw = static_cast<float>(Camera_GetInputDirYaw(GET_ACTIVE_CAM(gPlayState))) * (3.14159265f / 32768.0f);
        const float a = yaw + std::atan2(-sx, sy);
        lx += std::sin(a) * kAirSpeed * mag * seconds;
        lz += std::cos(a) * kAirSpeed * mag * seconds;
    }
    const float ly = GroundY(gPlayState, lx, lz, py);
    const ImU32 gold = IM_COL32(255, 236, 120, 255);
    ImVec2 at, under;
    const bool haveUnder = WorldToScreen(px, below + 2.0f, pz, &under);
    if (WorldToScreen(lx, (ly > -1.0e5f ? ly : below) + 2.0f, lz, &at) && at.x > 0 && at.x < ds.x && at.y > 0 && at.y < ds.y) {
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 6.0f);
        const float r = (16.0f + 3.0f * pulse) * scale;
        dl->AddCircle(at, r + 2 * scale, IM_COL32(0, 0, 0, 160), 24, 5.0f * scale);
        dl->AddCircle(at, r, gold, 24, 3.0f * scale);
        dl->AddCircleFilled(at, 3.5f * scale, gold);
        if (haveUnder && mag > 0.1f) dl->AddLine(under, at, IM_COL32(255, 236, 120, 90), 2.0f * scale);
        char text[48];
        std::snprintf(text, sizeof(text), "%d m", static_cast<int>((py - below) * 0.1f));
        const ImVec2 sz = font->CalcTextSizeA(15.0f * scale, FLT_MAX, 0, text);
        dl->AddText(font, 15.0f * scale, ImVec2(at.x - sz.x * 0.5f + scale, at.y + r + 5 * scale + scale), IM_COL32(0, 0, 0, 220), text);
        dl->AddText(font, 15.0f * scale, ImVec2(at.x - sz.x * 0.5f, at.y + r + 5 * scale), IM_COL32(255, 255, 255, 255), text);
    }
}

// Boots, Epona's Song and friends: the server allows a faster run and the game applies it by stretching the step Link just took.
// (Teleports and the drop to spawn move far more than a step, so those are left alone.)
royale::Vec2 gLastPos = {};
bool gHaveLastPos = false;
void ApplySpeedBuffs(Player* player, const royale::HudState& hud) {
    const bool active = LiveAndAlive(hud) && InField();
    const float mult = hud.speedMult;
    if (active && gHaveLastPos && std::fabs(mult - 1.0f) > 0.01f) {
        const float dx = player->actor.world.pos.x - gLastPos.x, dz = player->actor.world.pos.z - gLastPos.z;
        if (dx * dx + dz * dz < 40.0f * 40.0f) {
            player->actor.world.pos.x += dx * (mult - 1.0f);
            player->actor.world.pos.z += dz * (mult - 1.0f);
        }
    }
    gLastPos = { player->actor.world.pos.x, player->actor.world.pos.z };
    gHaveLastPos = active;
}

// Link holds what the server says he holds, using the game's own item code. The matching real item (Deku Stick, bow, bombs...) sits on the B
// button, so the game itself draws it, puts it in his hand and runs its swing, shot or throw; before this the item was forced into his hand with
// B empty, and the game put it away again a moment later. The save's own B item is put back when the match is over.
royale::ItemId gLocalWeaponShown = static_cast<royale::ItemId>(255);
bool gLocalLookApplied = false;
u8 gSavedButtonItem0 = ITEM_NONE;
u8 RealItemFor(royale::ItemId w) {
    using royale::ItemId;
    switch (w) {
        case ItemId::BasicSword: case ItemId::KokiriSword: return ITEM_SWORD_KOKIRI;
        case ItemId::MasterSword: return ITEM_SWORD_MASTER;
        case ItemId::BiggoronSword: return ITEM_SWORD_BGS;
        case ItemId::MegatonHammer: case ItemId::GiantsHammer: return ITEM_HAMMER;
        case ItemId::DekuStick: return ITEM_STICK;
        case ItemId::FairyBow: return ITEM_BOW;
        case ItemId::FireArrows: return ITEM_BOW_ARROW_FIRE;
        case ItemId::IceArrows: return ITEM_BOW_ARROW_ICE;
        case ItemId::LightArrows: return ITEM_BOW_ARROW_LIGHT;
        case ItemId::Slingshot: case ItemId::TripleSlingshot: return ITEM_SLINGSHOT;
        case ItemId::Boomerang: return ITEM_BOOMERANG;
        case ItemId::Bombs: return ITEM_BOMB;
        case ItemId::Bombchus: case ItemId::HomingBombchus: return ITEM_BOMBCHU;
        case ItemId::DekuNuts: return ITEM_NUT;
        default: return ITEM_NONE;
    }
}
// The game's own enhancements that let any Link hold and shoot any item (child with the bow, adult with the slingshot); on only during a match.
const char* const kItemCvars[3] = { CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), CVAR_ENHANCEMENT("BowSlingshotAmmoFix"), CVAR_CHEAT("TimelessEquipment") };
int gSavedItemCvars[3] = {};
bool gItemCvarsOn = false;
void ApplyItemCvars(bool on) {
    if (on == gItemCvarsOn) return;
    gItemCvarsOn = on;
    for (int i = 0; i < 3; i++) {
        if (on) { gSavedItemCvars[i] = CVarGetInteger(kItemCvars[i], 0); CVarSetInteger(kItemCvars[i], 1); }
        else CVarSetInteger(kItemCvars[i], gSavedItemCvars[i]);
    }
}
// The game's use of an item checks its ammo and (for the elemental arrows) magic, so give it the server's counts; the server stays the authority.
void SyncRealAmmo(const royale::HudState& hud) {
    auto set = [&](int slot, royale::AmmoKind k) { gSaveContext.inventory.ammo[slot] = static_cast<s8>(std::min(99, static_cast<int>(hud.ammo[static_cast<size_t>(k)]))); };
    set(SLOT_BOW, royale::AmmoKind::Arrows);
    set(SLOT_SLINGSHOT, royale::AmmoKind::Seeds);
    set(SLOT_BOMB, royale::AmmoKind::Bombs);
    set(SLOT_BOMBCHU, royale::AmmoKind::Bombchus);
    set(SLOT_NUT, royale::AmmoKind::Nuts);
    gSaveContext.inventory.ammo[SLOT_STICK] = 9;   // sticks never run out
    if (hud.weapon == royale::ItemId::FireArrows || hud.weapon == royale::ItemId::IceArrows || hud.weapon == royale::ItemId::LightArrows) {
        gSaveContext.isMagicAcquired = true;
        gSaveContext.magicLevel = 1;
        gSaveContext.magicCapacity = 0x30;
        gSaveContext.magic = 0x30;
    }
}
// What your own Link wears in a match, as the game's values: the boots and mask are only put on him while he is drawn (the game's own iron boots,
// hover boots and bunny hood would also change how he moves, and the server decides that), the shield for real (it is what he raises).
struct LocalDress {
    bool on = false;
    s8 boots = PLAYER_BOOTS_KOKIRI;
    u8 mask = PLAYER_MASK_NONE;
    royale::ItemId weapon = royale::ItemId::BasicSword;
};
LocalDress gLocalDress;
bool LocalDressOn() { return gLocalDress.on; }
royale::ItemId WornGear(const royale::HudState& hud, royale::GearSlot slot) {
    const int i = static_cast<int>(slot);
    return (hud.inv.gearMask & (1 << i)) ? static_cast<royale::ItemId>(hud.inv.gear[i].item) : royale::ItemId::Count;
}
void SyncLocalDress(Player* player, const royale::HudState& hud) {
    gLocalDress.on = gSession.Joined() && IsLive(hud) && hud.selfAlive && InField();
    gSelfInvulnLeft = hud.invulnLeft;
    if (!gLocalDress.on) return;
    gLocalDress.boots = PlayerBootsFor(WornGear(hud, royale::GearSlot::Boots));
    gLocalDress.mask = PlayerMaskFor(WornGear(hud, royale::GearSlot::Mask));
    gLocalDress.weapon = hud.weapon;
    const s8 shield = hud.hasShield ? PlayerShieldFor(hud.shield) : PLAYER_SHIELD_NONE;
    if (player->currentShield != shield && !(player->stateFlags1 & PLAYER_STATE1_SHIELDING)) {
        Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, static_cast<u16>(shield));   // the pause screen and the game's own checks agree (put back after the match)
        player->currentShield = shield;
        Player_SetModelGroup(player, Player_ActionToModelGroup(player, player->heldItemAction));
    }
    if (player->actor.draw == Player_Draw) player->actor.draw = LocalLink_Draw;
}
// Draws your own Link dressed (see LocalDress). While you drink, play a song or use a gadget, the bottle, the ocarina or the hookshot is in his
// hand as in the game, and if you are standing still he goes through the game's motions for it (the bottle raised, the ocarina played, the
// spell cast). Swings and shots need none of this: they are the game's own item code (SyncLocalWeapon).
void DrawLocalDressed(Player* player, PlayState* play, bool mayPose) {
    if (!gLocalDress.on) { Player_Draw(&player->actor, play); return; }
    const s8 boots = player->currentBoots;
    const u8 mask = player->currentMask;
    player->currentBoots = gLocalDress.boots;
    player->currentMask = gLocalDress.mask;
    const bool using_ = mayPose && gActionFrames > 0 && gActionItem != royale::ItemId::Count && player->meleeWeaponState == 0 &&
                        !(player->stateFlags1 & (PLAYER_STATE1_SHIELDING | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_DEAD));
    const s32 group = player->modelGroup;
    const s8 itemAction = player->itemAction, held = player->heldItemAction;
    const u8 button = gSaveContext.equips.buttonItems[0];
    bool swapped = false;
    if (using_) {
        const float t = static_cast<float>(ImGui::GetTime() - gActionStart);
        const Look look = ActionLook(static_cast<uint8_t>(gActionAnim), gLocalDress.weapon, gActionItem, t, gActionItem);
        if (look.modelGroup != group || look.itemAction != held) {
            gSaveContext.equips.buttonItems[0] = look.buttonItem;
            player->itemAction = player->heldItemAction = look.itemAction;
            Player_SetModelGroup(player, look.modelGroup);
            swapped = true;
        }
        if (std::fabs(player->linearVelocity) < 1.0f && (player->actor.bgCheckFlags & 1))
            PoseSeq(player, SeqFor(static_cast<uint8_t>(gActionAnim), gLocalDress.weapon, 0, gActionItem, player), t);
    }
    Player_Draw(&player->actor, play);
    if (swapped) {
        gSaveContext.equips.buttonItems[0] = button;
        player->itemAction = itemAction;
        player->heldItemAction = held;
        Player_SetModelGroup(player, group);
    }
    player->currentBoots = boots;
    player->currentMask = mask;
}

int gUseRetry = 0;
void SyncLocalWeapon(Player* player, const royale::HudState& hud) {
    ApplyItemCvars(gSession.Joined() && IsLive(hud));
    SyncLocalDress(player, hud);
    const bool on = LiveAndAlive(hud) && InField() && !gSkydiving && gEmote.id < 0;
    if (!on) {
        if (gLocalLookApplied && !(gSession.Joined() && IsLive(hud))) { gSaveContext.equips.buttonItems[0] = gSavedButtonItem0; gLocalLookApplied = false; gLocalWeaponShown = static_cast<royale::ItemId>(255); }
        return;
    }
    const Look look = LookFor(hud.weapon);
    const u8 item = RealItemFor(hud.weapon);
    if (!gLocalLookApplied) { gSavedButtonItem0 = gSaveContext.equips.buttonItems[0]; gLocalLookApplied = true; gLocalWeaponShown = static_cast<royale::ItemId>(255); }
    SyncRealAmmo(hud);
    gSaveContext.equips.buttonItems[0] = item;
    // The interface puts the bow, slingshot and bombchu back from this value when it refreshes the B button, so it must name the same item.
    gSaveContext.buttonStatus[0] = item;
    if (item == ITEM_NONE) return;
    const bool free = !(player->stateFlags1 & (PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_GETTING_ITEM | PLAYER_STATE1_START_CHANGING_HELD_ITEM | PLAYER_STATE1_SHIELDING)) &&
                      player->stateFlags2 == (player->stateFlags2 & ~PLAYER_STATE2_OCARINA_PLAYING);
    const s8 want = Player_ItemToItemAction(item);
    if (hud.weapon != gLocalWeaponShown || player->heldItemAction != want) {
        if (!free) return;
        if (gUseRetry > 0) { gUseRetry--; return; }
        gLocalWeaponShown = hud.weapon;
        gUseRetry = 10;
        Player_UseItem(gPlayState, player, item);   // the game's own take-out: the change animation, the sound, the item in his hand
        if (player->heldItemAction != want) {       // refused (nothing to shoot, say): still show it in the hand
            player->itemAction = player->heldItemAction = look.itemAction;
            Player_SetModelGroup(player, look.modelGroup);
        }
    }
}

// In a match only the server eliminates anyone. If the game's own death started anyway (health hit 0 inside the player's update, before
// this hook could put it back), undo it before the game-over screen, its music or its camera take over: the player would otherwise be
// stuck in the dying animation while spectating, then thrown to the game-over menu.
void CancelGameDeath(Player* player) {
    if (!(player->stateFlags1 & PLAYER_STATE1_DEAD) && gPlayState->gameOverCtx.state == GAMEOVER_INACTIVE) {
        gMatchSeqId = gSaveContext.seqId;
        gMatchAmbienceId = gSaveContext.natureAmbienceId;
        return;
    }
    gPlayState->gameOverCtx.state = GAMEOVER_INACTIVE;
    player->stateFlags1 &= ~PLAYER_STATE1_DEAD;
    gPlayState->func_11D54(player, gPlayState);   // back to standing still
    OnePointCutscene_EndCutscene(gPlayState, SUBCAM_ACTIVE);   // the death's close-up camera
    Audio_QueueSeqCmd(0x100000FF | (SEQ_PLAYER_FANFARE << 24));   // stop the game-over tune
    func_800F47FC();                                               // the death muted the music; bring it back up
    gSaveContext.seqId = gMatchSeqId;
    gSaveContext.natureAmbienceId = gMatchAmbienceId;
    if (gMatchSeqId != 0xFF) Audio_QueueSeqCmd((SEQ_PLAYER_BGM_MAIN << 24) | gMatchSeqId);
}

void OnPlayerUpdate() {
    Feat("player update");
    gRoyaleRunSpeedScale = 1.0f;   // normal speed unless UpdateSprint below says otherwise
    if (!gSession.Joined() || !InGame()) return;
    Player* player = GET_PLAYER(gPlayState);
    royale::GameClient* client = gSession.Client();
    royale::HudState hud = gSession.Hud();

    // The server moves everyone to spawn points when the match starts. It only knows x and z, so drop from above. This only
    // happens once the player has actually arrived in the field (they may still be loading in from the waiting room).
    if (InField() && client->Epoch() != gLastEpoch) {
        gLastEpoch = client->Epoch();
        if (const royale::net::PlayerNet* self = client->Self()) {
            player->actor.world.pos.x = self->x;
            player->actor.world.pos.z = self->z;
            const float ground = GroundY(gPlayState, self->x, self->z, 1500.0f);
            // At the start of a match you spawn high in the sky and fall in; any other teleport (Hookshot, songs) is a plain move.
            gSkydiving = hud.state == royale::MatchState::Countdown || hud.state == royale::MatchState::Drop;
            player->actor.world.pos.y = gSkydiving ? ground + kSkyHeight : ground + 20.0f;
            player->actor.prevPos = player->actor.world.pos;
            player->actor.home.pos = player->actor.world.pos;
        }
    }

    // Only report a position from a scene the server expects. Otherwise a player still loading in from the waiting room
    // would drag their spawn point toward their old coordinates.
    bool allowed = hud.state == royale::MatchState::Lobby ? InPlayableScene(hud.state) : InField();
    if (allowed) {
        const uint8_t anim = ClassifyAnim(player);
        if (hud.state == royale::MatchState::InMatch) ReportStrikeMoves(player, hud, anim);
        gSession.SendLocalPose(player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z,
                               player->actor.shape.rot.y, anim, static_cast<uint8_t>(gPlayState->sceneNum));
    }

    {   // Adult Power: you grow, and shrink back when it runs out
        const float target = 0.01f * (hud.adultLeft > 0.0f ? royale::kAdultScale : 1.0f);
        const float k = player->actor.scale.x + (target - player->actor.scale.x) * 0.15f;
        player->actor.scale.x = player->actor.scale.y = player->actor.scale.z = k;
    }
    UpdateSkydive(player, hud);
    UpdateLaunch(player);
    ReconcileLocalGlider(gSkydiving);
    if (gSkydiving) {   // hang from the glider's bar with both hands, like the puppets do
        LinkAnimationHeader* hang = RA(normal_jump_climb_wait);
        if (player->skelAnime.animation != (void*)hang) LinkAnimation_PlayLoop(gPlayState, &player->skelAnime, hang);
    }
    UpdateEmote(player, hud);
    // Safety net: a living player in a live match is never invisible (the flag is set only for emotes and eliminated spectators).
    if (gEmote.id < 0 && InField() && LiveAndAlive(hud)) player->stateFlags2 &= ~PLAYER_STATE2_DISABLE_DRAW;
    if (gSession.Joined() && IsLive(hud) && hud.selfAlive && InField()) HeldGlow(gPlayState, player, hud.weaponRarity, true);
    NoticePoi(player, hud);
    if (gActionFrames > 0) gActionFrames--;
    SyncLocalWeapon(player, hud);
    DriveFortnite(player, hud);
    EnsureSolidScenery();
    gAngryPlayerAlive = !(hud.haveSelf && !hud.selfAlive);
    ApplyPlatforms(player);
    ApplyRocks(player);
    HandleCombatInput(player, hud);
    if (hud.state == royale::MatchState::Ending && hud.isHost && (gPlayState->state.input[0].press.button & BTN_A)) gSession.RequestPlayAgain();
    UpdateSprint(player, hud);
    ApplySpeedBuffs(player, hud);
    UpdateLocalRide(player, hud);

    // The server owns health once the match is on. Overwrite the local value every frame so enemies, falls and the
    // game's own damage can't change it, and let a server-side elimination kill Link.
    bool dead = hud.haveSelf && !hud.selfAlive && (IsLive(hud) || hud.state == royale::MatchState::Ending);
    if (IsLive(hud) && hud.haveSelf) {
        if (!gHealthOverridden) {
            // Remember the player's real values so they can be put back afterwards (see RestoreHealth).
            gSavedCapacity = gSaveContext.healthCapacity;
            gSavedHealth = gSaveContext.health;
            gHealthOverridden = true;
        }
        gSaveContext.healthCapacity = static_cast<s16>(std::lround(hud.maxHealth * 16.0f));
        // An eliminated player does not die in the game (that would end in the game-over screen). They become an invisible,
        // invulnerable spectator who can still walk around and watch the rest of the match.
        // Never 0 while alive, even for a sliver of health or the tick where the server has zeroed health but not yet sent the
        // elimination: the game starts its own death and game-over screen the moment the save's health reads 0.
        gMatchHealth = hud.selfAlive ? static_cast<s16>(std::max(1L, std::lround(hud.selfHealth * 16.0f))) : gSaveContext.healthCapacity;
        gSaveContext.health = gMatchHealth;
        CancelGameDeath(player);
    }
    static bool wasDead = false;
    if (dead && !wasDead && InField()) {
        royale::PuppetState me;
        me.x = player->actor.world.pos.x; me.y = player->actor.world.pos.y; me.z = player->actor.world.pos.z;
        me.id = hud.selfId; me.rot = player->actor.shape.rot.y; me.weapon = hud.weapon; me.tunic = gLocalTunic; me.scene = static_cast<uint8_t>(gPlayState->sceneNum);
        const float a = me.rot * (3.14159265f / 32768.0f);
        SpawnCorpse(me, -std::sin(a), -std::cos(a));
    }
    if (dead && !wasDead) gSpectateTarget = kSpectateSelf;
    wasDead = dead;
    if (dead && gHealthOverridden) {
        player->stateFlags2 |= PLAYER_STATE2_DISABLE_DRAW;
        player->invincibilityTimer = 20;
        gSpectating = true;
        // The camera follows the invisible player, so watching someone means standing (unseen) on top of them.
        const u16 pressed = gPlayState->state.input[0].press.button;
        if (pressed & BTN_DLEFT) CycleSpectate(-1);
        if (pressed & BTN_DRIGHT) CycleSpectate(1);
        if (gSpectateTarget != kSpectateSelf && SpectateTarget() == nullptr) CycleSpectate(1); // who you watched is out
        if (const royale::PuppetState* t = SpectateTarget()) {
            player->actor.world.pos.x = t->x;
            player->actor.world.pos.z = t->z;
            player->actor.world.pos.y = t->isBot ? BotY(gPlayState, t->x, t->z, t->y) : t->y;
            player->actor.prevPos = player->actor.world.pos;
            player->actor.velocity.y = 0.0f;
            player->actor.shape.rot.y = t->rot;
        }
    } else if (gSpectating) {
        gSpectating = false; // the flag is per-frame, so it clears itself
    }
}

// Announce the moments players care about without needing the menu open.
void ReportEvents(const royale::HudState& hud) {
    for (const royale::ClientEvent& e : gSession.DrainEvents()) {
        auto nameOf = [&](uint16_t id) -> std::string {
            for (const auto& r : hud.roster) if (r.id == id) return r.name;
            if (royale::IsBossId(id)) return std::string("a mini boss");
            if (royale::IsVehicleId(id)) return std::string("a cart");
            return royale::BotName(id);
        };
        switch (e.type) {
            case royale::ClientEvent::Type::PlayerJoined:
                if (e.id != hud.selfId) {
                    // The roster entry arrives with the event, but this frame's HudState may predate it.
                    Say(nameOf(e.id) + " joined the lobby");
                }
                break;
            case royale::ClientEvent::Type::PlayerLeft:
                Say("A player left the lobby");
                break;
            case royale::ClientEvent::Type::Damaged: {
                if (!InGame()) break;
                Player* me = GET_PLAYER(gPlayState);
                const double now = ImGui::GetTime();
                auto swing = [&](uint16_t who) { if (who != hud.selfId && !royale::IsBossId(who)) gSwingFrames[who] = 10; };
                auto soundAt = [&](uint16_t who, u16 sfx) {
                    auto a = gActorOf.find(who);
                    if (a != gActorOf.end()) Audio_PlaySoundGeneral(sfx, &a->second->projectedPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                };
                {   // Homing bombchus leave a purple streak from the thrower to what they hit.
                    royale::ItemId weapon = royale::ItemId::BasicSword;
                    float fx = 0, fz = 0, tx2 = 0, tz2 = 0, fy = me->actor.world.pos.y;
                    bool haveFrom = false, haveTo = false;
                    if (e.other == hud.selfId) { weapon = hud.weapon; fx = me->actor.world.pos.x; fz = me->actor.world.pos.z; haveFrom = true; }
                    else if (gState.count(e.other)) { weapon = gState[e.other].weapon; fx = gState[e.other].x; fz = gState[e.other].z; haveFrom = true; }
                    if (e.id == hud.selfId) { tx2 = me->actor.world.pos.x; tz2 = me->actor.world.pos.z; haveTo = true; }
                    else if (gState.count(e.id)) { tx2 = gState[e.id].x; tz2 = gState[e.id].z; haveTo = true; }
                    if (haveTo) {   // fire, ice, light and nuts leave the game's own mark on whoever they hit
                        float ty0 = me->actor.world.pos.y;
                        if (e.id != hud.selfId) { auto tgt = gActorOf.find(e.id); if (tgt != gActorOf.end()) ty0 = tgt->second->world.pos.y; }
                        Vec3f hit = { tx2, ty0 + 35.0f, tz2 };
                        using royale::ItemId;
                        if (weapon == ItemId::FireArrows) {
                            for (int i = 0; i < 4; i++) { Vec3f p2 = { hit.x + (Rand_ZeroOne() - 0.5f) * 30.0f, hit.y + (Rand_ZeroOne() - 0.5f) * 40.0f, hit.z + (Rand_ZeroOne() - 0.5f) * 30.0f };
                                                          Color_RGBA8 pc = { 255, 200, 0, 255 }, ec = { 255, 0, 0, 255 }; Vec3f v = { 0, 1.5f, 0 }, ac = { 0, 0.05f, 0 };
                                                          EffectSsKiraKira_SpawnDispersed(gPlayState, &p2, &v, &ac, &pc, &ec, 110, 24); }
                            SparkBurst(gPlayState, hit.x, hit.y, hit.z, { 255, 120, 20, 255 }, 10, 3.0f);
                        } else if (weapon == ItemId::IceArrows) {
                            EffectSsIcePiece_SpawnBurst(gPlayState, &hit, 1.0f);
                        } else if (weapon == ItemId::LightArrows) {
                            EffectSsHitMark_SpawnFixedScale(gPlayState, 0, &hit);
                            SparkBurst(gPlayState, hit.x, hit.y, hit.z, { 255, 255, 170, 255 }, 14, 4.0f);
                        } else if (weapon == ItemId::DekuNuts) {
                            EffectSsHitMark_SpawnFixedScale(gPlayState, 0, &hit);
                        }
                    }
                    if (haveFrom && haveTo && weapon == royale::ItemId::HomingBombchus) {
                        for (int i = 0; i <= 14; i++) {
                            const float k = i / 14.0f;
                            SparkBurst(gPlayState, fx + (tx2 - fx) * k + (Rand_ZeroOne() - 0.5f) * 30.0f, fy + 25.0f + std::sin(k * 3.14159f) * 40.0f, fz + (tz2 - fz) * k + (Rand_ZeroOne() - 0.5f) * 30.0f, { 200, 100, 255, 255 }, 1, 0.8f);
                        }
                    }
                }
                // Whether the blow landed on a raised shield facing it: the server took most of it, and it rings off the shield.
                auto onShield = [&](bool shieldUp, int16_t rot, float tx, float tz) {
                    if (!shieldUp || e.other == royale::net::kNoPlayer16) return false;
                    float ax = 0, az = 0;
                    if (e.other == hud.selfId) { ax = me->actor.world.pos.x; az = me->actor.world.pos.z; }
                    else if (!KnownPosition(e.other, &ax, &az)) return false;
                    const s16 off = static_cast<s16>(static_cast<s16>(std::atan2(ax - tx, az - tz) * (32768.0f / 3.14159265f)) - rot);
                    return std::abs(static_cast<int>(off)) * (3.14159265f / 32768.0f) <= royale::kGuardHalfAngle;
                };
                if (e.id == hud.selfId && onShield((me->stateFlags1 & PLAYER_STATE1_SHIELDING) != 0, me->actor.shape.rot.y, me->actor.world.pos.x, me->actor.world.pos.z)) {
                    Audio_PlaySoundGeneral(NA_SE_IT_SHIELD_REFLECT_SW, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    {   // the spark the game shows when a blow rings off a shield
                        Vec3f at = { me->actor.world.pos.x + Math_SinS(me->actor.shape.rot.y) * 20.0f, me->actor.world.pos.y + 35.0f, me->actor.world.pos.z + Math_CosS(me->actor.shape.rot.y) * 20.0f };
                        EffectSsHitMark_SpawnFixedScale(gPlayState, EFFECT_HITMARK_METAL, &at);
                    }
                    gFloatingNumbers.push_back({ 0, 0, 0, e.amount, now, true });
                    if (e.other != royale::net::kNoPlayer16) { gIncomingHits.push_back({ e.other, now }); swing(e.other); }
                } else if (e.id != hud.selfId && gState.count(e.id) && gActorOf.count(e.id) &&
                           onShield(gState[e.id].anim == static_cast<uint8_t>(royale::Anim::Guard), gState[e.id].rot, gActorOf[e.id]->world.pos.x, gActorOf[e.id]->world.pos.z)) {
                    Actor* t = gActorOf[e.id];
                    if (e.other == hud.selfId) { gHitMarkerAt = now; gHitMarkerKill = e.health <= 0.001f; gFloatingNumbers.push_back({ t->world.pos.x, t->world.pos.y, t->world.pos.z, e.amount, now, false }); }
                    else swing(e.other);
                    if (std::hypot(t->world.pos.x - me->actor.world.pos.x, t->world.pos.z - me->actor.world.pos.z) < 1200.0f) soundAt(e.id, NA_SE_IT_SHIELD_REFLECT_SW);
                    Vec3f at = { t->world.pos.x + Math_SinS(t->shape.rot.y) * 20.0f, t->world.pos.y + 35.0f, t->world.pos.z + Math_CosS(t->shape.rot.y) * 20.0f };
                    EffectSsHitMark_SpawnFixedScale(gPlayState, EFFECT_HITMARK_METAL, &at);
                } else if (e.id == hud.selfId) {
                    // you were hit: flash, shake, a grunt, the sound of the blow, and an arrow towards the attacker
                    Actor_SetColorFilter(&me->actor, 0x4000, 0xFF, 0, 12);
                    Player_PlaySfx(&me->actor, NA_SE_VO_LI_DAMAGE_S);
                    Audio_PlaySoundGeneral(NA_SE_PL_BODY_HIT, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    gHurtAt = now; gHurtAmount = e.amount;
                    gFloatingNumbers.push_back({ 0, 0, 0, e.amount, now, true });
                    if (e.other != royale::net::kNoPlayer16) {
                        gIncomingHits.push_back({ e.other, now });
                        swing(e.other);
                        soundAt(e.other, NA_SE_IT_SWORD_SWING_HARD);
                    }
                } else {
                    float tx = 0, tz = 0;
                    const bool known = KnownPosition(e.id, &tx, &tz);
                    float ty = me->actor.world.pos.y;
                    auto target = gActorOf.find(e.id);
                    if (target != gActorOf.end()) { ty = target->second->world.pos.y; tx = target->second->world.pos.x; tz = target->second->world.pos.z; }
                    if (e.other == hud.selfId) {
                        // you hit someone: hit marker, their flash, the damage over their head, a clang
                        gHitMarkerAt = now;
                        gHitMarkerKill = e.health <= 0.001f;
                        if (known || target != gActorOf.end()) gFloatingNumbers.push_back({ tx, ty, tz, e.amount, now, false });
                        if (target != gActorOf.end()) Actor_SetColorFilter(target->second, 0x4000, 0xFF, 0, 14);
                        if (target != gActorOf.end() && e.health > 0.001f) gFlinchFrames[e.id] = 8;   // they reel from it
                        Audio_PlaySoundGeneral(NA_SE_IT_SWORD_STRIKE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    } else {
                        // two others fighting nearby: you can see the slash and hear the blow
                        swing(e.other);
                        if (target != gActorOf.end()) {
                            Actor_SetColorFilter(target->second, 0x4000, 0xFF, 0, 8);
                            if (e.health > 0.001f) gFlinchFrames[e.id] = 8;
                            if (std::hypot(tx - me->actor.world.pos.x, tz - me->actor.world.pos.z) < 1200.0f) soundAt(e.id, NA_SE_IT_SWORD_STRIKE);
                        }
                    }
                }
                break;
            }
            case royale::ClientEvent::Type::LootTaken:
                if (e.id != hud.selfId && gSession.Client() && e.index < gSession.Client()->Loot().size())
                    gLastFind[e.id] = { GidFor(static_cast<royale::ItemId>(gSession.Client()->Loot()[e.index].item)), ImGui::GetTime() };
                if (e.id == hud.selfId && gSession.Client() && e.index < gSession.Client()->Loot().size()) {
                    const auto& l = gSession.Client()->Loot()[e.index];
                    const royale::Rarity got = static_cast<royale::Rarity>(l.rarity);
                    const std::string label = LootLabel(l);
                    NotePickup(label, got, l.chest);
                    const royale::ItemId itemId = static_cast<royale::ItemId>(l.item);
                    float py = 0;
                    if (!FloorAt(l.x, l.z, &py)) py = GET_PLAYER(gPlayState)->actor.world.pos.y;
                    gPickupFx.push_back({ itemId, got, l.x, py, l.z, ImGui::GetTime() });
                    SparkBurst(gPlayState, l.x, py + 40.0f, l.z, RarityColor(got), 8 + 6 * static_cast<int>(got), 3.5f + 0.8f * static_cast<int>(got));
                    if (l.chest) {
                        StartAction(royale::Anim::ItemGet, 0.9f);   // everyone else sees you hold it up
                        Vec3f at = { l.x, py + 30.0f, l.z };
                        Audio_PlaySoundGeneral(NA_SE_EV_TBOX_OPEN, &at, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    }
                    Audio_PlaySoundGeneral(NA_SE_SY_GET_ITEM, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    if (itemId == royale::ItemId::HeartContainer) Audio_PlayFanfare(NA_BGM_HEART_GET);
                    else if (got >= royale::Rarity::Epic) Audio_PlayFanfare(NA_BGM_ITEM_GET);
                    else if (got == royale::Rarity::Rare) Audio_PlayFanfare(NA_BGM_SMALL_ITEM_GET);
                }
                break;
            case royale::ClientEvent::Type::BossDown: {
                const int kind = gBossKindSeen.count(e.id) ? gBossKindSeen[e.id] : 0;
                const std::string killer = e.other == hud.selfId ? std::string("You") : nameOf(e.other);
                AddFeed(killer + " defeated the " + royale::kBossDefs[kind].name, IM_COL32(255, 200, 120, 255));
                break;
            }
            case royale::ClientEvent::Type::SupplyDrop: {
                ShowBanner("Supply drop incoming", IM_COL32(255, 150, 60, 255), 3.0f);
                gSupplyMarks.push_back({ e.x, e.z, ImGui::GetTime() + 60.0 });
                Audio_PlaySoundGeneral(NA_SE_EV_FIRE_PILLAR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                break;
            }
            case royale::ClientEvent::Type::PropBroken: {
                if (e.id != hud.selfId && InGame() && gSession.Client() && e.index < gSession.Client()->Props().size()) {
                    // somebody else cut a bush or broke a rock near you: you see and hear it go
                    const auto& pr = gSession.Client()->Props()[e.index];
                    Player* me = GET_PLAYER(gPlayState);
                    float py = me->actor.world.pos.y;
                    if (std::hypot(pr.pos.x - me->actor.world.pos.x, pr.pos.z - me->actor.world.pos.z) < kPuppetHearing && FloorAt(pr.pos.x, pr.pos.z, &py)) {
                        const bool bush = pr.kind == royale::PropKind::Bush;
                        SparkBurst(gPlayState, pr.pos.x, py + 20.0f, pr.pos.z, bush ? Color_RGBA8{ 90, 220, 90, 255 } : Color_RGBA8{ 190, 190, 180, 255 }, 8, 3.0f);
                        Vec3f at = { pr.pos.x, py + 20.0f, pr.pos.z };
                        Audio_PlaySoundGeneral(bush ? NA_SE_EV_PLANT_BROKEN : NA_SE_EV_ROCK_BROKEN, &at, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    }
                }
                if (e.id != hud.selfId || !InGame() || !gSession.Client()) break;
                const auto& props = gSession.Client()->Props();
                float py = GET_PLAYER(gPlayState)->actor.world.pos.y, px = GET_PLAYER(gPlayState)->actor.world.pos.x, pz = GET_PLAYER(gPlayState)->actor.world.pos.z;
                if (e.index < props.size()) { px = props[e.index].pos.x; pz = props[e.index].pos.z; FloorAt(px, pz, &py); }
                if (e.item == 255) { SparkBurst(gPlayState, px, py + 25.0f, pz, { 150, 150, 150, 255 }, 4, 2.0f); break; } // nothing inside
                const royale::ItemId item = static_cast<royale::ItemId>(e.item);
                const bool money = item == royale::ItemId::Rupees;
                const std::string what = std::string("+") + std::to_string(e.count) + " " + ItemName(item);
                ShowGain(what, money ? (e.count >= 20 ? IM_COL32(255, 90, 90, 255) : e.count >= 5 ? IM_COL32(110, 170, 255, 255) : IM_COL32(110, 240, 130, 255)) : IM_COL32(255, 220, 120, 255));
                SparkBurst(gPlayState, px, py + 25.0f, pz, money ? Color_RGBA8{ 120, 255, 140, 255 } : Color_RGBA8{ 255, 220, 120, 255 }, 7, 3.0f);
                Audio_PlaySoundGeneral(money ? NA_SE_SY_GET_RUPY : NA_SE_SY_GET_ITEM, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                break;
            }
            case royale::ClientEvent::Type::BossSpawned: {
                const char* name = royale::kBossDefs[std::min<int>(e.item, royale::kBossKindCount - 1)].name;
                ShowBanner(std::string(name) + " has arrived!", IM_COL32(255, 120, 80, 255), 3.4f);
                Audio_PlaySoundGeneral(BossArrivalSound(e.item), &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                break;
            }
            case royale::ClientEvent::Type::Strike:
                AddStrikeFx(e.x, e.z, e.amount, e.health, e.id == royale::net::kNoPlayer16 ? static_cast<uint8_t>(royale::StrikeStyle::Bolt) : e.item, e.id);
                break;
            case royale::ClientEvent::Type::AllyChanged: {
                const char* name = royale::kAllyDefs[std::min<int>(e.index, royale::kAllyCount - 1)].name;
                if (e.item == 0) {
                    if (e.id == hud.selfId) ShowBanner(std::string("The ") + name + " joins you!", IM_COL32(130, 255, 150, 255), 2.4f);
                } else if (e.item != 1) {
                    auto it = gAllies.find(static_cast<uint8_t>(e.index));
                    if (it != gAllies.end() && it->second.actor != nullptr && gPlayState != nullptr)
                        SparkBurst(gPlayState, it->second.x, it->second.actor->world.pos.y + 60.0f, it->second.z, { 220, 220, 255, 255 }, 24, 5.0f);
                }
                break;
            }
            case royale::ClientEvent::Type::AllyAction:
                AllyActionFx(e, hud);
                break;
            case royale::ClientEvent::Type::WeatherChanged: {
                const auto sky = static_cast<royale::Sky>(e.item);
                if (!gSeasonAnnounced) {
                    gSeasonAnnounced = true;
                    const auto season = static_cast<royale::Season>(e.id & 3);
                    ShowBanner(std::string(royale::SeasonName(season)) + ": " + royale::SeasonBlurb(season), IM_COL32(255, 235, 170, 255), 3.4f);
                } else if (sky != royale::Sky::Clear) {
                    ShowBanner(std::string(royale::SkyName(sky)) + " rolls in", IM_COL32(190, 210, 255, 255), 2.8f);
                } else {
                    ShowBanner("The sky clears", IM_COL32(255, 235, 170, 255), 2.4f);
                }
                break;
            }
            case royale::ClientEvent::Type::MapChanged:
                gBrokenProps.clear();   // a new match (or a rematch): all the scenery is back
                gSeasonAnnounced = false;
                gPickupLog.clear();
                break;
            case royale::ClientEvent::Type::AbilityUsed:
                if (e.item < royale::kItemCount) AbilityFx(e.id, static_cast<royale::ItemId>(e.item), e.id == hud.selfId, e.x, e.z);
                if (e.id == hud.selfId && e.item == static_cast<uint8_t>(royale::ItemId::ShockwaveGrenade) && InField())
                    LaunchSelf(GET_PLAYER(gPlayState), static_cast<royale::Rarity>(hud.inv.ability.rarity));
                if (e.item == royale::net::kRevivedItem) {
                    if (e.id == hud.selfId) ShowBanner("A Fairy saved you!", IM_COL32(255, 170, 215, 255), 2.4f);
                }
                break;
            case royale::ClientEvent::Type::Eliminated: {
                const bool me = e.id == hud.selfId, mine = e.other == hud.selfId;
                if (InGame()) {   // Link's cry as he goes down
                    auto victim = gActorOf.find(e.id);
                    if (victim != gActorOf.end() && victim->second != nullptr) PuppetVoice((Player*)victim->second, NA_SE_VO_LI_DOWN);
                }
                // The server stops sending eliminated players, so their puppet just vanishes: leave their body here instead, thrown away
                // from whoever got them (or backwards, for the storm). Your own body is made in OnPlayerUpdate.
                // (A player who was only just seen, or just went out of range, is found in gLastSeen: any kind of death leaves a body.)
                const royale::PuppetState* known = gState.count(e.id) ? &gState[e.id] : gLastSeen.count(e.id) ? &gLastSeen[e.id] : nullptr;
                if (!me && InField() && !royale::IsBossId(e.id) && known != nullptr && known->scene == gPlayState->sceneNum) {
                    royale::PuppetState body = *known;
                    auto actor = gActorOf.find(e.id);
                    if (actor != gActorOf.end() && actor->second != nullptr) {   // where it is drawn right now, not the last network sample
                        body.x = actor->second->world.pos.x; body.y = actor->second->world.pos.y; body.z = actor->second->world.pos.z;
                    }
                    const float a = body.rot * (3.14159265f / 32768.0f);
                    float px = -std::sin(a), pz = -std::cos(a);
                    const Player* self = GET_PLAYER(gPlayState);
                    float kx = 0, kz = 0;
                    bool haveKiller = false;
                    if (mine) { kx = self->actor.world.pos.x; kz = self->actor.world.pos.z; haveKiller = true; }
                    else if (gState.count(e.other)) { kx = gState[e.other].x; kz = gState[e.other].z; haveKiller = true; }
                    if (haveKiller && std::hypot(body.x - kx, body.z - kz) > 1.0f) { px = body.x - kx; pz = body.z - kz; }
                    SpawnCorpse(body, px, pz);
                }
                const std::string victim = me ? std::string("You") : nameOf(e.id);
                std::string line;
                if (e.other == royale::net::kNoPlayer16) line = victim + (me ? " were" : " was") + " caught by the storm";
                else if (royale::IsBossId(e.other)) line = victim + (me ? " were" : " was") + " defeated by the " + royale::kBossDefs[gBossKindSeen.count(e.other) ? gBossKindSeen[e.other] : 0].name;
                else line = (mine ? std::string("You") : nameOf(e.other)) + " eliminated " + victim;
                AddFeed(line, me ? IM_COL32(255, 110, 110, 255) : mine ? IM_COL32(255, 220, 90, 255) : IM_COL32(230, 230, 235, 255));
                if (me) {
                    ShowBanner("ELIMINATED  -  #" + std::to_string(std::max(1, hud.alive)), IM_COL32(255, 110, 110, 255), 4.0f);
                    gSpectateTarget = kSpectateSelf;
                    for (const auto& st : gSession.Puppets()) if (st.alive && st.id == e.other) gSpectateTarget = st.id;   // watch whoever got you
                }
                break;
            }
            case royale::ClientEvent::Type::StateChanged:
                break;   // the big centre text already says what happened (see DrawOverlay)
            default:
                break;
        }
    }
}

// The host's Start does three things in order: get to Hyrule Field, measure the real playable area and rebuild the world on it
// (loot, spawns and storm on ground that exists), then begin.
void DriveStart(const royale::HudState& hud) {
    if (gSoloStartWanted) {   // the solo test needs no one else: press Start for the host as soon as they are in the lobby
        if (!gSession.Joined() || !gSession.SoloTest()) gSoloStartWanted = false;
        else if (hud.isHost && hud.state == royale::MatchState::Lobby && hud.haveSelf && InGame() && !gPendingStart) { gPendingStart = true; gSoloStartWanted = false; }
    }
    if (!gPendingStart) return;
    if (!gSession.Joined() || !hud.isHost || hud.state != royale::MatchState::Lobby) { gPendingStart = false; return; }
    if (!InGame()) return;
    if (!InField()) { Trace("start: going to the field"); WantsWaitingRoom = false; GoToField(); gInFieldFrames = 0; return; }
    if (gPlayState->transitionTrigger != TRANS_TRIGGER_OFF || ++gInFieldFrames < royale::kTickHz) return; // let the scene settle
    Trace("start: measuring the field");
    royale::Circle measured;
    if (MeasureField(&measured)) {
        Trace("start: configuring the map");
        gSession.ConfigureMap(measured, WalkableAt, [](royale::Vec2 p, float* y) { return RawFloorAt(p.x, p.z, y); });   // the bots learn the ledges and cliffs
    }
    Trace("start: starting the match");
    gSession.StartMatch();
    Trace("start: match started");
    gPendingStart = false;
}

// ---- Battle Royale save files, the lobby timer, and the time of day --------------------------------------------------------------

bool gWasInGame = false;
int gMenuOpenCountdown = -1;
int gLobbyAnnounced = 1 << 30;

// Open the game's own menu on the Battle Royale page.
void OpenRoyaleMenu() {
    CVarSetString(CVAR_SETTING("Menu.ActiveHeader"), "Battle Royale");
    if (SohGui::mSohMenu && !SohGui::mSohMenu->IsVisible()) SohGui::mSohMenu->ToggleVisibility();
}

// A save made with the "Battle Royale" quest option opens the Battle Royale menu a few seconds after it loads, so the mode starts from
// there: host a lobby or join one, with no digging through the menus. (The quest option is in the file select; see patches/0008.)
void NoticeRoyaleFile() {
    const bool in = InGame();
    if (in && !gWasInGame) {
        char key[48];
        std::snprintf(key, sizeof(key), CVAR_SETTING("Royale.BRFile%d"), static_cast<int>(gSaveContext.fileNum));
        if (CVarGetInteger(key, 0) != 0 && !gSession.Joined()) gMenuOpenCountdown = 60; // about 3 seconds
    }
    gWasInGame = in;
    if (gMenuOpenCountdown > 0 && --gMenuOpenCountdown == 0) {
        if (InGame() && !gSession.Joined()) {
            OpenRoyaleMenu();
            Say("Battle Royale: host a lobby or join one from this menu");
        }
        gMenuOpenCountdown = -1;
    }
}

// The lobby counts down on the server. At zero the host's game does what the Start button does (it has to go to the field and measure the
// map first); the server starts the match by itself if that never happens.
void DriveLobbyTimer(const royale::HudState& hud) {
    if (!gSession.Joined() || hud.state != royale::MatchState::Lobby || hud.lobbyLeft < 0) { gLobbyAnnounced = 1 << 30; return; }
    const int left = static_cast<int>(std::ceil(hud.lobbyLeft));
    for (int mark : { 60, 30, 10 }) {
        if (left <= mark && gLobbyAnnounced > mark) Say("The match starts automatically in " + std::to_string(mark) + " seconds");
    }
    gLobbyAnnounced = left;
    if (hud.isHost && hud.lobbyLeft <= 0.05f && !gPendingStart && InGame()) {
        Say("Time is up: starting the match");
        gPendingStart = true;
    }
}

// A match runs from morning to night: the game's own day and night lighting (sky, sun and ambient light) is driven by how far the storm
// has got, so the last circles are fought at dusk and in the dark. The player's own time of day is put back afterwards.
bool gTimeTaken = false;
u16 gSavedDayTime = 0;
void DriveTimeOfDay(const royale::HudState& hud) {
    const bool on = gSession.Joined() && InField() && (hud.state == royale::MatchState::Drop || hud.state == royale::MatchState::InMatch) && gSession.Client();
    if (on) {
        if (!gTimeTaken) { gSavedDayTime = gSaveContext.dayTime; gTimeTaken = true; }
        float total = 0;
        for (const auto& ph : royale::kStormPhases) total += ph.waitSec + ph.closeSec;
        const float p = std::clamp(gSession.Client()->StormTime() / std::max(1.0f, total), 0.0f, 1.0f);
        int t = 0x5000 + static_cast<int>(p * 0x9000); // about 7:30 in the morning to about 9 at night
        const float dark = std::max(gStormWeather * 0.85f, royale::SkyDarkness(gWeatherShown) * gWeatherBlend);   // the storm zone and heavy weather both darken the sky
        if (t < 0xD400) t += static_cast<int>((0xD400 - t) * dark);
        gSaveContext.dayTime = static_cast<u16>(t);
        gSaveContext.skyboxTime = static_cast<u16>(t);
    } else if (gTimeTaken) {
        gSaveContext.dayTime = gSavedDayTime;
        gSaveContext.skyboxTime = gSavedDayTime;
        gTimeTaken = false;
    }
}

// No way out. Every door, cave mouth and map edge that would load another scene is sealed while you are in a lobby or match: the game's
// request to change scene is cancelled the moment it is made (the only scene changes allowed are our own trips between the waiting room
// and the map), and Link is put back on the ground inside the map, facing the way he came from. That also catches falling into a void.
void SealExits(const royale::HudState& hud) {
    if (!gSession.Joined() || !InGame() || gOurTravel) return;
    if (!(InField() || InWaitingRoom())) return;
    if (gPlayState->transitionTrigger == TRANS_TRIGGER_OFF) return;
    Player* player = GET_PLAYER(gPlayState);
    gPlayState->transitionTrigger = TRANS_TRIGGER_OFF;
    gPlayState->transitionMode = TRANS_MODE_OFF;
    player->stateFlags1 &= ~PLAYER_STATE1_LOADING;
    float tx = hud.map.center.x, tz = hud.map.center.z;
    if (!InField() || hud.map.radius <= 0) { tx = player->actor.home.pos.x; tz = player->actor.home.pos.z; }
    float dx = tx - player->actor.world.pos.x, dz = tz - player->actor.world.pos.z;
    const float len = std::max(1.0f, std::hypot(dx, dz));
    dx /= len; dz /= len;
    float x = player->actor.world.pos.x + dx * 180.0f, z = player->actor.world.pos.z + dz * 180.0f;
    if (InField()) { x = player->actor.world.pos.x + dx * std::min(len, 220.0f); z = player->actor.world.pos.z + dz * std::min(len, 220.0f); }
    player->actor.world.pos.x = x;
    player->actor.world.pos.z = z;
    player->actor.world.pos.y = GroundY(gPlayState, x, z, player->actor.world.pos.y + 20.0f) + 5.0f;
    player->actor.prevPos = player->actor.world.pos;
    player->actor.velocity = { 0.0f, 0.0f, 0.0f };
    player->actor.speedXZ = 0.0f;
    player->actor.shape.rot.y = player->actor.world.rot.y = static_cast<s16>(std::atan2(dx, dz) * (32768.0f / 3.14159265f));
    static double lastNote = -100.0;
    if (ImGui::GetTime() - lastNote > 3.0) { Say("You can't leave the battle area"); lastNote = ImGui::GetTime(); }
}

// One minimap, not two: while a match is live the game's own field map is switched off (its switch is the same one the L button flips) and the
// Royale map takes its place.
bool gMinimapSwitched = false;
s16 gSavedMinimapDisabled = 0;
void DriveMinimapSwitch(bool on) {
    if (on) {
        if (!gMinimapSwitched) { gSavedMinimapDisabled = R_MINIMAP_DISABLED; gMinimapSwitched = true; }
        R_MINIMAP_DISABLED = 1;
    } else if (gMinimapSwitched) {
        R_MINIMAP_DISABLED = gSavedMinimapDisabled;
        gMinimapSwitched = false;
    }
}

// ---- storm alerts ------------------------------------------------------------------------------------------------------------
// A jingle (with a banner) whenever the storm changes phase, a siren fifteen and five seconds before the zone starts to close, and a siren every
// few seconds while you are standing in the storm. The sounds are synthesised in shared/tune.h and played on a small audio stream of their own.
void PlayOneShot(int kind) {   // 0 the storm warning, 1 the storm jingle, 2 Lilo's accident
    static const auto jingle = std::make_shared<const std::vector<int16_t>>(royale::BuildStormJingle());
    static const auto warning = std::make_shared<const std::vector<int16_t>>(royale::BuildStormWarning());
    static const auto fart = std::make_shared<const std::vector<int16_t>>(royale::BuildFart());
    const float volume = GameVolume(false);
    if (volume < 0.01f) return;
    StartVoice(kVoiceOneShot, kind == 1 ? jingle : kind == 2 ? fart : warning, false, royale::kTuneRate, false, volume);
}

void DriveStormAlerts(const royale::HudState& hud) {
    static int lastPhase = -1;
    static bool lastShrinking = false;
    static int warned = -1;            // the last countdown warning given for this hold (15 or 5)
    static double nextOutsideSiren = 0;
    if (!gSession.Joined() || hud.state != royale::MatchState::InMatch) { lastPhase = -1; warned = -1; return; }
    const double now = ImGui::GetTime();
    if (lastPhase < 0) { lastPhase = hud.stormPhase; lastShrinking = hud.stormShrinking; return; }
    if (hud.stormPhase != lastPhase || hud.stormShrinking != lastShrinking) {
        if (hud.stormPhase >= royale::kStormPhaseCount) {
            ShowBanner("THE STORM HAS TAKEN THE MAP", IM_COL32(190, 120, 255, 255), 3.2f);
        } else if (hud.stormShrinking) {
            ShowBanner("The storm is closing in!", IM_COL32(190, 120, 255, 255), 3.0f);
        } else {
            ShowBanner("New safe zone marked", IM_COL32(210, 190, 255, 255), 2.4f);
        }
        PlayOneShot(1);
        warned = -1;
        lastPhase = hud.stormPhase;
        lastShrinking = hud.stormShrinking;
    }
    if (!hud.stormShrinking && hud.stormPhase < royale::kStormPhaseCount) {
        const int left = static_cast<int>(std::ceil(hud.stormSecondsLeft));
        if (left <= 15 && left > 5 && warned < 15) {
            warned = 15;
            PlayOneShot(0);   // the zone timer (top right) shows the seconds; the sound is the warning
        } else if (left <= 5 && left > 0 && warned < 5) {
            warned = 5;
            ShowBanner("The storm closes in 5 seconds!", IM_COL32(255, 110, 90, 255), 2.2f);
            PlayOneShot(0);
        }
    }
    if (hud.selfAlive && hud.stormDamagePerSecond > 0 && now >= nextOutsideSiren) { // you are in it: keep nagging (the haze and the red compass say where; no text)
        nextOutsideSiren = now + 4.0;
        PlayOneShot(0);
    }
}

// ---- talking: the game's own text box --------------------------------------------------------------------------------------------
// Lilo, Maya and the sign speak through the game's real message system: the same text box, font, letter-by-letter typing and sounds as every
// NPC. Their lines are custom messages in a "RoyaleMod" table (text ids from 0x7F00, which patches/0012 looks up), and they offer to talk the
// way the game's own NPCs do, so the A button says "Speak", Link turns to face them and the box opens, waits for A and closes as usual.
constexpr u16 kTextLilo = 0x7F00;       // Lilo sitting on the map
constexpr u16 kTextLiloPet = 0x7F01;    // Lilo the pet: 0x7F01 onwards, one per line in royale::kLiloPetLines
constexpr u16 kTextMaya = 0x7F10;
constexpr u16 kTextSign = 0x7F20;
static_assert(kTextLiloPet + royale::kLiloPetLineCount <= kTextMaya, "Lilo's pet lines run into Maya's text id");
bool gRoyaleMessagesMade = false;

void RegisterRoyaleMessages() {
    if (gRoyaleMessagesMade || CustomMessageManager::Instance == nullptr) return;
    CustomMessageManager* cm = CustomMessageManager::Instance;
    cm->AddCustomMessageTable("RoyaleMod");
    cm->CreateMessage("RoyaleMod", kTextLilo, CustomMessage(royale::kLiloLine));
    for (int i = 0; i < royale::kLiloPetLineCount; i++)
        cm->CreateMessage("RoyaleMod", static_cast<u16>(kTextLiloPet + i), CustomMessage(royale::kLiloPetLines[i]));
    cm->CreateMessage("RoyaleMod", kTextMaya, CustomMessage(royale::kMayaGreeting));
    cm->CreateMessage("RoyaleMod", kTextSign, CustomMessage(royale::kMapSignText, TEXTBOX_TYPE_WOODEN));
    gRoyaleMessagesMade = true;
}

// True while the text box is open for this actor.
bool TalkingTo(const Actor* actor) {
    return actor != nullptr && gPlayState != nullptr && gPlayState->msgCtx.msgMode != MSGMODE_NONE && gPlayState->msgCtx.talkActor == actor;
}

// Called from an actor's update: offers Link a talk within `range` (he takes it with A, like with any NPC). True on the frame the talk starts.
bool OfferTalk(Actor* actor, PlayState* play, u16 textId, float range) {
    actor->textId = textId;
    if (Actor_ProcessTalkRequest(actor, play)) return true;
    func_8002F2CC(actor, play, range);   // the game's "offer to talk" (Actor_OfferTalk in the decompilation)
    return false;
}

bool MessageBoxUp() { return gPlayState != nullptr && gPlayState->msgCtx.msgMode != MSGMODE_NONE; }

// Opens the box directly, for an A press the game did not take as a talk (Link facing the other way, say). Does nothing if a box is already up.
bool StartTalk(Actor* actor, u16 textId) {
    if (actor == nullptr || MessageBoxUp()) return false;
    Message_StartTextbox(gPlayState, textId, actor);
    return true;
}

// ---- Maya ---------------------------------------------------------------------------------------------------------------------
// A little Kokiri called Maya stands in a random spot on every map (the same spot for everyone in the match). Walk up and press A to talk to her.
Actor* gMayaActor = nullptr;
royale::Vec2 gMayaPos = {};
bool gMayaKnown = false;

void TalkToMaya();
void Maya_Update(Actor* actor, PlayState* play) {
    Player* pl = GET_PLAYER(play);
    const float dx = pl->actor.world.pos.x - actor->world.pos.x, dz = pl->actor.world.pos.z - actor->world.pos.z;
    if (dx * dx + dz * dz < 700.0f * 700.0f) {   // she turns to look at you as you come close
        const s16 want = static_cast<s16>(std::atan2(dx, dz) * (32768.0f / 3.14159265f));
        actor->shape.rot.y = static_cast<s16>(actor->shape.rot.y + static_cast<s16>(want - actor->shape.rot.y) * 0.12f);
    }
    actor->focus.pos = actor->world.pos;
    actor->focus.pos.y += 60.0f;
    if (OfferTalk(actor, play, kTextMaya, royale::kHireRange)) TalkToMaya();
}
void Maya_Draw(Actor* actor, PlayState* play) {
    const GpuMesh* mesh = GpuMeshFor(royale::MeshKind::Ally, 0);
    if (mesh == nullptr || mesh->dl.empty()) return;
    const float t = static_cast<float>(play->gameplayFrames) / royale::kTickHz;
    const bool talking = TalkingTo(actor);
    const float hop = talking ? std::fabs(std::sin(t * 9.0f)) * 16.0f : std::fabs(std::sin(t * 2.2f)) * 3.0f;   // she bounces when she talks
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y + hop, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(actor->shape.rot.y * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    Matrix_RotateZ(talking ? std::sin(t * 7.0f) * 0.1f : 0.0f, MTXMODE_APPLY);
    Matrix_Scale(0.78f, 0.78f, 0.78f, MTXMODE_APPLY);   // she is small
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(mesh->dl.data()));
    CLOSE_DISPS(play->state.gfxCtx);
}
void Maya_Destroy(Actor* actor, PlayState*) { if (gMayaActor == actor) gMayaActor = nullptr; }

// Her spot: random but the same for everybody in the same match (it comes from the map's own numbers), on open walkable ground, clear of walls and
// rocks and well away from the sign.
bool FindMayaSpot(const royale::Circle& map, royale::Vec2* out) {
    if (!gSession.Client()) return false;
    const auto& props = gSession.Client()->Props();
    uint64_t seed = gSession.Client()->Seed() ^ 0x4D617961ull;   // "Maya"
    royale::Rng rng(seed);
    auto clear = [&](royale::Vec2 p) {
        if (!WalkableAt(p)) return false;
        if (gSignKnown && royale::Distance(p, gSignPos) < 500.0f) return false;
        for (const royale::Prop& pr : props) {
            const float r = royale::PropRadius(pr.kind);
            if (r > 0.0f && royale::Distance(pr.pos, p) < r + 100.0f) return false;
        }
        return true;
    };
    for (int attempt = 0; attempt < 400; attempt++) {
        const float a = static_cast<float>(rng.Unit() * 6.2831853), d = map.radius * (0.2f + 0.5f * static_cast<float>(rng.Unit()));
        const royale::Vec2 p = { map.center.x + std::cos(a) * d, map.center.z + std::sin(a) * d };
        if (clear(p)) { *out = p; return true; }
    }
    return false;
}

void ReconcileMaya(const royale::HudState& hud) {
    const bool want = gSession.Joined() && InField() && hud.map.radius > 0 &&
                      (hud.state == royale::MatchState::Countdown || hud.state == royale::MatchState::Drop || hud.state == royale::MatchState::InMatch);
    if (!want) {
        if (gMayaActor != nullptr) { Actor_Kill(gMayaActor); gMayaActor = nullptr; }
        gMayaKnown = false;
        return;
    }
    if (gMayaActor != nullptr) return;
    if (!gMayaKnown) { if (!FindMayaSpot(hud.map, &gMayaPos)) return; gMayaKnown = true; }
    float y = 0;
    if (!FloorAt(gMayaPos.x, gMayaPos.z, &y)) return;
    Actor* a = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ISHI, gMayaPos.x, y, gMayaPos.z, 0, 0x4000, 0, 0, false);
    if (a == nullptr) return;
    a->update = Maya_Update;
    a->draw = Maya_Draw;
    a->destroy = Maya_Destroy;
    a->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
    a->uncullZoneForward = 4000.0f; a->uncullZoneScale = 1500.0f; a->uncullZoneDownward = 1500.0f;
    a->shape.shadowScale = 30.0f;
    gMayaActor = a;
}

bool MayaNear() {
    if (gMayaActor == nullptr || !InField()) return false;
    Player* pl = GET_PLAYER(gPlayState);
    return std::hypot(pl->actor.world.pos.x - gMayaPos.x, pl->actor.world.pos.z - gMayaPos.z) < royale::kHireRange;
}

// The talk itself is the game's text box; this is the sparkle that goes with it.
void TalkToMaya() {
    if (gPlayState != nullptr && gMayaActor != nullptr)
        SparkBurst(gPlayState, gMayaPos.x, gMayaActor->world.pos.y + 90.0f, gMayaPos.z, { 255, 190, 220, 255 }, 16, 3.0f);
}

// Her name over her head (what she says is in the game's own text box).
void DrawMaya(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale) {
    (void)ds;
    if (gMayaActor == nullptr || !InField()) return;
    Player* pl = GET_PLAYER(gPlayState);
    const float d = std::hypot(pl->actor.world.pos.x - gMayaPos.x, pl->actor.world.pos.z - gMayaPos.z);
    ImVec2 at;
    if (d < 1800.0f && WorldToScreen(gMayaPos.x, gMayaActor->world.pos.y + 190.0f, gMayaPos.z, &at)) {
        const float size = std::clamp(24.0f * scale * (1800.0f / (d + 900.0f)), 13.0f * scale, 28.0f * scale);
        const char* label = "Maya";
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label);
        dl->AddText(font, size, ImVec2(at.x - sz.x * 0.5f + 2, at.y + 2), IM_COL32(40, 10, 30, 230), label);
        dl->AddText(font, size, ImVec2(at.x - sz.x * 0.5f, at.y), IM_COL32(255, 170, 215, 255), label);
    }
}

// ---- Lilo's model -------------------------------------------------------------------------------------------------------------------
// Lilo is a low poly model made in Blender (tools/lilo/, assets/lilo/lilo.blend) in the N64 style: about 650 triangles, a 64x32 fur texture and a
// 32x32 face in three versions (eyes open, half shut, shut), a skeleton of 25 bones and eleven animation clips: idle, walk, run, jump, sit, talk,
// groom, sleep, stretch, pounce and happy (shared/lilo_model.h). She is skinned on the CPU every frame (the way the game itself skins Epona), lit by
// a fixed sun baked into the vertex colours, and drawn with her own two textures.
constexpr float kLiloPetScale = 0.8f;   // the model stands about 50 units tall at 1.0 (sitting, ears up, about 52); Link is about 60
constexpr float kLiloMapScale = 1.0f;

void DrawLiloModel(PlayState* play, float x, float y, float z, float yaw, float scale, const royale::lilo::Pose& pose, int eyes) {
    namespace L = royale::lilo;
    constexpr float kSub = 8.0f;   // vertices go to the graphics chip in 1/8 units, so the small model keeps its shape
    Vtx* vtx = static_cast<Vtx*>(Graph_Alloc(play->state.gfxCtx, sizeof(Vtx) * L::kVertCount));
    if (vtx == nullptr) return;
    // The sun: high, a little in front and to one side, turned into the model's own axes (the inverse of Matrix_RotateY(yaw)).
    const float wx = 0.35f, wy = 0.82f, wz = 0.45f;
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    const float lx = wx * cy - wz * sy, ly = wy, lz = wx * sy + wz * cy;
    for (int i = 0; i < L::kVertCount; i++) {
        const L::Vert& v = L::kVerts[i];
        float p[3], n[3];
        L::SkinVertex(pose, v, p, n);
        const float lit = std::clamp(0.52f + 0.58f * std::max(0.0f, n[0] * lx + n[1] * ly + n[2] * lz), 0.0f, 1.0f);
        Vtx& o = vtx[i];
        for (int k = 0; k < 3; k++) o.v.ob[k] = static_cast<s16>(std::lround(std::clamp(p[k] * kSub, -32000.0f, 32000.0f)));
        o.v.flag = 0;
        o.v.tc[0] = v.s;
        o.v.tc[1] = v.t;
        o.v.cn[0] = static_cast<u8>(255.0f * lit);
        o.v.cn[1] = static_cast<u8>(250.0f * lit);
        o.v.cn[2] = static_cast<u8>(242.0f * lit);
        o.v.cn[3] = 255;
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);         // the light is already in the vertex colours
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIDECALA, G_CC_PASS2);     // her texture times the vertex colour
    Matrix_Translate(x, y, z, MTXMODE_NEW);
    Matrix_RotateY(yaw, MTXMODE_APPLY);
    Matrix_Scale(scale / kSub, scale / kSub, scale / kSub, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    const int face = std::clamp(eyes, 0, 2);
    int loaded = -1;
    for (int b = 0; b < L::kBatchCount; b++) {
        const L::Batch& bt = L::kBatches[b];
        const int tex = bt.texture == L::kFace ? 1 + face : 0;
        if (tex != loaded) {
            if (tex == 0)
                gDPLoadTextureBlock(POLY_OPA_DISP++, L::kFurTex, G_IM_FMT_RGBA, G_IM_SIZ_16b, L::kFurW, L::kFurH, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                                    G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            else
                gDPLoadTextureBlock(POLY_OPA_DISP++, L::kFaceTex[tex - 1], G_IM_FMT_RGBA, G_IM_SIZ_16b, L::kFaceW, L::kFaceH, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                                    G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            loaded = tex;
        }
        gSPVertex(POLY_OPA_DISP++, reinterpret_cast<uintptr_t>(&vtx[bt.firstVert]), bt.vertCount, 0);
        const int end = bt.firstTri + bt.triCount;
        int t = bt.firstTri;
        for (; t + 1 < end; t += 2)
            gSP2Triangles(POLY_OPA_DISP++, L::kTris[t][0], L::kTris[t][1], L::kTris[t][2], 0, L::kTris[t + 1][0], L::kTris[t + 1][1], L::kTris[t + 1][2], 0);
        if (t < end) gSP1Triangle(POLY_OPA_DISP++, L::kTris[t][0], L::kTris[t][1], L::kTris[t][2], 0);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// Her eyes: what the mood wants, with a quick blink every few seconds while they are open.
int LiloEyes(float seconds, int wanted) {
    if (wanted != royale::lilo::kEyesOpen) return wanted;
    return std::fmod(seconds, 3.7f) < 0.13f ? royale::lilo::kEyesShut : royale::lilo::kEyesOpen;
}

// -- Lilo's voice and her accidents (shared by the pet and the Lilo on the map) --
// Her mews are seven short recordings of the real Lilo (assets/lilo/sounds, shared/lilo_sounds.h), on a mixer voice of their own so a mew never
// cuts off a storm siren. Which one plays depends on what she is doing:
enum LiloMew { kMewChirp, kMewLong, kMewTrail, kMewShort, kMewAsk, kMewFall, kMewLoud };   // the clips in order (cat_01, 03, 04, 05, 06, 07, 10)
void PlayMeow(int clip, float gain = 0.7f) {
    namespace S = royale::lilo_snd;
    constexpr int kClipCount = sizeof(S::kClips) / sizeof(S::kClips[0]);
    static std::shared_ptr<const std::vector<int16_t>> cache[kClipCount];
    const float volume = GameVolume(false) * gain;
    if (clip < 0 || clip >= kClipCount || volume < 0.01f) return;
    if (!cache[clip]) cache[clip] = std::make_shared<const std::vector<int16_t>>(S::kClips[clip].data, S::kClips[clip].data + S::kClips[clip].count);
    StartVoice(kVoiceCat, cache[clip], false, S::kRate, false, volume);
}

// A little greenish cloud (a few brown puffs among it) that drifts up and thins out; called every tick while the cloud lasts.
void FartCloudStep(PlayState* play, float bx, float by, float bz, int puffs) {
    for (int i = 0; i < puffs; i++) {
        Vec3f pos = { bx + (Rand_ZeroOne() - 0.5f) * 34.0f, by + 22.0f + Rand_ZeroOne() * 22.0f, bz + (Rand_ZeroOne() - 0.5f) * 34.0f };
        Vec3f vel = { (Rand_ZeroOne() - 0.5f) * 0.9f, 0.5f + Rand_ZeroOne() * 0.7f, (Rand_ZeroOne() - 0.5f) * 0.9f }, accel = { 0.0f, 0.02f, 0.0f };
        const bool brown = Rand_ZeroOne() < 0.35f;
        Color_RGBA8 prim = brown ? Color_RGBA8{ 150, 130, 60, 255 } : Color_RGBA8{ 150, 200, 70, 255 };
        Color_RGBA8 env = brown ? Color_RGBA8{ 90, 70, 30, 255 } : Color_RGBA8{ 90, 140, 40, 255 };
        EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 260 + static_cast<int>(Rand_ZeroOne() * 160.0f), 40);
    }
}

// Everybody close enough to smell it (bots and other players you can see, and you) gags for a moment: a little word floats up from their head and
// a puff of green hangs there. Purely a joke on your screen: nothing the server knows about.
struct FartReaction { uint16_t id; double start; float x, y, z; };
std::vector<FartReaction> gFartReactions;
constexpr uint16_t kFartSelf = 0xFFFF;
constexpr float kFartSmellRange = 420.0f;
void FartReact(float x, float z) {
    if (gPlayState == nullptr) return;
    const double now = ImGui::GetTime();
    auto add = [&](uint16_t id, float px, float py, float pz) {
        gFartReactions.push_back({ id, now, px, py, pz });
        FartCloudStep(gPlayState, px, py - 12.0f, pz, 1);
    };
    Player* me = GET_PLAYER(gPlayState);
    if (me != nullptr && std::hypot(me->actor.world.pos.x - x, me->actor.world.pos.z - z) < kFartSmellRange)
        add(kFartSelf, me->actor.world.pos.x, me->actor.world.pos.y, me->actor.world.pos.z);
    for (const auto& [id, st] : gState) {
        if (!st.alive || st.scene != gPlayState->sceneNum || royale::IsBossId(id)) continue;
        if (me != nullptr && std::hypot(st.x - me->actor.world.pos.x, st.z - me->actor.world.pos.z) < 4.0f) continue;   // that is you
        if (std::hypot(st.x - x, st.z - z) < kFartSmellRange) add(id, st.x, st.y, st.z);
    }
}

void DrawFartReactions(ImDrawList* dl, ImFont* font, float scale) {
    static const char* const words[] = { "*cough*", "Ew!", "*gag*", "LILO!", "*sniff* ...ugh", "Who did that?!" };
    const double now = ImGui::GetTime();
    for (size_t i = 0; i < gFartReactions.size();) {
        const FartReaction& r = gFartReactions[i];
        const float age = static_cast<float>(now - r.start);
        if (age > 2.4f || gPlayState == nullptr) { gFartReactions.erase(gFartReactions.begin() + static_cast<long>(i)); continue; }
        float x = r.x, y = r.y, z = r.z;
        if (r.id != kFartSelf) {   // follow the puppet while it moves
            auto it = gState.find(r.id);
            if (it == gState.end() || !it->second.alive) { gFartReactions.erase(gFartReactions.begin() + static_cast<long>(i)); continue; }
            x = it->second.x; y = it->second.y; z = it->second.z;
        } else if (Player* me = GET_PLAYER(gPlayState)) { x = me->actor.world.pos.x; y = me->actor.world.pos.y; z = me->actor.world.pos.z; }
        ImVec2 at;
        if (WorldToScreen(x, y + 85.0f + age * 14.0f, z, &at)) {
            const float fade = std::clamp((2.4f - age) / 0.6f, 0.0f, 1.0f);
            const char* label = words[(r.id == kFartSelf ? 3u : r.id) % (sizeof(words) / sizeof(words[0]))];
            const float size = 21.0f * scale;
            const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label);
            dl->AddText(font, size, ImVec2(at.x - sz.x * 0.5f + 2, at.y + 2), IM_COL32(20, 40, 10, static_cast<int>(220 * fade)), label);
            dl->AddText(font, size, ImVec2(at.x - sz.x * 0.5f, at.y), IM_COL32(190, 235, 110, static_cast<int>(255 * fade)), label);
        }
        i++;
    }
}

// -- Lilo as a pet: follows you (never a bot), purely for looks: no collision, no targeting, nothing the server knows about, and nobody else sees her.
// Her moods pick her animation clip; the clips cross-fade, so walking, sitting, grooming and curling up to sleep blend instead of snapping. Stand
// still facing her and press A to talk: she sits and mews her line in the game's own text box.
enum class CatMood { Follow, Stand, Sit, Groom, Loaf, Stretch, Pounce, Happy, Jump, Talk };
struct CatBrain {
    Actor* actor = nullptr;
    CatMood mood = CatMood::Follow;
    float moodT = 0, idle = 0, sitT = 0;
    float x = 0, z = 0, y = 0, yaw = 0, speed = 0;
    float nextAt = 6.0f;
    float leapX = 0, leapZ = 0;
    size_t pickups = 0;
    bool placed = false;
    int line = 0;    // what she says next (royale::kLiloPetLines)
    CatMood seen = CatMood::Follow;            // the mood last tick, to mew when it changes
    float meowIn = 25.0f, fartIn = 70.0f;      // seconds until a mew of her own, and until her next accident
    float afterFartMew = -1.0f, cloudT = 0.0f; // the mew that follows an accident, and how long her cloud keeps drifting
    int eyes = 0;
    royale::lilo::Animator anim;
};
CatBrain gCat;

void SetMood(CatBrain& c, CatMood m) { c.mood = m; c.moodT = 0; }

void CatPoof(PlayState* play, float x, float y, float z) {
    for (int i = 0; i < 7; i++) {
        Vec3f pos = { x + (Rand_ZeroOne() - 0.5f) * 30.0f, y + 10.0f + Rand_ZeroOne() * 22.0f, z + (Rand_ZeroOne() - 0.5f) * 30.0f };
        Vec3f vel = { (Rand_ZeroOne() - 0.5f) * 1.6f, 0.6f + Rand_ZeroOne(), (Rand_ZeroOne() - 0.5f) * 1.6f }, accel = { 0, 0, 0 };
        Color_RGBA8 prim = { 255, 245, 210, 255 }, env = { 200, 190, 255, 255 };
        EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 120, 26);
    }
}

void Cat_Update(Actor* actor, PlayState* play) {
    namespace L = royale::lilo;
    CatBrain& c = gCat;
    const float dt = 1.0f / royale::kTickHz;
    Player* pl = GET_PLAYER(play);
    const float px = pl->actor.world.pos.x, pz = pl->actor.world.pos.z, py = pl->actor.world.pos.y;
    const float pyaw = pl->actor.shape.rot.y * (3.14159265f / 32768.0f);
    const float pspeed = std::fabs(pl->linearVelocity);
    const bool playerStill = pspeed < 0.6f;
    // Her place beside you: behind and to the left.
    const float wantX = px + std::sin(pyaw + 3.14159265f + 0.7f) * 95.0f, wantZ = pz + std::cos(pyaw + 3.14159265f + 0.7f) * 95.0f;
    float dx = wantX - c.x, dz = wantZ - c.z, d = std::hypot(dx, dz);
    if (!c.placed || d > 1400.0f || std::fabs(py - c.y) > 170.0f) {   // arrived, or left far behind: pop up beside you
        if (c.placed) CatPoof(play, c.x, c.y, c.z);
        c.x = wantX; c.z = wantZ; c.y = py; c.placed = true;
        CatPoof(play, c.x, c.y, c.z);
        dx = dz = 0; d = 0;
        SetMood(c, CatMood::Stand);
    }
    c.moodT += dt;
    if (playerStill) c.idle += dt; else c.idle = 0;
    const bool talking = TalkingTo(actor);
    if (talking && c.mood != CatMood::Talk) SetMood(c, CatMood::Talk);
    if (!talking && c.mood == CatMood::Talk) {   // done talking: she stays sitting a while, and has something new to say next time
        SetMood(c, CatMood::Sit);
        c.sitT = 0; c.nextAt = 4.0f;
        if (c.line == royale::kLiloPetLineCount - 1) c.fartIn = 0.9f;   // she said she smelled one, and now she has made one
        c.line = (c.line + 1) % royale::kLiloPetLineCount;
    }
    if (c.mood != CatMood::Talk) {
        if (gPickupLog.size() != c.pickups) { if (gPickupLog.size() > c.pickups && c.mood != CatMood::Happy) SetMood(c, CatMood::Happy); c.pickups = gPickupLog.size(); }
        if (gEmote.id >= 0 && c.mood != CatMood::Happy) SetMood(c, CatMood::Happy);
        // you jump, she jumps
        if ((c.mood == CatMood::Follow || c.mood == CatMood::Stand) && !(pl->actor.bgCheckFlags & 1) && pl->actor.velocity.y > 4.0f && d < 400.0f)
            SetMood(c, CatMood::Jump);
    }

    // Her voice: a mew for what she has just started doing, one now and then of her own, and her accidents.
    if (c.mood != c.seen) {
        static const int kLineMew[royale::kLiloPetLineCount] = { kMewShort, kMewAsk, kMewLong, kMewLoud, kMewTrail, kMewFall };
        switch (c.mood) {
            case CatMood::Talk: PlayMeow(kLineMew[c.line % royale::kLiloPetLineCount]); break;
            case CatMood::Happy: PlayMeow(kMewChirp); break;
            case CatMood::Jump: PlayMeow(kMewChirp, 0.45f); break;
            case CatMood::Pounce: PlayMeow(kMewLoud, 0.5f); break;
            case CatMood::Stretch: PlayMeow(kMewFall, 0.5f); break;
            default: break;
        }
        c.seen = c.mood;
    }
    if (c.mood != CatMood::Talk) {
        c.meowIn -= dt;
        if (c.meowIn <= 0.0f) {
            static const int kIdleMew[3] = { kMewChirp, kMewShort, kMewAsk };
            if (c.mood == CatMood::Follow || c.mood == CatMood::Stand || c.mood == CatMood::Sit) PlayMeow(kIdleMew[static_cast<int>(Rand_ZeroOne() * 2.99f)], 0.45f);
            c.meowIn = 25.0f + Rand_ZeroOne() * 30.0f;
        }
        c.fartIn -= dt;
        if (c.fartIn <= 0.0f && c.mood != CatMood::Pounce) {   // she farts a lot: now and then, wherever she is
            PlayOneShot(2);
            c.cloudT = 2.0f;
            FartReact(c.x, c.z);
            c.afterFartMew = 1.0f;
            c.fartIn = 55.0f + Rand_ZeroOne() * 75.0f;
            if (c.mood == CatMood::Sit || c.mood == CatMood::Stand || c.mood == CatMood::Loaf) SetMood(c, CatMood::Happy);   // so pleased with herself
        }
    }
    if (c.afterFartMew >= 0.0f) {
        c.afterFartMew -= dt;
        if (c.afterFartMew < 0.0f) PlayMeow(kMewFall, 0.5f);
    }
    if (c.cloudT > 0.0f) {   // the cloud, behind her
        c.cloudT -= dt;
        FartCloudStep(play, c.x - std::sin(c.yaw) * 48.0f, c.y, c.z - std::cos(c.yaw) * 48.0f, 2);
    }

    const float tm = static_cast<float>(play->gameplayFrames) * dt;
    bool moving = false;
    float heading = c.yaw;
    int clip = L::kIdle, eyes = L::kEyesOpen;
    float rate = 1.0f;
    auto follow = [&](float minSpeed) {   // go to her place: a walk when it is near, a run when you have gone on ahead
        c.speed += (std::clamp((d - 40.0f) * 2.8f, minSpeed, 290.0f) - c.speed) * 0.2f;
        heading = std::atan2(dx, dz);
        c.x += std::sin(heading) * c.speed * dt;
        c.z += std::cos(heading) * c.speed * dt;
        moving = true;
    };

    switch (c.mood) {
        case CatMood::Follow: case CatMood::Stand: {
            if (d > 58.0f) {
                follow(45.0f);
                c.mood = CatMood::Follow;
            } else {
                c.speed *= 0.7f;
                if (c.mood == CatMood::Follow) SetMood(c, CatMood::Stand);
                if (c.idle > 1.6f) { SetMood(c, CatMood::Sit); c.sitT = 0; c.nextAt = 5.0f + Rand_ZeroOne() * 4.0f; }
            }
            if (moving && c.speed > 150.0f) { clip = L::kRun; rate = std::clamp(c.speed / 230.0f, 0.8f, 1.4f); }
            else if (moving) { clip = L::kWalk; rate = std::clamp(c.speed / 75.0f, 0.6f, 1.8f); }
            break;
        }
        case CatMood::Sit: case CatMood::Groom: {
            c.sitT += dt;
            clip = c.mood == CatMood::Groom ? L::kGroom : L::kSit;
            if (c.mood == CatMood::Groom) {   // a front paw to the mouth and a lick
                eyes = L::kEyesHalf;
                if (c.moodT > 3.0f) SetMood(c, CatMood::Sit);
            } else if (c.moodT > c.nextAt) {
                const float r = Rand_ZeroOne();
                c.nextAt = 5.0f + Rand_ZeroOne() * 4.0f;
                SetMood(c, r < 0.45f ? CatMood::Groom : r < 0.7f ? CatMood::Stretch : CatMood::Pounce);
            }
            if (c.sitT > 16.0f && c.mood == CatMood::Sit) SetMood(c, CatMood::Loaf);
            if (!playerStill || d > 150.0f) SetMood(c, CatMood::Follow);
            break;
        }
        case CatMood::Loaf: {   // curled up asleep: paws tucked under, eyes shut, little 'z's
            clip = L::kSleep;
            eyes = L::kEyesShut;
            if (static_cast<int>(c.moodT * 10.0f) % 18 == 0 && play->gameplayFrames % 3 == 0) {
                Vec3f pos = { c.x + std::sin(c.yaw) * 20.0f, c.y + 40.0f, c.z + std::cos(c.yaw) * 20.0f }, vel = { 0.15f, 0.5f, 0 }, accel = { 0, 0, 0 };
                Color_RGBA8 prim = { 190, 210, 255, 255 }, env = { 90, 120, 255, 255 };
                EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 60, 40);
            }
            if (!playerStill || d > 150.0f) SetMood(c, CatMood::Stretch);
            break;
        }
        case CatMood::Stretch: {   // front legs forward, chest down, rump up, and a yawn
            clip = L::kStretch;
            eyes = L::kEyesHalf;
            if (c.moodT > 2.0f) SetMood(c, playerStill ? CatMood::Sit : CatMood::Follow);
            break;
        }
        case CatMood::Pounce: {   // crouch and wiggle, then a leap at nothing
            clip = L::kPounce;
            if (c.moodT < 0.9f) {
                c.leapX = std::sin(c.yaw) * 75.0f; c.leapZ = std::cos(c.yaw) * 75.0f;
            } else if (c.moodT < 1.35f) {
                c.x += c.leapX * dt / 0.45f; c.z += c.leapZ * dt / 0.45f;
            } else if (c.moodT > 1.5f) {
                SetMood(c, CatMood::Stand);
            }
            break;
        }
        case CatMood::Happy: {   // hops with her tail straight up
            clip = L::kHappy;
            eyes = L::kEyesShut;
            if (c.moodT > 1.5f) SetMood(c, CatMood::Stand);
            break;
        }
        case CatMood::Jump: {   // up after you, still heading for her place
            clip = L::kJump;
            if (d > 20.0f) follow(std::min(c.speed, 290.0f));
            if (c.moodT > L::ClipSeconds(L::kJump)) SetMood(c, CatMood::Follow);
            break;
        }
        case CatMood::Talk: {   // sitting, facing you, mewing
            clip = L::kTalk;
            c.speed = 0;
            heading = std::atan2(px - c.x, pz - c.z);
            break;
        }
    }
    // Talk: stand still facing her and press A (the game's own talk, so the A button says "Speak"). Not while you are on the move.
    if (!talking && c.mood != CatMood::Jump && c.mood != CatMood::Pounce && pspeed < 3.0f &&
        OfferTalk(actor, play, static_cast<u16>(kTextLiloPet + c.line), 120.0f))
        SetMood(c, CatMood::Talk);
    // turn to face the way she goes (or, when still, towards you if you are close)
    if (!moving && c.mood != CatMood::Pounce && c.mood != CatMood::Talk && d < 400.0f) heading = std::atan2(px - c.x, pz - c.z);
    {
        float diff = heading - c.yaw;
        while (diff > 3.14159265f) diff -= 6.2831853f;
        while (diff < -3.14159265f) diff += 6.2831853f;
        c.yaw += diff * (moving ? 0.25f : c.mood == CatMood::Talk ? 0.3f : 0.08f);
    }
    c.anim.Play(clip, 0.25f);
    c.anim.Update(dt, rate);
    c.eyes = LiloEyes(tm, eyes);

    c.y = GroundY(play, c.x, c.z, c.y);   // her clips carry their own hops and leaps
    actor->world.pos.x = c.x; actor->world.pos.z = c.z; actor->world.pos.y = c.y;
    actor->shape.rot.y = static_cast<s16>(c.yaw * (32768.0f / 3.14159265f));
    actor->focus.pos = actor->world.pos;
    actor->focus.pos.y += 30.0f;
}

void Cat_Draw(Actor* actor, PlayState* play) {
    royale::lilo::Pose pose;
    gCat.anim.Evaluate(pose);
    DrawLiloModel(play, actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, gCat.yaw, kLiloPetScale, pose, gCat.eyes);
}
void Cat_Destroy(Actor* actor, PlayState*) { if (gCat.actor == actor) { gCat.actor = nullptr; gCat.placed = false; } }

void ReconcileCatPet(const royale::HudState& hud) {
    const bool inMatch = IsLive(hud);
    const bool want = MapOption("LiloPet", false) && gSession.Joined() && (InField() || InWaitingRoom()) && gPlayState != nullptr && !gSkydiving && !gSpectating &&
                      !(inMatch && hud.haveSelf && !hud.selfAlive);
    if (!want) {
        if (gCat.actor != nullptr) { Actor_Kill(gCat.actor); gCat.actor = nullptr; gCat.placed = false; }
        return;
    }
    if (gCat.actor != nullptr) return;
    Player* pl = GET_PLAYER(gPlayState);
    Actor* a = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ISHI, pl->actor.world.pos.x, pl->actor.world.pos.y, pl->actor.world.pos.z, 0, 0, 0, 0, false);
    if (a == nullptr) return;
    a->update = Cat_Update;
    a->draw = Cat_Draw;
    a->destroy = Cat_Destroy;
    a->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
    a->uncullZoneForward = 4000.0f; a->uncullZoneScale = 1500.0f; a->uncullZoneDownward = 1500.0f;
    a->shape.shadowScale = 18.0f;
    gCat.actor = a;
    gCat.placed = false;
    gCat.pickups = gPickupLog.size();
    gCat.mood = CatMood::Follow;
    gCat.anim = royale::lilo::Animator{};
}

// ---- Lilo ---------------------------------------------------------------------------------------------------------------------------
// An Easter egg (switch it off with "Lilo the cat" under Minimap and game options): Lilo sits at a random spot on the map. Talk to her with A: she
// says her line in the game's text box, and when you close it there is a noise and a greenish cloud.
Actor* gLiloActor = nullptr;
royale::Vec2 gLiloPos = {};
bool gLiloKnown = false;
double gLiloTalkStart = -100.0;
bool gLiloFarted = true, gLiloHeard = false;
double gFartCloudUntil = 0, gLiloMewAt = -1.0;
royale::lilo::Animator gLiloAnim;
int gLiloEyes = 0;

void TalkToLilo();
void Lilo_Update(Actor* actor, PlayState* play) {
    namespace L = royale::lilo;
    Player* pl = GET_PLAYER(play);
    const bool talking = TalkingTo(actor);
    const float dx = pl->actor.world.pos.x - actor->world.pos.x, dz = pl->actor.world.pos.z - actor->world.pos.z;
    if (dx * dx + dz * dz < 600.0f * 600.0f) {   // she watches you come
        const s16 want = static_cast<s16>(std::atan2(dx, dz) * (32768.0f / 3.14159265f));
        actor->shape.rot.y = static_cast<s16>(actor->shape.rot.y + static_cast<s16>(want - actor->shape.rot.y) * (talking ? 0.25f : 0.1f));
    }
    actor->focus.pos = actor->world.pos;
    actor->focus.pos.y += 40.0f;
    if (OfferTalk(actor, play, kTextLilo, royale::kHireRange)) TalkToLilo();
    gLiloAnim.Play(talking ? L::kTalk : L::kSit, 0.3f);
    gLiloAnim.Update(1.0f / royale::kTickHz);
    gLiloEyes = LiloEyes(static_cast<float>(play->gameplayFrames) / royale::kTickHz, L::kEyesOpen);
}
void Lilo_Draw(Actor* actor, PlayState* play) {
    royale::lilo::Pose pose;
    gLiloAnim.Evaluate(pose);
    DrawLiloModel(play, actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, actor->shape.rot.y * (3.14159265f / 32768.0f), kLiloMapScale, pose, gLiloEyes);
}
void Lilo_Destroy(Actor* actor, PlayState*) { if (gLiloActor == actor) gLiloActor = nullptr; }

bool FindLiloSpot(const royale::Circle& map, royale::Vec2* out) {
    if (!gSession.Client()) return false;
    const auto& props = gSession.Client()->Props();
    royale::Rng rng(gSession.Client()->Seed() ^ 0x4C696C6Full);   // "Lilo"
    auto clear = [&](royale::Vec2 p) {
        if (!WalkableAt(p)) return false;
        if (gSignKnown && royale::Distance(p, gSignPos) < 500.0f) return false;
        if (gMayaKnown && royale::Distance(p, gMayaPos) < 500.0f) return false;
        for (const royale::Prop& pr : props) {
            const float r = royale::PropRadius(pr.kind);
            if (r > 0.0f && royale::Distance(pr.pos, p) < r + 100.0f) return false;
        }
        return true;
    };
    for (int attempt = 0; attempt < 400; attempt++) {
        const float a = static_cast<float>(rng.Unit() * 6.2831853), d = map.radius * (0.15f + 0.7f * static_cast<float>(rng.Unit()));
        const royale::Vec2 p = { map.center.x + std::cos(a) * d, map.center.z + std::sin(a) * d };
        if (clear(p)) { *out = p; return true; }
    }
    return false;
}

void ReconcileLilo(const royale::HudState& hud) {
    const bool want = MapOption("LiloCat", true) && gSession.Joined() && InField() && hud.map.radius > 0 &&
                      (hud.state == royale::MatchState::Countdown || hud.state == royale::MatchState::Drop || hud.state == royale::MatchState::InMatch);
    if (!want) {
        if (gLiloActor != nullptr) { Actor_Kill(gLiloActor); gLiloActor = nullptr; }
        gLiloKnown = false;
        return;
    }
    if (gLiloActor != nullptr) return;
    if (!gLiloKnown) { if (!FindLiloSpot(hud.map, &gLiloPos)) return; gLiloKnown = true; }
    float y = 0;
    if (!FloorAt(gLiloPos.x, gLiloPos.z, &y)) return;
    Actor* a = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ISHI, gLiloPos.x, y, gLiloPos.z, 0, 0x6000, 0, 0, false);
    if (a == nullptr) return;
    a->update = Lilo_Update;
    a->draw = Lilo_Draw;
    a->destroy = Lilo_Destroy;
    a->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
    a->uncullZoneForward = 4000.0f; a->uncullZoneScale = 1500.0f; a->uncullZoneDownward = 1500.0f;
    a->shape.shadowScale = 22.0f;
    gLiloActor = a;
    gLiloAnim = royale::lilo::Animator{};
    gLiloAnim.Play(royale::lilo::kSit, 0.0f);
}

bool LiloNear() {
    if (gLiloActor == nullptr || !InField()) return false;
    Player* pl = GET_PLAYER(gPlayState);
    return std::hypot(pl->actor.world.pos.x - gLiloPos.x, pl->actor.world.pos.z - gLiloPos.z) < royale::kHireRange;
}

// The talk itself is the game's text box (Lilo_Update); this arms the accident for when it closes.
void TalkToLilo() {
    PlayMeow(kMewLong);
    gLiloTalkStart = ImGui::GetTime();
    gLiloFarted = false;
    gLiloHeard = false;
}

// The accident: once her text box has closed, the noise, and a cloud of greenish-brown puffs behind the cat that drifts up and thins out.
void UpdateLiloFx() {
    if (gLiloActor == nullptr || gPlayState == nullptr || !InField()) return;
    const double now = ImGui::GetTime();
    if (!gLiloFarted) {
        if (TalkingTo(gLiloActor)) gLiloHeard = true;
        else if (gLiloHeard || now - gLiloTalkStart > 4.0) {
            gLiloFarted = true;
            gFartCloudUntil = now + 2.6;
            PlayOneShot(2);
            FartReact(gLiloPos.x, gLiloPos.z);
            gLiloMewAt = now + 1.0;
        }
    }
    if (gLiloMewAt > 0.0 && now >= gLiloMewAt) { gLiloMewAt = -1.0; PlayMeow(kMewFall, 0.6f); }   // a satisfied mew once the air has cleared a little
    if (now < gFartCloudUntil) {
        const float yaw = gLiloActor->shape.rot.y * (3.14159265f / 32768.0f);
        FartCloudStep(gPlayState, gLiloPos.x - std::sin(yaw) * 48.0f, gLiloActor->world.pos.y, gLiloPos.z - std::cos(yaw) * 48.0f, 3);   // behind her
    }
}

// Her name over her head (what she says is in the game's own text box).
void DrawLilo(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale) {
    (void)ds;
    if (gLiloActor == nullptr || !InField()) return;
    Player* pl = GET_PLAYER(gPlayState);
    const float d = std::hypot(pl->actor.world.pos.x - gLiloPos.x, pl->actor.world.pos.z - gLiloPos.z);
    ImVec2 at;
    if (d < 1800.0f && WorldToScreen(gLiloPos.x, gLiloActor->world.pos.y + 78.0f, gLiloPos.z, &at)) {
        const float size = std::clamp(24.0f * scale * (1800.0f / (d + 900.0f)), 13.0f * scale, 28.0f * scale);
        const char* label = royale::kLiloName;
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label);
        dl->AddText(font, size, ImVec2(at.x - sz.x * 0.5f + 2, at.y + 2), IM_COL32(20, 20, 20, 230), label);
        dl->AddText(font, size, ImVec2(at.x - sz.x * 0.5f, at.y), IM_COL32(235, 235, 230, 255), label);
    }
}

// ---- hireable allies -----------------------------------------------------------------------------------------------------------
// Four people wait around the map (a Kokiri, a Zora, a Goron and a Gerudo); pay one with rupees and they follow you and fight for you. The
// server runs them (BotController::StepAllies); each is drawn by a stand-in actor with our own model, smoothed between snapshots.
// The four allies are the game's own NPCs: the Kokiri kid, the Zora, the Goron and the Gerudo, each with its real skeleton, textures and animations
// (the same assets the game's NPC actors use). The game never walks or fights with these, so the movement is built here: the real standing, waving
// and posing animations, plus a procedural walk cycle and attack poses added to the limbs while they are drawn. What each carries is drawn with the
// real item model in its hand.
struct NpcSpec {
    const char* skeleton;
    int limbs;
    const char* idle;      // standing and ready (hired)
    const char* invite;    // free to hire: waves you over
    const char* act;       // attacking or healing: played through once
    float scale;           // actor scale
    int hairstyle;         // Gerudo: which hair
    royale::ItemId item;   // carried
    int lHand, rHand;      // which hand holds it (limb index)
    bool itemLeft;
};
NpcSpec NpcOf(int kind) {
    switch (kind) {
        case 0: return { gKm1Skel, 16, gKokiriStandingHandsOnHipsAnim, gKokiriStandingRightArmUpAnim, gKokiriStandingRightArmUpAnim, 0.0105f, 0, royale::ItemId::Slingshot, 11, 14, true };
        case 1: return { gZoraSkel, 20, gZoraHandsOnHipsTappingFootAnim, gZoraOpenArmsAnim, gZoraThrowRupeesAnim, 0.0105f, 0, royale::ItemId::DekuStick, 11, 14, false };
        case 2: return { gGoronSkel, 18, gGoronAnim_004930, gGoronAnim_004930, gGoronAnim_004930, 0.0115f, 0, royale::ItemId::MegatonHammer, 16, 13, false };
        default: return { gGerudoWhiteSkel, 16, gGerudoWhiteIdleAnim, gGerudoWhiteClapAnim, gGerudoWhiteDismissiveAnim, 0.0105f, 1, royale::ItemId::FairyBow, 11, 14, true };
    }
}
struct NpcRig { int torso, lArm, rArm, lFore, rFore, lThigh, rThigh, lShin, rShin, head; };
NpcRig RigOf(int kind) {
    if (kind == 2) return { 10, 11, 14, 12, 15, 2, 5, 3, 6, 17 };   // the Goron's skeleton is laid out differently
    return { 8, 9, 12, 10, 13, 2, 5, 3, 6, 15 };                    // Kokiri, Zora and Gerudo share the humanoid layout
}

constexpr float kRad = 32768.0f / 3.14159265f;

// Called for every limb as the skeleton is drawn: the walk cycle, the idle sway and the attack poses are added to the limb's rotation here.
AllyActor* gDrawingAlly = nullptr;
s32 Ally_OverrideLimb(PlayState*, s32 limb, Gfx** dList, Vec3f*, Vec3s* rot, void*) {
    const AllyActor* a = gDrawingAlly;
    if (a == nullptr) return 0;
    if (a->kind == 0 && limb == 15) *dList = (Gfx*)gKm1DL;   // the Kokiri's head is its own display list, put on at the head limb
    const NpcRig r = RigOf(a->kind);
    const float t = static_cast<float>(ImGui::GetTime());
    auto add = [&](float x, float y, float z) { rot->x = static_cast<s16>(rot->x + x * kRad); rot->y = static_cast<s16>(rot->y + y * kRad); rot->z = static_cast<s16>(rot->z + z * kRad); };
    const float w = a->walkW, s = std::sin(a->phase);
    // the walk: legs swing, knees bend as each foot lifts, the arms swing against the legs, the body twists a little
    if (limb == r.lThigh || limb == r.rThigh) add(0, 0, 0.62f * w * s);
    if (limb == r.lShin || limb == r.rShin) add(0, 0, 0.55f * w * std::max(0.0f, limb == r.lShin ? -s : s) + 0.1f * w);
    if (limb == r.lArm || limb == r.rArm) add(0, 0, -0.45f * w * s);
    if (limb == r.torso) add(0, 0.14f * w * s, -0.08f * w);
    // standing: a slow breath and a little weight shift
    if (limb == r.torso) add(0, 0.03f * std::sin(t * 1.3f + a->kind), 0.025f * std::sin(t * 1.7f));
    if (limb == r.head) add(0, 0.05f * std::sin(t * 0.8f + a->kind * 2.0f), 0.04f * std::sin(t * 1.1f));
    // the attack or heal: how far through it, 0 to 1
    const float T = a->actAge;
    if (T < 0.7f) {
        const float u = T / 0.7f, env = std::sin(u * 3.14159f);
        switch (a->kind) {
            case 0:   // the Kokiri aims the slingshot out in front, then it snaps back
                if (limb == r.lArm) add(0, 0, 1.45f * env);
                if (limb == r.rArm) add(0, 0, -(0.9f + 0.4f * (1.0f - u)) * env);
                if (limb == r.rFore) add(0, 0, -0.9f * env);
                if (limb == r.torso) add(0, -0.25f * env, 0);
                break;
            case 1:   // the Zora throws its arms wide, glowing, and bobs up
                if (limb == r.lArm) add(0, 0, 1.2f * env);
                if (limb == r.rArm) add(0, 0, -1.2f * env);
                if (limb == r.head) add(0, 0, -0.25f * env);
                break;
            case 2: { // the Goron heaves the hammer overhead and brings it down
                const float lift = u < 0.45f ? u / 0.45f : std::max(0.0f, 1.0f - (u - 0.45f) / 0.2f);
                if (limb == r.lArm || limb == r.rArm) add(0, 0, (limb == r.lArm ? 1.0f : -1.0f) * 2.5f * lift);
                if (limb == r.torso) add(0, 0, (u < 0.45f ? -0.3f * lift : 0.55f * (1.0f - std::fabs(u - 0.6f) * 4.0f)));
                break;
            }
            default:  // the Gerudo draws the bow: left arm out, right hand pulled back to the cheek
                if (limb == r.lArm) add(0, 0, 1.5f * env);
                if (limb == r.rArm) add(0, 0, -1.1f * env);
                if (limb == r.rFore) add(0, 0, -1.6f * env * (1.0f - u * 0.6f));
                if (limb == r.torso) add(0, -0.2f * env, 0);
                break;
        }
    }
    return 0;
}

void Ally_PostLimb(PlayState* play, s32 limb, Gfx** dList, Vec3s*, void*) {
    (void)dList;
    const AllyActor* a = gDrawingAlly;
    if (a == nullptr) return;
    const NpcSpec spec = NpcOf(a->kind);
    OPEN_DISPS(play->state.gfxCtx);
    if (a->kind == 3 && limb == 15) {   // the Gerudo's hair goes on at the head
        static const char* hair[2] = { gGerudoWhiteHairstyleBobDL, gGerudoWhiteHairstyleStraightFringeDL };
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)hair[spec.hairstyle % 2]);
    }
    if (limb == (spec.itemLeft ? spec.lHand : spec.rHand)) {   // what it carries, in the real item model
        const int gid = GidFor(spec.item);
        if (gid >= 0) {
            Matrix_Push();
            Matrix_Translate(600.0f, 0.0f, 0.0f, MTXMODE_APPLY);
            Matrix_RotateZ(spec.itemLeft ? 1.57f : -1.0f, MTXMODE_APPLY);
            Matrix_RotateY(1.57f, MTXMODE_APPLY);
            const float k = GidScale(gid) * 3.0f;
            Matrix_Scale(k, k, k, MTXMODE_APPLY);
            DrawItemModel(play, gid);
            Matrix_Pop();
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

void Ally_Update(Actor* actor, PlayState* play) {
    auto of = gAllyOf.find(actor);
    if (of == gAllyOf.end()) { Actor_Kill(actor); return; }
    AllyActor& a = gAllies[of->second];
    if (!a.init) { a.x = a.tx; a.z = a.tz; a.rot = a.trot; a.init = true; }
    const float nx = a.x + (a.tx - a.x) * 0.4f, nz = a.z + (a.tz - a.z) * 0.4f;
    a.moved = a.moved * 0.8f + std::hypot(nx - a.x, nz - a.z);
    a.x = nx; a.z = nz;
    a.rot = static_cast<s16>(a.rot + static_cast<s16>(a.trot - a.rot) * 0.35f);
    a.actAge += 1.0f / royale::kTickHz;
    actor->world.pos.x = a.x;
    actor->world.pos.z = a.z;
    actor->world.pos.y = GroundY(play, a.x, a.z, actor->world.pos.y);
    actor->shape.rot.y = a.rot;
    actor->world.rot.y = a.rot;
    actor->focus.pos = actor->world.pos;

    const NpcSpec spec = NpcOf(a.kind);
    if (!a.skReady) {
        SkelAnime_InitFlex(play, &a.sk, (FlexSkeletonHeader*)spec.skeleton, nullptr, a.joint, a.morph, spec.limbs);
        a.skReady = true;
    }
    // Which of the game's animations: waving you over while free to hire, the attack pose while it fights, the ready stance otherwise.
    const bool free = a.owner == royale::net::kNoPlayer16;
    const bool acting = a.actAge < 0.7f;
    const char* want = free ? spec.invite : (acting ? spec.act : spec.idle);
    if (a.playing != (const void*)want || (acting && !a.wasActing && !free)) {
        Animation_Change(&a.sk, (AnimationHeader*)want, (a.kind == 2 && want == spec.idle) ? 0.0f : 1.0f, 0.0f, Animation_GetLastFrame((void*)want), ANIMMODE_LOOP, -4.0f);
        a.playing = want;
    }
    a.wasActing = acting;
    SkelAnime_Update(&a.sk);
    const float moving = std::clamp(a.moved / 1.6f, 0.0f, 1.0f);
    a.walkW += (moving - a.walkW) * 0.25f;
    a.phase += (0.35f + 0.3f * a.walkW) * a.walkW;
    a.bob = a.walkW * std::fabs(std::sin(a.phase)) * 3.0f;
}

void Ally_Draw(Actor* actor, PlayState* play) {
    auto of = gAllyOf.find(actor);
    if (of == gAllyOf.end()) return;
    AllyActor& a = gAllies[of->second];
    if (!a.skReady) return;
    const NpcSpec spec = NpcOf(a.kind);
    const float t = static_cast<float>(play->gameplayFrames);
    const int eye = std::fmod(t + of->second * 37.0f, 90.0f) < 4.0f ? 2 : 0;   // a blink every few seconds
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    switch (a.kind) {
        case 0: {   // Kokiri: tunic and boots colours come through two small display lists, as the game's Kokiri do
            Gfx* tunic = static_cast<Gfx*>(Graph_Alloc(play->state.gfxCtx, sizeof(Gfx) * 2));
            gDPSetEnvColor(tunic, 30, 105, 27, 255);
            gSPEndDisplayList(tunic + 1);
            Gfx* boots = static_cast<Gfx*>(Graph_Alloc(play->state.gfxCtx, sizeof(Gfx) * 2));
            gDPSetEnvColor(boots, 110, 170, 39, 255);
            gSPEndDisplayList(boots + 1);
            gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)tunic);
            gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)boots);
            break;
        }
        case 1: {
            static const char* eyes[3] = { gZoraEyeOpenTex, gZoraEyeHalfTex, gZoraEyeClosedTex };
            gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)eyes[eye]);
            break;
        }
        case 2: {
            static const char* eyes[4] = { gGoronCsEyeClosed2Tex, gGoronCsEyeOpenTex, gGoronCsEyeHalfTex, gGoronCsEyeClosedTex };
            static const char* mouths[2] = { gGoronCsMouthNeutralTex, gGoronCsMouthSmileTex };
            gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)eyes[eye == 2 ? 3 : 1]);
            gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)mouths[a.owner == royale::net::kNoPlayer16 ? 1 : 0]);
            break;
        }
        default: {
            static const char* eyes[3] = { gGerudoWhiteEyeOpenTex, gGerudoWhiteEyeHalfTex, gGerudoWhiteEyeClosedTex };
            gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)eyes[eye]);
            break;
        }
    }
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y + a.bob, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(actor->shape.rot.y * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    Matrix_Scale(spec.scale, spec.scale, spec.scale, MTXMODE_APPLY);
    gDrawingAlly = &a;
    SkelAnime_DrawFlexOpa(play, a.sk.skeleton, a.sk.jointTable, a.sk.dListCount, Ally_OverrideLimb, Ally_PostLimb, actor);
    gDrawingAlly = nullptr;
    CLOSE_DISPS(play->state.gfxCtx);
}

void Ally_Destroy(Actor* actor, PlayState* play) {
    ActorFunc orig = nullptr;
    auto of = gAllyOf.find(actor);
    if (of != gAllyOf.end()) {
        auto a = gAllies.find(of->second);
        if (a != gAllies.end()) { orig = a->second.origDestroy; gAllies.erase(a); }
        gAllyOf.erase(of);
    }
    if (orig) orig(actor, play);
}

void ReconcileAllies(const royale::HudState& hud) {
    const bool show = gSession.Joined() && InField() && gSession.Client() &&
                      (hud.state == royale::MatchState::Countdown || hud.state == royale::MatchState::Drop || hud.state == royale::MatchState::InMatch || hud.state == royale::MatchState::Ending);
    std::unordered_map<uint8_t, bool> wanted;
    if (show) {
        for (const royale::net::AllyNet& n : gSession.Client()->Allies()) {
            wanted[n.index] = true;
            auto it = gAllies.find(n.index);
            if (it == gAllies.end()) {
                float y = 0;
                if (!FloorAt(n.x, n.z, &y)) continue;
                Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ISHI, n.x, y, n.z, 0, n.rot, 0, 0, false);
                if (actor == nullptr) continue;
                AllyActor a;
                a.actor = actor; a.origDestroy = actor->destroy; a.kind = n.kind; a.owner = n.owner;
                a.tx = n.x; a.tz = n.z; a.trot = n.rot; a.rot = n.rot; a.x = n.x; a.z = n.z; a.init = true;
                gAllies[n.index] = a;
                gAllyOf[actor] = n.index;
                actor->update = Ally_Update;
                actor->draw = Ally_Draw;
                actor->destroy = Ally_Destroy;
                actor->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED;
                actor->uncullZoneForward = 4000.0f; actor->uncullZoneScale = 1500.0f; actor->uncullZoneDownward = 1500.0f;
                actor->shape.shadowScale = 40.0f;
            } else {
                AllyActor& a = it->second;
                a.tx = n.x; a.tz = n.z; a.trot = n.rot; a.hp = n.hp / 255.0f; a.owner = n.owner;
                if ((n.flags & 2) && a.actAge > 0.5f) a.actAge = 0.0f;
            }
        }
    }
    for (auto& [id, a] : gAllies) if (!wanted.count(id) && a.actor) Actor_Kill(a.actor);
}

// The free ally you are standing next to, or -1.
int NearbyFreeAlly() {
    if (!InField() || gSession.Client() == nullptr) return -1;
    Player* pl = GET_PLAYER(gPlayState);
    int best = -1;
    float bestD = royale::kHireRange;
    for (const auto& [id, a] : gAllies) {
        if (a.owner != royale::net::kNoPlayer16 || a.actor == nullptr) continue;
        const float d = std::hypot(a.x - pl->actor.world.pos.x, a.z - pl->actor.world.pos.z);
        if (d < bestD) { bestD = d; best = id; }
    }
    return best;
}

void DrawAllyLabels(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const royale::HudState& h) {
    if (!InField()) return;
    Player* pl = GET_PLAYER(gPlayState);
    for (const auto& [id, a] : gAllies) {
        if (a.actor == nullptr) continue;
        const float d = std::hypot(a.x - pl->actor.world.pos.x, a.z - pl->actor.world.pos.z);
        const bool mine = a.owner == h.selfId, free = a.owner == royale::net::kNoPlayer16;
        if (d > (free ? 2600.0f : 1800.0f)) continue;
        ImVec2 at;
        if (!WorldToScreen(a.x, a.actor->world.pos.y + 245.0f, a.z, &at)) continue;
        const royale::AllyDef& def = royale::kAllyDefs[a.kind];
        const float k = std::clamp(1500.0f / (d + 700.0f), 0.7f, 1.3f), ts = 18.0f * scale * k;
        std::string line1 = free ? std::string(def.name) + " " + def.title : mine ? std::string(def.name) + " (yours)" : std::string(def.name) + " (an ally)";
        const ImU32 col = free ? IM_COL32(255, 222, 110, 255) : mine ? IM_COL32(130, 255, 150, 255) : IM_COL32(190, 200, 220, 255);
        ImVec2 sz = font->CalcTextSizeA(ts, FLT_MAX, 0.0f, line1.c_str());
        dl->AddText(font, ts, ImVec2(at.x - sz.x * 0.5f + 1.5f, at.y + 1.5f), IM_COL32(0, 0, 0, 220), line1.c_str());
        dl->AddText(font, ts, ImVec2(at.x - sz.x * 0.5f, at.y), col, line1.c_str());
        if (free) {
            const bool afford = h.rupees >= def.price;
            const std::string line2 = std::to_string(def.price) + " rupees";
            sz = font->CalcTextSizeA(ts * 0.9f, FLT_MAX, 0.0f, line2.c_str());
            dl->AddText(font, ts * 0.9f, ImVec2(at.x - sz.x * 0.5f + 1.5f, at.y + ts + 1.5f), IM_COL32(0, 0, 0, 220), line2.c_str());
            dl->AddText(font, ts * 0.9f, ImVec2(at.x - sz.x * 0.5f, at.y + ts), afford ? IM_COL32(120, 255, 140, 255) : IM_COL32(255, 120, 110, 255), line2.c_str());
        } else {
            const float w = 90.0f * scale * k, hgt = 7.0f * scale;
            const ImVec2 a0(at.x - w * 0.5f, at.y + ts + 3.0f * scale);
            dl->AddRectFilled(ImVec2(a0.x - 1, a0.y - 1), ImVec2(a0.x + w + 1, a0.y + hgt + 1), IM_COL32(0, 0, 0, 190));
            dl->AddRectFilled(a0, ImVec2(a0.x + w * a.hp, a0.y + hgt), mine ? IM_COL32(90, 220, 110, 255) : IM_COL32(180, 190, 210, 255));
        }
    }
}

// What an ally's hit looks like: a shot flies from it (or a thump lands, for the Goron); a Zora's gift is a shower of green sparks on you.
void AllyActionFx(const royale::ClientEvent& e, const royale::HudState& h) {
    auto it = gAllies.find(static_cast<uint8_t>(e.index));
    if (it == gAllies.end() || it->second.actor == nullptr || gPlayState == nullptr) return;
    AllyActor& a = it->second;
    a.actAge = 0.0f;
    const float ay = a.actor->world.pos.y;
    if (e.id == a.owner) {   // mending its owner
        float gy = ay;
        FloorAt(e.x, e.z, &gy);
        SparkBurst(gPlayState, e.x, gy + 60.0f, e.z, { 120, 255, 150, 255 }, 18, 3.0f);
        if (a.owner == h.selfId) Audio_PlaySoundGeneral(NA_SE_SY_HP_RECOVER, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return;
    }
    const s16 yaw = static_cast<s16>(std::atan2(e.x - a.x, e.z - a.z) * (32768.0f / 3.14159265f));
    switch (a.kind) {
        case 0: SpawnProjectileFrom(royale::ItemId::Slingshot, a.x, ay + 70.0f, a.z, yaw); break;
        case 1: SpawnProjectileFrom(royale::ItemId::IceArrows, a.x, ay + 90.0f, a.z, yaw); break;
        case 3: SpawnProjectileFrom(royale::ItemId::FairyBow, a.x, ay + 90.0f, a.z, yaw); break;
        default: {
            float gy = ay;
            FloorAt(e.x, e.z, &gy);
            SparkBurst(gPlayState, e.x, gy + 40.0f, e.z, { 255, 200, 120, 255 }, 20, 6.0f);
            Vec3f at = { e.x, gy + 40.0f, e.z };
            Audio_PlaySoundGeneral(NA_SE_IT_HAMMER_HIT, &at, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            break;
        }
    }
}

// ---- Link's clothes ------------------------------------------------------------------------------------------------------------
// The game calls us while it draws Link's limbs (patches add the hooks: 0009 for the cap, 0015 for the rest). The tail of the cap, the skirt
// of the tunic and the sheath on its strap each swing on a spring pushed by the air, which is the wind plus how fast Link is moving, running
// or falling, and they lag behind when he starts, stops, turns or lands. Everyone's clothes do it: the local player, the other players and
// the bots (they are all drawn as Player actors).
struct HatState {
    royale::HatSpring spring;                  // the cap's tail
    royale::ClothSwing skirt, sheath;          // the tunic's skirt (the waist limb) and the sheath
    float lx = 0, ly = 0, lz = 0;
    float lvx = 0, lvz = 0, lvy = 0;   // last velocity, to feel the acceleration
    int16_t lyaw = 0;                  // last facing, to feel him turning
    double lastT = 0, seen = 0;
    bool have = false;
};
std::unordered_map<const void*, HatState> gHats;
int gClothLimbCalls = 0;       // how many times the game has asked about the tunic (shown in the menu, to prove the hook is wired)
float gSkirtLastSwing = 0.0f;  // the last skirt swing applied, in degrees

// Steps every piece of one player's clothing, once per frame however many times the limbs are drawn.
HatState& StepClothes(const Player* pl) {
    const double now = ImGui::GetTime();
    HatState& h = gHats[pl];
    const float x = pl->actor.world.pos.x, y = pl->actor.world.pos.y, z = pl->actor.world.pos.z;
    if (!h.have) { h.lx = x; h.ly = y; h.lz = z; h.lastT = now; h.lyaw = pl->actor.shape.rot.y; h.have = true; }
    h.seen = now;
    const float dt = static_cast<float>(now - h.lastT);
    if (dt > 0.004f) {   // (the limbs are drawn more than once a frame sometimes: only step when time has passed)
        const float vx = std::clamp((x - h.lx) / dt, -900.0f, 900.0f), vy = std::clamp((y - h.ly) / dt, -1500.0f, 1500.0f), vz = std::clamp((z - h.lz) / dt, -900.0f, 900.0f);
        h.lx = x; h.ly = y; h.lz = z; h.lastT = now;
        float wx, wz, wind;
        WindNow(&wx, &wz, &wind);
        const float ax = wx - vx, az = wz - vz;
        const float yaw = pl->actor.shape.rot.y * (3.14159265f / 32768.0f), c = std::cos(yaw), sn = std::sin(yaw);
        // The air in Link's frame: x to his left, z in front of him. (Running forward makes air stream back over the cap.)
        // Link's own acceleration (in his frame) kicks the cloth the opposite way: it lags behind a start, and swings forward when he stops or lands.
        const float dvx = vx - h.lvx, dvz = vz - h.lvz, dvy = vy - h.lvy;
        h.lvx = vx; h.lvz = vz; h.lvy = vy;
        // How fast he is turning (radians a second, positive to his left): the cap drags and rolls behind a turn and the skirt swings out.
        const int16_t yawNow = pl->actor.shape.rot.y;
        const float turn = std::clamp(static_cast<int16_t>(yawNow - h.lyaw) * (3.14159265f / 32768.0f) / dt, -14.0f, 14.0f) * gClothScale;
        h.lyaw = yawNow;
        const float accSide = dvx * c - dvz * sn, accFore = dvx * sn + dvz * c;
        const float airX = (ax * c - az * sn) * gClothScale, airZ = (ax * sn + az * c) * gClothScale;
        const float step = std::min(dt, 0.1f), t = static_cast<float>(now);
        const float phase = static_cast<float>(reinterpret_cast<uintptr_t>(pl) % 61);
        h.spring.Step(step, airX, airZ, -vy * gClothScale, wind * gClothScale, t, phase,
                      (accFore * 0.0035f + dvy * 0.0016f) * gClothScale, accSide * 0.0035f * gClothScale, turn);
        // Ground speed drives the stride's flap. In the water or hanging on a ledge, the stride is not a run, so it doesn't flap.
        const bool stride = (pl->actor.bgCheckFlags & 1) && !(pl->stateFlags1 & (PLAYER_STATE1_IN_WATER | PLAYER_STATE1_HANGING_OFF_LEDGE | PLAYER_STATE1_CLIMBING_LADDER));
        const float ground = stride ? std::sqrt(vx * vx + vz * vz) * gClothScale : 0.0f;
        const float fall = std::max(0.0f, -vy) * gClothScale;
        // A landing (a sudden stop in falling) throws the skirt up-and-forward too.
        const float landKick = std::max(0.0f, dvy) * 0.6f;
        h.skirt.Step(royale::kSkirtSwing, step, airX, airZ, ground, fall, wind * gClothScale, t, phase, (accFore - landKick) * gClothScale, accSide * gClothScale, turn);
        h.sheath.Step(royale::kSheathSwing, step, airX, airZ, ground, fall, wind * gClothScale, t, phase + 2.0f, (accFore - landKick) * gClothScale, accSide * gClothScale, turn);
    }
    return h;
}

// Bots are drawn as Player actors too, but their clothes (and the wind on them) are left alone: only people's clothes move.
bool IsBotActor(const void* actor) {
    const auto id = gPuppetOf.find(static_cast<const Actor*>(actor));
    if (id == gPuppetOf.end()) return false;
    const auto st = gState.find(id->second);
    return st != gState.end() && st->second.isBot;
}

void OnPlayerHatLimb(void* playerPtr, int16_t* rot) {
    Feat("cap cloth");
    if (!DebugOn(kDbgCloth)) return;
    gHatHookCalls++;
    if (gClothScale <= 0.01f || playerPtr == nullptr || !InGame() || IsBotActor(playerPtr)) return;
    HatState& h = StepClothes(static_cast<const Player*>(playerPtr));
    const float toBinary = 32768.0f / 3.14159265f;
    gHatLastSwing = h.spring.fore * 57.2958f;
    rot[2] = static_cast<int16_t>(rot[2] + static_cast<int>(h.spring.fore * toBinary));   // fore and aft: the limb's pitch
    rot[1] = static_cast<int16_t>(rot[1] + static_cast<int>(h.spring.side * toBinary));   // sideways: its yaw
    rot[0] = static_cast<int16_t>(rot[0] + static_cast<int>(h.spring.twist * toBinary));  // roll along its length, thrown by turning
}

// The tunic's skirt is part of the waist limb, which every other limb hangs from. So the waist is swung, and each limb hanging straight off it
// turns back by exactly the same amount before its own joint is applied: the skirt sways, the legs and the upper body stay where the
// animation put them. Which limbs hang off the waist is read from the skeleton itself (custom Link models may differ), once per skeleton.
struct WaistSwing {
    const void* player = nullptr;
    s16 before[3] = {}, after[3] = {};
    bool active = false;
};
WaistSwing gWaistSwing;
std::unordered_map<const void*, uint32_t> gWaistChildren;   // skeleton -> bit mask of the limbs hanging straight off the waist

uint32_t WaistChildrenOf(const Player* pl) {
    void** skel = pl->skelAnime.skeleton;
    if (skel == nullptr) return 0;
    auto it = gWaistChildren.find(skel);
    if (it != gWaistChildren.end()) return it->second;
    uint32_t mask = 0;
    const int count = std::min<int>(pl->skelAnime.limbCount, 31);
    const LodLimb* waist = static_cast<const LodLimb*>(SEGMENTED_TO_VIRTUAL(skel[PLAYER_LIMB_WAIST - 1]));
    if (waist != nullptr && count >= PLAYER_LIMB_WAIST) {
        int child = waist->child;
        for (int guard = 0; child != LIMB_DONE && child < count && guard < 32; guard++) {
            mask |= 1u << (child + 1);   // the skeleton counts from 0, the draw hooks from 1
            const LodLimb* limb = static_cast<const LodLimb*>(SEGMENTED_TO_VIRTUAL(skel[child]));
            if (limb == nullptr) break;
            child = limb->sibling;
        }
    }
    if (gWaistChildren.size() > 16) gWaistChildren.clear();
    gWaistChildren[skel] = mask;
    return mask;
}

bool SheathHasChildren(const Player* pl) {
    void** skel = pl->skelAnime.skeleton;
    if (skel == nullptr || pl->skelAnime.limbCount < PLAYER_LIMB_SHEATH) return true;
    const LodLimb* sheath = static_cast<const LodLimb*>(SEGMENTED_TO_VIRTUAL(skel[PLAYER_LIMB_SHEATH - 1]));
    return sheath == nullptr || sheath->child != LIMB_DONE;
}

void OnPlayerClothLimb(void* playerPtr, int32_t limbIndex, int16_t* rot) {
    Feat("tunic and sheath cloth");
    if (!DebugOn(kDbgCloth)) return;
    if (limbIndex == PLAYER_LIMB_WAIST) {
        gClothLimbCalls++;
        gWaistSwing.active = false;
        if (gClothScale <= 0.01f || playerPtr == nullptr || !InGame() || IsBotActor(playerPtr)) return;
        const Player* pl = static_cast<const Player*>(playerPtr);
        if (WaistChildrenOf(pl) == 0) return;   // can't tell what hangs off the waist: leave this model alone
        HatState& h = StepClothes(pl);
        const float toBinary = 32768.0f / 3.14159265f;
        gSkirtLastSwing = h.skirt.fore * 57.2958f;
        gWaistSwing.player = playerPtr;
        for (int i = 0; i < 3; i++) gWaistSwing.before[i] = rot[i];
        rot[2] = static_cast<int16_t>(rot[2] + static_cast<int>(h.skirt.fore * toBinary));   // fore and aft
        rot[1] = static_cast<int16_t>(rot[1] + static_cast<int>(h.skirt.side * toBinary));   // sideways
        for (int i = 0; i < 3; i++) gWaistSwing.after[i] = rot[i];
        gWaistSwing.active = true;
        return;
    }
    if (!gWaistSwing.active || playerPtr == nullptr) return;
    if (limbIndex == PLAYER_LIMB_SHEATH && gWaistSwing.player == playerPtr && !SheathHasChildren(static_cast<const Player*>(playerPtr))) {
        auto it = gHats.find(playerPtr);
        if (it != gHats.end()) {
            const float toBinary = 32768.0f / 3.14159265f;
            rot[2] = static_cast<int16_t>(rot[2] + static_cast<int>(it->second.sheath.fore * toBinary));
            rot[1] = static_cast<int16_t>(rot[1] + static_cast<int>(it->second.sheath.side * toBinary));
        }
    }
    if (gWaistSwing.player != playerPtr || limbIndex < 0 || limbIndex > 31) return;
    if (!(WaistChildrenOf(static_cast<const Player*>(playerPtr)) & (1u << limbIndex))) return;
    // Undo the waist's swung joint rotation (it was applied Z, then Y, then X), then apply the unswung one: this limb's frame is exactly
    // what it would have been.
    const float toRad = 3.14159265f / 32768.0f;
    Matrix_RotateX(-gWaistSwing.after[0] * toRad, MTXMODE_APPLY);
    Matrix_RotateY(-gWaistSwing.after[1] * toRad, MTXMODE_APPLY);
    Matrix_RotateZ(-gWaistSwing.after[2] * toRad, MTXMODE_APPLY);
    Matrix_RotateZYX(gWaistSwing.before[0], gWaistSwing.before[1], gWaistSwing.before[2], MTXMODE_APPLY);
}

void ForgetOldHats() {
    const double now = ImGui::GetTime();
    for (auto it = gHats.begin(); it != gHats.end();) it = now - it->second.seen > 4.0 ? gHats.erase(it) : std::next(it);
}

// ---- the sign in the middle of the map -----------------------------------------------------------------------------------------
// A wooden sign stands at the centre of every map (on the nearest bit of open, walkable ground), and reads out its message when you walk up.
Actor* gSignActor = nullptr;
double gSignReadAt = -100.0;

void Sign_Update(Actor* actor, PlayState* play) {
    actor->focus.pos = actor->world.pos;
    actor->focus.pos.y += 120.0f;
    OfferTalk(actor, play, kTextSign, 220.0f);   // read it with A, in the game's wooden sign box
}
void Sign_Draw(Actor* actor, PlayState* play) {
    const GpuMesh* mesh = GpuMeshFor(royale::MeshKind::Sign, 0);
    if (mesh == nullptr || mesh->dl.empty()) return;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(actor->shape.rot.y * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    Matrix_Scale(1.35f, 1.35f, 1.35f, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(mesh->dl.data()));
    CLOSE_DISPS(play->state.gfxCtx);
}
void Sign_Destroy(Actor* actor, PlayState*) { if (gSignActor == actor) gSignActor = nullptr; }

// The spot for the sign: the walkable ground nearest the middle that is clear of walls, rocks and blocks.
bool FindSignSpot(const royale::Circle& map, royale::Vec2* out) {
    if (!gSession.Client()) return false;
    const auto& props = gSession.Client()->Props();
    auto clear = [&](royale::Vec2 p) {
        if (!WalkableAt(p)) return false;
        for (const royale::Prop& pr : props) {
            const float r = royale::PropRadius(pr.kind);
            if (r > 0.0f && royale::Distance(pr.pos, p) < r + 120.0f) return false;
        }
        return true;
    };
    for (float r = 0.0f; r <= 1100.0f; r += 70.0f) {
        const int n = r < 1.0f ? 1 : std::max(8, static_cast<int>(r / 28.0f));
        for (int i = 0; i < n; i++) {
            const float a = i * 6.2831853f / n;
            const royale::Vec2 p = { map.center.x + std::cos(a) * r, map.center.z + std::sin(a) * r };
            if (clear(p)) { *out = p; return true; }
        }
    }
    return false;
}

void ReconcileSign(const royale::HudState& hud) {
    const bool want = gSession.Joined() && InField() && hud.map.radius > 0 &&
                      (hud.state == royale::MatchState::Countdown || hud.state == royale::MatchState::Drop || hud.state == royale::MatchState::InMatch);
    if (!want) {
        if (gSignActor != nullptr) { Actor_Kill(gSignActor); gSignActor = nullptr; }
        gSignKnown = false;
        return;
    }
    if (gSignActor != nullptr) return;
    if (!gSignKnown) { if (!FindSignSpot(hud.map, &gSignPos)) return; gSignKnown = true; }
    float y = 0;
    if (!FloorAt(gSignPos.x, gSignPos.z, &y)) return;
    Actor* a = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ISHI, gSignPos.x, y, gSignPos.z, 0, 0x2000, 0, 0, false);
    if (a == nullptr) return;
    a->update = Sign_Update;
    a->draw = Sign_Draw;
    a->destroy = Sign_Destroy;
    a->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
    a->uncullZoneForward = 4000.0f; a->uncullZoneScale = 1500.0f; a->uncullZoneDownward = 1500.0f;
    a->shape.shadowScale = 0.0f;
    gSignActor = a;
}

bool SignNear() {
    if (gSignActor == nullptr || !InField()) return false;
    Player* pl = GET_PLAYER(gPlayState);
    return std::hypot(pl->actor.world.pos.x - gSignPos.x, pl->actor.world.pos.z - gSignPos.z) < 220.0f;
}

// A next to Lilo, Maya or the sign when the game did not already start the talk itself: open the same text box directly.
void PressedATalk() {
    if (MayaNear()) { if (StartTalk(gMayaActor, kTextMaya)) TalkToMaya(); }
    else if (LiloNear()) { if (StartTalk(gLiloActor, kTextLilo)) TalkToLilo(); }
    else if (SignNear()) StartTalk(gSignActor, kTextSign);
}

// A label over the sign from a distance; up close, A reads it in the game's own text box.
void DrawSign(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale) {
    (void)ds;
    if (gSignActor == nullptr || !InField()) return;
    Player* pl = GET_PLAYER(gPlayState);
    const float d = std::hypot(pl->actor.world.pos.x - gSignPos.x, pl->actor.world.pos.z - gSignPos.z);
    ImVec2 at;
    if (d >= 260.0f && d < 1500.0f && WorldToScreen(gSignPos.x, gSignActor->world.pos.y + 240.0f, gSignPos.z, &at)) {   // up close, the prompt says to read it
        const char* label = "Sign";
        const float size = std::clamp(26.0f * scale * (1800.0f / (d + 900.0f)), 14.0f * scale, 30.0f * scale);
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label);
        dl->AddText(font, size, ImVec2(at.x - sz.x * 0.5f + 2, at.y + 2), IM_COL32(20, 12, 4, 230), label);
        dl->AddText(font, size, ImVec2(at.x - sz.x * 0.5f, at.y), IM_COL32(255, 232, 160, 255), label);
    }
}

// ---- match music ----------------------------------------------------------------------------------------------------------------
// "Match music" in the lobby menu: 0 the game's own music as usual, 1 a random song from the music folder (with the game's music turned down),
// 2 silence. The game's music volume is put back to the player's setting whenever a match is not on.
bool gBgmMuted = false;
void SetGameBgmVolume(bool muted) {
    if (muted == gBgmMuted) return;
    gBgmMuted = muted;
    gOot.lastVolume = -1;   // a converted song on the second music player keeps its own volume (set again on the next update)
    const float main = muted ? 0.0f : static_cast<float>(CVarGetInteger(CVAR_SETTING("Volume.MainMusic"), 100)) / 100.0f;
    const float sub = muted ? 0.0f : static_cast<float>(CVarGetInteger(CVAR_SETTING("Volume.SubMusic"), 100)) / 100.0f;
    Audio_SetGameVolume(SEQ_PLAYER_BGM_MAIN, main);
    Audio_SetGameVolume(SEQ_PLAYER_BGM_SUB, sub);
}
bool DriveMatchMusic(const royale::HudState& hud, bool joined) {
    const bool live = joined && InField() && (hud.state == royale::MatchState::Countdown || hud.state == royale::MatchState::Drop || hud.state == royale::MatchState::InMatch);
    const bool random = live && gMusicMode == 1;
    if (random && !gLobbyMusic.scanned) ScanMusicFolder();
    const bool haveSongs = !gLobbyMusic.tracks.empty();
    const bool lobbySongs = joined && hud.state == royale::MatchState::Lobby && MapOption("LobbyMusic", true);
    if (lobbySongs && !gLobbyMusic.scanned) ScanMusicFolder();
    SetGameBgmVolume((live && (gMusicMode == 2 || (gMusicMode == 1 && haveSongs))) || (lobbySongs && !gLobbyMusic.tracks.empty() && !gLobbyMusic.failed));
    return random && haveSongs;
}

// ---- the pause menu shows what you carry -----------------------------------------------------------------------------------------
// While a match is on, the game's own pause-menu inventory (items, equipment, quest items and ammo) is rebuilt from what the server says you carry, so
// everything you pick up shows up there and everything you lose goes. Your own save is put back exactly as it was when the match ends or you leave.
// The C-button items are cleared for the match too (the hotbar does their job, and nothing real should fire from them).
struct SavedInventory {
    bool have = false;
    decltype(gSaveContext.inventory) inventory;
    decltype(gSaveContext.equips) equips;
};
SavedInventory gSavedInv;
uint64_t gKitHash = 0;

void GiveToSave(royale::ItemId id, int& bottles, int& tunics) {
    using royale::ItemId;
    auto put = [&](int item) { gSaveContext.inventory.items[SLOT(item)] = static_cast<u8>(item); };
    auto equip = [&](int type, int value) { gSaveContext.inventory.equipment |= OWNED_EQUIP_FLAG(type, value); };
    auto quest = [&](int q) { gSaveContext.inventory.questItems |= gBitFlags[q]; };
    auto bottle = [&](int item) { if (bottles < 4) gSaveContext.inventory.items[SLOT_BOTTLE_1 + bottles++] = static_cast<u8>(item); };
    (void)tunics;
    switch (id) {
        case ItemId::DekuStick: put(ITEM_STICK); break;
        case ItemId::KokiriSword: case ItemId::BasicSword: equip(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_KOKIRI); break;
        case ItemId::MasterSword: equip(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_MASTER); break;
        case ItemId::BiggoronSword: equip(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_BIGGORON); break;
        case ItemId::MegatonHammer: case ItemId::GiantsHammer: put(ITEM_HAMMER); break;
        case ItemId::Slingshot: case ItemId::TripleSlingshot: put(ITEM_SLINGSHOT); break;
        case ItemId::FairyBow: put(ITEM_BOW); break;
        case ItemId::Boomerang: put(ITEM_BOOMERANG); break;
        case ItemId::Bombs: put(ITEM_BOMB); break;
        case ItemId::Bombchus: case ItemId::HomingBombchus: put(ITEM_BOMBCHU); break;
        case ItemId::DekuNuts: put(ITEM_NUT); break;
        case ItemId::FireArrows: put(ITEM_BOW_ARROW_FIRE); break;
        case ItemId::IceArrows: put(ITEM_BOW_ARROW_ICE); break;
        case ItemId::LightArrows: put(ITEM_BOW_ARROW_LIGHT); break;
        case ItemId::DekuShield: equip(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_DEKU); break;
        case ItemId::HylianShield: equip(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_HYLIAN); break;
        case ItemId::MirrorShield: equip(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_MIRROR); break;
        case ItemId::GreenPotion: bottle(ITEM_POTION_GREEN); break;
        case ItemId::RedPotion: bottle(ITEM_POTION_RED); break;
        case ItemId::BluePotion: bottle(ITEM_POTION_BLUE); break;
        case ItemId::Fairy: bottle(ITEM_FAIRY); break;
        case ItemId::Milk: bottle(ITEM_MILK_BOTTLE); break;
        case ItemId::Fish: bottle(ITEM_FISH); break;
        case ItemId::BlueFire: bottle(ITEM_BLUE_FIRE); break;
        case ItemId::Bug: bottle(ITEM_BUG); break;
        case ItemId::Poe: bottle(ITEM_POE); break;
        case ItemId::SmallShieldPotion: case ItemId::LargeShieldPotion: bottle(ITEM_BOTTLE); break;
        case ItemId::DinsFire: put(ITEM_DINS_FIRE); break;
        case ItemId::FaroresWind: put(ITEM_FARORES_WIND); break;
        case ItemId::NayrusLove: put(ITEM_NAYRUS_LOVE); break;
        case ItemId::Hookshot: put(ITEM_HOOKSHOT); break;
        case ItemId::Longshot: put(ITEM_LONGSHOT); break;
        case ItemId::LensOfTruth: put(ITEM_LENS); break;
        case ItemId::MagicBeans: put(ITEM_BEAN); break;
        case ItemId::FairyOcarina: put(ITEM_OCARINA_FAIRY); break;
        case ItemId::OcarinaOfTime: put(ITEM_OCARINA_TIME); break;
        case ItemId::ZeldasLullaby: quest(QUEST_SONG_LULLABY); break;
        case ItemId::EponasSong: quest(QUEST_SONG_EPONA); break;
        case ItemId::SariasSong: quest(QUEST_SONG_SARIA); break;
        case ItemId::SunsSong: quest(QUEST_SONG_SUN); break;
        case ItemId::SongOfTime: quest(QUEST_SONG_TIME); break;
        case ItemId::SongOfStorms: quest(QUEST_SONG_STORMS); break;
        case ItemId::MinuetOfForest: quest(QUEST_SONG_MINUET); break;
        case ItemId::BoleroOfFire: quest(QUEST_SONG_BOLERO); break;
        case ItemId::SerenadeOfWater: quest(QUEST_SONG_SERENADE); break;
        case ItemId::NocturneOfShadow: quest(QUEST_SONG_NOCTURNE); break;
        case ItemId::RequiemOfSpirit: quest(QUEST_SONG_REQUIEM); break;
        case ItemId::PreludeOfLight: quest(QUEST_SONG_PRELUDE); break;
        case ItemId::KokiriTunic: equip(EQUIP_TYPE_TUNIC, EQUIP_VALUE_TUNIC_KOKIRI); break;
        case ItemId::GoronTunic: equip(EQUIP_TYPE_TUNIC, EQUIP_VALUE_TUNIC_GORON); break;
        case ItemId::ZoraTunic: equip(EQUIP_TYPE_TUNIC, EQUIP_VALUE_TUNIC_ZORA); break;
        case ItemId::KokiriBoots: equip(EQUIP_TYPE_BOOTS, EQUIP_VALUE_BOOTS_KOKIRI); break;
        case ItemId::IronBoots: equip(EQUIP_TYPE_BOOTS, EQUIP_VALUE_BOOTS_IRON); break;
        case ItemId::HoverBoots: equip(EQUIP_TYPE_BOOTS, EQUIP_VALUE_BOOTS_HOVER); break;
        case ItemId::GoronBracelet: Inventory_ChangeUpgrade(UPG_STRENGTH, 1); break;
        case ItemId::SilverGauntlets: Inventory_ChangeUpgrade(UPG_STRENGTH, 2); break;
        case ItemId::GoldenGauntlets: Inventory_ChangeUpgrade(UPG_STRENGTH, 3); break;
        case ItemId::SilverScale: Inventory_ChangeUpgrade(UPG_SCALE, 1); break;
        case ItemId::GoldenScale: Inventory_ChangeUpgrade(UPG_SCALE, 2); break;
        case ItemId::BigQuiver: Inventory_ChangeUpgrade(UPG_QUIVER, 3); break;
        case ItemId::BulletBag: Inventory_ChangeUpgrade(UPG_BULLET_BAG, 3); break;
        case ItemId::BombBag: Inventory_ChangeUpgrade(UPG_BOMB_BAG, 3); break;
        case ItemId::KeatonMask: gSaveContext.inventory.items[SLOT_TRADE_CHILD] = ITEM_MASK_KEATON; break;
        case ItemId::SkullMask: gSaveContext.inventory.items[SLOT_TRADE_CHILD] = ITEM_MASK_SKULL; break;
        case ItemId::SpookyMask: gSaveContext.inventory.items[SLOT_TRADE_CHILD] = ITEM_MASK_SPOOKY; break;
        case ItemId::BunnyHood: gSaveContext.inventory.items[SLOT_TRADE_CHILD] = ITEM_MASK_BUNNY; break;
        case ItemId::GoronMask: gSaveContext.inventory.items[SLOT_TRADE_CHILD] = ITEM_MASK_GORON; break;
        case ItemId::ZoraMask: gSaveContext.inventory.items[SLOT_TRADE_CHILD] = ITEM_MASK_ZORA; break;
        case ItemId::GerudoMask: gSaveContext.inventory.items[SLOT_TRADE_CHILD] = ITEM_MASK_GERUDO; break;
        case ItemId::MaskOfTruth: gSaveContext.inventory.items[SLOT_TRADE_CHILD] = ITEM_MASK_TRUTH; break;
        case ItemId::ForestMedallion: quest(QUEST_MEDALLION_FOREST); break;
        case ItemId::FireMedallion: quest(QUEST_MEDALLION_FIRE); break;
        case ItemId::WaterMedallion: quest(QUEST_MEDALLION_WATER); break;
        case ItemId::SpiritMedallion: quest(QUEST_MEDALLION_SPIRIT); break;
        case ItemId::ShadowMedallion: quest(QUEST_MEDALLION_SHADOW); break;
        case ItemId::LightMedallion: quest(QUEST_MEDALLION_LIGHT); break;
        case ItemId::KokiriEmerald: quest(QUEST_KOKIRI_EMERALD); break;
        case ItemId::GoronRuby: quest(QUEST_GORON_RUBY); break;
        case ItemId::ZoraSapphire: quest(QUEST_ZORA_SAPPHIRE); break;
        default: break;   // hearts, magic, rupees, grenades and the like have no place in the pause menu
    }
}

void SyncPauseInventory(const royale::HudState& hud) {
    const bool active = gSession.Joined() && IsLive(hud) && hud.haveSelf;
    if (!active) {
        if (gSavedInv.have) {   // the match is over: your own inventory comes back untouched
            gSaveContext.inventory = gSavedInv.inventory;
            gSaveContext.equips = gSavedInv.equips;
            gSavedInv.have = false;
            gKitHash = 0;
            if (InGame()) Player_SetEquipmentData(gPlayState, GET_PLAYER(gPlayState));   // and Link wears your own shield again
        }
        return;
    }
    if (!gSavedInv.have) {
        gSavedInv.inventory = gSaveContext.inventory;
        gSavedInv.equips = gSaveContext.equips;
        gSavedInv.have = true;
        gKitHash = 0;
    }
    // Cheap change detection: the whole kit folded into one number.
    uint64_t hsh = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { hsh = (hsh ^ v) * 1099511628211ull; };
    mix(static_cast<uint64_t>(hud.weapon)); mix(hud.hasShield ? static_cast<uint64_t>(hud.shield) + 1 : 0);
    for (const auto& r : hud.inv.reserve) mix(100 + r.item);
    for (const auto& pt : hud.inv.potions) mix(200 + pt.item);
    if (hud.inv.hasAbility) mix(300 + hud.inv.ability.item);
    mix(hud.inv.gearMask);
    for (int sl = 0; sl < royale::kGearSlots; sl++) if (hud.inv.gearMask & (1 << sl)) mix(400 + hud.inv.gear[sl].item);
    for (int a = 0; a < 5; a++) mix(500 + static_cast<uint64_t>(std::min(127, std::max(0, static_cast<int>(hud.ammo[a])))));
    if (hsh == gKitHash) { for (int i = 1; i <= 3; i++) gSaveContext.equips.buttonItems[i] = ITEM_NONE; return; }
    gKitHash = hsh;

    auto& inv = gSaveContext.inventory;
    for (auto& it : inv.items) it = ITEM_NONE;
    for (auto& am : inv.ammo) am = 0;
    inv.equipment = 0;
    inv.upgrades &= ~(gUpgradeMasks[UPG_QUIVER] | gUpgradeMasks[UPG_BOMB_BAG] | gUpgradeMasks[UPG_STRENGTH] | gUpgradeMasks[UPG_BULLET_BAG] | gUpgradeMasks[UPG_SCALE]);
    inv.questItems &= ~(gBitFlags[QUEST_MEDALLION_FOREST] | gBitFlags[QUEST_MEDALLION_FIRE] | gBitFlags[QUEST_MEDALLION_WATER] | gBitFlags[QUEST_MEDALLION_SPIRIT] |
                        gBitFlags[QUEST_MEDALLION_SHADOW] | gBitFlags[QUEST_MEDALLION_LIGHT] | gBitFlags[QUEST_KOKIRI_EMERALD] | gBitFlags[QUEST_GORON_RUBY] | gBitFlags[QUEST_ZORA_SAPPHIRE] |
                        gBitFlags[QUEST_SONG_MINUET] | gBitFlags[QUEST_SONG_BOLERO] | gBitFlags[QUEST_SONG_SERENADE] | gBitFlags[QUEST_SONG_REQUIEM] | gBitFlags[QUEST_SONG_NOCTURNE] |
                        gBitFlags[QUEST_SONG_PRELUDE] | gBitFlags[QUEST_SONG_LULLABY] | gBitFlags[QUEST_SONG_EPONA] | gBitFlags[QUEST_SONG_SARIA] | gBitFlags[QUEST_SONG_SUN] |
                        gBitFlags[QUEST_SONG_TIME] | gBitFlags[QUEST_SONG_STORMS]);
    int bottles = 0, tunics = 0;
    GiveToSave(hud.weapon, bottles, tunics);
    if (hud.hasShield) GiveToSave(hud.shield, bottles, tunics);
    for (const auto& r : hud.inv.reserve) GiveToSave(static_cast<royale::ItemId>(r.item), bottles, tunics);
    for (const auto& pt : hud.inv.potions) GiveToSave(static_cast<royale::ItemId>(pt.item), bottles, tunics);
    if (hud.inv.hasAbility) GiveToSave(static_cast<royale::ItemId>(hud.inv.ability.item), bottles, tunics);
    for (int sl = 0; sl < royale::kGearSlots; sl++) if (hud.inv.gearMask & (1 << sl)) GiveToSave(static_cast<royale::ItemId>(hud.inv.gear[sl].item), bottles, tunics);
    // Ammo: the counts the server keeps, shown against the weapon that uses them (a bag big enough for them is given too).
    auto ammo = [&](royale::AmmoKind k, int item, int upgrade) {
        const int n = std::min(127, std::max(0, static_cast<int>(hud.ammo[static_cast<size_t>(k)])));
        if (n > 0 && inv.items[SLOT(item)] == item) {
            inv.ammo[SLOT(item)] = static_cast<s8>(n);
            if (upgrade >= 0 && CUR_UPG_VALUE(upgrade) == 0) Inventory_ChangeUpgrade(upgrade, n > 30 ? 3 : n > 20 ? 2 : 1);
        }
    };
    ammo(royale::AmmoKind::Arrows, ITEM_BOW, UPG_QUIVER);
    ammo(royale::AmmoKind::Seeds, ITEM_SLINGSHOT, UPG_BULLET_BAG);
    ammo(royale::AmmoKind::Bombs, ITEM_BOMB, UPG_BOMB_BAG);
    ammo(royale::AmmoKind::Bombchus, ITEM_BOMBCHU, -1);
    ammo(royale::AmmoKind::Nuts, ITEM_NUT, -1);
    if (inv.items[SLOT(ITEM_BOW)] == ITEM_BOW && CUR_UPG_VALUE(UPG_QUIVER) == 0) Inventory_ChangeUpgrade(UPG_QUIVER, 1);
    if (inv.items[SLOT(ITEM_SLINGSHOT)] == ITEM_SLINGSHOT && CUR_UPG_VALUE(UPG_BULLET_BAG) == 0) Inventory_ChangeUpgrade(UPG_BULLET_BAG, 1);
    if (inv.items[SLOT(ITEM_BOMB)] == ITEM_BOMB && CUR_UPG_VALUE(UPG_BOMB_BAG) == 0) Inventory_ChangeUpgrade(UPG_BOMB_BAG, 1);
    for (int i = 1; i <= 3; i++) gSaveContext.equips.buttonItems[i] = ITEM_NONE;
}

void OnGameFrameUpdate() {
    EnsureHudWindow();
    // Game logic runs at 20 Hz, the same rate as the server tick, so one call is one step.
    Feat("session update"); gSession.Update(1.0f / royale::kTickHz);
    if (gTravelCooldown > 0) gTravelCooldown--;
    Feat("song melody"); if (DebugOn(kDbgMusic)) UpdateSongMelody();

    royale::HudState hud = gSession.Hud();
    bool joined = gSession.Joined();
    if (joined) gMapId = royale::ClampMap(hud.mapId);

    if (gHealthOverridden && !(joined && IsLive(hud))) RestoreHealth();
    Feat("platforms, rocks and trees"); if (DebugOn(kDbgTerrain) && joined && InField() && gPlayState != nullptr) { ApplyPlatforms(GET_PLAYER(gPlayState)); ApplyRocks(GET_PLAYER(gPlayState)); ApplyTrees(GET_PLAYER(gPlayState)); }
    Feat("tornado"); if (DebugOn(kDbgTornado) && gTornadoOn && InField() && gPlayState != nullptr) UpdateTornado(GET_PLAYER(gPlayState)); else gTornado.active = false;
    Feat("storm"); DriveStorm(hud);
    Feat("weather"); if (DebugOn(kDbgWeather)) DriveRealWeather();   // also before the player's own update, so it never sees itself as airborne
    Feat("tunic colour"); ApplyLocalTunic(joined && hud.state != royale::MatchState::Lobby && InField());
    NoticeRoyaleFile();
    Feat("pause inventory"); SyncPauseInventory(hud);
    Feat("chicken music"); if (DebugOn(kDbgMusic)) UpdateChickenMusic();
    Feat("music scan"); if (gScanRequested) { gScanRequested = false; ScanMusicFolder(); }   // asked for by the menu (which draws on another thread)
    Feat("lobby and match music"); if (DebugOn(kDbgMusic)) UpdateLobbyMusic(joined && hud.state == royale::MatchState::Lobby, DriveMatchMusic(hud, joined));
    Feat("lobby timer"); DriveLobbyTimer(hud);
    Feat("time of day"); if (DebugOn(kDbgTimeOfDay)) DriveTimeOfDay(hud);
    Feat("boss effects"); if (DebugOn(kDbgBossFx)) UpdateBossWorldFx();
    Feat("sign"); ReconcileSign(hud);
    Feat("messages"); RegisterRoyaleMessages();
    Feat("Maya"); if (DebugOn(kDbgAllies)) ReconcileMaya(hud);
    Feat("Lilo"); if (DebugOn(kDbgAllies)) ReconcileLilo(hud);
    Feat("cat pet"); if (DebugOn(kDbgAllies)) ReconcileCatPet(hud);
    Feat("Lilo effects"); if (DebugOn(kDbgAllies)) UpdateLiloFx();
    Feat("allies"); if (DebugOn(kDbgAllies)) ReconcileAllies(hud);
    Feat("carts"); if (DebugOn(kDbgCarts)) ReconcileCarts(hud);
    { static unsigned frames = 0; if (++frames % 100 == 0) ForgetOldHats(); }
    Feat("projectiles"); ReconcileProjectileActor();
    Feat("storm alerts"); DriveStormAlerts(hud);
    gStateNow = hud.state;
    Feat("sealed exits"); SealExits(hud);
    Feat("minimap switch"); if (DebugOn(kDbgMinimap)) DriveMinimapSwitch(joined && IsLive(hud) && InGame() && InField());

    // Just joined a lobby: head for the waiting room if the player wants that.
    if (joined && !gWasJoined) {
        Trace("lobby: joined");
        gLastEpoch = gSession.Client()->Epoch();
        WantsWaitingRoom = CVarGetInteger(CVAR_SETTING("Royale.WaitingRoom"), 1) != 0;
        Say(hud.isHost ? "Lobby open. Share your address from the Battle Royale menu" : "Joined the lobby");
    }
    if (!joined && gWasJoined) {
        WantsWaitingRoom = false;
        gLastCountdownShown = -1;
    }
    gWasJoined = joined;

    if (joined && InGame()) {
        if (hud.state == royale::MatchState::Lobby) {
            if (WantsWaitingRoom) {
                if (InWaitingRoom()) WantsWaitingRoom = false;
                else GoToWaitingRoom();
            }
        } else if (MustBeInField(hud.state) && !InField()) {
            GoToField(); // the match is on; everyone fights in Hyrule Field
        }
    }

    if (hud.state != gLastState) {
        if (hud.state == royale::MatchState::Countdown && joined) {
            KillAllEnemies();
            WantsWaitingRoom = false;
        }
        gLastState = hud.state;
        gLastCountdownShown = -1;
    }

    // Count down out loud so nobody has to keep the menu open.
    if (joined && hud.state == royale::MatchState::Countdown) {
        int left = static_cast<int>(std::ceil(hud.countdownLeft));
        if (left != gLastCountdownShown && (left == 10 || left == 5 || (left >= 1 && left <= 3))) Say("Match starts in " + std::to_string(left));
        gLastCountdownShown = left;
    }

    Feat("match start"); DriveStart(hud);
    Feat("match events"); ReportEvents(hud);
    Feat("other players"); ReconcilePuppets(hud.state);
    Feat("loot"); if (DebugOn(kDbgLoot)) ReconcileLoot(hud);
    Feat("props"); if (DebugOn(kDbgProps)) ReconcileProps(hud);
    Feat("bosses"); ReconcileBosses(hud);
    Feat("between updates");
}

void OnSceneInit(int16_t) {
    Feat("scene init");
    Trace("scene: init");
    gOurTravel = false;
    // Scene change destroys every puppet actor, so forget them all.
    gPuppetOf.clear();
    gActorOf.clear();
    gPlaying.clear();
    gMotion.clear();
    gLastFind.clear();
    gPlate.clear();
    gEmote = EmoteState{};   // the old Link is gone with the scene
    CloseEmoteWheel();
    gCorpses.clear();
    gCorpseOf.clear();
    gLastSeen.clear();
    gSpawningPuppet = 0;
    gLoot.clear();
    gLootOf.clear();
    gProps.clear();
    gPropOf.clear();
    gCulledProps.clear();
    gInFieldFrames = 0;
    gAngry.clear();
    gSolidActor = nullptr; gSolidBgId = -1; gSolidFailed = false; gSolidSet.clear();   // the scene's collision (and our actor with it) is gone
    gFortniteActor = nullptr; gFortniteArrived = false;
    ForgetCarts();   // the cart actors went with the scene
}

void RegisterRoyaleMod() {
    InstallCrashReporter();
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(OnGameFrameUpdate);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerUpdate>(OnPlayerUpdate);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>(OnSceneInit);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameStateMainStart>(OnEmoteWheelInput);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnZTitleInit>([](void*) { EnsureHudWindow(); });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameStateMainStart>([]() { EnsureHudWindow(); });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerHatLimb>(OnPlayerHatLimb);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerClothLimb>(OnPlayerClothLimb);
    // The server owns health during a match, so whatever the game itself does to it (a long fall, lava, a void out) is undone on the spot.
    // Otherwise a hit that takes it to 0 starts the game's own death and game-over screen before OnPlayerUpdate can put it back.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerHealthChange>([](int16_t) {
        if (gHealthOverridden) gSaveContext.health = gMatchHealth;
    });
    // The game's C-button icons (top right) and D-pad item icons are hidden during a match: the hotbar does their job.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnInterfaceUpdate>([]() {
        if (gPlayState == nullptr || !gSession.Joined() || !IsLive(gSession.Hud())) return;
        InterfaceContext& ic = gPlayState->interfaceCtx;
        ic.cLeftAlpha = ic.cDownAlpha = ic.cRightAlpha = 0;
        ic.dpadUpAlpha = ic.dpadDownAlpha = ic.dpadLeftAlpha = ic.dpadRightAlpha = 0;
    });

    // Turn the Player actor we spawn for a remote player into a puppet *before* its init runs. Requires patches/0001.
    GameInteractor::Instance->RegisterGameHookForID<GameInteractor::ShouldActorInit>(
        ACTOR_PLAYER, [](void* actorRef, bool* should) {
            if (gSpawningPuppet == 0) return; // the real player: leave alone
            Actor* actor = (Actor*)actorRef;
            gPuppetOf[actor] = gSpawningPuppet;
            // The actor was already added to the player list; move it so GET_PLAYER still finds the real Link.
            Actor_ChangeCategory(gPlayState, &gPlayState->actorCtx, actor, ACTORCAT_NPC);
            actor->id = ACTOR_EN_OE2;
            actor->category = ACTORCAT_NPC;
            actor->init = Puppet_Init;
            actor->update = Puppet_Update;
            actor->draw = Puppet_Draw;
            actor->destroy = Puppet_Destroy;
            if (gSpawningPuppet >= kCorpseIdBase) { actor->update = Corpse_Update; actor->draw = Corpse_Draw; } // a body, not a live player
        });

    // The Fortnite map has no use for the field's own scenery (grass, rocks, trees, cows): it stands on the old field's ground, not the island's.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneSpawnActors>([]() {
        if (!gFortniteScene || gPlayState == nullptr || gPlayState->sceneNum != SCENE_HYRULE_FIELD) return;
        for (int cat = 0; cat < ACTORCAT_MAX; cat++) {
            if (cat == ACTORCAT_PLAYER) continue;
            for (Actor* a = gPlayState->actorCtx.actorLists[cat].head; a != nullptr; a = a->next) Actor_Kill(a);
        }
    });

    // No enemies spawn while in a lobby or match.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::ShouldActorInit>([](void* actorRef, bool* should) {
        Actor* actor = (Actor*)actorRef;
        if (!gSession.Joined()) return;
        if (actor->category == ACTORCAT_ENEMY) *should = false;
        // Smashed rocks and cut bushes would drop the game's own rupees and hearts; the only items in a match come from the server.
        if (actor->id == ACTOR_EN_ITEM00 && !gSpawningLoot) *should = false;
    });
}

static RegisterShipInitFunc royaleInit(RegisterRoyaleMod);

// ---- the Battle Royale menu --------------------------------------------------------------------------------------------

#define ROYALE_CVAR(name) CVAR_SETTING("Royale." name)

struct UiState {
    char name[24] = "Link";
    char address[64] = "";
    int port = royale::net::kDefaultPort;
    bool waitingRoom = true;
    int botDifficulty = 1; // 0 easy, 1 normal, 2 hard
    int playerLimit = royale::kMaxPlayers; // the host's slider
    bool autoStart = true;                 // the lobby starts the match by itself after two minutes
    int mapId = 0;                         // which place to play (host)
    bool majorBoss = true;                 // the map's major boss arrives halfway through (host)
    int weatherSeason = royale::kSeasonRandom;   // 0-3 or kSeasonRandom (host)
    int weatherIntensity = 60;             // 0 = no weather (host)
    int weatherChange = 50;                // how often the weather changes (host)
    int weatherDensity = 100;              // particles drawn on this screen, per cent (local)
    int foliage = 100;                     // grass and trees scattered around, per cent (local)
    bool clothOn = true;                   // cloth physics on hats and gliders at all (local)
    bool windOn = true;                    // a breeze that moves cloth, grass and trees (local)
    bool windStreaks = true;               // streaks in the air that show the wind's direction (local)
    int windStrength = 100;                // how strong the breeze is, per cent (local)
    int clothPhysics = 100;                // how much cloth and wind physics the cap and glider get, per cent (local)
    int musicMode = 0;                     // match music: 0 the game's, 1 random from the music folder, 2 none (local)
    int skin = 0;          // index into royale::kSkins, or royale::kCustomSkin
    float customTunic[3] = { 0.12f, 0.41f, 0.11f };
    bool showCustomize = false;
    bool loaded = false;
    bool showPosition = false;
    std::string error;
    std::string exportMsg;   // result of the last map export
    std::vector<std::string> localAddresses;
    int addressAge = 1 << 30; // frames since localAddresses was refreshed
};

uint32_t SelectedTunic(const UiState& ui) {
    if (ui.skin == royale::kCustomSkin) {
        auto byte = [](float f) { return static_cast<uint8_t>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
        return royale::PackRgb(byte(ui.customTunic[0]), byte(ui.customTunic[1]), byte(ui.customTunic[2]));
    }
    return royale::SkinRgb(ui.skin);
}

UiState& Ui() {
    static UiState ui;
    if (!ui.loaded) {
        ui.loaded = true;
        std::snprintf(ui.name, sizeof(ui.name), "%s", CVarGetString(ROYALE_CVAR("Name"), "Link"));
        std::snprintf(ui.address, sizeof(ui.address), "%s", CVarGetString(ROYALE_CVAR("Address"), ""));
        ui.port = CVarGetInteger(ROYALE_CVAR("Port"), royale::net::kDefaultPort);
        ui.waitingRoom = CVarGetInteger(ROYALE_CVAR("WaitingRoom"), 1) != 0;
        ui.botDifficulty = std::clamp(CVarGetInteger(ROYALE_CVAR("BotDifficulty"), 1), 0, 2);
        gSession.SetBotDifficulty(static_cast<royale::BotDifficulty>(ui.botDifficulty));
        ui.playerLimit = std::clamp(CVarGetInteger(ROYALE_CVAR("PlayerLimit"), royale::kMaxPlayers), royale::kMinPlayers, royale::kMaxPlayers);
        ui.autoStart = CVarGetInteger(ROYALE_CVAR("AutoStart"), 1) != 0;
        ui.mapId = royale::ClampMap(CVarGetInteger(ROYALE_CVAR("Map"), 0));
        ui.majorBoss = CVarGetInteger(ROYALE_CVAR("MajorBoss"), 1) != 0;
        ui.weatherSeason = std::clamp(CVarGetInteger(ROYALE_CVAR("WeatherSeason"), royale::kSeasonRandom), 0, static_cast<int>(royale::kSeasonRandom));
        ui.weatherIntensity = std::clamp(CVarGetInteger(ROYALE_CVAR("WeatherIntensity"), 60), 0, 100);
        ui.weatherChange = std::clamp(CVarGetInteger(ROYALE_CVAR("WeatherChange"), 50), 0, 100);
        ui.weatherDensity = std::clamp(CVarGetInteger(ROYALE_CVAR("WeatherDensity"), 100), 0, 200);
        gWeatherDensity = ui.weatherDensity / 100.0f;
        ui.foliage = std::clamp(CVarGetInteger(ROYALE_CVAR("Foliage"), 100), 0, 200);
        gFoliage = ui.foliage / 100.0f;
        ui.clothPhysics = std::clamp(CVarGetInteger(ROYALE_CVAR("ClothPhysics"), 100), 0, 200);
        ui.clothOn = CVarGetInteger(ROYALE_CVAR("ClothOn"), 1) != 0;
        ui.windOn = CVarGetInteger(ROYALE_CVAR("WindOn"), 1) != 0;
        ui.windStrength = std::clamp(CVarGetInteger(ROYALE_CVAR("WindStrength"), 100), 0, 200);
        ui.windStreaks = CVarGetInteger(ROYALE_CVAR("WindStreaks"), 1) != 0;
        gWindStreaks = ui.windStreaks;
        gWindOn = ui.windOn;
        gWindScale = ui.windStrength / 100.0f;
        gClothScale = ui.clothOn ? ui.clothPhysics / 100.0f : 0.0f;
        ui.musicMode = std::clamp(CVarGetInteger(ROYALE_CVAR("MusicMode"), 0), 0, 2);
        gMusicMode = ui.musicMode;
        gSession.SetWeatherOptions({ static_cast<uint8_t>(ui.weatherSeason), static_cast<uint8_t>(ui.weatherIntensity), static_cast<uint8_t>(ui.weatherChange) });
        gSession.SelectMap(ui.mapId);
        gSession.SetMajorBoss(ui.majorBoss);
        gSession.SetPlayerLimit(ui.playerLimit);
        gSession.SetAutoStart(ui.autoStart ? royale::kLobbyAutoStartSec : 0.0f);
        ui.skin = std::clamp(CVarGetInteger(ROYALE_CVAR("Skin"), 0), 0, royale::kCustomSkin);
        const uint32_t saved = static_cast<uint32_t>(CVarGetInteger(ROYALE_CVAR("SkinColor"), static_cast<int>(royale::SkinRgb(0))));
        ui.customTunic[0] = royale::RgbR(saved) / 255.0f; ui.customTunic[1] = royale::RgbG(saved) / 255.0f; ui.customTunic[2] = royale::RgbB(saved) / 255.0f;
        gLocalTunic = SelectedTunic(ui);
        gSession.SetTunic(gLocalTunic);
    }
    return ui;
}

void SaveUi(const UiState& ui) {
    CVarSetString(ROYALE_CVAR("Name"), ui.name);
    CVarSetString(ROYALE_CVAR("Address"), ui.address);
    CVarSetInteger(ROYALE_CVAR("Port"), ui.port);
    CVarSetInteger(ROYALE_CVAR("WaitingRoom"), ui.waitingRoom ? 1 : 0);
    CVarSetInteger(ROYALE_CVAR("BotDifficulty"), ui.botDifficulty);
    CVarSetInteger(ROYALE_CVAR("Skin"), ui.skin);
    CVarSetInteger(ROYALE_CVAR("PlayerLimit"), ui.playerLimit);
    CVarSetInteger(ROYALE_CVAR("AutoStart"), ui.autoStart ? 1 : 0);
    CVarSetInteger(ROYALE_CVAR("Map"), ui.mapId);
    CVarSetInteger(ROYALE_CVAR("MajorBoss"), ui.majorBoss ? 1 : 0);
    CVarSetInteger(ROYALE_CVAR("WeatherSeason"), ui.weatherSeason);
    CVarSetInteger(ROYALE_CVAR("WeatherIntensity"), ui.weatherIntensity);
    CVarSetInteger(ROYALE_CVAR("WeatherChange"), ui.weatherChange);
    CVarSetInteger(ROYALE_CVAR("WeatherDensity"), ui.weatherDensity);
    CVarSetInteger(ROYALE_CVAR("Foliage"), ui.foliage);
    CVarSetInteger(ROYALE_CVAR("ClothPhysics"), ui.clothPhysics);
    CVarSetInteger(ROYALE_CVAR("ClothOn"), ui.clothOn ? 1 : 0);
    CVarSetInteger(ROYALE_CVAR("WindOn"), ui.windOn ? 1 : 0);
    CVarSetInteger(ROYALE_CVAR("WindStrength"), ui.windStrength);
    CVarSetInteger(ROYALE_CVAR("WindStreaks"), ui.windStreaks ? 1 : 0);
    CVarSetInteger(ROYALE_CVAR("MusicMode"), ui.musicMode);
    CVarSetInteger(ROYALE_CVAR("SkinColor"), static_cast<int>(SelectedTunic(ui)));
    Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

void RefreshLocalAddresses(UiState& ui, bool force) {
    if (!force && ui.addressAge < 600) { ui.addressAge++; return; }
    ui.localAddresses = royale::net::LocalIPv4Addresses();
    ui.addressAge = 0;
}

const ImVec4 kGold(0.97f, 0.84f, 0.13f, 1.0f);   // Triforce gold
const ImVec4 kGreen(0.42f, 0.78f, 0.36f, 1.0f);  // Kokiri green
const ImVec4 kGrey(0.68f, 0.65f, 0.6f, 1.0f);    // warm stone grey
const ImVec4 kRed(0.93f, 0.36f, 0.28f, 1.0f);    // Goron ruby red

void Heading(const char* text) {
    ImGui::TextColored(kGold, "%s", text);
    ImGui::Separator();
}

const char* CleanName(const char* name) {
    return name[0] != '\0' ? name : "Link";
}

// What the minimap shows. Players and bots are only the ones near you (the server sends the closest dozen), plus everyone while a Lens of
// Truth or Saria's Song is active.
void DrawMinimapOptions() {
    struct Opt { const char* key; const char* label; bool fallback; };
    static const Opt opts[] = {
        { "MapPlayers", "Show other players on the minimap", true },
        { "MapBots", "Show bots on the minimap", true },
        { "MapEnemies", "Show mini bosses (enemies) on the minimap", true },
        { "HeldGlow", "Glow on other players' weapons, coloured by rarity", true },
        { "HeldGlowSelf", "Glow on your own weapon too", false },
        { "LobbyMusic", "Play songs from the music folder in the lobby", true },
        { "OotConvert", "Play the music folder's songs with Ocarina of Time's own instruments (each song is converted once, in the background; off means your songs play as they are)", false },
        { "LiloCat", "Lilo the cat (an Easter egg) sits somewhere on the map", true },
        { "LiloPet", "Lilo follows me around as a pet (only for looks: she changes nothing in the match, and only you see her)", false },
    };
    if (!ImGui::CollapsingHeader("Minimap and game options")) return;
    for (const Opt& o : opts) {
        bool on = MapOption(o.key, o.fallback);
        if (ImGui::Checkbox(o.label, &on)) {
            char key[64];
            std::snprintf(key, sizeof(key), CVAR_SETTING("Royale.%s"), o.key);
            CVarSetInteger(key, on ? 1 : 0);
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
    }
    ImGui::TextColored(kGrey, "Only players near you are known; a Lens of Truth or Saria's Song shows everyone for a while.");
    ImGui::TextColored(kGrey, "Lobby songs: put 16-bit PCM .wav files in this folder (made for you now), then press Rescan.");
    Trace("options: opened");
    if (gLobbyMusic.status.empty()) ImGui::TextColored(kGrey, "Press Rescan to look for songs.");   // not scanned for you: opening this section stays light
    else ImGui::TextWrapped("%s", gLobbyMusic.status.c_str());
    if (OotInstrumentsOn()) ImGui::TextWrapped("%s", OotStatus().c_str());
    if (ImGui::Button("Rescan music folder")) gScanRequested = true;
    ImGui::Spacing();
    ImGui::TextColored(kGrey, "Custom dragon model (replaces Volvagia): put dragon.obj (+ dragon.mtl, dragon.cfg) in the 'models' folder next to the 'music' folder.");
    ImGui::TextWrapped("%s", gDragonModel.status.c_str());
    if (ImGui::Button(gDragonModel.tried ? "Reload custom dragon" : "Look for a custom dragon")) LoadCustomDragon();
    Trace("options: drawn");
}

// What every item and power does, read from the item table (shared/items.h) so it follows the items as they change. Grouped by kind, with the
// game's own icon where it has one, the rarities the item is found at, and its one-line effect.
void DrawItemGuide() {
    if (!ImGui::CollapsingHeader("Items and powers: what each one does")) return;
    static const char* kKindTitle[royale::kItemKindCount] = { "Weapons", "Shields", "Potions and bottles", "Pickups", "Powers: spells, songs and tools", "Gear" };
    static const char* kKindHow[royale::kItemKindCount] = {
        "Held in your hand. B attacks; picking up another weapon drops this one.",
        "Takes a share of the damage you would take.",
        "Kept in your bag (3 slots). D-pad Down drinks one. A Fairy is never drunk: it saves you once.",
        "Used the moment you pick it up. Left on the ground if it would do nothing.",
        "One power at a time. D-pad Up uses it, then it needs time and magic to recharge.",
        "Always on while you wear it. One of each kind: tunic, boots, gauntlets, mask, scale, pack and charm.",
    };
    ImGui::TextColored(kGrey, "Rarer finds are stronger: a Legendary item is %.2f times as strong as a Common one.",
                       royale::kRarityMultiplier[royale::kRarityCount - 1] / royale::kRarityMultiplier[0]);
    for (int k = 0; k < royale::kItemKindCount; k++) {
        if (!ImGui::TreeNode(kKindTitle[k])) continue;
        ImGui::TextColored(kGrey, "%s", kKindHow[k]);
        for (int i = 0; i < royale::kItemCount; i++) {
            const royale::ItemDef& d = royale::kItems[i];
            if (static_cast<int>(d.kind) != k || !royale::InPool(d.id)) continue;
            ImGui::PushID(i);
            if (void* icon = RealIcon(d.id)) { ImGui::Image(reinterpret_cast<ImTextureID>(icon), ImVec2(24, 24)); ImGui::SameLine(); }
            ImGui::TextColored(RarityIm(d.maxRarity), "%s", d.name);
            ImGui::SameLine();
            ImGui::TextColored(kGrey, "(%s)", d.minRarity == d.maxRarity ? RarityName(d.minRarity)
                                                                         : (std::string(RarityName(d.minRarity)) + " to " + RarityName(d.maxRarity)).c_str());
            ImGui::TextWrapped("    %s", d.effect);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
}

// Pick the colour other players see you in. Takes effect the next time you host or join (your colour is sent when you connect).
void DrawCustomize(UiState& ui) {
    ImGui::Spacing();
    ImGui::TextColored(kGold, "Choose a skin");
    bool changed = false;
    for (int i = 0; i < royale::kSkinCount; i++) {
        const royale::Skin& sk = royale::kSkins[i];
        ImGui::PushID(i);
        const ImVec4 col(sk.r / 255.0f, sk.g / 255.0f, sk.b / 255.0f, 1.0f);
        if (ImGui::ColorButton("##swatch", col, ImGuiColorEditFlags_NoTooltip | (ui.skin == i ? ImGuiColorEditFlags_None : ImGuiColorEditFlags_None), ImVec2(28, 28))) { ui.skin = i; changed = true; }
        ImGui::SameLine();
        if (ImGui::Selectable(sk.name, ui.skin == i, 0, ImVec2(190, 28))) { ui.skin = i; changed = true; }
        ImGui::PopID();
    }
    if (ImGui::Selectable("Custom colour", ui.skin == royale::kCustomSkin, 0, ImVec2(220, 24))) { ui.skin = royale::kCustomSkin; changed = true; }
    if (ui.skin == royale::kCustomSkin) changed |= ImGui::ColorEdit3("Tunic colour", ui.customTunic, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
    if (changed) {
        gLocalTunic = SelectedTunic(ui);
        gSession.SetTunic(gLocalTunic);
        SaveUi(ui);
        if (gSession.Joined()) ImGui::TextColored(kGrey, "Other players see the new colour from the next lobby you join.");
    }
    ImGui::TextColored(kGrey, "This changes the colour of Link's tunic. Each player's colour is shown on their character to everyone else.");
    ImGui::Spacing();
}

// The temporary Debug section: one switch per newer feature (see kDebugSwitches). Changes apply at once and are saved.
void DrawDebugSwitches() {
    if (!ImGui::CollapsingHeader("Debug: turn features on or off (temporary)")) return;
    if (!gDebugLoaded) LoadDebugSwitches();
    ImGui::TextColored(kGrey, "If the game crashes or glitches, switch features off one at a time to find the one causing it. Everything is on by default.");
    bool changed = false;
    for (int i = 0; i < kDebugCount; i++) {
        bool on = gDebugOn[i];
        const std::string id = std::string(kDebugSwitches[i].label) + "##dbg" + kDebugSwitches[i].key;
        if (ImGui::Checkbox(id.c_str(), &on)) {
            gDebugOn[i] = on;
            const std::string key = std::string(CVAR_SETTING("Royale.Debug.")) + kDebugSwitches[i].key;
            CVarSetInteger(key.c_str(), on ? 1 : 0);
            changed = true;
        }
    }
    if (ImGui::Button("Turn everything on")) {
        for (int i = 0; i < kDebugCount; i++) {
            gDebugOn[i] = true;
            const std::string key = std::string(CVAR_SETTING("Royale.Debug.")) + kDebugSwitches[i].key;
            CVarSetInteger(key.c_str(), 1);
        }
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Turn everything off")) {
        for (int i = 0; i < kDebugCount; i++) {
            gDebugOn[i] = false;
            const std::string key = std::string(CVAR_SETTING("Royale.Debug.")) + kDebugSwitches[i].key;
            CVarSetInteger(key.c_str(), 0);
        }
        changed = true;
    }
    if (changed) Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    ImGui::Spacing();
}

void DrawMainMenu(UiState& ui, const royale::HudState& h) {
    Heading("OOT ROYALE");
    ImGui::TextWrapped("32 players, a shrinking storm, one winner. Empty spots are filled with bots, so you can play alone.");
    ImGui::Spacing();

    if (!InGame()) {
        ImGui::TextColored(kRed, "Load a save file first (pick a save on the file select screen), then come back here.");
        ImGui::Spacing();
    }
    if (!h.status.empty() && h.status != "Not in a match") ImGui::TextColored(kRed, "%s", h.status.c_str());
    if (!ui.error.empty()) ImGui::TextColored(kRed, "%s", ui.error.c_str());

    if (ImGui::Button(ui.showCustomize ? "Close character menu" : "Customize character", ImVec2(220, 0))) ui.showCustomize = !ui.showCustomize;
    if (ui.showCustomize) DrawCustomize(ui);
    DrawMinimapOptions();
    DrawItemGuide();
    ImGui::Spacing();
    ImGui::Text("Your name");
    ImGui::InputText("##royale_name", ui.name, sizeof(ui.name));
    ImGui::Checkbox("Wait in the Temple of Time while the lobby fills", &ui.waitingRoom);
    {
        bool custom = CustomSceneryOn();
        if (ImGui::Checkbox("Custom rocks and buildings (experimental)", &custom)) {
            CVarSetInteger(CVAR_SETTING("Royale.CustomScenery"), custom ? 1 : 0);
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
        ImGui::TextColored(kGrey, "Our own low-poly stone posts, boulders and cottage roofs. Turn this off if the game ever crashes or glitches when a match starts.");
    }
    ImGui::Spacing();
    DrawDebugSwitches();

    ImGui::BeginDisabled(!InGame());

    Heading("Host a lobby");
    ImGui::Text("Port");
    ImGui::InputInt("##royale_port", &ui.port);
    if (ImGui::Button("Host a lobby", ImVec2(220, 0))) {
        ui.port = std::clamp(ui.port, 1024, 65535);
        ui.error.clear();
        SaveUi(ui);
        gSession.ClearLastEnded();
        Trace("host: pressed");
        if (!gSession.Host(static_cast<uint16_t>(ui.port), CleanName(ui.name), &ui.error)) {
            ui.error = "Could not host: " + ui.error;
        } else {
            Trace("host: server up");
            RefreshLocalAddresses(ui, true);
            Trace("host: addresses listed");
        }
    }
    ImGui::Spacing();

    {
        // Which map the solo test (and the next lobby you host) is played on
        const char* current = royale::MapOf(ui.mapId).name;
        ImGui::SetNextItemWidth(300);
        if (ImGui::BeginCombo("Map##solo_map", current)) {
            for (int i = 0; i < royale::kMapCount; i++) {
                if (ImGui::Selectable(royale::MapOf(i).name, i == ui.mapId)) {
                    ui.mapId = i;
                    gSession.SelectMap(i);
                    SaveUi(ui);
                }
            }
            ImGui::EndCombo();
        }
    }
    const std::string soloLabel = std::string("Solo test: ") + royale::MapOf(ui.mapId).name;
    if (ImGui::Button(soloLabel.c_str(), ImVec2(300, 0))) {
        ui.port = std::clamp(ui.port, 1024, 65535);
        ui.error.clear();
        SaveUi(ui);
        gSession.ClearLastEnded();
        if (!gSession.Host(static_cast<uint16_t>(ui.port), CleanName(ui.name), &ui.error, true)) {
            ui.error = "Could not host: " + ui.error;
        } else {
            gSoloStartWanted = true;
            RefreshLocalAddresses(ui, true);
        }
    }
    ImGui::TextColored(kGrey, "Just you on the map chosen above, no bots, and everything else as in a real match (storm, loot, chests, bosses, supply drops, helpers). The match keeps going until you are out. A test environment.");
    ImGui::Spacing();

    Heading("Join a lobby");
    ImGui::Text("Host address (the numbers your friend gives you, like 192.168.1.23)");
    ImGui::InputText("##royale_address", ui.address, sizeof(ui.address));
    ImGui::BeginDisabled(ui.address[0] == '\0');
    if (ImGui::Button("Join lobby", ImVec2(220, 0))) {
        ui.port = std::clamp(ui.port, 1024, 65535);
        ui.error.clear();
        SaveUi(ui);
        gSession.ClearLastEnded();
        if (!gSession.Join(ui.address, static_cast<uint16_t>(ui.port), CleanName(ui.name), &ui.error)) {
            ui.error = "Could not join: " + ui.error;
        }
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled(); // !InGame

    ImGui::Spacing();
    ImGui::TextColored(kGrey, "Everyone must run the same build. Same Wi-Fi: use the host's local address. Over the internet: a VPN such as Tailscale, or forward UDP port %d.", royale::net::kDefaultPort);
}

void DrawRoster(const royale::HudState& h) {
    if (ImGui::BeginTable("royale_roster", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableHeadersRow();
        // Host first, then everyone else by join order.
        std::vector<const royale::RosterRow*> rows;
        for (const auto& r : h.roster) rows.push_back(&r);
        std::stable_sort(rows.begin(), rows.end(), [](auto* a, auto* b) { return a->host && !b->host; });
        for (const auto* r : rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s%s", r->name.c_str(), r->self ? "  (you)" : "");
            ImGui::TableSetColumnIndex(1);
            if (r->host) ImGui::TextColored(kGold, "HOST");
            else if (r->ready) ImGui::TextColored(kGreen, "READY");
            else ImGui::TextColored(kGrey, "waiting");
        }
        ImGui::EndTable();
    }
}

void DrawLobby(UiState& ui, const royale::HudState& h) {
    Heading("LOBBY");
    ImGui::Text("%d of %d players. The host's Start fills the other %d spots with bots.", h.humanCount, h.playerLimit, h.botSlots);
    ImGui::Spacing();

    if (h.isHost) {
        RefreshLocalAddresses(ui, false);
        ImGui::Text("Tell your friends to join this address:");
        if (ui.localAddresses.empty()) {
            ImGui::TextColored(kGrey, "(no network address found. Connect to Wi-Fi, or check your VPN)");
        }
        for (size_t i = 0; i < ui.localAddresses.size() && i < 3; i++) {
            std::string full = ui.localAddresses[i] + ":" + std::to_string(h.hostPort);
            ImGui::TextColored(kGreen, "%s", full.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(("Copy##royale_copy" + std::to_string(i)).c_str())) ImGui::SetClipboardText(ui.localAddresses[i].c_str());
        }
        ImGui::TextColored(kGrey, "Friends type the numbers before the colon as the address (port %u is the default).", h.hostPort);
        ImGui::Spacing();
    }

    // Where the match will be played: everyone sees it, the host chooses.
    ImGui::TextColored(kGold, "Map: %s", royale::MapOf(h.mapId).name);
    ImGui::TextColored(kGrey, "%s", royale::MapOf(h.mapId).blurb);
    if (h.isHost) {
        UiState& ui = Ui();
        ui.mapId = h.mapId;
        ImGui::SetNextItemWidth(260);
        if (ImGui::BeginCombo("Choose the map", royale::MapOf(ui.mapId).name)) {
            for (int i = 0; i < royale::kMapCount; i++) {
                if (ImGui::Selectable(royale::kMaps[i].name, i == ui.mapId)) {
                    ui.mapId = i;
                    gSession.SelectMap(i);
                    SaveUi(ui);
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::Checkbox("The map's major boss arrives halfway through the match", &ui.majorBoss)) {
            gSession.SetMajorBoss(ui.majorBoss);
            SaveUi(ui);
        }
        ImGui::TextColored(kGrey, "Its look and the mini bosses match the map: forest, water, shadow, fire or sand.");
    }
    ImGui::Spacing();

    DrawRoster(h);
    ImGui::Spacing();

    int others = 0, readyOthers = 0;
    for (const auto& r : h.roster) if (!r.host) { others++; readyOthers += r.ready; }

    if (h.isHost) {
        if (others > 0) ImGui::Text("%d of %d players ready", readyOthers, others);
        else ImGui::TextColored(kGrey, "Nobody else has joined. You can start now and play against bots.");
        {
            UiState& ui = Ui();
            int limit = h.playerLimit;
            ImGui::SetNextItemWidth(280);
            if (ImGui::SliderInt("Players (bots fill the rest)", &limit, std::max(royale::kMinPlayers, h.humanCount), royale::kMaxPlayers)) {
                ui.playerLimit = limit;
                gSession.SetPlayerLimit(limit);
                SaveUi(ui);
            }
            ImGui::TextColored(kGrey, "Smaller matches get fewer towns and mini bosses.");
            if (ImGui::Checkbox("Start automatically after 2 minutes", &ui.autoStart)) {
                gSession.SetAutoStart(ui.autoStart ? royale::kLobbyAutoStartSec : 0.0f);
                SaveUi(ui);
            }
        }
        {
            UiState& ui = Ui();
            static const char* kLevels[] = { "Easy", "Normal", "Hard" };
            ImGui::SetNextItemWidth(160);
            if (ImGui::Combo("Bot difficulty", &ui.botDifficulty, kLevels, 3)) {
                gSession.SetBotDifficulty(static_cast<royale::BotDifficulty>(ui.botDifficulty));
                SaveUi(ui);
            }
            ImGui::TextColored(kGrey, "Hard bots aim better, react faster, see further and use their abilities well.");
        }
        {
            UiState& ui = Ui();
            bool changed = false;
            static const char* kSeasons[] = { "Spring", "Summer", "Autumn", "Winter", "Random" };
            ImGui::SetNextItemWidth(160);
            changed |= ImGui::Combo("Season", &ui.weatherSeason, kSeasons, 5);
            ImGui::SetNextItemWidth(280);
            changed |= ImGui::SliderInt("Weather strength (0 = none)", &ui.weatherIntensity, 0, 100);
            ImGui::SetNextItemWidth(280);
            changed |= ImGui::SliderInt("How often it changes", &ui.weatherChange, 0, 100);
            if (changed) {
                gSession.SetWeatherOptions({ static_cast<uint8_t>(ui.weatherSeason), static_cast<uint8_t>(ui.weatherIntensity), static_cast<uint8_t>(ui.weatherChange) });
                SaveUi(ui);
            }
            ImGui::TextColored(kGrey, "Each place has its own weather: fog and thunderstorms, snow in winter, ash in the crater, sandstorms in the desert. Fog and sand hide you from bots, rain puts out fire, lightning strikes in thunderstorms.");
        }
        ImGui::BeginDisabled(!InGame() || gPendingStart);
        if (ImGui::Button(gPendingStart ? "Preparing..." : "Start match", ImVec2(220, 0))) gPendingStart = true;
        ImGui::EndDisabled();
        if (gPendingStart) ImGui::TextColored(kGrey, "Heading to %s and measuring the map before the countdown...", CurrentMap().name);
        else ImGui::TextColored(kGrey, "Starting takes you to %s first, so the real map can be measured.", CurrentMap().name);
        if (readyOthers < others) ImGui::TextColored(kGrey, "Not everyone is ready yet. Starting anyway is allowed.");
    } else {
        if (ImGui::Button(h.selfReady ? "Not ready" : "I'm ready", ImVec2(220, 0))) gSession.SetReady(!h.selfReady);
        ImGui::TextColored(kGrey, "Waiting for the host to start the match...");
    }

    {
        UiState& ui = Ui();
        ImGui::SetNextItemWidth(280);
        if (ImGui::SliderInt("Weather effects on my screen (%)", &ui.weatherDensity, 0, 200)) { gWeatherDensity = ui.weatherDensity / 100.0f; SaveUi(ui); }
        ImGui::SetNextItemWidth(280);
        ImGui::SetNextItemWidth(280);
        if (ImGui::SliderInt("Grass and trees (%)", &ui.foliage, 0, 200)) { gFoliage = ui.foliage / 100.0f; SaveUi(ui); }
        ImGui::SetNextItemWidth(280);
        if (ImGui::Checkbox("Cloth physics (caps, tunics and gliders)", &ui.clothOn)) { gClothScale = ui.clothOn ? ui.clothPhysics / 100.0f : 0.0f; SaveUi(ui); }
        if (ui.clothOn) {
            ImGui::SetNextItemWidth(280);
            if (ImGui::SliderInt("Cloth strength (%)", &ui.clothPhysics, 0, 200)) { gClothScale = ui.clothPhysics / 100.0f; SaveUi(ui); }
            ImGui::TextColored(kGrey, "Check: cap asked for %d times, last swing %.1f degrees; tunic asked for %d times, last swing %.1f degrees; cloth glider drawn %d frames",
                               gHatHookCalls, gHatLastSwing, gClothLimbCalls, gSkirtLastSwing, gGliderClothFrames);
        }
        if (ImGui::Checkbox("Wind and breeze (moves cloth, grass and trees)", &ui.windOn)) { gWindOn = ui.windOn; SaveUi(ui); }
        if (ui.windOn) {
            ImGui::SetNextItemWidth(280);
            if (ImGui::SliderInt("Wind strength (%)", &ui.windStrength, 0, 200)) { gWindScale = ui.windStrength / 100.0f; SaveUi(ui); }
            if (ImGui::Checkbox("Wind streaks (show which way it blows)", &ui.windStreaks)) { gWindStreaks = ui.windStreaks; SaveUi(ui); }
            float wnx, wnz, wns;
            WindNow(&wnx, &wnz, &wns);
            ImGui::TextColored(kGrey, "Wind right now: %d%% (it gusts, and rain and thunder bring squalls)", static_cast<int>(wns * 100.0f));
        }
        { static bool tornado = false; if (ImGui::Checkbox("Tornado (easter egg, only you can see it)", &tornado)) gTornadoOn = tornado; }
        static const char* kMusic[] = { "The game's own music", "Random songs from the music folder", "No music" };
        ImGui::SetNextItemWidth(280);
        if (ImGui::Combo("Match music", &ui.musicMode, kMusic, 3)) { gMusicMode = ui.musicMode; SaveUi(ui); }
    }
    if (h.lobbyLeft >= 0) ImGui::TextColored(kGold, "The match starts by itself in %s", ClockText(h.lobbyLeft).c_str());
    ImGui::Spacing();
    Heading("Where you are");
    int scene = InGame() ? gPlayState->sceneNum : -1;
    ImGui::Text("%s", scene < 0 ? "Not in a game" : SceneName(scene));
    ImGui::TextColored(kGrey, "Players in the same place can see each other. The match itself is on the chosen map (%s), and you are taken there automatically.", CurrentMap().name);
    ImGui::BeginDisabled(!InGame());
    if (!InWaitingRoom() && ImGui::Button("Go to the waiting room", ImVec2(220, 0))) WantsWaitingRoom = true;
    if (!InField() && ImGui::Button((std::string("Go to ") + CurrentMap().name).c_str(), ImVec2(220, 0))) { WantsWaitingRoom = false; GoToField(); }
    ImGui::EndDisabled();

    ImGui::Spacing();
    Heading("Ragdoll test");
    ImGui::BeginDisabled(!InGame() || !DebugOn(kDbgRagdoll));
    if (ImGui::Button("Spawn a test ragdoll", ImVec2(220, 0))) SpawnTestRagdoll(h);
    if (ImGui::Button("Fling the test ragdolls", ImVec2(220, 0))) FlingTestRagdolls();
    if (ImGui::Button("Remove the test ragdolls", ImVec2(220, 0))) RemoveTestRagdolls();
    ImGui::EndDisabled();
    ImGui::TextColored(kGrey, "Walk into it to shove it, hit it with your sword, or hold L next to it to carry it around. (Needs the Ragdoll switch in Debug.)");

    ImGui::Spacing();
    if (ImGui::Button("Leave lobby", ImVec2(220, 0))) gSession.Leave();
}

void DrawCountdown(const royale::HudState& h) {
    Heading("MATCH STARTING");
    ImGui::TextColored(kGold, "Drop in %d", static_cast<int>(std::ceil(h.countdownLeft)));
    ImGui::TextWrapped("%s", InField() ? (std::string("You are in ") + CurrentMap().name + ". Get ready.").c_str() : (std::string("Heading to ") + CurrentMap().name + "...").c_str());
    DrawRoster(h);
    if (ImGui::Button("Leave", ImVec2(220, 0))) gSession.Leave();
}

void DrawInMatch(const royale::HudState& h) {
    Heading(h.state == royale::MatchState::Drop ? "DROP: you are protected for a moment" : "MATCH IN PROGRESS");
    DrawItemGuide();
    ImGui::Text("Players alive: %d / %d", h.alive, h.playerLimit);
    if (h.haveSelf) {
        char label[32];
        std::snprintf(label, sizeof(label), "%.1f / %.1f hearts", h.selfHealth, h.maxHealth);
        ImGui::ProgressBar(h.selfHealth / std::max(1.0f, h.maxHealth), ImVec2(-1, 0), label);
        if (!h.selfAlive) ImGui::TextColored(kRed, "You have been eliminated.");
        ImGui::Text("Safe zone radius: %.0f", h.safeZone.radius);
        if (h.stormDamagePerSecond > 0) ImGui::TextColored(kRed, "You are in the storm! %.1f hearts per second", h.stormDamagePerSecond);
        else ImGui::TextColored(kGreen, "You are inside the safe zone.");
        ImGui::Text("Potions: %d    Heart pieces: %d / %d", h.potions, h.inv.heartPieces, royale::kHeartPiecesPerContainer);
        if (h.inv.hasAbility) ImGui::TextColored(RarityIm(static_cast<royale::Rarity>(h.inv.ability.rarity)), "Ability: %s (%s)",
                                                 ItemLabel(static_cast<royale::ItemId>(h.inv.ability.item), static_cast<royale::Rarity>(h.inv.ability.rarity)).c_str(),
                                                 h.abilityReadyIn > 0.05f ? ClockText(h.abilityReadyIn).c_str() : "ready");
        else ImGui::TextColored(kGrey, "Ability: none");
        for (int slot = 0; slot < royale::kGearSlots; slot++) {
            if (!(h.inv.gearMask & (1 << slot))) continue;
            const royale::Rarity gr = static_cast<royale::Rarity>(h.inv.gear[slot].rarity);
            ImGui::TextColored(RarityIm(gr), "Gear: %s", ItemLabel(static_cast<royale::ItemId>(h.inv.gear[slot].item), gr).c_str());
        }
        ImGui::TextColored(RarityIm(h.weaponRarity), "Weapon: %s", ItemLabel(h.weapon, h.weaponRarity).c_str());
        if (h.hasShield) ImGui::TextColored(RarityIm(h.shieldRarity), "Shield: %s", ItemLabel(h.shield, h.shieldRarity).c_str());
        else ImGui::TextColored(kGrey, "Shield: none");
    }
    if (!gPickupLog.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(kGold, "Recent pickups");
        for (const auto& n : gPickupLog) ImGui::TextColored(RarityIm(n.rarity), "%s", n.text.c_str());
    }
    if (ImGui::CollapsingHeader("Controls")) {
        ImGui::TextColored(kGold, "Fighting");
        ImGui::BulletText("B: attack with what is in your hand (it fires, throws or swings by itself)");
        ImGui::BulletText("Z: lock on to a player (hold), and dive faster while skydiving");
        ImGui::BulletText("D-pad Right / Left: next / previous weapon");
        ImGui::TextColored(kGold, "Staying alive");
        ImGui::BulletText("D-pad Down: drink a health potion      C-Left: drink a shield potion");
        ImGui::BulletText("D-pad Up: use your ability (the song, spell or hookshot in the ability slot)");
        ImGui::TextColored(kGold, "Moving and interacting");
        ImGui::BulletText("Stick: move      C-Up: jump (jump at a ledge to climb it)      Z + move: sidestep");
        ImGui::BulletText("A: open a chest, take or swap an item, hire an ally, talk");
        ImGui::BulletText("C-Right: emote (or tap EMOTE)      Walk over a better item to pick it up");
        ImGui::TextColored(kGold, "Spectating");
        ImGui::BulletText("D-pad Left / Right: watch the previous / next player");
    }
    ImGui::Spacing();
    if (ImGui::Button("Leave match", ImVec2(220, 0))) gSession.Leave();
}

// Write what the game knows about the current map (the measured field, storm circles, every chest and prop, the players) to a JSON
// file that tools/map-viewer.html can open. Returns the file's path, or an error message starting with "Could not".
std::string ExportMapJson() {
    if (!gSession.Joined() || gSession.Client() == nullptr) return "Could not export: join or host a lobby first";
    const royale::GameClient& c = *gSession.Client();
    royale::HudState h = gSession.Hud();
    std::string json = "{\n";
    char buf[256];
    std::snprintf(buf, sizeof(buf), "  \"map\": {\"x\": %.1f, \"z\": %.1f, \"radius\": %.1f},\n", h.map.center.x, h.map.center.z, h.map.radius);
    json += buf;
    json += "  \"storm\": [";
    if (const royale::Storm* st = c.GetStorm()) {
        for (int i = 0; i < royale::kStormPhaseCount; i++) {
            const royale::Circle& e = st->PhaseEnd(i);
            std::snprintf(buf, sizeof(buf), "%s{\"x\": %.1f, \"z\": %.1f, \"radius\": %.1f}", i ? ", " : "", e.center.x, e.center.z, e.radius);
            json += buf;
        }
    }
    json += "],\n  \"pois\": [";
    bool firstPoi = true;
    for (const auto& poi : c.Pois()) {
        std::snprintf(buf, sizeof(buf), "%s{\"name\": \"%s\", \"x\": %.1f, \"z\": %.1f, \"radius\": %.1f}", firstPoi ? "" : ", ", royale::kPoiNames[poi.name], poi.center.x, poi.center.z, poi.radius);
        json += buf;
        firstPoi = false;
    }
    json += "],\n  \"loot\": [";
    bool first = true;
    for (const auto& l : c.Loot()) {
        std::snprintf(buf, sizeof(buf), "%s{\"x\": %.1f, \"z\": %.1f, \"item\": \"%s\", \"rarity\": %d, \"chest\": %s, \"taken\": %s}", first ? "" : ",\n    ",
                      l.x, l.z, ItemName(static_cast<royale::ItemId>(l.item)), static_cast<int>(l.rarity), l.chest ? "true" : "false", l.taken ? "true" : "false");
        json += (first ? "\n    " : "") + std::string(buf);
        first = false;
    }
    json += "\n  ],\n  \"props\": [";
    first = true;
    static const char* kKinds[] = { "rock", "boulder", "bush", "pillar", "roof" };
    for (const auto& p : c.Props()) {
        std::snprintf(buf, sizeof(buf), "%s{\"x\": %.1f, \"z\": %.1f, \"kind\": \"%s\"}", first ? "" : ", ", p.pos.x, p.pos.z, kKinds[static_cast<int>(p.kind)]);
        json += buf;
        first = false;
    }
    json += "],\n  \"players\": [";
    first = true;
    for (const auto& pl : gSession.Puppets()) {
        std::snprintf(buf, sizeof(buf), "%s{\"x\": %.1f, \"z\": %.1f, \"bot\": %s}", first ? "" : ", ", pl.x, pl.z, pl.isBot ? "true" : "false");
        json += buf;
        first = false;
    }
    json += "],\n  \"bosses\": [";
    first = true;
    for (const auto& bn : c.Bosses()) {
        std::snprintf(buf, sizeof(buf), "%s{\"x\": %.1f, \"z\": %.1f, \"name\": \"%s\"}", first ? "" : ", ", bn.x, bn.z, royale::kBossDefs[bn.kind].name);
        json += buf;
        first = false;
    }
    json += "]";
    if (InField()) {
        Player* pl = GET_PLAYER(gPlayState);
        std::snprintf(buf, sizeof(buf), ",\n  \"you\": {\"x\": %.1f, \"z\": %.1f}", pl->actor.world.pos.x, pl->actor.world.pos.z);
        json += buf;
    }
    json += "\n}\n";
    const std::string path = Ship::Context::GetPathRelativeToAppDirectory("royale-map.json");
    std::ofstream out(path, std::ios::binary);
    if (!out) return "Could not write " + path;
    out << json;
    return path;
}

void DrawResults(const royale::HudState& h) {
    Heading("MATCH OVER");
    if (h.winnerId == h.selfId && h.winnerId != royale::net::kNoPlayer16) ImGui::TextColored(kGold, "VICTORY ROYALE! You won!");
    else if (!h.winnerName.empty()) ImGui::TextColored(kGold, "Winner: %s", h.winnerName.c_str());
    else ImGui::Text("Nobody survived.");
    ImGui::Spacing();
    if (ImGui::BeginTable("royale_results", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Kills");
        ImGui::TableSetupColumn("Damage");
        ImGui::TableSetupColumn("Points");
        ImGui::TableHeadersRow();
        int rank = 1;
        for (const auto& r : h.results) {
            ImGui::TableNextRow();
            const ImVec4 col = r.self ? kGreen : ImVec4(1, 1, 1, 1);
            ImGui::TableNextColumn(); ImGui::TextColored(col, "%d", rank++);
            ImGui::TableNextColumn(); ImGui::TextColored(col, "%s%s", r.name.c_str(), r.placement == 1 ? "  (winner)" : "");
            ImGui::TableNextColumn(); ImGui::TextColored(col, "%d", r.kills);
            ImGui::TableNextColumn(); ImGui::TextColored(col, "%.1f", r.damage);
            ImGui::TableNextColumn(); ImGui::TextColored(col, "%d", r.score);
        }
        ImGui::EndTable();
    }
    ImGui::TextColored(kGrey, "Points: %d per heart of damage, %d per kill, %d per chest, plus a bonus for lasting longer and %d for winning.",
                       royale::kPointsPerHeartOfDamage, royale::kPointsPerKill, royale::kPointsPerChest, royale::kPointsForWinning);
    ImGui::Spacing();
    if (h.isHost) {
        if (ImGui::Button("Play again", ImVec2(220, 0))) gSession.RequestPlayAgain();
        ImGui::TextColored(kGrey, "Starts a new match right away with everyone who is connected.");
    } else {
        ImGui::TextColored(kGrey, "Waiting for the host to play again...");
    }
    if (ImGui::Button("Back to the menu", ImVec2(220, 0))) gSession.Leave();
}

// ---- in-game updater (Android) --------------------------------------------------------------------------------------------------
// "Game updates" on the Battle Royale page: asks GitHub for the newest published build (RoyaleUpdater.java, patches/0018), downloads it
// and opens Android's installer. The game build workflow publishes every main-branch build as a release tagged "build-<number>", and our
// version is "0.<number>", so the numbers compare directly.
#ifdef __ANDROID__
enum UpdaterState { kUpdIdle = 0, kUpdChecking, kUpdChecked, kUpdDownloading, kUpdNeedPermission, kUpdInstalling, kUpdError };

// The build number in ROYALE_BUILD_VERSION ("0.123" -> 123); 0 for a developer build ("dev").
int OwnBuildNumber() {
    const char* dot = std::strchr(ROYALE_BUILD_VERSION, '.');
    return dot != nullptr ? std::atoi(dot + 1) : 0;
}

// RoyaleUpdater, found through the activity's class loader (FindClass on the game thread only sees Android's own classes).
jclass UpdaterClass(JNIEnv* env) {
    static jclass cls = nullptr;
    if (cls != nullptr) return cls;
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (activity == nullptr) return nullptr;
    jclass activityCls = env->GetObjectClass(activity);
    jobject loader = env->CallObjectMethod(activity, env->GetMethodID(activityCls, "getClassLoader", "()Ljava/lang/ClassLoader;"));
    jclass loaderCls = env->FindClass("java/lang/ClassLoader");
    jstring name = env->NewStringUTF("com.dishii.soh.RoyaleUpdater");
    jobject found = env->CallObjectMethod(loader, env->GetMethodID(loaderCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;"), name);
    if (env->ExceptionCheck()) { env->ExceptionClear(); found = nullptr; }
    if (found != nullptr) cls = static_cast<jclass>(env->NewGlobalRef(found));
    env->DeleteLocalRef(name);
    env->DeleteLocalRef(loaderCls);
    env->DeleteLocalRef(loader);
    env->DeleteLocalRef(activityCls);
    env->DeleteLocalRef(activity);
    if (found != nullptr) env->DeleteLocalRef(found);
    return cls;
}

jint UpdaterInt(const char* method) {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jclass cls = env != nullptr ? UpdaterClass(env) : nullptr;
    if (cls == nullptr) return -1;
    const jint v = env->CallStaticIntMethod(cls, env->GetStaticMethodID(cls, method, "()I"));
    if (env->ExceptionCheck()) { env->ExceptionClear(); return -1; }
    return v;
}

jlong UpdaterLong(const char* method) {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jclass cls = env != nullptr ? UpdaterClass(env) : nullptr;
    if (cls == nullptr) return 0;
    const jlong v = env->CallStaticLongMethod(cls, env->GetStaticMethodID(cls, method, "()J"));
    if (env->ExceptionCheck()) { env->ExceptionClear(); return 0; }
    return v;
}

std::string UpdaterString(const char* method) {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jclass cls = env != nullptr ? UpdaterClass(env) : nullptr;
    if (cls == nullptr) return "";
    jstring s = static_cast<jstring>(env->CallStaticObjectMethod(cls, env->GetStaticMethodID(cls, method, "()Ljava/lang/String;")));
    if (env->ExceptionCheck()) { env->ExceptionClear(); return ""; }
    if (s == nullptr) return "";
    const char* chars = env->GetStringUTFChars(s, nullptr);
    std::string out = chars != nullptr ? chars : "";
    if (chars != nullptr) env->ReleaseStringUTFChars(s, chars);
    env->DeleteLocalRef(s);
    return out;
}

// check / download / install, each taking the activity as its Context
void UpdaterCall(const char* method) {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jclass cls = env != nullptr ? UpdaterClass(env) : nullptr;
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (cls == nullptr || activity == nullptr) return;
    env->CallStaticVoidMethod(cls, env->GetStaticMethodID(cls, method, "(Landroid/content/Context;)V"), activity);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(activity);
}

void DrawUpdater() {
    if (!ImGui::CollapsingHeader("Game updates")) return;
    const int state = UpdaterInt("getState");
    if (state < 0) {
        ImGui::TextColored(kRed, "The updater is missing from this build.");
        return;
    }
    const int own = OwnBuildNumber();
    const int latest = UpdaterInt("getLatestBuild");
    const std::string message = UpdaterString("getMessage");
    ImGui::TextColored(kGrey, "You have version %s. New builds are published on GitHub each time a change is merged.", ROYALE_BUILD_VERSION);

    switch (state) {
        case kUpdIdle:
        case kUpdError:
            if (state == kUpdError) ImGui::TextColored(kRed, "%s", message.c_str());
            if (ImGui::Button("Check for updates", ImVec2(260, 0))) UpdaterCall("check");
            break;
        case kUpdChecking:
            ImGui::Text("%s", message.c_str());
            break;
        case kUpdChecked:
            if (latest > own) {
                ImGui::TextColored(kGold, "Version 0.%d is ready (%lld MB).", latest, static_cast<long long>(UpdaterLong("getSizeKb") / 1024));
                if (ImGui::Button("Download and install", ImVec2(260, 0))) UpdaterCall("download");
            } else {
                ImGui::Text("You have the newest build (0.%d is the newest on GitHub).", latest);
                if (ImGui::Button("Check again", ImVec2(200, 0))) UpdaterCall("check");
                ImGui::SameLine();
                if (ImGui::Button("Reinstall it anyway", ImVec2(260, 0))) UpdaterCall("download");
            }
            break;
        case kUpdDownloading: {
            const int pct = UpdaterInt("getProgress");
            const long long kb = UpdaterLong("getDownloadedKb");
            ImGui::Text("%s", message.c_str());
            char label[64];
            std::snprintf(label, sizeof(label), "%lld MB", kb / 1024);
            ImGui::ProgressBar(pct >= 0 ? pct / 100.0f : 0.0f, ImVec2(360, 0), label);
            break;
        }
        case kUpdNeedPermission:
            ImGui::TextWrapped("%s", message.c_str());
            if (ImGui::Button("Install", ImVec2(260, 0))) UpdaterCall("install");
            break;
        case kUpdInstalling:
            ImGui::TextWrapped("%s", message.c_str());
            if (ImGui::Button("Open the installer again", ImVec2(260, 0))) UpdaterCall("install");
            break;
    }
    ImGui::Spacing();
}
#else
void DrawUpdater() {}
#endif

void DrawDebug(UiState& ui) {
    if (!ImGui::CollapsingHeader("Developer tools")) return;
    ImGui::Checkbox("Show Link position (for measuring the map)", &ui.showPosition);
    if (ImGui::Button("Export map data for tools/map-viewer.html")) ui.exportMsg = ExportMapJson();
    if (!ui.exportMsg.empty() && ui.exportMsg.rfind("Could not", 0) != 0) ImGui::TextWrapped("Wrote %s. Open tools/map-viewer.html in a browser and load that file.", ui.exportMsg.c_str());
    else if (!ui.exportMsg.empty()) ImGui::TextColored(kRed, "%s", ui.exportMsg.c_str());
    if (ui.showPosition && InGame()) {
        Player* p = GET_PLAYER(gPlayState);
        ImGui::Text("x=%.0f  y=%.0f  z=%.0f  scene=0x%02X", p->actor.world.pos.x, p->actor.world.pos.y, p->actor.world.pos.z,
                    gPlayState->sceneNum);
    }
}

// Everything the "Battle Royale" menu page shows. Called every frame the page is visible.
void DrawRoyaleUi() {
    UiState& ui = Ui();
    royale::HudState h = gSession.Hud();

    {   // the logo at the top of the page
        ImVec2 sz;
        if (ImTextureID tex = LogoTexture(&sz)) {
            const float width = std::min(ImGui::GetContentRegionAvail().x, 360.0f), height = width * sz.y / sz.x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - width) * 0.5f);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x - 8, at.y - 4), ImVec2(at.x + width + 8, at.y + height + 4), IM_COL32(246, 241, 229, 235), 12.0f);
            ImGui::Image(tex, ImVec2(width, height));
            ImGui::Spacing();
        }
        ImGui::TextColored(kGrey, "Version %s", ROYALE_BUILD_VERSION);
    }
    DrawCrashReport();
    DrawExitReason();

    if (h.mode == royale::HudState::Mode::Idle) {
        DrawMainMenu(ui, h);
    } else if (!h.connected) {
        Heading("CONNECTING");
        ImGui::Text("Connecting to the host...");
        if (ImGui::Button("Cancel", ImVec2(220, 0))) gSession.Leave();
    } else {
        switch (h.state) {
            case royale::MatchState::Lobby: DrawLobby(ui, h); break;
            case royale::MatchState::Countdown: DrawCountdown(h); break;
            case royale::MatchState::Drop:
            case royale::MatchState::InMatch: DrawInMatch(h); break;
            case royale::MatchState::Ending: DrawResults(h); break;
        }
    }
    ImGui::Spacing();
    DrawUpdater();
    DrawDebug(ui);
}

// Adds a top-level "Battle Royale" entry to the port menu (opened with F1 on Windows, Back/Select on Android) through the
// fork's own menu registration hook, so no patch to the fork's menu code is needed.
void RegisterRoyaleMenu() {
    using namespace SohGui;
    mSohMenu->AddMenuEntry("Battle Royale", CVAR_SETTING("Menu.BattleRoyaleSidebarSection"));
    WidgetPath path = { "Battle Royale", "Play", SECTION_COLUMN_1 };
    mSohMenu->AddSidebarEntry("Battle Royale", path.sidebarName, 1);
    mSohMenu->AddWidget(path, "OOT Royale##royale_ui", WIDGET_CUSTOM).CustomFunction([](WidgetInfo& info) { DrawRoyaleUi(); });
}

RegisterMenuInitFunc royaleMenuInit(RegisterRoyaleMenu);

} // namespace

// ---- the Fortnite map, the parts the game calls (patches/0013) ------------------------------------------------------------------

namespace {
// The island's collision in the game's own formats. Built once, then it stays: the game keeps pointers into it for as long as the scene is loaded.
std::vector<Vec3s> gFortniteVtx;
std::vector<CollisionPoly> gFortnitePoly;
SurfaceType gFortniteSurface[1];
CamData gFortniteCam[1];
WaterBox gFortniteWater[1];
CollisionHeader gFortniteHeader;
bool gFortniteBuilt = false;

CollisionHeader* FortniteHeader() {
    namespace fn = royale::fortnite;
    if (gFortniteBuilt) return &gFortniteHeader;
    const fn::Mesh mesh = fn::BuildCollision();
    gFortniteVtx.resize(mesh.verts.size());
    for (size_t i = 0; i < mesh.verts.size(); i++) gFortniteVtx[i] = { mesh.verts[i].x, mesh.verts[i].y, mesh.verts[i].z };
    gFortnitePoly.resize(mesh.polys.size());
    for (size_t i = 0; i < mesh.polys.size(); i++) {
        const fn::Poly& p = mesh.polys[i];
        CollisionPoly& c = gFortnitePoly[i];
        c = {};
        c.type = 0;
        c.flags_vIA = p.a;
        c.flags_vIB = p.b;
        c.vIC = p.c;
        c.normal = { p.nx, p.ny, p.nz };
        c.dist = p.dist;
    }
    gFortniteSurface[0] = {};                       // plain ground: no exit, no damage, the first camera entry
    gFortniteCam[0] = {};
    gFortniteCam[0].cameraSType = CAM_SET_NORMAL0;
    gFortniteCam[0].numCameras = 0;
    gFortniteCam[0].camPosData = nullptr;
    gFortniteWater[0] = {};                         // one box over the whole island: every room (0x3F), the scene's first light setting
    gFortniteWater[0].xMin = static_cast<s16>(std::lround(-fn::kHalfX - 2.0f));
    gFortniteWater[0].zMin = static_cast<s16>(std::lround(-fn::kHalfZ - 2.0f));
    gFortniteWater[0].xLength = static_cast<s16>(std::lround(2.0f * fn::kHalfX + 4.0f));
    gFortniteWater[0].zLength = static_cast<s16>(std::lround(2.0f * fn::kHalfZ + 4.0f));
    gFortniteWater[0].ySurface = static_cast<s16>(fn::kWaterY);
    gFortniteWater[0].properties = 0x3Fu << 13;
    gFortniteHeader = {};
    gFortniteHeader.minBounds = { mesh.lo.x, mesh.lo.y, mesh.lo.z };
    gFortniteHeader.maxBounds = { mesh.hi.x, mesh.hi.y, mesh.hi.z };
    gFortniteHeader.numVertices = static_cast<u16>(gFortniteVtx.size());
    gFortniteHeader.vtxList = gFortniteVtx.data();
    gFortniteHeader.numPolygons = static_cast<u16>(gFortnitePoly.size());
    gFortniteHeader.polyList = gFortnitePoly.data();
    gFortniteHeader.surfaceTypeList = gFortniteSurface;
    gFortniteHeader.cameraDataList = gFortniteCam;
    gFortniteHeader.cameraDataListLen = 1;
    gFortniteHeader.numWaterBoxes = 1;
    gFortniteHeader.waterBoxes = gFortniteWater;
    gFortniteBuilt = true;
    return &gFortniteHeader;
}
} // namespace

// Called as every scene loads its collision: the island's, if this is Hyrule Field and the lobby's map is the Fortnite map; otherwise null (the scene's own).
extern "C" CollisionHeader* Royale_CustomCollision(PlayState* play) {
    gFortniteScene = false;
    if (play == nullptr || play->sceneNum != SCENE_HYRULE_FIELD || !gSession.Joined() || gMapId != royale::fortnite::kMapId) return nullptr;
    gFortniteScene = true;
    return FortniteHeader();
}
extern "C" s32 Royale_IsCustomCollision(CollisionHeader* header) { return gFortniteBuilt && header == &gFortniteHeader; }
extern "C" s32 Royale_HideRooms(void) { return gFortniteScene && gPlayState != nullptr && gPlayState->sceneNum == SCENE_HYRULE_FIELD; }
