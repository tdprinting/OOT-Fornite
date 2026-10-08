// The bosses as the game draws them. Included once from RoyaleMod.cpp, inside its anonymous namespace, after the helpers it uses (GroundY, FloorAt,
// GpuMeshFor, WorldToScreen, ...); it is part of that file, split out so the boss code has a home of its own.
//
// The server decides everything a boss does (shared/boss.h, server/match.h) and sends where it is, what it is doing (its mode) and which variant
// of that (aux). Here every boss is the game's own model with the game's own animations, shown doing what the snapshot says, in its own colours,
// with the game's own particles and sprites for its attacks:
//   mini bosses   Stalfos (bone white, red glow)       Magma Dodongo (molten red-orange)   White Wolfos (icy blue)
//                 Moss Lizalfos (moss green, fades into the grass)   Big Octo (deep sea purple)   Dead Hand (pale and shadowy)
//                 Iron Knuckle (desert gold; its armour comes off at half health)
//   major bosses  Volvagia (Death Mountain Crater)   Morpha (Lake Hylia)   Phantom Ganon (Hyrule Field)   Bongo Bongo (Kakariko)
//                 Twinrova (Desert Colossus)
// The game's real assets come from the player's own ROM; nothing here is game data, only the names the game's resources go by.

using BM = royale::DragonMode;
using BK = royale::BossKind;

struct BossActor {
    Actor* actor = nullptr;
    ActorFunc origDestroy = nullptr;
    int kind = 0;
    float x = 0, z = 0;          // smoothed position
    float tx = 0, tz = 0;        // latest from the server
    int16_t rot = 0, trot = 0;
    float hp = 1.0f;
    float smashAge = 10.0f;      // seconds since it last swung
    float moved = 0;             // distance covered lately, for the walking animation
    float alt = 0, talt = 0;     // height above the ground, smoothed / latest (leaps, flight)
    int mode = 0, aux = 0;       // royale::DragonMode and its variant, from the server
    int shownMode = -1, shownAux = -1;
    float modeAge = 0;           // seconds since the mode (or its variant) last changed
    bool modeFresh = false;      // the mode changed this frame
    bool initialised = false;
    SkelAnime sk = {};           // the game's own model (zeroed: the game reads the old animation when it changes one)
    bool skReady = false;
    const void* playing = nullptr;
    SkelAnime hand[2] = {};      // Bongo Bongo's two hands; the Dead Hand's grabbing hands use the first
    bool handsReady = false;
    const void* handPlaying[2] = { nullptr, nullptr };
    float hurtAge = 10.0f;       // seconds since it was last hurt
    float lastHp = 1.0f;
    int swings = 0;              // which of its two swings comes next
    float fade = 1.0f;           // 0 when it is out of sight (under the ground, under the water, vanished, hiding in the grass)
    float spin = 0;              // the Big Octo's spin, the Dodongo's roll
    bool armourOff = false;      // the Iron Knuckle has lost its armour
    float fxClock = 0;           // for effects that come every so often
    float flashAge = 10.0f, flashLen = 0.0f;   // seconds since a hit flashed it, and how long that flash lasts (see HitFeedback)
    u8 flashR = 255, flashG = 50, flashB = 20; // the colour of that flash
    float recoilAge = 10.0f, recoilAmp = 0.0f, recoilDx = 0, recoilDz = 0;   // the rock away from a blow, laid over its animation (see RecoilTilt)
};
std::unordered_map<uint32_t, BossActor> gBosses;      // boss id -> its actor
std::unordered_map<const Actor*, uint32_t> gBossOf;
std::unordered_map<uint32_t, int> gBossKindSeen;      // remembered after it is gone, for the messages

BK KindOf(const BossActor& b) { return static_cast<BK>(b.kind); }
BM ModeOf(const BossActor& b) { return static_cast<BM>(b.mode); }
float BossTime(PlayState* play) { return static_cast<float>(play->gameplayFrames) / royale::kTickHz; }

// A strike on the ground: a circle that glows while the blow is on its way and blows up when it lands. `style` says what it is (fire, ice,
// water, shadow hands, rocks, magic, spores or lightning), `owner` which boss threw it (none for the storm's lightning).
struct StrikeFx { float x, z, radius; double start, land; bool boomed; royale::StrikeStyle style; uint32_t owner; };
std::vector<StrikeFx> gStrikeFx;

const BossActor* BossById(uint32_t id) {
    auto it = gBosses.find(id);
    return it == gBosses.end() || it->second.actor == nullptr ? nullptr : &it->second;
}

// ---- shared bits ------------------------------------------------------------------------------------------------------------------

Gfx* BossEnvDl(PlayState* play, u8 pr, u8 pg, u8 pb, u8 er, u8 eg, u8 eb) {
    Gfx* dl = static_cast<Gfx*>(Graph_Alloc(play->state.gfxCtx, 4 * sizeof(Gfx)));
    Gfx* h = dl;
    gDPPipeSync(h++);
    gDPSetPrimColor(h++, 0, 0, pr, pg, pb, 255);
    gDPSetEnvColor(h++, er, eg, eb, 255);
    gSPEndDisplayList(h++);
    return dl;
}
Gfx gBossNullDl[] = { gsSPEndDisplayList() };
Gfx gLizalfosTexDl[] = { gsSPTexture(0x0A00, 0x0A00, 0, G_TX_RENDERTILE, G_ON), gsSPEndDisplayList() };   // as the game's Lizalfos sets it

void BossSound(const BossActor& b, u16 sfx) { if (b.actor != nullptr) Audio_PlayActorSound2(b.actor, sfx); }
void SoundAt(float x, float y, float z, u16 sfx) {
    Vec3f at = { x, y, z };
    Audio_PlaySoundGeneral(sfx, &at, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

void SparkBurst(PlayState* play, float x, float y, float z, Color_RGBA8 prim, int count, float speed) {
    Color_RGBA8 env = { 255, 255, 255, 255 };
    for (int i = 0; i < count; i++) {
        const float a = Rand_ZeroOne() * 6.2831853f, up = 0.3f + Rand_ZeroOne() * 0.9f;
        Vec3f pos = { x, y, z };
        Vec3f vel = { std::cos(a) * speed * (0.4f + Rand_ZeroOne()), speed * up, std::sin(a) * speed * (0.4f + Rand_ZeroOne()) };
        Vec3f accel = { 0.0f, -0.35f, 0.0f };
        EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 60, 40);
    }
}
void Glitter(PlayState* play, float x, float y, float z, Color_RGBA8 prim, Color_RGBA8 env, float rise, s16 scale, s32 life) {
    Vec3f pos = { x, y, z }, vel = { (Rand_ZeroOne() - 0.5f) * 1.2f, rise, (Rand_ZeroOne() - 0.5f) * 1.2f }, accel = { 0, 0, 0 };
    EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, scale, life);
}
void Smoke(PlayState* play, float x, float y, float z, Color_RGBA8 c, s16 scale) {
    Vec3f pos = { x, y, z }, vel = { 0, 1.5f, 0 }, accel = { 0, 0.1f, 0 };
    EffectSsDeadDb_Spawn(play, &pos, &vel, &accel, scale, -1, c.r, c.g, c.b, c.a, c.r / 3, c.g / 3, c.b / 3, 1, 9, false);
}
void Ripple(PlayState* play, float x, float y, float z, s16 radius, s16 maxRadius) {
    Vec3f pos = { x, y, z };
    EffectSsGRipple_Spawn(play, &pos, radius, maxRadius, 0);
}
float WaterTop(float x, float z, float floorY) {   // the water's surface over (x, z), or the floor when it is dry there
    float surface = 0;
    WaterBox* box = nullptr;
    if (WaterBox_GetSurface1(gPlayState, &gPlayState->colCtx, x, z, &surface, &box) != 0 && surface > floorY) return surface;
    return floorY;
}

