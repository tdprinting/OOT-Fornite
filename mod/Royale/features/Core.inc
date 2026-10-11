// ---- where is the local player? -----------------------------------------------------------------------------------------

bool InGame() {
    return gPlayState != nullptr && GET_PLAYER(gPlayState) != nullptr && gSaveContext.fileNum >= 0 && gSaveContext.fileNum <= 2 &&
           gSaveContext.gameMode == GAMEMODE_NORMAL;
}
int gMapId = 0;   // which place this match is played in (from the server, see HudState::mapId)
bool gFortniteSandbox = false;   // ...and its ground is the Sandbox test map's (shared/sandbox_terrain.h) rather than the island's
bool gFortniteScene = false;   // the scene now loaded is Hyrule Field with the Fortnite map's collision (see "the Fortnite map" below)
// State the cloth and weather code shares (the weather is drawn much further down; the glider and the cap need the wind early).
royale::MatchState gStateNow = royale::MatchState::Lobby;   // the match state as of this frame (the glider only shows during the skydive)
int gHatHookCalls = 0;       // how many times the game has asked us about the cap (shown in the menu, to prove the hook is wired)
float gHatLastSwing = 0.0f;  // the last swing applied, in degrees
int gGliderClothFrames = 0;  // frames the cloth glider was drawn
float gStormWeather = 0.0f;         // 0 to 1: how far you are into the storm's dark weather
double gBoltFlashUntil = 0;   // a lightning bolt has landed nearby: the screen flashes until then
float gWeatherBlend = 0.0f;
bool gSeasonAnnounced = false;
royale::Weather gWeatherShown;
float gWeatherDensity = 1.0f;       // the local option, 0 to 2
royale::Vec2 gSignPos = {};         // where the sign in the middle of the map stands (once found)
bool gSignKnown = false;
float gClothScale = 1.0f;           // the local option: cloth and wind physics on the cap, tunic, sheath and glider, 0 (off) to 2
bool gWindOn = true;                // the local option: a breeze that moves cloth, grass and trees (off: still air)
float gWindScale = 1.0f;            // how strong the breeze is, 0 to 2 (the slider)
bool gWindStreaks = true;           // the local option: streaks in the air that show which way the wind blows
bool gSkyOot = true;                // the local option: the sky modelled in Blender in the Ocarina of Time style (off = the plain procedural sky)
bool gSkyStars = true;              // the local option: stars in the night sky
bool gSkyBodies = true;             // the local option: the sun and the moon
float gSkyClouds = 1.0f;            // the local option: how many clouds, 0 (none) to 2
float gFogAmount = 1.0f;            // the local option: how thick the fog banks are, 0 (none) to 2
bool gGroundPuddles = true, gGroundSnow = true, gGroundFrost = true, gGroundLeaves = true;   // the local options: which ground patches are drawn
bool gGroundMerge = true;           // the local option: patches that touch run into one bigger patch
bool gGroundTrails = true;          // the local option: snow keeps footprints and landing craters
float gGroundDeform = 1.0f;         // the local option: how far snow and puddles shape the ground, 0 (flat) to 2 (deep prints, high banks)
float gGroundAmount = 1.0f;         // the local option: how many ground patches the weather and the seasons leave, 0 (none) to 2
bool gHitFlash = true, gHitParticles = true, gHitBlood = true, gHitScaled = true, gHitOnSelf = true, gHitReact = true;   // the local options: hit feedback (see "hit feedback")
float gHitAmount = 1.0f;            // the local option: how many hit particles, 0 (none) to 2
int gWaterDetail = 1;               // the local option: how fine the water surface is, 0 low, 1 normal, 2 high
float gWaterWaves = 1.0f;           // the local option: how high the swell is, 0 (flat) to 2
bool gWaterFxOn = true;             // the local option: the game's own splashes, ripples and bubbles
bool gWaterLife = true;             // the local option: more bubbles, spray, vents and leaping fish (more of the game's own sprites)
bool gWaterWakes = true;            // the local option: swimmers, bots and carts push the surface and leave wakes
bool gWaterRain = true;             // the local option: rain makes rings on the water
bool gWaterSkyOn = true;            // the local option: the sky, the sun and the moon in the water
bool gWaterBodiesRefl = true;       // the local option: reflections of whoever is at the water
bool gWaterUnder = true;            // the local option: the underwater look
bool gWaterCurrent = true;          // the local option: a current carries a swimming player
float gWaterChop = 0.6f;            // the local option: the waves' shape, 0 round to 1 sharp, choppy crests
float gWaterClarity = 1.0f;         // the local option: how far you see into the water, 0 (murky) to 2 (glass clear)
bool gWaterGlints = true;           // the local option: the sparkling ripple texture on the surface
float gWaterGlintAmt = 1.0f;        // and how strong it is, 0 to 2
bool gWaterCaustics = true;         // the local option: caustics, the dancing light on the floor under shallow water
float gWaterCausticAmt = 1.0f;      // and how bright, 0 to 2
bool gWaterFoamTex = true;          // the local option: textured foam on shores, wakes and whitecaps
float gWaterFoamAmt = 1.0f;         // and how much, 0 to 2
bool gWaterRipples = true;          // the local option: the ripple simulation round the player (rings that spread, cross and interfere)
float gWaterUnderAmt = 1.0f;        // the local option: how strong the underwater look is, 0 to 2
bool gWaterSparkle = true;          // the local option: sparse bright sparkle on the sun's path instead of the even glint net
bool gWaterStreaks = true;          // the local option: wind-blown foam streaks and whitecaps on the swell (Medium detail and up)
bool gWaterClouds = true;           // the local option: clouds mirrored in the water (Medium detail and up)
bool gWaterBands = true;            // the local option: rolling swell-band shading between the grid's points (High detail only)
float gWaterHaze = 1.0f;            // the local option: how much the far water melts into the sky, 0 to 2
float gWaterTone = 1.0f;            // the local option: how deep the troughs go and how rich the colour is, 0 to 2
bool gTornadoOn = false;            // the easter egg: a tornado wanders the map (local only, never saved)
int gMusicMode = 0;                 // the local option: match music, 0 the game's, 1 random songs from the music folder, 2 none

// The wind, worked out from the weather: a breeze always, more in rain, thunder, snow, ash and sand and in the storm itself, gusting and slowly
// turning. Returns the wind in world units per second; `strength` is 0 to 1.
// Hireable allies as drawn (the actors and their smoothing are with the other ally code, further down).
struct AllyActor {
    Actor* actor = nullptr;
    ActorFunc origDestroy = nullptr;
    int kind = 0;
    float x = 0, z = 0, tx = 0, tz = 0;
    int16_t rot = 0, trot = 0;
    float hp = 1.0f, shownHp = 1.0f;
    uint16_t owner = royale::net::kNoPlayer16;
    uint16_t lastOwner = royale::net::kNoPlayer16;
    float actAge = 10.0f;      // seconds since it attacked or healed
    bool init = false;
    // Our own model (shared/ally_anim.h): its animation, and what it is doing
    royale::allymodel::Animator anim;
    float speed = 0.0f;        // ground speed, units a second, smoothed
    bool walking = false;
    int pending = 0;           // 1 an attack, 2 a heal, started on the next update
    bool leftFist = false;     // the Goron punches with each fist in turn
    float busy = 0.0f;         // a one-shot clip (attack, hurt, cheer, getting up) plays until this runs out
    bool resting = false;      // a free ally sits (a Goron curls into a ball) when nobody is near
    float lonely = 0.0f;       // seconds with no player close
    bool rolling = false;      // a Goron hurrying after its owner rolls
    float rollAngle = 0.0f;
    float slowFor = 0.0f;
    float age = 0.0f;
    // Jones's own Blender rig and local reactions; the four original allies keep their animator.
    float phase=0,walkW=0,bob=0;
    float greetAge=10,hurtAge=10;
    double nextGreetingAt=0;
    bool attackSnapshotActive=false;
    uint8_t weapon=0;
    float shotX=0,shotY=0,shotZ=0;
    bool hasShot=false;
    // Peter (same rigid-rig family as Jones): his idle phone call with Joe.
    float callAge=-1,stillFor=0;   // seconds into the call (-1: not on the phone); seconds standing idle
    double nextCallAt=0;
    float cheerAge=10,quipAge=10;  // "Freakin' sweet!" on hire; the quip bubble
    int quip=-1;
    uint8_t attackVariant=0;        // which of his moves is playing
    unsigned attackCount=0,hireCount=0;
    bool customCall=false;          // playing the player's own call.wav
};
std::unordered_map<uint8_t, AllyActor> gAllies;
std::unordered_map<const Actor*, uint8_t> gAllyOf;


