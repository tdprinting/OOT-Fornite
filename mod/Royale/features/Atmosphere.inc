// ---- the storm wall and the weather, in the world ------------------------------------------------------------------------------
// Where the safe zone's edge stands this frame, set by DriveStorm from the match state and drawn as a wall by DrawStormWall.
royale::Circle gWallZone;
float gWallAlpha = 0.0f;    // fades in when a match starts and out when it ends
float gWallBase = 0.0f;     // roughly where the ground is (the wall reaches well below and far above it)
bool gWallBaseKnown = false;

bool ScreenCovered();   // a menu or pause screen is over the game (defined with the screen drawing)

// Once per game tick: are you standing in the storm (alive, outside the safe zone), and where is the wall?
void DriveStorm(const royale::HudState& h) {
    const bool live = gSession.Joined() && InField() && (h.state == royale::MatchState::Drop || h.state == royale::MatchState::InMatch);
    const bool in = live && h.haveSelf && h.selfAlive && !ScreenCovered() && h.safeZone.radius > 0 && h.stormDamagePerSecond > 0;
    gStormWeather = std::clamp(gStormWeather + (in ? 0.06f : -0.09f), 0.0f, 1.0f);   // about a second to come and go
    const bool wall = live && h.safeZone.radius > 0 && !(h.haveSelf && !h.selfAlive) && !ScreenCovered();
    if (wall) gWallZone = h.safeZone;
    gWallAlpha = std::clamp(gWallAlpha + (wall ? 0.05f : -0.05f), 0.0f, 1.0f);
    if (!InField() || gPlayState == nullptr) { gWallBaseKnown = false; return; }
    Player* pl = GET_PLAYER(gPlayState);
    const float ground = pl->actor.floorHeight > BGCHECK_Y_MIN + 1.0f ? pl->actor.floorHeight : pl->actor.world.pos.y;
    gWallBase = gWallBaseKnown ? gWallBase + (ground - gWallBase) * 0.05f : ground;   // follows slowly, so it never jumps when you jump
    gWallBaseKnown = true;
}

// The edge of the safe zone as a wall of violet storm light all the way round, a little see-through, bright at the foot and fading out high
// up, with bands of light rolling along it. It is drawn without the game's distance fog, so it shows from anywhere on the map (out to the
// game's own draw distance), and the hills and buildings in front of it hide it as they should.
void DrawStormWall(PlayState* play) {
    static const char drawKey = 0;
    DrawIdentity identity(&drawKey, 0, 0);
    if (gWallAlpha <= 0.0f || gWallZone.radius <= 0.0f || !gWallBaseKnown) return;
    const float r = gWallZone.radius;
    const float shrink = std::max(1.0f, r / 30000.0f);   // vertex positions are 16 bit: scale them down for a huge circle
    const int segs = std::clamp(static_cast<int>(r / 120.0f), 48, 160);
    const float t = static_cast<float>(ImGui::GetTime());
    constexpr int kRows = 4;
    const float heights[kRows] = { gWallBase - 3000.0f, gWallBase + 250.0f, gWallBase + 2600.0f, gWallBase + 7500.0f };
    const float alphas[kRows] = { 175.0f, 160.0f, 110.0f, 0.0f };
    Vtx* v = static_cast<Vtx*>(FrameAlloc(play, static_cast<size_t>(segs) * 2 * kRows * sizeof(Vtx)));
    if (v == nullptr) return;
    auto put = [&](Vtx& o, float ang, int row) {
        const float c = std::cos(ang), s = std::sin(ang);
        // Rolling bands of brighter light, running round the wall and drifting upward.
        const float band = 0.5f + 0.5f * std::sin(ang * 23.0f + t * 0.9f + row * 1.3f) * std::sin(ang * 7.0f - t * 0.55f + heights[row] * 0.0007f + t * 0.4f);
        const float k = 0.72f + 0.38f * band;
        o.v.ob[0] = static_cast<s16>(std::lround(c * r / shrink));
        o.v.ob[1] = static_cast<s16>(std::clamp(std::lround(heights[row] / shrink), -32000L, 32000L));
        o.v.ob[2] = static_cast<s16>(std::lround(s * r / shrink));
        o.v.flag = 0;
        o.v.tc[0] = o.v.tc[1] = 0;
        o.v.cn[0] = static_cast<u8>(std::min(255.0f, 150.0f * k + 20.0f * (row == 0)));
        o.v.cn[1] = static_cast<u8>(std::min(255.0f, 70.0f * k));
        o.v.cn[2] = static_cast<u8>(std::min(255.0f, 235.0f * k));
        o.v.cn[3] = static_cast<u8>(std::clamp(alphas[row] * (0.8f + 0.3f * band), 0.0f, 255.0f));
    };
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_XLU_DISP++, G_LIGHTING | G_CULL_BACK | G_CULL_FRONT | G_FOG);   // seen from either side, and not lost in the fog
    gDPSetRenderMode(POLY_XLU_DISP++, G_RM_PASS, G_RM_ZB_XLU_SURF2);
    gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, SHADE, SHADE, 0, PRIMITIVE, 0, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED);   // vertex colour, vertex alpha x our fade
    gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 255, 255, static_cast<u8>(255.0f * gWallAlpha));
    Matrix_Translate(gWallZone.center.x, 0.0f, gWallZone.center.z, MTXMODE_NEW);
    if (shrink > 1.0f) Matrix_Scale(shrink, shrink, shrink, MTXMODE_APPLY);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    for (int i = 0; i < segs; i++) {   // one strip of the wall: two columns of four, three quads high
        const float a0 = 6.2831853f * static_cast<float>(i) / segs, a1 = 6.2831853f * static_cast<float>(i + 1) / segs;
        Vtx* q = &v[i * 2 * kRows];
        for (int row = 0; row < kRows; row++) { put(q[row], a0, row); put(q[kRows + row], a1, row); }
        gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(q), 2 * kRows, 0);
        for (int row = 0; row + 1 < kRows; row++)
            gSP2Triangles(POLY_XLU_DISP++, row, kRows + row, kRows + row + 1, 0, row, kRows + row + 1, row + 1, 0);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// Ash and blowing sand: specks drifting through the air all around the camera, in the world (they pass in front of and behind things, and
// you fly through them), carried by the wind. Embers glow and rise; sand streaks along the wind. "Weather density" sets how many (0: none).
void DrawWeatherParticles(PlayState* play) {
    static const char drawKey = 0;
    const WorldAnchor anchor(&drawKey, play->view.eye);
    const royale::Sky sky = gWeatherShown.sky;
    if (sky != royale::Sky::Ash && sky != royale::Sky::Sandstorm) return;
    const float amount = WeatherAmount() * gWeatherDensity;
    if (amount <= 0.02f) return;
    const bool ash = sky == royale::Sky::Ash;
    const int n = std::min(ash ? 220 : 300, static_cast<int>((ash ? 150.0f : 220.0f) * amount));
    if (n <= 0) return;
    const Vec3f eye = play->view.eye;
    const float t = static_cast<float>(ImGui::GetTime());
    float wx, wz, wind;
    WindNow(&wx, &wz, &wind);
    const float wl = std::max(1.0f, std::hypot(wx, wz)), dx = wx / wl, dz = wz / wl;
    const float speed = ash ? 60.0f + 0.25f * wl : 380.0f + 0.9f * wl;
    constexpr float kBox = 1400.0f, kHalf = kBox * 0.5f, kTall = 900.0f;
    auto wrap = [](float x, float size) { x = std::fmod(x, size); return x < 0.0f ? x + size : x; };
    Vtx* v = static_cast<Vtx*>(FrameAlloc(play, static_cast<size_t>(n) * 8 * sizeof(Vtx)));
    if (v == nullptr) return;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_XLU_DISP++, G_LIGHTING | G_CULL_BACK | G_CULL_FRONT | G_FOG);
    gDPSetRenderMode(POLY_XLU_DISP++, G_RM_PASS, G_RM_ZB_XLU_SURF2);
    gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED);
    // The specks are placed round the camera, and drawn from the anchor near it (so they hold still in the world between frames).
    const Vec3f origin = anchor.Origin();
    const float ox = eye.x - origin.x, oy = eye.y - origin.y, oz = eye.z - origin.z;
    Matrix_Translate(origin.x, origin.y, origin.z, MTXMODE_NEW);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    for (int i = 0; i < n; i++) {
        const float h1 = Flora01(i, 7, 201), h2 = Flora01(i, 11, 202), h3 = Flora01(i, 13, 203), depth = 0.6f + 0.8f * Flora01(i, 17, 204);
        // Each speck drifts along the wind (and up or down) through a box that moves with the camera, wrapping round its sides.
        const float drift = t * speed * depth;
        const float rise = ash ? t * (i % 3 == 0 ? 45.0f : -25.0f) * depth : std::sin(t * 2.0f + i) * 20.0f;
        float x = wrap(h1 * kBox - eye.x + dx * drift, kBox) - kHalf;
        float z = wrap(h2 * kBox - eye.z + dz * drift, kBox) - kHalf;
        float y = wrap(h3 * kTall - eye.y + rise, kTall) - kTall * 0.5f;
        if (ash) { x += std::sin(t * 0.8f + i) * 25.0f; z += std::cos(t * 0.7f + i * 1.7f) * 25.0f; }
        x += ox; y += oy; z += oz;
        // Two crossed quads, so the speck looks the same from any side: a small square for ash, a thin streak along the wind for sand.
        float ax, ay, az, bx, by, bz, cx, cy, cz;
        if (ash) {
            const float s = (i % 3 == 0 ? 3.0f : 4.5f) * depth;
            ax = 0.0f; ay = s; az = 0.0f; bx = s; by = 0.0f; bz = 0.0f; cx = 0.0f; cy = 0.0f; cz = s;
        } else {
            const float len = 26.0f * depth, th = 1.6f * depth;
            ax = dx * len; ay = 0.0f; az = dz * len; bx = 0.0f; by = th; bz = 0.0f; cx = -dz * th; cy = 0.0f; cz = dx * th;
        }
        u8 r, g, b, a;
        if (ash && i % 3 == 0) { const float glow = 0.75f + 0.25f * std::sin(t * 6.0f + i); r = 255; g = static_cast<u8>(140 * glow); b = 40; a = 235; }   // a glowing ember
        else if (ash) { r = 120; g = 112; b = 108; a = 200; }
        else { r = 232; g = 196; b = 132; a = static_cast<u8>(150 + 60 * h3); }
        Vtx* q = &v[i * 8];
        const float corners[8][3] = { {x - ax - bx, y - ay - by, z - az - bz}, {x + ax - bx, y + ay - by, z + az - bz}, {x + ax + bx, y + ay + by, z + az + bz}, {x - ax + bx, y - ay + by, z - az + bz},
                                      {x - ax - cx, y - ay - cy, z - az - cz}, {x + ax - cx, y + ay - cy, z + az - cz}, {x + ax + cx, y + ay + cy, z + az + cz}, {x - ax + cx, y - ay + cy, z - az + cz} };
        for (int k = 0; k < 8; k++) {
            q[k].v.ob[0] = static_cast<s16>(std::lround(corners[k][0]));
            q[k].v.ob[1] = static_cast<s16>(std::lround(corners[k][1]));
            q[k].v.ob[2] = static_cast<s16>(std::lround(corners[k][2]));
            q[k].v.flag = 0; q[k].v.tc[0] = q[k].v.tc[1] = 0;
            q[k].v.cn[0] = r; q[k].v.cn[1] = g; q[k].v.cn[2] = b; q[k].v.cn[3] = a;
        }
    }
    for (int first = 0; first < n; first += 4) {   // four specks (32 vertices) at a time
        const int count = std::min(4, n - first);
        gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(&v[first * 8]), count * 8, 0);
        for (int k = 0; k < count; k++) {
            const int o = k * 8;
            gSP2Triangles(POLY_XLU_DISP++, o, o + 1, o + 2, 0, o, o + 2, o + 3, 0);
            gSP2Triangles(POLY_XLU_DISP++, o + 4, o + 5, o + 6, 0, o + 4, o + 6, o + 7, 0);
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);
}