// Tints the model in the boss's colours the way the game tints an enemy that is frozen or hit (fog laid over it), or flashes it red for a moment
// after a hit, as every boss in the game does. `far` is how faint the tint is: 1700 strong, 4500 barely there.
void BossTintOn(PlayState* play, u8 r, u8 g, u8 b, s16 far, float hurtAge, const BossActor* flash = nullptr) {
    OPEN_DISPS(play->state.gfxCtx);
    if (flash != nullptr && gHitFlash && flash->flashAge < flash->flashLen) {   // a hit: the game's own enemy flash (strong, fading), in the colour of the target
        const float k = flash->flashAge / flash->flashLen;
        gDPPipeSync(POLY_OPA_DISP++);
        gDPSetFogColor(POLY_OPA_DISP++, flash->flashR, flash->flashG, flash->flashB, 255);
        gSPFogPosition(POLY_OPA_DISP++, 0, static_cast<s16>(1700.0f + 2800.0f * k * k));
    } else if (hurtAge < 0.12f) POLY_OPA_DISP = Gfx_SetFog(POLY_OPA_DISP, 255, 50, 0, 0, 900, 1099);
    else if (far > 0) {
        gDPPipeSync(POLY_OPA_DISP++);
        gDPSetFogColor(POLY_OPA_DISP++, r, g, b, 255);
        gSPFogPosition(POLY_OPA_DISP++, 0, far);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}
void BossTintOff(PlayState* play) {
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(POLY_OPA_DISP++);
    POLY_OPA_DISP = Play_SetFog(play, POLY_OPA_DISP);
    CLOSE_DISPS(play->state.gfxCtx);
}

// Plays `anim` on `sk` unless it already is (or `restart`). A negative speed plays it backwards, once.
void BossPlay(SkelAnime* sk, const void** playing, const char* anim, float speed = 1.0f, bool loop = true, bool restart = false, float morph = -4.0f) {
    if (anim == nullptr || (*playing == (const void*)anim && !restart)) return;
    const float last = Animation_GetLastFrame((void*)anim);
    if (speed < 0) Animation_Change(sk, (AnimationHeader*)anim, speed, last, 0.0f, ANIMMODE_ONCE, morph);
    else Animation_Change(sk, (AnimationHeader*)anim, speed, 0.0f, last, loop ? ANIMMODE_LOOP : ANIMMODE_ONCE, morph);
    *playing = anim;
}
// The game allocates the joint tables (sized for the skeleton) when they are null; Boss_Destroy frees them.
void BossInitSkel(PlayState* play, SkelAnime* sk, const char* skel, bool flex, const char* anim) {
    if (flex) SkelAnime_InitFlex(play, sk, (FlexSkeletonHeader*)skel, (AnimationHeader*)anim, nullptr, nullptr, 0);
    else SkelAnime_Init(play, sk, (SkeletonHeader*)skel, (AnimationHeader*)anim, nullptr, nullptr, 0);
}

// The newest strike from this boss that has not landed yet (Bongo's hands go where they are going to come down, the beams fly at them).
const StrikeFx* PendingStrikeOf(uint32_t owner) {
    const StrikeFx* best = nullptr;
    const double now = ImGui::GetTime();
    for (const StrikeFx& s : gStrikeFx) if (s.owner == owner && now < s.land && (best == nullptr || s.start > best->start)) best = &s;
    return best;
}

// ---- mini bosses --------------------------------------------------------------------------------------------------------------

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

// ---- the actor that stands in for each boss --------------------------------------------------------------------------------------------

// The mode changed: the moments that deserve a sound or a burst of the game's own particles.
void BossModeStarted(PlayState* play, BossActor& b, BM was) {
    const BM mode = ModeOf(b);
    const float ground = GroundY(play, b.x, b.z, b.actor->world.pos.y);
    const Vec3f at = { b.x, ground, b.z };
    switch (KindOf(b)) {
        case BK::Stone:
            if (mode == BM::Stunned && b.aux == 3) {   // knocked to pieces: bones everywhere, and it laughs
                Vec3f p = at;
                EffectSsHahen_SpawnBurst(play, &p, 8.0f, 0, 10, 6, 10, -1, 10, nullptr);
                SparkBurst(play, b.x, ground + 40.0f, b.z, { 255, 60, 40, 255 }, 18, 4.0f);
                BossSound(b, NA_SE_EN_STAL_DEAD);
            } else if (was == BM::Stunned && b.shownAux == 3) {   // ...and back together
                SparkBurst(play, b.x, ground + 70.0f, b.z, { 255, 40, 30, 255 }, 26, 3.0f);
                BossSound(b, NA_SE_EN_STAL_REBORN);
            } else if (mode == BM::Leap && b.aux == 1) BossSound(b, NA_SE_EN_STAL_JUMP);
            break;
        case BK::Lava:
            if (mode == BM::Breath) BossSound(b, NA_SE_EN_DODO_J_FIRE);
            else if (mode == BM::Charge) BossSound(b, NA_SE_EN_DODO_K_ROLL);
            break;
        case BK::Frost:
            if (mode == BM::Summon) {   // the howl: a ring of frost rolls out from it
                BossSound(b, NA_SE_EN_WOLFOS_CRY);
                Vec3f p = { b.x, ground + 20.0f, b.z }, v = { 0, 0, 0 }, a = { 0, 0, 0 };
                Color_RGBA8 prim = { 200, 235, 255, 255 }, env = { 60, 140, 255, 255 };
                EffectSsBlast_SpawnShockwave(play, &p, &v, &a, &prim, &env, 14);
            } else if (mode == BM::Leap && b.aux == 1) BossSound(b, NA_SE_EN_WOLFOS_ATTACK);
            break;
        case BK::Moss:
            if (mode == BM::Summon) BossSound(b, NA_SE_EN_RIZA_CRY);
            else if (mode == BM::Leap) BossSound(b, NA_SE_EN_RIZA_JUMP);
            break;
        case BK::Tide:
            if (mode == BM::Charge) BossSound(b, NA_SE_EN_DAIOCTA_VOICE);
            else if (mode == BM::Stunned) BossSound(b, NA_SE_EN_DAIOCTA_MAHI);
            break;
        case BK::Shade:
            if (mode == BM::Summon) BossSound(b, NA_SE_EN_DEADHAND_LAUGH);
            else if (mode == BM::Hidden) { BossSound(b, NA_SE_EN_DEADHAND_HIDE); Vec3f p = at; EffectSsHahen_SpawnBurst(play, &p, 6.0f, 0, 8, 6, 8, -1, 10, nullptr); }
            else if (mode == BM::Emerge) { BossSound(b, NA_SE_EN_DEADHAND_BITE); Vec3f p = at; EffectSsHahen_SpawnBurst(play, &p, 9.0f, 0, 10, 8, 12, -1, 10, nullptr); }
            break;
        case BK::Dune:
            if (mode == BM::Slam) BossSound(b, NA_SE_EN_IRONNACK_SWING_AXE);
            break;
        case BK::DragonFire:
            if (mode == BM::Hidden) { BossSound(b, NA_SE_EN_VALVAISA_LAND); Vec3f p = at; EffectSsGMagma_Spawn(play, &p); }
            else if (mode == BM::Emerge) {   // it bursts up out of the ground in a spray of lava and rock
                BossSound(b, NA_SE_EN_VALVAISA_APPEAR);
                Vec3f p = at, v = { 0, 0, 0 }, a = { 0, 0, 0 };
                EffectSsBomb2_SpawnLayered(play, &p, &v, &a, 120, 18);
                EffectSsHahen_SpawnBurst(play, &p, 14.0f, 0, 14, 10, 16, -1, 10, nullptr);
                for (int i = 0; i < 4; i++) { Vec3f m = { b.x + (Rand_ZeroOne() - 0.5f) * 200.0f, ground, b.z + (Rand_ZeroOne() - 0.5f) * 200.0f }; EffectSsGMagma_Spawn(play, &m); }
            } else if (mode == BM::Breath) BossSound(b, NA_SE_EN_VALVAISA_FIRE);
            else if (mode == BM::Landed) BossSound(b, NA_SE_EN_VALVAISA_LAND2);
            break;
        case BK::DragonWater: {
            const float water = WaterTop(b.x, b.z, ground);
            if (mode == BM::Emerge) {
                BossSound(b, NA_SE_EN_MOFER_APPEAR);
                Vec3f p = { b.x, water, b.z };
                EffectSsGSplash_Spawn(play, &p, nullptr, nullptr, 0, 900);
                Ripple(play, b.x, water, b.z, 100, 900);
            } else if (mode == BM::Slam) BossSound(b, NA_SE_EN_MOFER_ATTACK);
            else if (mode == BM::Stunned) BossSound(b, NA_SE_EN_MOFER_CORE_LAND);
            else if (mode == BM::Hidden) { Vec3f p = { b.x, water, b.z }; EffectSsGSplash_Spawn(play, &p, nullptr, nullptr, 0, 500); }
            break;
        }
        case BK::DragonForest:
            if (mode == BM::Hidden || mode == BM::Emerge) {   // into his portal, or out of one: a crackle of purple lightning
                BossSound(b, mode == BM::Hidden ? NA_SE_EN_FANTOM_TRANSFORM : NA_SE_EN_FANTOM_LAUGH);
                Vec3f p = { b.x, b.actor->world.pos.y + 60.0f, b.z };
                Color_RGBA8 prim = { 255, 200, 255, 255 }, env = { 160, 60, 255, 255 };
                EffectSsLightning_Spawn(play, &p, &prim, &env, 180, Rand_ZeroOne() * 0xFFFF, 6, 4);
                SparkBurst(play, b.x, b.actor->world.pos.y + 80.0f, b.z, { 200, 120, 255, 255 }, 30, 6.0f);
            } else if (mode == BM::Beam) BossSound(b, NA_SE_EN_FANTOM_MASIC1);
            else if (mode == BM::Cast) BossSound(b, NA_SE_EN_FANTOM_THUNDER);
            else if (mode == BM::Charge) BossSound(b, NA_SE_EN_FANTOM_ATTACK);
            else if (mode == BM::Stunned) BossSound(b, NA_SE_EN_FANTOM_DAMAGE2);
            break;
        case BK::DragonShadow:
            if (mode == BM::Hidden) BossSound(b, NA_SE_EN_SHADEST_DISAPPEAR);
            else if (mode == BM::Slam) BossSound(b, b.aux == 2 ? NA_SE_EN_SHADEST_CLAP : NA_SE_EN_SHADEST_HAND_FLY);
            else if (mode == BM::Swoop) BossSound(b, NA_SE_EN_SHADEST_FLY_ATTACK);
            break;
        case BK::DragonSand:
            if (mode == BM::Beam) BossSound(b, b.aux == 1 ? NA_SE_EN_TWINROBA_SHOOT_FREEZE : NA_SE_EN_TWINROBA_SHOOT_FIRE);
            else if (mode == BM::Summon) BossSound(b, NA_SE_EN_TWINROBA_POWERUP);
            else if (mode == BM::Swoop || mode == BM::Charge) BossSound(b, NA_SE_EN_TWINROBA_FLY);
            else if (mode == BM::Stunned) BossSound(b, NA_SE_EN_TWINROBA_DAMAGE_VOICE);
            break;
        default: break;
    }
}

void Boss_Update(Actor* actor, PlayState* play) {
    auto of = gBossOf.find(actor);
    if (of == gBossOf.end()) { Actor_Kill(actor); return; }
    BossActor& b = gBosses[of->second];
    const float dt = 1.0f / royale::kTickHz;
    if (!b.initialised) { b.x = b.tx; b.z = b.tz; b.rot = b.trot; b.initialised = true; }
    const float nx = b.x + (b.tx - b.x) * 0.4f, nz = b.z + (b.tz - b.z) * 0.4f;
    b.moved = b.moved * 0.8f + std::hypot(nx - b.x, nz - b.z);
    b.x = nx; b.z = nz;
    const s16 diff = static_cast<s16>(b.trot - b.rot);
    b.rot = static_cast<s16>(b.rot + diff * 0.35f);
    b.smashAge += dt;
    b.modeAge += dt;
    b.flashAge += dt;
    b.recoilAge += dt;
    b.hurtAge += royale::IsDragonKind(KindOf(b)) ? dt : 0.0f;   // the mini bosses count their own (MiniBoss_Update)
    if (royale::IsDragonKind(KindOf(b))) { if (b.hp < b.lastHp - 0.01f) b.hurtAge = 0.0f; b.lastHp = b.hp; }
    b.modeFresh = b.mode != b.shownMode || b.aux != b.shownAux;
    if (b.modeFresh) {
        const BM was = static_cast<BM>(b.shownMode < 0 ? b.mode : b.shownMode);
        if (b.shownMode >= 0) BossModeStarted(play, b, was);
        b.shownMode = b.mode;
        b.shownAux = b.aux;
        b.modeAge = 0.0f;
    }
    actor->world.pos.x = b.x;
    actor->world.pos.z = b.z;
    b.alt += (b.talt - b.alt) * 0.25f;
    const float ground = GroundY(play, b.x, b.z, royale::IsDragonKind(KindOf(b)) ? GET_PLAYER(play)->actor.world.pos.y : actor->world.pos.y);
    actor->world.pos.y = ground + b.alt;
    actor->shape.rot.y = b.rot;
    actor->world.rot.y = b.rot;
    // The Iron Knuckle's armour comes off at half health: it bursts off in pieces.
    if (KindOf(b) == BK::Dune && !b.armourOff && b.hp < 0.5f) {
        b.armourOff = true;
        Vec3f p = { b.x, ground + 80.0f, b.z };
        EffectSsHahen_SpawnBurst(play, &p, 10.0f, 0, 12, 8, 14, -1, 10, nullptr);
        SparkBurst(play, b.x, ground + 90.0f, b.z, { 255, 210, 90, 255 }, 30, 6.0f);
        BossSound(b, NA_SE_EN_IRONNACK_ARMOR_OFF_DEMO);
    }
    // How much of it shows.
    float want = 1.0f;
    const BM mode = ModeOf(b);
    if (mode == BM::Hidden) want = 0.0f;
    if (KindOf(b) == BK::Moss && (mode == BM::Chase || mode == BM::Patrol) && b.smashAge > 0.8f && b.hurtAge > 0.8f) {   // the Lizalfos melts into the grass...
        const Player* me = GET_PLAYER(play);
        const float d = std::hypot(me->actor.world.pos.x - b.x, me->actor.world.pos.z - b.z);
        want = d < 160.0f ? 0.6f : 0.12f;   // ...a shimmer you can just make out, until it strikes
    }
    b.fade += (want - b.fade) * (want > b.fade ? 0.35f : 0.12f);
    // Z-targeting, like any enemy: aim at the middle of the body, from as far off as the game allows for a big one. Not while it is hidden
    // (underground, under water, in shadow) or melted into the grass: nothing can hit it then, so there is nothing to lock on to.
    {
        const bool major = royale::IsDragonKind(KindOf(b));
        const float body = (major ? 90.0f : 60.0f) * royale::kBossDefs[b.kind].scale;
        actor->focus.pos = actor->world.pos;
        actor->focus.pos.y += body;
        actor->targetMode = major ? 5 : 4;   // the game's own ranges: 1000 and 700 units
        if (b.fade > 0.5f && mode != BM::Hidden) actor->flags |= ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE;
        else actor->flags &= ~(ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE);
    }
    // Spins and rolls.
    if ((KindOf(b) == BK::Tide || KindOf(b) == BK::Lava) && mode == BM::Charge) b.spin += KindOf(b) == BK::Tide ? 0.9f : 0.6f;
    else b.spin = 0.0f;
    switch (KindOf(b)) {
        case BK::DragonFire: Volvagia_Update(play, b); break;
        case BK::DragonWater: b.skReady = true; break;   // Morpha is drawn from pieces, not a skeleton
        case BK::DragonForest: PhantomGanon_Update(play, b); break;
        case BK::DragonShadow: Bongo_Update(play, b); break;
        case BK::DragonSand: Twinrova_Update(play, b); break;
        default: MiniBoss_Update(actor, play, b); break;
    }
}

void Boss_Draw(Actor* actor, PlayState* play) {
    auto of = gBossOf.find(actor);
    if (of == gBossOf.end()) return;
    BossActor& b = gBosses[of->second];
    switch (KindOf(b)) {
        case BK::DragonFire: Volvagia_Draw(actor, play, b); return;
        case BK::DragonWater: Morpha_Draw(actor, play, b); return;
        case BK::DragonForest: PhantomGanon_Draw(actor, play, b); return;
        case BK::DragonShadow: Bongo_Draw(actor, play, b, of->second); return;
        case BK::DragonSand: Twinrova_Draw(actor, play, b); return;
        default: break;
    }
    if (b.skReady) { MiniBoss_Draw(actor, play, b, of->second); return; }
    const GpuMesh* mesh = GpuMeshFor(royale::MeshKind::Golem, static_cast<uint32_t>(b.kind));   // the stand-in golem, until the model is ready
    if (mesh == nullptr || mesh->dl.empty()) return;
    const float t = static_cast<float>(play->gameplayFrames) / royale::kTickHz;
    const float bob = b.moved > 0.5f ? std::fabs(std::sin(t * 6.0f)) * 9.0f : std::sin(t * 1.5f) * 2.0f;
    const float lean = b.smashAge < 0.45f ? 0.55f * std::sin(b.smashAge / 0.45f * 3.14159f) : 0.0f; // a swing: it pitches forward
    const float scale = royale::kBossDefs[b.kind].scale;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y + bob, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(actor->shape.rot.y * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    Matrix_RotateX(lean, MTXMODE_APPLY);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(mesh->dl.data()));
    CLOSE_DISPS(play->state.gfxCtx);
}

void Boss_Destroy(Actor* actor, PlayState* play) {
    ActorFunc orig = nullptr;
    auto of = gBossOf.find(actor);
    if (of != gBossOf.end()) {
        auto b = gBosses.find(of->second);
        if (b != gBosses.end()) {
            orig = b->second.origDestroy;
            BossActor& ba = b->second;
            if (ba.skReady && KindOf(ba) != BK::DragonWater) SkelAnime_Free(&ba.sk, play);
            if (ba.handsReady) {
                SkelAnime_Free(&ba.hand[0], play);
                if (KindOf(ba) == BK::DragonShadow) SkelAnime_Free(&ba.hand[1], play);
            }
            gBosses.erase(b);
        }
        gBossOf.erase(of);
    }
    if (orig) orig(actor, play);
}

void ReconcileBosses(const royale::HudState& hud) {
    const bool show = gSession.Joined() && InField() && gSession.Client() &&
                      (hud.state == royale::MatchState::Drop || hud.state == royale::MatchState::InMatch || hud.state == royale::MatchState::Ending);
    std::unordered_map<uint32_t, bool> wanted;
    if (show) {
        for (const royale::net::BossNet& n : gSession.Client()->Bosses()) {
            const uint32_t id = n.Id();
            wanted[id] = true;
            gBossKindSeen[id] = n.kind;
            auto it = gBosses.find(id);
            if (it == gBosses.end()) {
                float y = 0;
                if (!FloorAt(n.x, n.z, &y)) {
                    if (!royale::IsDragonKind(static_cast<royale::BossKind>(n.kind))) continue;
                    y = GET_PLAYER(gPlayState)->actor.world.pos.y; // it flies: over a gap or the lava there may be no floor
                }
                Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ISHI, n.x, y, n.z, 0, n.rot, 0, 0, false);
                if (actor == nullptr) continue;
                BossActor b;
                b.actor = actor; b.origDestroy = actor->destroy; b.kind = n.kind; b.alt = b.talt = n.y; b.mode = n.mode; b.aux = n.aux;
                b.shownMode = n.mode; b.shownAux = n.aux;
                b.tx = n.x; b.tz = n.z; b.trot = n.rot; b.rot = n.rot; b.x = n.x; b.z = n.z; b.initialised = true;
                b.hp = b.lastHp = n.hp / 255.0f;
                b.armourOff = n.kind == static_cast<uint8_t>(BK::Dune) && b.hp < 0.5f;
                gBosses[id] = b;
                gBossOf[actor] = id;
                actor->update = Boss_Update;
                actor->draw = Boss_Draw;
                actor->destroy = Boss_Destroy;
                actor->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED;
                actor->uncullZoneForward = 5000.0f; actor->uncullZoneScale = 1500.0f; actor->uncullZoneDownward = 1500.0f;
                if (royale::IsDragonKind(static_cast<royale::BossKind>(n.kind))) {
                    actor->flags |= ACTOR_FLAG_DRAW_CULLING_DISABLED;
                    actor->uncullZoneForward = 12000.0f; actor->uncullZoneScale = 4000.0f; actor->uncullZoneDownward = 4000.0f;
                    actor->shape.shadowScale = 0.0f;
                } else {
                    actor->shape.shadowScale = 70.0f * royale::kBossDefs[n.kind].scale;
                }
            } else {
                BossActor& b = it->second;
                b.tx = n.x; b.tz = n.z; b.trot = n.rot; b.hp = n.hp / 255.0f; b.talt = n.y; b.mode = n.mode; b.aux = n.aux;
                if (n.smashing && b.smashAge > 0.3f) b.smashAge = 0.0f;
                if (b.actor) b.actor->shape.shadowScale = royale::BossHidden(static_cast<BM>(n.mode)) || b.fade < 0.5f ? 0.0f : (royale::IsDragonKind(KindOf(b)) ? 0.0f : 70.0f * royale::kBossDefs[n.kind].scale);
            }
        }
    }
    for (auto& [id, b] : gBosses) if (!wanted.count(id) && b.actor) Actor_Kill(b.actor);
}

