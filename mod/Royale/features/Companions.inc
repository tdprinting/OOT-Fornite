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
// The four allies are the game's own NPCs: the Kokiri kid, the Zora, the Goron and the Gerudo, each with its real skeleton, textures and animations
// (the same assets the game's NPC actors use). The game never walks or fights with these, so the movement is built here: the real standing, waving
// and posing animations, plus a procedural walk cycle and attack poses added to the limbs while they are drawn. What each carries is drawn with the
// real item model in its hand.
struct NpcSpec {
    const char* skeleton;
    int limbs;
    const char* idle;      // standing and ready (hired)
    const char* invite;    // free to hire: waves you over
    const char* act;       // attacking or healing: played through once
    float scale;           // actor scale
    int hairstyle;         // Gerudo: which hair
    royale::ItemId item;   // carried
    int lHand, rHand;      // which hand holds it (limb index)
    bool itemLeft;
};
NpcSpec NpcOf(int kind) {
    switch (kind) {
        case 0: return { gKm1Skel, 16, gKokiriStandingHandsOnHipsAnim, gKokiriStandingRightArmUpAnim, gKokiriStandingRightArmUpAnim, 0.0105f, 0, royale::ItemId::Slingshot, 11, 14, true };
        case 1: return { gZoraSkel, 20, gZoraHandsOnHipsTappingFootAnim, gZoraOpenArmsAnim, gZoraThrowRupeesAnim, 0.0105f, 0, royale::ItemId::DekuStick, 11, 14, false };
        case 2: return { gGoronSkel, 18, gGoronAnim_004930, gGoronAnim_004930, gGoronAnim_004930, 0.0115f, 0, royale::ItemId::MegatonHammer, 16, 13, false };
        default: return { gGerudoWhiteSkel, 16, gGerudoWhiteIdleAnim, gGerudoWhiteClapAnim, gGerudoWhiteDismissiveAnim, 0.0105f, 1, royale::ItemId::FairyBow, 11, 14, true };
    }
}
struct NpcRig { int torso, lArm, rArm, lFore, rFore, lThigh, rThigh, lShin, rShin, head; };
NpcRig RigOf(int kind) {
    if (kind == 2) return { 10, 11, 14, 12, 15, 2, 5, 3, 6, 17 };   // the Goron's skeleton is laid out differently
    return { 8, 9, 12, 10, 13, 2, 5, 3, 6, 15 };                    // Kokiri, Zora and Gerudo share the humanoid layout
}

constexpr float kRad = 32768.0f / 3.14159265f;

// Called for every limb as the skeleton is drawn: the walk cycle, the idle sway and the attack poses are added to the limb's rotation here.
AllyActor* gDrawingAlly = nullptr;
s32 Ally_OverrideLimb(PlayState*, s32 limb, Gfx** dList, Vec3f*, Vec3s* rot, void*) {
    const AllyActor* a = gDrawingAlly;
    if (a == nullptr) return 0;
    if (a->kind == 0 && limb == 15) *dList = (Gfx*)gKm1DL;   // the Kokiri's head is its own display list, put on at the head limb
    const NpcRig r = RigOf(a->kind);
    const float t = static_cast<float>(ImGui::GetTime());
    auto add = [&](float x, float y, float z) { rot->x = static_cast<s16>(rot->x + x * kRad); rot->y = static_cast<s16>(rot->y + y * kRad); rot->z = static_cast<s16>(rot->z + z * kRad); };
    const float w = a->walkW, s = std::sin(a->phase);
    // the walk: legs swing, knees bend as each foot lifts, the arms swing against the legs, the body twists a little
    if (limb == r.lThigh || limb == r.rThigh) add(0, 0, 0.62f * w * s);
    if (limb == r.lShin || limb == r.rShin) add(0, 0, 0.55f * w * std::max(0.0f, limb == r.lShin ? -s : s) + 0.1f * w);
    if (limb == r.lArm || limb == r.rArm) add(0, 0, -0.45f * w * s);
    if (limb == r.torso) add(0, 0.14f * w * s, -0.08f * w);
    // standing: a slow breath and a little weight shift
    if (limb == r.torso) add(0, 0.03f * std::sin(t * 1.3f + a->kind), 0.025f * std::sin(t * 1.7f));
    if (limb == r.head) add(0, 0.05f * std::sin(t * 0.8f + a->kind * 2.0f), 0.04f * std::sin(t * 1.1f));
    // the attack or heal: how far through it, 0 to 1
    const float T = a->actAge;
    if (T < 0.7f) {
        const float u = T / 0.7f, env = std::sin(u * 3.14159f);
        switch (a->kind) {
            case 0:   // the Kokiri aims the slingshot out in front, then it snaps back
                if (limb == r.lArm) add(0, 0, 1.45f * env);
                if (limb == r.rArm) add(0, 0, -(0.9f + 0.4f * (1.0f - u)) * env);
                if (limb == r.rFore) add(0, 0, -0.9f * env);
                if (limb == r.torso) add(0, -0.25f * env, 0);
                break;
            case 1:   // the Zora throws its arms wide, glowing, and bobs up
                if (limb == r.lArm) add(0, 0, 1.2f * env);
                if (limb == r.rArm) add(0, 0, -1.2f * env);
                if (limb == r.head) add(0, 0, -0.25f * env);
                break;
            case 2: { // the Goron heaves the hammer overhead and brings it down
                const float lift = u < 0.45f ? u / 0.45f : std::max(0.0f, 1.0f - (u - 0.45f) / 0.2f);
                if (limb == r.lArm || limb == r.rArm) add(0, 0, (limb == r.lArm ? 1.0f : -1.0f) * 2.5f * lift);
                if (limb == r.torso) add(0, 0, (u < 0.45f ? -0.3f * lift : 0.55f * (1.0f - std::fabs(u - 0.6f) * 4.0f)));
                break;
            }
            default:  // the Gerudo draws the bow: left arm out, right hand pulled back to the cheek
                if (limb == r.lArm) add(0, 0, 1.5f * env);
                if (limb == r.rArm) add(0, 0, -1.1f * env);
                if (limb == r.rFore) add(0, 0, -1.6f * env * (1.0f - u * 0.6f));
                if (limb == r.torso) add(0, -0.2f * env, 0);
                break;
        }
    }
    return 0;
}

