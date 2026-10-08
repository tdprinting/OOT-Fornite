void WarMusic() {
    if(!InGame())return;
    const bool want=gWar.open||EndScreenUp(gSession.Hud());
    if (gMenuAudioMuted.exchange(want,std::memory_order_relaxed)!=want && want)
        for(u8 bank=0;bank<7;++bank)Audio_StopSfxByBank(bank);
    if(want) {
        Audio_SetGameVolume(SEQ_PLAYER_BGM_SUB, 0.0f);
        Audio_SetGameVolume(SEQ_PLAYER_FANFARE, 0.0f);
        if(!gWar.music || gWar.savedScene!=gPlayState->sceneNum) {
            gWar.savedSequence=gActiveSeqs[SEQ_PLAYER_BGM_MAIN].seqId;
            if(gWar.savedSequence==NA_BGM_FILE_SELECT)gWar.savedSequence=gSaveContext.seqId;
            gWar.savedScene=gPlayState->sceneNum;
            gWar.music=true;
            func_800F5E18(SEQ_PLAYER_BGM_MAIN,NA_BGM_FILE_SELECT,0,7,1);
        }
        Audio_SetGameVolume(SEQ_PLAYER_BGM_MAIN,std::clamp(CVarGetInteger(ROYALE_CVAR("MenuMusic"),70),0,100)/100.0f);
    } else if(gWar.music) {
        const uint16_t sequence=gWar.savedScene==gPlayState->sceneNum?gWar.savedSequence:gSaveContext.seqId;
        Audio_QueueSeqCmd(NA_BGM_STOP|(SEQ_PLAYER_BGM_MAIN<<24));
        if(sequence!=NA_BGM_DISABLED && (sequence&255)!=255)Audio_QueueSeqCmd((SEQ_PLAYER_BGM_MAIN<<24)|sequence);
        Audio_SetGameVolume(SEQ_PLAYER_BGM_MAIN,CVarGetInteger(CVAR_SETTING("Volume.MainMusic"),100)/100.0f);
        Audio_SetGameVolume(SEQ_PLAYER_BGM_SUB,CVarGetInteger(CVAR_SETTING("Volume.SubMusic"),100)/100.0f);
        Audio_SetGameVolume(SEQ_PLAYER_FANFARE,CVarGetInteger(CVAR_SETTING("Volume.Fanfare"),100)/100.0f);
        gWar.music=false;
        gBgmMuted=false;
    }
}

void WarGameUpdate() {
    Ui(); // Load saved gameplay settings even before any menu page is visited.
    const auto h=gSession.Hud();
    const bool joined=gSession.Joined();
    if(gWar.homeTravel&&!joined&&InGame()) {
        if(InWaitingRoom())gWar.homeTravel=false;
        else GoToWaitingRoom();
    }
    if(!joined&&(gWar.lastJoined||(gWar.page==WarPage::Lobby&&gSession.GetMode()==royale::RoyaleSession::Mode::Idle&&!gSession.LastEnded().empty()))) {
        const std::string reason=gSession.LastEnded();
        WarQuit();gWar.notice=reason;
    }
    if(joined&&!gWar.lastJoined) { gWar.notice.clear(); if(!gSession.SoloTest())WarShow(WarPage::Lobby); }
    gWar.lastJoined=joined;
    if (gDeath.active && !gDeath.placeSettled && ImGui::GetTime()-gDeath.at>0.8) {
        gDeath.place = h.alive>=gDeath.aliveAtDeath ? gDeath.aliveAtDeath+1 : gDeath.aliveAtDeath;
        if (IsLive(h)) gDeath.place=std::max(2,gDeath.place);
        gDeath.placeSettled=true;
    }
    if (joined && wt::ShowDeathMenu(gDeath.active,gDeath.card,IsLive(h),ImGui::GetTime()-gDeath.at) &&
        (!gWar.open || gWar.page==WarPage::Pause)) { gUiPad={}; WarShow(WarPage::Death); }
    if (gWar.page==WarPage::Death && (!joined || h.selfAlive || h.state==royale::MatchState::Ending)) WarOpen();
    if(joined&&(h.state==royale::MatchState::Countdown||h.state==royale::MatchState::Drop)&&gWar.page==WarPage::Lobby)WarHide();
    if(joined&&h.state==royale::MatchState::InMatch&&gWar.page==WarPage::Lobby)WarHide();
    // The gameplay recap owns the initial end-of-match presentation. Start opens native results.
    if(joined&&h.state==royale::MatchState::Lobby&&gWar.page==WarPage::Results)WarShow(WarPage::Lobby);
    if(gWar.pendingZone>=0&&gSession.Sandbox()&&h.haveSelf&&InField()&&h.state==royale::MatchState::InMatch) {
        SandboxDo(SandboxCmd::Go,gWar.pendingZone);gWar.pendingZone=-1;WarHide();
    }
    gHitFlash=CVarGetInteger(ROYALE_CVAR("HitFlash"),1)!=0;
    gHitBlood=CVarGetInteger(ROYALE_CVAR("HitBlood"),1)!=0;
    WarMusic();
#ifdef __ANDROID__
    static int touchMenu = -1;
    if(touchMenu!=static_cast<int>(gWar.open)) {
        if(gWar.open)Ship::Mobile::DisableTouchArea();else Ship::Mobile::EnableTouchArea();
        touchMenu=gWar.open;
    }
#endif
}

