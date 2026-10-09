// Original Hollowbell model. Included after BossActor and shared boss helpers.
void PlayMorrowSound(int clip, float x, float z);
void Morrow_Update(PlayState* play, BossActor& b) {
    b.hurtAge += 1.0f/royale::kTickHz;
    if(b.hp<b.lastHp-.001f) b.hurtAge=0;
    b.lastHp=b.hp;
    if(ModeOf(b)==BM::Chase && !b.morrowAlerted) { PlayMorrowSound(0,b.x,b.z);b.morrowAlerted=true; }
    if(b.modeFresh) {
        if(ModeOf(b)==BM::Slam || ModeOf(b)==BM::Summon) PlayMorrowSound(3,b.x,b.z);
        else if(ModeOf(b)==BM::Stunned) PlayMorrowSound(4,b.x,b.z);
    }
    // A swing snapshot begins at windup; age crosses the impact once.
    if(b.smashAge<.04f) PlayMorrowSound(1,b.x,b.z);
    if(b.smashAge>=.85f && b.smashAge<.85f+1.0f/royale::kTickHz) PlayMorrowSound(2,b.x,b.z);
    if(ModeOf(b)==BM::Slam || ModeOf(b)==BM::Summon) {
        const float age=b.modeAge,dt=1.0f/royale::kTickHz;
        const float times[2]={ModeOf(b)==BM::Slam?.4f:.5f,ModeOf(b)==BM::Slam?.8f:1.0f};
        for(float t:times) if(age>=t && age<t+dt) PlayMorrowSound(3,b.x,b.z);
    }
}
void Morrow_Draw(Actor* actor, PlayState* play, const BossActor& b) {
    namespace A=royale::morrow_model;
    GfxLayer layer(play,GfxLayerId::Characters);
    Vtx* verts=static_cast<Vtx*>(FrameAlloc(play,sizeof(Vtx)*std::size(A::kCorners)));
    if(!verts) return;
    for(unsigned i=0;i<std::size(A::kCorners);++i) {
        const auto& c=A::kCorners[i];auto& v=verts[i];
        v.v.ob[0]=c.x;v.v.ob[1]=c.y;v.v.ob[2]=c.z;v.v.flag=0;v.v.tc[0]=v.v.tc[1]=0;
        v.v.cn[0]=c.r;v.v.cn[1]=c.g;v.v.cn[2]=c.b;v.v.cn[3]=255;
        if(b.hurtAge<.12f) {v.v.cn[0]=255;v.v.cn[1]/=2;v.v.cn[2]/=2;}
        if(c.r==255 && c.g==186 && c.b==87 && b.hp<=.5f) {v.v.cn[1]=220;v.v.cn[2]=110;}
    }
    const float t=BossTime(play),yaw=b.rot*(3.14159265f/32768.0f);
    const float walk=b.moved>.35f?std::sin(t*7)*.32f:0;
    float swing=0;
    if(b.smashAge<1.9f) swing=b.smashAge<.65f?-1.1f*(b.smashAge/.65f):b.smashAge<.85f?-1.1f+2.2f*((b.smashAge-.65f)/.2f):1.1f*std::max(0.0f,1-(b.smashAge-.85f)/1.05f);
    const bool road=ModeOf(b)==BM::Slam, toll=ModeOf(b)==BM::Summon;
    const float special=road?-.9f*std::sin(std::min(b.modeAge/2.0f,1.0f)*3.14159265f):toll?-.6f:0;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_OPA_DISP++,G_LIGHTING|G_CULL_BACK);
    gDPSetCombineMode(POLY_OPA_DISP++,G_CC_SHADE,G_CC_SHADE);
    gSPTexture(POLY_OPA_DISP++,0,0,0,G_TX_RENDERTILE,G_OFF);
    for(unsigned n=0;n<std::size(A::kParts);++n) {
        const auto& part=A::kParts[n];
        Matrix_Translate(actor->world.pos.x,actor->world.pos.y+std::sin(t*1.5f)*1.2f,actor->world.pos.z,MTXMODE_NEW);
        Matrix_RotateY(yaw,MTXMODE_APPLY);
        if(ModeOf(b)==BM::Stunned) Matrix_RotateX(.17f,MTXMODE_APPLY);
        Matrix_Translate(part.px,part.py,part.pz,MTXMODE_APPLY);
        float rx=0;
        if(n==1) rx=walk;else if(n==2) rx=-walk;
        else if(n==3) rx=toll?-.65f:-walk*.5f;
        else if(n==4 || n==8) rx=(swing!=0?swing:special)-walk*.4f;
        else if(n==5) rx=ModeOf(b)==BM::Patrol?std::sin(t*2.6f)*.08f:0;
        else if(n==7) rx=walk*.18f;
        Matrix_RotateX(rx,MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++,MATRIX_NEWMTX(play->state.gfxCtx),G_MTX_NOPUSH|G_MTX_LOAD|G_MTX_MODELVIEW);
        for(unsigned offset=0;offset<part.count;offset+=30) {
            const unsigned count=std::min(30u,part.count-offset);
            gSPVertex(POLY_OPA_DISP++,reinterpret_cast<uintptr_t>(&verts[part.first+offset]),count,0);
            unsigned v=0;for(;v+5<count;v+=6) gSP2Triangles(POLY_OPA_DISP++,v,v+1,v+2,0,v+3,v+4,v+5,0);
            if(v<count) gSP1Triangle(POLY_OPA_DISP++,v,v+1,v+2,0);
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);
}
