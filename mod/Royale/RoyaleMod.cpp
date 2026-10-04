// OOT Royale: connects the game to RoyaleSession (host/join/network) and shows other players as Link puppets.
//
// STATUS: compiled in CI, NOT yet run in the game. Written against Waterdish/Shipwright-Android c9d8f4a (Shipwright 9.0.2)
// plus patches/0001 (ShouldActorInit hook) and patches/0003 (window + menu entry). Copy into the fork with scripts/link_mod.*.
//
// Include order matters: our headers first, because the game's headers define short macro names (MIN, MAX, ABS, ...).
#include "RoyaleMod.h"
#include "RoyaleSession.h"
#include "anim.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/nametag.h"
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

namespace {

royale::RoyaleSession gSession;

// ---- puppets (other players drawn as Link) ---------------------------------------------------------------------------

uint16_t gSpawningPuppet = 0;                              // player id being spawned right now, 0 when none
std::unordered_map<const Actor*, uint16_t> gPuppetOf;      // actor -> player id
std::unordered_map<uint16_t, Actor*> gActorOf;             // player id -> actor
std::unordered_map<uint16_t, royale::PuppetState> gState;  // latest interpolated state per player id
std::unordered_map<const Actor*, const void*> gPlaying;    // animation each puppet is currently playing

bool InHyruleField() {
    return gPlayState != nullptr && GET_PLAYER(gPlayState) != nullptr && gSaveContext.fileNum >= 0 && gSaveContext.fileNum <= 2 &&
           gSaveContext.gameMode == GAMEMODE_NORMAL && gPlayState->sceneNum == royale::kHyruleFieldScene;
}

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

// Make the world match the session: spawn missing puppets, drop ones that left, died, or are out of range.
void ReconcilePuppets() {
    if (!InHyruleField() || !gSession.Joined()) {
        for (auto& [id, actor] : gActorOf) Actor_Kill(actor);
        gState.clear();
        return;
    }
    std::vector<royale::PuppetState> desired = gSession.Puppets();
    std::unordered_map<uint16_t, bool> wanted;
    for (const auto& s : desired) {
        if (!s.alive) continue;
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

void OnPlayerUpdate() {
    if (!gSession.Joined() || !InHyruleField()) return;
    Player* player = GET_PLAYER(gPlayState);
    royale::GameClient* client = gSession.Client();

    // The server moves everyone to spawn points when the match starts. It only knows x and z, so drop from above.
    if (client->Epoch() != gLastEpoch) {
        gLastEpoch = client->Epoch();
        if (const royale::net::PlayerNet* self = client->Self()) {
            player->actor.world.pos.x = self->x;
            player->actor.world.pos.z = self->z;
            player->actor.world.pos.y = GroundY(gPlayState, self->x, self->z, 1500.0f) + 20.0f;
            player->actor.prevPos = player->actor.world.pos;
            player->actor.home.pos = player->actor.world.pos;
        }
    }

    gSession.SendLocalPose(player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z,
                           player->actor.shape.rot.y, ClassifyAnim(player));

    // The server owns health once the match is on. Overwrite the local value every frame so enemies, falls and the
    // game's own damage can't change it, and let a server-side elimination kill Link.
    royale::HudState hud = gSession.Hud();
    bool live = hud.state == royale::MatchState::Drop || hud.state == royale::MatchState::InMatch;
    if (live && hud.haveSelf) {
        gSaveContext.healthCapacity = static_cast<s16>(royale::kMaxHealth * 16);
        gSaveContext.health = hud.selfAlive ? static_cast<s16>(std::lround(hud.selfHealth * 16.0f)) : 0;
    }
}

void OnGameFrameUpdate() {
    // Game logic runs at 20 Hz, the same rate as the server tick, so one call is one step.
    gSession.Update(1.0f / royale::kTickHz);

    royale::MatchState state = gSession.Hud().state;
    if (state != gLastState) {
        if (state == royale::MatchState::Countdown && gSession.Joined()) KillAllEnemies();
        gLastState = state;
    }
    ReconcilePuppets();
}

void RegisterRoyaleMod() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(OnGameFrameUpdate);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerUpdate>(OnPlayerUpdate);

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) {
        // Scene change destroys every puppet actor, so forget them all.
        gPuppetOf.clear();
        gActorOf.clear();
        gPlaying.clear();
        gSpawningPuppet = 0;
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
        });

