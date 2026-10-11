// ---- crash context: the mod's state for crash reports and the trace ---------------------------------------------------------
// The breadcrumb ring and the snapshot buffers are in Core.inc (Crumb, gCrashState); the crash handler that prints them is in
// MusicDiagnostics.inc. A few times a second, on the game thread, this file writes what the mod is doing into gCrashState (into the half the
// handler is not reading, then flips) and notes changes worth knowing (scene, match state, bus, knocked down, cart, weapon, allies...) as
// breadcrumbs. Each scene load also copies the block into royale-trace.txt, so a bug that is not a crash still has its context.
// How to read it: docs/BUILDING.md, "Crash reports and the trace". It is last in the include order so it can read every feature's state.
namespace {

const char* MatchStateName(royale::MatchState s) {
    switch (s) {
        case royale::MatchState::Lobby: return "lobby";
        case royale::MatchState::Countdown: return "countdown";
        case royale::MatchState::Drop: return "drop";
        case royale::MatchState::InMatch: return "in match";
        case royale::MatchState::Ending: return "ending";
    }
    return "?";
}
const char* CtxRarityName(int r) {
    static const char* const kNames[] = { "Common", "Uncommon", "Rare", "Epic", "Legendary" };
    return r >= 0 && r < 5 ? kNames[r] : "?";
}
const char* CtxItemName(int item) {
    return item >= 0 && item < static_cast<int>(royale::ItemId::Count) ? royale::NameOf(static_cast<royale::ItemId>(item)) : "?";
}
// Where the player is, in a few words.
const char* PlaceKind(const royale::HudState& hud) {
    if (gPlayState == nullptr || gSaveContext.gameMode != GAMEMODE_NORMAL) return "menu (title or file select)";
    if (gSession.Joined()) return hud.state == royale::MatchState::Lobby ? "BR lobby" : "BR match";
    if (InRegularGame()) return "regular game";
    return "BR save, no lobby joined";
}

// Appends formatted text to a fixed buffer, never past its end.
struct CtxText {
    char* out;
    size_t cap;
    size_t n = 0;
    void Add(const char* fmt, ...) {
        if (n + 1 >= cap) return;
        va_list args;
        va_start(args, fmt);
        const int w = std::vsnprintf(out + n, cap - n, fmt, args);
        va_end(args);
        if (w > 0) n = std::min(cap - 1, n + static_cast<size_t>(w));
    }
};

// The snapshot as text, one topic a line.
void BuildCrashState(char* out, size_t cap, const royale::HudState& hud) {
    CtxText t{ out, cap };
    out[0] = '\0';
    t.Add("Build %s  protocol %d  up %.1f s  frame feature: %s\n", ROYALE_BUILD_VERSION, static_cast<int>(royale::net::kProtocolVersion),
          SecondsSinceLaunch(), gFeature);
    const char* mode = hud.mode == royale::HudState::Mode::Hosting ? "hosting" : hud.mode == royale::HudState::Mode::Joined ? "joined" : "idle";
    t.Add("Where: %s  session %s%s%s  connected %d  native menu open %d\n", PlaceKind(hud), mode, gSession.SoloTest() ? " (solo test)" : "",
          gSession.Sandbox() ? " (sandbox)" : "", hud.connected ? 1 : 0, WarMenuOpen() ? 1 : 0);
    if (gPlayState != nullptr) {
        int actors = 0;
        for (int c = 0; c < ACTORCAT_MAX; c++) actors += gPlayState->actorCtx.actorLists[c].length;
        t.Add("Scene 0x%02X (%s)  room %d  entrance 0x%04X  game mode %d  outdoors %d  cutscene %d  actors %d\n", gPlayState->sceneNum,
              SceneName(gPlayState->sceneNum), gPlayState->roomCtx.curRoom.num, static_cast<int>(gSaveContext.entranceIndex),
              static_cast<int>(gSaveContext.gameMode), SceneOutdoors() ? 1 : 0, InCutscene() ? 1 : 0, actors);
    } else {
        t.Add("Scene: none (no play state)  game mode %d\n", static_cast<int>(gSaveContext.gameMode));
    }
    t.Add("Map %d (%s)  in field %d  island collision %d (built %d)  frames in field %d\n", gMapId, royale::MapOf(gMapId).name, InField() ? 1 : 0,
          gFortniteScene ? 1 : 0, gFortniteBuilt ? 1 : 0, gInFieldFrames);
    t.Add("Match: %s  countdown %.1f  lobby timer %.0f  alive %d  storm phase %d%s (%.0f s left)  team mode %s  team %d  squad %d\n",
          MatchStateName(hud.state), hud.countdownLeft, hud.lobbyLeft, hud.alive, hud.stormPhase, hud.stormShrinking ? " shrinking" : "",
          hud.stormSecondsLeft, royale::TeamModeName(hud.teamMode), hud.selfTeam, static_cast<int>(hud.squad.size()));
    t.Add("Me: id %u%s  alive %d  knocked %d  health %.1f/%.1f  aboard bus %d (seat %d, bus time %.1f)  skydiving %d (bus phase %d)  cart %s (index %d)\n",
          static_cast<unsigned>(hud.selfId), hud.haveSelf ? "" : " (not in snapshots)", hud.selfAlive ? 1 : 0, hud.selfKnocked ? 1 : 0, hud.selfHealth, hud.maxHealth,
          hud.aboard ? 1 : 0, hud.busSeat, hud.busTime, gSkydiving ? 1 : 0, gBusPhase, gRide.driving ? "driving" : gRide.seated ? "seated" : "none",
          gRide.index);
    Player* pl = gPlayState != nullptr ? GET_PLAYER(gPlayState) : nullptr;
    if (pl != nullptr) {
        t.Add("Link: pos %.0f %.0f %.0f  facing %d  age %d  health %d/%d  held item action %d  item action %d  state1 0x%08X\n",
              pl->actor.world.pos.x, pl->actor.world.pos.y, pl->actor.world.pos.z, static_cast<int>(pl->actor.shape.rot.y),
              static_cast<int>(gSaveContext.linkAge), static_cast<int>(gSaveContext.health), static_cast<int>(gSaveContext.healthCapacity),
              static_cast<int>(pl->heldItemAction), static_cast<int>(pl->itemAction), static_cast<unsigned>(pl->stateFlags1));
    } else {
        t.Add("Link: none\n");
    }
    t.Add("Hands: weapon %s (%s)  shield %s  hand stack %d (%s %s)  potions %d  reserve", CtxItemName(static_cast<int>(hud.weapon)),
          CtxRarityName(static_cast<int>(hud.weaponRarity)), hud.hasShield ? CtxItemName(static_cast<int>(hud.shield)) : "none", gHandStack,
          gHandStack >= 0 ? CtxItemName(static_cast<int>(gHandItem)) : "-", gHandStack >= 0 ? CtxRarityName(static_cast<int>(gHandRarity)) : "",
          static_cast<int>(hud.inv.potions.size()));
    for (const auto& r : hud.inv.reserve) t.Add(" [%s %s]", CtxItemName(r.item), CtxRarityName(r.rarity));
    t.Add("\n");
    int alliesServer = 0, alliesMine = 0;
    if (const auto* c = gSession.Client()) {
        for (const auto& a : c->Allies()) { alliesServer++; if (a.owner == hud.selfId && hud.haveSelf) alliesMine++; }
    }
    t.Add("Counts: humans %d  limit %d  bot slots %d  puppets drawn %d  allies %d (mine %d, drawn %d)  carts %d  loot actors %d  prop actors %d\n",
          hud.humanCount, hud.playerLimit, hud.botSlots, static_cast<int>(gPuppetOf.size()), alliesServer, alliesMine, static_cast<int>(gAllies.size()),
          static_cast<int>(gCarts.size()), static_cast<int>(gLoot.size()), static_cast<int>(gProps.size()));
    t.Add("Weather: %s %s strength %.2f blend %.2f  storm weather %.2f  wind %d x%.1f  tornado %d  music mode %d\n", royale::SeasonName(gWeatherShown.season),
          royale::SkyName(gWeatherShown.sky), gWeatherShown.Strength(), gWeatherBlend, gStormWeather, gWindOn ? 1 : 0, gWindScale, gTornadoOn ? 1 : 0, gMusicMode);
    t.Add("Graphics: gfx pool want %zu KB peak %zu KB last %zu KB overflow %d misses %u  shadows quality %d lights %d  water detail %d  custom scenery %d\n",
          gGfxPool.want / 1024, gGfxPool.peak / 1024, gGfxPool.usedLast / 1024, gGfxPool.overflowLast ? 1 : 0, gGfxPool.dataMisses, gShadowCfg.quality,
          gShadowCfg.lights ? 1 : 0, gWaterDetail, CVarGetInteger(CVAR_SETTING("Royale.CustomScenery"), 1));
    t.Add("Regular game:");
    for (int i = 0; i < kRegCount; i++) t.Add(" %s=%d", kRegularSettings[i].key, gRegular[i]);
    t.Add("\nDebug switches off:");
    int off = 0;
    for (int i = 0; i < kDebugCount; i++) if (!DebugOn(i) && i != kDbgGearReport && i != kDbgSelfTest) { t.Add(" %s", kDebugSwitches[i].key); off++; }
    t.Add(off == 0 ? " none\n" : "\n");
}

// What the last snapshot saw, to note changes as breadcrumbs.
struct CtxSeen {
    bool init = false;
    int scene = -1, room = -1, entrance = -1, gameMode = -1, map = -1, sessionMode = -1, busPhase = -1, handStack = -2, alliesMine = -1;
    royale::MatchState state = royale::MatchState::Lobby;
    bool joined = false, regular = false, menu = false, alive = true, knocked = false, aboard = false, skydiving = false, seated = false,
         driving = false, overflow = false;
    int weapon = -1;
};
CtxSeen gCtxSeen;
int gCtxTraceIn = -1;   // frames until the scene-load copy goes to the trace (-1: none waiting)

void NoteChanges(const royale::HudState& hud) {
    CtxSeen& s = gCtxSeen;
    const bool first = !s.init;
    s.init = true;
    const int scene = gPlayState != nullptr ? gPlayState->sceneNum : -1;
    const int room = gPlayState != nullptr ? gPlayState->roomCtx.curRoom.num : -1;
    const int entrance = static_cast<int>(gSaveContext.entranceIndex);
    if (scene != s.scene || entrance != s.entrance) Crumb("scene 0x%02X (%s) entrance 0x%04X", scene, scene >= 0 ? SceneName(scene) : "none", entrance);
    else if (room != s.room) Crumb("room %d -> %d", s.room, room);
    s.scene = scene; s.room = room; s.entrance = entrance;
    const int gameMode = static_cast<int>(gSaveContext.gameMode);
    if (gameMode != s.gameMode) Crumb("game mode %d -> %d", s.gameMode, gameMode);
    s.gameMode = gameMode;
    const bool joined = gSession.Joined();
    const int sessionMode = static_cast<int>(hud.mode);
    if (joined != s.joined || sessionMode != s.sessionMode) Crumb("session: %s, %s", joined ? "in a lobby" : "no lobby",
                                                                  sessionMode == 1 ? "hosting" : sessionMode == 2 ? "joined" : "idle");
    s.joined = joined; s.sessionMode = sessionMode;
    const bool regular = InRegularGame();
    if (regular != s.regular && !first) Crumb("regular game %s", regular ? "started" : "left");
    s.regular = regular;
    const bool menu = WarMenuOpen();
    if (menu != s.menu) Crumb("native menu %s", menu ? "opened" : "closed");
    s.menu = menu;
    if (joined) {
        if (gMapId != s.map) Crumb("map %d (%s)", gMapId, royale::MapOf(gMapId).name);
        s.map = gMapId;
        if (hud.state != s.state) Crumb("match state %s -> %s (alive %d)", MatchStateName(s.state), MatchStateName(hud.state), hud.alive);
        s.state = hud.state;
        if (hud.aboard != s.aboard) Crumb(hud.aboard ? "bus: aboard (seat %d)" : "bus: off (seat %d)", hud.busSeat);
        s.aboard = hud.aboard;
        if (hud.haveSelf) {
            if (hud.selfAlive != s.alive) Crumb(hud.selfAlive ? "me: alive again" : "me: eliminated (placing among %d)", hud.alive);
            if (hud.selfKnocked != s.knocked) Crumb(hud.selfKnocked ? "me: knocked down" : "me: back up (revived or out)");
            s.alive = hud.selfAlive; s.knocked = hud.selfKnocked;
        }
        const int weapon = static_cast<int>(hud.weapon);
        if (weapon != s.weapon && hud.haveSelf) Crumb("weapon: %s (%s)", CtxItemName(weapon), CtxRarityName(static_cast<int>(hud.weaponRarity)));
        s.weapon = weapon;
    } else {
        s.map = -1; s.state = royale::MatchState::Lobby; s.aboard = false; s.alive = true; s.knocked = false; s.weapon = -1;
    }
    if (gSkydiving != s.skydiving || gBusPhase != s.busPhase) Crumb("skydive %d bus phase %d (1 seated, 2 free fall, 0 glider)", gSkydiving ? 1 : 0, gBusPhase);
    s.skydiving = gSkydiving; s.busPhase = gBusPhase;
    if (gRide.seated != s.seated || gRide.driving != s.driving) Crumb("cart: %s (index %d)", gRide.driving ? "driving" : gRide.seated ? "seated" : "off", gRide.index);
    s.seated = gRide.seated; s.driving = gRide.driving;
    if (gHandStack != s.handStack && s.handStack != -2) Crumb("hand: %s", gHandStack >= 0 ? CtxItemName(static_cast<int>(gHandItem)) : "weapon");
    s.handStack = gHandStack;
    int mine = 0;
    if (const auto* c = gSession.Client()) for (const auto& a : c->Allies()) if (a.owner == hud.selfId && hud.haveSelf) mine++;
    if (mine != s.alliesMine && s.alliesMine >= 0) Crumb("allies hired by me: %d -> %d", s.alliesMine, mine);
    s.alliesMine = mine;
    if (gGfxPool.overflowLast && !s.overflow) Crumb("graphics: a layer overflowed (pool want %zu KB)", gGfxPool.want / 1024);
    s.overflow = gGfxPool.overflowLast;
}

void CrashContextFrame() {
#ifdef __ANDROID__
    if (gGameThreadId == 0) gGameThreadId = static_cast<long>(syscall(SYS_gettid));
#endif
    static unsigned frames = 0;
    const bool traceNow = gCtxTraceIn >= 0 && gCtxTraceIn-- == 0;
    if (frames++ % 5 != 0 && !traceNow) return;   // four times a second at the game's 20 Hz
    const royale::HudState hud = gSession.Hud();
    NoteChanges(hud);
    const int half = gCrashStateNow.load(std::memory_order_relaxed) == 0 ? 1 : 0;
    BuildCrashState(gCrashState[half], kCrashStateLen, hud);
    gCrashStateNow.store(half, std::memory_order_release);
    if (traceNow) TraceBlock("state after scene load", gCrashState[half]);
}

void CrashContextSceneInit(int16_t scene) {
    Crumb("scene init 0x%02X (%s)", static_cast<int>(scene), SceneName(scene));
    gCtxTraceIn = 10;   // half a second in, once Link and the scene's actors are in place
}

void InstallCrashContext() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(CrashContextFrame);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>(CrashContextSceneInit);
}

} // namespace
