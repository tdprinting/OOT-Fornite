// ---- map debug view (Debug > MapDebug, off by default) -----------------------------------------------------------------------------------
// For checking a map in play: the collision the engine really uses (the scene's polygons from EngineBridge.inc, not a copy) drawn see-through
// over the map around Link, coloured by what it is; a panel with where you are, what the floor is there, what is being drawn and how fast; and
// on Hyrule Before the Third Storm the problem spots the exporter found (holes, chests with no floor) as magenta posts.
//   floors green, walls blue, ceilings red, climbable faces yellow, hook-shot wood orange.
// Controls while it is on: hold L and press D-pad Right / Left to teleport to the next / previous place or problem spot, D-pad Up to change
// what is drawn (all collision, floors only, walls only, nothing), D-pad Down to rise 1500 units (look at a spot from above, then fall).
// A grey patch where you can see the map but no colour is drawn is drawn geometry with no collision: you would fall or walk through there.

namespace {   // the same unnamed namespace as the other features (Terrain.inc, EndMatch.inc and Vehicles.inc declare these functions)

struct MapDebugState {
    int mode = 0;                 // 0 all, 1 floors, 2 walls, 3 nothing
    int target = -1;              // last teleport target
    const CollisionPoly* indexed = nullptr; size_t indexedCount = 0;
    std::vector<std::vector<uint32_t>> buckets; int bw = 0, bh = 0; float bx = 0, bz = 0;
    int shown = 0;                // collision triangles drawn last frame
    double frameMs = 0, worstMs = 0; std::chrono::steady_clock::time_point last{};
    double windowWorst = 0; int windowFrames = 0; std::chrono::steady_clock::time_point windowStart{};
};
MapDebugState gMapDebug;
constexpr float kMapDebugBucket = 1024.0f, kMapDebugReach = 1700.0f;
constexpr int kMapDebugMaxTris = 4000;

bool MapDebugOn() { return DebugOn(kDbgMapDebug) && InField() && OnIsland() && gFortniteBuilt && !gFortnitePoly.empty(); }

// Every scene polygon into 1024-unit buckets by its bounding box (once per collision build).
void MapDebugIndex() {
    MapDebugState& m = gMapDebug;
    if (m.indexed == gFortnitePoly.data() && m.indexedCount == gFortnitePoly.size()) return;
    m.indexed = gFortnitePoly.data(); m.indexedCount = gFortnitePoly.size();
    float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
    for (const Vec3s& v : gFortniteVtx) { x0 = std::min(x0, (float)v.x); x1 = std::max(x1, (float)v.x); z0 = std::min(z0, (float)v.z); z1 = std::max(z1, (float)v.z); }
    m.bx = x0; m.bz = z0;
    m.bw = static_cast<int>((x1 - x0) / kMapDebugBucket) + 1; m.bh = static_cast<int>((z1 - z0) / kMapDebugBucket) + 1;
    m.buckets.assign(static_cast<size_t>(m.bw) * m.bh, {});
    for (size_t i = 0; i < gFortnitePoly.size(); ++i) {
        const CollisionPoly& p = gFortnitePoly[i];
        const Vec3s& a = gFortniteVtx[COLPOLY_VTX_INDEX(p.flags_vIA)]; const Vec3s& b = gFortniteVtx[COLPOLY_VTX_INDEX(p.flags_vIB)]; const Vec3s& c = gFortniteVtx[p.vIC];
        const int i0 = static_cast<int>((std::min({a.x, b.x, c.x}) - m.bx) / kMapDebugBucket), i1 = static_cast<int>((std::max({a.x, b.x, c.x}) - m.bx) / kMapDebugBucket);
        const int j0 = static_cast<int>((std::min({a.z, b.z, c.z}) - m.bz) / kMapDebugBucket), j1 = static_cast<int>((std::max({a.z, b.z, c.z}) - m.bz) / kMapDebugBucket);
        for (int j = std::max(0, j0); j <= std::min(m.bh - 1, j1); ++j)
            for (int k = std::max(0, i0); k <= std::min(m.bw - 1, i1); ++k) m.buckets[static_cast<size_t>(j) * m.bw + k].push_back(static_cast<uint32_t>(i));
    }
}

void DrawMapDebugWorld(PlayState* play) {
    gMapDebug.shown = 0;
    if (!MapDebugOn() || gMapDebug.mode == 3) return;
    MapDebugIndex();
    MapDebugState& m = gMapDebug;
    const Player* player = GET_PLAYER(play);
    if (player == nullptr) return;
    const Vec3f at = player->actor.world.pos;
    Vtx* v = static_cast<Vtx*>(FrameAlloc(play, static_cast<size_t>(kMapDebugMaxTris) * 3 * sizeof(Vtx)));
    if (v == nullptr) return;
    static std::vector<uint32_t> picked; picked.clear();
    static std::vector<uint32_t> stamp; static uint32_t stampNow = 0;
    if (stamp.size() != gFortnitePoly.size()) { stamp.assign(gFortnitePoly.size(), 0); stampNow = 0; }
    ++stampNow;
    const int i0 = static_cast<int>((at.x - kMapDebugReach - m.bx) / kMapDebugBucket), i1 = static_cast<int>((at.x + kMapDebugReach - m.bx) / kMapDebugBucket);
    const int j0 = static_cast<int>((at.z - kMapDebugReach - m.bz) / kMapDebugBucket), j1 = static_cast<int>((at.z + kMapDebugReach - m.bz) / kMapDebugBucket);
    for (int j = std::max(0, j0); j <= std::min(m.bh - 1, j1) && static_cast<int>(picked.size()) < kMapDebugMaxTris; ++j)
        for (int k = std::max(0, i0); k <= std::min(m.bw - 1, i1) && static_cast<int>(picked.size()) < kMapDebugMaxTris; ++k)
            for (uint32_t i : m.buckets[static_cast<size_t>(j) * m.bw + k]) {
                if (stamp[i] == stampNow) continue;
                stamp[i] = stampNow;
                const CollisionPoly& p = gFortnitePoly[i];
                const float ny = p.normal.y / 32767.0f;
                if ((m.mode == 1 && ny <= 0.5f) || (m.mode == 2 && std::fabs(ny) > 0.5f)) continue;
                const Vec3s& a = gFortniteVtx[p.vIC];
                if (std::fabs(a.x - at.x) > kMapDebugReach || std::fabs(a.z - at.z) > kMapDebugReach || std::fabs(a.y - at.y) > 1500.0f) continue;
                picked.push_back(i);
                if (static_cast<int>(picked.size()) >= kMapDebugMaxTris) break;
            }
    int n = 0;   // (before OPEN_DISPS: it opens a block of its own)
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_XLU_DISP++, G_LIGHTING | G_CULL_BACK | G_CULL_FRONT | G_FOG);
    gDPSetRenderMode(POLY_XLU_DISP++, G_RM_PASS, G_RM_ZB_XLU_SURF2);
    gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED);
    Matrix_Translate(0, 0, 0, MTXMODE_NEW);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    for (uint32_t i : picked) {
        const CollisionPoly& p = gFortnitePoly[i];
        const float nx = p.normal.x / 32767.0f, ny = p.normal.y / 32767.0f, nz = p.normal.z / 32767.0f;
        const SurfaceType& sf = gFortniteSurface[std::min<u16>(p.type, 15)];
        u8 r, g, b;
        if (sf.data[0] & (4u << 21)) { r = 255; g = 220; b = 40; }            // climbable
        else if (sf.data[1] & (1u << 17)) { r = 255; g = 140; b = 30; }       // hook-shot target
        else if (ny > 0.5f) { r = 60; g = 220; b = 90; }                      // floor
        else if (ny < -0.5f) { r = 255; g = 70; b = 70; }                     // ceiling
        else { r = 70; g = 140; b = 255; }                                     // wall
        const Vec3s* c[3] = {&gFortniteVtx[COLPOLY_VTX_INDEX(p.flags_vIA)], &gFortniteVtx[COLPOLY_VTX_INDEX(p.flags_vIB)], &gFortniteVtx[p.vIC]};
        for (int k = 0; k < 3; ++k) {   // 3 units off the face, so it is not lost in the drawn surface
            auto& o = v[n * 3 + k].v;
            o.ob[0] = static_cast<s16>(c[k]->x + nx * 3.0f); o.ob[1] = static_cast<s16>(c[k]->y + ny * 3.0f); o.ob[2] = static_cast<s16>(c[k]->z + nz * 3.0f);
            o.flag = 0; o.tc[0] = o.tc[1] = 0;
            // a darker corner each, so neighbouring triangles read as a mesh
            const float shade = k == 0 ? 1.0f : k == 1 ? 0.8f : 0.62f;
            o.cn[0] = static_cast<u8>(r * shade); o.cn[1] = static_cast<u8>(g * shade); o.cn[2] = static_cast<u8>(b * shade); o.cn[3] = 95;
        }
        ++n;
    }
    for (int s = 0; s < n; s += 10) {   // 30 corners a load
        const int count = std::min(10, n - s);
        if (!GfxHasRoom(play, 8)) break;
        gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(&v[s * 3]), count * 3, 0);
        for (int t = 0; t < count; ++t) gSP1Triangle(POLY_XLU_DISP++, t * 3, t * 3 + 1, t * 3 + 2, 0);
    }
    // the exporter's problem spots: tall magenta posts (holes: no floor in the open; chests with no floor under them)
    if (OnHyruleThirdStormTerrain()) {
        constexpr size_t kSpots = sizeof(royale::hyrule_third_storm::kDebugSpots) / sizeof(royale::hyrule_third_storm::kDebugSpots[0]);
        Vtx* q = static_cast<Vtx*>(FrameAlloc(play, kSpots * 8 * sizeof(Vtx)));
        if (q != nullptr) {
            int k = 0;
            for (const auto& s : royale::hyrule_third_storm::kDebugSpots) {
                if (s.kind < 0 || std::hypot(s.x - at.x, s.z - at.z) > 12000.0f) continue;
                const float y0 = royale::HyruleThirdStormGroundHeight({s.x, s.z}) - 200.0f, y1 = y0 + 2400.0f, w = 40.0f;
                const float xs[8] = {s.x - w, s.x + w, s.x + w, s.x - w, s.x, s.x, s.x, s.x}, zs[8] = {s.z, s.z, s.z, s.z, s.z - w, s.z + w, s.z + w, s.z - w};
                const float ys[8] = {y0, y0, y1, y1, y0, y0, y1, y1};
                for (int c = 0; c < 8; ++c) {
                    auto& o = q[k * 8 + c].v;
                    o.ob[0] = static_cast<s16>(xs[c]); o.ob[1] = static_cast<s16>(std::clamp(ys[c], -32000.0f, 32000.0f)); o.ob[2] = static_cast<s16>(zs[c]);
                    o.flag = 0; o.tc[0] = o.tc[1] = 0;
                    o.cn[0] = 255; o.cn[1] = s.kind == 0 ? 40 : 160; o.cn[2] = 255; o.cn[3] = 170;
                }
                if (!GfxHasRoom(play, 6)) break;
                gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(&q[k * 8]), 8, 0);
                gSP2Triangles(POLY_XLU_DISP++, 0, 1, 2, 0, 0, 2, 3, 0);
                gSP2Triangles(POLY_XLU_DISP++, 4, 5, 6, 0, 4, 6, 7, 0);
                ++k;
            }
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);
    gMapDebug.shown = n;
}