// ---- the sky and the fog ------------------------------------------------------------------------------------------------------
// A gradient sky dome (night, dawn, day and dusk colours, greyed by the weather), a field of stars that fades in at dusk and out at dawn, a sun
// and a moon, drifting clouds, and low banks of fog. Nothing here is real volume rendering (that is far too heavy for a phone): a cloud or a
// fog bank is a stack of soft flat discs, brighter on top and darker underneath, whose edges fade out; where the discs overlap they build up
// into something that reads as a lit, thick body. All of it is drawn as translucent triangles round the camera and is switched on and off
// with the "Sky" and "Fog" Debug switches. The game's own distance fog (DriveRealWeather) still does the close-in thickening.
struct SkyLight {
    float sunH;       // the sine of the sun's height: 1 at noon, 0 at dawn and dusk, -1 at midnight
    float day;        // 0 night to 1 day
    float night;      // 1 - day, but with a longer dark
    float twilight;   // 1 at the moment of sunrise or sunset
    bool morning;
};
SkyLight SkyLightNow() {
    SkyLight l;
    const float d = static_cast<float>(gSaveContext.dayTime) / 65536.0f;   // 0 midnight, 0.5 noon
    l.sunH = -std::cos(d * 6.2831853f);
    auto smooth = [](float a, float b, float x) { const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); };
    l.day = smooth(-0.12f, 0.35f, l.sunH);
    l.night = smooth(0.12f, -0.3f, l.sunH);
    l.twilight = std::clamp(1.0f - std::fabs(l.sunH) / 0.3f, 0.0f, 1.0f);
    l.morning = d < 0.5f;
    return l;
}

// How overcast the sky is, 0 to 1, from the weather and the storm; and the colour the overcast pulls it to.
float SkyOvercast(float tint[3]) {
    const float w = WeatherAmount();
    float c = 0.0f;
    tint[0] = 120.0f; tint[1] = 126.0f; tint[2] = 138.0f;
    switch (gWeatherShown.sky) {
        case royale::Sky::Rain: c = 0.8f * w; break;
        case royale::Sky::Thunder: c = 1.0f * w; tint[0] = 82; tint[1] = 88; tint[2] = 108; break;
        case royale::Sky::Fog: c = 0.6f * w; tint[0] = 170; tint[1] = 176; tint[2] = 184; break;
        case royale::Sky::Snow: c = 0.75f * w; tint[0] = 176; tint[1] = 184; tint[2] = 198; break;
        case royale::Sky::Ash: c = 0.95f * w; tint[0] = 74; tint[1] = 52; tint[2] = 46; break;
        case royale::Sky::Sandstorm: c = 0.8f * w; tint[0] = 196; tint[1] = 156; tint[2] = 100; break;
        default: break;
    }
    if (gStormWeather > c) { c = gStormWeather * 0.9f; tint[0] = 80; tint[1] = 52; tint[2] = 120; }
    return std::clamp(c, 0.0f, 1.0f);
}

void SetVtx(Vtx& o, float x, float y, float z, float r, float g, float b, float a) {
    o.v.ob[0] = static_cast<s16>(std::lround(x)); o.v.ob[1] = static_cast<s16>(std::lround(y)); o.v.ob[2] = static_cast<s16>(std::lround(z));
    o.v.flag = 0; o.v.tc[0] = o.v.tc[1] = 0;
    o.v.cn[0] = static_cast<u8>(std::clamp(r, 0.0f, 255.0f)); o.v.cn[1] = static_cast<u8>(std::clamp(g, 0.0f, 255.0f));
    o.v.cn[2] = static_cast<u8>(std::clamp(b, 0.0f, 255.0f)); o.v.cn[3] = static_cast<u8>(std::clamp(a, 0.0f, 255.0f));
}

// The state every sky and fog layer is drawn with: translucent, vertex colour and alpha, no lighting, no fog, no culling, centred on the camera.
bool SkyBegin(PlayState* play, const Vec3f* origin = nullptr) {
    Mtx* matrix = static_cast<Mtx*>(FrameAlloc(play, sizeof(Mtx)));
    if (matrix == nullptr) return false;
    const Vec3f eye = origin != nullptr ? *origin : play->view.eye;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_XLU_DISP++, G_LIGHTING | G_CULL_BACK | G_CULL_FRONT | G_FOG | G_ZBUFFER);
    if (gGfxBackdropOpen) gDPSetRenderMode(POLY_XLU_DISP++, G_RM_PASS, G_RM_XLU_SURF2);   // the sky pass: behind everything, no depth test (see DrawBackdrop)
    else {
        gSPSetGeometryMode(POLY_XLU_DISP++, G_ZBUFFER);
        gDPSetRenderMode(POLY_XLU_DISP++, G_RM_PASS, G_RM_ZB_XLU_SURF2);
    }
    gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED);
    Matrix_Translate(eye.x, eye.y, eye.z, MTXMODE_NEW);
    // This is a world/camera transform, independent of the dummy actor's transform.
    MtxF worldMatrix;
    Matrix_Get(&worldMatrix);
    gSPMatrix(POLY_XLU_DISP++, Matrix_MtxFToMtx(&worldMatrix, matrix), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    CLOSE_DISPS(play->state.gfxCtx);
    return true;
}

// Soft discs: a centre and an 8-point rim (9 vertices each). Three go in one vertex load.
struct DiscSet {
    Vtx* v = nullptr;
    int n = 0, cap = 0;
    bool Init(PlayState* play, int count) {
        cap = count; n = 0;
        v = static_cast<Vtx*>(FrameAlloc(play, static_cast<size_t>(std::max(1, count)) * 9 * sizeof(Vtx)));
        return v != nullptr;
    }
    // A lumpy, slightly stretched disc at (x,y,z), centre colour/alpha `c`, rim colour with alpha `ra`.
    void Add(float x, float y, float z, float r, const float c[4], const float rim[3], float ra, int seed) {
        if (v == nullptr || n >= cap) return;
        Vtx* q = &v[n * 9];
        SetVtx(q[0], x, y, z, c[0], c[1], c[2], c[3]);
        for (int k = 0; k < 8; k++) {
            const float a = 6.2831853f * k / 8.0f + Flora01(seed, 3, 701) * 1.5f;
            const float rr = r * (0.8f + 0.4f * Flora01(seed, k, 702));
            SetVtx(q[1 + k], x + std::cos(a) * rr * 1.15f, y, z + std::sin(a) * rr, rim[0], rim[1], rim[2], ra);
        }
        n++;
    }
    void Draw(PlayState* play) {
        if (v == nullptr || n == 0) return;
        OPEN_DISPS(play->state.gfxCtx);
        for (int first = 0; first < n; first += 3) {
            const int count = std::min(3, n - first);
            if (!GfxHasRoom(play, 1 + 8 * count)) break;
            gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(&v[first * 9]), count * 9, 0);
            for (int d = 0; d < count; d++) {
                const int o = d * 9;
                for (int k = 0; k < 8; k++) gSP1Triangle(POLY_XLU_DISP++, o, o + 1 + k, o + 1 + (k + 1) % 8, 0);
            }
        }
        CLOSE_DISPS(play->state.gfxCtx);
    }
};

// A flat quad facing the camera's centre, in the direction (dx,dy,dz) at distance R, `size` wide.
void SkyQuad(Vtx* q, float dx, float dy, float dz, float R, float size, float r, float g, float b, float a) {
    const float len = std::max(0.001f, std::sqrt(dx * dx + dy * dy + dz * dz));
    dx /= len; dy /= len; dz /= len;
    float ux = -dz, uz = dx;                                           // a horizontal axis across the view
    const float ul = std::max(0.001f, std::hypot(ux, uz));
    ux /= ul; uz /= ul;
    const float h = std::max(0.001f, std::hypot(dx, dz));
    const float ax = ux * size, az = uz * size;
    const float bx = -dy * uz * size, by = h * size, bz = dy * ux * size;   // up the sky, across the direction and the first axis
    const float cx = dx * R, cy = dy * R, cz = dz * R;
    SetVtx(q[0], cx - ax - bx, cy - by, cz - az - bz, r, g, b, a);
    SetVtx(q[1], cx + ax - bx, cy - by, cz + az - bz, r, g, b, a);
    SetVtx(q[2], cx + ax + bx, cy + by, cz + az + bz, r, g, b, a);
    SetVtx(q[3], cx - ax + bx, cy + by, cz - az + bz, r, g, b, a);
}


