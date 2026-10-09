// ---- Lilo's model -------------------------------------------------------------------------------------------------------------------
// Lilo is a low poly model made in Blender (tools/lilo/, assets/lilo/lilo.blend) in the N64 style: about 650 triangles, a 64x32 fur texture and a
// 32x32 face in three versions (eyes open, half shut, shut), a skeleton of 25 bones and eleven animation clips: idle, walk, run, jump, sit, talk,
// groom, sleep, stretch, pounce and happy (shared/lilo_model.h). She is skinned on the CPU every frame (the way the game itself skins Epona), lit by
// a fixed sun baked into the vertex colours, and drawn with her own two textures.
constexpr float kLiloPetScale = 0.4f;   // the model stands about 50 units tall at 1.0 (sitting, ears up, about 52) and Link about 60: at 0.4 she is cat sized, about 20 units
constexpr float kLiloMapScale = 0.5f;

void DrawLiloModel(PlayState* play, float x, float y, float z, float yaw, float scale, const royale::lilo::Pose& pose, int eyes) {
    GfxLayer layer(play, GfxLayerId::Characters);   // its thousands of vertices and commands go in the layer pool, not the game's buffer
    namespace L = royale::lilo;
    constexpr float kSub = 8.0f;   // vertices go to the graphics chip in 1/8 units, so the small model keeps its shape
    Vtx* vtx = static_cast<Vtx*>(FrameAlloc(play, sizeof(Vtx) * L::kVertCount));
    if (vtx == nullptr) return;
    // The sun: high, a little in front and to one side, turned into the model's own axes (the inverse of Matrix_RotateY(yaw)).
    const float wx = 0.35f, wy = 0.82f, wz = 0.45f;
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    const float lx = wx * cy - wz * sy, ly = wy, lz = wx * sy + wz * cy;
    for (int i = 0; i < L::kVertCount; i++) {
        const L::Vert& v = L::kVerts[i];
        float p[3], n[3];
        L::SkinVertex(pose, v, p, n);
        const float lit = std::clamp(0.52f + 0.58f * std::max(0.0f, n[0] * lx + n[1] * ly + n[2] * lz), 0.0f, 1.0f);
        Vtx& o = vtx[i];
        for (int k = 0; k < 3; k++) o.v.ob[k] = static_cast<s16>(std::lround(std::clamp(p[k] * kSub, -32000.0f, 32000.0f)));
        o.v.flag = 0;
        o.v.tc[0] = v.s;
        o.v.tc[1] = v.t;
        o.v.cn[0] = static_cast<u8>(255.0f * lit);
        o.v.cn[1] = static_cast<u8>(250.0f * lit);
        o.v.cn[2] = static_cast<u8>(242.0f * lit);
        o.v.cn[3] = 255;
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);         // the light is already in the vertex colours
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIDECALA, G_CC_PASS2);     // her texture times the vertex colour
    Matrix_Translate(x, y, z, MTXMODE_NEW);
    Matrix_RotateY(yaw, MTXMODE_APPLY);
    Matrix_Scale(scale / kSub, scale / kSub, scale / kSub, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    const int face = std::clamp(eyes, 0, 2);
    int loaded = -1;
    for (int b = 0; b < L::kBatchCount; b++) {
        const L::Batch& bt = L::kBatches[b];
        const int tex = bt.texture == L::kFace ? 1 + face : 0;
        if (tex != loaded) {
            if (tex == 0)
                gDPLoadTextureBlock(POLY_OPA_DISP++, L::kFurTex, G_IM_FMT_RGBA, G_IM_SIZ_16b, L::kFurW, L::kFurH, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                                    G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            else
                gDPLoadTextureBlock(POLY_OPA_DISP++, L::kFaceTex[tex - 1], G_IM_FMT_RGBA, G_IM_SIZ_16b, L::kFaceW, L::kFaceH, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                                    G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            loaded = tex;
        }
        gSPVertex(POLY_OPA_DISP++, reinterpret_cast<uintptr_t>(&vtx[bt.firstVert]), bt.vertCount, 0);
        const int end = bt.firstTri + bt.triCount;
        int t = bt.firstTri;
        for (; t + 1 < end; t += 2)
            gSP2Triangles(POLY_OPA_DISP++, L::kTris[t][0], L::kTris[t][1], L::kTris[t][2], 0, L::kTris[t + 1][0], L::kTris[t + 1][1], L::kTris[t + 1][2], 0);
        if (t < end) gSP1Triangle(POLY_OPA_DISP++, L::kTris[t][0], L::kTris[t][1], L::kTris[t][2], 0);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// Her eyes: what the mood wants, with a quick blink every few seconds while they are open.
int LiloEyes(float seconds, int wanted) {
    if (wanted != royale::lilo::kEyesOpen) return wanted;
    return std::fmod(seconds, 3.7f) < 0.13f ? royale::lilo::kEyesShut : royale::lilo::kEyesOpen;
}

// -- Lilo's voice and her accidents (shared by the pet and the Lilo on the map) --
// Her mews are seven short recordings of the real Lilo (assets/lilo/sounds, shared/lilo_sounds.h), on a mixer voice of their own so a mew never
// cuts off a storm siren. Which one plays depends on what she is doing:
enum LiloMew { kMewChirp, kMewLong, kMewTrail, kMewShort, kMewAsk, kMewFall, kMewLoud };   // the clips in order (cat_01, 03, 04, 05, 06, 07, 10)
void PlayMeow(int clip, float gain = 0.7f) {
    namespace S = royale::lilo_snd;
    constexpr int kClipCount = sizeof(S::kClips) / sizeof(S::kClips[0]);
    static std::shared_ptr<const std::vector<int16_t>> cache[kClipCount];
    const float volume = GameVolume(false) * gain;
    if (clip < 0 || clip >= kClipCount || volume < 0.01f) return;
    if (!cache[clip]) cache[clip] = std::make_shared<const std::vector<int16_t>>(S::kClips[clip].data, S::kClips[clip].data + S::kClips[clip].count);
    StartVoice(kVoiceCat, cache[clip], false, S::kRate, false, volume);
}

// A little greenish cloud (a few brown puffs among it) that drifts up and thins out; called every tick while the cloud lasts.
void FartCloudStep(PlayState* play, float bx, float by, float bz, int puffs, float size = 1.0f) {
    for (int i = 0; i < puffs; i++) {
        Vec3f pos = { bx + (Rand_ZeroOne() - 0.5f) * 34.0f * size, by + (22.0f + Rand_ZeroOne() * 22.0f) * size, bz + (Rand_ZeroOne() - 0.5f) * 34.0f * size };
        Vec3f vel = { (Rand_ZeroOne() - 0.5f) * 0.9f * size, (0.5f + Rand_ZeroOne() * 0.7f) * size, (Rand_ZeroOne() - 0.5f) * 0.9f * size }, accel = { 0.0f, 0.02f * size, 0.0f };
        const bool brown = Rand_ZeroOne() < 0.35f;
        Color_RGBA8 prim = brown ? Color_RGBA8{ 150, 130, 60, 255 } : Color_RGBA8{ 150, 200, 70, 255 };
        Color_RGBA8 env = brown ? Color_RGBA8{ 90, 70, 30, 255 } : Color_RGBA8{ 90, 140, 40, 255 };
        EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 260 + static_cast<int>(Rand_ZeroOne() * 160.0f), 40);
    }
}

// Everybody close enough to smell it (bots and other players you can see, and you) gags for a moment: a little word floats up from their head and
// a puff of green hangs there. Purely a joke on your screen: nothing the server knows about.
struct FartReaction { uint16_t id; double start; float x, y, z; };
std::vector<FartReaction> gFartReactions;
constexpr uint16_t kFartSelf = 0xFFFF;
constexpr float kFartSmellRange = 420.0f;
void FartReact(float x, float z) {
    if (gPlayState == nullptr) return;
    const double now = ImGui::GetTime();
    auto add = [&](uint16_t id, float px, float py, float pz) {
        gFartReactions.push_back({ id, now, px, py, pz });
        FartCloudStep(gPlayState, px, py - 12.0f, pz, 1);
    };
    Player* me = GET_PLAYER(gPlayState);
    if (me != nullptr && std::hypot(me->actor.world.pos.x - x, me->actor.world.pos.z - z) < kFartSmellRange)
        add(kFartSelf, me->actor.world.pos.x, me->actor.world.pos.y, me->actor.world.pos.z);
    for (const auto& [id, st] : gState) {
        if (!st.alive || st.scene != gPlayState->sceneNum || royale::IsBossId(id)) continue;
        if (me != nullptr && std::hypot(st.x - me->actor.world.pos.x, st.z - me->actor.world.pos.z) < 4.0f) continue;   // that is you
        if (std::hypot(st.x - x, st.z - z) < kFartSmellRange) add(id, st.x, st.y, st.z);
    }
}

// ---- Lilo's toxic cloud ---------------------------------------------------------------------------------------------------------
// A big green cloud (shared/balance.h: kFartCloudRadius wide, kFartCloudSeconds long) that stays where she made it. The server owns the damage
// (Match::TickGas: stand in it kFartGraceSeconds and it hurts kFartDps hearts a second) and tells everybody where each cloud is (EvFartCloud); here
// every client draws it as a thick drifting bank of dust puffs with sparkles round its edge, and warns the player standing in it with the same
// numbers: a green tint and a bar that fills over the grace time, then a red "taking damage" line.
struct ToxicCloud { float x, y, z; double until; };
std::vector<ToxicCloud> gToxicClouds;
float gGasTime = 0.0f;      // my own count of the server's gasTime, for the warning only
double gToxicPuffAcc = 0.0;
double gBubbleUntil = 0.0;   // the pet's warning bubble, and where it floats (kept up to date by her)
float gBubbleAt[3] = {};

void ToxicPuff(PlayState* play, float x, float y, float z, bool burst) {
    const float ang = Rand_ZeroOne() * 6.2831853f, r = std::sqrt(Rand_ZeroOne()) * royale::kFartCloudRadius * 0.92f;
    Vec3f pos = { x + std::cos(ang) * r, y + 4.0f + Rand_ZeroOne() * 70.0f, z + std::sin(ang) * r };
    Vec3f vel = { (Rand_ZeroOne() - 0.5f) * 1.2f, 0.3f + Rand_ZeroOne() * 0.9f, (Rand_ZeroOne() - 0.5f) * 1.2f }, accel = { 0, 0.02f, 0 };
    const bool brown = Rand_ZeroOne() < 0.2f;
    Color_RGBA8 prim = brown ? Color_RGBA8{ 170, 150, 60, 255 } : Color_RGBA8{ 170, 235, 70, 255 };
    Color_RGBA8 env = brown ? Color_RGBA8{ 90, 70, 20, 255 } : Color_RGBA8{ 70, 130, 30, 255 };
    EffectSsDust_Spawn(play, 4, &pos, &vel, &accel, &prim, &env, burst ? 650 : 480, 7, burst ? 14 : 11, 0);   // flag 4: a little random colour variation
}

void AddToxicCloud(float x, float y, float z, bool mine) {
    if (gPlayState == nullptr) return;
    gToxicClouds.push_back({ x, y, z, ImGui::GetTime() + royale::kFartCloudSeconds });
    if (gToxicClouds.size() > 24) gToxicClouds.erase(gToxicClouds.begin());
    for (int i = 0; i < 70; i++) ToxicPuff(gPlayState, x, y, z, true);   // the big first whoosh
    (void)mine;
}

void UpdateToxicClouds(const royale::HudState& hud) {
    const double now = ImGui::GetTime();
    const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.1f);
    for (size_t i = 0; i < gToxicClouds.size();) {
        if (now >= gToxicClouds[i].until || gPlayState == nullptr || !InGame()) gToxicClouds.erase(gToxicClouds.begin() + static_cast<long>(i));
        else i++;
    }
    bool inside = false;
    if (gPlayState != nullptr && InGame() && !gToxicClouds.empty()) {
        Player* me = GET_PLAYER(gPlayState);
        gToxicPuffAcc += dt;
        const bool tick = gToxicPuffAcc >= 0.05;   // twenty times a second, like the game's own effects
        if (tick) gToxicPuffAcc = 0.0;
        for (const ToxicCloud& c : gToxicClouds) {
            const float left = static_cast<float>(c.until - now), thin = std::clamp(left / 2.0f, 0.15f, 1.0f);   // it thins out over its last two seconds
            if (tick) {
                for (int i = 0; i < static_cast<int>(10.0f * thin); i++) ToxicPuff(gPlayState, c.x, c.y, c.z, false);
                for (int i = 0; i < 2; i++) {   // sparkles round the edge, so the size of it is plain
                    const float a = Rand_ZeroOne() * 6.2831853f;
                    Vec3f pos = { c.x + std::cos(a) * royale::kFartCloudRadius, c.y + 6.0f + Rand_ZeroOne() * 30.0f, c.z + std::sin(a) * royale::kFartCloudRadius };
                    Vec3f vel = { 0, 0.8f, 0 }, accel = { 0, 0, 0 };
                    Color_RGBA8 prim = { 200, 255, 90, 255 }, env = { 90, 200, 40, 255 };
                    EffectSsKiraKira_SpawnDispersed(gPlayState, &pos, &vel, &accel, &prim, &env, 150, 18);
                }
            }
            if (me != nullptr && std::hypot(me->actor.world.pos.x - c.x, me->actor.world.pos.z - c.z) < royale::kFartCloudRadius && std::fabs(me->actor.world.pos.y - c.y) < 220.0f) inside = true;
        }
    }
    const bool live = IsLive(hud) && hud.selfAlive;   // outside a live match the cloud is only for show: nothing hurts
    if (inside && live) gGasTime += dt;
    else gGasTime = std::max(0.0f, gGasTime - dt * 2.0f);
    if (!inside || !live) { if (gGasTime <= 0.0f) gGasTime = 0.0f; }
}

void DrawFartReactions(ImDrawList* dl, ImFont* font, float scale) {
    static const char* const words[] = { "*cough*", "Ew!", "*gag*", "LILO!", "*sniff* ...ugh", "Who did that?!" };
    const double now = ImGui::GetTime();
    if (gGasTime > 0.05f) {   // standing in the cloud: a green tint, a bar filling over the grace time, then the damage line
        const ImVec2 ds = ImGui::GetIO().DisplaySize;
        const float fill = std::clamp(gGasTime / royale::kFartGraceSeconds, 0.0f, 1.0f);
        dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(110, 190, 40, static_cast<int>(40.0f + 60.0f * fill)));
        const bool hurting = gGasTime > royale::kFartGraceSeconds;
        const char* line = hurting ? "THE FART CLOUD IS HURTING YOU! GET OUT!" : "Toxic fart cloud! Get out of it!";
        const float size = 30.0f * scale;
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, line);
        const float cx = ds.x * 0.5f, y = ds.y * 0.22f;
        const ImU32 col = hurting ? IM_COL32(255, 90, 70, 255) : IM_COL32(200, 255, 100, 255);
        dl->AddText(font, size, ImVec2(cx - sz.x * 0.5f + 2, y + 2), IM_COL32(20, 30, 10, 230), line);
        dl->AddText(font, size, ImVec2(cx - sz.x * 0.5f, y), col, line);
        const float barW = 280.0f * scale, barH = 12.0f * scale;
        dl->AddRectFilled(ImVec2(cx - barW * 0.5f - 2, y + sz.y + 6 - 2), ImVec2(cx + barW * 0.5f + 2, y + sz.y + 6 + barH + 2), IM_COL32(20, 30, 10, 200));
        dl->AddRectFilled(ImVec2(cx - barW * 0.5f, y + sz.y + 6), ImVec2(cx - barW * 0.5f + barW * fill, y + sz.y + 6 + barH), hurting ? IM_COL32(255, 80, 60, 255) : IM_COL32(170, 235, 70, 255));
    }
    if (now < gBubbleUntil) {   // the pet warns you before she lets one go
        ImVec2 at;
        if (WorldToScreen(gBubbleAt[0], gBubbleAt[1] + 36.0f, gBubbleAt[2], &at)) {
            const char* line = "I smell a fart nearby!";
            const float size = 24.0f * scale;
            const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, line);
            dl->AddRectFilled(ImVec2(at.x - sz.x * 0.5f - 8, at.y - sz.y - 6), ImVec2(at.x + sz.x * 0.5f + 8, at.y + 2), IM_COL32(250, 250, 240, 235), 6.0f);
            dl->AddText(font, size, ImVec2(at.x - sz.x * 0.5f, at.y - sz.y - 2), IM_COL32(40, 60, 20, 255), line);
        }
    }
    for (size_t i = 0; i < gFartReactions.size();) {
        const FartReaction& r = gFartReactions[i];
        const float age = static_cast<float>(now - r.start);
        if (age > 2.4f || gPlayState == nullptr) { gFartReactions.erase(gFartReactions.begin() + static_cast<long>(i)); continue; }
        float x = r.x, y = r.y, z = r.z;
        if (r.id != kFartSelf) {   // follow the puppet while it moves
            auto it = gState.find(r.id);
            if (it == gState.end() || !it->second.alive) { gFartReactions.erase(gFartReactions.begin() + static_cast<long>(i)); continue; }
            x = it->second.x; y = it->second.y; z = it->second.z;
        } else if (Player* me = GET_PLAYER(gPlayState)) { x = me->actor.world.pos.x; y = me->actor.world.pos.y; z = me->actor.world.pos.z; }
        ImVec2 at;
        if (WorldToScreen(x, y + 85.0f + age * 14.0f, z, &at)) {
            const float fade = std::clamp((2.4f - age) / 0.6f, 0.0f, 1.0f);
            const char* label = words[(r.id == kFartSelf ? 3u : r.id) % (sizeof(words) / sizeof(words[0]))];
            const float size = 21.0f * scale;
            const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label);
            dl->AddText(font, size, ImVec2(at.x - sz.x * 0.5f + 2, at.y + 2), IM_COL32(20, 40, 10, static_cast<int>(220 * fade)), label);
            dl->AddText(font, size, ImVec2(at.x - sz.x * 0.5f, at.y), IM_COL32(190, 235, 110, static_cast<int>(255 * fade)), label);
        }
        i++;
    }
}

