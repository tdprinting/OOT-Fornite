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
#include "logo_data.h"
#include "map.h"
#include "meshes.h"
#include "names.h"
#include "objmodel.h"
#include "skins.h"
#include "tune.h"
#include <algorithm>
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

#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/nametag.h"
#include "soh/Notification/Notification.h"
#include "soh/SohGui/SohGui.hpp"
#include "soh/SohGui/SohMenu.h"
#include "soh/cvar_prefixes.h"
#include <SDL2/SDL.h>
#include <imgui.h>
#include <libultraship/libultraship.h>
#include <spdlog/spdlog.h>

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "src/overlays/actors/ovl_En_Box/z_en_box.h" // the treasure chest actor
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
#include "objects/object_fd2/object_fd2.h"         // Volvagia (the dragons)
#include "regs.h"                                // WREG, for the game's own minimap switch
extern PlayState* gPlayState;

void Player_UseItem(PlayState* play, Player* player, s32 item);
void Player_Draw(Actor* actor, PlayState* play);
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
float gClothScale = 1.0f;           // the local option: cloth and wind physics on the hat and glider, 0 (off) to 2
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
    SkelAnime sk;
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
    const float t = static_cast<float>(ImGui::GetTime());
    float speed = 28.0f;
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
    speed *= 0.75f + 0.25f * std::sin(t * 0.9f) + 0.12f * std::sin(t * 2.3f + 1.0f);
    const float dir = 0.4f + t * 0.04f + static_cast<float>(static_cast<int>(gWeatherShown.season)) * 1.1f;
    *wx = std::cos(dir) * speed;
    *wz = std::sin(dir) * speed;
    *strength = std::clamp(speed / 330.0f, 0.0f, 1.0f);
}

const royale::MapDef& CurrentMap() { return royale::MapOf(gMapId); }
bool InField() { return InGame() && gPlayState->sceneNum == CurrentMap().scene; }
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
bool GoToWaitingRoom() { return TravelTo(ENTR_TEMPLE_OF_TIME_ENTRANCE); }
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
bool RawFloorAt(float x, float z, float* outY = nullptr) {   // the scene's own floor, without our climbing blocks
    if (!InField()) return false;
    CollisionPoly poly;
    Vec3f pos = { x, 4000.0f, z };
    float y = BgCheck_AnyRaycastFloor1(&gPlayState->colCtx, &poly, &pos);
    if (y <= BGCHECK_Y_MIN + 1.0f) return false;
    if (outY) *outY = y;
    return true;
}

// Floor that would load another scene (a doorway, a cave mouth, the edge of the field). Nothing is ever placed on or near it, so nobody
// spawns or finds a chest where the exit seal (SealExits) would shove them back. The ROM extractor's numbers (docs/MAPS.md) show how many
// of these each map has: Kakariko alone has nine loading zones and fourteen exit surfaces.
bool OnExitFloor(float x, float z) {
    if (!InField()) return false;
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
    float surface = 0;
    WaterBox* box = nullptr;
    return WaterBox_GetSurface1(gPlayState, &gPlayState->colCtx, x, z, &surface, &box) != 0 && surface > floorY + 15.0f;
}

bool WalkableAt(royale::Vec2 p) {
    float y;
    return FloorAt(p.x, p.z, &y) && std::fabs(y - gMedianFloorY) <= 1200.0f && !UnderWater(p.x, p.z, y) && !OnExitFloor(p.x, p.z) && !NearLoadingZone(p.x, p.z, 380.0f);
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

void SpawnProjectileFrom(royale::ItemId weapon, float x, float y, float z, s16 yaw); // below, with the other custom models

#define RA(n) ((LinkAnimationHeader*)&gPlayerAnim_link_##n)
// Actions are played once, from their first frame, each time one begins (see Puppet_Update); `combo` varies a sword's slash.
bool OneShotAnim(uint8_t anim) {
    switch (static_cast<royale::Anim>(anim)) {
        case royale::Anim::Attack: case royale::Anim::Shoot: case royale::Anim::Throw: case royale::Anim::Drink:
        case royale::Anim::Play: case royale::Anim::Cast: case royale::Anim::Roll: return true;
        default: return false;
    }
}
LinkAnimationHeader* AnimFor(uint8_t anim, royale::ItemId weapon = royale::ItemId::BasicSword, int combo = 0) {
    using royale::ItemId;
    const bool hammer = weapon == ItemId::MegatonHammer || weapon == ItemId::GiantsHammer;
    switch (static_cast<royale::Anim>(anim)) {
        case royale::Anim::Attack: {
            if (hammer) return RA(hammer_hit);
            static LinkAnimationHeader* const slashes[4] = { RA(fighter_Lnormal_kiru), RA(fighter_LLside_kiru), RA(fighter_LRside_kiru), RA(fighter_Lpierce_kiru) };
            if (weapon == ItemId::DekuStick) return RA(fighter_normal_kiru);
            return slashes[combo & 3];
        }
        case royale::Anim::Shoot:
            if (weapon == ItemId::Hookshot || weapon == ItemId::Longshot) return RA(hook_shot_ready);
            return RA(bow_bow_shoot);
        case royale::Anim::Throw:
            if (weapon == ItemId::Boomerang) return RA(boom_throwR);
            if (weapon == ItemId::Bombs || weapon == ItemId::Bombchus || weapon == ItemId::HomingBombchus) return RA(normal_throw);
            return RA(boom_throwL);
        case royale::Anim::Drink: return RA(bottle_drink_demo_start);
        case royale::Anim::Play: return RA(normal_okarina_start);
        case royale::Anim::Cast:
            if (weapon == ItemId::DinsFire) return RA(magic_honoo1);
            if (weapon == ItemId::FaroresWind) return RA(magic_kaze1);
            return RA(magic_tamashii1);
        case royale::Anim::Roll: return (LinkAnimationHeader*)&gPlayerAnim_link_normal_landing_roll;           // a dodge roll
        case royale::Anim::SideL: return (LinkAnimationHeader*)&gPlayerAnim_link_anchor_side_walkL;            // the lock-on footwork
        case royale::Anim::SideR: return (LinkAnimationHeader*)&gPlayerAnim_link_anchor_side_walkR;
        case royale::Anim::Back: return (LinkAnimationHeader*)&gPlayerAnim_link_anchor_back_walk;
        case royale::Anim::Stance: return (LinkAnimationHeader*)&gPlayerAnim_link_anchor_waitR;
        case royale::Anim::Emote1: return (LinkAnimationHeader*)&gPlayerAnim_link_demo_bikkuri;     // startled: "Wow!"
        case royale::Anim::Emote2: return (LinkAnimationHeader*)&gPlayerAnim_link_demo_jibunmiru;   // looks at his own hands
        case royale::Anim::Emote3: return (LinkAnimationHeader*)&gPlayerAnim_link_demo_kaoage;      // looks up
        case royale::Anim::Emote4: return (LinkAnimationHeader*)&gPlayerAnim_link_demo_kenmiru1;    // admires a sword
        case royale::Anim::Emote5: return (LinkAnimationHeader*)&gPlayerAnim_link_normal_wait;      // the chicken dance poses the limbs itself (ApplyChickenDance)
        case royale::Anim::Walk:
        case royale::Anim::Run:
            return (LinkAnimationHeader*)&gPlayerAnim_link_normal_run;
        default: // Idle, Attack, Hurt, Dead and anything newer than this build: stand still for now
            return (LinkAnimationHeader*)&gPlayerAnim_link_normal_wait;
    }
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

void Puppet_Update(Actor* actor, PlayState* play) {
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
    // Bots are simulated on a flat plane (y = 0), so stand them on the real floor. Humans report their own height.
    actor->world.pos.y = s.isBot ? GroundY(play, s.x, s.z, s.y) : s.y;
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

    // Hold what the server says this player holds.
    Look look = LookFor(s.weapon);
    if (player->modelGroup != look.modelGroup || player->heldItemAction != look.itemAction) {
        u8 original = gSaveContext.equips.buttonItems[0];
        gSaveContext.equips.buttonItems[0] = look.buttonItem;
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

    {   // somebody has just loosed an arrow or thrown something: show it in flight
        static std::unordered_map<uint16_t, uint8_t> previous;
        uint8_t& before = previous[s.id];
        if (s.anim != before && s.alive && (s.anim == static_cast<uint8_t>(royale::Anim::Shoot) || s.anim == static_cast<uint8_t>(royale::Anim::Throw)))
            SpawnProjectileFrom(s.weapon, s.x, actor->world.pos.y + 45.0f, s.z, s.rot);
        before = s.anim;
    }
    // Actions play once, from their first frame, every time they begin, and hold their last pose until the player does something else:
    // that is what makes a slash, a shot or a throw read as a movement instead of a looping wiggle. Sword slashes cycle through the
    // game's four different swings.
    static std::unordered_map<const Actor*, uint8_t> lastAnim;
    static std::unordered_map<uint16_t, int> combo;
    bool restart = false;
    LinkAnimationHeader* want = nullptr;
    const bool hanging = HangingFromGlider(&s, actor, play);
    if (hanging) {   // both hands up on the glider's bar (the game's ledge-hang pose)
        want = RA(normal_jump_climb_wait);
        auto cur = gPlaying.find(actor);
        if (cur == gPlaying.end() || cur->second != (const void*)want) { LinkAnimation_PlayLoop(play, &player->skelAnime, want); gPlaying[actor] = (const void*)want; }
        LinkAnimation_Update(play, &player->skelAnime);
        Vec3f ignored;
        SkelAnime_UpdateTranslation(&player->skelAnime, &ignored, actor->shape.rot.y);
        lastAnim[actor] = 255;
        return;
    }
    {
        auto sw = gSwingFrames.find(s.id);
        const bool hitting = sw != gSwingFrames.end() && sw->second > 0;
        const bool swingStart = hitting && sw->second == 10;
        if (hitting) sw->second--;
        uint8_t& before = lastAnim[actor];
        const bool attackStart = s.anim == static_cast<uint8_t>(royale::Anim::Attack) && before != s.anim;
        if (swingStart || attackStart) {
            want = AnimFor(static_cast<uint8_t>(royale::Anim::Attack), s.weapon, combo[s.id]++);
            restart = true;
        } else if (hitting) {
            auto cur = gPlaying.find(actor);
            want = cur != gPlaying.end() ? (LinkAnimationHeader*)cur->second : AnimFor(s.anim, s.weapon);
        } else {
            want = AnimFor(s.anim, s.weapon, combo[s.id]);
            restart = OneShotAnim(s.anim) && before != s.anim;
        }
        before = s.anim;
    }
    auto playing = gPlaying.find(actor);
    if (restart || playing == gPlaying.end() || playing->second != (const void*)want) {
        if (OneShotAnim(s.anim) || restart) LinkAnimation_PlayOnce(play, &player->skelAnime, want);
        else LinkAnimation_PlayLoop(play, &player->skelAnime, want);
        gPlaying[actor] = (const void*)want;
    }
    LinkAnimation_Update(play, &player->skelAnime);
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

void DrawGliderAt(PlayState* play, float x, float y, float z, s16 yaw, float roll, bool diving, uint32_t scheme); // with the other custom models, below

void Puppet_Draw(Actor* actor, PlayState* play) {
    // Player_Draw reads the local player's equipped item to pick the held model, so show the puppet's own.
    const royale::PuppetState* st = StateOf(actor);
    u8 original = gSaveContext.equips.buttonItems[0];
    gSaveContext.equips.buttonItems[0] = st ? LookFor(st->weapon).buttonItem : ITEM_NONE;
    if (st && gTunicApplied) SetTunicCosmetics(st->tunic); // this player's own colour
    Player_Draw(actor, play);
    if (st && gTunicApplied) SetTunicCosmetics(gLocalTunic);
    gSaveContext.equips.buttonItems[0] = original;
    // Everyone who is still in the sky during the drop hangs from a glider.
    if (st && HangingFromGlider(st, actor, play)) {
        DrawGliderAt(play, actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, actor->shape.rot.y, 0.0f, false, st->id);
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

// A body that tumbles and slides to a stop with simple physics (gravity, bounces off the ground, friction, a spin that dies away), then lies
// in Link's own knocked-down pose. It is a puppet actor that runs its own physics instead of following the network.
constexpr uint16_t kCorpseIdBase = 0xF000;
constexpr uint16_t kAllyIdBase = 0xE000;   // puppet ids from here up to the corpses are hireable allies (index = id - base)
struct Corpse {
    Actor* actor = nullptr;
    royale::Vec2 vel = {};       // horizontal speed, units per second
    float vy = 0;                // vertical speed
    float spin = 0;              // radians per second around the vertical axis
    float roll = 0, rollVel = 0; // a floppy wobble (binary angle units)
    float age = 0;
    royale::ItemId weapon = royale::ItemId::DekuStick;
    uint32_t tunic = royale::SkinRgb(0);
    bool animStarted = false;
    float pitch = 0, pitchVel = 0;        // tumbling head over heels in the air (radians)
    royale::Vec2 lastVel = {};
    float lastVy = 0;
    float limb[9][2] = {}, limbVel[9][2] = {};   // loose limbs: two swing angles each (radians), see ApplyRagdollLimbs
    int bounces = 0;
    bool pinned = false;         // an emote double: stands where the local player is and plays an emote, instead of falling
    int emote = 0;
};
std::unordered_map<uint16_t, Corpse> gCorpses;       // corpse id -> body
std::unordered_map<const Actor*, uint16_t> gCorpseOf;
uint16_t gNextCorpse = kCorpseIdBase;
std::unordered_map<uint16_t, royale::PuppetState> gLastSeen; // the last state of each living puppet, to see who just died

void ForgetCorpse(const Actor* actor) {
    auto corpse = gCorpseOf.find(actor);
    if (corpse != gCorpseOf.end()) { gCorpses.erase(corpse->second); gCorpseOf.erase(corpse); }
}

void Corpse_Update(Actor* actor, PlayState* play) {
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
    c.age += dt;
    if (c.age > 25.0f) { Actor_Kill(actor); return; }

    // Rigid-body-ish motion: gravity and a little air drag; the body tumbles head over heels in the air, bounces (losing most of its energy each time),
    // slides with friction that is stronger the slower it goes, and the spin dies away until it lies still.
    const bool wasAir = c.vy != 0.0f || c.bounces == 0;
    c.vy -= 980.0f * dt;
    c.vel.x *= 1.0f - 0.35f * dt; c.vel.z *= 1.0f - 0.35f * dt;
    actor->world.pos.x += c.vel.x * dt;
    actor->world.pos.z += c.vel.z * dt;
    actor->world.pos.y += c.vy * dt;
    const float ground = GroundY(play, actor->world.pos.x, actor->world.pos.z, actor->world.pos.y - 1.0f);
    bool onGround = false;
    if (actor->world.pos.y <= ground) {
        actor->world.pos.y = ground;
        onGround = true;
        if (c.vy < -120.0f && c.bounces < 4) {   // a bounce: it keeps a third of its height, the spin changes, and the limbs fling
            c.vy = -c.vy * 0.34f;
            c.bounces++;
            c.vel.x *= 0.72f; c.vel.z *= 0.72f;
            c.pitchVel *= -0.45f;
            c.rollVel += (c.vel.x > 0 ? 1.0f : -1.0f) * 2600.0f;
            for (auto& l : c.limbVel) { l[0] += (Rand_ZeroOne() - 0.5f) * 9.0f; l[1] += (Rand_ZeroOne() - 0.5f) * 9.0f; }
        } else {
            c.vy = 0;
            const float speed = std::hypot(c.vel.x, c.vel.z);
            const float drag = (speed > 60.0f ? 3.2f : 7.5f) * dt;   // sliding to a stop
            c.vel.x *= std::max(0.0f, 1.0f - drag); c.vel.z *= std::max(0.0f, 1.0f - drag);
            c.spin *= std::max(0.0f, 1.0f - 4.0f * dt);
        }
    }
    (void)wasAir;
    // Head over heels while airborne; once down, the tumble eases out and the knocked-down pose takes over.
    if (!onGround) { c.pitch += c.pitchVel * dt; }
    else { c.pitch *= std::max(0.0f, 1.0f - 6.0f * dt); c.pitchVel *= std::max(0.0f, 1.0f - 6.0f * dt); }
    actor->shape.rot.x = static_cast<s16>(c.pitch * (32768.0f / 3.14159265f));
    actor->shape.rot.y = static_cast<s16>(actor->shape.rot.y + static_cast<int>(c.spin * dt * (32768.0f / 3.14159265f)));
    actor->world.rot.y = actor->shape.rot.y;
    // A loose roll that wobbles and settles.
    c.rollVel += -c.roll * 30.0f * dt;
    c.rollVel *= 0.93f;
    c.roll += c.rollVel * dt;
    actor->shape.rot.z = static_cast<s16>(std::clamp(c.roll, -2500.0f, 2500.0f));
    actor->shape.shadowAlpha = 255;

    // The limbs lag behind the body: every change of speed (the blow, each bounce, the stop) swings them, springs pull them back to limp.
    {
        const float ax = (c.vel.x - c.lastVel.x) / dt, az = (c.vel.z - c.lastVel.z) / dt, ay = (c.vy - c.lastVy) / dt;
        c.lastVel = c.vel; c.lastVy = c.vy;
        const float kick = std::clamp((std::fabs(ax) + std::fabs(az) + std::fabs(ay) * 0.4f) * 0.0009f, 0.0f, 1.4f);
        for (int i = 0; i < 9; i++) {
            for (int a = 0; a < 2; a++) {
                const float push = (a == 0 ? ay * 0.00032f : (ax * 0.0003f + az * 0.0003f)) * (i % 2 ? -1.0f : 1.0f) + kick * (Rand_ZeroOne() - 0.5f) * 0.5f;
                c.limbVel[i][a] += push;
                if (!onGround) c.limbVel[i][a] += std::sin(c.age * (7.0f + i) + a) * 0.9f * dt * 20.0f;   // flailing through the air
                c.limbVel[i][a] += -c.limb[i][a] * 55.0f * dt;
                c.limbVel[i][a] *= std::max(0.0f, 1.0f - 4.5f * dt);
                c.limb[i][a] = std::clamp(c.limb[i][a] + c.limbVel[i][a] * dt, -1.1f, 1.1f);
            }
        }
    }

    if (!c.animStarted) {
        LinkAnimation_PlayOnce(play, &player->skelAnime, (LinkAnimationHeader*)&gPlayerAnim_link_normal_back_downA); // knocked flat on the back
        c.animStarted = true;
    }
    LinkAnimation_Update(play, &player->skelAnime);
    {   // the loose limbs, on top of the knocked-down pose
        static const int kLimbs[9] = { PLAYER_LIMB_HEAD, PLAYER_LIMB_L_SHOULDER, PLAYER_LIMB_R_SHOULDER, PLAYER_LIMB_L_FOREARM, PLAYER_LIMB_R_FOREARM,
                                       PLAYER_LIMB_L_THIGH, PLAYER_LIMB_R_THIGH, PLAYER_LIMB_L_SHIN, PLAYER_LIMB_R_SHIN };
        static const float kReach[9] = { 0.5f, 1.0f, 1.0f, 0.9f, 0.9f, 0.6f, 0.6f, 0.7f, 0.7f };
        Vec3s* j = player->skelAnime.jointTable;
        const float bin = 32768.0f / 3.14159265f;
        for (int i = 0; i < 9; i++) {
            j[kLimbs[i]].x = static_cast<s16>(j[kLimbs[i]].x + c.limb[i][0] * kReach[i] * bin);
            j[kLimbs[i]].z = static_cast<s16>(j[kLimbs[i]].z + c.limb[i][1] * kReach[i] * bin);
        }
    }
    Vec3f ignored;
    SkelAnime_UpdateTranslation(&player->skelAnime, &ignored, actor->shape.rot.y);
}

void Corpse_Draw(Actor* actor, PlayState* play) {
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
    if (gCorpses.size() >= 14 || gPlayState == nullptr) return nullptr;
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

void SpawnCorpse(const royale::PuppetState& s, float pushX, float pushZ) {
    if (gCorpses.size() >= 12 || gPlayState == nullptr) return;
    const uint16_t id = gNextCorpse++;
    if (gNextCorpse < kCorpseIdBase) gNextCorpse = kCorpseIdBase;
    gSpawningPuppet = id;
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_PLAYER, s.x, s.y, s.z, 0, s.rot, 0, 0, false);
    gSpawningPuppet = 0;
    if (actor == nullptr) return;
    Corpse c;
    c.actor = actor;
    const float len = std::max(1.0f, std::hypot(pushX, pushZ));
    c.vel = { pushX / len * 250.0f, pushZ / len * 250.0f };   // thrown back by the blow
    c.vy = 330.0f;
    c.pitchVel = -7.0f - (id % 3);                            // flips over backwards
    c.spin = (id & 1 ? 1.0f : -1.0f) * 3.2f;
    c.rollVel = (id & 1 ? 1.0f : -1.0f) * 4000.0f;
    c.weapon = s.weapon;
    c.tunic = s.tunic;
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
void DrawLilo(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale);
bool LiloNear();
void TalkToLilo();
void DrawMaya(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale);
bool MayaNear();
void TalkToMaya();
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
};
std::unordered_map<size_t, PropActor> gProps;        // prop index -> its actor
std::unordered_map<const Actor*, size_t> gPropOf;
std::unordered_set<size_t> gBrokenProps;              // rocks and bushes players smashed; they stay gone
std::unordered_set<size_t> gCulledProps;              // props we removed ourselves (far away), as opposed to smashed ones
constexpr float kPropSpawnRadius = 2200.0f;
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
    gProps[index] = pa;
    gPropOf[actor] = index;
    actor->destroy = Prop_Destroy;
    if (meshKind >= 0) actor->draw = Prop_DrawCustom; // the game's rock stays as the solid part, unseen; our model is what you see
    switch (p.kind) { // a blob shadow under each, sized to the model
        case royale::PropKind::Rock: actor->shape.shadowScale = 26.0f; break;
        case royale::PropKind::Boulder: actor->shape.shadowScale = 75.0f; break;
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

// ---- mini bosses ---------------------------------------------------------------------------------------------------------------

// Each boss is drawn by a stand-in actor (the game's small rock with its logic switched off) using our golem model. The server decides
// everything about it; here we only smooth what the snapshots say and animate it.
struct BossActor {
    Actor* actor = nullptr;
    ActorFunc origDestroy = nullptr;
    int kind = 0;
    float x = 0, z = 0;          // smoothed position
    float tx = 0, tz = 0;        // latest from the server
    int16_t rot = 0, trot = 0;
    float hp = 1.0f;
    float smashAge = 10.0f;      // seconds since it last swung
    float moved = 0;             // distance covered lately, for the walking bob
    float alt = 0, talt = 0;     // height above the ground, smoothed / latest (the dragon flies)
    int mode = 0;                // royale::DragonMode
    bool initialised = false;
    // the game's own enemy model for the mini bosses (see BossModelOf)
    SkelAnime sk;
    Vec3s joint[64] = {};
    Vec3s morph[64] = {};
    bool skReady = false;
    const void* playing = nullptr;
    float hurtAge = 10.0f;       // seconds since it was last hurt
    float lastHp = 1.0f;
    int swings = 0;              // which of its two attacks comes next
    float walkBlend = 0.0f;
};
std::unordered_map<uint32_t, BossActor> gBosses;      // boss id -> its actor
std::unordered_map<const Actor*, uint32_t> gBossOf;
std::unordered_map<uint32_t, int> gBossKindSeen;      // remembered after it is gone, for the messages

// The seven mini bosses are the game's own enemies, each with its real skeleton and animations: a Dodongo, two Iron Knuckles, a Wolfos and a white
// Wolfos, and a Stalfos. They are drawn just as the game draws them (same skeleton, same colour tricks); the server decides where they go and what they
// hit, and tells us when a blow starts so the right swing plays (the blow lands about half a second in).
struct BossModel {
    const char* skeleton;
    int limbs;
    bool flex;
    const char* idle; const char* walk; const char* attack[2]; const char* hurt;
    float scale;            // the game's own actor scale
    float idleSpeed;        // 0 holds the first frame (the Iron Knuckle's stance)
    int look;               // 0 plain, 1 Iron Knuckle gold, 2 Iron Knuckle green, 3 Wolfos, 4 white Wolfos, 5 Stalfos
};
BossModel BossModelOf(int kind) {
    switch (kind) {
        case 0: case 6: return { gDodongoSkel, 31, false, gDodongoWaitAnim, gDodongoWalkAnim, { gDodongoSweepTailRightAnim, gDodongoSweepTailLeftAnim }, gDodongoDamageAnim, 0.01875f, 1.0f, 0 };
        case 1: return { gIronKnuckleSkel, 30, true, gIronKnuckleWalkAnim, gIronKnuckleWalkAnim, { gIronKnuckleVerticalAttackAnim, gIronKnuckleHorizontalAttackAnim }, gIronKnuckleFrontHitAnim, 0.012f, 0.0f, 1 };
        case 5: return { gIronKnuckleSkel, 30, true, gIronKnuckleWalkAnim, gIronKnuckleWalkAnim, { gIronKnuckleHorizontalAttackAnim, gIronKnuckleVerticalAttackAnim }, gIronKnuckleFrontHitAnim, 0.012f, 0.0f, 2 };
        case 2: return { gWolfosWhiteSkel, 22, true, gWolfosWaitingAnim, gWolfosRunningAnim, { gWolfosSlashingAnim, gWolfosSlashingAnim }, gWolfosDamagedAnim, 0.01f, 1.0f, 4 };
        case 3: return { gWolfosNormalSkel, 22, true, gWolfosWaitingAnim, gWolfosRunningAnim, { gWolfosSlashingAnim, gWolfosSlashingAnim }, gWolfosDamagedAnim, 0.0075f, 1.0f, 3 };
        default: return { gStalfosSkel, 61, false, gStalfosMiddleGuardAnim, gStalfosSlowAdvanceAnim, { gStalfosDownSlashAnim, gStalfosUpSlashAnim }, gStalfosFlinchFromHitFrontAnim, 0.015f, 1.0f, 5 };
    }
}

Gfx* BossEnvDl(PlayState* play, u8 pr, u8 pg, u8 pb, u8 er, u8 eg, u8 eb) {
    Gfx* dl = static_cast<Gfx*>(Graph_Alloc(play->state.gfxCtx, 4 * sizeof(Gfx)));
    Gfx* h = dl;
    gDPPipeSync(h++);
    gDPSetPrimColor(h++, 0, 0, pr, pg, pb, 255);
    gDPSetEnvColor(h++, er, eg, eb, 255);
    gSPEndDisplayList(h++);
    return dl;
}

int gBossLook = 0;
s32 Boss_OverrideLimb(PlayState* play, s32 limb, Gfx** dList, Vec3f*, Vec3s*, void*) {
    if (gBossLook == 1 || gBossLook == 2) {   // Iron Knuckle: only the whole-armour pieces are drawn (the broken-armour limbs are not)
        if (limb == 28 || limb == 29) *dList = nullptr;
    } else if (gBossLook == 5 && limb == 11) {   // the Stalfos' eyes glow, pulsing
        OPEN_DISPS(play->state.gfxCtx);
        gDPPipeSync(POLY_OPA_DISP++);
        gDPSetEnvColor(POLY_OPA_DISP++, 80 + std::abs(static_cast<int>(std::sin(play->gameplayFrames * 0.1f) * 175.0f)), 0, 0, 255);
        CLOSE_DISPS(play->state.gfxCtx);
    }
    return 0;
}

void Boss_PostLimb(PlayState* play, s32 limb, Gfx**, Vec3s*, void*) {
    if (gBossLook != 1 && gBossLook != 2) return;
    OPEN_DISPS(play->state.gfxCtx);
    auto xlu = [&](const char* dl) {
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_XLU_DISP++, (Gfx*)dl);
    };
    switch (limb) {   // the armour's see-through decals, as the game's Iron Knuckle draws them
        case 12: xlu(object_ik_DL_016D88); break;
        case 22: xlu(object_ik_DL_016F88); break;
        case 24: xlu(object_ik_DL_016EE8); break;
        case 26: xlu(gIronKnuckleArmorRivetAndSymbolDL); break;
        case 27: xlu(object_ik_DL_016CD8); break;
        default: break;
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

void MiniBoss_Update(Actor* actor, PlayState* play, BossActor& b) {
    const BossModel m = BossModelOf(b.kind);
    if (!b.skReady) {
        if (m.flex) SkelAnime_InitFlex(play, &b.sk, (FlexSkeletonHeader*)m.skeleton, nullptr, b.joint, b.morph, m.limbs);
        else SkelAnime_Init(play, &b.sk, (SkeletonHeader*)m.skeleton, nullptr, b.joint, b.morph, m.limbs);
        b.skReady = true;
        b.lastHp = b.hp;
    }
    const float dt = 1.0f / royale::kTickHz;
    b.hurtAge += dt;
    if (b.hp < b.lastHp - 0.02f) b.hurtAge = 0.0f;
    b.lastHp = b.hp;
    static std::unordered_map<const Actor*, float> lastSmash;
    float& before = lastSmash[actor];
    const bool newSwing = b.smashAge < 0.05f && before >= 0.05f;
    before = b.smashAge;
    const bool swinging = b.smashAge < 1.1f;
    const char* want;
    float speed = 1.0f;
    bool loop = true;
    if (newSwing) b.swings++;
    if (swinging) { want = m.attack[(b.swings & 1)]; loop = false; }
    else if (b.hurtAge < 0.45f) { want = m.hurt; loop = false; }
    else if (b.moved > 0.5f) want = m.walk;
    else { want = m.idle; speed = m.idleSpeed; }
    if (b.playing != (const void*)want || newSwing) {
        Animation_Change(&b.sk, (AnimationHeader*)want, speed, 0.0f, Animation_GetLastFrame((void*)want), loop ? ANIMMODE_LOOP : ANIMMODE_ONCE, -4.0f);
        b.playing = want;
    }
    SkelAnime_Update(&b.sk);
}

void MiniBoss_Draw(Actor* actor, PlayState* play, const BossActor& b) {
    if (!b.skReady) return;
    const BossModel m = BossModelOf(b.kind);
    const float scale = m.scale * 1.55f * royale::kBossDefs[b.kind].scale;   // mini bosses are bigger than the game's own
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    switch (m.look) {
        case 1:
            gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)BossEnvDl(play, 245, 225, 155, 30, 30, 0));
            gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)BossEnvDl(play, 255, 40, 0, 40, 0, 0));
            gSPSegment(POLY_OPA_DISP++, 0x0A, (uintptr_t)BossEnvDl(play, 255, 255, 255, 20, 40, 30));
            break;
        case 2:
            gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)BossEnvDl(play, 55, 65, 55, 0, 0, 0));
            gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)BossEnvDl(play, 205, 165, 75, 25, 20, 0));
            gSPSegment(POLY_OPA_DISP++, 0x0A, (uintptr_t)BossEnvDl(play, 205, 165, 75, 25, 20, 0));
            break;
        case 3: case 4: {
            static const char* normal[4] = { gWolfosNormalEyeOpenTex, gWolfosNormalEyeHalfTex, gWolfosNormalEyeNarrowTex, gWolfosNormalEyeHalfTex };
            static const char* white[4] = { gWolfosWhiteEyeOpenTex, gWolfosWhiteEyeHalfTex, gWolfosWhiteEyeNarrowTex, gWolfosWhiteEyeHalfTex };
            const int eye = (play->gameplayFrames / 6) % 40 == 0 ? 2 : 0;
            gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)(m.look == 3 ? normal : white)[eye]);
            break;
        }
        default: break;
    }
    // Damage flash: white for a moment after a hit
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(actor->shape.rot.y * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    gBossLook = m.look;
    SkelAnime_DrawSkeletonOpa(play, &const_cast<BossActor&>(b).sk, Boss_OverrideLimb, Boss_PostLimb, actor);
    gBossLook = 0;
    CLOSE_DISPS(play->state.gfxCtx);
}

