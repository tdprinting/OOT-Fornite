// ---- water ---------------------------------------------------------------------------------------------------------------------------
// The water is ours from the horizon to the player's toes (the game's own water is a flat, scrolling-texture sheet; on lakes and rivers it is still
// there under ours, which hides it). On every water box (lakes, rivers) and the island maps' sea:
//   * splashes, rings, bubbles and spray drawn by us from soft 256 x 256 sprites (not the game's EffectSs cut-outs: those are caught and dropped
//     or replaced each frame), for everyone, the local player included; rain makes rings on the water;
//   * displacement: swimmers, bots and carts push the surface down under them and ahead of them, and leave rings that spread out and fade,
//     with a trail of foam, all in a height field that also rolls with the swell (bigger in wind and storms);
//   * a three-level mesh (16, 64 and 256 units a square, each inside the next) tinted by depth, foaming in the shallows, showing the sky in it
//     (stronger the lower you look: a Fresnel curve), the clouds mirrored in it and a glitter path for the sun and the moon;
//   * a stylised reflection of anyone standing or swimming at the water: a smeared, wobbling strip in their colours, mirrored over the surface
//     toward the camera. It is not a true mirror image of the scene (the N64 renderer has no render-to-texture), and trees and cliffs are not mirrored;
//   * a gentle current that carries a swimming player, and an underwater look (blue tint, light shafts) when the camera is below the surface.
// Everything is cosmetic and local (nothing is sent), apart from the swimmer's own drift. Debug switch "Water"; the Graphics page has the rest.
// The water boxes of an Ocarina of Time scene. In a scene with rooms stacked over each other (the Water Temple, Zora's Domain, Jabu-Jabu) one place
// can have water at several heights, so the box picked is the one whose surface is nearest the player (the game takes the first it finds).
float gWaterRefY = 0.0f;   // the height the water is looked for near: the player's, set each frame by DrawWater
bool GameWaterSurfaceAt(float x, float z, float* y) {
    PlayState* play = gPlayState;
    if (play == nullptr) return false;
    if (play->sceneNum == SCENE_ZORAS_DOMAIN) {   // the domain's pool has a box of its own in the game's code
        float s = gWaterRefY;
        WaterBox* box = nullptr;
        if (WaterBox_GetSurface1(play, &play->colCtx, x, z, &s, &box) == 0) return false;
        *y = s;
        return true;
    }
    const CollisionHeader* h = play->colCtx.colHeader;
    if (h == nullptr || h->numWaterBoxes == 0 || h->waterBoxes == nullptr) return false;
    bool found = false;
    float bestD = FLT_MAX;
    for (int i = 0; i < h->numWaterBoxes; i++) {
        const WaterBox& b = h->waterBoxes[i];
        const u32 room = (b.properties >> 13) & 0x3F;
        // The room you came from stays loaded through an open passage, and its water is hidden too (UpdateGameWater), so ours draws there as well.
        const bool roomLoaded = room == 0x3F || room == static_cast<u32>(play->roomCtx.curRoom.num) ||
                                (play->roomCtx.prevRoom.num >= 0 && room == static_cast<u32>(play->roomCtx.prevRoom.num));
        if (!roomLoaded || (b.properties & 0x80000) != 0) continue;
        if (!(b.xMin < x && x < b.xMin + b.xLength && b.zMin < z && z < b.zMin + b.zLength)) continue;
        const float d = std::fabs(static_cast<float>(b.ySurface) - gWaterRefY);
        if (d < bestD) { bestD = d; *y = b.ySurface; found = true; }
    }
    return found;
}
// Something that changes whenever the water of the scene might: the map, the scene, the room the player is in, the level of every box (the Water
// Temple raises and lowers its water, Lake Hylia fills up), and roughly how high the player is. The water's remembered lookups start over when it changes.
uint64_t WaterSceneKey() {
    uint64_t k = static_cast<uint64_t>(gMapId) * 1000003u;
    if (gPlayState == nullptr) return k;
    k = k * 31u + static_cast<uint64_t>(gPlayState->sceneNum);
    k = k * 31u + static_cast<uint64_t>(static_cast<u8>(gPlayState->roomCtx.curRoom.num));
    const CollisionHeader* h = gPlayState->colCtx.colHeader;
    if (h != nullptr && h->waterBoxes != nullptr)
        for (int i = 0; i < h->numWaterBoxes; i++) k = k * 1099511628211ull + static_cast<uint64_t>(static_cast<int64_t>(h->waterBoxes[i].ySurface) + 40000);
    if (!OnIsland()) k = k * 31u + static_cast<uint64_t>(static_cast<int64_t>(std::floor(gWaterRefY / 500.0f)) + 1000);
    return k;
}
bool WaterSurfaceAt(float x, float z, float* y);
// The floor under the water at a point: on the island maps their ground, elsewhere the first floor under the surface (cast from just above it,
// so a ceiling or an upper floor over the water is never taken for its bed).
bool WaterFloorAt(float x, float z, float* outY) {
    if (OnIsland()) return RawFloorAt(x, z, outY);
    float surface = 0;
    if (gPlayState == nullptr || !WaterSurfaceAt(x, z, &surface)) return RawFloorAt(x, z, outY);
    Vec3f pos = { x, surface + 4.0f, z };
    for (int tries = 0; tries < 4; tries++) {
        CollisionPoly poly;
        s32 bgId = BGCHECK_SCENE;
        const float y = BgCheck_AnyRaycastFloor2(&gPlayState->colCtx, &poly, &bgId, &pos);
        if (y <= BGCHECK_Y_MIN + 1.0f) return false;
        if (gSolidBgId >= 0 && bgId == gSolidBgId) { pos.y = y - 2.0f; continue; }
        if (outY) *outY = y;
        return true;
    }
    return false;
}
bool WaterSurfaceAt(float x, float z, float* y) {
    namespace fn = royale::fortnite;
    if (OnIsland()) {
        if (std::fabs(x) >= fn::kHalfX || std::fabs(z) >= fn::kHalfZ) return false;
        *y = OnRiftlandsTerrain() ? royale::RiftlandsWaterY({x,z}) : OnMoonfallTerrain() ? royale::MoonfallWaterY({x,z}) : OnSkyloftTerrain() ? royale::SkyloftWaterY({x,z}) : static_cast<float>(fn::kWaterY);
        return true;
    }
    return GameWaterSurfaceAt(x, z, y);
}


// ---- the game's own water, hidden -------------------------------------------------------------------------------------------------------
// Ours replaces it everywhere, so the game's flat scrolling sheets would only show through ours (a second surface a few units under, the old
// texture peeking out where ours is thin). Two kinds are hidden while our water is drawn (Graphics: "Hide the game's water"):
//   * the water drawn with a room: each loaded room's see-through display lists are looked through once, and every flat triangle lying on a
//     water box's surface (within a few units, inside its square) is swapped for a no-op. The display list stays loaded while it is changed,
//     and everything is put back as it was when the option or our water is switched off;
//   * the water drawn by an actor: the Water Temple's rising and falling water, Kakariko's well, the Forest Temple's well, the Colossus oasis, the
//     Bottom of the Well's water and Lake Hylia's water plane are not drawn (they still raise and lower their water boxes, and ours follows).
struct GameWaterPatch { Gfx* at; Gfx orig; Gfx mine; };
std::vector<GameWaterPatch> gGameWaterPatches;
std::unordered_set<const Gfx*> gGameWaterPatched;
std::unordered_map<const void*, std::shared_ptr<Ship::IResource>> gGameWaterHolds;   // the display lists looked at, kept loaded so the changes (and their undo) stay valid
std::unordered_set<const Gfx*> gGameWaterLooked;                 // display lists already looked through at the current water levels
std::unordered_map<Actor*, ActorFunc> gGameWaterActors;          // water actors not drawn, and their own draw
uint64_t gGameWaterKey = 0;
int gGameWaterHidden = 0;                                         // triangles hidden (shown on the Graphics page)

bool GameWaterHidden() {
    if (!DebugOn(kDbgWater) || gPlayState == nullptr || OnIsland() || !WorldFxOn(kRegWater, false)) return false;
    static bool loaded = false;
    if (!loaded) { RefreshRegularSettings(); loaded = true; }
    return gRegular[kRegHideWater] != 0;
}

// Is a triangle (world positions) part of a water surface: flat, at a water box's level and inside its square?
bool OnWaterBoxSurface(const CollisionHeader* h, const Vec3s& a, const Vec3s& b, const Vec3s& c) {
    const float y0 = std::min({ a.y, b.y, c.y }), y1 = std::max({ a.y, b.y, c.y });
    if (y1 - y0 > 10.0f) return false;
    const float cx = (a.x + b.x + c.x) / 3.0f, cy = (a.y + b.y + c.y) / 3.0f, cz = (a.z + b.z + c.z) / 3.0f;
    for (int i = 0; i < h->numWaterBoxes; i++) {
        const WaterBox& w = h->waterBoxes[i];
        if ((w.properties & 0x80000) != 0 || std::fabs(cy - w.ySurface) > 10.0f) continue;
        const float x0 = w.xMin - 12.0f, x1 = w.xMin + w.xLength + 12.0f, z0 = w.zMin - 12.0f, z1 = w.zMin + w.zLength + 12.0f;
        if (cx < x0 || cx > x1 || cz < z0 || cz > z1) continue;
        auto inReach = [&](const Vec3s& v) { return v.x > x0 - 200.0f && v.x < x1 + 200.0f && v.z > z0 - 200.0f && v.z < z1 + 200.0f; };
        if (inReach(a) && inReach(b) && inReach(c)) return true;
    }
    return false;
}

void PatchGameWater(Gfx* at, const Gfx& mine) {
    if (!gGameWaterPatched.insert(at).second) return;
    gGameWaterPatches.push_back({ at, *at, mine });
    *at = mine;
}

// Looks through one display list (and the ones it calls) for water triangles. Only the commands a room's geometry uses are followed: vertex
// loads (by hash, by file, or already resolved to a pointer), triangles, calls and the end; everything else is stepped over.
void LookForGameWater(Gfx* dl, const CollisionHeader* h, int depth) {
    if (dl == nullptr || depth > 4 || !gGameWaterLooked.insert(dl).second) return;
    // the vertex buffer lives on across the lists a room calls, as it does in the graphics chip
    static std::vector<Vec3s> vbuf;
    static std::vector<bool> have;
    if (depth == 0) { vbuf.assign(256, Vec3s{ 0, 0, 0 }); have.assign(256, false); }
    auto load = [&](const Vtx* v, int n, int first) {
        if (v == nullptr || n <= 0 || first < 0 || first + n > 4096) return;
        if (static_cast<int>(vbuf.size()) < first + n) { vbuf.resize(first + n); have.resize(first + n, false); }
        for (int i = 0; i < n; i++) { vbuf[first + i] = { v[i].v.ob[0], v[i].v.ob[1], v[i].v.ob[2] }; have[first + i] = true; }
    };
    auto water = [&](int i, int j, int k) {
        const int n = static_cast<int>(vbuf.size());
        if (i >= n || j >= n || k >= n || !have[i] || !have[j] || !have[k]) return false;
        return OnWaterBoxSurface(h, vbuf[i], vbuf[j], vbuf[k]);
    };
    static const Gfx kNoop[] = { gsDPNoOp() };
    const Gfx& noop = kNoop[0];
    for (int guard = 0; guard < 60000; guard++) {
        Gfx* g = &dl[guard];
        const u8 op = static_cast<u8>(g->words.w0 >> 24);
        const uintptr_t w0 = g->words.w0, w1 = g->words.w1;
        switch (op) {
            case G_ENDDL: return;
            case G_VTX: {
                const int n = static_cast<int>((w0 >> 12) & 0xFF), first = static_cast<int>((w0 >> 1) & 0x7F) - n;
                if (w1 > 0x0FFFFFFF) load(reinterpret_cast<const Vtx*>(w1), n, first);   // (a segment address would be small: not followed)
                break;
            }
            case G_VTX_OTR_HASH: {
                const int n = static_cast<int>((w0 >> 12) & 0xFF), first = static_cast<int>((w0 >> 1) & 0x7F) - n;
                const uint64_t hash = (static_cast<uint64_t>(dl[guard + 1].words.w0) << 32) + static_cast<uint32_t>(dl[guard + 1].words.w1);
                const Vtx* v = nullptr;
                if (w1 > 0xFFFFF) v = reinterpret_cast<const Vtx*>(w1);   // the renderer already swapped the hash for the pointer
                else if (const char* base = static_cast<const char*>(ResourceGetDataByCrc(hash))) v = reinterpret_cast<const Vtx*>(base + w1);
                load(v, n, first);
                guard++;
                break;
            }
            case G_VTX_OTR_FILEPATH: {
                const char* name = reinterpret_cast<const char*>(w1);
                const Gfx& next = dl[guard + 1];
                const int n = static_cast<int>(next.words.w0), first = static_cast<int>(next.words.w1 >> 16), off = static_cast<int>(next.words.w1 & 0xFFFF);
                if (const Vtx* v = static_cast<const Vtx*>(ResourceGetDataByName(name))) load(v + off, n, first);
                guard++;
                break;
            }
            case G_TRI1:
                if (water(static_cast<int>((w0 >> 16) & 0xFF) / 2, static_cast<int>((w0 >> 8) & 0xFF) / 2, static_cast<int>(w0 & 0xFF) / 2)) PatchGameWater(g, noop);
                break;
            case G_TRI1_OTR:
                if (water(static_cast<int>(w0 & 0xFFFF), static_cast<int>((w1 >> 16) & 0xFFFF), static_cast<int>(w1 & 0xFFFF))) PatchGameWater(g, noop);
                break;
            case G_TRI2:
            case G_QUAD: {
                const int a0 = static_cast<int>((w0 >> 16) & 0xFF) / 2, a1 = static_cast<int>((w0 >> 8) & 0xFF) / 2, a2 = static_cast<int>(w0 & 0xFF) / 2;
                const int b0 = static_cast<int>((w1 >> 16) & 0xFF) / 2, b1 = static_cast<int>((w1 >> 8) & 0xFF) / 2, b2 = static_cast<int>(w1 & 0xFF) / 2;
                const bool wa = water(a0, a1, a2), wb = water(b0, b1, b2);
                if (wa && wb) PatchGameWater(g, noop);
                else if (wa) { const Gfx keep[] = { gsSP1Triangle(b0, b1, b2, 0) }; PatchGameWater(g, keep[0]); }
                else if (wb) { const Gfx keep[] = { gsSP1Triangle(a0, a1, a2, 0) }; PatchGameWater(g, keep[0]); }
                break;
            }
            case G_DL:
                if (w1 > 0x0FFFFFFF) LookForGameWater(reinterpret_cast<Gfx*>(w1), h, depth + 1);
                if (((w0 >> 16) & 0xFF) == G_DL_NOPUSH) return;   // a branch: the rest of this list is never run
                break;
            case G_DL_OTR_HASH: {
                const uint64_t hash = (static_cast<uint64_t>(dl[guard + 1].words.w0) << 32) + static_cast<uint32_t>(dl[guard + 1].words.w1);
                if (auto res = ResourceLoad(hash)) {
                    gGameWaterHolds.emplace(res->GetRawPointer(), res);
                    LookForGameWater(static_cast<Gfx*>(res->GetRawPointer()), h, depth + 1);
                }
                guard++;
                break;
            }
            case G_DL_OTR_FILEPATH:
                if (auto res = ResourceLoad(reinterpret_cast<const char*>(w1))) {
                    gGameWaterHolds.emplace(res->GetRawPointer(), res);
                    LookForGameWater(static_cast<Gfx*>(res->GetRawPointer()), h, depth + 1);
                }
                if (((w0 >> 16) & 0x1) != 0) return;
                break;
            case G_SETTIMG_OTR_HASH: case G_MARKER: case G_BRANCH_Z_OTR: case G_MTX_OTR:
                guard++;   // the two-part commands
                break;
            default: break;
        }
    }
}