void WindNow(float* wx, float* wz, float* strength) {
    if (!gWindOn || gWindScale <= 0.01f) { *wx = *wz = *strength = 0.0f; return; }
    const float t = static_cast<float>(ImGui::GetTime());
    float speed = 62.0f;   // a steady breeze even on a fine day
    const float amount = gWeatherBlend * gWeatherShown.Strength();
    switch (gWeatherShown.sky) {
        case royale::Sky::Rain: speed += 120.0f * amount; break;
        case royale::Sky::Thunder: speed += 230.0f * amount; break;
        case royale::Sky::Snow: speed += 90.0f * amount; break;
        case royale::Sky::Sandstorm: speed += 330.0f * amount; break;
        case royale::Sky::Ash: speed += 150.0f * amount; break;
        case royale::Sky::Fog: speed += 15.0f * amount; break;
        default: break;
    }
    speed += 170.0f * gStormWeather;
    speed *= 0.7f + 0.3f * std::sin(t * 0.9f) + 0.16f * std::sin(t * 2.3f + 1.0f) + 0.08f * std::sin(t * 5.1f + 2.0f);   // gusts come and go
    // Squalls: rain and thunder come in sudden surges of wind, rolling in every half minute or so.
    if (gWeatherShown.sky == royale::Sky::Rain || gWeatherShown.sky == royale::Sky::Thunder) {
        const float sq = std::max(0.0f, std::sin(t * 0.21f + 1.3f)), sq4 = sq * sq * sq * sq;
        speed += (gWeatherShown.sky == royale::Sky::Thunder ? 210.0f : 110.0f) * amount * sq4;
    }
    speed *= gWindScale;
    const float dir = 0.4f + t * 0.04f + static_cast<float>(static_cast<int>(gWeatherShown.season)) * 1.1f;
    *wx = std::cos(dir) * speed;
    *wz = std::sin(dir) * speed;
    *strength = std::clamp(speed / 260.0f, 0.0f, 1.0f);
}

const royale::MapDef& CurrentMap() { return royale::MapOf(gMapId); }
// The Fortnite map is played in Hyrule Field's scene, so being in that scene is not enough: it must be the version loaded with the island's collision
// (and the other way round for the real field). Outside a lobby nobody has picked a map, so the scene alone counts.
bool InField() {
    return InGame() && gPlayState->sceneNum == CurrentMap().scene &&
           (!gSession.Joined() || (royale::IsIslandMap(gMapId) == gFortniteScene && (!gFortniteScene || gMapId == royale::fortnite::gTerrainMapId)));
}
int WaitingAreaIndex() { return std::clamp(CVarGetInteger(CVAR_SETTING("Royale.WaitingArea"),0),0,royale::lobby::kAreaCount-1); }
bool InWaitingRoom() { return InGame() && royale::lobby::IsWaitingScene(gPlayState->sceneNum); }
bool InSelectedWaitingRoom() { return InGame() && gPlayState->sceneNum==royale::lobby::WaitingArea(WaitingAreaIndex()).scene; }

// ---- the regular game ----------------------------------------------------------------------------------------------------------
// A save played as Ocarina of Time: not the Battle Royale quest and no lobby joined. Our water, sky, weather, fog and wind, shadows and lights,
// ground patches and companions can run in every scene of it (the "Regular game" settings page switches each one); a match never looks at these.
enum RegularFeature { kRegWater, kRegHideWater, kRegSky, kRegWeather, kRegAir, kRegShadows, kRegGround, kRegFlora, kRegCompanions, kRegAlly1, kRegAlly2,
                      kRegItemMaps, kRegCount };
struct RegularSetting { const char* key; int fallback; };
constexpr RegularSetting kRegularSettings[kRegCount] = {
    { "RegularWater", 1 },      // our water on every water box
    { "HideGameWater", 1 },     // the game's own water surfaces hidden under ours (in matches too)
    { "RegularSky", 1 },        // the Ocarina-style sky dome, stars, sun and moon (outdoors)
    { "RegularWeather", 1 },    // 0 none, 1 changes by itself, 2 + Sky: always that weather
    { "RegularAir", 1 },        // fog banks and wind streaks (outdoors)
    { "RegularShadows", 1 },    // dynamic shadows and lights
    { "RegularGround", 1 },     // puddles, snow, frost, leaves and blossom with the weather and the season
    { "RegularFlora", 0 },      // our grass and trees (off: the game's scenes have their own)
    { "RegularCompanions", 1 }, // the companion picked on the Character page follows you
    { "RegularAlly1", 0 },      // 0 none, 1 + AllyKind: an ally who follows you and fights for you
    { "RegularAlly2", 0 },
    { "RegularItemMaps", 1 },   // the sword and shield sunlight detail
};
int gRegular[kRegCount];
std::string gRegularKeys[kRegCount];
void RefreshRegularSettings() {
    for (int i = 0; i < kRegCount; i++) {
        if (gRegularKeys[i].empty()) gRegularKeys[i] = std::string(CVAR_SETTING("Royale.")) + kRegularSettings[i].key;
        gRegular[i] = CVarGetInteger(gRegularKeys[i].c_str(), kRegularSettings[i].fallback);
    }
}
bool InRegularGame() { return InGame() && !gSession.Joined() && gSaveContext.ship.quest.id != QUEST_BATTLEROYALE; }
bool RegularOn(int feature) {
    static bool loaded = false;
    if (!loaded) { RefreshRegularSettings(); loaded = true; }
    return InRegularGame() && gRegular[feature] != 0;
}
// A scene with a sky over it (fields, towns, lakes): not a house, a shop, a grotto or a dungeon.
bool SceneOutdoors() {
    return gPlayState != nullptr && gPlayState->skyboxId != SKYBOX_NONE && !gPlayState->envCtx.skyboxDisabled;
}
// A cutscene is playing: companions step aside and nothing new is spawned.
bool InCutscene() { return gPlayState != nullptr && gPlayState->csCtx.state != CS_STATE_IDLE; }
// Where a world effect runs: on a match's map as before, and in the regular game wherever its switch is on (outdoors only, for the sky ones).
bool WorldFxOn(int feature, bool outdoorsOnly) {
    if (InRegularGame()) return RegularOn(feature) && (!outdoorsOnly || SceneOutdoors());
    return InField();
}
// The actor our own things borrow (its update, draw and destroy are replaced): a small rock, as always in a match; in the dungeons and houses whose
// scene does not load the rock's object (it lives in the field and town objects), an invisible marker whose object every scene has.
Actor* SpawnStandIn(PlayState* play, float x, float y, float z, s16 rx, s16 ry, s16 rz, s16 params, bool) {
    const bool rock = Object_GetIndex(&play->objectCtx, OBJECT_GAMEPLAY_FIELD_KEEP) >= 0;
    return Actor_Spawn(&play->actorCtx, play, rock ? ACTOR_EN_ISHI : ACTOR_EN_IT, x, y, z, rx, ry, rz, params, false);
}
// Any world effect at all this frame (the stand-in actor that draws them is needed).
bool AnyWorldFx() {
    if (!InRegularGame()) return InField();
    for (int f : { kRegWater, kRegSky, kRegWeather, kRegAir, kRegShadows, kRegGround, kRegFlora, kRegAlly1, kRegAlly2 }) if (RegularOn(f)) return true;   // (allies' shots fly there too)
    return false;
}

