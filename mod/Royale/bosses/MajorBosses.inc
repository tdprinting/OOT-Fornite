
// Morpha: the water itself, a great tentacle of it with the nucleus riding at the top, drawn with the game's own Morpha pieces. It travels under the
// water (unseen but for the ripples), rises beside you, lashes its tentacle down, and then the tentacle collapses and leaves the core lying exposed.
const f32 kMorphaWidth[41] = {
    3.56f, 3.25f, 2.96f, 2.69f, 2.44f, 2.21f, 2.0f, 1.81f, 1.64f, 1.49f, 1.36f, 1.25f, 1.16f, 1.09f,
    1.04f, 1.01f, 1.0f,  1.0f,  1.0f,  1.0f,  1.0f, 1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
    1.0f,  1.0f,  1.0f,  1.0f,  0.98f, 0.95f, 0.9f, 0.8f,  0.6f,  1.0f,  1.0f,  1.0f,  1.0f,
};
const char* MorphaPart(int i) {
    static const char* parts[41] = {
        gMorphaTentaclePart0DL,  gMorphaTentaclePart1DL,  gMorphaTentaclePart2DL,  gMorphaTentaclePart3DL,  gMorphaTentaclePart4DL,  gMorphaTentaclePart5DL,
        gMorphaTentaclePart6DL,  gMorphaTentaclePart7DL,  gMorphaTentaclePart8DL,  gMorphaTentaclePart9DL,  gMorphaTentaclePart10DL, gMorphaTentaclePart11DL,
        gMorphaTentaclePart12DL, gMorphaTentaclePart13DL, gMorphaTentaclePart14DL, gMorphaTentaclePart15DL, gMorphaTentaclePart16DL, gMorphaTentaclePart17DL,
        gMorphaTentaclePart18DL, gMorphaTentaclePart19DL, gMorphaTentaclePart20DL, gMorphaTentaclePart21DL, gMorphaTentaclePart22DL, gMorphaTentaclePart23DL,
        gMorphaTentaclePart24DL, gMorphaTentaclePart25DL, gMorphaTentaclePart26DL, gMorphaTentaclePart27DL, gMorphaTentaclePart28DL, gMorphaTentaclePart29DL,
        gMorphaTentaclePart30DL, gMorphaTentaclePart31DL, gMorphaTentaclePart32DL, gMorphaTentaclePart33DL, gMorphaTentaclePart34DL, gMorphaTentaclePart35DL,
        gMorphaTentaclePart36DL, gMorphaTentaclePart37DL, gMorphaTentaclePart38DL, gMorphaTentaclePart39DL, gMorphaTentaclePart40DL,
    };
    return i == 0 ? gMorphaTentacleBaseDL : parts[i];
}
// How tall Morpha's tentacle stands (0 collapsed), and how far it is lashing forward.
float MorphaHeight(const BossActor& b) {
    switch (ModeOf(b)) {
        case BM::Hidden: return 0.0f;
        case BM::Emerge: return 480.0f * std::clamp(b.modeAge / 0.7f, 0.0f, 1.0f);
        case BM::Stunned: case BM::Landed: return 480.0f * std::clamp(1.0f - b.modeAge / 0.5f, 0.0f, 1.0f);
        default: return 480.0f;
    }
}
float MorphaLash(const BossActor& b) {
    if (ModeOf(b) != BM::Slam) return 0.0f;
    if (b.modeAge < 0.55f) return -0.012f * (b.modeAge / 0.55f);                                // rears back
    return std::min(0.045f, -0.012f + (b.modeAge - 0.55f) / 0.25f * 0.057f);                       // and whips down
}

