// ---- the map intro: a cinematic fly-over before the drop (patches/0038) ------------------------------------------------------------------
// On the authored islands (royale::IsAuthoredMap) the countdown, once the loading splash is gone, plays a short film of the map while everyone
// sits on the Moonfall Bus: a high establishing orbit over the whole island, four of its places (push-in, orbit, tracking pass, crane reveal,
// in a seeded order), then a pull up to the bus that settles into the seat camera as the countdown runs out, so the drop begins without a cut.
// Each shot has a place-name card in the style of Ocarina of Time's area titles, the picture is letterboxed, shots meet in a short dip to
// black, and the title theme plays (only with "Match music: Original"). B (or tapping "Skip") cuts back to the bus at once.
//
// Nothing here is networked: every game builds the same plan from the bus line and the map's places (both from the server), and times it by
// the countdown, so it ends exactly when the drop starts. The camera is the engine's own main camera with its drawn view replaced
// (gRoyaleCamOverride, patches/0038): the camera keeps following Link on his bench underneath, so the game's view is back the frame the intro
// lets go. UpdateMapIntro (OnGameFrameUpdate) runs it, DrawMapIntro (DrawOverlay) draws the cards and bars, MapIntroInput (OnEmoteWheelInput)
// holds the pad. Setting: Settings > Comfort > Map intro (Royale.MapIntro, on by default).

constexpr float kIntroMinSeconds = 7.0f;     // less countdown than this left when we could begin (a slow load): no intro
constexpr float kIntroEarly = 0.6f;          // begin this long before the loading splash has faded (its fade shows our opening black)
constexpr float kIntroOpenBlack = 0.35f, kIntroOpenFade = 0.9f;
constexpr float kIntroCutHalf = 0.24f;       // half of each dip to black between shots
constexpr float kIntroEndFade = 0.4f;        // the last moment dips to black...
constexpr float kIntroTailFade = 0.45f;      // ...and the game's view fades up from it
constexpr float kIntroSkipFade = 0.3f;
constexpr float kIntroHandover = 1.3f;       // the last shot blends into the seat camera over this long
constexpr float kIntroClearance = 170.0f;    // the camera never comes closer than this above the ground or the water
constexpr int kIntroMaxShots = 7;

struct IntroShot {
    enum Kind : uint8_t { Line, Orbit, Bus } kind = Line;
    Vec3f eye0{}, eye1{}, at0{}, at1{};       // Line: straight moves of the eye and the look-at
    Vec3f pivot{};                            // Orbit: round this point (yaw 0 = +z), r and h eased from 0 to 1
    float r0 = 0, r1 = 0, yaw0 = 0, yaw1 = 0, h0 = 0, h1 = 0, atLift = 0;
    float lift = 0;                           // added to the eye's height (the ground and sight-line fix-up)
    float fov0 = 60, fov1 = 60;
    float start = 0, length = 0;              // seconds from the intro's start
    char caption[48] = {}, title[64] = {}, sub[96] = {};
};

struct MapIntroState {
    bool active = false;
    bool tried = false;                       // this countdown already had its chance (played, skipped or not possible)
    float startLeft = 0;                      // the countdown's seconds left when it began; the intro ends when they run out
    float t = 0;                              // seconds into the intro (last game frame)
    int count = 0;
    IntroShot shots[kIntroMaxShots];          // fixed storage: the overlay may read it from the drawing thread
    double skipAt = -1.0;                     // ImGui time the skip began
    double tailAt = -10.0;                    // ImGui time the game's view came back (its fade up from black)
    bool tailFromSkip = false;
    bool music = false, musicFaded = false, musicOwed = false;
    int owedFrames = 0;
    u16 savedSeq = NA_BGM_DISABLED;
};
MapIntroState gIntro;
std::atomic<bool> gIntroTapSkip{false};      // "Skip" tapped on the screen (set by the overlay, taken by the input hook)

bool MapIntroActive() { return gIntro.active; }

float IntroSmooth(float u) { u = std::clamp(u, 0.0f, 1.0f); return u * u * (3.0f - 2.0f * u); }
// Always moving, softer at both ends: a camera dolly, not a lerp.
float IntroEase(float u) { u = std::clamp(u, 0.0f, 1.0f); return 0.35f * u + 0.65f * IntroSmooth(u); }
Vec3f IntroV(float x, float y, float z) { Vec3f v; v.x = x; v.y = y; v.z = z; return v; }
Vec3f IntroLerp(const Vec3f& a, const Vec3f& b, float k) { return IntroV(a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k); }