// Name and health bar over each boss (not over one that is out of sight).
void DrawBossBars(ImDrawList* dl, ImFont* font, float scale) {
    if (!InField()) return;
    Player* pl = GET_PLAYER(gPlayState);
    for (const auto& [id, b] : gBosses) {
        if (!b.actor || b.fade < 0.3f) continue;
        const float dx = b.x - pl->actor.world.pos.x, dz = b.z - pl->actor.world.pos.z;
        const float d = std::sqrt(dx * dx + dz * dz);
        const bool major = royale::IsDragonKind(static_cast<royale::BossKind>(b.kind));
        if (d > (major ? 9000.0f : 3800.0f)) continue;
        ImVec2 at;
        if (!WorldToScreen(b.x, b.actor->world.pos.y + (major ? 300.0f : 330.0f * royale::kBossDefs[b.kind].scale), b.z, &at)) continue;
        const float w = 130.0f * scale * std::clamp(1500.0f / (d + 700.0f), 0.55f, 1.4f), h = 11.0f * scale;
        const char* name = royale::kBossDefs[b.kind].name;
        const float ts = 17.0f * scale * std::clamp(1500.0f / (d + 700.0f), 0.7f, 1.3f);
        const ImVec2 sz = font->CalcTextSizeA(ts, FLT_MAX, 0.0f, name);
        dl->AddText(font, ts, ImVec2(at.x - sz.x * 0.5f + 1.5f, at.y - h - sz.y + 1.5f), IM_COL32(0, 0, 0, 230), name);
        dl->AddText(font, ts, ImVec2(at.x - sz.x * 0.5f, at.y - h - sz.y), IM_COL32(255, 120, 90, 255), name);
        dl->AddRectFilled(ImVec2(at.x - w * 0.5f - 2, at.y - h - 2), ImVec2(at.x + w * 0.5f + 2, at.y + 2), IM_COL32(0, 0, 0, 200));
        const ImU32 col = b.hp > 0.5f ? IM_COL32(120, 220, 90, 255) : b.hp > 0.25f ? IM_COL32(240, 200, 60, 255) : IM_COL32(230, 70, 60, 255);
        dl->AddRectFilled(ImVec2(at.x - w * 0.5f, at.y - h), ImVec2(at.x - w * 0.5f + w * std::clamp(b.hp, 0.0f, 1.0f), at.y), col);
    }
}