// ---- the Ocarina of Time sky ---------------------------------------------------------------------------------------------------
// The pieces (dome, hills, clouds, sun, moon, stars) are modelled and painted in Blender (tools/scenery/build_sky.py, shared/sky_model.h), each at most 32
// corners. A batch puts many copies of them in one buffer, each with its own place, size, turn, colour and fade, and draws them in the order they were added.
struct SkyBatch {
    struct Item { const royale::sky_model::Mesh* m; int first; };
    Vtx* v = nullptr;
    int n = 0, cap = 0, count = 0;
    Item items[512];
    bool Init(PlayState* play, int maxVerts) {
        cap = maxVerts; n = 0; count = 0;
        v = static_cast<Vtx*>(FrameAlloc(play, static_cast<size_t>(maxVerts) * sizeof(Vtx)));
        return v != nullptr;
    }
    // `o` is where the model's origin goes; a, b, c are the unit directions of the model's x, y and z axes; `size` is the size of one model unit.
    // tint is 0..1 per channel, fade multiplies the painted alpha, `turn` spins the model about its z axis (radians).
    void Add(const royale::sky_model::Mesh& m, const float o[3], const float a[3], const float b[3], const float c[3], float size, const float tint[3], float fade, float turn = 0.0f) {
        if (v == nullptr || fade <= 0.004f || n + m.verts > cap || count >= 512) return;
        const float k = m.scale * size, cs = std::cos(turn), sn = std::sin(turn);
        for (int i = 0; i < m.verts; i++) {
            const float x0 = m.pos[i * 3] * k, y0 = m.pos[i * 3 + 1] * k, z0 = m.pos[i * 3 + 2] * k;
            const float x = x0 * cs - y0 * sn, y = x0 * sn + y0 * cs;
            SetVtx(v[n + i], o[0] + a[0] * x + b[0] * y + c[0] * z0, o[1] + a[1] * x + b[1] * y + c[1] * z0, o[2] + a[2] * x + b[2] * y + c[2] * z0,
                   m.col[i * 4] * tint[0], m.col[i * 4 + 1] * tint[1], m.col[i * 4 + 2] * tint[2], m.col[i * 4 + 3] * fade);
        }
        items[count++] = { &m, n };
        n += m.verts;
    }
    // A flat model that faces the camera's centre, looking out along `dir`: its x is across the sky, y up the sky, z toward the viewer.
    void AddBillboard(const royale::sky_model::Mesh& m, const float dir[3], float R, float size, const float tint[3], float fade, float turn = 0.0f) {
        const float len = std::max(0.001f, std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]));
        const float dx = dir[0] / len, dy = dir[1] / len, dz = dir[2] / len;
        const float h = std::max(0.001f, std::hypot(dx, dz));
        const float a[3] = { -dz / h, 0.0f, dx / h }, b[3] = { -dy * dx / h, h, -dy * dz / h }, c[3] = { -dx, -dy, -dz };
        const float o[3] = { dx * R, dy * R, dz * R };
        Add(m, o, a, b, c, size, tint, fade, turn);
    }
    void Draw(PlayState* play) {
        if (v == nullptr || count == 0) return;
        OPEN_DISPS(play->state.gfxCtx);
        int i = 0;
        while (i < count) {
            int j = i, verts = 0;
            while (j < count && verts + items[j].m->verts <= 32) { verts += items[j].m->verts; j++; }
            if (j == i) { i++; continue; }
            if (!GfxHasRoom(play, 160)) break;
            gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(&v[items[i].first]), verts, 0);
            int base = 0;
            for (int q = i; q < j; q++) {
                const royale::sky_model::Mesh& m = *items[q].m;
                for (int t = 0; t < m.tris; t++) gSP1Triangle(POLY_XLU_DISP++, base + m.idx[t * 3], base + m.idx[t * 3 + 1], base + m.idx[t * 3 + 2], 0);
                base += m.verts;
            }
            i = j;
        }
        CLOSE_DISPS(play->state.gfxCtx);
    }
};

