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
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/nametag.h"
#include "soh/Notification/Notification.h"
#include "soh/SohGui/SohGui.hpp"
#include "soh/SohGui/SohMenu.h"
#include "soh/cvar_prefixes.h"
#include <imgui.h>
#include <libultraship/libultraship.h>
#include <spdlog/spdlog.h>

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
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

LinkAnimationHeader* AnimFor(uint8_t anim) {
    switch (static_cast<royale::Anim>(anim)) {
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

    auto it = gPuppetOf.find(actor);
    if (it != gPuppetOf.end()) {
        auto st = gState.find(it->second);
        if (st != gState.end()) {
            NameTag_RegisterForActor(actor, st->second.name.c_str());
        }
    }
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

    // Hold what the server says this player holds.
    Look look = LookFor(s.weapon);
    if (player->modelGroup != look.modelGroup || player->heldItemAction != look.itemAction) {
        u8 original = gSaveContext.equips.buttonItems[0];
        gSaveContext.equips.buttonItems[0] = look.buttonItem;
        player->itemAction = player->heldItemAction = look.itemAction;
        Player_SetModelGroup(player, look.modelGroup);
        gSaveContext.equips.buttonItems[0] = original;
    }

    LinkAnimationHeader* want = AnimFor(s.anim);
    auto playing = gPlaying.find(actor);
    if (playing == gPlaying.end() || playing->second != (const void*)want) {
        LinkAnimation_PlayLoop(play, &player->skelAnime, want);
        gPlaying[actor] = (const void*)want;
    }
    LinkAnimation_Update(play, &player->skelAnime);
    // Cancel the animation's own root motion: the network decides where the puppet is.
    Vec3f ignored;
    SkelAnime_UpdateTranslation(&player->skelAnime, &ignored, actor->shape.rot.y);
}

void Puppet_Draw(Actor* actor, PlayState* play) {
    // Player_Draw reads the local player's equipped item to pick the held model, so show the puppet's own.
    const royale::PuppetState* st = StateOf(actor);
    u8 original = gSaveContext.equips.buttonItems[0];
    gSaveContext.equips.buttonItems[0] = st ? LookFor(st->weapon).buttonItem : ITEM_NONE;
    Player_Draw(actor, play);
    gSaveContext.equips.buttonItems[0] = original;
}

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
}

void SpawnPuppet(const royale::PuppetState& s) {
    gSpawningPuppet = s.id;
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_PLAYER, s.x, s.y, s.z, 0, s.rot, 0, 0, false);
    gSpawningPuppet = 0;
    if (actor != nullptr) gActorOf[s.id] = actor;
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
};
std::unordered_map<size_t, LootActor> gLoot;        // loot index -> its actor
std::unordered_map<const Actor*, size_t> gLootOf;   // actor -> loot index
constexpr float kLootSpawnRadius = 1500.0f;          // only draw what is near you
constexpr size_t kMaxLootActors = 48;
constexpr float kLootPickupRange = 55.0f;            // the server allows 75

bool LiveAndAlive(const royale::HudState& h) {
    return gSession.Joined() && (h.state == royale::MatchState::Drop || h.state == royale::MatchState::InMatch) && h.haveSelf && h.selfAlive;
}

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

    Player* player = GET_PLAYER(play);
    royale::HudState h = gSession.Hud();
    if (player == nullptr || la.pickupCooldown > 0 || !LiveAndAlive(h)) return;
    float dx = player->actor.world.pos.x - actor->world.pos.x, dz = player->actor.world.pos.z - actor->world.pos.z;
    float dy = player->actor.world.pos.y - la.baseY;
    if (dx * dx + dz * dz <= kLootPickupRange * kLootPickupRange && std::fabs(dy) < 100.0f) {
        gSession.RequestPickup(static_cast<uint32_t>(idx->second));
        la.pickupCooldown = royale::kTickHz / 2; // ask again in half a second if the server said no
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

void SpawnLoot(size_t index, const royale::net::LootNet& l, float groundY) {
    royale::Rarity rarity = static_cast<royale::Rarity>(l.rarity);
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ITEM00, l.x, groundY + 22.0f, l.z, 0, 0, 0, RarityDropType(rarity), false);
    if (actor == nullptr) return;
    // Keep the game's rupee model and drawing, but replace its behaviour: no vanilla pickup, our own spin and server-checked grab.
    actor->update = Loot_Update;
    actor->destroy = Loot_Destroy;
    gLoot[index] = { actor, groundY, 0 };
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
        size_t idx = it->first;
        bool gone = idx >= loot.size() || loot[idx].taken;
        float dx = loot.size() > idx ? loot[idx].x - px : 1e9f, dz = loot.size() > idx ? loot[idx].z - pz : 1e9f;
        if (gone || dx * dx + dz * dz > kLootSpawnRadius * kLootSpawnRadius * 1.5f) {
            Actor_Kill(it->second.actor);
            gLootOf.erase(it->second.actor);
            it = gLoot.erase(it);
        } else {
            ++it;
        }
    }

    if (gLoot.size() >= kMaxLootActors) return;
    int spawnedThisFrame = 0;
    for (size_t i = 0; i < loot.size() && spawnedThisFrame < 4 && gLoot.size() < kMaxLootActors; i++) {
        if (loot[i].taken || gLoot.find(i) != gLoot.end()) continue;
        float dx = loot[i].x - px, dz = loot[i].z - pz;
        if (dx * dx + dz * dz > kLootSpawnRadius * kLootSpawnRadius) continue;
        float y;
        if (!FloorAt(loot[i].x, loot[i].z, &y)) continue; // nothing to stand on there
        SpawnLoot(i, loot[i], y);
        spawnedThisFrame++;
    }
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


