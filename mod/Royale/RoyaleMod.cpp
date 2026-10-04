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
#include <cmath>
#include <cstdio>
#include <cstring>
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
    // Player_Draw reads the local player's equipped item to pick the held model; draw puppets empty-handed for now.
    u8 original = gSaveContext.equips.buttonItems[0];
    gSaveContext.equips.buttonItems[0] = ITEM_NONE;
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
            player->actor.world.pos.y = GroundY(gPlayState, self->x, self->z, 1500.0f) + 20.0f;
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

    // The server owns health once the match is on. Overwrite the local value every frame so enemies, falls and the
    // game's own damage can't change it, and let a server-side elimination kill Link.
    if (IsLive(hud) && hud.haveSelf) {
        if (!gHealthOverridden) {
            // Remember the player's real values so they can be put back afterwards (see RestoreHealth).
            gSavedCapacity = gSaveContext.healthCapacity;
            gSavedHealth = gSaveContext.health;
            gHealthOverridden = true;
        }
        gSaveContext.healthCapacity = static_cast<s16>(royale::kMaxHealth * 16);
        gSaveContext.health = hud.selfAlive ? static_cast<s16>(std::lround(hud.selfHealth * 16.0f)) : 0;
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
            case royale::ClientEvent::Type::Eliminated:
                if (e.id == hud.selfId) Say("You were eliminated");
                else if (e.other == hud.selfId) Say("You eliminated " + nameOf(e.id));
                break;
            case royale::ClientEvent::Type::StateChanged:
                if (e.state == royale::MatchState::Drop) Say("Drop! Spawn protection for a few seconds");
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

void OnGameFrameUpdate() {
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

    ReportEvents(hud);
    ReconcilePuppets(hud.state);
}

void OnSceneInit(int16_t) {
    // Scene change destroys every puppet actor, so forget them all.
    gPuppetOf.clear();
    gActorOf.clear();
    gPlaying.clear();
    gSpawningPuppet = 0;
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
    }
    return ui;
}

void SaveUi(const UiState& ui) {
    CVarSetString(ROYALE_CVAR("Name"), ui.name);
    CVarSetString(ROYALE_CVAR("Address"), ui.address);
    CVarSetInteger(ROYALE_CVAR("Port"), ui.port);
    CVarSetInteger(ROYALE_CVAR("WaitingRoom"), ui.waitingRoom ? 1 : 0);
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
        ImGui::BeginDisabled(!InGame());
        if (ImGui::Button("Start match", ImVec2(220, 0))) gSession.StartMatch();
        ImGui::EndDisabled();
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
        std::snprintf(label, sizeof(label), "%.1f hearts", h.selfHealth);
        ImGui::ProgressBar(h.selfHealth / royale::kMaxHealth, ImVec2(-1, 0), label);
        if (!h.selfAlive) ImGui::TextColored(kRed, "You have been eliminated.");
        ImGui::Text("Safe zone radius: %.0f", h.safeZone.radius);
        if (h.stormDamagePerSecond > 0) ImGui::TextColored(kRed, "You are in the storm! %.1f hearts per second", h.stormDamagePerSecond);
        else ImGui::TextColored(kGreen, "You are inside the safe zone.");
        ImGui::Text("Potions: %d", h.potions);
    }
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
