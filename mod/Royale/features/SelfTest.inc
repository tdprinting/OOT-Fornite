// ---- self-test: scripted checks in the real game, written to royale-selftest.txt ------------------------------------------------------
// Layer 3 of the feature check (docs/TEST_TOOL_DESIGN.md). Settings > Debug > "SelfTest" (or the CVar gSettings.Royale.Debug.SelfTest = 1)
// starts one run: it hosts a solo Sandbox lobby, lets the match start, runs the checks below one after another (each may wait a few frames for
// its answer, up to its own deadline), leaves, and writes royale-selftest.txt next to royale-crash.txt (the previous run becomes
// royale-selftest-prev.txt). While it runs, royale-selftest-running.txt names the step, and every step is a breadcrumb, so a crash mid-run
// names it in royale-crash.txt. It only starts from a loaded save with no lobby: it never joins anyone else's game.
// The agent reads the file with: python scripts/feature_check.py --device-report royale-selftest.txt. Last in the include order, like
// CrashContext.inc, so it can read every feature's state. Steps that need a counter in another feature: gSkinStats (Skins.inc), gHudDraws
// (EndMatch.inc). Not yet here (design steps 8-10, 14, 16): bus model, launch pad, water surface, ally hire, Peter's voice files.
namespace {

enum class SelfResult { Pass, Fail, Skip, Wait };
struct SelfTestCtx {
    double runStart = 0, stepStart = 0;
    int stepFrames = 0;                  // frames since this step began (0 on its first call)
    char detail[160] = "";
    int savedSkin = 0;
    uint32_t missesAt = 0;               // graphics.pool: the miss count 20 frames before the end
    unsigned hudAt = 0;                  // hud.window: the HUD's frame count when the step began
    float jumpMax = 0;                   // link_anims.jump: the highest clip weight seen
    bool timing = false;                 // frame.time: counting since the scene was up
    double frameSum = 0, frameMax = 0, lastFrame = 0;
    int frameCount = 0;
    char state[kCrashStateLen] = "";     // the mod's state just before leaving (the report's state block)
};
struct SelfCheck { const char* feature; const char* name; float deadline; SelfResult (*fn)(SelfTestCtx&); };

SelfResult SelfDetail(SelfTestCtx& c, SelfResult r, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(c.detail, sizeof(c.detail), fmt, args);
    va_end(args);
    return r;
}
float StepSeconds(const SelfTestCtx& c) { return static_cast<float>(SecondsSinceLaunch() - c.stepStart); }

// ---- the checks (feature names are rows of scripts/feature_checks.json; check_mod_layout.py keeps them in step) ----
SelfResult StepHost(SelfTestCtx& c) {
    if (c.stepFrames == 0) {
        std::string err;
        if (!gSession.Host(static_cast<uint16_t>(CVarGetInteger(ROYALE_CVAR("Port"), royale::net::kDefaultPort)), "SelfTest", &err, true, true))
            return SelfDetail(c, SelfResult::Fail, "could not host: %s", err.c_str());
        gSoloStartWanted = true;   // Terrain.inc goes to the field and starts the match, as the menu's test course does
    }
    if (!gSession.Joined()) return SelfDetail(c, SelfResult::Wait, "not joined after %.1f s", StepSeconds(c));
    return SelfDetail(c, SelfResult::Pass, "%.1f s", StepSeconds(c));
}
SelfResult StepStart(SelfTestCtx& c) {
    const royale::HudState hud = gSession.Hud();
    if (hud.state != royale::MatchState::InMatch) return SelfDetail(c, SelfResult::Wait, "state %s after %.1f s", MatchStateName(hud.state), StepSeconds(c));
    return SelfDetail(c, SelfResult::Pass, "%.1f s", StepSeconds(c));
}
SelfResult StepScene(SelfTestCtx& c) {
    if (gPlayState == nullptr || !InField() || !gFortniteBuilt)
        return SelfDetail(c, SelfResult::Wait, "play state %d, in field %d, island collision %d", gPlayState ? 1 : 0, InField() ? 1 : 0, gFortniteBuilt ? 1 : 0);
    int actors = 0;
    for (int k = 0; k < ACTORCAT_MAX; k++) actors += gPlayState->actorCtx.actorLists[k].length;
    c.timing = true;   // frame.time counts from here (the scene load itself is one long frame)
    return SelfDetail(c, SelfResult::Pass, "actors %d, scene 0x%02X, %.1f s", actors, gPlayState->sceneNum, StepSeconds(c));
}
SelfResult StepCrashContext(SelfTestCtx& c) {
    const int now = gCrashStateNow.load(std::memory_order_acquire);
    if (now < 0) return SelfDetail(c, SelfResult::Wait, "no snapshot yet");
    const char* block = gCrashState[now];
    const size_t len = strnlen(block, kCrashStateLen);
    if (len >= kCrashStateLen) return SelfDetail(c, SelfResult::Fail, "snapshot not terminated");
    if (std::strstr(block, "Match:") == nullptr || std::strstr(block, "Me:") == nullptr) return SelfDetail(c, SelfResult::Fail, "no Match:/Me: lines (%zu bytes)", len);
    return SelfDetail(c, SelfResult::Pass, "%zu of %zu bytes", len, kCrashStateLen);
}
SelfResult StepLoot(SelfTestCtx& c) {
    const auto* client = gSession.Client();
    const size_t onServer = client ? client->Loot().size() : 0;
    if (onServer == 0) return SelfDetail(c, SelfResult::Skip, "no loot on this map");
    if (gLoot.empty()) {
        if (StepSeconds(c) < 8.0f) return SelfDetail(c, SelfResult::Wait, "%zu loot, 0 actors", onServer);
        return SelfDetail(c, SelfResult::Skip, "%zu loot, none near enough for an actor", onServer);
    }
    int actors = 0;
    for (const auto& [index, la] : gLoot) {
        if (la.actor == nullptr) continue;
        actors++;
        const auto it = gLootOf.find(la.actor);
        if (it == gLootOf.end() || it->second != index) return SelfDetail(c, SelfResult::Fail, "loot %zu's actor not mapped back", index);
    }
    return SelfDetail(c, SelfResult::Pass, "%zu loot, %d actors", onServer, actors);
}
SelfResult StepSkins(SelfTestCtx& c) {
    if (!DebugOn(kDbgSkins)) return SelfDetail(c, SelfResult::Skip, "Debug > Skins is off");
    if (c.stepFrames == 0) gSkinStats = SkinStats{};
    if (c.stepFrames < 30) return SelfDetail(c, SelfResult::Wait, "counting");
    const bool ok = gSkinStats.limbsSeen >= 18 && gSkinStats.drawn >= 1;
    return SelfDetail(c, ok ? SelfResult::Pass : SelfResult::Fail, "skin 1: limbs seen %u, drawn %u in 30 frames%s", gSkinStats.limbsSeen, gSkinStats.drawn,
                      ok ? "" : " (expected >= 18 and >= 1)");
}
SelfResult StepJump(SelfTestCtx& c) {
    if (c.stepFrames == 0) { StartLinkJumpClip(); c.jumpMax = 0; }
    c.jumpMax = std::max(c.jumpMax, link_clip::gJumpW);
    if (c.jumpMax > 0.0f) return SelfDetail(c, SelfResult::Pass, "clip weight %.2f after %d frames", c.jumpMax, c.stepFrames);
    return SelfDetail(c, SelfResult::Wait, "clip weight never rose");
}
SelfResult StepGfxPool(SelfTestCtx& c) {
    if (c.stepFrames == 40) c.missesAt = gGfxPool.dataMisses;
    if (c.stepFrames < 60) return SelfDetail(c, SelfResult::Wait, "counting");
    const bool ok = !gGfxPool.overflowLast && gGfxPool.dataMisses == c.missesAt;
    return SelfDetail(c, ok ? SelfResult::Pass : SelfResult::Fail, "overflow %d, misses +%u in 20 frames, peak %zu KB", gGfxPool.overflowLast ? 1 : 0,
                      gGfxPool.dataMisses - c.missesAt, gGfxPool.peak / 1024);
}
SelfResult StepLights(SelfTestCtx& c) {
    if (!DebugOn(kDbgShadows) || !gShadowCfg.lights) return SelfDetail(c, SelfResult::Skip, "dynamic lights off");
    Player* link = gPlayState ? GET_PLAYER(gPlayState) : nullptr;
    if (link == nullptr) return SelfDetail(c, SelfResult::Fail, "no Link");
    if (c.stepFrames == 0) AddLightFlash(link->actor.world.pos.x, link->actor.world.pos.y + 40, link->actor.world.pos.z, 255, 200, 120, 400, 1.0f, kLightKeyMagic);
    if (gLightsNowCount > 0) return SelfDetail(c, SelfResult::Pass, "%d lights", gLightsNowCount);
    return SelfDetail(c, SelfResult::Wait, "no light after %d frames", c.stepFrames);
}
SelfResult StepHud(SelfTestCtx& c) {
    if (c.stepFrames == 0) c.hudAt = gHudDraws;
    if (gHudDraws - c.hudAt >= 10) return SelfDetail(c, SelfResult::Pass, "%u HUD frames", gHudDraws - c.hudAt);
    return SelfDetail(c, SelfResult::Wait, "%u HUD frames drawn", gHudDraws - c.hudAt);
}
SelfResult StepStartup(SelfTestCtx& c) {
#ifdef __ANDROID__
    std::error_code ec;
    for (const char* f : { ".launch-pending", ".launch-guarded" })
        if (std::filesystem::exists(Ship::Context::GetPathRelativeToAppDirectory(f), ec)) return SelfDetail(c, SelfResult::Fail, "%s still there (patch 0030 guard)", f);
    if (gStartupLine.empty()) return SelfDetail(c, SelfResult::Fail, "no startup-times.txt line");
    return SelfDetail(c, SelfResult::Pass, "%s", gStartupLine.c_str());
#else
    return SelfDetail(c, SelfResult::Skip, "Android only");
#endif
}
SelfResult StepFrames(SelfTestCtx& c) {
    if (c.frameCount < 30) return SelfDetail(c, SelfResult::Skip, "only %d frames timed", c.frameCount);
    const double avg = c.frameSum / c.frameCount;
    const bool ok = avg < 0.080 && c.frameMax < 0.500;
    return SelfDetail(c, ok ? SelfResult::Pass : SelfResult::Fail, "avg %.1f ms, max %.0f ms over %d frames%s", avg * 1000, c.frameMax * 1000, c.frameCount,
                      ok ? "" : " (want avg < 80, max < 500)");
}
SelfResult StepLeave(SelfTestCtx& c) {
    if (c.stepFrames == 0) {
        const int now = gCrashStateNow.load(std::memory_order_acquire);
        if (now >= 0) std::snprintf(c.state, sizeof(c.state), "%s", gCrashState[now]);
        gSession.Leave();
    }
    if (gSession.Joined() || gSession.GetMode() != royale::RoyaleSession::Mode::Idle) return SelfDetail(c, SelfResult::Wait, "still in the lobby");
    return SelfDetail(c, SelfResult::Pass, "%.1f s", StepSeconds(c));
}

const SelfCheck kSelfChecks[] = {
    { "session", "session.host", 10, StepHost },
    { "session", "session.start", 60, StepStart },
    { "terrain", "scene.loaded", 15, StepScene },
    { "crash_context", "crash.context", 3, StepCrashContext },
    { "world_objects", "loot.actors", 10, StepLoot },
    { "skins", "skins.draw", 5, StepSkins },
    { "link_anims", "link_anims.jump", 1, StepJump },
    { "core", "graphics.pool", 6, StepGfxPool },
    { "lighting", "shadows.lights", 2, StepLights },
    { "hud", "hud.window", 3, StepHud },
    { "music_diagnostics", "startup.guard", 1, StepStartup },
    { "self_test", "frame.time", 1, StepFrames },
    { "session", "session.leave", 3, StepLeave },
};
constexpr int kSelfCheckCount = static_cast<int>(sizeof(kSelfChecks) / sizeof(kSelfChecks[0]));

// ---- the run ----
struct SelfTestRun {
    bool on = false;
    int step = 0;
    SelfTestCtx ctx;
    std::string lines;   // the report's result lines
    int passed = 0, failed = 0, skipped = 0;
    std::string firstFail;
};
SelfTestRun gSelfTest;
std::string gSelfTestLine;   // "Self-test: 12 passed, 1 failed (skins.draw)" (empty until a run ends)

std::string SelfTestPath(const char* name) { return Ship::Context::GetPathRelativeToAppDirectory(name); }
void SelfTestProgress(const char* step) {
    if (FILE* f = std::fopen(SelfTestPath("royale-selftest-running.txt").c_str(), "w")) { std::fprintf(f, "running %s\n", step); std::fclose(f); }
}

void SelfTestWriteReport(const SelfTestRun& r, const char* why) {
    std::error_code ec;
    const std::string path = SelfTestPath("royale-selftest.txt");
    if (std::filesystem::exists(path, ec)) std::filesystem::rename(path, SelfTestPath("royale-selftest-prev.txt"), ec);
    FILE* f = std::fopen(path.c_str(), "w");
    if (f == nullptr) return;
    std::fprintf(f, "Build %s  protocol %d  map %d (%s)  solo sandbox  started %.1f s after launch\n", ROYALE_BUILD_VERSION,
                 static_cast<int>(royale::net::kProtocolVersion), gMapId, royale::MapOf(gMapId).name, r.ctx.runStart);
    if (why != nullptr) std::fprintf(f, "SKIP  self-test        %s\n", why);
    std::fputs(r.lines.c_str(), f);
    std::fprintf(f, "%d passed, %d failed, %d skipped, %.1f s\n", r.passed, r.failed, r.skipped, SecondsSinceLaunch() - r.ctx.runStart);
    std::fprintf(f, "---- mod state ----\n%s", r.ctx.state[0] ? r.ctx.state : "(none)\n");
    std::fprintf(f, "---- last events ----\n");
    const unsigned next = gCrumbNext.load(std::memory_order_relaxed);
    for (unsigned k = next > static_cast<unsigned>(kCrumbCount) ? next - kCrumbCount : 0; k < next; k++) std::fprintf(f, "%s\n", gCrumbs[k % kCrumbCount]);
    std::fprintf(f, "---- end ----\n");
    std::fclose(f);
    std::filesystem::remove(SelfTestPath("royale-selftest-running.txt"), ec);
}

void SelfTestFinish(const char* why = nullptr) {
    SelfTestRun& r = gSelfTest;
    gLocalSkinModel = r.ctx.savedSkin;
    gSession.SetSkinModel(r.ctx.savedSkin);
    if (gSession.GetMode() != royale::RoyaleSession::Mode::Idle && r.on) gSession.Leave();   // a run cut short never leaves its lobby behind
    char line[160];
    if (why) std::snprintf(line, sizeof(line), "Self-test not run: %s", why);
    else std::snprintf(line, sizeof(line), "Self-test: %d passed, %d failed%s%s%s", r.passed, r.failed, r.failed ? " (" : "", r.firstFail.c_str(), r.failed ? ")" : "");
    gSelfTestLine = line;
    Crumb("%s", line);
    SelfTestWriteReport(r, why);
    r.on = false;
}

void SelfTestBegin() {
    gDebugOn[kDbgSelfTest] = false;   // one run per switch-on; a crash mid-run does not start another on the next launch
    CVarSetInteger((std::string(CVAR_SETTING("Royale.Debug.")) + kDebugSwitches[kDbgSelfTest].key).c_str(), 0);
    Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    gSelfTest = SelfTestRun{};
    SelfTestCtx& c = gSelfTest.ctx;
    c.runStart = c.stepStart = c.lastFrame = SecondsSinceLaunch();
    c.savedSkin = gLocalSkinModel;
    if (!InGame()) { SelfTestFinish("load a save first (the self-test needs a play state)"); return; }
    if (gSession.GetMode() != royale::RoyaleSession::Mode::Idle) { SelfTestFinish("leave the lobby first (it never runs in someone's game)"); return; }
    if (DebugOn(kDbgSkins)) { gLocalSkinModel = 1; gSession.SetSkinModel(1); }   // skins.draw needs a skin on Link
    gSelfTest.on = true;
    Crumb("selftest: start");
    SelfTestProgress(kSelfChecks[0].name);
}

void SelfTestFrame() {
    if (!gSelfTest.on) {
        if (DebugOn(kDbgSelfTest)) SelfTestBegin();
        return;
    }
    Feat("self test");
    SelfTestRun& r = gSelfTest;
    SelfTestCtx& c = r.ctx;
    const double now = SecondsSinceLaunch();
    if (c.timing) { const double d = now - c.lastFrame; c.frameSum += d; c.frameMax = std::max(c.frameMax, d); c.frameCount++; }
    c.lastFrame = now;
    const SelfCheck& s = kSelfChecks[r.step];
    SelfResult res = s.fn(c);
    if (res == SelfResult::Wait && StepSeconds(c) > s.deadline) {
        res = SelfResult::Fail;
        char d[sizeof(c.detail)];
        std::snprintf(d, sizeof(d), "timeout %.0f s: %s", s.deadline, c.detail);
        std::snprintf(c.detail, sizeof(c.detail), "%s", d);
    }
    if (res == SelfResult::Wait) { c.stepFrames++; return; }
    const char* word = res == SelfResult::Pass ? "PASS" : res == SelfResult::Fail ? "FAIL" : "SKIP";
    char line[256];
    std::snprintf(line, sizeof(line), "%s  %-18s  %s\n", word, s.name, c.detail);
    r.lines += line;
    Crumb("selftest %s: %s", s.name, word);
    if (res == SelfResult::Pass) r.passed++;
    else if (res == SelfResult::Skip) r.skipped++;
    else { r.failed++; if (r.firstFail.empty()) r.firstFail = s.name; }
    // Without a lobby or a match nothing after it can be checked: go straight to leaving.
    const bool fatal = res == SelfResult::Fail && r.step < 2;
    r.step = fatal ? kSelfCheckCount - 1 : r.step + 1;
    if (r.step >= kSelfCheckCount || (fatal && gSession.GetMode() == royale::RoyaleSession::Mode::Idle)) { SelfTestFinish(); return; }
    c.stepFrames = 0;
    c.stepStart = now;
    c.detail[0] = '\0';
    SelfTestProgress(kSelfChecks[r.step].name);
}

void InstallSelfTest() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(SelfTestFrame);
}

} // namespace
