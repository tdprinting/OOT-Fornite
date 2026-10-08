// ---- Link's clothes ------------------------------------------------------------------------------------------------------------
// The game calls us while it draws Link's limbs (patches add the hooks: 0009 for the cap, 0015 for the rest). The tail of the cap, the skirt
// of the tunic and the sheath on its strap each swing on a spring pushed by the air, which is the wind plus how fast Link is moving, running
// or falling, and they lag behind when he starts, stops, turns or lands. Everyone's clothes do it: the local player, the other players and
// the bots (they are all drawn as Player actors).
struct HatState {
    royale::HatSpring spring;                  // the cap's tail
    royale::ClothSwing skirt, sheath;          // the tunic's skirt (the waist limb) and the sheath
    float lx = 0, ly = 0, lz = 0;
    float lvx = 0, lvz = 0, lvy = 0;   // last velocity, to feel the acceleration
    int16_t lyaw = 0;                  // last facing, to feel him turning
    double lastT = 0, seen = 0;
    bool have = false;
};
std::unordered_map<const void*, HatState> gHats;
void TugLinkCap(const Player* pl, float side) {
    auto it = gHats.find(pl);
    if (it == gHats.end()) return;
    it->second.spring.vFore += 3.0f;
    it->second.spring.vSide += side;
}
int gClothLimbCalls = 0;       // how many times the game has asked about the tunic (shown in the menu, to prove the hook is wired)
float gSkirtLastSwing = 0.0f;  // the last skirt swing applied, in degrees

// Steps every piece of one player's clothing, once per frame however many times the limbs are drawn.
HatState& StepClothes(const Player* pl) {
    const double now = ImGui::GetTime();
    HatState& h = gHats[pl];
    const float x = pl->actor.world.pos.x, y = pl->actor.world.pos.y, z = pl->actor.world.pos.z;
    if (!h.have) { h.lx = x; h.ly = y; h.lz = z; h.lastT = now; h.lyaw = pl->actor.shape.rot.y; h.have = true; }
    h.seen = now;
    const float dt = static_cast<float>(now - h.lastT);
    if (dt > 0.004f) {   // (the limbs are drawn more than once a frame sometimes: only step when time has passed)
        const float vx = std::clamp((x - h.lx) / dt, -900.0f, 900.0f), vy = std::clamp((y - h.ly) / dt, -1500.0f, 1500.0f), vz = std::clamp((z - h.lz) / dt, -900.0f, 900.0f);
        h.lx = x; h.ly = y; h.lz = z; h.lastT = now;
        float wx, wz, wind;
        WindNow(&wx, &wz, &wind);
        const float ax = wx - vx, az = wz - vz;
        const float yaw = pl->actor.shape.rot.y * (3.14159265f / 32768.0f), c = std::cos(yaw), sn = std::sin(yaw);
        // The air in Link's frame: x to his left, z in front of him. (Running forward makes air stream back over the cap.)
        // Link's own acceleration (in his frame) kicks the cloth the opposite way: it lags behind a start, and swings forward when he stops or lands.
        const float dvx = vx - h.lvx, dvz = vz - h.lvz, dvy = vy - h.lvy;
        h.lvx = vx; h.lvz = vz; h.lvy = vy;
        // How fast he is turning (radians a second, positive to his left): the cap drags and rolls behind a turn and the skirt swings out.
        const int16_t yawNow = pl->actor.shape.rot.y;
        const float turn = std::clamp(static_cast<int16_t>(yawNow - h.lyaw) * (3.14159265f / 32768.0f) / dt, -14.0f, 14.0f) * gClothScale;
        h.lyaw = yawNow;
        const float accSide = dvx * c - dvz * sn, accFore = dvx * sn + dvz * c;
        const float airX = (ax * c - az * sn) * gClothScale, airZ = (ax * sn + az * c) * gClothScale;
        const float step = std::min(dt, 0.1f), t = static_cast<float>(now);
        const float phase = static_cast<float>(reinterpret_cast<uintptr_t>(pl) % 61);
        h.spring.Step(step, airX, airZ, -vy * gClothScale, wind * gClothScale, t, phase,
                      (accFore * 0.0035f + dvy * 0.0016f) * gClothScale, accSide * 0.0035f * gClothScale, turn);
        // Ground speed drives the stride's flap. In the water or hanging on a ledge, the stride is not a run, so it doesn't flap.
        const bool stride = (pl->actor.bgCheckFlags & 1) && !(pl->stateFlags1 & (PLAYER_STATE1_IN_WATER | PLAYER_STATE1_HANGING_OFF_LEDGE | PLAYER_STATE1_CLIMBING_LADDER));
        const float ground = stride ? std::sqrt(vx * vx + vz * vz) * gClothScale : 0.0f;
        const float fall = std::max(0.0f, -vy) * gClothScale;
        // A landing (a sudden stop in falling) throws the skirt up-and-forward too.
        const float landKick = std::max(0.0f, dvy) * 0.6f;
        h.skirt.Step(royale::kSkirtSwing, step, airX, airZ, ground, fall, wind * gClothScale, t, phase, (accFore - landKick) * gClothScale, accSide * gClothScale, turn);
        h.sheath.Step(royale::kSheathSwing, step, airX, airZ, ground, fall, wind * gClothScale, t, phase + 2.0f, (accFore - landKick) * gClothScale, accSide * gClothScale, turn);
    }
    return h;
}

