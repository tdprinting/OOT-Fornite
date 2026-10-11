// Included by Companions.inc. Jones uses his own Blender mesh and baked animation clips.
void Jones_Draw(Actor* actor, PlayState* play, const AllyActor& a) {
    namespace J=royale::jones_model;
    namespace G=royale::jones_guns;
    static_assert(J::kPositionScale==G::kPositionScale,"Body and weapon vertices must use the same fixed-point units");
    const auto& gun=G::kGuns[std::min<unsigned>(a.weapon,3)];
    GfxLayer layer(play,GfxLayerId::Characters);
    // Reserve vertices, all part matrices and a conservative full opaque command budget.
    // Only the setup requires translucent headroom; Jones emits no translucent geometry.
    const size_t bytes=sizeof(Vtx)*(std::size(J::kCorners)+gun.count+12)+
        (J::kPartCount+1)*(sizeof(Mtx)+16)+(J::kTriangleCount+gun.count/3+J::kPartCount*4+48)*sizeof(Gfx);
    if(!GfxHasRoom(play,16,bytes)) return;
    Vtx* verts=static_cast<Vtx*>(FrameAlloc(play,sizeof(Vtx)*(std::size(J::kCorners)+gun.count+12)));
    if(!verts) return;
    for(unsigned i=0;i<std::size(J::kCorners);++i) {
        const auto& c=J::kCorners[i];auto& v=verts[i];
        v.v.ob[0]=c.x;v.v.ob[1]=c.y;v.v.ob[2]=c.z;v.v.flag=0;v.v.tc[0]=v.v.tc[1]=0;
        v.v.cn[0]=c.r;v.v.cn[1]=c.g;v.v.cn[2]=c.b;v.v.cn[3]=255;
    }
    Vtx* gunVerts=verts+std::size(J::kCorners);
    for(unsigned i=0;i<gun.count;++i) {
        const auto& c=G::kCorners[gun.first+i];auto& v=gunVerts[i];
        v.v.ob[0]=c.x;v.v.ob[1]=c.y;v.v.ob[2]=c.z;v.v.flag=0;v.v.tc[0]=v.v.tc[1]=0;
        v.v.cn[0]=c.r;v.v.cn[1]=c.g;v.v.cn[2]=c.b;v.v.cn[3]=255;
    }
    Vec3f muzzle={actor->world.pos.x,actor->world.pos.y+90,actor->world.pos.z};
    const bool acting=a.actAge<.7f;
    const int clip=a.hurtAge<.35f?J::kHurt:a.greetAge<.8f?J::kGreet:acting?J::kAttack:a.walkW>.7f?J::kRun:a.walkW>.15f?J::kWalk:
        a.owner==royale::net::kNoPlayer16?J::kInvite:J::kIdle;
    // Use the movement phase for locomotion, so stopped network updates freeze the gait.
    const float time=clip==J::kHurt?a.hurtAge:clip==J::kGreet?a.greetAge:acting?a.actAge:(clip==J::kWalk || clip==J::kRun)?
        a.phase*(J::kClips[clip].duration/6.283185307f):static_cast<float>(ImGui::GetTime());
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_OPA_DISP++,G_LIGHTING|G_CULL_BACK);
    gDPSetCombineMode(POLY_OPA_DISP++,G_CC_SHADE,G_CC_SHADE);
    gSPTexture(POLY_OPA_DISP++,0,0,0,G_TX_RENDERTILE,G_OFF);
    for(unsigned p=0;p<J::kPartCount;++p) {
        const auto& part=J::kParts[p];
        Matrix_Translate(actor->world.pos.x,actor->world.pos.y+a.bob,actor->world.pos.z,MTXMODE_NEW);
        Matrix_RotateY(a.rot*(3.14159265f/32768.0f),MTXMODE_APPLY);
        // Match the adult NPC presentation scale in the existing ally renderer.
        Matrix_Scale(.75f,.75f,.75f,MTXMODE_APPLY);
        // Head expressions and elbows inherit their parents, exactly as in the Blender rig.
        unsigned chain[J::kPartCount],depth=0;int node=static_cast<int>(p);
        while(node>=0 && depth<J::kPartCount) { chain[depth++]=static_cast<unsigned>(node);node=J::kParents[node]; }
        while(depth>0) {
            const unsigned n=chain[--depth];const auto& bind=J::kParts[n];const int parent=J::kParents[n];
            if(parent>=0) { const auto& b=J::kParts[parent];Matrix_Translate(bind.px-b.px,bind.py-b.py,bind.pz-b.pz,MTXMODE_APPLY); }
            else Matrix_Translate(bind.px,bind.py,bind.pz,MTXMODE_APPLY);
            const auto pose=J::Sample(clip,n,time);
            Matrix_RotateY(pose.y,MTXMODE_APPLY);Matrix_RotateZ(pose.z,MTXMODE_APPLY);Matrix_RotateX(pose.x,MTXMODE_APPLY);
        }
        if(p==G::kAttachPart && a.actAge<.15f) Matrix_RotateX(-.07f*std::sin(a.actAge/.15f*3.14159265f),MTXMODE_APPLY);
        Matrix_Scale(J::kPositionScale,J::kPositionScale,J::kPositionScale,MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++,MATRIX_NEWMTX(play->state.gfxCtx),G_MTX_NOPUSH|G_MTX_LOAD|G_MTX_MODELVIEW);
        for(unsigned offset=0;offset<part.count;offset+=30) {
            const unsigned count=std::min(30u,part.count-offset);
            gSPVertex(POLY_OPA_DISP++,reinterpret_cast<uintptr_t>(&verts[part.first+offset]),count,0);
            unsigned v=0;for(;v+5<count;v+=6) gSP2Triangles(POLY_OPA_DISP++,v,v+1,v+2,0,v+3,v+4,v+5,0);
            if(v<count) gSP1Triangle(POLY_OPA_DISP++,v,v+1,v+2,0);
        }
        if(p==G::kAttachPart) {
            Vec3f local={gun.muzzleX/J::kPositionScale,gun.muzzleY/J::kPositionScale,gun.muzzleZ/J::kPositionScale};
            Matrix_MultVec3f(&local,&muzzle);
            for(unsigned offset=0;offset<gun.count;offset+=30) {
                const unsigned count=std::min(30u,gun.count-offset);
                gSPVertex(POLY_OPA_DISP++,reinterpret_cast<uintptr_t>(&gunVerts[offset]),count,0);
                unsigned v=0;for(;v+5<count;v+=6) gSP2Triangles(POLY_OPA_DISP++,v,v+1,v+2,0,v+3,v+4,v+5,0);
                if(v<count) gSP1Triangle(POLY_OPA_DISP++,v,v+1,v+2,0);
            }
            if(a.actAge<.08f) {
                Vtx* flash=gunVerts+gun.count;
                const short dx[4]={-4,0,4,0},dy[4]={0,6,0,-6};
                for(int i=0;i<4;++i) { auto& v=flash[i];v.v.ob[0]=static_cast<s16>((gun.muzzleX+dx[i])/J::kPositionScale);v.v.ob[1]=static_cast<s16>((gun.muzzleY+dy[i])/J::kPositionScale);v.v.ob[2]=static_cast<s16>((gun.muzzleZ+1)/J::kPositionScale);
                    v.v.flag=0;v.v.tc[0]=v.v.tc[1]=0;v.v.cn[0]=255;v.v.cn[1]=225;v.v.cn[2]=95;v.v.cn[3]=255; }
                gSPVertex(POLY_OPA_DISP++,reinterpret_cast<uintptr_t>(flash),4,0);
                gSP2Triangles(POLY_OPA_DISP++,0,1,2,0,0,2,3,0);
            }
        }
    }
    if(a.hasShot && a.actAge<.10f) {
        Vtx* tracer=gunVerts+gun.count+4;
        const Vec3f ends[2]={{muzzle.x-actor->world.pos.x,muzzle.y-actor->world.pos.y,muzzle.z-actor->world.pos.z},
            {a.shotX-actor->world.pos.x,a.shotY-actor->world.pos.y,a.shotZ-actor->world.pos.z}};
        for(int plane=0;plane<2;++plane) for(int i=0;i<4;++i) {
            const auto& e=ends[i/2];auto& v=tracer[plane*4+i];
            v.v.ob[0]=static_cast<s16>(e.x+(plane==0?(i%2?1:-1):0));
            v.v.ob[1]=static_cast<s16>(e.y+(plane==1?(i%2?1:-1):0));v.v.ob[2]=static_cast<s16>(e.z);
            v.v.flag=0;v.v.tc[0]=v.v.tc[1]=0;v.v.cn[0]=255;v.v.cn[1]=225;v.v.cn[2]=140;v.v.cn[3]=255;
        }
        Matrix_Translate(actor->world.pos.x,actor->world.pos.y,actor->world.pos.z,MTXMODE_NEW);
        gSPMatrix(POLY_OPA_DISP++,MATRIX_NEWMTX(play->state.gfxCtx),G_MTX_NOPUSH|G_MTX_LOAD|G_MTX_MODELVIEW);
        gSPVertex(POLY_OPA_DISP++,reinterpret_cast<uintptr_t>(tracer),8,0);
        gSP2Triangles(POLY_OPA_DISP++,0,1,2,0,1,3,2,0);gSP2Triangles(POLY_OPA_DISP++,4,5,6,0,5,7,6,0);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

void JonesShotFx(AllyActor& a,float x,float z) {
    a.shotX=x;a.shotZ=z;a.shotY=a.actor->world.pos.y;
    FloorAt(x,z,&a.shotY);a.shotY+=45;a.hasShot=true;
    SparkBurst(gPlayState,x,a.shotY,z,{255,220,120,255},4,2.0f);
}
