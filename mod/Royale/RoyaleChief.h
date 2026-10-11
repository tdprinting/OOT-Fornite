// Included by Companions.inc. Master Chief: his own low-poly body and Halo-CE-flavoured clips (tools/chief/build_chief.py),
// carrying Jones's private rifles (shared/jones_guns.h). The server picks the rifle; up close he plays a rifle-butt bash instead of a shot.
constexpr float kChiefBashRange = 170.0f;   // visual only: the server's damage is the rifle's either way

void Chief_Draw(Actor* actor, PlayState* play, const AllyActor& a) {
    namespace C=royale::chief_model;
    namespace G=royale::jones_guns;
    static_assert(C::kPositionScale==G::kPositionScale,"Body and weapon vertices must use the same fixed-point units");
    const auto& gun=G::kGuns[std::min<unsigned>(a.weapon,3)];
    GfxLayer layer(play,GfxLayerId::Characters);
    const size_t nv=std::size(C::kCorners)+gun.count+12;
    const size_t bytes=sizeof(Vtx)*nv+(C::kPartCount+2)*(sizeof(Mtx)+16)+(C::kTriangleCount+gun.count/3+C::kPartCount*4+48)*sizeof(Gfx);
    if(!GfxHasRoom(play,16,bytes)) return;
    Vtx* verts=static_cast<Vtx*>(FrameAlloc(play,sizeof(Vtx)*nv));
    if(!verts) return;
    // Shield flare: struck, the armour flashes gold-white for a moment (Halo's shield shimmer, in vertex colours).
    const float flare=a.hurtAge<.3f?1.0f-a.hurtAge/.3f:0.0f;
    for(unsigned i=0;i<std::size(C::kCorners);++i) {
        const auto& c=C::kCorners[i];auto& v=verts[i];
        v.v.ob[0]=c.x;v.v.ob[1]=c.y;v.v.ob[2]=c.z;v.v.flag=0;v.v.tc[0]=v.v.tc[1]=0;
        v.v.cn[0]=static_cast<u8>(c.r+(255-c.r)*flare*.8f);v.v.cn[1]=static_cast<u8>(c.g+(235-c.g)*flare*.8f);
        v.v.cn[2]=static_cast<u8>(c.b+(120-c.b)*flare*.6f);v.v.cn[3]=255;
    }
    Vtx* gunVerts=verts+std::size(C::kCorners);
    for(unsigned i=0;i<gun.count;++i) {
        const auto& c=G::kCorners[gun.first+i];auto& v=gunVerts[i];
        v.v.ob[0]=c.x;v.v.ob[1]=c.y;v.v.ob[2]=c.z;v.v.flag=0;v.v.tc[0]=v.v.tc[1]=0;
        v.v.cn[0]=c.r;v.v.cn[1]=c.g;v.v.cn[2]=c.b;v.v.cn[3]=255;
    }
    Vec3f muzzle={actor->world.pos.x,actor->world.pos.y+90,actor->world.pos.z};
    const bool down=a.hp<=.001f, acting=a.actAge<.6f, bash=a.attackVariant==1;
    const int clip=down?C::kDeath:a.hurtAge<.35f?C::kHurt:a.greetAge<.8f?C::kGreet:acting?(bash?C::kBash:C::kShoot):
        a.walkW>.7f?C::kRun:a.walkW>.15f?C::kWalk:a.owner==royale::net::kNoPlayer16?C::kInvite:C::kIdle;
    const float time=clip==C::kDeath?a.downAge:clip==C::kHurt?a.hurtAge:clip==C::kGreet?a.greetAge:acting?a.actAge:
        (clip==C::kWalk || clip==C::kRun)?a.phase*(C::kClips[clip].duration/6.283185307f):static_cast<float>(ImGui::GetTime());
    const float sink=clip==C::kDeath?-std::min(1.0f,a.downAge/.8f)*55.0f:0.0f;   // the fall: he goes down to the ground
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_OPA_DISP++,G_LIGHTING|G_CULL_BACK);
    gDPSetCombineMode(POLY_OPA_DISP++,G_CC_SHADE,G_CC_SHADE);
    gSPTexture(POLY_OPA_DISP++,0,0,0,G_TX_RENDERTILE,G_OFF);
    for(unsigned p=0;p<C::kPartCount;++p) {
        const auto& part=C::kParts[p];
        Matrix_Translate(actor->world.pos.x,actor->world.pos.y+a.bob*1.4f+sink,actor->world.pos.z,MTXMODE_NEW);   // heavier footfalls than Jones
        Matrix_RotateY(a.rot*(3.14159265f/32768.0f),MTXMODE_APPLY);
        Matrix_Scale(.75f,.75f,.75f,MTXMODE_APPLY);
        unsigned chain[C::kPartCount],depth=0;int node=static_cast<int>(p);
        while(node>=0 && depth<C::kPartCount) { chain[depth++]=static_cast<unsigned>(node);node=C::kParents[node]; }
        while(depth>0) {
            const unsigned n=chain[--depth];const auto& bind=C::kParts[n];const int parent=C::kParents[n];
            if(parent>=0) { const auto& b=C::kParts[parent];Matrix_Translate(bind.px-b.px,bind.py-b.py,bind.pz-b.pz,MTXMODE_APPLY); }
            else Matrix_Translate(bind.px,bind.py,bind.pz,MTXMODE_APPLY);
            const auto pose=C::Sample(clip,n,time);
            Matrix_RotateY(pose.y,MTXMODE_APPLY);Matrix_RotateZ(pose.z,MTXMODE_APPLY);Matrix_RotateX(pose.x,MTXMODE_APPLY);
        }
        Matrix_Scale(C::kPositionScale,C::kPositionScale,C::kPositionScale,MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++,MATRIX_NEWMTX(play->state.gfxCtx),G_MTX_NOPUSH|G_MTX_LOAD|G_MTX_MODELVIEW);
        for(unsigned offset=0;offset<part.count;offset+=30) {
            const unsigned count=std::min(30u,part.count-offset);
            gSPVertex(POLY_OPA_DISP++,reinterpret_cast<uintptr_t>(&verts[part.first+offset]),count,0);
            unsigned v=0;for(;v+5<count;v+=6) gSP2Triangles(POLY_OPA_DISP++,v,v+1,v+2,0,v+3,v+4,v+5,0);
            if(v<count) gSP1Triangle(POLY_OPA_DISP++,v,v+1,v+2,0);
        }
        if(p==C::kGunPart) {   // Jones's rifle, its grip moved into the Spartan's right hand
            Matrix_Translate(C::kGunOffset[0]/C::kPositionScale,C::kGunOffset[1]/C::kPositionScale,C::kGunOffset[2]/C::kPositionScale,MTXMODE_APPLY);
            gSPMatrix(POLY_OPA_DISP++,MATRIX_NEWMTX(play->state.gfxCtx),G_MTX_NOPUSH|G_MTX_LOAD|G_MTX_MODELVIEW);
            Vec3f local={gun.muzzleX/C::kPositionScale,gun.muzzleY/C::kPositionScale,gun.muzzleZ/C::kPositionScale};
            Matrix_MultVec3f(&local,&muzzle);
            for(unsigned offset=0;offset<gun.count;offset+=30) {
                const unsigned count=std::min(30u,gun.count-offset);
                gSPVertex(POLY_OPA_DISP++,reinterpret_cast<uintptr_t>(&gunVerts[offset]),count,0);
                unsigned v=0;for(;v+5<count;v+=6) gSP2Triangles(POLY_OPA_DISP++,v,v+1,v+2,0,v+3,v+4,v+5,0);
                if(v<count) gSP1Triangle(POLY_OPA_DISP++,v,v+1,v+2,0);
            }
            if(a.actAge<.08f && !bash) {
                Vtx* flash=gunVerts+gun.count;
                const short dx[4]={-4,0,4,0},dy[4]={0,6,0,-6};
                for(int i=0;i<4;++i) { auto& v=flash[i];v.v.ob[0]=static_cast<s16>((gun.muzzleX+dx[i])/C::kPositionScale);v.v.ob[1]=static_cast<s16>((gun.muzzleY+dy[i])/C::kPositionScale);v.v.ob[2]=static_cast<s16>((gun.muzzleZ+1)/C::kPositionScale);
                    v.v.flag=0;v.v.tc[0]=v.v.tc[1]=0;v.v.cn[0]=255;v.v.cn[1]=225;v.v.cn[2]=95;v.v.cn[3]=255; }
                gSPVertex(POLY_OPA_DISP++,reinterpret_cast<uintptr_t>(flash),4,0);
                gSP2Triangles(POLY_OPA_DISP++,0,1,2,0,0,2,3,0);
            }
        }
    }
    if(a.hasShot && a.actAge<.10f && !bash) {
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

void UpdateChief(Actor* actor, PlayState*, AllyActor& a, float dt) {
    actor->focus.pos=actor->world.pos;actor->focus.pos.y+=115.0f;
    actor->shape.shadowScale=20.0f;actor->targetMode=3;
    actor->flags|=ACTOR_FLAG_ATTENTION_ENABLED;actor->flags&=~ACTOR_FLAG_HOSTILE;
    const float moving=std::clamp(a.speed/170.0f,0.0f,1.0f);
    a.walkW+=(moving-a.walkW)*.2f;                 // slower to get going: a heavy man
    a.phase+=(.30f+.28f*a.walkW)*a.walkW;
    a.bob=a.walkW*std::fabs(std::sin(a.phase))*3.0f;
    a.greetAge+=dt;a.hurtAge+=dt;
    a.downAge=a.hp<=.001f?a.downAge+dt:0.0f;
}

void ChiefOnSnapshot(AllyActor& a, const royale::net::AllyNet& n) {
    if(n.owner!=a.owner && n.owner!=royale::net::kNoPlayer16) a.greetAge=0;   // hired: one firm nod
    if(n.hp/255.0f<a.hp-.001f && a.hurtAge>.35f) a.hurtAge=0;
    if((n.flags&2) && !a.attackSnapshotActive && a.actAge>.5f) { a.attackVariant=0;PlayJonesSound(2,n.x,n.z,n.weapon);a.actAge=0;a.hasShot=false; }
    a.attackSnapshotActive=(n.flags&2)!=0;
}

void ChiefActionFx(AllyActor& a, float x, float z, uint8_t weapon) {
    a.weapon=weapon;
    const bool bash=std::hypot(x-a.x,z-a.z)<kChiefBashRange;
    if(bash) {   // rifle-butt bash: a thump and sparks where the stock lands
        a.attackVariant=1;a.actAge=0;a.hasShot=false;
        float gy=a.actor->world.pos.y;FloorAt(x,z,&gy);
        SparkBurst(gPlayState,x,gy+50.0f,z,{180,230,255,255},10,4.0f);
        Vec3f at={x,gy+50.0f,z};
        Audio_PlaySoundGeneral(NA_SE_IT_HAMMER_HIT,&at,4,&gSfxDefaultFreqAndVolScale,&gSfxDefaultFreqAndVolScale,&gSfxDefaultReverb);
        return;
    }
    if(a.actAge>.5f) PlayJonesSound(2,a.x,a.z,weapon);
    a.attackVariant=0;a.actAge=0;JonesShotFx(a,x,z);
}