// The places to jump between: the map's regions, then (on Hyrule Before the Third Storm) the exporter's problem spots.
struct MapDebugTarget { float x, z; std::string name; };
std::vector<MapDebugTarget> MapDebugTargets() {
    std::vector<MapDebugTarget> out;
    if (OnHyruleThirdStormTerrain()) {
        for (const auto& r : royale::hyrule_third_storm::kRegions) out.push_back({r.x, r.z, r.name});
        int k = 0;
        for (const auto& s : royale::hyrule_third_storm::kDebugSpots)
            if (s.kind >= 0) out.push_back({s.x, s.z, std::string(s.kind == 0 ? "Problem: hole " : "Problem: chest with no floor ") + std::to_string(++k)});
    } else if (gSession.Client() != nullptr) {
        for (const auto& p : gSession.Client()->Pois()) out.push_back({p.center.x, p.center.z, royale::kPoiNames[p.name]});
    }
    return out;
}

bool MapDebugInput(Input& in) {
    if (!MapDebugOn() || !(in.cur.button & BTN_L)) return false;
    const u16 press = in.press.button;
    if (!(press & (BTN_DRIGHT | BTN_DLEFT | BTN_DUP | BTN_DDOWN))) return false;
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr) return false;
    if (press & BTN_DUP) gMapDebug.mode = (gMapDebug.mode + 1) % 4;
    if (press & BTN_DDOWN) { player->actor.world.pos.y += 1500.0f; player->actor.prevPos = player->actor.world.pos; player->actor.velocity.y = 0; }
    if (press & (BTN_DRIGHT | BTN_DLEFT)) {
        const auto targets = MapDebugTargets();
        if (!targets.empty()) {
            const int n = static_cast<int>(targets.size());
            gMapDebug.target = ((gMapDebug.target + ((press & BTN_DRIGHT) ? 1 : -1)) % n + n) % n;
            const MapDebugTarget& t = targets[gMapDebug.target];
            float y = 0;
            if (!RawFloorAt(t.x, t.z, &y)) y = player->actor.world.pos.y + 1500.0f;   // no floor there (a hole): arrive above it and fall in
            player->actor.world.pos = {t.x, y + 2.0f, t.z};
            player->actor.home.pos = player->actor.prevPos = player->actor.world.pos;
            player->actor.velocity = {0, 0, 0}; player->actor.speedXZ = 0; player->fallDistance = 0;
            Crumb("map debug: teleport to %s (%.0f, %.0f)", t.name.c_str(), t.x, t.z);
        }
    }
    in.press.button &= ~(BTN_DRIGHT | BTN_DLEFT | BTN_DUP | BTN_DDOWN);
    in.cur.button &= ~(BTN_DRIGHT | BTN_DLEFT | BTN_DUP | BTN_DDOWN);
    return true;
}