// Bots are drawn as Player actors too, but their clothes (and the wind on them) are left alone: only people's clothes move.
bool IsBotActor(const void* actor) {
    const auto id = gPuppetOf.find(static_cast<const Actor*>(actor));
    if (id == gPuppetOf.end()) return false;
    const auto st = gState.find(id->second);
    return st != gState.end() && st->second.isBot;
}

// Wrap equipment in an explicit material scope. The wrapper and sunlight
// snapshot live in Claude's double-buffered graphics pool, like character data.
void OnPlayerItemMaterial(void* playPtr, void* playerPtr, int32_t limb, void* dListPtr) {
    if (!gSession.Client() || !playPtr || !playerPtr || !dListPtr) return;
    PlayState* play = static_cast<PlayState*>(playPtr);
    Player* player = static_cast<Player*>(playerPtr);
    Gfx** dList = static_cast<Gfx**>(dListPtr);
    if (!*dList) return;
    const int normals = std::clamp(CVarGetInteger(CVAR_SETTING("Royale.ItemNormals"), 100), 0, 200);
    const int bumps = std::clamp(CVarGetInteger(CVAR_SETTING("Royale.ItemBumps"), 50), 0, 200);
    if (!normals && !bumps) return;
    using namespace royale::item_surface;
    const uint8_t* normal = nullptr;
    const uint8_t* height = nullptr;
    float scale = 1.0f / 2000.0f;
    if (limb == PLAYER_LIMB_L_HAND &&
        (player->leftHandType == PLAYER_MODELTYPE_LH_SWORD || player->leftHandType == PLAYER_MODELTYPE_LH_SWORD_2 ||
         player->leftHandType == PLAYER_MODELTYPE_LH_BGS)) {
        switch (player->heldItemAction) {
            case PLAYER_IA_SWORD_KOKIRI: normal = kokiri_sword_normal; height = kokiri_sword_bump; break;
            case PLAYER_IA_SWORD_MASTER: normal = master_sword_normal; height = master_sword_bump; break;
            case PLAYER_IA_SWORD_BIGGORON: normal = biggoron_sword_normal; height = biggoron_sword_bump; break;
            default: return;
        }
        scale = 1.0f / 4000.0f;
    } else if ((limb == PLAYER_LIMB_R_HAND && player->rightHandType == PLAYER_MODELTYPE_RH_SHIELD) ||
               limb == PLAYER_LIMB_SHEATH) {
        switch (player->currentShield) {
            case PLAYER_SHIELD_DEKU: normal = deku_shield_normal; height = deku_shield_bump; break;
            case PLAYER_SHIELD_HYLIAN: normal = hylian_shield_normal; height = hylian_shield_bump; break;
            case PLAYER_SHIELD_MIRROR: normal = mirror_shield_normal; height = mirror_shield_bump; break;
            default: return;
        }
    } else return;
    struct ItemDraw { GfxSurfaceMap material; Gfx commands[4]; };
    ItemDraw* draw = static_cast<ItemDraw*>(FrameAlloc(play, sizeof(ItemDraw)));
    if (!draw) return;
    draw->material = {};
    draw->material.normal = normal;
    draw->material.height = height;
    draw->material.width = draw->material.heightPixels = kSize;
    draw->material.normalStrength = normals / 100.0f;
    draw->material.bumpStrength = bumps / 100.0f;
    draw->material.uvScale = scale;
    const auto& lighting = play->envCtx.lightSettings;
    for (int k = 0; k < 3; ++k) {
        draw->material.sunDirection[k] = lighting.light1Dir[k] / 127.0f;
        draw->material.sunColor[k] = lighting.light1Color[k] / 255.0f;
        draw->material.ambientColor[k] = lighting.ambientColor[k] / 255.0f;
    }
    gSPSurfaceMap(&draw->commands[0], reinterpret_cast<uintptr_t>(&draw->material));
    gSPDisplayList(&draw->commands[1], *dList);
    gSPSurfaceMap(&draw->commands[2], 0);
    gSPEndDisplayList(&draw->commands[3]);
    *dList = draw->commands;
}