// A room's see-through display list, as the room keeps it (a resource path in this port, or a display list).
void LookForGameWaterIn(void* xlu, const CollisionHeader* h) {
    if (xlu == nullptr) return;
    if (ResourceMgr_OTRSigCheck(static_cast<char*>(xlu))) {
        auto res = ResourceMgr_GetResourceByNameHandlingMQ(static_cast<const char*>(xlu));
        if (res == nullptr) return;
        gGameWaterHolds.emplace(res->GetRawPointer(), res);
        LookForGameWater(static_cast<Gfx*>(res->GetRawPointer()), h, 0);
    } else {
        LookForGameWater(static_cast<Gfx*>(xlu), h, 0);
    }
}
void LookForGameWaterInRoom(const Room& room, const CollisionHeader* h) {
    if (room.num < 0 || room.meshHeader == nullptr) return;
    const MeshHeader* mesh = room.meshHeader;
    if (mesh->base.type == 0) {
        const PolygonDlist* d = static_cast<const PolygonDlist*>(SEGMENTED_TO_VIRTUAL(mesh->polygon0.start));
        for (int i = 0; d != nullptr && i < mesh->polygon0.num; i++) LookForGameWaterIn(d[i].xlu, h);
    } else if (mesh->base.type == 2) {
        const PolygonDlist2* d = static_cast<const PolygonDlist2*>(SEGMENTED_TO_VIRTUAL(mesh->polygon2.start));
        for (int i = 0; d != nullptr && i < mesh->polygon2.num; i++) LookForGameWaterIn(d[i].xlu, h);
    }
}

void HiddenWater_Draw(Actor*, PlayState*) {}
bool IsGameWaterActor(const Actor* a) {
    switch (a->id) {
        case ACTOR_BG_MIZU_WATER: case ACTOR_BG_SPOT01_IDOMIZU: case ACTOR_BG_MORI_IDOMIZU: case ACTOR_BG_SPOT11_OASIS: case ACTOR_BG_HAKA_WATER: return true;
        case ACTOR_BG_SPOT06_OBJECTS: return a->params == 2;   // Lake Hylia's water plane (its init turns the params into the object's type)
        default: return false;
    }
}

void ShowGameWater() {
    for (auto it = gGameWaterPatches.rbegin(); it != gGameWaterPatches.rend(); ++it)
        if (std::memcmp(it->at, &it->mine, sizeof(Gfx)) == 0) *it->at = it->orig;
    gGameWaterPatches.clear(); gGameWaterPatched.clear(); gGameWaterHolds.clear(); gGameWaterLooked.clear();
    // Only actors still in the lists: a room change frees the old room's water actors, and their memory may already hold something else.
    if (gPlayState != nullptr)
        for (int cat = 0; cat < ACTORCAT_MAX; cat++)
            for (Actor* a = gPlayState->actorCtx.actorLists[cat].head; a != nullptr; a = a->next)
                if (a->draw == HiddenWater_Draw) { const auto it = gGameWaterActors.find(a); if (it != gGameWaterActors.end()) a->draw = it->second; }
    gGameWaterActors.clear();
    gGameWaterKey = 0;
    gGameWaterHidden = 0;
}

// Once a game frame: hide what has appeared (a room loaded, a water level moved), or put it all back.
void UpdateGameWater() {
    if (!GameWaterHidden()) { if (!gGameWaterPatches.empty() || !gGameWaterActors.empty() || gGameWaterKey != 0) ShowGameWater(); return; }
    PlayState* play = gPlayState;
    const CollisionHeader* h = play->colCtx.colHeader;
    if (h != nullptr && h->waterBoxes != nullptr && h->numWaterBoxes > 0) {
        uint64_t key = static_cast<uint64_t>(play->sceneNum) * 131u + static_cast<u8>(play->roomCtx.curRoom.num);
        key = key * 131u + static_cast<u8>(play->roomCtx.prevRoom.num);
        for (int i = 0; i < h->numWaterBoxes; i++) key = key * 1099511628211ull + static_cast<uint64_t>(h->waterBoxes[i].ySurface + 40000);
        key |= 1;
        if (key != gGameWaterKey) {
            gGameWaterKey = key;
            gGameWaterLooked.clear();   // a moved water level is looked for again (what was hidden stays hidden)
            const size_t before = gGameWaterPatches.size();
            LookForGameWaterInRoom(play->roomCtx.curRoom, h);
            LookForGameWaterInRoom(play->roomCtx.prevRoom, h);
            if (gGameWaterPatches.size() != before) Trace("water: hid the game's water");
            gGameWaterHidden = static_cast<int>(gGameWaterPatches.size());
        }
    }
    for (int cat = 0; cat < ACTORCAT_MAX; cat++)
        for (Actor* a = play->actorCtx.actorLists[cat].head; a != nullptr; a = a->next)
            if (a->draw != nullptr && a->draw != HiddenWater_Draw && a->update != nullptr && IsGameWaterActor(a)) {
                gGameWaterActors[a] = a->draw;
                a->draw = HiddenWater_Draw;
            }
}
// The scene is going: its actors go with it (the display lists stay changed and held; they are the same when it comes back).
void ForgetGameWaterActors() { gGameWaterActors.clear(); gGameWaterKey = 0; }

float WaveAmp() {
    float wx, wz, wind;
    WindNow(&wx, &wz, &wind);
    return gWaterWaves * (0.8f + 1.1f * std::clamp(wind, 0.0f, 1.0f));
}

// Daylight on the water, 0.38 at night to 1 at noon (the game's own clock).
bool WaterIndoors() { return InRegularGame() && !SceneOutdoors(); }   // a dungeon's, a grotto's or a house's water: lit by the room, no sky over it
float WaterLight() {
    if (WaterIndoors() && gPlayState != nullptr) {
        const u8* a = gPlayState->envCtx.lightSettings.ambientColor;
        return std::clamp(0.5f + 0.5f * (a[0] + a[1] + a[2]) / (3.0f * 160.0f), 0.5f, 1.0f);
    }
    const float f = std::sin((static_cast<float>(gSaveContext.dayTime) - 16384.0f) / 32768.0f * 3.14159265f);
    return 0.38f + 0.62f * std::clamp(f * 1.4f, 0.0f, 1.0f);
}

// The current, in units a second: a slow wandering flow that differs from place to place, plus a little of the wind.
void WaterCurrentAt(float x, float z, float t, float* cx, float* cz) {
    const float a = std::sin(x * 0.0012f + t * 0.11f) + 0.8f * std::cos(z * 0.0009f - t * 0.07f);
    const float ang = a * 1.7f + 0.6f;
    const float m = 9.0f + 5.0f * std::sin(z * 0.0006f + x * 0.0004f);
    float wx, wz, wind;
    WindNow(&wx, &wz, &wind);
    *cx = std::cos(ang) * m + wx * 0.1f;
    *cz = std::sin(ang) * m + wz * 0.1f;
}

// ---- displacement: rings that spread over the surface, and the dip and bow wave under and ahead of whoever is in it
struct WaterDisturb { float x, z, birth, strength, size; };
std::vector<WaterDisturb> gWaterDist;
constexpr size_t kMaxWaterDist = 40;
struct WaterBodyNow { float x, z, size, dx, dz, speed; };
std::vector<WaterBodyNow> gWaterBodies;
struct WaterRefl { float x, z, surface, h, w; float c1[3], c2[3]; };
std::vector<WaterRefl> gWaterRefl;
struct WaterTrack { float x = 0, y = 0, z = 0, ring = 0, spawn = 0, dist = 0; bool in = false; int seen = 0; };
std::unordered_map<const void*, WaterTrack> gWaterTrack;
float gCamUnder = 0.0f;   // 0 above the surface .. 1 below it (eased), for the underwater overlay
float gCamDepth = 0.0f;   // how far under the surface the camera is (units)
float gWaterNow = 0.0f;   // the clock the displacement runs on (seconds)
// The ripple simulation round the player (shared/water_sim.h): 64 x 64 cells of 14 units. Rings spread, cross, mix and fade at its rim.
royale::water::RippleField gRipples(14.0f, 120.0f, 0.55f);

void AddWaterDisturb(float x, float z, float strength, float size) {
    if (gWaterRipples && gRipples.Contains(x, z, gRipples.Cell() * 8.0f)) {   // near the player the simulation makes the ring
        gRipples.Impulse(x, z, strength * size * 0.45f, size * 0.9f);
        return;
    }
    if (gWaterDist.size() >= kMaxWaterDist) gWaterDist.erase(gWaterDist.begin());
    gWaterDist.push_back({ x, z, gWaterNow, strength, size });
}

constexpr float kWaterRingSpeed = 90.0f;   // how fast a ring spreads, in units a second
constexpr float kWaterRingLife = 4.5f;     // and how long it lasts

// How far the displacement lifts the surface here (it can be negative), and how much foam is on it (0 to 1).
float WaterDisplaceAt(float x, float z, float* foam) {
    float h = 0.0f, f = 0.0f;
    for (const WaterDisturb& d : gWaterDist) {
        const float dx = x - d.x, dz = z - d.z, r2 = dx * dx + dz * dz, a = gWaterNow - d.birth;
        const float front = kWaterRingSpeed * a, band = 26.0f + d.size * 1.3f;
        const float reach = front + band * 2.5f + d.size * 2.0f;
        if (r2 > reach * reach) continue;
        const float r = std::sqrt(r2), off = r - front, env = std::exp(-(off * off) / (band * band));
        const float life = std::max(0.0f, 1.0f - a / kWaterRingLife);   // the ring dies away smoothly (it is dropped when its time is up, so it must not stop short)
        h += d.strength * d.size * 0.28f * env * std::exp(-a * 0.55f) * life / (1.0f + r / (d.size * 6.0f)) * std::cos(off * 0.09f);
        if (a < 1.6f) f += d.strength * 0.7f * std::exp(-r2 / (d.size * d.size * 1.5f)) * (1.0f - a / 1.6f);
        f += d.strength * 0.3f * env * life * (0.5f + 0.5f * std::cos(off * 0.09f)) / (1.0f + r / (d.size * 8.0f));   // a pale crest on the front of the ring
    }
    for (const WaterBodyNow& b : gWaterBodies) {
        const float dx = x - b.x, dz = z - b.z, r2 = dx * dx + dz * dz, s2 = b.size * b.size;
        if (r2 > s2 * 40.0f) continue;
        h -= b.size * 0.3f * std::exp(-r2 / (s2 * 3.0f));   // the dip under it
        const float run = std::min(1.0f, b.speed / 120.0f);
        if (run > 0.05f) {
            const float bx = x - (b.x + b.dx * b.size * 1.6f), bz = z - (b.z + b.dz * b.size * 1.6f);
            h += b.size * 0.2f * run * std::exp(-(bx * bx + bz * bz) / (s2 * 2.0f));   // the bow wave ahead
            const float tx = x - (b.x - b.dx * b.size * 1.4f), tz = z - (b.z - b.dz * b.size * 1.4f);
            f += 0.45f * run * std::exp(-(tx * tx + tz * tz) / (s2 * 2.5f));   // and foam in its wake
        }
    }
    if (gWaterRipples) {
        h += gRipples.Sample(x, z);
        float sx, sz;
        gRipples.Slope(x, z, &sx, &sz);
        f += std::clamp((std::hypot(sx, sz) - 0.12f) * 1.5f, 0.0f, 0.5f);   // steep ripples break into a little foam
    }
    *foam = std::min(1.0f, f);
    return h;
}

// ---- for creatures, boats and water physics to come: the water at a point, as it is drawn ------------------------------------------------
// `height` is where the surface is now (the level, plus the swell and the ripples); `depth` how deep the water is there; (nx, ny, nz) the
// surface's normal; (flowX, flowZ) the current in units a second. Push the water with AddWaterDisturb (a ring) or gRipples.Impulse.
struct WaterInfo { bool water = false; float surface = 0, height = 0, depth = 0, nx = 0, ny = 1, nz = 0, flowX = 0, flowZ = 0; };
[[maybe_unused]] WaterInfo WaterInfoAt(float x, float z) {
    WaterInfo w;
    if (gPlayState == nullptr || !WaterSurfaceAt(x, z, &w.surface)) return w;
    w.water = true;
    float floorY = 0;
    w.depth = WaterFloorAt(x, z, &floorY) ? w.surface - floorY : 1000.0f;
    const float shore = std::min(1.0f, std::max(0.0f, w.depth) / 90.0f);
    const royale::water::SwellPoint sp = royale::water::Swell(x, z, gWaterNow, WaveAmp() * (OnIsland() ? 1.0f : 0.35f), gWaterChop);
    float foam = 0;
    w.height = w.surface + (sp.h + WaterDisplaceAt(x, z, &foam)) * shore;
    w.nx = sp.nx * shore; w.ny = sp.ny; w.nz = sp.nz * shore;
    const float l = std::sqrt(w.nx * w.nx + w.ny * w.ny + w.nz * w.nz);
    w.nx /= l; w.ny /= l; w.nz /= l;
    WaterCurrentAt(x, z, gWaterNow, &w.flowX, &w.flowZ);
    return w;
}

// ---- our own water sprites -------------------------------------------------------------------------------------------------------------------
// Splashes, ripples, bubbles and spray are drawn by the water itself (not the game's EffectSs sprites: the flat rings and the splash cut-outs that
// sat on top of our surface and read as the original game's water). Four soft sprites from one 256 x 256 atlas (shared/water_sim.h,
// MakeSplashAtlas): a droplet (stretched along its flight), a bubble, a puff of spray and a lacy foam disc that lies on the surface and spreads.
// The game's own water effects are caught each frame (WaterAdoptGameFx) and either dropped, where the water already makes the effect, or turned
// into ours.
enum WaterPartKind : u8 { WP_Drop = 0, WP_Bubble = 1, WP_Puff = 2, WP_Disc = 3 };
struct WaterPart {
    float x, y, z, vx, vy, vz, age, life, size, grow, spin, surface;
    u8 kind, pop;   // pop: a bubble that breaks at the surface in a ring
};
std::vector<WaterPart> gWaterParts;
constexpr size_t kMaxWaterParts = 640;
constexpr float kWaterGravity = 520.0f;   // units a second squared for a flung droplet

bool WaterPartRoom(size_t n = 1) { return gWaterFxOn && gPlayState != nullptr && gWaterParts.size() + n <= kMaxWaterParts; }
void AddWaterPart(WaterPartKind kind, float x, float y, float z, float vx, float vy, float vz, float size, float grow, float life, float surface, bool pop = false, float age = 0.0f) {
    if (!WaterPartRoom()) return;
    gWaterParts.push_back({ x, y, z, vx, vy, vz, age, life, size, grow, Flora01(static_cast<int>(x), static_cast<int>(z), 697) * 6.2831853f, surface, static_cast<u8>(kind), static_cast<u8>(pop ? 1 : 0) });
}

