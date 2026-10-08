// ---- Battle Royale save files, the lobby timer, and the time of day --------------------------------------------------------------

int gLobbyAnnounced = 1 << 30;

void OpenRoyaleMenu() { WarOpen(); }

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
// What the Sandbox panel shows (the panel's own switches; the server's copies are set by the commands below). Reset whenever there is no sandbox.
struct SandboxUi {
    bool god = true, stormRuns = false, weatherFree = false, botsStill = false;
    int season = 1, sky = 0, intensity = 60;   // the weather to set
    int lastBoss = -1;
    int item = 0, rarity = 4;                  // the item to give
    float day = 0.3f;                          // the time of day, 0 (morning) to 1 (night)
    bool dayRuns = false;
};
SandboxUi gSbx;
void DriveTimeOfDay(const royale::HudState& hud) {
    const bool on = gSession.Joined() && InField() && (hud.state == royale::MatchState::Drop || hud.state == royale::MatchState::InMatch) && gSession.Client();
    if (on) {
        if (!gTimeTaken) { gSavedDayTime = gSaveContext.dayTime; gTimeTaken = true; }
        float total = 0;
        for (const auto& ph : royale::kStormPhases) total += ph.waitSec + ph.closeSec;
        float p = std::clamp(gSession.Client()->StormTime() / std::max(1.0f, total), 0.0f, 1.0f);
        if (gSession.Sandbox()) {   // the test map: the slider decides the hour (and it can run by itself)
            if (gSbx.dayRuns) gSbx.day = std::fmod(gSbx.day + 1.0f / (60.0f * 150.0f), 1.0f);
            p = gSbx.day;
        }
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


// ---- the Sandbox test map ---------------------------------------------------------------------------------------------------------------
// "Sandbox test map" in the Battle Royale menu: a flat test arena (shared/sandbox_terrain.h) with a match that has no countdown, no end and a frozen
// storm. The host's panel (DrawSandboxPanel, under "MATCH IN PROGRESS") has a button for every feature: teleports to the places, carts, bots, bosses,
// any item, the storm, the weather, the time of day and the glider. The server's side is in Match (SandboxBot, SandboxBoss, ...), and everything
// the panel does goes through one queue, run on the game's thread (the menu draws on another).
struct SandboxCmd {
    enum Kind { ArenaClear, Encounter, Go, Cart, ClearCarts, Bot, ClearBots, FreezeBots, Boss, ClearBosses, Give, Heal, Revive, God, StormRuns, StormPhase, Supply, Weather, WeatherFree, Restock, Glide } kind = Go;
    int a = 0, b = 0, c = 0;
};
std::mutex gSandboxLock;
std::vector<SandboxCmd> gSandboxCmds;
void SandboxDo(SandboxCmd::Kind kind, int a = 0, int b = 0, int c = 0) {
    std::lock_guard<std::mutex> lock(gSandboxLock);
    gSandboxCmds.push_back({kind, a, b, c});
}

void RunSandboxCommands(const royale::HudState& hud) {
    Feat("sandbox commands");
    royale::Match* m = gSession.SandboxMatch();
    std::vector<SandboxCmd> cmds;
    { std::lock_guard<std::mutex> lock(gSandboxLock); cmds.swap(gSandboxCmds); }
    if (m == nullptr) { gSbx = SandboxUi(); gSandboxGlide = false; return; }
    if (!hud.haveSelf || !InGame()) return;
    Player* player = GET_PLAYER(gPlayState);
    const uint32_t self = hud.selfId;
    const royale::Vec2 at = { player->actor.world.pos.x, player->actor.world.pos.z };
    const float facing = static_cast<float>(player->actor.shape.rot.y) * (3.14159265f / 32768.0f);
    auto ahead = [&](float d) { return royale::Vec2{ at.x + std::sin(facing) * d, at.z + std::cos(facing) * d }; };
    for (const SandboxCmd& c : cmds) {
        switch (c.kind) {
            case SandboxCmd::ArenaClear: if(m->SandboxClearArena(self)) gStrikeFx.clear(); break;
            case SandboxCmd::Encounter:
                if(m->SandboxBossEncounter(static_cast<royale::BossKind>(c.a),self)) gStrikeFx.clear();
                break;
            case SandboxCmd::Go: m->SandboxTeleport(self, royale::IsBossArena(hud.mapId)?(c.a==1?royale::arena::kArmory:c.a==2?royale::arena::kBoss:royale::arena::kSpawn):royale::SandboxZoneAt(c.a)); break;
            case SandboxCmd::Cart: if (!m->SandboxCart(ahead(360.0f), facing)) Say("No room for another cart: take one away first"); break;
            case SandboxCmd::ClearCarts: m->SandboxClearCarts(); break;
            case SandboxCmd::Bot:
                for (int i = 0; i < c.a; i++) m->SandboxBot(ahead(300.0f + 90.0f * static_cast<float>(i % 5) + 60.0f * static_cast<float>(i / 5)));
                break;
            case SandboxCmd::ClearBots: m->SandboxClearBots(); break;
            case SandboxCmd::FreezeBots: if (royale::BotController* bots = gSession.SandboxBots()) bots->SetFrozen(c.a != 0); break;
            case SandboxCmd::Boss: if (!m->SandboxBoss(static_cast<royale::BossKind>(c.a), ahead(royale::IsMajorKind(static_cast<royale::BossKind>(c.a)) ? 900.0f : 600.0f))) Say("Too many bosses: clear them first"); break;
            case SandboxCmd::ClearBosses: m->SandboxClearBosses(); break;
            case SandboxCmd::Give: m->SandboxGive(self, static_cast<royale::ItemId>(c.a), static_cast<royale::Rarity>(c.b)); break;
            case SandboxCmd::Heal: m->SandboxHeal(self); break;
            case SandboxCmd::Revive: m->SandboxRevive(self); break;
            case SandboxCmd::God: m->SandboxGod(c.a != 0); break;
            case SandboxCmd::StormRuns: m->SandboxStormRuns(c.a != 0); break;
            case SandboxCmd::StormPhase: m->SandboxStormPhase(c.a); break;
            case SandboxCmd::Supply: m->SandboxSupplyDrop(ahead(500.0f)); break;
            case SandboxCmd::Weather: m->SandboxWeather(static_cast<royale::Season>(c.a), static_cast<royale::Sky>(c.b), c.c); break;
            case SandboxCmd::WeatherFree: m->SandboxWeatherFree(c.a != 0); break;
            case SandboxCmd::Restock: Say("Put back " + std::to_string(m->SandboxRestock()) + " things on the plaza"); break;
            case SandboxCmd::Glide:
                if (InField() && !gSkydiving) {   // 1500 up (the safe-landing pull starts below that), then the skydive's own glider takes over
                    gSkydiving = true;
                    gSandboxGlide = true;
                    player->actor.world.pos.y += 2200.0f;
                    player->actor.prevPos = player->actor.world.pos;
                }
                break;
        }
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
// few seconds while you are standing in the storm. The storm sounds are synthesised in shared/tune.h; Lilo's accident is a real recording
// (shared/lilo_sounds.h). All of them play on a small audio stream of their own.
void PlayOneShot(int kind) {   // 0 the storm warning, 1 the storm jingle, 2 Lilo's accident
    static const auto jingle = std::make_shared<const std::vector<int16_t>>(royale::BuildStormJingle());
    static const auto warning = std::make_shared<const std::vector<int16_t>>(royale::BuildStormWarning());
    static const auto fart = std::make_shared<const std::vector<int16_t>>(royale::lilo_snd::kFart.data, royale::lilo_snd::kFart.data + royale::lilo_snd::kFart.count);
    const float volume = GameVolume(false);
    if (volume < 0.01f) return;
    if (kind == 2) StartVoice(kVoiceOneShot, fart, false, royale::lilo_snd::kRate, false, volume);
    else StartVoice(kVoiceOneShot, kind == 1 ? jingle : warning, false, royale::kTuneRate, false, volume);
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
constexpr u16 kTextMayaCompanion = 0x7F60;
constexpr u16 kTextSign = 0x7F20;
constexpr u16 kTextAvriellaPet = 0x7F30;   // Avriella the baby pet: 0x7F30 onwards, one per line in royale::kAvriellaPetLines
static_assert(kTextAvriellaPet + royale::kAvriellaPetLineCount <= 0x7FFF, "Avriella's lines run past the Royale text ids");
static_assert(kTextLiloPet + royale::kLiloPetLineCount <= kTextMaya, "Lilo's pet lines run into Maya's text id");
bool gRoyaleMessagesMade = false;

void RegisterRoyaleMessages() {
    if (gRoyaleMessagesMade || CustomMessageManager::Instance == nullptr) return;
    CustomMessageManager* cm = CustomMessageManager::Instance;
    cm->AddCustomMessageTable("RoyaleMod");
    cm->CreateMessage("RoyaleMod", kTextLilo, CustomMessage(royale::kLiloLine));
    for (int i = 0; i < royale::kLiloPetLineCount; i++)
        cm->CreateMessage("RoyaleMod", static_cast<u16>(kTextLiloPet + i), CustomMessage(royale::kLiloPetLines[i]));
    for (int i = 0; i < royale::kAvriellaPetLineCount; i++)
        cm->CreateMessage("RoyaleMod", static_cast<u16>(kTextAvriellaPet + i), CustomMessage(royale::kAvriellaPetLines[i]));
    for (int i=0; i<royale::kMayaCompanionLineCount; ++i)
        cm->CreateMessage("RoyaleMod", static_cast<u16>(kTextMayaCompanion+i), CustomMessage(royale::kMayaCompanionLines[i]));
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

