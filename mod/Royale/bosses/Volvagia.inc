// ---- the glider, the custom dragon and the major bosses ----------------------------------------------------------------------------

constexpr float kDragonDrawScale = 0.55f; // the stand-in block dragon is 1200 across with its wings out

void Dragon_DrawBlocks(Actor* actor, PlayState* play, const BossActor& b) {
    const royale::BossKind kind = static_cast<royale::BossKind>(b.kind);
    const uint32_t theme = static_cast<uint32_t>(kind) - static_cast<uint32_t>(royale::BossKind::DragonFire);
    const float t = static_cast<float>(play->gameplayFrames) / royale::kTickHz;
    // Wings: a steady beat while it flies (up, level, down, level), folded down when it has landed.
    static const uint32_t kBeat[4] = { 0, 1, 2, 1 };
    const bool landed = b.mode == static_cast<int>(royale::DragonMode::Landed);
    const float rate = b.mode == static_cast<int>(royale::DragonMode::Swoop) ? 9.0f : 4.5f;
    const uint32_t pose = landed ? 2u : kBeat[static_cast<int>(t * rate) & 3];
    const GpuMesh* mesh = GpuMeshFor(royale::MeshKind::Dragon, pose + 4u * theme);
    if (mesh == nullptr || mesh->dl.empty()) return;
    const float bob = landed ? 0.0f : std::sin(t * 2.2f) * 14.0f;
    float pitch = 0.0f;                                                  // nose down in a dive, up as it climbs
    if (b.mode == static_cast<int>(royale::DragonMode::Swoop)) pitch = 0.5f;
    else if (b.mode == static_cast<int>(royale::DragonMode::Climb)) pitch = -0.35f;
    else if (b.mode == static_cast<int>(royale::DragonMode::Breath)) pitch = 0.18f;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y + bob, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(actor->shape.rot.y * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    Matrix_RotateX(pitch, MTXMODE_APPLY);
    Matrix_Scale(kDragonDrawScale, kDragonDrawScale, kDragonDrawScale, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(mesh->dl.data()));
    CLOSE_DISPS(play->state.gfxCtx);
}

// Volvagia, the Fire Temple's dragon, with its real skeleton, eyes, scrolling-lava skin and animations: its idle sway, its fire-breathing, its claw
// swipe, burrowing into the ground and bursting back out of it, and the vulnerable pose when it comes down.
const char* DragonAnim(int mode) {
    switch (static_cast<royale::DragonMode>(mode)) {
        case royale::DragonMode::Breath: return gHoleVolvagiaBreatheFireAnim;
        case royale::DragonMode::Cast: return gHoleVolvagiaClawSwipeAnim;
        case royale::DragonMode::Swoop: return gHoleVolvagiaHitAnim;
        case royale::DragonMode::Landed: return gHoleVolvagiaVulnerableAnim;
        case royale::DragonMode::Climb: return gHoleVolvagiaTurnAnim;
        case royale::DragonMode::Emerge: return gHoleVolvagiaEmergeAnim;
        case royale::DragonMode::Hidden: return gHoleVolvagiaBurrowAnim;
        default: return gHoleVolvagiaIdleAnim;
    }
}

void Volvagia_Update(PlayState* play, BossActor& b) {
    if (!b.skReady) {
        BossInitSkel(play, &b.sk, gHoleVolvagiaSkel, true, gHoleVolvagiaIdleAnim);
        b.playing = gHoleVolvagiaIdleAnim;
        b.skReady = true;
        b.lastHp = b.hp;
    }
    const char* want = b.hurtAge < 0.5f && b.mode != static_cast<int>(royale::DragonMode::Breath) ? gHoleVolvagiaDamagedAnim : DragonAnim(b.mode);
    const bool loopIt = want == gHoleVolvagiaIdleAnim || want == gHoleVolvagiaVulnerableAnim;
    BossPlay(&b.sk, &b.playing, want, 1.0f, loopIt, b.modeFresh && !loopIt, -6.0f);
    SkelAnime_Update(&b.sk);
}

float gDragonJaw = 0.0f;
s32 Dragon_OverrideLimb(PlayState* play, s32 limb, Gfx**, Vec3f*, Vec3s* rot, void*) {
    switch (limb) {
        case 35: case 36: rot->z = static_cast<s16>(rot->z - gDragonJaw * 0.1f); break;
        case 32: rot->z = static_cast<s16>(rot->z + gDragonJaw); break;
        default: break;
    }
    if (limb == 32 || limb == 35 || limb == 36) {
        OPEN_DISPS(play->state.gfxCtx);
        gDPPipeSync(POLY_OPA_DISP++);
        gDPSetEnvColor(POLY_OPA_DISP++, 255, 255, 255, 0);
        CLOSE_DISPS(play->state.gfxCtx);
    }
    return 0;
}

// ---- custom model files (Volvagia) -----------------------------------------------------------------------------------------------
// Put dragon.obj (and its dragon.mtl) in the "models" folder inside the game's data folder and it replaces Volvagia. Parts are animated by their names
// in the file: "wing" (flaps; left or right comes from which side of the body it is on), "jaw" (opens when it breathes fire), "tail" (sways) and "head"
// (nods); everything else is the body. A model that is one piece still bobs, banks and tilts. An optional dragon.cfg changes how it is fitted:
//   scale=1.0   size multiplier      yaw=0   degrees to turn it so its nose points along the flight direction (try 180 or 90)
//   lift=0      raise it             flap=35 how far the wings beat, in degrees
// The game draws these with vertex colours and its own lighting, so colours come from the .mtl (Kd) or from per-vertex colours; textures are not used.
struct CustomPart {
    std::unique_ptr<GpuMesh> gpu;
    royale::ObjRole role = royale::ObjRole::Body;
    float centre[3] = {0, 0, 0}, mn[3] = {0, 0, 0}, mx[3] = {0, 0, 0};
    float side = 1.0f;   // wings: +1 on the +x side, -1 on the other
};
struct CustomModel {
    bool tried = false, ok = false;
    std::string status = "No custom dragon: put dragon.obj in the models folder";
    std::vector<CustomPart> parts;
    float flapDegrees = 35.0f;
    size_t triangles = 0;
};
CustomModel gDragonModel;

std::filesystem::path ModelsFolder() { return std::filesystem::path(Ship::Context::GetPathRelativeToAppDirectory("models")); }

bool ReadWholeFile(const std::filesystem::path& file, std::string* out) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    *out = ss.str();
    return true;
}