const char* SceneName(int scene) {
    for (const auto& area:royale::lobby::kAreas) if(area.scene==scene)return area.name;
    static char other[24];
    for (int i = 0; i < royale::kMapCount; i++) if (scene == royale::kMaps[i].scene) return royale::kMaps[i].name;
    if (scene == SCENE_TEMPLE_OF_TIME) return "Temple of Time (waiting room)";
    std::snprintf(other, sizeof(other), "scene 0x%02X", scene);
    return other;
}

// Scene travel. Moves the player with the same mechanism the game's own warp console command uses.
int gTravelCooldown = 0; // game frames (20 per second) before another travel request is allowed

bool gOurTravel = false; // a scene change we asked for ourselves (see SealExits)
void Trace(const char* step);   // the crash breadcrumb trail, defined with the music code
// The feature that is running right now, for the crash report. Every feature's per-frame code starts with Feat("name"), so when the game
// dies the report names the last feature that started (a string literal, so the crash handler can read it safely).
const char* volatile gFeature = "(none yet)";
inline void Feat(const char* name) { gFeature = name; }

// ---- graphics layers ----------------------------------------------------------------------------------------------------------
// The game's display-list buffers are small and fixed: "Zelda 0" (opaque) holds about 195 KB of commands growing up and Graph_Alloc's vertices and
// matrices growing down, "Zelda 1" (translucent) only 4096 commands. They were sized for the original game. Our sky alone writes over 3000
// translucent commands on a clear night; add clouds, fog banks, the storm wall, weather specks and water and the translucent buffer overflows. When
// that happens the game throws the whole frame away, and when the opaque one runs low our old allocator refused memory and a layer skipped the frame:
// both read as flicker in the sky, fog, grass, storm wall and weather. (Before that guard, the two ends met and the graphics thread ran garbage:
// the match-start crash in gfx_modify_vtx_handler.)
//
// So every effect now draws as a layer (shared/graphics_layers.h has the rules):
//   * its own command streams in a pool the mod owns: a translucent window and an opaque stream, with its vertices and matrices behind them. While a
//     layer is open the game's two buffers point into the pool, so the drawing code is unchanged (POLY_OPA_DISP, POLY_XLU_DISP, MATRIX_NEWMTX and
//     FrameAlloc all land there). The game's buffers get one "draw this list" command per stream.
//   * a clean state at both ends: each stream starts and ends with the game's standard setup, so no layer's render mode, fog or culling leaks into
//     the next layer or into the game's own drawing.
//   * a fixed slot in the drawing order (Projectile_Draw), and the sky its own pass behind the world (DrawBackdrop, patches/0022).
//   * one shared budget: the pool grows to what busy frames need (never past a cap) and each layer's window is sized from what it used before. A
//     layer that still does not fit is left out of that one frame and the next frame is given more room; it never takes the game's frame with it.
enum class GfxLayerId : int { Sky, Foliage, Scenery, Ground, Water, Storm, Fog, Fireflies, Tornado, Weather, Wind, Characters, Shadows, Terrain, Count };
constexpr int kGfxLayerCount = static_cast<int>(GfxLayerId::Count);
constexpr const char* kGfxLayerNames[kGfxLayerCount] = { "Sky", "Grass and trees", "Island scenery", "Ground patches", "Water", "Storm wall", "Fog banks",
                                                          "Fireflies", "Tornado", "Weather specks", "Wind streaks", "Characters", "Shadows and light", "Map terrain and buildings" };
// Each layer's translucent window to start with and never go below, in KB: enough for its busiest normal frame (the sky on an overcast night with the
// Milky Way is about 5500 commands, 88 KB), so a layer is only ever cut short by something unusual. Solid drawing has no window: it takes what is free.
constexpr size_t kGfxLayerXluKB[kGfxLayerCount] = { 112, 4, 4, 32, 128, 16, 16, 8, 12, 16, 8, 4, 16, 4 };

struct GfxLayerStats {
    size_t xlu = 0, opa = 0, data = 0;   // bytes used this frame
    size_t window = 0;                   // the translucent window it is given (0: not drawn yet, start from kGfxLayerXluKB)
    uint32_t drops = 0;                  // frames it did not fit, or was cut short (since the game started)
};

struct GfxPool {
    std::unique_ptr<uint8_t[]> raw[2];
    uint8_t* base[2] = { nullptr, nullptr };
    size_t size[2] = { 0, 0 };
    size_t want = royale::gfxlayers::kPoolMin;
    int cur = 0;
    uint32_t frame = 0;
    bool started = false;
    bool trim[2] = {false, false};
    royale::gfxlayers::Span free;        // what is left in the current frame's half
    size_t usedLast = 0, peak = 0;       // bytes the last frame used; the most any recent frame used
    bool overflowLast = false, overflowNow = false;
    uint32_t dataMisses = 0;             // allocations outside a layer that did not fit (they fall back to the game's buffer)
    GfxLayerStats layers[kGfxLayerCount];
    size_t gameOpaLeft = 0, gameXluLeft = 0;   // room left in the game's own buffers after our layers, last frame (shown on the Graphics page)
};
GfxPool gGfxPool;
int gGfxLayerOpen = -1;   // the layer being drawn now, or -1
bool gGfxLayerShort = false;   // the open layer ran out of room and left something out (GfxHasRoom said no)

// The first call each game frame swaps to the other half of the pool (the half the previous frame drew from stays untouched) and resizes it if needed.
void GfxPoolFrame(PlayState* play) {
    namespace gl = royale::gfxlayers;
    GfxPool& p = gGfxPool;
    const uint32_t frame = play->state.frames;
    if (p.started && frame == p.frame) return;
    if (p.started) {
        p.usedLast = p.size[p.cur] - p.free.Free();
        p.peak = std::max(p.usedLast, p.peak - p.peak / 256);   // a slowly falling high-water mark
        p.overflowLast = p.overflowNow;
        p.want = gl::NextPoolSize(p.want, p.peak, p.overflowNow);
    }
    p.started = true;
    p.frame = frame;
    p.overflowNow = false;
    p.cur ^= 1;
    if (p.size[p.cur] < p.want || (p.trim[p.cur] && p.size[p.cur] > p.want)) {
        // Reclaim only the retired half, at the same safe point used for growth.
        std::unique_ptr<uint8_t[]> next(new (std::nothrow) uint8_t[p.want + gl::kGuard + gl::kAlign]);
        if (!next) { p.want = p.size[p.cur]; }
        else {
            p.raw[p.cur] = std::move(next);
            p.base[p.cur] = reinterpret_cast<uint8_t*>(gl::AlignUp(reinterpret_cast<uintptr_t>(p.raw[p.cur].get())));
            p.size[p.cur] = p.want;
        }
    }
    p.trim[p.cur] = false;
    p.free.head = p.base[p.cur];
    p.free.tail = p.base[p.cur] == nullptr ? nullptr : p.base[p.cur] + p.size[p.cur];
}

// Bytes left in the opaque (or translucent) buffer being written now: the game's, or the open layer's.
inline size_t GfxRoomOpa(PlayState* play) {
    const TwoHeadGfxArena& a = play->state.gfxCtx->polyOpa;
    return a.d > a.p ? static_cast<size_t>(a.d - a.p) * sizeof(Gfx) : 0;
}
inline size_t GfxRoomXlu(PlayState* play) {
    const TwoHeadGfxArena& a = play->state.gfxCtx->polyXlu;
    return a.d > a.p ? static_cast<size_t>(a.d - a.p) * sizeof(Gfx) : 0;
}
// Is there room for about `cmds` more commands (and `bytes` more data) in both streams? Big loops ask before each piece, so a layer that runs out
// stops cleanly (missing a few far-off pieces for one frame) instead of writing past its room.
inline bool GfxHasRoom(PlayState* play, size_t cmds, size_t bytes = 0) {
    const size_t need = (cmds + 8) * sizeof(Gfx);
    if (GfxRoomOpa(play) > need + bytes && GfxRoomXlu(play) > need) return true;
    if (gGfxLayerOpen >= 0) gGfxLayerShort = true;
    return false;
}

