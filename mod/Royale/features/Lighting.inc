// ---- dynamic lights and shadows ------------------------------------------------------------------------------------------------
// Shadows that follow the sun, the moon and nearby lights, and short-lived lights from explosions, spells, arrows, lightning and chest reveals.
// shared/dynamic_shadows.h has the maths and explains the method; this is the part that talks to the game.
//
// Each frame (in Projectile_Draw, as the "Shadows and light" layer):
//   1. gather what casts a shadow, by the kinds switched on in Graphics > Shadows and lights: your Link and other players and bots (from the joints
//      the game records each time it draws a Link), allies and pets, mini bosses and bosses, carts, rocks and boulders, chests and loot;
//   2. keep the nearest within the quality's budget (your own Link always first) and give each a sun or moon shadow, and with Medium and up a
//      second one from the strongest light near it;
//   3. paint each shadow's small map (only when it changed: a still boulder keeps its map until the sun moves) and lay it on the ground as a mesh
//      that follows the floor. The game's own round shadow under those objects is switched off while they have a dynamic one.
// The lights live in the game's own light system (UpdateDynamicLights, before the actors are drawn), so Link, enemies and everything the game
// lights are lit by them; a soft glow on the ground under each light shows it on the scenery, which the game's light system does not reach.
namespace sh = royale::shadows;

enum ShadowKind { kShadowYou, kShadowPlayers, kShadowAllies, kShadowBosses, kShadowCarts, kShadowProps, kShadowChests, kShadowKinds };
struct ShadowKindInfo { const char* cvar; const char* label; bool on; };
ShadowKindInfo gShadowKinds[kShadowKinds] = {
    { "ShadowYou", "Your Link", true },
    { "ShadowPlayers", "Other players, bots and fallen bodies", true },
    { "ShadowAllies", "Allies and pets (Maya, Lilo, the cat, Avriella, hired allies)", true },
    { "ShadowBosses", "Mini bosses and bosses", true },
    { "ShadowCarts", "Carts", true },
    { "ShadowProps", "Rocks, boulders and standing stones", false },
    { "ShadowChests", "Chests and loot on the ground", false },
};

struct ShadowConfig {
    int quality = static_cast<int>(sh::Quality::Low);   // the default: cheap enough for an Android handheld
    bool custom = false;          // fine-tuned below instead of the preset
    sh::Settings fine;            // the fine-tuned numbers (start from the preset)
    int darkness = 100;           // % of the normal shadow strength
    int softness = 100;           // % of the normal soft edge
    bool lights = true;           // dynamic lights from explosions, spells, arrows, lightning and chest reveals
    bool sunOnBuildings = true;   // Hyrule Kingdom's lit stone, wood and roofs follow the sun and the moon through the match
};
ShadowConfig gShadowCfg;
bool gShadowCfgLoaded = false;

sh::Settings ShadowSettings() {
    if (!gShadowCfgLoaded) LoadShadowSettings();
    if (gShadowCfg.custom) return gShadowCfg.fine;
    return sh::Preset(static_cast<sh::Quality>(gShadowCfg.quality));
}

void LoadShadowSettings() {
    gShadowCfgLoaded = true;
    ShadowConfig& c = gShadowCfg;
    c.quality = std::clamp(CVarGetInteger(ROYALE_CVAR("ShadowQuality"), static_cast<int>(sh::Quality::Low)), 0, static_cast<int>(sh::Quality::Count) - 1);
    c.custom = CVarGetInteger(ROYALE_CVAR("ShadowCustom"), 0) != 0;
    const sh::Settings base = sh::Preset(static_cast<sh::Quality>(std::max(1, c.quality)));
    c.fine = base;
    const int size = CVarGetInteger(ROYALE_CVAR("ShadowMapSize"), base.mapSize);
    c.fine.mapSize = size <= 16 ? 16 : size <= 32 ? 32 : 64;
    c.fine.maxMaps = std::clamp(CVarGetInteger(ROYALE_CVAR("ShadowMax"), base.maxMaps), 1, sh::kMaxMaps);
    c.fine.range = static_cast<float>(std::clamp(CVarGetInteger(ROYALE_CVAR("ShadowRange"), static_cast<int>(base.range)), 300, 4000));
    c.fine.grid = std::clamp(CVarGetInteger(ROYALE_CVAR("ShadowGrid"), base.grid), 1, sh::kMaxGrid);
    c.fine.lightShadows = std::clamp(CVarGetInteger(ROYALE_CVAR("ShadowLightShadows"), base.lightShadows), 0, 2);
    c.fine.coverCheck = CVarGetInteger(ROYALE_CVAR("ShadowCover"), base.coverCheck ? 1 : 0) != 0;
    c.fine.refreshFar = std::clamp(CVarGetInteger(ROYALE_CVAR("ShadowRefresh"), base.refreshFar), 1, 4);
    c.fine.maxLights = std::clamp(CVarGetInteger(ROYALE_CVAR("DynLightMax"), base.maxLights), 0, sh::kMaxLights);
    c.fine.lightPools = CVarGetInteger(ROYALE_CVAR("LightPools"), base.lightPools ? 1 : 0) != 0;
    c.darkness = std::clamp(CVarGetInteger(ROYALE_CVAR("ShadowDarkness"), 100), 0, 200);
    c.softness = std::clamp(CVarGetInteger(ROYALE_CVAR("ShadowSoftness"), 100), 25, 300);
    c.lights = CVarGetInteger(ROYALE_CVAR("DynLights"), 1) != 0;
    c.sunOnBuildings = CVarGetInteger(ROYALE_CVAR("SunOnBuildings"), 1) != 0;
    for (ShadowKindInfo& k : gShadowKinds) k.on = CVarGetInteger((std::string(CVAR_SETTING("Royale.")) + k.cvar).c_str(), k.on ? 1 : 0) != 0;
}

// ---- the lights ----
// Timed flashes (an explosion, a bolt) are added by the code that makes them; steady lights (a spell, a burning arrow, a fire, a chest opening)
// are found again each frame. Of all of them, the ones that matter most from where the camera is get the game's light slots.
constexpr int kLightKeyExplosion = 1, kLightKeyFire = 2, kLightKeyBolt = 3, kLightKeyMagic = 4, kLightKeyReveal = 5, kLightKeyStrike = 6, kLightKeyArrow = 7;
constexpr int kMaxFlashes = 24;
sh::Light gFlashes[kMaxFlashes];
int gFlashCount = 0;
sh::Light gLightsNow[kMaxFlashes + 16];   // this frame's lights (flashes and steady ones), brightest first
float gLightsNowBright[kMaxFlashes + 16];
int gLightsNowCount = 0;

