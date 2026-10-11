// ---- mini bosses --------------------------------------------------------------------------------------------------------------

#include "RoyaleChuChu.h"

struct MiniLook {
    const char* skeleton;
    bool flex;
    const char* idle; const char* walk; const char* attack[2]; const char* hurt;
    float scale;            // the game's own actor scale
    float idleSpeed;        // 0 holds the first frame (the Iron Knuckle's stance)
    u8 r, g, b; s16 far;    // its colour scheme, laid over the model (see BossTintOn)
};
const MiniLook& MiniLookOf(int kind) {
    static const MiniLook looks[7] = {
        // Stalfos: bone white, with a faint red glow over it and red eyes
        { gStalfosSkel, false, gStalfosMiddleGuardAnim, gStalfosFastAdvanceAnim, { gStalfosDownSlashAnim, gStalfosUpSlashAnim }, gStalfosFlinchFromHitFrontAnim, 0.015f, 1.0f, 255, 30, 20, 4300 },
        // Magma Dodongo: molten red-orange
        { gDodongoSkel, false, gDodongoWaitAnim, gDodongoWalkAnim, { gDodongoSweepTailRightAnim, gDodongoSweepTailLeftAnim }, gDodongoDamageAnim, 0.01875f, 1.0f, 255, 70, 0, 2300 },
        // White Wolfos: icy blue
        { gWolfosWhiteSkel, true, gWolfosWaitingAnim, gWolfosRunningAnim, { gWolfosSlashingAnim, gWolfosSlashingAnim }, gWolfosDamagedAnim, 0.01f, 1.0f, 110, 190, 255, 2700 },
        // Moss Lizalfos: moss green
        { gZfLizalfosSkel, false, gZfSidesteppingAnim, gZfWalkingAnim, { gZfSlashAnim, gZfSlashAnim }, gZfKnockedBackAnim, 0.015f, 0.6f, 50, 150, 30, 2200 },
        // Big Octo: deep sea purple
        { object_bigokuta_Skel_006BC0, true, object_bigokuta_Anim_0014B8, object_bigokuta_Anim_001CA4, { object_bigokuta_Anim_000444, object_bigokuta_Anim_000A74 }, object_bigokuta_Anim_000D1C, 0.02f, 1.0f, 110, 30, 175, 2300 },
        // Dead Hand: pale and shadowy
        { object_dh_Skel_007E88, true, object_dh_Anim_003A8C, object_dh_Anim_005880, { object_dh_Anim_004658, object_dh_Anim_004658 }, object_dh_Anim_003D6C, 0.01f, 1.0f, 40, 15, 60, 2500 },
        // Iron Knuckle: desert gold
        { gIronKnuckleSkel, true, gIronKnuckleWalkAnim, gIronKnuckleWalkAnim, { gIronKnuckleVerticalAttackAnim, gIronKnuckleHorizontalAttackAnim }, gIronKnuckleFrontHitAnim, 0.012f, 0.0f, 255, 190, 50, 3200 },
    };
    return looks[std::clamp(kind, 0, 6)];
}
float MiniScale(int kind) { return MiniLookOf(kind).scale * 1.55f * royale::kBossDefs[kind].scale; }   // mini bosses are bigger than the game's own