// The sky with the Blender models. `zen` and `hor` are the zenith and horizon colours DrawSky worked out for the time of day and weather.
void DrawSkyOot(PlayState* play, const SkyLight& L, float ov, const float tintW[3], const float zen[3], const float hor[3]) {
    static const char drawKey = 0;
    DrawIdentity identity(&drawKey, 0, 0);
    using namespace royale::sky_model;
    constexpr float kR = 7000.0f;
    const float t = static_cast<float>(ImGui::GetTime());
    auto mix = [](float a, float b, float k) { return a + (b - a) * k; };
    const float kTau = 6.2831853f;
    const float yAxis[3] = { 0, 1, 0 };
    if (!SkyBegin(play)) return;

    // The dome, from the modelled rings: horizon colour up to zenith colour, with the glow of sunrise and sunset on the sun's side.
    {
        Vtx* dv = static_cast<Vtx*>(FrameAlloc(play, static_cast<size_t>(sky_dome.verts) * sizeof(Vtx)));
        if (dv == nullptr) return;
        const float glow[3] = { 255, 128, 62 };
        for (int i = 0; i < sky_dome.verts; i++) {
            const float x = sky_dome.pos[i * 3] * sky_dome.scale, y = sky_dome.pos[i * 3 + 1] * sky_dome.scale, z = sky_dome.pos[i * 3 + 2] * sky_dome.scale;
            const float k = std::pow(std::clamp(y, 0.0f, 1.0f), 0.6f);
            const float a = std::atan2(z, x), toSun = 0.5f + 0.5f * std::cos(a - 0.6f);
            const float g = L.twilight * (0.35f + 0.65f * toSun) * (1.0f - k) * (1.0f - ov * 0.7f);
            float col[3];
            for (int ch = 0; ch < 3; ch++) col[ch] = mix(mix(hor[ch], zen[ch], k), glow[ch], g * 0.35f);
            SetVtx(dv[i], x * kR, y * kR, z * kR, col[0], col[1], col[2], y < 0.0f ? 255.0f : 232.0f);
        }
        OPEN_DISPS(play->state.gfxCtx);
        const int w = kDomeSegs + 1;
        for (int r = 0; r + 1 < kDomeRings; r++) {
            gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(&dv[r * w]), 2 * w, 0);
            for (int j = 0; j < kDomeSegs; j++) gSP2Triangles(POLY_XLU_DISP++, j, j + 1, w + j, 0, j + 1, w + j + 1, w + j, 0);
        }
        CLOSE_DISPS(play->state.gfxCtx);
    }

    SkyBatch sky;
    if (!sky.Init(play, 2200)) return;
    const float spin = static_cast<float>(gSaveContext.dayTime) / 65536.0f * kTau;
    const float white[3] = { 1, 1, 1 };

    // The Milky Way: a slanted ribbon painted in Blender (a bright core split by a dark dust lane, a cloudy halo and a bulge on one side, blue deepening to warm
    // white) with a dust of tiny stars where it is brightest. It turns slowly with the night and fades with cloud and storm.
    {
        const float mwA = L.night * (1.0f - ov) * (1.0f - 0.9f * std::min(1.0f, gStormWeather * 1.4f));
        if (mwA > 0.03f && gSkyStars) {
            const float yaw = spin * 0.5f + 0.8f, cy = std::cos(yaw), sy = std::sin(yaw), tc = std::cos(1.0f), ts = std::sin(1.0f);
            const float ba[3] = { cy, 0.0f, sy }, bb[3] = { -ts * sy, tc, ts * cy }, bc[3] = { -tc * sy, -ts, tc * cy }, zero[3] = { 0, 0, 0 };
            const float white[3] = { 1, 1, 1 };
            SkyBatch mw;
            if (mw.Init(play, 1400)) {
                for (int i = 0; i < kMwPieces; i++) {
                    const float ph = kTau * (i + 0.5f) / kMwPieces;   // the piece's middle on the band; skip pieces that are under the horizon
                    const float cyv = std::sin(ph) * tc * cy * 0.0f + std::sin(ph) * tc;
                    if (cyv < -0.2f) continue;
                    mw.Add(kMilkyWay[i], zero, ba, bb, bc, kR * 0.975f, white, mwA * 0.9f);
                }
                mw.Draw(play);
            }
            for (int i = 0; i < kDustPieces; i++) {
                const float ph = kTau * (i + 0.5f) / kDustPieces;
                if (std::sin(ph) * tc < -0.1f) continue;
                const float tw = 0.75f + 0.25f * std::sin(t * kTau / kStarTwinkle + i * 1.7f);
                sky.Add(kMwDust[i], zero, ba, bb, bc, kR * 0.972f, white, mwA * tw);
            }
        }
    }

    // Stars: the two modelled kinds, turning slowly through the night and twinkling on the loop built in Blender; they come in as the sun sets, go out as it
    // rises, and hide behind cloud and the storm.
    const float starA = L.night * (1.0f - ov) * (1.0f - 0.8f * std::min(1.0f, gStormWeather * 1.4f));
    if (starA > 0.03f && gSkyStars) {
        constexpr int kStars = 150;
        for (int i = 0; i < kStars; i++) {
            const float a = Flora01(i, 5, 711) * kTau + spin * 0.5f, s = 0.04f + 0.96f * std::pow(Flora01(i, 9, 712), 0.8f);
            const float c = std::sqrt(1.0f - s * s), big = Flora01(i, 13, 713);
            const float tw = 0.7f + 0.3f * std::sin(t * kTau / kStarTwinkle * (0.6f + 0.8f * Flora01(i, 17, 714)) + i * 2.3f);
            const float fade = std::min(1.0f, s * 6.0f);
            const float warm = Flora01(i, 21, 715);
            const float tint[3] = { 1.0f, 1.0f - 0.08f * warm, 1.0f - 0.35f * warm };
            const float dir[3] = { std::cos(a) * c, s, std::sin(a) * c };
            const bool sparkle = big > 0.72f;
            sky.AddBillboard(sparkle ? star_sparkle : star_dot, dir, kR * 0.97f, sparkle ? (44.0f + 54.0f * big) * (0.8f + 0.2f * tw) : (22.0f + 22.0f * big) * (0.85f + 0.15f * tw),
                             tint, starA * tw * fade * (0.5f + 0.5f * big), sparkle ? 0.0f : 0.785f);
        }
    }

    // The sun and the moon, opposite each other, behind thick cloud. The sun's rays turn and its halo breathes on the loops from the Blender file.
    {
        const float sunA = std::clamp(L.sunH * 6.0f + 0.4f, 0.0f, 1.0f) * (1.0f - ov), moonA = std::clamp(-L.sunH * 6.0f + 0.4f, 0.0f, 1.0f) * (1.0f - ov);
        const float c = std::sqrt(std::max(0.0f, 1.0f - L.sunH * L.sunH));
        if (gSkyBodies) {
            if (sunA > 0.02f) {
                const float dir[3] = { std::cos(0.6f) * c, L.sunH, std::sin(0.6f) * c };
                const float warm = L.twilight;
                const float tint[3] = { 1.0f, mix(1.0f, 0.72f, warm), mix(1.0f, 0.5f, warm) };
                const float breath = 1.0f + 0.06f * (1.0f - std::cos(t * kTau / kSunRayBreath));
                const float turn = 0.7853982f * std::fmod(t, kSunRayTurn) / kSunRayTurn;
                sky.AddBillboard(sun_halo, dir, kR * 0.955f, 420.0f * breath, tint, sunA * (1.0f - 0.5f * ov));
                sky.AddBillboard(sun_rays_long, dir, kR * 0.956f, 380.0f, tint, sunA * (0.55f + 0.2f * warm), turn);
                sky.AddBillboard(sun_rays_short, dir, kR * 0.957f, 380.0f, tint, sunA * (0.55f + 0.2f * warm), -turn);
                sky.AddBillboard(sun_core, dir, kR * 0.958f, 400.0f, tint, sunA);
            }
            if (moonA > 0.02f) {
                const float dir[3] = { -std::cos(0.6f) * c, -L.sunH, -std::sin(0.6f) * c };
                const float pulse = 1.0f + 0.05f * (1.0f - std::cos(t * kTau / kMoonPulse));
                sky.AddBillboard(moon_halo, dir, kR * 0.955f, 320.0f * pulse, white, moonA);
                sky.AddBillboard(moon_disc, dir, kR * 0.958f, 320.0f, white, moonA);
                sky.AddBillboard(moon_craters, dir, kR * 0.959f, 320.0f, white, moonA);
            }
        }
    }

    // Distant hills ring the horizon. Keep them low and softly lit so the camera-centred skybox reads as a far ridge,
    // rather than isolated dark peaks floating over the landscape during the glider drop.
    {
        const float lit = 0.55f + 0.75f * L.day, tw = L.twilight;
        const float tint[3] = { lit * (1.0f - 0.5f * ov) * (1.0f + 0.35f * tw), lit * (1.0f - 0.5f * ov) * (1.0f - 0.05f * tw), lit * (1.0f - 0.3f * ov) * (1.0f - 0.2f * tw) };
        constexpr int kHillCount = 22;
        constexpr float sizeScale = 0.55f;
        for (int i = 0; i < kHillCount; i++) {
            const float a = kTau * i / kHillCount;
            const float ca = std::cos(a), sa = std::sin(a);
            const float size = kR * (0.085f + 0.065f * Flora01(i, 5, 742)) * sizeScale;
            const float o[3] = { ca * kR * 0.93f, -kR * 0.001f, sa * kR * 0.93f };
            const float ax[3] = { -sa, 0.0f, ca }, cz[3] = { -ca, 0.0f, -sa };
            sky.Add(kHills[i % 3], o, ax, yAxis, cz, size, tint, 1.0f);
        }
    }
    sky.Draw(play);

    // Clouds: the four modelled loaves, drifting in world space with the wind, more of them and greyer the more overcast
    // it is; warm at sunrise and sunset, dim blue at night.
    if (gSkyClouds <= 0.01f) return;
    float wx, wz, wind;
    WindNow(&wx, &wz, &wind);
    const float wl = std::max(1.0f, std::hypot(wx, wz));
    const float cover = std::clamp(0.3f + 0.7f * ov, 0.0f, 1.0f);
    const int puffs = std::min(30, static_cast<int>((8.0f + 22.0f * cover) * std::min(1.5f, std::max(0.4f, gWeatherDensity)) * gSkyClouds));
    const Vec3f cloudEye = play->view.eye;
    const Vec3f cloudOrigin = {std::floor(cloudEye.x / 12000.0f) * 12000.0f, 0.0f, std::floor(cloudEye.z / 12000.0f) * 12000.0f};
    DrawIdentity cloudIdentity(&kClouds, static_cast<int>(cloudOrigin.x), static_cast<int>(cloudOrigin.z));
    if (!SkyBegin(play, &cloudOrigin)) return;
    SkyBatch cl;
    if (!cl.Init(play, 1100)) return;
    float lit[3];
    for (int ch = 0; ch < 3; ch++) {
        const float dayc = mix(250.0f, tintW[ch], ov * 0.8f);
        float c = mix(40.0f + ch * 6.0f, dayc, L.day);
        c = mix(c, ch == 0 ? 255.0f : (ch == 1 ? 150.0f : 120.0f), L.twilight * 0.55f * (1.0f - ov * 0.6f));
        lit[ch] = std::clamp(c / 255.0f, 0.0f, 1.0f);
    }
    for (int i = 0; i < puffs; i++) {
        const float h1 = Flora01(i, 7, 751), h2 = Flora01(i, 11, 752), h3 = Flora01(i, 13, 753), h4 = Flora01(i, 17, 754);
        const Vec3f eye = cloudEye;
        const float drift = t * (18.0f + 0.25f * wl) * (0.7f + 0.6f * h4);
        const auto cloud = royale::graphics::CloudPosition(h1, h2, h3, eye.x, eye.y, eye.z, wx / wl * drift, wz / wl * drift);
        const float a = h4 * kTau, ca = std::cos(a), sa = std::sin(a);
        const float size = (420.0f + 380.0f * h3) * (1.0f + 0.5f * ov);
        const float o[3] = { cloud.x + eye.x - cloudOrigin.x, cloud.y + eye.y, cloud.z + eye.z - cloudOrigin.z };
        // World orientation and height stay fixed as the camera moves or turns.
        const float ax[3] = { -sa, 0.0f, ca }, cz[3] = { -ca, 0.0f, -sa };
        cl.Add(kClouds[i % 4], o, ax, yAxis, cz, size, lit, (0.55f + 0.45f * ov) * cloud.fade);
    }
    cl.Draw(play);
}