LightInfo gLightInfo[sh::kMaxLights];
LightNode* gLightNode[sh::kMaxLights] = {};
PlayState* gLightPlay = nullptr;   // the play state our light nodes belong to
int gLightIdle = 0;                // frames with nothing to light (the nodes are given back after a while)

void AddLightFlash(float x, float y, float z, uint8_t r, uint8_t g, uint8_t b, float radius, float life, int key) {
    if (!gShadowCfgLoaded) LoadShadowSettings();
    if (!gShadowCfg.lights || !DebugOn(kDbgShadows)) return;
    sh::Light l;
    l.pos = { x, y, z };
    l.r = r / 255.0f; l.g = g / 255.0f; l.b = b / 255.0f;
    l.radius = radius;
    l.life = life;
    l.key = key;
    l.flicker = key == kLightKeyExplosion ? 0.25f : 0.0f;
    if (!sh::MergeLight(gFlashes, &gFlashCount, kMaxFlashes, l)) {   // full: replace the faintest
        int weakest = 0;
        for (int i = 1; i < gFlashCount; i++)
            if (sh::FlashCurve(gFlashes[i].age, gFlashes[i].life) < sh::FlashCurve(gFlashes[weakest].age, gFlashes[weakest].life)) weakest = i;
        gFlashes[weakest] = l;
    }
}

// Gives the game back our light nodes (they come from a small table the scene's torches and fairies share).
void ReleaseLightNodes(PlayState* play) {
    for (int i = 0; i < sh::kMaxLights; i++) {
        if (gLightNode[i] != nullptr && play != nullptr && play == gLightPlay) LightContext_RemoveLight(play, &play->lightCtx, gLightNode[i]);
        gLightNode[i] = nullptr;
    }
}

// A new scene: the game cleared its light table, so the old nodes are gone with it; and the flashes belonged to the old place.
void ForgetDynamicLights() {
    for (LightNode*& n : gLightNode) n = nullptr;
    gLightPlay = nullptr;
    gFlashCount = 0;
    gLightsNowCount = 0;
}

// The steady lights of this frame: spells, burning, frozen and light arrows, the spin attack's charge, fire and explosions among the game's
// particles, and chests being opened.
void GatherSteadyLights(PlayState* play, int* n) {
    auto add = [&](float x, float y, float z, uint8_t r, uint8_t g, uint8_t b, float radius, float flicker, int key) {
        sh::Light l;
        l.pos = { x, y, z };
        l.r = r / 255.0f; l.g = g / 255.0f; l.b = b / 255.0f;
        l.radius = radius;
        l.life = 0.0f;   // steady
        l.flicker = flicker;
        l.key = key;
        sh::MergeLight(gLightsNow, n, kMaxFlashes + 16, l, 120.0f);
    };
    for (int cat = 0; cat < ACTORCAT_MAX; cat++) {
        for (Actor* a = play->actorCtx.actorLists[cat].head; a != nullptr; a = a->next) {
            const Vec3f& p = a->world.pos;
            switch (a->id) {
                case ACTOR_MAGIC_FIRE: add(p.x, p.y + 40.0f, p.z, 255, 120, 40, 420.0f, 0.35f, kLightKeyMagic); break;   // Din's Fire
                case ACTOR_MAGIC_WIND: add(p.x, p.y + 30.0f, p.z, 120, 255, 140, 200.0f, 0.1f, kLightKeyMagic); break;   // Farore's Wind
                case ACTOR_MAGIC_DARK: add(p.x, p.y + 30.0f, p.z, 130, 160, 255, 200.0f, 0.1f, kLightKeyMagic); break;   // Nayru's Love
                case ACTOR_ARROW_FIRE: add(p.x, p.y, p.z, 255, 130, 50, 170.0f, 0.3f, kLightKeyArrow); break;
                case ACTOR_ARROW_ICE: add(p.x, p.y, p.z, 140, 210, 255, 140.0f, 0.05f, kLightKeyArrow); break;
                case ACTOR_ARROW_LIGHT: add(p.x, p.y, p.z, 255, 250, 190, 190.0f, 0.05f, kLightKeyArrow); break;
                case ACTOR_EN_M_THUNDER: add(p.x, p.y + 20.0f, p.z, 170, 200, 255, 160.0f, 0.2f, kLightKeyMagic); break;   // the spin attack's charge
                default: break;
            }
        }
    }
    // The game's own particles: explosions and fire. Many puffs make one light (MergeLight).
    const EffectSsInfo& fx = sEffectSsInfo;
    if (fx.table != nullptr) {
        for (s32 i = 0; i < fx.tableSize; i++) {
            const EffectSs& e = fx.table[i];
            if (e.life < 0) continue;
            switch (e.type) {
                case EFFECT_SS_BOMB2: case EFFECT_SS_BOMB:
                    add(e.pos.x, e.pos.y, e.pos.z, 255, 170, 80, 320.0f * std::clamp(e.life / 12.0f, 0.3f, 1.0f), 0.3f, kLightKeyExplosion); break;
                case EFFECT_SS_EN_FIRE: case EFFECT_SS_FIRE_TAIL: case EFFECT_SS_D_FIRE:
                    add(e.pos.x, e.pos.y, e.pos.z, 255, 120, 40, 150.0f, 0.4f, kLightKeyFire); break;
                case EFFECT_SS_LIGHTNING: add(e.pos.x, e.pos.y, e.pos.z, 180, 200, 255, 260.0f, 0.5f, kLightKeyBolt); break;
                case EFFECT_SS_FHG_FLASH: add(e.pos.x, e.pos.y, e.pos.z, 255, 255, 200, 160.0f, 0.2f, kLightKeyMagic); break;
                default: break;
            }
        }
    }
    // Chests opening: the find rises out of them in its rarity's colour.
    for (const Reveal& r : gReveals) {
        const float k = 1.0f - r.age / kRevealSeconds;
        if (k <= 0.0f) continue;
        const Rgb c = kRarityRgb[static_cast<int>(r.rarity)];
        add(r.x, r.y + 40.0f, r.z, c.r, c.g, c.b, 90.0f + 90.0f * k, 0.05f, kLightKeyReveal);
    }
}