    // No enemies spawn into a running Royale match.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::ShouldActorInit>([](void* actorRef, bool* should) {
        Actor* actor = (Actor*)actorRef;
        if (gSession.Joined() && actor->category == ACTORCAT_ENEMY) *should = false;
    });
}

static RegisterShipInitFunc royaleInit(RegisterRoyaleMod);

// ---- the OOT Royale window (Enhancements menu -> OOT Royale) --------------------------------------------------------------

class RoyaleWindow : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void InitElement() override {}
    void UpdateElement() override {}

    void DrawElement() override {
        using royale::HudState;
        HudState h = gSession.Hud();
        ImGui::Text("Status: %s", h.status.c_str());

        if (h.mode == HudState::Mode::Idle) {
            DrawStartPanel();
            return;
        }

        if (h.mode == HudState::Mode::Hosting) {
            ImGui::Text("Hosting on UDP port %u (%d player%s)", h.hostPort, h.humanCount, h.humanCount == 1 ? "" : "s");
            ImGui::TextWrapped("Friends join with your IP address and this port. On the same Wi-Fi use your local IP; over the "
                               "internet forward the port or use a VPN such as Tailscale.");
        }
        if (!InHyruleField()) {
            ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "Load a save and stand in Hyrule Field. Players only see each other there.");
        }

        ImGui::Separator();
        const char* stateName[] = { "Lobby", "Countdown", "Drop", "In match", "Ended" };
        ImGui::Text("Match: %s    Alive: %d", stateName[static_cast<int>(h.state)], h.alive);
        if (h.haveSelf) {
            ImGui::Text("Health: %.1f / %.0f hearts%s", h.selfHealth, royale::kMaxHealth, h.selfAlive ? "" : "  (eliminated)");
            ImGui::Text("Safe zone radius: %.0f   Storm damage here: %.1f hearts/s", h.safeZone.radius, h.stormDamagePerSecond);
        }

        ImGui::Separator();
        ImGui::Text("Players in lobby:");
        for (const auto& [id, name] : h.roster) ImGui::BulletText("%s%s", name.c_str(), id == h.selfId ? " (you)" : "");

        if (h.mode == HudState::Mode::Hosting && h.state == royale::MatchState::Lobby) {
            if (ImGui::Button("Start match (bots fill empty slots)")) gSession.StartMatch();
        }
        if (ImGui::Button("Leave")) gSession.Leave();

        DrawDebug();
    }

  private:
    void DrawStartPanel() {
        ImGui::InputText("Your name", name, sizeof(name));
        ImGui::Separator();
        ImGui::Text("Host a match");
        ImGui::InputInt("Port", &port);
        if (ImGui::Button("Host")) {
            std::string err;
            gSession.ClearLastEnded();
            if (!gSession.Host(static_cast<uint16_t>(port), name, &err)) lastError = err;
            else lastError.clear();
        }
        ImGui::Separator();
        ImGui::Text("Join a match");
        ImGui::InputText("Host address", address, sizeof(address));
        if (ImGui::Button("Join")) {
            std::string err;
            gSession.ClearLastEnded();
            if (!gSession.Join(address, static_cast<uint16_t>(port), name, &err)) lastError = err;
            else lastError.clear();
        }
        if (!lastError.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", lastError.c_str());
        DrawDebug();
    }

    // Used to measure the real extent of Hyrule Field for shared/map.h.
    void DrawDebug() {
        ImGui::Separator();
        ImGui::Checkbox("Show Link position (for measuring the map)", &showPosition);
        if (showPosition && gPlayState != nullptr && GET_PLAYER(gPlayState) != nullptr) {
            Player* p = GET_PLAYER(gPlayState);
            ImGui::Text("x=%.0f  y=%.0f  z=%.0f  scene=0x%02X", p->actor.world.pos.x, p->actor.world.pos.y, p->actor.world.pos.z,
                        gPlayState->sceneNum);
        }
    }

    char name[24] = "Link";
    char address[64] = "127.0.0.1";
    int port = royale::net::kDefaultPort;
    std::string lastError;
    bool showPosition = false;
};

} // namespace

void RoyaleMod_RegisterWindow() {
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    gui->AddGuiWindow(std::make_shared<RoyaleWindow>(CVAR_WINDOW("Royale"), "OOT Royale", ImVec2(460, 560)));
}