const BossActor* gDrawingBoss = nullptr;
s32 MiniBoss_OverrideLimb(PlayState* play, s32 limb, Gfx** dList, Vec3f*, Vec3s*, void*) {
    const BossActor* b = gDrawingBoss;
    if (b == nullptr) return 0;
    switch (KindOf(*b)) {
        case BK::Dune:   // the Iron Knuckle: whole armour, or (once it has come off) the bare body under it, as the game swaps them
            if (b->armourOff ? (limb == 26 || limb == 27) : (limb == 28 || limb == 29)) *dList = nullptr;
            break;
        case BK::Stone:   // the Stalfos' eyes glow red, pulsing
            if (limb == 11) {
                OPEN_DISPS(play->state.gfxCtx);
                gDPPipeSync(POLY_OPA_DISP++);
                gDPSetEnvColor(POLY_OPA_DISP++, 120 + std::abs(static_cast<int>(std::sin(play->gameplayFrames * 0.15f) * 135.0f)), 0, 0, 255);
                CLOSE_DISPS(play->state.gfxCtx);
            }
            break;
        default: break;
    }
    return 0;
}
void MiniBoss_PostLimb(PlayState* play, s32 limb, Gfx**, Vec3s*, void*) {
    const BossActor* b = gDrawingBoss;
    if (b == nullptr || KindOf(*b) != BK::Dune) return;
    OPEN_DISPS(play->state.gfxCtx);
    auto xlu = [&](const char* dl) {
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_XLU_DISP++, (Gfx*)dl);
    };
    switch (limb) {   // the armour's see-through decals, as the game's Iron Knuckle draws them
        case 12: xlu(object_ik_DL_016D88); break;
        case 22: xlu(object_ik_DL_016F88); break;
        case 24: xlu(object_ik_DL_016EE8); break;
        case 26: if (!b->armourOff) xlu(gIronKnuckleArmorRivetAndSymbolDL); break;
        case 27: if (!b->armourOff) xlu(object_ik_DL_016CD8); break;
        default: break;
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

void MiniBoss_Update(Actor* actor, PlayState* play, BossActor& b) {
    const MiniLook& m = MiniLookOf(b.kind);
    if (!b.skReady) {
        BossInitSkel(play, &b.sk, m.skeleton, m.flex, m.idle);
        b.skReady = true;
        b.playing = m.idle;
        b.lastHp = b.hp;
    }
    if (KindOf(b) == BK::Shade && !b.handsReady) {   // the Dead Hand's hands, that come up out of the ground
        BossInitSkel(play, &b.hand[0], object_dh_Skel_000BD8, true, object_dh_Anim_0015B0);
        b.handPlaying[0] = object_dh_Anim_0015B0;
        b.handsReady = true;
    }
    const float dt = 1.0f / royale::kTickHz;
    b.hurtAge += dt;
    if (b.hp < b.lastHp - 0.02f) b.hurtAge = 0.0f;
    b.lastHp = b.hp;
    static std::unordered_map<const Actor*, float> lastSmash;
    float& before = lastSmash[actor];
    const bool newSwing = b.smashAge < 0.05f && before >= 0.05f;
    before = b.smashAge;
    if (newSwing) b.swings++;
    const BM mode = ModeOf(b);
    const bool swinging = b.smashAge < 1.1f && (mode == BM::Chase || mode == BM::Patrol);
    const char* want = nullptr;
    float speed = 1.0f;
    bool loop = true, restart = newSwing;
    auto once = [&](const char* a, float s = 1.0f) { want = a; speed = s; loop = false; restart = b.modeFresh; };
    switch (KindOf(b)) {
        case BK::Stone:
            if (mode == BM::Leap) once(b.aux == 1 ? gStalfosJumpslashAnim : gStalfosJumpAnim);
            else if (mode == BM::Stunned && b.aux == 3) once(gStalfosFallOverBackwardsAnim);   // in pieces...
            else if (mode == BM::Stunned) once(m.hurt);
            break;
        case BK::Lava:
            if (mode == BM::Breath) once(gDodongoBreatheFireAnim);
            else if (mode == BM::Charge) { want = gDodongoWalkAnim; speed = 2.5f; }
            else if (mode == BM::Stunned) want = gDodongoTwitchAnim;
            break;
        case BK::Frost:
            if (mode == BM::Summon) once(gWolfosRearingUpFallingOverAnim, 1.3f);   // rears up to howl
            else if (mode == BM::Leap) { if (b.aux == 1) once(gWolfosSlashingAnim); else want = gWolfosRunningAnim; }
            else if (mode == BM::Stunned) want = m.hurt;
            break;
        case BK::Moss:
            if (mode == BM::Summon) once(gZfCryingAnim);   // rears back and spits its spore pods
            else if (mode == BM::Leap) once(b.aux == 1 ? gZfJumpingAnim : gZfHopLeapingAnim);
            else if (mode == BM::Climb) want = gZfWalkingAnim;
            else if (mode == BM::Stunned) want = m.hurt;
            break;
        case BK::Tide:
            if (mode == BM::Charge) { want = m.attack[0]; speed = 2.0f; }
            else if (mode == BM::Stunned) want = m.hurt;
            else if (b.aux == 1) { want = m.walk; speed = 1.4f; }   // swimming
            break;
        case BK::Shade:
            if (mode == BM::Summon) once(object_dh_Anim_004658);
            else if (mode == BM::Emerge) once(object_dh_Anim_002148, -1.5f);   // the burrow, played backwards: up out of the ground
            else if (mode == BM::Hidden) once(object_dh_Anim_002148);
            else if (mode == BM::Stunned) want = m.hurt;
            break;
        case BK::Dune:
            if (mode == BM::Slam) once(gIronKnuckleVerticalAttackAnim, b.armourOff ? 1.4f : 1.0f);
            else if (mode == BM::Stunned) want = gIronKnuckleAxeStuckAnim;   // the axe is stuck in the ground
            else if (mode == BM::Leap) want = gIronKnuckleRunAnim;
            break;
        default: break;
    }
    if (want == nullptr) {
        restart = newSwing;
        if (swinging) { want = m.attack[b.swings & 1]; loop = false; }
        else if (b.hurtAge < 0.45f) { want = m.hurt; loop = false; }
        else if (b.moved > 0.5f || mode == BM::Leap || mode == BM::Charge) {
            want = KindOf(b) == BK::Dune && b.armourOff ? gIronKnuckleRunAnim : m.walk;
            if (KindOf(b) == BK::Dune && b.armourOff) speed = 1.3f;
        } else { want = m.idle; speed = KindOf(b) == BK::Dune && b.armourOff ? 1.0f : m.idleSpeed; }
    }
    BossPlay(&b.sk, &b.playing, want, speed, loop, restart);
    SkelAnime_Update(&b.sk);
    if (b.handsReady) SkelAnime_Update(&b.hand[0]);
}

// How far into the ground (0 out, 1 all the way under) the Dead Hand is, as it burrows and comes back up.
float DeadHandSink(const BossActor& b) {
    switch (ModeOf(b)) {
        case BM::Hidden: return 1.0f;
        case BM::Emerge: return std::clamp(1.0f - b.modeAge / 0.6f, 0.0f, 1.0f);
        default: return 0.0f;
    }
}

// The Dead Hand's hands: one comes up out of the ground under each of its grabs, holds on, and sinks back.
void DrawDeadHandHands(PlayState* play, const BossActor& b, uint32_t id) {
    if (!b.handsReady) return;
    const double now = ImGui::GetTime();
    const float scale = 0.012f;
    for (const StrikeFx& s : gStrikeFx) {
        if (s.owner != id || s.style != royale::StrikeStyle::Shadow) continue;
        const float rise = static_cast<float>(std::clamp((now - (s.land - 0.55)) / 0.3, 0.0, 1.0));   // up just before the grab lands
        const float sink = static_cast<float>(std::clamp((now - (s.land + 0.35)) / 0.4, 0.0, 1.0));   // and back down after
        const float out = rise * (1.0f - sink);
        if (out <= 0.01f) continue;
        const float ground = GroundY(play, s.x, s.z, b.actor->world.pos.y);
        OPEN_DISPS(play->state.gfxCtx);
        Gfx_SetupDL_25Opa(play->state.gfxCtx);
        Matrix_Translate(s.x, ground - 90.0f * (1.0f - out), s.z, MTXMODE_NEW);
        Matrix_RotateY(static_cast<float>(static_cast<int>(s.x * 7 + s.z * 13) % 628) * 0.01f, MTXMODE_APPLY);
        Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
        CLOSE_DISPS(play->state.gfxCtx);
        BossTintOn(play, 40, 15, 60, 2500, 10.0f);
        SkelAnime_DrawSkeletonOpa(play, &const_cast<BossActor&>(b).hand[0], nullptr, nullptr, nullptr);
        BossTintOff(play);
    }
}

void MiniBoss_Draw(Actor* actor, PlayState* play, BossActor& b, uint32_t id) {
    if (KindOf(b) == BK::Shade) DrawDeadHandHands(play, b, id);
    if (!b.skReady || b.fade <= 0.03f) return;
    const MiniLook& m = MiniLookOf(b.kind);
    const float scale = MiniScale(b.kind);
    const BM mode = ModeOf(b);
    const float t = BossTime(play);
    const bool see = b.fade < 0.97f;   // see-through: the Lizalfos hiding in the grass
    const u8 alpha = static_cast<u8>(std::clamp(b.fade, 0.0f, 1.0f) * 255.0f);
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    switch (KindOf(b)) {
        case BK::Dune:   // the Iron Knuckle's own colours, gold armour
            gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)BossEnvDl(play, 245, 225, 155, 30, 30, 0));
            gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)BossEnvDl(play, 255, 40, 0, 40, 0, 0));
            gSPSegment(POLY_OPA_DISP++, 0x0A, (uintptr_t)BossEnvDl(play, 255, 255, 255, 20, 40, 30));
            break;
        case BK::Frost: {
            static const char* white[4] = { gWolfosWhiteEyeOpenTex, gWolfosWhiteEyeHalfTex, gWolfosWhiteEyeNarrowTex, gWolfosWhiteEyeHalfTex };
            gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)white[(play->gameplayFrames / 6) % 40 == 0 ? 2 : 0]);
            break;
        }
        case BK::Moss:   // as the game draws its Lizalfos; see-through the way the game fades one out
            func_8002EBCC(actor, play, 1);
            gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)gLizalfosTexDl);
            gSPSegment(POLY_XLU_DISP++, 0x08, (uintptr_t)gLizalfosTexDl);
            gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, 255);
            gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)&D_80116280[2]);
            gDPSetEnvColor(POLY_XLU_DISP++, 0, 0, 0, alpha);
            gSPSegment(POLY_XLU_DISP++, 0x09, (uintptr_t)&D_80116280[0]);
            break;
        case BK::Tide:
            gSPSegment(POLY_OPA_DISP++, 0x0C, (uintptr_t)&D_80116280[2]);
            gDPSetEnvColor(POLY_OPA_DISP++, 255, 255, 255, 255);
            break;
        case BK::Shade:
            gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, 255);
            gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)&D_80116280[2]);
            break;
        default: break;
    }
    float lift = 0.0f;
    if (KindOf(b) == BK::Shade) lift = -150.0f * DeadHandSink(b);
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y + lift, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(actor->shape.rot.y * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    if (gHitReact && b.recoilAge < 0.5f) {   // a blow rocks it away from the attacker, over whatever it is doing
        HitRecoil r; r.amp = b.recoilAmp; r.age = b.recoilAge; r.dx = b.recoilDx; r.dz = b.recoilDz;
        float pitch, roll;
        RecoilTilt(r, actor->shape.rot.y * (3.14159265f / 32768.0f), &pitch, &roll);
        Matrix_RotateX(pitch, MTXMODE_APPLY);
        Matrix_RotateZ(roll, MTXMODE_APPLY);
    }
    if (KindOf(b) == BK::Lava && mode == BM::Charge) {   // the roll: curled up, tumbling forward about its middle
        Matrix_Translate(0.0f, 60.0f, 0.0f, MTXMODE_APPLY);
        Matrix_RotateX(b.spin, MTXMODE_APPLY);
        Matrix_Translate(0.0f, -60.0f, 0.0f, MTXMODE_APPLY);
        Matrix_Scale(1.0f, 0.8f, 0.85f, MTXMODE_APPLY);
    }
    if (KindOf(b) == BK::Tide && mode == BM::Charge) Matrix_RotateY(b.spin, MTXMODE_APPLY);   // the spin
    if ((KindOf(b) == BK::Tide || KindOf(b) == BK::Lava) && mode == BM::Stunned) Matrix_RotateZ(std::sin(t * 9.0f) * 0.12f, MTXMODE_APPLY);   // dizzy
    if (KindOf(b) == BK::Moss && mode == BM::Climb) Matrix_RotateX(-0.9f, MTXMODE_APPLY);   // up the wall
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    CLOSE_DISPS(play->state.gfxCtx);
    gDrawingBoss = &b;
    if (see) {
        OPEN_DISPS(play->state.gfxCtx);
        POLY_XLU_DISP = SkelAnime_DrawSkeleton2(play, &b.sk, nullptr, nullptr, actor, POLY_XLU_DISP);
        CLOSE_DISPS(play->state.gfxCtx);
    } else {
        BossTintOn(play, m.r, m.g, m.b, m.far, b.hurtAge, &b);
        SkelAnime_DrawSkeletonOpa(play, &b.sk, MiniBoss_OverrideLimb, MiniBoss_PostLimb, actor);
        BossTintOff(play);
    }
    gDrawingBoss = nullptr;
}