void Dragon_UpdateModel(Actor*, PlayState* play, BossActor& b);

void Boss_Update(Actor* actor, PlayState* play) {
    auto of = gBossOf.find(actor);
    if (of == gBossOf.end()) { Actor_Kill(actor); return; }
    BossActor& b = gBosses[of->second];
    const float dt = 1.0f / royale::kTickHz;
    if (!b.initialised) { b.x = b.tx; b.z = b.tz; b.rot = b.trot; b.initialised = true; }
    const float nx = b.x + (b.tx - b.x) * 0.4f, nz = b.z + (b.tz - b.z) * 0.4f;
    b.moved = b.moved * 0.8f + std::hypot(nx - b.x, nz - b.z);
    b.x = nx; b.z = nz;
    const s16 diff = static_cast<s16>(b.trot - b.rot);
    b.rot = static_cast<s16>(b.rot + diff * 0.35f);
    b.smashAge += dt;
    actor->world.pos.x = b.x;
    actor->world.pos.z = b.z;
    b.alt += (b.talt - b.alt) * 0.25f;
    if (royale::IsDragonKind(static_cast<royale::BossKind>(b.kind))) {
        const float ground = GroundY(play, b.x, b.z, GET_PLAYER(play)->actor.world.pos.y);
        actor->world.pos.y = ground + b.alt;
    } else {
        actor->world.pos.y = GroundY(play, b.x, b.z, actor->world.pos.y);
    }
    actor->shape.rot.y = b.rot;
    actor->world.rot.y = b.rot;
    if (!royale::IsDragonKind(static_cast<royale::BossKind>(b.kind))) MiniBoss_Update(actor, play, b);
    else Dragon_UpdateModel(actor, play, b);
}

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

void DrawGliderAt(PlayState* play, float x, float y, float z, s16 yaw, float roll, bool diving, uint32_t scheme) {
    const bool cloth = gClothScale > 0.01f;
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
    Matrix_Translate(x, y, z, MTXMODE_NEW);
    Matrix_RotateY(yaw * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    Matrix_RotateZ(roll + std::sin(t * 3.1f + scheme) * 0.045f, MTXMODE_APPLY);               // gentle sway in the wind
    Matrix_RotateX((diving ? 0.62f : 0.12f) + std::sin(t * 2.3f + scheme * 1.7f) * 0.03f, MTXMODE_APPLY); // nose down for a dive
    Matrix_Scale(1.0f, diving ? 0.65f : 1.0f, diving ? 0.75f : 1.0f, MTXMODE_APPLY);
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
    DrawGliderAt(play, actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, actor->shape.rot.y, gGliderRoll, gGliderDiving, 0);
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

constexpr float kDragonDrawScale = 0.55f; // the mesh is 1200 across with its wings out

void Dragon_DrawBlocks(Actor* actor, PlayState* play, const BossActor& b) {
    const royale::BossKind kind = static_cast<royale::BossKind>(b.kind);
    const uint32_t theme = static_cast<uint32_t>(kind) - static_cast<uint32_t>(royale::BossKind::DragonFire);
    const float t = static_cast<float>(play->gameplayFrames) / royale::kTickHz;
    // Wings: a steady beat while it flies (up, level, down, level), folded down when it has landed.
    static const uint32_t kBeat[4] = { 0, 1, 2, 1 };
    const bool landed = b.mode == static_cast<int>(royale::DragonMode::Landed);
    const float rate = b.mode == static_cast<int>(royale::DragonMode::Swoop) ? 9.0f : 4.5f;
    const uint32_t pose = landed ? 2u : kBeat[static_cast<int>(t * rate) & 3];
    const GpuMesh* mesh = GpuMeshFor(royale::MeshKind::Dragon, pose + 4u * theme);
    if (mesh == nullptr || mesh->dl.empty()) return;
    const float bob = landed ? 0.0f : std::sin(t * 2.2f) * 14.0f;
    float pitch = 0.0f;                                                  // nose down in a dive, up as it climbs
    if (b.mode == static_cast<int>(royale::DragonMode::Swoop)) pitch = 0.5f;
    else if (b.mode == static_cast<int>(royale::DragonMode::Climb)) pitch = -0.35f;
    else if (b.mode == static_cast<int>(royale::DragonMode::Breath)) pitch = 0.18f;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y + bob, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(actor->shape.rot.y * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    Matrix_RotateX(pitch, MTXMODE_APPLY);
    Matrix_Scale(kDragonDrawScale, kDragonDrawScale, kDragonDrawScale, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(mesh->dl.data()));
    CLOSE_DISPS(play->state.gfxCtx);
}


// The major bosses are Volvagia, the game's own dragon boss (the Fire Temple's), with its real skeleton, eyes, scrolling-lava skin and animations: its
// idle sway, its fire-breathing, its claw swipe, and the vulnerable pose when it comes down. Each map's dragon is tinted for its place.
const char* DragonAnim(int mode) {
    switch (static_cast<royale::DragonMode>(mode)) {
        case royale::DragonMode::Breath: return gHoleVolvagiaBreatheFireAnim;
        case royale::DragonMode::Cast: return gHoleVolvagiaClawSwipeAnim;
        case royale::DragonMode::Swoop: return gHoleVolvagiaHitAnim;
        case royale::DragonMode::Landed: return gHoleVolvagiaVulnerableAnim;
        case royale::DragonMode::Climb: return gHoleVolvagiaTurnAnim;
        default: return gHoleVolvagiaIdleAnim;
    }
}

void Dragon_UpdateModel(Actor*, PlayState* play, BossActor& b) {
    if (!b.skReady) {
        SkelAnime_InitFlex(play, &b.sk, (FlexSkeletonHeader*)gHoleVolvagiaSkel, nullptr, b.joint, b.morph, 37);
        b.skReady = true;
        b.lastHp = b.hp;
    }
    b.hurtAge += 1.0f / royale::kTickHz;
    if (b.hp < b.lastHp - 0.01f) b.hurtAge = 0.0f;
    b.lastHp = b.hp;
    const char* want = b.hurtAge < 0.5f && b.mode != static_cast<int>(royale::DragonMode::Breath) ? gHoleVolvagiaDamagedAnim : DragonAnim(b.mode);
    if (b.playing != (const void*)want) {
        const bool loopIt = want == gHoleVolvagiaIdleAnim || want == gHoleVolvagiaVulnerableAnim;
        Animation_Change(&b.sk, (AnimationHeader*)want, 1.0f, 0.0f, Animation_GetLastFrame((void*)want), loopIt ? ANIMMODE_LOOP : ANIMMODE_ONCE, -6.0f);
        b.playing = want;
    }
    SkelAnime_Update(&b.sk);
}

float gDragonJaw = 0.0f;
s32 Dragon_OverrideLimb(PlayState* play, s32 limb, Gfx**, Vec3f*, Vec3s* rot, void*) {
    switch (limb) {
        case 35: case 36: rot->z = static_cast<s16>(rot->z - gDragonJaw * 0.1f); break;
        case 32: rot->z = static_cast<s16>(rot->z + gDragonJaw); break;
        default: break;
    }
    if (limb == 32 || limb == 35 || limb == 36) {
        OPEN_DISPS(play->state.gfxCtx);
        gDPPipeSync(POLY_OPA_DISP++);
        gDPSetEnvColor(POLY_OPA_DISP++, 255, 255, 255, 0);
        CLOSE_DISPS(play->state.gfxCtx);
    }
    return 0;
}

// ---- custom model files (the dragon) -----------------------------------------------------------------------------------------------------------
// Put dragon.obj (and its dragon.mtl) in the "models" folder inside the game's data folder and it replaces the dragon. Parts are animated by their names
// in the file: "wing" (flaps; left or right comes from which side of the body it is on), "jaw" (opens when it breathes fire), "tail" (sways) and "head"
// (nods); everything else is the body. A model that is one piece still bobs, banks and tilts. An optional dragon.cfg changes how it is fitted:
//   scale=1.0   size multiplier      yaw=0   degrees to turn it so its nose points along the flight direction (try 180 or 90)
//   lift=0      raise it             flap=35 how far the wings beat, in degrees
// The game draws these with vertex colours and its own lighting, so colours come from the .mtl (Kd) or from per-vertex colours; textures are not used.
struct CustomPart {
    std::unique_ptr<GpuMesh> gpu;
    royale::ObjRole role = royale::ObjRole::Body;
    float centre[3] = {0, 0, 0}, mn[3] = {0, 0, 0}, mx[3] = {0, 0, 0};
    float side = 1.0f;   // wings: +1 on the +x side, -1 on the other
};
struct CustomModel {
    bool tried = false, ok = false;
    std::string status = "No custom dragon: put dragon.obj in the models folder";
    std::vector<CustomPart> parts;
    float flapDegrees = 35.0f;
    size_t triangles = 0;
};
CustomModel gDragonModel;

std::filesystem::path ModelsFolder() { return std::filesystem::path(Ship::Context::GetPathRelativeToAppDirectory("models")); }

bool ReadWholeFile(const std::filesystem::path& file, std::string* out) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    *out = ss.str();
    return true;
}

void LoadCustomDragon() {
    CustomModel& cm = gDragonModel;
    cm = CustomModel{};
    cm.tried = true;
    std::error_code ec;
    std::filesystem::create_directories(ModelsFolder(), ec);
    std::string obj, mtl, cfg;
    if (!ReadWholeFile(ModelsFolder() / "dragon.obj", &obj)) { cm.status = "No custom dragon: put dragon.obj in " + ModelsFolder().string(); return; }
    ReadWholeFile(ModelsFolder() / "dragon.mtl", &mtl);
    ReadWholeFile(ModelsFolder() / "dragon.cfg", &cfg);
    float extra = 1.0f, yaw = 0.0f, lift = 0.0f;
    {
        std::istringstream in(cfg);
        std::string line;
        while (std::getline(in, line)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = line.substr(0, eq);
            const float val = static_cast<float>(std::atof(line.c_str() + eq + 1));
            if (key == "scale") extra = std::clamp(val, 0.1f, 10.0f);
            else if (key == "yaw") yaw = val;
            else if (key == "lift") lift = val;
            else if (key == "flap") cm.flapDegrees = std::clamp(val, 0.0f, 80.0f);
        }
    }
    royale::ObjModel model = royale::ParseObj(obj, mtl);
    if (!model.ok) { cm.status = "dragon.obj could not be used: " + model.error; return; }
    royale::FitObjModel(model, 1200.0f, extra, yaw, lift);
    for (auto& part : model.parts) {
        CustomPart cp;
        cp.gpu = std::make_unique<GpuMesh>();
        if (!BuildGpuMesh(part.mesh, *cp.gpu)) continue;
        cp.role = royale::RoleOf(part.name);
        for (int i = 0; i < 3; i++) { cp.centre[i] = part.centre[i]; cp.mn[i] = part.mn[i]; cp.mx[i] = part.mx[i]; }
        cp.side = part.centre[0] >= 0 ? 1.0f : -1.0f;
        cm.parts.push_back(std::move(cp));
    }
    cm.triangles = model.triangles;
    cm.ok = !cm.parts.empty();
    int wings = 0, jaws = 0, tails = 0, heads = 0;
    for (const auto& p : cm.parts) { wings += p.role == royale::ObjRole::Wing; jaws += p.role == royale::ObjRole::Jaw; tails += p.role == royale::ObjRole::Tail; heads += p.role == royale::ObjRole::Head; }
    cm.status = "Custom dragon loaded: " + std::to_string(cm.triangles) + " triangles, " + std::to_string(cm.parts.size()) + " parts (" + std::to_string(wings) + " wing, " +
                std::to_string(jaws) + " jaw, " + std::to_string(tails) + " tail, " + std::to_string(heads) + " head)";
}

void DrawCustomDragon(Actor* actor, PlayState* play, const BossActor& b) {
    const CustomModel& cm = gDragonModel;
    const float t = static_cast<float>(ImGui::GetTime());
    const bool landed = b.mode == static_cast<int>(royale::DragonMode::Landed);
    const bool breathing = b.mode == static_cast<int>(royale::DragonMode::Breath);
    const bool swoop = b.mode == static_cast<int>(royale::DragonMode::Swoop);
    const float rate = swoop ? 9.0f : 4.5f;
    const float flap = landed ? -0.5f : std::sin(t * rate) * cm.flapDegrees * 0.0174533f;
    float pitch = 0.0f;
    if (swoop) pitch = 0.5f; else if (b.mode == static_cast<int>(royale::DragonMode::Climb)) pitch = -0.3f; else if (breathing) pitch = 0.18f;
    const float bob = landed ? 0.0f : std::sin(t * 2.2f) * 14.0f;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    CLOSE_DISPS(play->state.gfxCtx);
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y + bob, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(actor->shape.rot.y * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    Matrix_RotateX(pitch, MTXMODE_APPLY);
    Matrix_RotateZ(landed ? 0.0f : std::sin(t * 1.3f) * 0.06f, MTXMODE_APPLY);   // a slow bank
    Matrix_Scale(kDragonDrawScale, kDragonDrawScale, kDragonDrawScale, MTXMODE_APPLY);
    for (const CustomPart& p : cm.parts) {
        Matrix_Push();
        switch (p.role) {
            case royale::ObjRole::Wing:   // flaps about the root, the edge nearest the body
                Matrix_Translate(p.side > 0 ? p.mn[0] : p.mx[0], p.centre[1], p.centre[2], MTXMODE_APPLY);
                Matrix_RotateZ(p.side * flap, MTXMODE_APPLY);
                Matrix_Translate(-(p.side > 0 ? p.mn[0] : p.mx[0]), -p.centre[1], -p.centre[2], MTXMODE_APPLY);
                break;
            case royale::ObjRole::Jaw:    // opens about its back edge
                Matrix_Translate(p.centre[0], p.centre[1], p.mn[2], MTXMODE_APPLY);
                Matrix_RotateX(breathing ? 0.55f + std::sin(t * 9.0f) * 0.08f : 0.05f + std::sin(t * 1.5f) * 0.04f, MTXMODE_APPLY);
                Matrix_Translate(-p.centre[0], -p.centre[1], -p.mn[2], MTXMODE_APPLY);
                break;
            case royale::ObjRole::Tail:   // sways from where it joins the body
                Matrix_Translate(p.centre[0], p.centre[1], p.mx[2], MTXMODE_APPLY);
                Matrix_RotateY(std::sin(t * 2.0f) * 0.3f, MTXMODE_APPLY);
                Matrix_Translate(-p.centre[0], -p.centre[1], -p.mx[2], MTXMODE_APPLY);
                break;
            case royale::ObjRole::Head:   // nods
                Matrix_Translate(p.centre[0], p.centre[1], p.mn[2], MTXMODE_APPLY);
                Matrix_RotateX((breathing ? 0.2f : 0.0f) + std::sin(t * 1.1f) * 0.06f, MTXMODE_APPLY);
                Matrix_Translate(-p.centre[0], -p.centre[1], -p.mn[2], MTXMODE_APPLY);
                break;
            default: break;
        }
        OPEN_DISPS(play->state.gfxCtx);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(p.gpu->dl.data()));
        CLOSE_DISPS(play->state.gfxCtx);
        Matrix_Pop();
    }
}

void Dragon_Draw(Actor* actor, PlayState* play, const BossActor& b) {
    if (!gDragonModel.tried) LoadCustomDragon();
    if (gDragonModel.ok) { DrawCustomDragon(actor, play, b); return; }
    if (!b.skReady) { Dragon_DrawBlocks(actor, play, b); return; }
    const uint32_t theme = static_cast<uint32_t>(b.kind) - static_cast<uint32_t>(royale::BossKind::DragonFire);
    static const u8 tint[5][3] = { {255, 255, 255}, {130, 190, 255}, {170, 255, 150}, {195, 150, 255}, {255, 228, 165} };
    static const char* eyes[3] = { gHoleVolvagiaEyeOpenTex, gHoleVolvagiaEyeHalfTex, gHoleVolvagiaEyeClosedTex };
    const float t = static_cast<float>(play->gameplayFrames);
    const bool landed = b.mode == static_cast<int>(royale::DragonMode::Landed);
    const float bob = landed ? 0.0f : std::sin(t * 0.11f) * 14.0f;
    float pitch = 0.0f;
    if (b.mode == static_cast<int>(royale::DragonMode::Swoop)) pitch = 0.5f;
    else if (b.mode == static_cast<int>(royale::DragonMode::Climb)) pitch = -0.3f;
    gDragonJaw = b.mode == static_cast<int>(royale::DragonMode::Breath) ? 2600.0f + std::sin(t * 0.7f) * 500.0f : (std::sin(t * 0.05f) + 1.0f) * 150.0f;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)eyes[(play->gameplayFrames / 7) % 60 == 0 ? 2 : 0]);
    gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)Gfx_TwoTexScroll(play->state.gfxCtx, 0, static_cast<u32>(play->gameplayFrames * 1) % 0x80, static_cast<u32>(play->gameplayFrames * 2) % 0x80, 0x20, 0x20, 1,
                                                                    static_cast<u32>(play->gameplayFrames * 3) % 0x80, static_cast<u32>(play->gameplayFrames * -2) % 0x80, 0x20, 0x20));
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, tint[theme % 5][0], tint[theme % 5][1], tint[theme % 5][2], 255);
    gDPSetEnvColor(POLY_OPA_DISP++, 255, 255, 255, 128);
    const float scale = 0.014f;
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y + bob, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(actor->shape.rot.y * (3.14159265f / 32768.0f) + 3.14159265f, MTXMODE_APPLY);   // it faces the way the dragon goes
    Matrix_RotateX(pitch, MTXMODE_APPLY);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    SkelAnime_DrawSkeletonOpa(play, &const_cast<BossActor&>(b).sk, Dragon_OverrideLimb, nullptr, actor);
    CLOSE_DISPS(play->state.gfxCtx);
}


