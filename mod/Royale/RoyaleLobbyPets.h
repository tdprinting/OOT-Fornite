// Coordinated lobby play. The three existing rigs keep their own poses and dialogue;
// this controller supplies shared goals and responsive activities only while waiting.
bool SocialPet_Update(Actor* actor, PlayState* play, int pet) {
    namespace P=royale::lobby;
    if(!LobbyPetsActive() || !gLobbyPetGroup.ready) return false;
    // Let the ordinary controller finish an open conversation and advance its line.
    if(TalkingTo(actor) || (pet==0 && gCat.mood==CatMood::Talk) ||
       (pet==1 && gBaby.mood==BabyMood::Talk) || (pet==2 && gMayaCompanion.talking)) return false;
    const auto phase=gLobbyPetGroup.Phase();
    // Group events (conga, tag, story circle, nap pile, dance): only ever here, because all three pets are out.
    float into=-1;const auto trio=gLobbyPetGroup.Event(&into);const bool event=trio!=P::Trio::None;
    const float dt=1.0f/royale::kTickHz;
    auto& pos=actor->world.pos;
    const auto& player=GET_PLAYER(play)->actor.world.pos;
    const float playerDistance=std::hypot(player.x-pos.x,player.z-pos.z);
    const bool attentive=!event && (phase==P::Activity::Greet || phase==P::Activity::Attention || playerDistance<85);
    P::Point goal=event ? gLobbyPetGroup.TrioGoal(trio,pet,into) : gLobbyPetGroup.Goal(pet);
    Actor* peers[]={gCat.actor,gBaby.actor,gMayaCompanion.actor};
    for(int i=0;i<3;++i) if(i!=pet && peers[i]) {
        const auto& p=peers[i]->world.pos;
        const float gx=goal.x-p.x,gz=goal.z-p.z,len=std::hypot(gx,gz);
        if(len<42 && len>.01f) { goal.x+=gx/len*(42-len);goal.z+=gz/len*(42-len); }
    }
    float dx=goal.x-pos.x,dz=goal.z-pos.z,d=std::hypot(dx,dz);
    float speed=pet==0 ? 100.0f : pet==1 ? 45.0f : 76.0f;
    if(trio==P::Trio::Conga) speed=pet==0 ? 80.0f : pet==1 ? 62.0f : 72.0f;
    if(trio==P::Trio::Tag) speed=pet==0 ? 135.0f : pet==1 ? 45.0f : 112.0f;
    bool moving=d>9;
    if(moving) {
        const float step=std::min(d,speed*dt);
        const float x=pos.x+dx/d*step,z=pos.z+dz/d*step;
        float y;
        if(PetPathClear(play,pos,x,z) && RawFloorAt(x,z,&y) && std::fabs(y-pos.y)<22 && !WaterAt(x,z,y) && !OnExitFloor(x,z) && !HazardFloorAt(x,z)) {
            pos.x=x; pos.z=z; pos.y=y;
        } else {
            moving=false;
            // Never step off ledges or into a loading zone. Rejoin only if left far behind.
            if(playerDistance>650 || std::fabs(player.y-pos.y)>170) {
                for(int i=0;i<8;++i) {
                    const float a=pet*2.094395f+i*.3f;
                    const float nx=player.x+std::sin(a)*80,nz=player.z+std::cos(a)*80;
                    if(RawFloorAt(nx,nz,&y) && std::fabs(y-player.y)<35 && !WaterAt(nx,nz,y) && !OnExitFloor(nx,nz) && !HazardFloorAt(nx,nz)) {
                        CatPoof(play,pos.x,pos.y,pos.z); pos={nx,y,nz}; CatPoof(play,nx,y,nz); break;
                    }
                }
            }
        }
    }
    // Turn toward the pet that is leading the activity, or acknowledge nearby Link.
    Actor* partner=pet==2 ? gCat.actor : gMayaCompanion.actor;
    if(phase==P::Activity::Drawing && pet==2) partner=gBaby.actor;
    const auto& look=attentive || !partner ? player : partner->world.pos;
    P::Point eventLook={look.x,look.z};   // who she faces when standing still during an event
    if(trio==P::Trio::Circle || trio==P::Trio::Nap) eventLook={gLobbyPetGroup.center.x,gLobbyPetGroup.center.z};
    else if(trio==P::Trio::Dance) eventLook={player.x,player.z};
    else if(trio==P::Trio::Tag && pet==1 && gCat.actor) eventLook={gCat.actor->world.pos.x,gCat.actor->world.pos.z};
    else if(trio==P::Trio::Tag && pet==1) eventLook={gLobbyPetGroup.center.x,gLobbyPetGroup.center.z};
    const float heading=moving ? std::atan2(dx,dz) : event ? std::atan2(eventLook.x-pos.x,eventLook.z-pos.z) : std::atan2(look.x-pos.x,look.z-pos.z);
    float* yaw=pet==0 ? &gCat.yaw : pet==1 ? &gBaby.yaw : &gMayaCompanion.yaw;
    *yaw+=std::atan2(std::sin(heading-*yaw),std::cos(heading-*yaw))*(moving ? .18f : .09f);
    if(pet==0) {
        auto& c=gCat; c.x=pos.x;c.y=pos.y;c.z=pos.z;c.placed=true;
        int clip=moving ? royale::lilo::kWalk : attentive ? royale::lilo::kHappy :
            phase==P::Activity::Celebrate ? royale::lilo::kHappy : royale::lilo::kSit;
        if(event) {   // Lilo's parts: leads the conga, flees the tag, grooms in the circle, curls up in the pile, springs about at the dance
            const int beat=static_cast<int>(into*2);
            clip=trio==P::Trio::Conga ? (moving ? royale::lilo::kRun : royale::lilo::kHappy) : trio==P::Trio::Tag ? (moving ? royale::lilo::kRun : royale::lilo::kJump) :
                 trio==P::Trio::Circle ? royale::lilo::kGroom : trio==P::Trio::Nap ? royale::lilo::kSleep : (beat%4==3 ? royale::lilo::kJump : royale::lilo::kHappy);
            if(trio==P::Trio::Tag && moving && static_cast<int>(into*3)%7==0) clip=royale::lilo::kJump;   // a leap out of Maya's reach
            if(trio==P::Trio::Conga && into<.3f) PlayMeow(kMewChirp,.4f);
        }
        c.mood=moving ? CatMood::Follow : CatMood::Sit;
        if(clip!=c.anim.clip && clip==royale::lilo::kHappy) PlayMeow(kMewChirp,.4f);
        c.anim.Play(clip,.3f);c.anim.Update(dt);c.eyes=LiloEyes(gLobbyPetGroup.time,0);
    } else if(pet==1) {
        auto& c=gBaby; if(gToy.kind!=ToyKind::None) EndBabyToy(c,true);c.x=pos.x;c.y=pos.y;c.z=pos.z;c.placed=true;
        int clip=moving ? royale::avriella::kRoll : attentive ? royale::avriella::kWave :
            phase==P::Activity::Chase ? royale::avriella::kGiggle : phase==P::Activity::Drawing ? royale::avriella::kReach :
            phase==P::Activity::Celebrate ? royale::avriella::kClap : royale::avriella::kSit;
        if(event) {   // Avriella's parts: crawls along in the conga, claps for the chasers, babbles in the circle, naps in the pile, claps and giggles to the music
            const int beat=static_cast<int>(into*2);
            clip=trio==P::Trio::Conga ? (moving ? royale::avriella::kCrawl : royale::avriella::kClap) : trio==P::Trio::Tag ? (beat%2 ? royale::avriella::kClap : royale::avriella::kGiggle) :
                 trio==P::Trio::Circle ? royale::avriella::kBabble : trio==P::Trio::Nap ? royale::avriella::kNap : (beat%4==3 ? royale::avriella::kGiggle : royale::avriella::kClap);
            if(clip==royale::avriella::kGiggle && clip!=c.anim.clip) BabyCoo(kCooGiggle);
        }
        if(clip!=c.anim.clip && clip==royale::avriella::kGiggle) BabyCoo(kCooGiggle);
        c.mood=BabyMood::Sit;c.face=clip==royale::avriella::kGiggle ? royale::avriella::kFaceGiggle : royale::avriella::kFaceSmile;
        c.anim.Play(clip,.3f);c.anim.Update(dt);
    } else {
        auto& c=gMayaCompanion;c.expressionClock+=dt;c.speed=moving ? speed : 0;
        int clip=moving ? royale::maya::kWalk : attentive ? royale::maya::kWave :
            phase==P::Activity::Chase ? royale::maya::kPoint : phase==P::Activity::Drawing ? royale::maya::kDraw :
            phase==P::Activity::Celebrate ? royale::maya::kCheer : phase==P::Activity::Rest ? royale::maya::kSit : royale::maya::kFidget;
        // A one-shot ends in a relaxed pose rather than freezing an outstretched arm.
        if(event) {   // Maya's parts: skips along the conga, chases Lilo, draws the story, dozes in the pile, leads the dance
            const int beat=static_cast<int>(into*2);
            clip=trio==P::Trio::Conga ? (moving ? royale::maya::kRun : royale::maya::kHop) : trio==P::Trio::Tag ? (moving ? royale::maya::kRun : royale::maya::kCheer) :
                 trio==P::Trio::Circle ? royale::maya::kDraw : trio==P::Trio::Nap ? royale::maya::kSleep : royale::maya::kDance;
            if(trio==P::Trio::Nap && moving) clip=royale::maya::kWalk;
            // Little shared sparkles: hearts-ish pink at the dance and the story circle, and sleepy blue at the nap pile.
            if(std::fmod(into,1.5f)<dt && (trio==P::Trio::Dance || trio==P::Trio::Circle || trio==P::Trio::Nap))
                SparkBurst(play,gLobbyPetGroup.center.x,pos.y+34.0f,gLobbyPetGroup.center.z,trio==P::Trio::Nap ? Color_RGBA8{150,170,255,255} : Color_RGBA8{255,150,200,255},6,2.0f);
            if((trio==P::Trio::Dance && into<.1f) || (trio==P::Trio::Tag && beat==15 && clip!=c.anim.clip)) PlayMayaVoice(royale::maya_snd::kHappy);
        }
        if(!royale::maya::InfoOf(clip).loops && c.anim.clip==clip && c.anim.time>=royale::maya::ClipSeconds(clip)) clip=royale::maya::kFidget;
        if(!event && c.anim.clip==royale::maya::kFidget && c.anim.time<2.0f && clip!=royale::maya::kWalk) clip=royale::maya::kFidget;
        if(clip!=c.anim.clip && clip==royale::maya::kCheer) PlayMayaVoice(royale::maya_snd::kHappy);
        c.anim.Play(clip,clip==royale::maya::kSit || c.anim.clip==royale::maya::kSit ? .8f : .35f);
        // Walk at the clip's own stride so her feet plant.
        c.anim.Update(dt,clip==royale::maya::kWalk ? std::clamp(speed/royale::maya::kWalkStride*royale::maya::ClipSeconds(royale::maya::kWalk),.35f,2.0f) : clip==royale::maya::kRun ? std::clamp(speed/royale::maya::kRunStride*royale::maya::ClipSeconds(royale::maya::kRun),.6f,2.1f) : 1.0f);
    }
    actor->shape.rot.y=static_cast<s16>(*yaw*(32768.0f/3.14159265f));
    actor->focus.pos=pos;actor->focus.pos.y+=pet==0 ? 16 : pet==1 ? 25 : royale::maya::kFocusHeight;
    if(playerDistance<140 && std::fabs(GET_PLAYER(play)->linearVelocity)<3) {
        const u16 text=pet==0 ? kTextLiloPet+gCat.line : pet==1 ? kTextAvriellaPet+gBaby.line : kTextMayaCompanion+gMayaCompanion.line;
        // OfferTalk accepts the talk request; the original controller handles it next frame.
        if(OfferTalk(actor,play,text,140)) return false;
    }
    return true;
}
