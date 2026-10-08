// ---- local player <-> session ----------------------------------------------------------------------------------------

// ---- emotes -------------------------------------------------------------------------------------------------------------------
// Your own Link does the emote himself: the game keeps running him as usual (standing still), and just before he is drawn his pose is
// replaced with the emote's frame. (It used to be done by a stand-in copy of Link while the real one was hidden, which could leave him
// invisible afterwards with only his shadow showing.)

struct EmoteState {
    int id = -1;               // which emote is playing, -1 for none
    double startAt = 0, endAt = 0;
    Player* player = nullptr;  // the Link whose drawing was taken over
};
EmoteState gEmote;

bool CanEmote(const royale::HudState& hud) { return LiveAndAlive(hud) && InField() && !gSkydiving; }

void LocalLink_Draw(Actor* actor, PlayState* play);
bool LocalDressOn();   // below, with the held weapon

// Gives Link back his own drawing (only if he is still the same Link: a scene change makes a new one).
void ReleaseEmoteDraw() {
    if (gPlayState == nullptr) return;
    Player* player = GET_PLAYER(gPlayState);
    if (player != nullptr && player->actor.draw == LocalLink_Draw) player->actor.draw = Player_Draw;
}

void StopEmote() {
    if (gEmote.id < 0) return;
    ReleaseEmoteDraw();
    gEmote = EmoteState{};
}

void StartEmote(int index, const royale::HudState& hud) {
    if (!CanEmote(hud) || index < 0 || index >= royale::kEmoteCount) return;
    StopEmote();
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr || (player->actor.draw != Player_Draw && player->actor.draw != LocalLink_Draw)) return;
    gEmote.id = index;
    gEmote.player = player;
    gEmote.startAt = ImGui::GetTime();
    gEmote.endAt = gEmote.startAt + (index == royale::kChickenDanceEmote ? royale::kChickenDanceSeconds : 3.4);
    gLastEmote = index;
    player->actor.draw = LocalLink_Draw;
}

// Any movement, attack or a few seconds ends an emote.
void UpdateEmote(Player* player, const royale::HudState& hud) {
    if (gEmote.id < 0) { if (player->actor.draw == LocalLink_Draw && !LocalDressOn()) player->actor.draw = Player_Draw; return; }
    const Input& in = gPlayState->state.input[0];
    const bool moved = std::fabs(static_cast<float>(in.cur.stick_x)) > 25.0f || std::fabs(static_cast<float>(in.cur.stick_y)) > 25.0f;
    if (!CanEmote(hud) || player != gEmote.player || ImGui::GetTime() > gEmote.endAt || moved ||
        (in.press.button & (BTN_B | BTN_A | BTN_Z | BTN_R | BTN_CUP | BTN_CLEFT | BTN_CDOWN | BTN_DDOWN | BTN_DUP))) {
        StopEmote();
    }
}

// Copies one frame of one of Link's animations straight into a joint table, the same way the game's own loader reads it (patched Ship of
// Harkinian reads the frame at once rather than by DMA), so it can be done while drawing, after the game has posed Link for the frame.
void LoadLinkFrame(LinkAnimationHeader* animation, int frame, int limbCount, Vec3s* out) {
    if (animation == nullptr) return;
    if (ResourceMgr_OTRSigCheck(reinterpret_cast<char*>(animation)) != 0)
        animation = reinterpret_cast<LinkAnimationHeader*>(ResourceMgr_LoadAnimByName(reinterpret_cast<const char*>(animation)));
    if (animation == nullptr) return;
    const LinkAnimationHeader* header = static_cast<const LinkAnimationHeader*>(SEGMENTED_TO_VIRTUAL(animation));
    char path[128];
    snprintf(path, sizeof(path), "misc/link_animetion/gPlayerAnimData_%06X",
             static_cast<unsigned>(reinterpret_cast<uintptr_t>(header->segment) - 0x07000000));
    const char* data = ResourceMgr_LoadPlayerAnimByName(path);
    if (data == nullptr) return;
    const size_t stride = sizeof(Vec3s) * limbCount + 2;   // every limb, then the eyes and mouth
    std::memcpy(out, data + stride * frame, stride);
}

// Poses Link `t` seconds into a sequence of animations: one after another (Link's animations step once per game tick), the last one
// looping or holding its final pose. He stays where he stands: only the height of the animation's root is used.
void PoseSeq(Player* player, const AnimSeq& seq, float t) {
    float f = t * royale::kTickHz;
    LinkAnimationHeader* anim = nullptr;
    int frame = 0;
    for (int i = 0; i < seq.count; i++) {
        const float len = static_cast<float>(Animation_GetLastFrame(seq.step[i].anim)) + 1.0f;
        if (seq.step[i].loop) { anim = seq.step[i].anim; frame = static_cast<int>(std::fmod(f, len)); break; }
        if (f < len || i == seq.count - 1) { anim = seq.step[i].anim; frame = static_cast<int>(std::min(f, len - 1.0f)); break; }
        f -= len;
    }
    if (anim == nullptr) return;
    Vec3s* j = player->skelAnime.jointTable;
    const s16 rootX = j[0].x, rootZ = j[0].z;
    LoadLinkFrame(anim, std::max(0, frame), player->skelAnime.limbCount, j);
    j[0].x = rootX;
    j[0].z = rootZ;
}
// Poses Link for the emote at this moment.
void PoseEmote(Player* player) {
    const float t = static_cast<float>(ImGui::GetTime() - gEmote.startAt);
    PoseSeq(player, SeqFor(royale::EmoteAnim(gEmote.id), royale::ItemId::BasicSword, 0, royale::ItemId::DinsFire, player), t);
    Vec3s* j = player->skelAnime.jointTable;
    if (gEmote.id == royale::kChickenDanceEmote) {
        ApplyChickenDance(player, t);
        if (player->actor.scale.y > 0.0f) j[0].y = static_cast<s16>(j[0].y + ChickenDanceBob(t) / player->actor.scale.y);
    }
}

void DrawLocalDressed(Player* player, PlayState* play, bool mayPose);   // below, with the held weapon
// Your own Link while you are in a match (and while you emote): drawn wearing what you have and holding what you use. See DrawLocalDressed.
void LocalLink_Draw(Actor* actor, PlayState* play) {
    Player* player = reinterpret_cast<Player*>(actor);
    if (player != GET_PLAYER(play)) { Player_Draw(actor, play); return; }
    const bool emote = gEmote.id >= 0 && gEmote.player == player;
    if (emote) PoseEmote(player);
    DrawLocalDressed(player, play, !emote);
}