void DrawSky(PlayState* play) {
    static const char drawKey = 0;
    DrawIdentity identity(&drawKey, 0, 0);
    if (!InField()) return;
    constexpr float kR = 7000.0f;
    const SkyLight L = SkyLightNow();
    float tint[3];
    const float ov = SkyOvercast(tint);
    const float t = static_cast<float>(ImGui::GetTime());
    auto mix = [](float a, float b, float k) { return a + (b - a) * k; };
    // Palette: night, day, then the glow of sunrise and sunset on the horizon, then the weather greys it all.
    float zen[3], hor[3];
    const float nz[3] = { 5, 8, 30 }, nh[3] = { 22, 28, 66 }, dz[3] = { 62, 122, 224 }, dh[3] = { 168, 206, 242 };
    const float glow[3] = { 255, 128, 62 }, glowZ[3] = { 96, 78, 150 };
    const float dim = 1.0f - 0.55f * ov * L.day;
    for (int i = 0; i < 3; i++) {
        zen[i] = mix(nz[i], dz[i], L.day); hor[i] = mix(nh[i], dh[i], L.day);
        zen[i] = mix(zen[i], glowZ[i], L.twilight * 0.55f); hor[i] = mix(hor[i], glow[i], L.twilight * 0.8f);
        const float grey = tint[i] * (0.25f + 0.75f * L.day);
        zen[i] = mix(zen[i], grey * 0.8f, ov * 0.85f) * dim; hor[i] = mix(hor[i], grey, ov * 0.85f) * dim;
    }
    if (gSkyOot) { DrawSkyOot(play, L, ov, tint, zen, hor); return; }
    if (!SkyBegin(play)) return;
    // The dome: rings from just below the horizon up to the zenith, blended from the horizon colour to the zenith colour.
    constexpr int kSegs = 14, kRings = 8;
    static const float kElev[kRings] = { -0.12f, 0.0f, 0.07f, 0.16f, 0.30f, 0.52f, 0.78f, 1.0f };   // the sine of each ring's height
    Vtx* dv = static_cast<Vtx*>(FrameAlloc(play, static_cast<size_t>(kRings) * (kSegs + 1) * sizeof(Vtx)));
    if (dv == nullptr) return;
    const float spin = static_cast<float>(gSaveContext.dayTime) / 65536.0f * 6.2831853f;
    for (int r = 0; r < kRings; r++) {
        const float s = kElev[r], c = std::sqrt(std::max(0.0f, 1.0f - s * s));
        const float k = std::pow(std::clamp(s, 0.0f, 1.0f), 0.6f);
        for (int j = 0; j <= kSegs; j++) {
            const float a = 6.2831853f * j / kSegs;
            // the glow sits toward the sun's side of the horizon
            const float toSun = 0.5f + 0.5f * std::cos(a - 0.6f);
            const float g = L.twilight * (0.35f + 0.65f * toSun) * (1.0f - k) * (1.0f - ov * 0.7f);
            float col[3];
            for (int i = 0; i < 3; i++) col[i] = mix(mix(hor[i], zen[i], k), glow[i], g * 0.35f);
            SetVtx(dv[r * (kSegs + 1) + j], std::cos(a) * c * kR, s * kR, std::sin(a) * c * kR, col[0], col[1], col[2], r == 0 ? 255.0f : 232.0f);
        }
    }
    OPEN_DISPS(play->state.gfxCtx);
    for (int r = 0; r + 1 < kRings; r++) {
        gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(&dv[r * (kSegs + 1)]), 2 * (kSegs + 1), 0);
        for (int j = 0; j < kSegs; j++) gSP2Triangles(POLY_XLU_DISP++, j, j + 1, kSegs + 1 + j, 0, j + 1, kSegs + 2 + j, kSegs + 1 + j, 0);
    }
    CLOSE_DISPS(play->state.gfxCtx);

    // Stars: a field that turns slowly through the night, twinkling, fading in as the sun sets and out as it rises (and behind cloud).
    const float starA = L.night * (1.0f - ov) * (1.0f - 0.8f * std::min(1.0f, gStormWeather * 1.4f));
    if (starA > 0.03f && gSkyStars) {
        constexpr int kStars = 220;
        Vtx* sv = static_cast<Vtx*>(FrameAlloc(play, static_cast<size_t>(kStars) * 4 * sizeof(Vtx)));
        if (sv != nullptr) {
            for (int i = 0; i < kStars; i++) {
                const float a = Flora01(i, 5, 711) * 6.2831853f + spin * 0.5f, s = 0.04f + 0.96f * std::pow(Flora01(i, 9, 712), 0.8f);
                const float c = std::sqrt(1.0f - s * s), big = Flora01(i, 13, 713);
                const float tw = 0.65f + 0.35f * std::sin(t * (1.5f + 3.0f * Flora01(i, 17, 714)) + i * 2.3f);
                const float fade = std::min(1.0f, s * 6.0f);   // dim near the horizon
                const float warm = Flora01(i, 21, 715);        // blue-white to yellow
                const float size = (9.0f + 20.0f * big * big) * (0.9f + 0.1f * tw);
                SkyQuad(&sv[i * 4], std::cos(a) * c, s, std::sin(a) * c, kR * 0.97f, size, 205 + 50 * (1 - warm), 215 + 30 * (1 - warm), 255 - 70 * warm,
                        255.0f * starA * tw * fade * (0.45f + 0.55f * big));
            }
            OPEN_DISPS(play->state.gfxCtx);
            for (int first = 0; first < kStars; first += 8) {
                const int count = std::min(8, kStars - first);
                gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(&sv[first * 4]), count * 4, 0);
                for (int k = 0; k < count; k++) gSP2Triangles(POLY_XLU_DISP++, k * 4, k * 4 + 1, k * 4 + 2, 0, k * 4, k * 4 + 2, k * 4 + 3, 0);
            }
            CLOSE_DISPS(play->state.gfxCtx);
        }
    }

    // The sun and the moon, opposite each other, hidden by thick cloud.
    {
        const float sunA = std::clamp(L.sunH * 6.0f + 0.4f, 0.0f, 1.0f) * (1.0f - ov), moonA = std::clamp(-L.sunH * 6.0f + 0.4f, 0.0f, 1.0f) * (1.0f - ov);
        const float c = std::sqrt(std::max(0.0f, 1.0f - L.sunH * L.sunH));
        Vtx* qv = static_cast<Vtx*>(FrameAlloc(play, 8 * sizeof(Vtx)));
        if (gSkyBodies && qv != nullptr && (sunA > 0.02f || moonA > 0.02f)) {
            const float warm = L.twilight;
            SkyQuad(&qv[0], std::cos(0.6f) * c, L.sunH, std::sin(0.6f) * c, kR * 0.96f, 330.0f, 255, mix(244, 160, warm), mix(205, 90, warm), 255.0f * sunA);
            SkyQuad(&qv[4], -std::cos(0.6f) * c, -L.sunH, -std::sin(0.6f) * c, kR * 0.96f, 250.0f, 226, 232, 245, 245.0f * moonA);
            OPEN_DISPS(play->state.gfxCtx);
            gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(qv), 8, 0);
            gSP2Triangles(POLY_XLU_DISP++, 0, 1, 2, 0, 0, 2, 3, 0);
            gSP2Triangles(POLY_XLU_DISP++, 4, 5, 6, 0, 4, 6, 7, 0);
            CLOSE_DISPS(play->state.gfxCtx);
        }
    }

    // Clouds: puffs of stacked soft discs at fixed world heights, drifting with the wind. More of them, and darker,
    // the more overcast it is; lit warm at sunrise and sunset, dim blue at night.
    if (gSkyClouds <= 0.01f) return;
    const float cover = std::clamp(0.3f + 0.7f * ov, 0.0f, 1.0f);
    const int puffs = std::min(64, static_cast<int>((12.0f + 36.0f * cover) * std::min(1.5f, std::max(0.4f, gWeatherDensity)) * gSkyClouds));
    float wx, wz, wind;
    WindNow(&wx, &wz, &wind);
    const float wl = std::max(1.0f, std::hypot(wx, wz)), dxw = wx / wl, dzw = wz / wl;
    constexpr int kLayers = 5;
    static const float kProfile[kLayers] = { 0.62f, 1.0f, 0.92f, 0.66f, 0.34f }, kLift[kLayers] = { 0.0f, 0.14f, 0.28f, 0.42f, 0.56f };
    DiscSet cd;
    if (!cd.Init(play, puffs * kLayers)) return;
    const Vec3f eye = play->view.eye;
    const Vec3f cloudOrigin = {std::floor(eye.x / 12000.0f) * 12000.0f, 0.0f, std::floor(eye.z / 12000.0f) * 12000.0f};
    static const char cloudKey = 0;
    DrawIdentity cloudIdentity(&cloudKey, static_cast<int>(cloudOrigin.x), static_cast<int>(cloudOrigin.z));
    if (!SkyBegin(play, &cloudOrigin)) return;
    for (int i = 0; i < puffs; i++) {
        const float h1 = Flora01(i, 7, 721), h2 = Flora01(i, 11, 722), h3 = Flora01(i, 13, 723), h4 = Flora01(i, 17, 724);
        const float drift = t * (18.0f + 0.25f * wl) * (0.7f + 0.6f * h4);
        const auto cloud = royale::graphics::CloudPosition(h1, h2, h3, eye.x, eye.y, eye.z, dxw * drift, dzw * drift);
        const float x = cloud.x + eye.x - cloudOrigin.x, z = cloud.z + eye.z - cloudOrigin.z, edge = cloud.fade;
        if (edge <= 0.01f) continue;
        const float y = cloud.y + eye.y - 160.0f * ov;
        const float width = (650.0f + 800.0f * h4) * (1.0f + 0.5f * ov), thick = width * (0.34f + 0.2f * ov);
        for (int k = 0; k < kLayers; k++) {
            const float up = static_cast<float>(k) / (kLayers - 1);   // 0 underside, 1 top
            float lit[3], shade[3];
            for (int ch = 0; ch < 3; ch++) {
                const float dayc = mix(250.0f, tint[ch], ov * 0.8f);
                float c = mix(40.0f + ch * 6.0f, dayc, L.day);
                c = mix(c, ch == 0 ? 255.0f : (ch == 1 ? 150.0f : 120.0f), L.twilight * 0.55f * (1.0f - ov * 0.6f));
                lit[ch] = c; shade[ch] = c * (0.5f + 0.2f * (1.0f - ov));
            }
            float col[4], rim[3];
            for (int ch = 0; ch < 3; ch++) { col[ch] = mix(shade[ch], lit[ch], up); rim[ch] = col[ch]; }
            col[3] = (60.0f + 70.0f * (0.4f + 0.6f * ov)) * edge * (k == 0 ? 0.7f : 1.0f);
            cd.Add(x, y + kLift[k] * thick * 2.0f, z, width * kProfile[k], col, rim, 0.0f, i * 8 + k);
        }
    }
    cd.Draw(play);
}

// Fog banks: wide, low drifts of fog lying on the ground round the camera, thicker in fog, sandstorms, ash, rain and the storm, as a mist at dawn,
// and pooled low over water. The game's own distance fog (DriveRealWeather) hazes the far view; these give the near ground its depth.
void DrawFogBanks(PlayState* play) {
    static const char drawKey = 0;
    const WorldAnchor anchor(&drawKey, play->view.eye);
    if (!InField()) return;
    const SkyLight L = SkyLightNow();
    const float w = WeatherAmount();
    float dens = 0.0f, col[3] = { 214, 220, 228 };
    auto kind = [&](float a, float d, float r, float g, float b) { if (a * d > dens) { dens = a * d; col[0] = r; col[1] = g; col[2] = b; } };
    switch (gWeatherShown.sky) {
        case royale::Sky::Fog: kind(w, 1.0f, 214, 220, 228); break;
        case royale::Sky::Rain: kind(w, 0.35f, 150, 158, 170); break;
        case royale::Sky::Thunder: kind(w, 0.45f, 110, 116, 134); break;
        case royale::Sky::Snow: kind(w, 0.4f, 226, 232, 244); break;
        case royale::Sky::Ash: kind(w, 0.6f, 84, 66, 62); break;
        case royale::Sky::Sandstorm: kind(w, 0.75f, 214, 176, 118); break;
        default: break;
    }
    kind(gStormWeather, 0.7f, 100, 64, 150);
    const float mist = L.morning ? std::clamp(1.0f - std::fabs(L.sunH - 0.05f) / 0.3f, 0.0f, 1.0f) * (gWeatherShown.season == royale::Season::Autumn ? 0.5f : 0.28f) : 0.0f;
    if (mist > dens) { dens = mist; col[0] = 218; col[1] = 222; col[2] = 232; }
    const bool island = OnIsland();
    const bool lake = gMapId == 1;
    const float baseWater = (island ? 0.22f : (lake ? 0.14f : 0.0f)) * gFogAmount;   // sea mist and lake mist hang low all the time
    dens = std::min(1.0f, dens * std::min(1.5f, std::max(0.3f, gWeatherDensity)) * gFogAmount);
    if (dens + baseWater < 0.04f) return;
    const float light = 0.35f + 0.65f * L.day;   // fog is lit by the sky: dim at night
    const Vec3f eye = play->view.eye;
    const float t = static_cast<float>(ImGui::GetTime());
    float wx, wz, wind;
    WindNow(&wx, &wz, &wind);
    const float wl = std::max(1.0f, std::hypot(wx, wz)), dxw = wx / wl, dzw = wz / wl;
    const int banks = std::min(22, static_cast<int>(6.0f + 16.0f * std::min(1.0f, dens + baseWater)));
    constexpr int kLayers = 3;
    DiscSet fd;
    if (!fd.Init(play, banks * kLayers)) return;
    const Vec3f origin = anchor.Origin();
    if (!SkyBegin(play, &origin)) return;
    const float ox = eye.x - origin.x, oy = eye.y - origin.y, oz = eye.z - origin.z;   // the camera, seen from the anchor
    constexpr float kBox = 5200.0f;
    auto wrap = [](float x, float size) { x = std::fmod(x, size); return x < 0.0f ? x + size : x; };
    for (int i = 0; i < banks; i++) {
        const float h1 = Flora01(i, 7, 731), h2 = Flora01(i, 11, 732), h3 = Flora01(i, 13, 733);
        const float drift = t * (8.0f + 0.12f * wl) * (0.6f + 0.8f * h3);
        const float x = wrap(h1 * kBox - eye.x + dxw * drift, kBox) - kBox * 0.5f, z = wrap(h2 * kBox - eye.z + dzw * drift, kBox) - kBox * 0.5f;
        const float d = std::hypot(x, z);
        const float edge = std::clamp((2600.0f - d) / 1000.0f, 0.0f, 1.0f) * std::clamp((d - 150.0f) / 450.0f, 0.0f, 1.0f);
        if (edge <= 0.01f) continue;
        float gy = 0.0f;
        if (!RawFloorAt(eye.x + x, eye.z + z, &gy)) { if (!island) continue; gy = static_cast<float>(royale::fortnite::kWaterY); }
        float local = dens;
        if (island) {   // lower ground and the sea's edge hold more mist
            const float low = std::clamp(1.0f - (gy - static_cast<float>(royale::fortnite::kWaterY)) / 260.0f, 0.0f, 1.0f);
            local = std::min(1.0f, dens + baseWater * low);
            gy = std::max(gy, static_cast<float>(royale::fortnite::kWaterY));
        } else local = std::min(1.0f, dens + baseWater);
        if (local < 0.03f) continue;
        const float width = 520.0f + 520.0f * h3 + 300.0f * local;
        for (int k = 0; k < kLayers; k++) {
            const float up = static_cast<float>(k) / (kLayers - 1);
            float c[4] = { col[0] * light * (0.82f + 0.18f * up), col[1] * light * (0.82f + 0.18f * up), col[2] * light * (0.82f + 0.18f * up), (40.0f + 60.0f * local) * edge * (1.0f - 0.35f * up) };
            const float rim[3] = { c[0], c[1], c[2] };
            fd.Add(x + ox, gy - eye.y + oy + 14.0f + k * 55.0f * (0.6f + 0.6f * local), z + oz, width * (1.0f - 0.2f * up), c, rim, 0.0f, i * 4 + k);
        }
    }
    fd.Draw(play);
}