// The highest of the floor and the water at (x, z); the map's middle height off the map.
float IntroSurfaceAt(float x, float z) {
    float y = gMedianFloorY;
    if (!RawFloorAt(x, z, &y)) y = gMedianFloorY;
    if (OnIsland()) y = std::max(y, royale::fortnite::WaterHeightAt(x, z));
    return y;
}

// What the game's camera would draw this frame (patches/0020's tilt and pull-back included, as Camera_Update applies them).
void IntroGameView(Vec3f* eye, Vec3f* at, float* fov) {
    Camera* cam = GET_ACTIVE_CAM(gPlayState);
    Vec3f a = cam->at, e = cam->eye;
    a.y -= gRoyaleCamPitchDown;
    const float bx = e.x - a.x, by = e.y - a.y, bz = e.z - a.z, bl = std::sqrt(bx * bx + by * by + bz * bz);
    if (gRoyaleCamBack != 0.0f && bl > 1.0f) { const float k = (bl + gRoyaleCamBack) / bl; e = IntroV(a.x + bx * k, a.y + by * k, a.z + bz * k); }
    e.y += gRoyaleCamLift; a.y += gRoyaleCamLift;
    *eye = e; *at = a; *fov = cam->fov;
}

// Where shot s puts the camera u of the way through (0 to 1).
void IntroShotView(const IntroShot& s, float u, Vec3f* eye, Vec3f* at, float* fov) {
    const float e = IntroEase(u);
    *fov = s.fov0 + (s.fov1 - s.fov0) * e;
    if (s.kind == IntroShot::Orbit) {
        const float yaw = s.yaw0 + (s.yaw1 - s.yaw0) * e, r = s.r0 + (s.r1 - s.r0) * e, h = s.h0 + (s.h1 - s.h0) * e;
        *eye = IntroV(s.pivot.x + std::sin(yaw) * r, s.pivot.y + h + s.lift, s.pivot.z + std::cos(yaw) * r);
        *at = IntroV(s.pivot.x, s.pivot.y + s.atLift, s.pivot.z);
        return;
    }
    if (s.kind == IntroShot::Bus) {   // the bus moves (it climbs through the countdown): the shot follows it
        if (!gBusFrame.valid) { IntroGameView(eye, at, fov); return; }
        const Vec3f eye0 = BusWorld(s.eye0.x, s.eye0.y, s.eye0.z), eye1 = BusWorld(s.eye1.x, s.eye1.y, s.eye1.z);
        const Vec3f at0 = BusWorld(s.at0.x, s.at0.y, s.at0.z), at1 = BusWorld(s.at1.x, s.at1.y, s.at1.z);
        *eye = IntroLerp(eye0, eye1, e);
        *at = IntroLerp(at0, at1, e);
        eye->y = std::max(eye->y, IntroSurfaceAt(eye->x, eye->z) + kIntroClearance);
        return;
    }
    *eye = IntroLerp(s.eye0, s.eye1, e);
    eye->y += s.lift;
    *at = IntroLerp(s.at0, s.at1, e);
}

// Raise a shot until its eye clears the ground all the way along, and until the middle of it can see what it looks at.
void IntroSettleShot(IntroShot& s) {
    if (s.kind == IntroShot::Bus) return;
    float need = 0;
    for (int i = 0; i <= 10; i++) {
        Vec3f eye, at; float fov;
        IntroShotView(s, i / 10.0f, &eye, &at, &fov);
        need = std::max(need, IntroSurfaceAt(eye.x, eye.z) + kIntroClearance - eye.y);
    }
    s.lift += need;
    for (int tries = 0; tries < 3; tries++) {
        Vec3f eye, at, hit; float fov;
        IntroShotView(s, 0.5f, &eye, &at, &fov);
        CollisionPoly* poly = nullptr;
        s32 bgId = 0;
        if (!BgCheck_CameraLineTest1(&gPlayState->colCtx, &eye, &at, &hit, &poly, true, true, true, true, &bgId)) break;
        s.lift += 380.0f;   // a hill or a tower in the way: look over it
    }
}

void IntroCopy(char* dst, size_t n, const char* src) { std::snprintf(dst, n, "%s", src ? src : ""); }

