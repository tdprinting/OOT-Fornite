// Included once inside RoyaleMod.cpp, after RoyaleBosses.h.
void DrawBokoblinModel(PlayState* play, float x, float y, float z, float yaw, float scale, const royale::bokoblin::Pose& pose, int face) {
    GfxLayer layer(play, GfxLayerId::Characters);   // its thousands of vertices and commands go in the layer pool, not the game's buffer
    namespace A = royale::bokoblin;
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
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);         // the light is already in the vertex colours; it is seen from all sides
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIDECALA, G_CC_PASS2);     // the texture times the vertex colour
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



void PlayBokoblinSound(int clip, float x, float z);
struct BokoblinActor {
    Actor* actor = nullptr;
    ActorFunc originalDestroy = nullptr;
    royale::net::HelperNet net;
    royale::bokoblin::Animator animation;
    float age = 0, rockAge = 0;
    uint8_t action = 0;
};
std::unordered_map<uint32_t, BokoblinActor> gBokoblins;
std::unordered_map<const Actor*, uint32_t> gBokoblinOf;

int BokoblinClip(royale::BokoMode mode) {
    namespace B = royale::bokoblin;
    switch (mode) {
        case royale::BokoMode::Walk: return B::kWalk;
        case royale::BokoMode::Alert: return B::kAlert;
        case royale::BokoMode::Swing: return B::kSwing;
        case royale::BokoMode::Jump: return B::kJump;
        case royale::BokoMode::Throw: return B::kThrow;
        case royale::BokoMode::Recover: return B::kRecover;
        case royale::BokoMode::Dance: return B::kDance;
        case royale::BokoMode::Hurt: return B::kHurt;
        case royale::BokoMode::Flee: return B::kFlee;
        case royale::BokoMode::Dead: return B::kDead;
        default: return B::kIdle;
    }
}
int BokoblinSound(royale::BokoMode mode) {
    switch (mode) {
        case royale::BokoMode::Alert: return 0;
        case royale::BokoMode::Swing: case royale::BokoMode::Jump: return 1;
        case royale::BokoMode::Throw: return 2;
        case royale::BokoMode::Recover: return 3;
        case royale::BokoMode::Dance: return 4;
        case royale::BokoMode::Hurt: return 5;
        case royale::BokoMode::Flee: return 6;
        case royale::BokoMode::Dead: return 7;
        default: return -1;
    }
}
void Bokoblin_Update(Actor* actor, PlayState*) {
    auto id=gBokoblinOf.find(actor); if (id==gBokoblinOf.end()) return;
    auto it=gBokoblins.find(id->second); if (it==gBokoblins.end()) return;
    auto& b=it->second;
    const float dt=1.0f/royale::kTickHz;
    b.age+=dt; b.rockAge+=dt; b.animation.Update(dt);
    actor->world.pos.x+=(b.net.x-actor->world.pos.x)*0.6f;
    actor->world.pos.z+=(b.net.z-actor->world.pos.z)*0.6f;
    float floor=0;
    if (FloorAt(actor->world.pos.x,actor->world.pos.z,&floor)) actor->world.pos.y=floor+b.net.y;
    actor->shape.rot.y=b.net.rot;
    actor->focus.pos=actor->world.pos; actor->focus.pos.y+=45.0f;
    actor->targetMode=3;
    if (b.net.hp>0) actor->flags|=ACTOR_FLAG_ATTENTION_ENABLED|ACTOR_FLAG_HOSTILE;
    else actor->flags&=~(ACTOR_FLAG_ATTENTION_ENABLED|ACTOR_FLAG_HOSTILE);
}
void Bokoblin_Draw(Actor* actor, PlayState* play) {
    auto id=gBokoblinOf.find(actor); if (id==gBokoblinOf.end()) return;
    auto it=gBokoblins.find(id->second); if (it==gBokoblins.end()) return;
    const auto& b=it->second;
    const auto mode=static_cast<royale::BokoMode>(b.net.mode);
    royale::bokoblin::Pose pose; b.animation.Evaluate(pose);
    const int face=mode==royale::BokoMode::Hurt || mode==royale::BokoMode::Dead ? royale::bokoblin::kFaceHurt :
        mode==royale::BokoMode::Alert || mode==royale::BokoMode::Flee ? royale::bokoblin::kFaceAlarm : royale::bokoblin::kFaceGrin;
    DrawBokoblinModel(play,actor->world.pos.x,actor->world.pos.y,actor->world.pos.z,b.net.rot*(3.14159265f/32768.0f),0.72f,pose,face);
    // The stone visibly travels to the position committed by the server; it never homes in after release.
    if (b.net.rock && b.rockAge<=0.8f) {
        const float t=std::clamp(b.rockAge/0.8f,0.0f,1.0f);
        const float x=b.net.fromX+(b.net.toX-b.net.fromX)*t, z=b.net.fromZ+(b.net.toZ-b.net.fromZ)*t;
        float fromY=actor->world.pos.y-b.net.y,toY=fromY;
        FloorAt(b.net.fromX,b.net.fromZ,&fromY); FloorAt(b.net.toX,b.net.toZ,&toY);
        const float y=fromY+(toY-fromY)*t+45.0f*(1-t)+95.0f*std::sin(t*3.14159265f);
        const GpuMesh* rock=GpuMeshFor(royale::MeshKind::Rock,0);
        if (rock) {
            GfxLayer layer(play,GfxLayerId::Characters);
            OPEN_DISPS(play->state.gfxCtx);
            Gfx_SetupDL_25Opa(play->state.gfxCtx);
            gSPClearGeometryMode(POLY_OPA_DISP++,G_LIGHTING);
            gDPSetCombineMode(POLY_OPA_DISP++,G_CC_SHADE,G_CC_SHADE);
            Matrix_Translate(x,y,z,MTXMODE_NEW);Matrix_RotateX(t*8.0f,MTXMODE_APPLY);Matrix_Scale(.23f,.23f,.23f,MTXMODE_APPLY);
            gSPMatrix(POLY_OPA_DISP++,MATRIX_NEWMTX(play->state.gfxCtx),G_MTX_NOPUSH|G_MTX_LOAD|G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++,const_cast<Gfx*>(rock->dl.data()));
            CLOSE_DISPS(play->state.gfxCtx);
        }
    }
}
void Bokoblin_Destroy(Actor* actor, PlayState* play) {
    ActorFunc original=nullptr;
    auto id=gBokoblinOf.find(actor);
    if (id!=gBokoblinOf.end()) {
        auto it=gBokoblins.find(id->second);
        if (it!=gBokoblins.end()) { original=it->second.originalDestroy;gBokoblins.erase(it); }
        gBokoblinOf.erase(id);
    }
    if (original) original(actor,play);
}
void ReconcileBokoblins(const royale::HudState& hud) {
    const bool show=gSession.Joined() && InField() && gSession.Client() &&
        (hud.state==royale::MatchState::Drop || hud.state==royale::MatchState::InMatch || hud.state==royale::MatchState::Ending);
    std::unordered_set<uint32_t> wanted;
    if (show) for (const auto& n:gSession.Client()->Helpers()) {
        wanted.insert(n.Id());
        auto it=gBokoblins.find(n.Id());
        if (it==gBokoblins.end()) {
            float y=0;if (!FloorAt(n.x,n.z,&y)) continue;
            Actor* actor=Actor_Spawn(&gPlayState->actorCtx,gPlayState,ACTOR_EN_ISHI,n.x,y,n.z,0,n.rot,0,0,false);
            if (!actor) continue;
            BokoblinActor b;b.actor=actor;b.originalDestroy=actor->destroy;b.net=n;b.action=n.action;b.age=n.age;b.rockAge=n.rockAge;
            b.animation.Play(BokoblinClip(static_cast<royale::BokoMode>(n.mode)),0);b.animation.time=n.age;
            gBokoblins.emplace(n.Id(),b);gBokoblinOf.emplace(actor,n.Id());
            actor->update=Bokoblin_Update;actor->draw=Bokoblin_Draw;actor->destroy=Bokoblin_Destroy;
            actor->flags|=ACTOR_FLAG_UPDATE_CULLING_DISABLED;actor->uncullZoneForward=3200;actor->uncullZoneScale=300;actor->uncullZoneDownward=600;
            actor->shape.shadowScale=18.0f;
        } else {
            auto& b=it->second;
            if (n.action!=b.action || n.mode!=b.net.mode) {
                const auto mode=static_cast<royale::BokoMode>(n.mode);
                b.animation.Play(BokoblinClip(mode),.08f,true);b.action=n.action;
                if (n.age<0.3f) PlayBokoblinSound(BokoblinSound(mode),n.x,n.z);
            }
            b.net=n;b.age=n.age;b.rockAge=n.rockAge;b.animation.time=n.age;
        }
    }
    // Kill first; the engine invokes the saved rock destructor later to free its collider.
    for (auto& entry:gBokoblins) if (!wanted.count(entry.first) && entry.second.actor) Actor_Kill(entry.second.actor);
}
