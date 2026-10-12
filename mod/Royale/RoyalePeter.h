// Included by Companions.inc. Peter uses his own Blender mesh and baked clips (tools/peter, shared/peter_model.h).
// His special: standing idle for a while, his phone rings. Joe: "Hey Peter!" They chat (subtitles + original
// synthesised voices, shared/peter_sounds.h); moving, fighting or getting hurt hangs up. Cosmetic and client-local.
// He fights with a rotation of cartoon moves (haymaker, chicken-fight flurry, belly bump, roadhouse kick), cheers
// "Freakin' sweet!" when hired, grabs his knee when hurt and does a chicken-flap dance when his owner emotes.
// The server's damage is the same for every move; only the look, sound and quip change.
// Dedicated to Cynthia, who asked for Peter as a joke and got a real game asset.

// ---- your own voice clips (personal use; they live on your device and are never committed) ----
// Drop 16-bit .wav files into <game data folder>/voices/peter/ and they replace the built-in synthesised sounds:
//   greet hire attack hurt belly chicken kick cheer .wav   one-shot cues (names as in kPeterCueNames)
//   call.wav                                             the whole phone call (ring to hang-up, 26.6 s; subtitles keep their timing)
//   hey_peter.wav or line01.wav ... line10.wav           single call lines (assets/npcs/peter/audio/CUSTOM-VOICES.md lists them);
//                                                        the built-in line is muted while yours plays
// Each file is read once, the first time it is wanted, and converted to mono 22050 Hz.
inline constexpr const char* kPeterCueNames[]={"greet","hire","attack","hurt","belly","chicken","kick","cheer"};
static_assert(std::size(kPeterCueNames)==royale::peter_snd::kCall,"one custom-clip name per one-shot cue");
constexpr int kPeterCustomRate=22050;
std::shared_ptr<const std::vector<int16_t>> PeterCustomClip(const std::string& name) {
    static std::unordered_map<std::string,std::shared_ptr<const std::vector<int16_t>>> cache;
    if(auto it=cache.find(name);it!=cache.end()) return it->second;
    std::shared_ptr<const std::vector<int16_t>> pcm;
    const std::filesystem::path file=std::filesystem::path(Ship::Context::GetPathRelativeToAppDirectory("voices"))/"peter"/(name+".wav");
    std::error_code ec;
    SDL_AudioSpec spec={};Uint8* buf=nullptr;Uint32 len=0;
    if(std::filesystem::is_regular_file(file,ec) && SafeLoadWav(file.string(),&spec,&buf,&len)!=nullptr) {
        SDL_AudioCVT cvt;
        if(SDL_BuildAudioCVT(&cvt,spec.format,spec.channels,spec.freq,AUDIO_S16SYS,1,kPeterCustomRate)>=0) {
            std::vector<Uint8> work(static_cast<size_t>(len)*(cvt.len_mult>0?cvt.len_mult:1));
            std::memcpy(work.data(),buf,len);cvt.buf=work.data();cvt.len=static_cast<int>(len);
            if(!cvt.needed || SDL_ConvertAudio(&cvt)==0) {
                const size_t bytes=cvt.needed?static_cast<size_t>(cvt.len_cvt):len;
                const auto* s=reinterpret_cast<const int16_t*>(work.data());
                if(bytes>=2) pcm=std::make_shared<const std::vector<int16_t>>(s,s+bytes/2);
            }
        }
        SDL_FreeWAV(buf);
    }
    cache[name]=pcm;
    return pcm;
}
std::shared_ptr<const std::vector<int16_t>> PeterCustomLine(int line) {
    if(line==0) if(auto hey=PeterCustomClip("hey_peter")) return hey;
    char name[16];std::snprintf(name,sizeof name,"line%02d",line+1);
    return PeterCustomClip(name);
}
// A cue: the player's own clip if there is one, else the built-in.
void PeterSay(int clip, float x, float z) {
    auto custom=clip>=0 && clip<static_cast<int>(std::size(kPeterCueNames))?PeterCustomClip(kPeterCueNames[clip]):nullptr;
    PlayPeterSound(clip,x,z,custom,kPeterCustomRate);
}