// The film for this match: the island's middle, four places and the bus, fitted into `window` seconds. False when there is nothing to show.
bool BuildMapIntro(const royale::HudState& hud, float window) {
    royale::GameClient* client = gSession.Client();
    if (!client) return false;
    const auto& pois = client->Pois();
    royale::Vec2 c{0, 0};
    float radius = 0;
    if (hud.bus.valid && hud.bus.radius > 100.0f) { c = hud.bus.center; radius = hud.bus.radius; }
    else if (!pois.empty()) {
        for (const auto& p : pois) { c.x += p.center.x / pois.size(); c.z += p.center.z / pois.size(); }
        for (const auto& p : pois) radius = std::max(radius, royale::Distance(c, p.center) + p.radius);
    }
    if (radius < 500.0f) return false;
    royale::Rng rng(static_cast<uint64_t>(static_cast<int64_t>(hud.bus.start.x * 7.0f) * 73856093ll) ^
                    static_cast<uint64_t>(static_cast<int64_t>(hud.bus.start.z * 7.0f) * 19349663ll) ^ static_cast<uint64_t>(gMapId) * 0x9E3779B97F4A7C15ull);
    auto unit = [&]() { return static_cast<float>(rng.Unit()); };

    // The places: the big landmark first (each map lists it first), then whichever is furthest from those already picked, a little at random,
    // so the film crosses the island. Ordered from the far side of the bus to the near one, so the last place hands over to the bus.
    const bool bus = hud.bus.valid && gBusFrame.valid;
    const float busShot = bus ? 3.4f : 0.0f, wideShot = 3.4f;
    const float rest = window - busShot - wideShot;
    int want = std::clamp(static_cast<int>(rest / 2.6f), 0, 4);
    std::vector<size_t> picked;
    std::vector<size_t> pool;
    for (size_t i = 0; i < pois.size(); i++)
        if (pois[i].name < royale::kPoiNameTotal) pool.push_back(i);
    if (!pool.empty() && want > 0) picked.push_back(pool[0]);
    while (static_cast<int>(picked.size()) < want && picked.size() < pool.size()) {
        size_t best = pool[0];
        float bestScore = -1;
        for (size_t i : pool) {
            if (std::find(picked.begin(), picked.end(), i) != picked.end()) continue;
            float d = 1e9f;
            for (size_t j : picked) d = std::min(d, royale::Distance(pois[i].center, pois[j].center));
            const float score = d * (0.7f + 0.6f * unit());
            if (score > bestScore) { bestScore = score; best = i; }
        }
        picked.push_back(best);
    }
    want = static_cast<int>(picked.size());
    const royale::Vec2 busAt = hud.bus.valid ? hud.bus.start : c;
    std::sort(picked.begin(), picked.end(), [&](size_t a, size_t b) { return royale::Distance(pois[a].center, busAt) > royale::Distance(pois[b].center, busAt); });

    const float per = want > 0 ? rest / want : 0.0f;
    const float wide = want > 0 ? wideShot : window - busShot;
    if (wide < 2.0f) return false;
    const float groundMid = gMedianFloorY;
    const char* mapName = CurrentMap().name;
    char upper[48];
    IntroCopy(upper, sizeof(upper), mapName);
    for (char* p = upper; *p; ++p) *p = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));

    gIntro.count = 0;
    float at = 0;
    auto add = [&](float length) -> IntroShot& {
        IntroShot& s = gIntro.shots[gIntro.count++];
        s = IntroShot{};
        s.start = at; s.length = length; at += length;
        return s;
    };
    {   // 1. the whole island from high up, turning slowly
        IntroShot& s = add(wide);
        s.kind = IntroShot::Orbit;
        s.pivot = IntroV(c.x, groundMid, c.z);
        s.yaw0 = unit() * 6.2831853f;
        s.yaw1 = s.yaw0 + (unit() < 0.5f ? 0.42f : -0.42f);
        s.r0 = radius * 0.62f; s.r1 = radius * 0.52f;
        s.h0 = radius * 0.44f + 500.0f; s.h1 = radius * 0.38f + 450.0f;
        s.atLift = 0;
        s.fov0 = 64; s.fov1 = 60;
        IntroCopy(s.caption, sizeof(s.caption), "WELCOME TO");
        IntroCopy(s.title, sizeof(s.title), mapName);
        IntroCopy(s.sub, sizeof(s.sub), CurrentMap().blurb);
    }
    const int styleBase = static_cast<int>(unit() * 4.0f);
    for (int k = 0; k < want; k++) {   // 2-5. the places
        const royale::Poi& p = pois[picked[k]];
        const float gy = IntroSurfaceAt(p.center.x, p.center.z);
        const float sc = std::clamp(p.radius / 900.0f, 0.8f, 1.5f);
        float ox = p.center.x - c.x, oz = p.center.z - c.z, ol = std::hypot(ox, oz);
        if (ol < 300.0f) { const float a = unit() * 6.2831853f; ox = std::sin(a); oz = std::cos(a); } else { ox /= ol; oz /= ol; }
        const float sx = oz, sz = -ox;   // to the right of the outward line
        const float side = unit() < 0.5f ? 1.0f : -1.0f;
        auto P = [&](float out, float right, float up) { return IntroV(p.center.x + (ox * out + sx * right * side) * sc, gy + up * sc, p.center.z + (oz * out + sz * right * side) * sc); };
        IntroShot& s = add(per);
        switch ((styleBase + k) % 4) {
            case 0:   // push in from the shore side, coming down
                s.eye0 = P(2400, 350, 1150); s.eye1 = P(1350, 120, 560);
                s.at0 = P(0, 0, 60); s.at1 = P(0, 0, 140);
                s.fov0 = 62; s.fov1 = 54;
                break;
            case 1: { // a slow orbit
                s.kind = IntroShot::Orbit;
                s.pivot = IntroV(p.center.x, gy, p.center.z);
                const float yawOut = std::atan2(ox, oz);
                s.yaw0 = yawOut - 0.75f * side; s.yaw1 = yawOut - 0.15f * side;
                s.r0 = 1750 * sc; s.r1 = 1550 * sc; s.h0 = 900 * sc; s.h1 = 700 * sc; s.atLift = 120;
                s.fov0 = 56; s.fov1 = 56;
                break;
            }
            case 2:   // a tracking pass across it
                s.eye0 = P(1500, -1300, 1250); s.eye1 = P(1200, 1300, 1050);
                s.at0 = P(0, -350, 80); s.at1 = P(0, 350, 80);
                s.fov0 = 58; s.fov1 = 58;
                break;
            default:  // a crane: from low by it up and back, to the island behind
                s.eye0 = P(1050, 200, 260); s.eye1 = P(1700, -150, 1650);
                s.at0 = P(0, 0, 260); s.at1 = P(-700, 0, 0);
                s.fov0 = 54; s.fov1 = 62;
                break;
        }
        IntroCopy(s.caption, sizeof(s.caption), upper);
        IntroCopy(s.title, sizeof(s.title), royale::kPoiNames[p.name]);
        IntroSettleShot(s);
    }
    IntroSettleShot(gIntro.shots[0]);
    if (bus) {   // 6. up to the bus, in its own frame (x right, y up, z forward), then into the seat camera
        IntroShot& s = add(window - at);
        s.kind = IntroShot::Bus;
        s.eye0 = IntroV(900, -150, 1300); s.eye1 = IntroV(250, 450, -950);
        s.at0 = IntroV(0, 150, 0); s.at1 = IntroV(0, 0, 500);
        s.fov0 = 58; s.fov1 = 60;
        IntroCopy(s.caption, sizeof(s.caption), "ALL ABOARD");
        IntroCopy(s.title, sizeof(s.title), "The Moonfall Bus");
        IntroCopy(s.sub, sizeof(s.sub), "Press A over the island to jump");
    } else {
        gIntro.shots[gIntro.count - 1].length += window - at;   // no bus: the last shot runs to the end
    }
    return gIntro.count > 0;
}

