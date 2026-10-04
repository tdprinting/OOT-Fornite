// OOT Royale: connects the game to RoyaleSession (host/join/network), shows other players as Link puppets, and provides the
// "Battle Royale" menu with a lobby and a waiting room.
//
// STATUS: compiled in CI, NOT yet run in the game. Written against Waterdish/Shipwright-Android c9d8f4a (Shipwright 9.0.2)
// plus patches/0001 (ShouldActorInit hook). Copy into the fork with scripts/link_mod.*.
//
// Include order matters: our headers first, because the game's headers define short macro names (MIN, MAX, ABS, ...).
#include "RoyaleSession.h"
#include "anim.h"
#include "map.h"
#include "meshes.h"
#include "skins.h"
#include "tune.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <deque>
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
extern PlayState* gPlayState;

void Player_UseItem(PlayState* play, Player* player, s32 item);
void Player_Draw(Actor* actor, PlayState* play);
}

// The waiting room scene id lives in shared/map.h (no game headers there); make sure it still matches the engine.
static_assert(SCENE_TEMPLE_OF_TIME == royale::kWaitingRoomScene, "update kWaitingRoomScene in shared/map.h");
static_assert(SCENE_HYRULE_FIELD == royale::kHyruleFieldScene, "update kHyruleFieldScene in shared/map.h");

namespace SohGui {
extern std::shared_ptr<SohMenu> mSohMenu;
}

// Set by the game's file select (patches/0008) every frame the "Battle Royale" quest option is on screen; our overlay then writes its subtitle.
int gRoyaleQuestLabel = 0;
extern "C" void Royale_ShowQuestLabel(void) {
    gRoyaleQuestLabel = 8;
}