// Before the actors are drawn: age the flashes, find the steady lights, rank them all and put the best few in the game's light system.
void UpdateDynamicLights(PlayState* play) {
    if (play == nullptr || play != gPlayState) return;
    if (gLightPlay != nullptr && gLightPlay != play) ForgetDynamicLights();   // a new play state without a scene-init call: its table is new too
    const sh::Settings set = ShadowSettings();
    const bool on = gShadowCfg.lights && DebugOn(kDbgShadows) && InField() && set.maxLights > 0;
    constexpr float dt = 1.0f / royale::kTickHz;
    int live = 0;
    for (int i = 0; i < gFlashCount; i++) {
        gFlashes[i].age += dt;
        if (gFlashes[i].age < gFlashes[i].life) gFlashes[live++] = gFlashes[i];
    }
    gFlashCount = on ? live : 0;
    int n = 0;
    if (on) {
        for (int i = 0; i < gFlashCount; i++) gLightsNow[n++] = gFlashes[i];
        GatherSteadyLights(play, &n);
    }
    // Brightness now (flash curve, fire flicker), and rank by what they add to the view.
    const float t = static_cast<float>(play->gameplayFrames) / royale::kTickHz;
    const sh::V3 eye{ play->view.eye.x, play->view.eye.y, play->view.eye.z };
    int order[kMaxFlashes + 16];
    float score[kMaxFlashes + 16];
    for (int i = 0; i < n; i++) {
        const sh::Light& l = gLightsNow[i];
        gLightsNowBright[i] = sh::FlashCurve(l.age, l.life) * sh::Flicker(t, l.flicker, i * 31 + l.key) * l.intensity;
        score[i] = sh::LightScore(l, gLightsNowBright[i], eye);
        order[i] = i;
    }
    std::sort(order, order + n, [&](int a, int b) { return score[a] > score[b]; });
    {   // keep them in that order (the shadows and the ground glow read the brightest first)
        sh::Light sorted[kMaxFlashes + 16];
        float bright[kMaxFlashes + 16];
        for (int i = 0; i < n; i++) { sorted[i] = gLightsNow[order[i]]; bright[i] = gLightsNowBright[order[i]]; }
        for (int i = 0; i < n; i++) { gLightsNow[i] = sorted[i]; gLightsNowBright[i] = bright[i]; }
    }
    gLightsNowCount = n;
    // The game's light slots.
    const int slots = on ? std::min(set.maxLights, sh::kMaxLights) : 0;
    gLightIdle = n > 0 ? 0 : gLightIdle + 1;
    if (!on || gLightIdle > 3 * royale::kTickHz) { ReleaseLightNodes(play); gLightPlay = nullptr; return; }
    gLightPlay = play;
    for (int i = 0; i < sh::kMaxLights; i++) {
        LightInfo& info = gLightInfo[i];
        if (i >= slots) {   // more nodes than this quality uses: give them back
            if (gLightNode[i] != nullptr) { LightContext_RemoveLight(play, &play->lightCtx, gLightNode[i]); gLightNode[i] = nullptr; }
            continue;
        }
        if (i < n && gLightsNowBright[i] > 0.02f) {
            const sh::Light& l = gLightsNow[i];
            const float k = std::clamp(gLightsNowBright[i], 0.0f, 1.0f) * 255.0f;
            Lights_PointNoGlowSetInfo(&info, static_cast<s16>(std::clamp(l.pos.x, -32000.0f, 32000.0f)), static_cast<s16>(std::clamp(l.pos.y, -32000.0f, 32000.0f)),
                                      static_cast<s16>(std::clamp(l.pos.z, -32000.0f, 32000.0f)), static_cast<u8>(l.r * k), static_cast<u8>(l.g * k), static_cast<u8>(l.b * k),
                                      static_cast<s16>(std::min(l.radius, 2000.0f)));
        } else {
            Lights_PointNoGlowSetInfo(&info, 0, 0, 0, 0, 0, 0, 0);   // nothing in this slot: a light of no reach binds to nobody
        }
        if (gLightNode[i] == nullptr) gLightNode[i] = LightContext_InsertLight(play, &play->lightCtx, &info);   // null when the table is full: try next frame
    }
}

// ---- the ground under a shadow or a light ----
// Floor heights, by a lattice of world points, so a shadow that moves across the ground mostly finds its heights already measured.
royale::FrameCache<float, 8192> gShadowFloor;
bool gShadowFloorDeferred = false;
struct ShadowCollisionRegion { float x, z, radius; uint64_t signature; };
ShadowCollisionRegion gShadowCollisionRegions[BG_ACTOR_MAX];
int gShadowCollisionRegionCount = 0;

// Version only floor samples near changed dynamic collision. Moving one platform
// should not discard cached ground heights across the whole visible scene.
void GatherShadowCollisionRegions(PlayState* play) {
    gShadowCollisionRegionCount = 0;
    for (int i = 0; i < BG_ACTOR_MAX; ++i) {
        const auto& b = play->colCtx.dyna.bgActors[i];
        if (!b.actor || !(play->colCtx.dyna.bgActorFlags[i] & 1)) continue;
        uint64_t h = 1469598103934665603ULL;
        auto bytes = [&](const void* data, size_t n) {
            const auto* p = static_cast<const uint8_t*>(data);
            for (size_t j = 0; j < n; ++j) { h ^= p[j]; h *= 1099511628211ULL; }
        };
        bytes(&i, sizeof(i)); bytes(&b.actor, sizeof(b.actor)); bytes(&b.colHeader, sizeof(b.colHeader));
        bytes(&play->colCtx.dyna.bgActorFlags[i], sizeof(play->colCtx.dyna.bgActorFlags[i]));
        bytes(&b.curTransform.scale, sizeof(b.curTransform.scale));
        bytes(&b.curTransform.rot, sizeof(b.curTransform.rot));
        bytes(&b.curTransform.pos, sizeof(b.curTransform.pos));
        gShadowCollisionRegions[gShadowCollisionRegionCount++] = {
            static_cast<float>(b.boundingSphere.center.x), static_cast<float>(b.boundingSphere.center.z),
            static_cast<float>(b.boundingSphere.radius) + 96.0f, h};
    }
}
uint64_t ShadowFloorVersion(float x, float z) {
    uint64_t version = 0;
    for (int i = 0; i < gShadowCollisionRegionCount; ++i) {
        const auto& r = gShadowCollisionRegions[i];
        if (std::fabs(x-r.x) <= r.radius && std::fabs(z-r.z) <= r.radius)
            version ^= r.signature;
    }
    return version;
}
constexpr float kNoFloor = -1.0e9f;