// ---- the music ---------------------------------------------------------------------------------------------------------------------
// The title theme on the main music player, then whatever the scene played before (the mod's match music). Only with "Match music: Original";
// the music folder and silence are left alone. If the War Table has the music when the intro ends, it is put back once the menu closes.
void IntroQueueSceneMusic() {
    u16 seq = gIntro.savedSeq;
    if (seq == NA_BGM_DISABLED || (seq & 0xFF) == 0xFF || seq == NA_BGM_TITLE) seq = gSaveContext.seqId;
    if (seq != NA_BGM_DISABLED && (seq & 0xFF) != 0xFF) Audio_QueueSeqCmd((SEQ_PLAYER_BGM_MAIN << 24) | seq);
    else Audio_QueueSeqCmd(0x10000000 | (SEQ_PLAYER_BGM_MAIN << 24) | 0xFF);
}
void IntroMusicStart() {
    gIntro.music = gIntro.musicFaded = false;
    if (gMusicMode != 0 || WarMenuOpen() || !DebugOn(kDbgMusic)) return;
    gIntro.savedSeq = gActiveSeqs[SEQ_PLAYER_BGM_MAIN].seqId;
    Audio_QueueSeqCmd((SEQ_PLAYER_BGM_MAIN << 24) | NA_BGM_TITLE);
    gIntro.music = true;
    gIntro.musicOwed = false;
}
void IntroMusicFade() {
    if (!gIntro.music || gIntro.musicFaded || WarMenuOpen()) return;
    gIntro.musicFaded = true;
    Audio_QueueSeqCmd(0x10000000 | (SEQ_PLAYER_BGM_MAIN << 24) | (0x0C << 16) | 0xFF);   // stop, fading out
}
void IntroMusicEnd() {
    if (!gIntro.music) return;
    gIntro.music = false;
    if (WarMenuOpen() || !InGame()) { gIntro.musicOwed = InGame(); gIntro.owedFrames = 0; return; }
    IntroQueueSceneMusic();
}