void WarInput() {
    const bool wasOpen = gWar.open;
    const uint32_t now=SDL_GetTicks();
    const float dt=gWar.lastTicks?std::clamp((now-gWar.lastTicks)/1000.0f,0.001f,0.1f):0.05f;
    gWar.lastTicks=now;gWar.clock+=dt;
    gWar.reduced=CVarGetInteger(ROYALE_CVAR("ReducedMotion"),0)!=0;
    gWar.transition=gWar.reduced?1.0f:std::min(1.0f,gWar.transition+dt/0.18f);
    int keyCount=0;const uint8_t* keys=SDL_GetKeyboardState(&keyCount);
    auto pressed=[&](SDL_Scancode key){return key<keyCount&&keys[key]&&!gWar.keys[key];};
    auto held=[&](SDL_Scancode key){return key<keyCount&&keys[key];};
    Input* in=InGame()?&gPlayState->state.input[0]:nullptr;
    const u16 press=in?in->press.button:0, buttons=in?in->cur.button:0;
    const bool start=(press&BTN_START)||pressed(SDL_SCANCODE_ESCAPE)||pressed(SDL_SCANCODE_F1);
    if(InGame()&&start) {
        if(gWar.open&&gWar.page==WarPage::Lobby&&!gSession.Joined())WarQuit();
        else if(gWar.open&&(gWar.page==WarPage::Pause||gWar.page==WarPage::Lobby))WarHide();
        else WarOpen();
        WarSound(gWar.open?4:2);
    }
    if(gWar.open&&gWar.bootError.empty()) {
        const int previousId=gWar.focus>=0&&gWar.focus<static_cast<int>(gWar.controls.size())?gWar.controls[gWar.focus].view.id:-1;
        WarBuild();
        if(gWar.focus<0||gWar.focus>=static_cast<int>(gWar.controls.size()))gWar.focus=0;
        if(previousId>=0)for(int i=0;i<static_cast<int>(gWar.controls.size());++i)if(gWar.controls[i].view.id==previousId)gWar.focus=i;
        bool activate=(press&BTN_A)||pressed(SDL_SCANCODE_RETURN)||pressed(SDL_SCANCODE_SPACE);
        const bool back=(press&BTN_B)||pressed(SDL_SCANCODE_BACKSPACE);
        int dx=((buttons&BTN_DRIGHT)||held(SDL_SCANCODE_RIGHT)||(in&&in->cur.stick_x>40)?1:0)-((buttons&BTN_DLEFT)||held(SDL_SCANCODE_LEFT)||(in&&in->cur.stick_x<-40)?1:0);
        int dy=((buttons&BTN_DDOWN)||held(SDL_SCANCODE_DOWN)||(in&&in->cur.stick_y<-40)?1:0)-((buttons&BTN_DUP)||held(SDL_SCANCODE_UP)||(in&&in->cur.stick_y>40)?1:0);
        const int direction=dx?dx:(dy?dy*2:0);
        if(!start&&gWar.repeat.Tick(direction,dt)&&!gWar.controls.empty()) {
            const auto w=gWar.controls[gWar.focus].view;
            if(dx&&w.adjustable&&w.enabled) {
                auto action=gWar.controls[gWar.focus].act;action(dx);WarSound(0);
            } else {
                std::vector<wt::Widget> views;for(const auto& c:gWar.controls)views.push_back(c.view);
                const int next=wt::Neighbor(views,gWar.focus,dx,dx?0:dy);
                if(next!=gWar.focus){gWar.focus=next;gWar.notice.clear();WarSound(0);}
            }
        }
        int mx=0,my=0;const bool mouse=SDL_GetMouseState(&mx,&my)&SDL_BUTTON(SDL_BUTTON_LEFT);
        SDL_Window* window=SDL_GetMouseFocus();
        if(!window)window=SDL_GetKeyboardFocus();
        int ww=640,wh=360;if(window)SDL_GetWindowSize(window,&ww,&wh);
        auto canvas=wt::Canvas::Fit(static_cast<float>(ww),static_cast<float>(wh));
        float px=canvas.X(static_cast<float>(mx)),py=canvas.Y(static_cast<float>(my));
        bool tap=mouse&&!gWar.mouseDown;gWar.mouseDown=mouse;
        bool touch=false;
        for(int d=0;d<SDL_GetNumTouchDevices();++d) {
            SDL_TouchID id=SDL_GetTouchDevice(d);
            if(SDL_GetNumTouchFingers(id)>0) {
                if(SDL_Finger* finger=SDL_GetTouchFinger(id,0)) {touch=true;px=canvas.X(finger->x*ww);py=canvas.Y(finger->y*wh);}
                break;
            }
        }
        tap|=touch&&!gWar.wasTouch;gWar.wasTouch=touch;
        int activateDirection=1;
        if(tap)for(int i=0;i<static_cast<int>(gWar.controls.size());++i)if(gWar.controls[i].view.rect.Contains(px,py)) {gWar.focus=i;activate=true;if(gWar.controls[i].view.adjustable&&px<gWar.controls[i].view.rect.x+gWar.controls[i].view.rect.w/2)activateDirection=-1;break;}
        if(back&&!start) {WarSound(2);WarBack();}
        else if(activate&&!start&&!gWar.controls.empty()) {
            const auto c=gWar.controls[gWar.focus];
            if(c.view.enabled){gWar.notice.clear();WarSound(1);c.act(activateDirection);}else WarSound(3);
        }
        if(gWar.page!=WarPage::Keyboard&&!start) {
            int tab=((press&BTN_R)||pressed(SDL_SCANCODE_E)?1:0)-((press&BTN_L)||pressed(SDL_SCANCODE_Q)?1:0);
            if(tab) {
                int n=static_cast<int>(gWar.page);if(n>3)n=0;n=(n+tab+4)%4;
                if(n==0)WarOpen();else WarShow(static_cast<WarPage>(n));WarSound(4);
            }
        }
        WarBuild();
        if(!gWar.controls.empty()) {
            gWar.focus=std::clamp(gWar.focus,0,static_cast<int>(gWar.controls.size())-1);
            const auto r=gWar.controls[gWar.focus].view.rect;
            if(gWar.focusRect.w==0)gWar.focusRect=r;
            gWar.focusRect.x=wt::Approach(gWar.focusRect.x,r.x,dt,gWar.reduced);
            gWar.focusRect.y=wt::Approach(gWar.focusRect.y,r.y,dt,gWar.reduced);
            gWar.focusRect.w=wt::Approach(gWar.focusRect.w,r.w,dt,gWar.reduced);
            gWar.focusRect.h=wt::Approach(gWar.focusRect.h,r.h,dt,gWar.reduced);
        }
    }
    // Consume the opening and closing frame as well. Start cannot leak to the
    // old inventory, and A cannot accidentally fire or interact after Resume.
    if(in&&(wasOpen||gWar.open||start)) {
        in->cur.button=in->press.button=in->rel.button=0;
        in->cur.stick_x=in->cur.stick_y=in->press.stick_x=in->press.stick_y=in->rel.stick_x=in->rel.stick_y=0;
    }
    std::copy(keys,keys+std::min(keyCount,static_cast<int>(gWar.keys.size())),gWar.keys.begin());
}