void LoadCustomDragon() {
    CustomModel& cm = gDragonModel;
    cm = CustomModel{};
    cm.tried = true;
    std::error_code ec;
    std::filesystem::create_directories(ModelsFolder(), ec);
    std::string obj, mtl, cfg;
    if (!ReadWholeFile(ModelsFolder() / "dragon.obj", &obj)) { cm.status = "No custom dragon: put dragon.obj in " + ModelsFolder().string(); return; }
    ReadWholeFile(ModelsFolder() / "dragon.mtl", &mtl);
    ReadWholeFile(ModelsFolder() / "dragon.cfg", &cfg);
    float extra = 1.0f, yaw = 0.0f, lift = 0.0f;
    {
        std::istringstream in(cfg);
        std::string line;
        while (std::getline(in, line)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = line.substr(0, eq);
            const float val = static_cast<float>(std::atof(line.c_str() + eq + 1));
            if (key == "scale") extra = std::clamp(val, 0.1f, 10.0f);
            else if (key == "yaw") yaw = val;
            else if (key == "lift") lift = val;
            else if (key == "flap") cm.flapDegrees = std::clamp(val, 0.0f, 80.0f);
        }
    }
    royale::ObjModel model = royale::ParseObj(obj, mtl);
    if (!model.ok) { cm.status = "dragon.obj could not be used: " + model.error; return; }
    royale::FitObjModel(model, 1200.0f, extra, yaw, lift);
    for (auto& part : model.parts) {
        CustomPart cp;
        cp.gpu = std::make_unique<GpuMesh>();
        if (!BuildGpuMesh(part.mesh, *cp.gpu)) continue;
        cp.role = royale::RoleOf(part.name);
        for (int i = 0; i < 3; i++) { cp.centre[i] = part.centre[i]; cp.mn[i] = part.mn[i]; cp.mx[i] = part.mx[i]; }
        cp.side = part.centre[0] >= 0 ? 1.0f : -1.0f;
        cm.parts.push_back(std::move(cp));
    }
    cm.triangles = model.triangles;
    cm.ok = !cm.parts.empty();
    int wings = 0, jaws = 0, tails = 0, heads = 0;
    for (const auto& p : cm.parts) { wings += p.role == royale::ObjRole::Wing; jaws += p.role == royale::ObjRole::Jaw; tails += p.role == royale::ObjRole::Tail; heads += p.role == royale::ObjRole::Head; }
    cm.status = "Custom dragon loaded: " + std::to_string(cm.triangles) + " triangles, " + std::to_string(cm.parts.size()) + " parts (" + std::to_string(wings) + " wing, " +
                std::to_string(jaws) + " jaw, " + std::to_string(tails) + " tail, " + std::to_string(heads) + " head)";
}