// ---- running it ----------------------------------------------------------------------------------------------------------------------
void EndMapIntro(bool tail) {
    if (!gIntro.active) return;
    gIntro.active = false;
    gRoyaleCamOverride = 0;
    gIntro.tailAt = tail ? ImGui::GetTime() : -10.0;
    gIntro.tailFromSkip = gIntro.skipAt >= 0.0;
    gIntro.skipAt = -1.0;
    gIntroTapSkip = false;
    IntroMusicEnd();
    Crumb("map intro ended%s", gIntro.tailFromSkip ? " (skipped)" : "");
}

bool MapIntroWanted(const royale::HudState& hud) {
    static const bool autotest = std::getenv("ROYALE_AUTOTEST") != nullptr;   // the playtest scripts expect the bus camera in the countdown
    return !autotest && MapOption("MapIntro", true) && royale::IsAuthoredMap(gMapId) && OnIsland() && InField() && gSession.Client() &&
           gPlayState->transitionTrigger == TRANS_TRIGGER_OFF && gPlayState->transitionMode == TRANS_MODE_OFF && hud.haveSelf;
}

void StartMapIntro(const royale::HudState& hud) {
    gIntro.tried = true;
    const float window = hud.countdownLeft;
    if (window < kIntroMinSeconds || !BuildMapIntro(hud, window)) return;
    gIntro.active = true;
    gIntro.startLeft = window;
    gIntro.t = 0;
    gIntro.skipAt = -1.0;
    gIntro.tailAt = -10.0;
    gIntroTapSkip = false;
    IntroMusicStart();
    Crumb("map intro: %d shots over %.1f s", gIntro.count, window);
}

// Every game frame (OnGameFrameUpdate): start, run or end the intro, and put the music back if the menu was holding it.
void UpdateMapIntro(const royale::HudState& hud, bool joined) {
    if (gIntro.musicOwed && !gIntro.active) {
        if (!InGame() || (joined && hud.state == royale::MatchState::Lobby)) gIntro.musicOwed = false;
        else if (WarMenuOpen()) gIntro.owedFrames = 0;
        else if (++gIntro.owedFrames > royale::kTickHz / 2) { gIntro.musicOwed = false; IntroQueueSceneMusic(); }   // after the menu's own restore
    }
    const bool countdown = joined && hud.state == royale::MatchState::Countdown;
    if (!countdown) gIntro.tried = false;
    if (gIntro.active) {
        if (!countdown || !InField() || hud.countdownLeft <= 0.0f) { EndMapIntro(joined && InField()); return; }
        gIntro.t = gIntro.startLeft - hud.countdownLeft;
        if (gIntro.skipAt >= 0.0 && ImGui::GetTime() - gIntro.skipAt >= kIntroSkipFade) { EndMapIntro(true); return; }
        if (gIntro.t >= gIntro.startLeft - 1.6f) IntroMusicFade();
        // This frame's view: the shot under way, blended into the seat camera at the very end.
        int i = 0;
        while (i + 1 < gIntro.count && gIntro.t >= gIntro.shots[i + 1].start) i++;
        const IntroShot& s = gIntro.shots[i];
        Vec3f eye, at; float fov;
        IntroShotView(s, s.length > 0 ? (gIntro.t - s.start) / s.length : 1.0f, &eye, &at, &fov);
        const float hand = IntroSmooth((gIntro.t - (gIntro.startLeft - kIntroHandover)) / kIntroHandover);
        if (hand > 0.0f && s.kind == IntroShot::Bus) {
            Vec3f ge, ga; float gf;
            IntroGameView(&ge, &ga, &gf);
            eye = IntroLerp(eye, ge, hand); at = IntroLerp(at, ga, hand); fov += (gf - fov) * hand;
        }
        gRoyaleCamEye = eye;
        gRoyaleCamAt = at;
        gRoyaleCamFov = fov;
        gRoyaleCamOverride = 1;
        return;
    }
    gRoyaleCamOverride = 0;
    if (!countdown || gIntro.tried) return;
    if (!royale::IsAuthoredMap(gMapId) || hud.countdownLeft < kIntroMinSeconds) { gIntro.tried = true; return; }
    if (hud.countdownLeft > royale::kCountdownSec - kSplashSeconds + kIntroEarly) return;   // the loading splash is still up
    if (MapIntroWanted(hud)) StartMapIntro(hud);
}