int gWaterFrame = 0, gWaterBudget = 0;

// A ring of foam that spreads over the surface (scale 1 is Link's own splash-in).
void WaterRipple(float x, float z, float surface, float scale, int delay) {
    if (!gWaterFxOn || gWaterBudget <= 0) return;
    gWaterBudget--;
    AddWaterPart(WP_Disc, x, surface, z, 0, 0, 0, 7.0f * scale, 30.0f * scale, 1.1f, surface, false, -static_cast<float>(delay) / 20.0f);
}

// A splash: a crown of droplets thrown out and up, a column of spray at its middle and a disc of foam spreading from it.
void WaterSplashFx(float x, float z, float surface, float size, float strength) {
    if (!gWaterFxOn || gWaterBudget <= 1) return;
    gWaterBudget -= 2;
    const float k = size / 16.0f, power = 0.6f + 0.55f * strength;
    const int drops = std::clamp(static_cast<int>(8.0f + 9.0f * strength), 8, 20);
    for (int d = 0; d < drops; d++) {
        const float a = (d + Flora01(gWaterFrame, d, 700)) / drops * 6.2831853f, sp = (34.0f + 70.0f * Flora01(gWaterFrame, d, 701)) * k * power;
        AddWaterPart(WP_Drop, x + std::cos(a) * 3.0f * k, surface + 1.0f, z + std::sin(a) * 3.0f * k, std::cos(a) * sp, (150.0f + 170.0f * Flora01(gWaterFrame, d, 702)) * power * (0.7f + 0.3f * k),
                     std::sin(a) * sp, (1.5f + 1.5f * Flora01(gWaterFrame, d, 703)) * (0.6f + 0.4f * k), 0.0f, 3.0f, surface);
    }
    for (int c = 0; c < 3; c++)
        AddWaterPart(WP_Puff, x + (Flora01(gWaterFrame, c, 704) - 0.5f) * 6.0f * k, surface + 3.0f * k, z + (Flora01(gWaterFrame, c, 705) - 0.5f) * 6.0f * k, 0.0f, (70.0f + 50.0f * Flora01(gWaterFrame, c, 706)) * power,
                     0.0f, (7.0f + 4.0f * c) * k * power, 12.0f * k, 0.55f + 0.1f * c, surface);
    AddWaterPart(WP_Disc, x, surface, z, 0, 0, 0, 9.0f * k, 34.0f * k * power, 1.0f, surface);
}

// ---- more water life: bubbles that rise and break, spray flung off the surface, foam that lies on it
float gBubbleLoad = 0.0f;   // kept for the budget below: roughly how many of our bubbles are alive (counted as they are made, drained over time)
int gBubbleBudget = 0;
constexpr float kBubbleCap = 90.0f;

bool BubbleRoom(float cost) {
    if (!gWaterFxOn || !gWaterLife || gBubbleBudget <= 0 || gBubbleLoad + cost > kBubbleCap || !WaterPartRoom()) return false;
    gBubbleBudget--;
    gBubbleLoad += cost;
    return true;
}

// One bubble that rises from (x, y, z) at `rise` units a frame (of the game's 20) for `life` frames, then breaks. `scale` ~ 24 (small) to 60 (big).
void WaterBubble(float x, float y, float z, float rise, int life, int scale, bool wander = true) {
    if (!BubbleRoom(std::max(1.0f, life / 30.0f))) return;
    float surface = 0;
    if (!WaterSurfaceAt(x, z, &surface)) surface = y + rise * life;
    AddWaterPart(WP_Bubble, x, y, z, wander ? 1.0f : 0.0f, rise * 20.0f, 0.0f, 1.2f + scale * 0.1f, 0.0f, life / 20.0f, surface, true);
}

// A burst of bubbles from a body that has gone under: a fountain of them, each a little different, climbing at their own pace.
void WaterBubbleBurst(float x, float y, float z, float surface, int count, float spread) {
    for (int k = 0; k < count; k++) {
        const float a = Flora01(gWaterFrame, k, 640) * 6.2831853f, r = spread * Flora01(gWaterFrame, k, 641);
        const float by = y + 10.0f * Flora01(gWaterFrame, k, 642), rise = 1.6f + 1.8f * Flora01(gWaterFrame, k, 643);
        const int life = std::clamp(static_cast<int>((surface - by) / rise) - 1, 6, 70);
        WaterBubble(x + std::cos(a) * r, by, z + std::sin(a) * r, rise, life, 26 + static_cast<int>(30.0f * Flora01(gWaterFrame, k, 644)));
    }
}

// Spray thrown off the surface: droplets flung sideways, up and back down.
void WaterSpray(float x, float surface, float z, int count) {
    for (int k = 0; k < count; k++) {
        if (!BubbleRoom(0.4f)) return;
        const float a = Flora01(gWaterFrame, k, 650) * 6.2831853f, sp = 24.0f + 60.0f * Flora01(gWaterFrame, k, 651);
        AddWaterPart(WP_Drop, x + (Flora01(gWaterFrame, k, 653) - 0.5f) * 10.0f, surface + 2.0f, z + (Flora01(gWaterFrame, k, 654) - 0.5f) * 10.0f, std::cos(a) * sp, 110.0f + 110.0f * Flora01(gWaterFrame, k, 652),
                     std::sin(a) * sp, 1.3f + 1.3f * Flora01(gWaterFrame, k, 655), 0.0f, 3.0f, surface);
    }
}

// A small patch of foam that lies on the surface a moment and fades: the foam bubbles of a wake.
void WaterFoamBubble(float x, float surface, float z, float size) {
    if (!BubbleRoom(0.3f)) return;
    AddWaterPart(WP_Disc, x + (Flora01(gWaterFrame, 3, 660) - 0.5f) * size, surface, z + (Flora01(gWaterFrame, 4, 661) - 0.5f) * size, 0, 0, 0, 2.5f + 2.0f * Flora01(gWaterFrame, 5, 662), 3.0f, 1.3f, surface);
}

// The life of the water itself, round the player: vents on the sea floor breathe out strings of bubbles that pop in rings at the surface, and now and
// then something leaps out of the water (a splash, spray and rings). Fixed spots from the map, so everyone sees the same vents.
void WaterLife(PlayState* play, float dt, bool paused) {
    if (!gWaterLife || !gWaterFxOn || paused) return;
    Player* pl = GET_PLAYER(play);
    if (pl == nullptr) return;
    const float px = pl->actor.world.pos.x, pz = pl->actor.world.pos.z;
    constexpr float kVentCell = 640.0f;
    const int vcx = static_cast<int>(std::floor(px / kVentCell)), vcz = static_cast<int>(std::floor(pz / kVentCell));
    // one vent is tried each frame (the ones that exist are always the same; which is tried is by the frame)
    const int ox = static_cast<int>(Flora01(gWaterFrame, 1, 671) * 5.0f) - 2, oz = static_cast<int>(Flora01(gWaterFrame, 2, 672) * 5.0f) - 2;
    const int cx = vcx + ox, cz = vcz + oz;
    if (Flora01(cx, cz, 673) < 0.34f) {
        const float x = (cx + 0.15f + 0.7f * Flora01(cx, cz, 674)) * kVentCell, z = (cz + 0.15f + 0.7f * Flora01(cx, cz, 675)) * kVentCell;
        float surface = 0, floorY = 0;
        if (WaterSurfaceAt(x, z, &surface) && WaterFloorAt(x, z, &floorY) && surface - floorY > 70.0f && surface - floorY < 700.0f && std::hypot(x - px, z - pz) < 1300.0f) {
            // a vent breathes in puffs: on for a couple of seconds, off for a few
            const float phase = std::fmod(gWaterNow * 0.22f + Flora01(cx, cz, 676), 1.0f);
            if (phase < 0.45f) {
                const float by = std::max(floorY, surface - 520.0f) + 4.0f;
                const float rise = 2.0f + 0.8f * Flora01(cx, cz, 677);
                const int life = std::clamp(static_cast<int>((surface - by) / rise) - 1, 8, 80);
                WaterBubble(x, by, z, rise, life, 28 + static_cast<int>(26.0f * Flora01(gWaterFrame, 9, 678)));
                if (Flora01(gWaterFrame, 10, 679) < 0.3f) { gWaterBudget++; WaterRipple(x, z, surface, 0.22f, life); }   // it pops at the surface in a ring, when the bubble gets there
            }
        }
    }
    // something leaps: about once in four seconds, somewhere in view
    if (Flora01(gWaterFrame, 11, 680) < dt * 0.25f) {
        const float a = Flora01(gWaterFrame, 12, 681) * 6.2831853f, r = 350.0f + 1100.0f * Flora01(gWaterFrame, 13, 682);
        const float x = px + std::cos(a) * r, z = pz + std::sin(a) * r;
        float surface = 0, floorY = 0;
        if (WaterSurfaceAt(x, z, &surface) && (!WaterFloorAt(x, z, &floorY) || surface - floorY > 60.0f)) {
            const float rw = std::clamp(Flora01(gWaterFrame, 14, 683), 0.0f, 1.0f);
            WaterSplashFx(x, z, surface, 10.0f + 6.0f * rw, 0.5f);
            if (gWaterWakes) AddWaterDisturb(x, z, 0.5f, 12.0f);
            WaterSpray(x, surface, z, 4);
            WaterBubbleBurst(x, surface - 30.0f, z, surface, 3, 14.0f);
            if (gWaterBudget > 0) WaterRipple(x, z, surface, 0.35f, 8);
        }
    }
}

// One body in the water this frame: the local player and everyone else get the same splashes, rings and bubbles from here (the game's own are
// caught and dropped by WaterAdoptGameFx). All of them push the surface (displacement) and leave foam.
void WaterBody(const void* key, float x, float y, float z, float size, float dt, bool local) {
    WaterTrack& t = gWaterTrack[key];
    float surface = 0, floorY = 0;
    const bool have = WaterSurfaceAt(x, z, &surface);
    const bool in = have && y < surface + 8.0f && (!WaterFloorAt(x, z, &floorY) || floorY < surface - 4.0f);
    const float mx = x - t.x, mz = z - t.z, my = y - t.y;
    const bool jumped = t.seen == 0 || std::hypot(mx, mz) > 500.0f || std::fabs(my) > 400.0f || dt <= 0.0001f;
    const float speed = jumped ? 0.0f : std::hypot(mx, mz) / dt;
    const float eyeD = std::hypot(x - gPlayState->view.eye.x, z - gPlayState->view.eye.z);
    if (!jumped && eyeD < 4500.0f) {
        if (in) {
            const float inv = 1.0f / std::max(1.0f, speed), dx = mx / dt * inv, dz = mz / dt * inv;
            if (gWaterWakes) gWaterBodies.push_back({ x, z, size, speed > 8.0f ? dx : 0.0f, speed > 8.0f ? dz : 0.0f, speed });
            if (!t.in) {   // going in
                const float strength = std::clamp(0.35f + std::max(0.0f, -my / dt) / 500.0f + speed / 400.0f, 0.3f, 1.5f);
                if (gWaterWakes) AddWaterDisturb(x, z, strength * 1.2f, size);
                WaterSplashFx(x, z, surface, size, strength);
                WaterSpray(x, surface, z, std::clamp(static_cast<int>(2.0f + 5.0f * strength), 2, 8));   // spray off the surface as it goes in...
                WaterBubbleBurst(x, std::max(y, surface - 60.0f), z, surface, std::clamp(static_cast<int>(3.0f + 6.0f * strength), 3, 10), size * 0.8f);   // ...and the air it takes down
            } else {
                t.ring -= dt;
                t.spawn -= dt;
                if (speed > 25.0f) {
                    if (t.spawn <= 0.0f) {   // a ring left in the water every so often along the way
                        t.spawn = std::clamp(0.30f - speed / 1500.0f, 0.1f, 0.3f);
                        if (gWaterWakes) AddWaterDisturb(x - dx * size * 0.4f, z - dz * size * 0.4f, std::min(1.0f, 0.25f + speed / 250.0f), size);
                    }
                    if (t.ring <= 0.0f) {
                        t.ring = std::clamp(0.45f - speed / 900.0f, 0.12f, 0.45f);
                        WaterRipple(x, z, surface, size / 16.0f, 0);
                        if (speed > 150.0f) WaterSplashFx(x - dx * size * 0.4f, z - dz * size * 0.4f, surface, size * 0.6f, 0.25f);   // fast (a cart, a sprint): spray off the bow
                    }
                } else if (t.ring <= 0.0f) {   // treading water
                    t.ring = 0.9f;
                    if (gWaterWakes) AddWaterDisturb(x, z, 0.35f, size * 0.8f);
                    WaterRipple(x, z, surface, size / 16.0f * 0.8f, 0);
                }
                if (y + 38.0f < surface && gWaterFxOn) {   // someone under water: a steady string of bubbles (the game's own, and ours that rise and fade)
                    if (Flora01(gWaterFrame, 7, 504) < dt * 2.0f) WaterBubble(x + (Flora01(gWaterFrame, 6, 508) - 0.5f) * size, y + size * 1.2f, z + (Flora01(gWaterFrame, 5, 509) - 0.5f) * size, 1.4f, 40, 46);
                    if (Flora01(gWaterFrame, 8, 505) < dt * (local ? 6.0f : 8.0f)) WaterBubbleBurst(x, y + size * 1.5f, z, surface, 1 + (speed > 60.0f ? 1 : 0), size * 0.5f);
                } else if (speed > 40.0f && Flora01(gWaterFrame, 9, 506) < dt * 7.0f) {   // swimming at the top: foam bubbles in the wake
                    WaterFoamBubble(x - dx * size * 0.8f, surface, z - dz * size * 0.8f, size * 1.4f);
                    if (speed > 150.0f && Flora01(gWaterFrame, 10, 507) < 0.5f) WaterSpray(x + dx * size, surface, z + dz * size, 2);   // fast: spray off the bow
                }
            }
            if (gWaterRefl.size() < 12 && gWaterSkyOn && gWaterBodiesRefl && y + size * 3.4f > surface + 6.0f) {
                const bool cart = size > 30.0f;
                WaterRefl r;
                r.x = x; r.z = z; r.surface = surface; r.h = std::min(size * (cart ? 1.4f : 3.6f), y + size * (cart ? 1.4f : 3.6f) - surface); r.w = size * (cart ? 1.0f : 0.5f);
                const float c1[3] = { cart ? 120.0f : 52.0f, cart ? 80.0f : 110.0f, cart ? 46.0f : 58.0f }, c2[3] = { cart ? 190.0f : 226.0f, cart ? 150.0f : 186.0f, cart ? 100.0f : 150.0f };
                for (int i = 0; i < 3; i++) { r.c1[i] = c1[i]; r.c2[i] = c2[i]; }
                gWaterRefl.push_back(r);
            }
        } else if (t.in) {   // climbing out: the ring it leaves, and the water running off
            if (gWaterWakes) AddWaterDisturb(x, z, 0.5f, size);
            WaterRipple(x, z, surface, size / 16.0f, 0);
            WaterSpray(x, surface, z, 3);
        }
    }
    t.x = x; t.y = y; t.z = z; t.in = in; t.seen = gWaterFrame;
}