void Morpha_Draw(Actor* actor, PlayState* play, const BossActor& b) {
    if (ModeOf(b) == BM::Hidden) return;
    const u32 frames = play->gameplayFrames;
    const float t = BossTime(play);
    const float base = WaterTop(actor->world.pos.x, actor->world.pos.z, GroundY(play, actor->world.pos.x, actor->world.pos.z, actor->world.pos.y));
    const float height = MorphaHeight(b), lash = MorphaLash(b);
    const float width = 0.013f;
    const u8 alpha = 170;
    OPEN_DISPS(play->state.gfxCtx);
    // The tentacle, segment by segment, as the game builds it: each piece's matrix goes in a table the pieces' display lists read from.
    if (height > 5.0f) {
        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        gSPSegment(POLY_XLU_DISP++, 0x08, (uintptr_t)Gfx_TwoTexScroll(play->state.gfxCtx, 0, frames * 3, frames * 3, 32, 32, 1, frames * -3, frames * -3, 32, 32));
        gDPSetPrimColor(POLY_XLU_DISP++, 0xFF, 0xFF, 200, 255, 255, static_cast<u8>(alpha * 1.2f > 255 ? 255 : alpha * 1.2f));
        gDPSetEnvColor(POLY_XLU_DISP++, 0, 100, 255, alpha);
        const u16 scroll = static_cast<u16>(std::sin(t * 6.0f) * 30.0f + 350.0f);
        gSPTexture(POLY_XLU_DISP++, scroll, scroll, 0, G_TX_RENDERTILE, G_ON);
        Mtx* matrix = static_cast<Mtx*>(Graph_Alloc(play->state.gfxCtx, 41 * sizeof(Mtx)));
        gSPSegment(POLY_XLU_DISP++, 0x0C, (uintptr_t)matrix);
        Matrix_Push();
        Matrix_Translate(actor->world.pos.x, base, actor->world.pos.z, MTXMODE_NEW);
        Matrix_RotateY(actor->shape.rot.y * (3.14159265f / 32768.0f), MTXMODE_APPLY);
        const float stretch = height / 38.0f;
        for (int i = 0; i < 41; i++, matrix++) {
            if (i < 2) {
                Matrix_Push();
                Matrix_Scale(0.0f, 0.0f, 0.0f, MTXMODE_APPLY);
            } else {
                if (i >= 3) {
                    Matrix_Translate(0.0f, stretch, 0.0f, MTXMODE_APPLY);
                    Matrix_RotateX(lash + std::cos(t * 1.7f + i * 0.2f) * 0.01f, MTXMODE_APPLY);
                    Matrix_RotateZ(std::sin(t * 2.0f + i * 0.25f) * 0.012f, MTXMODE_APPLY);
                }
                Matrix_Push();
                const float w = kMorphaWidth[i] * (1.0f + 0.08f * std::sin(t * 4.0f - i * 0.3f)) * width;
                Matrix_Scale(w, w, w, MTXMODE_APPLY);
            }
            Matrix_RotateX(3.14159265f / 2.0f, MTXMODE_APPLY);
            Matrix_ToMtx(matrix, const_cast<char*>(__FILE__), __LINE__);
            gSPMatrix(POLY_XLU_DISP++, matrix, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_XLU_DISP++, (Gfx*)MorphaPart(i));
            Matrix_Pop();
        }
        Matrix_Pop();
    }
    // The nucleus: at the top of the tentacle, or lying on the ground once it has collapsed.
    const bool down = ModeOf(b) == BM::Stunned || ModeOf(b) == BM::Landed;
    float cx = actor->world.pos.x, cz = actor->world.pos.z, cy = base + std::max(height, 0.0f) + 40.0f;
    if (lash != 0.0f) {   // it rides the tip of the lash
        const float a = lash * 38.0f, ang = actor->shape.rot.y * (3.14159265f / 32768.0f);
        cy = base + height * std::cos(a) + 40.0f;
        cx += std::sin(ang) * height * std::sin(a);
        cz += std::cos(ang) * height * std::sin(a);
    }
    if (down) cy = GroundY(play, cx, cz, base) + 30.0f + std::fabs(std::sin(t * 5.0f)) * 12.0f;
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPSegment(POLY_XLU_DISP++, 0x08, (uintptr_t)Gfx_TwoTexScroll(play->state.gfxCtx, 0, frames * 3, frames * 3, 32, 32, 1, frames * -3, frames * -3, 32, 32));
    gSPSegment(POLY_XLU_DISP++, 0x09, (uintptr_t)Gfx_TwoTexScroll(play->state.gfxCtx, 0, frames * 5, 0, 32, 32, 1, 0, frames * -10, 32, 32));
    Matrix_Translate(cx, cy, cz, MTXMODE_NEW);
    Matrix_RotateX(frames * 0.05f, MTXMODE_APPLY);
    Matrix_RotateZ(frames * 0.08f, MTXMODE_APPLY);
    const float core = 0.012f;
    Matrix_Scale(core, core, core, MTXMODE_APPLY);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gDPSetPrimColor(POLY_XLU_DISP++, 0x80, 0x80, 255, 255, 255, alpha);
    func_8002ED80(actor, play, 0);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gMorphaCoreMembraneDL);
    gDPPipeSync(POLY_XLU_DISP++);
    gDPSetEnvColor(POLY_XLU_DISP++, 0, 220, 255, 128);
    if (b.hurtAge < 0.3f && (frames & 1)) gDPSetPrimColor(POLY_XLU_DISP++, 0x80, 0x80, 255, 60, 0, 255);
    else gDPSetPrimColor(POLY_XLU_DISP++, 0x80, 0x80, 255, 255, 255, 255);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gMorphaCoreNucleusDL);
    CLOSE_DISPS(play->state.gfxCtx);
}