// Before the game reads the pad (OnEmoteWheelInput): B skips, Start still opens the menu, nothing else reaches the game.
bool MapIntroInput(Input& in, const royale::HudState& hud) {
    (void)hud;
    if (!gIntro.active) { gIntroTapSkip = false; return false; }
    const bool tapped = gIntroTapSkip.exchange(false);
    if (gIntro.skipAt < 0.0 && ((in.press.button & BTN_B) || tapped)) {
        gIntro.skipAt = ImGui::GetTime();
        UiSfx(NA_SE_SY_DECIDE);
        IntroMusicFade();
    }
    in.cur.button &= BTN_START; in.press.button &= BTN_START; in.rel.button &= BTN_START;
    in.cur.stick_x = in.cur.stick_y = in.rel.stick_x = in.rel.stick_y = in.press.stick_x = in.press.stick_y = 0;
    return true;
}

// ---- drawing it: letterbox, dips to black, the place cards, the skip hint ---------------------------------------------------------------
// Text with its letters spread out, centred on cx. Each letter rises into place a moment after the one before it (reveal 0 to 1 over the line).
// Shrunk to fit maxW when it would not.
float IntroTracked(ImDrawList* dl, ImFont* font, float size, float cx, float y, float spacing, ImU32 col, const char* text, float reveal, bool glow, float maxW) {
    const char* end = text + std::strlen(text);
    float width = 0;
    int letters = 0;
    for (const char* p = text; p < end;) {
        const char* q = p + 1;
        while (q < end && (static_cast<unsigned char>(*q) & 0xC0) == 0x80) q++;
        width += font->CalcTextSizeA(size, FLT_MAX, 0.0f, p, q).x + spacing;
        letters++;
        p = q;
    }
    width -= spacing;
    if (width > maxW && width > 1.0f) { const float k = maxW / width; size *= k; spacing *= k; width = maxW; }
    float x = cx - width * 0.5f;
    const int alpha = static_cast<int>(col >> 24);
    int k = 0;
    for (const char* p = text; p < end; k++) {
        const char* q = p + 1;
        while (q < end && (static_cast<unsigned char>(*q) & 0xC0) == 0x80) q++;
        const float w = font->CalcTextSizeA(size, FLT_MAX, 0.0f, p, q).x;
        const float lag = letters > 1 ? static_cast<float>(k) / (letters - 1) : 0.0f;
        const float r = IntroSmooth((reveal * 1.6f - lag * 0.6f));
        const int a = static_cast<int>(alpha * r);
        if (a > 0) {
            const float dy = (1.0f - r) * size * 0.18f;
            if (glow) {   // a soft dark halo, as the game's own title cards sit on
                for (int i = 0; i < 4; i++) {
                    const float ox = (i & 1 ? 1.0f : -1.0f) * size * 0.05f, oy = (i & 2 ? 1.0f : -1.0f) * size * 0.05f;
                    dl->AddText(font, size, ImVec2(x + ox, y + dy + oy), IM_COL32(8, 6, 2, a * 70 / 255), p, q);
                }
            }
            dl->AddText(font, size, ImVec2(x + size * 0.05f, y + dy + size * 0.06f), IM_COL32(10, 8, 4, a * 200 / 255), p, q);
            dl->AddText(font, size, ImVec2(x, y + dy), (col & 0x00FFFFFF) | (static_cast<ImU32>(a) << 24), p, q);
        }
        x += w + spacing;
        p = q;
    }
    return width;
}