// Draws everything inside it as one layer (see above). Layers do not nest: one opened inside another just adds to the outer one.
// `backdrop`: the layer is the sky pass, drawn before the world (both of its streams go to the front of the game's opaque buffer).
class GfxLayer {
public:
    GfxLayer(PlayState* play, GfxLayerId id, bool backdrop = false) : play_(play), id_(static_cast<int>(id)), backdrop_(backdrop) {
        namespace gl = royale::gfxlayers;
        if (gGfxLayerOpen >= 0 || play == nullptr) return;
        GfxPoolFrame(play);
        GfxPool& p = gGfxPool;
        if (p.layers[id_].window == 0) p.layers[id_].window = kGfxLayerXluKB[id_] * 1024;
        plan_ = gl::PlanLayer(p.free, p.layers[id_].window);
        if (!plan_.ok) { p.overflowNow = true; p.layers[id_].drops++; return; }
        GraphicsContext* gfx = play->state.gfxCtx;
        savedOpa_ = gfx->polyOpa;
        savedXlu_ = gfx->polyXlu;
        gfx->polyXlu.bufp = gfx->polyXlu.p = reinterpret_cast<Gfx*>(plan_.xlu);
        gfx->polyXlu.d = reinterpret_cast<Gfx*>(plan_.xluEnd - gl::kEndSlack);
        gfx->polyXlu.size = static_cast<u32>(plan_.xluEnd - plan_.xlu);
        gfx->polyOpa.bufp = gfx->polyOpa.p = reinterpret_cast<Gfx*>(plan_.opa);
        gfx->polyOpa.d = reinterpret_cast<Gfx*>(plan_.data);
        gfx->polyOpa.size = static_cast<u32>(plan_.data - plan_.opa);
        gGfxLayerOpen = id_;
        gGfxLayerShort = false;
        open_ = true;
        // A known state to start from, whatever was drawn before.
        OPEN_DISPS(gfx);
        gDPPipeSync(POLY_OPA_DISP++);
        gDPPipeSync(POLY_XLU_DISP++);
        CLOSE_DISPS(gfx);
        Gfx_SetupDL_25Opa(gfx);
        Gfx_SetupDL_25Xlu(gfx);
        opaFirst_ = gfx->polyOpa.p;
        xluFirst_ = gfx->polyXlu.p;
    }
    ~GfxLayer() {
        namespace gl = royale::gfxlayers;
        if (!open_) return;
        GraphicsContext* gfx = play_->state.gfxCtx;
        GfxPool& p = gGfxPool;
        GfxLayerStats& st = p.layers[id_];
        const bool opaDrawn = gfx->polyOpa.p != opaFirst_, xluDrawn = gfx->polyXlu.p != xluFirst_;
        const gl::Used used = gl::Measure(plan_, reinterpret_cast<uint8_t*>(gfx->polyXlu.p), reinterpret_cast<uint8_t*>(gfx->polyOpa.p),
                                          reinterpret_cast<uint8_t*>(gfx->polyOpa.d));
        Gfx* opaList = reinterpret_cast<Gfx*>(plan_.opa);
        Gfx* xluList = reinterpret_cast<Gfx*>(plan_.xlu);
        if (!used.overflowed) {
            // Leave the state as the game's own drawing expects it, and end both lists (the closing slack is kept free for this).
            Gfx_SetupDL_25Opa(gfx);
            Gfx_SetupDL_25Xlu(gfx);
            OPEN_DISPS(gfx);
            gDPPipeSync(POLY_OPA_DISP++);
            gSPEndDisplayList(POLY_OPA_DISP++);
            gDPPipeSync(POLY_XLU_DISP++);
            gSPEndDisplayList(POLY_XLU_DISP++);
            CLOSE_DISPS(gfx);
        }
        gfx->polyOpa = savedOpa_;
        gfx->polyXlu = savedXlu_;
        gGfxLayerOpen = -1;
        st.xlu = used.xlu; st.opa = used.opa; st.data = used.data;
        st.window = gl::NextXluWindow(st.window, used.xlu, used.overflowed, kGfxLayerXluKB[id_] * 1024, gGfxLayerShort);
        p.free = used.overflowed ? gl::After(plan_, used) : gl::After(plan_, used, opaDrawn, xluDrawn);
        if (used.overflowed || gGfxLayerShort) { st.drops++; p.overflowNow = true; }   // it gets more room next frame
        if (used.overflowed) return;                                                 // and is not drawn this one
        OPEN_DISPS(gfx);
        if (backdrop_) {
            if (opaDrawn) gSPDisplayList(POLY_OPA_DISP++, opaList);
            if (xluDrawn) gSPDisplayList(POLY_OPA_DISP++, xluList);
        } else {
            if (opaDrawn) gSPDisplayList(POLY_OPA_DISP++, opaList);
            if (xluDrawn) gSPDisplayList(POLY_XLU_DISP++, xluList);
        }
        CLOSE_DISPS(gfx);
    }
    bool Open() const { return open_; }
    GfxLayer(const GfxLayer&) = delete;
    GfxLayer& operator=(const GfxLayer&) = delete;

private:
    PlayState* play_;
    int id_;
    bool backdrop_;
    bool open_ = false;
    royale::gfxlayers::Plan plan_;
    TwoHeadGfxArena savedOpa_ = {}, savedXlu_ = {};
    Gfx* opaFirst_ = nullptr;
    Gfx* xluFirst_ = nullptr;
};
bool gGfxBackdropOpen = false;   // the sky pass is being drawn (no depth test: the world is drawn over it afterwards)

// Per-frame memory (vertices, display lists, matrices) for our drawing. Inside a layer it comes from the layer's own room; outside one, from the back
// of the pool. Only when the pool is full (it is bigger next frame) does it fall back to the game's buffer, and then only while a generous reserve is
// left there for the game's own commands; nullptr means skip drawing this piece for one frame.
inline void* FrameAlloc(PlayState* play, size_t bytes) {
    if (gGfxLayerOpen >= 0) {
        if (bytes + 4096 > GfxRoomOpa(play)) return nullptr;
        return Graph_Alloc(play->state.gfxCtx, bytes);
    }
    GfxPoolFrame(play);
    if (uint8_t* d = royale::gfxlayers::TakeData(gGfxPool.free, bytes, 32 * 1024)) return d;
    gGfxPool.dataMisses++;
    gGfxPool.overflowNow = true;
    constexpr size_t kReserve = 64 * 1024;   // about a third of the game's buffer, kept free for its own commands
    if (bytes + kReserve > GfxRoomOpa(play)) return nullptr;
    return Graph_Alloc(play->state.gfxCtx, bytes);
}