struct PeterMove { int clip,sound,quip; };
inline constexpr PeterMove kPeterMoves[] = {
    {royale::peter_model::kAttack,royale::peter_snd::kAttack,royale::peter_snd::kQuipHaymaker},
    {royale::peter_model::kChicken,royale::peter_snd::kChicken,royale::peter_snd::kQuipChicken},
    {royale::peter_model::kBelly,royale::peter_snd::kBelly,royale::peter_snd::kQuipBelly},
    {royale::peter_model::kKick,royale::peter_snd::kKick,royale::peter_snd::kQuipRoadhouse},
};

void PeterQuip(AllyActor& a, int quip) { a.quip=quip;a.quipAge=0; }

// One of his moves, in turn; a quip on about half of them so the bubbles stay a treat.
void PeterAttack(AllyActor& a) {
    if(a.actAge<.5f) return;
    a.attackVariant=static_cast<uint8_t>(a.attackCount++%std::size(kPeterMoves));
    const auto& move=kPeterMoves[a.attackVariant];
    a.actAge=0;PeterSay(move.sound,a.x,a.z);
    if(a.attackCount%2==1) PeterQuip(a,move.quip);
}

// Snapshot reactions: hired, hurt, swinging.
void PeterOnSnapshot(AllyActor& a, const royale::net::AllyNet& n) {
    namespace P=royale::peter_snd;
    if(n.owner!=a.owner && n.owner!=royale::net::kNoPlayer16) {
        const bool cynthia=a.hireCount++%2==0;   // his first hire (while in view) always credits Cynthia
        a.cheerAge=0;PeterSay(cynthia?P::kHire:P::kCheer,n.x,n.z);
        PeterQuip(a,cynthia?P::kQuipCynthia:P::kQuipSweet);
    }
    if(n.hp/255.0f<a.hp-.001f && a.hurtAge>.5f) { a.hurtAge=0;PeterSay(P::kHurt,n.x,n.z);PeterQuip(a,P::kQuipHurt); }
    if((n.flags&2) && !a.attackSnapshotActive) PeterAttack(a);
    a.attackSnapshotActive=(n.flags&2)!=0;
}

// Where a move lands: the haymaker and kick thump like the Goron's fist, the belly bump shoves up a cloud of dust,
// the chicken fight leaves a flurry of feathers.
void PeterActionFx(AllyActor& a, float x, float z) {
    PeterAttack(a);
    float gy=a.actor->world.pos.y;FloorAt(x,z,&gy);
    switch(a.attackVariant) {
        case 1: SparkBurst(gPlayState,x,gy+55.0f,z,{255,255,245,255},26,5.0f);SparkBurst(gPlayState,x,gy+45.0f,z,{255,210,90,255},10,3.0f);break;
        case 2: AllyDust(gPlayState,x,gy,z,9,55.0f,110);SparkBurst(gPlayState,x,gy+40.0f,z,{255,235,200,255},12,4.0f);break;
        default: SparkBurst(gPlayState,x,gy+40.0f,z,{255,200,120,255},20,6.0f);AllyDust(gPlayState,x,gy,z,4,30.0f,70);break;
    }
    SfxAt(NA_SE_IT_HAMMER_HIT,x,gy+40.0f,z);
}

constexpr float kPeterIdleBeforeCall=14.0f;   // seconds standing still before the phone can ring
constexpr double kPeterCallEvery=60.0;        // after a finished call
constexpr double kPeterCallRetry=25.0;        // after he had to hang up

float PeterCallVolume(const AllyActor& a) {
    if(!gPlayState) return 0;
    Player* player=GET_PLAYER(gPlayState);if(!player) return 0;
    const float d=std::hypot(player->actor.world.pos.x-a.x,player->actor.world.pos.z-a.z);
    return GameVolume(false)*.55f*std::clamp(1.0f-d/1400.0f,0.0f,1.0f);
}