// Light effects tied to the sky: shafts of sun (gold at sunrise and sunset) or moon (cool blue) fanning out from the sun or moon, strongest in broken cloud, mist and
// at the low sun, gone in a storm or heavy overcast; and fireflies that glow and drift near the ground at dusk and through clear nights.
// `shafts`: the shafts (part of the sky pass); `flies`: the fireflies (in the world, near the player).
void DrawSkyLight(PlayState* play, bool shafts, bool flies) {
    static const char drawKey = 0;
    DrawIdentity identity(&drawKey, 0, 0);
    if (!InField() || !gSkyOot) return;
    const SkyLight L = SkyLightNow();
    float tint[3];
    const float ov = SkyOvercast(tint);
    const float t = static_cast<float>(ImGui::GetTime());
    const float kTau = 6.2831853f;
    constexpr float kR = 6900.0f;
    const bool sunUp = L.sunH > -0.02f;
    const float c = std::sqrt(std::max(0.0f, 1.0f - L.sunH * L.sunH));
    float amt = sunUp ? std::clamp(L.sunH * 5.0f + 0.3f, 0.0f, 1.0f) : std::clamp(-L.sunH * 5.0f, 0.0f, 1.0f) * 0.45f;
    const float broken = std::clamp(0.35f + ov * 1.4f, 0.0f, 1.0f) * (1.0f - std::clamp((ov - 0.55f) * 2.2f, 0.0f, 1.0f));
    amt *= broken * (1.0f + 0.9f * L.twilight) * (gWeatherShown.sky == royale::Sky::Fog ? 1.4f : 1.0f) * (1.0f - 0.9f * std::min(1.0f, gStormWeather * 1.4f));
    amt = std::min(1.0f, amt) * gFogAmount;
    if (shafts && amt > 0.03f) {
        float dx = std::cos(0.6f) * c, dy = L.sunH, dz = std::sin(0.6f) * c;
        if (!sunUp) { dx = -dx; dy = -dy; dz = -dz; }
        const float h = std::max(0.001f, std::hypot(dx, dz));
        const float ax = -dz / h, az = dx / h, bx = -dy * dx / h, by = h, bz = -dy * dz / h;
        const float col[3] = { sunUp ? 255.0f : 170.0f, sunUp ? 226.0f - 70.0f * L.twilight : 196.0f, sunUp ? 150.0f - 60.0f * L.twilight : 255.0f };
        constexpr int kShafts = 10;
        Vtx* v = static_cast<Vtx*>(FrameAlloc(play, static_cast<size_t>(kShafts) * 3 * sizeof(Vtx)));
        if (v == nullptr) return;
        for (int i = 0; i < kShafts; i++) {
            const float th = kTau * (i + 0.5f * Flora01(i, 3, 761)) / kShafts + 0.15f * std::sin(t * 0.12f + i);
            const float len = kR * (0.5f + 0.5f * Flora01(i, 5, 762)), w = kR * (0.025f + 0.04f * Flora01(i, 7, 763));
            const float pulse = 0.6f + 0.4f * std::sin(t * (0.25f + 0.2f * Flora01(i, 9, 764)) + i * 1.7f);
            const float ux = std::cos(th), uy = std::sin(th), vx = -uy, vy = ux;   // along the shaft and across it, in the sky plane
            auto P = [&](float x, float y, float* o) { o[0] = dx * kR * 0.95f + (ax * x + bx * y); o[1] = dy * kR * 0.95f + by * y; o[2] = dz * kR * 0.95f + (az * x + bz * y); };
            float p0[3], p1[3], p2[3];
            P(vx * w * 0.3f, vy * w * 0.3f, p0); P(-vx * w * 0.3f, -vy * w * 0.3f, p1); P(ux * len + vx * w, uy * len + vy * w, p2);
            const float a0 = 70.0f * amt * pulse;
            SetVtx(v[i * 3], p0[0], p0[1], p0[2], col[0], col[1], col[2], a0);
            SetVtx(v[i * 3 + 1], p1[0], p1[1], p1[2], col[0], col[1], col[2], a0);
            SetVtx(v[i * 3 + 2], p2[0], p2[1], p2[2], col[0], col[1], col[2], 0.0f);
        }
        if (!SkyBegin(play)) return;
        OPEN_DISPS(play->state.gfxCtx);
        gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(v), kShafts * 3, 0);
        for (int i = 0; i < kShafts; i++) gSP1Triangle(POLY_XLU_DISP++, i * 3, i * 3 + 1, i * 3 + 2, 0);
        CLOSE_DISPS(play->state.gfxCtx);
    }
    // Fireflies.
    if (!flies) return;
    const float ff = std::max(L.twilight * 0.8f, L.night) * (1.0f - std::min(1.0f, ov * 1.5f)) * (1.0f - std::min(1.0f, gStormWeather * 2.0f)) * gFogAmount;
    if (ff < 0.05f) return;
    constexpr int kFlies = 36;
    Vtx* q = static_cast<Vtx*>(FrameAlloc(play, static_cast<size_t>(kFlies) * 4 * sizeof(Vtx)));
    if (q == nullptr) return;
    for (int i = 0; i < kFlies; i++) {
        const float a = Flora01(i, 3, 771) * kTau + 0.1f * std::sin(t * 0.3f + i), d = 150.0f + 900.0f * Flora01(i, 5, 772);
        const float y = -30.0f + 150.0f * Flora01(i, 7, 773) + 18.0f * std::sin(t * 0.8f + i * 1.9f);
        const float x = std::cos(a) * d + 30.0f * std::sin(t * 0.5f + i), z = std::sin(a) * d + 30.0f * std::cos(t * 0.45f + i * 2.0f);
        const float blink = std::max(0.0f, std::sin(t * (1.1f + Flora01(i, 9, 774)) + i * 3.1f));
        SkyQuad(&q[i * 4], x, y, z, std::sqrt(x * x + y * y + z * z), 5.0f + 4.0f * blink, 220, 255, 120, 255.0f * ff * (0.25f + 0.75f * blink));
    }
    if (!SkyBegin(play)) return;
    OPEN_DISPS(play->state.gfxCtx);
    for (int first = 0; first < kFlies; first += 8) {
        const int n = std::min(8, kFlies - first);
        gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(&q[first * 4]), n * 4, 0);
        for (int k = 0; k < n; k++) gSP2Triangles(POLY_XLU_DISP++, k * 4, k * 4 + 1, k * 4 + 2, 0, k * 4, k * 4 + 2, k * 4 + 3, 0);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// ---- wind streaks and the tornado --------------------------------------------------------------------------------------------------
