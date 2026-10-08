// ---- Avriella's toys: a stuffed Bluey, a little television and a laptop ----------------------------------------------------------------
// Made in Blender (tools/avriella/build_toys.py, assets/avriella/toys.blend; the plush is about 1300 triangles, the TV about 600, the laptop about 400, all with tiny
// textures) and drawn from shared/avriella_toys.h. Each is a few stiff parts joined at pivots, posed by angles every frame, so the plush's arms, legs, ears and
// head move however the game wants. The TV and the laptop show a little picture of the pup (two cheering frames, two of static); the sounds are original
// (shared/avriella_toy_sounds.h: a xylophone tune, slide whistle, boing, bonk, ta-da).
struct ToyModel {
    const royale::avriella_toys::Part* parts; int partCount;
    const royale::avriella_toys::Vert* verts; int vertCount;
    const royale::avriella_toys::Batch* batches; int batchCount;
    const uint8_t (*tris)[3];
};
constexpr int kToyMaxParts = 10;
struct ToyPose {
    float r[kToyMaxParts][3] = {};          // each part's turn about its pivot (x, then y, then z), radians
    float off[3] = {};                      // the whole toy shifted (model units)
    float sx = 1, sy = 1, sz = 1;           // squash and stretch of the whole toy
    int screen = royale::avriella_toys::kTexBlueyA;   // which picture is on the glass
    float glow = 1.0f;                      // how bright the glass is
};
const ToyModel& PlushModel() {
    namespace T = royale::avriella_toys;
    static const ToyModel m = { T::kPlushParts, T::kPlushPartCount, T::kPlushVerts, T::kPlushVertCount, T::kPlushBatches, T::kPlushBatchCount, T::kPlushTris };
    return m;
}
const ToyModel& TvModel() {
    namespace T = royale::avriella_toys;
    static const ToyModel m = { T::kTvParts, T::kTvPartCount, T::kTvVerts, T::kTvVertCount, T::kTvBatches, T::kTvBatchCount, T::kTvTris };
    return m;
}
const ToyModel& LaptopModel() {
    namespace T = royale::avriella_toys;
    static const ToyModel m = { T::kLaptopParts, T::kLaptopPartCount, T::kLaptopVerts, T::kLaptopVertCount, T::kLaptopBatches, T::kLaptopBatchCount, T::kLaptopTris };
    return m;
}
// the order of the plush's parts (kPlushPartNames)
enum PlushPart { kPlBody, kPlHead, kPlEarL, kPlEarR, kPlArmL, kPlArmR, kPlLegL, kPlLegR, kPlTail };

struct ToyXf { float m[9]; float t[3]; };   // a turn (row-major) and a shift
ToyXf ToyMul(const ToyXf& a, const ToyXf& b) {   // a after b: apply b, then a
    ToyXf o;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) o.m[i * 3 + j] = a.m[i * 3] * b.m[j] + a.m[i * 3 + 1] * b.m[3 + j] + a.m[i * 3 + 2] * b.m[6 + j];
        o.t[i] = a.m[i * 3] * b.t[0] + a.m[i * 3 + 1] * b.t[1] + a.m[i * 3 + 2] * b.t[2] + a.t[i];
    }
    return o;
}
ToyXf ToyTurn(float rx, float ry, float rz, float tx, float ty, float tz) {   // Rz * Ry * Rx, then the shift
    const float cx = std::cos(rx), sx = std::sin(rx), cy = std::cos(ry), sy = std::sin(ry), cz = std::cos(rz), sz = std::sin(rz);
    ToyXf o = { { cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx,
                  sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx,
                  -sy, cy * sx, cy * cx }, { tx, ty, tz } };
    return o;
}