// -- Lilo as a pet: follows you (never a bot), purely for looks: no collision, no targeting, nothing the server knows about, and nobody else sees her.
// Her moods pick her animation clip; the clips cross-fade, so walking, sitting, grooming and curling up to sleep blend instead of snapping. Stand
// still facing her and press A to talk: she sits and mews her line in the game's own text box.
// Bored pets (Lilo and Avriella) stroll off a little way from where you are standing: while you stand still, every 10 to 20 seconds they pick a new
// spot 130 to 260 units from their usual place by you (or, one time in three, go back to it). The moment you move they come back to you.
struct PetWander { float ox = 0, oz = 0, next = 12.0f; };
void PetWanderStep(PetWander& w, float dt, bool playerStill) {
    if (!playerStill) { w.ox = w.oz = 0; w.next = 10.0f + Rand_ZeroOne() * 8.0f; return; }
    w.next -= dt;
    if (w.next > 0.0f) return;
    w.next = 10.0f + Rand_ZeroOne() * 10.0f;
    if (Rand_ZeroOne() < 0.33f) { w.ox = w.oz = 0; return; }
    const float a = Rand_ZeroOne() * 6.2831853f, r = 130.0f + Rand_ZeroOne() * 130.0f;
    w.ox = std::sin(a) * r; w.oz = std::cos(a) * r;
}