void OnPlayerHatLimb(void* playerPtr, int16_t* rot) {
    Feat("cap cloth");
    if (!DebugOn(kDbgCloth)) return;
    gHatHookCalls++;
    if (gClothScale <= 0.01f || playerPtr == nullptr || !InGame() || IsBotActor(playerPtr)) return;
    HatState& h = StepClothes(static_cast<const Player*>(playerPtr));
    const float toBinary = 32768.0f / 3.14159265f;
    gHatLastSwing = h.spring.fore * 57.2958f;
    rot[2] = static_cast<int16_t>(rot[2] + static_cast<int>(h.spring.fore * toBinary));   // fore and aft: the limb's pitch
    rot[1] = static_cast<int16_t>(rot[1] + static_cast<int>(h.spring.side * toBinary));   // sideways: its yaw
    rot[0] = static_cast<int16_t>(rot[0] + static_cast<int>(h.spring.twist * toBinary));  // roll along its length, thrown by turning
}

// The tunic's skirt is part of the waist limb, which every other limb hangs from. So the waist is swung, and each limb hanging straight off it
// turns back by exactly the same amount before its own joint is applied: the skirt sways, the legs and the upper body stay where the
// animation put them. Which limbs hang off the waist is read from the skeleton itself (custom Link models may differ), once per skeleton.
struct WaistSwing {
    const void* player = nullptr;
    s16 before[3] = {}, after[3] = {};
    bool active = false;
};
WaistSwing gWaistSwing;
std::unordered_map<const void*, uint32_t> gWaistChildren;   // skeleton -> bit mask of the limbs hanging straight off the waist

uint32_t WaistChildrenOf(const Player* pl) {
    void** skel = pl->skelAnime.skeleton;
    if (skel == nullptr) return 0;
    auto it = gWaistChildren.find(skel);
    if (it != gWaistChildren.end()) return it->second;
    uint32_t mask = 0;
    const int count = std::min<int>(pl->skelAnime.limbCount, 31);
    const LodLimb* waist = static_cast<const LodLimb*>(SEGMENTED_TO_VIRTUAL(skel[PLAYER_LIMB_WAIST - 1]));
    if (waist != nullptr && count >= PLAYER_LIMB_WAIST) {
        int child = waist->child;
        for (int guard = 0; child != LIMB_DONE && child < count && guard < 32; guard++) {
            mask |= 1u << (child + 1);   // the skeleton counts from 0, the draw hooks from 1
            const LodLimb* limb = static_cast<const LodLimb*>(SEGMENTED_TO_VIRTUAL(skel[child]));
            if (limb == nullptr) break;
            child = limb->sibling;
        }
    }
    if (gWaistChildren.size() > 16) gWaistChildren.clear();
    gWaistChildren[skel] = mask;
    return mask;
}

bool SheathHasChildren(const Player* pl) {
    void** skel = pl->skelAnime.skeleton;
    if (skel == nullptr || pl->skelAnime.limbCount < PLAYER_LIMB_SHEATH) return true;
    const LodLimb* sheath = static_cast<const LodLimb*>(SEGMENTED_TO_VIRTUAL(skel[PLAYER_LIMB_SHEATH - 1]));
    return sheath == nullptr || sheath->child != LIMB_DONE;
}

