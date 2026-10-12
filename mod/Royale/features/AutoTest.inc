// ---- automated playtest driver (tools/playtest) -------------------------------------------------------------------------------
// Off unless the environment variable ROYALE_AUTOTEST holds a script, so a normal launch never runs any of this. tools/playtest/run.py
// starts the PC build headless with a script such as "ingame; host:12; state:InMatch; wait:20; check; quit" and reads back:
//   autotest-result.txt     one line per step (OK/FAIL, game frames, seconds), the mod state block at each check and at a failure
//   autotest-heartbeat.txt  rewritten once a second: frame count and the step running (the runner calls a stale file a hang)
// Steps (separated by ';'; every step but wait/quit gives up after its own timeout and fails the run):
//   ingame                  wait until a save is loaded and Link exists (the War Table boot loads one by itself)
//   wait:<sec>              let the game run (game frames at 20 Hz, so a slow software renderer still runs the same game time)
//   cvar:<name>=<int>       set an integer CVar (full name, e.g. gSettings.Royale.RegularWater=1)
//   skin:<n>                wear player model n (shared/skins.h kSkinModelNames)
//   host:<map>              solo test on map id <map> (shared/map.h; test maps 6, 9, 10 open as the sandbox)
//   hostbots:<map>          host a lobby on <map> and press Start: bots fill every slot, bus, storm, bosses
//   state:<Lobby|Countdown|Drop|InMatch|Ending>   wait until the match reaches that state
//   jump                    press A on the bus until Link is off it (and A again in free fall: the glider)
//   leave                   leave the session (back to the waiting room)
//   land                    off the bus, glider closed, feet on the ground in the field
//   fight                   swing B until the server takes a hit from Link (fails on timeout: nobody in reach)
//   outcome                 wait until Link is out (died) or the match is won by him (stand still and the storm decides)
//   results                 wait until the end screen is up (state Ending)
//   rematch                 host "New match": re-asks until the state is Countdown
//   board                   during the Countdown Link is sat on the bus again
//   fresh                   in Drop after a rematch: alive, no death card or end screen left, Link drawn (else FAIL)
//   regular                 leave any session and play the save as the normal quest (the regular game's water, sky, companions)
//   warp:<scene hex>        travel to the scene's first entrance and wait until it is loaded
//   tour:<overworld|dungeons|all>   warp through each scene of the list in turn, 3 seconds in each
//   check                   assert Link exists in a loaded scene and log the mod state block
//   shot:<name>             ask the runner for a screenshot (<name>.png in the run folder) and wait until it is taken
//   quit                    write PASS and exit with code 0
// Exit codes: 0 pass, 3 a step failed or timed out. A crash or hang is caught by the runner (gdb), not here.