// Wind streaks: thin pale lines that drift through the air round the camera along the wind, longer, quicker and more of them as it picks up
// (so a storm's squalls show), which is how you see which way it blows. Ash and sand have their own specks, so those skies skip this.
void DrawWindParticles(PlayState* play) {
    static const char drawKey = 0;
    const WorldAnchor anchor(&drawKey, play->view.eye);
    if (!gWindStreaks || !gWindOn || !InField()) return;
    if (gWeatherShown.sky == royale::Sky::Ash || gWeatherShown.sky == royale::Sky::Sandstorm) return;
    float wx, wz, wind;
    WindNow(&wx, &wz, &wind);
    if (wind < 0.04f) return;
    const int n = std::min(80, static_cast<int>((14.0f + 70.0f * wind) * std::min(1.5f, gWeatherDensity)));
    if (n <= 0) return;
    const Vec3f eye = play->view.eye;
    const float t = static_cast<float>(ImGui::GetTime());
    const float wl = std::max(1.0f, std::hypot(wx, wz)), dx = wx / wl, dz = wz / wl;
    constexpr float kBox = 1500.0f, kHalf = kBox * 0.5f, kTall = 650.0f;
    auto wrap = [](float x, float size) { x = std::fmod(x, size); return x < 0.0f ? x + size : x; };
    const Vec3f origin = anchor.Origin();
    const float ox = eye.x - origin.x, oy = eye.y - origin.y, oz = eye.z - origin.z;
    Vtx* v = static_cast<Vtx*>(FrameAlloc(play, static_cast<size_t>(n) * 4 * sizeof(Vtx)));
    if (v == nullptr) return;
    for (int i = 0; i < n; i++) {
        const float h1 = Flora01(i, 31, 301), h2 = Flora01(i, 37, 302), h3 = Flora01(i, 41, 303), depth = 0.6f + 0.8f * Flora01(i, 43, 304);
        const float drift = t * wl * 1.5f * depth;   // streaks outrun the wind a little so the direction reads at a glance
        const float x = wrap(h1 * kBox - eye.x + dx * drift, kBox) - kHalf + ox;   // (from the anchor, see below)
        const float z = wrap(h2 * kBox - eye.z + dz * drift, kBox) - kHalf + oz;
        const float y = wrap(h3 * kTall + eye.y * 0.0f, kTall) - kTall * 0.35f + std::sin(t * 1.7f + i) * 14.0f + oy;
        const float len = (30.0f + 120.0f * wind) * depth, th = 1.8f * depth;
        const float fade = std::clamp(std::min(wrap(drift * 0.001f + h1, 1.0f), 1.0f - wrap(drift * 0.001f + h1, 1.0f)) * 5.0f, 0.0f, 1.0f);
        const u8 a = static_cast<u8>(std::clamp((55.0f + 110.0f * wind) * fade * (0.6f + 0.4f * depth), 0.0f, 220.0f));
        // A flat ribbon along the wind, tilted a little so it is seen from above and from the side.
        const float ax = dx * len, az = dz * len, bx = -dz * th, bz = dx * th;
        const float corners[4][3] = { {x - ax - bx, y, z - az - bz}, {x + ax - bx, y + th, z + az - bz}, {x + ax + bx, y + th, z + az + bz}, {x - ax + bx, y, z - az + bz} };
        for (int k = 0; k < 4; k++) {
            Vtx& o = v[i * 4 + k];
            o.v.ob[0] = static_cast<s16>(std::lround(corners[k][0])); o.v.ob[1] = static_cast<s16>(std::lround(corners[k][1])); o.v.ob[2] = static_cast<s16>(std::lround(corners[k][2]));
            o.v.flag = 0; o.v.tc[0] = o.v.tc[1] = 0;
            const bool tip = k == 1 || k == 2;   // the head of the streak is brighter than its tail
            o.v.cn[0] = 235; o.v.cn[1] = 242; o.v.cn[2] = 255; o.v.cn[3] = tip ? a : static_cast<u8>(a / 5);
        }
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_XLU_DISP++, G_LIGHTING | G_CULL_BACK | G_CULL_FRONT | G_FOG);
    gDPSetRenderMode(POLY_XLU_DISP++, G_RM_PASS, G_RM_ZB_XLU_SURF2);
    gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED);
    Matrix_Translate(origin.x, origin.y, origin.z, MTXMODE_NEW);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    for (int first = 0; first < n; first += 8) {   // eight streaks (32 vertices) at a time
        const int count = std::min(8, n - first);
        gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(&v[first * 4]), count * 4, 0);
        for (int k = 0; k < count; k++) gSP2Triangles(POLY_XLU_DISP++, k * 4, k * 4 + 1, k * 4 + 2, 0, k * 4, k * 4 + 2, k * 4 + 3, 0);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// The tornado: a funnel that wanders over the ground, drifting a little with the wind (and bigger in storms). It is local scenery for fun, so
// nothing about it is sent to anyone else: you see it, and only you are pulled in. Its physics: inside its reach Link is drawn toward the funnel,
// whirled round it (faster nearer the core) and lifted; high up he is flung out of the top. Its model is built fresh every frame (rings of
// the funnel, twisting and leaning, with a dust skirt at the foot), and loose leaves and grass whirl round it.
struct TornadoState {
    bool active = false;
    float x = 0, z = 0, ground = 0, heading = 0;
    int scene = -1;
};
TornadoState gTornado;
constexpr float kTornadoHeight = 900.0f, kTornadoReach = 460.0f;

float TornadoRadius(float h01) { return 38.0f + 270.0f * std::pow(h01, 1.7f); }   // narrow at the foot, wide at the top
float TornadoSway(float h01, float t, int axis) { return std::sin(t * (1.1f + 0.3f * axis) + h01 * 3.4f + axis * 2.0f) * 70.0f * h01 * h01; }

// Once per game tick (20 a second): it wanders, and if you are within reach it takes hold of you.
void UpdateTornado(Player* player) {
    if (!gTornadoOn || !InField() || gPlayState == nullptr) { gTornado.active = false; return; }
    Feat("tornado");
    const float dt = 1.0f / 20.0f, t = static_cast<float>(ImGui::GetTime());
    const float px = player->actor.world.pos.x, pz = player->actor.world.pos.z;
    if (!gTornado.active || gTornado.scene != gPlayState->sceneNum) {   // (re)spawn some way off, where there is ground
        gTornado.active = false;
        for (int tries = 0; tries < 24; tries++) {
            const float a = Flora01(tries, static_cast<int>(t), 311) * 6.2831853f, d = 900.0f + 500.0f * Flora01(tries, 7, 312);
            const float x = px + std::cos(a) * d, z = pz + std::sin(a) * d;
            float gy = 0;
            if (!RawFloorAt(x, z, &gy)) continue;
            gTornado = { true, x, z, gy, a + 3.14159f, gPlayState->sceneNum };
            break;
        }
        if (!gTornado.active) return;
    }
    float wx, wz, wind;
    WindNow(&wx, &wz, &wind);
    TornadoState& tn = gTornado;
    tn.heading += std::sin(t * 0.23f + tn.x * 0.001f) * 0.35f * dt;                    // a slow, drunken wander
    const float speed = 70.0f + 60.0f * wind;
    const float nx = tn.x + (std::cos(tn.heading) * speed + wx * 0.35f) * dt, nz = tn.z + (std::sin(tn.heading) * speed + wz * 0.35f) * dt;
    float gy = tn.ground;
    if (RawFloorAt(nx, nz, &gy) && std::fabs(gy - tn.ground) < 60.0f) { tn.x = nx; tn.z = nz; tn.ground = gy; }
    else tn.heading += 2.2f;   // a wall, a cliff or the water's edge: it turns away
    if (std::hypot(tn.x - px, tn.z - pz) > 6000.0f) tn.active = false;   // lost far behind: a new one will find you
    // Physics
    const float ddx = px - tn.x, ddz = pz - tn.z, d = std::max(1.0f, std::hypot(ddx, ddz));
    if (d > kTornadoReach) return;
    const float s = std::pow(1.0f - d / kTornadoReach, 1.4f);
    const float ix = -ddx / d, iz = -ddz / d;            // inward
    const float tx = -iz, tz = ix;                       // round it (counter-clockwise)
    Vec3f& pos = player->actor.world.pos;
    const float above = pos.y - tn.ground;
    const float pull = 220.0f * s, whirl = 150.0f + 520.0f * s;
    pos.x += (ix * pull + tx * whirl * s) * dt;
    pos.z += (iz * pull + tz * whirl * s) * dt;
    if (d < kTornadoReach * 0.6f && above < kTornadoHeight) {
        pos.y += 340.0f * s * dt;                                              // lifted up the funnel
        player->actor.velocity.y = std::max(player->actor.velocity.y, 70.0f * s);
    }
    if (above > kTornadoHeight * 0.7f) {                                       // flung out of the top
        pos.x -= ix * 700.0f * dt; pos.z -= iz * 700.0f * dt;
    }
}