// ---- foliage, snow on the ground and the weather the game itself draws ------------------------------------------------------
// Patches of swaying grass, trees (a different set of leaves for each season) and, when it snows, mounds of snow that build up on the ground and
// slowly melt away afterwards are scattered around the player. All of it is local scenery: where it stands is worked out from the map and a hash of
// each cell, so nothing is sent over the network, and it is only ever drawn near the player. Trees are solid (you walk around the trunk).
bool WaterAt(float x, float z, float floorY) { return UnderWater(x, z, floorY); }
float gFoliage = 1.0f;       // the local option, 0 (none) to 2
float gSnowCover = 0.0f;     // 0 bare ground to 1 deep snow
float WeatherAmount();

uint32_t FloraHash(int a, int b, int salt) {
    uint32_t h = static_cast<uint32_t>(a) * 374761393u + static_cast<uint32_t>(b) * 668265263u + static_cast<uint32_t>(salt) * 2246822519u + 0x9E3779B9u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
float Flora01(int a, int b, int salt) { return static_cast<float>(FloraHash(a, b, salt) & 0xFFFF) / 65535.0f; }

struct FloraSpot { bool ok; float y; };
std::unordered_map<uint64_t, FloraSpot> gFloraSpots;
int gFloraScene = -1;
int gFloraBudget = 0;

// Is there good ground at (x, z)? Cached per cell. nullptr = not measured yet (the per-frame budget of measurements ran out).
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
        if (kind != 2) {   // grass and trees stay off steep ground (and cliff edges)
            spot.ok = RawFloorAt(x + 45.0f, z, &y2) && RawFloorAt(x, z + 45.0f, &y3) && std::fabs(y2 - y) < 26.0f && std::fabs(y3 - y) < 26.0f;
        }
        if (spot.ok && kind == 1 && gSession.Client()) {   // a tree keeps clear of scenery, towns and loot sites
            for (const royale::Prop& p : gSession.Client()->Props())
                if (std::fabs(p.pos.x - x) < 140.0f && std::fabs(p.pos.z - z) < 140.0f) { spot.ok = false; break; }
            for (const royale::Poi& poi : gSession.Client()->Pois())
                if (std::hypot(poi.center.x - x, poi.center.z - z) < poi.radius * 0.7f + 140.0f) { spot.ok = false; break; }
        }
    }
    return &gFloraSpots.emplace(key, spot).first->second;
}

constexpr float kGrassCell = 95.0f, kTreeCell = 380.0f, kSnowCell = 125.0f;

struct TreeSpot { float x, y, z, scale, yaw; uint32_t variant; };
bool TreeIn(int cx, int cz, int season, TreeSpot* out) {
    if (Flora01(cx / 2, cz / 2, 21) < 0.45f - 0.2f * std::min(1.0f, gFoliage)) return false;   // groves: whole blocks of cells are empty
    if (Flora01(cx, cz, 22) > 0.62f) return false;
    const float x = (static_cast<float>(cx) + 0.12f + 0.76f * Flora01(cx, cz, 23)) * kTreeCell, z = (static_cast<float>(cz) + 0.12f + 0.76f * Flora01(cx, cz, 24)) * kTreeCell;
    const FloraSpot* spot = FloraSpotAt(1, cx, cz, x, z);
    if (spot == nullptr || !spot->ok) return false;
    *out = { x, spot->y, z, 0.8f + 0.5f * Flora01(cx, cz, 25), Flora01(cx, cz, 26) * 6.2831853f, (FloraHash(cx, cz, 27) % 4) + 4u * static_cast<uint32_t>(season) };
    return true;
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

int FloraSeason() { return gSession.Joined() ? (static_cast<int>(gWeatherShown.season) & 3) : 1; }

void DrawFlora(PlayState* play) {
    if (!InField() || gPlayState == nullptr) return;
    if (play->sceneNum != gFloraScene) { gFloraScene = play->sceneNum; gFloraSpots.clear(); gSnowCover = 0.0f; }
    const float dt = std::min(0.05f, ImGui::GetIO().DeltaTime);
    const int season = FloraSeason();
    const bool snowing = gWeatherShown.sky == royale::Sky::Snow && WeatherAmount() > 0.15f;
    if (snowing) gSnowCover = std::min(1.0f, gSnowCover + dt / 45.0f * (0.5f + WeatherAmount()));
    else gSnowCover = std::max(season == 3 ? 0.3f : 0.0f, gSnowCover - dt / 150.0f);   // it melts slowly (winter keeps a little)
    const bool snowOn = gSnowCover > 0.02f;
    if (gFoliage <= 0.01f && !snowOn) return;

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

    if (snowOn) {   // mounds of snow, thicker the longer it has snowed
        const float reach = 1000.0f;
        const int c0x = static_cast<int>(std::floor((px - reach) / kSnowCell)), c1x = static_cast<int>(std::floor((px + reach) / kSnowCell));
        const int c0z = static_cast<int>(std::floor((pz - reach) / kSnowCell)), c1z = static_cast<int>(std::floor((pz + reach) / kSnowCell));
        for (int cz = c0z; cz <= c1z; cz++)
            for (int cx = c0x; cx <= c1x; cx++) {
                if (Flora01(cx, cz, 31) > gSnowCover * 0.92f) continue;
                const float x = (static_cast<float>(cx) + 0.2f + 0.6f * Flora01(cx, cz, 32)) * kSnowCell, z = (static_cast<float>(cz) + 0.2f + 0.6f * Flora01(cx, cz, 33)) * kSnowCell;
                const float d = std::hypot(x - px, z - pz);
                if (d > reach) continue;
                const FloraSpot* spot = FloraSpotAt(2, cx, cz, x, z);
                if (spot == nullptr || !spot->ok) continue;
                const GpuMesh* m = GpuMeshFor(royale::MeshKind::SnowPatch, FloraHash(cx, cz, 34) % 4);
                if (m == nullptr || m->dl.empty()) continue;
                const float k = (0.75f + 0.5f * gSnowCover) * (0.85f + 0.5f * Flora01(cx, cz, 35)) * fade(d, reach);
                if (k > 0.02f) DrawFloraMesh(play, m, x, spot->y - 1.5f, z, Flora01(cx, cz, 36) * 6.2831853f, 0, 0, k);
            }
    }

    if (gFoliage > 0.01f) {
        // grass: patches (blocks of cells that are grassy) of tufts, leaning and swaying in the wind
        const float reach = 700.0f + 650.0f * std::min(1.5f, gFoliage);
        const int c0x = static_cast<int>(std::floor((px - reach) / kGrassCell)), c1x = static_cast<int>(std::floor((px + reach) / kGrassCell));
        const int c0z = static_cast<int>(std::floor((pz - reach) / kGrassCell)), c1z = static_cast<int>(std::floor((pz + reach) / kGrassCell));
        const float amp = 0.07f + 0.2f * wind, lean = 0.05f + 0.3f * wind;
        for (int cz = c0z; cz <= c1z; cz++)
            for (int cx = c0x; cx <= c1x; cx++) {
                if (Flora01(cx / 6, cz / 6, 41) < 0.5f) continue;                                   // not a grassy patch
                if (Flora01(cx, cz, 42) > 0.55f * std::min(1.2f, gFoliage) + 0.1f) continue;
                const float x = (static_cast<float>(cx) + 0.15f + 0.7f * Flora01(cx, cz, 43)) * kGrassCell, z = (static_cast<float>(cz) + 0.15f + 0.7f * Flora01(cx, cz, 44)) * kGrassCell;
                const float d = std::hypot(x - px, z - pz);
                if (d > reach) continue;
                const FloraSpot* spot = FloraSpotAt(0, cx, cz, x, z);
                if (spot == nullptr || !spot->ok) continue;
                const GpuMesh* m = GpuMeshFor(royale::MeshKind::Grass, (FloraHash(cx, cz, 45) % 4) + 4u * static_cast<uint32_t>(season));
                if (m == nullptr || m->dl.empty()) continue;
                const float phase = t * (1.6f + 2.4f * wind) + x * 0.011f + z * 0.009f;
                const float a = lean + std::sin(phase) * amp + std::sin(phase * 2.3f + 1.0f) * amp * 0.35f;
                const float k = 0.27f * (0.8f + 0.5f * Flora01(cx, cz, 46)) * fade(d, reach);
                if (k > 0.01f) DrawFloraMesh(play, m, x, spot->y - 1.0f, z, Flora01(cx, cz, 47) * 6.2831853f, dz * a, -dx * a, k);
            }

        // trees
        const float treeReach = 1800.0f + 1800.0f * std::min(1.5f, gFoliage);
        const int t0x = static_cast<int>(std::floor((px - treeReach) / kTreeCell)), t1x = static_cast<int>(std::floor((px + treeReach) / kTreeCell));
        const int t0z = static_cast<int>(std::floor((pz - treeReach) / kTreeCell)), t1z = static_cast<int>(std::floor((pz + treeReach) / kTreeCell));
        const float tamp = 0.008f + 0.03f * wind;
        for (int cz = t0z; cz <= t1z; cz++)
            for (int cx = t0x; cx <= t1x; cx++) {
                TreeSpot tr;
                if (!TreeIn(cx, cz, season, &tr)) continue;
                const float d = std::hypot(tr.x - px, tr.z - pz);
                if (d > treeReach) continue;
                const GpuMesh* m = GpuMeshFor(royale::MeshKind::Tree, tr.variant);
                if (m == nullptr || m->dl.empty()) continue;
                const float a = tamp * std::sin(t * (1.1f + wind) + tr.x * 0.004f) + wind * 0.02f;
                DrawFloraMesh(play, m, tr.x, tr.y - 2.0f, tr.z, tr.yaw, dz * a, -dx * a, tr.scale * std::max(0.01f, fade(d, treeReach)));
            }
    }
}

// Trunks are solid: stand against one and you are pushed out of it (the same sort of local solidity the climbing blocks have).
void ApplyTrees(Player* player) {
    if (!InField() || gFoliage <= 0.01f) return;
    const int season = FloraSeason();
    const float px = player->actor.world.pos.x, pz = player->actor.world.pos.z;
    gFloraBudget = 6;
    for (int cz = static_cast<int>(std::floor((pz - 160.0f) / kTreeCell)); cz <= static_cast<int>(std::floor((pz + 160.0f) / kTreeCell)); cz++)
        for (int cx = static_cast<int>(std::floor((px - 160.0f) / kTreeCell)); cx <= static_cast<int>(std::floor((px + 160.0f) / kTreeCell)); cx++) {
            TreeSpot tr;
            if (!TreeIn(cx, cz, season, &tr)) continue;
            const float r = 20.0f * tr.scale + 14.0f, ddx = px - tr.x, ddz = pz - tr.z, d = std::hypot(ddx, ddz);
            if (d < r && player->actor.world.pos.y < tr.y + 160.0f * tr.scale && d > 0.01f) {
                player->actor.world.pos.x = tr.x + ddx / d * r;
                player->actor.world.pos.z = tr.z + ddz / d * r;
            }
        }
}

// Rain and snow are the game's own: its rain streaks and its falling snow (the same flakes as its winter holiday mode), switched on and scaled with
// the server's spell of weather and the storm. The flat 2D weather in DrawWeather keeps only the tint, fog and lightning.
void DriveRealWeather() {
    if (!InField() || gPlayState == nullptr) return;
    PlayState* play = gPlayState;
    const float w = WeatherAmount();
    const bool rainSky = gWeatherShown.sky == royale::Sky::Rain || gWeatherShown.sky == royale::Sky::Thunder;
    const float rain = std::max(rainSky ? w : 0.0f, gStormWeather * 0.8f) * gWeatherDensity;
    const float snow = gWeatherShown.sky == royale::Sky::Snow ? w * gWeatherDensity : 0.0f;
    static bool rainManaged = false, snowManaged = false;
    const int wantRain = std::clamp(static_cast<int>(rain * 70.0f), 0, 130);
    if (wantRain > 0 || (rainManaged && play->envCtx.unk_EE[1] > 0)) {
        const int cur = play->envCtx.unk_EE[1];
        play->envCtx.unk_EE[1] = static_cast<u8>(cur < wantRain ? std::min(wantRain, cur + 2) : std::max(wantRain, cur - 2));
        rainManaged = true;
    } else rainManaged = false;
    const int wantSnow = std::clamp(static_cast<int>(snow * 40.0f) & ~1, 0, 62);
    if (wantSnow > 0) {
        snowManaged = true;
        play->envCtx.unk_EE[3] = static_cast<u8>(wantSnow);
        static int tryFrame = 0;
        if (tryFrame++ % 40 == 0) Actor_Spawn(&play->actorCtx, play, ACTOR_OBJECT_KANKYO, 0, 0, 0, 0, 0, 0, 3, false);   // a second one removes itself
    } else if (snowManaged) {
        play->envCtx.unk_EE[3] = 0;   // the flakes thin out by themselves
        if (play->envCtx.unk_EE[2] == 0) snowManaged = false;
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
void Sparkle(PlayState* play, const Vec3f& at, royale::Rarity rarity);

void Projectile_Draw(Actor*, PlayState* play) {
    const float dt = std::min(0.05f, ImGui::GetIO().DeltaTime);
    DrawFlora(play);
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
        GetItem_Draw(play, static_cast<s16>(r.gid));
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

void Boss_Draw(Actor* actor, PlayState* play) {
    auto of = gBossOf.find(actor);
    if (of == gBossOf.end()) return;
    const BossActor& b = gBosses[of->second];
    if (royale::IsDragonKind(static_cast<royale::BossKind>(b.kind))) { Dragon_Draw(actor, play, b); return; }
    if (b.skReady) { MiniBoss_Draw(actor, play, b); return; }
    const GpuMesh* mesh = GpuMeshFor(royale::MeshKind::Golem, static_cast<uint32_t>(b.kind));
    if (mesh == nullptr || mesh->dl.empty()) return;
    const float t = static_cast<float>(play->gameplayFrames) / royale::kTickHz;
    const float bob = b.moved > 0.5f ? std::fabs(std::sin(t * 6.0f)) * 9.0f : std::sin(t * 1.5f) * 2.0f;
    const float lean = b.smashAge < 0.45f ? 0.55f * std::sin(b.smashAge / 0.45f * 3.14159f) : 0.0f; // a swing: it pitches forward
    const float scale = royale::kBossDefs[b.kind].scale;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y + bob, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(actor->shape.rot.y * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    Matrix_RotateX(lean, MTXMODE_APPLY);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(mesh->dl.data()));
    CLOSE_DISPS(play->state.gfxCtx);
}

void Boss_Destroy(Actor* actor, PlayState* play) {
    ActorFunc orig = nullptr;
    auto of = gBossOf.find(actor);
    if (of != gBossOf.end()) {
        auto b = gBosses.find(of->second);
        if (b != gBosses.end()) { orig = b->second.origDestroy; gBosses.erase(b); }
        gBossOf.erase(of);
    }
    if (orig) orig(actor, play);
}

void ReconcileBosses(const royale::HudState& hud) {
    const bool show = gSession.Joined() && InField() && gSession.Client() &&
                      (hud.state == royale::MatchState::Drop || hud.state == royale::MatchState::InMatch || hud.state == royale::MatchState::Ending);
    std::unordered_map<uint32_t, bool> wanted;
    if (show) {
        for (const royale::net::BossNet& n : gSession.Client()->Bosses()) {
            const uint32_t id = n.Id();
            wanted[id] = true;
            gBossKindSeen[id] = n.kind;
            auto it = gBosses.find(id);
            if (it == gBosses.end()) {
                float y = 0;
                if (!FloorAt(n.x, n.z, &y)) {
                    if (!royale::IsDragonKind(static_cast<royale::BossKind>(n.kind))) continue;
                    y = GET_PLAYER(gPlayState)->actor.world.pos.y; // it flies: over a gap or the lava there may be no floor
                }
                Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ISHI, n.x, y, n.z, 0, n.rot, 0, 0, false);
                if (actor == nullptr) continue;
                BossActor b;
                b.actor = actor; b.origDestroy = actor->destroy; b.kind = n.kind; b.alt = b.talt = n.y; b.mode = n.mode;
                b.tx = n.x; b.tz = n.z; b.trot = n.rot; b.rot = n.rot; b.x = n.x; b.z = n.z; b.initialised = true;
                gBosses[id] = b;
                gBossOf[actor] = id;
                actor->update = Boss_Update;
                actor->draw = Boss_Draw;
                actor->destroy = Boss_Destroy;
                actor->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED;
                actor->uncullZoneForward = 5000.0f; actor->uncullZoneScale = 1500.0f; actor->uncullZoneDownward = 1500.0f;
                if (royale::IsDragonKind(static_cast<royale::BossKind>(n.kind))) {
                    actor->flags |= ACTOR_FLAG_DRAW_CULLING_DISABLED;
                    actor->uncullZoneForward = 12000.0f; actor->uncullZoneScale = 4000.0f; actor->uncullZoneDownward = 4000.0f;
                    actor->shape.shadowScale = 0.0f;
                } else {
                    actor->shape.shadowScale = 70.0f * royale::kBossDefs[n.kind].scale;
                }
            } else {
                BossActor& b = it->second;
                b.tx = n.x; b.tz = n.z; b.trot = n.rot; b.hp = n.hp / 255.0f; b.talt = n.y; b.mode = n.mode;
                if (n.smashing && b.smashAge > 0.3f) b.smashAge = 0.0f;
            }
        }
    }
    for (auto& [id, b] : gBosses) if (!wanted.count(id) && b.actor) Actor_Kill(b.actor);
}

// Name and health bar over each boss.
void DrawBossBars(ImDrawList* dl, ImFont* font, float scale) {
    if (!InField()) return;
    Player* pl = GET_PLAYER(gPlayState);
    for (const auto& [id, b] : gBosses) {
        if (!b.actor) continue;
        const float dx = b.x - pl->actor.world.pos.x, dz = b.z - pl->actor.world.pos.z;
        const float d = std::sqrt(dx * dx + dz * dz);
        const bool dragon = royale::IsDragonKind(static_cast<royale::BossKind>(b.kind));
        if (d > (dragon ? 9000.0f : 3800.0f)) continue;
        ImVec2 at;
        if (!WorldToScreen(b.x, b.actor->world.pos.y + (dragon ? 300.0f : 330.0f * royale::kBossDefs[b.kind].scale), b.z, &at)) continue;
        const float w = 130.0f * scale * std::clamp(1500.0f / (d + 700.0f), 0.55f, 1.4f), h = 11.0f * scale;
        const char* name = royale::kBossDefs[b.kind].name;
        const float ts = 17.0f * scale * std::clamp(1500.0f / (d + 700.0f), 0.7f, 1.3f);
        const ImVec2 sz = font->CalcTextSizeA(ts, FLT_MAX, 0.0f, name);
        dl->AddText(font, ts, ImVec2(at.x - sz.x * 0.5f + 1.5f, at.y - h - sz.y + 1.5f), IM_COL32(0, 0, 0, 230), name);
        dl->AddText(font, ts, ImVec2(at.x - sz.x * 0.5f, at.y - h - sz.y), IM_COL32(255, 120, 90, 255), name);
        dl->AddRectFilled(ImVec2(at.x - w * 0.5f - 2, at.y - h - 2), ImVec2(at.x + w * 0.5f + 2, at.y + 2), IM_COL32(0, 0, 0, 200));
        const ImU32 col = b.hp > 0.5f ? IM_COL32(120, 220, 90, 255) : b.hp > 0.25f ? IM_COL32(240, 200, 60, 255) : IM_COL32(230, 70, 60, 255);
        dl->AddRectFilled(ImVec2(at.x - w * 0.5f, at.y - h), ImVec2(at.x - w * 0.5f + w * std::clamp(b.hp, 0.0f, 1.0f), at.y), col);
    }
}

// Keep a ring of scenery alive around the player, the same for everyone because the list comes from the host.
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
    for (auto& [i, pa] : gProps) {
        const float dx = i < props.size() ? props[i].pos.x - px : 1e9f, dz = i < props.size() ? props[i].pos.z - pz : 1e9f;
        if (dx * dx + dz * dz > kPropSpawnRadius * kPropSpawnRadius * 1.4f && gCulledProps.insert(i).second) Actor_Kill(pa.actor);
    }
    if (gProps.size() >= kMaxPropActors) return;
    int spawned = 0;
    for (int pass = 0; pass < 2; pass++) {              // the climbing blocks first: without them the climbs are just chests in the air
        for (size_t i = 0; i < props.size() && spawned < 6 && gProps.size() < kMaxPropActors; i++) {
            if (royale::IsPlatform(props[i].kind) != (pass == 0)) continue;
            if (gProps.find(i) != gProps.end() || gBrokenProps.count(i)) continue;
            const float dx = props[i].pos.x - px, dz = props[i].pos.z - pz;
            if (dx * dx + dz * dz > kPropSpawnRadius * kPropSpawnRadius) continue;
            float y;
            if (!RawFloorAt(props[i].pos.x, props[i].pos.z, &y)) continue;
            SpawnProp(i, props[i], y);
            spawned++;
        }
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
        case ItemId::Bombs: case ItemId::BombAmmo: case ItemId::ShockwaveGrenade: return GID_BOMB;
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
    GetItem_Draw(play, static_cast<s16>(la.gid));
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
    gBannerText = (fromChest ? "CHEST OPENED  " : "GOT  ") + label;
    gBannerRarity = rarity;
    gBannerUntil = ImGui::GetTime() + 3.5;
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

// The storm. There is no wall to see (anything drawn over the world showed through hills and buildings); the edge is on the map and the
// timer. What you do see is the weather: while you stand outside the safe zone the sky goes dark (DriveTimeOfDay), and this puts a heavy
// grey-violet haze, driving rain, gusting wind streaks and lightning on the screen. It fades in and out over a few seconds.
void DrawStorm(ImDrawList* dl, ImVec2 ds, float scale, const royale::HudState& h) {
    const bool in = InField() && h.safeZone.radius > 0 && h.stormDamagePerSecond > 0;
    gStormWeather = std::clamp(gStormWeather + (in ? 0.02f : -0.03f), 0.0f, 1.0f);
    const float w = gStormWeather;
    if (w <= 0.0f || !InField()) return;
    const double t = ImGui::GetTime();
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ds, IM_COL32(22, 12, 44, static_cast<int>(150 * w)), IM_COL32(22, 12, 44, static_cast<int>(150 * w)),
                                IM_COL32(48, 34, 84, static_cast<int>(110 * w)), IM_COL32(48, 34, 84, static_cast<int>(110 * w)));
    const int drops = static_cast<int>(260 * w);
    for (int i = 0; i < drops; i++) { // slanting rain, three depths
        const float depth = 0.6f + 0.4f * (i % 3) / 2.0f;
        const float x = std::fmod(i * 97.3f + static_cast<float>(t) * 420.0f * depth, ds.x + 320.0f) - 160.0f;
        const float y = std::fmod(i * 61.7f + static_cast<float>(t) * 1250.0f * depth, ds.y + 120.0f) - 60.0f;
        dl->AddLine(ImVec2(x, y), ImVec2(x - 16 * scale * depth, y + 40 * scale * depth), IM_COL32(205, 195, 255, static_cast<int>(130 * w * depth)), 1.6f * scale * depth);
    }
    for (int i = 0; i < 14; i++) { // long gust streaks racing across
        const float y = std::fmod(i * 131.1f, ds.y);
        const float x = std::fmod(static_cast<float>(t) * (900.0f + 70.0f * i) + i * 200.0f, ds.x * 1.6f) - ds.x * 0.3f;
        dl->AddLine(ImVec2(x, y), ImVec2(x - 150 * scale, y + 14 * scale), IM_COL32(190, 180, 235, static_cast<int>(50 * w)), 2.0f * scale);
    }
    const float cycle = std::fmod(static_cast<float>(t), 6.1f); // lightning every few seconds: a double flash, then a fork
    const float flash = cycle < 0.12f ? 1.0f - cycle / 0.12f : (cycle > 0.25f && cycle < 0.40f ? 0.7f * (1.0f - (cycle - 0.25f) / 0.15f) : 0.0f);
    if (flash > 0.0f) dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(235, 225, 255, static_cast<int>(flash * 170.0f * w)));
    if (cycle < 0.32f) {
        const int seed = static_cast<int>(t / 6.1f);
        float x = ds.x * (0.15f + 0.7f * std::fmod(seed * 0.381f, 1.0f)), y = 0;
        while (y < ds.y * 0.7f) {
            const float nx = x + (std::fmod((y + seed) * 12.9898f, 1.0f) - 0.5f) * 70.0f * scale, ny = y + 40.0f * scale;
            dl->AddLine(ImVec2(x, y), ImVec2(nx, ny), IM_COL32(240, 235, 255, static_cast<int>(230 * w)), 3.0f * scale);
            x = nx; y = ny;
        }
    }
}

// ---- seasons and weather ----------------------------------------------------------------------------------------------------
// What the sky is doing (the server's spell of weather) drawn over the game: a seasonal tint, then rain, fog, snow, ash or sand streaming
// across the screen. Everything fades in and out as spells change. The "Weather density" option scales the particles (0 turns them off).

float WeatherAmount() { return gWeatherBlend * gWeatherShown.Strength(); }

void DrawWeather(ImDrawList* dl, ImVec2 ds, float scale, const royale::HudState& h) {
    const bool live = InField() && gSession.Joined() && (h.state == royale::MatchState::Drop || h.state == royale::MatchState::InMatch);
    const bool active = live && h.weather.sky != royale::Sky::Clear && h.weather.intensity > 0;
    if (active) gWeatherShown = h.weather;
    gWeatherBlend = std::clamp(gWeatherBlend + (active ? 0.012f : -0.012f), 0.0f, 1.0f);
    if (live) gWeatherShown.season = h.weather.season;
    const double t = ImGui::GetTime();
    const float tf = static_cast<float>(t);
    if (!live) return;
    // The season colours the whole picture a little.
    static const ImU32 kSeasonTint[4] = { IM_COL32(150, 230, 140, 16), IM_COL32(255, 214, 120, 18), IM_COL32(255, 150, 60, 26), IM_COL32(190, 215, 255, 30) };
    dl->AddRectFilled(ImVec2(0, 0), ds, kSeasonTint[static_cast<int>(h.weather.season) & 3]);
    const float w = WeatherAmount();
    const float density = gWeatherDensity;
    if (w > 0.01f && density > 0.0f) {
        const float d = std::clamp(w * density, 0.0f, 2.0f);
        switch (gWeatherShown.sky) {
            case royale::Sky::Rain: case royale::Sky::Thunder: {
                const bool thunder = gWeatherShown.sky == royale::Sky::Thunder;
                dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(40, 50, 70, static_cast<int>((thunder ? 80 : 50) * w)));
                break;
            }
            case royale::Sky::Fog: {
                dl->AddRectFilledMultiColor(ImVec2(0, 0), ds, IM_COL32(205, 210, 215, static_cast<int>(120 * w)), IM_COL32(205, 210, 215, static_cast<int>(120 * w)),
                                            IM_COL32(215, 218, 222, static_cast<int>(185 * w)), IM_COL32(215, 218, 222, static_cast<int>(185 * w)));
                const int n = static_cast<int>(7 * std::min(1.5f, density));
                for (int i = 0; i < n; i++) {   // slow drifting banks
                    const float x = std::fmod(tf * (22.0f + 9.0f * i) + i * 310.0f, ds.x + 700.0f) - 350.0f, y = ds.y * (0.25f + 0.1f * i);
                    dl->AddRectFilledMultiColor(ImVec2(x, y - 55 * scale), ImVec2(x + 650 * scale, y + 55 * scale), IM_COL32(225, 228, 232, 0), IM_COL32(225, 228, 232, static_cast<int>(60 * w)),
                                                IM_COL32(225, 228, 232, 0), IM_COL32(225, 228, 232, static_cast<int>(60 * w)));
                }
                break;
            }
            case royale::Sky::Snow: {
                dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(220, 230, 245, static_cast<int>(48 * w)));
                break;
            }
            case royale::Sky::Ash: {
                dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(60, 40, 36, static_cast<int>(95 * w)));
                const int n = static_cast<int>(120 * d);
                for (int i = 0; i < n; i++) {
                    const float depth = 0.5f + 0.5f * (i % 4) / 3.0f;
                    const float x = std::fmod(i * 91.3f + tf * 60.0f * depth, ds.x + 120.0f) - 60.0f + std::sin(tf + i) * 18.0f * scale;
                    const float y = ds.y - std::fmod(i * 53.1f + tf * 70.0f * depth, ds.y + 40.0f) + 20.0f;   // embers rise
                    const bool ember = i % 3 == 0;
                    dl->AddCircleFilled(ImVec2(x, y), (1.2f + 2.0f * depth) * scale, ember ? IM_COL32(255, 150, 50, 230) : IM_COL32(150, 145, 140, 170), 6);
                }
                break;
            }
            case royale::Sky::Sandstorm: {
                dl->AddRectFilledMultiColor(ImVec2(0, 0), ds, IM_COL32(205, 165, 105, static_cast<int>(95 * w)), IM_COL32(205, 165, 105, static_cast<int>(95 * w)),
                                            IM_COL32(190, 145, 85, static_cast<int>(165 * w)), IM_COL32(190, 145, 85, static_cast<int>(165 * w)));
                const int n = static_cast<int>(120 * d);
                for (int i = 0; i < n; i++) {
                    const float depth = 0.5f + 0.5f * (i % 3) / 2.0f;
                    const float y = std::fmod(i * 71.9f, ds.y), x = std::fmod(i * 131.3f + tf * 1300.0f * depth, ds.x + 400.0f) - 200.0f;
                    dl->AddLine(ImVec2(x, y), ImVec2(x - 70 * scale * depth, y + 6 * scale), IM_COL32(235, 200, 140, static_cast<int>(120 * depth)), 1.5f * scale);
                }
                break;
            }
            default: break;
        }
    }
    // A lightning bolt landing nearby lights up the whole screen.
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