namespace {

struct AutoStep { std::string verb, arg; };
struct AutoTest {
    bool on = false;
    std::vector<AutoStep> steps;
    size_t at = 0;
    unsigned frames = 0, stepFrames = 0, stepStartFrame = 0;
    double stepStartTime = 0, startTime = 0;
    std::string outPath, beatPath;
    std::vector<int> tour;        // scenes left to visit
    int tourScene = -1;
    int tourLoads = 0;            // scenesLoaded when the warp to tourScene started
    int sub = 0;                  // per-step progress
    int scenesLoaded = 0, lastScene = -1;
    int attacksAt = 0;            // gSession.attacksReported when the fight step started
};
AutoTest gAuto;

double AutoNow() { return SDL_GetTicks() / 1000.0; }

void AutoLog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void AutoLog(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    std::ofstream out(gAuto.outPath, std::ios::app);
    if (out) out << buf << "\n";
    std::fprintf(stderr, "[autotest] %s\n", buf);
}

void AutoState(const char* title) {
    char block[kCrashStateLen];
    BuildCrashState(block, sizeof(block), gSession.Hud());
    std::ofstream out(gAuto.outPath, std::ios::app);
    if (out) out << "---- " << title << " ----\n" << block << "---- end ----\n";
}

void AutoFinish(bool pass, const char* why) {
    if (!pass) AutoState("state at failure");
    AutoLog("%s %s (frames %u, %.1f s, scenes loaded %d)", pass ? "PASS" : "FAIL", why, gAuto.frames, AutoNow() - gAuto.startTime,
            gAuto.scenesLoaded);
    std::fflush(nullptr);
    std::_Exit(pass ? 0 : 3);
}

// The first entrance of a scene in the game's table (spawn 0 if it has one).
int AutoEntranceFor(int scene) {
    int any = -1;
    for (int i = 0; i < ENTR_MAX; i++) {
        if (gEntranceTable[i].scene != scene) continue;
        if (gEntranceTable[i].spawn == 0) return i;
        if (any < 0) any = i;
    }
    return any;
}

std::vector<int> AutoTourList(const std::string& which) {
    static const int overworld[] = { SCENE_HYRULE_FIELD, SCENE_KAKARIKO_VILLAGE, SCENE_GRAVEYARD, SCENE_ZORAS_RIVER, SCENE_KOKIRI_FOREST,
        SCENE_SACRED_FOREST_MEADOW, SCENE_LAKE_HYLIA, SCENE_ZORAS_DOMAIN, SCENE_ZORAS_FOUNTAIN, SCENE_GERUDO_VALLEY, SCENE_LOST_WOODS,
        SCENE_DESERT_COLOSSUS, SCENE_GERUDOS_FORTRESS, SCENE_HAUNTED_WASTELAND, SCENE_HYRULE_CASTLE, SCENE_DEATH_MOUNTAIN_TRAIL,
        SCENE_DEATH_MOUNTAIN_CRATER, SCENE_GORON_CITY, SCENE_LON_LON_RANCH, SCENE_TEMPLE_OF_TIME, SCENE_MARKET_DAY };
    static const int dungeons[] = { SCENE_DEKU_TREE, SCENE_DODONGOS_CAVERN, SCENE_JABU_JABU, SCENE_FOREST_TEMPLE, SCENE_FIRE_TEMPLE,
        SCENE_WATER_TEMPLE, SCENE_SPIRIT_TEMPLE, SCENE_SHADOW_TEMPLE, SCENE_BOTTOM_OF_THE_WELL, SCENE_ICE_CAVERN, SCENE_GERUDO_TRAINING_GROUND,
        SCENE_INSIDE_GANONS_CASTLE };
    std::vector<int> list;
    if (which == "overworld" || which == "all") list.insert(list.end(), std::begin(overworld), std::end(overworld));
    if (which == "dungeons" || which == "all") list.insert(list.end(), std::begin(dungeons), std::end(dungeons));
    if (which == "all") for (int s = 0; s < SCENE_ID_MAX; s++) if (std::find(list.begin(), list.end(), s) == list.end()) list.push_back(s);
    return list;
}

int AutoStateOf(const std::string& name) {
    static const char* names[] = { "Lobby", "Countdown", "Drop", "InMatch", "Ending" };
    for (int i = 0; i < 5; i++) if (name == names[i]) return i;
    return -1;
}

// Start a travel and report whether the scene has arrived (Link exists and no transition is running) on later frames.
bool AutoArrived(int scene) {
    return InGame() && gPlayState->sceneNum == scene && gPlayState->transitionTrigger == TRANS_TRIGGER_OFF &&
           gPlayState->transitionMode == TRANS_MODE_OFF;
}

void AutoHost(int map, bool solo) {
    auto& ui = Ui();
    ui.mapId = map;
    gSession.SelectMap(map);
    WarHost(solo, solo && royale::IsTestMap(map), royale::IsTestMap(map) ? map : royale::kSandboxMapIndex);
}

// One step's work this frame: true when it is done.
bool AutoRun(const AutoStep& s, float secs) {
    const royale::HudState hud = gSession.Hud();
    if (s.verb == "ingame") {
        // The title screen's attract demo plays first; after 10 s of it, press Start the way the title logo does (EnMag).
        if (gSaveContext.gameMode == GAMEMODE_TITLE_SCREEN && gPlayState != nullptr && gAuto.stepFrames > 200 &&
            gPlayState->transitionTrigger == TRANS_TRIGGER_OFF) {
            AutoLog("  title: pressing Start");
            gSaveContext.gameMode = GAMEMODE_FILE_SELECT;
            gPlayState->transitionTrigger = TRANS_TRIGGER_START;
            gPlayState->transitionType = TRANS_TYPE_FADE_BLACK;
        }
        if (!InGame() || gPlayState->transitionTrigger != TRANS_TRIGGER_OFF || gAuto.stepFrames <= 20) return false;
        if (WarMenuOpen()) WarHide();   // the War Table opens over the waiting room at boot; a player closes it to walk around
        return true;
    }
    if (s.verb == "wait") return secs >= std::atof(s.arg.c_str());
    if (s.verb == "cvar") {
        const size_t eq = s.arg.find('=');
        if (eq != std::string::npos) CVarSetInteger(s.arg.substr(0, eq).c_str(), std::atoi(s.arg.c_str() + eq + 1));
        Ui().loaded = false; Ui(); RefreshRegularSettings();
        return true;
    }
    if (s.verb == "skin") {
        auto& ui = Ui();
        ui.model = std::clamp(std::atoi(s.arg.c_str()), 0, royale::kSkinModelCount - 1);
        WarSave();
        return true;
    }
    if (s.verb == "host" || s.verb == "hostbots") {
        const bool solo = s.verb == "host";
        if (gAuto.sub == 0) { if (!InGame()) return false; AutoHost(std::atoi(s.arg.c_str()), solo); gAuto.sub = 1; return false; }
        if (!gSession.Joined() || !hud.haveSelf) return false;
        if (!solo && hud.isHost && hud.state == royale::MatchState::Lobby && !gPendingStart) gPendingStart = true;   // press Start
        return hud.state != royale::MatchState::Lobby || (solo && royale::IsTestMap(hud.mapId) && InField());
    }
    if (s.verb == "state") return gSession.Joined() && static_cast<int>(hud.state) >= AutoStateOf(s.arg);
    if (s.verb == "jump") {
        if (gAuto.stepFrames % 10 == 0) gBusJumpPressed = true;
        return !hud.aboard && hud.state != royale::MatchState::Countdown && (gAuto.stepFrames > 40 || !gSkydiving);
    }
    if (s.verb == "leave") { gSession.Leave(); return true; }
    if (s.verb == "land") {
        if (!InGame() || gAuto.stepFrames <= 40 || !gSession.Joined() || hud.aboard || gSkydiving || !InField()) return false;
        return (GET_PLAYER(gPlayState)->actor.bgCheckFlags & 1) != 0;
    }
    if (s.verb == "fight") {
        if (gAuto.sub == 0) { gAuto.sub = 1; gAuto.attacksAt = gSession.attacksReported; }
        gAutoPressB = gAuto.stepFrames % 15 == 0;   // a B press every 15 frames: the sword, or the bow when that is what Link holds
        if (gSession.attacksReported > gAuto.attacksAt) {
            gAutoPressB = false;
            AutoLog("  fight: %d swing(s) reported to the server", gSession.attacksReported - gAuto.attacksAt);
            return true;
        }
        return false;
    }
    if (s.verb == "outcome") {
        if (!gSession.Joined() || !hud.haveSelf) return false;
        if (!hud.selfAlive) { AutoLog("  outcome: died with %d alive", hud.alive); return true; }
        if (hud.state == royale::MatchState::Ending && hud.winnerId == hud.selfId) { AutoLog("  outcome: won"); return true; }
        return false;
    }
    if (s.verb == "results") return hud.state == royale::MatchState::Ending && EndScreenUp(hud);
    if (s.verb == "rematch") {
        if (hud.state == royale::MatchState::Ending && hud.isHost && gAuto.stepFrames % 60 == 0) {
            AutoLog("  rematch: asked the server");
            gSession.RequestPlayAgain();
        }
        return hud.state == royale::MatchState::Countdown;
    }
    if (s.verb == "board") return hud.state == royale::MatchState::Countdown && hud.aboard && hud.busSeat >= 0;
    if (s.verb == "fresh") {
        if (!InGame() || !hud.haveSelf || hud.state != royale::MatchState::Drop) return false;
        const bool drawn = (GET_PLAYER(gPlayState)->stateFlags2 & PLAYER_STATE2_DISABLE_DRAW) == 0;
        const bool clean = !gDeath.active && !gEnd.open && hud.selfAlive;
        AutoLog("  fresh: alive %d death card %d end screen %d drawn %d", hud.selfAlive ? 1 : 0, gDeath.active ? 1 : 0, gEnd.open ? 1 : 0, drawn ? 1 : 0);
        if (!clean || !drawn) AutoFinish(false, "fresh: the last match's death card, end screen or hidden Link is still here");
        return true;
    }
    if (s.verb == "regular") {
        if (gSession.GetMode() != royale::RoyaleSession::Mode::Idle) gSession.Leave();
        gSaveContext.ship.quest.id = QUEST_NORMAL;
        if (WarMenuOpen()) WarHide();
        RefreshRegularSettings();
        return true;
    }
    if (s.verb == "warp" || s.verb == "tour") {
        if (gAuto.sub == 0) {
            gAuto.tour = s.verb == "warp" ? std::vector<int>{ static_cast<int>(std::strtol(s.arg.c_str(), nullptr, 16)) } : AutoTourList(s.arg);
            gAuto.tourScene = -1;
            gAuto.sub = 1;
        }
        if (gAuto.tourScene >= 0) {
            // the game may send you elsewhere for your age (adult Link at Hyrule Castle lands outside Ganon's Castle): any scene loaded since the
            // warp counts, and the log names where you ended up
            const bool redirected = gAuto.scenesLoaded > gAuto.tourLoads && gPlayState != nullptr && gPlayState->sceneNum != gAuto.tourScene;
            if (!AutoArrived(redirected ? gPlayState->sceneNum : gAuto.tourScene)) {
                // a scene that never arrives is a failure of its own (timeout below names it)
                return false;
            }
            static unsigned arrivedAt = 0;
            if (gAuto.sub == 1) { arrivedAt = gAuto.frames; gAuto.sub = 2; }
            if (gAuto.frames - arrivedAt < (s.verb == "tour" ? 60u : 20u)) return false;
            if (redirected) AutoLog("  scene 0x%02X arrived as 0x%02X (%d actors)", gAuto.tourScene, gPlayState->sceneNum, gPlayState->actorCtx.total);
            else AutoLog("  scene 0x%02X arrived (%d actors)", gAuto.tourScene, gPlayState->actorCtx.total);
            gAuto.tourScene = -1;
            gAuto.sub = 1;
        }
        if (gAuto.tour.empty()) return true;
        if (!InGame() || gPlayState->transitionTrigger != TRANS_TRIGGER_OFF) return false;
        const int scene = gAuto.tour.front();
        const int entrance = AutoEntranceFor(scene);
        gAuto.tour.erase(gAuto.tour.begin());
        if (entrance < 0) { AutoLog("  scene 0x%02X has no entrance, skipped", scene); return false; }
        gTravelCooldown = 0;
        Crumb("autotest: warp to scene 0x%02X entrance 0x%03X", scene, entrance);
        if (!TravelTo(entrance)) { gAuto.tour.insert(gAuto.tour.begin(), scene); return false; }
        gAuto.tourScene = scene;
        gAuto.tourLoads = gAuto.scenesLoaded;
        gAuto.stepStartFrame = gAuto.frames;   // each scene gets the full timeout
        return false;
    }
    if (s.verb == "check") {
        if (!InGame()) { AutoFinish(false, "check: no save loaded or no Link"); }
        Player* link = GET_PLAYER(gPlayState);
        AutoLog("  check: scene 0x%02X room %d actors %d link (%.0f, %.0f, %.0f) floor %.0f", gPlayState->sceneNum, gPlayState->roomCtx.curRoom.num,
                gPlayState->actorCtx.total, link->actor.world.pos.x, link->actor.world.pos.y, link->actor.world.pos.z, link->actor.floorHeight);
        if (link->actor.floorHeight <= BGCHECK_Y_MIN + 1.0f && !gSkydiving && !hud.aboard)
            AutoLog("  WARN no floor under Link (void or missing collision)");
        {   // where the drawn camera is; in a match it must not sit inside the ground or behind a solid face from Link (patches/0020)
            Vec3f eye = gPlayState->view.eye;
            Vec3f at = gPlayState->view.lookAt;
            Vec3f hit;
            Vec3f top = { eye.x, eye.y + 3000.0f, eye.z };
            CollisionPoly floorPoly;
            CollisionPoly* poly = nullptr;
            s32 bgId = 0;
            const f32 ground = BgCheck_AnyRaycastFloor2(&gPlayState->colCtx, &floorPoly, &bgId, &top);
            const bool blocked = BgCheck_CameraLineTest1(&gPlayState->colCtx, &at, &eye, &hit, &poly, 1, 1, 1, 1, &bgId);
            // Ground above the eye is an overhang (a ledge, a sign, an arch) when its underside faces the eye; inside a hill there is none.
            bool underside = false;
            if (ground > eye.y + 2.0f) {
                Vec3f up = { eye.x, ground - 1.0f, eye.z };
                underside = BgCheck_CameraLineTest1(&gPlayState->colCtx, &eye, &up, &hit, &poly, 1, 1, 1, 1, &bgId);
            }
            AutoLog("  camera eye (%.0f, %.0f, %.0f) at (%.0f, %.0f, %.0f) ground under eye %.0f%s%s", eye.x, eye.y, eye.z, at.x, at.y, at.z, ground,
                    underside ? " (under an overhang)" : "", blocked ? " (solid between Link and eye)" : "");
            if (gRoyaleCamGuard && ground > eye.y + 2.0f && ground < eye.y + 2900.0f && !underside) AutoLog("  WARN camera inside the ground");
            else if (gRoyaleCamGuard && blocked) AutoLog("  WARN camera behind a solid face");
        }
        AutoState("state at check");
        return true;
    }
    if (s.verb == "shot") {
        const std::string req = gAuto.beatPath.substr(0, gAuto.beatPath.find_last_of("/\\") + 1) + "shot-request.txt";
        if (gAuto.sub == 0) { std::ofstream(req, std::ios::trunc) << s.arg << "\n"; gAuto.sub = 1; return false; }
        if (std::filesystem::exists(req)) {
            if (secs > 20) { std::error_code ec; std::filesystem::remove(req, ec); AutoLog("  shot %s: runner never took it", s.arg.c_str()); return true; }
            return false;
        }
        return true;
    }
    if (s.verb == "quit") AutoFinish(true, "all steps done");
    AutoFinish(false, ("unknown step " + s.verb).c_str());
    return true;
}

float AutoTimeout(const AutoStep& s) {
    if (s.verb == "wait") return 1e9f;
    if (s.verb == "ingame") return 180;
    if (s.verb == "state") return 600;
    if (s.verb == "host" || s.verb == "hostbots") return 300;
    if (s.verb == "land" || s.verb == "board" || s.verb == "rematch") return 120;
    if (s.verb == "fight") return 300;
    if (s.verb == "outcome" || s.verb == "results") return 1800;   // the storm and the rest of the match: game time, not wall time
    if (s.verb == "warp" || s.verb == "tour") return 90;   // per scene
    return 60;
}

void AutoTick() {
    if (!gAuto.on) return;
    gAuto.frames++;
    if (gAuto.frames % 20 == 0) {
        std::ofstream beat(gAuto.beatPath, std::ios::trunc);
        if (beat) beat << gAuto.frames << " " << gAuto.at << " " << (gAuto.at < gAuto.steps.size() ? gAuto.steps[gAuto.at].verb : "end") << "\n";
    }
    if (gAuto.at >= gAuto.steps.size()) AutoFinish(true, "script ended");
    const AutoStep& s = gAuto.steps[gAuto.at];
    gAuto.stepFrames = gAuto.frames - gAuto.stepStartFrame;
    const float secs = gAuto.stepFrames / 20.0f;
    if (AutoRun(s, secs)) {
        AutoLog("OK   %-8s %-12s frames %5u  %.1f s", s.verb.c_str(), s.arg.c_str(), gAuto.stepFrames, AutoNow() - gAuto.stepStartTime);
        gAuto.at++;
        gAuto.sub = 0;
        gAuto.stepStartFrame = gAuto.frames;
        gAuto.stepStartTime = AutoNow();
        return;
    }
    if (secs > AutoTimeout(s)) {
        char why[200];
        if (gAuto.tourScene >= 0) std::snprintf(why, sizeof(why), "timeout in %s %s: scene 0x%02X never finished loading", s.verb.c_str(), s.arg.c_str(), gAuto.tourScene);
        else std::snprintf(why, sizeof(why), "timeout in %s %s after %.0f s game time", s.verb.c_str(), s.arg.c_str(), secs);
        AutoFinish(false, why);
    }
}

void AutoSceneInit(int16_t scene) {
    if (!gAuto.on) return;
    gAuto.scenesLoaded++;
    gAuto.lastScene = scene;
    AutoLog("  scene init 0x%02X (%s)", static_cast<int>(scene), SceneName(scene));
}

void InstallAutoTest() {
    const char* script = std::getenv("ROYALE_AUTOTEST");
    if (script == nullptr || *script == '\0') return;
    gAuto.on = true;
    const char* out = std::getenv("ROYALE_AUTOTEST_OUT");
    gAuto.outPath = out ? out : Ship::Context::GetPathRelativeToAppDirectory("autotest-result.txt");
    gAuto.beatPath = gAuto.outPath.substr(0, gAuto.outPath.find_last_of("/\\") + 1) + "autotest-heartbeat.txt";
    std::stringstream ss(script);
    std::string item;
    while (std::getline(ss, item, ';')) {
        item.erase(0, item.find_first_not_of(" \t"));
        item.erase(item.find_last_not_of(" \t") + 1);
        if (item.empty()) continue;
        const size_t colon = item.find(':');
        gAuto.steps.push_back({ item.substr(0, colon), colon == std::string::npos ? "" : item.substr(colon + 1) });
    }
    { std::ofstream clear(gAuto.outPath, std::ios::trunc); }
    gAuto.startTime = gAuto.stepStartTime = AutoNow();
    AutoLog("autotest: %zu steps: %s", gAuto.steps.size(), script);
    // Every game state runs this hook each frame (the boot screen too), so the script keeps time before a save is loaded.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameStateMainStart>(AutoTick);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>(AutoSceneInit);
}

static RegisterShipInitFunc autoTestInit(InstallAutoTest);

} // namespace