void DrawTornado(PlayState* play) {
    static const char drawKey = 0;
    DrawIdentity identity(&drawKey, 0, 0);
    if (!gTornado.active || !gTornadoOn || !InField() || gTornado.scene != play->sceneNum) return;
    Feat("tornado draw");
    const float t = static_cast<float>(ImGui::GetTime());
    const Vec3f eye = play->view.eye;
    if (std::hypot(gTornado.x - eye.x, gTornado.z - eye.z) > 5500.0f) return;
    constexpr int kRings = 10, kSegs = 14;
    Vtx* v = static_cast<Vtx*>(FrameAlloc(play, static_cast<size_t>(kSegs + 1) * kRings * sizeof(Vtx)));
    if (v == nullptr) return;
    for (int i = 0; i < kRings; i++) {
        const float h01 = static_cast<float>(i) / (kRings - 1), h = h01 * kTornadoHeight;
        const float r = TornadoRadius(h01), ox = TornadoSway(h01, t, 0), oz = TornadoSway(h01, t, 1);
        const float spin = t * (6.0f - 3.5f * h01) + h01 * 5.0f;   // the foot whirls fastest, the top lags: the funnel twists
        for (int j = 0; j <= kSegs; j++) {
            const float a = spin + 6.2831853f * j / kSegs;
            const float streak = 0.5f + 0.5f * std::sin(a * 3.0f + h01 * 9.0f - t * 5.0f);   // dark bands spiralling up it
            const float shade = 0.55f + 0.4f * streak;
            Vtx& o = v[i * (kSegs + 1) + j];
            o.v.ob[0] = static_cast<s16>(std::lround(ox + std::cos(a) * r)); o.v.ob[1] = static_cast<s16>(std::lround(h)); o.v.ob[2] = static_cast<s16>(std::lround(oz + std::sin(a) * r));
            o.v.flag = 0; o.v.tc[0] = o.v.tc[1] = 0;
            o.v.cn[0] = static_cast<u8>(150.0f * shade + 20.0f * (1.0f - h01)); o.v.cn[1] = static_cast<u8>(140.0f * shade + 12.0f * (1.0f - h01)); o.v.cn[2] = static_cast<u8>(130.0f * shade);
            o.v.cn[3] = static_cast<u8>(std::clamp((i == 0 ? 120.0f : 175.0f) * (1.0f - 0.55f * h01 * h01) * (0.7f + 0.3f * streak), 0.0f, 255.0f));
        }
    }
    OPEN_DISPS(play->state.gfxCtx);
    // Debris whirling round it: leaves and grass tufts climbing the funnel.
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    CLOSE_DISPS(play->state.gfxCtx);
    for (int i = 0; i < 16; i++) {
        const float life = std::fmod(t * (0.22f + 0.1f * Flora01(i, 3, 321)) + Flora01(i, 5, 322), 1.0f);
        const float h01 = life * 0.85f, r = TornadoRadius(h01) * (0.85f + 0.5f * Flora01(i, 9, 323));
        const float a = t * (5.0f - 2.5f * h01) + i * 0.39f;
        const GpuMesh* m = (i & 1) ? GpuMeshFor(royale::MeshKind::LeafPile, 3u) : GpuMeshFor(royale::MeshKind::Grass, static_cast<uint32_t>(i % 4) + 4u);
        if (m == nullptr || m->dl.empty()) continue;
        DrawFloraMesh(play, m, gTornado.x + TornadoSway(h01, t, 0) + std::cos(a) * r, gTornado.ground + h01 * kTornadoHeight, gTornado.z + TornadoSway(h01, t, 1) + std::sin(a) * r,
                      t * 3.0f + i, t * 2.0f + i, t * 2.6f, (i & 1) ? 0.16f : 0.3f);
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_XLU_DISP++, G_LIGHTING | G_CULL_BACK | G_CULL_FRONT | G_FOG);
    gDPSetRenderMode(POLY_XLU_DISP++, G_RM_PASS, G_RM_ZB_XLU_SURF2);
    gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED);
    Matrix_Translate(gTornado.x, gTornado.ground, gTornado.z, MTXMODE_NEW);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    for (int j = 0; j < kSegs; j++) {   // one strip of the funnel at a time: two columns of rings
        Vtx* strip = static_cast<Vtx*>(FrameAlloc(play, static_cast<size_t>(2 * kRings) * sizeof(Vtx)));
        if (strip == nullptr) break;
        for (int i = 0; i < kRings; i++) { strip[i] = v[i * (kSegs + 1) + j]; strip[kRings + i] = v[i * (kSegs + 1) + j + 1]; }
        gSPVertex(POLY_XLU_DISP++, reinterpret_cast<uintptr_t>(strip), 2 * kRings, 0);
        for (int i = 0; i + 1 < kRings; i++) gSP2Triangles(POLY_XLU_DISP++, i, kRings + i, kRings + i + 1, 0, i, kRings + i + 1, i + 1, 0);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// Rain and snow are the game's own: its rain streaks and its falling snow (the same flakes as its winter holiday mode), switched on and scaled with
// the server's spell of weather and the storm. So are the rest: thunderstorms and the storm zone bring the game's lightning (bolts on the horizon,
// the sky flashing), fog, sand, ash and the storm thicken the game's own distance fog and tint it, and a sandstorm brings the Haunted Wasteland's
// sandstorm. Everything is handed back to the game when the weather clears or the match ends.
void DriveRealWeather() {
    static bool rainManaged = false, snowManaged = false, lightningManaged = false, sandManaged = false, fogManaged = false;
    static float fogNearAdj = 0.0f, fogFarAdj = 0.0f, fogColAdj[3] = { 0.0f, 0.0f, 0.0f };
    if (!InField() || gPlayState == nullptr) {   // the scene is going (or gone): the next one starts from its own settings
        rainManaged = snowManaged = lightningManaged = sandManaged = fogManaged = false;
        fogNearAdj = fogFarAdj = fogColAdj[0] = fogColAdj[1] = fogColAdj[2] = 0.0f;
        return;
    }
    PlayState* play = gPlayState;
    const float w = WeatherAmount();
    const royale::Sky sky = gWeatherShown.sky;
    const bool rainSky = sky == royale::Sky::Rain || sky == royale::Sky::Thunder;
    const float rain = std::max(rainSky ? w : 0.0f, gStormWeather * 0.8f) * gWeatherDensity;
    const float snow = sky == royale::Sky::Snow ? w * gWeatherDensity : 0.0f;
    const int wantRain = std::clamp(static_cast<int>(rain * 70.0f), 0, 130);
    if (wantRain > 0 || (rainManaged && play->envCtx.unk_EE[1] > 0)) {
        if (!rainManaged) Trace("weather: rain on");
        const int cur = play->envCtx.unk_EE[1];
        play->envCtx.unk_EE[1] = static_cast<u8>(cur < wantRain ? std::min(wantRain, cur + 2) : std::max(wantRain, cur - 2));
        rainManaged = true;
    } else rainManaged = false;
    const int wantSnow = std::clamp(static_cast<int>(snow * 40.0f) & ~1, 0, 62);
    if (wantSnow > 0) {
        if (!snowManaged) Trace("weather: snow on");
        snowManaged = true;
        play->envCtx.unk_EE[3] = static_cast<u8>(wantSnow);
        bool haveSnow = false;
        for (Actor* a = play->actorCtx.actorLists[ACTORCAT_ITEMACTION].head; a != nullptr; a = a->next)
            if (a->id == ACTOR_OBJECT_KANKYO && a->params == 3 && a->update != nullptr) { haveSnow = true; break; }
        static int tryFrame = 0;
        if (!haveSnow && tryFrame++ % 40 == 0)
            Actor_Spawn(&play->actorCtx, play, ACTOR_OBJECT_KANKYO, 0, 0, 0, 0, 0, 0, 3, false);
    } else if (snowManaged) {
        play->envCtx.unk_EE[3] = 0;   // the flakes thin out by themselves
        if (play->envCtx.unk_EE[2] == 0) snowManaged = false;
    }

    // Lightning: the game strikes at random every few seconds while it is on. It is turned off with "one last strike", which lets a flash
    // that is under way finish (switching straight off would leave the sky lit).
    const bool wantLightning = (sky == royale::Sky::Thunder && w > 0.25f) || gStormWeather > 0.5f;
    if (wantLightning) {
        if (!lightningManaged) Trace("weather: lightning on");
        play->envCtx.lightningMode = LIGHTNING_MODE_ON;
        lightningManaged = true;
    } else if (lightningManaged) {
        if (play->envCtx.lightningMode == LIGHTNING_MODE_ON) play->envCtx.lightningMode = LIGHTNING_MODE_LAST;
        lightningManaged = false;
    }

    // The desert's sandstorm, as in the Haunted Wasteland. "Weather density" 0 leaves it out.
    const bool wantSand = sky == royale::Sky::Sandstorm && w * gWeatherDensity > 0.2f;
    if (wantSand) {
        if (!sandManaged) Trace("weather: sandstorm on");
        if (play->envCtx.sandstormState == SANDSTORM_OFF || play->envCtx.sandstormState == SANDSTORM_DISSIPATE) play->envCtx.sandstormState = SANDSTORM_ACTIVE;
        sandManaged = true;
    } else if (sandManaged) {
        if (play->envCtx.sandstormState != SANDSTORM_OFF) play->envCtx.sandstormState = SANDSTORM_DISSIPATE;   // it fades out and switches itself off
        sandManaged = false;
    }

    // Fog, in the game's own fog: a fog "near" of 996 is no fog at all, lower brings it closer; "far" is where it is solid (and also how far the
    // game draws). Each kind of weather pulls both in and tints the fog its colour; the storm wins where it is thicker.
    float near = 996.0f, far = 12800.0f, amount = 0.0f;
    float col[3] = { 0.0f, 0.0f, 0.0f };
    auto want = [&](float a, float n, float f, int r, int g, int b) {
        if (a <= 0.0f) return;
        const float nn = 996.0f + (n - 996.0f) * a, ff = 12800.0f + (f - 12800.0f) * a;
        if (nn >= near && a <= amount) return;
        near = std::min(near, nn); far = std::min(far, ff);
        amount = std::max(amount, a);
        col[0] = static_cast<float>(r); col[1] = static_cast<float>(g); col[2] = static_cast<float>(b);
    };
    switch (sky) {
        case royale::Sky::Fog: want(w, 900.0f, 3200.0f, 205, 212, 218); break;
        case royale::Sky::Sandstorm: want(w, 930.0f, 4200.0f, 205, 165, 105); break;
        case royale::Sky::Ash: want(w, 960.0f, 6000.0f, 70, 52, 48); break;
        case royale::Sky::Thunder: want(w, 975.0f, 8000.0f, 60, 66, 84); break;
        case royale::Sky::Rain: want(w, 985.0f, 10000.0f, 110, 118, 130); break;
        case royale::Sky::Snow: want(w, 975.0f, 8000.0f, 220, 228, 240); break;
        default: break;
    }
    want(gStormWeather, 940.0f, 5000.0f, 70, 40, 120);   // inside the storm: violet murk
    const EnvLightSettings& base = play->envCtx.lightSettings;
    const float targetNear = amount > 0.0f ? std::min(0.0f, near - static_cast<float>(base.fogNear & 0x3FF)) : 0.0f;
    const float targetFar = amount > 0.0f ? std::min(0.0f, far - static_cast<float>(base.fogFar)) : 0.0f;
    if (amount > 0.0f || fogManaged) {
        if (!fogManaged) Trace("weather: fog on");
        auto toward = [](float cur, float target, float step) { return cur < target ? std::min(target, cur + step) : std::max(target, cur - step); };
        fogNearAdj = toward(fogNearAdj, targetNear, 1.5f);
        fogFarAdj = toward(fogFarAdj, targetFar, 220.0f);
        for (int i = 0; i < 3; i++) {
            const float target = amount > 0.0f ? (col[i] - static_cast<float>(base.fogColor[i])) * std::min(1.0f, amount * 1.2f) : 0.0f;
            fogColAdj[i] = toward(fogColAdj[i], target, 4.0f);
        }
        play->envCtx.adjFogNear = static_cast<s16>(std::lround(fogNearAdj));
        play->envCtx.adjFogFar = static_cast<s16>(std::lround(fogFarAdj));
        for (int i = 0; i < 3; i++) play->envCtx.adjFogColor[i] = static_cast<s16>(std::lround(fogColAdj[i]));
        fogManaged = amount > 0.0f || fogNearAdj != 0.0f || fogFarAdj != 0.0f || fogColAdj[0] != 0.0f || fogColAdj[1] != 0.0f || fogColAdj[2] != 0.0f;
    }
}