// ---- effects ----------------------------------------------------------------------------------------------------------------------

struct StyleColours { Color_RGBA8 prim, env; };
StyleColours StyleColoursOf(royale::StrikeStyle s) {
    switch (s) {
        case royale::StrikeStyle::Bolt: return { { 210, 225, 255, 255 }, { 90, 120, 255, 255 } };
        case royale::StrikeStyle::Ice: return { { 225, 245, 255, 255 }, { 70, 150, 255, 255 } };
        case royale::StrikeStyle::Water: return { { 170, 230, 255, 255 }, { 0, 110, 255, 255 } };
        case royale::StrikeStyle::Shadow: return { { 200, 110, 255, 255 }, { 50, 0, 90, 255 } };
        case royale::StrikeStyle::Rock: return { { 235, 200, 140, 255 }, { 120, 80, 30, 255 } };
        case royale::StrikeStyle::Magic: return { { 220, 255, 190, 255 }, { 80, 210, 60, 255 } };
        case royale::StrikeStyle::Spore: return { { 210, 255, 130, 255 }, { 80, 160, 20, 255 } };
        default: return { { 255, 200, 60, 255 }, { 255, 60, 20, 255 } };
    }
}

// Things a strike does on its way down: Phantom Ganon's and Twinrova's blasts fly from their hands, the Lizalfos's spore pods arc through the air,
// rocks fall out of the sky.
bool StrikeFlies(const StrikeFx& s, const BossActor** from) {
    const BossActor* b = BossById(s.owner);
    if (b == nullptr) return false;
    const BK k = KindOf(*b);
    *from = b;
    switch (s.style) {
        case royale::StrikeStyle::Magic: return k == BK::DragonForest;
        case royale::StrikeStyle::Spore: return k == BK::Moss;
        case royale::StrikeStyle::Fire: case royale::StrikeStyle::Ice: return k == BK::DragonSand || (k == BK::DragonFire && ModeOf(*b) == BM::Cast);
        default: return false;
    }
}