void DrawOverlay() {
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

    if (!live || !h.haveSelf) return;

    // Top left: the numbers.
    float x = 16 * scale, y = 14 * scale, line = 24 * scale;
    text(x, y, gold, 24 * scale, "ALIVE " + std::to_string(h.alive) + " / " + std::to_string(royale::kMaxPlayers));
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

uint8_t ClassifyAnim(Player* player) {
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
void HandleCombatInput(Player* player, const royale::HudState& hud) {
    if (gAttackCooldown > 0) gAttackCooldown--;
    if (!LiveAndAlive(hud) || !InField()) return;
    const Input& in = gPlayState->state.input[0];

    if (in.press.button & BTN_DDOWN) {
        if (hud.potions > 0) gSession.RequestUsePotion();
        else Say("No potions");
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
    HandleCombatInput(player, hud);
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
                    Say("Picked up " + ItemLabel(static_cast<royale::ItemId>(l.item), static_cast<royale::Rarity>(l.rarity)));
                    Audio_PlaySoundGeneral(NA_SE_SY_GET_ITEM, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                }
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
                else if (e.state == royale::MatchState::InMatch) Say("Match started. Stay inside the safe zone!");
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

void OnGameFrameUpdate() {
    EnsureHudWindow();
    // Game logic runs at 20 Hz, the same rate as the server tick, so one call is one step.
    gSession.Update(1.0f / royale::kTickHz);
    if (gTravelCooldown > 0) gTravelCooldown--;

    royale::HudState hud = gSession.Hud();
    bool joined = gSession.Joined();

    if (gHealthOverridden && !(joined && IsLive(hud))) RestoreHealth();

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
}

void OnSceneInit(int16_t) {
    // Scene change destroys every puppet actor, so forget them all.
    gPuppetOf.clear();
    gActorOf.clear();
    gPlaying.clear();
    gSpawningPuppet = 0;
    gLoot.clear();
    gLootOf.clear();
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
        });

    // No enemies spawn while in a lobby or match.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::ShouldActorInit>([](void* actorRef, bool* should) {
        Actor* actor = (Actor*)actorRef;
        if (gSession.Joined() && actor->category == ACTORCAT_ENEMY) *should = false;
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
    bool loaded = false;
    bool showPosition = false;
    std::string error;
    std::vector<std::string> localAddresses;
    int addressAge = 1 << 30; // frames since localAddresses was refreshed
};

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
    }
    return ui;
}

void SaveUi(const UiState& ui) {
    CVarSetString(ROYALE_CVAR("Name"), ui.name);
    CVarSetString(ROYALE_CVAR("Address"), ui.address);
    CVarSetInteger(ROYALE_CVAR("Port"), ui.port);
    CVarSetInteger(ROYALE_CVAR("WaitingRoom"), ui.waitingRoom ? 1 : 0);
    CVarSetInteger(ROYALE_CVAR("BotDifficulty"), ui.botDifficulty);
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

    ImGui::Text("Your name");
    ImGui::InputText("##royale_name", ui.name, sizeof(ui.name));
    ImGui::Checkbox("Wait in the Temple of Time while the lobby fills", &ui.waitingRoom);
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
    ImGui::Text("%d of %d players. The host's Start fills the other %d spots with bots.", h.humanCount, royale::kMaxPlayers, h.botSlots);
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
    ImGui::Text("Players alive: %d / %d", h.alive, royale::kMaxPlayers);
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
    ImGui::TextColored(kGrey, "B: attack    D-pad Down: drink a potion    D-pad Up: use your ability    Walk over items to pick them up");
    ImGui::Spacing();
    if (ImGui::Button("Leave match", ImVec2(220, 0))) gSession.Leave();
}

void DrawResults(const royale::HudState& h) {
    Heading("MATCH OVER");
    if (h.winnerId == h.selfId && h.winnerId != royale::net::kNoPlayer16) ImGui::TextColored(kGold, "VICTORY ROYALE! You won!");
    else if (!h.winnerName.empty()) ImGui::TextColored(kGold, "Winner: %s", h.winnerName.c_str());
    else ImGui::Text("Nobody survived.");
    ImGui::Spacing();
    if (ImGui::Button("Back to the menu", ImVec2(220, 0))) gSession.Leave();
}

void DrawDebug(UiState& ui) {
    if (!ImGui::CollapsingHeader("Developer tools")) return;
    ImGui::Checkbox("Show Link position (for measuring the map)", &ui.showPosition);
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