namespace {

royale::RoyaleSession gSession;

// ---- where is the local player? -----------------------------------------------------------------------------------------

bool InGame() {
    return gPlayState != nullptr && GET_PLAYER(gPlayState) != nullptr && gSaveContext.fileNum >= 0 && gSaveContext.fileNum <= 2 &&
           gSaveContext.gameMode == GAMEMODE_NORMAL;
}
bool InField() { return InGame() && gPlayState->sceneNum == SCENE_HYRULE_FIELD; }
bool InWaitingRoom() { return InGame() && gPlayState->sceneNum == SCENE_TEMPLE_OF_TIME; }

const char* SceneName(int scene) {
    static char other[24];
    if (scene == SCENE_HYRULE_FIELD) return "Hyrule Field";
    if (scene == SCENE_TEMPLE_OF_TIME) return "Temple of Time (waiting room)";
    std::snprintf(other, sizeof(other), "scene 0x%02X", scene);
    return other;
}

// Scene travel. Moves the player with the same mechanism the game's own warp console command uses.
int gTravelCooldown = 0; // game frames (20 per second) before another travel request is allowed

bool TravelTo(int entrance) {
    if (!InGame() || gTravelCooldown > 0 || gPlayState->transitionTrigger != TRANS_TRIGGER_OFF) return false;
    gPlayState->nextEntranceIndex = entrance;
    gPlayState->transitionTrigger = TRANS_TRIGGER_START;
    gPlayState->transitionType = TRANS_TYPE_FADE_BLACK;
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK;
    gTravelCooldown = 5 * royale::kTickHz;
    return true;
}
bool GoToWaitingRoom() { return TravelTo(ENTR_TEMPLE_OF_TIME_ENTRANCE); }
bool GoToField() { return TravelTo(ENTR_HYRULE_FIELD_0_1); }

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

struct Rgb { u8 r, g, b; };
constexpr Rgb kRarityRgb[royale::kRarityCount] = { { 190, 190, 190 }, { 80, 220, 100 }, { 80, 150, 255 }, { 190, 100, 255 }, { 255, 200, 60 } };

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
const char* ItemName(royale::ItemId id) {
    return royale::kItems[static_cast<int>(id)].name;
}
std::string ItemLabel(royale::ItemId id, royale::Rarity r) {
    return std::string(ItemName(id)) + " [" + RarityName(r) + "]";
}

// ---- the real map -----------------------------------------------------------------------------------------------------------

// Is there floor under (x, z)? Used to measure Hyrule Field and to keep loot, spawns and storm centres on ground.
bool FloorAt(float x, float z, float* outY = nullptr) {
    if (!InField()) return false;
    CollisionPoly poly;
    Vec3f pos = { x, 4000.0f, z };
    float y = BgCheck_AnyRaycastFloor1(&gPlayState->colCtx, &poly, &pos);
    if (y <= BGCHECK_Y_MIN + 1.0f) return false;
    if (outY) *outY = y;
    return true;
}

float gMedianFloorY = 0;   // heights far from this are cliffs or rooftops, not ground to place things on
bool gMapMeasured = false;
float gMeasuredRadius = 0;

bool WalkableAt(royale::Vec2 p) {
    float y;
    return FloorAt(p.x, p.z, &y) && std::fabs(y - gMedianFloorY) <= 1200.0f;
}

// Probe the floor on a grid across the whole scene and fit a circle around the part that has ground. Takes a few thousand
// raycasts (a few milliseconds), once per match.
bool MeasureField(royale::Circle* out) {
    if (!InField()) return false;
    std::vector<royale::Vec2> points;
    std::vector<float> heights;
    for (float x = -9000.0f; x <= 9000.0f; x += 300.0f) {
        for (float z = -9000.0f; z <= 9000.0f; z += 300.0f) {
            float y;
            if (FloorAt(x, z, &y)) { points.push_back({ x, z }); heights.push_back(y); }
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
    radius = std::clamp(radius, 1500.0f, 8000.0f);
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
        case ItemId::KokiriSword: return { PLAYER_MODELGROUP_SWORD_AND_SHIELD, PLAYER_IA_SWORD_KOKIRI, ITEM_SWORD_KOKIRI };
        case ItemId::MasterSword: return { PLAYER_MODELGROUP_SWORD_AND_SHIELD, PLAYER_IA_SWORD_MASTER, ITEM_SWORD_MASTER };
        case ItemId::BiggoronSword: return { PLAYER_MODELGROUP_BGS, PLAYER_IA_SWORD_BIGGORON, ITEM_SWORD_BGS };
        case ItemId::MegatonHammer: return { PLAYER_MODELGROUP_HAMMER, PLAYER_IA_HAMMER, ITEM_HAMMER };
        case ItemId::FairyBow: return { PLAYER_MODELGROUP_BOW_SLINGSHOT, PLAYER_IA_BOW, ITEM_BOW };
        case ItemId::Slingshot: return { PLAYER_MODELGROUP_BOW_SLINGSHOT, PLAYER_IA_SLINGSHOT, ITEM_SLINGSHOT };
        case ItemId::Boomerang: return { PLAYER_MODELGROUP_BOOMERANG, PLAYER_IA_BOOMERANG, ITEM_BOOMERANG };
        default: return { PLAYER_MODELGROUP_DEFAULT, PLAYER_IA_NONE, ITEM_NONE }; // sticks, bombs, spells: empty-handed for now
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

LinkAnimationHeader* AnimFor(uint8_t anim) {
    switch (static_cast<royale::Anim>(anim)) {
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

    // Hold what the server says this player holds.
    Look look = LookFor(s.weapon);
    if (player->modelGroup != look.modelGroup || player->heldItemAction != look.itemAction) {
        u8 original = gSaveContext.equips.buttonItems[0];
        gSaveContext.equips.buttonItems[0] = look.buttonItem;
        player->itemAction = player->heldItemAction = look.itemAction;
        Player_SetModelGroup(player, look.modelGroup);
        gSaveContext.equips.buttonItems[0] = original;
    }

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

    LinkAnimationHeader* want = AnimFor(s.anim);
    auto playing = gPlaying.find(actor);
    if (playing == gPlaying.end() || playing->second != (const void*)want) {
        LinkAnimation_PlayLoop(play, &player->skelAnime, want);
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

void Puppet_Draw(Actor* actor, PlayState* play) {
    // Player_Draw reads the local player's equipped item to pick the held model, so show the puppet's own.
    const royale::PuppetState* st = StateOf(actor);
    u8 original = gSaveContext.equips.buttonItems[0];
    gSaveContext.equips.buttonItems[0] = st ? LookFor(st->weapon).buttonItem : ITEM_NONE;
    if (st && gTunicApplied) SetTunicCosmetics(st->tunic); // this player's own colour
    Player_Draw(actor, play);
    if (st && gTunicApplied) SetTunicCosmetics(gLocalTunic);
    gSaveContext.equips.buttonItems[0] = original;
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

    c.vy -= 900.0f * dt;
    actor->world.pos.x += c.vel.x * dt;
    actor->world.pos.z += c.vel.z * dt;
    actor->world.pos.y += c.vy * dt;
    const float ground = GroundY(play, actor->world.pos.x, actor->world.pos.z, actor->world.pos.y - 1.0f);
    if (actor->world.pos.y <= ground) {
        actor->world.pos.y = ground;
        if (c.vy < -140.0f) { c.vy = -c.vy * 0.3f; c.rollVel += (c.vel.x > 0 ? 1.0f : -1.0f) * 3000.0f; } // a bounce, and the body flops
        else c.vy = 0;
        c.vel.x *= 0.82f; c.vel.z *= 0.82f;      // sliding on the ground
        c.spin *= 0.85f;
    }
    actor->shape.rot.y = static_cast<s16>(actor->shape.rot.y + static_cast<int>(c.spin * dt * (32768.0f / 3.14159265f)));
    actor->world.rot.y = actor->shape.rot.y;
    // A loose roll that wobbles and settles.
    c.rollVel += -c.roll * 30.0f * dt;
    c.rollVel *= 0.93f;
    c.roll += c.rollVel * dt;
    actor->shape.rot.z = static_cast<s16>(std::clamp(c.roll, -2500.0f, 2500.0f));
    actor->shape.shadowAlpha = 255;

    if (!c.animStarted) {
        LinkAnimation_PlayOnce(play, &player->skelAnime, (LinkAnimationHeader*)&gPlayerAnim_link_normal_back_downA); // knocked flat on the back
        c.animStarted = true;
    }
    LinkAnimation_Update(play, &player->skelAnime);
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
    c.vel = { pushX / len * 190.0f, pushZ / len * 190.0f };   // thrown back by the blow
    c.vy = 260.0f;
    c.spin = (id & 1 ? 1.0f : -1.0f) * 4.5f;
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
    royale::Rarity rarity = royale::Rarity::Common;
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
    la.actor = actor; la.baseY = groundY; la.chest = true; la.opened = l.taken; la.big = big; la.rarity = rarity;
    la.origUpdate = actor->update;
    la.origDestroy = actor->destroy;
    gLoot[index] = la;
    gLootOf[actor] = index;
    actor->update = Chest_Update;
    actor->destroy = Chest_Destroy;
    if (!l.taken) {
        std::string label = std::string(RarityName(rarity)) + " Chest";
        NameTag_RegisterForActorWithOptions(actor, label.c_str(), NameTagOptions{ "royale-loot", static_cast<int16_t>(big ? 50 : 34), RarityColor(rarity) });
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
GpuMesh gGpuMeshes[static_cast<int>(royale::MeshKind::Count)][royale::kMeshVariants];
bool gGpuBuilt[static_cast<int>(royale::MeshKind::Count)][royale::kMeshVariants] = {};

const GpuMesh* GpuMeshFor(royale::MeshKind kind, uint32_t variant) {
    const int k = static_cast<int>(kind);
    variant %= royale::kMeshVariants;
    GpuMesh& m = gGpuMeshes[k][variant];
    if (gGpuBuilt[k][variant]) return &m;
    const royale::MeshData data = royale::BuildMesh(kind, variant);
    if (data.v.empty()) return nullptr;
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
        if (gCulledProps.erase(i) == 0) gBrokenProps.insert(i);
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
        default: return;
    }
    int meshKind = -1;
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
    pa.variant = p.rot >> 4;
    gProps[index] = pa;
    gPropOf[actor] = index;
    actor->destroy = Prop_Destroy;
    if (meshKind >= 0) actor->draw = Prop_DrawCustom; // the game's rock stays as the solid part, unseen; our model is what you see
    switch (p.kind) { // a blob shadow under each, sized to the model
        case royale::PropKind::Rock: actor->shape.shadowScale = 26.0f; break;
        case royale::PropKind::Boulder: actor->shape.shadowScale = 75.0f; break;
        case royale::PropKind::Pillar: actor->shape.shadowScale = 40.0f; break;
        case royale::PropKind::Roof: actor->shape.shadowScale = 0.0f; break;
        default: break;
    }
    if (p.kind == royale::PropKind::Roof) {
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
    bool initialised = false;
};
std::unordered_map<uint32_t, BossActor> gBosses;      // boss id -> its actor
std::unordered_map<const Actor*, uint32_t> gBossOf;
std::unordered_map<uint32_t, int> gBossKindSeen;      // remembered after it is gone, for the messages

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
    actor->world.pos.y = GroundY(play, b.x, b.z, actor->world.pos.y);
    actor->shape.rot.y = b.rot;
    actor->world.rot.y = b.rot;
}

void Boss_Draw(Actor* actor, PlayState* play) {
    auto of = gBossOf.find(actor);
    if (of == gBossOf.end()) return;
    const BossActor& b = gBosses[of->second];
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
                if (!FloorAt(n.x, n.z, &y)) continue;
                Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ISHI, n.x, y, n.z, 0, n.rot, 0, 0, false);
                if (actor == nullptr) continue;
                BossActor b;
                b.actor = actor; b.origDestroy = actor->destroy; b.kind = n.kind;
                b.tx = n.x; b.tz = n.z; b.trot = n.rot; b.rot = n.rot; b.x = n.x; b.z = n.z; b.initialised = true;
                gBosses[id] = b;
                gBossOf[actor] = id;
                actor->update = Boss_Update;
                actor->draw = Boss_Draw;
                actor->destroy = Boss_Destroy;
                actor->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED;
                actor->uncullZoneForward = 5000.0f; actor->uncullZoneScale = 1500.0f; actor->uncullZoneDownward = 1500.0f;
                actor->shape.shadowScale = 70.0f * royale::kBossDefs[n.kind].scale;
            } else {
                BossActor& b = it->second;
                b.tx = n.x; b.tz = n.z; b.trot = n.rot; b.hp = n.hp / 255.0f;
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
        if (d > 3800.0f) continue;
        ImVec2 at;
        if (!WorldToScreen(b.x, b.actor->world.pos.y + 330.0f * royale::kBossDefs[b.kind].scale, b.z, &at)) continue;
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
    for (auto& [i, pa] : gProps) {
        const float dx = i < props.size() ? props[i].pos.x - px : 1e9f, dz = i < props.size() ? props[i].pos.z - pz : 1e9f;
        if (dx * dx + dz * dz > kPropSpawnRadius * kPropSpawnRadius * 1.4f && gCulledProps.insert(i).second) Actor_Kill(pa.actor);
    }
    if (gProps.size() >= kMaxPropActors) return;
    int spawned = 0;
    for (size_t i = 0; i < props.size() && spawned < 6 && gProps.size() < kMaxPropActors; i++) {
        if (gProps.find(i) != gProps.end() || gBrokenProps.count(i)) continue;
        const float dx = props[i].pos.x - px, dz = props[i].pos.z - pz;
        if (dx * dx + dz * dz > kPropSpawnRadius * kPropSpawnRadius) continue;
        float y;
        if (!FloorAt(props[i].pos.x, props[i].pos.z, &y)) continue;
        SpawnProp(i, props[i], y);
        spawned++;
    }
}

void SpawnLoot(size_t index, const royale::net::LootNet& l, float groundY) {
    if (l.chest) { SpawnChest(index, l, groundY); return; } // generated loot is in chests; only dropped items lie on the ground
    royale::Rarity rarity = static_cast<royale::Rarity>(l.rarity);
    gSpawningLoot = true;
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ITEM00, l.x, groundY + 22.0f, l.z, 0, 0, 0, RarityDropType(rarity), false);
    gSpawningLoot = false;
    if (actor == nullptr) return;
    // Keep the game's rupee model and drawing, but replace its behaviour: no vanilla pickup, our own spin and server-checked grab.
    actor->update = Loot_Update;
    actor->destroy = Loot_Destroy;
    gLoot[index] = { actor, groundY, 0 };
    gLoot[index].rarity = rarity;
    gLootOf[actor] = index;
    std::string label = ItemLabel(static_cast<royale::ItemId>(l.item), rarity);
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
            if (known && loot[idx].taken && !la.opened) OpenChest(la);
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

// The storm: a wall of purple rain standing on the edge of the safe zone, a dark rainy tint with lightning when you are in it.
void DrawStorm(ImDrawList* dl, ImVec2 ds, float scale, const royale::HudState& h) {
    if (!InField() || h.safeZone.radius <= 0) return;
    Player* pl = GET_PLAYER(gPlayState);
    const double t = ImGui::GetTime();
    const royale::Circle z = h.safeZone;
    const float baseY = pl->actor.world.pos.y;
    const int kSegments = 120;
    for (int i = 0; i < kSegments; i++) {
        const float a0 = 6.2831853f * i / kSegments, a1 = 6.2831853f * (i + 1) / kSegments;
        const float x0 = z.center.x + std::cos(a0) * z.radius, z0 = z.center.z + std::sin(a0) * z.radius;
        const float x1 = z.center.x + std::cos(a1) * z.radius, z1 = z.center.z + std::sin(a1) * z.radius;
        const float dx = x0 - pl->actor.world.pos.x, dz = z0 - pl->actor.world.pos.z;
        if (dx * dx + dz * dz > 9000.0f * 9000.0f) continue;
        ImVec2 b0, t0, b1, t1;
        if (!WorldToScreen(x0, baseY - 250.0f, z0, &b0) || !WorldToScreen(x0, baseY + 2600.0f, z0, &t0) ||
            !WorldToScreen(x1, baseY - 250.0f, z1, &b1) || !WorldToScreen(x1, baseY + 2600.0f, z1, &t1)) continue;
        const int alpha = static_cast<int>(70 + 28 * std::sin(t * 2.4 + i * 0.55));
        const ImVec2 quad[4] = { b0, b1, t1, t0 };
        dl->AddConvexPolyFilled(quad, 4, IM_COL32(96, 44, 170, alpha));
        if (i % 2 == 0) dl->AddLine(t0, b0, IM_COL32(205, 185, 255, 90), 1.6f * scale); // rain falling in the wall
    }

    if (h.stormDamagePerSecond > 0) {
        dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(34, 10, 64, 105));
        for (int i = 0; i < 110; i++) { // slanting rain
            const float x = std::fmod(i * 97.3f + static_cast<float>(t) * 260.0f, ds.x + 240.0f) - 120.0f;
            const float y = std::fmod(i * 61.7f + static_cast<float>(t) * 950.0f * (0.7f + 0.3f * (i % 3)), ds.y + 80.0f) - 40.0f;
            dl->AddLine(ImVec2(x, y), ImVec2(x - 9 * scale, y + 30 * scale), IM_COL32(205, 195, 255, 120), 1.4f * scale);
        }
        const float cycle = std::fmod(static_cast<float>(t), 7.3f); // lightning every few seconds
        if (cycle < 0.2f) dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(235, 225, 255, static_cast<int>((0.2f - cycle) / 0.2f * 150.0f)));
    }
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

// The item bar, like Fortnite's: weapons (the one in hand highlighted), shield, potions and your ability. D-pad Left cycles weapons;
// on a touch screen you can tap a slot.
void DrawHotbar(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const royale::HudState& h) {
    struct Slot { std::string title, sub; ImU32 border; bool filled, selected; float cooldown; int action; };
    const ImU32 grey = IM_COL32(120, 120, 130, 255);
    std::vector<Slot> slots;
    slots.push_back({ ShortName(h.weapon), RarityName(h.weaponRarity), RarityU32(h.weaponRarity), true, true, 0.0f, 0 });
    for (int i = 0; i < royale::kMaxReserveWeapons; i++) {
        if (i < static_cast<int>(h.inv.reserve.size())) {
            const auto& r = h.inv.reserve[i];
            slots.push_back({ ShortName(static_cast<royale::ItemId>(r.item)), RarityName(static_cast<royale::Rarity>(r.rarity)), RarityU32(static_cast<royale::Rarity>(r.rarity)), true, false, 0.0f, i + 1 });
        } else {
            slots.push_back({ "", "", grey, false, false, 0.0f, 0 });
        }
    }
    if (h.hasShield) slots.push_back({ ShortName(h.shield), RarityName(h.shieldRarity), RarityU32(h.shieldRarity), true, false, 0.0f, 0 });
    else slots.push_back({ "", "Shield", grey, false, false, 0.0f, 0 });
    if (!h.inv.potions.empty()) {
        const auto& p = h.inv.potions.front();
        slots.push_back({ ShortName(static_cast<royale::ItemId>(p.item)), "x" + std::to_string(h.inv.potions.size()), RarityU32(static_cast<royale::Rarity>(p.rarity)), true, false, 0.0f, 10 });
    } else {
        slots.push_back({ "", "Potions", grey, false, false, 0.0f, 10 });
    }
    if (h.inv.hasAbility) {
        const royale::ItemId id = static_cast<royale::ItemId>(h.inv.ability.item);
        const float cd = royale::AbilityOf(id).cooldown;
        slots.push_back({ ShortName(id), h.abilityReadyIn > 0.05f ? ClockText(h.abilityReadyIn) : "READY", RarityU32(static_cast<royale::Rarity>(h.inv.ability.rarity)), true, false,
                          cd > 0 ? std::min(1.0f, h.abilityReadyIn / cd) : 0.0f, 11 });
    } else {
        slots.push_back({ "", "Ability", grey, false, false, 0.0f, 11 });
    }

    const float w = 84.0f * scale, hgt = 60.0f * scale, gap = 8.0f * scale;
    const float total = slots.size() * w + (slots.size() - 1) * gap;
    float x = (ds.x - total) * 0.5f;
    const float y = ds.y - hgt - 20.0f * scale;
    ImGuiIO& io = ImGui::GetIO();
    const bool tap = ImGui::IsMouseClicked(0) && !io.WantCaptureMouse;
    for (size_t i = 0; i < slots.size(); i++) {
        const Slot& sl = slots[i];
        const ImVec2 a(x, y), b(x + w, y + hgt);
        dl->AddRectFilled(a, b, IM_COL32(8, 18, 28, 195), 6.0f * scale);
        if (sl.cooldown > 0) dl->AddRectFilled(a, ImVec2(b.x, a.y + hgt * sl.cooldown), IM_COL32(0, 0, 0, 150), 6.0f * scale);
        dl->AddRect(a, b, sl.selected ? IM_COL32(255, 236, 120, 255) : sl.border, 6.0f * scale, 0, (sl.selected ? 4.0f : 2.5f) * scale);
        const float ts = 13.0f * scale;
        dl->AddText(font, ts, ImVec2(a.x + 6 * scale, a.y + 6 * scale), IM_COL32(255, 255, 255, sl.filled ? 255 : 120), sl.title.c_str());
        dl->AddText(font, ts * 0.92f, ImVec2(a.x + 6 * scale, b.y - 21 * scale), sl.filled ? sl.border : grey, sl.sub.c_str());
        if (tap && io.MousePos.x >= a.x && io.MousePos.x <= b.x && io.MousePos.y >= a.y && io.MousePos.y <= b.y) {
            if (sl.action >= 1 && sl.action <= royale::kMaxReserveWeapons) gSession.SelectWeapon(sl.action);
            else if (sl.action == 10 && !h.inv.potions.empty()) gSession.RequestUsePotion();
            else if (sl.action == 11 && h.inv.hasAbility && h.abilityReadyIn <= 0.05f) gSession.UseAbility();
        }
        x += w + gap;
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
    dl->AddRectFilled(a, b, IM_COL32(8, 18, 28, 200), 6.0f * scale);
    dl->AddRect(a, b, gEmotePanelOpen ? IM_COL32(255, 236, 120, 255) : IM_COL32(190, 190, 200, 255), 6.0f * scale, 0, 2.5f * scale);
    dl->AddText(font, 16.0f * scale, ImVec2(a.x + 12 * scale, a.y + 10 * scale), IM_COL32(255, 255, 255, 255), "EMOTE");
    if (tap && inside(a, b)) gEmotePanelOpen = !gEmotePanelOpen;
    if (!gEmotePanelOpen) return;
    for (int i = 0; i < royale::kEmoteCount; i++) {
        const ImVec2 ea(a.x - 40.0f * scale, a.y - (i + 1) * (hgt + 6.0f * scale)), eb(b.x, ea.y + hgt);
        dl->AddRectFilled(ea, eb, IM_COL32(8, 18, 28, 215), 6.0f * scale);
        dl->AddRect(ea, eb, IM_COL32(120, 200, 255, 255), 6.0f * scale, 0, 2.0f * scale);
        dl->AddText(font, 15.0f * scale, ImVec2(ea.x + 10 * scale, ea.y + 11 * scale), IM_COL32(255, 255, 255, 255), royale::kEmoteNames[i]);
        if (tap && inside(ea, eb)) StartEmote(i, h);
    }
}

// The end-of-match standings.
void DrawResultsPanel(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const royale::HudState& h) {
    if (h.results.empty()) return;
    const float pw = std::min(ds.x * 0.9f, 640.0f * scale), rowH = 26.0f * scale;
    const int shown = std::min<int>(8, static_cast<int>(h.results.size()));
    const float ph = (shown + 4) * rowH + 70.0f * scale;
    const ImVec2 a((ds.x - pw) * 0.5f, ds.y * 0.28f), b(a.x + pw, a.y + ph);
    dl->AddRectFilled(a, b, IM_COL32(8, 16, 26, 225), 10.0f * scale);
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

    float y = ds.y * 0.27f + hh + 28 * scale;
    y += lettered(y, 58 * scale, gold, "OOT ROYALE") + 10 * scale;
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

void DrawOverlay() {
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
        if (!InField()) centered(ds.y * 0.16f + 56 * scale, white, 24 * scale, "Heading to Hyrule Field...");
    }
    if (h.state == royale::MatchState::Ending) {
        centered(ds.y * 0.16f, gold, 46 * scale, h.winnerId == h.selfId && h.winnerId != royale::net::kNoPlayer16 ? "VICTORY ROYALE!" : "MATCH OVER");
        if (!h.winnerName.empty() && h.winnerId != h.selfId) centered(ds.y * 0.16f + 56 * scale, white, 26 * scale, "Winner: " + h.winnerName);
        centered(ds.y * 0.16f + 92 * scale, grey, 20 * scale, "Open the menu and choose Leave to go back");
    }
    if (live && h.haveSelf && !h.selfAlive) centered(ds.y * 0.12f, red, 34 * scale, "ELIMINATED - spectating");
    if (live && h.state == royale::MatchState::Drop) {
        centered(ds.y * 0.2f, green, 30 * scale, gSkydiving ? "SKYDIVE - stick steers, hold Z to dive faster" : "DROP - you are protected for a moment");
    }
    if (h.state == royale::MatchState::Countdown && gSkydiving && !splashing) centered(ds.y * 0.16f + 90 * scale, white, 22 * scale, "You will fall from the sky when the countdown ends");

    if (h.state == royale::MatchState::Ending) DrawResultsPanel(dl, font, ds, scale, h);

    if (!live || !h.haveSelf) return;

    // What is at your feet: chests say how rare they are (not what is inside); items on the ground say what they are.
    if (InField() && h.selfAlive && gSession.Client()) {
        const size_t near = NearestLootIndex();
        const auto& loot = gSession.Client()->Loot();
        if (near != kNoLoot && near < loot.size()) {
            const royale::Rarity r = static_cast<royale::Rarity>(loot[near].rarity);
            if (loot[near].chest) {
                centered(ds.y * 0.66f, RarityU32(r), 26 * scale, std::string(RarityName(r)) + " Chest");
                centered(ds.y * 0.66f + 31 * scale, white, 20 * scale, "A: open");
            } else {
                centered(ds.y * 0.66f, RarityU32(r), 26 * scale, ItemLabel(static_cast<royale::ItemId>(loot[near].item), r));
                centered(ds.y * 0.66f + 31 * scale, white, 20 * scale, "D-pad Right or A: take or swap");
            }
        }
    }
    if (ImGui::GetTime() < gBannerUntil) {
        const float fade = static_cast<float>(std::min(1.0, gBannerUntil - ImGui::GetTime()));
        ImU32 col = RarityU32(gBannerRarity);
        col = (col & 0x00FFFFFF) | (static_cast<ImU32>(255 * fade) << 24);
        centered(ds.y * 0.58f, col, 30 * scale, gBannerText);
    }
    DrawStorm(dl, ds, scale, h);
    DrawBossBars(dl, font, scale);
    DrawPoiLabels(dl, font, ds, scale, h);
    DrawMinimap(dl, ds, scale, h);
    DrawHotbar(dl, font, ds, scale, h);
    DrawEmotes(dl, font, ds, scale, h);

    // Top left: the numbers.
    float x = 16 * scale, y = 14 * scale, line = 24 * scale;
    text(x, y, gold, 24 * scale, "ALIVE " + std::to_string(h.alive) + " / " + std::to_string(h.playerLimit));
    y += line;
    if (h.stormPhase >= royale::kStormPhaseCount) {
        text(x, y, red, 20 * scale, "FINAL ZONE");
    } else {
        std::string phase = "Zone " + std::to_string(h.stormPhase + 1) + "/" + std::to_string(royale::kStormPhaseCount);
        text(x, y, h.stormShrinking ? red : white, 20 * scale,
             phase + (h.stormShrinking ? "  CLOSING " : "  holds ") + ClockText(h.stormSecondsLeft));
    }
    y += line;
    if (h.stormDamagePerSecond > 0) { text(x, y, red, 22 * scale, "IN THE STORM!"); y += line; }
    if (h.selfAlive) {
        text(x, y, RarityU32(h.weaponRarity), 20 * scale, "B  " + ItemLabel(h.weapon, h.weaponRarity));
        y += line;
        if (h.hasShield) text(x, y, RarityU32(h.shieldRarity), 20 * scale, "Shield  " + ItemLabel(h.shield, h.shieldRarity));
        else text(x, y, grey, 20 * scale, "Shield  none");
        y += line;
        text(x, y, h.potions > 0 ? white : grey, 20 * scale, "D-pad Down  Potion x" + std::to_string(h.potions));
        y += line;
        if (h.inv.hasAbility) {
            const royale::Rarity ar = static_cast<royale::Rarity>(h.inv.ability.rarity);
            const std::string name = ItemLabel(static_cast<royale::ItemId>(h.inv.ability.item), ar);
            if (h.abilityReadyIn > 0.05f) text(x, y, grey, 20 * scale, "D-pad Up  " + name + "  " + ClockText(h.abilityReadyIn));
            else text(x, y, RarityU32(ar), 20 * scale, "D-pad Up  " + name + "  READY");
        } else {
            text(x, y, grey, 20 * scale, "D-pad Up  no ability");
        }
        y += line;
        if (h.inv.hasMark) { text(x, y, green, 18 * scale, "Farore's Wind: spot marked, use again to return"); y += line; }
        for (int slot = 0; slot < royale::kGearSlots; slot++) {
            if (!(h.inv.gearMask & (1 << slot))) continue;
            const royale::Rarity gr = static_cast<royale::Rarity>(h.inv.gear[slot].rarity);
            text(x, y, RarityU32(gr), 17 * scale, ItemLabel(static_cast<royale::ItemId>(h.inv.gear[slot].item), gr));
            y += 20 * scale;
        }
        if (h.invulnLeft > 0) { text(x, y, gold, 18 * scale, "INVULNERABLE " + ClockText(h.invulnLeft)); y += line; }
        if (h.speedLeft > 0) { text(x, y, green, 18 * scale, "SPEED UP " + ClockText(h.speedLeft)); y += line; }
        if (h.revealLeft > 0) { text(x, y, green, 18 * scale, "REVEALING " + ClockText(h.revealLeft)); y += line; }
        if (h.burnLeft > 0) { text(x, y, red, 18 * scale, "BURNING"); y += line; }
        if (h.stunLeft > 0) { text(x, y, red, 18 * scale, "STUNNED"); y += line; }
        if (h.shieldLeft > 0) { text(x, y, green, 18 * scale, "DAMAGE REDUCED " + ClockText(h.shieldLeft)); y += line; }
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

uint8_t ClassifyAnim(Player* player) {
    if (gEmote.id >= 0) return royale::EmoteAnim(gEmote.id); // others see the gesture
    if (player->stateFlags1 & PLAYER_STATE1_DEAD) return static_cast<uint8_t>(royale::Anim::Dead);
    float v = std::fabs(player->linearVelocity);
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
int gJumpAssistFrames = 0;   // frames left in which a jump pulls you onto a ledge in front of you     // which backup slot D-pad Left swaps in next

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
        const float ground = GroundY(gPlayState, player->actor.world.pos.x, player->actor.world.pos.z, player->actor.world.pos.y - 1.0f);
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
        if (hud.potions > 0) gSession.RequestUsePotion();
        else Say("No potions");
    }

    // A (or D-pad Right): open the chest in front of you, or take/swap the item on the ground. Walking over an upgrade picks it up on its own.
    if (in.press.button & (BTN_A | BTN_DRIGHT)) {
        const size_t target = NearestLootIndex();
        if (target != kNoLoot) gSession.RequestPickup(static_cast<uint32_t>(target), true);
    }

    // D-pad Left: switch to your next weapon, like cycling the hotbar. With backups B and C, hand A goes to B, then C, then back to A.
    if (in.press.button & BTN_DLEFT) {
        const int spares = static_cast<int>(hud.inv.reserve.size());
        if (spares == 0) {
            Say("No other weapon: open chests to find more");
        } else {
            gNextWeaponSlot = gNextWeaponSlot > spares ? 1 : gNextWeaponSlot;
            gSession.SelectWeapon(gNextWeaponSlot);
            gNextWeaponSlot = gNextWeaponSlot >= spares ? 1 : gNextWeaponSlot + 1;
        }
    }

    if (in.press.button & BTN_DUP) {
        if (!hud.inv.hasAbility) Say("No ability");
        else if (hud.abilityReadyIn > 0.05f) Say("Ability recharging: " + ClockText(hud.abilityReadyIn));
        else gSession.UseAbility();
    }

    if (!(in.press.button & BTN_B) || gAttackCooldown > 0) return;
    royale::WeaponStats w = royale::WeaponOf(hud.weapon);
    if (w.damage <= 0) return;
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
    if (bestDist < 1e8f) gSession.ReportAttack(best, true);
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

    UpdateSkydive(player, hud);
    UpdateEmote(player, hud);
    NoticePoi(player, hud);
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
    wasDead = dead;
    if (dead && gHealthOverridden) {
        player->stateFlags2 |= PLAYER_STATE2_DISABLE_DRAW;
        player->invincibilityTimer = 20;
        gSpectating = true;
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
            return "Bot " + std::to_string(id >= 1000 ? id - 999 : id);
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
            case royale::ClientEvent::Type::Damaged:
                if (e.id == hud.selfId && InGame()) {
                    Player* p = GET_PLAYER(gPlayState);
                    Actor_SetColorFilter(&p->actor, 0x4000, 0xFF, 0, 8); // red flash
                    Player_PlaySfx(&p->actor, NA_SE_VO_LI_DAMAGE_S);
                } else if (e.other == hud.selfId) {
                    auto target = gActorOf.find(e.id);
                    if (target != gActorOf.end()) Actor_SetColorFilter(target->second, 0x4000, 0xFF, 0, 12); // hit marker
                }
                break;
            case royale::ClientEvent::Type::LootTaken:
                if (e.id == hud.selfId && gSession.Client() && e.index < gSession.Client()->Loot().size()) {
                    const auto& l = gSession.Client()->Loot()[e.index];
                    const royale::Rarity got = static_cast<royale::Rarity>(l.rarity);
                    const std::string label = ItemLabel(static_cast<royale::ItemId>(l.item), got);
                    Say((l.chest ? "Opened a chest: " : "Picked up ") + label);
                    NotePickup(label, got, l.chest);
                    Audio_PlaySoundGeneral(NA_SE_SY_GET_ITEM, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                }
                break;
            case royale::ClientEvent::Type::BossDown: {
                const int kind = gBossKindSeen.count(e.id) ? gBossKindSeen[e.id] : 0;
                const std::string killer = e.other == hud.selfId ? std::string("You") : nameOf(e.other);
                Say(std::string(royale::kBossDefs[kind].name) + " was defeated by " + killer + "! Its chests are on the ground");
                break;
            }
            case royale::ClientEvent::Type::MapChanged:
                gBrokenProps.clear();   // a new match (or a rematch): all the scenery is back
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
            case royale::ClientEvent::Type::Eliminated:
                if (e.id == hud.selfId) Say("You were eliminated. Spectating until the match ends");
                else if (e.other == hud.selfId) Say("You eliminated " + nameOf(e.id));
                break;
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
        const u16 t = static_cast<u16>(0x5000 + static_cast<int>(p * 0x9000)); // about 7:30 in the morning to about 9 at night
        gSaveContext.dayTime = t;
        gSaveContext.skyboxTime = t;
    } else if (gTimeTaken) {
        gSaveContext.dayTime = gSavedDayTime;
        gSaveContext.skyboxTime = gSavedDayTime;
        gTimeTaken = false;
    }
}

void OnGameFrameUpdate() {
    EnsureHudWindow();
    // Game logic runs at 20 Hz, the same rate as the server tick, so one call is one step.
    gSession.Update(1.0f / royale::kTickHz);
    if (gTravelCooldown > 0) gTravelCooldown--;

    royale::HudState hud = gSession.Hud();
    bool joined = gSession.Joined();

    if (gHealthOverridden && !(joined && IsLive(hud))) RestoreHealth();
    ApplyLocalTunic(joined && hud.state != royale::MatchState::Lobby && InField());
    NoticeRoyaleFile();
    UpdateChickenMusic();
    DriveLobbyTimer(hud);
    DriveTimeOfDay(hud);

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
    CVarSetInteger(ROYALE_CVAR("SkinColor"), static_cast<int>(SelectedTunic(ui)));
    Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

void RefreshLocalAddresses(UiState& ui, bool force) {
    if (!force && ui.addressAge < 600) { ui.addressAge++; return; }
    ui.localAddresses = royale::net::LocalIPv4Addresses();
    ui.addressAge = 0;
}

const ImVec4 kGold(1.0f, 0.82f, 0.25f, 1.0f);
const ImVec4 kGreen(0.35f, 0.85f, 0.45f, 1.0f);
const ImVec4 kGrey(0.65f, 0.65f, 0.65f, 1.0f);
const ImVec4 kRed(1.0f, 0.4f, 0.4f, 1.0f);

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
    };
    if (!ImGui::CollapsingHeader("Minimap options")) return;
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
        ImGui::BeginDisabled(!InGame() || gPendingStart);
        if (ImGui::Button(gPendingStart ? "Preparing..." : "Start match", ImVec2(220, 0))) gPendingStart = true;
        ImGui::EndDisabled();
        if (gPendingStart) ImGui::TextColored(kGrey, "Heading to Hyrule Field and measuring the map before the countdown...");
        else ImGui::TextColored(kGrey, "Starting takes you to Hyrule Field first, so the real map can be measured.");
        if (readyOthers < others) ImGui::TextColored(kGrey, "Not everyone is ready yet. Starting anyway is allowed.");
    } else {
        if (ImGui::Button(h.selfReady ? "Not ready" : "I'm ready", ImVec2(220, 0))) gSession.SetReady(!h.selfReady);
        ImGui::TextColored(kGrey, "Waiting for the host to start the match...");
    }

    if (h.lobbyLeft >= 0) ImGui::TextColored(kGold, "The match starts by itself in %s", ClockText(h.lobbyLeft).c_str());
    ImGui::Spacing();
    Heading("Where you are");
    int scene = InGame() ? gPlayState->sceneNum : -1;
    ImGui::Text("%s", scene < 0 ? "Not in a game" : SceneName(scene));
    ImGui::TextColored(kGrey, "Players in the same place can see each other. The match itself is in Hyrule Field, and you are taken there automatically.");
    ImGui::BeginDisabled(!InGame());
    if (!InWaitingRoom() && ImGui::Button("Go to the waiting room", ImVec2(220, 0))) WantsWaitingRoom = true;
    if (!InField() && ImGui::Button("Go to Hyrule Field", ImVec2(220, 0))) { WantsWaitingRoom = false; GoToField(); }
    ImGui::EndDisabled();

    ImGui::Spacing();
    if (ImGui::Button("Leave lobby", ImVec2(220, 0))) gSession.Leave();
}

void DrawCountdown(const royale::HudState& h) {
    Heading("MATCH STARTING");
    ImGui::TextColored(kGold, "Drop in %d", static_cast<int>(std::ceil(h.countdownLeft)));
    ImGui::TextWrapped("%s", InField() ? "You are in Hyrule Field. Get ready." : "Heading to Hyrule Field...");
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
    ImGui::TextColored(kGrey, "B: attack    D-pad Left: next weapon    A: open chests and take items    D-pad Down: drink a potion    D-pad Up: use your ability    Walk over items to pick them up");
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