void StrikeWarning(PlayState* play, StrikeFx& s, float ground, double now) {
    const float span = static_cast<float>(std::max(0.3, s.land - s.start));
    const float danger = std::clamp(1.0f - static_cast<float>(s.land - now) / span, 0.0f, 1.0f);   // sparks come faster as the blow gets close
    StyleColours c = StyleColoursOf(s.style);
    if (s.style == royale::StrikeStyle::Fire) c.prim.g = static_cast<u8>(210 - danger * 150.0f);
    const int n = 5 + static_cast<int>(danger * 9.0f);
    for (int i = 0; i < n; i++) {   // the ring on the ground, in the strike's colours
        const float a = Rand_ZeroOne() * 6.2831853f;
        Glitter(play, s.x + std::cos(a) * s.radius, ground + 6.0f, s.z + std::sin(a) * s.radius, c.prim, c.env, 1.2f + Rand_ZeroOne() * 1.5f, 25, 32);
    }
    const bool tick = (play->gameplayFrames + static_cast<u32>(s.x)) % 5 == 0;
    switch (s.style) {
        case royale::StrikeStyle::Water: if (tick) Ripple(play, s.x, WaterTop(s.x, s.z, ground), s.z, static_cast<s16>(s.radius * 0.3f), static_cast<s16>(s.radius * 2.0f)); break;
        case royale::StrikeStyle::Shadow: if (tick) Smoke(play, s.x + (Rand_ZeroOne() - 0.5f) * s.radius, ground + 10.0f, s.z + (Rand_ZeroOne() - 0.5f) * s.radius, { 60, 20, 90, 200 }, 60); break;
        case royale::StrikeStyle::Spore: if (tick) Glitter(play, s.x + (Rand_ZeroOne() - 0.5f) * s.radius, ground + 20.0f, s.z + (Rand_ZeroOne() - 0.5f) * s.radius, c.prim, c.env, 0.6f, 60, 40); break;
        case royale::StrikeStyle::Ice: if (tick) { Vec3f p = { s.x + (Rand_ZeroOne() - 0.5f) * s.radius, ground + 5.0f, s.z + (Rand_ZeroOne() - 0.5f) * s.radius }, v = { 0, 2.0f, 0 }, a = { 0, 0, 0 }; Color_RGBA8 pr = { 150, 150, 150, 250 }, en = { 235, 245, 255, 255 }; EffectSsEnIce_Spawn(play, &p, 0.5f, &v, &a, &pr, &en, 25); } break;
        default: break;
    }
    // Flying blasts: a ball of sparks travelling from the boss to where it will land.
    const BossActor* from = nullptr;
    if (StrikeFlies(s, &from)) {
        const float flight = std::min(1.0f, span);
        const float k = 1.0f - static_cast<float>(s.land - now) / flight;
        if (k >= 0.0f && k < 1.0f) {
            const float sy = from->actor->world.pos.y + (royale::IsDragonKind(KindOf(*from)) ? 120.0f : 70.0f);
            const float px = from->x + (s.x - from->x) * k, pz = from->z + (s.z - from->z) * k;
            const float arc = s.style == royale::StrikeStyle::Spore ? std::sin(k * 3.14159f) * 260.0f : 0.0f;
            const float py = sy + (ground + 20.0f - sy) * k + arc;
            for (int i = 0; i < 4; i++) Glitter(play, px + (Rand_ZeroOne() - 0.5f) * 20.0f, py + (Rand_ZeroOne() - 0.5f) * 20.0f, pz + (Rand_ZeroOne() - 0.5f) * 20.0f, c.prim, c.env, 0.0f, 120, 12);
            if (s.style == royale::StrikeStyle::Spore && tick) Smoke(play, px, py, pz, { 120, 200, 60, 160 }, 30);
        }
    }
    // Rocks out of the sky (Volvagia's eruption, Bongo's drumming): one falls onto the spot just before it lands.
    if (s.style == royale::StrikeStyle::Rock && !s.boomed) {
        const BossActor* b = BossById(s.owner);
        const double fall = 0.55;
        if (b && royale::IsDragonKind(KindOf(*b)) && now >= s.land - fall && now < s.land - fall + 0.06) {
            Vec3f p = { s.x, ground + 550.0f, s.z }, v = { 0, -18.0f, 0 }, a = { 0, -1.5f, 0 };
            EffectSsHahen_Spawn(play, &p, &v, &a, 0, 40, -1, 20, nullptr);
            EffectSsHahen_Spawn(play, &p, &v, &a, 0, 25, -1, 20, nullptr);
        }
    }
}