// Phantom Ganon: his own skeleton and moves. He floats and drifts, throws volleys of energy, calls lightning down, steps into a portal and charges out
// of another one through where you stood, and hangs stunned when he is knocked down.
void PhantomGanon_Update(PlayState* play, BossActor& b) {
    if (!b.skReady) {
        BossInitSkel(play, &b.sk, gPhantomGanonSkel, false, gPhantomGanonNeutralAnim);
        b.playing = gPhantomGanonNeutralAnim;
        b.skReady = true;
        b.lastHp = b.hp;
    }
    const char* want = gPhantomGanonNeutralAnim;
    bool loop = true;
    switch (ModeOf(b)) {
        case BM::Charge: want = b.modeAge < 0.25f ? gPhantomGanonChargeStartAnim : gPhantomGanonChargeAnim; break;
        case BM::Beam: want = gPhantomGanonThrowAnim; loop = false; break;
        case BM::Cast: want = gPhantomGanonChargeWindupAnim; loop = false; break;   // gathers the storm in his hands
        case BM::Emerge: want = gPhantomGanonReturn1Anim; loop = false; break;
        case BM::Stunned: case BM::Landed: want = gPhantomGanonStunnedAnim; break;
        default: if (b.hurtAge < 0.5f) { want = gPhantomGanonAirDamageAnim; loop = false; } break;
    }
    BossPlay(&b.sk, &b.playing, want, 1.0f, loop, b.modeFresh && !loop, -5.0f);
    SkelAnime_Update(&b.sk);
}
void PhantomGanon_Draw(Actor* actor, PlayState* play, const BossActor& b) {
    if (!b.skReady || ModeOf(b) == BM::Hidden) return;   // gone through his portal
    const float t = BossTime(play);
    const BM mode = ModeOf(b);
    const float scale = 0.018f;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gDPSetEnvColor(POLY_OPA_DISP++, 255, 255, 255, 255);   // his eyes, lit
    gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)gBossNullDl);
    const bool down = mode == BM::Stunned || mode == BM::Landed;
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y + (down ? 0.0f : std::sin(t * 2.0f) * 12.0f), actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(actor->shape.rot.y * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    if (mode == BM::Charge) Matrix_RotateX(0.35f, MTXMODE_APPLY);   // leaning into the charge
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    CLOSE_DISPS(play->state.gfxCtx);
    BossTintOn(play, 0, 0, 0, 0, b.hurtAge, &b);
    SkelAnime_DrawSkeletonOpa(play, &const_cast<BossActor&>(b).sk, nullptr, nullptr, actor);
    BossTintOff(play);
}