void PeterHangUp(AllyActor& a, double retryAt) {
    if(a.callAge<0) return;
    a.callAge=-1;a.nextCallAt=retryAt;StopVoice(kVoicePeterCall);
    // A custom line clip shares his cue voice: silence it, unless the cue that interrupted the call just took that voice.
    if(!(a.hurtAge<.2f || a.actAge<.2f || a.cheerAge<.2f)) StopVoice(kVoicePeter);
}

void UpdatePeter(Actor* actor, PlayState* play, AllyActor& a, float dt) {
    namespace P=royale::peter_snd;
    actor->focus.pos=actor->world.pos;actor->focus.pos.y+=120.0f;
    actor->shape.shadowScale=22.0f;actor->targetMode=3;
    actor->flags|=ACTOR_FLAG_ATTENTION_ENABLED;actor->flags&=~ACTOR_FLAG_HOSTILE;
    const float moving=std::clamp(a.speed/170.0f,0.0f,1.0f);
    a.walkW+=(moving-a.walkW)*.25f;
    a.phase+=(.33f+.3f*a.walkW)*a.walkW;
    a.bob=a.walkW*std::fabs(std::sin(a.phase))*2.5f;
    a.greetAge+=dt;a.hurtAge+=dt;a.cheerAge+=dt;a.quipAge+=dt;
    const double now=ImGui::GetTime();
    if(a.nextCallAt==0) a.nextCallAt=now+kPeterIdleBeforeCall;
    const bool busy=a.walkW>.12f || a.actAge<1.0f || a.hurtAge<1.0f || a.cheerAge<1.2f || AllyOwnerCelebrates(a.owner);
    a.stillFor=busy?0.0f:a.stillFor+dt;
    Player* player=GET_PLAYER(play);
    const float playerDist=player?std::hypot(player->actor.world.pos.x-a.x,player->actor.world.pos.z-a.z):1e9f;
    if(a.callAge>=0) {
        if(a.walkW>.3f || a.actAge<.1f || a.hurtAge<.1f || a.cheerAge<.1f) { PeterHangUp(a,now+kPeterCallRetry); return; }
        const float before=a.callAge;a.callAge+=dt;
        if(a.callAge>=P::kCallDuration) { a.callAge=-1;a.nextCallAt=now+kPeterCallEvery;return; }
        // The player's own line clips start on cue; the built-in line under them is muted.
        bool duck=false;
        if(!a.customCall) for(int i=0;i<static_cast<int>(std::size(P::kLines));++i) {
            const auto& line=P::kLines[i];auto mine=PeterCustomLine(i);
            if(!mine) continue;
            if(before<line.start && a.callAge>=line.start) StartVoice(kVoicePeter,mine,false,kPeterCustomRate,false,PeterCallVolume(a));
            if(a.callAge>=line.start && a.callAge<=line.end) { duck=true;SetVoiceVolume(kVoicePeter,PeterCallVolume(a)); }
        }
        SetVoiceVolume(kVoicePeterCall,duck?0.0f:PeterCallVolume(a));
        return;
    }
    // Ring only when someone is close enough to enjoy it.
    if(a.stillFor>=kPeterIdleBeforeCall && now>=a.nextCallAt && playerDist<1400.0f) {
        a.callAge=0;a.stillFor=0;
        static auto call=std::make_shared<const std::vector<int16_t>>(P::kClips[P::kCall].data,P::kClips[P::kCall].data+P::kClips[P::kCall].count);
        auto custom=PeterCustomClip("call");a.customCall=custom!=nullptr;
        if(custom) StartVoice(kVoicePeterCall,custom,false,kPeterCustomRate,false,PeterCallVolume(a));
        else StartVoice(kVoicePeterCall,call,false,P::kClips[P::kCall].rate,false,PeterCallVolume(a));
        return;
    }
    if(a.owner==royale::net::kNoPlayer16 && playerDist<royale::kHireRange && now>=a.nextGreetingAt) {
        a.greetAge=0;a.nextGreetingAt=now+12.0;PeterSay(P::kGreet,a.x,a.z);PeterQuip(a,P::kQuipLaugh);
    }
}