float ShadowFloorAt(PlayState* play, float x, float z, float fromY) {
    Vec3f pos = { x, fromY, z };
    CollisionPoly* poly = nullptr;
    s32 bgId = BGCHECK_SCENE;
    const float y = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &pos);
    return y <= BGCHECK_Y_MIN + 1.0f ? kNoFloor : y;
}
float LatticeFloor(PlayState* play, int ix, int iz, float cell, float fromY) {
    const int band = static_cast<int>(std::floor(fromY / 96.0f));
    const uint64_t latticeKey = (static_cast<uint64_t>(static_cast<uint32_t>(ix) & 0xFFFFF) << 44) | (static_cast<uint64_t>(static_cast<uint32_t>(iz) & 0xFFFFF) << 24) |
                         (static_cast<uint64_t>(static_cast<uint32_t>(band) & 0xFFFF) << 8) | (static_cast<uint64_t>(cell) & 0xFF);
    const uint64_t key = latticeKey ^ ShadowFloorVersion(ix * cell, iz * cell);
    float y = kNoFloor;
    if (!gShadowFloor.Get(key, 540 + static_cast<uint32_t>((key ^ (key >> 8)) % 121), &y, [&] {
        return ShadowFloorAt(play, ix * cell, iz * cell, (band + 1) * 96.0f);
    })) gShadowFloorDeferred = true;
    return y;
}

// The lattice spacing for a patch of ground `half` wide each way, split in about `grid` squares: one of a few fixed sizes, so patches share points.
float LatticeCell(float half, int grid) {
    static const float kCells[] = { 8, 12, 16, 24, 32, 48, 64, 96 };
    const float want = 2.0f * half / std::max(1, grid);
    for (float c : kCells) if (c >= want) return c;
    return 96.0f;
}

// A patch of ground as a grid of vertices (at most (kMaxGrid + 2) squared), relative to `origin` (its first corner), heights from the lattice.
struct GroundPatch {
    int cols = 0, rows = 0;                 // squares
    float cell = 0;
    int ix0 = 0, iz0 = 0;
    float y[(sh::kMaxGrid + 3) * (sh::kMaxGrid + 3)];
};
bool MeasurePatch(PlayState* play, float cx, float cz, float half, int grid, float fromY, GroundPatch* g) {
    g->cell = LatticeCell(half, grid);
    g->ix0 = static_cast<int>(std::floor((cx - half) / g->cell));
    g->iz0 = static_cast<int>(std::floor((cz - half) / g->cell));
    g->cols = std::min(sh::kMaxGrid + 2, static_cast<int>(std::ceil((cx + half) / g->cell)) - g->ix0);
    g->rows = std::min(sh::kMaxGrid + 2, static_cast<int>(std::ceil((cz + half) / g->cell)) - g->iz0);
    if (g->cols <= 0 || g->rows <= 0) return false;
    bool any = false;
    gShadowFloorDeferred = false;
    for (int j = 0; j <= g->rows; j++)
        for (int i = 0; i <= g->cols; i++) {
            const float y = LatticeFloor(play, g->ix0 + i, g->iz0 + j, g->cell, fromY);
            g->y[j * (g->cols + 1) + i] = y;
            any = any || y > kNoFloor;
        }
    return any && !gShadowFloorDeferred;
}

// Draws a measured patch's squares as triangles (two rows of vertices at a time, as the water does). The vertices are already made.
void DrawPatch(PlayState* play, const Vtx* v, int cols, int rows) {
    OPEN_DISPS(play->state.gfxCtx);
    const int V = cols + 1;
    for (int j = 0; j < rows; j++) {
        if (!GfxHasRoom(play, 4 + cols)) break;
        gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(&v[j * V]), V, 0);
        gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(&v[(j + 1) * V]), V, V);
        for (int k = 0; k < cols; k++) gSP2Triangles(POLY_XLU_DISP++, k, k + 1, V + k + 1, 0, k, V + k + 1, V + k, 0);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// ---- the casters ----
struct ShadowSource {
    const void* key = nullptr;
    Actor* actor = nullptr;     // whose own round shadow to switch off
    int kind = 0;
    bool moving = true;         // animated or moving: repaint each frame (a still thing keeps its map)
    float floorY = 0;
    sh::V3 centre;
    sh::Capsule caps[16];
    int n = 0;
};

struct ShadowMapState {
    uint32_t seen = 0, painted = 0;
    sh::MapFrame frame;
    sh::V3 centre;
    float cover = 0.0f;         // 0 in the open, 1 under a roof (no sun shadow), eased
    uint32_t coverAt = 0;
    int size = 0;
    std::vector<uint8_t> map;
};
std::unordered_map<uint64_t, std::unique_ptr<ShadowMapState>> gShadowMaps;
struct SwappedShadow { ActorShadowFunc draw; s16 id; };
std::unordered_map<Actor*, SwappedShadow> gShadowSwapped;   // actors whose own round shadow we switched off, with what it was
alignas(16) uint8_t gShadowTex[2][sh::kMaxMaps][sh::kMaxMapSize * sh::kMaxMapSize];   // this frame's maps (two sets: one being drawn while the next is made)

void ForgetShadows() {
    gShadowMaps.clear();
    gShadowSwapped.clear();   // the actors went with the scene
    gShadowFloor.Invalidate();
}

// Joints of a Link the game has drawn (puppets are Links too).
bool LinkCapsules(Player* p, ShadowSource* s) {
    sh::V3 parts[sh::PartCount];
    for (int i = 0; i < sh::PartCount; i++) parts[i] = { p->bodyPartsPos[i].x, p->bodyPartsPos[i].y, p->bodyPartsPos[i].z };
    const sh::V3 at{ p->actor.world.pos.x, p->actor.world.pos.y, p->actor.world.pos.z };
    if (!sh::BodyValid(parts, at)) return false;
    s->n = sh::BodyCapsules(parts, s->caps);
    s->centre = parts[sh::Waist];
    return true;
}