// ---- debug switches ----------------------------------------------------------------------------------------------------------
// A temporary Debug section in the Battle Royale menu: each newer feature can be switched off to find out which one causes a crash or glitch.
// They are all on by default and remembered between runs. Remove this section (and the DebugOn checks) once the features are trusted.
struct DebugSwitch { const char* key; const char* label; };
constexpr DebugSwitch kDebugSwitches[] = {
    { "Carts", "Lon Lon Buggy carts" },
    { "Weather", "Weather (the game's rain, snow, lightning, fog, sand)" },
    { "StormWall", "Storm wall" },
    { "Foliage", "Foliage (grass, trees and the small things on the ground)" },
    { "Cloth", "Cloth physics (caps, tunics, sheaths, gliders)" },
    { "Music", "Custom match and lobby music" },
    { "Terrain", "Custom rocks, trees and platforms" },
    { "TimeOfDay", "Time of day changes" },
    { "Allies", "Maya, Lilo, the cat pet and allies" },
    { "BossFx", "Boss effects in the world" },
    { "Loot", "Loot and chests in the world" },
    { "Props", "Map props" },
    { "Projectiles", "Arrows, bombs and chest reveals" },
    { "Minimap", "Minimap switching" },
    { "Wind", "Wind streaks in the air" },
    { "Tornado", "Tornado easter egg" },
    { "Sky", "Sky: gradient, stars, sun, moon and clouds" },
    { "Fog", "Fog banks (low volumetric-style fog)" },
    { "Scenery", "Fortnite Map scenery (oaks, cliffs, crags, flowers)" },
    { "GroundPatches", "Ground patches (puddles, snow, frost, leaves and blossom)" },
    { "Water", "Realistic water (waves, splashes, wakes, reflections, swim current, underwater look)" },
    { "Avriella", "Avriella the baby pet (the pet picker)" },
    { "Ragdoll", "Ragdoll bodies: full-body joints and the lobby test ragdoll" },
    { "LobbyFish", "Lobby reef aquarium (clownfish and cleaner wrasse)" },
    { "HitFx", "Hit feedback: flash and particles on whatever is hit" },
    { "Shadows", "Dynamic shadows and lights" },
    { "ItemMaps", "Sword and shield sunlight detail (normal and bump maps)" },
    { "GearReport", "Gear report: show on screen what Link's hands, back and feet draw (off by default)" },
};
constexpr int kDebugCount = static_cast<int>(sizeof(kDebugSwitches) / sizeof(kDebugSwitches[0]));
enum DebugId { kDbgCarts, kDbgWeather, kDbgStormWall, kDbgFoliage, kDbgCloth, kDbgMusic, kDbgTerrain, kDbgTimeOfDay, kDbgAllies, kDbgBossFx,
               kDbgLoot, kDbgProps, kDbgProjectiles, kDbgMinimap, kDbgWind, kDbgTornado, kDbgSky, kDbgFog, kDbgScenery, kDbgGroundPatches, kDbgWater, kDbgAvriella, kDbgRagdoll, kDbgLobbyFish, kDbgHitFx, kDbgShadows, kDbgItemMaps, kDbgGearReport };
static_assert(kDbgGearReport + 1 == kDebugCount, "one switch per DebugId");
bool gDebugOn[kDebugCount];
bool gDebugLoaded = false;
void LoadDebugSwitches() {
    for (int i = 0; i < kDebugCount; i++) {
        const std::string key = std::string(CVAR_SETTING("Royale.Debug.")) + kDebugSwitches[i].key;
        gDebugOn[i] = CVarGetInteger(key.c_str(), i == kDbgGearReport ? 0 : 1) != 0;   // (the gear report is a bug-report aid: off unless asked for)
    }
    gDebugLoaded = true;
}
inline bool DebugOn(int id) {
    if (!gDebugLoaded) LoadDebugSwitches();
    return gDebugOn[id];
}

// Dynamic lights and shadows (defined in their own section, further down).
void AddLightFlash(float x, float y, float z, uint8_t r, uint8_t g, uint8_t b, float radius, float life, int key);   // a short light: an explosion, a bolt
void UpdateDynamicLights(PlayState* play);
void DrawShadowsAndLights(PlayState* play);
void ForgetDynamicLights();
void ForgetShadows();
void LoadShadowSettings();
void BuildingSunNow(float dir[3], float sun[3], float amb[3]);
bool BuildingSunOn();

bool TravelTo(int entrance) {
    if (!InGame() || gTravelCooldown > 0 || gPlayState->transitionTrigger != TRANS_TRIGGER_OFF) return false;
    gOurTravel = true;
    gPlayState->nextEntranceIndex = entrance;
    gPlayState->transitionTrigger = TRANS_TRIGGER_START;
    gPlayState->transitionType = TRANS_TYPE_FADE_BLACK;
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK;
    gTravelCooldown = 5 * royale::kTickHz;
    return true;
}
bool GoToWaitingRoom() { Trace("travel: to the waiting room"); return TravelTo(royale::lobby::WaitingArea(WaitingAreaIndex()).entrance); }
int EntranceFor(int mapId) {
    switch (royale::ClampMap(mapId)) {
        case 1: return ENTR_LAKE_HYLIA_0_1;
        case 2: return ENTR_KAKARIKO_VILLAGE_0_1;
        case 3: return ENTR_DEATH_MOUNTAIN_CRATER_0_1;
        case 4: return ENTR_DESERT_COLOSSUS_0_4;
        default: return ENTR_HYRULE_FIELD_0_1;
    }
}
bool GoToField() { return TravelTo(EntranceFor(gMapId)); }

// Lobby rule: players may wait in either the waiting room or the field. Once the countdown starts everyone must be in the
// field, and the client takes them there by itself.
bool WantsWaitingRoom = false; // set when joining (if the player left the option on) or by the button
bool InPlayableScene(royale::MatchState state) {
    return InField() || (state == royale::MatchState::Lobby && InWaitingRoom());
}
// Where a companion may follow you: a match's playable scenes (or its lobby), or anywhere in the regular game unless a cutscene is playing.
bool CompanionScene(const royale::HudState& hud) {
    if (gSession.Joined()) return InPlayableScene(hud.state);
    return RegularOn(kRegCompanions) && !InCutscene();
}
bool MustBeInField(royale::MatchState state) {
    return state == royale::MatchState::Countdown || state == royale::MatchState::Drop || state == royale::MatchState::InMatch;
}

// ---- rarity presentation ------------------------------------------------------------------------------------------------

// Tiers wear the colours of Ocarina of Time's rupees (green, blue, red, purple, gold), the same rupee models loot is drawn with below.
struct Rgb { u8 r, g, b; };
constexpr Rgb kRarityRgb[royale::kRarityCount] = { { 96, 210, 84 }, { 72, 132, 250 }, { 236, 64, 52 }, { 186, 92, 236 }, { 250, 210, 40 } };

// HUD and native menus share one War Table palette.
ImU32 HudColor(uint32_t rgba, int alpha=255) { return IM_COL32(rgba>>24,(rgba>>16)&255,(rgba>>8)&255,alpha); }
// ---- the pad layout and the button pictures ------------------------------------------------------------------------------------
// ControlScheme 0 is the Battle Royale layout (the default), 1 the Classic one (jump on C-Up); see shared/controls.h. The prompts in the HUD
// follow whichever is chosen. Buttons are drawn like the N64 pad: A blue, B green, C yellow, Z grey, R grey, Start red.
royale::controls::Scheme ControlScheme() { return royale::controls::Clamp(CVarGetInteger(CVAR_SETTING("Royale.ControlScheme"), 0)); }
bool gTouchJump = false;   // the on-screen JUMP button was tapped this frame