// Bongo Bongo: the one-eyed head and the two great hands, each the game's own. The hands beat their drum (the ground), slam down one at a time,
// clap together on you, and hang limp when he is stunned; the head charges at you. Unseen when he vanishes, but for the shadowy mist.
const float kBongoScale = 0.024f;
void Bongo_Update(PlayState* play, BossActor& b) {
    if (!b.skReady) {
        BossInitSkel(play, &b.sk, gBongoHeadSkel, true, gBongoHeadEyeOpenIdleAnim);
        b.playing = gBongoHeadEyeOpenIdleAnim;
        BossInitSkel(play, &b.hand[0], gBongoLeftHandSkel, true, gBongoLeftHandIdleAnim);
        BossInitSkel(play, &b.hand[1], gBongoRightHandSkel, true, gBongoRightHandIdleAnim);
        b.handPlaying[0] = gBongoLeftHandIdleAnim;
        b.handPlaying[1] = gBongoRightHandIdleAnim;
        b.skReady = b.handsReady = true;
        b.lastHp = b.hp;
    }
    const BM mode = ModeOf(b);
    const char* head = gBongoHeadEyeOpenIdleAnim;
    bool headLoop = true;
    if (mode == BM::Swoop) head = gBongoHeadChargeAnim;
    else if (mode == BM::Stunned || mode == BM::Landed) head = gBongoHeadStunnedAnim;
    else if (mode == BM::Hidden) head = gBongoHeadEyeCloseIdleAnim;
    else if (b.hurtAge < 0.5f) { head = gBongoHeadDamageAnim; headLoop = false; }
    BossPlay(&b.sk, &b.playing, head, 1.0f, headLoop);
    static const char* idle[2] = { gBongoLeftHandIdleAnim, gBongoRightHandIdleAnim };
    static const char* fist[2] = { gBongoLeftHandFistPoseAnim, gBongoRightHandFistPoseAnim };
    static const char* flat[2] = { gBongoLeftHandFlatPoseAnim, gBongoRightHandFlatPoseAnim };
    static const char* open[2] = { gBongoLeftHandOpenPoseAnim, gBongoRightHandOpenPoseAnim };
    static const char* hurt[2] = { gBongoLeftHandDamagePoseAnim, gBongoRightHandDamagePoseAnim };
    for (int h = 0; h < 2; h++) {
        const char* want = idle[h];
        if (mode == BM::Slam && (b.aux == 2 || b.aux == h)) want = b.aux == 2 ? open[h] : (b.modeAge < 0.7f ? fist[h] : flat[h]);
        else if (mode == BM::Summon) want = flat[h];
        else if (mode == BM::Stunned || mode == BM::Landed) want = hurt[h];
        BossPlay(&b.hand[h], &b.handPlaying[h], want, 1.0f, true, false, -3.0f);
        SkelAnime_Update(&b.hand[h]);
    }
    SkelAnime_Update(&b.sk);
}
// Where each of Bongo's hands is now: beside the head at rest, up over you and down for a slam, together for the clap, on the drum when he drums.
Vec3f BongoHandAt(PlayState* play, const BossActor& b, uint32_t id, int h) {
    const float ang = b.rot * (3.14159265f / 32768.0f), fx = std::sin(ang), fz = std::cos(ang), rx = std::cos(ang), rz = -std::sin(ang);
    const float side = h == 0 ? -1.0f : 1.0f;   // the left hand on his left
    const float t = BossTime(play);
    const float ground = GroundY(play, b.x, b.z, b.actor->world.pos.y);
    Vec3f rest = { b.x + rx * side * 330.0f + fx * 160.0f, ground + 140.0f + std::sin(t * 2.0f + h) * 20.0f, b.z + rz * side * 330.0f + fz * 160.0f };
    const BM mode = ModeOf(b);
    if (mode == BM::Slam && (b.aux == 2 || b.aux == h)) {
        const StrikeFx* s = PendingStrikeOf(id);
        Vec3f aim = s ? Vec3f{ s->x, GroundY(play, s->x, s->z, ground), s->z } : Vec3f{ b.x + fx * 400.0f, ground, b.z + fz * 400.0f };
        if (b.aux == 2) { aim.x += rx * side * (b.modeAge < 0.9f ? 260.0f : 60.0f); aim.z += rz * side * (b.modeAge < 0.9f ? 260.0f : 60.0f); }   // clap: apart, then together
        const float up = b.aux == 2 ? 0.9f : 0.7f;   // rises over the spot...
        if (b.modeAge < up) {
            const float k = b.modeAge / up;
            return { rest.x + (aim.x - rest.x) * k, rest.y + (aim.y + 420.0f - rest.y) * k, rest.z + (aim.z - rest.z) * k };
        }
        const float k = std::min(1.0f, (b.modeAge - up) / 0.25f);   // ...and comes down on it
        return { aim.x, aim.y + 420.0f * (1.0f - k) + 15.0f, aim.z };
    }
    if (mode == BM::Summon) { rest.y = ground + 30.0f + std::fabs(std::sin(t * 9.0f + h * 1.5708f)) * 170.0f; return rest; }   // drumming
    if (mode == BM::Stunned || mode == BM::Landed) { rest.y = ground + 20.0f; return rest; }
    return rest;
}
void Bongo_Draw(Actor* actor, PlayState* play, const BossActor& b, uint32_t id) {
    if (!b.skReady || ModeOf(b) == BM::Hidden) return;
    const float t = BossTime(play);
    auto setup = [&]() {
        OPEN_DISPS(play->state.gfxCtx);
        Gfx_SetupDL_25Opa(play->state.gfxCtx);
        gDPSetPrimColor(POLY_OPA_DISP++, 0x00, 0x80, 255, 255, 255, 255);
        gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)&D_80116280[2]);
        CLOSE_DISPS(play->state.gfxCtx);
    };
    setup();
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y + std::sin(t * 1.6f) * 18.0f, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(actor->shape.rot.y * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    if (ModeOf(b) == BM::Emerge) {   // fading back into sight: grows out of nothing
        const float k = std::clamp(b.modeAge / 0.6f, 0.05f, 1.0f);
        Matrix_Scale(k, k, k, MTXMODE_APPLY);
    }
    Matrix_Scale(kBongoScale, kBongoScale, kBongoScale, MTXMODE_APPLY);
    Matrix_Translate(0.0f, 70000.0f, 0.0f, MTXMODE_APPLY);   // the game draws the head this far above where it is
    BossTintOn(play, 0, 0, 0, 0, b.hurtAge, &b);
    SkelAnime_DrawSkeletonOpa(play, &const_cast<BossActor&>(b).sk, nullptr, nullptr, actor);
    BossTintOff(play);
    for (int h = 0; h < 2; h++) {
        const Vec3f at = BongoHandAt(play, b, id, h);
        setup();
        Matrix_Translate(at.x, at.y, at.z, MTXMODE_NEW);
        Matrix_RotateY(actor->shape.rot.y * (3.14159265f / 32768.0f), MTXMODE_APPLY);
        Matrix_Scale(kBongoScale, kBongoScale, kBongoScale, MTXMODE_APPLY);
        BossTintOn(play, 0, 0, 0, 0, b.hurtAge, &b);
        SkelAnime_DrawSkeletonOpa(play, &const_cast<BossActor&>(b).hand[h], nullptr, nullptr, actor);
        BossTintOff(play);
    }
}

