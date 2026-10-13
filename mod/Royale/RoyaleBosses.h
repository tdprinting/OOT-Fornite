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
    SfxAt(sfx, x, y, z);
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

#include "bosses/MiniBosses.inc"
#include "bosses/Volvagia.inc"
#include "bosses/MajorBosses.inc"
#include "bosses/BossActor.inc"
#include "bosses/StrikeFx.inc"