// A small Triforce, the cards' ornament.
void IntroTriforce(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    auto tri = [&](float x, float y) { dl->AddTriangleFilled(ImVec2(x, y - s * 0.5f), ImVec2(x - s * 0.58f, y + s * 0.5f), ImVec2(x + s * 0.58f, y + s * 0.5f), col); };
    tri(c.x, c.y - s * 0.5f);
    tri(c.x - s * 0.58f, c.y + s * 0.5f);
    tri(c.x + s * 0.58f, c.y + s * 0.5f);
}

// The place-name card: a gold caption between two fading rules with a Triforce above, the name in large cream letters on a soft dark band,
// and a line under it on the opening and closing shots. In and out with the shot.
void DrawIntroCard(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const IntroShot& s, float ts, float barH) {
    const float in = IntroSmooth((ts - 0.35f) / 0.6f), out = IntroSmooth((s.length - ts - 0.3f) / 0.45f);
    const float a = std::min(in, out);
    if (a <= 0.0f || !s.title[0]) return;
    auto A = [&](int v) { return static_cast<int>(v * a); };
    const float nameSize = 46.0f * scale, capSize = 16.0f * scale, subSize = 18.0f * scale;
    const float y = ds.y - barH - (s.sub[0] ? 150.0f : 122.0f) * scale;
    const float cx = ds.x * 0.5f;
    const float maxW = ds.x * 0.86f;
    // the band behind the words, dark in the middle and gone at both ends
    const float bandW = std::min(ds.x * 0.42f, 520.0f * scale), bandTop = y - 6.0f * scale, bandBot = y + capSize + nameSize + (s.sub[0] ? subSize + 22.0f : 12.0f) * scale + 14.0f * scale;
    dl->AddRectFilledMultiColor(ImVec2(cx - bandW, bandTop), ImVec2(cx, bandBot), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, A(120)), IM_COL32(0, 0, 0, A(120)), IM_COL32(0, 0, 0, 0));
    dl->AddRectFilledMultiColor(ImVec2(cx, bandTop), ImVec2(cx + bandW, bandBot), IM_COL32(0, 0, 0, A(120)), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, A(120)));
    // the caption and its rules
    const ImU32 gold = IM_COL32(247, 215, 116, A(255));
    const float capW = s.caption[0] ? IntroTracked(dl, font, capSize, cx, y, 3.5f * scale, gold, s.caption, in, false, maxW) : 60.0f * scale;
    const float ruleY = y + capSize * 0.55f, gap = 14.0f * scale, ruleLen = (90.0f + 90.0f * in) * scale;
    dl->AddRectFilledMultiColor(ImVec2(cx - capW * 0.5f - gap - ruleLen, ruleY - 0.8f * scale), ImVec2(cx - capW * 0.5f - gap, ruleY + 0.8f * scale),
                                IM_COL32(247, 215, 116, 0), IM_COL32(247, 215, 116, A(230)), IM_COL32(247, 215, 116, A(230)), IM_COL32(247, 215, 116, 0));
    dl->AddRectFilledMultiColor(ImVec2(cx + capW * 0.5f + gap, ruleY - 0.8f * scale), ImVec2(cx + capW * 0.5f + gap + ruleLen, ruleY + 0.8f * scale),
                                IM_COL32(247, 215, 116, A(230)), IM_COL32(247, 215, 116, 0), IM_COL32(247, 215, 116, 0), IM_COL32(247, 215, 116, A(230)));
    for (int side = -1; side <= 1; side += 2) {   // a little diamond where each rule meets the caption
        const float dx = cx + side * (capW * 0.5f + gap * 0.55f), d = 3.2f * scale;
        dl->AddQuadFilled(ImVec2(dx, ruleY - d), ImVec2(dx + d, ruleY), ImVec2(dx, ruleY + d), ImVec2(dx - d, ruleY), gold);
    }
    IntroTriforce(dl, ImVec2(cx, y - 13.0f * scale), 7.0f * scale, IM_COL32(247, 215, 116, A(220)));
    // the name
    const float ny = y + capSize + 6.0f * scale;
    IntroTracked(dl, font, nameSize, cx, ny, 1.8f * scale, IM_COL32(255, 249, 232, A(255)), s.title, IntroSmooth((ts - 0.45f) / 0.9f), true, maxW);
    if (s.sub[0]) {
        const float sa = IntroSmooth((ts - 1.0f) / 0.6f) * a;
        if (sa > 0.0f) IntroTracked(dl, font, subSize, cx, ny + nameSize + 10.0f * scale, 0.6f * scale, IM_COL32(214, 226, 236, static_cast<int>(235 * sa)), s.sub, 1.0f, false, maxW);
    }
}