void DrawMapDebugPanel(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale) {
    MapDebugState& m = gMapDebug;
    const auto now = std::chrono::steady_clock::now();
    if (m.last.time_since_epoch().count() != 0) {
        const double ms = std::chrono::duration<double, std::milli>(now - m.last).count();
        m.frameMs = m.frameMs * 0.9 + ms * 0.1;
        m.windowWorst = std::max(m.windowWorst, ms); ++m.windowFrames;
        if (now - m.windowStart > std::chrono::seconds(2)) { m.worstMs = m.windowWorst; m.windowWorst = 0; m.windowFrames = 0; m.windowStart = now; }
    }
    m.last = now;
    if (!MapDebugOn() || gPlayState == nullptr) return;
    const Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr) return;
    const Vec3f p = player->actor.world.pos;
    std::vector<std::string> lines;
    char buf[200];
    std::snprintf(buf, sizeof buf, "MAP DEBUG  %s", royale::MapOf(gMapId).name); lines.push_back(buf);
    std::snprintf(buf, sizeof buf, "x %.0f  y %.0f  z %.0f   (%.0f m, %.0f m)", p.x, p.y, p.z, p.x / 36.0f + 900.0f, -p.z / 36.0f + 900.0f); lines.push_back(buf);
    float walk = 0;
    const bool hasWalk = RawFloorAt(p.x, p.z, &walk);
    const bool grounded = (player->actor.bgCheckFlags & 1) != 0;
    std::snprintf(buf, sizeof buf, "engine floor %.0f%s   walk grid %s", player->actor.floorHeight, grounded ? " (standing)" : "",
                  hasWalk ? std::to_string(static_cast<int>(walk)).c_str() : "NONE (a hole)"); lines.push_back(buf);
    if (grounded && hasWalk && std::fabs(player->actor.floorHeight - walk) > 40.0f && std::fabs(player->actor.floorHeight - walk) < 200.0f)
        lines.push_back("!! the bots' floor and the real floor disagree here (bots and chests float or sink)");
    if (OnHyruleThirdStormTerrain()) {
        const int f = royale::hyrule_third_storm::FlagsAt(p.x, p.z);
        std::snprintf(buf, sizeof buf, "water %s   steep %s   cell %d,%d", (f & 1) ? "yes" : "no", (f & 2) ? "yes" : "no",
                      static_cast<int>((p.x + royale::hyrule_third_storm::kHalf) / royale::hyrule_third_storm::kCell), static_cast<int>((p.z + royale::hyrule_third_storm::kHalf) / royale::hyrule_third_storm::kCell));
        lines.push_back(buf);
        const ThirdStormStats& st = gThirdStormStats;
        std::snprintf(buf, sizeof buf, "drawn: %d near + %d far triangles in %d batches, %d near squares", st.nearTris, st.farTris, st.batches, st.nearSquares);
        lines.push_back(buf);
        float best = 1e30f; const char* place = "";
        for (const auto& r : royale::hyrule_third_storm::kRegions) { const float d = std::hypot(r.x - p.x, r.z - p.z); if (d < best) { best = d; place = r.name; } }
        std::snprintf(buf, sizeof buf, "nearest place: %s (%.0f m)", place, best / 36.0f); lines.push_back(buf);
    }
    std::snprintf(buf, sizeof buf, "frame %.1f ms (worst %.0f in 2 s)   collision shown %d of %d", m.frameMs, m.worstMs, m.shown, static_cast<int>(gFortnitePoly.size()));
    lines.push_back(buf);
    static const char* const kModes[] = {"all collision", "floors only", "walls only", "nothing"};
    std::snprintf(buf, sizeof buf, "L+Right/Left teleport   L+Up draw: %s   L+Down rise", kModes[m.mode]); lines.push_back(buf);
    lines.push_back("green floor  blue wall  red ceiling  yellow climb  orange hookshot  magenta problem");
    const float size = 15.0f * scale, lh = size * 1.25f, x = 14.0f * scale, y = ds.y * 0.30f;
    dl->AddRectFilled(ImVec2(x - 6 * scale, y - 6 * scale), ImVec2(x + 560 * scale, y + lh * lines.size() + 6 * scale), IM_COL32(0, 0, 0, 150), 6 * scale);
    for (size_t i = 0; i < lines.size(); ++i)
        dl->AddText(font, size, ImVec2(x, y + lh * i), lines[i].rfind("!!", 0) == 0 ? IM_COL32(255, 110, 110, 255) : IM_COL32(235, 235, 235, 255), lines[i].c_str());
}

}  // namespace