void GatherShadowSources(PlayState* play, std::vector<ShadowSource>& out, float range, const std::vector<Actor*>* selected = nullptr) {
    out.clear();
    const Vec3f eye = play->view.eye;
    auto near = [&](const Vec3f& p, float pad) { return std::hypot(p.x - eye.x, p.z - eye.z) < range + pad; };
    Player* me = GET_PLAYER(play);
    auto floorOf = [&](Actor* a, float above) {   // under an actor the game does not keep a floor for
        if (!selected) return a->world.pos.y;
        const float y = ShadowFloorAt(play, a->world.pos.x, a->world.pos.z, a->world.pos.y + above);
        return y > kNoFloor ? y : a->world.pos.y;
    };
    // Links: you, other players and bots, fallen bodies.
    for (Actor* a = play->actorCtx.actorLists[ACTORCAT_PLAYER].head; a != nullptr; a = a->next) {
        const bool mine = a == &me->actor;
        if (!gShadowKinds[mine ? kShadowYou : kShadowPlayers].on || a->draw == nullptr) continue;
        if (!mine && a->draw != Puppet_Draw && a->draw != Corpse_Draw) continue;
        if (!near(a->world.pos, 0) || (selected && std::find(selected->begin(), selected->end(), a) == selected->end())) continue;
        ShadowSource s;
        s.key = a; s.actor = a; s.kind = mine ? kShadowYou : kShadowPlayers;
        if (selected) { if (!LinkCapsules(reinterpret_cast<Player*>(a), &s)) continue; }
        else { s.n = 1; s.centre = {a->world.pos.x, a->world.pos.y + 30, a->world.pos.z}; }
        s.floorY = a->floorHeight > BGCHECK_Y_MIN + 10.0f && a->floorHeight <= a->world.pos.y + 5.0f ? a->floorHeight : floorOf(a, 30.0f);
        out.push_back(s);
    }
    // Everything else, told apart by how the mod draws it.
    for (int cat = 0; cat < ACTORCAT_MAX; cat++) {
        if (cat == ACTORCAT_PLAYER) continue;
        for (Actor* a = play->actorCtx.actorLists[cat].head; a != nullptr; a = a->next) {
            if (a->draw == nullptr || !near(a->world.pos, 400.0f) ||
                (selected && std::find(selected->begin(), selected->end(), a) == selected->end())) continue;
            ShadowSource s;
            s.key = a; s.actor = a;
            const Vec3f& p = a->world.pos;
            const sh::V3 feet{ p.x, p.y, p.z };
            if (a->draw == Maya_Draw || a->draw == MayaCompanion_Draw || a->draw == Ally_Draw || a->draw == Cat_Draw || a->draw == Baby_Draw || a->draw == Lilo_Draw) {
                if (!gShadowKinds[kShadowAllies].on) continue;
                s.kind = kShadowAllies;
                const bool person = a->draw == Maya_Draw || a->draw == MayaCompanion_Draw || a->draw == Ally_Draw;
                const float h = person ? 62.0f : a->draw == Cat_Draw ? 16.0f : a->draw == Baby_Draw ? 26.0f : 30.0f;
                const float r = person ? 11.0f : a->draw == Cat_Draw ? 7.0f : 10.0f;
                s.n = sh::UprightCapsule(feet, h, r, s.caps);
                s.floorY = floorOf(a, 20.0f);
            } else if (a->draw == Boss_Draw) {
                if (!gShadowKinds[kShadowBosses].on) continue;
                auto of = gBossOf.find(a);
                if (of == gBossOf.end()) continue;
                auto b = gBosses.find(of->second);
                if (b == gBosses.end() || b->second.fade < 0.3f) continue;
                const bool dragon = royale::IsDragonKind(KindOf(b->second));
                const float body = (dragon ? 90.0f : 60.0f) * royale::kBossDefs[b->second.kind].scale;
                s.kind = kShadowBosses;
                if (dragon) {   // a big body round its middle, wherever it flies
                    const sh::V3 f{ a->focus.pos.x, a->focus.pos.y, a->focus.pos.z };
                    s.caps[0] = { { f.x, f.y - 0.3f * body, f.z }, { f.x, f.y + 0.3f * body, f.z }, 0.6f * body };
                    s.n = 1;
                } else {
                    s.n = sh::UprightCapsule(feet, 1.7f * body, 0.42f * body, s.caps);
                }
                s.floorY = floorOf(a, 40.0f);
            } else if (a->draw == Cart_Draw) {
                if (!gShadowKinds[kShadowCarts].on) continue;
                auto of = gCartOf.find(a);
                if (of == gCartOf.end()) continue;
                auto v = gCarts.find(of->second);
                if (v == gCarts.end()) continue;
                const royale::CartBody& body = v->second.body;
                s.kind = kShadowCarts;
                s.n = sh::BoxCapsules({ body.x, body.y + 8.0f, body.z }, body.yaw, 128.0f, 92.0f, 44.0f, s.caps);
                s.floorY = floorOf(a, 30.0f);
                s.moving = v->second.body.speed != 0.0f || v->second.air > 0.0f;
            } else if (a->draw == Prop_DrawCustom) {
                if (!gShadowKinds[kShadowProps].on) continue;
                auto of = gPropOf.find(a);
                if (of == gPropOf.end()) continue;
                auto pa = gProps.find(of->second);
                if (pa == gProps.end()) continue;
                s.kind = kShadowProps;
                s.moving = false;
                const float k = pa->second.scale;
                switch (static_cast<royale::MeshKind>(pa->second.meshKind)) {
                    case royale::MeshKind::Boulder: {
                        const float r = 64.0f * k, tall = royale::BoulderHeight(static_cast<int>(pa->second.variant % royale::kBoulderShapes)) * k;
                        s.caps[0] = { { p.x, p.y + 0.6f * r, p.z }, { p.x, p.y + std::max(0.6f * r, tall - 0.6f * r), p.z }, r };
                        s.n = 1;
                        break;
                    }
                    case royale::MeshKind::Rock: s.caps[0] = { { p.x, p.y + 16.0f, p.z }, { p.x, p.y + 16.0f, p.z }, 26.0f }; s.n = 1; break;
                    case royale::MeshKind::Pillar: s.caps[0] = { { p.x, p.y + 40.0f, p.z }, { p.x, p.y + 175.0f, p.z }, 36.0f }; s.n = 1; break;
                    default: continue;
                }
                s.floorY = p.y;
            } else if (a->draw == Loot_Draw || a->id == ACTOR_EN_BOX) {
                if (!gShadowKinds[kShadowChests].on) continue;
                s.kind = kShadowChests;
                s.moving = false;
                if (a->id == ACTOR_EN_BOX) {
                    s.n = sh::BoxCapsules(feet, a->shape.rot.y * (3.14159265f / 32768.0f), 46.0f, 34.0f, 30.0f, s.caps);
                    s.floorY = p.y;
                } else {
                    s.caps[0] = { feet, feet, 7.0f };
                    s.n = 1;
                    s.floorY = floorOf(a, 4.0f);
                }
            } else {
                continue;
            }
            s.centre = { p.x, p.y + 20.0f, p.z };
            out.push_back(s);
        }
    }
}