void DrawPadGlyph(ImDrawList* dl, ImFont* font, ImVec2 c, float r, royale::controls::Glyph g, int alpha = 255) {
    using G = royale::controls::Glyph;
    auto a = [&](int v) { return v * alpha / 255; };
    auto tri = [&](ImVec2 at, float s, int dir, ImU32 col) {   // dir: 0 up, 1 down, 2 left, 3 right
        const ImVec2 pts[4][3] = { { {at.x, at.y - s}, {at.x - s, at.y + s * 0.8f}, {at.x + s, at.y + s * 0.8f} },
                                   { {at.x, at.y + s}, {at.x - s, at.y - s * 0.8f}, {at.x + s, at.y - s * 0.8f} },
                                   { {at.x - s, at.y}, {at.x + s * 0.8f, at.y - s}, {at.x + s * 0.8f, at.y + s} },
                                   { {at.x + s, at.y}, {at.x - s * 0.8f, at.y - s}, {at.x - s * 0.8f, at.y + s} } };
        dl->AddTriangleFilled(pts[dir][0], pts[dir][1], pts[dir][2], col);
    };
    if (g == G::None) return;
    if (g == G::DLeftRight) {   // a pair of D-pad arrows
        DrawPadGlyph(dl, font, ImVec2(c.x - r * 0.95f, c.y), r * 0.9f, G::DLeft, alpha);
        DrawPadGlyph(dl, font, ImVec2(c.x + r * 0.95f, c.y), r * 0.9f, G::DRight, alpha);
        return;
    }
    const char* letter = nullptr;
    ImU32 fill = IM_COL32(120, 120, 130, a(255));
    bool round = true;
    switch (g) {
        case G::A: letter = "A"; fill = IM_COL32(70, 120, 215, a(255)); break;
        case G::B: letter = "B"; fill = IM_COL32(60, 175, 85, a(255)); break;
        case G::Z: letter = "Z"; round = false; fill = IM_COL32(150, 150, 160, a(255)); break;
        case G::R: letter = "R"; round = false; fill = IM_COL32(150, 150, 160, a(255)); break;
        case G::Start: letter = "S"; fill = IM_COL32(205, 55, 45, a(255)); break;
        case G::CUp: case G::CDown: case G::CLeft: case G::CRight: fill = IM_COL32(245, 205, 40, a(255)); break;
        default: fill = IM_COL32(60, 62, 72, a(255)); round = false; break;   // the D-pad's dark plastic
    }
    const ImU32 edge = IM_COL32(10, 12, 20, a(235)), shade = IM_COL32(0, 0, 0, a(90));
    if (round) {
        dl->AddCircleFilled(ImVec2(c.x + r * 0.12f, c.y + r * 0.16f), r, shade, 20);
        dl->AddCircleFilled(c, r, fill, 20);
        dl->AddCircle(c, r, edge, 20, std::max(1.5f, r * 0.14f));
        dl->AddCircleFilled(ImVec2(c.x - r * 0.3f, c.y - r * 0.35f), r * 0.28f, IM_COL32(255, 255, 255, a(70)), 12);
    } else if (letter != nullptr) {   // Z and R: the trigger, a wide rounded tab
        const ImVec2 p0(c.x - r * 1.05f, c.y - r * 0.8f), p1(c.x + r * 1.05f, c.y + r * 0.8f);
        dl->AddRectFilled(ImVec2(p0.x + 1, p0.y + 2), ImVec2(p1.x + 1, p1.y + 2), shade, r * 0.5f);
        dl->AddRectFilled(p0, p1, fill, r * 0.5f);
        dl->AddRect(p0, p1, edge, r * 0.5f, 0, std::max(1.5f, r * 0.14f));
    } else {   // the D-pad: a dark cross with the pressed arm lit
        const float t = r * 0.38f;
        dl->AddRectFilled(ImVec2(c.x - r, c.y - t), ImVec2(c.x + r, c.y + t), fill, 2.0f);
        dl->AddRectFilled(ImVec2(c.x - t, c.y - r), ImVec2(c.x + t, c.y + r), fill, 2.0f);
        dl->AddRect(ImVec2(c.x - r, c.y - t), ImVec2(c.x + r, c.y + t), edge, 2.0f, 0, 1.2f);
        const ImU32 lit = IM_COL32(245, 245, 235, a(255));
        const float o = r * 0.62f, s = r * 0.26f;
        if (g == G::DUp) tri(ImVec2(c.x, c.y - o), s, 0, lit);
        if (g == G::DDown) tri(ImVec2(c.x, c.y + o), s, 1, lit);
        if (g == G::DLeft) tri(ImVec2(c.x - o, c.y), s, 2, lit);
        if (g == G::DRight) tri(ImVec2(c.x + o, c.y), s, 3, lit);
        return;
    }
    const ImU32 ink = IM_COL32(g == G::A || g == G::B || g == G::Start ? 255 : 30, g == G::A || g == G::B || g == G::Start ? 255 : 30, g == G::A || g == G::B || g == G::Start ? 255 : 36, a(255));
    if (letter != nullptr) {
        const float fs = r * 1.25f;
        const ImVec2 sz = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, letter);
        dl->AddText(font, fs, ImVec2(c.x - sz.x * 0.5f, c.y - sz.y * 0.5f), ink, letter);
    } else {   // a C button: a small dark arrow on the yellow
        const float s = r * 0.42f;
        tri(c, s, g == G::CUp ? 0 : g == G::CDown ? 1 : g == G::CLeft ? 2 : 3, IM_COL32(60, 40, 5, a(255)));
    }
}

// A row of "button + what it does" chips, centred on `cx` (or left-aligned from `cx` when `centred` is false). Returns the width drawn.
struct PromptChip { royale::controls::Glyph glyph; std::string label; };
float DrawPromptRow(ImDrawList* dl, ImFont* font, float cx, float y, float scale, const std::vector<PromptChip>& chips, bool centred = true, int alpha = 255) {
    const float r = 9.0f * scale, ts = 14.0f * scale, gap = 14.0f * scale;
    float total = 0;
    std::vector<float> widths;
    for (const auto& c : chips) {
        const float gw = c.glyph == royale::controls::Glyph::DLeftRight ? r * 3.7f : r * 2.2f;
        widths.push_back(gw + 5.0f * scale + font->CalcTextSizeA(ts, FLT_MAX, 0.0f, c.label.c_str()).x);
        total += widths.back() + (&c == &chips.back() ? 0.0f : gap);
    }
    float x = centred ? cx - total * 0.5f : cx;
    const float start = x;
    for (size_t i = 0; i < chips.size(); i++) {
        const bool pair = chips[i].glyph == royale::controls::Glyph::DLeftRight;
        const float gw = pair ? r * 3.7f : r * 2.2f;
        DrawPadGlyph(dl, font, ImVec2(x + gw * 0.5f, y), r, chips[i].glyph, alpha);
        const ImVec2 tp(x + gw + 5.0f * scale, y - ts * 0.5f);
        dl->AddText(font, ts, ImVec2(tp.x + 1, tp.y + 1), IM_COL32(0, 0, 0, alpha * 220 / 255), chips[i].label.c_str());
        dl->AddText(font, ts, tp, IM_COL32(240, 236, 220, alpha), chips[i].label.c_str());
        x += widths[i] + gap;
    }
    return x - start - gap;
}