void DrawCustomDragon(Actor* actor, PlayState* play, const BossActor& b) {
    const CustomModel& cm = gDragonModel;
    const float t = static_cast<float>(ImGui::GetTime());
    const bool landed = b.mode == static_cast<int>(royale::DragonMode::Landed);
    const bool breathing = b.mode == static_cast<int>(royale::DragonMode::Breath);
    const bool swoop = b.mode == static_cast<int>(royale::DragonMode::Swoop);
    const float rate = swoop ? 9.0f : 4.5f;
    const float flap = landed ? -0.5f : std::sin(t * rate) * cm.flapDegrees * 0.0174533f;
    float pitch = 0.0f;
    if (swoop) pitch = 0.5f; else if (b.mode == static_cast<int>(royale::DragonMode::Climb)) pitch = -0.3f; else if (breathing) pitch = 0.18f;
    const float bob = landed ? 0.0f : std::sin(t * 2.2f) * 14.0f;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    CLOSE_DISPS(play->state.gfxCtx);
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y + bob, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(actor->shape.rot.y * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    Matrix_RotateX(pitch, MTXMODE_APPLY);
    Matrix_RotateZ(landed ? 0.0f : std::sin(t * 1.3f) * 0.06f, MTXMODE_APPLY);   // a slow bank
    Matrix_Scale(kDragonDrawScale, kDragonDrawScale, kDragonDrawScale, MTXMODE_APPLY);
    for (const CustomPart& p : cm.parts) {
        Matrix_Push();
        switch (p.role) {
            case royale::ObjRole::Wing:   // flaps about the root, the edge nearest the body
                Matrix_Translate(p.side > 0 ? p.mn[0] : p.mx[0], p.centre[1], p.centre[2], MTXMODE_APPLY);
                Matrix_RotateZ(p.side * flap, MTXMODE_APPLY);
                Matrix_Translate(-(p.side > 0 ? p.mn[0] : p.mx[0]), -p.centre[1], -p.centre[2], MTXMODE_APPLY);
                break;
            case royale::ObjRole::Jaw:    // opens about its back edge
                Matrix_Translate(p.centre[0], p.centre[1], p.mn[2], MTXMODE_APPLY);
                Matrix_RotateX(breathing ? 0.55f + std::sin(t * 9.0f) * 0.08f : 0.05f + std::sin(t * 1.5f) * 0.04f, MTXMODE_APPLY);
                Matrix_Translate(-p.centre[0], -p.centre[1], -p.mn[2], MTXMODE_APPLY);
                break;
            case royale::ObjRole::Tail:   // sways from where it joins the body
                Matrix_Translate(p.centre[0], p.centre[1], p.mx[2], MTXMODE_APPLY);
                Matrix_RotateY(std::sin(t * 2.0f) * 0.3f, MTXMODE_APPLY);
                Matrix_Translate(-p.centre[0], -p.centre[1], -p.mx[2], MTXMODE_APPLY);
                break;
            case royale::ObjRole::Head:   // nods
                Matrix_Translate(p.centre[0], p.centre[1], p.mn[2], MTXMODE_APPLY);
                Matrix_RotateX((breathing ? 0.2f : 0.0f) + std::sin(t * 1.1f) * 0.06f, MTXMODE_APPLY);
                Matrix_Translate(-p.centre[0], -p.centre[1], -p.mn[2], MTXMODE_APPLY);
                break;
            default: break;
        }
        OPEN_DISPS(play->state.gfxCtx);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(p.gpu->dl.data()));
        CLOSE_DISPS(play->state.gfxCtx);
        Matrix_Pop();
    }
}