// Rain falling on the water makes rings, near the camera.
void WaterRain(PlayState* play, float dt) {
    if (!gWaterRain || !gWaterFxOn) return;
    float amount = 0.0f;
    switch (gWeatherShown.sky) {
        case royale::Sky::Rain: amount = WeatherAmount(); break;
        case royale::Sky::Thunder: amount = WeatherAmount() * 1.3f; break;
        default: break;
    }
    if (amount < 0.05f) return;
    static float carry = 0.0f;
    carry += dt * 10.0f * amount;
    const Vec3f eye = play->view.eye;
    for (; carry >= 1.0f && gWaterBudget > 0; carry -= 1.0f) {
        const float a = Flora01(gWaterFrame, 31, 601) * 6.2831853f, r = 120.0f + 900.0f * Flora01(gWaterFrame, 32, 602);
        const float x = eye.x + std::cos(a) * r, z = eye.z + std::sin(a) * r;
        float s = 0, fy = 0;
        if (WaterSurfaceAt(x, z, &s) && (!WaterFloorAt(x, z, &fy) || fy < s - 8.0f)) {
            WaterRipple(x, z, s, 0.3f + 0.2f * Flora01(gWaterFrame, 33, 603), 0);
            if (gWaterRipples) AddWaterDisturb(x, z, 0.1f, 8.0f);
        }
    }
    carry = std::min(carry, 3.0f);
}

// ---- the surface -----------------------------------------------------------------------------------------------------------------------
// Vertex positions are kept in quarter units (the matrix scales them back) so a swell of a few units does not step by whole units.
void PushVtx4(Vtx& o, float x, float y, float z, float r, float g, float b, float a) {
    o.v.ob[0] = static_cast<s16>(std::lround(std::clamp(x * 4.0f, -32000.0f, 32000.0f)));
    o.v.ob[1] = static_cast<s16>(std::lround(std::clamp(y * 4.0f, -32000.0f, 32000.0f)));
    o.v.ob[2] = static_cast<s16>(std::lround(std::clamp(z * 4.0f, -32000.0f, 32000.0f)));
    o.v.flag = 0; o.v.tc[0] = o.v.tc[1] = 0;
    o.v.cn[0] = static_cast<u8>(std::clamp(r, 0.0f, 255.0f)); o.v.cn[1] = static_cast<u8>(std::clamp(g, 0.0f, 255.0f));
    o.v.cn[2] = static_cast<u8>(std::clamp(b, 0.0f, 255.0f)); o.v.cn[3] = static_cast<u8>(std::clamp(a, 0.0f, 255.0f));
}

void SetupWaterXlu(PlayState* play) {
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_XLU_DISP++, G_LIGHTING | G_CULL_BACK | G_CULL_FRONT | G_FOG);
    gDPSetRenderMode(POLY_XLU_DISP++, G_RM_PASS, G_RM_ZB_XLU_SURF2);
    gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED);
    CLOSE_DISPS(play->state.gfxCtx);
}

// What the water sees above it: the colours of the sky at the horizon and overhead, and where the sun and the moon are (the same as DrawSky).
struct WaterSky { float hor[3], zen[3], sun[3], moon[3], sunA, moonA, sunWarm; };
WaterSky WaterSkyNow() {
    WaterSky s;
    if (WaterIndoors() && gPlayState != nullptr) {   // what is over the water is the room: its fog colour at the far end, its ambient light above
        const EnvLightSettings& L = gPlayState->envCtx.lightSettings;
        for (int i = 0; i < 3; i++) {
            s.hor[i] = 0.75f * L.fogColor[i] + 0.25f * L.ambientColor[i];
            s.zen[i] = 0.6f * L.ambientColor[i] + 0.15f * L.fogColor[i];
            s.sun[i] = s.moon[i] = 0.0f;
        }
        s.sun[1] = 1.0f; s.moon[1] = -1.0f;
        s.sunA = s.moonA = s.sunWarm = 0.0f;
        return s;
    }
    const SkyLight L = SkyLightNow();
    float tint[3];
    const float ov = SkyOvercast(tint);
    auto mix = [](float a, float b, float k) { return a + (b - a) * k; };
    const float nz[3] = { 5, 8, 30 }, nh[3] = { 22, 28, 66 }, dz[3] = { 62, 122, 224 }, dh[3] = { 168, 206, 242 };
    const float glow[3] = { 255, 128, 62 }, glowZ[3] = { 96, 78, 150 };
    const float dim = 1.0f - 0.55f * ov * L.day;
    for (int i = 0; i < 3; i++) {
        s.zen[i] = mix(nz[i], dz[i], L.day); s.hor[i] = mix(nh[i], dh[i], L.day);
        s.zen[i] = mix(s.zen[i], glowZ[i], L.twilight * 0.55f); s.hor[i] = mix(s.hor[i], glow[i], L.twilight * 0.8f);
        const float grey = tint[i] * (0.25f + 0.75f * L.day);
        s.zen[i] = mix(s.zen[i], grey * 0.8f, ov * 0.85f) * dim; s.hor[i] = mix(s.hor[i], grey, ov * 0.85f) * dim;
    }
    const float c = std::sqrt(std::max(0.0f, 1.0f - L.sunH * L.sunH));
    s.sun[0] = std::cos(0.6f) * c; s.sun[1] = L.sunH; s.sun[2] = std::sin(0.6f) * c;
    s.moon[0] = -s.sun[0]; s.moon[1] = -s.sun[1]; s.moon[2] = -s.sun[2];
    s.sunA = std::clamp(L.sunH * 6.0f + 0.4f, 0.0f, 1.0f) * (1.0f - ov);
    s.moonA = std::clamp(-L.sunH * 6.0f + 0.4f, 0.0f, 1.0f) * (1.0f - ov);
    s.sunWarm = L.twilight;
    return s;
}

// ---- the water's textures (shared/water_sim.h): made once, on threads of their own, the first time water is drawn. Until they are ready the
// water is drawn without them. Every one is 256 x 256 8-bit intensity (the game's I8 format: the intensity is also the alpha), loaded with a tile
// load (a block load stops at 4096 texels: docs/ENGINE_LIMITS.md). Caustics, glints, sparkle and swell bands are 16-frame flipbooks that loop;
// every frame lives at its own address, so the renderer keeps one texture per frame and never has to reload.
constexpr int kWaterTex = 256, kWaterTexFrames = 16;
alignas(16) uint8_t gWaterCausticTex[kWaterTexFrames][kWaterTex * kWaterTex];
alignas(16) uint8_t gWaterGlintTex[kWaterTexFrames][kWaterTex * kWaterTex];
alignas(16) uint8_t gWaterFoamImage[kWaterTex * kWaterTex];
alignas(16) uint8_t gWaterSparkleTex[kWaterTexFrames][kWaterTex * kWaterTex];   // the sparse twinkling sparkle (in place of the glint net)
alignas(16) uint8_t gWaterStreakImage[kWaterTex * kWaterTex];                  // the wind's foam streaks (along s)
alignas(16) uint8_t gWaterBandTex[kWaterTexFrames][kWaterTex * kWaterTex];      // the rolling swell bands (travelling along s)
alignas(16) uint8_t gWaterCloudImage[kWaterTex * kWaterTex];                   // the clouds the water mirrors
alignas(16) uint8_t gWaterSplashAtlas[kWaterTex * kWaterTex];                  // droplet, bubble, spray puff and foam disc (2 x 2 cells)
std::atomic<int> gWaterTexState{ 0 };   // 0 not made, 1 being made, 2 ready
std::atomic<int> gWaterTexPending{ 0 };

bool WaterTexturesReady() {
    int expect = 0;
    if (gWaterTexState.compare_exchange_strong(expect, 1)) {
        gWaterTexPending.store(3);
        auto done = [] { if (gWaterTexPending.fetch_sub(1) == 1) gWaterTexState.store(2, std::memory_order_release); };
        std::thread([done] {
            royale::water::MakeCaustics(&gWaterCausticTex[0][0], kWaterTex, kWaterTexFrames, 11, 4, 12, 0.75f, true, 0.0021f, 20);
            royale::water::MakeGlints(&gWaterGlintTex[0][0], kWaterTex, kWaterTexFrames, 23, 6, 14, 0.0021f, 20);
            done();
        }).detach();
        std::thread([done] {
            royale::water::MakeSparkle(&gWaterSparkleTex[0][0], kWaterTex, kWaterTexFrames, 41, 130, 2.0f, 2);
            done();
        }).detach();
        std::thread([done] {
            royale::water::MakeFoam(gWaterFoamImage, kWaterTex, 37, 10);
            royale::water::MakeStreaks(gWaterStreakImage, kWaterTex, 53, 2);
            royale::water::MakeSwellBands(&gWaterBandTex[0][0], kWaterTex, kWaterTexFrames);
            royale::water::MakeClouds(gWaterCloudImage, kWaterTex);
            royale::water::MakeSplashAtlas(gWaterSplashAtlas, kWaterTex);
            done();
        }).detach();
    }
    return gWaterTexState.load(std::memory_order_acquire) == 2;
}

// World units per texel of each texture. Texture coordinates are counted from an origin that moves a whole tile at a time, so they stay small
// (the renderer holds them in 16 bits, about +-1000 texels) and the pattern never jumps when the grid follows the camera.
constexpr float kGlintUnits = 1.5f, kFoamUnits = 1.4f, kCausticUnits = 1.7f, kStreakUnits = 4.0f, kBandUnits = 3.5f, kCloudUnits = 40.0f;
float TexOrigin(float c, float units) { const float tile = kWaterTex * units; return std::round(c / tile) * tile; }
// A scroll offset in texels, wrapped to -128..128 (the texture repeats every 256, and the offset adds to coordinates that are already large)
float TexScroll(float texels) { return texels - static_cast<float>(kWaterTex) * std::round(texels / static_cast<float>(kWaterTex)); }

void PushVtxT(Vtx& o, float x, float y, float z, float s, float t, float r, float g, float b, float a) {
    PushVtx4(o, x, y, z, r, g, b, a);
    o.v.tc[0] = static_cast<s16>(std::lround(std::clamp(s * 32.0f, -32000.0f, 32000.0f)));
    o.v.tc[1] = static_cast<s16>(std::lround(std::clamp(t * 32.0f, -32000.0f, 32000.0f)));
}

// Switches the water's translucent drawing to a texture: the colour is the vertex colour, the alpha the texture times the vertex alpha. So each
// vertex says how much of the pattern shows there (and in which colour), and the pattern itself is the texture.
void WaterTexturePass(PlayState* play, const uint8_t* tex, bool clampEdges = false) {
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(POLY_XLU_DISP++);
    gDPSetTextureFilter(POLY_XLU_DISP++, G_TF_BILERP);
    gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, SHADE, TEXEL0, 0, SHADE, 0, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED);
    const int wrap = clampEdges ? G_TX_CLAMP : G_TX_WRAP;
    gDPLoadTextureTile(POLY_XLU_DISP++, tex, G_IM_FMT_I, G_IM_SIZ_8b, kWaterTex, kWaterTex, 0, 0, kWaterTex - 1, kWaterTex - 1, 0, wrap | G_TX_NOMIRROR, wrap | G_TX_NOMIRROR, 8, 8,
                       G_TX_NOLOD, G_TX_NOLOD);
    gSPTexture(POLY_XLU_DISP++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    CLOSE_DISPS(play->state.gfxCtx);
}
void WaterPlainPass(PlayState* play) {
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(POLY_XLU_DISP++);
    gSPTexture(POLY_XLU_DISP++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED);
    CLOSE_DISPS(play->state.gfxCtx);
}

// ---- the sprites' life and drawing
// Where the surface is at a point now (the swell and the pushes), for sprites that lie on it or break through it.
float WaterPartSurface(const royale::water::PreparedSwell& swell, float x, float z, float level) {
    float foam = 0.0f;
    return level + swell.Sample(x, z).h + (gWaterWakes || gWaterRipples ? WaterDisplaceAt(x, z, &foam) : 0.0f);
}

void WaterPartsStep(float dt, float t) {
    if (gWaterParts.empty() || dt <= 0.0f) return;
    const royale::water::PreparedSwell swell(t, WaveAmp() * (OnIsland() ? 1.0f : 0.35f), gWaterChop, 30.0f);
    size_t keep = 0;
    int pops = 0;
    for (size_t k = 0; k < gWaterParts.size(); k++) {
        WaterPart p = gWaterParts[k];
        p.age += dt;
        bool dead = p.age >= p.life && p.age > 0.0f;
        if (p.age >= 0.0f && !dead) {
            switch (p.kind) {
                case WP_Drop:
                    p.vy -= kWaterGravity * dt;
                    p.x += p.vx * dt; p.y += p.vy * dt; p.z += p.vz * dt;
                    if (p.vy < 0.0f && p.y <= WaterPartSurface(swell, p.x, p.z, p.surface)) {
                        dead = true;
                        if (pops < 3 && Flora01(gWaterFrame, static_cast<int>(k), 710) < 0.35f) { pops++; AddWaterPart(WP_Disc, p.x, p.surface, p.z, 0, 0, 0, 1.6f, 7.0f, 0.5f, p.surface); }
                    }
                    break;
                case WP_Bubble: {
                    const float wob = p.vx > 0.5f ? 1.0f : 0.0f;
                    p.y += p.vy * dt;
                    p.vy += 14.0f * dt;
                    p.x += std::sin(p.age * 5.0f + p.spin) * 9.0f * wob * dt;
                    p.z += std::cos(p.age * 4.3f + p.spin) * 9.0f * wob * dt;
                    if (p.y >= WaterPartSurface(swell, p.x, p.z, p.surface)) {
                        dead = true;
                        if (p.pop) {
                            if (pops < 4) { pops++; AddWaterPart(WP_Disc, p.x, p.surface, p.z, 0, 0, 0, 3.0f, 12.0f, 0.55f, p.surface); }
                            if (gWaterRipples && Flora01(gWaterFrame, static_cast<int>(k), 711) < 0.35f) AddWaterDisturb(p.x, p.z, 0.12f, 6.0f);
                        }
                    }
                    break;
                }
                case WP_Puff:
                    p.x += p.vx * dt; p.y += p.vy * dt; p.z += p.vz * dt;
                    p.vy *= std::max(0.0f, 1.0f - 2.2f * dt);
                    p.size += p.grow * dt;
                    break;
                default:
                    p.size += p.grow * dt;
                    p.grow *= std::max(0.0f, 1.0f - 2.0f * dt);
                    p.y = WaterPartSurface(swell, p.x, p.z, p.surface);
                    break;
            }
        } else if (p.age < 0.0f && p.kind == WP_Disc) {
            p.y = p.surface;
        }
        if (!dead) gWaterParts[keep++] = p;
    }
    gWaterParts.resize(keep);
}