ImU32 OotPanel(int alpha) { return HudColor(royale::wartable::theme::Panel,alpha); }
void HudFrame(ImDrawList* dl,ImVec2 a,ImVec2 b,float scale,bool selected=false) {
    using namespace royale::wartable::theme;
    dl->AddRectFilled(ImVec2(a.x+2*scale,a.y+3*scale),ImVec2(b.x+2*scale,b.y+3*scale),IM_COL32(6,15,36,192));
    dl->AddRectFilled(a,b,HudColor(selected?Button:Panel,237));
    dl->AddRect(a,b,HudColor(selected?Focus:Border),0,0,(selected?2.5f:1.0f)*scale);
    const float length=7*scale;
    for(int i=0;i<4;++i) {
        const ImVec2 c(i&1?b.x:a.x,i&2?b.y:a.y);
        dl->AddLine(c,ImVec2(c.x+(i&1?-length:length),c.y),HudColor(selected?Focus:Gold),2*scale);
        dl->AddLine(c,ImVec2(c.x,c.y+(i&2?-length:length)),HudColor(selected?Focus:Gold),2*scale);
    }
}

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
// Dropped piles of money and ammo use the game's own models for them (rupee colour by how much; arrows, seeds, bombs and nuts as themselves).
s16 DropTypeFor(royale::ItemId item, royale::Rarity rarity, int amount) {
    using royale::ItemId;
    switch (item) {
        case ItemId::Rupees: return amount >= 50 ? ITEM00_RUPEE_PURPLE : amount >= 20 ? ITEM00_RUPEE_RED : amount >= 5 ? ITEM00_RUPEE_BLUE : ITEM00_RUPEE_GREEN;
        case ItemId::ArrowAmmo: return ITEM00_ARROWS_MEDIUM;
        case ItemId::SeedAmmo: return ITEM00_SEEDS;
        case ItemId::BombAmmo: return ITEM00_BOMBS_A;
        case ItemId::BombchuAmmo: return ITEM00_BOMBS_B;
        case ItemId::NutAmmo: return ITEM00_NUTS;
        default: return RarityDropType(rarity);
    }
}
const char* ItemName(royale::ItemId id) {
    return royale::kItems[static_cast<int>(id)].name;
}
std::string ItemLabel(royale::ItemId id, royale::Rarity r) {
    if (!royale::InPool(id) && id != royale::ItemId::BasicSword) return ItemName(id);   // money and ammo have no rarity
    return std::string(ItemName(id)) + " [" + RarityName(r) + "]";
}
// What a pile on the ground is called: "20 Rupees", "9 Deku Seeds", or the item and its rarity.
std::string LootLabel(const royale::net::LootNet& l) {
    const royale::ItemId id = static_cast<royale::ItemId>(l.item);
    if (royale::InstantOf(id) == royale::InstantEffect::Rupees || royale::InstantOf(id) == royale::InstantEffect::Ammo)
        return std::to_string(std::max<int>(1, l.amount)) + " " + ItemName(id);
    return ItemLabel(id, static_cast<royale::Rarity>(l.rarity));
}

// ---- the real map -----------------------------------------------------------------------------------------------------------

// Is there floor under (x, z)? Used to measure the map and to keep loot, spawns and storm centres on ground.
s32 gSolidBgId = -1;   // the collision slot holding the solid scenery (see "solid scenery" below), -1 when there is none

// On the Fortnite map the scene's ground is the island's own triangles (shared/fortnite_map.h), so its height, slope and water are worked out
// directly instead of with the game's raycasts: the same answers, far cheaper (the host asks thousands of times when it lays out a match).
bool OnIsland() { return gFortniteScene && gPlayState != nullptr && gPlayState->sceneNum == SCENE_HYRULE_FIELD; }
bool BlocksAreBaked();   // the climbing blocks are in the island's collision (see "WantedBlocks")
// The maps built by hand in Blender (shared/convergence_data.h, shared/kingdom_data.h): everything on them is baked, nothing is scattered.
bool OnAuthoredTerrain() { return OnIsland() && royale::IsAuthoredMap(royale::fortnite::gTerrainMapId); }
bool OnConvergenceTerrain() { return OnIsland() && royale::fortnite::gTerrainMapId==royale::kConvergenceMapIndex; }
bool OnRiftlandsTerrain() { return OnIsland() && royale::fortnite::gTerrainMapId==royale::kRiftlandsMapIndex; }
bool OnMoonfallTerrain() { return OnIsland() && royale::fortnite::gTerrainMapId==royale::kMoonfallMapIndex; }
bool OnKingdomTerrain() { return OnIsland() && royale::fortnite::gTerrainMapId==royale::kKingdomMapIndex; }

// Where the floor raycasts start. A match's maps are open to the sky, so from high above; in the regular game's houses, grottos and dungeons, floors
// lie over floors, so from a little above the player (the floor of the level they are on, not the roof).
float FloorProbeTop() {
    if (InRegularGame() && !SceneOutdoors()) return GET_PLAYER(gPlayState)->actor.world.pos.y + 250.0f;
    return 4000.0f;
}
bool RawFloorAt(float x, float z, float* outY = nullptr) {   // the scene's own floor, without our climbing blocks
    if (!InField() && !InRegularGame()) return false;
    if (OnIsland() && gMapId == royale::kConvergenceMapIndex) {
        if (std::fabs(x)>=royale::fortnite::kHalfX || std::fabs(z)>=royale::fortnite::kHalfZ) return false;
        if (outY) *outY=royale::ConvergenceGroundHeight({x,z});
        return true;
    }
    if (OnIsland() && gMapId == royale::kKingdomMapIndex) {
        if (std::fabs(x)>=royale::fortnite::kHalfX || std::fabs(z)>=royale::fortnite::kHalfZ) return false;
        float lootY;
        if (outY) *outY = royale::KingdomLootHeightAt({x,z},&lootY) ? lootY : royale::KingdomGroundHeight({x,z});   // a chest site on an upper floor or a roof is at its own height
        return true;
    }
    if (OnIsland() && gMapId == royale::kRiftlandsMapIndex) {
        if (std::fabs(x)>=royale::fortnite::kHalfX || std::fabs(z)>=royale::fortnite::kHalfZ) return false;
        float lootY;
        if (outY) *outY = royale::RiftlandsLootHeightAt({x,z},&lootY) ? lootY : royale::RiftlandsGroundHeight({x,z});   // a chest site on an upper floor or a roof is at its own height
        return true;
    }
    if (OnIsland() && gMapId == royale::kMoonfallMapIndex) {
        if (std::fabs(x)>=royale::fortnite::kHalfX || std::fabs(z)>=royale::fortnite::kHalfZ) return false;
        float lootY;
        if (outY) *outY = royale::MoonfallLootHeightAt({x,z},&lootY) ? lootY : royale::MoonfallGroundHeight({x,z});   // a chest site on an upper floor or a roof is at its own height
        return true;
    }
    if (OnIsland()) return royale::fortnite::GroundHeight(x, z, outY);
    Vec3f pos = { x, FloorProbeTop(), z };
    for (int tries = 0; tries < 6; tries++) {
        CollisionPoly poly;
        s32 bgId = BGCHECK_SCENE;
        const float y = BgCheck_AnyRaycastFloor2(&gPlayState->colCtx, &poly, &bgId, &pos);
        if (y <= BGCHECK_Y_MIN + 1.0f) return false;
        if (gSolidBgId >= 0 && bgId == gSolidBgId) { pos.y = y - 2.0f; continue; }   // the top of a block or a boulder: look under it
        if (outY) *outY = y;
        return true;
    }
    return false;
}

// Floor that would load another scene (a doorway, a cave mouth, the edge of the field). Nothing is ever placed on or near it, so nobody
// spawns or finds a chest where the exit seal (SealExits) would shove them back. The ROM extractor's numbers (docs/MAPS.md) show how many
// of these each map has: Kakariko alone has nine loading zones and fourteen exit surfaces.
bool OnExitFloor(float x, float z) {
    if ((!InField() && !InRegularGame()) || OnIsland()) return false;   // the island has no exits
    CollisionPoly poly;
    Vec3f pos = { x, FloorProbeTop(), z };
    const float y = BgCheck_AnyRaycastFloor1(&gPlayState->colCtx, &poly, &pos);
    if (y <= BGCHECK_Y_MIN + 1.0f) return false;
    return SurfaceType_GetSceneExitIndex(&gPlayState->colCtx, &poly, BGCHECK_SCENE) != 0;
}
bool NearLoadingZone(float x, float z, float within) {
    if (!InField() && !InRegularGame()) return false;
    const TransitionActorContext& t = gPlayState->transiActorCtx;
    for (int i = 0; i < t.numActors && t.list != nullptr; i++) {
        if (std::hypot(x - t.list[i].pos.x, z - t.list[i].pos.z) < within) return true;
    }
    return false;
}

// ---- climbing blocks ---------------------------------------------------------------------------------------------------------
// The scene's collision can't be changed, so the stone blocks of the climbs (shared/props.h) are solid in the mod's own terms: FloorAt knows
// their tops (chests and the ledge assist see them as floor), and ApplyPlatforms holds the player on top of them and out of their sides.
std::vector<size_t> gPlatformIdx;
const royale::Prop* gPlatformSrc = nullptr;
size_t gPlatformSrcCount = 0;
std::unordered_map<size_t, float> gPlatformBase;   // floor height under each block's middle
std::unordered_map<size_t, float> gPlatformFoot;   // the lowest ground under each block's footprint (PlatformFoot)