void Volvagia_Draw(Actor* actor, PlayState* play, const BossActor& b) {
    if (ModeOf(b) == BM::Hidden) return;   // under the ground: only the lava bubbling where it is shows (UpdateBossBodyFx)
    if (!gDragonModel.tried) LoadCustomDragon();
    if (gDragonModel.ok) { DrawCustomDragon(actor, play, b); return; }
    if (!b.skReady) { Dragon_DrawBlocks(actor, play, b); return; }
    static const char* eyes[3] = { gHoleVolvagiaEyeOpenTex, gHoleVolvagiaEyeHalfTex, gHoleVolvagiaEyeClosedTex };
    const float t = static_cast<float>(play->gameplayFrames);
    const BM mode = ModeOf(b);
    const bool grounded = mode == BM::Landed || mode == BM::Emerge;
    const float bob = grounded ? 0.0f : std::sin(t * 0.11f) * 14.0f;
    float pitch = 0.0f;
    if (mode == BM::Swoop) pitch = 0.5f;
    else if (mode == BM::Climb) pitch = -0.3f;
    else if (mode == BM::Emerge) pitch = -0.6f * std::max(0.0f, 1.0f - b.modeAge);   // bursting up out of the ground, nose first
    gDragonJaw = mode == BM::Breath ? 2600.0f + std::sin(t * 0.7f) * 500.0f : (std::sin(t * 0.05f) + 1.0f) * 150.0f;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)eyes[(play->gameplayFrames / 7) % 60 == 0 ? 2 : 0]);
    gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)Gfx_TwoTexScroll(play->state.gfxCtx, 0, static_cast<u32>(play->gameplayFrames * 1) % 0x80, static_cast<u32>(play->gameplayFrames * 2) % 0x80, 0x20, 0x20, 1,
                                                                    static_cast<u32>(play->gameplayFrames * 3) % 0x80, static_cast<u32>(play->gameplayFrames * -2) % 0x80, 0x20, 0x20));
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, 255);
    gDPSetEnvColor(POLY_OPA_DISP++, 255, 255, 255, 128);
    const float scale = 0.014f;
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y + bob, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(actor->shape.rot.y * (3.14159265f / 32768.0f) + 3.14159265f, MTXMODE_APPLY);   // it faces the way the dragon goes
    Matrix_RotateX(pitch, MTXMODE_APPLY);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    CLOSE_DISPS(play->state.gfxCtx);
    BossTintOn(play, 0, 0, 0, 0, b.hurtAge, &b);
    SkelAnime_DrawSkeletonOpa(play, &const_cast<BossActor&>(b).sk, Dragon_OverrideLimb, nullptr, actor);
    BossTintOff(play);
}