void StrikeBlast(PlayState* play, const StrikeFx& s, float ground) {
    const StyleColours c = StyleColoursOf(s.style);
    // A flash of the strike's own colour lights up whoever is near (a bolt is brighter and whiter).
    if (s.style == royale::StrikeStyle::Bolt) AddLightFlash(s.x, ground + 80.0f, s.z, 200, 215, 255, 600.0f, 0.5f, 3);
    else AddLightFlash(s.x, ground + 40.0f, s.z, c.prim.r, c.prim.g, c.prim.b, std::clamp(s.radius * 2.5f, 220.0f, 520.0f), 0.8f, 6);
    Vec3f pos = { s.x, ground + 20.0f, s.z }, zero = { 0, 0, 0 };
    const BossActor* owner = BossById(s.owner);
    switch (s.style) {
        case royale::StrikeStyle::Bolt:
            gBoltFlashUntil = ImGui::GetTime() + 0.35;
            for (int k = 0; k < 14; k++) {   // the bolt itself: a column of white sparks from the sky
                Glitter(play, s.x + (Rand_ZeroOne() - 0.5f) * 16.0f, ground + 40.0f + k * 55.0f, s.z + (Rand_ZeroOne() - 0.5f) * 16.0f, { 240, 245, 255, 255 }, { 120, 150, 255, 255 }, 0.0f, 260, 10);
            }
            SparkBurst(play, s.x, ground + 20.0f, s.z, { 190, 210, 255, 255 }, 30, 8.0f);
            SoundAt(s.x, ground, s.z, NA_SE_IT_BOMB_EXPLOSION);
            break;
        case royale::StrikeStyle::Ice: {   // shards of ice burst up and out, and a ring of frost
            for (int i = 0; i < 12; i++) {
                const float a = i * 0.5236f;
                Vec3f p = { s.x + std::cos(a) * s.radius * 0.4f, ground + 10.0f, s.z + std::sin(a) * s.radius * 0.4f };
                Vec3f v = { std::cos(a) * 4.0f, 6.0f + Rand_ZeroOne() * 4.0f, std::sin(a) * 4.0f }, acc = { 0, -0.8f, 0 };
                Color_RGBA8 pr = { 150, 150, 150, 250 }, en = { 235, 245, 255, 255 };
                EffectSsEnIce_Spawn(play, &p, 0.9f + Rand_ZeroOne() * 0.6f, &v, &acc, &pr, &en, 30);
            }
            Color_RGBA8 pr = c.prim, en = c.env;
            EffectSsBlast_SpawnShockwave(play, &pos, &zero, &zero, &pr, &en, 10);
            SparkBurst(play, s.x, ground + 20.0f, s.z, c.prim, 16, 5.0f);
            SoundAt(s.x, ground, s.z, NA_SE_EN_TWINROBA_MS_FREEZE);
            break;
        }
        case royale::StrikeStyle::Water: {   // a geyser
            Vec3f p = { s.x, WaterTop(s.x, s.z, ground), s.z };
            EffectSsGSplash_Spawn(play, &p, nullptr, nullptr, 0, static_cast<s16>(std::clamp(s.radius * 5.0f, 300.0f, 1000.0f)));
            EffectSsSibuki_SpawnBurst(play, &p);
            Ripple(play, p.x, p.y, p.z, 80, static_cast<s16>(s.radius * 3.0f));
            SoundAt(s.x, ground, s.z, NA_SE_EN_DAIOCTA_SPLASH);
            break;
        }
        case royale::StrikeStyle::Shadow:   // a puff of darkness (and the Dead Hand's hands, drawn by it)
            for (int i = 0; i < 5; i++) Smoke(play, s.x + (Rand_ZeroOne() - 0.5f) * s.radius, ground + 15.0f, s.z + (Rand_ZeroOne() - 0.5f) * s.radius, { 90, 30, 140, 220 }, 120);
            SparkBurst(play, s.x, ground + 20.0f, s.z, c.prim, 14, 4.0f);
            SoundAt(s.x, ground, s.z, owner && KindOf(*owner) == BK::Shade ? NA_SE_EN_DEADHAND_GRIP : NA_SE_EN_SHADEST_LAND);
            break;
        case royale::StrikeStyle::Rock: {   // the ground breaks: rocks thrown up, dust and a shockwave
            EffectSsHahen_SpawnBurst(play, &pos, 10.0f, 0, 14, 8, 10, -1, 10, nullptr);
            Color_RGBA8 pr = { 200, 170, 120, 255 }, en = { 120, 90, 50, 255 };
            EffectSsBlast_SpawnShockwave(play, &pos, &zero, &zero, &pr, &en, 8);
            for (int i = 0; i < 6; i++) {
                const float a = i * 1.0472f;
                Vec3f p = { s.x + std::cos(a) * s.radius * 0.5f, ground + 5.0f, s.z + std::sin(a) * s.radius * 0.5f };
                Vec3f v = { std::cos(a) * 3.0f, 1.0f, std::sin(a) * 3.0f }, acc = { 0, 0, 0 };
                EffectSsDust_Spawn(play, 0, &p, &v, &acc, &pr, &en, 300, 30, 18, 0);
            }
            SoundAt(s.x, ground, s.z, owner && !royale::IsDragonKind(KindOf(*owner)) ? NA_SE_EN_IRONNACK_HIT_GND : NA_SE_EN_VALVAISA_ROCK);
            break;
        }
        case royale::StrikeStyle::Magic: {   // Phantom Ganon's energy: a crackle of lightning and a ring of light
            Color_RGBA8 pr = c.prim, en = c.env;
            EffectSsLightning_Spawn(play, &pos, &pr, &en, 140, Rand_ZeroOne() * 0xFFFF, 6, 3);
            EffectSsBlast_SpawnShockwave(play, &pos, &zero, &zero, &pr, &en, 10);
            SparkBurst(play, s.x, ground + 20.0f, s.z, c.prim, 20, 6.0f);
            SoundAt(s.x, ground, s.z, NA_SE_EN_FANTOM_THUNDER_GND);
            break;
        }
        case royale::StrikeStyle::Spore:   // the pod bursts in a cloud of green
            for (int i = 0; i < 4; i++) Smoke(play, s.x + (Rand_ZeroOne() - 0.5f) * s.radius, ground + 20.0f, s.z + (Rand_ZeroOne() - 0.5f) * s.radius, { 120, 210, 60, 200 }, 110);
            SparkBurst(play, s.x, ground + 20.0f, s.z, c.prim, 18, 4.0f);
            SoundAt(s.x, ground, s.z, NA_SE_EN_GOMA_BJR_EGG2);
            break;
        default: {   // fire: the blast, and flames left burning on the ground
            EffectSsBomb2_SpawnLayered(play, &pos, &zero, &zero, static_cast<s16>(std::clamp(s.radius * 0.6f, 40.0f, 110.0f)), 14);
            SparkBurst(play, s.x, ground + 20.0f, s.z, { 255, 190, 60, 255 }, 26, 7.0f);
            const int flames = s.radius > 100.0f ? 4 : 2;
            for (int i = 0; i < flames; i++) {
                Vec3f p = { s.x + (Rand_ZeroOne() - 0.5f) * s.radius, ground, s.z + (Rand_ZeroOne() - 0.5f) * s.radius };
                EffectSsGFire_Spawn(play, &p);
            }
            SoundAt(s.x, ground, s.z, NA_SE_IT_BOMB_EXPLOSION);
            break;
        }
    }
}