// ---- the Gilded Sword in the hand and on the back (patch 0023) ---------------------------------------------------------------------------------
// Link's own sword limbs ask the mod about every sword they are about to draw. For the Gilded Sword (the player in gGildedPlayer) the hand draws
// an empty fist, the sheath is drawn without the Master Sword in it, and the sword comes from shared/gilded_sword_model.h (made in Blender: see
// tools/gilded_sword). The moves, the sounds and the hit trail are the Master Sword's own: only the blade's length (the trail follows it) is ours.
// what: 0 = how long is the custom blade (limb units; stays 0 when this player holds no custom sword), 1 = draw it in the hand (the hand limb's
// matrix is current), 2 = draw it stowed on the back (the sheath limb's matrix is current).
constexpr float kGildedBladeBase = royale::gilded_sword_model::kBladeStart;   // the blade starts here along the model's X; the game's own blade starts at the hand's X = 0
constexpr float kGildedGripLift = 400.0f;      // the game's blades run along y = 400 of the hand limb (see func_80090A28 in z_player_lib.c)
constexpr float kGildedHandRoll = 0.0f;        // turn about the blade, radians: change this if the guard is seen edge-on
constexpr float kGildedBackX = 1000.0f, kGildedBackY = 300.0f, kGildedBackZ = 0.0f, kGildedBackTurn = 2.55f;   // where and how it rests on the back (the sheath limb)

// The sword is drawn lit by the same lights as Link himself, one run of triangles per (surface class, material): the material's colour is the primitive
// colour, the triangles carry their outward normals. Each surface class has its own normal and bump map (shared/gilded_sword_surface.h), set up the
// way the game's own sword and shield maps are (OnPlayerItemMaterial, libultraship patch 0003), so the blade shows its engraved diamonds in the sun.
struct GildedGroup { int cls = 0, mat = 0; std::vector<Vtx> vtx; std::vector<Gfx> dl; };
struct GildedGpu { std::vector<GildedGroup> groups; bool built = false; };
GildedGpu gGildedGpu[2];
std::unique_ptr<royale::gilded_surface::Map> gGildedMaps[4];

const GildedGpu& GildedMeshFor(int variant) {
    namespace gm = royale::gilded_sword_model;
    GildedGpu& g = gGildedGpu[variant];
    if (g.built) return g;
    g.built = true;
    g.groups.reserve(16);   // (never grows past this: the display lists point into each group)
    auto add = [&](const gm::Tri* tris, int count) {
        for (int i = 0; i < count; i++) {
            GildedGroup* grp = nullptr;
            for (GildedGroup& e : g.groups) if (e.cls == tris[i].cls && e.mat == tris[i].mat) { grp = &e; break; }
            if (grp == nullptr) { g.groups.emplace_back(); grp = &g.groups.back(); grp->cls = tris[i].cls; grp->mat = tris[i].mat; }
            for (int k = 0; k < 3; k++) {
                Vtx v{};
                v.n.ob[0] = tris[i].p[k * 3]; v.n.ob[1] = tris[i].p[k * 3 + 1]; v.n.ob[2] = tris[i].p[k * 3 + 2];
                v.n.flag = 0; v.n.tc[0] = v.n.tc[1] = 0;
                v.n.n[0] = tris[i].n[0]; v.n.n[1] = tris[i].n[1]; v.n.n[2] = tris[i].n[2];
                v.n.a = 255;
                grp->vtx.push_back(v);
            }
        }
    };
    add(gm::kHilt, gm::kHiltCount);
    if (variant == 0) add(gm::kBlade, gm::kBladeCount); else add(gm::kScabbard, gm::kScabbardCount);
    for (GildedGroup& e : g.groups) {
        const size_t verts = e.vtx.size(), batches = (verts / 3 + 9) / 10;
        e.dl.assign(verts / 3 + batches + 2, Gfx{});
        Gfx* d = e.dl.data();
        gDPSetPrimColor(d++, 0, 0, gm::kMatRgb[e.mat][0], gm::kMatRgb[e.mat][1], gm::kMatRgb[e.mat][2], 255);
        for (size_t first = 0; first < verts; first += 30) {
            const size_t n = std::min<size_t>(30, verts - first);
            gSPVertex(d++, reinterpret_cast<uintptr_t>(&e.vtx[first]), static_cast<int>(n), 0);
            for (size_t t = 0; t + 2 < n; t += 3) gSP1Triangle(d++, static_cast<int>(t), static_cast<int>(t + 1), static_cast<int>(t + 2), 0);
        }
        gSPEndDisplayList(d++);
        e.dl.resize(static_cast<size_t>(d - e.dl.data()));
    }
    return g;
}