void RefreshPlatforms() {
    if (!gSession.Client()) { gPlatformIdx.clear(); gPlatformSrc = nullptr; gPlatformSrcCount = 0; return; }
    const auto& props = gSession.Client()->Props();
    if (props.data() == gPlatformSrc && props.size() == gPlatformSrcCount) return;
    gPlatformSrc = props.data();
    gPlatformSrcCount = props.size();
    gPlatformIdx.clear();
    gPlatformBase.clear();
    gPlatformFoot.clear();
    for (size_t i = 0; i < props.size(); i++) if (royale::IsPlatform(props[i].kind)) gPlatformIdx.push_back(i);
}

float PlatformBase(size_t i) {
    auto it = gPlatformBase.find(i);
    if (it != gPlatformBase.end()) return it->second;
    float y = 0;
    const auto& p = gSession.Client()->Props()[i];
    if (!RawFloorAt(p.pos.x, p.pos.z, &y)) return -1.0e9f;   // not measurable yet: don't remember it
    gPlatformBase[i] = y;
    return y;
}

// The top of the highest block covering (x, z), widened by `margin` all round.
bool PlatformTopAt(float x, float z, float* top, float margin = 0.0f) {
    RefreshPlatforms();
    bool found = false;
    for (size_t i : gPlatformIdx) {
        const royale::Prop& p = gSession.Client()->Props()[i];
        if (std::fabs(x - p.pos.x) > royale::kPlatformHalf + margin || std::fabs(z - p.pos.z) > royale::kPlatformHalf + margin) continue;
        const float base = PlatformBase(i);
        if (base < -1.0e8f) continue;
        const float t = base + royale::PlatformHeight(p.kind);
        if (!found || t > *top) { *top = t; found = true; }
    }
    return found;
}

bool FloorAt(float x, float z, float* outY = nullptr) {
    float raw = 0, top = 0;
    const bool haveRaw = RawFloorAt(x, z, &raw);
    if (!InField() && !InRegularGame()) return false;
    if (PlatformTopAt(x, z, &top) && (!haveRaw || top > raw)) { if (outY) *outY = top; return true; }
    if (!haveRaw) return false;
    if (outY) *outY = raw;
    return true;
}

float gMedianFloorY = 0;   // heights far from this are cliffs or rooftops, not ground to place things on
bool gMapMeasured = false;
float gMeasuredRadius = 0;

// Water is no place for a chest or a spawn: the surface is above the floor under it.
bool UnderWater(float x, float z, float floorY) {
    if (OnIsland()) return std::fabs(x) < royale::fortnite::kHalfX && std::fabs(z) < royale::fortnite::kHalfZ && floorY + 15.0f < royale::fortnite::WaterHeightAt(x,z);
    float surface = 0;
    WaterBox* box = nullptr;
    return WaterBox_GetSurface1(gPlayState, &gPlayState->colCtx, x, z, &surface, &box) != 0 && surface > floorY + 15.0f;
}

// Ground that hurts or throws you out: lava and other damage floors, floors that put you back at the last door, bottomless drops, and slopes too steep to
// stand on. (The floor property numbers are the game's own, see FUNC_80041EA4_*.) Looks at the scene's own floor only: our blocks are always fine.
bool HazardFloorAt(float x, float z) {
    if (!InField() && !InRegularGame()) return false;
    if (OnIsland()) return royale::fortnite::GroundUp(x, z) < 0.8f;   // off the island, or too steep to stand on
    CollisionPoly poly;
    s32 bgId = BGCHECK_SCENE;
    Vec3f pos = { x, FloorProbeTop(), z };
    const float y = BgCheck_AnyRaycastFloor2(&gPlayState->colCtx, &poly, &bgId, &pos);
    if (y <= BGCHECK_Y_MIN + 1.0f) return true;   // nothing there at all
    if (gSolidBgId >= 0 && bgId == gSolidBgId) return false;
    const u32 property = func_80041EA4(&gPlayState->colCtx, &poly, bgId);
    if (property == FUNC_80041EA4_RESPAWN || property == FUNC_80041EA4_STOP || property == FUNC_80041EA4_VOID_OUT) return true;
    return poly.normal.y < COLPOLY_SNORMAL(0.8f);   // steeper than about 37 degrees: you slide off it
}

bool WalkableAt(royale::Vec2 p) {
    if (gMapId == royale::kRiftlandsMapIndex) return royale::RiftlandsPlacementValid(p);
    if (gMapId == royale::kMoonfallMapIndex) return royale::MoonfallPlacementValid(p);
    if (gMapId == royale::kConvergenceMapIndex && royale::ConvergenceObstacleAt(p)) return false;
    if (gMapId == royale::kKingdomMapIndex && royale::KingdomObstacleAt(p)) return false;
    if (gMapId == royale::kRiftlandsMapIndex && royale::RiftlandsObstacleAt(p)) return false;
    if (gMapId == royale::kMoonfallMapIndex && royale::MoonfallObstacleAt(p)) return false;
    float y;
    return FloorAt(p.x, p.z, &y) && std::fabs(y - gMedianFloorY) <= 1200.0f && !UnderWater(p.x, p.z, y) && !OnExitFloor(p.x, p.z) && !NearLoadingZone(p.x, p.z, 380.0f) && !HazardFloorAt(p.x, p.z);
}

// Probe the floor on a grid across the whole scene and fit a circle around the part that has ground. Takes a few thousand
// raycasts (a few milliseconds), once per match.
bool MeasureField(royale::Circle* out) {
    if (!InField()) return false;
    if (royale::IsTestMap(gMapId) || royale::IsAuthoredMap(gMapId)) {   // authored maps retain their intended storm footprint
        *out = royale::MapOf(gMapId).fallback;
        gMedianFloorY = gMapId == royale::kConvergenceMapIndex ? 60.0f : gMapId == royale::kKingdomMapIndex ? 300.0f : gMapId == royale::kMoonfallMapIndex ? 100.0f : 0.0f;
        gMapMeasured = true;
        gMeasuredRadius = out->radius;
        return true;
    }
    std::vector<royale::Vec2> points;
    std::vector<float> heights;
    // The window is the scene's own collision bounds (the old fixed +-9000 box cut Hyrule Field, which runs from x -11000 to 6700 and z -1800 to 16500,
    // and put its centre in the wrong place). The step grows with the area so the number of probes stays about the same.
    const CollisionContext& cc = gPlayState->colCtx;
    const float x0 = std::max(-20000.0f, cc.minBounds.x), x1 = std::min(20000.0f, cc.maxBounds.x);
    const float z0 = std::max(-20000.0f, cc.minBounds.z), z1 = std::min(20000.0f, cc.maxBounds.z);
    const float step = std::max(300.0f, std::sqrt(std::max(1.0f, (x1 - x0) * (z1 - z0)) / 9000.0f));
    for (float x = x0; x <= x1; x += step) {
        for (float z = z0; z <= z1; z += step) {
            float y;
            if (FloorAt(x, z, &y) && !UnderWater(x, z, y) && !OnExitFloor(x, z)) { points.push_back({ x, z }); heights.push_back(y); }
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
    // The Fortnite map is played on the whole island, coast to coast: its towns run right out to the cliffs (Junk Junction, Lucky Landing).
    if (gMapId == royale::fortnite::kMapId) radius = dist[std::min(dist.size() - 1, static_cast<size_t>(dist.size() * 0.99f))];
    radius = std::clamp(radius, 1500.0f, royale::MapOf(gMapId).maxRadius); // a smaller arena keeps the fights close and the storm in sight (Hyrule Field may be bigger)
    *out = { centre, radius };
    gMapMeasured = true;
    gMeasuredRadius = radius;
    return true;
}