// From DrawOverlay, under the loading splash. True while the intro has the screen (the "MATCH STARTS IN" line stays away).
bool DrawMapIntro(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const royale::HudState& h) {
    const double now = ImGui::GetTime();
    const float tail = static_cast<float>(now - gIntro.tailAt);
    if (!gIntro.active) {   // the game's view fading up from the last black, the bars sliding away
        if (tail >= 0.0f && tail < kIntroTailFade) {
            const float k = 1.0f - IntroSmooth(tail / kIntroTailFade);
            const float barH = ds.y * 0.105f * k;
            dl->AddRectFilled(ImVec2(0, 0), ImVec2(ds.x, barH), IM_COL32(0, 0, 0, 255));
            dl->AddRectFilled(ImVec2(0, ds.y - barH), ds, IM_COL32(0, 0, 0, 255));
            dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, static_cast<int>(255 * k)));
        }
        return false;
    }
    const float t = h.state == royale::MatchState::Countdown ? gIntro.startLeft - h.countdownLeft : gIntro.t;
    const float total = gIntro.startLeft;
    // letterbox, sliding in at the start
    const float barH = ds.y * 0.105f * IntroSmooth(t / 0.8f);
    // the shot on screen and how far into it
    int i = 0;
    while (i + 1 < gIntro.count && t >= gIntro.shots[i + 1].start) i++;
    const IntroShot& s = gIntro.shots[i];
    DrawIntroCard(dl, font, ds, scale, s, t - s.start, barH);
    // black: the opening, each cut, the closing moment and a skip
    float black = t < kIntroOpenBlack ? 1.0f : 1.0f - IntroSmooth((t - kIntroOpenBlack) / kIntroOpenFade);
    for (int c = 1; c < gIntro.count; c++) black = std::max(black, 1.0f - std::fabs(t - gIntro.shots[c].start) / kIntroCutHalf);
    black = std::max(black, IntroSmooth((t - (total - kIntroEndFade)) / kIntroEndFade));
    if (gIntro.skipAt >= 0.0) black = std::max(black, IntroSmooth(static_cast<float>(now - gIntro.skipAt) / kIntroSkipFade));
    if (black > 0.0f) dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, static_cast<int>(255 * std::min(1.0f, black))));
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(ds.x, barH), IM_COL32(0, 0, 0, 255));
    dl->AddRectFilled(ImVec2(0, ds.y - barH), ds, IM_COL32(0, 0, 0, 255));
    // in the bottom bar: the countdown on the left, the skip on the right (tap it, or press B)
    if (barH > 20.0f * scale) {
        const float hintA = std::min(1.0f, IntroSmooth((t - 1.0f) / 0.6f)) * (gIntro.skipAt >= 0.0 ? 0.0f : 1.0f);
        if (hintA > 0.0f) {
            const int ia = static_cast<int>(200 * hintA);
            const float size = 17.0f * scale, midY = ds.y - barH * 0.5f;
            const std::string left = "Match starts in " + std::to_string(static_cast<int>(std::ceil(std::max(0.0f, h.countdownLeft))));
            dl->AddText(font, size, ImVec2(28.0f * scale, midY - size * 0.5f), IM_COL32(214, 226, 236, ia), left.c_str());
            const char* skip = "Skip";
            const float w = font->CalcTextSizeA(size, FLT_MAX, 0.0f, skip).x, r = 11.0f * scale;
            const float x1 = ds.x - 28.0f * scale, x0 = x1 - w - r * 2.0f - 8.0f * scale;
            DrawPadGlyph(dl, font, ImVec2(x0 + r, midY), r, royale::controls::Glyph::B, ia);
            dl->AddText(font, size, ImVec2(x1 - w, midY - size * 0.5f), IM_COL32(244, 233, 202, ia), skip);
            if (UiTapIn(ImVec2(x0 - 16.0f * scale, ds.y - barH), ImVec2(ds.x, ds.y))) gIntroTapSkip = true;
        }
    }
    return true;
}