// The sun's (or moon's) shadow light now, from the sky's own numbers.
sh::ShadowLight SunShadowNow() {
    float tint[3];
    return sh::SunShadow(SkyLightNow().sunH, SkyOvercast(tint));
}

// Is something at `at` under cover from the light (a roof, a bridge, the inside of a building)? Asked now and then, not every frame.
bool CoveredFrom(PlayState* play, const sh::V3& at, const sh::V3& toward) {
    Vec3f a = { at.x, at.y + 10.0f, at.z }, b = { at.x + toward.x * 900.0f, at.y + 10.0f + toward.y * 900.0f, at.z + toward.z * 900.0f }, hit;
    CollisionPoly* poly = nullptr;
    s32 bgId = BGCHECK_SCENE;
    return BgCheck_EntityLineTest1(&play->colCtx, &a, &b, &hit, &poly, true, true, true, true, &bgId) != 0;
}

// One shadow: paint (or reuse) its map, then lay it on the ground.
bool DrawOneShadow(PlayState* play, const ShadowSource& s, int lightIndex, const sh::ShadowLight& light, float alpha, const sh::Settings& set, int slot, bool farAway) {
    const uint64_t id = (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(s.key)) << 2) | static_cast<uint64_t>(lightIndex & 3);
    std::unique_ptr<ShadowMapState>& st = gShadowMaps[id];
    if (!st) st.reset(new ShadowMapState());
    ShadowMapState& m = *st;
    const uint32_t frame = play->state.frames;
    const float soft = gShadowCfg.softness / 100.0f;
    const bool fresh = m.seen + 2 < frame || m.size != set.mapSize;
    m.seen = frame;
    // Repaint when it moves or animates (far ones only every few frames), or when the light turns.
    const bool turned = sh::Dot(m.frame.toward, light.toward) < 0.9995f;
    const bool moved = sh::Len(sh::Sub(m.centre, s.centre)) > 0.5f || std::fabs(m.frame.groundY - s.floorY) > 0.5f;
    const int refresh = s.kind == kShadowYou ? 1 : farAway ? std::max(1, set.refreshFar) : set.mapSize <= 16 ? 2 : 1;
    const bool due = refresh == 1 || ((frame + static_cast<uint32_t>(reinterpret_cast<uintptr_t>(s.key) >> 4)) % refresh == 0);
    if (fresh || ((s.moving || moved || turned) && due)) {
        m.frame = sh::PlanMap(s.caps, s.n, light.toward, s.floorY, soft);
        m.size = set.mapSize;
        m.map.resize(static_cast<size_t>(m.size) * m.size);
        sh::Rasterize(m.frame, s.caps, s.n, soft, m.map.data(), m.size);
        m.painted = frame;
        m.centre = s.centre;
    }
    uint8_t* tex = gShadowTex[frame & 1][slot];
    std::memcpy(tex, m.map.data(), static_cast<size_t>(m.size) * m.size);

    // The ground under it.
    const sh::MapFrame& f = m.frame;
    GroundPatch g;
    if (!MeasurePatch(play, f.cx, f.cz, f.half, set.grid, f.groundY + 30.0f, &g)) return false;
    const int V = g.cols + 1, count = V * (g.rows + 1);
    Vtx* v = static_cast<Vtx*>(FrameAlloc(play, sizeof(Vtx) * count));
    if (v == nullptr) return false;
    // Vertices relative to the middle of the map: the frames the game blends in between slide the whole shadow along with its caster.
    const float ox = g.ix0 * g.cell, oz = g.iz0 * g.cell;
    const float mx = f.cx, my = f.groundY, mz = f.cz;
    const float lift = 1.5f + 0.04f * g.cell;   // a hair over the ground; more on a coarse mesh, whose straight squares can cut under a bump
    for (int j = 0; j <= g.rows; j++)
        for (int i = 0; i <= g.cols; i++) {
            const float x = ox + i * g.cell, z = oz + j * g.cell;
            float y = g.y[j * V + i];
            const bool none = y <= kNoFloor;
            if (none) y = f.groundY;
            float su, tv;
            sh::MapUV(f, x, y, z, &su, &tv);
            const float a = none ? 0.0f : alpha * sh::LedgeFade(y, f.groundY);
            Vtx& o = v[j * V + i];
            o.v.ob[0] = static_cast<s16>(std::lround(x - mx));
            o.v.ob[1] = static_cast<s16>(std::lround(y - my + lift));
            o.v.ob[2] = static_cast<s16>(std::lround(z - mz));
            o.v.flag = 0;
            // Texture coordinates in texels (S10.5), clamped well inside the renderer's 16 bits.
            o.v.tc[0] = static_cast<s16>(std::lround(std::clamp(su * m.size, -500.0f, 500.0f) * 32.0f));
            o.v.tc[1] = static_cast<s16>(std::lround(std::clamp(tv * m.size, -500.0f, 500.0f) * 32.0f));
            o.v.cn[0] = o.v.cn[1] = o.v.cn[2] = 255;
            o.v.cn[3] = static_cast<u8>(std::clamp(a * 255.0f, 0.0f, 255.0f));
        }
    if (!GfxHasRoom(play, 24 + g.rows * (4 + g.cols))) return false;
    int shift = 4;
    while ((1 << shift) < m.size) shift++;
    FrameInterpolation_RecordOpenChild(s.key, 0x5AD0 + lightIndex);
    OPEN_DISPS(play->state.gfxCtx);
    gSPInvalidateTexCache(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(tex));
    gDPLoadTextureBlock(POLY_XLU_DISP++, tex, G_IM_FMT_I, G_IM_SIZ_8b, m.size, m.size, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, shift, shift,
                        G_TX_NOLOD, G_TX_NOLOD);
    Matrix_Translate(mx, my, mz, MTXMODE_NEW);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    CLOSE_DISPS(play->state.gfxCtx);
    DrawPatch(play, v, g.cols, g.rows);
    FrameInterpolation_RecordCloseChild();
    return true;
}

