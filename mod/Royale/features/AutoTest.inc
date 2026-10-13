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
//   regular                 leave any session and play the save as the normal quest (the regular game's water, sky, companions)
//   warp:<scene hex>        travel to the scene's first entrance and wait until it is loaded
//   tour:<overworld|dungeons|all>   warp through each scene of the list in turn, 3 seconds in each
//   boots:<0|1|2>           dress Link in kokiri, iron or hover boots whatever he holds (shot them to check patch 0032)
//   tp:<x>,<z>[,<deg>]      put Link on the ground at x,z facing <deg> (0 = +z, 90 = +x); for shots of one place
//   check                   assert Link exists in a loaded scene and log the mod state block
//   shot:<name>             ask the runner for a screenshot (<name>.png in the run folder) and wait until it is taken
//   prof:<sec>              ask the runner to sample the game's CPU use with perf for <sec> wall seconds; logs the frame rate meanwhile
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
        gDebugLoaded = false;   // (a Royale.Debug switch is read again)
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
    if (s.verb == "boots") { gAutoBoots = std::clamp(std::atoi(s.arg.c_str()), 0, 2); return true; }
    if (s.verb == "tp") {
        float x = 0, z = 0, deg = 0;
        if (std::sscanf(s.arg.c_str(), "%f,%f,%f", &x, &z, &deg) < 2 || gPlayState == nullptr) return true;
        float y = 0;
        if (!RawFloorAt(x, z, &y)) { AutoLog("  tp %s: no floor", s.arg.c_str()); return true; }
        Player* p = GET_PLAYER(gPlayState);
        p->actor.world.pos = { x, y + 2.0f, z };
        p->actor.home.pos = p->actor.prevPos = p->actor.world.pos;
        p->actor.shape.rot.y = p->actor.world.rot.y = static_cast<s16>(deg * (32768.0f / 180.0f));
        return true;
    }
    if (s.verb == "leave") { gSession.Leave(); return true; }
    if (s.verb == "tp") {   // tp:x,z[,yaw degrees]: put Link on the floor there, facing yaw (screenshots of one place)
        if (!InGame()) return false;
        float x = 0, z = 0, yaw = 0;
        std::sscanf(s.arg.c_str(), "%f,%f,%f", &x, &z, &yaw);
        Player* link = GET_PLAYER(gPlayState);
        float y = link->actor.world.pos.y;
        if (!RawFloorAt(x, z, &y)) y = GroundY(gPlayState, x, z, y);
        link->actor.world.pos = link->actor.prevPos = link->actor.home.pos = { x, y + 2.0f, z };
        link->actor.velocity.y = 0.0f; link->actor.speedXZ = 0.0f;
        link->actor.shape.rot.y = link->actor.world.rot.y = link->yaw = static_cast<s16>(yaw * (32768.0f / 180.0f));
        AutoLog("  tp: (%.0f, %.0f, %.0f) yaw %.0f", x, y, z, yaw);
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
    if (s.verb == "prof") {
        static unsigned startFrames = 0;
        static double startTime = 0;
        const std::string req = gAuto.beatPath.substr(0, gAuto.beatPath.find_last_of("/\\") + 1) + "prof-request.txt";
        if (gAuto.sub == 0) {
            std::ofstream(req, std::ios::trunc) << s.arg << "\n";
            startFrames = gAuto.frames; startTime = AutoNow(); gAuto.sub = 1;
            return false;
        }
        if (std::filesystem::exists(req) && AutoNow() - startTime < std::atof(s.arg.c_str()) + 90) return false;
        const double wall = AutoNow() - startTime;
        AutoLog("  prof: %u game updates in %.1f s = %.1f per second (full speed is 20)", gAuto.frames - startFrames, wall, (gAuto.frames - startFrames) / std::max(wall, 0.1));
        return true;
    }
    if (s.verb == "quit") AutoFinish(true, "all steps done");
    AutoFinish(false, ("unknown step " + s.verb).c_str());
    return true;
}

float AutoTimeout(const AutoStep& s) {
    if (s.verb == "wait" || s.verb == "prof") return 1e9f;
    if (s.verb == "ingame") return 180;
    if (s.verb == "state") return 600;
    if (s.verb == "host" || s.verb == "hostbots") return 300;
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
