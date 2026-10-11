// ---- the Moonfall Bus (shared/bus.h, docs/BATTLE_BUS.md) -------------------------------------------------------------------------------------
// Everyone starts the match on a bench on the bus. It waits offshore through the countdown, climbing to its height, then flies its one straight
// line across the map during the drop. Once it is over the island A jumps: you free-fall with your arms out, the glider opens (A again, or by
// itself kAutoGlideHeight above the ground) and from there it is the skydive in Terrain.inc. Whoever is still aboard when the bus leaves the
// island is pushed off. C-Right honks the horn. The server decides who is aboard (Snapshot.aboardMask) and where the line goes (EvBusPath);
// every game works out where the bus is from the time since the drop began, so nothing about it is sent while it flies.
//
// gSkydiving stays on for the whole time in the air (sat on the bus included), so everything that is off while skydiving (items, emotes, sprint,
// the jump button) is off on the bus too; gBusPhase says which part of it this is (Hud.inc: 0 the glider, 1 sat on the bus, 2 free fall).
//
// The bus's model (tools/bus, shared/bus_model.h) is drawn by Bus_Draw; the benches, the riders, the horn and the minimap all follow the same
// frame (UpdateBusFrame).

constexpr float kBusSaddleDrop = 27.0f;      // the riding pose sits this far below the bench (as the cart's kSaddleDrop)
constexpr float kBusSeenFrom = 9000.0f;      // the bus actor is drawn from this far away (it has to be seen from the ground)
constexpr float kBusFlyOnSeconds = 6.0f;     // after the drop it flies on out to sea this long, then goes
constexpr float kBusHornHearing = 6000.0f;

void DrawAllyModel(PlayState* play, float x, float y, float z, float yaw, const royale::allymodel::Model& m, const royale::allymodel::Pose& pose,
                   int face, float roll, float rollPivot);   // Companions.inc

struct BusFrame {
    bool valid = false;
    float x = 0, y = 0, z = 0, yaw = 0;      // floor centre and heading (radians, 0 = +z)
    float roll = 0, pitch = 0;               // the balloon's pendulum sway, passed down to the body a little
    float t = 0;                             // seconds since the drop began (negative in the countdown)
};
BusFrame gBusFrame;
Actor* gBusActor = nullptr;
ActorFunc gBusOrigDestroy = nullptr;
bool gBusJumpPressed = false, gBusHornPressed = false;   // this frame's A and C-Right while sat on the bus or falling (BusInput)
bool gBusHornNow = false;                                // send the honk with this tick's pose
double gBusHornAt = -10.0, gBusExitAt = -10.0, gGliderPopAt = -10.0;
double gBusFlyOnUntil = 0;
float gBusFlyOnFrom = 0;
bool gBusLeavingWarned = false;

float BusBaseY() { return std::max(0.0f, gMedianFloorY); }

// Where the bus is this frame, from the line and the clock.
void UpdateBusFrame(const royale::HudState& hud) {
    gBusFrame.valid = false;
    if (!hud.bus.valid || !InField()) return;
    const double now = ImGui::GetTime();
    float t = 0, alt = hud.bus.altitude;
    if (hud.state == royale::MatchState::Countdown) {
        t = -std::max(0.0f, hud.countdownLeft);
        alt = royale::BusClimbAltitude(hud.bus, royale::kCountdownSec - hud.countdownLeft);
        gBusFlyOnUntil = 0;
    } else if (hud.state == royale::MatchState::Drop) {
        t = hud.busTime;
        gBusFlyOnFrom = t;
        gBusFlyOnUntil = now + kBusFlyOnSeconds;
    } else if (hud.state == royale::MatchState::InMatch && now < gBusFlyOnUntil) {
        t = gBusFlyOnFrom + static_cast<float>(kBusFlyOnSeconds - (gBusFlyOnUntil - now));   // flying on out to sea
    } else {
        return;
    }
    const royale::Vec2 at = royale::BusPosUnclamped(hud.bus, std::max(0.0f, t));
    gBusFrame.valid = true;
    gBusFrame.x = at.x;
    gBusFrame.z = at.z;
    gBusFrame.y = BusBaseY() + alt;
    gBusFrame.yaw = royale::BusYaw(hud.bus);
    const float ph = static_cast<float>(now) * (6.2831853f / 4.0f);   // a 4 s pendulum
    gBusFrame.roll = 0.012f * std::sin(ph);
    gBusFrame.pitch = 0.008f * std::sin(ph + 1.3f);
    gBusFrame.t = t;
}

Vec3f BusWorld(float lx, float ly, float lz) {
    Vec3f w;
    royale::BusLocalToWorld(gBusFrame.yaw, gBusFrame.x, gBusFrame.y, gBusFrame.z, lx, ly, lz, &w.x, &w.y, &w.z);
    return w;
}
Vec3f BusSeatWorld(int seat) {
    float lx, ly, lz;
    royale::BusSeatLocal(seat, &lx, &ly, &lz);
    return BusWorld(lx, ly, lz);
}
s16 BusYawBinang() { return static_cast<s16>(gBusFrame.yaw * (32768.0f / 3.14159265f)); }