void DrawGildedMesh(PlayState* play, int variant) {
    const GildedGpu& mesh = GildedMeshFor(variant);
    const int normals = std::clamp(CVarGetInteger(CVAR_SETTING("Royale.ItemNormals"), 100), 0, 200);
    const int bumps = std::clamp(CVarGetInteger(CVAR_SETTING("Royale.ItemBumps"), 50), 0, 200);
    const bool surfaces = normals != 0 || bumps != 0;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_CULL_BACK);   // (lit by the game's lights; both sides of the blade show)
    gDPSetCombineLERP(POLY_OPA_DISP++, SHADE, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, SHADE, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE);   // light x the material's colour
    for (int cls = 0; cls < 4; cls++) {
        bool any = false;
        for (const GildedGroup& e : mesh.groups) any |= e.cls == cls;
        if (!any) continue;
        if (surfaces) {
            if (!gGildedMaps[cls]) gGildedMaps[cls] = std::make_unique<royale::gilded_surface::Map>(royale::gilded_surface::Build(static_cast<royale::gilded_surface::Class>(cls)));
            const royale::gilded_surface::Map& map = *gGildedMaps[cls];
            GfxSurfaceMap* material = static_cast<GfxSurfaceMap*>(FrameAlloc(play, sizeof(GfxSurfaceMap)));
            if (material != nullptr) {
                *material = {};
                material->normal = map.normal.data();
                material->height = map.height.data();
                material->width = material->heightPixels = static_cast<uint32_t>(map.size);
                material->normalStrength = normals / 100.0f;
                material->bumpStrength = bumps / 100.0f;
                material->uvScale = map.uvScale;
                const auto& lighting = play->envCtx.lightSettings;
                for (int k = 0; k < 3; ++k) {
                    material->sunDirection[k] = lighting.light1Dir[k] / 127.0f;
                    material->sunColor[k] = lighting.light1Color[k] / 255.0f;
                    material->ambientColor[k] = lighting.ambientColor[k] / 255.0f;
                }
                gSPSurfaceMap(POLY_OPA_DISP++, reinterpret_cast<uintptr_t>(material));
            }
        }
        for (const GildedGroup& e : mesh.groups) if (e.cls == cls) gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(e.dl.data()));
        if (surfaces) gSPSurfaceMap(POLY_OPA_DISP++, 0);
    }
    Gfx_SetupDL_25Opa(play->state.gfxCtx);                              // and the game's own limbs after it get the state they expect
    CLOSE_DISPS(play->state.gfxCtx);
}

void GildedGlint(PlayState* play, const Vec3f& at, s16 scale, s16 life) {
    Vec3f pos = at, vel = { 0.0f, 0.5f, 0.0f }, accel = { 0.0f, -0.02f, 0.0f };
    Color_RGBA8 prim = { 255, 238, 150, 255 }, env = { 255, 176, 40, 255 };
    EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, scale, life);
}