void Ally_PostLimb(PlayState* play, s32 limb, Gfx** dList, Vec3s*, void*) {
    (void)dList;
    const AllyActor* a = gDrawingAlly;
    if (a == nullptr) return;
    const NpcSpec spec = NpcOf(a->kind);
    OPEN_DISPS(play->state.gfxCtx);
    if (a->kind == 3 && limb == 15) {   // the Gerudo's hair goes on at the head
        static const char* hair[2] = { gGerudoWhiteHairstyleBobDL, gGerudoWhiteHairstyleStraightFringeDL };
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)hair[spec.hairstyle % 2]);
    }
    if (limb == (spec.itemLeft ? spec.lHand : spec.rHand)) {   // what it carries, in the real item model
        const int gid = GidFor(spec.item);
        if (gid >= 0) {
            Matrix_Push();
            Matrix_Translate(600.0f, 0.0f, 0.0f, MTXMODE_APPLY);
            Matrix_RotateZ(spec.itemLeft ? 1.57f : -1.0f, MTXMODE_APPLY);
            Matrix_RotateY(1.57f, MTXMODE_APPLY);
            const float k = GidScale(gid) * 3.0f;
            Matrix_Scale(k, k, k, MTXMODE_APPLY);
            DrawItemModel(play, gid);
            Matrix_Pop();
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

void Ally_Update(Actor* actor, PlayState* play) {
    auto of = gAllyOf.find(actor);
    if (of == gAllyOf.end()) { Actor_Kill(actor); return; }
    AllyActor& a = gAllies[of->second];
    if (!a.init) { a.x = a.tx; a.z = a.tz; a.rot = a.trot; a.init = true; }
    const float nx = a.x + (a.tx - a.x) * 0.4f, nz = a.z + (a.tz - a.z) * 0.4f;
    a.moved = a.moved * 0.8f + std::hypot(nx - a.x, nz - a.z);
    a.x = nx; a.z = nz;
    a.rot = static_cast<s16>(a.rot + static_cast<s16>(a.trot - a.rot) * 0.35f);
    a.actAge += 1.0f / royale::kTickHz;
    actor->world.pos.x = a.x;
    actor->world.pos.z = a.z;
    actor->world.pos.y = GroundY(play, a.x, a.z, actor->world.pos.y);
    actor->shape.rot.y = a.rot;
    actor->world.rot.y = a.rot;
    actor->focus.pos = actor->world.pos;
    actor->focus.pos.y += 55.0f;
    // Z-targeting, as with the game's own NPCs (the white reticle, not the red one an enemy gets): works on the free ones and on the helpers
    // alike, so you can face them to talk or see who is fighting for whom.
    actor->targetMode = 3;
    actor->flags |= ACTOR_FLAG_ATTENTION_ENABLED;
    actor->flags &= ~ACTOR_FLAG_HOSTILE;

    const NpcSpec spec = NpcOf(a.kind);
    if (!a.skReady) {
        SkelAnime_InitFlex(play, &a.sk, (FlexSkeletonHeader*)spec.skeleton, nullptr, a.joint, a.morph, spec.limbs);
        a.skReady = true;
    }
    // Which of the game's animations: waving you over while free to hire, the attack pose while it fights, the ready stance otherwise.
    const bool free = a.owner == royale::net::kNoPlayer16;
    const bool acting = a.actAge < 0.7f;
    const char* want = free ? spec.invite : (acting ? spec.act : spec.idle);
    if (a.playing != (const void*)want || (acting && !a.wasActing && !free)) {
        Animation_Change(&a.sk, (AnimationHeader*)want, (a.kind == 2 && want == spec.idle) ? 0.0f : 1.0f, 0.0f, Animation_GetLastFrame((void*)want), ANIMMODE_LOOP, -4.0f);
        a.playing = want;
    }
    a.wasActing = acting;
    SkelAnime_Update(&a.sk);
    const float moving = std::clamp(a.moved / 1.6f, 0.0f, 1.0f);
    a.walkW += (moving - a.walkW) * 0.25f;
    a.phase += (0.35f + 0.3f * a.walkW) * a.walkW;
    a.bob = a.walkW * std::fabs(std::sin(a.phase)) * 3.0f;
}

void Ally_Draw(Actor* actor, PlayState* play) {
    auto of = gAllyOf.find(actor);
    if (of == gAllyOf.end()) return;
    AllyActor& a = gAllies[of->second];
    if (!a.skReady) return;
    const NpcSpec spec = NpcOf(a.kind);
    const float t = static_cast<float>(play->gameplayFrames);
    const int eye = std::fmod(t + of->second * 37.0f, 90.0f) < 4.0f ? 2 : 0;   // a blink every few seconds
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    switch (a.kind) {
        case 0: {   // Kokiri: tunic and boots colours come through two small display lists, as the game's Kokiri do
            Gfx* tunic = static_cast<Gfx*>(Graph_Alloc(play->state.gfxCtx, sizeof(Gfx) * 2));
            gDPSetEnvColor(tunic, 30, 105, 27, 255);
            gSPEndDisplayList(tunic + 1);
            Gfx* boots = static_cast<Gfx*>(Graph_Alloc(play->state.gfxCtx, sizeof(Gfx) * 2));
            gDPSetEnvColor(boots, 110, 170, 39, 255);
            gSPEndDisplayList(boots + 1);
            gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)tunic);
            gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)boots);
            break;
        }
        case 1: {
            static const char* eyes[3] = { gZoraEyeOpenTex, gZoraEyeHalfTex, gZoraEyeClosedTex };
            gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)eyes[eye]);
            break;
        }
        case 2: {
            static const char* eyes[4] = { gGoronCsEyeClosed2Tex, gGoronCsEyeOpenTex, gGoronCsEyeHalfTex, gGoronCsEyeClosedTex };
            static const char* mouths[2] = { gGoronCsMouthNeutralTex, gGoronCsMouthSmileTex };
            gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)eyes[eye == 2 ? 3 : 1]);
            gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)mouths[a.owner == royale::net::kNoPlayer16 ? 1 : 0]);
            break;
        }
        default: {
            static const char* eyes[3] = { gGerudoWhiteEyeOpenTex, gGerudoWhiteEyeHalfTex, gGerudoWhiteEyeClosedTex };
            gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)eyes[eye]);
            break;
        }
    }
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y + a.bob, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(actor->shape.rot.y * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    Matrix_Scale(spec.scale, spec.scale, spec.scale, MTXMODE_APPLY);
    gDrawingAlly = &a;
    SkelAnime_DrawFlexOpa(play, a.sk.skeleton, a.sk.jointTable, a.sk.dListCount, Ally_OverrideLimb, Ally_PostLimb, actor);
    gDrawingAlly = nullptr;
    CLOSE_DISPS(play->state.gfxCtx);
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
                gAllies[n.index] = a;
                gAllyOf[actor] = n.index;
                actor->update = Ally_Update;
                actor->draw = Ally_Draw;
                actor->destroy = Ally_Destroy;
                actor->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED;
                actor->uncullZoneForward = 4000.0f; actor->uncullZoneScale = 1500.0f; actor->uncullZoneDownward = 1500.0f;
                actor->shape.shadowScale = 40.0f;
            } else {
                AllyActor& a = it->second;
                a.tx = n.x; a.tz = n.z; a.trot = n.rot; a.hp = n.hp / 255.0f; a.owner = n.owner;
                if ((n.flags & 2) && a.actAge > 0.5f) a.actAge = 0.0f;
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
        if (!WorldToScreen(a.x, a.actor->world.pos.y + 245.0f, a.z, &at)) continue;
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
        case 0: SpawnProjectileFrom(royale::ItemId::Slingshot, a.x, ay + 70.0f, a.z, yaw); break;
        case 1: SpawnProjectileFrom(royale::ItemId::IceArrows, a.x, ay + 90.0f, a.z, yaw); break;
        case 3: SpawnProjectileFrom(royale::ItemId::FairyBow, a.x, ay + 90.0f, a.z, yaw); break;
        default: {
            float gy = ay;
            FloorAt(e.x, e.z, &gy);
            SparkBurst(gPlayState, e.x, gy + 40.0f, e.z, { 255, 200, 120, 255 }, 20, 6.0f);
            Vec3f at = { e.x, gy + 40.0f, e.z };
            Audio_PlaySoundGeneral(NA_SE_IT_HAMMER_HIT, &at, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            break;
        }
    }
}