void DrawWaterParts(PlayState* play, float light) {
    if (gWaterParts.empty() || !WaterTexturesReady()) return;
    const Vec3f eye = play->view.eye, at = play->view.lookAt;
    float fx = at.x - eye.x, fy = at.y - eye.y, fz = at.z - eye.z;
    const float fl = std::max(0.001f, std::sqrt(fx * fx + fy * fy + fz * fz));
    fx /= fl; fy /= fl; fz /= fl;
    float rx = -fz, rz = fx;   // right = forward x up
    const float rl = std::max(0.001f, std::hypot(rx, rz));
    rx /= rl; rz /= rl;
    const float ux = -rz * fy, uy = rz * fx - rx * fz, uz = rx * fy;   // up = right x forward
    const float ox = std::round(eye.x / 1024.0f) * 1024.0f, oz = std::round(eye.z / 1024.0f) * 1024.0f;
    const size_t cap = gWaterParts.size();
    Vtx* vtx = static_cast<Vtx*>(FrameAlloc(play, cap * 4 * sizeof(Vtx)));
    if (vtx == nullptr) return;
    size_t quads = 0;
    const float tone = 0.45f + 0.55f * light;
    for (const WaterPart& p : gWaterParts) {
        if (p.age < 0.0f) continue;
        if (std::fabs(p.x - ox) > 7000.0f || std::fabs(p.z - oz) > 7000.0f) continue;
        const float life01 = std::clamp(p.age / std::max(0.05f, p.life), 0.0f, 1.0f);
        float alpha = 0.0f, cr = 232.0f, cg = 246.0f, cb = 252.0f, half = p.size;
        switch (p.kind) {
            case WP_Drop: alpha = 215.0f * (1.0f - 0.5f * life01); cr = 206.0f; cg = 236.0f; cb = 252.0f; break;
            case WP_Bubble: alpha = 225.0f * std::min(1.0f, p.age * 8.0f) * (1.0f - 0.65f * std::max(0.0f, life01 - 0.6f) / 0.4f); cr = 226.0f; cg = 250.0f; cb = 255.0f; break;
            case WP_Puff: alpha = 175.0f * std::min(1.0f, p.age * 10.0f) * (1.0f - life01) * (1.0f - life01); half = p.size; break;
            default: alpha = 200.0f * std::min(1.0f, p.age * 12.0f) * (1.0f - life01) * (1.0f - 0.4f * life01); break;
        }
        if (alpha < 2.0f) continue;
        cr *= tone; cg *= tone; cb *= tone;
        Vtx* q = &vtx[quads * 4];
        const float cx = p.x - ox, cz = p.z - oz;
        float ax, ay, az, bx, by, bz;   // the quad's two half-axes
        if (p.kind == WP_Disc) {
            const float cs = std::cos(p.spin), sn = std::sin(p.spin);
            ax = cs * half; ay = 0.0f; az = sn * half; bx = -sn * half; by = 0.0f; bz = cs * half;
        } else if (p.kind == WP_Drop) {   // stretched along its flight, as seen from the camera
            float vx = p.vx, vy = p.vy, vz = p.vz;
            const float d = vx * fx + vy * fy + vz * fz;
            vx -= fx * d; vy -= fy * d; vz -= fz * d;
            const float vl = std::sqrt(vx * vx + vy * vy + vz * vz), stretch = 1.0f + std::min(2.4f, std::sqrt(p.vx * p.vx + p.vy * p.vy + p.vz * p.vz) / 120.0f);
            if (vl > 0.001f) { vx /= vl; vy /= vl; vz /= vl; } else { vx = ux; vy = uy; vz = uz; }
            ax = vx * half * stretch; ay = vy * half * stretch; az = vz * half * stretch;
            bx = fy * vz - fz * vy; by = fz * vx - fx * vz; bz = fx * vy - fy * vx;
            const float bl = std::max(0.001f, std::sqrt(bx * bx + by * by + bz * bz));
            bx *= half / bl; by *= half / bl; bz *= half / bl;
        } else {
            ax = rx * half; ay = 0.0f; az = rz * half; bx = ux * half; by = uy * half; bz = uz * half;
        }
        const int cell = p.kind;   // 0 drop, 1 bubble, 2 puff, 3 disc: the atlas's 2 x 2 cells
        const float u0 = (cell & 1) * 128.0f, v0 = (cell >> 1) * 128.0f;
        const float px[4] = { cx - ax - bx, cx + ax - bx, cx + ax + bx, cx - ax + bx }, py[4] = { p.y - ay - by, p.y + ay - by, p.y + ay + by, p.y - ay + by },
                    pz[4] = { cz - az - bz, cz + az - bz, cz + az + bz, cz - az + bz };
        const float us[4] = { u0, u0 + 127.0f, u0 + 127.0f, u0 }, vs[4] = { v0, v0, v0 + 127.0f, v0 + 127.0f };
        for (int c = 0; c < 4; c++) PushVtxT(q[c], px[c], py[c], pz[c], us[c], vs[c], cr, cg, cb, alpha);
        quads++;
    }
    if (quads == 0) return;
    SetupWaterXlu(play);
    WaterTexturePass(play, gWaterSplashAtlas, true);
    OPEN_DISPS(play->state.gfxCtx);
    Matrix_Translate(ox, 0.0f, oz, MTXMODE_NEW);
    Matrix_Scale(0.25f, 0.25f, 0.25f, MTXMODE_APPLY);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    for (size_t first = 0; first < quads; first += 8) {
        const size_t n = std::min<size_t>(8, quads - first);
        if (!GfxHasRoom(play, 3 + n * 2)) break;
        gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(&vtx[first * 4]), static_cast<int>(n * 4), 0);
        for (size_t k = 0; k < n; k++) gSP2Triangles(POLY_XLU_DISP++, static_cast<int>(k * 4), static_cast<int>(k * 4 + 1), static_cast<int>(k * 4 + 2), 0, static_cast<int>(k * 4), static_cast<int>(k * 4 + 2), static_cast<int>(k * 4 + 3), 0);
    }
    CLOSE_DISPS(play->state.gfxCtx);
    WaterPlainPass(play);
}

// The game's own water effects (the splash cut-out, the flat ring, its bubbles and spray) are caught each frame, before they are drawn. Where a body
// of ours is in the water (Link, a bot, a cart) the effect is just dropped, since WaterBody makes the same splash and rings; anywhere else (an
// arrow or a bomb hitting the water) a splash or a ring of ours takes its place.
void WaterAdoptGameFx() {
    EffectSsInfo& fx = sEffectSsInfo;
    if (fx.table == nullptr || gPlayState == nullptr) return;
    for (int i = 0; i < fx.tableSize; i++) {
        EffectSs& e = fx.table[i];
        if (e.life < 0) continue;
        const u8 ty = e.type;
        if (ty != EFFECT_SS_G_SPLASH && ty != EFFECT_SS_G_RIPPLE && ty != EFFECT_SS_BUBBLE && ty != EFFECT_SS_DT_BUBBLE && ty != EFFECT_SS_SIBUKI && ty != EFFECT_SS_SIBUKI2) continue;
        float surface = 0.0f;
        if (!WaterSurfaceAt(e.pos.x, e.pos.z, &surface) || e.pos.y < surface - 600.0f || e.pos.y > surface + 160.0f) continue;
        bool ours = false;
        for (const WaterBodyNow& b : gWaterBodies)
            if (std::hypot(e.pos.x - b.x, e.pos.z - b.z) < b.size * 3.0f + 40.0f) { ours = true; break; }
        if (!ours && gWaterFxOn) {
            if (ty == EFFECT_SS_G_SPLASH) WaterSplashFx(e.pos.x, e.pos.z, surface, 12.0f, 0.6f);
            else if (ty == EFFECT_SS_G_RIPPLE) WaterRipple(e.pos.x, e.pos.z, surface, 0.8f, 0);
        }
        EffectSs_Delete(&e);
    }
}