// Each boss's own effects while it moves: fire breath, embers, frost, bubbles and ripples, dirt where something burrows, sparks, stars when dazed.
void UpdateBossBodyFx(PlayState* play) {
    const float t = BossTime(play);
    for (auto& [id, b] : gBosses) {
        if (!b.actor) continue;
        const BM mode = ModeOf(b);
        const float ang = b.rot * (3.14159265f / 32768.0f), fx = std::sin(ang), fz = std::cos(ang);
        const float ground = GroundY(play, b.x, b.z, b.actor->world.pos.y);
        const float y = b.actor->world.pos.y;
        b.fxClock += 1.0f / royale::kTickHz;
        const bool every = static_cast<int>(b.fxClock * 20.0f) % 4 == 0;
        auto breath = [&](float reach, float height, Color_RGBA8 prim, int count, float sp0) {   // a stream of sparks out of its mouth
            Color_RGBA8 env = { 255, 255, 255, 255 };
            for (int i = 0; i < count; i++) {
                const float spread = (Rand_ZeroOne() - 0.5f) * 0.45f, sp = sp0 + Rand_ZeroOne() * sp0 * 0.9f;
                const float dx = fx * std::cos(spread) - fz * std::sin(spread), dz = fz * std::cos(spread) + fx * std::sin(spread);
                Vec3f pos = { b.x + fx * reach, y + height, b.z + fz * reach };
                Vec3f vel = { dx * sp, -sp * 0.2f, dz * sp };
                Vec3f accel = { 0.0f, -0.3f, 0.0f };
                EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 30, 70);
            }
        };
        auto dazed = [&](float height) {   // stars circling its head
            for (int i = 0; i < 2; i++) {
                const float a = t * 5.0f + i * 3.14159f;
                Glitter(play, b.x + std::cos(a) * 45.0f, y + height, b.z + std::sin(a) * 45.0f, { 255, 255, 150, 255 }, { 255, 200, 0, 255 }, 0.0f, 60, 8);
            }
        };
        switch (KindOf(b)) {
            case BK::Stone:
                if (mode == BM::Stunned && b.aux == 3 && every) Glitter(play, b.x + (Rand_ZeroOne() - 0.5f) * 80.0f, ground + 10.0f, b.z + (Rand_ZeroOne() - 0.5f) * 80.0f, { 255, 60, 40, 255 }, { 180, 0, 0, 255 }, 2.0f, 60, 20);
                break;
            case BK::Lava:   // embers drift off its molten hide; fire out of its mouth; a roll throws up sparks
                if (every) Glitter(play, b.x + (Rand_ZeroOne() - 0.5f) * 90.0f, y + 40.0f + Rand_ZeroOne() * 60.0f, b.z + (Rand_ZeroOne() - 0.5f) * 90.0f, { 255, 140, 30, 255 }, { 255, 40, 0, 255 }, 1.6f, 40, 25);
                if (mode == BM::Breath && b.modeAge > 0.25f) breath(85.0f * MiniScale(b.kind) / 0.033f, 45.0f, { 255, 120, 30, 255 }, 10, 14.0f);
                if (mode == BM::Charge) SparkBurst(play, b.x, ground + 10.0f, b.z, { 255, 120, 30, 255 }, 2, 3.0f);
                if (mode == BM::Stunned) dazed(110.0f);
                break;
            case BK::Frost:   // frosty breath, and a cold glitter
                if (every) Glitter(play, b.x + fx * 50.0f, y + 60.0f, b.z + fz * 50.0f, { 230, 245, 255, 255 }, { 120, 180, 255, 255 }, 0.4f, 40, 20);
                if (mode == BM::Summon) {   // the howl: frost spreading out around it
                    const float r = 60.0f + b.modeAge * 380.0f;
                    for (int i = 0; i < 6; i++) { const float a = Rand_ZeroOne() * 6.2831853f; Glitter(play, b.x + std::cos(a) * r, ground + 8.0f, b.z + std::sin(a) * r, { 230, 245, 255, 255 }, { 80, 160, 255, 255 }, 1.0f, 50, 20); }
                }
                break;
            case BK::Moss:   // the odd spore drifting off it, so you can just about spot it
                if (every && b.fade > 0.05f && Rand_ZeroOne() < 0.5f) Glitter(play, b.x + (Rand_ZeroOne() - 0.5f) * 60.0f, y + 40.0f + Rand_ZeroOne() * 50.0f, b.z + (Rand_ZeroOne() - 0.5f) * 60.0f, { 200, 255, 120, 200 }, { 60, 140, 20, 255 }, 0.5f, 30, 25);
                break;
            case BK::Tide:   // a wake of ripples while it swims, spray while it spins, stars when it is dizzy
                if ((b.aux == 1 || b.moved > 2.0f) && every) Ripple(play, b.x, WaterTop(b.x, b.z, ground), b.z, 60, 300);
                if (mode == BM::Charge) { Vec3f p = { b.x, ground + 30.0f, b.z }; if (every) EffectSsSibuki_SpawnBurst(play, &p); }
                if (mode == BM::Stunned) dazed(150.0f);
                break;
            case BK::Shade:   // the dirt churning where it moves under the ground
                if (mode == BM::Hidden && every) {
                    Vec3f p = { b.x, ground + 5.0f, b.z }, v = { 0, 2.0f, 0 }, a = { 0, 0, 0 };
                    Color_RGBA8 pr = { 130, 100, 70, 255 }, en = { 70, 50, 30, 255 };
                    EffectSsDust_Spawn(play, 0, &p, &v, &a, &pr, &en, 200, 20, 14, 0);
                    EffectSsHahen_Spawn(play, &p, &v, &a, 0, 6, -1, 10, nullptr);
                }
                break;
            case BK::Dune:
                if (mode == BM::Stunned && every) { Vec3f p = { b.x + fx * 120.0f, ground + 5.0f, b.z + fz * 120.0f }, v = { 0, 1.0f, 0 }, a = { 0, 0, 0 }; Color_RGBA8 pr = { 220, 190, 130, 255 }, en = { 140, 110, 60, 255 }; EffectSsDust_Spawn(play, 0, &p, &v, &a, &pr, &en, 150, 15, 12, 0); }
                if (b.armourOff && every) Glitter(play, b.x, y + 90.0f, b.z, { 255, 220, 120, 255 }, { 255, 120, 0, 255 }, 1.5f, 30, 15);   // steaming with fury
                break;
            case BK::DragonFire:   // lava welling up where it is under the ground; fire out of its mouth
                if (mode == BM::Hidden) {
                    if (every) { Vec3f p = { b.x, ground, b.z }; EffectSsGMagma_Spawn(play, &p); }
                    SparkBurst(play, b.x, ground + 10.0f, b.z, { 255, 120, 30, 255 }, 1, 4.0f);
                }
                if (mode == BM::Breath) breath(190.0f, 120.0f, { 255, 120, 40, 255 }, 14, 20.0f);
                break;
            case BK::DragonWater: {   // ripples and bubbles where it moves under the water; drips off the tentacle
                const float water = WaterTop(b.x, b.z, ground);
                if (mode == BM::Hidden) {
                    if (every) Ripple(play, b.x, water, b.z, 80, 500);
                    if (Rand_ZeroOne() < 0.3f) { Vec3f p = { b.x, water, b.z }; EffectSsBubble_Spawn(play, &p, 0.0f, 10.0f, 60.0f, 0.06f); }
                } else if (every) {
                    Ripple(play, b.x, water, b.z, 150, 600);
                    if (mode == BM::Slam) { Vec3f p = { b.x + fx * 200.0f, water + 40.0f, b.z + fz * 200.0f }; EffectSsSibuki_SpawnBurst(play, &p); }
                }
                break;
            }
            case BK::DragonForest:   // a trail of dark energy when he charges; the storm gathering in his hands
                if (mode == BM::Charge) SparkBurst(play, b.x, y + 80.0f, b.z, { 180, 90, 255, 255 }, 3, 2.0f);
                if (mode == BM::Cast || mode == BM::Beam) Glitter(play, b.x + fx * 40.0f, y + 150.0f, b.z + fz * 40.0f, { 230, 255, 200, 255 }, { 90, 220, 60, 255 }, 0.0f, 90, 10);
                if (mode == BM::Stunned) dazed(220.0f);
                break;
            case BK::DragonShadow:   // shadowy mist where he is, seen or not; a shockwave off every beat of his drum
                if (every) Smoke(play, b.x + (Rand_ZeroOne() - 0.5f) * 200.0f, ground + 40.0f, b.z + (Rand_ZeroOne() - 0.5f) * 200.0f, { 60, 20, 90, mode == BM::Hidden ? (u8)140 : (u8)90 }, 160);
                if (mode == BM::Summon && static_cast<int>(b.modeAge * 9.0f / 3.14159f) != static_cast<int>((b.modeAge - 0.05f) * 9.0f / 3.14159f)) {
                    for (int h = 0; h < 2; h++) {
                        const Vec3f at = BongoHandAt(play, b, id, h);
                        Vec3f p = { at.x, ground + 10.0f, at.z }, zero = { 0, 0, 0 };
                        EffectSsBlast_SpawnWhiteShockwave(play, &p, &zero, &zero);
                    }
                    BossSound(b, static_cast<int>(b.modeAge * 9.0f / 3.14159f) & 1 ? NA_SE_EN_SHADEST_TAIKO_HIGH : NA_SE_EN_SHADEST_TAIKO_LOW);
                }
                break;
            case BK::DragonSand: {   // Koume's fire on one side of her, Kotake's ice on the other
                const float rx = std::cos(ang), rz = -std::sin(ang);
                Glitter(play, b.x + rx * 70.0f, y + 150.0f, b.z + rz * 70.0f, { 255, 170, 40, 255 }, { 255, 40, 0, 255 }, 1.2f, 70, 14);
                Glitter(play, b.x - rx * 70.0f, y + 150.0f, b.z - rz * 70.0f, { 220, 240, 255, 255 }, { 60, 140, 255, 255 }, 1.2f, 70, 14);
                if (mode == BM::Charge || mode == BM::Swoop) SparkBurst(play, b.x - fx * 60.0f, y + 40.0f, b.z - fz * 60.0f, b.aux == 1 ? Color_RGBA8{ 200, 230, 255, 255 } : Color_RGBA8{ 255, 160, 60, 255 }, 3, 2.0f);
                if (mode == BM::Stunned) dazed(260.0f);
                break;
            }
            default: break;
        }
    }
}