enum class CatMood { Follow, Stand, Sit, Groom, Loaf, Stretch, Pounce, Happy, Jump, Talk };
struct CatBrain {
    Actor* actor = nullptr;
    CatMood mood = CatMood::Follow;
    float moodT = 0, idle = 0, sitT = 0;
    float x = 0, z = 0, y = 0, yaw = 0, speed = 0;
    float nextAt = 6.0f;
    float leapX = 0, leapZ = 0;
    size_t pickups = 0;
    bool placed = false;
    int line = 0;    // what she says next (royale::kLiloPetLines)
    CatMood seen = CatMood::Follow;            // the mood last tick, to mew when it changes
    float meowIn = 25.0f, fartIn = 70.0f;      // seconds until a mew of her own, and until her next accident
    float afterFartMew = -1.0f;                // the mew that follows an accident
    bool warned = false;                       // has she said she smells one, for the accident that is coming
    int eyes = 0;
    PetWander wander;
    royale::lilo::Animator anim;
};
CatBrain gCat;

void SetMood(CatBrain& c, CatMood m) { c.mood = m; c.moodT = 0; }

void CatPoof(PlayState* play, float x, float y, float z) {
    for (int i = 0; i < 7; i++) {
        Vec3f pos = { x + (Rand_ZeroOne() - 0.5f) * 18.0f, y + 5.0f + Rand_ZeroOne() * 12.0f, z + (Rand_ZeroOne() - 0.5f) * 18.0f };
        Vec3f vel = { (Rand_ZeroOne() - 0.5f) * 1.6f, 0.6f + Rand_ZeroOne(), (Rand_ZeroOne() - 0.5f) * 1.6f }, accel = { 0, 0, 0 };
        Color_RGBA8 prim = { 255, 245, 210, 255 }, env = { 200, 190, 255, 255 };
        EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 120, 26);
    }
}