// Draws a grid of vertices (`cols` wide) as squares, a row at a time in runs of up to 15 (two rows of 16 points fill the 32 the RSP holds),
// leaving out the squares `use` turns down.
template <typename Use>
void DrawWaterGrid(PlayState* play, const Vtx* v, int cols, int rows, Use use) {
    OPEN_DISPS(play->state.gfxCtx);
    const int V = cols + 1;
    for (int j = 0; j < rows; j++) {
        int i = 0;
        while (i < cols) {
            if (!use(i, j)) { i++; continue; }
            const int c0 = i;
            while (i < cols && i - c0 < 15 && use(i, j)) i++;
            const int m = i - c0;
            if (!GfxHasRoom(play, 4 + m)) { j = rows; break; }
            gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(&v[j * V + c0]), m + 1, 0);
            gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(&v[(j + 1) * V + c0]), m + 1, m + 1);
            for (int k = 0; k < m; k++) gSP2Triangles(POLY_XLU_DISP++, k, k + 1, m + 1 + k + 1, 0, k, m + 1 + k + 1, m + 1 + k, 0);
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// What lies under the grid points: whether there is water, its surface and its depth. The points sit on the world's own grid, so what is found
// once stays right while the camera moves (only the new row and column are looked up); it is forgotten now and then, and when the map changes.
struct WaterCell { float surface, depth; bool have; };
std::unordered_map<uint64_t, WaterCell> gWaterCells[3];   // one lookup memory per mesh (see DrawWaterSheet)
int gWaterCellsAge = 0;
uint64_t gWaterCellsMap = 0;
int gWaterCellsGen = 0;   // counts the clears, so what was copied out of gWaterCells knows when it is stale
void ClearWaterCells() { for (auto& m : gWaterCells) m.clear(); gWaterCellsGen++; }
uint64_t WaterCellKey(int gi, int gj) { return (static_cast<uint64_t>(static_cast<uint32_t>(gi + 100000)) << 32) | static_cast<uint32_t>(gj + 100000); }

// ---- caustics: the light the waves focus onto the floor under shallow water, as a sheet that lies on the floor (its own fine grid round the
// camera, 40 units a square) under the surface. Brightest in the shallows in full sun; it fades with depth, at night and under cloud.
std::unordered_map<uint64_t, WaterCell> gCausticCells;
uint64_t gCausticCellsMap = 0;

void DrawWaterCaustics(PlayState* play, float t, float light, float sunAmt, float sunWarm) {
    if (!gWaterCaustics || gWaterCausticAmt <= 0.0f || !WaterTexturesReady()) return;
    constexpr int NC = 32, VC = NC + 1;
    constexpr float cc = 40.0f;
    const Vec3f eye = play->view.eye;
    const bool island = OnIsland();
    if (WaterSceneKey() != gCausticCellsMap || gCausticCells.size() > 6000) { gCausticCells.clear(); gCausticCellsMap = WaterSceneKey(); }
    const int gi0 = static_cast<int>(std::lround(eye.x / cc)), gj0 = static_cast<int>(std::lround(eye.z / cc));
    const float cx = gi0 * cc, cz = gj0 * cc;
    static std::vector<WaterCell> cells;
    cells.assign(static_cast<size_t>(VC) * VC, WaterCell{ 0, 0, false });
    float baseY = 0;
    bool any = false;
    for (int j = 0; j < VC; j++)
        for (int i = 0; i < VC; i++) {
            const int gi = gi0 + i - NC / 2, gj = gj0 + j - NC / 2;
            auto it = gCausticCells.find(WaterCellKey(gi, gj));
            if (it == gCausticCells.end()) {
                WaterCell c{ 0, 0, false };
                float fy = 0;
                if (WaterSurfaceAt(gi * cc, gj * cc, &c.surface) && WaterFloorAt(gi * cc, gj * cc, &fy)) {
                    c.depth = c.surface - fy;
                    if (island && !OnKingdomTerrain() && !OnRiftlandsTerrain() && !OnMoonfallTerrain() && !OnSkyloftTerrain()) c.depth = std::min(c.depth, static_cast<float>(royale::fortnite::kSeabedDrop));   // the island's sea bed is drawn no deeper than this
                    c.have = c.depth > 1.0f;
                }
                it = gCausticCells.emplace(WaterCellKey(gi, gj), c).first;
            }
            cells[j * VC + i] = it->second;
            if (it->second.have && !any) { any = true; baseY = it->second.surface; }
        }
    if (!any || eye.y - baseY > 2600.0f) return;
    Vtx* v = static_cast<Vtx*>(FrameAlloc(play, static_cast<size_t>(VC) * VC * sizeof(Vtx)));
    if (v == nullptr) return;
    const float amt = std::min(2.0f, gWaterCausticAmt) * (0.25f + 0.75f * std::clamp(sunAmt, 0.0f, 1.0f)) * light * light * (1.0f + 0.35f * gCamUnder);
    const float os = TexOrigin(cx, kCausticUnits), ot = TexOrigin(cz, kCausticUnits);
    float flowX, flowZ;
    WaterCurrentAt(eye.x, eye.z, t, &flowX, &flowZ);
    const float drift = TexScroll(t * 1.1f);
    const float cr = 200.0f + 55.0f * sunWarm, cg = 255.0f - 30.0f * sunWarm, cb = 238.0f - 80.0f * sunWarm;   // pale aqua light, golden at dusk
    static std::vector<uint8_t> lit;
    lit.assign(static_cast<size_t>(VC) * VC, 0);
    for (int j = 0; j < VC; j++)
        for (int i = 0; i < VC; i++) {
            const WaterCell& c = cells[j * VC + i];
            const float lx = (i - NC / 2) * cc, lz = (j - NC / 2) * cc, wx = cx + lx, wz = cz + lz;
            float a = 0.0f;
            const float floorY = c.surface - c.depth;
            if (c.have) {
                const float depthK = std::clamp((c.depth - 3.0f) / 30.0f, 0.0f, 1.0f) * (1.0f - std::clamp((c.depth - 220.0f) / 420.0f, 0.0f, 1.0f));
                const float d = std::hypot(lx, lz), edge = std::clamp((NC / 2 * cc - d) / (cc * 4.0f), 0.0f, 1.0f);
                a = 215.0f * amt * depthK * edge;
                if (a > 1.0f) lit[j * VC + i] = 1;
            }
            // the pattern sways with the swell above it and drifts with the current
            const float sway = 2.2f * std::sin(t * 0.9f + wx * 0.011f) + 1.6f * std::cos(t * 0.7f + wz * 0.013f);
            PushVtxT(v[j * VC + i], lx, (c.have ? floorY : baseY - 40.0f) + 2.5f - baseY, lz, (wx - os) / kCausticUnits + drift + sway + TexScroll(flowX * 0.02f * t),
                     (wz - ot) / kCausticUnits + drift * 0.6f - sway * 0.5f + TexScroll(flowZ * 0.02f * t), cr, cg, cb, a);
        }
    const int frame = static_cast<int>(t * 11.0f) % kWaterTexFrames;
    DrawIdentity identity(&gCausticCells, gi0, gj0);
    SetupWaterXlu(play);
    WaterTexturePass(play, gWaterCausticTex[frame]);
    OPEN_DISPS(play->state.gfxCtx);
    Matrix_Translate(cx, baseY, cz, MTXMODE_NEW);
    Matrix_Scale(0.25f, 0.25f, 0.25f, MTXMODE_APPLY);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    CLOSE_DISPS(play->state.gfxCtx);
    DrawWaterGrid(play, v, NC, NC, [&](int i, int j) {
        return lit[j * VC + i] || lit[j * VC + i + 1] || lit[(j + 1) * VC + i] || lit[(j + 1) * VC + i + 1];
    });
    WaterPlainPass(play);
}

// ---- the surface: three meshes, one inside the next, that follow the camera (a clipmap).
// Mesh 0 round the player has squares 16 units wide (the swimmer's wake and rings, the fine chop), mesh 1 squares of 64, mesh 2 squares of 256 out to
// the horizon. Each mesh sits on the world's own grid (so the waves never slide) and leaves a hole where the finer one lies; the finer mesh's
// outer rim is blended into the coarser mesh's own edge, so the two meet without a crack. Every pass (the water, the clouds, the swell bands, the
// sparkle, the foam, the streaks) is drawn on every mesh it reaches, so nothing drops out round the player. Before, a coarse sheet carried the
// bands and streaks and small squares near the player had none: the hole under the swimmer, and creases wherever coarse met fine.
// The surface is drawn in up to six passes over the same points:
//   1. the water itself: its colour (turquoise shallows to deep blue, by depth), the sky mirrored in it (Fresnel), the sun's glitter path, the
//      green glow of light through the crests, foam;
//   2. (High detail) the rolling swell bands: a looping texture of mid-sized waves travelling with the wind (docs/water/README.md);
//   3. (Medium detail and up) the clouds mirrored in the water: a cloud texture laid where each cloud's reflection falls (parallax from the
//      camera's height), strongest at a low angle;
//   4. the sparkling ripples: a texture of the light caught on small ripples, in the sky's and the sun's colours, stronger at a low angle;
//   5. textured foam: lacy foam on the shores, in wakes and on the crests of breaking waves;
//   6. (Medium detail and up) the wind's foam streaks: long wisps turned along the wind, on the backs of the swell and the whitecaps.
// The open-sea look (sparse sparkle, streaks, bands, haze, deep troughs, clouds) is shaded in shared/water_look.h; the preview tool draws it too.
constexpr int kWaterLevels = 3;
constexpr float kWaterPush = 1.7f;   // how much stronger than the raw height field the displacement is drawn (it has to read from a camera behind the swimmer)

using WaterLook = royale::water::Look;

enum { Q_X, Q_Y, Q_Z, Q_CR, Q_CG, Q_CB, Q_CA, Q_GR, Q_GG, Q_GB, Q_GA, Q_FO, Q_SK, Q_BR, Q_BG, Q_BB, Q_BA, Q_LR, Q_LG, Q_LB, Q_LA, Q_NX, Q_NZ, Q_N };
enum { PS_BASE, PS_GLINT, PS_FOAM, PS_STREAK, PS_BAND, PS_CLOUD, PS_N };
constexpr int kPassAlpha[PS_N] = { Q_CA, Q_GA, Q_FO, Q_SK, Q_BA, Q_LA };

struct WaterPoint { float q[Q_N]; };
struct WaterLevel {
    float cell = 0.0f, cx = 0.0f, cz = 0.0f;
    int n = 0, V = 0, gi0 = 0, gj0 = 0;
    int hi0 = 0, hi1 = 0, hj0 = 0, hj1 = 0;   // the squares [hi0, hi1) x [hj0, hj1) belong to the finer mesh: not drawn here
    bool want[PS_N] = {};
    std::vector<WaterPoint> pt;
    std::vector<WaterCell> under;
    int underGi = 0, underGj = 0, underV = -1, underGen = -1;   // what `under` was filled for (DrawWaterSheet step 1)
    bool underAny = false; float underBase = 0.0f;           // and its first point with water, if any
    Vtx* vtx[PS_N] = {};
    std::vector<uint8_t> live[PS_N];   // a point where the pass has something to show
    bool Hole(int i, int j) const { return i >= hi0 && i < hi1 && j >= hj0 && j < hj1; }
    bool Any(int pass, int i, int j) const {
        const std::vector<uint8_t>& f = live[pass];
        return f[j * V + i] || f[j * V + i + 1] || f[(j + 1) * V + i] || f[(j + 1) * V + i + 1];
    }
};
WaterLevel gWaterLevel[kWaterLevels];

void DrawWaterSheet(PlayState* play, float t, float light) {
    namespace rw = royale::water;
    const int detail = std::clamp(gWaterDetail, 0, 2);
    const bool island = OnIsland();
    const Vec3f eye = play->view.eye;
    static const float kCell[kWaterLevels] = { 16.0f, 64.0f, 256.0f };
    const int nCells[kWaterLevels] = { detail == 0 ? 24 : 32, detail == 0 ? 24 : 32, detail == 0 ? 16 : 24 };   // each a multiple of 8 so the finer mesh's rim lies on the coarser one's lines
    const float half = nCells[2] * kCell[2] * 0.5f - 256.0f;   // where the sheet has faded out: a snap and a square inside the outer mesh
    if (WaterSceneKey() != gWaterCellsMap || ++gWaterCellsAge > 1200 || gWaterCells[0].size() > 6000 || gWaterCells[1].size() > 6000 || gWaterCells[2].size() > 6000) {
        ClearWaterCells(); gWaterCellsMap = WaterSceneKey(); gWaterCellsAge = 0;
    }
    for (int l = 0; l < kWaterLevels; l++) {
        WaterLevel& L = gWaterLevel[l];
        L.cell = kCell[l]; L.n = nCells[l]; L.V = L.n + 1;
        const float snap = l + 1 < kWaterLevels ? kCell[l + 1] : kCell[l];   // centred on the coarser mesh's lines, so the hole it leaves is whole squares
        L.cx = std::round(eye.x / snap) * snap; L.cz = std::round(eye.z / snap) * snap;
        L.gi0 = static_cast<int>(std::lround(L.cx / L.cell)); L.gj0 = static_cast<int>(std::lround(L.cz / L.cell));
        L.hi0 = L.hi1 = L.hj0 = L.hj1 = 0;
    }
    for (int l = 0; l + 1 < kWaterLevels; l++) {
        const WaterLevel& c = gWaterLevel[l];
        WaterLevel& P = gWaterLevel[l + 1];
        const int span = static_cast<int>(std::lround(c.n * c.cell / P.cell));
        P.hi0 = static_cast<int>(std::lround((c.cx - c.n * c.cell * 0.5f) / P.cell)) - (P.gi0 - P.n / 2);
        P.hj0 = static_cast<int>(std::lround((c.cz - c.n * c.cell * 0.5f) / P.cell)) - (P.gj0 - P.n / 2);
        P.hi1 = std::clamp(P.hi0 + span, 0, P.n); P.hj1 = std::clamp(P.hj0 + span, 0, P.n);
        P.hi0 = std::clamp(P.hi0, 0, P.n); P.hj0 = std::clamp(P.hj0, 0, P.n);
    }
    // Everything is positioned from one origin (a point on a 2048-unit grid near the camera), so a mesh's points keep their place when it re-centres.
    const float Ox = std::round(eye.x / 2048.0f) * 2048.0f, Oz = std::round(eye.z / 2048.0f) * 2048.0f;

    // 1. what is under each point (looked up once, then remembered)
    float baseY = 0; bool any = false;
    for (int l = 0; l < kWaterLevels; l++) {
        WaterLevel& L = gWaterLevel[l];
        // The mesh only moves when the camera crosses a snap line, so most frames it covers the very same cells as last frame: then `under`
        // already holds what the lookups would give (the memory is only ever added to, between clears).
        if (L.underGen == gWaterCellsGen && L.underGi == L.gi0 && L.underGj == L.gj0 && L.underV == L.V) {
            if (L.underAny && !any) { any = true; baseY = L.underBase; }
            continue;
        }
        L.underGen = gWaterCellsGen; L.underGi = L.gi0; L.underGj = L.gj0; L.underV = L.V; L.underAny = false; L.underBase = 0.0f;
        L.under.assign(static_cast<size_t>(L.V) * L.V, WaterCell{ 0, 0, false });
        for (int j = 0; j < L.V; j++)
            for (int i = 0; i < L.V; i++) {
                const int gi = L.gi0 + i - L.n / 2, gj = L.gj0 + j - L.n / 2;
                auto it = gWaterCells[l].find(WaterCellKey(gi, gj));
                if (it == gWaterCells[l].end()) {
                    WaterCell c{ 0, 0, false };
                    const float wx = gi * L.cell, wz = gj * L.cell;
                    if (WaterSurfaceAt(wx, wz, &c.surface)) {
                        float fy = 0;
                        c.have = true;
                        c.depth = WaterFloorAt(wx, wz, &fy) ? c.surface - fy : 1000.0f;
                    }
                    it = gWaterCells[l].emplace(WaterCellKey(gi, gj), c).first;
                }
                L.under[static_cast<size_t>(j) * L.V + i] = it->second;
                if (it->second.have && !L.underAny) { L.underAny = true; L.underBase = it->second.surface; }
            }
        if (L.underAny && !any) { any = true; baseY = L.underBase; }
    }
    if (!any) return;

    const WaterSky sky = WaterSkyNow();
    // caustics first: they lie on the floor, under the surface
    DrawWaterCaustics(play, t, light, std::max(sky.sunA, 0.3f * sky.moonA), sky.sunWarm);

    // 2. the surface's look at a point
    float wx0, wz0, windNow;
    WindNow(&wx0, &wz0, &windNow);
    const bool texOk = WaterTexturesReady();
    const float glintAmt = gWaterGlints && texOk ? std::min(2.0f, gWaterGlintAmt) : 0.0f;
    const float foamAmt = gWaterFoamTex && texOk ? std::min(2.0f, gWaterFoamAmt) : 0.0f;
    const float reach = 60.0f + 320.0f * gWaterClarity;   // how deep the water must be to hide the floor
    float ampTotal = 0.0f;
    // On lakes and rivers the game's own water sits just under ours unless it is hidden (GameWaterHidden): a small swell then, so it never shows through
    const float amp = WaveAmp() * (island ? 1.0f : GameWaterHidden() ? (WaterIndoors() ? 0.3f : 0.55f) : 0.35f), chop = gWaterChop;
    for (const rw::GerstnerWave& w : rw::kSwell) ampTotal += w.amp * amp;
    rw::LookParams look;
    look.eye[0] = eye.x; look.eye[1] = eye.y; look.eye[2] = eye.z;
    look.t = t; look.light = light; look.reach = reach; look.ampTotal = ampTotal; look.wind = windNow;
    look.glintAmt = glintAmt; look.foamAmt = foamAmt; look.skyOn = gWaterSkyOn;
    look.sharpSpec = 0.0f;   // pin-point glints come from the sparkle texture; on a vertex they light a whole square
    const bool streaksOn = gWaterStreaks && texOk && detail >= 1 && foamAmt > 0.0f, bandsOn = gWaterBands && texOk && detail >= 2;
    const bool cloudsOn = gWaterClouds && texOk && detail >= 1 && gWaterSkyOn && !WaterIndoors();
    look.sparkle = gWaterSparkle && texOk ? 1.0f : 0.0f;
    look.streaks = streaksOn ? std::min(2.0f, gWaterFoamAmt) : 0.0f;
    look.bands = bandsOn ? 1.0f : 0.0f;
    look.haze = gWaterHaze; look.hazeNear = half * 0.35f; look.hazeFar = half * 0.92f;
    look.depthTone = gWaterTone;
    float cloudTint[3];
    look.clouds = cloudsOn ? 1.0f : 0.0f;
    look.cover = std::clamp(0.2f + 0.8f * SkyOvercast(cloudTint), 0.0f, 1.0f);
    // the wind's heading turns the streaks and the bands along it (steady: the wind turns slowly)
    const float windAng = std::atan2(wz0, wx0), windC = std::cos(windAng), windS = std::sin(windAng);
    rw::LookSky lsky;
    for (int q = 0; q < 3; q++) { lsky.hor[q] = sky.hor[q]; lsky.zen[q] = sky.zen[q]; lsky.sun[q] = sky.sun[q]; lsky.moon[q] = sky.moon[q]; }
    lsky.sunA = sky.sunA; lsky.moonA = sky.moonA; lsky.sunWarm = sky.sunWarm;
    // where the surface sits: over the game's own water on lakes and rivers, on the island's (lowered) sea bed there is room to dip
    const float baseLift = island ? 0.0f : GameWaterHidden() ? 2.0f : 5.0f, lowest = island ? -34.0f : GameWaterHidden() ? -6.0f : 1.0f;
    auto ss01 = [](float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); };
    // Textures fade out before the meshes that carry them end (and before their coordinates grow past what the renderer holds).
    const float reachFine = detail == 0 ? 520.0f : 700.0f, reachWide = detail == 0 ? 1400.0f : 2200.0f;
    float flowX, flowZ;
    WaterCurrentAt(eye.x, eye.z, t, &flowX, &flowZ);
    const float gS = TexScroll(t * 1.7f), gT = TexScroll(t * 1.1f);
    const float fS = TexScroll(flowX * t / kFoamUnits), fT = TexScroll(flowZ * t / kFoamUnits);
    const float sS = TexScroll(t * (0.6f + 1.2f * windNow)), bS = TexScroll(t * (1.8f + 1.6f * windNow)), cS = TexScroll(t * (0.5f + 0.9f * windNow));
    const float fl = 0.6f + 0.4f * light;
    // the mirrored clouds lie in the sky: the cloud under a water point at distance d from the camera is kc times as far off (the lower the camera, the more)
    const float hEye = std::max(40.0f, eye.y - baseY), kc = 1.0f + std::min(4.0f, 1500.0f / hEye);
    const float ecx = TexOrigin(eye.x, kCloudUnits), ecz = TexOrigin(eye.z, kCloudUnits);

    static std::vector<rw::SwellPoint> sps;
    static std::vector<float> pushRaw, wakeFoam, surfA, shoreA, alphaA, fadeA, distA;
    for (int l = kWaterLevels - 1; l >= 0; l--) {
        WaterLevel& L = gWaterLevel[l];
        const int n = L.n, V = L.V;
        const size_t nv = static_cast<size_t>(V) * V;
        L.pt.assign(nv, WaterPoint{});
        sps.assign(nv, rw::SwellPoint{}); pushRaw.assign(nv, 0.0f); wakeFoam.assign(nv, 0.0f); surfA.assign(nv, 0.0f);
        shoreA.assign(nv, 0.0f); alphaA.assign(nv, 0.0f); fadeA.assign(nv, 0.0f); distA.assign(nv, 0.0f);
        // Only waves this mesh can draw (longer than about two and a half of its squares): shorter ones land on a random phase at each point, so
        // neighbouring squares get unrelated heights, tilts and sun glints, and the water reads as a patchwork of squares.
        const rw::PreparedSwell swell(t, amp, chop, 2.5f * L.cell);
        // The mesh's points are a regular grid: the swell comes from per-row and per-column tables (rw::SwellGrid, drawing only), made the
        // first time a point needs it.
        static rw::SwellGrid swellGrid;
        static std::vector<float> gridX, gridZ;
        bool gridReady = false;
        for (int j = 0; j < V; j++) {
            // dry points borrow their row's nearest water height (per row: carrying it over from the previous row's far end tilted the triangles at the shore)
            float lastSurface = baseY;
            for (int i = 0; i < V; i++) if (L.under[static_cast<size_t>(j) * V + i].have) { lastSurface = L.under[static_cast<size_t>(j) * V + i].surface; break; }
            for (int i = 0; i < V; i++) {
                const size_t id = static_cast<size_t>(j) * V + i;
                const WaterCell& c = L.under[id];
                const float wx = (L.gi0 + i - n / 2) * L.cell, wz = (L.gj0 + j - n / 2) * L.cell;
                if (!c.have) { surfA[id] = lastSurface; continue; }
                lastSurface = c.surface; surfA[id] = c.surface;
                const float d = std::hypot(wx - eye.x, wz - eye.z);
                distA[id] = d;
                fadeA[id] = std::clamp((half - d) / (half * 0.35f), 0.0f, 1.0f);
                alphaA[id] = ss01((c.depth + 8.0f) / 10.0f);   // the waterline: the surface fades in as the floor drops under it, and the floor's depth test cuts it
                if (alphaA[id] <= 0.0f || fadeA[id] <= 0.0f) continue;
                shoreA[id] = std::min(1.0f, std::max(0.0f, c.depth) / 90.0f);
                if (!gridReady) {
                    gridX.resize(static_cast<size_t>(V)); gridZ.resize(static_cast<size_t>(V));
                    for (int k = 0; k < V; k++) { gridX[k] = (L.gi0 + k - n / 2) * L.cell; gridZ[k] = (L.gj0 + k - n / 2) * L.cell; }
                    swellGrid.Prepare(swell, gridX.data(), V, gridZ.data(), V);
                    gridReady = true;
                }
                sps[id] = swellGrid.Sample(i, j);
                if (l < 2 && (gWaterWakes || gWaterRipples)) pushRaw[id] = WaterDisplaceAt(wx, wz, &wakeFoam[id]) * kWaterPush;
            }
        }
        for (int j = 0; j < V; j++)
            for (int i = 0; i < V; i++) {
                const size_t id = static_cast<size_t>(j) * V + i;
                const WaterCell& c = L.under[id];
                const float wx = (L.gi0 + i - n / 2) * L.cell, wz = (L.gj0 + j - n / 2) * L.cell;
                float* q = L.pt[id].q;
                WaterLook o{};
                float y = surfA[id], px = wx - Ox, pz = wz - Oz, nx = 0.0f, nz = 0.0f;
                if (c.have && alphaA[id] > 0.0f && fadeA[id] > 0.0f) {
                    const rw::SwellPoint& sp = sps[id];
                    const float shore = shoreA[id], fade = fadeA[id], ak = alphaA[id];
                    y = c.surface + royale::graphics::WaterSurfaceOffset(sp.h, pushRaw[id], shore, baseLift, lowest);
                    px += sp.ox * shore; pz += sp.oz * shore;
                    // the swell's own normal, tipped by the slope of the displacement and the ripples
                    const int i0 = std::max(0, i - 1), i1 = std::min(V - 1, i + 1), j0 = std::max(0, j - 1), j1 = std::min(V - 1, j + 1);
                    const float sx = (pushRaw[j * V + i1] * shoreA[j * V + i1] - pushRaw[j * V + i0] * shoreA[j * V + i0]) / ((i1 - i0) * L.cell);
                    const float sz = (pushRaw[j1 * V + i] * shoreA[j1 * V + i] - pushRaw[j0 * V + i] * shoreA[j0 * V + i]) / ((j1 - j0) * L.cell);
                    float ny = sp.ny;
                    nx = sp.nx * shore - sx * ny; nz = sp.nz * shore - sz * ny;
                    const float nl = std::sqrt(nx * nx + ny * ny + nz * nz); nx /= nl; ny /= nl; nz /= nl;
                    const float depth = std::max(c.depth, 0.5f);
                    rw::ShadeWater(look, lsky, wx, wz, y, depth, nx, ny, nz, wakeFoam[id], sp.fold * shore, sp.h * shore, Flora01(static_cast<int>(wx / 16.0f), static_cast<int>(wz / 16.0f), 611), fade, o);
                    // over the game's own water on lakes and rivers, ours has to hide it (it is the same height, just under)
                    if (!island) o.col[3] = std::max(o.col[3], 232.0f * ss01((depth - 3.0f) / 22.0f) * fade);
                    const float d = distA[id];   // (worked out in the first pass: the same point)
                    const float fine = ss01((reachFine - d) / 300.0f), wide = ss01((reachWide - d) / 800.0f);
                    o.col[3] *= ak; o.glint[3] *= ak * fine; o.foam *= ak * fine; o.streak *= ak * wide; o.band[3] *= ak * wide; o.cloud[3] *= ak;
                }
                q[Q_X] = px; q[Q_Y] = y - baseY; q[Q_Z] = pz;
                for (int k = 0; k < 4; k++) { q[Q_CR + k] = o.col[k]; q[Q_GR + k] = o.glint[k]; q[Q_BR + k] = o.band[k]; q[Q_LR + k] = o.cloud[k]; }
                q[Q_FO] = o.foam; q[Q_SK] = o.streak; q[Q_NX] = nx; q[Q_NZ] = nz;
            }
        // The rim of a finer mesh is the coarser mesh's own edge (position, colour, everything), blended into its own shape over a few squares.
        if (l + 1 < kWaterLevels) {
            const WaterLevel& P = gWaterLevel[l + 1];
            constexpr float kRamp = 4.0f;
            for (int j = 0; j < V; j++)
                for (int i = 0; i < V; i++) {
                    const float dist = static_cast<float>(std::min(std::min(i, n - i), std::min(j, n - j)));
                    if (dist >= kRamp) continue;
                    const float w = ss01(dist / kRamp);
                    const float fi = (L.gi0 + i - n / 2) * L.cell / P.cell - (P.gi0 - P.n / 2), fj = (L.gj0 + j - n / 2) * L.cell / P.cell - (P.gj0 - P.n / 2);
                    const int pi = std::clamp(static_cast<int>(std::floor(fi)), 0, P.n - 1), pj = std::clamp(static_cast<int>(std::floor(fj)), 0, P.n - 1);
                    const float u = std::clamp(fi - pi, 0.0f, 1.0f), v = std::clamp(fj - pj, 0.0f, 1.0f);
                    const float* a = P.pt[static_cast<size_t>(pj) * P.V + pi].q;
                    const float* b = P.pt[static_cast<size_t>(pj) * P.V + pi + 1].q;
                    const float* cc = P.pt[static_cast<size_t>(pj + 1) * P.V + pi].q;
                    const float* dd = P.pt[static_cast<size_t>(pj + 1) * P.V + pi + 1].q;
                    float* q = L.pt[static_cast<size_t>(j) * V + i].q;
                    for (int k = 0; k < Q_N; k++) {
                        const float edge = (a[k] * (1 - u) + b[k] * u) * (1 - v) + (cc[k] * (1 - u) + dd[k] * u) * v;
                        q[k] = edge + (q[k] - edge) * w;
                    }
                }
        }
        // the vertices of each pass
        const float og = TexOrigin(L.cx, kGlintUnits), ogz = TexOrigin(L.cz, kGlintUnits), of = TexOrigin(L.cx, kFoamUnits), ofz = TexOrigin(L.cz, kFoamUnits);
        const float cwx = L.cx * windC + L.cz * windS, cwz = -L.cx * windS + L.cz * windC;
        const float osk = TexOrigin(cwx, kStreakUnits), otk = TexOrigin(cwz, kStreakUnits), obs = TexOrigin(cwx, kBandUnits), obt = TexOrigin(cwz, kBandUnits);
        L.want[PS_BASE] = true;
        L.want[PS_GLINT] = glintAmt > 0.0f && l < 2;
        L.want[PS_FOAM] = foamAmt > 0.0f && l < 2;
        L.want[PS_STREAK] = streaksOn;
        L.want[PS_BAND] = bandsOn;
        L.want[PS_CLOUD] = cloudsOn;
        for (int p = 0; p < PS_N; p++) {
            L.vtx[p] = L.want[p] ? static_cast<Vtx*>(FrameAlloc(play, nv * sizeof(Vtx))) : nullptr;
            L.live[p].assign(nv, 0);
            if (L.vtx[p] == nullptr && p == PS_BASE) return;
        }
        for (int j = 0; j < V; j++)
            for (int i = 0; i < V; i++) {
                const size_t id = static_cast<size_t>(j) * V + i;
                const float* q = L.pt[id].q;
                const float wx = (L.gi0 + i - n / 2) * L.cell, wz = (L.gj0 + j - n / 2) * L.cell, nx = q[Q_NX], nz = q[Q_NZ];
                const float wu = wx * windC + wz * windS, wv = -wx * windS + wz * windC;
                PushVtx4(L.vtx[PS_BASE][id], q[Q_X], q[Q_Y], q[Q_Z], q[Q_CR], q[Q_CG], q[Q_CB], q[Q_CA]);
                L.live[PS_BASE][id] = q[Q_CA] >= 1.0f;
                if (Vtx* g = L.vtx[PS_GLINT]) {
                    PushVtxT(g[id], q[Q_X], q[Q_Y] + 0.5f, q[Q_Z], (wx - og) / kGlintUnits + gS + nx * 9.0f, (wz - ogz) / kGlintUnits + gT + nz * 9.0f, q[Q_GR], q[Q_GG], q[Q_GB], q[Q_GA]);
                    L.live[PS_GLINT][id] = q[Q_GA] >= 1.0f;
                }
                if (Vtx* f = L.vtx[PS_FOAM]) {
                    PushVtxT(f[id], q[Q_X], q[Q_Y] + 0.7f, q[Q_Z], (wx - of) / kFoamUnits + fS + nx * 4.0f, (wz - ofz) / kFoamUnits + fT + nz * 4.0f, 240.0f * fl, 248.0f * fl, 252.0f * fl, q[Q_FO]);
                    L.live[PS_FOAM][id] = q[Q_FO] >= 1.0f;
                }
                if (Vtx* k = L.vtx[PS_STREAK]) {
                    PushVtxT(k[id], q[Q_X], q[Q_Y] + 0.9f, q[Q_Z], (wu - osk) / kStreakUnits - sS + nx * 3.0f, (wv - otk) / kStreakUnits + nz * 3.0f, 236.0f * fl, 248.0f * fl, 250.0f * fl, q[Q_SK]);
                    L.live[PS_STREAK][id] = q[Q_SK] >= 1.0f;
                }
                if (Vtx* b = L.vtx[PS_BAND]) {
                    PushVtxT(b[id], q[Q_X], q[Q_Y] + 0.3f, q[Q_Z], (wu - obs) / kBandUnits - bS, (wv - obt) / kBandUnits, q[Q_BR], q[Q_BG], q[Q_BB], q[Q_BA]);
                    L.live[PS_BAND][id] = q[Q_BA] >= 1.0f;
                }
                if (Vtx* cl = L.vtx[PS_CLOUD]) {
                    PushVtxT(cl[id], q[Q_X], q[Q_Y] + 0.4f, q[Q_Z], ((eye.x - ecx) + (wx - eye.x) * kc) / kCloudUnits + windC * cS + nx * 5.0f, ((eye.z - ecz) + (wz - eye.z) * kc) / kCloudUnits + windS * cS + nz * 5.0f,
                             q[Q_LR], q[Q_LG], q[Q_LB], q[Q_LA]);
                    L.live[PS_CLOUD][id] = q[Q_LA] >= 1.0f;
                }
            }
    }

    // The vertex grid is rebuilt at its new origin immediately. Interpolating the old origin with these new vertices would move the entire sheet.
    DrawIdentity identity(&gWaterCells[0], static_cast<int>(std::lround(Ox / 2048.0f)), static_cast<int>(std::lround(Oz / 2048.0f)));
    SetupWaterXlu(play);
    {
        OPEN_DISPS(play->state.gfxCtx);
        Matrix_Translate(Ox, baseY, Oz, MTXMODE_NEW);
        Matrix_Scale(0.25f, 0.25f, 0.25f, MTXMODE_APPLY);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        CLOSE_DISPS(play->state.gfxCtx);
    }
    auto drawPass = [&](int pass) {
        for (int l = kWaterLevels - 1; l >= 0; l--) {
            const WaterLevel& L = gWaterLevel[l];
            if (L.vtx[pass] == nullptr) continue;
            DrawWaterGrid(play, L.vtx[pass], L.n, L.n, [&](int i, int j) { return !L.Hole(i, j) && L.Any(pass, i, j); });
        }
    };
    bool textured = false;
    // pass 1: the water
    drawPass(PS_BASE);
    // pass 2: the rolling swell bands
    if (bandsOn) { WaterTexturePass(play, gWaterBandTex[static_cast<int>(t * 7.0f) % kWaterTexFrames]); drawPass(PS_BAND); textured = true; }
    // pass 3: the clouds in the water
    if (cloudsOn) { WaterTexturePass(play, gWaterCloudImage); drawPass(PS_CLOUD); textured = true; }
    // pass 4: the sparkling ripples (the texture is only drawn fairly near: far off it would shimmer)
    if (glintAmt > 0.0f) {
        WaterTexturePass(play, look.sparkle > 0.0f ? gWaterSparkleTex[static_cast<int>(t * 9.0f) % kWaterTexFrames] : gWaterGlintTex[static_cast<int>(t * 9.0f) % kWaterTexFrames]);
        drawPass(PS_GLINT);
        textured = true;
    }
    // pass 5: the foam
    if (foamAmt > 0.0f) { WaterTexturePass(play, gWaterFoamImage); drawPass(PS_FOAM); textured = true; }
    // pass 6: the wind's foam streaks and whitecaps
    if (streaksOn) { WaterTexturePass(play, gWaterStreakImage); drawPass(PS_STREAK); textured = true; }
    if (textured) WaterPlainPass(play);
    const float cx = Ox, cz = Oz;
    OPEN_DISPS(play->state.gfxCtx);
    // the reflections of whoever stands or swims here: a strip from the feet toward the camera, in their colours, wobbling with the swell
    if (!gWaterRefl.empty() && gWaterSkyOn && gWaterBodiesRefl) {
        constexpr int S = 4;   // slices
        Vtx* rv = static_cast<Vtx*>(FrameAlloc(play, gWaterRefl.size() * 2 * S * sizeof(Vtx)));
        if (rv != nullptr) {
            Vtx* o = rv;
            for (const WaterRefl& w : gWaterRefl) {
                const float height = eye.y - w.surface;
                if (height < 5.0f) continue;
                const float tt = height / (height + w.h);   // where the line to the mirrored head meets the surface
                const float fx = w.x - eye.x, fz = w.z - eye.z, fl = std::max(1.0f, std::hypot(fx, fz)), pxn = -fz / fl, pzn = fx / fl;
                const float cosv = std::clamp(height / std::sqrt(height * height + fl * fl), 0.0f, 1.0f);
                const float fres = 0.28f + 0.6f * std::pow(1.0f - cosv, 2.0f);
                for (int s = 0; s < S; s++) {
                    const float u = static_cast<float>(s) / (S - 1);
                    const float px = w.x - fx * (1.0f - tt) * u, pz = w.z - fz * (1.0f - tt) * u;   // from the feet (u 0) to the mirrored head (u 1)
                    const float wob = std::sin(t * 4.0f + u * 3.0f + w.x * 0.02f) * w.w * 0.2f * (0.5f + amp);
                    const float wd = w.w * (1.0f - 0.3f * u);
                    float col[3];
                    for (int q = 0; q < 3; q++) col[q] = (w.c1[q] + (w.c2[q] - w.c1[q]) * u) * (0.45f + 0.55f * light);
                    const float al = 150.0f * fres * (1.0f - 0.75f * u) * std::min(1.0f, (eye.y - w.surface) / 60.0f);
                    const float oy = w.surface + 3.5f - baseY;
                    PushVtx4(o[s * 2], px - cx + pxn * (wob - wd), oy, pz - cz + pzn * (wob - wd), col[0], col[1], col[2], al * (s == 0 ? 0.6f : 1.0f));
                    PushVtx4(o[s * 2 + 1], px - cx + pxn * (wob + wd), oy, pz - cz + pzn * (wob + wd), col[0], col[1], col[2], al * (s == 0 ? 0.6f : 1.0f));
                }
                gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(o), 2 * S, 0);
                for (int s = 0; s + 1 < S; s++) gSP2Triangles(POLY_XLU_DISP++, s * 2, s * 2 + 1, s * 2 + 3, 0, s * 2, s * 2 + 3, s * 2 + 2, 0);
                o += 2 * S;
            }
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// Puddles get the water's look too: the sky mirrored in them and the sparkling ripples (the same texture as the open water), as a soft oval over
// each puddle that fades out toward its rim. Drawn after all the ground patches, as a decal on them.
void DrawPuddleSheens(PlayState* play) {
    if (gPuddleSheens.empty() || !DebugOn(kDbgWater) || !gWaterGlints || gWaterGlintAmt <= 0.0f || !WaterTexturesReady()) return;
    constexpr int R = 12;   // rim points
    Vtx* v = static_cast<Vtx*>(FrameAlloc(play, gPuddleSheens.size() * (R + 1) * sizeof(Vtx)));
    if (v == nullptr) return;
    const float t = static_cast<float>(ImGui::GetTime()), light = WaterLight();
    const WaterSky sky = WaterSkyNow();
    const float amt = std::min(2.0f, gWaterGlintAmt);
    float col[3];
    for (int q = 0; q < 3; q++) col[q] = std::min(255.0f, (sky.hor[q] + sky.zen[q]) * 0.6f + 80.0f) * (0.55f + 0.45f * light);
    const float gS = std::fmod(t * 1.2f, static_cast<float>(kWaterTex)), gT = std::fmod(t * 0.8f, static_cast<float>(kWaterTex));
    SetupWaterXlu(play);
    WaterTexturePass(play, gWaterSparkle ? gWaterSparkleTex[static_cast<int>(t * 9.0f) % kWaterTexFrames] : gWaterGlintTex[static_cast<int>(t * 9.0f) % kWaterTexFrames]);
    OPEN_DISPS(play->state.gfxCtx);
    gDPSetRenderMode(POLY_XLU_DISP++, G_RM_PASS, G_RM_AA_ZB_XLU_DECAL2);   // nudged toward the camera, like the puddle under it
    for (size_t k = 0; k < gPuddleSheens.size(); k++) {
        const PuddleSheen& p = gPuddleSheens[k];
        if (!GfxHasRoom(play, 12)) break;
        Vtx* o = &v[k * (R + 1)];
        const float ys = std::sin(p.yaw), yc = std::cos(p.yaw), os = TexOrigin(p.x, kGlintUnits), ot = TexOrigin(p.z, kGlintUnits);
        const float centreA = amt * p.fade * (90.0f + 70.0f * light);
        for (int r = 0; r <= R; r++) {
            float lx = 0.0f, lz = 0.0f, al = centreA;
            if (r > 0) {
                const float ang = (r - 1) * 6.2831853f / R;
                lx = std::cos(ang) * p.a * 0.92f; lz = std::sin(ang) * p.b * 0.92f;
                al = 0.0f;
            }
            const float ox = lx * yc + lz * ys, oz = -lx * ys + lz * yc;
            const float wx = p.x + ox, wz = p.z + oz;
            PushVtxT(o[r], ox, p.sx * ox + p.sz * oz, oz, (wx - os) / kGlintUnits + gS, (wz - ot) / kGlintUnits + gT, col[0], col[1], col[2], al);
        }
        Matrix_Translate(p.x, p.y, p.z, MTXMODE_NEW);
        Matrix_Scale(0.25f, 0.25f, 0.25f, MTXMODE_APPLY);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(o), R + 1, 0);
        for (int r = 1; r <= R; r += 2) {
            const int a1 = r, a2 = r % R + 1, a3 = a2 % R + 1;
            gSP2Triangles(POLY_XLU_DISP++, 0, a1, a2, 0, 0, a2, a3, 0);
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);
    WaterPlainPass(play);
    gPuddleSheens.clear();
}

// Drawn every frame with the other world effects (Projectile_Draw).
void CollectCartWalkers(const std::function<void(const void*, float, float, float, float)>& add) {
    for (const auto& [index, c] : gCarts)
        if (c.actor != nullptr && !c.wrecked) add(&c, c.body.x, c.body.y, c.body.z, 46.0f);
}

void DrawWater(PlayState* play) {
    if (!DebugOn(kDbgWater) || gPlayState == nullptr || !WorldFxOn(kRegWater, false)) { gWaterDist.clear(); gWaterTrack.clear(); ClearWaterCells(); gCausticCells.clear(); gWaterParts.clear(); if (!gRipples.Sleeping()) gRipples.Clear(); gCamUnder = 0.0f; return; }
    if (Player* p = GET_PLAYER(play)) gWaterRefY = p->actor.world.pos.y;
    static int previousMap = -1, previousScene = -1;
    if (previousMap != gMapId || previousScene != play->sceneNum) {
        gWaterDist.clear(); gWaterTrack.clear(); ClearWaterCells(); gCausticCells.clear(); gWaterParts.clear();
        gRipples.Clear(); gCamUnder = 0.0f; previousMap = gMapId; previousScene = play->sceneNum;
    }
    Feat("draw: water");
    const float dt = std::min(0.05f, ImGui::GetIO().DeltaTime), t = static_cast<float>(ImGui::GetTime()), light = WaterLight();
    gWaterFrame++;
    gWaterNow = t;
    const bool paused = play->pauseCtx.state != 0;
    gWaterBudget = paused ? 0 : 10;   // splashes and rings made this frame
    gBubbleBudget = paused ? 0 : 16;   // and bubbles and spray
    gBubbleLoad = std::max(0.0f, gBubbleLoad - dt * 26.0f);
    for (size_t i = 0; i < gWaterDist.size();) { if (t - gWaterDist[i].birth > kWaterRingLife) gWaterDist.erase(gWaterDist.begin() + i); else i++; }
    gWaterBodies.clear();
    gWaterRefl.clear();
    Player* player = GET_PLAYER(play);
    if (player == nullptr) return;
    const float pdt = paused ? 0.0f : dt;
    WaterBody(player, player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z, 16.0f, pdt, true);
    for (const auto& [id, actor] : gActorOf) {
        if (actor == nullptr || actor == &player->actor) continue;
        WaterBody(actor, actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, 16.0f * actor->scale.y / 0.01f, pdt, false);
    }
    for (const auto& [index, c] : gCarts) {
        if (c.actor == nullptr || c.wrecked) continue;
        WaterBody(&c, c.body.x, c.body.y, c.body.z, 46.0f, pdt, false);
    }
    for (auto it = gWaterTrack.begin(); it != gWaterTrack.end();) it = it->second.seen != gWaterFrame ? gWaterTrack.erase(it) : std::next(it);
    WaterAdoptGameFx();   // the game's own splashes, rings and bubbles go before they are drawn (ours replace them)
    // The ripple simulation follows the player. Whoever moves through it keeps pushing it (a wake fans out behind, rings cross and mix);
    // someone treading water bobs and sends out slow rings; rain pocks it.
    if (gWaterRipples) {
        gRipples.Recenter(player->actor.world.pos.x, player->actor.world.pos.z);
        const float k = pdt * 60.0f;
        for (const WaterBodyNow& b : gWaterBodies) {
            if (!gRipples.Contains(b.x, b.z, gRipples.Cell() * 4.0f)) continue;
            const float run = std::min(1.5f, b.speed / 120.0f);
            if (run > 0.05f) gRipples.Impulse(b.x, b.z, b.size * 0.05f * run * k, b.size * 0.85f);
            else gRipples.Impulse(b.x, b.z, b.size * 0.03f * std::sin(t * 3.1f + b.x * 0.01f) * k, b.size * 0.8f);
        }
        if ((gWeatherShown.sky == royale::Sky::Rain || gWeatherShown.sky == royale::Sky::Thunder) && gWaterRain && pdt > 0.0f) {
            const int drops = static_cast<int>(WeatherAmount() * 6.0f * k + Flora01(gWaterFrame, 41, 690));
            for (int d = 0; d < drops; d++) {
                const float x = gRipples.MinX() + gRipples.Size() * Flora01(gWaterFrame, d, 691);
                const float z = gRipples.MinZ() + gRipples.Size() * Flora01(gWaterFrame, d, 692);
                float surface = 0;
                if (WaterSurfaceAt(x, z, &surface)) gRipples.Impulse(x, z, 1.2f + 1.5f * Flora01(gWaterFrame, d, 693), 10.0f);
            }
        }
        gRipples.Step(pdt);
    } else {
        if (!gRipples.Sleeping()) gRipples.Clear();
    }
    WaterRain(play, pdt);
    WaterLife(play, pdt, paused);
    // Is the camera under the surface?
    float surface = 0, floorY = 0;
    const Vec3f eye = play->view.eye;
    const bool under = gWaterUnder && WaterSurfaceAt(eye.x, eye.z, &surface) && eye.y < surface && (!WaterFloorAt(eye.x, eye.z, &floorY) || floorY < surface - 6.0f);
    gCamUnder += ((under ? 1.0f : 0.0f) - gCamUnder) * std::min(1.0f, dt * 8.0f);
    if (under) gCamDepth = surface - eye.y;
    DrawWaterSheet(play, t, light);
    WaterPartsStep(pdt, t);
    DrawWaterParts(play, light);
}

// The look from below the surface (the Zora's-Domain-under-water look): the screen goes teal, bright toward the surface and deep blue below,
// darker the deeper you are; soft shafts of light lean down from above and sway; the surface shimmers overhead when you are near it; motes
// drift in the water and bubbles wobble up (white rings with a glint, as the game draws its own).
void DrawUnderwaterOverlay(ImDrawList* dl, ImVec2 ds) {
    if (!DebugOn(kDbgWater) || gCamUnder < 0.02f || gWaterUnderAmt <= 0.0f) return;
    const float k = gCamUnder * std::min(2.0f, gWaterUnderAmt), t = static_cast<float>(ImGui::GetTime()), light = WaterLight();
    const float deep = std::clamp(gCamDepth / 650.0f, 0.0f, 1.0f), px = ds.y / 720.0f;
    const auto a = [&](float v) { return static_cast<int>(std::clamp(v * k, 0.0f, 255.0f)); };
    const auto col = [&](float r, float g, float b, float al) { return IM_COL32(static_cast<int>(std::clamp(r, 0.0f, 255.0f)), static_cast<int>(std::clamp(g, 0.0f, 255.0f)), static_cast<int>(std::clamp(b, 0.0f, 255.0f)), a(al)); };
    const float lt = light * (1.0f - 0.45f * deep);
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ds, col(70 * lt, 200 * lt, 212 * lt, 70 + 50 * deep), col(70 * lt, 200 * lt, 212 * lt, 70 + 50 * deep),
                                col(6, 58 * lt, 104 * lt, 140 + 60 * deep), col(6, 58 * lt, 104 * lt, 140 + 60 * deep));
    // light shafts: each a wide faint quad with a narrower, brighter core
    const float shaftA = 20.0f * light * (1.0f - 0.75f * deep);
    for (int i = 0; i < 8; i++) {
        const float x = ds.x * (0.04f + 0.125f * i + 0.04f * std::sin(t * 0.3f + i * 1.9f)), lean = ds.y * (0.22f + 0.06f * std::sin(t * 0.21f + i));
        const float len = ds.y * (0.75f + 0.25f * std::sin(i * 2.3f)), pulse = 0.6f + 0.4f * std::sin(t * 0.8f + i * 2.7f);
        for (int layer = 0; layer < 2; layer++) {
            const float w = ds.x * (layer == 0 ? 0.06f : 0.022f) * (0.7f + 0.3f * std::sin(t * 0.5f + i));
            const float al = shaftA * pulse * (layer == 0 ? 0.7f : 1.3f);
            dl->AddQuadFilled(ImVec2(x - w * 0.5f, 0), ImVec2(x + w * 0.5f, 0), ImVec2(x + w * 0.9f + lean, len), ImVec2(x - w * 0.1f + lean * 0.9f, len),
                              col(200, 255, 245, al));
        }
    }
    // the surface overhead, rippling, when it is close
    const float nearTop = std::clamp(1.0f - gCamDepth / 260.0f, 0.0f, 1.0f);
    if (nearTop > 0.02f)
        for (int line = 0; line < 4; line++) {
            const float y0 = ds.y * (0.015f + 0.03f * line);
            ImVec2 prev(0, y0);
            for (int sgm = 1; sgm <= 24; sgm++) {
                const float x = ds.x * sgm / 24.0f, y = y0 + std::sin(x * 0.012f / px + t * (1.3f + 0.4f * line) + line * 2.0f) * 5.0f * px;
                dl->AddLine(prev, ImVec2(x, y), col(220, 255, 250, 60.0f * nearTop * light * (1.0f - line * 0.2f)), 2.0f * px);
                prev = ImVec2(x, y);
            }
        }
    // motes drifting in the water
    for (int i = 0; i < 40; i++) {
        const float sx = royale::water::Hash01(i, 1, 77), sy = royale::water::Hash01(i, 2, 77), sp = 0.01f + 0.02f * royale::water::Hash01(i, 3, 77);
        const float x = ds.x * std::fmod(sx + 0.02f * std::sin(t * 0.4f + i), 1.0f), y = ds.y * (1.0f - std::fmod(sy + t * sp, 1.0f));
        dl->AddCircleFilled(ImVec2(x, y), (1.0f + 1.5f * royale::water::Hash01(i, 4, 77)) * px, col(210, 255, 240, 70.0f * light), 6);
    }
    // bubbles wobbling up
    for (int i = 0; i < 9; i++) {
        const float life = std::fmod(t * (0.12f + 0.05f * royale::water::Hash01(i, 5, 78)) + royale::water::Hash01(i, 6, 78), 1.0f);
        const float x = ds.x * (0.08f + 0.84f * royale::water::Hash01(i, 7, 78)) + std::sin(t * 3.0f + i) * 6.0f * px, y = ds.y * (1.05f - 1.1f * life);
        const float r = (4.0f + 7.0f * royale::water::Hash01(i, 8, 78)) * px;
        dl->AddCircle(ImVec2(x, y), r, col(235, 255, 255, 120), 12, 1.6f * px);
        dl->AddCircleFilled(ImVec2(x - r * 0.35f, y - r * 0.35f), r * 0.25f, col(255, 255, 255, 150), 6);
    }
}

// Swim physics, once per game update: in the water a current carries you along (so treading water drifts and swimming against it is slower),
// and you bob with the swell. Only the local player's own body; the server and the others are unaffected.
void ApplySwimPhysics(Player* player) {
    if (!DebugOn(kDbgWater) || !gWaterCurrent || !InField() || InRegularGame() || player == nullptr) return;   // (no drift in the regular game: its water puzzles are the game's)
    Feat("swim physics");
    if (!(player->stateFlags1 & PLAYER_STATE1_IN_WATER)) return;
    const float t = static_cast<float>(ImGui::GetTime()), dt = 1.0f / royale::kTickHz;
    float cx, cz, surface = 0;
    WaterCurrentAt(player->actor.world.pos.x, player->actor.world.pos.z, t, &cx, &cz);
    const float nx = player->actor.world.pos.x + cx * dt, nz = player->actor.world.pos.z + cz * dt;
    float floorY = 0;
    if (WaterSurfaceAt(nx, nz, &surface) && WaterFloorAt(nx, nz, &floorY) && floorY < surface - 40.0f) {   // never drift a swimmer into the shore
        player->actor.world.pos.x = nx;
        player->actor.world.pos.z = nz;
    }
    player->actor.velocity.y += std::sin(t * 2.4f) * 0.04f * WaveAmp();
}