// ---- the bus actor: alive while the bus is to be seen ----------------------------------------------------------------------------------------
void Bus_Update(Actor* actor, PlayState*) {
    if (!gBusFrame.valid) { Actor_Kill(actor); return; }
    actor->world.pos = { gBusFrame.x, gBusFrame.y, gBusFrame.z };
    actor->shape.rot.y = actor->world.rot.y = BusYawBinang();
    actor->shape.rot.x = static_cast<s16>(gBusFrame.pitch * (32768.0f / 3.14159265f));
    actor->shape.rot.z = static_cast<s16>(gBusFrame.roll * (32768.0f / 3.14159265f));
    // The burner's roar every 2.5 s, and the propeller's hum, heard from close by.
    const double now = ImGui::GetTime();
    static double nextBurn = 0;
    if (now >= nextBurn) {
        nextBurn = now + 2.5;
        Vec3f at = BusWorld(royale::busmodel::kNozzle.x, royale::busmodel::kNozzle.y + 60.0f, royale::busmodel::kNozzle.z);
        Audio_PlaySoundGeneral(NA_SE_EV_FLAME_IGNITION, &at, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }
    if (gPlayState != nullptr && (gPlayState->gameplayFrames % 20) == 0) {
        const auto& hub = royale::busmodel::kPivot[royale::busmodel::kPropeller];
        Vec3f prop = BusWorld(hub.x, hub.y, hub.z);
        Audio_PlaySoundGeneral(NA_SE_EV_WINDMILL_LEVEL, &prop, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }
}
// ---- drawing the bus (shared/bus_model.h, made by tools/bus) ---------------------------------------------------------------------------------
// Rigid parts, as the cart is drawn: the body (with the burner cage, the deck, the benches inside), the roof, the Moon (its rock, its face as a
// decal that fades into the rock, its glowing eyes), the propeller, eight lanterns, four wheels, four ropes, the pennant and the flame. Lighting
// is baked into the vertex colours from the sun, in the bus's own frame: the bus keeps one heading for the whole flight, so the colours are
// worked out again only when the heading changes. The runes, lamps, eyes and lanterns glow (full bright).
namespace BusM = royale::busmodel;
constexpr float kBusSub = 8.0f;              // vertices go to the graphics chip in 1/8 units
constexpr float kBusFarLod = 4500.0f;        // past this the far model (a box and a ball) is drawn

struct BusGpu {
    std::vector<Vtx> vtx;
    std::vector<Gfx> dl[BusM::kPartCount];
    bool built = false;
    float litYaw = 1.0e9f;
};
BusGpu gBusGpu;

void LoadBusTexture(Gfx*& g, int tex) {
    const int w = BusM::kTexW[tex], h = BusM::kTexH[tex];
    if (tex == 2)   // the Moon's rock tiles round the sphere
        gDPLoadTextureTile(g++, BusM::kTextures[tex], G_IM_FMT_RGBA, G_IM_SIZ_16b, w, h, 0, 0, w - 1, h - 1, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP,
                           7, 7, G_TX_NOLOD, G_TX_NOLOD);
    else
        gDPLoadTextureTile(g++, BusM::kTextures[tex], G_IM_FMT_RGBA, G_IM_SIZ_16b, w, h, 0, 0, w - 1, h - 1, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP,
                           G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
}

// The vertex colours for a heading: the sun (as for the cart) turned into the bus's frame.
void LightBus(float yaw) {
    const float wx = 0.30f, wy = 0.86f, wz = 0.41f;
    const float c = std::cos(yaw), s = std::sin(yaw);
    const float lx = wx * c - wz * s, ly = wy, lz = wx * s + wz * c;   // the sun seen from the bus (turned by -yaw)
    for (int b = 0; b < BusM::kBatchCount; b++) {
        const BusM::Batch& bt = BusM::kBatches[b];
        for (uint32_t i = bt.firstVert; i < bt.firstVert + bt.vertCount; i++) {
            const BusM::Vert& v = BusM::kVerts[i];
            Vtx& o = gBusGpu.vtx[i];
            float lit = 1.0f;
            if (!bt.glow) {
                const float nx = v.nx / 127.0f, ny = v.ny / 127.0f, nz = v.nz / 127.0f;
                lit = std::clamp(0.46f + 0.62f * std::max(0.0f, nx * lx + ny * ly + nz * lz) + 0.10f * ny, 0.0f, 1.0f);
            }
            o.v.cn[0] = static_cast<u8>(255.0f * lit);
            o.v.cn[1] = static_cast<u8>(248.0f * lit);
            o.v.cn[2] = static_cast<u8>(238.0f * lit);
            o.v.cn[3] = v.a;
        }
    }
    gBusGpu.litYaw = yaw;
}

void BuildBusGpu(float yaw) {
    if (!gBusGpu.built) {
        gBusGpu.vtx.resize(BusM::kVertCount);
        for (int i = 0; i < BusM::kVertCount; i++) {
            const BusM::Vert& v = BusM::kVerts[i];
            Vtx& o = gBusGpu.vtx[i];
            o.v.ob[0] = static_cast<s16>(std::lround(v.x * kBusSub));
            o.v.ob[1] = static_cast<s16>(std::lround(v.y * kBusSub));
            o.v.ob[2] = static_cast<s16>(std::lround(v.z * kBusSub));
            o.v.flag = 0;
            o.v.tc[0] = v.s;
            o.v.tc[1] = v.t;
        }
        for (int part = 0; part < BusM::kPartCount; part++) {
            std::vector<Gfx>& dl = gBusGpu.dl[part];
            size_t need = 4;
            for (int b = 0; b < BusM::kBatchCount; b++) if (BusM::kBatches[b].part == part) need += 8 + (BusM::kBatches[b].triCount + 1) / 2;
            dl.assign(need, Gfx{});
            Gfx* g = dl.data();
            int loaded = -1;
            for (int b = 0; b < BusM::kBatchCount; b++) {
                const BusM::Batch& bt = BusM::kBatches[b];
                if (bt.part != part) continue;
                if (bt.tex != loaded) { LoadBusTexture(g, bt.tex); loaded = bt.tex; }
                gSPVertex(g++, reinterpret_cast<uintptr_t>(&gBusGpu.vtx[bt.firstVert]), bt.vertCount, 0);
                const uint32_t end = bt.firstTri + bt.triCount;
                uint32_t t = bt.firstTri;
                for (; t + 1 < end; t += 2)
                    gSP2Triangles(g++, BusM::kTris[t][0], BusM::kTris[t][1], BusM::kTris[t][2], 0, BusM::kTris[t + 1][0], BusM::kTris[t + 1][1], BusM::kTris[t + 1][2], 0);
                if (t < end) gSP1Triangle(g++, BusM::kTris[t][0], BusM::kTris[t][1], BusM::kTris[t][2], 0);
            }
            gSPEndDisplayList(g++);
            dl.resize(static_cast<size_t>(g - dl.data()));
        }
        gBusGpu.built = true;
    }
    if (std::fabs(yaw - gBusGpu.litYaw) > 0.01f) LightBus(yaw);
}

// The Moon's sway (radians about x and z, about its collar), the same on every machine: it only depends on the clock.
void BusMoonSway(float* rx, float* rz) {
    const float ph = static_cast<float>(ImGui::GetTime()) * (6.2831853f / 4.0f);
    *rx = 0.035f * std::sin(ph + 1.3f);
    *rz = 0.035f * std::sin(ph);
}
// A point in the Moon's frame (bus coordinates, before the sway) after the sway.
Vec3f BusMoonPoint(const BusM::P3& p, float rx, float rz) {
    const BusM::P3& c = BusM::kPivot[BusM::kMoon];
    float x = p.x - c.x, y = p.y - c.y, z = p.z - c.z;
    // as Matrix_RotateX then Matrix_RotateZ applied to the point: z first, then x
    const float cz = std::cos(rz), sz = std::sin(rz);
    float x1 = x * cz - y * sz, y1 = x * sz + y * cz;
    const float cx = std::cos(rx), sx = std::sin(rx);
    float y2 = y1 * cx - z * sx, z2 = y1 * sx + z * cx;
    return { c.x + x1, c.y + y2, c.z + z2 };
}

void BusPart(PlayState* play, Gfx*& disp, int part) {
    gSPMatrix(disp++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(disp++, gBusGpu.dl[part].data());
}

// The pennant waves: its vertices are moved each frame (a wave running back along it, growing toward the tail).
void DrawBusBanner(PlayState* play, Gfx*& disp) {
    const double now = ImGui::GetTime();
    for (int b = 0; b < BusM::kBatchCount; b++) {
        const BusM::Batch& bt = BusM::kBatches[b];
        if (bt.part != BusM::kBanner) continue;
        Vtx* v = static_cast<Vtx*>(FrameAlloc(play, sizeof(Vtx) * bt.vertCount));
        if (v == nullptr) return;
        for (uint32_t i = 0; i < bt.vertCount; i++) {
            v[i] = gBusGpu.vtx[bt.firstVert + i];
            const BusM::Vert& src = BusM::kVerts[bt.firstVert + i];
            const float along = std::max(0.0f, -src.z) / 150.0f;
            const float wave = std::sin(static_cast<float>(now) * 9.0f + src.z * 0.05f) * 16.0f * along;
            v[i].v.ob[0] = static_cast<s16>(std::lround((src.x + wave) * kBusSub));
            v[i].v.ob[1] = static_cast<s16>(std::lround((src.y - 6.0f * along * along) * kBusSub));
        }
        gSPVertex(disp++, reinterpret_cast<uintptr_t>(v), bt.vertCount, 0);
        const uint32_t end = bt.firstTri + bt.triCount;
        for (uint32_t t = bt.firstTri; t < end; t++) gSP1Triangle(disp++, BusM::kTris[t][0], BusM::kTris[t][1], BusM::kTris[t][2], 0);
    }
}

void Bus_Draw(Actor* actor, PlayState* play) {
    if (!gBusFrame.valid) return;
    GfxLayer layer(play, GfxLayerId::Characters);   // its tens of thousands of vertices go through the layer pool, not the game's buffer
    BuildBusGpu(gBusFrame.yaw);
    const Vec3f eye = play->view.eye;
    const bool far = std::hypot(eye.x - gBusFrame.x, eye.y - gBusFrame.y, eye.z - gBusFrame.z) > kBusFarLod;
    const float t = static_cast<float>(ImGui::GetTime());
    float rx = 0, rz = 0;
    BusMoonSway(&rx, &rz);
    auto busMatrix = [&]() {
        Matrix_Translate(gBusFrame.x, gBusFrame.y, gBusFrame.z, MTXMODE_NEW);
        Matrix_RotateY(gBusFrame.yaw, MTXMODE_APPLY);
        Matrix_RotateX(gBusFrame.pitch, MTXMODE_APPLY);
        Matrix_RotateZ(gBusFrame.roll, MTXMODE_APPLY);
    };
    auto sub = [&]() { Matrix_Scale(1.0f / kBusSub, 1.0f / kBusSub, 1.0f / kBusSub, MTXMODE_APPLY); };
    auto at = [&](const BusM::P3& p) { Matrix_Translate(p.x, p.y, p.z, MTXMODE_APPLY); };
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);   // the light is in the vertex colours; thin parts show both sides
    gDPSetCombineLERP(POLY_OPA_DISP++, TEXEL0, 0, SHADE, 0, 0, 0, 0, 1, COMBINED, 0, PRIMITIVE, 0, 0, 0, 0, 1);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, 255);
    busMatrix();
    if (far) {
        Matrix_Push(); sub(); BusPart(play, POLY_OPA_DISP, BusM::kBodyFar); Matrix_Pop();
        Matrix_Push(); at(BusM::kPivot[BusM::kMoonFar]); Matrix_RotateX(rx, MTXMODE_APPLY); Matrix_RotateZ(rz, MTXMODE_APPLY); sub();
        BusPart(play, POLY_OPA_DISP, BusM::kMoonFar); Matrix_Pop();
    } else {
    // the body, and the roof unless you are sat inside (the camera looks down through it at the island and the other riders)
    Matrix_Push(); sub(); BusPart(play, POLY_OPA_DISP, BusM::kBody);
    if (gBusPhase != 1) BusPart(play, POLY_OPA_DISP, BusM::kRoof);
    Matrix_Pop();
    // the Moon: rock, then its eyes (their glow breathing slowly)
    Matrix_Push(); at(BusM::kPivot[BusM::kMoon]); Matrix_RotateX(rx, MTXMODE_APPLY); Matrix_RotateZ(rz, MTXMODE_APPLY); sub();
    BusPart(play, POLY_OPA_DISP, BusM::kMoon);
    const u8 glow = static_cast<u8>(215.0f + 40.0f * (0.5f + 0.5f * std::sin(t * 1.3f)));
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, glow, glow, 255);
    BusPart(play, POLY_OPA_DISP, BusM::kMoonEyes);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, 255);
    Matrix_Pop();
    // the propeller, spinning about its hub
    Matrix_Push(); at(BusM::kPivot[BusM::kPropeller]); Matrix_RotateZ(t * 9.42f, MTXMODE_APPLY); sub(); BusPart(play, POLY_OPA_DISP, BusM::kPropeller); Matrix_Pop();
    // the lanterns, swinging against the sway; the wheels, the left ones turned round
    for (int k = 0; k < 8; k++) {
        Matrix_Push(); at(BusM::kLanternHook[k]);
        if (BusM::kLanternHook[k].x < 0) Matrix_RotateY(3.14159265f, MTXMODE_APPLY);
        Matrix_RotateX(0.10f * std::sin(t * 1.6f + k * 0.7f), MTXMODE_APPLY);
        Matrix_RotateZ(-gBusFrame.roll * 4.0f + 0.06f * std::sin(t * 1.9f + k), MTXMODE_APPLY);
        sub(); BusPart(play, POLY_OPA_DISP, BusM::kLantern); Matrix_Pop();
    }
    for (int k = 0; k < 4; k++) {
        Matrix_Push(); at(BusM::kWheelAxle[k]);
        if (BusM::kWheelAxle[k].x < 0) Matrix_RotateY(3.14159265f, MTXMODE_APPLY);
        sub(); BusPart(play, POLY_OPA_DISP, BusM::kWheel); Matrix_Pop();
    }
    // the ropes, from the cage's ring to their knots on the swaying net
    for (int k = 0; k < 4; k++) {
        const BusM::P3& a = BusM::kRopeBody[k];
        const Vec3f b = BusMoonPoint(BusM::kRopeMoon[k], rx, rz);
        const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z, len = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (len < 1.0f) continue;
        const float ux = dx / len, uy = dy / len, uz = dz / len;
        // two axes across the rope
        float sx = uy, sy = -ux, sz = 0.0f;
        float sl = std::sqrt(sx * sx + sy * sy);
        if (sl < 1e-3f) { sx = 1; sy = 0; sl = 1; }
        sx /= sl; sy /= sl;
        const float tx = uy * sz - uz * sy, ty = uz * sx - ux * sz, tz = ux * sy - uy * sx;
        MtxF mf;
        mf.xx = sx; mf.yx = sy; mf.zx = sz; mf.wx = 0;
        mf.xy = ux * len; mf.yy = uy * len; mf.zy = uz * len; mf.wy = 0;
        mf.xz = tx; mf.yz = ty; mf.zz = tz; mf.wz = 0;
        mf.xw = a.x; mf.yw = a.y; mf.zw = a.z; mf.ww = 1;
        Matrix_Push(); Matrix_Mult(&mf, MTXMODE_APPLY); sub(); BusPart(play, POLY_OPA_DISP, BusM::kRope); Matrix_Pop();
    }
    // the pennant, waving
    Matrix_Push(); at(BusM::kPivot[BusM::kBanner]); sub();
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    LoadBusTexture(POLY_OPA_DISP, 0);
    DrawBusBanner(play, POLY_OPA_DISP);
    Matrix_Pop();
    // translucent: the Moon's face (a decal that fades into the rock) and the flame
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_XLU_DISP++, G_LIGHTING | G_CULL_BACK);
    gDPSetRenderMode(POLY_XLU_DISP++, G_RM_PASS, G_RM_AA_ZB_XLU_SURF2);
    gDPSetCombineLERP(POLY_XLU_DISP++, TEXEL0, 0, SHADE, 0, 0, 0, 0, SHADE, COMBINED, 0, PRIMITIVE, 0, COMBINED, 0, PRIMITIVE, 0);
    gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 255, 255, 255);
    busMatrix();
    Matrix_Push(); at(BusM::kPivot[BusM::kMoonFace]); Matrix_RotateX(rx, MTXMODE_APPLY); Matrix_RotateZ(rz, MTXMODE_APPLY); sub();
    BusPart(play, POLY_XLU_DISP, BusM::kMoonFace); Matrix_Pop();
    gDPSetCombineLERP(POLY_XLU_DISP++, TEXEL0, 0, SHADE, 0, TEXEL0, 0, PRIMITIVE, 0, COMBINED, 0, PRIMITIVE, 0, 0, 0, 0, COMBINED);
    const float flick = 0.5f + 0.5f * std::sin(t * 23.0f) * std::sin(t * 7.3f);
    gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, static_cast<u8>(225 + 30 * flick), static_cast<u8>(200 + 40 * flick), 225);
    Matrix_Push(); at(BusM::kPivot[BusM::kFlame]); Matrix_RotateY(t * 2.0f, MTXMODE_APPLY);
    Matrix_Scale(0.9f + 0.15f * flick, 0.85f + 0.3f * flick, 0.9f + 0.15f * flick, MTXMODE_APPLY); sub();
    BusPart(play, POLY_XLU_DISP, BusM::kFlame); Matrix_Pop();
    }
    CLOSE_DISPS(play->state.gfxCtx);
    // the Goron at the tiller (the ally model, standing at ease behind it)
    if (!far) {
        namespace A = royale::allymodel;
        static A::Pose pose;
        A::SampleClip(A::ModelOf(2), A::kIdle, std::fmod(t, A::ClipSeconds(A::ModelOf(2), A::kIdle)), pose);
        const Vec3f p = BusWorld(BusM::kPilot.x, BusM::kPilot.y, BusM::kPilot.z);
        DrawAllyModel(play, p.x, p.y, p.z, gBusFrame.yaw, A::ModelOf(2), pose, 0, 0.0f, 0.0f);
    }
}
void Bus_Destroy(Actor* actor, PlayState* play) {
    if (gBusActor == actor) gBusActor = nullptr;
    if (gBusOrigDestroy) gBusOrigDestroy(actor, play);
}