bool LobbyPetsActive() {
    return MapOption("PetsEnabled",true) && CVarGetInteger(CVAR_SETTING("Royale.LobbyAllPets"),0)!=0 && gSession.Joined() &&
        gSession.Hud().state==royale::MatchState::Lobby && InPlayableScene(royale::MatchState::Lobby) && !gSkydiving && !gSpectating;
}
bool SocialPet_Update(Actor* actor, PlayState* play, int pet);
royale::lobby::Group gLobbyPetGroup;
void Cat_Update(Actor* actor, PlayState* play) {
    if (SocialPet_Update(actor,play,0)) return;
    namespace L = royale::lilo;
    CatBrain& c = gCat;
    const float dt = 1.0f / royale::kTickHz;
    Player* pl = GET_PLAYER(play);
    const float px = pl->actor.world.pos.x, pz = pl->actor.world.pos.z, py = pl->actor.world.pos.y;
    const float pyaw = pl->actor.shape.rot.y * (3.14159265f / 32768.0f);
    const float pspeed = std::fabs(pl->linearVelocity);
    const bool playerStill = pspeed < 0.6f;
    // Her place beside you: behind and to the left.
    PetWanderStep(c.wander, dt, playerStill);
    const float wantX = c.wander.ox + px + std::sin(pyaw + 3.14159265f + 0.7f) * 75.0f, wantZ = c.wander.oz + pz + std::cos(pyaw + 3.14159265f + 0.7f) * 75.0f;
    float dx = wantX - c.x, dz = wantZ - c.z, d = std::hypot(dx, dz);
    if (!c.placed || d > 1400.0f || std::fabs(py - c.y) > 170.0f) {   // arrived, or left far behind: pop up beside you
        if (c.placed) CatPoof(play, c.x, c.y, c.z);
        c.x = wantX; c.z = wantZ; c.y = py; c.placed = true;
        CatPoof(play, c.x, c.y, c.z);
        dx = dz = 0; d = 0;
        SetMood(c, CatMood::Stand);
    }
    c.moodT += dt;
    if (playerStill) c.idle += dt; else c.idle = 0;
    const bool talking = TalkingTo(actor);
    if (talking && c.mood != CatMood::Talk) SetMood(c, CatMood::Talk);
    if (!talking && c.mood == CatMood::Talk) {   // done talking: she stays sitting a while, and has something new to say next time
        SetMood(c, CatMood::Sit);
        c.sitT = 0; c.nextAt = 4.0f;
        if (c.line == royale::kLiloPetLineCount - 1) { c.fartIn = 0.9f; c.warned = true; }   // she said she smelled one, and now she has made one
        c.line = (c.line + 1) % royale::kLiloPetLineCount;
    }
    if (c.mood != CatMood::Talk) {
        if (gPickupLog.size() != c.pickups) { if (gPickupLog.size() > c.pickups && c.mood != CatMood::Happy) SetMood(c, CatMood::Happy); c.pickups = gPickupLog.size(); }
        if (gEmote.id >= 0 && c.mood != CatMood::Happy) SetMood(c, CatMood::Happy);
        // you jump, she jumps
        if ((c.mood == CatMood::Follow || c.mood == CatMood::Stand) && !(pl->actor.bgCheckFlags & 1) && pl->actor.velocity.y > 4.0f && d < 400.0f)
            SetMood(c, CatMood::Jump);
    }

    // Her voice: a mew for what she has just started doing, one now and then of her own, and her accidents.
    if (c.mood != c.seen) {
        static const int kLineMew[royale::kLiloPetLineCount] = { kMewShort, kMewAsk, kMewLong, kMewLoud, kMewTrail, kMewFall };
        switch (c.mood) {
            case CatMood::Talk: PlayMeow(kLineMew[c.line % royale::kLiloPetLineCount]); break;
            case CatMood::Happy: PlayMeow(kMewChirp); break;
            case CatMood::Jump: PlayMeow(kMewChirp, 0.45f); break;
            case CatMood::Pounce: PlayMeow(kMewLoud, 0.5f); break;
            case CatMood::Stretch: PlayMeow(kMewFall, 0.5f); break;
            default: break;
        }
        c.seen = c.mood;
    }
    if (c.mood != CatMood::Talk) {
        c.meowIn -= dt;
        if (c.meowIn <= 0.0f) {
            static const int kIdleMew[3] = { kMewChirp, kMewShort, kMewAsk };
            if (c.mood == CatMood::Follow || c.mood == CatMood::Stand || c.mood == CatMood::Sit) PlayMeow(kIdleMew[static_cast<int>(Rand_ZeroOne() * 2.99f)], 0.45f);
            c.meowIn = 25.0f + Rand_ZeroOne() * 30.0f;
        }
        c.fartIn -= dt;
        if (c.fartIn <= 3.5f && !c.warned && c.fartIn > 0.9f) {   // a few seconds before: "I smell a fart nearby!" over her head
            c.warned = true;
            gBubbleUntil = ImGui::GetTime() + 3.2;
            PlayMeow(kMewAsk, 0.6f);
        }
        if (c.fartIn <= 0.0f && c.mood != CatMood::Pounce) {   // she farts a lot: now and then, wherever she is
            PlayOneShot(2);
            c.warned = false;
            const float bx = c.x - std::sin(c.yaw) * 30.0f, bz = c.z - std::cos(c.yaw) * 30.0f;
            AddToxicCloud(bx, c.y, bz, true);
            gSession.ReportFartCloud(bx, bz);
            FartReact(c.x, c.z);
            c.afterFartMew = 1.0f;
            c.fartIn = 55.0f + Rand_ZeroOne() * 75.0f;
            if (c.mood == CatMood::Sit || c.mood == CatMood::Stand || c.mood == CatMood::Loaf) SetMood(c, CatMood::Happy);   // so pleased with herself
        }
    }
    if (c.afterFartMew >= 0.0f) {
        c.afterFartMew -= dt;
        if (c.afterFartMew < 0.0f) PlayMeow(kMewFall, 0.5f);
    }
    if (ImGui::GetTime() < gBubbleUntil) { gBubbleAt[0] = c.x; gBubbleAt[1] = c.y; gBubbleAt[2] = c.z; }

    const float tm = static_cast<float>(play->gameplayFrames) * dt;
    bool moving = false;
    float heading = c.yaw;
    int clip = L::kIdle, eyes = L::kEyesOpen;
    float rate = 1.0f;
    auto follow = [&](float minSpeed) {   // go to her place: a walk when it is near, a run when you have gone on ahead
        c.speed += (std::clamp((d - 30.0f) * 2.8f, minSpeed, 290.0f) - c.speed) * 0.2f;
        heading = std::atan2(dx, dz);
        c.x += std::sin(heading) * c.speed * dt;
        c.z += std::cos(heading) * c.speed * dt;
        moving = true;
    };

    switch (c.mood) {
        case CatMood::Follow: case CatMood::Stand: {
            if (d > 46.0f) {
                follow(45.0f);
                c.mood = CatMood::Follow;
            } else {
                c.speed *= 0.7f;
                if (c.mood == CatMood::Follow) SetMood(c, CatMood::Stand);
                if (c.idle > 1.6f) { SetMood(c, CatMood::Sit); c.sitT = 0; c.nextAt = 5.0f + Rand_ZeroOne() * 4.0f; }
            }
            if (moving && c.speed > 150.0f) { clip = L::kRun; rate = std::clamp(c.speed / 230.0f, 0.8f, 1.4f); }
            else if (moving) { clip = L::kWalk; rate = std::clamp(c.speed / 75.0f, 0.6f, 1.8f); }
            break;
        }
        case CatMood::Sit: case CatMood::Groom: {
            c.sitT += dt;
            clip = c.mood == CatMood::Groom ? L::kGroom : L::kSit;
            if (c.mood == CatMood::Groom) {   // a front paw to the mouth and a lick
                eyes = L::kEyesHalf;
                if (c.moodT > 3.0f) SetMood(c, CatMood::Sit);
            } else if (c.moodT > c.nextAt) {
                const float r = Rand_ZeroOne();
                c.nextAt = 5.0f + Rand_ZeroOne() * 4.0f;
                SetMood(c, r < 0.45f ? CatMood::Groom : r < 0.7f ? CatMood::Stretch : CatMood::Pounce);
            }
            if (c.sitT > 16.0f && c.mood == CatMood::Sit) SetMood(c, CatMood::Loaf);
            if (!playerStill || d > 150.0f) SetMood(c, CatMood::Follow);
            break;
        }
        case CatMood::Loaf: {   // curled up asleep: paws tucked under, eyes shut, little 'z's
            clip = L::kSleep;
            eyes = L::kEyesShut;
            if (static_cast<int>(c.moodT * 10.0f) % 18 == 0 && play->gameplayFrames % 3 == 0) {
                Vec3f pos = { c.x + std::sin(c.yaw) * 10.0f, c.y + 20.0f, c.z + std::cos(c.yaw) * 10.0f }, vel = { 0.15f, 0.5f, 0 }, accel = { 0, 0, 0 };
                Color_RGBA8 prim = { 190, 210, 255, 255 }, env = { 90, 120, 255, 255 };
                EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 60, 40);
            }
            if (!playerStill || d > 150.0f) SetMood(c, CatMood::Stretch);
            break;
        }
        case CatMood::Stretch: {   // front legs forward, chest down, rump up, and a yawn
            clip = L::kStretch;
            eyes = L::kEyesHalf;
            if (c.moodT > 2.0f) SetMood(c, playerStill ? CatMood::Sit : CatMood::Follow);
            break;
        }
        case CatMood::Pounce: {   // crouch and wiggle, then a leap at nothing
            clip = L::kPounce;
            if (c.moodT < 0.9f) {
                c.leapX = std::sin(c.yaw) * 45.0f; c.leapZ = std::cos(c.yaw) * 45.0f;
            } else if (c.moodT < 1.35f) {
                c.x += c.leapX * dt / 0.45f; c.z += c.leapZ * dt / 0.45f;
            } else if (c.moodT > 1.5f) {
                SetMood(c, CatMood::Stand);
            }
            break;
        }
        case CatMood::Happy: {   // hops with her tail straight up
            clip = L::kHappy;
            eyes = L::kEyesShut;
            if (c.moodT > 1.5f) SetMood(c, CatMood::Stand);
            break;
        }
        case CatMood::Jump: {   // up after you, still heading for her place
            clip = L::kJump;
            if (d > 20.0f) follow(std::min(c.speed, 290.0f));
            if (c.moodT > L::ClipSeconds(L::kJump)) SetMood(c, CatMood::Follow);
            break;
        }
        case CatMood::Talk: {   // sitting, facing you, mewing
            clip = L::kTalk;
            c.speed = 0;
            heading = std::atan2(px - c.x, pz - c.z);
            break;
        }
    }
    // Talk: stand still facing her and press A (the game's own talk, so the A button says "Speak"). Not while you are on the move.
    if (!talking && c.mood != CatMood::Jump && c.mood != CatMood::Pounce && pspeed < 3.0f &&
        OfferTalk(actor, play, static_cast<u16>(kTextLiloPet + c.line), 120.0f))
        SetMood(c, CatMood::Talk);
    // turn to face the way she goes (or, when still, towards you if you are close)
    if (!moving && c.mood != CatMood::Pounce && c.mood != CatMood::Talk && d < 400.0f) heading = std::atan2(px - c.x, pz - c.z);
    {
        float diff = heading - c.yaw;
        while (diff > 3.14159265f) diff -= 6.2831853f;
        while (diff < -3.14159265f) diff += 6.2831853f;
        c.yaw += diff * (moving ? 0.25f : c.mood == CatMood::Talk ? 0.3f : 0.08f);
    }
    c.anim.Play(clip, 0.25f);
    c.anim.Update(dt, rate);
    c.eyes = LiloEyes(tm, eyes);

    c.y = GroundY(play, c.x, c.z, c.y);   // her clips carry their own hops and leaps
    actor->world.pos.x = c.x; actor->world.pos.z = c.z; actor->world.pos.y = c.y;
    actor->shape.rot.y = static_cast<s16>(c.yaw * (32768.0f / 3.14159265f));
    actor->focus.pos = actor->world.pos;
    actor->focus.pos.y += 16.0f;
}