// The soft glow on the ground under a light.
void DrawLightPool(PlayState* play, const sh::Light& l, float bright, int index) {
    const float half = std::clamp(l.radius * 0.5f, 40.0f, 260.0f);
    const float ground = ShadowFloorAt(play, l.pos.x, l.pos.z, l.pos.y + 20.0f);
    if (ground <= kNoFloor || l.pos.y - ground > l.radius * 0.8f) return;   // too high above the ground to light it
    const float lift = std::clamp(1.0f - (l.pos.y - ground) / (l.radius * 0.8f), 0.0f, 1.0f);
    GroundPatch g;
    if (!MeasurePatch(play, l.pos.x, l.pos.z, half, 6, ground + 40.0f, &g)) return;
    const int V = g.cols + 1, count = V * (g.rows + 1);
    Vtx* v = static_cast<Vtx*>(FrameAlloc(play, sizeof(Vtx) * count));
    if (v == nullptr) return;
    const float ox = g.ix0 * g.cell, oz = g.iz0 * g.cell;
    // Warm, soft and painterly: the light's colour lifted toward white in the middle, fading out to nothing at the edge.
    for (int j = 0; j <= g.rows; j++)
        for (int i = 0; i <= g.cols; i++) {
            const float x = ox + i * g.cell, z = oz + j * g.cell;
            float y = g.y[j * V + i];
            const bool none = y <= kNoFloor;
            if (none) y = ground;
            const float d = std::hypot(x - l.pos.x, z - l.pos.z) / half;
            const float fall = std::max(0.0f, 1.0f - d * d);
            const float a = none ? 0.0f : 0.42f * bright * lift * fall * fall * sh::LedgeFade(y, ground);
            const float w = 0.35f * fall;
            Vtx& o = v[j * V + i];
            o.v.ob[0] = static_cast<s16>(std::lround(x - ox));
            o.v.ob[1] = static_cast<s16>(std::lround(y - ground + 1.5f));
            o.v.ob[2] = static_cast<s16>(std::lround(z - oz));
            o.v.flag = 0; o.v.tc[0] = o.v.tc[1] = 0;
            o.v.cn[0] = static_cast<u8>(std::clamp((l.r + (1.0f - l.r) * w) * 255.0f, 0.0f, 255.0f));
            o.v.cn[1] = static_cast<u8>(std::clamp((l.g + (1.0f - l.g) * w) * 255.0f, 0.0f, 255.0f));
            o.v.cn[2] = static_cast<u8>(std::clamp((l.b + (1.0f - l.b) * w) * 255.0f, 0.0f, 255.0f));
            o.v.cn[3] = static_cast<u8>(std::clamp(a * 255.0f, 0.0f, 255.0f));
        }
    if (!GfxHasRoom(play, 16 + g.rows * (4 + g.cols))) return;
    {
        DrawIdentity identity(&gLightsNow[index], g.ix0, g.iz0);   // the lights are ranked anew each frame: never blend one light's glow into another's
        OPEN_DISPS(play->state.gfxCtx);
        Matrix_Translate(ox, ground, oz, MTXMODE_NEW);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        CLOSE_DISPS(play->state.gfxCtx);
        DrawPatch(play, v, g.cols, g.rows);
    }
}

// Gives back the game's round shadow to anything we no longer draw a dynamic one for (if it still exists).
void RestoreRoundShadows(PlayState* play, const std::unordered_set<Actor*>& keep) {
    if (gShadowSwapped.empty()) return;
    std::unordered_set<Actor*> live;
    for (int cat = 0; cat < ACTORCAT_MAX; cat++)
        for (Actor* a = play->actorCtx.actorLists[cat].head; a != nullptr; a = a->next) live.insert(a);
    for (auto it = gShadowSwapped.begin(); it != gShadowSwapped.end();) {
        if (keep.count(it->first)) { ++it; continue; }
        if (live.count(it->first) && it->first->id == it->second.id && it->first->shape.shadowDraw == nullptr) it->first->shape.shadowDraw = it->second.draw;
        it = gShadowSwapped.erase(it);
    }
}