void ReconcileBusActor() {
    const bool want = gBusFrame.valid && InField() && gPlayState != nullptr;
    if (want && gBusActor == nullptr) {
        Actor* a = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ISHI, gBusFrame.x, gBusFrame.y, gBusFrame.z, 0, BusYawBinang(), 0, 0, false);
        if (a == nullptr) return;
        gBusOrigDestroy = a->destroy;
        a->update = Bus_Update;
        a->draw = Bus_Draw;
        a->destroy = Bus_Destroy;
        a->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
        a->uncullZoneForward = kBusSeenFrom; a->uncullZoneScale = 4000.0f; a->uncullZoneDownward = 4000.0f;
        a->shape.shadowScale = 0.0f;
        gBusActor = a;
    } else if (!want && gBusActor != nullptr) {
        Actor_Kill(gBusActor);
        gBusActor = nullptr;
    }
}

// ---- the horn ----------------------------------------------------------------------------------------------------------------------------------
// Two notes of Epona's Song on the ocarina, from the front of the bus.
void PlayBusHorn() {
    if (!gBusFrame.valid || gPlayState == nullptr) return;
    static Vec3f at;
    at = BusWorld(royale::busmodel::kHorn.x, royale::busmodel::kHorn.y, royale::busmodel::kHorn.z);
    const Player* me = GET_PLAYER(gPlayState);
    if (std::hypot(at.x - me->actor.world.pos.x, at.z - me->actor.world.pos.z) > kBusHornHearing) return;
    static f32 high = 1.0f, low = 0.84f;   // D then B, three semitones down
    Audio_PlaySoundGeneral(NA_SE_OC_OCARINA, &at, 4, &high, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    Audio_PlaySoundGeneral(NA_SE_OC_OCARINA, &at, 4, &low, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

// ---- you, on the bus and falling from it ----------------------------------------------------------------------------------------------------
// Sat on the bench: Link is pinned to it (the game would let him fall) and posed in the riding sit by OnPlayerUpdate.
void PlaceOnBusSeat(Player* player, int seat) {
    const Vec3f w = BusSeatWorld(seat);
    player->actor.world.pos = { w.x, w.y - kBusSaddleDrop, w.z };
    player->actor.prevPos = player->actor.world.pos;
    player->actor.home.pos = player->actor.world.pos;
    player->actor.velocity = { 0, 0, 0 };
    player->actor.speedXZ = 0;
    player->linearVelocity = 0;
    player->fallStartHeight = static_cast<s16>(player->actor.world.pos.y);
    player->fallDistance = 0;
    const s16 yaw = BusYawBinang();
    player->actor.shape.rot.y = player->actor.world.rot.y = player->yaw = yaw;
}

// The epoch teleport at the start of the match puts you on your bench instead of in the sky.
bool BoardBusAtEpoch(Player* player, const royale::HudState& hud) {
    if (hud.busSeat < 0 || !(hud.state == royale::MatchState::Countdown || hud.state == royale::MatchState::Drop)) return false;
    UpdateBusFrame(hud);
    if (!gBusFrame.valid) return false;
    gSkydiving = true;
    gBusPhase = 1;
    gBusLeavingWarned = false;
    gBusJumpPressed = gBusHornPressed = gBusHornNow = false;
    PlaceOnBusSeat(player, hud.busSeat);
    return true;
}

void LeaveBusLocal(Player* player, bool pushed) {
    const Vec3f out = BusWorld(royale::kBusExitX, royale::kBusExitY, royale::kBusExitZ);
    player->actor.world.pos = out;
    player->actor.prevPos = out;
    gBusPhase = 2;
    gBusExitAt = ImGui::GetTime();
    Player_PlaySfx(&player->actor, static_cast<u16>(NA_SE_VO_LI_AUTO_JUMP + player->ageProperties->unk_92));
    Audio_PlaySoundGeneral(NA_SE_EV_FLYING_AIR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    if (pushed) ShowBanner("Off you go!", IM_COL32(200, 150, 255, 255), 2.0f);
}

void OpenGliderFromFreefall() {
    gBusPhase = 0;
    gGliderPopAt = ImGui::GetTime();
    Audio_PlaySoundGeneral(NA_SE_EV_WIND_TRAP, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}
// The glider pops open: it grows from a third of its size to full over 0.4 s.
float GliderPopScale() {
    const float k = static_cast<float>((ImGui::GetTime() - gGliderPopAt) / 0.4);
    if (k >= 1.0f) return 1.0f;
    const float e = 1.0f - (1.0f - k) * (1.0f - k);
    return 0.3f + 0.7f * std::max(0.0f, e);
}

// The bus part of the skydive (UpdateSkydive calls this first). Returns true when it has handled this frame.
bool UpdateBusRide(Player* player, const royale::HudState& hud) {
    if (gBusPhase == 0) return false;
    constexpr float dt = 1.0f / royale::kTickHz;
    UpdateBusFrame(hud);
    if (gBusPhase == 1) {
        if (!gBusFrame.valid || hud.busSeat < 0) { gBusPhase = 0; return false; }   // no bus after all: the plain skydive
        const royale::BusPath& b = hud.bus;
        const bool drop = hud.state == royale::MatchState::Drop;
        float open = 0, close = 0;
        royale::BusWindow(b, open, close);
        if (gBusHornPressed && ImGui::GetTime() - gBusHornAt >= royale::kBusHornCooldown) {
            gBusHornAt = ImGui::GetTime();
            gBusHornNow = true;
            PlayBusHorn();
        }
        if (drop && !gBusLeavingWarned && hud.busTime >= close - 5.0f) { gBusLeavingWarned = true; ShowBanner("The bus is leaving!", IM_COL32(200, 150, 255, 255), 2.5f); }
        const bool doorsOpen = drop && royale::BusOverLand(b, hud.busTime);
        const bool pushed = drop && (hud.busTime >= close || (!hud.aboard && hud.busTime > open));   // the server pushed you off
        if ((doorsOpen && gBusJumpPressed) || pushed || hud.state == royale::MatchState::InMatch) {
            LeaveBusLocal(player, pushed && !gBusJumpPressed);
        } else {
            PlaceOnBusSeat(player, hud.busSeat);
        }
        gBusJumpPressed = gBusHornPressed = false;
        return true;
    }
    // Free fall: arms out, falling fast, steering as with the glider, and carried on ahead for a moment by the bus's speed.
    const Input& in = gPlayState->state.input[0];
    const float sx = in.cur.stick_x, sy = in.cur.stick_y;
    const float mag = std::min(1.0f, std::sqrt(sx * sx + sy * sy) / 60.0f);
    if (mag > 0.1f) {
        const float yaw = static_cast<float>(Camera_GetInputDirYaw(GET_ACTIVE_CAM(gPlayState))) * (3.14159265f / 32768.0f);
        const float a = yaw + std::atan2(-sx, sy);
        player->actor.world.pos.x += std::sin(a) * royale::kAirSpeed * mag * dt;
        player->actor.world.pos.z += std::cos(a) * royale::kAirSpeed * mag * dt;
        player->actor.shape.rot.y = player->actor.world.rot.y = static_cast<s16>(a * (32768.0f / 3.14159265f));
    }
    if (hud.bus.valid) {
        const float since = static_cast<float>(ImGui::GetTime() - gBusExitAt);
        if (since < royale::kBusExitPush) {
            const royale::Vec2 d = royale::BusDir(hud.bus);
            const float v = hud.bus.speed * (1.0f - since / royale::kBusExitPush);
            player->actor.world.pos.x += d.x * v * dt;
            player->actor.world.pos.z += d.z * v * dt;
        }
    }
    const float ground = GroundY(gPlayState, player->actor.world.pos.x, player->actor.world.pos.z, -1.0e6f);
    float y = player->actor.world.pos.y - royale::kFreefallSpeed * dt;
    const bool low = ground > -1.0e5f && y - ground <= royale::kAutoGlideHeight;
    player->actor.world.pos.y = y;
    player->actor.prevPos = player->actor.world.pos;
    player->actor.velocity.y = 0.0f;
    player->actor.speedXZ = 0.0f;
    player->fallDistance = 0;
    if (gBusJumpPressed || low) OpenGliderFromFreefall();
    gBusJumpPressed = gBusHornPressed = false;
    return true;
}

// Sat on the bus or falling from it: A and C-Right are the bus's, and nothing else on the pad does anything (CartInput's pattern). Returns true
// when it took the controller.
bool BusInput(Input& in, const royale::HudState& hud) {
    if (gBusPhase == 0 || !gSkydiving || !InField()) return false;
    if (in.press.button & BTN_A) gBusJumpPressed = true;
    if (gBusPhase == 1 && (in.press.button & BTN_CRIGHT)) gBusHornPressed = true;
    if (gTouchJump) { gBusJumpPressed = true; gTouchJump = false; }
    (void)hud;
    const u16 keep = BTN_START;
    in.cur.button &= keep; in.press.button &= keep; in.rel.button &= keep;
    if (gBusPhase == 1) in.cur.stick_x = in.cur.stick_y = in.rel.stick_x = in.rel.stick_y = in.press.stick_x = in.press.stick_y = 0;
    return true;
}

// What others see you doing while it is the bus's turn (ClassifyAnim asks first). Anim::Count means "not the bus".
uint8_t BusAnim() {
    using royale::Anim;
    if (!gSkydiving) return static_cast<uint8_t>(Anim::Count);
    if (gBusPhase == 1) {
        if (gBusHornNow) { gBusHornNow = false; return static_cast<uint8_t>(Anim::BusHorn); }
        return static_cast<uint8_t>(Anim::BusRide);
    }
    if (gBusPhase == 2) return static_cast<uint8_t>(Anim::Freefall);
    if (gStateNow == royale::MatchState::Countdown || gStateNow == royale::MatchState::Drop || gStateNow == royale::MatchState::InMatch)
        return static_cast<uint8_t>(Anim::Glide);
    return static_cast<uint8_t>(Anim::Count);
}

// Your own pose for the bus's parts (OnPlayerUpdate): the riding sit on the bench, the long fall with arms out. Returns false for the glider.
bool PoseLocalBus(Player* player) {
    LinkAnimationHeader* want = gBusPhase == 1 ? RA(uma_wait_1) : gBusPhase == 2 ? RA(normal_fall) : nullptr;
    if (want == nullptr) return false;
    if (player->skelAnime.animation != (void*)want) LinkAnimation_PlayLoop(gPlayState, &player->skelAnime, want);
    LinkAnimation_Update(gPlayState, &player->skelAnime);
    return true;
}

// ---- the others on the bus and falling from it (Puppet_Update calls this) ---------------------------------------------------------------------
// Returns true while the puppet is sat on its bench or in free fall: it is placed and posed here.
bool PuppetBusSeat(PlayState* play, Player* player, PuppetMotion& m, const royale::PuppetState& s) {
    using royale::Anim;
    Actor* actor = &player->actor;
    if (royale::IsBusSeat(s.anim) && s.busSeat >= 0 && gBusFrame.valid) {
        if (m.anim != 236) { StartSeq(play, player, m, AnimSeq(RA(uma_wait_1), true), -4.0f); m.anim = 236; }
        LinkAnimation_Update(play, &player->skelAnime);
        const Vec3f w = BusSeatWorld(s.busSeat);
        actor->world.pos = { w.x, w.y - kBusSaddleDrop, w.z };
        actor->shape.rot.y = actor->world.rot.y = BusYawBinang();
        actor->shape.rot.x = actor->shape.rot.z = 0;
        actor->shape.shadowAlpha = 0;
        actor->focus.pos = actor->world.pos;
        actor->focus.pos.y += 50.0f;
        return true;
    }
    if (s.anim == static_cast<uint8_t>(Anim::Freefall)) {
        if (m.anim != 235) { StartSeq(play, player, m, AnimSeq(RA(normal_fall), true), -3.0f); m.anim = 235; }
        LinkAnimation_Update(play, &player->skelAnime);
        actor->shape.shadowAlpha = 0;
        return true;
    }
    return false;
}

// ---- the HUD: prompts, the top line, the bus on the minimap -----------------------------------------------------------------------------------
// The name of the place the bus is over (the nearest point of interest, within reach), or empty.
std::string BusOverPlace() {
    if (!gBusFrame.valid || gSession.Client() == nullptr) return {};
    float best = 2600.0f;
    const char* name = nullptr;
    for (const royale::Poi& p : gSession.Client()->Pois()) {
        const float d = std::hypot(p.center.x - gBusFrame.x, p.center.z - gBusFrame.z);
        if (d < best) { best = d; name = royale::kPoiNames[p.name]; }
    }
    return name ? std::string(name) : std::string();
}

void DrawBusHud(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const royale::HudState& h) {
    if (gBusPhase == 0 || !gSkydiving || !InField() || WarMenuOpen()) return;
    using G = royale::controls::Glyph;
    const royale::controls::Prompt pr = royale::controls::PromptsFor(ControlScheme());
    std::vector<PromptChip> chips;
    if (gBusPhase == 1) {
        const bool open = h.state == royale::MatchState::Drop && h.bus.valid && royale::BusOverLand(h.bus, h.busTime);
        chips = { { pr.jump, open ? "Jump" : "Jump (soon)" }, { G::CRight, "Honk" } };
        if (CVarGetInteger(CVAR_SETTING("Royale.ButtonPrompts"), 1)) DrawPromptRow(dl, font, ds.x * 0.5f, ds.y - 164.0f * scale, scale, chips, true, open ? 255 : 150);
        if (h.state == royale::MatchState::Countdown) {
            const char* t = "All aboard the Moonfall Bus";
            const ImVec2 sz = font->CalcTextSizeA(24 * scale, FLT_MAX, 0.0f, t);
            dl->AddText(font, 24 * scale, ImVec2((ds.x - sz.x) * 0.5f + 1.5f, ds.y * 0.16f + 58 * scale + 1.5f), IM_COL32(0, 0, 0, 230), t);
            dl->AddText(font, 24 * scale, ImVec2((ds.x - sz.x) * 0.5f, ds.y * 0.16f + 58 * scale), IM_COL32(255, 236, 170, 255), t);
        }
        // The top line: where the bus is and how long until it has crossed the island.
        if (h.state == royale::MatchState::Drop && h.bus.valid) {
            const float close = royale::BusCloseSec(h.bus), left = std::max(0.0f, close - h.busTime);
            std::string line;
            const std::string over = BusOverPlace();
            if (!open && h.busTime < royale::BusOpenSec(h.bus)) line = "Doors open over the island";
            else if (!over.empty()) line = "Over " + over;
            if (!line.empty()) {
                const ImVec2 sz = font->CalcTextSizeA(24 * scale, FLT_MAX, 0.0f, line.c_str());
                dl->AddText(font, 24 * scale, ImVec2((ds.x - sz.x) * 0.5f + 1.5f, ds.y * 0.16f + 1.5f), IM_COL32(0, 0, 0, 230), line.c_str());
                dl->AddText(font, 24 * scale, ImVec2((ds.x - sz.x) * 0.5f, ds.y * 0.16f), IM_COL32(255, 236, 170, 255), line.c_str());
            }
            char t[48];
            const bool leaving = left <= 5.0f;
            std::snprintf(t, sizeof(t), leaving ? "The bus is leaving!  %d" : "Leaves the island in %d s", static_cast<int>(std::ceil(left)));
            const float ts = (leaving ? 26.0f : 18.0f) * scale;
            const ImVec2 sz = font->CalcTextSizeA(ts, FLT_MAX, 0.0f, t);
            const ImU32 col = leaving ? IM_COL32(200, 150, 255, 255) : IM_COL32(235, 235, 235, 230);
            dl->AddText(font, ts, ImVec2((ds.x - sz.x) * 0.5f + 1.5f, ds.y * 0.16f + 34 * scale + 1.5f), IM_COL32(0, 0, 0, 220), t);
            dl->AddText(font, ts, ImVec2((ds.x - sz.x) * 0.5f, ds.y * 0.16f + 34 * scale), col, t);
        }
    } else {
        chips = { { pr.jump, "Open glider" } };
        if (CVarGetInteger(CVAR_SETTING("Royale.ButtonPrompts"), 1)) DrawPromptRow(dl, font, ds.x * 0.5f, ds.y - 164.0f * scale, scale, chips);
    }
    // Touch: the round JUMP button is the bus's jump and the glider's opener here.
    if (!CVarGetInteger(CVAR_SETTING("Royale.TouchJump"), 1)) return;
    ImGuiIO& io = ImGui::GetIO();
    const float rad = 30.0f * scale;
    const ImVec2 c(ds.x - 18.0f * scale - 64.0f * scale, ds.y - 40.0f * scale - 24.0f * scale - 18.0f * scale - rad);
    const bool over = std::hypot(io.MousePos.x - c.x, io.MousePos.y - c.y) <= rad;
    const bool down = over && ImGui::IsMouseDown(0) && !io.WantCaptureMouse;
    dl->AddCircleFilled(ImVec2(c.x + 2 * scale, c.y + 3 * scale), rad, IM_COL32(6, 15, 36, 192), 28);
    dl->AddCircleFilled(c, rad, OotPanel(down ? 255 : 205), 28);
    dl->AddCircle(c, rad, HudColor(down ? royale::wartable::theme::Focus : royale::wartable::theme::Gold), 28, 2.5f * scale);
    DrawPadGlyph(dl, font, ImVec2(c.x, c.y - 7.0f * scale), 13.0f * scale, pr.jump);
    const char* label = gBusPhase == 1 ? "JUMP" : "GLIDE";
    const ImVec2 tsz = font->CalcTextSizeA(12.0f * scale, FLT_MAX, 0, label);
    dl->AddText(font, 12.0f * scale, ImVec2(c.x - tsz.x * 0.5f, c.y + 9.0f * scale), IM_COL32(240, 236, 220, 255), label);
    if (over && ImGui::IsMouseClicked(0) && !io.WantCaptureMouse) gBusJumpPressed = true;
}

// On the minimap while the bus flies: its line as a dashed cream line and the bus as a little moon moving along it.
void DrawBusOnMap(ImDrawList* dl, float scale, const std::function<ImVec2(float, float)>& toMap, const royale::HudState& h) {
    if (!h.bus.valid || !(h.state == royale::MatchState::Countdown || h.state == royale::MatchState::Drop)) return;
    const ImU32 cream = IM_COL32(240, 226, 176, 230);
    const int dashes = 28;
    for (int i = 0; i < dashes; i += 2) {
        const royale::Vec2 a = royale::BusPosAt(h.bus, royale::BusCrossSec(h.bus) * i / dashes);
        const royale::Vec2 b = royale::BusPosAt(h.bus, royale::BusCrossSec(h.bus) * (i + 1) / dashes);
        dl->AddLine(toMap(a.x, a.z), toMap(b.x, b.z), IM_COL32(0, 0, 0, 140), 3.4f * scale);
        dl->AddLine(toMap(a.x, a.z), toMap(b.x, b.z), cream, 2.0f * scale);
    }
    const royale::Vec2 at = royale::BusPosAt(h.bus, h.state == royale::MatchState::Drop ? h.busTime : 0.0f);
    const ImVec2 p = toMap(at.x, at.z);
    const float r = 5.5f * scale;
    dl->AddCircleFilled(p, r + 1.5f * scale, IM_COL32(0, 0, 0, 200), 16);
    dl->AddCircleFilled(p, r, IM_COL32(240, 226, 176, 255), 16);                          // the Moon balloon
    dl->AddCircleFilled(ImVec2(p.x - r * 0.35f, p.y - r * 0.2f), r * 0.22f, IM_COL32(173, 55, 37, 255), 8);   // its eyes
    dl->AddCircleFilled(ImVec2(p.x + r * 0.35f, p.y - r * 0.2f), r * 0.22f, IM_COL32(173, 55, 37, 255), 8);
    dl->AddLine(ImVec2(p.x - r * 0.45f, p.y + r * 0.35f), ImVec2(p.x + r * 0.45f, p.y + r * 0.35f), IM_COL32(120, 100, 70, 255), 1.2f * scale);
}

// Every frame (from OnPlayerUpdate): the bus's frame and its actor.
void UpdateBattleBus(const royale::HudState& hud) {
    UpdateBusFrame(hud);
    ReconcileBusActor();
    if (hud.state == royale::MatchState::Lobby || hud.state == royale::MatchState::Ending) { gBusPhase = 0; gBusFlyOnUntil = 0; }
}
