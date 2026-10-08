// ---- Avriella, the baby pet ----------------------------------------------------------------------------------------------------------
// A second pet to pick instead of Lilo (the "Your pet" section of the Battle Royale menu): a baby girl in the N64 style, made in Blender
// (tools/avriella/, assets/avriella/avriella.blend, about 1050 triangles, a 64x32 cloth and a 64x32 skin texture and five 32x32 face pictures, 23 bones,
// fifteen clips (rolling about, kicking and chewing among them); shared/avriella_model.h). She is skinned on the CPU every
// frame exactly like Lilo (DrawLiloModel) and, like Lilo, is only for looks: no collision, no targeting, nothing the server knows about, nobody else sees her.
// She rolls after you, sits when you stop, and then does baby things: waves, claps, babbles, giggles, stacks three tiny rocks, rolls over, stands up
// and wobbles, reaches for loot nearby, and falls asleep when you stand still for long. Stand still facing her and press A to talk: she sits and
// babbles a line in the game's own text box. Her voice is Link's own child voice sounds played much higher (no recordings of a real baby).
constexpr float kBabyScale = 0.62f;   // the model stands about 64 units tall at 1.0 (sitting about 54 with her tuft, crawling about 50); Link is about 60, so standing she comes up to about two thirds of him

void DrawAvriellaModel(PlayState* play, float x, float y, float z, float yaw, float scale, const royale::avriella::Pose& pose, int face) {
    GfxLayer layer(play, GfxLayerId::Characters);   // its thousands of vertices and commands go in the layer pool, not the game's buffer
    namespace A = royale::avriella;
    constexpr float kSub = 8.0f;   // vertices go to the graphics chip in 1/8 units, so the small model keeps its shape
    Vtx* vtx = static_cast<Vtx*>(FrameAlloc(play, sizeof(Vtx) * A::kVertCount));
    if (vtx == nullptr) return;
    const float wx = 0.35f, wy = 0.82f, wz = 0.45f;   // the sun, as for Lilo
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    const float lx = wx * cy - wz * sy, ly = wy, lz = wx * sy + wz * cy;
    for (int i = 0; i < A::kVertCount; i++) {
        const A::Vert& v = A::kVerts[i];
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
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);         // the light is already in the vertex colours; she is seen from all sides
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIDECALA, G_CC_PASS2);     // her texture times the vertex colour
    Matrix_Translate(x, y, z, MTXMODE_NEW);
    Matrix_RotateY(yaw, MTXMODE_APPLY);
    Matrix_Scale(scale / kSub, scale / kSub, scale / kSub, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    const int pic = std::clamp(face, 0, static_cast<int>(A::kFaceCount) - 1);
    int loaded = -1;
    for (int b = 0; b < A::kBatchCount; b++) {
        const A::Batch& bt = A::kBatches[b];
        const int tex = bt.texture == A::kFace ? 2 + pic : static_cast<int>(bt.texture);   // 0 cloth, 1 skin, 2 and up the faces
        if (tex != loaded) {
            const uint8_t* data = tex == 0 ? A::kClothTex : tex == 1 ? A::kSkinTex : A::kFaceTex[tex - 2];
            const int w = tex == 0 ? A::kClothW : tex == 1 ? A::kSkinW : A::kFaceW, h = tex == 0 ? A::kClothH : tex == 1 ? A::kSkinH : A::kFaceH;
            gDPLoadTextureBlock(POLY_OPA_DISP++, data, G_IM_FMT_RGBA, G_IM_SIZ_16b, w, h, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                                G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            loaded = tex;
        }
        gSPVertex(POLY_OPA_DISP++, reinterpret_cast<uintptr_t>(&vtx[bt.firstVert]), bt.vertCount, 0);
        const int end = bt.firstTri + bt.triCount;
        int t = bt.firstTri;
        for (; t + 1 < end; t += 2)
            gSP2Triangles(POLY_OPA_DISP++, A::kTris[t][0], A::kTris[t][1], A::kTris[t][2], 0, A::kTris[t + 1][0], A::kTris[t + 1][1], A::kTris[t + 1][2], 0);
        if (t < end) gSP1Triangle(POLY_OPA_DISP++, A::kTris[t][0], A::kTris[t][1], A::kTris[t][2], 0);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}


// `screen` is the tablet's picture; `onlyBone` (when not -1) draws just that bone's parts (the dropped tablet on its own).
void DrawMayaCompanionModel(PlayState* play, float x, float y, float z, float yaw, float scale, const royale::maya::Pose& pose, int face, int screen = 0, int onlyBone = -1) {
    GfxLayer layer(play, GfxLayerId::Characters);   // its thousands of vertices and commands go in the layer pool, not the game's buffer
    namespace A = royale::maya;
    constexpr float kSub = 8.0f;   // vertices go to the graphics chip in 1/8 units, so the small model keeps its shape
    Vtx* vtx = static_cast<Vtx*>(FrameAlloc(play, sizeof(Vtx) * A::kVertCount));
    if (vtx == nullptr) return;
    const float wx = 0.35f, wy = 0.82f, wz = 0.45f;   // the sun, as for Lilo
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    const float lx = wx * cy - wz * sy, ly = wy, lz = wx * sy + wz * cy;
    for (int i = 0; i < A::kVertCount; i++) {
        const A::Vert& v = A::kVerts[i];
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
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);         // the light is already in the vertex colours; she is seen from all sides
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIDECALA, G_CC_PASS2);     // her texture times the vertex colour
    Matrix_Translate(x, y, z, MTXMODE_NEW);
    Matrix_RotateY(yaw, MTXMODE_APPLY);
    Matrix_Scale(scale / kSub, scale / kSub, scale / kSub, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    const int pic = std::clamp(face, 0, static_cast<int>(A::kFaceCount) - 1);
    int loaded = -1;
    for (int b = 0; b < A::kBatchCount; b++) {
        const A::Batch& bt = A::kBatches[b];
        // Hobby props parked below the floor are omitted, even beside cliffs.
        const int bone = A::kVerts[bt.firstVert].b0;
        if (onlyBone >= 0 && bone != onlyBone) continue;
        if (A::kBoneIsProp[bone] && pose.bone[bone].t[1] < -100.0f) continue;
        const int scr = std::clamp(screen, 0, static_cast<int>(A::kScreenCount) - 1), faceTex = 2 + static_cast<int>(A::kFaceCount);
        const int tex = bt.texture == A::kFace ? 2 + pic : bt.texture == A::kScreen ? faceTex + scr : static_cast<int>(bt.texture);   // 0 cloth, 1 skin, then the faces, then the screens
        if (tex != loaded) {
            const bool isScreen = tex >= faceTex;
            const uint8_t* data = tex == 0 ? A::kClothTex : tex == 1 ? A::kSkinTex : isScreen ? A::kScreenTex[tex - faceTex] : A::kFaceTex[tex - 2];
            const int w = tex == 0 ? A::kClothW : tex == 1 ? A::kSkinW : isScreen ? A::kScreenW : A::kFaceW, h = tex == 0 ? A::kClothH : tex == 1 ? A::kSkinH : isScreen ? A::kScreenH : A::kFaceH;
            gDPLoadTextureBlock(POLY_OPA_DISP++, data, G_IM_FMT_RGBA, G_IM_SIZ_16b, w, h, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                                G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            loaded = tex;
        }
        gSPVertex(POLY_OPA_DISP++, reinterpret_cast<uintptr_t>(&vtx[bt.firstVert]), bt.vertCount, 0);
        const int end = bt.firstTri + bt.triCount;
        int t = bt.firstTri;
        for (; t + 1 < end; t += 2)
            gSP2Triangles(POLY_OPA_DISP++, A::kTris[t][0], A::kTris[t][1], A::kTris[t][2], 0, A::kTris[t + 1][0], A::kTris[t + 1][1], A::kTris[t + 1][2], 0);
        if (t < end) gSP1Triangle(POLY_OPA_DISP++, A::kTris[t][0], A::kTris[t][1], A::kTris[t][2], 0);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}



// Maya's two real, user-confirmed playful responses. A dedicated mixer slot
// respects game volume and never interrupts Lilo, Avriella, music or other SFX.
double gMayaVoiceQuietUntil=0;
void StopMayaVoice() { StopVoice(kVoiceMaya); StopVoice(kVoicePhone); gMayaVoiceQuietUntil=0; }
// Mom's cartoon phone babble on the video call (synthesised, shared/maya_phone.h).
void PlayMayaPhone() {
    static const auto babble = std::make_shared<const std::vector<int16_t>>(royale::maya_phone::kData, royale::maya_phone::kData + royale::maya_phone::kCount);
    const float volume = GameVolume(false);
    if (volume < 0.01f || !CVarGetInteger(CVAR_SETTING("Royale.MayaVoice"),1)) return;
    StartVoice(kVoicePhone, babble, false, royale::maya_phone::kRate, false, volume * 0.8f);
}
bool PlayMayaVoice(int clip, bool force=false) {
    namespace S=royale::maya_snd;
    static std::shared_ptr<const std::vector<int16_t>> cache[S::kClipCount];
    const double now=ImGui::GetTime();
    const float volume=GameVolume(false)*.8f;
    if (!CVarGetInteger(CVAR_SETTING("Royale.MayaVoice"),1) || volume<.01f || clip<0 || clip>=S::kClipCount ||
        !VoiceDone(kVoiceMaya) || (!force && now<gMayaVoiceQuietUntil)) return false;
    if(!cache[clip])cache[clip]=std::make_shared<const std::vector<int16_t>>(S::kClips[clip].data,S::kClips[clip].data+S::kClips[clip].count);
    StartVoice(kVoiceMaya,cache[clip],false,S::kRate,false,volume);
    gMayaVoiceQuietUntil=now+static_cast<double>(S::kClips[clip].count)/S::kRate+3.0+Rand_ZeroOne()*2.0;
    return true;
}

// Maya is a local cosmetic companion. Original mesh/rig: tools/maya/.
struct MayaCompanionState {
    Actor* actor = nullptr;
    royale::maya::Animator anim;
    royale::TailTracker tail;   // her ponytail's cloth springs (royale::maya::ApplyPonytail)
    // The video-chat scene: 1 chatting, 2 running from the cloud, 3 watching it clear and then going back for the tablet; 0 none.
    int scene = 0;
    float sceneT = 0, cloudX = 0, cloudY = 0, cloudZ = 0;
    bool fartHeard = false, phoneHeard = false;
    royale::TabletBody tablet;   // the dropped tablet, a physics object lying about until she fetches it
    bool tabletLive = false;
    float tabletAge = 0;
    float yaw = 0, still = 0, hobbyTime = 0, blockedTime = 0, speed = 0, expressionClock = 0, greeting = 2.8f;
    int line = 0, hobby = 0;
    bool talking = false, greeted = false, voiceReplyPlayed = false;
};
MayaCompanionState gMayaCompanion;

static_assert(kTextAvriellaPet + royale::kAvriellaPetLineCount <= kTextMayaCompanion, "Companion text overlap");
static_assert(kTextMayaCompanion + royale::kMayaCompanionLineCount <= 0x7FFF, "Maya text overflow");
bool PetPathClear(PlayState* play,const Vec3f& pos,float x,float z) {
    Vec3f from={pos.x,pos.y+20,pos.z},to={x,pos.y+20,z},hit;
    CollisionPoly* poly=nullptr;s32 bgId=0;
    return !BgCheck_EntityLineTest1(&play->colCtx,&from,&to,&hit,&poly,true,false,false,true,&bgId);
}
bool PetFloorAt(float x,float z,float* y) {
    if(InField()) return WalkableAt({x,z}) && FloorAt(x,z,y);
    return RawFloorAt(x,z,y) && !WaterAt(x,z,*y) && !OnExitFloor(x,z) && !HazardFloorAt(x,z);
}
// Where the tablet is in the world right now (centre and orientation), from her pose, facing and the scale she is drawn at.
void MayaTabletWorld(const royale::maya::Pose& pose, const Vec3f& base, float yaw, float scale, float outPos[3], float outQ[4]) {
    namespace M = royale::maya;
    static const int tb = M::BoneByName("tablet");
    const float c0[3] = {M::kTabletCenter[0], M::kTabletCenter[1], M::kTabletCenter[2]};
    float m[3];
    royale::TabletBody::Rot(pose.bone[tb].q, c0, m);
    for (int i = 0; i < 3; i++) m[i] += pose.bone[tb].t[i];
    const float cy = std::cos(yaw), sn = std::sin(yaw);
    outPos[0] = base.x + (m[0] * cy + m[2] * sn) * scale;
    outPos[1] = base.y + m[1] * scale;
    outPos[2] = base.z + (-m[0] * sn + m[2] * cy) * scale;
    const float hy = std::sin(yaw * 0.5f), hw = std::cos(yaw * 0.5f), *q = pose.bone[tb].q;   // world = turn about Y by yaw, then the pose
    outQ[0] = hw * q[0] + hy * q[2];
    outQ[1] = hw * q[1] + hy * q[3];
    outQ[2] = hw * q[2] - hy * q[0];
    outQ[3] = hw * q[3] - hy * q[1];
}
// The dropped tablet on its own, lying wherever it came to rest.
void DrawMayaTablet(PlayState* play, const royale::TabletBody& b) {
    namespace M = royale::maya;
    static const int tb = M::BoneByName("tablet");
    M::Pose pose;
    for (int i = 0; i < 4; i++) pose.bone[tb].q[i] = b.q[i];
    float rc[3];
    royale::TabletBody::Rot(b.q, M::kTabletCenter, rc);
    for (int i = 0; i < 3; i++) pose.bone[tb].t[i] = -rc[i];
    DrawMayaCompanionModel(play, b.p[0], b.p[1], b.p[2], 0.0f, M::kWorldScale, pose, 0, M::kScreenBuilding, tb);
}
void MayaCompanion_Update(Actor* actor, PlayState* play) {
    if (SocialPet_Update(actor,play,2)) return;
    namespace M = royale::maya;
    auto& c = gMayaCompanion;
    Player* player = GET_PLAYER(play);
    const float dt = 1.0f / royale::kTickHz;
    const auto& pp = player->actor.world.pos;
    float dx = pp.x - actor->world.pos.x, dz = pp.z - actor->world.pos.z;
    float distance = std::hypot(dx, dz);
    c.expressionClock += dt; c.greeting = std::max(0.0f,c.greeting-dt);
    bool talking = TalkingTo(actor);
    if (c.scene && talking) { c.scene = 0; StopVoice(kVoicePhone); }   // you spoke to her: the scene is over (a dropped tablet stays where it fell)
    if (c.tabletLive) {   // the dropped tablet bounces and settles; it is gone once she has fetched it or after a while
        const float groundY = actor->world.pos.y;
        auto floorAt = [groundY](float x, float z) { float y; return FloorAt(x, z, &y) ? y : groundY; };
        for (int i = 0; i < 6; i++) c.tablet.Step(dt / 6.0f, floorAt);
        c.tabletAge += dt;
        if (c.tabletAge > 30.0f) c.tabletLive = false;
    }
    if (c.talking && !talking) { c.voiceReplyPlayed=false; c.line = (c.line + 1) % royale::kMayaCompanionLineCount; c.hobby = c.line % 8; c.hobbyTime = 0; }
    c.talking = talking;
    int clip = M::kIdle;
    if (c.scene >= 2 && !talking) {
        // The video-chat scene's second half: bolt from the cloud (the way the cloud is not), stand and gape while it drifts, then fetch the tablet.
        c.sceneT += dt; c.still = 0; c.hobbyTime = 0;
        float dirX, dirZ, desired;
        const float tx = c.tablet.p[0] - actor->world.pos.x, tz = c.tablet.p[2] - actor->world.pos.z, td = std::hypot(tx, tz);
        if (c.scene == 2) {
            dirX = actor->world.pos.x - c.cloudX; dirZ = actor->world.pos.z - c.cloudZ;
            const float l = std::hypot(dirX, dirZ);
            if (l < 1.0f) { dirX = -std::sin(c.yaw); dirZ = -std::cos(c.yaw); } else { dirX /= l; dirZ /= l; }
            desired = 150.0f;
            if (c.sceneT > 2.6f) { c.scene = 3; c.sceneT = 0; }
        } else {
            dirX = tx; dirZ = tz;
            if (td > 1.0f) { dirX /= td; dirZ /= td; }
            desired = c.sceneT > 4.5f && c.tabletLive && td > 30.0f ? 62.0f : 0.0f;   // wait for the cloud to thin, then walk back for the tablet
            if (c.sceneT > 4.5f && (!c.tabletLive || td <= 30.0f)) {
                if (c.tabletLive) { c.tabletLive = false; SparkBurst(play, c.tablet.p[0], c.tablet.p[1] + 6.0f, c.tablet.p[2], { 255, 190, 220, 255 }, 8, 2.0f); }
                c.scene = 0; c.hobby = 0; c.hobbyTime = 0;
            }
            if (distance > 600.0f) c.scene = 0;
        }
        const float wantYaw = std::atan2(dirX, dirZ);
        c.yaw += std::atan2(std::sin(wantYaw-c.yaw),std::cos(wantYaw-c.yaw))*(c.scene == 2 ? .35f : .15f);
        c.speed += std::clamp(desired-c.speed,-300.0f*dt,260.0f*dt);
        const float step = c.speed * dt;
        const float nx = actor->world.pos.x + std::sin(c.yaw) * step, nz = actor->world.pos.z + std::cos(c.yaw) * step;
        float floor = 0;
        if (step > 0.01f && PetFloorAt(nx,nz,&floor) && PetPathClear(play,actor->world.pos,nx,nz) && std::fabs(floor-actor->world.pos.y)<45.0f) {
            actor->world.pos.x = nx; actor->world.pos.z = nz; actor->world.pos.y = floor;
        }
        if (c.scene == 2) clip = M::kRun;
        else clip = c.speed > 20.0f ? M::kWalk : M::kIdle;
        if (c.scene == 2 && c.sceneT < 1.8f) {   // the cloud keeps rolling out where the tablet fell
            FartCloudStep(play, c.cloudX, c.cloudY - 12.0f, c.cloudZ, 3, 2.4f);
        } else if (c.scene >= 2 && c.sceneT < 3.2f) FartCloudStep(play, c.cloudX, c.cloudY - 12.0f, c.cloudZ, 1, 2.6f);
    } else if (distance > 115.0f && !talking) {
        if (c.scene == 1) { c.scene = 0; StopVoice(kVoicePhone); }   // she was called away mid-chat
        c.still = 0; c.hobbyTime = 0;
        const float yaw = std::atan2(dx, dz);
        c.yaw += std::atan2(std::sin(yaw-c.yaw),std::cos(yaw-c.yaw))*.18f;
        const bool scooter = distance > 260.0f;
        const bool running = distance > 185.0f;
        const float desired = scooter ? 240.0f : running ? 130.0f : 62.0f;
        c.speed += std::clamp(desired-c.speed,-260.0f*dt,180.0f*dt);
        const float step = std::min(distance - 90.0f, c.speed * dt);
        float nx = actor->world.pos.x + dx / distance * step;
        float nz = actor->world.pos.z + dz / distance * step;
        float floor = 0;
        if (PetFloorAt(nx,nz,&floor) && PetPathClear(play,actor->world.pos,nx,nz) && std::fabs(floor-actor->world.pos.y)<45.0f) {
            actor->world.pos.x = nx; actor->world.pos.z = nz; actor->world.pos.y = floor; c.blockedTime = 0;
        } else {
            c.blockedTime += dt;
            if (distance > 700.0f || c.blockedTime > 2.5f || std::fabs(pp.y-actor->world.pos.y)>170.0f) {
            // Catch up on a valid nearby floor after a climb or teleport.
            nx = pp.x - std::sin(yaw)*90; nz = pp.z - std::cos(yaw)*90;
            if (PetFloorAt(nx,nz,&floor)) { actor->world.pos = {nx,floor,nz}; c.blockedTime=0; }
            }
        }
        clip = scooter ? M::kScooter : running ? M::kRun : M::kWalk;
    } else {
        c.still += dt; c.hobbyTime += dt;
        const float yaw=std::atan2(dx,dz);
        c.yaw += std::atan2(std::sin(yaw-c.yaw),std::cos(yaw-c.yaw))*.08f;
        c.speed=std::max(0.0f,c.speed-260.0f*dt);
        static constexpr int hobbies[] = {M::kFidget,M::kTablet,M::kDraw,M::kHop,M::kPizza,M::kLearn,M::kCheer,M::kPoint,M::kVideochat};
        const int hobbyClip=hobbies[c.hobby];
        const float duration=M::InfoOf(hobbyClip).loops ? M::ClipSeconds(hobbyClip)+.4f : M::ClipSeconds(hobbyClip)+.3f;
        if (!talking && c.scene == 0 && c.hobbyTime > duration) { c.hobby = (c.hobby + 1) % 9; c.hobbyTime = 0; }
        // Hobbies first; after a long wait she sits down, and after a longer one she nods off hugging her knees.
        clip = talking ? (c.line>=10 ? M::kGiggle : M::kTalk) : c.greeting>0 ? M::kWave : c.still > 45 ? M::kSleep : c.still > 24 ? M::kSit : c.still > 3 ? hobbies[c.hobby] : M::kIdle;
    }
    if (c.scene == 0 && clip == M::kVideochat && !talking) { c.scene = 1; c.sceneT = 0; c.fartHeard = false; c.phoneHeard = false; c.tabletLive = false; }
    if (c.scene == 1) {
        clip = M::kVideochat;
        const float t = c.anim.clip == M::kVideochat ? c.anim.time : 0.0f;
        M::Pose pose; M::SampleClip(M::kVideochat, t, pose);
        float tp[3], tq[4];
        MayaTabletWorld(pose, actor->world.pos, c.yaw, M::kWorldScale, tp, tq);
        if (t >= 0.55f && !c.phoneHeard && t < M::kVideoChatFartTime) { c.phoneHeard = true; PlayMayaPhone(); }   // mom's cartoon babble
        if (t >= M::kVideoChatFartTime && !c.fartHeard) { c.fartHeard = true; StopVoice(kVoicePhone); PlayOneShot(2); }   // ...cut off by the big one
        if (t >= M::kVideoChatFartTime + 0.15f && t < M::kVideoChatDropTime)   // the cloud pours out of the screen, thicker and thicker
            FartCloudStep(play, tp[0], tp[1] - 12.0f, tp[2], 2, 0.7f + 1.2f * (t - M::kVideoChatFartTime));
        if (t >= M::kVideoChatDropTime && !c.tabletLive) {   // it leaves her hands: from here on it is a falling object
            M::Pose held; M::SampleClip(M::kVideochat, M::kVideoChatDropTime, held);
            MayaTabletWorld(held, actor->world.pos, c.yaw, M::kWorldScale, tp, tq);
            c.tablet = royale::TabletBody{};
            for (int i = 0; i < 3; i++) c.tablet.p[i] = tp[i];
            for (int i = 0; i < 4; i++) c.tablet.q[i] = tq[i];
            // flung forward and up a little as her hands fly open
            c.tablet.v[0] = std::sin(c.yaw) * 55.0f; c.tablet.v[2] = std::cos(c.yaw) * 55.0f; c.tablet.v[1] = 30.0f;
            c.tablet.w[0] = 7.0f; c.tablet.w[2] = (Rand_ZeroOne() - 0.5f) * 9.0f;
            c.tabletLive = true; c.tabletAge = 0;
            c.cloudX = tp[0]; c.cloudY = tp[1]; c.cloudZ = tp[2];
            FartCloudStep(play, tp[0], tp[1] - 12.0f, tp[2], 14, 2.2f);
        }
        if (t >= M::kVideoChatFleeTime) { c.scene = 2; c.sceneT = 0; clip = M::kRun; c.speed = 60.0f; }
    }
    if (!talking && distance < 140 && std::fabs(player->linearVelocity) < 3.0f && c.scene == 0 && OfferTalk(actor, play, static_cast<u16>(kTextMayaCompanion+c.line), 140.0f)) { c.talking=true; clip=M::kWave; }
    if (!c.greeted && c.greeting>0) c.greeted=PlayMayaVoice(royale::maya_snd::kHappy);
    if (talking && !c.voiceReplyPlayed) c.voiceReplyPlayed=PlayMayaVoice(c.line>=10 ? royale::maya_snd::kHappy : royale::maya_snd::kPlayful,true);
    if (clip!=c.anim.clip && (clip==M::kCheer || clip==M::kHop || clip==M::kGiggle)) PlayMayaVoice(royale::maya_snd::kHappy);
    c.anim.Play(clip,(clip==M::kSit || clip==M::kSleep || c.anim.clip==M::kSit || c.anim.clip==M::kSleep) ? .8f : .35f);
    // Cycles per second that match the ground speed to the stride, so her feet plant instead of skating.
    const float rate=clip==M::kWalk ? std::clamp(c.speed/M::kWalkStride*M::ClipSeconds(M::kWalk),.35f,2.0f) : clip==M::kRun ? std::clamp(c.speed/M::kRunStride*M::ClipSeconds(M::kRun),.6f,2.1f) : 1.0f;
    c.anim.Update(dt,rate);
    c.yaw=std::atan2(std::sin(c.yaw),std::cos(c.yaw));
    actor->shape.rot.y = static_cast<s16>(static_cast<int32_t>(c.yaw * (32768.0f / 3.14159265f)));
    actor->focus.pos = actor->world.pos; actor->focus.pos.y += royale::maya::kFocusHeight;
}
void MayaCompanion_Draw(Actor* actor, PlayState* play) {
    royale::maya::Pose pose; gMayaCompanion.anim.Evaluate(pose);
    {   // the ponytail follows how she moves and turns, and the wind (the cloth physics setting scales it; off = the baked sway only)
        float wx, wz, wind; WindNow(&wx, &wz, &wind);
        gMayaCompanion.tail.Update(ImGui::GetTime(), actor->world.pos.x, actor->world.pos.z, gMayaCompanion.yaw, std::clamp(wind, 0.0f, 1.0f));
        royale::maya::ApplyPonytail(pose, gMayaCompanion.tail.spring, gClothScale);
    }
    const int face = royale::maya::Expression(gMayaCompanion.anim.clip,gMayaCompanion.anim.time,gMayaCompanion.expressionClock);
    const int screen = royale::maya::ScreenFor(gMayaCompanion.anim.clip,gMayaCompanion.anim.time);
    if (gMayaCompanion.tabletLive) { static const int tb = royale::maya::BoneByName("tablet"); pose.bone[tb].t[1] = -300.0f; }   // the one in her hands has gone: it is on the floor
    DrawMayaCompanionModel(play,actor->world.pos.x,actor->world.pos.y,actor->world.pos.z,gMayaCompanion.yaw,royale::maya::kWorldScale,pose,face,screen);
    if (gMayaCompanion.tabletLive) DrawMayaTablet(play, gMayaCompanion.tablet);
}
void MayaCompanion_Destroy(Actor* actor, PlayState*) { if (gMayaCompanion.actor==actor) { StopMayaVoice(); gMayaCompanion = MayaCompanionState{}; } }
void ReconcileMayaCompanion(const royale::HudState& hud) {
    const bool want = DebugOn(kDbgAllies) && (LobbyPetsActive() || (MapOption("LiloPet",false) && PetKind()==2)) && gSession.Joined() &&
        InPlayableScene(hud.state) && gPlayState != nullptr && !gSkydiving && !gSpectating && !(IsLive(hud) && hud.haveSelf && !hud.selfAlive);
    if (!want) { if (gMayaCompanion.actor) { Actor_Kill(gMayaCompanion.actor); StopMayaVoice(); } gMayaCompanion = MayaCompanionState{}; return; }
    if (gMayaCompanion.actor) return;
    Player* player=GET_PLAYER(gPlayState);
    const auto& p=player->actor.world.pos;
    float y; const float x=p.x+70, z=p.z+45;
    if (!PetFloorAt(x,z,&y)) return;
    Actor* a=Actor_Spawn(&gPlayState->actorCtx,gPlayState,ACTOR_EN_ISHI,x,y,z,0,0,0,0,false);
    if (!a) return;
    a->update=MayaCompanion_Update; a->draw=MayaCompanion_Draw; a->destroy=MayaCompanion_Destroy;
    a->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
    a->uncullZoneForward=4000; a->uncullZoneScale=1500; a->uncullZoneDownward=1500; a->shape.shadowScale=12;
    gMayaCompanion = MayaCompanionState{}; gMayaCompanion.actor=a;
}