void OnPlayerCustomSword(void* playPtr, void* playerPtr, int32_t what, int32_t* value) {
    Feat("gilded sword");
    if (value == nullptr || playerPtr == nullptr || static_cast<const Player*>(playerPtr) != gGildedPlayer) return;
    namespace gm = royale::gilded_sword_model;
    if (what == 0) { *value = static_cast<int32_t>(gm::kBladeTip - gm::kBladeStart); return; }
    PlayState* play = static_cast<PlayState*>(playPtr);
    const Player* pl = static_cast<const Player*>(playerPtr);
    Matrix_Push();
    if (what == 1) {
        Matrix_Translate(-kGildedBladeBase, kGildedGripLift, 0.0f, MTXMODE_APPLY);
        if (kGildedHandRoll != 0.0f) Matrix_RotateX(kGildedHandRoll, MTXMODE_APPLY);
        // gold glints: along the blade now and then, and a burst at the point while it swings
        if (pl->meleeWeaponState != 0) {
            Vec3f tip = { gm::kBladeTip, 0.0f, 0.0f }, world;
            Matrix_MultVec3f(&tip, &world);
            GildedGlint(play, world, 80, 12);
            if (Rand_ZeroOne() < 0.5f) { Vec3f mid = { gm::kBladeStart + (gm::kBladeTip - gm::kBladeStart) * (0.35f + 0.5f * Rand_ZeroOne()), 0.0f, 0.0f }; Matrix_MultVec3f(&mid, &world); GildedGlint(play, world, 50, 10); }
        } else if (Rand_ZeroOne() < 0.02f) {
            Vec3f spot = { gm::kBladeStart + (gm::kBladeTip - gm::kBladeStart) * Rand_ZeroOne(), 0.0f, 0.0f }, world;
            Matrix_MultVec3f(&spot, &world);
            GildedGlint(play, world, 36, 16);
        }
        DrawGildedMesh(play, 0);
    } else if (what == 2) {
        Matrix_Translate(kGildedBackX, kGildedBackY, kGildedBackZ, MTXMODE_APPLY);
        Matrix_RotateY(kGildedBackTurn, MTXMODE_APPLY);
        DrawGildedMesh(play, 1);
    }
    Matrix_Pop();
}

void OnPlayerClothLimb(void* playerPtr, int32_t limbIndex, int16_t* rot) {
    Feat("tunic and sheath cloth");
    if (!DebugOn(kDbgCloth)) return;
    if (limbIndex == PLAYER_LIMB_WAIST) {
        gClothLimbCalls++;
        gWaistSwing.active = false;
        if (gClothScale <= 0.01f || playerPtr == nullptr || !InGame() || IsBotActor(playerPtr)) return;
        const Player* pl = static_cast<const Player*>(playerPtr);
        if (WaistChildrenOf(pl) == 0) return;   // can't tell what hangs off the waist: leave this model alone
        HatState& h = StepClothes(pl);
        const float toBinary = 32768.0f / 3.14159265f;
        gSkirtLastSwing = h.skirt.fore * 57.2958f;
        gWaistSwing.player = playerPtr;
        for (int i = 0; i < 3; i++) gWaistSwing.before[i] = rot[i];
        rot[2] = static_cast<int16_t>(rot[2] + static_cast<int>(h.skirt.fore * toBinary));   // fore and aft
        rot[1] = static_cast<int16_t>(rot[1] + static_cast<int>(h.skirt.side * toBinary));   // sideways
        for (int i = 0; i < 3; i++) gWaistSwing.after[i] = rot[i];
        gWaistSwing.active = true;
        return;
    }
    if (!gWaistSwing.active || playerPtr == nullptr) return;
    if (limbIndex == PLAYER_LIMB_SHEATH && gWaistSwing.player == playerPtr && !SheathHasChildren(static_cast<const Player*>(playerPtr))) {
        auto it = gHats.find(playerPtr);
        if (it != gHats.end()) {
            const float toBinary = 32768.0f / 3.14159265f;
            rot[2] = static_cast<int16_t>(rot[2] + static_cast<int>(it->second.sheath.fore * toBinary));
            rot[1] = static_cast<int16_t>(rot[1] + static_cast<int>(it->second.sheath.side * toBinary));
        }
    }
    if (gWaistSwing.player != playerPtr || limbIndex < 0 || limbIndex > 31) return;
    if (!(WaistChildrenOf(static_cast<const Player*>(playerPtr)) & (1u << limbIndex))) return;
    // Undo the waist's swung joint rotation (it was applied Z, then Y, then X), then apply the unswung one: this limb's frame is exactly
    // what it would have been.
    const float toRad = 3.14159265f / 32768.0f;
    Matrix_RotateX(-gWaistSwing.after[0] * toRad, MTXMODE_APPLY);
    Matrix_RotateY(-gWaistSwing.after[1] * toRad, MTXMODE_APPLY);
    Matrix_RotateZ(-gWaistSwing.after[2] * toRad, MTXMODE_APPLY);
    Matrix_RotateZYX(gWaistSwing.before[0], gWaistSwing.before[1], gWaistSwing.before[2], MTXMODE_APPLY);
}

void ForgetOldHats() {
    const double now = ImGui::GetTime();
    for (auto it = gHats.begin(); it != gHats.end();) it = now - it->second.seen > 4.0 ? gHats.erase(it) : std::next(it);
}

