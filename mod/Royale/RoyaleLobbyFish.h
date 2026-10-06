// Included inside RoyaleMod.cpp after the engine helpers. Kept separate from the match code.
Actor* gReefActor = nullptr;
royale::reef::Aquarium gReef;
int gReefRetry = 0;

bool LobbyReefActive() {
    return gPlayState != nullptr && gSession.Joined() && InWaitingRoom() &&
           gSession.Hud().state == royale::MatchState::Lobby && DebugOn(kDbgLobbyFish);
}

// Triangle soup in batches of ten: stays below the N64's 32-vertex load limit.
void DrawReefMesh(PlayState* play, const royale::reef::ModelVertex* mesh, size_t count,
                  const royale::reef::Fish* fish) {
    if (count == 0 || count % 3 != 0) return;
    Vtx* verts = static_cast<Vtx*>(FrameAlloc(play, count*sizeof(Vtx)));
    if (verts == nullptr) return;
    for (size_t i = 0; i < count; ++i) {
        const auto& v = mesh[i];
        royale::reef::Point p = {v.x, v.y, v.z};
        if (fish != nullptr) {
            p = royale::reef::PoseVertex(p, v.part, fish->swim, fish->cleaning);
            const float cp = std::cos(fish->pitch), sp = std::sin(fish->pitch);
            const float y = cp*p.y+sp*p.z, z = -sp*p.y+cp*p.z;
            const float cy = std::cos(fish->yaw), sy = std::sin(fish->yaw);
            p = {fish->pos.x+(cy*p.x+sy*z)*fish->size,
                 fish->pos.y+y*fish->size, fish->pos.z+(-sy*p.x+cy*z)*fish->size};
        }
        PushVtx4(verts[i], p.x,p.y,p.z, v.r,v.g,v.b,255);
    }
    OPEN_DISPS(play->state.gfxCtx);
    for (size_t first = 0; first < count; first += 30) {
        const int n = static_cast<int>(std::min(size_t(30),count-first));
        gSPVertex(POLY_OPA_DISP++, reinterpret_cast<uintptr_t>(verts+first), n, 0);
        for (int t = 0; t < n; t += 3) gSP1Triangle(POLY_OPA_DISP++,t,t+1,t+2,0);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

void Reef_Draw(Actor* actor, PlayState* play) {
    if (!LobbyReefActive()) return;
    Feat("draw: lobby reef aquarium");
    Mtx* matrix = nullptr;
    // One transform for the entire aquarium; vertex animation stays in tank-local coordinates.
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_OPA_DISP++,G_LIGHTING | G_CULL_BACK);
    gDPSetCombineMode(POLY_OPA_DISP++,G_CC_SHADE,G_CC_SHADE);
    Matrix_Translate(actor->world.pos.x,actor->world.pos.y,actor->world.pos.z,MTXMODE_NEW);
    Matrix_RotateY(actor->shape.rot.y*(3.14159265f/32768.0f),MTXMODE_APPLY);
    Matrix_Scale(0.25f,0.25f,0.25f,MTXMODE_APPLY);
    matrix = MATRIX_NEWMTX(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++,matrix,G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    CLOSE_DISPS(play->state.gfxCtx);
    namespace R = royale::reef;
    DrawReefMesh(play,R::kAquarium,sizeof(R::kAquarium)/sizeof(R::kAquarium[0]),nullptr);
    for (const auto& f : gReef.fish) {
        FrameInterpolation_RecordOpenChild(&f,0);
        if (f.species == 0) DrawReefMesh(play,R::kClownfish,sizeof(R::kClownfish)/sizeof(R::kClownfish[0]),&f);
        else DrawReefMesh(play,R::kCleanerWrasse,sizeof(R::kCleanerWrasse)/sizeof(R::kCleanerWrasse[0]),&f);
        FrameInterpolation_RecordCloseChild();
    }
    // Pale blue water/glass panes. Use the translucent list; don't interfere with engine water or swimming.
    Vtx* glass = static_cast<Vtx*>(FrameAlloc(play,20*sizeof(Vtx)));
    if (glass == nullptr) return;
    const float points[20][3] = {
        {-160,34,-86},{160,34,-86},{160,183,-86},{-160,183,-86},
        {-160,34,86},{-160,183,86},{160,183,86},{160,34,86},
        {-160,34,-86},{-160,183,-86},{-160,183,86},{-160,34,86},
        {160,34,-86},{160,34,86},{160,183,86},{160,183,-86},
        {-160,181,-86},{160,181,-86},{160,181,86},{-160,181,86}};
    for (int i=0;i<20;++i) PushVtx4(glass[i],points[i][0],points[i][1],points[i][2],100,196,208,i>=16 ? 64 : 24);
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_XLU_DISP++,G_LIGHTING | G_CULL_BACK);
    gDPSetCombineMode(POLY_XLU_DISP++,G_CC_SHADE,G_CC_SHADE);
    gSPMatrix(POLY_XLU_DISP++,matrix,G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPVertex(POLY_XLU_DISP++,reinterpret_cast<uintptr_t>(glass),20,0);
    for (int q=0;q<20;q+=4) gSP2Triangles(POLY_XLU_DISP++,q,q+1,q+2,0,q,q+2,q+3,0);
    CLOSE_DISPS(play->state.gfxCtx);
}

void Reef_Init(Actor* actor, PlayState*) { actor->shape.shadowScale = 0; }
void Reef_Destroy(Actor* actor, PlayState*) { if (gReefActor == actor) gReefActor = nullptr; }
void Reef_Update(Actor* actor, PlayState* play) {
    if (!LobbyReefActive()) { Actor_Kill(actor); return; }
    Feat("lobby fish swimming");
    const Player* player = GET_PLAYER(play);
    const float yaw = actor->shape.rot.y*(3.14159265f/32768.0f);
    const float dx = player->actor.world.pos.x-actor->world.pos.x, dz = player->actor.world.pos.z-actor->world.pos.z;
    gReef.Step(1.0f/royale::kTickHz,{std::cos(yaw)*dx-std::sin(yaw)*dz,
                                    player->actor.world.pos.y-actor->world.pos.y,
                                    std::sin(yaw)*dx+std::cos(yaw)*dz});
}

int ReefActorId() {
    static int id = -1;
    if (id < 0) {
        ActorDBInit init;
        init.name = "Royale_LobbyReef";
        init.desc = "Waiting-room reef aquarium";
        init.category = ACTORCAT_PROP;
        init.flags = ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
        init.objectId = OBJECT_GAMEPLAY_KEEP;
        init.instanceSize = sizeof(Actor);
        init.init = Reef_Init; init.destroy = Reef_Destroy; init.update = Reef_Update; init.draw = Reef_Draw;
        id = ActorDB::Instance->AddEntry(init).entry.id;
    }
    return id;
}

// Avoid doors, stairs and walls by testing the complete footprint in the actual waiting-room collision.
bool FindReefSpot(Player* player, Vec3f* spot) {
    const float angle = player->actor.shape.rot.y*(3.14159265f/32768.0f);
    const float sy = std::sin(angle), cy = std::cos(angle);
    for (float forward : {450.0f,650.0f,850.0f}) for (float side : {-250.0f,250.0f,0.0f}) {
        Vec3f center = {player->actor.world.pos.x+sy*forward+cy*side,0,player->actor.world.pos.z+cy*forward-sy*side};
        center.y = GroundY(gPlayState,center.x,center.z,BGCHECK_Y_MIN);
        if (center.y <= BGCHECK_Y_MIN+1 || std::fabs(center.y-player->actor.world.pos.y)>40) continue;
        bool clear = true;
        for (float x : {-180.0f,180.0f}) for (float z : {-105.0f,105.0f}) {
            Vec3f corner = {center.x+cy*x+sy*z,center.y+100,center.z-sy*x+cy*z};
            const float floor = GroundY(gPlayState,corner.x,corner.z,BGCHECK_Y_MIN);
            if (std::fabs(floor-center.y)>6) { clear=false; break; }
            Vec3f from = {player->actor.world.pos.x,center.y+100,player->actor.world.pos.z}, hit;
            CollisionPoly* poly = nullptr; s32 bgId = 0;
            if (BgCheck_EntityLineTest1(&gPlayState->colCtx,&from,&corner,&hit,&poly,true,false,false,true,&bgId)) { clear=false; break; }
        }
        if (clear) { *spot = center; return true; }
    }
    return false;
}

void ReconcileLobbyReef() {
    if (!LobbyReefActive()) {
        if (gReefActor != nullptr) { Actor_Kill(gReefActor); gReefActor = nullptr; }
        gReefRetry = 0;
        return;
    }
    if (gReefActor != nullptr) return;
    if (gReefRetry > 0) { --gReefRetry; return; }
    gReefRetry = royale::kTickHz;
    Player* player = GET_PLAYER(gPlayState);
    Vec3f pos;
    if (!FindReefSpot(player,&pos)) return;
    gReef.Reset();
    gReefActor = Actor_Spawn(&gPlayState->actorCtx,gPlayState,static_cast<s16>(ReefActorId()),
                            pos.x,pos.y,pos.z,0,player->actor.shape.rot.y,0,0,false);
}

void ForgetLobbyReef() { gReefActor = nullptr; gReefRetry = 0; gReef.Reset(); }