// Twinrova: the sisters as one, Koume's fire and Kotake's ice in her hair, flying on her broom. Fire and ice beams, rings of fire and ice, broom dives.
const char* gTwinrovaEyes[3] = { gTwinrovaEyeOpenTex, gTwinrovaEyeHalfTex, gTwinrovaEyeClosedTex };
void Twinrova_Update(PlayState* play, BossActor& b) {
    if (!b.skReady) {
        BossInitSkel(play, &b.sk, gTwinrovaSkel, true, gTwinrovaHoverAnim);
        b.playing = gTwinrovaHoverAnim;
        b.skReady = true;
        b.lastHp = b.hp;
    }
    const char* want = gTwinrovaHoverAnim;
    bool loop = true;
    switch (ModeOf(b)) {
        case BM::Beam: want = b.aux == 1 ? gTwinrovaIceAttackAnim : gTwinrovaFireAttackAnim; loop = false; break;
        case BM::Summon: want = b.modeAge < 0.9f ? gTwinrovaWindUpAnim : gTwinrovaChargedAttackHitAnim; loop = false; break;
        case BM::Stunned: case BM::Landed: want = gTwinrovaStunLoopAnim; break;
        default: if (b.hurtAge < 0.5f) { want = gTwinrovaDamageAnim; loop = false; } else if (b.hurtAge < 2.5f && b.hp < 0.5f) want = gTwinrovaLaughAnim; break;
    }
    BossPlay(&b.sk, &b.playing, want, 1.0f, loop, b.modeFresh && !loop, -5.0f);
    SkelAnime_Update(&b.sk);
}
s32 Twinrova_OverrideLimb(PlayState* play, s32 limb, Gfx** dList, Vec3f*, Vec3s*, void*) {
    const u32 f = play->gameplayFrames;
    OPEN_DISPS(play->state.gfxCtx);
    switch (limb) {   // as the game draws her: her face, and her hair (fire on one side, ice on the other) drawn see-through after the limb
        case 21:
            gSPSegment(POLY_OPA_DISP++, 0x0C, (uintptr_t)Gfx_TexScroll(play->state.gfxCtx, 0, f, 8, 8));
            gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)gTwinrovaEyes[(f / 5) % 50 == 0 ? 2 : 0]);
            gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)gTwinrovaEyes[(f / 5) % 50 == 0 ? 2 : 0]);
            gDPSetEnvColor(POLY_OPA_DISP++, 255, 255, 255, 0);
            break;
        case 17: case 41:
            *dList = nullptr;
            gSPSegment(POLY_XLU_DISP++, 0x0A, (uintptr_t)Gfx_TwoTexScroll(play->state.gfxCtx, 0, 0, 0, 0x20, 0x20, 1, 0, -f * 0xF, 0x20, 0x40));
            break;
        case 18: case 42:
            *dList = nullptr;
            gSPSegment(POLY_XLU_DISP++, 0x0B, (uintptr_t)Gfx_TwoTexScroll(play->state.gfxCtx, 0, 0, 0, 0x20, 0x20, 1, 0, -f * 0xA, 0x20, 0x40));
            break;
        case 16: case 32:
            *dList = nullptr;
            gSPSegment(POLY_XLU_DISP++, 0x08, (uintptr_t)Gfx_TwoTexScroll(play->state.gfxCtx, 0, 0, 0, 0x20, 0x20, 1, f, -f * 7, 0x20, 0x40));
            break;
        case 15: case 31:
            *dList = nullptr;
            gSPSegment(POLY_XLU_DISP++, 0x09, (uintptr_t)Gfx_TexScroll(play->state.gfxCtx, 0, f, 0x20, 0x40));
            break;
        default: break;
    }
    CLOSE_DISPS(play->state.gfxCtx);
    return 0;
}
void Twinrova_PostLimb(PlayState* play, s32 limb, Gfx** dList, Vec3s*, void*) {
    switch (limb) {
        case 15: case 16: case 17: case 18: case 31: case 32: case 41: case 42:
            if (dList == nullptr || *dList == nullptr) return;
            OPEN_DISPS(play->state.gfxCtx);
            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_XLU_DISP++, *dList);
            CLOSE_DISPS(play->state.gfxCtx);
            break;
        default: break;
    }
}
void Twinrova_Draw(Actor* actor, PlayState* play, const BossActor& b) {
    if (!b.skReady) return;
    const float t = BossTime(play);
    const BM mode = ModeOf(b);
    float pitch = 0.0f;
    if (mode == BM::Swoop) pitch = 0.55f;
    else if (mode == BM::Charge) pitch = 0.35f;
    const float scale = 0.03f;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y + std::sin(t * 2.4f) * 16.0f, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(actor->shape.rot.y * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    Matrix_RotateX(pitch, MTXMODE_APPLY);
    Matrix_RotateZ(std::sin(t * 1.3f) * 0.08f, MTXMODE_APPLY);   // swaying on the broom
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    CLOSE_DISPS(play->state.gfxCtx);
    BossTintOn(play, 0, 0, 0, 0, b.hurtAge, &b);
    SkelAnime_DrawSkeletonOpa(play, &const_cast<BossActor&>(b).sk, Twinrova_OverrideLimb, Twinrova_PostLimb, actor);
    BossTintOff(play);
}