void Peter_Draw(Actor* actor, PlayState* play, const AllyActor& a) {
    namespace P=royale::peter_model;
    GfxLayer layer(play,GfxLayerId::Characters);
    const size_t bytes=sizeof(Vtx)*std::size(P::kCorners)+(P::kPartCount+1)*(sizeof(Mtx)+16)+(P::kTriangleCount+P::kPartCount*4+48)*sizeof(Gfx);
    if(!GfxHasRoom(play,16,bytes)) return;
    Vtx* verts=static_cast<Vtx*>(FrameAlloc(play,sizeof(Vtx)*std::size(P::kCorners)));
    if(!verts) return;
    for(unsigned i=0;i<std::size(P::kCorners);++i) {
        const auto& c=P::kCorners[i];auto& v=verts[i];
        v.v.ob[0]=c.x;v.v.ob[1]=c.y;v.v.ob[2]=c.z;v.v.flag=0;v.v.tc[0]=v.v.tc[1]=0;
        v.v.cn[0]=c.r;v.v.cn[1]=c.g;v.v.cn[2]=c.b;v.v.cn[3]=255;
    }
    const int move=kPeterMoves[a.attackVariant%std::size(kPeterMoves)].clip;
    const bool acting=a.actAge<P::kClips[move].duration,calling=a.callAge>=0;
    const bool dancing=a.owner!=royale::net::kNoPlayer16 && AllyOwnerCelebrates(a.owner);
    const int clip=a.hurtAge<.5f?P::kHurt:a.cheerAge<1.2f?P::kCheer:a.greetAge<1.0f?P::kGreet:acting?move:a.walkW>.7f?P::kRun:a.walkW>.15f?P::kWalk:
        calling?P::kPhone:dancing?P::kDance:a.owner==royale::net::kNoPlayer16?P::kInvite:P::kIdle;
    const float time=clip==P::kHurt?a.hurtAge:clip==P::kCheer?a.cheerAge:clip==P::kGreet?a.greetAge:acting?a.actAge:clip==P::kPhone?a.callAge:
        (clip==P::kWalk || clip==P::kRun)?a.phase*(P::kClips[clip].duration/6.283185307f):static_cast<float>(ImGui::GetTime());
    const bool phoneOut=clip==P::kPhone && a.callAge>=P::kPhoneShowFrom && a.callAge<=P::kPhoneShowUntil;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_OPA_DISP++,G_LIGHTING|G_CULL_BACK);
    gDPSetCombineMode(POLY_OPA_DISP++,G_CC_SHADE,G_CC_SHADE);
    gSPTexture(POLY_OPA_DISP++,0,0,0,G_TX_RENDERTILE,G_OFF);
    for(unsigned p=0;p<P::kPartCount;++p) {
        if(p==P::kPhonePart && !phoneOut) continue;
        const auto& part=P::kParts[p];
        Matrix_Translate(actor->world.pos.x,actor->world.pos.y+a.bob,actor->world.pos.z,MTXMODE_NEW);
        Matrix_RotateY(a.rot*(3.14159265f/32768.0f),MTXMODE_APPLY);
        Matrix_Scale(.75f,.75f,.75f,MTXMODE_APPLY);   // the adult NPC scale used by the other allies
        // Head, jaw, brows, forearms and phone inherit their parents, exactly as in the Blender rig.
        unsigned chain[P::kPartCount],depth=0;int node=static_cast<int>(p);
        while(node>=0 && depth<P::kPartCount) { chain[depth++]=static_cast<unsigned>(node);node=P::kParents[node]; }
        while(depth>0) {
            const unsigned n=chain[--depth];const auto& bind=P::kParts[n];const int parent=P::kParents[n];
            if(parent>=0) { const auto& b=P::kParts[parent];Matrix_Translate(bind.px-b.px,bind.py-b.py,bind.pz-b.pz,MTXMODE_APPLY); }
            else Matrix_Translate(bind.px,bind.py,bind.pz,MTXMODE_APPLY);
            const auto pose=P::Sample(clip,n,time);
            Matrix_RotateY(pose.y,MTXMODE_APPLY);Matrix_RotateZ(pose.z,MTXMODE_APPLY);Matrix_RotateX(pose.x,MTXMODE_APPLY);
        }
        Matrix_Scale(P::kPositionScale,P::kPositionScale,P::kPositionScale,MTXMODE_APPLY);
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

// Zelda-style text box over Peter's head while he is on the phone: who is talking, and what they say.
void DrawPeterCall(ImDrawList* dl, ImFont* font, float scale, const AllyActor& a, float playerDist) {
    namespace P=royale::peter_snd;
    if(playerDist>1500.0f || !a.actor) return;
    if(a.callAge<0) {
        // A quick quip bubble: the line pops in, holds, and fades.
        if(a.quip<0 || a.quip>=P::kQuipCount || a.quipAge>1.8f) return;
        ImVec2 at;
        if(!WorldToScreen(a.x,a.actor->world.pos.y+215.0f,a.z,&at)) return;
        const float k=std::clamp(1500.0f/(playerDist+700.0f),.75f,1.2f),pop=std::min(1.0f,a.quipAge/.12f),ts=19.0f*scale*k*(.7f+.3f*pop);
        const int alpha=static_cast<int>(255*std::clamp((1.8f-a.quipAge)/.3f,0.0f,1.0f));
        const char* text=P::kQuips[a.quip];const ImVec2 sz=font->CalcTextSizeA(ts,FLT_MAX,0,text);const float pad=6.0f*scale*k;
        const ImVec2 p0(at.x-sz.x*.5f-pad,at.y-sz.y-pad*2),p1(at.x+sz.x*.5f+pad,at.y);
        dl->AddRectFilled(p0,p1,IM_COL32(255,255,255,alpha*235/255),9.0f*scale);
        dl->AddTriangleFilled(ImVec2(at.x-6*scale,at.y),ImVec2(at.x+6*scale,at.y),ImVec2(at.x,at.y+8*scale),IM_COL32(255,255,255,alpha*235/255));
        dl->AddText(font,ts,ImVec2(p0.x+pad,p0.y+pad),IM_COL32(25,25,30,alpha),text);
        return;
    }
    const char* who=nullptr;const char* text=nullptr;bool joe=false;
    if(a.callAge<royale::peter_model::kPhoneShowFrom+.6f) { who="";text="*ring ring*"; }
    for(const auto& line:P::kLines) if(a.callAge>=line.start && a.callAge<=line.end+.25f) { joe=line.speaker==0;who=joe?"Joe (on the phone)":"Peter";text=line.text; }
    if(!text) return;
    ImVec2 at;
    if(!WorldToScreen(a.x,a.actor->world.pos.y+205.0f,a.z,&at)) return;
    const float k=std::clamp(1500.0f/(playerDist+700.0f),.75f,1.2f),ts=17.0f*scale*k,wrap=320.0f*scale*k,pad=8.0f*scale*k;
    const ImVec2 body=font->CalcTextSizeA(ts,FLT_MAX,wrap,text);
    const float nameH=*who?ts*1.05f:0.0f,w=std::max(body.x,*who?font->CalcTextSizeA(ts*.9f,FLT_MAX,0,who).x:0.0f)+pad*2,h=body.y+nameH+pad*2;
    const ImVec2 p0(at.x-w*.5f,at.y-h),p1(at.x+w*.5f,at.y);
    dl->AddRectFilled(p0,p1,IM_COL32(10,14,40,205),6.0f*scale);
    dl->AddRect(p0,p1,IM_COL32(235,235,245,230),6.0f*scale,0,1.5f*scale);
    if(*who) dl->AddText(font,ts*.9f,ImVec2(p0.x+pad,p0.y+pad),joe?IM_COL32(130,200,255,255):IM_COL32(255,222,110,255),who);
    dl->AddText(font,ts,ImVec2(p0.x+pad,p0.y+pad+nameH),IM_COL32(255,255,255,255),text,nullptr,wrap);
}
