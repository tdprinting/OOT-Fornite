// ---- Lilo ---------------------------------------------------------------------------------------------------------------------------
// An Easter egg (switch it off with "Lilo the cat" under Minimap and game options): Lilo sits at a random spot on the map. Talk to her with A: she
// says her line in the game's text box, and when you close it there is a noise and a greenish cloud.
Actor* gLiloActor = nullptr;
royale::Vec2 gLiloPos = {};
bool gLiloKnown = false;
double gLiloTalkStart = -100.0;
bool gLiloFarted = true, gLiloHeard = false;
double gLiloMewAt = -1.0;
royale::lilo::Animator gLiloAnim;
int gLiloEyes = 0;

void TalkToLilo();
void Lilo_Update(Actor* actor, PlayState* play) {
    namespace L = royale::lilo;
    Player* pl = GET_PLAYER(play);
    const bool talking = TalkingTo(actor);
    const float dx = pl->actor.world.pos.x - actor->world.pos.x, dz = pl->actor.world.pos.z - actor->world.pos.z;
    if (dx * dx + dz * dz < 600.0f * 600.0f) {   // she watches you come
        const s16 want = static_cast<s16>(std::atan2(dx, dz) * (32768.0f / 3.14159265f));
        actor->shape.rot.y = static_cast<s16>(actor->shape.rot.y + static_cast<s16>(want - actor->shape.rot.y) * (talking ? 0.25f : 0.1f));
    }
    actor->focus.pos = actor->world.pos;
    actor->focus.pos.y += 20.0f;
    if (OfferTalk(actor, play, kTextLilo, royale::kHireRange)) TalkToLilo();
    gLiloAnim.Play(talking ? L::kTalk : L::kSit, 0.3f);
    gLiloAnim.Update(1.0f / royale::kTickHz);
    gLiloEyes = LiloEyes(static_cast<float>(play->gameplayFrames) / royale::kTickHz, L::kEyesOpen);
}
void Lilo_Draw(Actor* actor, PlayState* play) {
    royale::lilo::Pose pose;
    gLiloAnim.Evaluate(pose);
    DrawLiloModel(play, actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, actor->shape.rot.y * (3.14159265f / 32768.0f), kLiloMapScale, pose, gLiloEyes);
}
void Lilo_Destroy(Actor* actor, PlayState*) { if (gLiloActor == actor) gLiloActor = nullptr; }

bool FindLiloSpot(const royale::Circle& map, royale::Vec2* out) {
    if (!gSession.Client()) return false;
    const auto& props = gSession.Client()->Props();
    royale::Rng rng(gSession.Client()->Seed() ^ 0x4C696C6Full);   // "Lilo"
    auto clear = [&](royale::Vec2 p) {
        if (!WalkableAt(p)) return false;
        if (gSignKnown && royale::Distance(p, gSignPos) < 500.0f) return false;
        if (gMayaKnown && royale::Distance(p, gMayaPos) < 500.0f) return false;
        for (const royale::Prop& pr : props) {
            const float r = royale::PropRadius(pr.kind);
            if (r > 0.0f && royale::Distance(pr.pos, p) < r + 100.0f) return false;
        }
        return true;
    };
    for (int attempt = 0; attempt < 400; attempt++) {
        const float a = static_cast<float>(rng.Unit() * 6.2831853), d = map.radius * (0.15f + 0.7f * static_cast<float>(rng.Unit()));
        const royale::Vec2 p = { map.center.x + std::cos(a) * d, map.center.z + std::sin(a) * d };
        if (clear(p)) { *out = p; return true; }
    }
    return false;
}