// Bottom-left map of the whole field: the storm in purple, the safe zone, chests by rarity, other players and you.
void DrawMinimap(ImDrawList* dl, ImVec2 ds, float scale, const royale::HudState& h) {
    if (h.map.radius <= 0 || !InField()) return;
    Player* pl = GET_PLAYER(gPlayState);
    const float R = 92.0f * scale;
    const ImVec2 c(R + 20.0f * scale, ds.y - R - 28.0f * scale);
    const float k = R / h.map.radius;
    auto toMap = [&](float x, float z) { return ImVec2(c.x + (x - h.map.center.x) * k, c.y - (z - h.map.center.z) * k); };
    auto inside = [&](ImVec2 p) { return (p.x - c.x) * (p.x - c.x) + (p.y - c.y) * (p.y - c.y) < R * R; };

    dl->AddCircleFilled(c, R + 5.0f * scale, IM_COL32(0, 0, 0, 175), 72);
    dl->AddCircleFilled(c, R, IM_COL32(104, 48, 170, 190), 72);                       // everything is storm...
    const ImVec2 zc = toMap(h.safeZone.center.x, h.safeZone.center.z);
    dl->AddCircleFilled(zc, h.safeZone.radius * k, IM_COL32(28, 62, 42, 255), 72);      // ...except the safe zone
    dl->AddCircle(zc, h.safeZone.radius * k, IM_COL32(255, 255, 255, 235), 72, 2.0f * scale);
    dl->AddCircle(c, R, IM_COL32(255, 210, 70, 255), 72, 2.0f * scale);
    dl->AddCircle(c, R + 5.0f * scale, IM_COL32(150, 108, 30, 255), 72, 1.5f * scale);
    dl->AddText(ImGui::GetFont(), 12.0f * scale, ImVec2(c.x - 4.0f * scale, c.y - R - 17.0f * scale), IM_COL32(255, 222, 110, 255), "N");

    {   // supply drops: a pulsing star on the announced spot, and on the crate once it has landed
        const double now = ImGui::GetTime();
        const float pulse = 0.75f + 0.25f * static_cast<float>(std::sin(now * 5.0));
        auto star = [&](float wx, float wz, ImU32 col) {
            ImVec2 p = toMap(wx, wz);
            const float dx = p.x - c.x, dy = p.y - c.y, d = std::sqrt(dx * dx + dy * dy);
            if (d > R - 4.0f * scale) { p = ImVec2(c.x + dx / d * (R - 4.0f * scale), c.y + dy / d * (R - 4.0f * scale)); }   // off the map's edge: pinned to the rim
            const float u = 7.0f * scale * pulse;
            dl->AddQuadFilled(ImVec2(p.x, p.y - u), ImVec2(p.x + u * 0.4f, p.y), ImVec2(p.x, p.y + u), ImVec2(p.x - u * 0.4f, p.y), col);
            dl->AddQuadFilled(ImVec2(p.x - u, p.y), ImVec2(p.x, p.y - u * 0.4f), ImVec2(p.x + u, p.y), ImVec2(p.x, p.y + u * 0.4f), col);
        };
        gSupplyMarks.erase(std::remove_if(gSupplyMarks.begin(), gSupplyMarks.end(), [&](const SupplyMark& m) { return now > m.until; }), gSupplyMarks.end());
        for (const SupplyMark& m : gSupplyMarks) star(m.x, m.z, IM_COL32(255, 160, 60, 255));
        for (const auto& [aid, al] : gAllies) star(al.x, al.z, al.owner == royale::net::kNoPlayer16 ? IM_COL32(255, 222, 110, 255) : al.owner == h.selfId ? IM_COL32(120, 255, 150, 255) : IM_COL32(170, 180, 210, 255));
        if (gSession.Client()) for (const auto& l : gSession.Client()->Loot()) if (l.supply && l.chest && !l.taken) star(l.x, l.z, IM_COL32(255, 220, 90, 255));
    }
    if (gSession.Client() && MapOption("MapChests")) {
        for (const auto& l : gSession.Client()->Loot()) {
            if (l.taken || !l.chest) continue;
            const float dx = l.x - pl->actor.world.pos.x, dz = l.z - pl->actor.world.pos.z;
            if (dx * dx + dz * dz > 2800.0f * 2800.0f) continue;
            const ImVec2 p = toMap(l.x, l.z);
            if (!inside(p)) continue;
            dl->AddRectFilled(ImVec2(p.x - 2.0f * scale, p.y - 2.0f * scale), ImVec2(p.x + 2.0f * scale, p.y + 2.0f * scale), RarityU32(static_cast<royale::Rarity>(l.rarity)));
        }
    }
    if (gSession.Client()) {
        for (const royale::Poi& poi : gSession.Client()->Pois()) {
            const ImVec2 p = toMap(poi.center.x, poi.center.z);
            if (!inside(p)) continue;
            const float u = 4.0f * scale;
            dl->AddQuadFilled(ImVec2(p.x, p.y - u), ImVec2(p.x + u, p.y), ImVec2(p.x, p.y + u), ImVec2(p.x - u, p.y), IM_COL32(255, 222, 110, 255));
            dl->AddText(ImGui::GetFont(), 10.5f * scale, ImVec2(p.x + u + 2.0f, p.y - 6.0f * scale), IM_COL32(255, 240, 190, 235), royale::kPoiNames[poi.name]);
        }
    }
    if (gSession.Client() && MapOption("MapEnemies")) {
        for (const auto& bn : gSession.Client()->Bosses()) { // mini bosses: a big purple diamond
            const ImVec2 p = toMap(bn.x, bn.z);
            if (!inside(p)) continue;
            const float u = 6.0f * scale;
            dl->AddQuadFilled(ImVec2(p.x, p.y - u), ImVec2(p.x + u, p.y), ImVec2(p.x, p.y + u), ImVec2(p.x - u, p.y), IM_COL32(190, 60, 255, 255));
            dl->AddQuad(ImVec2(p.x, p.y - u), ImVec2(p.x + u, p.y), ImVec2(p.x, p.y + u), ImVec2(p.x - u, p.y), IM_COL32(255, 255, 255, 230), 1.5f);
        }
    }
    for (const auto& st : gSession.Puppets()) {
        if (!st.alive || !MapOption(st.isBot ? "MapBots" : "MapPlayers")) continue;
        const ImVec2 p = toMap(st.x, st.z);
        if (inside(p)) dl->AddCircleFilled(p, 3.2f * scale, st.isBot ? IM_COL32(255, 100, 100, 255) : IM_COL32(255, 170, 60, 255));
    }
    // You: a triangle pointing the way Link faces.
    const ImVec2 me = toMap(pl->actor.world.pos.x, pl->actor.world.pos.z);
    const float th = pl->actor.shape.rot.y * (3.14159265f / 32768.0f);
    const ImVec2 fwd(std::sin(th), -std::cos(th)), side(-fwd.y, fwd.x);
    const float u = 6.0f * scale;
    dl->AddTriangleFilled(ImVec2(me.x + fwd.x * u * 1.3f, me.y + fwd.y * u * 1.3f), ImVec2(me.x - fwd.x * u + side.x * u * 0.8f, me.y - fwd.y * u + side.y * u * 0.8f),
                          ImVec2(me.x - fwd.x * u - side.x * u * 0.8f, me.y - fwd.y * u - side.y * u * 0.8f), IM_COL32(120, 255, 140, 255));
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
        case ItemId::ShockwaveGrenade: return "gItemIconBombTex";
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
        case ItemId::ShockwaveGrenade:
            dl->AddCircleFilled(c, u * 0.38f, IM_COL32(60, 70, 90, 255), 16);
            for (int i = 0; i < 8; i++) {
                const float a = i * 0.7853982f;
                dl->AddLine(ImVec2(c.x + std::cos(a) * u * 0.5f, c.y + std::sin(a) * u * 0.5f), ImVec2(c.x + std::cos(a) * u * 0.9f, c.y + std::sin(a) * u * 0.9f), cyan, th * 1.2f);
            }
            break;
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
    slots.push_back({ ShortName(h.weapon), h.weapon == royale::ItemId::BasicSword ? std::string("Starter") : std::string(RarityName(h.weaponRarity)), RarityU32(h.weaponRarity), true, true, 0.0f, 0, h.weapon, ammoOf(h.weapon) });
    for (int i = 0; i < royale::kMaxReserveWeapons; i++) {
        if (i < static_cast<int>(h.inv.reserve.size())) {
            const auto& r = h.inv.reserve[i];
            const royale::ItemId id = static_cast<royale::ItemId>(r.item);
            slots.push_back({ ShortName(id), RarityName(static_cast<royale::Rarity>(r.rarity)), RarityU32(static_cast<royale::Rarity>(r.rarity)), true, false, 0.0f, i + 1, id, ammoOf(id) });
        } else {
            slots.push_back({ "", "", grey, false, false, 0.0f, 0, none });
        }
    }
    if (h.hasShield) slots.push_back({ ShortName(h.shield), RarityName(h.shieldRarity), RarityU32(h.shieldRarity), true, false, 0.0f, 0, h.shield });
    else slots.push_back({ "", "Shield", grey, false, false, 0.0f, 0, none });
    if (!h.inv.potions.empty()) {
        const auto& p = h.inv.potions.front();
        const royale::ItemId id = static_cast<royale::ItemId>(p.item);
        slots.push_back({ ShortName(id), "x" + std::to_string(h.inv.potions.size()), RarityU32(static_cast<royale::Rarity>(p.rarity)), true, false, 0.0f, 10, id });
    } else {
        slots.push_back({ "", "Potions", grey, false, false, 0.0f, 10, none });
    }
    if (h.inv.hasAbility) {
        const royale::ItemId id = static_cast<royale::ItemId>(h.inv.ability.item);
        const float cd = royale::AbilityOf(id).cooldown;
        const bool lowMagic = h.magic + 0.001f < royale::AbilityMagic(id);
        slots.push_back({ ShortName(id), h.abilityReadyIn > 0.05f ? ClockText(h.abilityReadyIn) : lowMagic ? "NO MAGIC" : "READY", RarityU32(static_cast<royale::Rarity>(h.inv.ability.rarity)), true, false,
                          cd > 0 ? std::min(1.0f, h.abilityReadyIn / cd) : 0.0f, 11, id });
    } else {
        slots.push_back({ "", "Ability", grey, false, false, 0.0f, 11, none });
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
        {   // which button uses the slot
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

bool gEmotePanelOpen = false;
void StartEmote(int index, const royale::HudState& hud); // below, with the emote logic

// The emote button (bottom right): tap it to open the list, tap an emote to play it. C-Right plays them in turn.
void DrawEmotes(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const royale::HudState& h) {
    if (!h.selfAlive || !InField()) return;
    ImGuiIO& io = ImGui::GetIO();
    const bool tap = ImGui::IsMouseClicked(0) && !io.WantCaptureMouse;
    const float w = 128.0f * scale, hgt = 40.0f * scale;
    const ImVec2 a(ds.x - w - 18.0f * scale, ds.y - hgt - 24.0f * scale), b(a.x + w, a.y + hgt);
    auto inside = [&](ImVec2 p0, ImVec2 p1) { return io.MousePos.x >= p0.x && io.MousePos.x <= p1.x && io.MousePos.y >= p0.y && io.MousePos.y <= p1.y; };
    dl->AddRectFilled(a, b, OotPanel(200), 6.0f * scale);
    dl->AddRect(a, b, gEmotePanelOpen ? IM_COL32(255, 236, 120, 255) : IM_COL32(190, 190, 200, 255), 6.0f * scale, 0, 2.5f * scale);
    dl->AddText(font, 16.0f * scale, ImVec2(a.x + 12 * scale, a.y + 10 * scale), IM_COL32(255, 255, 255, 255), "EMOTE");
    if (tap && inside(a, b)) gEmotePanelOpen = !gEmotePanelOpen;
    if (!gEmotePanelOpen) return;
    for (int i = 0; i < royale::kEmoteCount; i++) {
        const ImVec2 ea(a.x - 40.0f * scale, a.y - (i + 1) * (hgt + 6.0f * scale)), eb(b.x, ea.y + hgt);
        dl->AddRectFilled(ea, eb, OotPanel(215), 6.0f * scale);
        dl->AddRect(ea, eb, IM_COL32(120, 200, 255, 255), 6.0f * scale, 0, 2.0f * scale);
        dl->AddText(font, 15.0f * scale, ImVec2(ea.x + 10 * scale, ea.y + 11 * scale), IM_COL32(255, 255, 255, 255), royale::kEmoteNames[i]);
        if (tap && inside(ea, eb)) StartEmote(i, h);
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
// A big line of text across the top for the moments that matter (the dragon arriving, what a chest gave you), an item that floats up out of
// a chest you open, and the world-space effects of the dragon: warning rings on the ground, the blast, and its breath.
struct Banner { std::string text; ImU32 colour; double until; double start; };
std::vector<Banner> gBanners;
void ShowBanner(const std::string& text, ImU32 colour, float seconds = 2.6f) {
    const double now = ImGui::GetTime();
    gBanners.push_back({ text, colour, now + seconds, now });
    if (gBanners.size() > 3) gBanners.erase(gBanners.begin());
}

// Small "+5 Rupees" lines that float up on the right when something drops out of a rock or bush.
// The kill feed, top right: who got whom.
struct FeedLine { std::string text; ImU32 colour; double at; };
std::vector<FeedLine> gFeed;
void AddFeed(const std::string& text, ImU32 colour) {
    gFeed.push_back({ text, colour, ImGui::GetTime() });
    if (gFeed.size() > 6) gFeed.erase(gFeed.begin());
}
void DrawFeed(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale) {
    const double now = ImGui::GetTime();
    float y = 14.0f * scale;
    for (const FeedLine& f : gFeed) {
        const double age = now - f.at;
        if (age > 7.0) continue;
        const float a = static_cast<float>(std::min(1.0, (7.0 - age) / 1.0));
        const float size = 17.0f * scale;
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, f.text.c_str());
        const ImVec2 pos(ds.x - sz.x - 18.0f * scale, y);
        dl->AddRectFilled(ImVec2(pos.x - 8 * scale, pos.y - 2 * scale), ImVec2(pos.x + sz.x + 8 * scale, pos.y + sz.y + 2 * scale), IM_COL32(6, 12, 18, static_cast<int>(150 * a)), 5.0f * scale);
        dl->AddText(font, size, pos, (f.colour & 0x00FFFFFF) | (static_cast<ImU32>(255 * a) << 24), f.text.c_str());
        y += sz.y + 7.0f * scale;
    }
    gFeed.erase(std::remove_if(gFeed.begin(), gFeed.end(), [&](const FeedLine& f) { return now - f.at > 7.0; }), gFeed.end());
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

struct StrikeFx { float x, z, radius; double land; bool boomed; bool bolt; };   // bolt: lightning from a thunderstorm
std::vector<StrikeFx> gStrikeFx;

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

Color_RGBA8 BossThemeColour(int kind) {
    switch (static_cast<royale::BossKind>(kind)) {
        case royale::BossKind::DragonWater: return { 90, 200, 255, 255 };
        case royale::BossKind::DragonForest: return { 120, 235, 90, 255 };
        case royale::BossKind::DragonShadow: return { 210, 90, 255, 255 };
        case royale::BossKind::DragonSand: return { 255, 205, 90, 255 };
        default: return { 255, 120, 40, 255 };
    }
}

void SparkBurst(PlayState* play, float x, float y, float z, Color_RGBA8 prim, int count, float speed) {
    Color_RGBA8 env = { 255, 255, 255, 255 };
    for (int i = 0; i < count; i++) {
        const float a = Rand_ZeroOne() * 6.2831853f, up = 0.3f + Rand_ZeroOne() * 0.9f;
        Vec3f pos = { x, y, z };
        Vec3f vel = { std::cos(a) * speed * (0.4f + Rand_ZeroOne()), speed * up, std::sin(a) * speed * (0.4f + Rand_ZeroOne()) };
        Vec3f accel = { 0.0f, -0.35f, 0.0f };
        EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 60, 40);
    }
}

// Every game frame: strike rings and blasts, the dragon's breath.
void UpdateBossWorldFx() {
    if (!InField() || gPlayState == nullptr) { gStrikeFx.clear(); return; }
    const double now = ImGui::GetTime();
    Player* pl = GET_PLAYER(gPlayState);
    for (StrikeFx& s : gStrikeFx) {
        const float ground = GroundY(gPlayState, s.x, s.z, pl->actor.world.pos.y);
        if (now < s.land) {
            const float danger = static_cast<float>(1.0 - (s.land - now) / 1.4);   // sparks come faster as the blast gets close
            const int n = 5 + static_cast<int>(danger * 9.0f);
            Color_RGBA8 prim = { 255, static_cast<u8>(210 - danger * 150.0f), 40, 255 }, env = { 255, 60, 20, 255 };
            if (s.bolt) { prim = { 210, 225, 255, 255 }; env = { 90, 120, 255, 255 }; }   // lightning crackles blue-white
            for (int i = 0; i < n; i++) {
                const float a = Rand_ZeroOne() * 6.2831853f;
                Vec3f pos = { s.x + std::cos(a) * s.radius, ground + 6.0f, s.z + std::sin(a) * s.radius };
                Vec3f vel = { 0.0f, 1.2f + Rand_ZeroOne() * 1.5f, 0.0f };
                Vec3f accel = { 0.0f, 0.0f, 0.0f };
                EffectSsKiraKira_SpawnDispersed(gPlayState, &pos, &vel, &accel, &prim, &env, 25, 32);
            }
        } else if (!s.boomed) {
            s.boomed = true;
            Vec3f pos = { s.x, ground + 20.0f, s.z }, vel = { 0, 0, 0 }, accel = { 0, 0, 0 };
            if (s.bolt) {
                gBoltFlashUntil = now + 0.35;
                for (int k = 0; k < 14; k++) {   // the bolt itself: a column of white sparks from the sky
                    Vec3f col = { s.x + (Rand_ZeroOne() - 0.5f) * 16.0f, ground + 40.0f + k * 55.0f, s.z + (Rand_ZeroOne() - 0.5f) * 16.0f };
                    Vec3f v = { 0, 0, 0 }, a = { 0, 0, 0 };
                    Color_RGBA8 p = { 240, 245, 255, 255 }, e2 = { 120, 150, 255, 255 };
                    EffectSsKiraKira_SpawnDispersed(gPlayState, &col, &v, &a, &p, &e2, 260, 10);
                }
                SparkBurst(gPlayState, s.x, ground + 20.0f, s.z, { 190, 210, 255, 255 }, 30, 8.0f);
            } else {
                EffectSsBomb2_SpawnLayered(gPlayState, &pos, &vel, &accel, 90, 14);
                SparkBurst(gPlayState, s.x, ground + 20.0f, s.z, { 255, 190, 60, 255 }, 26, 7.0f);
            }
            Audio_PlaySoundGeneral(NA_SE_IT_BOMB_EXPLOSION, &pos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        }
    }
    gStrikeFx.erase(std::remove_if(gStrikeFx.begin(), gStrikeFx.end(), [&](const StrikeFx& s) { return now > s.land + 0.8; }), gStrikeFx.end());
    // Breath: a stream of sparks out of its mouth along the way it faces.
    for (const auto& [id, b] : gBosses) {
        if (!b.actor || !royale::IsDragonKind(static_cast<royale::BossKind>(b.kind)) || b.mode != static_cast<int>(royale::DragonMode::Breath)) continue;
        const float ang = b.rot * (3.14159265f / 32768.0f), fx = std::sin(ang), fz = std::cos(ang);
        Color_RGBA8 prim = BossThemeColour(b.kind), env = { 255, 255, 255, 255 };
        for (int i = 0; i < 14; i++) {
            const float spread = (Rand_ZeroOne() - 0.5f) * 0.45f, sp = 20.0f + Rand_ZeroOne() * 18.0f;
            const float dx = fx * std::cos(spread) - fz * std::sin(spread), dz = fz * std::cos(spread) + fx * std::sin(spread);
            Vec3f pos = { b.x + fx * 190.0f, b.actor->world.pos.y + 120.0f, b.z + fz * 190.0f };
            Vec3f vel = { dx * sp, -sp * 0.28f, dz * sp };
            Vec3f accel = { 0.0f, -0.4f, 0.0f };
            EffectSsKiraKira_SpawnDispersed(gPlayState, &pos, &vel, &accel, &prim, &env, 30, 70);
        }
    }
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
    if (live && h.state == royale::MatchState::Drop) {
        centered(ds.y * 0.2f, green, 30 * scale, gSkydiving ? "SKYDIVE - stick steers, hold Z to dive faster" : "DROP - you are protected for a moment");
    }
    if (h.state == royale::MatchState::Countdown && gSkydiving && !splashing) centered(ds.y * 0.16f + 90 * scale, white, 22 * scale, "You will fall from the sky when the countdown ends");

    if (h.state == royale::MatchState::Ending) { DrawResultsPanel(dl, font, ds, scale, h); DrawReplay(dl, font, ds, scale, h); }

    DrawPickupFx(dl, ds, scale);
    DrawFeed(dl, font, ds, scale);
    DrawGains(dl, font, ds, scale);
    DrawHitEffects(dl, font, ds, scale);
    DrawBanners(dl, font, ds, scale);

    if (!live || !h.haveSelf) return;

    // What is at your feet: chests say how rare they are (not what is inside); items on the ground say what they are.
    if (InField() && h.selfAlive && gSession.Client()) {
        const size_t near = NearestLootIndex();
        const auto& loot = gSession.Client()->Loot();
        if (near != kNoLoot && near < loot.size()) {
            const royale::Rarity r = static_cast<royale::Rarity>(loot[near].rarity);
            if (loot[near].chest) {
                centered(ds.y * 0.66f, loot[near].special ? IM_COL32(255, 130, 190, 255) : RarityU32(r), 26 * scale, loot[near].special ? std::string("Heart Container Chest") : std::string(RarityName(r)) + " Chest");
                centered(ds.y * 0.66f + 31 * scale, white, 20 * scale, "A: open");
            } else {
                centered(ds.y * 0.66f, RarityU32(r), 26 * scale, ItemLabel(static_cast<royale::ItemId>(loot[near].item), r));
                centered(ds.y * 0.66f + 31 * scale, white, 20 * scale, "A: take or swap");
            }
        }
        else if (const int ally = NearbyFreeAlly(); ally >= 0) {
            const royale::AllyDef& def = royale::kAllyDefs[ally];
            const bool afford = h.rupees >= def.price;
            centered(ds.y * 0.66f, IM_COL32(255, 222, 110, 255), 26 * scale, std::string(def.name) + " " + def.title);
            centered(ds.y * 0.66f + 31 * scale, afford ? white : IM_COL32(255, 130, 120, 255), 20 * scale,
                     afford ? "A: hire for " + std::to_string(def.price) + " rupees" : "Needs " + std::to_string(def.price) + " rupees (you have " + std::to_string(h.rupees) + ")");
        } else if (MayaNear()) {
            centered(ds.y * 0.66f, IM_COL32(255, 170, 215, 255), 26 * scale, "Maya");
            centered(ds.y * 0.66f + 31 * scale, white, 20 * scale, "A: talk");
        } else if (LiloNear()) {
            centered(ds.y * 0.66f, IM_COL32(235, 235, 230, 255), 26 * scale, "Lilo");
            centered(ds.y * 0.66f + 31 * scale, white, 20 * scale, "A: talk to the cat");
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
    DrawAllyLabels(dl, font, ds, scale, h);
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

    // Top right: the match at a glance (where the game's C buttons used to be; the hotbar at the bottom does their job now). Right-aligned,
    // under the safe-zone compass: how many are left, the zone, the weather and anything that is affecting you.
    {
        const float rx = ds.x - 16 * scale, line = 24 * scale;
        float y = 150 * scale;
        auto textR = [&](ImU32 col, float size, const std::string& t) {
            const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, t.c_str());
            text(rx - sz.x, y, col, size, t);
            y += size + 4 * scale;
        };
        textR(gold, 24 * scale, "ALIVE " + std::to_string(h.alive) + " / " + std::to_string(h.playerLimit));
        if (h.stormPhase >= royale::kStormPhaseCount) {
            textR(red, 20 * scale, "FINAL ZONE");
        } else {
            textR(h.stormShrinking ? red : white, 20 * scale, "Zone " + std::to_string(h.stormPhase + 1) + "/" + std::to_string(royale::kStormPhaseCount) + (h.stormShrinking ? "  CLOSING " : "  holds ") + ClockText(h.stormSecondsLeft));
        }
        if (h.stormDamagePerSecond > 0) textR(red, 22 * scale, "IN THE STORM!");
        static const char* kSeasonName[4] = { "Spring", "Summer", "Autumn", "Winter" };
        static const char* kSkyName[7] = { "Clear", "Rain", "Thunder", "Fog", "Snow", "Ash", "Sandstorm" };
        textR(grey, 17 * scale, std::string(CurrentMap().name) + "  -  " + kSeasonName[static_cast<int>(h.weather.season) & 3] + ", " + kSkyName[static_cast<int>(h.weather.sky) % 7]);
        if (h.selfAlive) {
            if (h.adultLeft > 0.0f) textR(gold, 20 * scale, "ADULT POWER  " + ClockText(h.adultLeft));
            if (h.inv.hasMark) textR(green, 17 * scale, "Farore's Wind marked");
            if (h.invulnLeft > 0) textR(gold, 18 * scale, "INVULNERABLE " + ClockText(h.invulnLeft));
            if (h.speedLeft > 0) textR(green, 18 * scale, "SPEED UP " + ClockText(h.speedLeft));
            if (h.revealLeft > 0) textR(green, 18 * scale, "REVEALING " + ClockText(h.revealLeft));
            if (h.burnLeft > 0) textR(red, 18 * scale, "BURNING");
            if (h.stunLeft > 0) textR(red, 18 * scale, "STUNNED");
            if (h.shieldLeft > 0) textR(green, 18 * scale, "DAMAGE REDUCED " + ClockText(h.shieldLeft));
            for (int slot = 0; slot < royale::kGearSlots; slot++) {   // what you are wearing
                if (!(h.inv.gearMask & (1 << slot))) continue;
                const royale::Rarity gr = static_cast<royale::Rarity>(h.inv.gear[slot].rarity);
                textR(RarityU32(gr), 16 * scale, ItemLabel(static_cast<royale::ItemId>(h.inv.gear[slot].item), gr));
            }
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
        ImVec2 sz = font->CalcTextSizeA(16 * scale, FLT_MAX, 0.0f, outside ? "SAFE ZONE" : "inside");
        text(c.x - sz.x * 0.5f, c.y + r + 14 * scale, outside ? red : green, 16 * scale, outside ? "SAFE ZONE" : "inside");
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

struct EmoteState {
    int id = -1;               // which emote is playing, -1 for none
    double endAt = 0;
    Actor* actor = nullptr;    // the double that performs it
};
EmoteState gEmote;
int gNextEmote = 0;            // what C-Right plays next

void StopEmote() {
    if (gEmote.id < 0) return;
    if (gEmote.actor) Actor_Kill(gEmote.actor);
    gEmote = EmoteState{};
}

void StartEmote(int index, const royale::HudState& hud) {
    if (!LiveAndAlive(hud) || !InField() || gSkydiving) return;
    StopEmote();
    Player* player = GET_PLAYER(gPlayState);
    royale::PuppetState me;
    me.x = player->actor.world.pos.x; me.y = player->actor.world.pos.y; me.z = player->actor.world.pos.z;
    me.rot = player->actor.shape.rot.y; me.weapon = hud.weapon; me.tunic = gLocalTunic;
    Actor* a = SpawnEmoteDouble(me, index);
    if (a == nullptr) return;
    gEmote.id = index;
    gEmote.actor = a;
    gEmote.endAt = ImGui::GetTime() + (index == royale::kChickenDanceEmote ? royale::kChickenDanceSeconds : 3.4);
    gEmotePanelOpen = false;
}

// While an emote plays, the real character is hidden and any movement, attack or a few seconds ends it.
void UpdateEmote(Player* player, const royale::HudState& hud) {
    if (gEmote.id < 0) return;
    const Input& in = gPlayState->state.input[0];
    const bool moved = std::fabs(static_cast<float>(in.cur.stick_x)) > 25.0f || std::fabs(static_cast<float>(in.cur.stick_y)) > 25.0f;
    if (!LiveAndAlive(hud) || !InField() || ImGui::GetTime() > gEmote.endAt || moved || (in.press.button & (BTN_B | BTN_A | BTN_CUP | BTN_DDOWN | BTN_DUP))) {
        StopEmote();
        return;
    }
    player->stateFlags2 |= PLAYER_STATE2_DISABLE_DRAW;
}

// The chicken dance tune (shared/tune.h), played on a small audio device of its own beside the game's. You hear it when you do the dance, or
// when somebody doing it is near: louder the closer they are.
struct ChickenMusic {
    SDL_AudioDeviceID device = 0;
    std::vector<int16_t> cycle;
    size_t pos = 0;
    bool playing = false;
    bool failed = false;
};
ChickenMusic gMusic;

void StopChickenMusic() {
    if (gMusic.device != 0 && gMusic.playing) SDL_ClearQueuedAudio(gMusic.device);
    gMusic.playing = false;
}

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
    volume *= std::clamp(static_cast<float>(CVarGetInteger(CVAR_SETTING("Volume.Master"), 100)) / 100.0f, 0.0f, 1.0f) * 0.7f;
    if (volume < 0.01f || gMusic.failed) { StopChickenMusic(); return; }
    if (gMusic.device == 0) {
        SDL_AudioSpec want = {}, have = {};
        want.freq = royale::kTuneRate; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 1024; want.callback = nullptr;
        gMusic.device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (gMusic.device == 0) { gMusic.failed = true; return; } // no second audio stream available: the dance just stays silent
        SDL_PauseAudioDevice(gMusic.device, 0);
        gMusic.cycle = royale::BuildChickenTune();
    }
    // Keep about a third of a second queued, in quarter-second pieces at the current volume.
    const Uint32 bytesPerSecond = royale::kTuneRate * 2;
    if (SDL_GetQueuedAudioSize(gMusic.device) > bytesPerSecond / 3) return;
    const size_t chunk = royale::kTuneRate / 4;
    std::vector<int16_t> out(chunk);
    for (size_t i = 0; i < chunk; i++) {
        out[i] = static_cast<int16_t>(gMusic.cycle[gMusic.pos] * volume);
        gMusic.pos = (gMusic.pos + 1) % gMusic.cycle.size();
    }
    SDL_QueueAudio(gMusic.device, out.data(), static_cast<Uint32>(out.size() * sizeof(int16_t)));
    gMusic.playing = true;
}

// ---- lobby music from a folder ----------------------------------------------------------------------------------------------
// Put .wav files in the "music" folder inside the game's data folder and they play in a shuffled loop while you wait in the lobby, then fade out when
// the countdown starts. (Only WAV is read: the game has no MP3/OGG decoder to hook into.) The folder is created on first use.
void Say(const std::string& text);
bool HeldGlowOn(bool self) {
    return self ? MapOption("HeldGlowSelf", false) : MapOption("HeldGlow", true);
}

struct LobbyMusic {
    SDL_AudioDeviceID device = 0;
    std::vector<std::filesystem::path> tracks;
    std::vector<int16_t> pcm;   // the current track, converted to the device format (stereo, 44.1 kHz)
    size_t pos = 0;
    size_t next = 0;
    bool scanned = false;
    bool failed = false;
    bool playing = false;
    std::string nowPlaying;
    std::string status;         // shown in the menu: where the folder is, how many songs, and why one could not be read
};
LobbyMusic gLobbyMusic;

std::filesystem::path MusicFolder() { return std::filesystem::path(Ship::Context::GetPathRelativeToAppDirectory("music")); }

void ScanMusicFolder() {
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
}

bool LoadNextTrack() {
    LobbyMusic& m = gLobbyMusic;
    for (size_t tries = 0; tries < m.tracks.size(); tries++) {
        const std::filesystem::path& file = m.tracks[m.next++ % m.tracks.size()];
        SDL_AudioSpec spec = {};
        Uint8* buf = nullptr;
        Uint32 len = 0;
        if (SDL_LoadWAV(file.string().c_str(), &spec, &buf, &len) == nullptr) {
            m.status = "Could not read " + file.filename().string() + ": " + SDL_GetError() + " (use a 16-bit PCM .wav)";
            continue;
        }
        SDL_AudioCVT cvt;
        if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, 2, 44100) < 0) { SDL_FreeWAV(buf); continue; }
        cvt.len = static_cast<int>(len);
        std::vector<Uint8> work(static_cast<size_t>(len) * (cvt.len_mult > 0 ? cvt.len_mult : 1));
        std::memcpy(work.data(), buf, len);
        SDL_FreeWAV(buf);
        cvt.buf = work.data();
        if (cvt.needed && SDL_ConvertAudio(&cvt) < 0) continue;
        const size_t bytes = cvt.needed ? static_cast<size_t>(cvt.len_cvt) : len;
        m.pcm.assign(bytes / 2, 0);
        std::memcpy(m.pcm.data(), work.data(), m.pcm.size() * 2);
        m.pos = 0;
        m.nowPlaying = file.stem().string();
        return !m.pcm.empty();
    }
    return false;
}

// The music folder's songs play in the lobby (if that option is on) and, when "Match music" is set to Random, through the match too.
void UpdateLobbyMusic(bool inLobby, bool inMatchRandom = false) {
    LobbyMusic& m = gLobbyMusic;
    const bool want = ((inLobby && MapOption("LobbyMusic", true)) || inMatchRandom) && !m.failed;
    if (!want) {
        if (m.device != 0 && m.playing) SDL_ClearQueuedAudio(m.device);
        m.playing = false;
        if (!inLobby && !inMatchRandom) m.scanned = false; // pick up newly added songs next time
        return;
    }
    if (!m.scanned) ScanMusicFolder();
    if (m.tracks.empty()) return;
    if (m.device == 0) {
        SDL_AudioSpec want2 = {}, have = {};
        want2.freq = 44100; want2.format = AUDIO_S16SYS; want2.channels = 2; want2.samples = 2048; want2.callback = nullptr;
        m.device = SDL_OpenAudioDevice(nullptr, 0, &want2, &have, 0);
        if (m.device == 0) { m.failed = true; return; }
        SDL_PauseAudioDevice(m.device, 0);
    }
    if (SDL_GetQueuedAudioSize(m.device) > 44100 * 4 / 3) return; // about a third of a second queued is enough
    if (m.pcm.empty() || m.pos >= m.pcm.size()) {
        if (!LoadNextTrack()) { m.failed = true; return; }
        Say((inLobby ? "Lobby music: " : "Now playing: ") + m.nowPlaying);
    }
    const float volume = std::clamp(static_cast<float>(CVarGetInteger(CVAR_SETTING("Volume.Master"), 100)) / 100.0f, 0.0f, 1.0f) * 0.8f;
    const size_t chunk = std::min<size_t>(44100 / 2, m.pcm.size() - m.pos);
    std::vector<int16_t> out(chunk);
    for (size_t i = 0; i < chunk; i++) out[i] = static_cast<int16_t>(m.pcm[m.pos + i] * volume);
    m.pos += chunk;
    SDL_QueueAudio(m.device, out.data(), static_cast<Uint32>(out.size() * sizeof(int16_t)));
    m.playing = true;
}

// What the local player has just done with an item, held for a moment so that everyone sees the pose (potion, ocarina, bow, swing...).
int gActionFrames = 0;
royale::Anim gActionAnim = royale::Anim::Idle;
void StartAction(royale::Anim pose, float seconds) { gActionAnim = pose; gActionFrames = std::max(1, static_cast<int>(seconds * royale::kTickHz)); }
royale::Anim PoseForWeapon(royale::ItemId weapon) {
    const royale::WeaponStats w = royale::WeaponOf(weapon);
    if (!w.ranged) return royale::Anim::Attack;
    const royale::AmmoKind a = royale::AmmoUsedBy(weapon);
    return (a == royale::AmmoKind::Arrows || a == royale::AmmoKind::Seeds) ? royale::Anim::Shoot : royale::Anim::Throw;
}

uint8_t ClassifyAnim(Player* player) {
    if (gEmote.id >= 0) return royale::EmoteAnim(gEmote.id); // others see the gesture
    if (gActionFrames > 0) return static_cast<uint8_t>(gActionAnim);
    if (player->stateFlags1 & PLAYER_STATE1_DEAD) return static_cast<uint8_t>(royale::Anim::Dead);
    float v = std::fabs(player->linearVelocity);
    if (v > 7.5f && (player->actor.bgCheckFlags & 1)) return static_cast<uint8_t>(royale::Anim::Roll);   // a roll is faster than any run
    if (v < 0.5f) return static_cast<uint8_t>(royale::Anim::Idle);
    return static_cast<uint8_t>(v < 4.0f ? royale::Anim::Walk : royale::Anim::Run);
}

uint8_t gLastEpoch = 0;
royale::MatchState gLastState = royale::MatchState::Lobby;
bool gWasJoined = false;
int gLastCountdownShown = -1;
int gAttackCooldown = 0;     // game frames until B may attack again
bool gPendingStart = false;  // the host pressed Start; waiting to be in Hyrule Field, measure the map, then begin
int gInFieldFrames = 0;      // frames spent in the field without a transition, so the scene's collision is ready
bool gSpectating = false;

// While a match is live the server's health replaces the save's. Keep the real values so leaving a match, or the match
// ending, doesn't leave the player's save file with 3 hearts.
bool gHealthOverridden = false;
s16 gSavedCapacity = 0, gSavedHealth = 0;

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
// A burst of sparks round the player and a sound for an item just used.
void UseBurst(Player* player, Color_RGBA8 colour, u16 sfx) {
    SparkBurst(gPlayState, player->actor.world.pos.x, player->actor.world.pos.y + 40.0f, player->actor.world.pos.z, colour, 14, 3.2f);
    Audio_PlaySoundGeneral(sfx, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}
Color_RGBA8 AbilityColour(royale::ItemId id) {
    using royale::ItemId;
    switch (id) {
        case ItemId::DinsFire: case ItemId::BoleroOfFire: return { 255, 110, 40, 255 };
        case ItemId::NayrusLove: case ItemId::SerenadeOfWater: return { 100, 160, 255, 255 };
        case ItemId::FaroresWind: case ItemId::MinuetOfForest: case ItemId::SariasSong: return { 110, 240, 130, 255 };
        case ItemId::LensOfTruth: case ItemId::NocturneOfShadow: return { 190, 110, 255, 255 };
        case ItemId::SunsSong: case ItemId::PreludeOfLight: return { 255, 240, 140, 255 };
        case ItemId::ShockwaveGrenade: case ItemId::SongOfTime: return { 120, 225, 245, 255 };
        default: return { 255, 220, 150, 255 };
    }
}

void ApplyPlatforms(Player* player) {
    if (!InField()) return;
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
    if (!InField() || !gSession.Client()) return;
    const auto& props = gSession.Client()->Props();
    const float px = player->actor.world.pos.x, pz = player->actor.world.pos.z;
    const float py = player->actor.world.pos.y;
    const bool grounded = (player->actor.bgCheckFlags & 1) != 0;
    const float reach = grounded ? 64.0f : 28.0f;
    for (size_t i = 0; i < props.size(); i++) {
        const royale::Prop& p = props[i];
        if (p.kind != royale::PropKind::Rock && p.kind != royale::PropKind::Boulder && p.kind != royale::PropKind::Pillar) continue;
        const float dx = px - p.pos.x, dz = pz - p.pos.z;
        const float radius = royale::PropRadius(p.kind);
        if (std::fabs(dx) > radius + 40.0f || std::fabs(dz) > radius + 40.0f) continue;
        const float d = std::hypot(dx, dz);
        const float base = PlatformBase(i);
        if (base < -1.0e8f) continue;
        const float height = p.kind == royale::PropKind::Rock ? 24.0f : p.kind == royale::PropKind::Boulder ? 60.0f : 200.0f;
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

    if ((in.press.button & BTN_CRIGHT) && gEmote.id < 0) {
        StartEmote(gNextEmote, hud);
        gNextEmote = (gNextEmote + 1) % royale::kEmoteCount;
    }

    if (in.press.button & BTN_DDOWN) {
        if (hud.potions > 0) { gSession.RequestUsePotion(); StartAction(royale::Anim::Drink, 0.9f); UseBurst(player, { 120, 255, 150, 255 }, NA_SE_SY_HP_RECOVER); }
        else Say("No potions");
    }

    // C-Left: drink a shield potion (the bar under your hearts).
    if (in.press.button & BTN_CLEFT) {
        bool has = false;
        for (const auto& pot : hud.inv.potions) has |= royale::PotionOf(static_cast<royale::ItemId>(pot.item)).shield > 0;
        if (!has) Say("No shield potion");
        else if (hud.inv.shield >= royale::kMaxShield - 0.05f) Say("Your shield is full");
        else { gSession.UseShield(); StartAction(royale::Anim::Drink, 0.9f); UseBurst(player, { 120, 190, 255, 255 }, NA_SE_SY_HP_RECOVER); }
    }

    // A: open the chest in front of you, take or swap the item on the ground, hire an ally or talk. Walking over an upgrade picks it up on its own.
    if (in.press.button & BTN_A) {
        const size_t target = NearestLootIndex();
        if (target != kNoLoot) gSession.RequestPickup(static_cast<uint32_t>(target), true);
        else {   // nothing to open or take: maybe somebody to hire
            const int ally = NearbyFreeAlly();
            if (ally >= 0) {
                const royale::AllyDef& def = royale::kAllyDefs[ally];
                if (hud.inv.rupees < def.price) Say(std::string("The ") + def.name + " wants " + std::to_string(def.price) + " rupees (you have " + std::to_string(hud.inv.rupees) + ")");
                else gSession.HireAlly(ally);
            } else if (MayaNear()) {
                TalkToMaya();
            } else if (LiloNear()) {
                TalkToLilo();
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
                        : ab == royale::ItemId::ShockwaveGrenade || ab == royale::ItemId::Hookshot || ab == royale::ItemId::Longshot ? royale::Anim::Throw : royale::Anim::Cast, 0.9f);
            UseBurst(player, AbilityColour(ab), NA_SE_PL_MAGIC_SOUL_NORMAL);
        }
    }

    if (!(in.press.button & BTN_B) || gAttackCooldown > 0) return;
    const royale::AmmoKind ammoKind = royale::AmmoUsedBy(hud.weapon);
    const bool hasAmmo = ammoKind == royale::AmmoKind::None || hud.ammo[static_cast<size_t>(ammoKind)] > 0;
    royale::WeaponStats w = royale::ActiveWeapon(hud.weapon, hasAmmo);   // no ammo: the weapon is only bashed with
    if (w.damage <= 0) return;
    if (!hasAmmo && ammoKind != royale::AmmoKind::None && gAttackCooldown <= 0) Say(std::string("Out of ") + royale::AmmoName(ammoKind) + ": bash them with it, or find more");
    gAttackCooldown = std::max(1, static_cast<int>(std::ceil(w.cooldown * royale::kTickHz)));

    uint16_t best = 0;
    float bestDist = 1e9f;
    for (const auto& [id, actor] : gActorOf) {
        auto st = gState.find(id);
        if (st == gState.end() || !st->second.alive) continue;
        float dx = st->second.x - player->actor.world.pos.x, dz = st->second.z - player->actor.world.pos.z;
        float d = std::sqrt(dx * dx + dz * dz);
        if (d > w.range * 1.05f) continue;
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
            if (d > w.range * 1.05f) continue;
            s16 toTarget = static_cast<s16>(std::atan2(dx, dz) * (32768.0f / 3.14159265f));
            s16 off = static_cast<s16>(toTarget - player->actor.shape.rot.y);
            if (std::abs(static_cast<int>(off)) > 0x2800 && d > 40.0f) continue;
            if (d < bestDist) { bestDist = d; best = static_cast<uint16_t>(bn.Id()); }
        }
    }
    // Show the swing or the shot: face what you are hitting, strike the pose, and loose the arrow or throw the bomb.
    s16 aim = player->actor.shape.rot.y;
    if (bestDist < 1e8f) {
        float tx = 0, tz = 0;
        if (KnownPosition(best, &tx, &tz)) aim = static_cast<s16>(std::atan2(tx - player->actor.world.pos.x, tz - player->actor.world.pos.z) * (32768.0f / 3.14159265f));
        player->actor.shape.rot.y = player->actor.world.rot.y = aim;
    }
    StartAction(hasAmmo ? PoseForWeapon(hud.weapon) : royale::Anim::Attack, 0.45f);
    if (hasAmmo && w.ranged) SpawnProjectileFrom(hud.weapon, player->actor.world.pos.x, player->actor.world.pos.y + 45.0f, player->actor.world.pos.z, aim);
    else Audio_PlaySoundGeneral(NA_SE_IT_SWORD_SWING_HARD, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    if (bestDist < 1e8f) { gSession.ReportAttack(best, true); return; }
    SmashPropInFront(player, w);
}

// The match-start skydive, as in Fortnite: you spawn high above your spawn point, hang there during the countdown, then fall
// during the drop. The stick steers, holding Z dives faster, and the ground ends it (anyone still airborne when the drop ends
// plummets). The server only knows x and z, and protects everyone for the whole drop, so this is all done on the client.
constexpr float kSkyHeight = 3000.0f;   // units above the ground you start
constexpr float kGlideSpeed = 190.0f;   // units per second falling normally (the drop lasts 18 s)
constexpr float kDiveSpeed = 380.0f;    // holding Z
constexpr float kAirSpeed = 130.0f;     // steering speed
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
    gGliderDiving = hud.state == royale::MatchState::Drop && (in.cur.button & BTN_Z);

    float fall = 0.0f;                                                          // hold in the sky during the countdown
    if (hud.state == royale::MatchState::Drop) fall = (in.cur.button & BTN_Z) ? kDiveSpeed : kGlideSpeed;
    else if (hud.state == royale::MatchState::InMatch) fall = 700.0f;            // the drop is over: land now
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

// Boots, Epona's Song and friends: the server allows a faster run and the game applies it by stretching the step Link just took.
// (Teleports and the drop to spawn move far more than a step, so those are left alone.)
royale::Vec2 gLastPos = {};
bool gHaveLastPos = false;
void ApplySpeedBuffs(Player* player, const royale::HudState& hud) {
    const bool active = LiveAndAlive(hud) && InField();
    if (active && gHaveLastPos && std::fabs(hud.speedMult - 1.0f) > 0.01f) {
        const float dx = player->actor.world.pos.x - gLastPos.x, dz = player->actor.world.pos.z - gLastPos.z;
        if (dx * dx + dz * dz < 40.0f * 40.0f) {
            player->actor.world.pos.x += dx * (hud.speedMult - 1.0f);
            player->actor.world.pos.z += dz * (hud.speedMult - 1.0f);
        }
    }
    gLastPos = { player->actor.world.pos.x, player->actor.world.pos.z };
    gHaveLastPos = active;
}

// Link holds what the server says he holds (a sword in the hand, the bow across the back...), the way the puppets do. The save's own B item is left
// pointing at the sword so the game's own swing and sound play when you hit B, and put back when the match is over.
royale::ItemId gLocalWeaponShown = static_cast<royale::ItemId>(255);
bool gLocalLookApplied = false;
u8 gSavedButtonItem0 = ITEM_NONE;
void SyncLocalWeapon(Player* player, const royale::HudState& hud) {
    const bool on = LiveAndAlive(hud) && InField() && !gSkydiving && gEmote.id < 0;
    if (!on) {
        if (gLocalLookApplied && !(gSession.Joined() && IsLive(hud))) { gSaveContext.equips.buttonItems[0] = gSavedButtonItem0; gLocalLookApplied = false; gLocalWeaponShown = static_cast<royale::ItemId>(255); }
        return;
    }
    const Look look = LookFor(hud.weapon);
    if (!gLocalLookApplied) { gSavedButtonItem0 = gSaveContext.equips.buttonItems[0]; gLocalLookApplied = true; gLocalWeaponShown = static_cast<royale::ItemId>(255); }
    if (hud.weapon != gLocalWeaponShown || player->heldItemAction != look.itemAction) {
        gLocalWeaponShown = hud.weapon;
        const bool sword = look.modelGroup == PLAYER_MODELGROUP_SWORD_AND_SHIELD || look.modelGroup == PLAYER_MODELGROUP_BGS;
        gSaveContext.equips.buttonItems[0] = sword ? look.buttonItem : static_cast<u8>(ITEM_NONE);
        u8 original = gSaveContext.equips.buttonItems[0];
        gSaveContext.equips.buttonItems[0] = look.buttonItem;
        player->itemAction = player->heldItemAction = look.itemAction;
        Player_SetModelGroup(player, look.modelGroup);
        gSaveContext.equips.buttonItems[0] = original;
    }
}

void OnPlayerUpdate() {
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
        gSession.SendLocalPose(player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z,
                               player->actor.shape.rot.y, ClassifyAnim(player), static_cast<uint8_t>(gPlayState->sceneNum));
    }

    {   // Adult Power: you grow, and shrink back when it runs out
        const float target = 0.01f * (hud.adultLeft > 0.0f ? royale::kAdultScale : 1.0f);
        const float k = player->actor.scale.x + (target - player->actor.scale.x) * 0.15f;
        player->actor.scale.x = player->actor.scale.y = player->actor.scale.z = k;
    }
    UpdateSkydive(player, hud);
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
    ApplyPlatforms(player);
    ApplyRocks(player);
    HandleCombatInput(player, hud);
    if (hud.state == royale::MatchState::Ending && hud.isHost && (gPlayState->state.input[0].press.button & BTN_A)) gSession.RequestPlayAgain();
    ApplySpeedBuffs(player, hud);

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
        gSaveContext.health = hud.selfAlive ? static_cast<s16>(std::lround(hud.selfHealth * 16.0f)) : gSaveContext.healthCapacity;
    }
    static bool wasDead = false;
    if (dead && !wasDead && InField()) {
        royale::PuppetState me;
        me.x = player->actor.world.pos.x; me.y = player->actor.world.pos.y; me.z = player->actor.world.pos.z;
        me.rot = player->actor.shape.rot.y; me.weapon = hud.weapon; me.tunic = gLocalTunic; me.scene = static_cast<uint8_t>(gPlayState->sceneNum);
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
            player->actor.world.pos.y = t->isBot ? GroundY(gPlayState, t->x, t->z, t->y) : t->y;
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
                    if (haveFrom && haveTo && weapon == royale::ItemId::HomingBombchus) {
                        for (int i = 0; i <= 14; i++) {
                            const float k = i / 14.0f;
                            SparkBurst(gPlayState, fx + (tx2 - fx) * k + (Rand_ZeroOne() - 0.5f) * 30.0f, fy + 25.0f + std::sin(k * 3.14159f) * 40.0f, fz + (tz2 - fz) * k + (Rand_ZeroOne() - 0.5f) * 30.0f, { 200, 100, 255, 255 }, 1, 0.8f);
                        }
                    }
                }
                if (e.id == hud.selfId) {
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
                        Audio_PlaySoundGeneral(NA_SE_IT_SWORD_STRIKE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    } else {
                        // two others fighting nearby: you can see the slash and hear the blow
                        swing(e.other);
                        if (target != gActorOf.end()) {
                            Actor_SetColorFilter(target->second, 0x4000, 0xFF, 0, 8);
                            if (std::hypot(tx - me->actor.world.pos.x, tz - me->actor.world.pos.z) < 1200.0f) soundAt(e.id, NA_SE_IT_SWORD_STRIKE);
                        }
                    }
                }
                break;
            }
            case royale::ClientEvent::Type::LootTaken:
                if (e.id == hud.selfId && gSession.Client() && e.index < gSession.Client()->Loot().size()) {
                    const auto& l = gSession.Client()->Loot()[e.index];
                    const royale::Rarity got = static_cast<royale::Rarity>(l.rarity);
                    const std::string label = LootLabel(l);
                    Say((l.chest ? "Opened a chest: " : "Picked up ") + label);
                    NotePickup(label, got, l.chest);
                    const royale::ItemId itemId = static_cast<royale::ItemId>(l.item);
                    float py = 0;
                    if (!FloorAt(l.x, l.z, &py)) py = GET_PLAYER(gPlayState)->actor.world.pos.y;
                    gPickupFx.push_back({ itemId, got, l.x, py, l.z, ImGui::GetTime() });
                    ShowBanner((l.chest ? "You got: " : "Picked up: ") + label, RarityU32(got), got >= royale::Rarity::Epic ? 3.2f : 2.2f);
                    SparkBurst(gPlayState, l.x, py + 40.0f, l.z, RarityColor(got), 8 + 6 * static_cast<int>(got), 3.5f + 0.8f * static_cast<int>(got));
                    if (l.chest) {
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
                Say(std::string(royale::kBossDefs[kind].name) + " was defeated by " + killer + "! Its chests are on the ground");
                break;
            }
            case royale::ClientEvent::Type::SupplyDrop: {
                ShowBanner("SUPPLY DROP INCOMING!  Marked on your map", IM_COL32(255, 150, 60, 255), 3.6f);
                Say("A supply drop is coming down: a crate of Legendary loot. It is marked on the map");
                gSupplyMarks.push_back({ e.x, e.z, ImGui::GetTime() + 60.0 });
                Audio_PlaySoundGeneral(NA_SE_EV_FIRE_PILLAR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                break;
            }
            case royale::ClientEvent::Type::PropBroken: {
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
                Say(std::string("The ") + name + " has arrived! Ranged weapons reach it in the air; it lands after a dive");
                ShowBanner(std::string(name) + " has arrived!", IM_COL32(255, 120, 80, 255), 4.0f);
                Audio_PlaySoundGeneral(NA_SE_EN_VALVAISA_FIRE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                break;
            }
            case royale::ClientEvent::Type::Strike:
                gStrikeFx.push_back({ e.x, e.z, e.amount, ImGui::GetTime() + std::max(0.3f, e.health), false, e.id == royale::net::kNoPlayer16 });
                break;
            case royale::ClientEvent::Type::AllyChanged: {
                const char* name = royale::kAllyDefs[std::min<int>(e.index, royale::kAllyCount - 1)].name;
                if (e.item == 0) {
                    if (e.id == hud.selfId) { ShowBanner(std::string("The ") + name + " joins you!", IM_COL32(130, 255, 150, 255), 2.8f); Say(std::string("You hired the ") + name + ": they follow you and fight for you"); }
                    else Say(std::string("Somebody hired the ") + name);
                } else if (e.item == 1) {
                    Say(std::string("The ") + name + " is free to hire again");
                } else {
                    Say(std::string("The ") + name + " has fallen");
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
                if (sky == royale::Sky::Fog || sky == royale::Sky::Sandstorm) Say(std::string(royale::SkyName(sky)) + ": bots (and you) see less far");
                if (sky == royale::Sky::Thunder) Say("Thunderstorm: watch the marked circles, lightning is about to strike");
                break;
            }
            case royale::ClientEvent::Type::MapChanged:
                gBrokenProps.clear();   // a new match (or a rematch): all the scenery is back
                gSeasonAnnounced = false;
                gPickupLog.clear();
                break;
            case royale::ClientEvent::Type::AbilityUsed:
                if (e.item == royale::net::kRevivedItem) {
                    Say(e.id == hud.selfId ? "A Fairy saved you!" : nameOf(e.id) + " was revived by a Fairy");
                } else if (e.item < royale::kItemCount) {
                    const std::string what = ItemName(static_cast<royale::ItemId>(e.item));
                    if (e.id == hud.selfId) Say("You used " + what);
                    else if (hud.haveSelf && std::hypot(e.x - hud.selfX, e.z - hud.selfZ) < 700.0f) Say(nameOf(e.id) + " used " + what);
                }
                break;
            case royale::ClientEvent::Type::Eliminated: {
                const bool me = e.id == hud.selfId, mine = e.other == hud.selfId;
                const std::string victim = me ? std::string("You") : nameOf(e.id);
                std::string line;
                if (e.other == royale::net::kNoPlayer16) line = victim + (me ? " were" : " was") + " caught by the storm";
                else if (royale::IsBossId(e.other)) line = victim + (me ? " were" : " was") + " defeated by the " + royale::kBossDefs[gBossKindSeen.count(e.other) ? gBossKindSeen[e.other] : 0].name;
                else line = (mine ? std::string("You") : nameOf(e.other)) + " eliminated " + victim;
                AddFeed(line, me ? IM_COL32(255, 110, 110, 255) : mine ? IM_COL32(255, 220, 90, 255) : IM_COL32(230, 230, 235, 255));
                if (me) {
                    Say("You were eliminated. Spectating until the match ends");
                    ShowBanner("ELIMINATED  -  #" + std::to_string(std::max(1, hud.alive)), IM_COL32(255, 110, 110, 255), 4.0f);
                    gSpectateTarget = kSpectateSelf;
                    for (const auto& st : gSession.Puppets()) if (st.alive && st.id == e.other) gSpectateTarget = st.id;   // watch whoever got you
                } else if (mine) {
                    Say("You eliminated " + victim);
                }
                break;
            }
            case royale::ClientEvent::Type::StateChanged:
                if (e.state == royale::MatchState::Drop) Say("Drop! Skydive to the ground. Z dives faster");
                else if (e.state == royale::MatchState::InMatch) Say("Match started. Stay inside the safe zone! Mini bosses guard the caves and drop Legendary loot");
                else if (e.state == royale::MatchState::Ending) {
                    if (hud.winnerId == hud.selfId) Say("VICTORY ROYALE! You won!");
                    else if (!hud.winnerName.empty()) Say("Match over. Winner: " + hud.winnerName);
                    else Say("Match over");
                }
                break;
            default:
                break;
        }
    }
}

// The host's Start does three things in order: get to Hyrule Field, measure the real playable area and rebuild the world on it
// (loot, spawns and storm on ground that exists), then begin.
void DriveStart(const royale::HudState& hud) {
    if (!gPendingStart) return;
    if (!gSession.Joined() || !hud.isHost || hud.state != royale::MatchState::Lobby) { gPendingStart = false; return; }
    if (!InGame()) return;
    if (!InField()) { WantsWaitingRoom = false; GoToField(); gInFieldFrames = 0; return; }
    if (gPlayState->transitionTrigger != TRANS_TRIGGER_OFF || ++gInFieldFrames < royale::kTickHz) return; // let the scene settle
    royale::Circle measured;
    if (MeasureField(&measured)) {
        gSession.ConfigureMap(measured, WalkableAt);
        Say("Map measured: radius " + std::to_string(static_cast<int>(measured.radius)));
    } else {
        Say("Could not measure the map, using the default size");
    }
    gSession.StartMatch();
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
struct OneShotAudio {
    SDL_AudioDeviceID device = 0;
    bool failed = false;
    std::vector<int16_t> jingle, warning, fart;
};
OneShotAudio gOneShot;

void PlayOneShot(int kind) {   // 0 the storm warning, 1 the storm jingle, 2 Lilo's accident
    if (gOneShot.failed) return;
    if (gOneShot.device == 0) {
        SDL_AudioSpec want = {}, have = {};
        want.freq = royale::kTuneRate; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 1024; want.callback = nullptr;
        gOneShot.device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (gOneShot.device == 0) { gOneShot.failed = true; return; }
        SDL_PauseAudioDevice(gOneShot.device, 0);
        gOneShot.jingle = royale::BuildStormJingle();
        gOneShot.warning = royale::BuildStormWarning();
        gOneShot.fart = royale::BuildFart();
    }
    const float volume = std::clamp(static_cast<float>(CVarGetInteger(CVAR_SETTING("Volume.Master"), 100)) / 100.0f, 0.0f, 1.0f);
    if (volume < 0.01f) return;
    const std::vector<int16_t>& src = kind == 1 ? gOneShot.jingle : kind == 2 ? gOneShot.fart : gOneShot.warning;
    std::vector<int16_t> out(src.size());
    for (size_t i = 0; i < src.size(); i++) out[i] = static_cast<int16_t>(src[i] * volume);
    SDL_ClearQueuedAudio(gOneShot.device);
    SDL_QueueAudio(gOneShot.device, out.data(), static_cast<Uint32>(out.size() * sizeof(int16_t)));
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
            ShowBanner("THE STORM IS CLOSING IN!", IM_COL32(190, 120, 255, 255), 3.4f);
            Say("The storm is closing in: get inside the white circle on the map");
        } else {
            ShowBanner("The storm holds. A new safe zone is marked", IM_COL32(210, 190, 255, 255), 2.8f);
            Say("Zone " + std::to_string(hud.stormPhase + 1) + ": the storm holds for " + std::to_string(static_cast<int>(hud.stormSecondsLeft)) + " seconds");
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
            ShowBanner("The storm closes in 15 seconds", IM_COL32(255, 200, 90, 255), 2.4f);
            PlayOneShot(0);
        } else if (left <= 5 && left > 0 && warned < 5) {
            warned = 5;
            ShowBanner("The storm closes in 5 seconds!", IM_COL32(255, 110, 90, 255), 2.2f);
            PlayOneShot(0);
        }
    }
    if (hud.selfAlive && hud.stormDamagePerSecond > 0 && now >= nextOutsideSiren) { // you are in it: keep nagging
        nextOutsideSiren = now + 4.0;
        PlayOneShot(0);
        Say("You are in the storm! Run for the safe zone");
    }
}

// ---- Maya ---------------------------------------------------------------------------------------------------------------------
// A little Kokiri called Maya stands in a random spot on every map (the same spot for everyone in the match). Walk up and press A to talk to her.
Actor* gMayaActor = nullptr;
royale::Vec2 gMayaPos = {};
bool gMayaKnown = false;
double gMayaTalkStart = -100.0;
constexpr double kMayaTalkSeconds = 5.0;

void Maya_Update(Actor* actor, PlayState* play) {
    Player* pl = GET_PLAYER(play);
    const float dx = pl->actor.world.pos.x - actor->world.pos.x, dz = pl->actor.world.pos.z - actor->world.pos.z;
    if (dx * dx + dz * dz < 700.0f * 700.0f) {   // she turns to look at you as you come close
        const s16 want = static_cast<s16>(std::atan2(dx, dz) * (32768.0f / 3.14159265f));
        actor->shape.rot.y = static_cast<s16>(actor->shape.rot.y + static_cast<s16>(want - actor->shape.rot.y) * 0.12f);
    }
    actor->focus.pos = actor->world.pos;
}
void Maya_Draw(Actor* actor, PlayState* play) {
    const GpuMesh* mesh = GpuMeshFor(royale::MeshKind::Ally, 0);
    if (mesh == nullptr || mesh->dl.empty()) return;
    const float t = static_cast<float>(play->gameplayFrames) / royale::kTickHz;
    const bool talking = ImGui::GetTime() - gMayaTalkStart < kMayaTalkSeconds;
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

void TalkToMaya() {
    gMayaTalkStart = ImGui::GetTime();
    Say(std::string(royale::kMayaName) + ": " + royale::kMayaGreeting);
    if (gPlayState != nullptr && gMayaActor != nullptr)
        SparkBurst(gPlayState, gMayaPos.x, gMayaActor->world.pos.y + 90.0f, gMayaPos.z, { 255, 190, 220, 255 }, 16, 3.0f);
    Audio_PlaySoundGeneral(NA_SE_SY_GET_ITEM, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

// Her name over her head, and what she says in a box at the bottom of the screen, typed out a letter at a time.
void DrawMaya(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale) {
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
    const double since = ImGui::GetTime() - gMayaTalkStart;
    if (since >= 0.0 && since < kMayaTalkSeconds) {
        const std::string full = royale::kMayaGreeting;
        const size_t shown = std::min(full.size(), static_cast<size_t>(since * 28.0));
        const std::string text = full.substr(0, shown);
        const float wrap = ds.x * 0.6f, size = 30.0f * scale;
        const ImVec2 tsz = font->CalcTextSizeA(size, FLT_MAX, wrap, full.c_str());
        const ImVec2 box(ds.x * 0.5f - tsz.x * 0.5f - 24.0f * scale, ds.y * 0.70f - 12.0f * scale);
        const ImVec2 end(box.x + tsz.x + 48.0f * scale, box.y + tsz.y + 60.0f * scale);
        dl->AddRectFilled(box, end, IM_COL32(40, 18, 40, 228), 12.0f * scale);
        dl->AddRect(box, end, IM_COL32(255, 170, 215, 255), 12.0f * scale, 0, 3.0f * scale);
        dl->AddText(font, 19.0f * scale, ImVec2(box.x + 24.0f * scale, box.y + 8.0f * scale), IM_COL32(255, 170, 215, 255), royale::kMayaName);
        dl->AddText(font, size, ImVec2(box.x + 24.0f * scale, box.y + 36.0f * scale), IM_COL32(255, 248, 252, 255), text.c_str(), nullptr, wrap);
    }
}

// ---- the cat, drawn in parts and animated (Lilo on the map, and Lilo as a pet that follows you) ----------------------------------------------------
// A body, a head, four legs and a chain of tail segments (shared/meshes.h), posed from a CatPose. The pose is only ever eased towards a target, so every
// change of mood (walking, sitting, grooming, curling up to sleep) blends instead of snapping.
struct CatPose {
    float bodyPitch = 0, bodyDrop = 0, bodyYaw = 0;       // nose up (radians), how far the body sinks, a wiggle of the hindquarters
    float swing[4] = {0, 0, 0, 0};                        // front left, front right, hind left, hind right: forward is negative
    float legScale[4] = {1, 1, 1, 1};                     // tucked-up legs are short
    float headPitch = 0, headYaw = 0;                     // down (radians), turn
    float tailUp = 1.0f, tailCurl = 0.1f, tailSway = 0;   // the tail's angle from straight up, how much each segment curls, side to side
    int eyes = 0;                                         // CatHead variant: 0 open, 1 shut, 2 mewing, 3 half shut
};
void ApproachPose(CatPose& c, const CatPose& t, float k) {
    auto a = [&](float& x, float y) { x += (y - x) * k; };
    a(c.bodyPitch, t.bodyPitch); a(c.bodyDrop, t.bodyDrop); a(c.bodyYaw, t.bodyYaw);
    for (int i = 0; i < 4; i++) { a(c.swing[i], t.swing[i]); a(c.legScale[i], t.legScale[i]); }
    a(c.headPitch, t.headPitch); a(c.headYaw, t.headYaw); a(c.tailUp, t.tailUp); a(c.tailCurl, t.tailCurl); a(c.tailSway, t.tailSway);
    c.eyes = t.eyes;
}

void DrawCatPart(PlayState* play, royale::MeshKind kind, uint32_t variant) {
    const GpuMesh* m = GpuMeshFor(kind, variant);
    if (m == nullptr || m->dl.empty()) return;
    OPEN_DISPS(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(m->dl.data()));
    CLOSE_DISPS(play->state.gfxCtx);
}

void DrawCat(PlayState* play, float x, float y, float z, float yaw, float scale, const CatPose& p) {
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    CLOSE_DISPS(play->state.gfxCtx);
    constexpr float kLeg = 24.0f;
    Matrix_Translate(x, y, z, MTXMODE_NEW);
    Matrix_RotateY(yaw, MTXMODE_APPLY);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    // legs: front left/right, hind left/right; each swings about its hip and is squashed if tucked
    static const float kHip[4][3] = { {8, 24, 14}, {-8, 24, 14}, {10, 24, -16}, {-10, 24, -16} };
    for (int i = 0; i < 4; i++) {
        Matrix_Push();
        Matrix_Translate(kHip[i][0], kHip[i][1] + p.bodyDrop, kHip[i][2], MTXMODE_APPLY);
        Matrix_RotateX(p.swing[i], MTXMODE_APPLY);
        Matrix_Scale(1.0f, p.legScale[i], 1.0f, MTXMODE_APPLY);
        Matrix_Translate(0, -kLeg, 0, MTXMODE_APPLY);
        DrawCatPart(play, royale::MeshKind::CatLeg, i < 2 ? 0u : 1u);
        Matrix_Pop();
    }
    // body, pitched about the hips (so sitting up lifts the chest and keeps the rump down)
    Matrix_Push();
    Matrix_Translate(0, p.bodyDrop, 0, MTXMODE_APPLY);
    Matrix_Translate(0, 24, -18, MTXMODE_APPLY);
    Matrix_RotateY(p.bodyYaw, MTXMODE_APPLY);
    Matrix_RotateX(-p.bodyPitch, MTXMODE_APPLY);
    Matrix_Translate(0, -24, 18, MTXMODE_APPLY);
    DrawCatPart(play, royale::MeshKind::CatBody, 0);
    // head on its neck (a child of the body, so it is levelled by headPitch)
    Matrix_Push();
    Matrix_Translate(0, 36, 22, MTXMODE_APPLY);
    Matrix_RotateX(p.headPitch - 0.0f, MTXMODE_APPLY);
    Matrix_RotateY(p.headYaw, MTXMODE_APPLY);
    DrawCatPart(play, royale::MeshKind::CatHead, static_cast<uint32_t>(p.eyes));
    Matrix_Pop();
    // the tail: five segments, each bent a little further than the one before, swaying
    Matrix_Push();
    Matrix_Translate(0, 30, -28, MTXMODE_APPLY);
    Matrix_RotateX(-p.tailUp, MTXMODE_APPLY);   // 0 is straight up, about 1.5 is level, pointing back
    Matrix_RotateZ(p.tailSway * 0.4f, MTXMODE_APPLY);
    for (int seg = 0; seg < 5; seg++) {
        DrawCatPart(play, royale::MeshKind::CatTailSeg, seg == 4 ? 2u : static_cast<uint32_t>(seg & 1));
        Matrix_Translate(0, 11.5f, 0, MTXMODE_APPLY);
        Matrix_RotateX(p.tailCurl, MTXMODE_APPLY);
        Matrix_RotateZ(p.tailSway * 0.22f, MTXMODE_APPLY);
    }
    Matrix_Pop();
    Matrix_Pop();
}

// -- Lilo as a pet: follows you (never a bot), purely for looks: no collision, no targeting, nothing the server knows about, and nobody else sees her.
enum class CatMood { Follow, Stand, Sit, Groom, Loaf, Stretch, Pounce, Happy };
struct CatBrain {
    Actor* actor = nullptr;
    CatMood mood = CatMood::Follow;
    float moodT = 0, phase = 0, idle = 0, sitT = 0;
    float x = 0, z = 0, y = 0, yaw = 0, speed = 0;
    float nextAt = 6.0f, blink = 0;
    float leapX = 0, leapZ = 0;
    size_t pickups = 0;
    bool placed = false;
    CatPose pose;
};
CatBrain gCat;

void CatPoof(PlayState* play, float x, float y, float z) {
    for (int i = 0; i < 7; i++) {
        Vec3f pos = { x + (Rand_ZeroOne() - 0.5f) * 30.0f, y + 10.0f + Rand_ZeroOne() * 22.0f, z + (Rand_ZeroOne() - 0.5f) * 30.0f };
        Vec3f vel = { (Rand_ZeroOne() - 0.5f) * 1.6f, 0.6f + Rand_ZeroOne(), (Rand_ZeroOne() - 0.5f) * 1.6f }, accel = { 0, 0, 0 };
        Color_RGBA8 prim = { 255, 245, 210, 255 }, env = { 200, 190, 255, 255 };
        EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 120, 26);
    }
}

void Cat_Update(Actor* actor, PlayState* play) {
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
        c.mood = CatMood::Stand; c.moodT = 0;
    }
    c.moodT += dt;
    if (playerStill) c.idle += dt; else c.idle = 0;
    if (gPickupLog.size() != c.pickups) { if (gPickupLog.size() > c.pickups && c.mood != CatMood::Happy) { c.mood = CatMood::Happy; c.moodT = 0; } c.pickups = gPickupLog.size(); }
    if (gEmote.id >= 0 && c.mood != CatMood::Happy) { c.mood = CatMood::Happy; c.moodT = 0; }

    CatPose t;   // the pose she is easing towards
    t.tailUp = 0.9f; t.tailCurl = 0.07f;
    float blinkNow = 0.0f;
    c.blink -= dt;
    if (c.blink < -3.5f) c.blink = 0.14f;
    blinkNow = c.blink > 0.0f ? 1.0f : 0.0f;
    const float tm = static_cast<float>(ImGui::GetTime());
    bool moving = false;
    float heading = c.yaw;

    switch (c.mood) {
        case CatMood::Follow: case CatMood::Stand: {
            if (d > 58.0f) {   // go to her place: a walk when it is near, a trot when you have run on
                c.speed += (std::clamp((d - 40.0f) * 2.8f, 45.0f, 290.0f) - c.speed) * 0.2f;
                heading = std::atan2(dx, dz);
                c.x += std::sin(heading) * c.speed * dt;
                c.z += std::cos(heading) * c.speed * dt;
                moving = true;
                c.mood = CatMood::Follow;
            } else {
                c.speed *= 0.7f;
                if (c.mood == CatMood::Follow) { c.mood = CatMood::Stand; c.moodT = 0; }
                if (c.idle > 1.6f) { c.mood = CatMood::Sit; c.moodT = 0; c.sitT = 0; c.nextAt = 5.0f + Rand_ZeroOne() * 4.0f; }
            }
            if (c.mood == CatMood::Stand) {   // standing about: looks around, tail swishing slowly
                t.tailSway = std::sin(tm * 1.7f) * 0.5f; t.headYaw = std::sin(tm * 0.6f) * 0.5f;
            }
            break;
        }
        case CatMood::Sit: case CatMood::Groom: {
            c.sitT += dt;
            t.bodyPitch = 0.95f; t.bodyDrop = -9.0f;
            t.swing[0] = t.swing[1] = 0.12f; t.swing[2] = t.swing[3] = -1.5f; t.legScale[2] = t.legScale[3] = 0.7f;
            t.headPitch = -0.7f; t.tailUp = 1.55f; t.tailCurl = 0.34f; t.tailSway = std::sin(tm * 1.2f) * 0.6f;
            t.headYaw = std::sin(tm * 0.45f) * 0.35f;
            if (c.mood == CatMood::Groom) {   // a front paw to the mouth and a lick
                t.swing[0] = -2.1f + std::sin(tm * 11.0f) * 0.12f;
                t.headPitch = -0.1f + std::sin(tm * 11.0f) * 0.06f; t.headYaw = 0.35f; t.eyes = 3;
                if (c.moodT > 3.0f) { c.mood = CatMood::Sit; c.moodT = 0; }
            } else if (c.moodT > c.nextAt) {
                const float r = Rand_ZeroOne();
                c.moodT = 0; c.nextAt = 5.0f + Rand_ZeroOne() * 4.0f;
                if (r < 0.45f) c.mood = CatMood::Groom;
                else if (r < 0.7f) c.mood = CatMood::Stretch;
                else c.mood = CatMood::Pounce;
            }
            if (c.sitT > 16.0f && c.mood == CatMood::Sit) { c.mood = CatMood::Loaf; c.moodT = 0; }
            if (!playerStill || d > 150.0f) { c.mood = CatMood::Follow; c.moodT = 0; }
            break;
        }
        case CatMood::Loaf: {   // curled up asleep: the paws tucked under, head down, eyes shut, little 'z's
            t.bodyDrop = -16.0f; t.bodyPitch = 0.0f;
            for (int i = 0; i < 4; i++) { t.legScale[i] = 0.4f; t.swing[i] = i < 2 ? -0.5f : 0.5f; }
            t.headPitch = 0.4f; t.eyes = 1; t.tailUp = 1.6f; t.tailCurl = 0.5f; t.tailSway = std::sin(tm * 0.8f) * 0.2f;
            t.bodyPitch = std::sin(tm * 1.4f) * 0.015f;   // breathing
            if (static_cast<int>(c.moodT * 10.0f) % 18 == 0 && play->gameplayFrames % 3 == 0) {
                Vec3f pos = { c.x + std::sin(c.yaw) * 20.0f, c.y + 40.0f, c.z + std::cos(c.yaw) * 20.0f }, vel = { 0.15f, 0.5f, 0 }, accel = { 0, 0, 0 };
                Color_RGBA8 prim = { 190, 210, 255, 255 }, env = { 90, 120, 255, 255 };
                EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 60, 40);
            }
            if (!playerStill || d > 150.0f) { c.mood = CatMood::Stretch; c.moodT = 0; }
            break;
        }
        case CatMood::Stretch: {   // front legs forward, chest down, rump up
            t.bodyPitch = -0.42f; t.bodyDrop = -4.0f;
            t.swing[0] = t.swing[1] = -1.0f; t.headPitch = 0.7f; t.tailUp = 0.15f; t.tailCurl = 0.05f; t.eyes = 3;
            t.swing[2] = t.swing[3] = 0.15f;
            if (c.moodT > 2.2f) { c.mood = playerStill ? CatMood::Sit : CatMood::Follow; c.moodT = 0; }
            break;
        }
        case CatMood::Pounce: {   // crouch and wiggle, then a leap at nothing
            if (c.moodT < 0.9f) {
                t.bodyDrop = -12.0f; t.bodyPitch = -0.12f; t.headPitch = 0.3f;
                t.bodyYaw = std::sin(c.moodT * 30.0f) * 0.14f; t.tailUp = 0.9f; t.tailSway = std::sin(c.moodT * 25.0f) * 1.2f;
                for (int i = 0; i < 4; i++) t.swing[i] = i < 2 ? -0.3f : 0.4f;
                heading = c.yaw;
                c.leapX = std::sin(c.yaw) * 75.0f; c.leapZ = std::cos(c.yaw) * 75.0f;
            } else if (c.moodT < 1.35f) {
                const float u = (c.moodT - 0.9f) / 0.45f;
                c.x += c.leapX * dt / 0.45f; c.z += c.leapZ * dt / 0.45f;
                c.y = py;   // (the height is added below)
                t.bodyPitch = 0.3f - u * 0.6f; t.swing[0] = t.swing[1] = -1.1f; t.swing[2] = t.swing[3] = 0.9f; t.tailUp = 0.2f;
            } else { c.mood = CatMood::Stand; c.moodT = 0; }
            break;
        }
        case CatMood::Happy: {   // hops and the tail goes straight up
            t.tailUp = 0.05f; t.tailSway = std::sin(tm * 22.0f) * 0.25f; t.eyes = 1;
            t.bodyDrop = std::fabs(std::sin(c.moodT * 9.0f)) * 9.0f;
            for (int i = 0; i < 4; i++) t.swing[i] = std::sin(c.moodT * 9.0f + i) * 0.2f;
            if (c.moodT > 1.5f) { c.mood = CatMood::Stand; c.moodT = 0; }
            break;
        }
    }
    if (moving) {   // the gait: diagonal pairs of legs, quicker and wider as she speeds up
        c.phase += c.speed * dt * 0.052f;
        const float amp = std::clamp(0.45f + c.speed * 0.0016f, 0.45f, 0.95f), s = std::sin(c.phase);
        t.swing[0] = t.swing[3] = amp * s;
        t.swing[1] = t.swing[2] = -amp * s;
        t.bodyDrop = -std::fabs(std::sin(c.phase)) * 2.2f;
        t.tailUp = c.speed > 160.0f ? 0.6f : 0.95f; t.tailSway = std::sin(c.phase * 0.5f) * 0.4f;
        t.headYaw = 0; t.headPitch = c.speed > 160.0f ? 0.1f : 0.0f;
    }
    // turn to face the way she goes (or, when still, towards you if you are close)
    if (!moving && c.mood != CatMood::Pounce) {
        if (d < 400.0f) heading = std::atan2(px - c.x, pz - c.z);
    }
    {
        float diff = heading - c.yaw;
        while (diff > 3.14159265f) diff -= 6.2831853f;
        while (diff < -3.14159265f) diff += 6.2831853f;
        c.yaw += diff * (moving ? 0.25f : 0.08f);
    }
    if (blinkNow > 0.5f && t.eyes == 0) t.eyes = 1;
    ApproachPose(c.pose, t, moving ? 0.35f : 0.18f);
    c.pose.eyes = t.eyes;

    float floorY = c.y;
    floorY = GroundY(play, c.x, c.z, c.y);
    float hop = 0.0f;
    if (c.mood == CatMood::Pounce && c.moodT >= 0.9f && c.moodT < 1.35f) hop = std::sin((c.moodT - 0.9f) / 0.45f * 3.14159f) * 32.0f;
    if (c.mood == CatMood::Happy) hop = std::fabs(std::sin(c.moodT * 9.0f)) * 10.0f;
    c.y = floorY;
    actor->world.pos.x = c.x; actor->world.pos.z = c.z; actor->world.pos.y = c.y + hop;
    actor->shape.rot.y = static_cast<s16>(c.yaw * (32768.0f / 3.14159265f));
    actor->focus.pos = actor->world.pos;
}

void Cat_Draw(Actor* actor, PlayState* play) {
    DrawCat(play, actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, gCat.yaw, 0.8f, gCat.pose);
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
    a->shape.shadowScale = 22.0f;
    gCat.actor = a;
    gCat.placed = false;
    gCat.pickups = gPickupLog.size();
    gCat.pose = CatPose{};
}

// ---- Lilo -------------------------------------------------------------------------------------------------------------------------
// An Easter egg (switch it off with "Lilo the cat" under Minimap and game options): a grey and white cat called Lilo sits at a random spot on the
// map. Talk to her with A: she says her line, and a moment later there is a noise and a greenish cloud.
Actor* gLiloActor = nullptr;
royale::Vec2 gLiloPos = {};
bool gLiloKnown = false;
double gLiloTalkStart = -100.0;
bool gLiloFarted = true;
double gFartCloudUntil = 0;
constexpr double kLiloTalkSeconds = 5.0, kLiloFartAt = 1.7;

void Lilo_Update(Actor* actor, PlayState* play) {
    Player* pl = GET_PLAYER(play);
    const float dx = pl->actor.world.pos.x - actor->world.pos.x, dz = pl->actor.world.pos.z - actor->world.pos.z;
    if (dx * dx + dz * dz < 600.0f * 600.0f) {   // she watches you come
        const s16 want = static_cast<s16>(std::atan2(dx, dz) * (32768.0f / 3.14159265f));
        actor->shape.rot.y = static_cast<s16>(actor->shape.rot.y + static_cast<s16>(want - actor->shape.rot.y) * 0.1f);
    }
    actor->focus.pos = actor->world.pos;
}
void Lilo_Draw(Actor* actor, PlayState* play) {
    const float t = static_cast<float>(ImGui::GetTime());
    const double since = ImGui::GetTime() - gLiloTalkStart;
    const bool talking = since >= 0.0 && since < kLiloTalkSeconds;
    CatPose p;   // sitting up, watching, tail curled; when "speaking" she mews and sways
    p.bodyPitch = 0.95f; p.bodyDrop = -9.0f;
    p.swing[0] = p.swing[1] = 0.12f; p.swing[2] = p.swing[3] = -1.5f; p.legScale[2] = p.legScale[3] = 0.7f;
    p.headPitch = -0.7f; p.tailUp = 1.55f; p.tailCurl = 0.34f;
    p.tailSway = talking ? std::sin(t * 9.0f) * 1.0f : std::sin(t * 1.3f) * 0.6f;
    p.headYaw = talking ? std::sin(t * 12.0f) * 0.12f : std::sin(t * 0.5f) * 0.3f;
    p.eyes = talking ? ((static_cast<int>(t * 6.0f) & 1) ? 2 : 0) : (std::fmod(t, 4.3f) < 0.13f ? 1 : 0);
    const float lift = talking && since < kLiloFartAt ? 6.0f * std::fabs(std::sin(t * 8.0f)) : 0.0f;
    DrawCat(play, actor->world.pos.x, actor->world.pos.y + lift, actor->world.pos.z, actor->shape.rot.y * (3.14159265f / 32768.0f), 1.0f, p);
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
    a->shape.shadowScale = 28.0f;
    gLiloActor = a;
}

bool LiloNear() {
    if (gLiloActor == nullptr || !InField()) return false;
    Player* pl = GET_PLAYER(gPlayState);
    return std::hypot(pl->actor.world.pos.x - gLiloPos.x, pl->actor.world.pos.z - gLiloPos.z) < royale::kHireRange;
}

void TalkToLilo() {
    gLiloTalkStart = ImGui::GetTime();
    gLiloFarted = false;
    Say(std::string(royale::kLiloName) + ": " + royale::kLiloLine);
    Audio_PlaySoundGeneral(NA_SE_SY_GET_ITEM, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

// The accident: the noise, and a cloud of greenish-brown puffs behind the cat that drifts up and thins out.
void UpdateLiloFx() {
    if (gLiloActor == nullptr || gPlayState == nullptr || !InField()) return;
    const double now = ImGui::GetTime();
    if (!gLiloFarted && now - gLiloTalkStart >= kLiloFartAt) {
        gLiloFarted = true;
        gFartCloudUntil = now + 2.6;
        PlayOneShot(2);
    }
    if (now < gFartCloudUntil) {
        const float yaw = gLiloActor->shape.rot.y * (3.14159265f / 32768.0f);
        const float bx = gLiloPos.x - std::sin(yaw) * 48.0f, bz = gLiloPos.z - std::cos(yaw) * 48.0f;   // behind her
        for (int i = 0; i < 3; i++) {
            Vec3f pos = { bx + (Rand_ZeroOne() - 0.5f) * 34.0f, gLiloActor->world.pos.y + 22.0f + Rand_ZeroOne() * 22.0f, bz + (Rand_ZeroOne() - 0.5f) * 34.0f };
            Vec3f vel = { (Rand_ZeroOne() - 0.5f) * 0.9f, 0.5f + Rand_ZeroOne() * 0.7f, (Rand_ZeroOne() - 0.5f) * 0.9f }, accel = { 0.0f, 0.02f, 0.0f };
            const bool brown = Rand_ZeroOne() < 0.35f;
            Color_RGBA8 prim = brown ? Color_RGBA8{ 150, 130, 60, 255 } : Color_RGBA8{ 150, 200, 70, 255 };
            Color_RGBA8 env = brown ? Color_RGBA8{ 90, 70, 30, 255 } : Color_RGBA8{ 90, 140, 40, 255 };
            EffectSsKiraKira_SpawnDispersed(gPlayState, &pos, &vel, &accel, &prim, &env, 260 + static_cast<int>(Rand_ZeroOne() * 160.0f), 40);
        }
    }
}

// Her name over her head, and what she says in a box at the bottom of the screen, typed out a letter at a time.
void DrawLilo(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale) {
    if (gLiloActor == nullptr || !InField()) return;
    Player* pl = GET_PLAYER(gPlayState);
    const float d = std::hypot(pl->actor.world.pos.x - gLiloPos.x, pl->actor.world.pos.z - gLiloPos.z);
    ImVec2 at;
    if (d < 1800.0f && WorldToScreen(gLiloPos.x, gLiloActor->world.pos.y + 125.0f, gLiloPos.z, &at)) {
        const float size = std::clamp(24.0f * scale * (1800.0f / (d + 900.0f)), 13.0f * scale, 28.0f * scale);
        const char* label = royale::kLiloName;
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label);
        dl->AddText(font, size, ImVec2(at.x - sz.x * 0.5f + 2, at.y + 2), IM_COL32(20, 20, 20, 230), label);
        dl->AddText(font, size, ImVec2(at.x - sz.x * 0.5f, at.y), IM_COL32(235, 235, 230, 255), label);
    }
    const double since = ImGui::GetTime() - gLiloTalkStart;
    if (since >= 0.0 && since < kLiloTalkSeconds) {
        const std::string full = royale::kLiloLine;
        const size_t shown = std::min(full.size(), static_cast<size_t>(since * 22.0));
        const std::string text = full.substr(0, shown);
        const float wrap = ds.x * 0.6f, size = 30.0f * scale;
        const ImVec2 tsz = font->CalcTextSizeA(size, FLT_MAX, wrap, full.c_str());
        const ImVec2 box(ds.x * 0.5f - tsz.x * 0.5f - 24.0f * scale, ds.y * 0.70f - 12.0f * scale);
        const ImVec2 end(box.x + tsz.x + 48.0f * scale, box.y + tsz.y + 60.0f * scale);
        dl->AddRectFilled(box, end, IM_COL32(30, 30, 36, 228), 12.0f * scale);
        dl->AddRect(box, end, IM_COL32(220, 220, 225, 255), 12.0f * scale, 0, 3.0f * scale);
        dl->AddText(font, 19.0f * scale, ImVec2(box.x + 24.0f * scale, box.y + 8.0f * scale), IM_COL32(200, 200, 210, 255), royale::kLiloName);
        dl->AddText(font, size, ImVec2(box.x + 24.0f * scale, box.y + 36.0f * scale), IM_COL32(255, 255, 255, 255), text.c_str(), nullptr, wrap);
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
            GetItem_Draw(play, static_cast<s16>(gid));
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

// ---- Link's cap ---------------------------------------------------------------------------------------------------------------
// The game calls us while it draws the cap's limb (a patch adds the hook, patches/0009): the tail of the cap swings on a spring pushed by the
// air, which is the wind plus how fast Link is moving, running or falling. Everyone's cap does it, the other players' too.
struct HatState {
    royale::HatSpring spring;
    float lx = 0, ly = 0, lz = 0;
    float lvx = 0, lvz = 0, lvy = 0;   // last velocity, to feel the acceleration
    double lastT = 0, seen = 0;
    bool have = false;
};
std::unordered_map<const void*, HatState> gHats;

void OnPlayerHatLimb(void* playerPtr, int16_t* rot) {
    gHatHookCalls++;
    if (gClothScale <= 0.01f || playerPtr == nullptr || !InGame()) return;
    const Player* pl = static_cast<const Player*>(playerPtr);
    const double now = ImGui::GetTime();
    HatState& h = gHats[playerPtr];
    const float x = pl->actor.world.pos.x, y = pl->actor.world.pos.y, z = pl->actor.world.pos.z;
    if (!h.have) { h.lx = x; h.ly = y; h.lz = z; h.lastT = now; h.have = true; }
    h.seen = now;
    const float dt = static_cast<float>(now - h.lastT);
    if (dt > 0.004f) {   // (the limb is drawn more than once a frame sometimes: only step when time has passed)
        const float vx = std::clamp((x - h.lx) / dt, -900.0f, 900.0f), vy = std::clamp((y - h.ly) / dt, -1500.0f, 1500.0f), vz = std::clamp((z - h.lz) / dt, -900.0f, 900.0f);
        h.lx = x; h.ly = y; h.lz = z; h.lastT = now;
        float wx, wz, wind;
        WindNow(&wx, &wz, &wind);
        const float ax = wx - vx, az = wz - vz;
        const float yaw = pl->actor.shape.rot.y * (3.14159265f / 32768.0f), c = std::cos(yaw), sn = std::sin(yaw);
        // The air in Link's frame: x to his left, z in front of him. (Running forward makes air stream back over the cap.)
        // Link's own acceleration (in his frame) kicks the cap the opposite way: it lags behind a start, and swings forward when he stops or lands.
        const float dvx = vx - h.lvx, dvz = vz - h.lvz, dvy = vy - h.lvy;
        h.lvx = vx; h.lvz = vz; h.lvy = vy;
        const float accSide = dvx * c - dvz * sn, accFore = dvx * sn + dvz * c;
        h.spring.Step(std::min(dt, 0.1f), (ax * c - az * sn) * gClothScale, (ax * sn + az * c) * gClothScale, -vy * gClothScale, wind * gClothScale,
                      static_cast<float>(now), static_cast<float>(reinterpret_cast<uintptr_t>(playerPtr) % 61),
                      (accFore * 0.0035f + dvy * 0.0016f) * gClothScale, accSide * 0.0035f * gClothScale);
    }
    const float toBinary = 32768.0f / 3.14159265f;
    gHatLastSwing = h.spring.fore * 57.2958f;
    rot[2] = static_cast<int16_t>(rot[2] + static_cast<int>(h.spring.fore * toBinary));   // fore and aft: the limb's pitch
    rot[1] = static_cast<int16_t>(rot[1] + static_cast<int>(h.spring.side * toBinary));   // sideways: its yaw
}

void ForgetOldHats() {
    const double now = ImGui::GetTime();
    for (auto it = gHats.begin(); it != gHats.end();) it = now - it->second.seen > 4.0 ? gHats.erase(it) : std::next(it);
}

// ---- the sign in the middle of the map -----------------------------------------------------------------------------------------
// A wooden sign stands at the centre of every map (on the nearest bit of open, walkable ground), and reads out its message when you walk up.
Actor* gSignActor = nullptr;
double gSignReadAt = -100.0;
bool gSignRead = false;

void Sign_Update(Actor* actor, PlayState*) { actor->focus.pos = actor->world.pos; }
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

// A label over the sign from a distance, and the message in a box at the bottom of the screen when you stand in front of it.
void DrawSign(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale) {
    if (gSignActor == nullptr || !InField()) return;
    Player* pl = GET_PLAYER(gPlayState);
    const float d = std::hypot(pl->actor.world.pos.x - gSignPos.x, pl->actor.world.pos.z - gSignPos.z);
    ImVec2 at;
    if (d < 1500.0f && WorldToScreen(gSignPos.x, gSignActor->world.pos.y + 240.0f, gSignPos.z, &at)) {
        const char* label = d < 260.0f ? "Sign" : "Sign (walk up to read)";
        const float size = std::clamp(26.0f * scale * (1800.0f / (d + 900.0f)), 14.0f * scale, 30.0f * scale);
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label);
        dl->AddText(font, size, ImVec2(at.x - sz.x * 0.5f + 2, at.y + 2), IM_COL32(20, 12, 4, 230), label);
        dl->AddText(font, size, ImVec2(at.x - sz.x * 0.5f, at.y), IM_COL32(255, 232, 160, 255), label);
    }
    if (d < 260.0f) {
        if (!gSignRead) { gSignRead = true; Say(std::string("The sign reads: ") + royale::kMapSignText); }
        const float wrap = ds.x * 0.62f, size = 27.0f * scale;
        const ImVec2 tsz = font->CalcTextSizeA(size, FLT_MAX, wrap, royale::kMapSignText);
        const ImVec2 box(ds.x * 0.5f - tsz.x * 0.5f - 22.0f * scale, ds.y * 0.70f - 12.0f * scale);
        const ImVec2 end(box.x + tsz.x + 44.0f * scale, box.y + tsz.y + 56.0f * scale);
        dl->AddRectFilled(box, end, IM_COL32(24, 16, 8, 225), 10.0f * scale);
        dl->AddRect(box, end, IM_COL32(222, 178, 100, 255), 10.0f * scale, 0, 3.0f * scale);
        dl->AddText(font, 18.0f * scale, ImVec2(box.x + 22.0f * scale, box.y + 8.0f * scale), IM_COL32(222, 178, 100, 255), "Sign");
        dl->AddText(font, size, ImVec2(box.x + 22.0f * scale, box.y + 34.0f * scale), IM_COL32(255, 246, 224, 255), royale::kMapSignText, nullptr, wrap);
    } else if (d > 420.0f) {
        gSignRead = false;   // walk away and it can be read (and announced) again
    }
}

// ---- match music ----------------------------------------------------------------------------------------------------------------
// "Match music" in the lobby menu: 0 the game's own music as usual, 1 a random song from the music folder (with the game's music turned down),
// 2 silence. The game's music volume is put back to the player's setting whenever a match is not on.
bool gBgmMuted = false;
void SetGameBgmVolume(bool muted) {
    if (muted == gBgmMuted) return;
    gBgmMuted = muted;
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
    gSession.Update(1.0f / royale::kTickHz);
    if (gTravelCooldown > 0) gTravelCooldown--;

    royale::HudState hud = gSession.Hud();
    bool joined = gSession.Joined();
    if (joined) gMapId = royale::ClampMap(hud.mapId);

    if (gHealthOverridden && !(joined && IsLive(hud))) RestoreHealth();
    if (joined && InField() && gPlayState != nullptr) { ApplyPlatforms(GET_PLAYER(gPlayState)); ApplyRocks(GET_PLAYER(gPlayState)); ApplyTrees(GET_PLAYER(gPlayState)); }
    DriveRealWeather();   // also before the player's own update, so it never sees itself as airborne
    ApplyLocalTunic(joined && hud.state != royale::MatchState::Lobby && InField());
    NoticeRoyaleFile();
    SyncPauseInventory(hud);
    UpdateChickenMusic();
    UpdateLobbyMusic(joined && hud.state == royale::MatchState::Lobby, DriveMatchMusic(hud, joined));
    DriveLobbyTimer(hud);
    DriveTimeOfDay(hud);
    UpdateBossWorldFx();
    ReconcileSign(hud);
    ReconcileMaya(hud);
    ReconcileLilo(hud);
    ReconcileCatPet(hud);
    UpdateLiloFx();
    ReconcileAllies(hud);
    { static unsigned frames = 0; if (++frames % 100 == 0) ForgetOldHats(); }
    ReconcileProjectileActor();
    DriveStormAlerts(hud);
    gStateNow = hud.state;
    SealExits(hud);
    DriveMinimapSwitch(joined && IsLive(hud) && InGame() && InField());

    // Just joined a lobby: head for the waiting room if the player wants that.
    if (joined && !gWasJoined) {
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

    DriveStart(hud);
    ReportEvents(hud);
    ReconcilePuppets(hud.state);
    ReconcileLoot(hud);
    ReconcileProps(hud);
    ReconcileBosses(hud);
}

void OnSceneInit(int16_t) {
    gOurTravel = false;
    // Scene change destroys every puppet actor, so forget them all.
    gPuppetOf.clear();
    gActorOf.clear();
    gPlaying.clear();
    gPlate.clear();
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
}

void RegisterRoyaleMod() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(OnGameFrameUpdate);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerUpdate>(OnPlayerUpdate);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>(OnSceneInit);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnZTitleInit>([](void*) { EnsureHudWindow(); });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameStateMainStart>([]() { EnsureHudWindow(); });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerHatLimb>(OnPlayerHatLimb);
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
    bool majorBoss = true;                 // the map's dragon arrives halfway through (host)
    int weatherSeason = royale::kSeasonRandom;   // 0-3 or kSeasonRandom (host)
    int weatherIntensity = 60;             // 0 = no weather (host)
    int weatherChange = 50;                // how often the weather changes (host)
    int weatherDensity = 100;              // particles drawn on this screen, per cent (local)
    int foliage = 100;                     // grass and trees scattered around, per cent (local)
    bool clothOn = true;                   // cloth physics on hats and gliders at all (local)
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
        { "MapChests", "Show chests on the minimap", true },
        { "HeldGlow", "Glow on other players' weapons, coloured by rarity", true },
        { "HeldGlowSelf", "Glow on your own weapon too", false },
        { "LobbyMusic", "Play songs from the music folder in the lobby", true },
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
    if (gLobbyMusic.status.empty()) ScanMusicFolder();
    ImGui::TextWrapped("%s", gLobbyMusic.status.c_str());
    if (ImGui::Button("Rescan music folder")) ScanMusicFolder();
    ImGui::Spacing();
    ImGui::TextColored(kGrey, "Custom dragon model: put dragon.obj (+ dragon.mtl, dragon.cfg) in the 'models' folder next to the 'music' folder.");
    if (!gDragonModel.tried) LoadCustomDragon();
    ImGui::TextWrapped("%s", gDragonModel.status.c_str());
    if (ImGui::Button("Reload custom dragon")) LoadCustomDragon();
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

    ImGui::BeginDisabled(!InGame());

    Heading("Host a lobby");
    ImGui::Text("Port");
    ImGui::InputInt("##royale_port", &ui.port);
    if (ImGui::Button("Host a lobby", ImVec2(220, 0))) {
        ui.port = std::clamp(ui.port, 1024, 65535);
        ui.error.clear();
        SaveUi(ui);
        gSession.ClearLastEnded();
        if (!gSession.Host(static_cast<uint16_t>(ui.port), CleanName(ui.name), &ui.error)) {
            ui.error = "Could not host: " + ui.error;
        } else {
            RefreshLocalAddresses(ui, true);
        }
    }
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
        if (ImGui::Checkbox("A dragon boss arrives halfway through the match", &ui.majorBoss)) {
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
        if (ImGui::Checkbox("Cloth physics (hats and gliders)", &ui.clothOn)) { gClothScale = ui.clothOn ? ui.clothPhysics / 100.0f : 0.0f; SaveUi(ui); }
        if (ui.clothOn) {
            ImGui::SetNextItemWidth(280);
            if (ImGui::SliderInt("Cloth strength (%)", &ui.clothPhysics, 0, 200)) { gClothScale = ui.clothPhysics / 100.0f; SaveUi(ui); }
            ImGui::TextColored(kGrey, "Check: cap asked for %d times, last swing %.1f degrees; cloth glider drawn %d frames", gHatHookCalls, gHatLastSwing, gGliderClothFrames);
        }
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
