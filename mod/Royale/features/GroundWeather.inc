// ---- the glider ---------------------------------------------------------------------------------------------------------------
// Everyone skydives under a striped glider. Other players' gliders are drawn along with them (Puppet_Draw); yours is a stand-in actor that
// follows you for the length of the fall, tilting as you steer and folding back when you dive.
float gGliderRoll = 0.0f;       // how far the local glider is banked
bool gGliderDiving = false;
Actor* gLocalGlider = nullptr;

// Each glider's canopy is a small piece of cloth (shared/cloth.h): it billows when air pushes up from below, ripples at the trailing edge and
// flaps harder in storms and wind. Simulated here per glider, from how it is moving through the air now.
struct GliderClothState {
    royale::GliderCloth cloth;
    float lx = 0, ly = 0, lz = 0;
    double lastT = 0;
    bool have = false;
};
std::unordered_map<uint32_t, GliderClothState> gGliderCloth;

void DrawGliderAt(PlayState* play, float x, float y, float z, s16 yaw, float roll, bool diving, uint32_t scheme, bool plain, const Player* hanger) {
    const bool cloth = gClothScale > 0.01f && !plain;
    if (cloth) gGliderClothFrames++;
    const GpuMesh* mesh = GpuMeshFor(cloth ? royale::MeshKind::GliderFrame : royale::MeshKind::Glider, cloth ? 0u : scheme);
    if (mesh == nullptr || mesh->dl.empty()) return;
    const float t = static_cast<float>(play->gameplayFrames) / royale::kTickHz;
    const double now = ImGui::GetTime();
    std::vector<royale::ClothVertex> tris;
    if (cloth) {
        if (gGliderCloth.size() > 48) gGliderCloth.clear();
        GliderClothState& st = gGliderCloth[scheme];
        if (!st.have) { st.lx = x; st.ly = y; st.lz = z; st.lastT = now; st.have = true; }
        const float dt = static_cast<float>(now - st.lastT);
        if (dt > 0.004f) {
            const float h = std::min(dt, 0.1f);
            const float vx = (x - st.lx) / dt, vy = (y - st.ly) / dt, vz = (z - st.lz) / dt;
            st.lx = x; st.ly = y; st.lz = z; st.lastT = now;
            float wx, wz, wind;
            WindNow(&wx, &wz, &wind);
            // The air as the glider sees it: wind minus its own movement, turned into the glider's frame (x to its left, z along its nose).
            const float ax = wx - vx, az = wz - vz, ay = -vy;
            const float yawRad = yaw * (3.14159265f / 32768.0f), c = std::cos(yawRad), sn = std::sin(yawRad);
            const royale::ClothV3 air = { (ax * c - az * sn) * gClothScale, ay * gClothScale, (ax * sn + az * c) * gClothScale };
            st.cloth.Update(h, air, wind * gClothScale, static_cast<float>(now), scheme);
        }
        static const uint8_t schemes[4][2][3] = { {{230, 70, 60}, {245, 235, 220}}, {{70, 130, 235}, {245, 220, 90}}, {{70, 190, 100}, {245, 245, 235}}, {{170, 90, 230}, {250, 210, 120}} };
        st.cloth.Build(tris, schemes[scheme % 4][0], schemes[scheme % 4][1]);
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    // The glider's origin is its handle bar, and the bar is put exactly where Link's two hands are (the game works out where each hand is as it
    // draws him), so he always holds the grips, whatever his size or pose. Banking, pitching and swaying all turn about the bar, so it stays in his hands.
    // Link's size (1 at normal, bigger under Adult Power) scales the windows below. In the ledge-hang pose he hangs from his hands, so the hands are
    // at (or even a little below) the point the game calls his position, with his body below them: the old check that the hands were well *above*
    // that point threw the real hands away every frame, and the bar floated at the fixed fallback height above them instead. Trust any hand
    // position that is finite and anywhere near him; the pose may not have taken yet only on the very first frame, when the position is (0, 0, 0).
    const float size = hanger != nullptr ? std::max(0.3f, hanger->actor.scale.y / 0.01f) : 1.0f;
    static std::unordered_map<uint32_t, Vec3f> lastGrip;   // per glider: hands' offset from his position
    Vec3f grip = { x, y + 20.0f * size, z };   // (before he has been drawn once: roughly where hands hanging from a ledge are)
    float gripScale = 0.7f * size;
    if (auto it = lastGrip.find(scheme); it != lastGrip.end()) grip = { x + it->second.x, y + it->second.y, z + it->second.z };
    if (hanger != nullptr) {
        const Vec3f& l = hanger->bodyPartsPos[PLAYER_BODYPART_L_HAND];
        const Vec3f& r = hanger->bodyPartsPos[PLAYER_BODYPART_R_HAND];
        const float mx = (l.x + r.x) * 0.5f, my = (l.y + r.y) * 0.5f, mz = (l.z + r.z) * 0.5f;
        if (std::isfinite(mx + my + mz) && std::fabs(mx - x) < 150.0f * size && std::fabs(mz - z) < 150.0f * size && std::fabs(my - y) < 260.0f * size) {
            grip = { mx, my, mz };
            if (lastGrip.size() > 48) lastGrip.clear();
            lastGrip[scheme] = { mx - x, my - y, mz - z };
            const float span = std::sqrt((l.x-r.x)*(l.x-r.x)+(l.y-r.y)*(l.y-r.y)+(l.z-r.z)*(l.z-r.z));
            const float handle = royale::glider_model::kGripInner + royale::glider_model::kGripOuter;
            if (span > 10.0f * size && span < 80.0f * size)
                gripScale = std::clamp(span / handle, 0.4f * size, 1.4f * size);
        }
    }
    Matrix_Translate(grip.x, grip.y, grip.z, MTXMODE_NEW);
    Matrix_RotateY(yaw * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    Matrix_RotateZ(roll + std::sin(t * 3.1f + scheme) * 0.04f, MTXMODE_APPLY);               // gentle sway in the wind
    Matrix_RotateX((diving ? 0.5f : 0.1f) + std::sin(t * 2.3f + scheme * 1.7f) * 0.025f, MTXMODE_APPLY); // nose down for a dive
    // Smaller than the model, and flatter so the wing sits just over his head with short struts: it hangs on his hands, not high above him.
    // The scale is about the bar, so the grips stay in his hands (the live cloth wing is drawn in this same space, so it shrinks with the frame).
    Matrix_Scale(gripScale, 0.58f * size, 0.7f * size, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(mesh->dl.data()));
    Vtx* v = tris.empty() ? nullptr : static_cast<Vtx*>(FrameAlloc(play, tris.size() * sizeof(Vtx)));
    if (v != nullptr) {   // the canopy: this frame's triangles, in memory the game hands out for one frame
        for (size_t i = 0; i < tris.size(); i++) {
            v[i].v.ob[0] = static_cast<s16>(std::lround(tris[i].x));
            v[i].v.ob[1] = static_cast<s16>(std::lround(tris[i].y));
            v[i].v.ob[2] = static_cast<s16>(std::lround(tris[i].z));
            v[i].v.flag = 0; v[i].v.tc[0] = v[i].v.tc[1] = 0;
            v[i].v.cn[0] = tris[i].r; v[i].v.cn[1] = tris[i].g; v[i].v.cn[2] = tris[i].b; v[i].v.cn[3] = 255;
        }
        for (size_t first = 0; first < tris.size(); first += 30) {
            const size_t count = std::min<size_t>(30, tris.size() - first);
            gSPVertex(POLY_OPA_DISP++, reinterpret_cast<uintptr_t>(&v[first]), static_cast<int>(count), 0);
            for (size_t k = 0; k + 2 < count; k += 3) gSP1Triangle(POLY_OPA_DISP++, static_cast<int>(k), static_cast<int>(k + 1), static_cast<int>(k + 2), 0);
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

void LocalGlider_Update(Actor* actor, PlayState* play) {
    Player* p = GET_PLAYER(play);
    actor->world.pos = p->actor.world.pos;
    actor->shape.rot.y = p->actor.shape.rot.y;
    actor->focus.pos = actor->world.pos;
}
void LocalGlider_Draw(Actor* actor, PlayState* play) {
    DrawGliderAt(play, actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, actor->shape.rot.y, gGliderRoll, gGliderDiving, 0, false, GET_PLAYER(play));
}
void LocalGlider_Destroy(Actor* actor, PlayState*) { if (gLocalGlider == actor) gLocalGlider = nullptr; }

void ReconcileLocalGlider(bool want) {
    if (want && gLocalGlider == nullptr && InField() && gPlayState != nullptr) {
        Player* p = GET_PLAYER(gPlayState);
        Actor* a = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ISHI, p->actor.world.pos.x, p->actor.world.pos.y, p->actor.world.pos.z, 0, p->actor.shape.rot.y, 0, 0, false);
        if (a == nullptr) return;
        a->update = LocalGlider_Update;
        a->draw = LocalGlider_Draw;
        a->destroy = LocalGlider_Destroy;
        a->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
        a->uncullZoneForward = 6000.0f; a->uncullZoneScale = 3000.0f; a->uncullZoneDownward = 3000.0f;
        a->shape.shadowScale = 0.0f;
        gLocalGlider = a;
    } else if (!want && gLocalGlider != nullptr) {
        Actor_Kill(gLocalGlider);
        gLocalGlider = nullptr;
    }
}

// ---- foliage, snow and puddles on the ground and the weather the game itself draws ------------------------------------------
// Patches of swaying grass, trees (a different set of leaves for each season) and, when it snows, mounds of snow that build up on the ground and
// slowly melt away afterwards are scattered around the player. Rain leaves puddles on level ground that grow while it pours, ripple with
// every drop and dry up slowly once it stops; rain also washes the snow away, and snow covers the puddles over. All of it is local scenery: where it stands is worked out from the map and a hash of
// each cell, so nothing is sent over the network, and it is only ever drawn near the player. Trees are solid (you walk around the trunk).
bool WaterAt(float x, float z, float floorY) { return UnderWater(x, z, floorY); }
float gFoliage = 1.0f;       // the local option, 0 (none) to 2
float gSnowCover = 0.0f;     // 0 bare ground to 1 deep snow
float gPuddleCover = 0.0f;   // 0 dry ground to 1 soaked (big puddles everywhere it is level)
float WeatherAmount();

uint32_t FloraHash(int a, int b, int salt) {
    uint32_t h = static_cast<uint32_t>(a) * 374761393u + static_cast<uint32_t>(b) * 668265263u + static_cast<uint32_t>(salt) * 2246822519u + 0x9E3779B9u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
float Flora01(int a, int b, int salt) { return static_cast<float>(FloraHash(a, b, salt) & 0xFFFF) / 65535.0f; }

constexpr int kGroundSpotKind = 8;   // FloraSpotAt kinds from here are ground patches (below)
struct FloraSpot { bool ok; float y; float sx = 0.0f, sz = 0.0f; };   // sx, sz: how the ground slopes (puddles lie along it)
std::unordered_map<uint64_t, FloraSpot> gFloraSpots;
int gFloraScene = -1;
int gFloraBudget = 0;

// Is there good ground at (x, z)? Cached per cell. `kind`: 0 grass, 1 a tree, 2 a snow mound, 3 a puddle, 4 a snow blanket, 5 small scenery
// (Decor), 6 a town's clutter (cell = the town and the piece), 7 a piece of the Fortnite Map's scenery (fortnite_scenery.h: any slope is fine).
// nullptr = not measured yet (the per-frame budget of measurements ran out).
const FloraSpot* FloraSpotAt(int kind, int cx, int cz, float x, float z) {
    // Ground patches (kinds 8 and up, one per royale::ground::Kind: puddle, snow, frost, leaves, blossom) are measured by the rule of an older kind.
    static const int kPatchRule[royale::ground::kKinds] = { 3, 2, 2, 5, 5 };
    const uint64_t key = (static_cast<uint64_t>(kind) << 58) | (static_cast<uint64_t>(cx + 65536) << 29) | static_cast<uint64_t>(cz + 65536);
    auto it = gFloraSpots.find(key);
    if (it != gFloraSpots.end()) return &it->second;
    if (gFloraBudget <= 0) return nullptr;
    gFloraBudget--;
    FloraSpot spot = { false, 0.0f };
    float y = 0, y2 = 0, y3 = 0;
    if (kind >= kGroundSpotKind) kind = kPatchRule[kind - kGroundSpotKind];
    if (RawFloorAt(x, z, &y) && !WaterAt(x, z, y) && !OnExitFloor(x, z)) {
        spot.ok = true;
        spot.y = y;
        if (kind == 3) {   // puddles only lie on level, even ground: a gentle slope at most, and no bumps or edges across them
            float y4 = 0, y5 = 0;
            spot.ok = RawFloorAt(x + 45.0f, z, &y2) && RawFloorAt(x, z + 45.0f, &y3) && RawFloorAt(x - 45.0f, z, &y4) && RawFloorAt(x, z - 45.0f, &y5) &&
                      std::fabs(y2 - y4) < 14.0f && std::fabs(y3 - y5) < 14.0f && std::fabs(y2 + y4 - 2.0f * y) < 3.0f && std::fabs(y3 + y5 - 2.0f * y) < 3.0f;
            spot.sx = (y2 - y4) / 90.0f;
            spot.sz = (y3 - y5) / 90.0f;
        } else if (kind == 0 || kind == 1 || kind == 5 || kind == 6) {   // grass, trees and the small things stay off steep ground (and cliff edges)
            spot.ok = RawFloorAt(x + 45.0f, z, &y2) && RawFloorAt(x, z + 45.0f, &y3) && std::fabs(y2 - y) < 26.0f && std::fabs(y3 - y) < 26.0f;
        }
        if (spot.ok && kind == 7 && gSession.Client()) {   // island scenery keeps clear of props and the middle of the towns, whose own pieces stand there
            for (const royale::Prop& p : gSession.Client()->Props())
                if (std::fabs(p.pos.x - x) < 100.0f && std::fabs(p.pos.z - z) < 100.0f) { spot.ok = false; break; }
            for (const royale::Poi& poi : gSession.Client()->Pois())
                if (std::hypot(poi.center.x - x, poi.center.z - z) < poi.radius * 0.55f) { spot.ok = false; break; }
        }
        if (spot.ok && kind == 1 && gSession.Client()) {   // a tree keeps clear of scenery, towns and loot sites
            for (const royale::Prop& p : gSession.Client()->Props())
                if (std::fabs(p.pos.x - x) < 140.0f && std::fabs(p.pos.z - z) < 140.0f) { spot.ok = false; break; }
            for (const royale::Poi& poi : gSession.Client()->Pois())
                if (std::hypot(poi.center.x - x, poi.center.z - z) < poi.radius * 0.7f + 140.0f) { spot.ok = false; break; }
        }
        if (spot.ok && kind == 6 && gSession.Client()) {   // a town's clutter stands clear of its walls, rocks and climbing blocks
            for (const royale::Prop& p : gSession.Client()->Props()) {
                const float clear = royale::IsPlatform(p.kind) ? 120.0f : royale::PropRadius(p.kind) + 35.0f;
                if (royale::PropRadius(p.kind) > 0 && std::fabs(p.pos.x - x) < clear && std::fabs(p.pos.z - z) < clear) { spot.ok = false; break; }
            }
        }
    }
    return &gFloraSpots.emplace(key, spot).first->second;
}

constexpr float kGrassCell = 95.0f, kTreeCell = 380.0f, kDecorCell = 170.0f;
// The Fortnite Map's trees stand where its texture has them painted (fortnite::CoverAt): thick in the woods, a lone one here and there on the
// meadows, none on the roads or in the towns. Its cells are smaller, so the woods can be dense.
constexpr float kIslandTreeCell = 200.0f;
float TreeCell() { return OnIsland() ? kIslandTreeCell : kTreeCell; }

// Each map's plants: Hyrule Field, Lake Hylia and Kakariko keep the leafy trees and green grass (Kakariko's trees sparser), the desert has
// golden dry grass and palms gathered in oases, and Death Mountain has no grass, only a few dead, burnt trees.
int FloraTheme() { return static_cast<int>(CurrentMap().theme); }

struct TreeSpot { float x, y, z, scale, yaw; uint32_t variant; royale::MeshKind kind = royale::MeshKind::Tree; };
bool TreeIn(int cx, int cz, int season, TreeSpot* out) {
    if (OnIsland()) {
        const float x = (static_cast<float>(cx) + 0.12f + 0.76f * Flora01(cx, cz, 23)) * kIslandTreeCell, z = (static_cast<float>(cz) + 0.12f + 0.76f * Flora01(cx, cz, 24)) * kIslandTreeCell;
        const royale::fortnite::Cover cover = royale::fortnite::CoverAt(x, z);
        const float roll = Flora01(cx, cz, 22), dense = std::min(1.0f, gFoliage);
        if (cover == royale::fortnite::Cover::Woods ? roll > 0.35f + 0.5f * dense : cover != royale::fortnite::Cover::Meadow || roll > 0.03f * dense) return false;
        const FloraSpot* spot = FloraSpotAt(1, cx, cz, x, z);
        if (spot == nullptr || !spot->ok) return false;
        *out = { x, spot->y, z, 0.85f + 0.55f * Flora01(cx, cz, 25), Flora01(cx, cz, 26) * 6.2831853f, (FloraHash(cx, cz, 27) % 4) + 4u * static_cast<uint32_t>(season) };
        return true;
    }
    const int theme = FloraTheme();
    const float grove = theme == 4 ? 0.82f : theme == 3 ? 0.7f : theme == 2 ? 0.55f : 0.45f;   // the desert's oases and the mountain's few trees are rare
    if (Flora01(cx / 2, cz / 2, 21) < grove - 0.2f * std::min(1.0f, gFoliage)) return false;   // groves: whole blocks of cells are empty
    if (Flora01(cx, cz, 22) > (theme == 3 ? 0.35f : 0.62f)) return false;
    const float x = (static_cast<float>(cx) + 0.12f + 0.76f * Flora01(cx, cz, 23)) * kTreeCell, z = (static_cast<float>(cz) + 0.12f + 0.76f * Flora01(cx, cz, 24)) * kTreeCell;
    const FloraSpot* spot = FloraSpotAt(1, cx, cz, x, z);
    if (spot == nullptr || !spot->ok) return false;
    *out = { x, spot->y, z, 0.8f + 0.5f * Flora01(cx, cz, 25), Flora01(cx, cz, 26) * 6.2831853f, (FloraHash(cx, cz, 27) % 4) + 4u * static_cast<uint32_t>(season) };
    if (theme == 4) { out->kind = royale::MeshKind::ThemeTree; out->variant = FloraHash(cx, cz, 27) % 4; }          // palms
    if (theme == 3) { out->kind = royale::MeshKind::ThemeTree; out->variant = 4u + FloraHash(cx, cz, 27) % 4; }     // dead trees
    return true;
}

// The small things on the ground between the towns (meshes.h, Decor): which of the map's six is in a cell, or -1 for none. The first two of
// each map are the common ones; the rest turn up now and then, and each kind gathers in drifts (blocks of cells) rather than evenly.
int DecorIn(int cx, int cz) {
    if (Flora01(cx, cz, 81) > 0.34f * std::min(1.3f, gFoliage)) return -1;
    const float r = Flora01(cx / 3, cz / 3, 82) * 0.6f + Flora01(cx, cz, 83) * 0.4f;
    return r < 0.3f ? 0 : r < 0.55f ? 1 : r < 0.68f ? 2 : r < 0.8f ? 3 : r < 0.9f ? 4 : 5;
}

// The lived-in clutter round a town (meshes.h, Clutter): a few groups (a cart with hay, crates and barrels, pots by a lantern, a fire pit
// with its logs, a signpost at the edge), placed from the town's name so everyone sees the same.
struct ClutterPiece { float x, z, yaw; uint32_t variant; };
void TownClutter(const royale::Poi& poi, std::vector<ClutterPiece>& out) {
    static const uint32_t groups[5][3] = {{4, 3, 3}, {0, 1, 0}, {2, 5, 2}, {6, 0, 1}, {7, 1, 2}};
    const int n = 6;
    for (int g = 0; g < n; g++) {
        const float a = (g + 0.6f * Flora01(poi.name, g, 91)) * 6.2831853f / n, d = poi.radius * (0.35f + 0.5f * Flora01(poi.name, g, 92));
        const float gx = poi.center.x + std::cos(a) * d, gz = poi.center.z + std::sin(a) * d;
        const uint32_t* set = groups[FloraHash(poi.name, g, 93) % 5];
        for (int k = 0; k < 3; k++) {
            const float b = a + k * 2.1f + Flora01(g, k, 94), r = k == 0 ? 0.0f : 46.0f + 14.0f * Flora01(poi.name + k, g, 95);
            out.push_back({gx + std::cos(b) * r, gz + std::sin(b) * r, Flora01(poi.name, g * 3 + k, 96) * 6.2831853f, set[k]});
        }
    }
}

void DrawFloraMesh(PlayState* play, const GpuMesh* m, float x, float y, float z, float yaw, float tiltX, float tiltZ, float scale) {
    if (!GfxHasRoom(play, 4, sizeof(Mtx))) return;   // this layer is full: leave the rest out for this frame
    DrawIdentity identity(m, static_cast<int>(std::lround(x * 4.0f)), static_cast<int>(std::lround(z * 4.0f)));
    OPEN_DISPS(play->state.gfxCtx);
    Matrix_Translate(x, y, z, MTXMODE_NEW);
    Matrix_RotateY(yaw, MTXMODE_APPLY);
    Matrix_RotateX(tiltX, MTXMODE_APPLY);
    Matrix_RotateZ(tiltZ, MTXMODE_APPLY);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(m->dl.data()));
    CLOSE_DISPS(play->state.gfxCtx);
}

int FloraSeason() { return gSession.Joined() ? (static_cast<int>(gWeatherShown.season) & 3) : 1; }

// A new scene, or a new world from the host (new towns and scenery): forget which ground was clear of them, and every ground patch worked out from it.
void RefreshFloraWorld(PlayState* play);
void ForgetFloraGround();

void DrawFlora(PlayState* play) {
    if (!InField() || gPlayState == nullptr) return;
    RefreshFloraWorld(play);
    if (gFoliage <= 0.01f) return;
    const int season = FloraSeason();

    Player* pl = GET_PLAYER(play);
    const float px = pl->actor.world.pos.x, pz = pl->actor.world.pos.z;
    const float t = static_cast<float>(ImGui::GetTime());
    float wx, wz, wind;
    WindNow(&wx, &wz, &wind);
    const float wl = std::max(1.0f, std::hypot(wx, wz)), dx = wx / wl, dz = wz / wl;
    gFloraBudget = 36;

    {
        OPEN_DISPS(play->state.gfxCtx);
        Gfx_SetupDL_25Opa(play->state.gfxCtx);
        gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
        gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
        CLOSE_DISPS(play->state.gfxCtx);
    }
    auto fade = [](float dist, float reach) { const float f = std::clamp((reach - dist) / (reach * 0.25f), 0.0f, 1.0f); return f * f * (3.0f - 2.0f * f); };
    // Nothing behind the camera is drawn: about half of everything near the player. `size` is how far the thing reaches from its spot.
    const Vec3f eye = play->view.eye;
    float vx = play->view.lookAt.x - eye.x, vz = play->view.lookAt.z - eye.z;
    const float vl = std::hypot(vx, vz), vy = std::fabs(play->view.lookAt.y - eye.y);
    const bool cull = vl > 1.0f && vl > vy * 0.5f;   // not when looking steeply down (the skydive): then all round is in view
    if (cull) { vx /= vl; vz /= vl; }
    auto inView = [&](float x, float z, float size) { return !cull || (x - eye.x) * vx + (z - eye.z) * vz > -size; };
    const bool island = OnIsland();

    if (gFoliage > 0.01f) {
        // grass: patches (blocks of cells that are grassy) of tufts, leaning and swaying in the wind
        const float reach = 700.0f + 650.0f * std::min(1.5f, gFoliage);
        const int c0x = static_cast<int>(std::floor((px - reach) / kGrassCell)), c1x = static_cast<int>(std::floor((px + reach) / kGrassCell));
        const int c0z = static_cast<int>(std::floor((pz - reach) / kGrassCell)), c1z = static_cast<int>(std::floor((pz + reach) / kGrassCell));
        Feat("wind on scenery");
        // With the wind on, gust fronts roll across the ground along the wind: the plants in a front bend further, then spring back.
        const float amp = 0.07f + 0.3f * wind, lean = 0.05f + 0.45f * wind;
        const float along = 0.0035f, front = t * (1.4f + 1.2f * wind);
        const int theme = FloraTheme();
        const uint32_t grassSeason = theme == 4 ? 2u : static_cast<uint32_t>(season);   // the desert's grass is dry and golden whatever the season
        const float grassy = theme == 4 ? 0.75f : theme == 2 ? 0.58f : 0.5f;           // and sparser, as is Kakariko's
        for (int cz = c0z; cz <= c1z && theme != 3; cz++)                               // none at all on Death Mountain
            for (int cx = c0x; cx <= c1x; cx++) {
                if (Flora01(cx / 6, cz / 6, 41) < (island ? 0.3f : grassy)) continue;               // not a grassy patch
                if (Flora01(cx, cz, 42) > 0.55f * std::min(1.2f, gFoliage) + 0.1f) continue;
                const float x = (static_cast<float>(cx) + 0.15f + 0.7f * Flora01(cx, cz, 43)) * kGrassCell, z = (static_cast<float>(cz) + 0.15f + 0.7f * Flora01(cx, cz, 44)) * kGrassCell;
                const float d = std::hypot(x - px, z - pz);
                if (d > reach || !inView(x, z, 40.0f)) continue;
                if (island) {   // the island's grass grows on its meadows and under its trees, not on the roads, the paving or the fields
                    const royale::fortnite::Cover cover = royale::fortnite::CoverAt(x, z);
                    if (cover != royale::fortnite::Cover::Meadow && cover != royale::fortnite::Cover::Woods) continue;
                }
                const FloraSpot* spot = FloraSpotAt(0, cx, cz, x, z);
                if (spot == nullptr || !spot->ok) continue;
                const GpuMesh* m = GpuMeshFor(royale::MeshKind::Grass, (FloraHash(cx, cz, 45) % 4) + 4u * grassSeason);
                if (m == nullptr || m->dl.empty()) continue;
                const float phase = t * (1.6f + 2.4f * wind) + x * 0.011f + z * 0.009f;
                const float gustFront = 0.55f + 0.45f * std::sin(front - (x * dx + z * dz) * along);
                const float a = lean * gustFront + std::sin(phase) * amp * (0.5f + 0.5f * gustFront) + std::sin(phase * 2.3f + 1.0f) * amp * 0.35f;
                const float k = 0.27f * (0.8f + 0.5f * Flora01(cx, cz, 46)) * fade(d, reach);
                if (k > 0.01f) DrawFloraMesh(play, m, x, spot->y - 1.0f, z, Flora01(cx, cz, 47) * 6.2831853f, dz * a, -dx * a, k);
            }

        // trees
        const float treeReach = (island ? 1500.0f : 1800.0f) + (island ? 1300.0f : 1800.0f) * std::min(1.5f, gFoliage);   // the island's woods are dense: not quite as far
        const float treeCell = TreeCell();
        const int t0x = static_cast<int>(std::floor((px - treeReach) / treeCell)), t1x = static_cast<int>(std::floor((px + treeReach) / treeCell));
        const int t0z = static_cast<int>(std::floor((pz - treeReach) / treeCell)), t1z = static_cast<int>(std::floor((pz + treeReach) / treeCell));
        const float tamp = 0.008f + 0.05f * wind;
        for (int cz = t0z; cz <= t1z; cz++)
            for (int cx = t0x; cx <= t1x; cx++) {
                TreeSpot tr;
                if (!TreeIn(cx, cz, season, &tr)) continue;
                const float d = std::hypot(tr.x - px, tr.z - pz);
                if (d > treeReach || !inView(tr.x, tr.z, 260.0f * tr.scale)) continue;
                const GpuMesh* m = GpuMeshFor(tr.kind, tr.variant);
                if (m == nullptr || m->dl.empty()) continue;
                const float gustFront = 0.55f + 0.45f * std::sin(front - (tr.x * dx + tr.z * dz) * along);
                const float a = tamp * std::sin(t * (1.1f + wind) + tr.x * 0.004f) * (0.6f + 0.4f * gustFront) + wind * 0.035f * gustFront;
                DrawFloraMesh(play, m, tr.x, tr.y - 2.0f, tr.z, tr.yaw, dz * a, -dx * a, tr.scale * std::max(0.01f, fade(d, treeReach)));
            }
    }

    if (gFoliage > 0.01f) {
        // the small things on the ground: flowers, ferns and logs in the field, reeds and driftwood by the lake, gourds and old fences in
        // Kakariko, Bomb Flowers and embers on the mountain, cacti and bones in the desert
        const int theme = FloraTheme();
        const float reach = 900.0f + 700.0f * std::min(1.5f, gFoliage);
        const int c0x = static_cast<int>(std::floor((px - reach) / kDecorCell)), c1x = static_cast<int>(std::floor((px + reach) / kDecorCell));
        const int c0z = static_cast<int>(std::floor((pz - reach) / kDecorCell)), c1z = static_cast<int>(std::floor((pz + reach) / kDecorCell));
        for (int cz = c0z; cz <= c1z; cz++)
            for (int cx = c0x; cx <= c1x; cx++) {
                const int item = DecorIn(cx, cz);
                if (item < 0) continue;
                const float x = (static_cast<float>(cx) + 0.15f + 0.7f * Flora01(cx, cz, 84)) * kDecorCell, z = (static_cast<float>(cz) + 0.15f + 0.7f * Flora01(cx, cz, 85)) * kDecorCell;
                const float d = std::hypot(x - px, z - pz);
                if (d > reach || !inView(x, z, 80.0f)) continue;
                if (island && royale::fortnite::CoverAt(x, z) != royale::fortnite::Cover::Meadow && royale::fortnite::CoverAt(x, z) != royale::fortnite::Cover::Woods) continue;
                const FloraSpot* spot = FloraSpotAt(5, cx, cz, x, z);
                if (spot == nullptr || !spot->ok) continue;
                const GpuMesh* m = GpuMeshFor(royale::MeshKind::Decor, static_cast<uint32_t>(item + 6 * theme));
                if (m == nullptr || m->dl.empty()) continue;
                const float k = (0.85f + 0.4f * Flora01(cx, cz, 86)) * fade(d, reach);
                if (k > 0.02f) DrawFloraMesh(play, m, x, spot->y - 1.0f, z, Flora01(cx, cz, 87) * 6.2831853f, 0, 0, k);
            }
        // the clutter of the people who live in the towns
        if (gSession.Client()) {
            const float townReach = 2200.0f;
            static std::vector<ClutterPiece> pieces;
            const auto& pois = gSession.Client()->Pois();
            for (size_t pi = 0; pi < pois.size(); pi++) {
                if (std::hypot(pois[pi].center.x - px, pois[pi].center.z - pz) > townReach + pois[pi].radius) continue;
                pieces.clear();
                TownClutter(pois[pi], pieces);
                for (size_t k = 0; k < pieces.size(); k++) {
                    const ClutterPiece& c = pieces[k];
                    const float d = std::hypot(c.x - px, c.z - pz);
                    if (d > townReach || !inView(c.x, c.z, 80.0f)) continue;
                    const FloraSpot* spot = FloraSpotAt(6, static_cast<int>(pi), static_cast<int>(k), c.x, c.z);
                    if (spot == nullptr || !spot->ok) continue;
                    const GpuMesh* m = GpuMeshFor(royale::MeshKind::Clutter, c.variant);
                    if (m == nullptr || m->dl.empty()) continue;
                    DrawFloraMesh(play, m, c.x, spot->y - 1.0f, c.z, c.yaw, 0, 0, std::max(0.01f, fade(d, townReach)));
                }
            }
        }
    }

    // The island's gusts: when the wind gets up (a storm blowing in, the storm itself, or any breezy autumn day) leaves are torn off its woods and
    // tumble past the player along the wind, red and gold in autumn, green the rest of the year. Each leaf lives a few seconds, then starts again
    // somewhere upwind; where they are comes from the time alone, so nothing is stored.
    const float gust = !island ? 0.0f : std::clamp(std::max({ (wind - 0.25f) * 1.6f, season == 2 ? 0.35f + wind : 0.0f, gStormWeather * 0.8f }), 0.0f, 1.0f);
    if (gust > 0.05f && gFoliage > 0.01f) {
        // Each leaf belongs to a square of ground near the player (so it stays put in the world as you run past), starting at a spot in it and
        // carried downwind for its few seconds.
        const float cellSize = 700.0f, life = 5.0f, speed = 120.0f + 320.0f * wind;
        const int perCell = static_cast<int>(7.0f * gust * std::min(1.3f, gFoliage));
        const int pcx = static_cast<int>(std::floor(px / cellSize)), pcz = static_cast<int>(std::floor(pz / cellSize));
        for (int ccz = pcz - 1; ccz <= pcz + 1; ccz++)
            for (int ccx = pcx - 1; ccx <= pcx + 1; ccx++)
                for (int j = 0; j < perCell; j++) {
                    const int id = (ccx * 31 + ccz) * 16 + j;
                    const float clock = t / life + Flora01(ccx * 16 + j, ccz, 101);
                    const int round = static_cast<int>(std::floor(clock));
                    const float age = clock - static_cast<float>(round);
                    const float sx = (static_cast<float>(ccx) + Flora01(id, round, 102)) * cellSize - dx * speed * life * 0.5f;
                    const float sz = (static_cast<float>(ccz) + Flora01(id, round, 103)) * cellSize - dz * speed * life * 0.5f;
                    const float flutter = std::sin(t * 3.1f + j) * 30.0f;
                    const float x = sx + dx * speed * age * life - dz * flutter, z = sz + dz * speed * age * life + dx * flutter;
                    if (std::hypot(x - px, z - pz) > 1000.0f || !inView(x, z, 20.0f)) continue;
                    float gy = 0;
                    if (!RawFloorAt(x, z, &gy)) continue;
                    const float y = std::max(gy, static_cast<float>(royale::fortnite::kWaterY)) + 25.0f + 140.0f * Flora01(id, round, 104) * (1.0f - age) + std::sin(t * 2.3f + j * 1.7f) * 18.0f;
                    const GpuMesh* m = GpuMeshFor(royale::MeshKind::LeafPile, season == 2 ? FloraHash(id, round, 105) % 2 : 3u);   // autumn's reds and golds, or a mix with green in it
                    if (m == nullptr || m->dl.empty()) continue;
                    const float k = 0.13f * std::min(1.0f, std::min(age, 1.0f - age) * 6.0f);
                    if (k > 0.004f) DrawFloraMesh(play, m, x, y, z, t * (1.3f + Flora01(id, 3, 106)) + j, std::sin(t * 4.0f + j) * 1.2f, std::cos(t * 3.3f + j * 0.7f) * 1.2f, k);
                }
    }
}

// ---- ground patches: puddles, snow, frost, leaves and blossom ---------------------------------------------------------------------------------
// One system for everything the weather and the seasons leave lying on the ground (shared/ground_patches.h, meshes made in Blender, shared/ground_model.h).
// Every cell may hold a patch of each kind; the weather's cover (how wet, how snowy, how frosty, how autumnal, how springlike) says how many show and how
// big they are. Patches that touch run into one: a field in the rain has a few big puddles, not a heap of small ones on top of each other, and deep
// snow lies in long drifts. Where each is comes from the map and a hash of its cell, so nothing is sent; only the ones near the player are drawn.
royale::ground::Field gGround;   // the patches worked out so far, forgotten when the world changes (ForgetFloraGround)

void ForgetFloraGround() { gGround.Clear(); }

void RefreshFloraWorld(PlayState* play) {
    if (play->sceneNum != gFloraScene) { gFloraScene = play->sceneNum; gFloraSpots.clear(); gGround.Clear(); gSnowCover = 0.0f; gPuddleCover = 0.0f; }
    static float lastSig = 0.0f;
    float sig = 0.0f;
    if (gSession.Client()) {
        const auto& pois = gSession.Client()->Pois();
        sig = static_cast<float>(pois.size()) + static_cast<float>(gSession.Client()->Props().size()) * 1000.0f + (pois.empty() ? 0.0f : pois[0].center.x * 0.37f + pois.back().center.z * 0.11f);
    }
    if (sig != lastSig) { lastSig = sig; gFloraSpots.clear(); gGround.Clear(); }
}

// Something lying on (possibly gently sloping) ground: stretched `a` along its long axis and `b` across it, `rise` as tall as it is wide, turned by `yaw`.
// `alpha` < 0 draws it solid, otherwise see-through with that alpha (0-255).
void DrawGroundPatch(PlayState* play, const GpuMesh* m, float x, float y, float z, float sx, float sz, float yaw, float a, float rise, float b, int alpha, Gfx* dl = nullptr) {
    if (!GfxHasRoom(play, 4, sizeof(Mtx))) return;   // this layer is full: leave the rest out for this frame
    DrawIdentity identity(m, static_cast<int>(std::lround(x * 4.0f)), static_cast<int>(std::lround(z * 4.0f)));
    OPEN_DISPS(play->state.gfxCtx);
    if (dl == nullptr) dl = const_cast<Gfx*>(m->dl.data());
    Matrix_Translate(x, y, z, MTXMODE_NEW);
    Matrix_RotateZ(std::atan(sx), MTXMODE_APPLY);    // lean along the slope, then turn about the ground's own up
    Matrix_RotateX(-std::atan(sz), MTXMODE_APPLY);
    Matrix_RotateY(yaw, MTXMODE_APPLY);
    const float inv = 1.0f / static_cast<float>(m->sub);
    Matrix_Scale(a / royale::ground::kMeshRadius * inv, rise * inv, b / royale::ground::kMeshRadius * inv, MTXMODE_APPLY);
    if (alpha < 0) {
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, dl);
    } else {
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 255, 255, static_cast<u8>(std::clamp(alpha, 0, 255)));
        gSPDisplayList(POLY_XLU_DISP++, dl);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// ---- walking through the patches ----------------------------------------------------------------------------------------------------------
// Whoever walks through snow leaves footprints (a left and a right foot, turned the way they were going, each a soft dent with a low rim of pushed
// snow) that fill in again, quickly while it snows; whoever lands in it from a jump, a fall or the skydive leaves a crater as big as the fall was
// hard. The snow mesh is cut into smaller triangles where a print lies so the print is a smooth shape, not a stamped square (DentedSnow), and every
// point's height change comes from shared/ground_patches.h (DentsChange). Whoever walks through a puddle splashes it and sends rings out across it.
// The walkers are the local player, everyone else's actors and the carts, the same set the water uses.
struct Walker { const void* key; float x, y, z, size, speed, hx, hz, step, vy; };   // (hx, hz: the way they are going, a unit vector; step: the distance moved this frame; vy: up is positive)
struct WalkTrack { float x = 0, y = 0, z = 0, sinceDent = 0, ring = 0, fall = 0; int side = 1; bool inPuddle = false; bool wasPuddle = false; uint32_t seen = 0, dentFrame = 0; };
struct SnowDent { royale::ground::Dent d; float birth, life; };   // d.depth is how deep it was made (before it fills in)
struct PuddleRing { float x, y, z, birth, size; };
// The water's sparkle on puddles: each puddle drawn this frame asks for it here, and the water section draws them all at the end (DrawPuddleSheens).
struct PuddleSheen { float x, y, z, sx, sz, yaw, a, b, fade; };
std::vector<PuddleSheen> gPuddleSheens;
void DrawPuddleSheens(PlayState* play);   // the water section
std::unordered_map<const void*, WalkTrack> gWalkTrack;
std::vector<SnowDent> gSnowDents;
std::vector<PuddleRing> gPuddleRings;
uint32_t gWalkFrame = 0;
int gDentTriBudget = 0;   // triangles the pressed-in snow may use this frame (each print splits the triangles round it into four)
void CollectCartWalkers(const std::function<void(const void*, float, float, float, float)>& add);   // (the carts are known further down)
constexpr float kDentRimGrow = 30.0f;   // a walker this close outside a snow patch's edge still presses the rim
constexpr float kFootLen = 11.0f, kFootWid = 6.5f, kFootDepth = 8.0f;   // a footprint (half-extents and depth in units) for someone of size 16

// A copy of a snow patch's mesh with the snow pressed down where the dents are. The triangles a dent reaches are each split into four (so a print
// is round and smooth: the base mesh has a corner about every 10 units, a print is 20 long), then every corner is moved by the dents' combined height
// change and tinted (pressed snow goes bluer in the pit, a shade whiter on the rim). Triangles no dent reaches are copied as they are, so the pile
// looks exactly as it did until it is walked on, and neighbouring triangles always agree on their shared corners (no cracks). nullptr when there is
// no memory or triangle budget left this frame (the pile is then drawn whole).
Gfx* DentedSnow(PlayState* play, const GpuMesh* m, float x, float z, float yaw, float a, float rise, float b, const std::vector<royale::ground::Dent>& dents) {
    namespace gp = royale::ground;
    struct PV { float lx, h, lz, r, g, bl; };
    const size_t n = m->vtx.size(), tris = n / 3;
    const float inv = 1.0f / static_cast<float>(m->sub);
    const float sa = a / gp::kMeshRadius, sb = b / gp::kMeshRadius, c = std::cos(yaw), sn = std::sin(yaw);
    auto corner = [&](size_t i) { const Vtx& v = m->vtx[i]; return PV{ v.v.ob[0] * inv * sa, v.v.ob[1] * inv * rise, v.v.ob[2] * inv * sb, static_cast<float>(v.v.cn[0]), static_cast<float>(v.v.cn[1]), static_cast<float>(v.v.cn[2]) }; };
    auto worldX = [&](const PV& p) { return x + p.lx * c + p.lz * sn; };
    auto worldZ = [&](const PV& p) { return z - p.lx * sn + p.lz * c; };
    std::vector<uint8_t> touched(tris, 0);
    size_t touchedCount = 0;
    for (size_t t = 0; t < tris; t++) {
        const PV p0 = corner(t * 3), p1 = corner(t * 3 + 1), p2 = corner(t * 3 + 2);
        const float cx = (worldX(p0) + worldX(p1) + worldX(p2)) / 3.0f, cz = (worldZ(p0) + worldZ(p1) + worldZ(p2)) / 3.0f;
        const float rad = std::max({ std::hypot(worldX(p0) - cx, worldZ(p0) - cz), std::hypot(worldX(p1) - cx, worldZ(p1) - cz), std::hypot(worldX(p2) - cx, worldZ(p2) - cz) });
        for (const gp::Dent& d : dents)
            if (std::hypot(cx - d.x, cz - d.z) < gp::DentReach(d) + rad) { touched[t] = 1; touchedCount++; break; }
    }
    if (touchedCount == 0) return nullptr;
    const size_t outTris = tris + 3 * touchedCount;
    if (static_cast<int>(outTris) > gDentTriBudget) return nullptr;
    Vtx* v = static_cast<Vtx*>(FrameAlloc(play, outTris * 3 * sizeof(Vtx)));
    if (v == nullptr) return nullptr;
    const size_t batches = (outTris + 9) / 10;
    Gfx* dl = static_cast<Gfx*>(FrameAlloc(play, (outTris + batches + 1) * sizeof(Gfx)));
    if (dl == nullptr) return nullptr;
    gDentTriBudget -= static_cast<int>(outTris);
    const float cutoff = 0.4f;   // the snow never goes below this: the ground
    size_t w = 0;
    auto put = [&](const PV& p) {
        Vtx& o = v[w++];
        o = m->vtx[0];   // (flag, texture coordinates and alpha as the mesh has them)
        const float wx = worldX(p), wz = worldZ(p);
        const float dy = gp::DentsChange(dents.data(), static_cast<int>(dents.size()), wx, wz);
        float h = p.h, r = p.r, g = p.g, bl = p.bl;
        if (dy != 0.0f) {
            h = std::max(cutoff, p.h + dy);
            const float pit = std::clamp(-dy / kFootDepth, 0.0f, 1.0f) * 0.7f, rim = std::clamp(dy / 3.0f, 0.0f, 1.0f) * 0.35f;
            r = r * (1.0f - pit) + 168.0f * pit; g = g * (1.0f - pit) + 188.0f * pit; bl = bl * (1.0f - pit) + 224.0f * pit;
            r = r * (1.0f - rim) + 255.0f * rim; g = g * (1.0f - rim) + 255.0f * rim; bl = bl * (1.0f - rim) + 255.0f * rim;
        }
        o.v.ob[0] = static_cast<s16>(std::lround(p.lx / std::max(0.05f, sa) * static_cast<float>(m->sub)));
        o.v.ob[1] = static_cast<s16>(std::lround(h / std::max(0.05f, rise) * static_cast<float>(m->sub)));
        o.v.ob[2] = static_cast<s16>(std::lround(p.lz / std::max(0.05f, sb) * static_cast<float>(m->sub)));
        o.v.cn[0] = static_cast<u8>(std::clamp(r, 0.0f, 255.0f)); o.v.cn[1] = static_cast<u8>(std::clamp(g, 0.0f, 255.0f)); o.v.cn[2] = static_cast<u8>(std::clamp(bl, 0.0f, 255.0f));
    };
    auto mid = [](const PV& p, const PV& q) { return PV{ (p.lx + q.lx) * 0.5f, (p.h + q.h) * 0.5f, (p.lz + q.lz) * 0.5f, (p.r + q.r) * 0.5f, (p.g + q.g) * 0.5f, (p.bl + q.bl) * 0.5f }; };
    for (size_t t = 0; t < tris; t++) {
        const PV p0 = corner(t * 3), p1 = corner(t * 3 + 1), p2 = corner(t * 3 + 2);
        if (!touched[t]) { put(p0); put(p1); put(p2); continue; }
        const PV m01 = mid(p0, p1), m12 = mid(p1, p2), m20 = mid(p2, p0);
        put(p0); put(m01); put(m20);
        put(m01); put(p1); put(m12);
        put(m20); put(m12); put(p2);
        put(m01); put(m12); put(m20);
    }
    Gfx* g = dl;
    for (size_t first = 0; first < w; first += 30) {
        const size_t count = std::min<size_t>(30, w - first);
        gSPVertex(g++, reinterpret_cast<uintptr_t>(&v[first]), static_cast<int>(count), 0);
        for (size_t t = 0; t + 2 < count; t += 3) gSP1Triangle(g++, static_cast<int>(t), static_cast<int>(t + 1), static_cast<int>(t + 2), 0);
    }
    gSPEndDisplayList(g++);
    return dl;
}

void DrawGroundPatches(PlayState* play) {
    gPuddleSheens.clear();
    namespace gp = royale::ground;
    if (!InField() || gPlayState == nullptr) return;
    RefreshFloraWorld(play);
    const float dt = std::min(0.05f, ImGui::GetIO().DeltaTime);
    const int season = FloraSeason();
    const bool snowing = gWeatherShown.sky == royale::Sky::Snow && WeatherAmount() > 0.15f;
    const bool rainSky = gWeatherShown.sky == royale::Sky::Rain || gWeatherShown.sky == royale::Sky::Thunder;
    const float rainNow = std::max(rainSky ? WeatherAmount() : 0.0f, gStormWeather * 0.8f);   // the same rain DriveRealWeather lets fall
    const bool raining = rainNow > 0.12f;
    // How snowy and how wet the ground is: snow builds up while it snows and melts slowly (winter keeps a little), rain washes it away and fills the
    // puddles, which dry up slowly or are covered over by snow.
    if (snowing) gSnowCover = std::min(1.0f, gSnowCover + dt / 45.0f * (0.5f + WeatherAmount()));
    else if (raining) gSnowCover = std::max(0.0f, gSnowCover - dt / 40.0f * (0.5f + rainNow));
    else gSnowCover = std::max(season == 3 ? 0.3f : 0.0f, gSnowCover - dt / 150.0f);
    if (raining) gPuddleCover = std::min(1.0f, gPuddleCover + dt / 40.0f * (0.5f + rainNow));
    else gPuddleCover = std::max(0.0f, gPuddleCover - dt / (snowing ? 25.0f : 120.0f));
    const bool island = OnIsland();
    const int theme = FloraTheme();
    const bool mild = island || theme <= 2;   // not the desert or the mountain: frost, leaves and blossom belong to the seasons
    const float covered = std::clamp(1.0f - gSnowCover * 2.5f, 0.0f, 1.0f);   // snow hides what lies under it
    const float amount = gGroundAmount;
    const bool frozen = season == 3 || gSnowCover > 0.25f;
    struct Layer { gp::Kind kind; bool on; float cover; float reach; };
    const Layer layers[gp::kKinds] = {
        { gp::Kind::Snow,    gGroundSnow,    gSnowCover * amount,                                        1000.0f },
        { gp::Kind::Leaves,  gGroundLeaves,  mild && season == 2 ? 0.8f * covered * (raining ? 0.5f : 1.0f) * amount : 0.0f, 650.0f },
        { gp::Kind::Petals,  gGroundLeaves,  mild && season == 0 ? 0.75f * covered * (raining ? 0.4f : 1.0f) * amount : 0.0f, 650.0f },
        { gp::Kind::Frost,   gGroundFrost,   mild && season == 3 ? 0.85f * covered * amount : 0.0f,     800.0f },
        { gp::Kind::Puddle,  gGroundPuddles, std::max(gPuddleCover, island ? 0.12f : 0.0f) * amount,     950.0f },   // (the island's standing water is always there)
    };
    bool any = false;
    for (const Layer& l : layers) any = any || (l.on && l.cover > 0.02f);
    if (!any) return;
    gGround.SetJoin(gGroundMerge ? gp::kJoin : 0.0f);

    Player* pl = GET_PLAYER(play);
    const float px = pl->actor.world.pos.x, pz = pl->actor.world.pos.z;
    const float t = static_cast<float>(ImGui::GetTime());
    gFloraBudget = 48;
    // Who is walking where (tracked frame to frame for speed), and the trail the snow keeps.
    gWalkFrame++;
    const bool paused = play->pauseCtx.state != 0;
    std::vector<Walker> walkers;
    {
        auto add = [&](const void* key, float x, float y, float z, float size) {
            WalkTrack& tr = gWalkTrack[key];
            const float mx = x - tr.x, mz = z - tr.z;
            const bool jumped = tr.seen == 0 || std::hypot(mx, mz) > 500.0f || dt <= 0.0001f || paused;
            const float step = jumped ? 0.0f : std::hypot(mx, mz);
            walkers.push_back({ key, x, y, z, size, step / std::max(dt, 0.0001f), step > 0.01f ? mx / step : 1.0f, step > 0.01f ? mz / step : 0.0f, step, jumped ? 0.0f : (y - tr.y) / std::max(dt, 0.0001f) });
        };
        add(pl, pl->actor.world.pos.x, pl->actor.world.pos.y, pl->actor.world.pos.z, 16.0f);
        for (const auto& [id, actor] : gActorOf)
            if (actor != nullptr && actor != &pl->actor) add(actor, actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, 16.0f * actor->scale.y / 0.01f);
        CollectCartWalkers(add);
    }
    const float nowT = t;
    for (size_t i = 0; i < gSnowDents.size();) { if (nowT - gSnowDents[i].birth > gSnowDents[i].life) gSnowDents.erase(gSnowDents.begin() + i); else i++; }
    gDentTriBudget = 2600;
    const float deform = gGroundDeform;
    const float dentLife = snowing ? 10.0f : 40.0f;   // how long a print lasts (it fills in faster while it snows)
    int fxBudget = paused ? 0 : 3;   // the game's own splashes and ripples this frame (its effect table is shared)
    // The ground says whether a seed may lie where it is: on level ground for water, off the water and the steep for the rest, and the island's
    // meadows and woods for what grows or falls there. Water that stands all the time (the wet places of the Fortnite Map) has an onset below zero.
    auto probe = [&](gp::Kind k, int cx, int cz, gp::Seed& s) -> int {
        const FloraSpot* spot = FloraSpotAt(kGroundSpotKind + static_cast<int>(k), cx, cz, s.x, s.z);
        if (spot == nullptr) return -1;
        if (!spot->ok) return 0;
        if (island) {
            const royale::fortnite::Cover c = royale::fortnite::CoverAt(s.x, s.z);
            if ((k == gp::Kind::Leaves || k == gp::Kind::Petals) && c != royale::fortnite::Cover::Meadow && (k == gp::Kind::Petals || c != royale::fortnite::Cover::Woods)) return 0;
            if (k == gp::Kind::Puddle || k == gp::Kind::Frost) {   // never inside a town
                if (gSession.Client())
                    for (const royale::Poi& poi : gSession.Client()->Pois())
                        if (std::hypot(poi.center.x - s.x, poi.center.z - s.z) < poi.radius * 0.8f) return 0;
            }
            if (k == gp::Kind::Puddle && gp::Hash01(cx, cz, 7) < royale::fortnite::PuddleWetness(s.x, s.z)) s.onset = -0.3f;
        }
        return 1;
    };

    {   // the solid patches first, then the see-through ones
        OPEN_DISPS(play->state.gfxCtx);
        Gfx_SetupDL_25Opa(play->state.gfxCtx);
        gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
        gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        gSPClearGeometryMode(POLY_XLU_DISP++, G_LIGHTING | G_CULL_BACK | G_FOG);   // (the vertex alpha is the soft rim, not the fog)
        gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, SHADE, SHADE, 0, PRIMITIVE, 0, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED);   // vertex colour, vertex alpha x our fade
        gDPSetRenderMode(POLY_XLU_DISP++, G_RM_PASS, G_RM_AA_ZB_XLU_DECAL2);   // a decal: nudged toward the camera so it never z-fights with the ground it lies on
        CLOSE_DISPS(play->state.gfxCtx);
    }
    const Vec3f eye = play->view.eye;
    float vx = play->view.lookAt.x - eye.x, vz = play->view.lookAt.z - eye.z;
    const float vl = std::hypot(vx, vz), vy = std::fabs(play->view.lookAt.y - eye.y);
    const bool cull = vl > 1.0f && vl > vy * 0.5f;   // not when looking steeply down (the skydive): then all round is in view
    if (cull) { vx /= vl; vz /= vl; }
    const GpuMesh* ripple = GpuMeshFor(royale::MeshKind::Ripple, 0);

    for (const Layer& layer : layers) {
        if (!layer.on || layer.cover <= 0.02f) continue;
        const bool seeThrough = !gp::Info(layer.kind).opaque;
        const int c0x = static_cast<int>(std::floor((px - layer.reach) / gp::kCell)), c1x = static_cast<int>(std::floor((px + layer.reach) / gp::kCell));
        const int c0z = static_cast<int>(std::floor((pz - layer.reach) / gp::kCell)), c1z = static_cast<int>(std::floor((pz + layer.reach) / gp::kCell));
        for (int cz = c0z; cz <= c1z; cz++)
            for (int cx = c0x; cx <= c1x; cx++) {
                gp::Patch patch;
                if (gGround.Lead(layer.kind, cx, cz, probe, &patch) != 1) continue;
                const gp::Shape sh = gp::Evaluate(patch, layer.cover);
                if (!sh.shown) continue;
                const float x = patch.x + sh.dx, z = patch.z + sh.dz, d = std::hypot(x - px, z - pz);
                if (d > layer.reach || (cull && (x - eye.x) * vx + (z - eye.z) * vz < -std::max(sh.a, 90.0f))) continue;
                const FloraSpot* spot = FloraSpotAt(kGroundSpotKind + static_cast<int>(layer.kind), patch.cx, patch.cz, patch.x, patch.z);
                if (spot == nullptr || !spot->ok) continue;
                const GpuMesh* m = GpuMeshFor(royale::MeshKind::Ground, gp::MeshVariant(patch, frozen));
                if (m == nullptr || m->dl.empty()) continue;
                const float f = std::clamp((layer.reach - d) / (layer.reach * 0.25f), 0.0f, 1.0f), fade = f * f * (3.0f - 2.0f * f);
                if (fade < 0.02f) continue;
                const float y = spot->y + spot->sx * sh.dx + spot->sz * sh.dz;
                const float shrink = layer.kind == gp::Kind::Snow ? 0.6f + 0.4f * fade : fade;   // heaps sink into the ground at a distance, flat things fade out
                // Is anyone inside this patch? (an ellipse: along its long axis `a`, across it `b`)
                const float ys = std::sin(sh.yaw), yc = std::cos(sh.yaw);
                auto inside = [&](const Walker& w, float grow) {
                    const float ex = w.x - x, ez = w.z - z, lx = ex * yc - ez * ys, lz = ex * ys + ez * yc;
                    const float ra = sh.a * shrink + grow, rb = sh.b * shrink + grow;
                    return lx * lx / (ra * ra) + lz * lz / (rb * rb) < 1.0f && w.y < y + 40.0f;
                };
                if (!seeThrough) {
                    const float lift = layer.kind == gp::Kind::Snow ? 0.6f + 0.4f * std::min(deform, 1.5f) : 1.0f;   // deeper deformation also builds the snow up higher
                    Gfx* dented = nullptr;
                    if (layer.kind == gp::Kind::Snow && gGroundTrails && deform > 0.02f && !paused) {
                        // Anyone standing or walking in this patch presses it down and leaves prints; someone who just landed leaves a crater.
                        for (const Walker& w : walkers) {
                            if (!inside(w, kDentRimGrow)) continue;
                            WalkTrack& tr = gWalkTrack[w.key];
                            if (tr.dentFrame == gWalkFrame) continue;   // (once a frame, even inside two patches)
                            tr.dentFrame = gWalkFrame;
                            const float sz = std::clamp(w.size / 16.0f, 0.5f, 3.0f);
                            const bool onGround = w.y < y + 14.0f && w.vy > -80.0f;
                            if (!onGround) continue;
                            if (tr.fall > 0.0f) {
                                const gp::Crater cr = gp::CraterFor(tr.fall, sz);
                                if (cr.radius > 0.0f) {
                                    if (gSnowDents.size() >= 160) gSnowDents.erase(gSnowDents.begin());
                                    gSnowDents.push_back({ { w.x, w.z, 0.0f, cr.radius, cr.radius, cr.depth }, nowT, dentLife * 2.0f });
                                }
                                tr.fall = 0.0f;
                            }
                            if (w.speed > 10.0f) {
                                tr.sinceDent += w.step;
                                const float stride = (20.0f + std::min(w.speed, 400.0f) / 25.0f) * sz;   // runners take longer strides
                                if (tr.sinceDent >= stride) {
                                    tr.sinceDent = 0.0f;
                                    tr.side = -tr.side;
                                    float fx, fz;
                                    gp::FootprintAt(w.x, w.z, w.hx, w.hz, tr.side, 5.0f * sz, &fx, &fz);
                                    if (gSnowDents.size() >= 160) gSnowDents.erase(gSnowDents.begin());
                                    const float press = 0.7f + 0.3f * std::min(1.0f, w.speed / 200.0f);
                                    gSnowDents.push_back({ { fx, fz, std::atan2(w.hz, w.hx), kFootLen * sz, kFootWid * sz, kFootDepth * press * sz }, nowT, dentLife });
                                }
                            }
                        }
                        // The dents that reach this patch (what is left of each one, and the weight on the snow under everyone's feet right now).
                        const float reachD = std::max(sh.a, sh.b) * shrink + 60.0f;
                        std::vector<gp::Dent> dents;
                        for (const SnowDent& dn : gSnowDents)
                            if (std::hypot(dn.d.x - x, dn.d.z - z) < reachD) {
                                gp::Dent e = dn.d;
                                e.depth *= gp::DentLeft(nowT - dn.birth, dn.life) * deform;
                                if (e.depth > 0.05f) dents.push_back(e);
                            }
                        for (const Walker& w : walkers)
                            if (inside(w, kDentRimGrow) && w.y < y + 14.0f) { const float sz = std::clamp(w.size / 16.0f, 0.5f, 3.0f); dents.push_back({ w.x, w.z, 0.0f, 12.0f * sz, 12.0f * sz, 5.0f * sz * deform }); }
                        if (dents.size() > 40) dents.erase(dents.begin(), dents.end() - 40);   // the newest 40
                        if (!dents.empty()) dented = DentedSnow(play, m, x, z, sh.yaw, sh.a * shrink, sh.rise * shrink * lift, sh.b * shrink, dents);
                    }
                    DrawGroundPatch(play, m, x, y - 1.2f, z, spot->sx, spot->sz, sh.yaw, sh.a * shrink, sh.rise * shrink * lift, sh.b * shrink, -1, dented);
                    continue;
                }
                if (layer.kind == gp::Kind::Puddle && !frozen) {   // wading through: a splash on the way in, rings while moving
                    for (const Walker& w : walkers) {
                        if (!inside(w, 0.0f) || w.y > y + 14.0f) continue;
                        WalkTrack& tr = gWalkTrack[w.key];
                        tr.inPuddle = true;
                        const float pk = std::clamp(0.4f + w.speed / 300.0f, 0.4f, 1.4f);
                        if (!tr.wasPuddle && w.speed > 20.0f) {
                            if (fxBudget >= 2) { fxBudget -= 2; Vec3f sp = { w.x, y + 1.0f, w.z }; EffectSsGSplash_Spawn(gPlayState, &sp, nullptr, nullptr, 0, static_cast<s16>(std::clamp(300.0f * pk * w.size / 16.0f, 220.0f, 900.0f))); }
                            if (gPuddleRings.size() < 48) gPuddleRings.push_back({ w.x, y + 1.8f, w.z, nowT, w.size / 16.0f * pk });
                            tr.ring = 0.0f;
                        } else if (w.speed > 25.0f) {
                            tr.ring -= dt;
                            if (tr.ring <= 0.0f) {
                                tr.ring = std::clamp(0.4f - w.speed / 1200.0f, 0.12f, 0.4f);
                                if (gPuddleRings.size() < 48) gPuddleRings.push_back({ w.x, y + 1.8f, w.z, nowT, w.size / 16.0f * pk * 0.8f });
                                if (w.speed > 140.0f && fxBudget >= 1) { fxBudget--; Vec3f sp = { w.x, y + 1.0f, w.z }; EffectSsGRipple_Spawn(gPlayState, &sp, static_cast<s16>(60.0f * pk), static_cast<s16>(300.0f * pk), 0); }
                            }
                        }
                    }
                }
                const int alpha = static_cast<int>((layer.kind == gp::Kind::Puddle ? 225.0f : 190.0f) * fade);
                if (layer.kind == gp::Kind::Puddle && deform > 0.02f && d < 700.0f) {   // the lip of mud round the water, so the water sits in a slight dip
                    const GpuMesh* bank = GpuMeshFor(royale::MeshKind::Ground, gp::BankVariant(patch));
                    if (bank != nullptr && !bank->dl.empty()) {
                        const float growth = std::clamp(std::sqrt(sh.a * sh.b) / 70.0f, 0.35f, 1.0f);   // a puddle that is only just forming has hardly any bank
                        DrawGroundPatch(play, bank, x, y + 0.5f, z, spot->sx, spot->sz, sh.yaw, sh.a, std::min(deform, 1.6f) * growth * fade, sh.b, -1);
                    }
                }
                DrawGroundPatch(play, m, x, y + 1.0f, z, spot->sx, spot->sz, sh.yaw, sh.a, 1.0f, sh.b, alpha);
                if (layer.kind == gp::Kind::Puddle && !frozen && d < 900.0f && gPuddleSheens.size() < 48) gPuddleSheens.push_back({ x, y + 1.3f, z, spot->sx, spot->sz, sh.yaw, sh.a, sh.b, fade });
                if (layer.kind != gp::Kind::Puddle || !raining || frozen || ripple == nullptr || ripple->dl.empty() || d > 650.0f) continue;
                // Every drop that lands on a puddle rings out across it: each ring grows and fades over 0.8 s, then starts again somewhere else on the puddle.
                const int rings = std::clamp(static_cast<int>((1.0f + rainNow * 3.0f) * std::sqrt(sh.a * sh.b) / 90.0f), 1, 6);
                for (int j = 0; j < rings; j++) {
                    const float phase = t / 0.8f + Flora01(patch.cx, patch.cz, 60 + j);
                    const int drop = static_cast<int>(std::floor(phase));
                    const float age = phase - static_cast<float>(drop);
                    const float ang = Flora01(patch.cx * 31 + j, patch.cz + drop, 61) * 6.2831853f, rr = std::sqrt(Flora01(patch.cx + drop, patch.cz * 17 + j, 62)) * 0.6f;
                    const float lx = std::cos(ang) * rr * sh.a, lz = std::sin(ang) * rr * sh.b;   // along and across the puddle's long axis
                    const float ox = lx * yc + lz * ys, oz = -lx * ys + lz * yc;
                    DrawGroundPatch(play, ripple, x + ox, y + 1.6f + spot->sx * ox + spot->sz * oz, z + oz, spot->sx, spot->sz, 0.0f,
                                    40.0f + 130.0f * age, 1.0f, 40.0f + 130.0f * age, static_cast<int>(190.0f * (1.0f - age) * fade));
                }
            }
    }
    // The rings the walkers left in the puddles, spreading and fading over a moment.
    if (ripple != nullptr && !ripple->dl.empty())
        for (size_t i = 0; i < gPuddleRings.size();) {
            const PuddleRing& r = gPuddleRings[i];
            const float age = (nowT - r.birth) / 0.9f;
            if (age >= 1.0f || age < 0.0f) { gPuddleRings.erase(gPuddleRings.begin() + i); continue; }
            const float sz = (30.0f + 110.0f * age) * std::max(0.6f, r.size);
            DrawGroundPatch(play, ripple, r.x, r.y, r.z, 0.0f, 0.0f, 0.0f, sz, 1.0f, sz, static_cast<int>(200.0f * (1.0f - age)));
            i++;
        }
    for (auto it = gWalkTrack.begin(); it != gWalkTrack.end();) {
        WalkTrack& tr = it->second;
        bool live = false;
        for (const Walker& w : walkers)
            if (w.key == it->first) {
                tr.x = w.x; tr.y = w.y; tr.z = w.z; tr.seen = gWalkFrame; live = true;
                if (w.vy < -200.0f) tr.fall = std::max(tr.fall, -w.vy);   // falling hard: remember how hard until they land
                else if (w.vy > -80.0f) tr.fall = 0.0f;                    // (a landing in snow has used it by now)
                break;
            }
        tr.wasPuddle = tr.inPuddle;
        tr.inPuddle = false;
        it = live ? std::next(it) : gWalkTrack.erase(it);
    }
    DrawPuddleSheens(play);
}

// The Fortnite Map's Hyrule Field scenery (shared/fortnite_scenery.h, meshes.h Scenery): oaks on the hilltops, hedges along the woods and the towns,
// flowers by the roads, cliffs on the steep ground and snowy crags on the high. Where each stands comes from the ground alone, so nothing is stored or
// sent; only the near ones are drawn, the foliage option thins them, and each lives in the same cached-spot table as the trees.
void DrawIslandScenery(PlayState* play) {
    if (!OnIsland() || gPlayState == nullptr || gFoliage <= 0.01f) return;
    const int season = FloraSeason();
    Player* pl = GET_PLAYER(play);
    const float px = pl->actor.world.pos.x, pz = pl->actor.world.pos.z;
    const float t = static_cast<float>(ImGui::GetTime());
    float wx, wz, wind;
    WindNow(&wx, &wz, &wind);
    const float wl = std::max(1.0f, std::hypot(wx, wz)), dx = wx / wl, dz = wz / wl;
    gFloraBudget = 30;
    {
        OPEN_DISPS(play->state.gfxCtx);
        Gfx_SetupDL_25Opa(play->state.gfxCtx);
        gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
        gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
        CLOSE_DISPS(play->state.gfxCtx);
    }
    // Nothing behind the camera is drawn (see DrawFlora).
    const Vec3f eye = play->view.eye;
    float vx = play->view.lookAt.x - eye.x, vz = play->view.lookAt.z - eye.z;
    const float vl = std::hypot(vx, vz), vy = std::fabs(play->view.lookAt.y - eye.y);
    const bool cull = vl > 1.0f && vl > vy * 0.5f;
    if (cull) { vx /= vl; vz /= vl; }
    const float dens = std::min(1.3f, gFoliage), reachScale = 0.5f + 0.5f * std::min(1.0f, gFoliage);
    const float maxReach = royale::fortnite::kSceneryMaxReach * reachScale, cellSize = royale::fortnite::kSceneryCell;
    const int c0x = static_cast<int>(std::floor((px - maxReach) / cellSize)), c1x = static_cast<int>(std::floor((px + maxReach) / cellSize));
    const int c0z = static_cast<int>(std::floor((pz - maxReach) / cellSize)), c1z = static_cast<int>(std::floor((pz + maxReach) / cellSize));
    const float tamp = 0.008f + 0.03f * wind;
    for (int cz = c0z; cz <= c1z; cz++)
        for (int cx = c0x; cx <= c1x; cx++) {
            if (std::hypot((static_cast<float>(cx) + 0.5f) * cellSize - px, (static_cast<float>(cz) + 0.5f) * cellSize - pz) > maxReach + cellSize) continue;
            royale::fortnite::SceneryPiece pc;
            if (!royale::fortnite::SceneryIn(cx, cz, dens, &pc)) continue;
            const float d = std::hypot(pc.x - px, pc.z - pz), reach = royale::fortnite::SceneryReach(pc.kind) * reachScale;
            if (d > reach) continue;
            const float size = 260.0f * pc.scale;
            if (cull && (pc.x - eye.x) * vx + (pc.z - eye.z) * vz < -size) continue;
            const FloraSpot* spot = FloraSpotAt(7, cx, cz, pc.x, pc.z);
            if (spot == nullptr || !spot->ok) continue;
            const GpuMesh* m = GpuMeshFor(royale::MeshKind::Scenery, static_cast<uint32_t>(pc.kind) + 8u * static_cast<uint32_t>(season));
            if (m == nullptr || m->dl.empty()) continue;
            const float f = std::clamp((reach - d) / (reach * 0.25f), 0.0f, 1.0f), fade = f * f * (3.0f - 2.0f * f);
            if (fade < 0.02f) continue;
            using royale::fortnite::SceneryKind;
            const bool soft = pc.kind == SceneryKind::FlowersWhite || pc.kind == SceneryKind::FlowersPink || pc.kind == SceneryKind::Reeds;
            const float a = soft ? (0.05f + 0.2f * wind) * std::sin(t * (1.6f + 2.4f * wind) + pc.x * 0.011f + pc.z * 0.009f) + 0.05f + 0.3f * wind
                                 : (pc.kind == SceneryKind::Oak || pc.kind == SceneryKind::Hedge) ? tamp * std::sin(t * (1.1f + wind) + pc.x * 0.004f) + wind * 0.02f : 0.0f;
            const float sink = pc.kind == SceneryKind::Cliff ? 14.0f * pc.scale : pc.kind == SceneryKind::Boulder ? 6.0f : 1.0f;   // rock sits in the ground, not on it
            DrawFloraMesh(play, m, pc.x, spot->y - sink, pc.z, pc.yaw, dz * a, -dx * a, pc.scale * fade);
        }
}

// Trunks are solid: stand against one and you are pushed out of it (the same sort of local solidity the climbing blocks have).
void ApplyTrees(Player* player) {
    if (!InField() || gFoliage <= 0.01f) return;
    const int season = FloraSeason();
    const float px = player->actor.world.pos.x, pz = player->actor.world.pos.z;
    gFloraBudget = 6;
    const float cell = TreeCell();
    for (int cz = static_cast<int>(std::floor((pz - 160.0f) / cell)); cz <= static_cast<int>(std::floor((pz + 160.0f) / cell)); cz++)
        for (int cx = static_cast<int>(std::floor((px - 160.0f) / cell)); cx <= static_cast<int>(std::floor((px + 160.0f) / cell)); cx++) {
            TreeSpot tr;
            if (!TreeIn(cx, cz, season, &tr)) continue;
            const float r = 20.0f * tr.scale + 14.0f, ddx = px - tr.x, ddz = pz - tr.z, d = std::hypot(ddx, ddz);
            if (d < r && player->actor.world.pos.y < tr.y + 160.0f * tr.scale && d > 0.01f) {
                player->actor.world.pos.x = tr.x + ddx / d * r;
                player->actor.world.pos.z = tr.z + ddz / d * r;
            }
        }
}

// The island's oaks and boulders are solid too (shared/fortnite_scenery.h, SceneryRadius).
bool SolidActive();
void ApplyScenery(Player* player) {
    if (!OnIsland() || gFoliage <= 0.01f || SolidActive()) return;   // with the game's own collision in place (see "solid scenery") the oaks and boulders are real
    const float px = player->actor.world.pos.x, pz = player->actor.world.pos.z, cell = royale::fortnite::kSceneryCell;
    gFloraBudget = 6;
    for (int cz = static_cast<int>(std::floor((pz - 140.0f) / cell)); cz <= static_cast<int>(std::floor((pz + 140.0f) / cell)); cz++)
        for (int cx = static_cast<int>(std::floor((px - 140.0f) / cell)); cx <= static_cast<int>(std::floor((px + 140.0f) / cell)); cx++) {
            royale::fortnite::SceneryPiece pc;
            if (!royale::fortnite::SceneryIn(cx, cz, std::min(1.3f, gFoliage), &pc)) continue;
            const float r = royale::fortnite::SceneryRadius(pc.kind, pc.scale);
            if (r <= 0.0f) continue;
            const float ddx = px - pc.x, ddz = pz - pc.z, d = std::hypot(ddx, ddz);
            if (d >= r || d < 0.01f) continue;
            const FloraSpot* spot = FloraSpotAt(7, cx, cz, pc.x, pc.z);
            if (spot == nullptr || !spot->ok || player->actor.world.pos.y > spot->y + royale::fortnite::SceneryTop(pc.kind, pc.scale)) continue;
            player->actor.world.pos.x = pc.x + ddx / d * r;
            player->actor.world.pos.z = pc.z + ddz / d * r;
        }
}