// The wheel's controller side. Runs before the game reads the controller each frame, so while the wheel is up the stick and buttons
// choose an emote instead of moving Link.
void OnEmoteWheelInput() {
    if (WarMenuOpen()) return;
    if (gPlayState == nullptr || !gSession.Joined() || !InGame()) { if (gWheel.open) CloseEmoteWheel(); return; }
    Input& in = gPlayState->state.input[0];
    const royale::HudState hud = gSession.Hud();
    if (MatchUiInput(in, hud)) { if (gWheel.open) CloseEmoteWheel(); return; }   // the death card or the end screen has the controller
    if (CartInput(in, hud)) { if (gWheel.open) CloseEmoteWheel(); return; }   // riding: the controller drives the cart
    // Skydiving: Z dives faster. It is taken off the controller before the game reads it, so Link doesn't Z-target (which parks the camera level
    // and hides the ground you are heading for); the camera stays free to look down at where you are going to land.
    gDiveHeld = gSkydiving && (in.cur.button & BTN_Z);
    if (gSkydiving) in.cur.button &= ~BTN_Z, in.press.button &= ~BTN_Z, in.rel.button &= ~BTN_Z;
    if (!gWheel.open) {
        if (!(in.press.button & BTN_CRIGHT) || !CanEmote(hud)) return;
        OpenEmoteWheel(false);
    }
    if (!CanEmote(hud) || (in.press.button & BTN_START)) { CloseEmoteWheel(); return; }
    if (in.press.button & BTN_B) {
        CloseEmoteWheel();
        Audio_PlaySoundGeneral(NA_SE_SY_FSEL_CLOSE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    } else {
        const float sx = in.cur.stick_x, sy = in.cur.stick_y;
        if (sx * sx + sy * sy > 35.0f * 35.0f) {   // the last emote pointed at stays chosen when the stick springs back
            const int slice = WheelSliceAt(sx, sy);
            if (slice != gWheel.hover)
                Audio_PlaySoundGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            gWheel.hover = slice;
        }
        int pick = -1;
        if (in.press.button & BTN_A) pick = gWheel.hover;
        if (!gWheel.byTouch && !(in.cur.button & BTN_CRIGHT)) {   // let go of C-Right
            if (gWheel.hover >= 0) pick = gWheel.hover;
            else if (ImGui::GetTime() - gWheel.openedAt < 0.3) pick = gLastEmote;   // a quick tap: the last one again
            else CloseEmoteWheel();
        }
        if (pick >= 0) { CloseEmoteWheel(); StartEmote(pick, hud); }
    }
    // This frame's buttons and stick were the wheel's.
    in.cur.button = in.press.button = in.rel.button = 0;
    in.cur.stick_x = in.cur.stick_y = in.rel.stick_x = in.rel.stick_y = in.press.stick_x = in.press.stick_y = 0;
}

// ---- the mod's own sounds, mixed into the game's audio ------------------------------------------------------------------------------
// Everything the mod plays (the music folder's songs, the chicken dance tune, the storm sounds, Lilo's mews and her accident) is mixed into each buffer the game's own
// audio thread makes (patch 0016 calls MixVoices for every buffer, 44.1 kHz stereo), each on a voice of its own, so they play together and none
// silences another or the game's sound. They used to be queued on extra SDL audio streams, which never open on Windows (the game uses WASAPI
// there and never starts SDL's audio) and may not on a phone: those sounds stayed silent.
enum Voice { kVoiceSong, kVoiceChicken, kVoiceOneShot, kVoiceCat, kVoiceBaby, kVoiceToy, kVoiceToySfx, kVoiceMaya, kVoicePhone, kVoiceCount };
struct MixVoiceState {
    std::shared_ptr<const std::vector<int16_t>> pcm;
    bool stereo = false, loop = false;
    int rate = 44100;
    double pos = 0;     // in frames of the voice's own rate
    float volume = 0.0f;
};
std::mutex gMixMutex;
MixVoiceState gVoices[kVoiceCount];
}   // namespace
extern "C" void (*gRoyaleAudioMix)(int16_t* buf, uint32_t frames);
namespace {
void MixVoices(int16_t* buf, uint32_t frames) {
    std::lock_guard<std::mutex> lock(gMixMutex);
    for (MixVoiceState& v : gVoices) {
        if (!v.pcm || v.volume <= 0.0f) continue;
        const std::vector<int16_t>& p = *v.pcm;
        const size_t ch = v.stereo ? 2 : 1, total = p.size() / ch;
        if (total == 0) continue;
        const double step = static_cast<double>(v.rate) / 44100.0;
        for (uint32_t i = 0; i < frames; i++) {
            size_t f = static_cast<size_t>(v.pos);
            if (f >= total) {
                if (!v.loop) { v.pcm.reset(); break; }
                v.pos = std::fmod(v.pos, static_cast<double>(total));
                f = static_cast<size_t>(v.pos);
            }
            const float gain = gMenuAudioMuted.load(std::memory_order_relaxed) ? 0.0f : v.volume;
            const int l = static_cast<int>(p[f * ch] * gain), r = v.stereo ? static_cast<int>(p[f * ch + 1] * gain) : l;
            buf[2 * i] = static_cast<int16_t>(std::clamp(buf[2 * i] + l, -32768, 32767));
            buf[2 * i + 1] = static_cast<int16_t>(std::clamp(buf[2 * i + 1] + r, -32768, 32767));
            v.pos += step;
        }
    }
}
void StartVoice(Voice id, std::shared_ptr<const std::vector<int16_t>> pcm, bool stereo, int rate, bool loop, float volume) {
    gRoyaleAudioMix = MixVoices;
    std::lock_guard<std::mutex> lock(gMixMutex);
    MixVoiceState& v = gVoices[id];
    v.pcm = std::move(pcm); v.stereo = stereo; v.rate = rate; v.loop = loop; v.volume = volume; v.pos = 0;
}
void StopVoice(Voice id) {
    std::lock_guard<std::mutex> lock(gMixMutex);
    gVoices[id].pcm.reset();
}
void SetVoiceVolume(Voice id, float volume) {
    std::lock_guard<std::mutex> lock(gMixMutex);
    gVoices[id].volume = volume;
}
bool VoiceDone(Voice id) {
    std::lock_guard<std::mutex> lock(gMixMutex);
    return !gVoices[id].pcm;
}
// The game's own master volume (its default is 40) times the music volume.
float GameVolume(bool music) {
    const float master = static_cast<float>(CVarGetInteger(CVAR_SETTING("Volume.Master"), 40)) / 100.0f;
    const float sub = music ? static_cast<float>(CVarGetInteger(CVAR_SETTING("Volume.MainMusic"), 100)) / 100.0f : 1.0f;
    return std::clamp(master * sub, 0.0f, 1.0f);
}

// The chicken dance tune (shared/tune.h), played on a small audio device of its own beside the game's. You hear it when you do the dance, or
// when somebody doing it is near: louder the closer they are.
void StopChickenMusic() { StopVoice(kVoiceChicken); }

void UpdateChickenMusic() {
    float volume = 0.0f;
    if (gEmote.id == royale::kChickenDanceEmote) volume = 1.0f;
    else if (gPlayState != nullptr && InField() && GET_PLAYER(gPlayState) != nullptr) {
        Player* me = GET_PLAYER(gPlayState);
        for (const auto& [id, st] : gState) {
            if (!st.alive || st.anim != static_cast<uint8_t>(royale::Anim::Emote5)) continue;
            const float d = std::hypot(st.x - me->actor.world.pos.x, st.z - me->actor.world.pos.z);
            const float v = std::clamp(1.0f - d / 1800.0f, 0.0f, 1.0f);
            volume = std::max(volume, v * v * 0.8f);
        }
    }
    volume *= GameVolume(false) * 0.7f;
    if (volume < 0.01f) { StopChickenMusic(); return; }
    if (VoiceDone(kVoiceChicken)) {
        static const auto tune = std::make_shared<const std::vector<int16_t>>(royale::BuildChickenTune());
        StartVoice(kVoiceChicken, tune, false, royale::kTuneRate, true, volume);
    } else SetVoiceVolume(kVoiceChicken, volume);
}

// ---- carts: the Lon Lon Buggy -------------------------------------------------------------------------------------------------------
// A two-seat wooden buggy made in Blender in the N64 style (tools/cart/, assets/cart/cart.blend; shared/cart_model.h has the mesh, shared/vehicle.h the
// physics). The server parks them round the map and knows who sits where; whoever drives runs the physics in their own game against the real
// ground (so it rolls over every bump the game has) and reports the cart each tick. Carts driven by bots, or rolling with nobody at the reins, are
// moved by the server and drawn here on the real ground under their wheels.
//
// Controls. On foot next to a cart: A gets in (the driver's seat from its left, the back seat from its right). Driving: A goes, B brakes and then
// backs up, the stick steers, Z is the handbrake (slide round corners), R gets out. In the back seat: B and the C items still work (shoot from the
// saddle), A takes the reins if nobody is driving, R gets out.
namespace C = royale::cart;
constexpr float kSaddleDrop = 27.0f;   // Link's root sits this far under the top of the saddle (the game puts him 27 under Epona's rider point)
constexpr float kCartSub = 8.0f;       // the model goes to the graphics chip in 1/8 units, so its small parts keep their shape
constexpr float kCartHearing = 1800.0f;
constexpr float kSaddleClimbSeconds = 0.45f;   // the cart doesn't go until you are sat down

// The model, built once: the vertices with a fixed light baked into their colours (lit from above and a little in front, as the N64 games bake
// theirs), and one display list per rigid part, each loading the 64x32 texture.
struct CartGpu {
    std::vector<Vtx> vtx;
    std::vector<Gfx> dl[C::kPartCount];
    bool built = false;
};
CartGpu gCartGpu;

void BuildCartGpu() {
    if (gCartGpu.built) return;
    gCartGpu.vtx.resize(C::kVertCount);
    const float lx = 0.30f, ly = 0.86f, lz = 0.41f;
    for (int i = 0; i < C::kVertCount; i++) {
        const C::Vert& v = C::kVerts[i];
        const float nx = v.nx / 127.0f, ny = v.ny / 127.0f, nz = v.nz / 127.0f;
        const float lit = std::clamp(0.50f + 0.62f * std::max(0.0f, nx * lx + ny * ly + nz * lz) + 0.08f * ny, 0.0f, 1.0f);
        Vtx& o = gCartGpu.vtx[i];
        o.v.ob[0] = static_cast<s16>(std::lround(v.x * kCartSub));
        o.v.ob[1] = static_cast<s16>(std::lround(v.y * kCartSub));
        o.v.ob[2] = static_cast<s16>(std::lround(v.z * kCartSub));
        o.v.flag = 0;
        o.v.tc[0] = v.s;
        o.v.tc[1] = v.t;
        o.v.cn[0] = static_cast<u8>(255.0f * lit);
        o.v.cn[1] = static_cast<u8>(248.0f * lit);
        o.v.cn[2] = static_cast<u8>(236.0f * lit);
        o.v.cn[3] = 255;
    }
    for (int part = 0; part < C::kPartCount; part++) {
        std::vector<Gfx>& dl = gCartGpu.dl[part];
        size_t need = 16;
        for (int b = 0; b < C::kBatchCount; b++) if (C::kBatches[b].part == part) need += 2 + (C::kBatches[b].triCount + 1) / 2;
        dl.assign(need, Gfx{});
        Gfx* g = dl.data();
        gDPLoadTextureBlock(g++, C::kTex, G_IM_FMT_RGBA, G_IM_SIZ_16b, C::kTexW, C::kTexH, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP,
                            G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        for (int b = 0; b < C::kBatchCount; b++) {
            const C::Batch& bt = C::kBatches[b];
            if (bt.part != part) continue;
            gSPVertex(g++, reinterpret_cast<uintptr_t>(&gCartGpu.vtx[bt.firstVert]), bt.vertCount, 0);
            const int end = bt.firstTri + bt.triCount;
            int t = bt.firstTri;
            for (; t + 1 < end; t += 2)
                gSP2Triangles(g++, C::kTris[t][0], C::kTris[t][1], C::kTris[t][2], 0, C::kTris[t + 1][0], C::kTris[t + 1][1], C::kTris[t + 1][2], 0);
            if (t < end) gSP1Triangle(g++, C::kTris[t][0], C::kTris[t][1], C::kTris[t][2], 0);
        }
        gSPEndDisplayList(g++);
        dl.resize(static_cast<size_t>(g - dl.data()));   // only shrinks: nothing moves
    }
    gCartGpu.built = true;
}

// One cart as it is shown: smoothed from the snapshots (or, for the one you drive, your own physics), sat on the real ground, wheels turning.
struct CartView {
    Actor* actor = nullptr;
    ActorFunc origDestroy = nullptr;
    int index = -1;
    royale::net::VehicleNet net{};
    royale::CartBody body;           // as drawn
    float air = 0;
    bool drift = false;
    float spin[4] = {};              // each wheel's roll, radians
    float prevX = 0, prevZ = 0;
    bool placed = false;
    float hp = 1.0f;
    bool wrecked = false;
    double wreckedAt = 0, hurtAt = -10.0;
    uint16_t driver = royale::net::kNoPlayer16, passenger = royale::net::kNoPlayer16;
    float wasAir = 0;
};
std::map<int, CartView> gCarts;
std::unordered_map<const Actor*, int> gCartOf;

CartView* CartViewOf(int index) {
    auto it = gCarts.find(index);
    return it == gCarts.end() || it->second.actor == nullptr ? nullptr : &it->second;
}

// ---- the ground a cart drives on, in this game: the scene's floors (and our blocks and walls), its walls, water, rocks and the other carts.
float gCartRefY = 0;               // the cart's own height: the ground is looked for from a little above it (so a bridge overhead isn't the floor)
royale::Vec2 gCartFrom = {};       // the cart's middle: walls are looked for between it and each corner
int gCartSelf = -1;                // the cart being moved (it doesn't run into itself)
float gCartMapRadius = 0;
royale::Vec2 gCartMapCentre = {};

bool CartGroundAt(float x, float z, float* y) {
    if (!InField()) return false;
    CollisionPoly poly;
    s32 bgId = BGCHECK_SCENE;
    Vec3f pos = { x, gCartRefY + 160.0f, z };
    const float h = BgCheck_AnyRaycastFloor2(&gPlayState->colCtx, &poly, &bgId, &pos);
    if (h <= BGCHECK_Y_MIN + 1.0f) return false;
    *y = h;
    return true;
}
bool CartSolidAt(float x, float z) {
    if (!InField()) return true;
    if (gCartMapRadius > 0.0f && royale::Distance({ x, z }, gCartMapCentre) > gCartMapRadius + 400.0f) return true;   // the far edge of the world
    float gy = 0;
    if (CartGroundAt(x, z, &gy) && UnderWater(x, z, gy)) return true;
    {   // a wall between the middle of the cart and this corner, at bumper height
        Vec3f from = { gCartFrom.x, gCartRefY + 32.0f, gCartFrom.z }, to = { x, gCartRefY + 32.0f, z }, hit;
        CollisionPoly* poly = nullptr;
        s32 bgId = 0;
        if (BgCheck_EntityLineTest1(&gPlayState->colCtx, &from, &to, &hit, &poly, true, false, false, true, &bgId)) return true;
    }
    if (const royale::GameClient* c = gSession.Client()) {   // rocks, boulders and pillars (the blocks are solid ground already)
        const auto& props = c->Props();
        for (size_t i = 0; i < props.size(); i++) {
            const royale::Prop& p = props[i];
            if (p.kind != royale::PropKind::Rock && p.kind != royale::PropKind::Boulder && p.kind != royale::PropKind::Pillar) continue;
            if (gBrokenProps.count(i)) continue;
            const float r = royale::PropRadius(p.kind) * (p.kind == royale::PropKind::Boulder ? royale::BoulderScale(p.rot) : 1.0f);
            const float dx = x - p.pos.x, dz = z - p.pos.z;
            if (dx * dx + dz * dz < r * r) return true;
        }
    }
    for (const auto& [i, v] : gCarts) if (i != gCartSelf && v.actor != nullptr && royale::InsideCart(v.body, x, z, -4.0f)) return true;
    return false;
}
const royale::CartWorld& ClientCartWorld() {
    static const royale::CartWorld w{ [](float x, float z, float* y) { return CartGroundAt(x, z, y); }, [](float x, float z) { return CartSolidAt(x, z); } };
    return w;
}

// ---- your own ride ----------------------------------------------------------------------------------------------------------------
struct LocalRide {
    // the controller, read before the game sees it (and taken away from Link while you ride)
    float throttle = 0, steer = 0;
    bool brake = false, handbrake = false;
    bool wantExit = false, wantSwitch = false;
    // the seat
    bool seated = false;
    int index = -1;
    royale::Seat seat = royale::Seat::None;
    double mountAt = -10.0, dismountAt = -10.0;
    royale::Seat dismountSeat = royale::Seat::Driver;
    royale::CartBody lastBody;        // where the cart was when you got off (the climb down is shown from its saddle)
    royale::Vec2 exitTo = {};
    bool settleOnGround = false;
    // driving
    bool driving = false;
    royale::CartBody body;
    float air = 0;
    double enterAskedAt = -10.0;
    bool exitWhenStopped = false;
    float shake = 0;
    Player* player = nullptr;
};
LocalRide gRide;

void LocalRide_Draw(Actor* actor, PlayState* play);

// The climb into the saddle, the ride and the climb down: Link's own horse-riding animations (the game's, for Epona).
LinkAnimationHeader* MountAnim(royale::Seat s) { return s == royale::Seat::Passenger ? RA(uma_right_up) : RA(uma_left_up); }
LinkAnimationHeader* DismountAnim(royale::Seat s) { return s == royale::Seat::Passenger ? RA(uma_right_down) : RA(uma_left_down); }
float AnimSeconds(LinkAnimationHeader* a) { return (static_cast<float>(Animation_GetLastFrame(a)) + 1.0f) / royale::kTickHz; }

// Which cart (and seat) a player is on, and the saddle's place and the cart's tilt for drawing them there.
struct RiderPose {
    float x = 0, y = 0, z = 0;        // the top of the saddle
    s16 yaw = 0, pitch = 0, roll = 0;
    royale::Seat seat = royale::Seat::None;
    float steer = 0, speed = 0;
    int index = -1;
};
bool CartRiderPose(uint16_t id, RiderPose* out) {
    for (const auto& [i, v] : gCarts) {
        if (v.actor == nullptr || v.wrecked) continue;
        royale::Seat s = royale::Seat::None;
        if (v.driver == id) s = royale::Seat::Driver;
        else if (v.passenger == id) s = royale::Seat::Passenger;
        else continue;
        royale::SeatSpot(v.body, s, &out->x, &out->y, &out->z);
        out->yaw = royale::YawToBinang(v.body.yaw);
        out->pitch = royale::YawToBinang(-v.body.pitch);
        // leaning into the turn (and with the cart's own sway over bumps)
        const float lean = std::clamp(-v.body.speed * v.body.steer * 0.0011f, -0.22f, 0.22f);
        out->roll = royale::YawToBinang(v.body.roll + lean);
        out->seat = s;
        out->steer = v.body.steer;
        out->speed = v.body.speed;
        out->index = i;
        return true;
    }
    return false;
}

// ---- drawing ----------------------------------------------------------------------------------------------------------------------
void DrawCart(PlayState* play, const CartView& v) {
    BuildCartGpu();
    const royale::CartBody& b = v.body;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);   // the light is baked into the vertex colours; both sides of thin planks show
    // texture times the vertex colour, times a tint: charred black once wrecked, darker as it takes damage, a red flash when hit
    gDPSetCombineLERP(POLY_OPA_DISP++, TEXEL0, 0, SHADE, 0, 0, 0, 0, 1, COMBINED, 0, PRIMITIVE, 0, 0, 0, 0, 1);
    u8 r = 255, g = 255, bl = 255;
    if (v.wrecked) { r = 58; g = 48; bl = 42; }
    else {
        const float worn = 0.62f + 0.38f * std::clamp(v.hp, 0.0f, 1.0f);
        r = static_cast<u8>(255 * worn); g = static_cast<u8>(255 * worn); bl = static_cast<u8>(255 * worn);
        if (ImGui::GetTime() - v.hurtAt < 0.15) { r = 255; g = 120; bl = 110; }
    }
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, r, g, bl, 255);
    // the cart: yaw, then pitch (nose up is a turn about its left axis), then roll, as royale::CartToWorld does
    Matrix_Translate(b.x, b.y, b.z, MTXMODE_NEW);
    Matrix_RotateY(b.yaw, MTXMODE_APPLY);
    Matrix_RotateX(-b.pitch, MTXMODE_APPLY);
    Matrix_RotateZ(b.roll, MTXMODE_APPLY);
    for (int part = 0; part < C::kPartCount; part++) {
        Matrix_Push();
        const C::P3& pv = C::kPivot[part];
        Matrix_Translate(pv.x, pv.y, pv.z, MTXMODE_APPLY);
        if (part == C::kWheelFL || part == C::kWheelFR) Matrix_RotateY(b.steer, MTXMODE_APPLY);   // the front wheels steer
        if (part == C::kHandlebar) Matrix_RotateY(b.steer * 0.8f, MTXMODE_APPLY);
        if (part >= C::kWheelFL && part <= C::kWheelBR) Matrix_RotateX(v.spin[part - C::kWheelFL], MTXMODE_APPLY);   // and all four roll
        Matrix_Scale(1.0f / kCartSub, 1.0f / kCartSub, 1.0f / kCartSub, MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, gCartGpu.dl[part].data());
        Matrix_Pop();
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// ---- dust, smoke, sparks and fire, and how it sounds -------------------------------------------------------------------------------
bool CartAudible(const Actor* a) {
    if (gPlayState == nullptr) return false;
    const Player* me = GET_PLAYER(gPlayState);
    const float dx = a->world.pos.x - me->actor.world.pos.x, dz = a->world.pos.z - me->actor.world.pos.z;
    return dx * dx + dz * dz < kCartHearing * kCartHearing;
}
void CartSfx(Actor* a, u16 sfx) {
    if (CartAudible(a)) Audio_PlaySoundGeneral(sfx, &a->projectedPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}
Vec3f CartPoint(const royale::CartBody& b, const C::P3& p) {
    Vec3f out;
    royale::CartToWorld(b, p, &out.x, &out.y, &out.z);
    return out;
}
// A crash: splinters of light off the bumper and a wooden thump (a hard one rings the iron tyres).
void CartCrashFx(PlayState* play, CartView& v, float impact) {
    Vec3f at = CartPoint(v.body, { 0.0f, 30.0f, v.body.speed >= 0.0f ? C::kMaxZ : C::kMinZ });
    Vec3f vel = { 0, 2.0f, 0 }, accel = { 0, -0.4f, 0 };
    for (int i = 0; i < 3 + static_cast<int>(impact / 120.0f); i++) {
        Vec3f p = { at.x + Rand_CenteredFloat(30.0f), at.y + Rand_CenteredFloat(14.0f), at.z + Rand_CenteredFloat(30.0f) };
        Vec3f sv = { Rand_CenteredFloat(6.0f), 3.0f + Rand_ZeroOne() * 4.0f, Rand_CenteredFloat(6.0f) };
        EffectSsGSpk_SpawnRandColor(play, v.actor, &p, &sv, &accel, 60, 0);
    }
    EffectSsHitMark_SpawnFixedScale(play, 0, &at);
    (void)vel;
    CartSfx(v.actor, NA_SE_EV_WOOD_BOUND);
    if (impact > 300.0f) CartSfx(v.actor, NA_SE_EV_BLOCK_SHAKE);
}
void CartBlastFx(PlayState* play, CartView& v) {
    Vec3f at = { v.body.x, v.body.y + 40.0f, v.body.z }, zero = { 0, 0, 0 };
    EffectSsBomb2_SpawnLayered(play, &at, &zero, &zero, 120, 19);
    AddLightFlash(at.x, at.y + 20.0f, at.z, 255, 160, 70, 520.0f, 1.0f, 1);
    Vec3f low = { v.body.x, v.body.y + 4.0f, v.body.z };
    EffectSsBlast_SpawnWhiteShockwave(play, &low, &zero, &zero);
    for (int i = 0; i < 10; i++) {
        Vec3f p = { at.x + Rand_CenteredFloat(60.0f), at.y + Rand_CenteredFloat(30.0f), at.z + Rand_CenteredFloat(60.0f) };
        Vec3f sv = { Rand_CenteredFloat(10.0f), 6.0f + Rand_ZeroOne() * 6.0f, Rand_CenteredFloat(10.0f) }, acc = { 0, -0.6f, 0 };
        EffectSsGSpk_SpawnRandColor(play, v.actor, &p, &sv, &acc, 90, 0);
    }
    CartSfx(v.actor, NA_SE_IT_BOMB_EXPLOSION);
    CartSfx(v.actor, NA_SE_EV_WOODBOX_BREAK);
}
void CartEffects(PlayState* play, CartView& v) {
    const u32 frame = play->gameplayFrames;
    const royale::CartBody& b = v.body;
    const float speed = std::fabs(b.speed);
    const bool onGround = v.air < 3.0f;
    if (v.wrecked) {   // burning: flames on the deck, black smoke, the crackle of the fire
        if (frame % 2 == 0) {
            Vec3f p = CartPoint(b, { Rand_CenteredFloat(70.0f), 45.0f + Rand_ZeroOne() * 20.0f, Rand_CenteredFloat(120.0f) });
            Vec3f vel = { 0, 0.6f, 0 }, acc = { 0, 0.15f, 0 };
            EffectSsKFire_Spawn(play, &p, &vel, &acc, 18 + static_cast<s16>(Rand_ZeroOne() * 14.0f), 0);
        }
        if (frame % 3 == 0) {
            Vec3f p = CartPoint(b, { Rand_CenteredFloat(50.0f), 70.0f, Rand_CenteredFloat(90.0f) });
            Vec3f vel = { 0, 1.6f, 0 }, acc = { 0, 0.08f, 0 };
            Color_RGBA8 prim = { 40, 36, 34, 255 }, env = { 10, 10, 10, 255 };
            func_8002836C(play, &p, &vel, &acc, &prim, &env, 90, 24, 26);
        }
        if (CartAudible(v.actor)) func_8002F974(v.actor, NA_SE_EV_BURN_OUT - SFX_FLAG);
        return;
    }
    // The chimney: a puff of smoke every few frames (more and darker as the cart is smashed up), streaming back as it goes.
    const int every = v.hp < 0.35f ? 1 : (speed > 200.0f ? 3 : 5);
    if (frame % every == 0) {
        Vec3f p = CartPoint(b, C::kExhaust);
        const float fx = std::sin(b.yaw), fz = std::cos(b.yaw);
        Vec3f vel = { -fx * b.speed * 0.02f, 1.4f, -fz * b.speed * 0.02f }, acc = { 0, 0.05f, 0 };
        const u8 grey = v.hp < 0.35f ? 50 : 205;
        Color_RGBA8 prim = { grey, grey, static_cast<u8>(grey - 5), 255 }, env = { static_cast<u8>(grey * 0.6f), static_cast<u8>(grey * 0.6f), static_cast<u8>(grey * 0.6f), 255 };
        func_8002836C(play, &p, &vel, &acc, &prim, &env, v.hp < 0.35f ? 60 : 34, 14, 22);
    }
    // A damaged firebox spits sparks.
    if (v.hp < 0.35f && frame % 7 == 0) {
        Vec3f p = CartPoint(b, { 0.0f, 55.0f, C::kMinZ + 10.0f });
        Vec3f sv = { Rand_CenteredFloat(3.0f), 3.0f, Rand_CenteredFloat(3.0f) }, acc = { 0, -0.3f, 0 };
        EffectSsGSpk_SpawnRandColor(play, v.actor, &p, &sv, &acc, 40, 0);
    }
    // Dust off the wheels: kicked up behind the back wheels at speed, from all four in a slide, and a burst when it lands from a jump.
    if (onGround && (speed > 90.0f || v.drift) && frame % (v.drift ? 1 : 2) == 0) {
        for (int w = v.drift ? 0 : 2; w < 4; w++) {
            const C::P3& pv = C::kPivot[C::kWheelFL + w];
            Vec3f at = CartPoint(b, { pv.x, 0.0f, pv.z });
            const s16 scale = static_cast<s16>(std::min(130.0f, 50.0f + speed * 0.15f + (v.drift ? 30.0f : 0.0f)));
            v.actor->floorHeight = at.y;   // (the ring is put on the actor's floor)
            Actor_SpawnFloorDustRing(play, v.actor, &at, 6.0f, 0, 3.0f, scale, 14, true);
        }
    }
    if (v.wasAir > 25.0f && onGround) {
        for (int w = 0; w < 4; w++) {
            const C::P3& pv = C::kPivot[C::kWheelFL + w];
            Vec3f at = CartPoint(b, { pv.x, 0.0f, pv.z });
            v.actor->floorHeight = at.y;
            Actor_SpawnFloorDustRing(play, v.actor, &at, 14.0f, 2, 5.0f, 110, 18, true);
        }
        CartSfx(v.actor, NA_SE_EV_WOOD_BOUND);
    }
    v.wasAir = v.air;
    // The iron tyres on the ground, higher the faster it goes; a scrape in a slide.
    if (onGround && speed > 25.0f && CartAudible(v.actor)) func_800F436C(&v.actor->projectedPos, NA_SE_EV_STONE_ROLLING - SFX_FLAG, 0.55f + std::min(1.0f, speed / royale::kCartMaxSpeed) * 0.75f);
    if (onGround && v.drift && speed > 120.0f && frame % 4 == 0) CartSfx(v.actor, NA_SE_PL_SLIP);
}

// ---- the actors ---------------------------------------------------------------------------------------------------------------------
void Cart_Update(Actor* actor, PlayState* play) {
    auto of = gCartOf.find(actor);
    if (of == gCartOf.end()) { Actor_Kill(actor); return; }
    CartView& v = gCarts[of->second];
    const bool mine = gRide.driving && gRide.index == v.index;
    const float prevHp = v.hp;
    const bool wasWrecked = v.wrecked;
    royale::net::VehicleNet n;
    const bool sampled = gSession.Client() && gSession.Client()->SampleVehicle(v.index, n);
    if (sampled) {
        v.net = n;
        v.hp = n.hp / 255.0f;
        v.wrecked = (n.flags & royale::net::VehicleNet::kWrecked) != 0;
        v.driver = n.driver;
        v.passenger = n.passenger;
    }
    if (mine) {
        v.body = gRide.body;
        v.air = gRide.air;
        v.drift = gRide.handbrake && std::fabs(gRide.body.speed) > 60.0f;
    } else if (sampled) {
        royale::CartBody& b = v.body;
        b.x = n.x; b.z = n.z;
        b.yaw = royale::BinangToYaw(n.yaw);
        b.steer = n.steer / 100.0f;
        b.speed = n.speed;
        v.air = n.air;
        v.drift = (n.flags & royale::net::VehicleNet::kDrift) != 0;
        // sat on the real ground under its wheels (the server only knows its own rougher copy of the ground), tilted to it
        gCartRefY = v.placed ? b.y : n.y;
        const royale::cartdetail::Footing f = royale::cartdetail::FootingAt(ClientCartWorld(), b.x, b.z, b.yaw);
        const float groundY = f.any ? f.y : n.y - n.air;
        const float k = v.placed ? 0.35f : 1.0f;
        b.y = v.placed ? b.y + (groundY + n.air - b.y) * 0.6f : groundY + n.air;
        if (f.any) { b.pitch += (f.pitch - b.pitch) * k; b.roll += (f.roll - b.roll) * k; }
        b.grounded = n.air < 1.0f;
    }
    // the wheels roll by how far it has gone along its heading
    if (v.placed) {
        const float along = (v.body.x - v.prevX) * std::sin(v.body.yaw) + (v.body.z - v.prevZ) * std::cos(v.body.yaw);
        if (std::fabs(along) < 300.0f) for (float& s : v.spin) s = std::fmod(s + along / C::kWheelRadius, 6.2831853f);
    }
    v.prevX = v.body.x; v.prevZ = v.body.z;
    v.placed = true;
    actor->world.pos = { v.body.x, v.body.y, v.body.z };
    actor->shape.rot.y = actor->world.rot.y = royale::YawToBinang(v.body.yaw);
    actor->focus.pos = actor->world.pos;
    actor->focus.pos.y += 60.0f;
    actor->floorHeight = v.body.y - v.air;   // its shadow, on the ground under it
    if (v.wrecked && !wasWrecked) { v.wreckedAt = ImGui::GetTime(); CartBlastFx(play, v); }
    else if (v.hp < prevHp - 0.004f && !v.wrecked) {
        v.hurtAt = ImGui::GetTime();
        if (!mine) CartCrashFx(play, v, 200.0f);   // your own crashes are shown as they happen
    }
    CartEffects(play, v);
}
void Cart_Draw(Actor* actor, PlayState* play) {
    auto of = gCartOf.find(actor);
    if (of == gCartOf.end()) return;
    DrawCart(play, gCarts[of->second]);
}
void Cart_Destroy(Actor* actor, PlayState* play) {
    ActorFunc orig = nullptr;
    auto of = gCartOf.find(actor);
    if (of != gCartOf.end()) {
        auto v = gCarts.find(of->second);
        if (v != gCarts.end()) { orig = v->second.origDestroy; gCarts.erase(v); }
        gCartOf.erase(of);
    }
    if (orig) orig(actor, play);
}

// A stand-in actor for each cart in the snapshot (the ones near you, and yours), gone when it leaves view or burns out.
void ReconcileCarts(const royale::HudState& hud) {
    const bool show = gSession.Joined() && InField() && gSession.Client() &&
                      (hud.state == royale::MatchState::InMatch || hud.state == royale::MatchState::Ending);
    gCartMapRadius = hud.map.radius;
    gCartMapCentre = hud.map.center;
    std::unordered_set<int> wanted;
    if (show) {
        for (const royale::net::VehicleNet& n : gSession.Client()->Vehicles()) {
            wanted.insert(n.index);
            if (gCarts.count(n.index)) continue;
            Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ISHI, n.x, n.y, n.z, 0, n.yaw, 0, 0, false);
            if (actor == nullptr) continue;
            CartView v;
            v.actor = actor; v.origDestroy = actor->destroy; v.index = n.index; v.net = n;
            v.body.x = n.x; v.body.y = n.y; v.body.z = n.z; v.body.yaw = royale::BinangToYaw(n.yaw);
            v.hp = n.hp / 255.0f; v.wrecked = (n.flags & royale::net::VehicleNet::kWrecked) != 0;
            v.driver = n.driver; v.passenger = n.passenger;
            gCarts[n.index] = v;
            gCartOf[actor] = n.index;
            actor->update = Cart_Update;
            actor->draw = Cart_Draw;
            actor->destroy = Cart_Destroy;
            actor->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
            actor->uncullZoneForward = 5000.0f; actor->uncullZoneScale = 1500.0f; actor->uncullZoneDownward = 1500.0f;
            actor->shape.shadowScale = 70.0f;
            actor->shape.shadowAlpha = 160;
        }
    }
    for (auto& [i, v] : gCarts) if (!wanted.count(i) && v.actor) { Actor_Kill(v.actor); v.actor = nullptr; }
}

// The free seat of a cart you are standing by, if there is one (the seat whose side you are on, else the other).
bool NearbyCartSeat(int* index, royale::Seat* seat) {
    if (!InField() || gRide.seated) return false;
    Player* pl = GET_PLAYER(gPlayState);
    const royale::Vec2 me = { pl->actor.world.pos.x, pl->actor.world.pos.z };
    float best = royale::kEnterRange;
    bool found = false;
    for (const auto& [i, v] : gCarts) {
        if (v.actor == nullptr || v.wrecked || std::fabs(v.body.speed) > 200.0f) continue;
        if (std::fabs(pl->actor.world.pos.y - v.body.y) > 80.0f) continue;
        for (royale::Seat s : { royale::Seat::Driver, royale::Seat::Passenger }) {
            const bool free = (s == royale::Seat::Driver ? v.driver : v.passenger) == royale::net::kNoPlayer16;
            if (!free) continue;
            const float d = std::min(royale::Distance(me, royale::ExitSpot(v.body, s)), royale::Distance(me, { v.body.x, v.body.z }) + 20.0f);
            if (d < best) { best = d; *index = i; *seat = s; found = true; }
        }
    }
    return found;
}

// ---- the controller --------------------------------------------------------------------------------------------------------------
// Runs before the game reads the controller. Riding, it is all the cart's: Link stays sat down (the back seat keeps B and the C items to shoot with).
// On foot by a cart, A gets in instead of rolling.
bool CartInput(Input& in, const royale::HudState& hud) {
    if (!LiveAndAlive(hud) || !InField()) { gRide.wantExit = gRide.wantSwitch = false; return false; }
    if (!gRide.seated) {
        int index; royale::Seat seat;
        if ((in.press.button & BTN_A) && !MessageBoxUp() && NearestLootIndex() == kNoLoot && NearbyCartSeat(&index, &seat) &&
            ImGui::GetTime() - gRide.enterAskedAt > 0.4) {
            gSession.EnterVehicle(index, seat);
            gRide.enterAskedAt = ImGui::GetTime();
            in.press.button &= ~BTN_A; in.cur.button &= ~BTN_A;
        }
        return false;
    }
    const float sx = in.cur.stick_x / 70.0f;
    gRide.steer = std::fabs(sx) < 0.12f ? 0.0f : -std::clamp(sx, -1.0f, 1.0f);   // stick right turns right
    const bool driver = gRide.seat == royale::Seat::Driver;
    gRide.throttle = driver && (in.cur.button & BTN_A) ? 1.0f : 0.0f;
    gRide.brake = false;
    if (driver && (in.cur.button & BTN_B)) {
        if (gRide.body.speed > 15.0f) gRide.brake = true;   // braking first, then backing up
        else gRide.throttle = -1.0f;
    }
    gRide.handbrake = driver && (in.cur.button & BTN_Z);
    if (in.press.button & BTN_R) gRide.wantExit = true;
    if (!driver && (in.press.button & BTN_A)) gRide.wantSwitch = true;
    // Link himself doesn't move: the stick and the buttons were the cart's (the passenger keeps B and the C items).
    const u16 keep = driver ? BTN_START : static_cast<u16>(BTN_START | BTN_B | BTN_CLEFT | BTN_CDOWN | BTN_DLEFT | BTN_DRIGHT);
    in.cur.button &= keep; in.press.button &= keep; in.rel.button &= keep;
    in.cur.stick_x = in.cur.stick_y = in.rel.stick_x = in.rel.stick_y = in.press.stick_x = in.press.stick_y = 0;
    return true;
}

// ---- you, in the saddle -----------------------------------------------------------------------------------------------------------
// Link stands on the ground under the saddle (so the game keeps him on his feet and the camera follows the cart), and is drawn up on it: his draw
// is lifted to the saddle and tilted with the cart, and his pose is the horse-riding one (LocalRide_Draw).
void PlaceOnSeat(Player* player, const royale::CartBody& body, royale::Seat seat) {
    float sx, sy, sz;
    royale::SeatSpot(body, seat, &sx, &sy, &sz);
    gCartRefY = body.y;
    float gy = body.y;
    CartGroundAt(sx, sz, &gy);
    player->actor.world.pos = { sx, gy, sz };
    player->actor.prevPos = player->actor.world.pos;
    player->actor.velocity = { 0, 0, 0 };
    player->actor.speedXZ = 0;
    player->linearVelocity = 0;
    player->fallStartHeight = static_cast<s16>(gy);
    player->fallDistance = 0;
    const s16 yaw = royale::YawToBinang(body.yaw);
    player->actor.shape.rot.y = player->actor.world.rot.y = player->yaw = yaw;
    const float lean = std::clamp(-body.speed * body.steer * 0.0011f, -0.22f, 0.22f);
    player->actor.shape.rot.x = royale::YawToBinang(-body.pitch);
    player->actor.shape.rot.z = royale::YawToBinang(body.roll + lean);
    const float scale = std::max(0.001f, player->actor.scale.y);
    player->actor.shape.yOffset = (sy - kSaddleDrop - gy) / scale;
}
void LeaveSeatPose(Player* player) {
    player->actor.shape.rot.x = player->actor.shape.rot.z = 0;
    player->actor.shape.yOffset = 0;
    if (player->actor.draw == LocalRide_Draw) player->actor.draw = Player_Draw;
}

// The camera follows Link's feet on the ground under the saddle, so in the saddle it sits as low as a walking Link's and the front of the cart
// blocks the road. While seated, the view is lifted by how high he is drawn above his feet, plus a little more to see over the nose.
constexpr float kRideCamExtra = 30.0f;
float gCamLiftNow = 0.0f;
void UpdateRideCamera(Player* player) {
    const float scale = std::max(0.001f, player->actor.scale.y);
    const bool seated = gRide.seated && player->actor.draw == LocalRide_Draw;
    const float target = seated ? std::max(0.0f, player->actor.shape.yOffset * scale) + kRideCamExtra : 0.0f;
    gCamLiftNow += (target - gCamLiftNow) * 0.12f;   // eased, so getting on and off glides the view up and down
    if (std::fabs(gCamLiftNow) < 0.05f && target == 0.0f) gCamLiftNow = 0.0f;
    gRoyaleCamLift = gCamLiftNow;
}

// On foot, a cart is solid: you are pushed out of it to its nearest side (as the rocks push you, ApplyRocks).
void PushOutOfCarts(Player* player) {
    Vec3f& p = player->actor.world.pos;
    for (const auto& [i, v] : gCarts) {
        if (v.actor == nullptr || p.y > v.body.y + 70.0f || p.y < v.body.y - 60.0f) continue;
        const royale::CartBody& b = v.body;
        const float cy = std::cos(b.yaw), sy = std::sin(b.yaw);
        const float dx = p.x - b.x, dz = p.z - b.z;
        const float lx = dx * cy - dz * sy, lz = dx * sy + dz * cy;   // in the cart's axes (as royale::InsideCart)
        const float m = 10.0f;
        const float x0 = C::kMinX - m, x1 = C::kMaxX + m, z0 = C::kMinZ - m, z1 = C::kMaxZ + m;
        if (lx <= x0 || lx >= x1 || lz <= z0 || lz >= z1) continue;
        float nx = lx, nz = lz;
        const float out[4] = { lx - x0, x1 - lx, lz - z0, z1 - lz };
        const int side = static_cast<int>(std::min_element(out, out + 4) - out);
        if (side == 0) nx = x0; else if (side == 1) nx = x1; else if (side == 2) nz = z0; else nz = z1;
        p.x = b.x + nx * cy + nz * sy;
        p.z = b.z - nx * sy + nz * cy;
    }
}

void UpdateLocalRide(Player* player, const royale::HudState& hud) {
    royale::GameClient* client = gSession.Client();
    const double now = ImGui::GetTime();
    int index = -1;
    royale::Seat seat = royale::Seat::None;
    const bool riding = client && LiveAndAlive(hud) && InField() && client->MyVehicle(&index, &seat);
    CartView* v = riding ? CartViewOf(index) : nullptr;
    if (gRide.player != player) { gRide.seated = false; gRide.driving = false; gRide.player = player; }   // a new scene, a new Link

    // Climbing down: Link is shown stepping off the saddle, then stands at the cart's side.
    if (!gRide.seated && gRide.settleOnGround) {
        if (now - gRide.dismountAt < AnimSeconds(DismountAnim(gRide.dismountSeat)) && LiveAndAlive(hud)) {
            PlaceOnSeat(player, gRide.lastBody, gRide.dismountSeat);
            return;
        }
        gRide.settleOnGround = false;
        float gy = player->actor.world.pos.y;
        gCartRefY = gRide.lastBody.y;
        CartGroundAt(gRide.exitTo.x, gRide.exitTo.z, &gy);
        player->actor.world.pos = { gRide.exitTo.x, gy + 2.0f, gRide.exitTo.z };
        player->actor.prevPos = player->actor.world.pos;
        player->fallStartHeight = static_cast<s16>(gy);
        LeaveSeatPose(player);
    }

    if (gRide.seated && (!riding || index != gRide.index)) {   // off (by choice, or thrown off a wreck)
        const bool thrown = !LiveAndAlive(hud) || (CartViewOf(gRide.index) && CartViewOf(gRide.index)->wrecked);
        gRide.lastBody = gRide.driving ? gRide.body : (CartViewOf(gRide.index) ? CartViewOf(gRide.index)->body : gRide.lastBody);
        gRide.dismountSeat = gRide.seat;
        gRide.exitTo = royale::ExitSpot(gRide.lastBody, gRide.seat);
        gRide.seated = false;
        gRide.driving = false;
        gRide.index = -1;
        gRide.seat = royale::Seat::None;
        gRide.dismountAt = thrown ? -10.0 : now;
        gRide.settleOnGround = true;
        Player_PlaySfx(&player->actor, NA_SE_PL_GET_OFF_HORSE);
        if (thrown) { player->actor.velocity.y = 8.0f; }
        return;
    }
    if (!riding || v == nullptr) { if (!gRide.seated && !gRide.settleOnGround && InField()) PushOutOfCarts(player); return; }

    if (!gRide.seated || seat != gRide.seat) {   // just got on, or moved over to the other seat
        if (!gRide.seated) { gRide.mountAt = now; Player_PlaySfx(&player->actor, NA_SE_PL_SIT_ON_HORSE); StopEmote(); }
        gRide.seated = true;
        gRide.index = index;
        gRide.seat = seat;
        gRide.driving = false;
        gRide.wantExit = gRide.wantSwitch = gRide.exitWhenStopped = false;
        gRide.settleOnGround = false;
    }
    player->actor.draw = LocalRide_Draw;

    if (gRide.wantExit) {
        gRide.wantExit = false;
        if (seat == royale::Seat::Driver && std::fabs(gRide.body.speed) > 60.0f) gRide.exitWhenStopped = true;   // pull up first
        else gSession.ExitVehicle();
    }
    if (gRide.wantSwitch) { gRide.wantSwitch = false; if (v->driver == royale::net::kNoPlayer16) gSession.SwitchSeat(); }

    if (seat == royale::Seat::Driver) {
        if (!gRide.driving) {   // take over from where it is shown
            gRide.driving = true;
            gRide.body = v->body;
            gRide.body.speed = v->net.speed;
            gRide.body.grounded = v->air < 1.0f;
        }
        royale::CartControls c;
        c.throttle = gRide.throttle; c.steer = gRide.steer; c.brake = gRide.brake; c.handbrake = gRide.handbrake;
        if (now - gRide.mountAt < kSaddleClimbSeconds) c = {};   // still climbing on
        if (gRide.exitWhenStopped) {   // R at speed: pull up, then get out
            c.throttle = 0; c.brake = true;
            if (std::fabs(gRide.body.speed) < 40.0f) { gSession.ExitVehicle(); gRide.exitWhenStopped = false; }
        }
        gCartRefY = gRide.body.y;
        gCartFrom = { gRide.body.x, gRide.body.z };
        gCartSelf = index;
        const royale::CartStep st = royale::StepCart(gRide.body, c, ClientCartWorld(), 1.0f / royale::kTickHz);
        gCartSelf = -1;
        gRide.air = 0.0f;
        if (!gRide.body.grounded) {
            gCartRefY = gRide.body.y;
            const royale::cartdetail::Footing f = royale::cartdetail::FootingAt(ClientCartWorld(), gRide.body.x, gRide.body.z, gRide.body.yaw);
            if (f.any) gRide.air = std::max(0.0f, gRide.body.y - f.y);
        }
        gSession.SendDrive(index, gRide.body, gRide.air, gRide.handbrake, st.impact, st.landing);
        if (st.impact > royale::kCartCrashFrom) { CartCrashFx(gPlayState, *v, st.impact); gRide.shake = std::min(1.0f, st.impact / 500.0f); }
        if (st.landing > 300.0f) gRide.shake = std::max(gRide.shake, std::min(1.0f, st.landing / 1200.0f));
        // The game's own rumble for a crash or a hard landing.
        if (gRide.shake > 0.05f) { func_800AA000(0.0f, static_cast<u8>(120 + 120 * gRide.shake), 10, 150); gRide.shake = 0; }
    } else {
        gRide.driving = false;
    }
    PlaceOnSeat(player, gRide.driving ? gRide.body : v->body, seat);
}

void PoseRider(Player* player) {
    const double now = ImGui::GetTime();
    LinkAnimationHeader* anim = RA(uma_wait_1);
    float f = 0;
    if (!gRide.seated && gRide.settleOnGround) {   // climbing down
        anim = DismountAnim(gRide.dismountSeat);
        f = static_cast<float>(now - gRide.dismountAt) * royale::kTickHz;
    } else if (now - gRide.mountAt < AnimSeconds(MountAnim(gRide.seat))) {   // climbing on
        anim = MountAnim(gRide.seat);
        f = static_cast<float>(now - gRide.mountAt) * royale::kTickHz;
    } else {
        // sat down: the game's riding idles; trotting along when it goes, a bouncier one flat out
        const float speed = gRide.driving ? std::fabs(gRide.body.speed) : (CartViewOf(gRide.index) ? std::fabs(CartViewOf(gRide.index)->body.speed) : 0.0f);
        anim = speed > 300.0f ? RA(uma_anim_slowrun) : speed > 60.0f ? RA(uma_anim_walk) : RA(uma_wait_1);
        const float len = static_cast<float>(Animation_GetLastFrame(anim)) + 1.0f;
        f = std::fmod(static_cast<float>(now) * royale::kTickHz * (speed > 60.0f ? std::clamp(speed / 250.0f, 0.6f, 1.6f) : 1.0f), len);
    }
    const float last = static_cast<float>(Animation_GetLastFrame(anim));
    LoadLinkFrame(anim, static_cast<int>(std::clamp(f, 0.0f, last)), player->skelAnime.limbCount, player->skelAnime.jointTable);
}
void LocalRide_Draw(Actor* actor, PlayState* play) {
    Player* player = reinterpret_cast<Player*>(actor);
    if (player == GET_PLAYER(play) && (gRide.seated || gRide.settleOnGround)) PoseRider(player);
    Player_Draw(actor, play);
}

// ---- other players in carts (Puppet_Update calls this) ----------------------------------------------------------------------------
// Returns true while the puppet is sat in a cart (or climbing down from one): it is placed and posed here.
bool PuppetRide(PlayState* play, Player* player, PuppetMotion& m, uint16_t id) {
    Actor* actor = &player->actor;
    RiderPose rp;
    if (CartRiderPose(id, &rp)) {
        const uint8_t key = static_cast<uint8_t>(240 + static_cast<int>(rp.seat));
        const float speed = std::fabs(rp.speed);
        const uint8_t gait = speed > 300.0f ? 2 : speed > 60.0f ? 1 : 0;
        auto gaitAnim = [](uint8_t g) { return g == 2 ? RA(uma_anim_slowrun) : g == 1 ? RA(uma_anim_walk) : RA(uma_wait_1); };
        if (m.anim != key) {   // just got on: the climb into the saddle first
            StartSeq(play, player, m, AnimSeq(MountAnim(rp.seat), false), -2.0f);
            m.anim = key;
            m.rideStage = 0;
            PuppetSfx(actor, NA_SE_PL_SIT_ON_HORSE);
        }
        const bool finished = LinkAnimation_Update(play, &player->skelAnime);
        if (m.rideStage == 0 && finished) {   // sat down: the riding idle (or trot) from here
            StartSeq(play, player, m, AnimSeq(gaitAnim(gait), true), -3.0f);
            m.rideStage = 1;
            m.rideGait = gait;
        } else if (m.rideStage == 1 && gait != m.rideGait) {
            StartSeq(play, player, m, AnimSeq(gaitAnim(gait), true), -6.0f);
            m.rideGait = gait;
        }
        if (m.rideStage == 1 && gait > 0) player->skelAnime.playSpeed = std::clamp(speed / 250.0f, 0.6f, 1.6f);
        actor->world.pos = { rp.x, rp.y - kSaddleDrop, rp.z };
        actor->shape.rot.y = actor->world.rot.y = rp.yaw;
        actor->shape.rot.x = rp.pitch;
        actor->shape.rot.z = rp.roll;
        actor->focus.pos = actor->world.pos;
        actor->focus.pos.y += 50.0f;
        m.rideSeat = rp.seat;
        m.rideAt = actor->world.pos;
        m.rideYaw = rp.yaw;
        m.dismount = 0;
        return true;   // (the animation's own root movement is kept: the climb up onto the saddle is the point of it)
    }
    if (m.anim == 240 || m.anim == 241) {   // just got off: climb down from where the saddle was
        StartSeq(play, player, m, AnimSeq(DismountAnim(m.rideSeat), false), -2.0f);
        m.anim = 239;
        m.dismount = static_cast<int>(AnimSeconds(DismountAnim(m.rideSeat)) * royale::kTickHz);
        PuppetSfx(actor, NA_SE_PL_GET_OFF_HORSE);
    }
    if (m.dismount > 0) {
        m.dismount--;
        LinkAnimation_Update(play, &player->skelAnime);
        actor->world.pos = m.rideAt;
        actor->shape.rot.y = actor->world.rot.y = m.rideYaw;
        actor->shape.rot.x = actor->shape.rot.z = 0;
        if (m.dismount == 0) m.anim = 255;   // back to whatever they are doing
        return true;
    }
    actor->shape.rot.x = actor->shape.rot.z = 0;
    return false;
}

// ---- the HUD: getting in, the controls while driving, the cart's health ------------------------------------------------------------
std::string CartPromptText() {
    int index; royale::Seat seat;
    if (!NearbyCartSeat(&index, &seat)) return {};
    return seat == royale::Seat::Driver ? "Drive the cart" : "Ride in the cart";
}
void DrawCartHud(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale) {
    if (!gRide.seated || !InField()) return;
    CartView* v = CartViewOf(gRide.index);
    if (v == nullptr) return;
    const float w = 180.0f * scale, hgt = 10.0f * scale;
    const ImVec2 a((ds.x - w) * 0.5f, ds.y - 118.0f * scale);
    const char* title = gRide.seat == royale::Seat::Driver ? "Lon Lon Buggy" : "Riding along";
    const float ts = 18.0f * scale;
    ImVec2 sz = font->CalcTextSizeA(ts, FLT_MAX, 0.0f, title);
    dl->AddText(font, ts, ImVec2((ds.x - sz.x) * 0.5f + 1.5f, a.y - ts - 2.5f * scale), IM_COL32(0, 0, 0, 220), title);
    dl->AddText(font, ts, ImVec2((ds.x - sz.x) * 0.5f, a.y - ts - 4.0f * scale), IM_COL32(255, 226, 150, 255), title);
    dl->AddRectFilled(ImVec2(a.x - 2, a.y - 2), ImVec2(a.x + w + 2, a.y + hgt + 2), IM_COL32(0, 0, 0, 190), 3.0f);
    const float hp = std::clamp(v->hp, 0.0f, 1.0f);
    const ImU32 col = hp > 0.5f ? IM_COL32(150, 110, 60, 255) : hp > 0.25f ? IM_COL32(230, 150, 50, 255) : IM_COL32(230, 60, 40, 255);
    dl->AddRectFilled(a, ImVec2(a.x + w * hp, a.y + hgt), col, 2.0f);
    const char* help = gRide.seat == royale::Seat::Driver ? "A go   B brake/back   Stick steer   Z slide   R get out"
                                                          : (v->driver == royale::net::kNoPlayer16 ? "A take the reins   R get out   B shoot" : "R get out   B shoot");
    const float hs = 15.0f * scale;
    sz = font->CalcTextSizeA(hs, FLT_MAX, 0.0f, help);
    dl->AddText(font, hs, ImVec2((ds.x - sz.x) * 0.5f + 1.0f, a.y + hgt + 5.0f * scale + 1.0f), IM_COL32(0, 0, 0, 220), help);
    dl->AddText(font, hs, ImVec2((ds.x - sz.x) * 0.5f, a.y + hgt + 5.0f * scale), IM_COL32(235, 235, 235, 255), help);
}
// On the minimap: a little brown cart for each one you can see (white edged when somebody is in it, grey when it is a wreck).
void DrawCartsOnMap(ImDrawList* dl, float scale, const std::function<ImVec2(float, float)>& toMap, const std::function<bool(ImVec2)>& inside) {
    for (const auto& [i, v] : gCarts) {
        if (v.actor == nullptr) continue;
        const ImVec2 p = toMap(v.body.x, v.body.z);
        if (!inside(p)) continue;
        const float u = 3.6f * scale;
        const bool used = v.driver != royale::net::kNoPlayer16 || v.passenger != royale::net::kNoPlayer16;
        dl->AddRectFilled(ImVec2(p.x - u, p.y - u * 0.7f), ImVec2(p.x + u, p.y + u * 0.7f), v.wrecked ? IM_COL32(90, 90, 90, 255) : IM_COL32(170, 110, 50, 255), 1.0f);
        dl->AddRect(ImVec2(p.x - u, p.y - u * 0.7f), ImVec2(p.x + u, p.y + u * 0.7f), used ? IM_COL32(255, 255, 255, 240) : IM_COL32(40, 25, 10, 230), 1.0f, 0, 1.3f);
    }
}
// A cart as something to hit with a sword or an arrow.
bool CartTargetAt(uint16_t id, float* x, float* z) {
    if (!royale::IsVehicleId(id)) return false;
    const CartView* v = CartViewOf(static_cast<int>(id - royale::kVehicleIdBase));
    if (v == nullptr) return false;
    *x = v->body.x; *z = v->body.z;
    return true;
}
struct CartTarget { uint16_t id; float x, z; };
std::vector<CartTarget> CartTargets() {
    std::vector<CartTarget> out;
    for (const auto& [i, v] : gCarts) {
        if (v.actor == nullptr || v.wrecked || gRide.index == i) continue;   // not the one you are sat in
        out.push_back({ static_cast<uint16_t>(royale::kVehicleIdBase + i), v.body.x, v.body.z });
    }
    return out;
}

void ForgetCarts() {
    gCarts.clear();
    gCartOf.clear();
    gRide.seated = gRide.driving = gRide.settleOnGround = false;
    gRide.index = -1;
    gRide.player = nullptr;
}