// Draws a toy standing at (x, y, z) turned `yaw`, at `scale` (the same scale as Avriella herself).
void DrawToy(PlayState* play, const ToyModel& model, float x, float y, float z, float yaw, float scale, const ToyPose& pose) {
    GfxLayer layer(play, GfxLayerId::Characters);   // its thousands of vertices and commands go in the layer pool, not the game's buffer
    namespace T = royale::avriella_toys;
    if (model.partCount > kToyMaxParts || scale < 0.01f) return;
    constexpr float kSub = 8.0f;
    Vtx* vtx = static_cast<Vtx*>(FrameAlloc(play, sizeof(Vtx) * model.vertCount));
    if (vtx == nullptr) return;
    ToyXf world[kToyMaxParts];
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    for (int i = 0; i < model.partCount; i++) {
        const T::Part& pt = model.parts[i];
        const ToyXf local = ToyTurn(pose.r[i][0], pose.r[i][1], pose.r[i][2], pt.ox, pt.oy, pt.oz);
        if (pt.parent < 0) {
            const ToyXf root = { { cy * pose.sx, 0, sy * pose.sz, 0, pose.sy, 0, -sy * pose.sx, 0, cy * pose.sz }, { 0, 0, 0 } };   // turn about Y after the squash
            ToyXf withOff = local;
            withOff.t[0] += pose.off[0]; withOff.t[1] += pose.off[1]; withOff.t[2] += pose.off[2];
            world[i] = ToyMul(root, withOff);
        } else {
            world[i] = ToyMul(world[pt.parent], local);
        }
    }
    const float lx = 0.35f, ly = 0.82f, lz = 0.45f;
    for (int i = 0; i < model.partCount; i++) {
        const T::Part& pt = model.parts[i];
        const ToyXf& w = world[i];
        for (int k = 0; k < pt.vertCount; k++) {
            const T::Vert& v = model.verts[pt.firstVert + k];
            float p[3], n[3];
            for (int a = 0; a < 3; a++) {
                p[a] = w.m[a * 3] * v.x + w.m[a * 3 + 1] * v.y + w.m[a * 3 + 2] * v.z + w.t[a];
                n[a] = w.m[a * 3] * v.nx + w.m[a * 3 + 1] * v.ny + w.m[a * 3 + 2] * v.nz;
            }
            const float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            const float lit = nl > 1e-4f ? std::clamp(0.55f + 0.55f * std::max(0.0f, (n[0] * lx + n[1] * ly + n[2] * lz) / nl), 0.0f, 1.0f) : 0.8f;
            Vtx& o = vtx[pt.firstVert + k];
            for (int a = 0; a < 3; a++) o.v.ob[a] = static_cast<s16>(std::lround(std::clamp(p[a] * kSub, -32000.0f, 32000.0f)));
            o.v.flag = 0;
            o.v.tc[0] = v.s;
            o.v.tc[1] = v.t;
            o.v.cn[0] = static_cast<u8>(255.0f * lit);
            o.v.cn[1] = static_cast<u8>(250.0f * lit);
            o.v.cn[2] = static_cast<u8>(245.0f * lit);
            o.v.cn[3] = 255;
        }
    }
    for (int b = 0; b < model.batchCount; b++) {   // the glass glows: lit the same all over, brighter or dimmer with `glow`
        const T::Batch& bt = model.batches[b];
        if (bt.texture != T::kTexScreen) continue;
        const u8 g = static_cast<u8>(255.0f * std::clamp(pose.glow, 0.0f, 1.0f));
        for (int k = 0; k < bt.vertCount; k++) vtx[bt.firstVert + k].v.cn[0] = vtx[bt.firstVert + k].v.cn[1] = vtx[bt.firstVert + k].v.cn[2] = g;
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIDECALA, G_CC_PASS2);
    Matrix_Translate(x, y, z, MTXMODE_NEW);
    Matrix_Scale(scale / kSub, scale / kSub, scale / kSub, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    int loaded = -1;
    for (int b = 0; b < model.batchCount; b++) {
        const T::Batch& bt = model.batches[b];
        const int tex = bt.texture == T::kTexScreen ? std::clamp(pose.screen, static_cast<int>(T::kTexBlueyA), static_cast<int>(T::kTexOff)) : bt.texture;
        if (tex != loaded) {
            gDPLoadTextureBlock(POLY_OPA_DISP++, T::kTextures[tex].data, G_IM_FMT_RGBA, G_IM_SIZ_16b, T::kTextures[tex].w, T::kTextures[tex].h, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                                G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            loaded = tex;
        }
        gSPVertex(POLY_OPA_DISP++, reinterpret_cast<uintptr_t>(&vtx[bt.firstVert]), bt.vertCount, 0);
        const int end = bt.firstTri + bt.triCount;
        int t = bt.firstTri;
        for (; t + 1 < end; t += 2)
            gSP2Triangles(POLY_OPA_DISP++, model.tris[t][0], model.tris[t][1], model.tris[t][2], 0, model.tris[t + 1][0], model.tris[t + 1][1], model.tris[t + 1][2], 0);
        if (t < end) gSP1Triangle(POLY_OPA_DISP++, model.tris[t][0], model.tris[t][1], model.tris[t][2], 0);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// The toys' sounds, on two voices of their own (the tune, and the effects over it).
void ToySfx(int clip, float gain = 0.8f) {
    namespace S = royale::avriella_toy_snd;
    static std::shared_ptr<const std::vector<int16_t>> cache[S::kSfxCount];
    const float volume = GameVolume(false) * gain;
    if (clip < 0 || clip >= S::kSfxCount || volume < 0.01f) return;
    if (!cache[clip]) cache[clip] = std::make_shared<const std::vector<int16_t>>(S::kClips[clip].data, S::kClips[clip].data + S::kClips[clip].count);
    StartVoice(clip == S::kTune ? kVoiceToy : kVoiceToySfx, cache[clip], false, S::kRate, false, volume);
}
void ToyStopTune() { StopVoice(kVoiceToy); }

float ToyEaseBack(float k) {   // 0..1 with a little overshoot: the pop out of the ground
    k = std::clamp(k, 0.0f, 1.0f);
    const float c1 = 2.2f, c3 = c1 + 1.0f, u = k - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

enum class ToyKind { None, Plush, Tv, Laptop };
struct BabyToy {
    ToyKind kind = ToyKind::None;
    int variant = 0;          // which way she plays with the plush: 0 hug, 1 swing it, 2 chew it, 3 make it dance, 4 wave it about
    float len = 10.0f;        // how long the game lasts
    float nextSfx = 3.0f;
    float x = 0, z = 0, y = 0;   // where a TV or laptop stands
    bool tuneOn = false;
};
BabyToy gToy;
constexpr float kToyAppearSeconds = 0.7f, kToyLeaveSeconds = 0.5f;
// 0 to about 1.1 as a toy pops out (a little overshoot), 1 while it plays, down to 0 as it goes away
float ToyScale(float t, float len) { return ToyEaseBack(t / kToyAppearSeconds) * std::clamp((len - t) / kToyLeaveSeconds, 0.0f, 1.0f); }

// Her voice: Link's child voice sounds, pitched way up and strung together into babble. Each coo is a few sounds a moment apart.
enum BabyCoo { kCooBaba, kCooDada, kCooGiggle, kCooOoh, kCooYawn, kCooHi, kCooBoth, kCooChew, kCooCount };
struct PendingCoo { float at; u16 sfx; float pitch, volume; };
std::vector<PendingCoo> gBabyCoos;

void BabyCooNow(Actor* a, u16 sfx, float pitch, float volume) {
    if (a == nullptr || volume < 0.01f) return;
    f32 freq = pitch, vol = volume;
    Audio_PlaySoundGeneral(sfx, &a->projectedPos, 4, &freq, &vol, &gSfxDefaultReverb);
}

// The real Avriella. Her recordings (assets/avriella/sounds, shared/avriella_sounds.h) are sorted by how they sound: laughs, babbling, little "hi"
// squeals, soft coos and low grunts. A coo asks for one kind; a random clip of that kind plays (never the one before) on a voice of its own, and
// only when she is not in the middle of one and has had a few seconds' quiet, so she is not constant. Without a recording the Link-voice babble
// below plays instead.
enum BabyClipKind { kBabyLaugh, kBabyBabble, kBabyHi, kBabyCoo, kBabyLow, kBabyKindCount };
constexpr int kBabyKindClips[kBabyKindCount][4] = {   // by file number (avriella_NN.wav); 0 ends a list
    { 7, 10, 18, 0 }, { 1, 4, 17, 0 }, { 2, 11, 13, 20 }, { 8, 19, 21, 0 }, { 3, 14, 0, 0 } };
double gBabyVoiceUntil = 0, gBabyVoiceQuietUntil = 0;
bool gBabyVoiceLaugh = false;

bool BabyVoice(int kind, bool force = false) {
    namespace S = royale::avriella_snd;
    constexpr int kClipCount = sizeof(S::kClips) / sizeof(S::kClips[0]);
    static std::shared_ptr<const std::vector<int16_t>> cache[kClipCount];
    static int last = -1;
    const double now = ImGui::GetTime();
    const float volume = GameVolume(false) * 0.9f;
    if (kind < 0 || kind >= kBabyKindCount || volume < 0.01f || now < gBabyVoiceUntil || (!force && now < gBabyVoiceQuietUntil)) return false;
    int pool[4], n = 0;
    for (int number : kBabyKindClips[kind]) {
        for (int i = 0; number != 0 && i < kClipCount; i++) if (S::kClips[i].number == number && i != last) pool[n++] = i;
    }
    if (n == 0) return false;
    const int pick = pool[static_cast<int>(Rand_ZeroOne() * static_cast<float>(n)) % n];
    last = pick;
    if (!cache[pick]) cache[pick] = std::make_shared<const std::vector<int16_t>>(S::kClips[pick].data, S::kClips[pick].data + S::kClips[pick].count);
    StartVoice(kVoiceBaby, cache[pick], false, S::kRate, false, volume);
    const double len = static_cast<double>(S::kClips[pick].count) / S::kRate;
    gBabyVoiceUntil = now + len;
    gBabyVoiceQuietUntil = now + len + 2.0 + static_cast<double>(Rand_ZeroOne()) * 3.0;
    gBabyVoiceLaugh = kind == kBabyLaugh;
    return true;
}

void BabyCoo(int kind, bool force = false) {
    switch (kind) {
        case kCooBaba: case kCooDada: case kCooBoth: if (BabyVoice(kBabyBabble, force)) return; break;
        case kCooGiggle: if (BabyVoice(kBabyLaugh, force)) return; break;
        case kCooOoh: if (BabyVoice(kBabyCoo, force)) return; break;
        case kCooYawn: if (BabyVoice(kBabyLow, force)) return; break;
        case kCooHi: if (BabyVoice(kBabyHi, force)) return; break;
        default: break;   // chewing stays a quiet gnaw
    }
    if (kind != kCooChew && ImGui::GetTime() < gBabyVoiceUntil) return;   // a recording is playing: no synth babble over it
    auto add = [](float at, u16 sfx, float pitch, float volume = 0.8f) { if (gBabyCoos.size() < 24) gBabyCoos.push_back({ at, sfx, pitch, volume }); };
    switch (kind) {
        case kCooBaba: add(0.0f, NA_SE_VO_LI_SWORD_N, 2.0f); add(0.24f, NA_SE_VO_LI_SWORD_N, 2.2f); add(0.5f, NA_SE_VO_LI_SWORD_N, 1.9f); break;
        case kCooDada: add(0.0f, NA_SE_VO_LI_AUTO_JUMP, 2.3f); add(0.26f, NA_SE_VO_LI_AUTO_JUMP, 2.1f); break;
        case kCooGiggle:
            for (int i = 0; i < 5; i++) add(0.14f * static_cast<float>(i), NA_SE_VO_LI_SWORD_N, 2.3f + 0.15f * static_cast<float>(i % 3), 0.6f);
            break;
        case kCooOoh: add(0.0f, NA_SE_VO_LI_SURPRISE, 1.8f); break;
        case kCooChew:
            for (int i = 0; i < 4; i++) add(0.2f * static_cast<float>(i), NA_SE_VO_LI_SWORD_N, 1.5f + 0.1f * static_cast<float>(i % 2), 0.35f);
            break;
        case kCooYawn: add(0.0f, NA_SE_VO_LI_SWORD_L, 1.2f, 0.6f); break;
        case kCooHi: add(0.0f, NA_SE_VO_LI_SURPRISE, 2.1f); add(0.22f, NA_SE_VO_LI_SWORD_N, 2.3f); break;
        default: add(0.0f, NA_SE_VO_LI_SWORD_N, 2.0f); add(0.22f, NA_SE_VO_LI_AUTO_JUMP, 2.3f); add(0.46f, NA_SE_VO_LI_SWORD_N, 2.1f); add(0.7f, NA_SE_VO_LI_AUTO_JUMP, 2.2f); break;
    }
}

enum class BabyMood { Crawl, Sit, Wave, Giggle, Clap, Babble, Roll, Nap, Stand, Reach, Stack, Kick, Chew, Cap, Talk, Plush, Tv, Laptop };   // Crawl is how she gets about: she cannot crawl yet, so she rolls
struct BabyBrain {
    Actor* actor = nullptr;
    BabyMood mood = BabyMood::Sit;
    float moodT = 0, idle = 0, sitT = 0, nextAt = 5.0f;
    float x = 0, z = 0, y = 0, yaw = 0, speed = 0;
    float reachX = 0, reachZ = 0, reachCool = 6.0f;   // where the thing she is reaching for is, and a pause before she reaches again
    float cooIn = 20.0f, sparkleIn = 0.0f;
    float toyCool = 25.0f, laptopCool = 0.0f;   // seconds before she may pull out a toy (the plush or TV) again, and the laptop
    size_t pickups = 0;
    bool placed = false;
    bool sleepy = false;      // just woke: heavy eyes for a moment
    int line = 0;             // what she says next (royale::kAvriellaPetLines)
    int face = 0;
    royale::TailTracker tail;                          // her tuft's cloth springs (royale::ApplyTuft)
    royale::HatSpring capSpring;                       // the green cap she waves about: its tail swings with her hand
    float hx = 0, hy = 0, hz = 0, hvx = 0, hvy = 0, hvz = 0;   // where her hand was and how fast it was going last frame
    double capSeen = 0, tugAt = 0;
    bool haveHand = false;
    PetWander wander;
    royale::avriella::Animator anim;
};
BabyBrain gBaby;

void SetBabyMood(BabyBrain& c, BabyMood m) { c.mood = m; c.moodT = 0; }

// Where she puts a TV or laptop: a little way off to one side, so you see the screen from the front-ish and she turns to look at it.
void BabyToySpot(PlayState* play, BabyBrain& c, float dist) {
    const float a = c.yaw + 0.9f;
    gToy.x = c.x + std::sin(a) * dist;
    gToy.z = c.z + std::cos(a) * dist;
    gToy.y = GroundY(play, gToy.x, gToy.z, c.y);
}

void StartBabyToy(PlayState* play, BabyBrain& c, ToyKind kind) {
    gToy = BabyToy{};
    gToy.kind = kind;
    gToy.nextSfx = 3.5f;
    c.haveHand = false;
    if (kind == ToyKind::Plush) {
        gToy.variant = std::min(4, static_cast<int>(Rand_ZeroOne() * 5.0f));
        gToy.len = 9.0f;
        SetBabyMood(c, BabyMood::Plush);
        c.toyCool = 40.0f;
        BabyToySpot(play, c, 42.0f);   // (only the dance uses it)
        CatPoof(play, c.x - std::sin(c.yaw) * 10.0f, c.y + 6.0f, c.z - std::cos(c.yaw) * 10.0f);
        ToySfx(royale::avriella_toy_snd::kPop, 0.8f);
    } else if (kind == ToyKind::Tv) {
        gToy.len = 16.0f;
        SetBabyMood(c, BabyMood::Tv);
        c.toyCool = 50.0f;
        BabyToySpot(play, c, 64.0f);
        CatPoof(play, gToy.x, gToy.y + 4.0f, gToy.z);
        ToySfx(royale::avriella_toy_snd::kBoing, 0.8f);
    } else {
        gToy.len = 22.0f;
        SetBabyMood(c, BabyMood::Laptop);
        c.laptopCool = 120.0f;
        BabyToySpot(play, c, 38.0f);
        CatPoof(play, gToy.x, gToy.y + 4.0f, gToy.z);
        ToySfx(royale::avriella_toy_snd::kPop, 0.8f);
    }
    BabyCoo(kCooOoh);
}

void EndBabyToy(BabyBrain& c, bool quietly) {
    if (gToy.kind != ToyKind::None && !quietly) ToySfx(royale::avriella_toy_snd::kOff, 0.7f);
    ToyStopTune();
    gToy = BabyToy{};
}

void BabySparkle(PlayState* play, float x, float y, float z) {
    Vec3f pos = { x + (Rand_ZeroOne() - 0.5f) * 26.0f, y + 8.0f + Rand_ZeroOne() * 14.0f, z + (Rand_ZeroOne() - 0.5f) * 26.0f };
    Vec3f vel = { (Rand_ZeroOne() - 0.5f) * 1.0f, 0.5f + Rand_ZeroOne() * 0.6f, (Rand_ZeroOne() - 0.5f) * 1.0f }, accel = { 0, 0, 0 };
    Color_RGBA8 prim = { 255, 225, 235, 255 }, env = { 255, 150, 190, 255 };
    EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 100, 24);
}

// The nearest loot (an item on the ground or a closed chest) within `range` of a spot, or nullptr.
const Actor* NearestLootTo(float x, float z, float range) {
    const Actor* best = nullptr;
    float bestD = range;
    for (const auto& entry : gLoot) {
        const LootActor& la = entry.second;
        if (la.actor == nullptr || la.killing || la.opened) continue;
        const float d = std::hypot(la.actor->world.pos.x - x, la.actor->world.pos.z - z);
        if (d < bestD) { bestD = d; best = la.actor; }
    }
    return best;
}

void Baby_Update(Actor* actor, PlayState* play) {
    if (SocialPet_Update(actor,play,1)) return;
    namespace A = royale::avriella;
    BabyBrain& c = gBaby;
    const float dt = 1.0f / royale::kTickHz;
    Player* pl = GET_PLAYER(play);
    const float px = pl->actor.world.pos.x, pz = pl->actor.world.pos.z, py = pl->actor.world.pos.y;
    const float pyaw = pl->actor.shape.rot.y * (3.14159265f / 32768.0f);
    const float pspeed = std::fabs(pl->linearVelocity);
    const bool playerStill = pspeed < 0.6f;
    // Her place beside you: behind and to the right (Lilo takes the left).
    PetWanderStep(c.wander, dt, playerStill);
    const float wantX = c.wander.ox + px + std::sin(pyaw + 3.14159265f - 0.7f) * 80.0f, wantZ = c.wander.oz + pz + std::cos(pyaw + 3.14159265f - 0.7f) * 80.0f;
    float dx = wantX - c.x, dz = wantZ - c.z, d = std::hypot(dx, dz);
    if (!c.placed || d > 1400.0f || std::fabs(py - c.y) > 170.0f) {   // arrived, or left far behind: pop up beside you
        if (c.placed) CatPoof(play, c.x, c.y, c.z);
        c.x = wantX; c.z = wantZ; c.y = py; c.placed = true;
        CatPoof(play, c.x, c.y, c.z);
        dx = dz = 0; d = 0;
        SetBabyMood(c, BabyMood::Sit);
        c.sitT = 0;
        c.nextAt = 3.0f;
    }
    c.moodT += dt;
    if (playerStill) c.idle += dt; else c.idle = 0;
    c.reachCool -= dt;
    c.toyCool -= dt;
    if (gToy.kind != ToyKind::None && c.mood != BabyMood::Plush && c.mood != BabyMood::Tv && c.mood != BabyMood::Laptop) EndBabyToy(c, true);   // something else took over (she was spoken to)
    c.laptopCool -= dt;

    // The coos that are waiting their turn.
    for (size_t i = 0; i < gBabyCoos.size();) {
        gBabyCoos[i].at -= dt;
        if (gBabyCoos[i].at <= 0.0f) {
            BabyCooNow(actor, gBabyCoos[i].sfx, gBabyCoos[i].pitch, gBabyCoos[i].volume);
            gBabyCoos.erase(gBabyCoos.begin() + static_cast<long>(i));
        } else {
            i++;
        }
    }

    const bool talking = TalkingTo(actor);
    if (talking && c.mood != BabyMood::Talk) {
        SetBabyMood(c, BabyMood::Talk);
        static const int kLineCoo[royale::kAvriellaPetLineCount] = { kCooBaba, kCooBaba, kCooDada, kCooOoh, kCooGiggle, kCooBoth,
                                                                      kCooOoh, kCooYawn, kCooGiggle, kCooBaba, kCooHi, kCooBoth,
                                                                      kCooChew, kCooChew, kCooGiggle, kCooGiggle, kCooGiggle };
        BabyCoo(kLineCoo[c.line % royale::kAvriellaPetLineCount], true);
    }
    if (!talking && c.mood == BabyMood::Talk) {   // done talking: she sits a while and has something new to say next time
        SetBabyMood(c, BabyMood::Sit);
        c.sitT = 0; c.nextAt = 4.0f;
        c.line = (c.line + 1) % royale::kAvriellaPetLineCount;
    }
    if (c.mood != BabyMood::Talk) {
        const bool busy = c.mood == BabyMood::Roll || c.mood == BabyMood::Clap || c.mood == BabyMood::Giggle || c.mood == BabyMood::Plush || c.mood == BabyMood::Tv || c.mood == BabyMood::Laptop;
        // You pick something up: she claps for you. You use an emote: she waves. You jump: she giggles.
        if (gPickupLog.size() != c.pickups) {
            if (gPickupLog.size() > c.pickups && !busy) { SetBabyMood(c, BabyMood::Clap); BabyCoo(kCooHi); }
            c.pickups = gPickupLog.size();
        }
        if (gEmote.id >= 0 && c.mood != BabyMood::Wave && !busy) { SetBabyMood(c, BabyMood::Wave); BabyCoo(kCooHi); }
        if ((c.mood == BabyMood::Crawl || c.mood == BabyMood::Sit) && !(pl->actor.bgCheckFlags & 1) && pl->actor.velocity.y > 4.0f && d < 400.0f) {
            SetBabyMood(c, BabyMood::Giggle);
            BabyCoo(kCooGiggle);
        }
        // A coo of her own now and then.
        c.cooIn -= dt;
        if (c.cooIn <= 0.0f) {
            if (c.mood == BabyMood::Crawl || c.mood == BabyMood::Sit) BabyCoo(Rand_ZeroOne() < 0.5f ? kCooBaba : kCooDada);
            c.cooIn = 22.0f + Rand_ZeroOne() * 28.0f;
        }
    }

    const float tm = static_cast<float>(play->gameplayFrames) * dt;
    bool moving = false;
    float heading = c.yaw;
    int clip = A::kSit, face = A::kFaceSmile;
    float rate = 1.0f;
    auto follow = [&](float minSpeed) {   // crawl to her place: slowly when it is near, quickly when you have gone on ahead
        c.speed += (std::clamp((d - 45.0f) * 2.2f, minSpeed, 190.0f) - c.speed) * 0.2f;
        heading = std::atan2(dx, dz);
        c.x += std::sin(heading) * c.speed * dt;
        c.z += std::cos(heading) * c.speed * dt;
        moving = true;
    };
    auto faceYou = [&]() { heading = std::atan2(px - c.x, pz - c.z); };

    switch (c.mood) {
        case BabyMood::Crawl: {
            if (d > 70.0f) {
                follow(35.0f);
                clip = A::kTumble;   // rolling over and over after you
                face = A::kFaceGiggle;
                rate = std::clamp(c.speed / 75.0f, 0.5f, 2.2f);
            } else {
                c.speed *= 0.6f;
                SetBabyMood(c, BabyMood::Sit);
                c.sitT = 0; c.nextAt = 3.0f + Rand_ZeroOne() * 3.0f;
            }
            break;
        }
        case BabyMood::Sit: {
            c.sitT += dt;
            clip = std::fmod(c.sitT, 10.0f) < 5.0f ? A::kIdle : A::kSit;
            if (c.sleepy && c.moodT < 1.6f) face = A::kFaceHalf; else c.sleepy = false;
            if (!playerStill || d > 150.0f) { SetBabyMood(c, BabyMood::Crawl); break; }
            if (c.sitT > 15.0f && c.laptopCool <= 0.0f) {   // bored from sitting still so long: she pops out a laptop and watches her favourite show
                StartBabyToy(play, c, ToyKind::Laptop);
                break;
            }
            if (c.sitT > 24.0f) {   // you have stood still a long time: she nods off
                SetBabyMood(c, BabyMood::Nap);
                break;
            }
            if (c.sitT > 17.0f) face = A::kFaceHalf;   // heavy eyes first
            else if (std::fmod(c.sitT, 6.0f) < 1.5f) face = A::kFaceGiggle;   // she smiles a lot
            if (c.moodT > c.nextAt) {
                c.nextAt = 4.5f + Rand_ZeroOne() * 4.0f;
                const Actor* loot = c.reachCool <= 0.0f ? NearestLootTo(c.x, c.z, 320.0f) : nullptr;
                if (loot != nullptr) {
                    c.reachX = loot->world.pos.x; c.reachZ = loot->world.pos.z; c.reachCool = 14.0f;
                    SetBabyMood(c, BabyMood::Reach);
                    BabyCoo(kCooOoh);
                } else if (c.toyCool <= 0.0f && Rand_ZeroOne() < 0.22f) {   // she gets a toy out (the plush, or a TV for the show)
                    StartBabyToy(play, c, Rand_ZeroOne() < 0.6f ? ToyKind::Plush : ToyKind::Tv);
                } else {
                    const float r = Rand_ZeroOne();
                    if (r < 0.12f) { SetBabyMood(c, BabyMood::Wave); BabyCoo(kCooHi); }
                    else if (r < 0.22f) { SetBabyMood(c, BabyMood::Clap); BabyCoo(kCooBaba); }
                    else if (r < 0.31f) { SetBabyMood(c, BabyMood::Babble); BabyCoo(kCooBoth); }
                    else if (r < 0.43f) { SetBabyMood(c, BabyMood::Giggle); BabyCoo(kCooGiggle); }
                    else if (r < 0.55f) { SetBabyMood(c, BabyMood::Stack); }
                    else if (r < 0.61f) { SetBabyMood(c, BabyMood::Stand); BabyCoo(kCooOoh); }
                    else if (r < 0.69f) { SetBabyMood(c, BabyMood::Roll); BabyCoo(kCooGiggle); }
                    else if (r < 0.78f) { SetBabyMood(c, BabyMood::Kick); BabyCoo(kCooGiggle); }
                    else if (r < 0.88f) { SetBabyMood(c, BabyMood::Chew); BabyCoo(kCooChew); }
                    else { SetBabyMood(c, BabyMood::Cap); c.haveHand = false; c.capSpring = royale::HatSpring{}; c.tugAt = 0.0; BabyCoo(kCooHi); }
                }
            }
            break;
        }
        case BabyMood::Plush: {   // pulls her stuffed Bluey out from behind her and plays with it
            const int v = gToy.variant;
            static const int kClipFor[5] = { A::kBabble, A::kCap, A::kChew, A::kClap, A::kWave };
            clip = kClipFor[v];
            face = v == 2 ? A::kFaceSmile : A::kFaceGiggle;
            if (c.moodT < 0.8f) face = A::kFaceOh;
            if (v == 3) { BabyToySpot(play, c, 42.0f); }
            if (c.moodT > 1.0f && std::fmod(c.moodT, 2.2f) < dt) BabyCoo(v == 2 ? kCooChew : (Rand_ZeroOne() < 0.5f ? kCooGiggle : kCooBaba));
            if (!playerStill || d > 150.0f) { EndBabyToy(c, false); SetBabyMood(c, BabyMood::Crawl); break; }
            if (c.moodT > gToy.len) { EndBabyToy(c, true); SetBabyMood(c, BabyMood::Sit); c.sitT = 0; BabyCoo(kCooHi); }
            break;
        }
        case BabyMood::Tv:
        case BabyMood::Laptop: {   // watches her favourite show: a TV pops out of the ground (or a laptop opens) and the show is on
            const bool tv = c.mood == BabyMood::Tv;
            namespace S = royale::avriella_toy_snd;
            heading = std::atan2(gToy.x - c.x, gToy.z - c.z);
            const int phase = static_cast<int>(c.moodT / 3.0f) % 3;
            clip = c.moodT < 1.2f ? A::kSit : (phase == 1 ? A::kClap : (phase == 2 ? A::kGiggle : A::kSit));
            face = c.moodT < 1.2f ? A::kFaceOh : A::kFaceGiggle;
            if (!gToy.tuneOn && c.moodT > (tv ? 1.0f : 1.6f)) { gToy.tuneOn = true; ToySfx(S::kChime, 0.6f); ToySfx(S::kTune, 0.5f); }
            if (c.moodT > gToy.nextSfx && c.moodT < gToy.len - 2.0f) {   // something funny happens in the show
                const int pick = static_cast<int>(Rand_ZeroOne() * 5.0f) % 5;
                static const int kShow[5] = { S::kSlide, S::kBonk, S::kXyloRun, S::kTada, S::kBoing };
                ToySfx(kShow[pick], 0.7f);
                BabyCoo(pick == 3 ? kCooHi : kCooGiggle);
                gToy.nextSfx = c.moodT + 3.0f + Rand_ZeroOne() * 2.5f;
            }
            if (!playerStill || d > 150.0f) { EndBabyToy(c, false); SetBabyMood(c, BabyMood::Crawl); break; }
            if (c.moodT > gToy.len) { EndBabyToy(c, false); SetBabyMood(c, BabyMood::Sit); c.sitT = 0; BabyCoo(kCooHi); }
            break;
        }
        case BabyMood::Wave: {
            clip = A::kWave;
            face = std::fmod(c.moodT, 0.8f) < 0.4f ? A::kFaceSmile : A::kFaceGiggle;
            if (c.moodT > 3.2f) SetBabyMood(c, BabyMood::Sit);
            break;
        }
        case BabyMood::Giggle: {
            clip = A::kGiggle;
            face = A::kFaceGiggle;
            if (c.moodT > 2.4f) SetBabyMood(c, BabyMood::Sit);
            break;
        }
        case BabyMood::Clap: {
            clip = A::kClap;
            face = A::kFaceGiggle;
            if (c.moodT > 3.0f) SetBabyMood(c, BabyMood::Sit);
            break;
        }
        case BabyMood::Babble: {
            clip = A::kBabble;
            face = std::fmod(c.moodT, 0.4f) < 0.2f ? A::kFaceOh : A::kFaceSmile;   // her mouth goes with the babble
            if (c.moodT > 2.4f) SetBabyMood(c, BabyMood::Sit);
            break;
        }
        case BabyMood::Roll: {   // tips over, rolls right round and sits up again
            clip = A::kRoll;
            face = A::kFaceGiggle;
            if (c.moodT > A::ClipSeconds(A::kRoll) + 0.2f) SetBabyMood(c, BabyMood::Sit);
            break;
        }
        case BabyMood::Nap: {   // asleep on her back with little 'z's
            clip = A::kNap;
            face = A::kFaceShut;
            if (static_cast<int>(c.moodT * 10.0f) % 18 == 0 && play->gameplayFrames % 3 == 0) {
                Vec3f pos = { c.x, c.y + 38.0f, c.z }, vel = { 0.15f, 0.5f, 0 }, accel = { 0, 0, 0 };
                Color_RGBA8 prim = { 190, 210, 255, 255 }, env = { 90, 120, 255, 255 };
                EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 60, 40);
            }
            if (!playerStill || d > 150.0f) {   // you move: she wakes with a yawn and crawls after you
                SetBabyMood(c, BabyMood::Crawl);
                c.sleepy = true;
                BabyCoo(kCooYawn);
            }
            break;
        }
        case BabyMood::Stand: {   // pulls herself up and wobbles on her two feet, then plops down
            clip = A::kStand;
            face = A::kFaceOh;
            if (static_cast<int>(c.moodT * 10.0f) % 12 == 0 && play->gameplayFrames % 3 == 0) BabySparkle(play, c.x, c.y + 20.0f, c.z);
            if (c.moodT > 4.8f) { SetBabyMood(c, BabyMood::Sit); BabyCoo(kCooGiggle); }
            break;
        }
        case BabyMood::Reach: {   // reaches both ways for loot she can see
            clip = A::kReach;
            face = A::kFaceOh;
            heading = std::atan2(c.reachX - c.x, c.reachZ - c.z);
            if (c.moodT > 2.4f) { SetBabyMood(c, BabyMood::Chew); BabyCoo(kCooChew); }   // whatever she can get her hands on goes in her mouth
            break;
        }
        case BabyMood::Kick: {   // on her back, kicking over and over
            clip = A::kKick;
            face = A::kFaceGiggle;
            if (std::fmod(c.moodT, 1.4f) < dt) BabyCoo(kCooGiggle);
            if (c.moodT > 4.8f) SetBabyMood(c, BabyMood::Sit);
            break;
        }
        case BabyMood::Cap: {   // snatches a green cap (a copy of yours) and swirls it round by its tail
            clip = A::kCap;
            face = A::kFaceGiggle;
            if (std::fmod(c.moodT, 1.3f) < dt) BabyCoo(kCooGiggle);
            if (c.moodT > 5.4f) SetBabyMood(c, BabyMood::Sit);
            if (!playerStill || d > 150.0f) SetBabyMood(c, BabyMood::Crawl);
            break;
        }
        case BabyMood::Chew: {   // gnawing on a tiny rock
            clip = A::kChew;
            face = A::kFaceSmile;
            if (std::fmod(c.moodT, 1.2f) < dt) BabyCoo(kCooChew);
            if (c.moodT > 4.8f) SetBabyMood(c, BabyMood::Sit);
            break;
        }
        case BabyMood::Stack: {   // three tiny rocks, one on top of the other, and then they fall over
            clip = A::kRocks;
            face = c.moodT < 0.8f ? A::kFaceHalf : A::kFaceSmile;
            if (c.moodT > 6.8f) { SetBabyMood(c, BabyMood::Sit); BabyCoo(kCooGiggle); }
            if (std::fmod(c.moodT, 3.4f) > 2.7f && std::fmod(c.moodT, 3.4f) < 2.75f) BabyCoo(kCooOoh);   // the tower falls
            if (!playerStill || d > 150.0f) SetBabyMood(c, BabyMood::Crawl);
            break;
        }
        case BabyMood::Talk: {   // sitting, facing you, babbling
            clip = A::kBabble;
            face = std::fmod(c.moodT, 0.5f) < 0.25f ? A::kFaceOh : A::kFaceSmile;
            c.speed = 0;
            break;
        }
    }
    // Sparkles while she is delighted.
    if (c.mood == BabyMood::Giggle || c.mood == BabyMood::Clap) {
        c.sparkleIn -= dt;
        if (c.sparkleIn <= 0.0f) { BabySparkle(play, c.x, c.y + 24.0f, c.z); c.sparkleIn = 0.3f; }
    }
    // Talk: stand still facing her and press A (the game's own talk, so the A button says "Speak"). Not while you are on the move.
    if (!talking && c.mood != BabyMood::Roll && c.mood != BabyMood::Nap && c.mood != BabyMood::Kick && c.mood != BabyMood::Plush && c.mood != BabyMood::Tv && c.mood != BabyMood::Laptop && pspeed < 3.0f &&
        OfferTalk(actor, play, static_cast<u16>(kTextAvriellaPet + c.line), 110.0f))
        SetBabyMood(c, BabyMood::Talk);
    // turn to face the way she goes (or, when still, towards you if you are close)
    if (!moving && c.mood != BabyMood::Reach && c.mood != BabyMood::Roll && c.mood != BabyMood::Nap && d < 400.0f && c.mood != BabyMood::Kick && c.mood != BabyMood::Tv && c.mood != BabyMood::Laptop) faceYou();
    {
        float diff = heading - c.yaw;
        while (diff > 3.14159265f) diff -= 6.2831853f;
        while (diff < -3.14159265f) diff += 6.2831853f;
        c.yaw += diff * (moving ? 0.25f : 0.1f);
    }
    c.anim.Play(clip, 0.25f);
    c.anim.Update(dt, rate);
    // eyes: a quick blink every few seconds while they are open
    if (ImGui::GetTime() < gBabyVoiceUntil && (face == A::kFaceSmile || face == A::kFaceGiggle))   // her mouth goes with the recording
        face = gBabyVoiceLaugh ? A::kFaceGiggle : (std::fmod(tm, 0.5f) < 0.25f ? A::kFaceOh : A::kFaceSmile);
    c.face = (face == A::kFaceSmile && std::fmod(tm, 3.9f) < 0.13f) ? static_cast<int>(A::kFaceShut) : face;

    c.y = GroundY(play, c.x, c.z, c.y);   // her clips carry their own bounce
    actor->world.pos.x = c.x; actor->world.pos.z = c.z; actor->world.pos.y = c.y;
    actor->shape.rot.y = static_cast<s16>(c.yaw * (32768.0f / 3.14159265f));
    actor->focus.pos = actor->world.pos;
    actor->focus.pos.y += 24.0f;
}

// The cap she waves: a little green cone in four rings, hung from her hand. Link's own cap is part of the game's model and cannot be taken off,
// so this is a copy; his real cap gets a tug each time she grabs it. The tail swings on the same springs as his own (royale::HatSpring), fed by
// how her hand moves.
void TugLinkCap(const Player* pl, float side);   // (defined with the cap's springs, further down)
void DrawBabyCap(PlayState* play, const royale::avriella::Pose& pose) {
    namespace A = royale::avriella;
    BabyBrain& c = gBaby;
    Actor* a = c.actor;
    if (a == nullptr) return;
    // her hand: the middle of the mitt's vertices (the ones that follow only the right hand bone)
    float sx = 0, sy = 0, sz = 0;
    int n = 0;
    for (int i = 0; i < A::kVertCount; i++) {
        const A::Vert& v = A::kVerts[i];
        if (v.b0 != 13 || v.w0 != 255) continue;
        float p[3], nr[3];
        A::SkinVertex(pose, v, p, nr);
        sx += p[0]; sy += p[1]; sz += p[2]; n++;
    }
    if (n == 0) return;
    sx /= static_cast<float>(n); sy /= static_cast<float>(n); sz /= static_cast<float>(n);
    const float cy = std::cos(c.yaw), sn = std::sin(c.yaw);
    const double now = ImGui::GetTime();
    const float dt = static_cast<float>(now - c.capSeen);
    if (!c.haveHand || dt > 0.25f) { c.hx = sx; c.hy = sy; c.hz = sz; c.hvx = c.hvy = c.hvz = 0; c.haveHand = true; c.capSeen = now; }
    else if (dt > 0.004f) {   // (drawn more than once a frame sometimes: only step when time has passed)
        const float vx = std::clamp((sx - c.hx) * kBabyScale / dt, -600.0f, 600.0f), vy = std::clamp((sy - c.hy) * kBabyScale / dt, -600.0f, 600.0f),
                    vz = std::clamp((sz - c.hz) * kBabyScale / dt, -600.0f, 600.0f);
        const float ax = (vx - c.hvx) / dt, ay = (vy - c.hvy) / dt, az = (vz - c.hvz) / dt;   // her hand speeding up: the tail is flung the other way
        c.capSpring.Step((std::min)(dt, 0.05f), -vx, -vz, -vy, 0.0f, static_cast<float>(now), 0.7f, std::clamp(az * 0.0012f, -2.5f, 2.5f), std::clamp(-ax * 0.0012f, -2.5f, 2.5f), 0.0f);
        (void)ay;
        c.hx = sx; c.hy = sy; c.hz = sz; c.hvx = vx; c.hvy = vy; c.hvz = vz; c.capSeen = now;
    }
    // the tail's four rings: each bends a little further than the one before, drooping with its own weight
    constexpr int kRings = 4, kSides = 5;
    const float radius[kRings] = { 5.0f, 4.2f, 2.8f, 1.4f };
    float cx = 0.0f, cyy = 0.0f, cz = 0.0f;   // ring centres, in her own space
    Vtx* vtx = static_cast<Vtx*>(FrameAlloc(play, sizeof(Vtx) * (kRings * kSides + 1)));
    if (vtx == nullptr) return;
    constexpr float kSub = 8.0f;
    const float fore = std::clamp(c.capSpring.fore, -1.1f, 1.1f), side = std::clamp(c.capSpring.side, -1.0f, 1.0f);
    float tipX = 0, tipY = 0, tipZ = 0;
    for (int r = 0; r < kRings; r++) {
        const float k = static_cast<float>(r) / 3.0f;
        const float pitch = 0.35f + 1.5f * fore * k + 0.5f * k, roll = 1.5f * side * k;   // how far this ring has bent from straight up
        const float dx = std::sin(roll), dy = std::cos(pitch) * std::cos(roll), dz = -std::sin(pitch);
        if (r > 0) { cx += dx * 9.0f; cyy += dy * 9.0f; cz += dz * 9.0f; }
        // two axes at right angles to the tail's direction
        float ux = dy, uy = -dx, uz = 0.0f;
        const float ul = std::sqrt(ux * ux + uy * uy) + 1e-5f;
        ux /= ul; uy /= ul;
        const float wx = uy * dz - uz * dy, wy = uz * dx - ux * dz, wz = ux * dy - uy * dx;
        for (int j = 0; j < kSides; j++) {
            const float ang = 6.2831853f * static_cast<float>(j) / kSides;
            const float ca = std::cos(ang) * radius[r], sa = std::sin(ang) * radius[r];
            Vtx& o = vtx[r * kSides + j];
            const float px = cx + ux * ca + wx * sa, py = cyy + uy * ca + wy * sa, pz = cz + uz * ca + wz * sa;
            o.v.ob[0] = static_cast<s16>(std::lround(px * kSub)); o.v.ob[1] = static_cast<s16>(std::lround(py * kSub)); o.v.ob[2] = static_cast<s16>(std::lround(pz * kSub));
            o.v.flag = 0; o.v.tc[0] = 0; o.v.tc[1] = 0;
            const float lit = 0.62f + 0.38f * std::cos(ang + 0.7f);
            o.v.cn[0] = static_cast<u8>(70.0f * lit); o.v.cn[1] = static_cast<u8>(170.0f * lit); o.v.cn[2] = static_cast<u8>(60.0f * lit); o.v.cn[3] = 255;
        }
        tipX = cx + dx * 6.0f; tipY = cyy + dy * 6.0f; tipZ = cz + dz * 6.0f;
    }
    Vtx& tip = vtx[kRings * kSides];
    tip.v.ob[0] = static_cast<s16>(std::lround(tipX * kSub)); tip.v.ob[1] = static_cast<s16>(std::lround(tipY * kSub)); tip.v.ob[2] = static_cast<s16>(std::lround(tipZ * kSub));
    tip.v.flag = 0; tip.v.tc[0] = 0; tip.v.tc[1] = 0;
    tip.v.cn[0] = 230; tip.v.cn[1] = 225; tip.v.cn[2] = 150; tip.v.cn[3] = 255;   // a pale tip
    // her hand's place in the world (the model turns about her feet by yaw, scaled)
    const float hwx = a->world.pos.x + (sx * cy + sz * sn) * kBabyScale, hwy = a->world.pos.y + sy * kBabyScale, hwz = a->world.pos.z + (-sx * sn + sz * cy) * kBabyScale;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    Matrix_Translate(hwx, hwy, hwz, MTXMODE_NEW);
    Matrix_RotateY(c.yaw, MTXMODE_APPLY);
    Matrix_Scale(1.0f / kSub * 1.0f, 1.0f / kSub, 1.0f / kSub, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPVertex(POLY_OPA_DISP++, reinterpret_cast<uintptr_t>(vtx), kRings * kSides + 1, 0);
    for (int r = 0; r + 1 < kRings; r++)
        for (int j = 0; j < kSides; j++) {
            const int j2 = (j + 1) % kSides;
            const int a0 = r * kSides + j, a1 = r * kSides + j2, b0 = (r + 1) * kSides + j, b1 = (r + 1) * kSides + j2;
            gSP2Triangles(POLY_OPA_DISP++, a0, a1, b1, 0, a0, b1, b0, 0);
        }
    for (int j = 0; j < kSides; j++) gSP1Triangle(POLY_OPA_DISP++, (kRings - 1) * kSides + j, (kRings - 1) * kSides + (j + 1) % kSides, kRings * kSides, 0);
    CLOSE_DISPS(play->state.gfxCtx);
    // and Link's own cap gets a tug each time she pulls it
    if (now - c.tugAt > 0.9) {
        c.tugAt = now;
        TugLinkCap(GET_PLAYER(play), std::fmod(now, 2.0) < 1.0 ? 2.0f : -2.0f);
    }
}

// Her hand (the middle of the mitt's vertices) in the world.
bool BabyHandWorld(const royale::avriella::Pose& pose, float out[3]) {
    namespace A = royale::avriella;
    const BabyBrain& c = gBaby;
    float sx = 0, sy = 0, sz = 0;
    int n = 0;
    for (int i = 0; i < A::kVertCount; i++) {
        const A::Vert& v = A::kVerts[i];
        if (v.b0 != 13 || v.w0 != 255) continue;
        float p[3], nr[3];
        A::SkinVertex(pose, v, p, nr);
        sx += p[0]; sy += p[1]; sz += p[2]; n++;
    }
    if (n == 0) return false;
    sx /= static_cast<float>(n); sy /= static_cast<float>(n); sz /= static_cast<float>(n);
    const float cy = std::cos(c.yaw), sn = std::sin(c.yaw);
    out[0] = c.x + (sx * cy + sz * sn) * kBabyScale;
    out[1] = c.y + sy * kBabyScale;
    out[2] = c.z + (-sx * sn + sz * cy) * kBabyScale;
    return true;
}

// The plush's limbs for a way of playing (v: 0 hug, 1 swing, 2 chew, 3 dance, 4 wave) or, with `seated`, sitting with its legs out in front watching TV.
void PlushPose(ToyPose& tp, int v, float t, bool seated) {
    namespace T = royale::avriella_toys;
    auto L = [&](int part, int axis) -> float& { return tp.r[part][axis]; };
    if (seated) {
        L(kPlLegL, 0) = L(kPlLegR, 0) = -1.45f;
        L(kPlArmL, 0) = L(kPlArmR, 0) = -0.9f;
        L(kPlArmL, 2) = 0.3f + 0.5f * std::max(0.0f, std::sin(t * 3.0f)); L(kPlArmR, 2) = -0.3f - 0.5f * std::max(0.0f, std::sin(t * 3.0f + 1.0f));
        L(kPlHead, 0) = 0.08f;
        L(kPlEarL, 2) = 0.1f * std::sin(t * 4.0f); L(kPlEarR, 2) = -0.1f * std::sin(t * 4.0f + 1.0f);
        tp.sy = 1.0f - 0.03f * std::sin(t * 5.0f);
        return;
    }
    switch (v) {
        case 0:   // hugged: it squishes a little with each squeeze and wobbles
            tp.sy = 1.0f - 0.08f * (0.5f + 0.5f * std::sin(t * 4.0f)); tp.sx = tp.sz = 1.0f + 0.05f * (0.5f + 0.5f * std::sin(t * 4.0f));
            L(kPlArmL, 2) = 0.5f; L(kPlArmR, 2) = -0.5f; L(kPlHead, 2) = 0.12f * std::sin(t * 2.0f);
            L(kPlEarL, 2) = 0.2f * std::sin(t * 4.0f); L(kPlEarR, 2) = -0.2f * std::sin(t * 4.0f);
            break;
        case 3:   // dances: hops, sways, waves its arms and kicks
            tp.off[1] = std::fabs(std::sin(t * 5.0f)) * 5.0f;
            L(kPlBody, 2) = 0.14f * std::sin(t * 5.0f);
            L(kPlArmL, 2) = 1.1f + 0.6f * std::sin(t * 10.0f); L(kPlArmR, 2) = -1.1f - 0.6f * std::sin(t * 10.0f + 1.5f);
            L(kPlLegL, 0) = 0.6f * std::sin(t * 5.0f); L(kPlLegR, 0) = -0.6f * std::sin(t * 5.0f);
            L(kPlHead, 2) = 0.2f * std::sin(t * 5.0f + 1.0f);
            L(kPlEarL, 2) = 0.3f * std::sin(t * 10.0f); L(kPlEarR, 2) = -0.3f * std::sin(t * 10.0f + 1.0f);
            L(kPlTail, 2) = 0.4f * std::sin(t * 10.0f);
            break;
        default:   // held by a hand: it flops about
            L(kPlArmL, 2) = 0.7f + 0.5f * std::sin(t * 9.0f); L(kPlArmR, 2) = -0.7f - 0.5f * std::sin(t * 9.0f + 1.0f);
            L(kPlLegL, 0) = 0.5f * std::sin(t * 7.0f); L(kPlLegR, 0) = -0.5f * std::sin(t * 7.0f + 1.0f);
            L(kPlHead, 2) = 0.25f * std::sin(t * 5.0f);
            L(kPlEarL, 2) = 0.3f * std::sin(t * 8.0f); L(kPlEarR, 2) = -0.3f * std::sin(t * 8.0f + 1.0f);
            L(kPlTail, 2) = 0.4f * std::sin(t * 9.0f);
            break;
    }
    (void)T::kTexCount;
}

void DrawBabyToys(PlayState* play, const royale::avriella::Pose& pose) {
    namespace T = royale::avriella_toys;
    const BabyBrain& c = gBaby;
    if (gToy.kind == ToyKind::None) return;
    const float t = c.moodT;
    const float sc = kBabyScale * ToyScale(t, gToy.len);
    if (sc < 0.02f) return;
    const float fx = std::sin(c.yaw), fz = std::cos(c.yaw);          // her way
    const float face = c.yaw + 3.14159265f;                            // a toy turned to look at her
    const float tm = static_cast<float>(play->gameplayFrames) / royale::kTickHz;
    if (gToy.kind == ToyKind::Plush) {
        ToyPose tp;
        const int v = gToy.variant;
        PlushPose(tp, v, t, false);
        float x, y, z, yaw = face;
        if (v == 0) { x = c.x + fx * 14.0f; z = c.z + fz * 14.0f; y = c.y + 8.0f; tp.r[kPlBody][0] = -0.25f; }
        else if (v == 3) { x = gToy.x; z = gToy.z; y = gToy.y; yaw = std::atan2(c.x - gToy.x, c.z - gToy.z); }
        else {
            float h[3];
            if (!BabyHandWorld(pose, h)) { h[0] = c.x + fx * 14.0f; h[1] = c.y + 20.0f; h[2] = c.z + fz * 14.0f; }
            x = h[0]; y = h[1] - (v == 2 ? 2.0f : 7.0f); z = h[2];
            if (v == 2) yaw = face + 0.6f;
            else if (v == 1) yaw = face + 0.8f * std::sin(t * 5.0f);
        }
        const float k = std::clamp(t / kToyAppearSeconds, 0.0f, 1.0f);   // it arcs in from behind her
        if (k < 1.0f) {
            const float bx = c.x - fx * 10.0f, bz = c.z - fz * 10.0f, by = c.y + 8.0f;
            x = bx + (x - bx) * k; z = bz + (z - bz) * k; y = by + (y - by) * k + std::sin(3.14159265f * k) * 16.0f;
            tp.r[kPlBody][0] += (1.0f - k) * 1.2f * std::sin(k * 9.0f);   // tumbling through the air
        }
        DrawToy(play, PlushModel(), x, y, z, yaw, sc, tp);
        return;
    }
    // The TV or the laptop, standing beside her and turned to face her.
    const float toX = c.x - gToy.x, toZ = c.z - gToy.z;
    const float yaw = std::atan2(toX, toZ);
    ToyPose tp;
    const bool showOn = gToy.tuneOn;
    const float since = t - (gToy.kind == ToyKind::Tv ? 1.0f : 1.6f);
    if (!showOn) tp.screen = T::kTexOff;
    else if (since < 0.7f) tp.screen = std::fmod(since, 0.12f) < 0.06f ? T::kTexStaticA : T::kTexStaticB;   // it tunes in
    else tp.screen = std::fmod(since, 1.0f) < 0.5f ? T::kTexBlueyA : T::kTexBlueyB;
    tp.glow = showOn ? 0.92f + 0.08f * std::sin(tm * 40.0f) : 0.5f;
    if (gToy.kind == ToyKind::Tv) {
        tp.r[1][2] = 0.12f * std::sin(tm * 3.0f) + 0.1f; tp.r[2][2] = -0.12f * std::sin(tm * 3.0f + 1.0f) - 0.1f;   // the aerials sway
        tp.sx = 1.0f + 0.04f * std::sin(tm * 14.0f) * (showOn ? 1.0f : 0.0f);
        tp.sy = 1.0f - 0.04f * std::sin(tm * 14.0f) * (showOn ? 1.0f : 0.0f);
        DrawToy(play, TvModel(), gToy.x, gToy.y, gToy.z, yaw, sc, tp);
        // and the plush sits beside her, watching too
        ToyPose pp;
        PlushPose(pp, 0, t, true);
        const float rx = c.x + std::cos(c.yaw) * 26.0f, rz = c.z - std::sin(c.yaw) * 26.0f;
        const float ps = kBabyScale * ToyEaseBack((t - 0.4f) / kToyAppearSeconds) * std::clamp((gToy.len - t) / kToyLeaveSeconds, 0.0f, 1.0f);
        DrawToy(play, PlushModel(), rx, GroundY(play, rx, rz, c.y), rz, std::atan2(gToy.x - rx, gToy.z - rz), ps, pp);
    } else {
        const float open = std::clamp((t - 0.5f) / 0.9f, 0.0f, 1.0f), close = std::clamp((gToy.len - t - 0.2f) / 0.6f, 0.0f, 1.0f);
        tp.r[1][0] = 1.83f * (1.0f - ToyEaseBack(std::min(open, close)));   // the lid swings up (and shut again at the end)
        if (tp.r[1][0] > 0.2f) tp.screen = T::kTexOff;
        DrawToy(play, LaptopModel(), gToy.x, gToy.y, gToy.z, yaw, sc * 1.0f, tp);
    }
}

void Baby_Draw(Actor* actor, PlayState* play) {
    royale::avriella::Pose pose;
    gBaby.anim.Evaluate(pose);
    {   // the tuft follows how she moves and turns, and the wind (the cloth physics setting scales it; off = a stiff tuft)
        float wx, wz, wind; WindNow(&wx, &wz, &wind);
        gBaby.tail.Update(ImGui::GetTime(), actor->world.pos.x, actor->world.pos.z, gBaby.yaw, std::clamp(wind, 0.0f, 1.0f));
        royale::avriella::ApplyTuft(pose, gBaby.tail.spring, gClothScale);
    }
    DrawAvriellaModel(play, actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, gBaby.yaw, kBabyScale, pose, gBaby.face);
    if (gBaby.mood == BabyMood::Cap) DrawBabyCap(play, pose);
    if (gBaby.mood == BabyMood::Plush || gBaby.mood == BabyMood::Tv || gBaby.mood == BabyMood::Laptop) DrawBabyToys(play, pose);
}
void Baby_Destroy(Actor* actor, PlayState*) { if (gBaby.actor == actor) { gBaby.actor = nullptr; gBaby.placed = false; EndBabyToy(gBaby, true); } }

void ReconcileBabyPet(const royale::HudState& hud) {
    const bool inMatch = IsLive(hud);
    const bool want = DebugOn(kDbgAvriella) && (LobbyPetsActive() || (MapOption("LiloPet", false) && PetKind() == 1)) && gSession.Joined() && InPlayableScene(hud.state) && gPlayState != nullptr &&
                      !gSkydiving && !gSpectating && !(inMatch && hud.haveSelf && !hud.selfAlive);
    if (!want) {
        if (gBaby.actor != nullptr) { Actor_Kill(gBaby.actor); gBaby.actor = nullptr; gBaby.placed = false; gBabyCoos.clear(); EndBabyToy(gBaby, true); }
        return;
    }
    if (gBaby.actor != nullptr) return;
    Player* pl = GET_PLAYER(gPlayState);
    Actor* a = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ISHI, pl->actor.world.pos.x, pl->actor.world.pos.y, pl->actor.world.pos.z, 0, 0, 0, 0, false);
    if (a == nullptr) return;
    a->update = Baby_Update;
    a->draw = Baby_Draw;
    a->destroy = Baby_Destroy;
    a->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
    a->uncullZoneForward = 4000.0f; a->uncullZoneScale = 1500.0f; a->uncullZoneDownward = 1500.0f;
    a->shape.shadowScale = 20.0f;
    gBaby.actor = a;
    gBaby.placed = false;
    gBaby.pickups = gPickupLog.size();
    gBaby.mood = BabyMood::Sit;
    gBaby.anim = royale::avriella::Animator{};
    gBaby.anim.Play(royale::avriella::kSit, 0.0f);
}

#include "RoyaleLobbyPets.h"