// Every game frame: the strikes on the ground and each boss's own effects.
void UpdateBossWorldFx() {
    if (!InField() || gPlayState == nullptr) { gStrikeFx.clear(); return; }
    const double now = ImGui::GetTime();
    Player* pl = GET_PLAYER(gPlayState);
    for (StrikeFx& s : gStrikeFx) {
        const float ground = GroundY(gPlayState, s.x, s.z, pl->actor.world.pos.y);
        if (now < s.land) StrikeWarning(gPlayState, s, ground, now);
        else if (!s.boomed) { s.boomed = true; StrikeBlast(gPlayState, s, ground); }
    }
    gStrikeFx.erase(std::remove_if(gStrikeFx.begin(), gStrikeFx.end(), [&](const StrikeFx& s) { return now > s.land + 1.0; }), gStrikeFx.end());
    UpdateBossBodyFx(gPlayState);
}

// The sound everyone hears when a major boss arrives.
u16 BossArrivalSound(int kind) {
    switch (static_cast<BK>(kind)) {
        case BK::DragonWater: return NA_SE_EN_MOFER_APPEAR;
        case BK::DragonForest: return NA_SE_EN_FANTOM_LAUGH;
        case BK::DragonShadow: return NA_SE_EN_SHADEST_TAIKO_HIGH;
        case BK::DragonSand: return NA_SE_EN_TWINROBA_LAUGH;
        default: return NA_SE_EN_VALVAISA_ROAR;
    }
}

// A strike the server just announced.
void AddStrikeFx(float x, float z, float radius, float delay, uint8_t style, uint16_t owner) {
    const double now = ImGui::GetTime();
    const royale::StrikeStyle st = style < static_cast<uint8_t>(royale::StrikeStyle::Count) ? static_cast<royale::StrikeStyle>(style) : royale::StrikeStyle::Fire;
    uint32_t id = 0;
    for (const auto& [bid, b] : gBosses) if (static_cast<uint16_t>(bid) == owner) id = bid;
    gStrikeFx.push_back({ x, z, radius, now, now + std::max(0.3f, delay), false, st, id });
}