void ReconcileLilo(const royale::HudState& hud) {
    const bool want = MapOption("LiloCat", true) && gSession.Joined() && InField() && hud.map.radius > 0 &&
                      (hud.state == royale::MatchState::Countdown || hud.state == royale::MatchState::Drop || hud.state == royale::MatchState::InMatch);
    if (!want) {
        if (gLiloActor != nullptr) { Actor_Kill(gLiloActor); gLiloActor = nullptr; }
        gLiloKnown = false;
        return;
    }
    if (gLiloActor != nullptr) return;
    if (!gLiloKnown) {
        if (gMapId == royale::kSandboxMapIndex) gLiloPos = { royale::sandbox::kSpawnX + 260.0f, royale::sandbox::kSpawnZ + 120.0f };   // the test map: beside the spawn pad
        else if (!FindLiloSpot(hud.map, &gLiloPos)) return;
        gLiloKnown = true;
    }
    float y = 0;
    if (!FloorAt(gLiloPos.x, gLiloPos.z, &y)) return;
    Actor* a = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ISHI, gLiloPos.x, y, gLiloPos.z, 0, 0x6000, 0, 0, false);
    if (a == nullptr) return;
    a->update = Lilo_Update;
    a->draw = Lilo_Draw;
    a->destroy = Lilo_Destroy;
    a->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
    a->uncullZoneForward = 4000.0f; a->uncullZoneScale = 1500.0f; a->uncullZoneDownward = 1500.0f;
    a->shape.shadowScale = 11.0f;
    gLiloActor = a;
    gLiloAnim = royale::lilo::Animator{};
    gLiloAnim.Play(royale::lilo::kSit, 0.0f);
}

bool LiloNear() {
    if (gLiloActor == nullptr || !InField()) return false;
    Player* pl = GET_PLAYER(gPlayState);
    return std::hypot(pl->actor.world.pos.x - gLiloPos.x, pl->actor.world.pos.z - gLiloPos.z) < royale::kHireRange;
}

// The talk itself is the game's text box (Lilo_Update); this arms the accident for when it closes.
void TalkToLilo() {
    PlayMeow(kMewLong);
    gLiloTalkStart = ImGui::GetTime();
    gLiloFarted = false;
    gLiloHeard = false;
}

// The accident: once her text box has closed, the noise, and a cloud of greenish-brown puffs behind the cat that drifts up and thins out.
void UpdateLiloFx() {
    if (gLiloActor == nullptr || gPlayState == nullptr || !InField()) return;
    const double now = ImGui::GetTime();
    if (!gLiloFarted) {
        if (TalkingTo(gLiloActor)) gLiloHeard = true;
        else if (gLiloHeard || now - gLiloTalkStart > 4.0) {
            gLiloFarted = true;
            PlayOneShot(2);
            const float yaw = gLiloActor->shape.rot.y * (3.14159265f / 32768.0f);
            const float bx = gLiloPos.x - std::sin(yaw) * 30.0f, bz = gLiloPos.z - std::cos(yaw) * 30.0f;
            AddToxicCloud(bx, gLiloActor->world.pos.y, bz, true);
            gSession.ReportFartCloud(bx, bz);
            FartReact(gLiloPos.x, gLiloPos.z);
            gLiloMewAt = now + 1.0;
        }
    }
    if (gLiloMewAt > 0.0 && now >= gLiloMewAt) { gLiloMewAt = -1.0; PlayMeow(kMewFall, 0.6f); }   // a satisfied mew once the air has cleared a little
}

// Her name over her head (what she says is in the game's own text box).
void DrawLilo(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale) {
    (void)ds;
    if (gLiloActor == nullptr || !InField()) return;
    Player* pl = GET_PLAYER(gPlayState);
    const float d = std::hypot(pl->actor.world.pos.x - gLiloPos.x, pl->actor.world.pos.z - gLiloPos.z);
    ImVec2 at;
    if (d < 1800.0f && WorldToScreen(gLiloPos.x, gLiloActor->world.pos.y + 42.0f, gLiloPos.z, &at)) {
        const float size = std::clamp(24.0f * scale * (1800.0f / (d + 900.0f)), 13.0f * scale, 28.0f * scale);
        const char* label = royale::kLiloName;
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label);
        dl->AddText(font, size, ImVec2(at.x - sz.x * 0.5f + 2, at.y + 2), IM_COL32(20, 20, 20, 230), label);
        dl->AddText(font, size, ImVec2(at.x - sz.x * 0.5f, at.y), IM_COL32(235, 235, 230, 255), label);
    }
}