void DrawShadowsAndLights(PlayState* play) {
    static std::unordered_set<Actor*> keep;
    keep.clear();
    const sh::Settings set = ShadowSettings();
    const bool shadowsOn = DebugOn(kDbgShadows) && set.maxMaps > 0;
    if (!shadowsOn) { RestoreRoundShadows(play, keep); }
    if (!DebugOn(kDbgShadows)) return;
    const uint32_t frame = play->state.frames;
    GatherShadowCollisionRegions(play);
    gShadowFloor.BeginFrame(frame, 64);
    Feat("draw: shadows and lights");
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_XLU_DISP++, G_LIGHTING | G_CULL_BACK | G_CULL_FRONT | G_FOG | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(POLY_XLU_DISP++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    gDPSetRenderMode(POLY_XLU_DISP++, G_RM_PASS, G_RM_ZB_XLU_DECAL2);   // on the ground, tested against it but never hiding anything
    CLOSE_DISPS(play->state.gfxCtx);

    // The ground glow under the lights, first: shadows lie over it.
    if (set.lightPools && gShadowCfg.lights && DebugOn(kDbgShadows)) {
        OPEN_DISPS(play->state.gfxCtx);
        gDPPipeSync(POLY_XLU_DISP++);
        gSPTexture(POLY_XLU_DISP++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
        gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED);
        CLOSE_DISPS(play->state.gfxCtx);
        int pools = 0;
        for (int i = 0; i < gLightsNowCount && pools < 6; i++) {
            if (gLightsNowBright[i] < 0.05f) continue;
            DrawLightPool(play, gLightsNow[i], gLightsNowBright[i], pools++);
        }
    }
    if (!shadowsOn) return;

    // Reserve a separate allowance for casters, so light pools cannot starve the local shadow.
    gShadowFloor.BeginFrame(frame, 256);
    // The shadows.
    static std::vector<ShadowSource> sources;
    GatherShadowSources(play, sources, set.range);
    const Vec3f eye = play->view.eye;
    static std::vector<sh::Candidate> cands;
    cands.clear();
    for (size_t i = 0; i < sources.size(); i++) {
        const ShadowSource& s = sources[i];
        cands.push_back({ std::hypot(s.centre.x - eye.x, s.centre.z - eye.z), s.kind == kShadowYou ? 3 : (s.kind == kShadowPlayers || s.kind == kShadowBosses) ? 1 : 0,
                          static_cast<int>(i) });
    }
    static std::vector<int> chosen;
    chosen.resize(cands.size());
    const int picked = sh::Choose(cands.data(), static_cast<int>(cands.size()), set.maxMaps, chosen.data());
    static std::vector<Actor*> selected;
    selected.clear();
    for (int i = 0; i < picked; ++i) selected.push_back(sources[chosen[i]].actor);
    GatherShadowSources(play, sources, set.range, &selected);
    std::sort(sources.begin(), sources.end(), [&](const ShadowSource& a, const ShadowSource& b) {
        return std::find(selected.begin(), selected.end(), a.actor) < std::find(selected.begin(), selected.end(), b.actor);
    });

    const sh::ShadowLight sun = SunShadowNow();
    const float darkness = gShadowCfg.darkness / 100.0f;
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(POLY_XLU_DISP++);
    // The colour of shade in Ocarina of Time's painted look: a deep, cool indigo rather than black, so the grass under it still reads.
    gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 26, 24, 58, 255);
    gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, SHADE, 0, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED);
    gSPTexture(POLY_XLU_DISP++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetTextureFilter(POLY_XLU_DISP++, G_TF_BILERP);
    CLOSE_DISPS(play->state.gfxCtx);
    int slot = 0;
    for (size_t c = 0; c < sources.size() && slot < set.maxMaps; c++) {
        const ShadowSource& s = sources[c];
        const float dist = std::hypot(s.centre.x - eye.x, s.centre.z - eye.z);
        const float fade = sh::RangeFade(dist, set.range);
        if (fade <= 0.0f || s.n <= 0) continue;
        const bool farAway = dist > 0.5f * set.range;
        // The sun (or moon), unless it is under cover.
        const uint64_t coverId = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(s.key)) << 2;
        float cover = 0.0f;
        if (set.coverCheck && sun.strength > 0.0f) {
            std::unique_ptr<ShadowMapState>& st = gShadowMaps[coverId];
            if (!st) st.reset(new ShadowMapState());
            if (frame - st->coverAt >= 8 || st->coverAt == 0) {
                const bool covered = CoveredFrom(play, s.centre, sun.toward);
                st->cover += ((covered ? 1.0f : 0.0f) - st->cover) * (st->coverAt == 0 ? 1.0f : 0.5f);
                st->coverAt = frame;
            }
            cover = st->cover;
        }
        const float sunAlpha = sun.strength * darkness * fade * (1.0f - cover);
        bool drawn = false;
        if (sunAlpha > 0.01f) drawn = DrawOneShadow(play, s, 0, sun, sunAlpha, set, slot++, farAway);
        // Nearby lights (Medium and up): the strongest give a second shadow, away from them.
        for (int k = 0, given = 0; k < gLightsNowCount && given < set.lightShadows && slot < set.maxMaps; k++) {
            const sh::Light& l = gLightsNow[k];
            const sh::ShadowLight pl = sh::PointShadow(l.pos, l.radius, gLightsNowBright[k], s.centre);
            const float a = pl.strength * darkness * fade;
            if (a < 0.05f) continue;
            drawn = DrawOneShadow(play, s, 1 + given, pl, a, set, slot++, farAway) || drawn;
            given++;
        }
        if (drawn && s.actor != nullptr) {   // its own round shadow goes while it has this one
            keep.insert(s.actor);
            if (s.actor->shape.shadowDraw != nullptr) {
                gShadowSwapped.emplace(s.actor, SwappedShadow{ s.actor->shape.shadowDraw, s.actor->id });
                s.actor->shape.shadowDraw = nullptr;
            }
        }
    }
    RestoreRoundShadows(play, keep);
    // Forget maps of things that are gone or out of range.
    if ((frame & 63) == 0)
        for (auto it = gShadowMaps.begin(); it != gShadowMaps.end();) it = frame - it->second->seen > 120 && frame - it->second->coverAt > 120 ? gShadowMaps.erase(it) : std::next(it);
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(POLY_XLU_DISP++);
    gSPTexture(POLY_XLU_DISP++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    CLOSE_DISPS(play->state.gfxCtx);
}

// Hyrule Kingdom's lit faces (stone, wood, roofs): the sun's direction and colour at this time of day (the sun the sky draws), warm at sunrise
// and sunset, or the moon's at night, instead of the fixed late-morning sun they were baked with. By day it is close to the baked light.
bool BuildingSunOn() {
    if (!gShadowCfgLoaded) LoadShadowSettings();
    return gShadowCfg.sunOnBuildings && DebugOn(kDbgShadows);
}
void BuildingSunNow(float dir[3], float sun[3], float amb[3]) {
    const SkyLight L = SkyLightNow();
    float tint[3];
    const float ov = SkyOvercast(tint);
    const bool moon = L.sunH < 0.0f;
    const float h = std::max(0.12f, std::fabs(L.sunH));
    const float c = std::sqrt(std::max(0.0f, 1.0f - h * h)) * (moon ? -1.0f : 1.0f);
    dir[0] = std::cos(0.6f) * c; dir[1] = h; dir[2] = std::sin(0.6f) * c;
    const float baked[3] = { 107.0f / 255.0f, 104.0f / 255.0f, 98.0f / 255.0f }, bakedAmb = 148.0f / 255.0f;
    const float warm[3] = { 1.0f, 0.78f, 0.55f }, moonCol[3] = { 0.42f, 0.48f, 0.68f }, nightAmb[3] = { 0.70f, 0.74f, 0.92f };
    const float dim = 1.0f - 0.6f * ov;
    for (int i = 0; i < 3; i++) {
        const float day = baked[i] * (0.8f + 0.25f * std::max(0.0f, L.sunH)) * (1.0f - L.twilight * (1.0f - warm[i]));
        sun[i] = std::clamp((L.day * day + L.night * baked[i] * moonCol[i]) * dim, 0.0f, 1.0f);
        amb[i] = std::clamp(bakedAmb * (0.62f + 0.38f * L.day) * (L.night * nightAmb[i] + (1.0f - L.night)) * (0.85f + 0.15f * dim), 0.0f, 1.0f);
    }
}


} // namespace