void Cat_Draw(Actor* actor, PlayState* play) {
    royale::lilo::Pose pose;
    gCat.anim.Evaluate(pose);
    DrawLiloModel(play, actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, gCat.yaw, kLiloPetScale, pose, gCat.eyes);
}
void Cat_Destroy(Actor* actor, PlayState*) { if (gCat.actor == actor) { gCat.actor = nullptr; gCat.placed = false; } }

// Which pet follows you when the pet option is on: 0 Lilo the cat, 1 Avriella the baby (picked in the "Your pet" section of the menu).
int PetKind() { return std::clamp(CVarGetInteger(CVAR_SETTING("Royale.PetKind"), 0), 0, 2); }

void ReconcileCatPet(const royale::HudState& hud) {
    const bool inMatch = IsLive(hud);
    const bool want = DebugOn(kDbgAllies) && royale::wartable::PetEnabled(MapOption("PetsEnabled",true),LobbyPetsActive(),MapOption("LiloPet",false),PetKind(),0) && gSession.Joined() && InPlayableScene(hud.state) && gPlayState != nullptr && !gSkydiving && !gSpectating &&
                      !(inMatch && hud.haveSelf && !hud.selfAlive);
    if (!want) {
        if (gCat.actor != nullptr) { Actor_Kill(gCat.actor); gCat.actor = nullptr; gCat.placed = false; }
        return;
    }
    if (gCat.actor != nullptr) return;
    Player* pl = GET_PLAYER(gPlayState);
    Actor* a = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ISHI, pl->actor.world.pos.x, pl->actor.world.pos.y, pl->actor.world.pos.z, 0, 0, 0, 0, false);
    if (a == nullptr) return;
    a->update = Cat_Update;
    a->draw = Cat_Draw;
    a->destroy = Cat_Destroy;
    a->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
    a->uncullZoneForward = 4000.0f; a->uncullZoneScale = 1500.0f; a->uncullZoneDownward = 1500.0f;
    a->shape.shadowScale = 9.0f;
    gCat.actor = a;
    gCat.placed = false;
    gCat.pickups = gPickupLog.size();
    gCat.mood = CatMood::Follow;
    gCat.anim = royale::lilo::Animator{};
}