// ---- hireable allies -----------------------------------------------------------------------------------------------------------
// Four people wait around the map (a Kokiri, a Zora, a Goron and a Gerudo); pay one with rupees and they follow you and fight for you. The
// server runs them (BotController::StepAllies); each is drawn by a stand-in actor with our own model, smoothed between snapshots.
// The models are original stylised ones (Ocarina of Time x Majora's Mask x Fortnite) with their own rig and clips (tools/allies, shared/ally_anim.h): they
// walk, wave you over, sit down when nobody is about (a Goron curls into his rock ball), get up when you come near, cheer when hired, flinch
// when hit, dance along when their owner does an emote or wins, and fight their own way: the Kokiri's slingshot, the Zora's thrown magic, the
// Goron's left and right punches, the Gerudo's bow. A Goron who falls behind curls up and rolls after his owner.

// Draws an ally's model in a pose: skinned on the CPU, lit by the sun in the vertex colours, the face picked from its three textures.
void DrawAllyModel(PlayState* play, float x, float y, float z, float yaw, const royale::allymodel::Model& m, const royale::allymodel::Pose& pose,
                   int face, float roll, float rollPivot) {
    GfxLayer layer(play, GfxLayerId::Characters);
    namespace A = royale::allymodel;
    constexpr float kSub = 8.0f;   // vertices go to the graphics chip in 1/8 units, so the small model keeps its shape
    Vtx* vtx = static_cast<Vtx*>(FrameAlloc(play, sizeof(Vtx) * m.vertCount));
    if (vtx == nullptr) return;
    const float wx = 0.35f, wy = 0.82f, wz = 0.45f;   // the sun, as for the Bokoblin and Lilo
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    const float lx = wx * cy - wz * sy, ly = wy, lz = wx * sy + wz * cy;
    for (int i = 0; i < m.vertCount; i++) {
        const A::Vert& v = m.verts[i];
        float p[3], n[3];
        A::SkinVertex(pose, v, p, n);
        const float lit = std::clamp(0.55f + 0.55f * std::max(0.0f, n[0] * lx + n[1] * ly + n[2] * lz), 0.0f, 1.0f);
        Vtx& o = vtx[i];
        for (int k = 0; k < 3; k++) o.v.ob[k] = static_cast<s16>(std::lround(std::clamp(p[k] * kSub, -32000.0f, 32000.0f)));
        o.v.flag = 0;
        o.v.tc[0] = v.s;
        o.v.tc[1] = v.t;
        o.v.cn[0] = static_cast<u8>(255.0f * lit);
        o.v.cn[1] = static_cast<u8>(250.0f * lit);
        o.v.cn[2] = static_cast<u8>(245.0f * lit);
        o.v.cn[3] = 255;
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);         // the light is already in the vertex colours; fins are seen from both sides
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIDECALA, G_CC_PASS2);     // the texture times the vertex colour
    Matrix_Translate(x, y, z, MTXMODE_NEW);
    Matrix_RotateY(yaw, MTXMODE_APPLY);
    if (roll != 0.0f) {   // a rolling Goron turns about the middle of his ball
        Matrix_Translate(0.0f, rollPivot, 0.0f, MTXMODE_APPLY);
        Matrix_RotateX(roll, MTXMODE_APPLY);
        Matrix_Translate(0.0f, -rollPivot, 0.0f, MTXMODE_APPLY);
    }
    Matrix_Scale(1.0f / kSub, 1.0f / kSub, 1.0f / kSub, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    // Two textures: the 256 x 256 body atlas, then the face in the expression asked for (the batches come body first). LOADBLOCK stops at
    // 2048 texels; LOADTILE takes the whole texture in Shipwright, as the 128-wide Hyrule Kingdom textures do.
    int loaded = -1;
    for (int b = 0; b < m.batchCount; b++) {
        const A::Batch& bt = m.batches[b];
        if (bt.tex != loaded) {
            loaded = bt.tex;
            if (bt.tex == A::kTexBody)
                gDPLoadTextureTile(POLY_OPA_DISP++, m.body, G_IM_FMT_RGBA, G_IM_SIZ_16b, A::kBodyW, A::kBodyH, 0, 0, A::kBodyW - 1, A::kBodyH - 1, 0,
                                   G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            else
                gDPLoadTextureTile(POLY_OPA_DISP++, m.faces[std::clamp(face, 0, static_cast<int>(A::kFaceCount) - 1)], G_IM_FMT_RGBA, G_IM_SIZ_16b,
                                   A::kFaceW, A::kFaceH, 0, 0, A::kFaceW - 1, A::kFaceH - 1, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP,
                                   G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        }
        gSPVertex(POLY_OPA_DISP++, reinterpret_cast<uintptr_t>(&vtx[bt.firstVert]), bt.vertCount, 0);
        const int end = bt.firstTri + bt.triCount;
        int t = bt.firstTri;
        for (; t + 1 < end; t += 2)
            gSP2Triangles(POLY_OPA_DISP++, m.tris[t][0], m.tris[t][1], m.tris[t][2], 0, m.tris[t + 1][0], m.tris[t + 1][1], m.tris[t + 1][2], 0);
        if (t < end) gSP1Triangle(POLY_OPA_DISP++, m.tris[t][0], m.tris[t][1], m.tris[t][2], 0);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// The middle of the curled-up Goron's ball, the point he rolls about (worked out once from his resting pose).
float GoronBallPivot() {
    static float pivot = -1.0f;
    if (pivot < 0.0f) {
        namespace A = royale::allymodel;
        A::Pose p;
        A::SampleClip(A::goron::kModel, A::kRest, 0.0f, p);
        float mn[3], mx[3];
        A::PoseBounds(A::goron::kModel, p, mn, mx);
        pivot = std::max(10.0f, (mn[1] + mx[1]) * 0.5f);
    }
    return pivot;
}

constexpr float kAllyShadow[royale::kAllyCount] = { 13.0f, 17.0f, 30.0f, 17.0f };

void AllyDust(PlayState* play, float x, float y, float z, int count, float spread, s16 scale) {
    for (int i = 0; i < count; i++) {
        Vec3f pos = { x + Rand_CenteredFloat(spread), y + 4.0f, z + Rand_CenteredFloat(spread) };
        Vec3f vel = { Rand_CenteredFloat(2.0f), 0.6f + Rand_ZeroOne(), Rand_CenteredFloat(2.0f) }, accel = { 0.0f, 0.1f, 0.0f };
        Color_RGBA8 prim = { 200, 180, 150, 255 }, env = { 130, 110, 90, 255 };
        EffectSsDust_Spawn(play, 4, &pos, &vel, &accel, &prim, &env, scale, 8, 14, 0);
    }
}

// Whether the person an ally works for is dancing (doing an emote) or has just won the match: the ally dances along.
bool AllyOwnerCelebrates(uint16_t owner) {
    if (owner == royale::net::kNoPlayer16) return false;
    const auto& h = gSession.Hud();
    if (h.winnerId == owner) return true;
    if (owner == h.selfId) return gEmote.id >= 0;
    auto it = gState.find(owner);
    return it != gState.end() && it->second.alive && royale::IsEmote(it->second.anim);
}

void Ally_Update(Actor* actor, PlayState* play) {
    namespace A = royale::allymodel;
    auto of = gAllyOf.find(actor);
    if (of == gAllyOf.end()) { Actor_Kill(actor); return; }
    AllyActor& a = gAllies[of->second];
    const float dt = 1.0f / royale::kTickHz;
    if (!a.init) { a.x = a.tx; a.z = a.tz; a.rot = a.trot; a.init = true; }
    const float nx = a.x + (a.tx - a.x) * 0.4f, nz = a.z + (a.tz - a.z) * 0.4f;
    a.speed += (std::hypot(nx - a.x, nz - a.z) / dt - a.speed) * 0.3f;
    a.x = nx; a.z = nz;
    a.rot = static_cast<s16>(a.rot + static_cast<s16>(a.trot - a.rot) * 0.35f);
    a.actAge += dt;
    a.age += dt;
    a.busy = std::max(0.0f, a.busy - dt);
    actor->world.pos.x = a.x;
    actor->world.pos.z = a.z;
    actor->world.pos.y = GroundY(play, a.x, a.z, actor->world.pos.y);
    actor->shape.rot.y = a.rot;
    actor->world.rot.y = a.rot;
    const A::Model& m = A::ModelOf(a.kind);
    a.anim.model = &m;
    actor->focus.pos = actor->world.pos;
    actor->focus.pos.y += (a.resting || a.rolling) ? 30.0f : m.height * 0.75f;
    actor->shape.shadowScale = kAllyShadow[a.kind % royale::kAllyCount];
    // Z-targeting, as with the game's own NPCs (the white reticle, not the red one an enemy gets): works on the free ones and on the helpers
    // alike, so you can face them to talk or see who is fighting for whom.
    actor->targetMode = 3;
    actor->flags |= ACTOR_FLAG_ATTENTION_ENABLED;
    actor->flags &= ~ACTOR_FLAG_HOSTILE;

    const bool free = a.owner == royale::net::kNoPlayer16;
    const bool goron = a.kind == static_cast<int>(royale::AllyKind::Goron);
    Player* pl = GET_PLAYER(play);
    const float playerDist = std::hypot(pl->actor.world.pos.x - a.x, pl->actor.world.pos.z - a.z);
    auto oneShot = [&](int clip, float fade, float hold) {
        a.anim.Play(clip, fade, true);
        a.busy = A::ClipSeconds(m, clip) * hold;
    };
    auto getUp = [&]() {
        if (goron) Audio_PlayActorSound2(actor, NA_SE_EN_GOLON_WAKE_UP);
        oneShot(A::kRise, 0.1f, 1.0f);
    };

    // Moving or standing, with a little hysteresis so it does not flicker between the two.
    if (!a.walking && a.speed > 14.0f) a.walking = true;
    else if (a.walking && a.speed < 6.0f) a.walking = false;

    // Just hired: a cheer (getting up first if it was sitting or curled).
    if (a.lastOwner == royale::net::kNoPlayer16 && !free) {
        const bool wasDown = a.resting;
        a.resting = false;
        a.rolling = false;
        if (wasDown) getUp();
        else oneShot(A::kCheer, 0.15f, 1.0f);
        if (goron) Audio_PlayActorSound2(actor, NA_SE_EN_GOLON_GOOD_BIG);
    }
    a.lastOwner = a.owner;

    // An attack or a heal the server just reported.
    if (a.pending != 0) {
        int clip = A::kAttack;
        if (a.pending == 2 && a.kind == static_cast<int>(royale::AllyKind::Zora)) clip = A::kHeal;
        else if (goron) { clip = a.leftFist ? A::kAttack2 : A::kAttack; a.leftFist = !a.leftFist; }
        if (a.rolling || a.resting) { a.rolling = false; a.resting = false; a.rollAngle = 0.0f; }
        oneShot(clip, 0.06f, clip == A::kHeal ? 0.85f : 0.5f);
        a.pending = 0;
    }

    // Hurt: health dropped since the last look.
    if (a.hp < a.shownHp - 0.015f && !a.rolling) {
        if (a.anim.clip != A::kAttack && a.anim.clip != A::kAttack2 && a.anim.clip != A::kHeal) oneShot(A::kHurt, 0.05f, 1.0f);
        if (a.resting) a.resting = false;
    }
    a.shownHp = a.hp;

    // A Goron who has to hurry curls up and rolls; he unrolls once he has caught up.
    if (goron && !free) {
        if (!a.rolling && a.speed > m.walkSpeed * 1.3f) {
            a.rolling = true;
            a.slowFor = 0.0f;
            a.anim.Play(A::kRest, 0.15f, true);
            a.busy = 0.0f;
            Audio_PlayActorSound2(actor, NA_SE_EN_GOLON_SIT_DOWN);
        } else if (a.rolling) {
            a.slowFor = a.speed < m.walkSpeed * 1.05f ? a.slowFor + dt : 0.0f;
            if (a.slowFor > 0.35f) {
                a.rolling = false;
                a.rollAngle = 0.0f;
                getUp();
                AllyDust(play, a.x, actor->world.pos.y, a.z, 5, 40.0f, 60);
            }
        }
    } else if (a.rolling) {
        a.rolling = false;
        a.rollAngle = 0.0f;
    }
    if (a.rolling) {
        a.rollAngle += a.speed * dt / GoronBallPivot();
        func_8002F974(actor, NA_SE_EV_BIGBALL_ROLL - SFX_FLAG);
        if ((play->gameplayFrames & 3) == 0) AllyDust(play, a.x, actor->world.pos.y, a.z, 1, 30.0f, 45);
    }

    // A free ally with nobody near sits down (a Goron curls up and dozes); when someone comes close it gets up and waves.
    if (free) {
        a.lonely = playerDist > 900.0f ? a.lonely + dt : 0.0f;
        if (!a.resting && a.lonely > 2.5f && !a.walking && a.busy <= 0.0f) {
            a.resting = true;
            a.anim.Play(A::kRest, 0.6f);
            if (goron) Audio_PlayActorSound2(actor, NA_SE_EN_GOLON_SIT_DOWN);
        } else if (a.resting && (playerDist < 900.0f || a.walking)) {
            a.resting = false;
            getUp();
        }
    } else {
        a.lonely = 0.0f;
    }

    // Otherwise: walking, dancing along when its owner dances or wins, waving you over (free, and you are close enough to see), or the ready
    // stance.
    if (a.busy <= 0.0f && !a.rolling && !a.resting) {
        const int base = a.walking ? A::kWalk : AllyOwnerCelebrates(a.owner) ? A::kDance : (free && playerDist < 1600.0f) ? A::kInvite : A::kIdle;
        a.anim.Play(base, 0.22f);
    }
    const float rate = a.anim.clip == A::kWalk ? std::clamp(a.speed / std::max(1.0f, m.walkSpeed), 0.6f, 1.8f) : 1.0f;
    a.anim.Update(dt, rate);
}

void Ally_Draw(Actor* actor, PlayState* play) {
    namespace A = royale::allymodel;
    auto of = gAllyOf.find(actor);
    if (of == gAllyOf.end()) return;
    AllyActor& a = gAllies[of->second];
    const A::Model& m = A::ModelOf(a.kind);
    A::Pose pose;
    a.anim.model = &m;
    a.anim.Evaluate(pose);
    int face = A::kFaceNormal;
    const int clip = a.anim.clip;
    if (a.resting || (a.rolling && a.kind == static_cast<int>(royale::AllyKind::Goron))) face = A::kFaceBlink;   // dozing, or eyes shut as he rolls
    else if (clip == A::kAttack || clip == A::kAttack2 || clip == A::kHurt) face = A::kFaceFierce;
    else if (std::fmod(a.age + of->second * 1.37f, 3.4f) < 0.12f) face = A::kFaceBlink;                  // a blink every few seconds
    DrawAllyModel(play, actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, actor->shape.rot.y * (3.14159265f / 32768.0f), m, pose, face,
                  a.rolling ? a.rollAngle : 0.0f, GoronBallPivot());
}

void Ally_Destroy(Actor* actor, PlayState* play) {
    ActorFunc orig = nullptr;
    auto of = gAllyOf.find(actor);
    if (of != gAllyOf.end()) {
        auto a = gAllies.find(of->second);
        if (a != gAllies.end()) { orig = a->second.origDestroy; gAllies.erase(a); }
        gAllyOf.erase(of);
    }
    if (orig) orig(actor, play);
}

void ReconcileAllies(const royale::HudState& hud) {
    const bool show = gSession.Joined() && InField() && gSession.Client() &&
                      (hud.state == royale::MatchState::Countdown || hud.state == royale::MatchState::Drop || hud.state == royale::MatchState::InMatch || hud.state == royale::MatchState::Ending);
    std::unordered_map<uint8_t, bool> wanted;
    if (show) {
        for (const royale::net::AllyNet& n : gSession.Client()->Allies()) {
            wanted[n.index] = true;
            auto it = gAllies.find(n.index);
            if (it == gAllies.end()) {
                float y = 0;
                if (!FloorAt(n.x, n.z, &y)) continue;
                Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ISHI, n.x, y, n.z, 0, n.rot, 0, 0, false);
                if (actor == nullptr) continue;
                AllyActor a;
                a.actor = actor; a.origDestroy = actor->destroy; a.kind = n.kind; a.owner = n.owner;
                a.tx = n.x; a.tz = n.z; a.trot = n.rot; a.rot = n.rot; a.x = n.x; a.z = n.z; a.init = true;
                a.lastOwner = n.owner;   // one already working for someone when it comes into view does not cheer
                a.hp = a.shownHp = n.hp / 255.0f;
                a.anim.model = &royale::allymodel::ModelOf(n.kind);
                a.anim.Play(royale::allymodel::kIdle, 0.0f, true);
                gAllies[n.index] = a;
                gAllyOf[actor] = n.index;
                actor->update = Ally_Update;
                actor->draw = Ally_Draw;
                actor->destroy = Ally_Destroy;
                actor->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED;
                actor->uncullZoneForward = 4000.0f; actor->uncullZoneScale = 1500.0f; actor->uncullZoneDownward = 1500.0f;
                actor->shape.shadowScale = kAllyShadow[n.kind % royale::kAllyCount];
            } else {
                AllyActor& a = it->second;
                a.tx = n.x; a.tz = n.z; a.trot = n.rot; a.hp = n.hp / 255.0f; a.owner = n.owner;
                if ((n.flags & 2) && a.actAge > 0.5f) { a.actAge = 0.0f; a.pending = 1; }
            }
        }
    }
    for (auto& [id, a] : gAllies) if (!wanted.count(id) && a.actor) Actor_Kill(a.actor);
}

// The free ally you are standing next to, or -1.
int NearbyFreeAlly() {
    if (!InField() || gSession.Client() == nullptr) return -1;
    Player* pl = GET_PLAYER(gPlayState);
    int best = -1;
    float bestD = royale::kHireRange;
    for (const auto& [id, a] : gAllies) {
        if (a.owner != royale::net::kNoPlayer16 || a.actor == nullptr) continue;
        const float d = std::hypot(a.x - pl->actor.world.pos.x, a.z - pl->actor.world.pos.z);
        if (d < bestD) { bestD = d; best = id; }
    }
    return best;
}

void DrawAllyLabels(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const royale::HudState& h) {
    if (!InField()) return;
    Player* pl = GET_PLAYER(gPlayState);
    for (const auto& [id, a] : gAllies) {
        if (a.actor == nullptr) continue;
        const float d = std::hypot(a.x - pl->actor.world.pos.x, a.z - pl->actor.world.pos.z);
        const bool mine = a.owner == h.selfId, free = a.owner == royale::net::kNoPlayer16;
        if (d > (free ? 2600.0f : 1800.0f)) continue;
        ImVec2 at;
        const float top = (a.resting || a.rolling) ? 70.0f : royale::allymodel::ModelOf(a.kind).height + 28.0f;
        if (!WorldToScreen(a.x, a.actor->world.pos.y + top, a.z, &at)) continue;
        const royale::AllyDef& def = royale::kAllyDefs[a.kind];
        const float k = std::clamp(1500.0f / (d + 700.0f), 0.7f, 1.3f), ts = 18.0f * scale * k;
        std::string line1 = free ? std::string(def.name) + " " + def.title : mine ? std::string(def.name) + " (yours)" : std::string(def.name) + " (an ally)";
        const ImU32 col = free ? IM_COL32(255, 222, 110, 255) : mine ? IM_COL32(130, 255, 150, 255) : IM_COL32(190, 200, 220, 255);
        ImVec2 sz = font->CalcTextSizeA(ts, FLT_MAX, 0.0f, line1.c_str());
        dl->AddText(font, ts, ImVec2(at.x - sz.x * 0.5f + 1.5f, at.y + 1.5f), IM_COL32(0, 0, 0, 220), line1.c_str());
        dl->AddText(font, ts, ImVec2(at.x - sz.x * 0.5f, at.y), col, line1.c_str());
        if (free) {
            const bool afford = h.rupees >= def.price;
            const std::string line2 = std::to_string(def.price) + " rupees";
            sz = font->CalcTextSizeA(ts * 0.9f, FLT_MAX, 0.0f, line2.c_str());
            dl->AddText(font, ts * 0.9f, ImVec2(at.x - sz.x * 0.5f + 1.5f, at.y + ts + 1.5f), IM_COL32(0, 0, 0, 220), line2.c_str());
            dl->AddText(font, ts * 0.9f, ImVec2(at.x - sz.x * 0.5f, at.y + ts), afford ? IM_COL32(120, 255, 140, 255) : IM_COL32(255, 120, 110, 255), line2.c_str());
        } else {
            const float w = 90.0f * scale * k, hgt = 7.0f * scale;
            const ImVec2 a0(at.x - w * 0.5f, at.y + ts + 3.0f * scale);
            dl->AddRectFilled(ImVec2(a0.x - 1, a0.y - 1), ImVec2(a0.x + w + 1, a0.y + hgt + 1), IM_COL32(0, 0, 0, 190));
            dl->AddRectFilled(a0, ImVec2(a0.x + w * a.hp, a0.y + hgt), mine ? IM_COL32(90, 220, 110, 255) : IM_COL32(180, 190, 210, 255));
        }
    }
}

// What an ally's hit looks like: a shot flies from it (or a thump lands, for the Goron); a Zora's gift is a shower of green sparks on you.
void AllyActionFx(const royale::ClientEvent& e, const royale::HudState& h) {
    auto it = gAllies.find(static_cast<uint8_t>(e.index));
    if (it == gAllies.end() || it->second.actor == nullptr || gPlayState == nullptr) return;
    AllyActor& a = it->second;
    a.actAge = 0.0f;
    a.pending = e.id == a.owner ? 2 : 1;   // the model plays its attack (or the Zora's healing) on its next update
    const float ay = a.actor->world.pos.y;
    if (e.id == a.owner) {   // mending its owner
        float gy = ay;
        FloorAt(e.x, e.z, &gy);
        SparkBurst(gPlayState, e.x, gy + 60.0f, e.z, { 120, 255, 150, 255 }, 18, 3.0f);
        if (a.owner == h.selfId) Audio_PlaySoundGeneral(NA_SE_SY_HP_RECOVER, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return;
    }
    const s16 yaw = static_cast<s16>(std::atan2(e.x - a.x, e.z - a.z) * (32768.0f / 3.14159265f));
    switch (a.kind) {
        case 0: SpawnProjectileFrom(royale::ItemId::Slingshot, a.x, ay + 42.0f, a.z, yaw); break;    // from the slingshot, at the Kokiri's eye height
        case 1: SpawnProjectileFrom(royale::ItemId::IceArrows, a.x, ay + 80.0f, a.z, yaw); break;    // from the Zora's palms
        case 3: SpawnProjectileFrom(royale::ItemId::FairyBow, a.x, ay + 88.0f, a.z, yaw); break;     // off the Gerudo's bow
        default: {   // the Goron's fist lands: a thump, sparks and a puff of dust where it hit
            float gy = ay;
            FloorAt(e.x, e.z, &gy);
            SparkBurst(gPlayState, e.x, gy + 40.0f, e.z, { 255, 200, 120, 255 }, 20, 6.0f);
            AllyDust(gPlayState, e.x, gy, e.z, 4, 30.0f, 70);
            Vec3f at = { e.x, gy + 40.0f, e.z };
            Audio_PlaySoundGeneral(NA_SE_IT_HAMMER_HIT, &at, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            break;
        }
    }
}

