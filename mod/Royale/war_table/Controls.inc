// Included once, inside RoyaleMod.cpp's integration namespace, after gameplay services.
// This UI emits native Fast3D display lists. It has no ImGui widgets or draw calls.
namespace wt = royale::wartable;
using WarPage = wt::Page;
struct WarControl { wt::Widget view; std::function<void(int)> act; };
struct WarState {
    bool open = true, booted = false, music = false, reduced = false, previewAnimate = true;
    bool lastJoined = false, mouseDown = false, wasTouch = false, homeTravel = false;
    WarPage page = WarPage::Play, returnPage = WarPage::Play;
    int focus = 0, section = 0, zone = 0, item = 1, boss = 0, rosterOffset = 0;
    int keyboardTarget = 0, pendingZone = -1, optionPage = 0, optionPages = 1;
    uint32_t lastTicks = 0, enteredAt = 0, mapChangedAt = 0;
    uint16_t savedSequence = NA_BGM_DISABLED;
    int savedScene = -1;
    float clock = 0, transition = 1, pulse = 0;
    wt::Rect focusRect;
    wt::Repeat repeat;
    std::array<uint8_t, SDL_NUM_SCANCODES> keys{};
    std::vector<WarControl> controls;
    std::string edit, notice, bootError;
};
WarState gWar;
bool WarMenuOpen() { return gWar.open; }
void WarBuild();
void WarDraw(GameState* state);

void WarSound(int kind) {
    static float volume = 0.75f;
    static const u16 sounds[] = {NA_SE_SY_FSEL_CURSOR, NA_SE_SY_FSEL_DECIDE_L, NA_SE_SY_FSEL_CLOSE,
                                 NA_SE_SY_FSEL_ERROR, NA_SE_SY_CURSOR};
    volume = std::clamp(CVarGetInteger(ROYALE_CVAR("MenuSfx"), 75), 0, 100) / 100.0f;
    kind = std::clamp(kind, 0, 4);
    if (volume > 0) Audio_PlaySoundGeneral(sounds[kind], &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &volume, &gSfxDefaultReverb);
}
void WarShow(WarPage page) {
    gWar.open = true;
    gMenuAudioMuted.store(true, std::memory_order_relaxed);
    for (u8 bank = 0; bank < 7; ++bank) Audio_StopSfxByBank(bank);
    gWar.page = page;
    gWar.focus = 4;
    gWar.section = 0;
    gWar.optionPage = 0;
    gWar.transition = gWar.reduced ? 1.0f : 0.0f;
    gWar.enteredAt = SDL_GetTicks();
    gWar.repeat = {};
    CloseEmoteWheel();
    Ship::Context::GetInstance()->GetWindow()->SetMouseCapture(false);
}
void WarHide() {
    gWar.open = false;
    gMenuAudioMuted.store(EndScreenUp(gSession.Hud()), std::memory_order_relaxed);
    gWar.repeat = {};
    if (Ship::Context::GetInstance()->GetWindow()->IsFullscreen())
        Ship::Context::GetInstance()->GetWindow()->SetMouseCapture(true);
}
void WarOpen() {
    const auto h = gSession.Hud();
    WarShow(wt::ActivePage(gSession.Joined(), gSession.GetMode()!=royale::RoyaleSession::Mode::Idle, h.state==royale::MatchState::Lobby, h.state==royale::MatchState::Ending, !h.haveSelf || h.selfAlive));
}
void WarSave() {
    UiState& ui = Ui();
    gSession.SelectMap(ui.mapId);
    gSession.SetBotDifficulty(static_cast<royale::BotDifficulty>(ui.botDifficulty));
    gSession.SetPlayerLimit(ui.playerLimit);
    gSession.SetAutoStart(ui.autoStart ? royale::kLobbyAutoStartSec : 0);
    gSession.SetMajorBoss(ui.majorBoss);
    gSession.SetWeatherOptions({static_cast<uint8_t>(ui.weatherSeason), static_cast<uint8_t>(ui.weatherIntensity), static_cast<uint8_t>(ui.weatherChange)});
    gLocalTunic = SelectedTunic(ui);
    gSession.SetTunic(gLocalTunic);
    gWeatherDensity = ui.weatherDensity / 100.0f;
    gFoliage = ui.foliage / 100.0f;
    gClothScale = ui.clothOn ? ui.clothPhysics / 100.0f : 0;
    gWindOn = ui.windOn;
    gWindScale = ui.windStrength / 100.0f;
    gMusicMode = ui.musicMode;
    SaveUi(ui);
}
void WarQuit() {
    // The same exit path is used by pause, lobby, practice, results and connection cancellation.
    gSession.Leave();
    gSession.ClearLastEnded();
    gPendingStart = false;
    gSoloStartWanted = false;
    gWar.pendingZone = -1;
    WantsWaitingRoom = false;
    { std::lock_guard<std::mutex> lock(gSandboxLock); gSandboxCmds.clear(); }
    if (gHealthOverridden) RestoreHealth();
    SyncPauseInventory(gSession.Hud());
    gWar.notice.clear();
    Ui().error.clear();
    WarShow(WarPage::Play);
    gWar.homeTravel = true;
}
void WarSpectate() {
    gDeath.card = false;
    gUiPad = {};
    if (!gDeath.spectated) {
        gDeath.spectated = true;
        for (const auto& player : gSession.Puppets())
            if (player.alive && player.id == gDeath.killer) gSpectateTarget = player.id;
        if (gSpectateTarget == kSpectateSelf) CycleSpectate(1);
    }
    WarHide();
}
void WarBack() {
    if (gWar.page == WarPage::Death) { WarSpectate(); return; }
    if (gWar.page == WarPage::Keyboard) { WarShow(gWar.returnPage); return; }
    if (gWar.page == WarPage::Quit) { WarOpen(); return; }
    if (gWar.page == WarPage::Play) return; // Home always remains available after launch.
    if (gWar.page == WarPage::Lobby && !gSession.Joined()) { WarQuit(); return; }
    if (gWar.page == WarPage::Pause || gWar.page == WarPage::Lobby) { WarHide(); return; }
    WarOpen();
}
void WarEdit(int target) {
    gWar.returnPage = gWar.page;
    gWar.keyboardTarget = target;
    gWar.edit = target == 0 ? Ui().name : target == 1 ? Ui().address : std::to_string(Ui().port);
    WarShow(WarPage::Keyboard);
}
void WarFinishEdit() {
    auto& ui = Ui();
    if (gWar.keyboardTarget == 0) {
        if (gWar.edit.empty()) { gWar.notice = "Enter a name first."; WarSound(3); return; }
        std::snprintf(ui.name, sizeof(ui.name), "%s", gWar.edit.c_str());
    } else if (gWar.keyboardTarget == 1) {
        if (!wt::ValidAddress(gWar.edit)) { gWar.notice = "Use a host name or IP address."; WarSound(3); return; }
        std::snprintf(ui.address, sizeof(ui.address), "%s", gWar.edit.c_str());
    } else {
        uint16_t port;
        if (!wt::ValidPort(gWar.edit, &port)) { gWar.notice = "Port must be from 1024 to 65535."; WarSound(3); return; }
        ui.port = port;
    }
    WarSave();
    gWar.notice.clear();
    WarShow(gWar.returnPage);
}
void WarHost(bool solo, bool sandbox, int testMap = royale::kSandboxMapIndex) {
    if (!InGame() || gSession.GetMode() != royale::RoyaleSession::Mode::Idle) return;
    auto& ui = Ui();
    ui.port = std::clamp(ui.port, 1024, 65535);
    WarSave();
    ui.error.clear();
    gSession.ClearLastEnded();
    if (!gSession.Host(static_cast<uint16_t>(ui.port), CleanName(ui.name), &ui.error, solo, sandbox, testMap)) {
        gWar.notice = "Could not open the lobby: " + ui.error;
        WarSound(3);
        return;
    }
    gWar.homeTravel = false;
    gWar.rosterOffset = 0;
    RefreshLocalAddresses(ui, true);
    gSoloStartWanted = solo;
    gWar.pendingZone = sandbox && !royale::IsBossArena(testMap) ? gWar.zone : -1;
    if(royale::IsBossArena(testMap)) {gWar.section=2;gWar.boss=testMap==royale::kMainBossArenaIndex?static_cast<int>(royale::BossKind::DragonFire):0;gSbx.lastBoss=-1;}
    WarShow(WarPage::Lobby);
    gWar.notice = "Connecting to your lobby...";
}
void WarMap(int direction) {
    if (gSession.Joined() && (!gSession.Hud().isHost || gSession.Hud().state != royale::MatchState::Lobby)) return;
    Ui().mapId = wt::CycleMap(Ui().mapId, direction);
    gWar.mapChangedAt = SDL_GetTicks();
    WarSave();
}
void WarAdd(int id, wt::Rect rect, std::string label, std::string hint, std::function<void(int)> act,
            bool enabled = true, bool adjustable = false) {
    gWar.controls.push_back({{id,rect,std::move(label),std::move(hint),enabled,adjustable},std::move(act)});
}
void WarRow(int id, int row, std::string label, std::string hint, std::function<void(int)> act, bool enabled = true, bool adjustable = false) {
    WarAdd(id,{234,102.0f+row*29,378,25},std::move(label),std::move(hint),std::move(act),enabled,adjustable);
}
void WarSetting(int row, const char* label, const char* key, int fallback, int low, int high, int step, const char* hint, bool enabled=true) {
    int n = std::clamp(CVarGetInteger(key, fallback), low, high);
    const bool toggle = low == 0 && high == 1;
    std::string text = std::string(label) + "   " + (toggle ? (n ? "On" : "Off") : std::to_string(n));
    WarRow(400+row,row,text,hint,[key,low,high,step,n](int direction) {
        int value = low == 0 && high == 1 ? !n : std::clamp(n+(direction < 0 ? -step : step),low,high);
        CVarSetInteger(key,value);
        // Refresh cached local graphics values as well as the saved setting.
        Ui().loaded=false; Ui();
        if(gWar.page==WarPage::Settings&&gWar.section==4)gShadowCfgLoaded=false;
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        if (std::strcmp(key,CVAR_SETTING("Volume.SFX")) == 0) Audio_SetGameVolume(SEQ_PLAYER_SFX,value/100.0f);
    },enabled,true);
}
struct WarOption { const char* label; const char* key; int fallback, low=0, high=1, step=1; const char* hint="Change this game option"; };
void WarFocusFirstOption() {
    for(int i=0;i<static_cast<int>(gWar.controls.size());++i) {
        const auto& w=gWar.controls[i].view;
        if(w.enabled&&w.rect.x>=234&&w.rect.y<280){gWar.focus=i;break;}
    }
}
void WarOptionPager(int pages) {
    gWar.optionPages=std::max(1,pages);
    if(pages<=1)return;
    gWar.optionPage=std::clamp(gWar.optionPage,0,pages-1);
    WarAdd(800,{235,282,100,22},"< OPTIONS","Previous option page",[](int){gWar.optionPage=std::max(0,gWar.optionPage-1);WarFocusFirstOption();},gWar.optionPage>0);
    WarAdd(801,{507,282,100,22},"OPTIONS >","More options in this category",[pages](int){gWar.optionPage=std::min(pages-1,gWar.optionPage+1);WarFocusFirstOption();},gWar.optionPage+1<pages);
}
void WarOptions(std::initializer_list<WarOption> options,bool enabled=true) {
    const int pages=(static_cast<int>(options.size())+5)/6;
    gWar.optionPage=std::clamp(gWar.optionPage,0,std::max(0,pages-1));
    int index=0;for(const auto& option:options) {
        if(index/6==gWar.optionPage)WarSetting(index%6,option.label,option.key,option.fallback,option.low,option.high,option.step,option.hint,enabled);
        ++index;
    }
    WarOptionPager(pages);
}
void WarDebugToggle(int row,int id) {
    const bool on=DebugOn(id);
    WarRow(700+id,row,std::string(kDebugSwitches[id].key)+"   "+(on?"On":"Off"),kDebugSwitches[id].label,[id,on](int){
        gDebugOn[id]=!on;const auto key=std::string(ROYALE_CVAR("Debug."))+kDebugSwitches[id].key;
        CVarSetInteger(key.c_str(),!on);WarSave();
    },true,true);
}
void WarBuild() {
    auto& ui = Ui();
    const auto h = gSession.Hud();
    const bool joined = gSession.Joined();
    const bool idle = gSession.GetMode() == royale::RoyaleSession::Mode::Idle;
    gWar.controls.clear();
    if (gWar.page == WarPage::Keyboard) {
        static const std::string keys = "1234567890abcdefghijklmnopqrstuvwxyz.-: ";
        for (int i=0;i<static_cast<int>(keys.size());++i) {
            const char c=keys[i];
            WarAdd(1000+i,{104.0f+(i%10)*43,135.0f+(i/10)*31,37,26},c==' '?"_":std::string(1,c),"A: type   B: cancel",[c](int) {
                const size_t limit = gWar.keyboardTarget==0 ? 23 : gWar.keyboardTarget==1 ? 63 : 5;
                if (gWar.edit.size()<limit && (gWar.keyboardTarget!=2 || (c>='0' && c<='9'))) gWar.edit+=c;
                else WarSound(3);
            });
        }
        WarAdd(1100,{104,271,130,27},"Delete","Remove the last character",[](int){if(!gWar.edit.empty())gWar.edit.pop_back();});
        WarAdd(1101,{245,271,130,27},"Done","Save this value",[](int){WarFinishEdit();});
        WarAdd(1102,{386,271,142,27},"Cancel","Keep the previous value",[](int){WarBack();});
        return;
    }
    const char* tabs[] = {joined?"MATCH":"PLAY","CHARACTER","SETTINGS","GUIDE"};
    for(int i=0;i<4;++i) WarAdd(1+i,{24.0f+i*150,64,144,27},tabs[i],"L / R: change page",[i](int){
        if(i==0) WarOpen(); else WarShow(static_cast<WarPage>(i));
    });
    switch(gWar.page) {
    case WarPage::Play:
        WarAdd(10,{450,119,162,47},"HOST GAME","Create a lobby on the selected battlefield",[](int){WarHost(false,false);},InGame()&&idle);
        WarAdd(11,{450,177,162,47},"JOIN GAME","Connect to a friend's lobby",[](int){WarShow(WarPage::Join);},InGame()&&idle);
        WarAdd(12,{450,235,162,47},"PRACTICE","Test courses, equipment and solo exploration",[](int){WarShow(WarPage::Practice);},InGame()&&idle);
        WarAdd(13,{174,278,30,26},"<","Previous battlefield",[](int){WarMap(-1);});
        WarAdd(14,{406,278,30,26},">","Next battlefield",[](int){WarMap(1);});
        break;
    case WarPage::Join:
        WarRow(20,0,"Host   "+std::string(ui.address[0]?ui.address:"Enter address"),"Enter the host's IP address or host name",[](int){WarEdit(1);});
        WarRow(21,1,"Port   "+std::to_string(ui.port),"Use the port shown in your friend's lobby",[](int){WarEdit(2);});
        WarRow(22,2,"Name   "+std::string(ui.name),"Your name in the player roster",[](int){WarEdit(0);});
        WarRow(23,4,"CONNECT","Use the same game version as the host",[](int){
            auto& u=Ui(); u.error.clear(); WarSave(); gSession.ClearLastEnded();
            if(gSession.Join(u.address,static_cast<uint16_t>(u.port),CleanName(u.name),&u.error)) {gWar.homeTravel=false;gWar.rosterOffset=0;WarShow(WarPage::Lobby);gWar.notice="Connecting...";}
            else {gWar.notice=u.error;WarSound(3);}
        },idle&&InGame()&&wt::ValidAddress(ui.address));
        break;
    case WarPage::Practice:
        WarRow(30,0,std::string("Course   ")+royale::sandbox::kZones[gWar.zone].name,"Left / right chooses a sandbox test course",[](int d){gWar.zone=(gWar.zone+(d<0?-1:1)+royale::sandbox::kZoneCount)%royale::sandbox::kZoneCount;gWar.mapChangedAt=SDL_GetTicks();},true,true);
        WarRow(31,1,"ENTER TEST COURSE",royale::sandbox::kZones[gWar.zone].blurb,[](int){WarHost(true,true);},idle&&InGame());
        WarRow(34,2,"MINI BOSS ARENA","Flat ground, all twelve mini bosses, one encounter at a time",[](int){WarHost(true,true,royale::kMiniBossArenaIndex);},idle&&InGame());
        WarRow(35,3,"MAIN BOSS ARENA","A larger enclosed arena for the five main bosses",[](int){WarHost(true,true,royale::kMainBossArenaIndex);},idle&&InGame());
        WarRow(32,4,std::string("Battlefield   ")+royale::MapOf(ui.mapId).name,"Left / right chooses a battlefield for exploration",[](int d){WarMap(d);},true,true);
        WarRow(33,5,"EXPLORE BATTLEFIELD","Solo match with no bots. Loot, bosses and storm are active.",[](int){WarHost(true,false);},idle&&InGame());
        break;
    case WarPage::Character:
        WarRow(40,0,"Name   "+std::string(ui.name),"Edit your name for the next lobby",[](int){WarEdit(0);},idle);
        WarRow(41,1,std::string("Tunic   ")+(ui.skin<royale::kSkinCount?royale::kSkins[ui.skin].name:"Custom"),"Left / right changes your tunic",[](int d){Ui().skin=(Ui().skin+(d<0?-1:1)+royale::kSkinCount)%royale::kSkinCount;WarSave();},idle,true);
        { int pet=MapOption("LiloPet",false)?PetKind()+1:0; const char* names[]={"None","Lilo","Avriella","Maya"};
          WarRow(42,2,std::string("Companion   ")+names[std::clamp(pet,0,3)],"Your companion follows you in the world",[pet](int d){int n=(pet+(d<0?-1:1)+4)%4;CVarSetInteger(ROYALE_CVAR("LiloPet"),n!=0);CVarSetInteger(ROYALE_CVAR("PetKind"),std::max(0,n-1));WarSave();},true,true); }
        WarRow(44,3,"PETS & PREVIEW","Enable pets and preview each companion",[](int){WarShow(WarPage::Settings);gWar.section=6;});
        WarRow(43,4,"RETURN","Back to the game",[](int){WarOpen();});
        break;
    case WarPage::Settings: {
        const char* sections[]={"Audio","Graphics","Comfort","Match rules","Shadows & lights","Updates","Pets & preview","World & weather","Water","Combat effects","Advanced"};
        gWar.optionPages=1;
        const int menuPage=wt::SettingsPage(gWar.section);
        for(int row=0;row<wt::kSettingsPageSize;++row) {
            const int index=menuPage*wt::kSettingsPageSize+row;if(index>=wt::kSettingsCount)break;
            const int section=wt::kSettingsOrder[index];
            WarAdd(50+section,{28,108.0f+row*29,184,25},sections[section],"Choose a settings category",[section](int){gWar.section=section;gWar.optionPage=0;});
        }
        WarAdd(850,{28,282,86,22},"< BACK","Previous categories",[](int){gWar.section=wt::kSettingsOrder[0];gWar.optionPage=0;gWar.focus=4;},menuPage>0);
        WarAdd(851,{122,282,90,22},"MORE >","World, water, effects, advanced and updates",[](int){gWar.section=wt::kSettingsOrder[6];gWar.optionPage=0;gWar.focus=4;},menuPage==0);
        if(gWar.section==0) {
            WarOptionPager(2);
            if(gWar.optionPage==1) {
                WarSetting(0,"Master volume",CVAR_SETTING("Volume.Master"),40,0,100,5,"Overall game volume");
                WarSetting(1,"Fanfares",CVAR_SETTING("Volume.Fanfare"),100,0,100,5,"Victory and item music");
                WarSetting(2,"Ambient music",CVAR_SETTING("Volume.SubMusic"),100,0,100,5,"Secondary world music");
                WarSetting(3,"Lobby music folder",ROYALE_CVAR("LobbyMusic"),1,0,1,1,"Play your music folder while waiting");
                WarSetting(4,"OoT instruments",ROYALE_CVAR("OotConvert"),0,0,1,1,"Use game instruments for converted songs");
                WarRow(405,5,"RESCAN MUSIC FOLDER","Refresh locally installed music",[](int){gScanRequested=true;});
            } else {
            WarSetting(0,"Menu music",ROYALE_CVAR("MenuMusic"),70,0,100,5,"Original Ocarina of Time file-select music");
            WarSetting(1,"Menu sounds",ROYALE_CVAR("MenuSfx"),75,0,100,5,"Navigation, confirm, back and error sounds");
            WarSetting(2,"Game music",CVAR_SETTING("Volume.MainMusic"),100,0,100,5,"Music volume during gameplay");
            WarSetting(3,"Game effects",CVAR_SETTING("Volume.SFX"),100,0,100,5,"Gameplay sound effects");
            WarRow(404,4,std::string("Match music   ")+(ui.musicMode==0?"Original":ui.musicMode==1?"Music folder":"Off"),"Choose original music, your music folder or silence",[](int d){Ui().musicMode=(Ui().musicMode+(d<0?-1:1)+3)%3;WarSave();},true,true);
            }
        } else if(gWar.section==1) {
            WarOptionPager(2);
            if(gWar.optionPage==1) {
                WarSetting(0,"Cloth strength",ROYALE_CVAR("ClothPhysics"),100,0,200,10,"Amount of cloth motion");
                WarSetting(1,"Wind strength",ROYALE_CVAR("WindStrength"),100,0,200,10,"Strength of local wind animation");
                WarSetting(2,"Wind streaks",ROYALE_CVAR("WindStreaks"),1,0,1,1,"Show air trails that indicate wind");
                WarSetting(3,"Cloud amount",ROYALE_CVAR("SkyClouds"),100,0,200,10,"Density of moving sky clouds");
                WarSetting(4,"Fog amount",ROYALE_CVAR("FogAmount"),100,0,200,10,"Strength of atmospheric fog");
            } else {
            WarRow(400,0,"Weather detail   "+std::to_string(ui.weatherDensity)+"%","Particle density on this device",[](int d){Ui().weatherDensity=std::clamp(Ui().weatherDensity+(d<0?-25:25),0,200);WarSave();},true,true);
            WarRow(401,1,"Foliage   "+std::to_string(ui.foliage)+"%","Grass and foliage density",[](int d){Ui().foliage=std::clamp(Ui().foliage+(d<0?-25:25),0,200);WarSave();},true,true);
            WarRow(402,2,std::string("Cloth motion   ")+(ui.clothOn?"On":"Off"),"Animate cloth on hats and gliders",[](int){Ui().clothOn=!Ui().clothOn;WarSave();},true,true);
            WarRow(403,3,std::string("Wind   ")+(ui.windOn?"On":"Off"),"Wind movement in the world",[](int){Ui().windOn=!Ui().windOn;WarSave();},true,true);
            WarSetting(4,"Frame interpolation",CVAR_SETTING("InterpolationFPS"),60,20,120,10,"Higher values may need more GPU power");
            }
        } else if(gWar.section==2) {
            WarSetting(0,"Reduced menu motion",ROYALE_CVAR("ReducedMotion"),0,0,1,1,"Remove slides and selection pulses");
            WarSetting(1,"Show players on map",ROYALE_CVAR("MapPlayers"),1,0,1,1,"Show nearby known players on the minimap");
            WarSetting(2,"Show bots on map",ROYALE_CVAR("MapBots"),1,0,1,1,"Show nearby bots on the minimap");
            WarSetting(3,"Hit flashes",ROYALE_CVAR("HitFlash"),1,0,1,1,"Flash feedback when attacks connect");
            WarSetting(4,"Blood effects",ROYALE_CVAR("HitBlood"),1,0,1,1,"Optional combat blood effects");
            WarDebugToggle(5,kDbgMinimap);
        } else if(gWar.section==3) {
            const bool can=idle||(h.isHost&&h.state==royale::MatchState::Lobby&&!gPendingStart);
            WarOptionPager(2);
            if(gWar.optionPage==1) {
                const int season=std::clamp(ui.weatherSeason,0,static_cast<int>(royale::kSeasonRandom));
                WarRow(400,0,std::string("Season   ")+(season==royale::kSeasonRandom?"Random":royale::SeasonName(static_cast<royale::Season>(season))),"Choose the hosted match season",[](int d){Ui().weatherSeason=(Ui().weatherSeason+(d<0?-1:1)+5)%5;WarSave();},can,true);
                WarRow(401,1,"Weather changes   "+std::to_string(ui.weatherChange)+"%","How often hosted weather changes",[](int d){Ui().weatherChange=std::clamp(Ui().weatherChange+(d<0?-10:10),0,100);WarSave();},can,true);
                WarRow(402,2,std::string("Temple waiting room   ")+(ui.waitingRoom?"On":"Off"),"Use the Temple while waiting for your lobby",[](int){Ui().waitingRoom=!Ui().waitingRoom;WarSave();},idle,true);
            } else {
            WarRow(400,0,"Players   "+std::to_string(ui.playerLimit),"Empty places are filled with bots",[](int d){Ui().playerLimit=std::clamp(Ui().playerLimit+(d<0?-1:1),royale::kMinPlayers,royale::kMaxPlayers);WarSave();},can,true);
            const char* difficulty[]={"Easy","Normal","Hard"};
            WarRow(401,1,std::string("Bots   ")+difficulty[ui.botDifficulty],"Bot difficulty for hosted games",[](int d){Ui().botDifficulty=(Ui().botDifficulty+(d<0?-1:1)+3)%3;WarSave();},can,true);
            WarRow(402,2,std::string("Major boss   ")+(ui.majorBoss?"On":"Off"),"Allow the map's major boss",[](int){Ui().majorBoss=!Ui().majorBoss;WarSave();},can,true);
            WarRow(403,3,std::string("Automatic start   ")+(ui.autoStart?"On":"Off"),"Start after the lobby timer. Host can always start manually.",[](int){Ui().autoStart=!Ui().autoStart;WarSave();},can,true);
            WarRow(404,4,"Weather   "+std::to_string(ui.weatherIntensity)+"%","Weather strength for this match",[](int d){Ui().weatherIntensity=std::clamp(Ui().weatherIntensity+(d<0?-10:10),0,100);WarSave();},can,true);
            WarRow(405,5,"Host port   "+std::to_string(ui.port),"Change the port before creating a lobby",[](int){WarEdit(2);},idle);
            }
        } else if(gWar.section==4) {
            WarRow(400,0,std::string("Dynamic shadows   ")+(DebugOn(kDbgShadows)?"On":"Off"),"Shadows follow the sun, moon and nearby lights",[](int){gDebugOn[kDbgShadows]=!DebugOn(kDbgShadows);CVarSetInteger(ROYALE_CVAR("Debug.Shadows"),gDebugOn[kDbgShadows]);WarSave();},true,true);
            const int quality=std::clamp(CVarGetInteger(ROYALE_CVAR("ShadowQuality"),1),0,static_cast<int>(sh::Quality::Count)-1);
            WarRow(401,1,std::string("Quality   ")+sh::kQualityNames[quality],"Low suits handhelds. Higher presets add detail and distance.",[quality](int d){int q=(quality+(d<0?-1:1)+static_cast<int>(sh::Quality::Count))%static_cast<int>(sh::Quality::Count);CVarSetInteger(ROYALE_CVAR("ShadowQuality"),q);CVarSetInteger(ROYALE_CVAR("ShadowCustom"),0);gShadowCfgLoaded=false;WarSave();},true,true);
            WarSetting(2,"Shadow darkness",ROYALE_CVAR("ShadowDarkness"),100,0,200,10,"Strength of ground shadows");
            WarSetting(3,"Soft edges",ROYALE_CVAR("ShadowSoftness"),100,25,300,25,"Softness of shadow edges");
            WarSetting(4,"Dynamic lights",ROYALE_CVAR("DynLights"),1,0,1,1,"Light from explosions, spells and chests");
            WarSetting(5,"Building sunlight",ROYALE_CVAR("SunOnBuildings"),1,0,1,1,"Hyrule Kingdom lighting follows sun and moon");
        } else if(gWar.section==6) {
            auto row=[](int id,int line,std::string label,const char* hint,std::function<void(int)> action) {
                WarAdd(id,{393,115.0f+line*33,213,28},std::move(label),hint,std::move(action),true,true);
            };
            const bool enabled=MapOption("PetsEnabled",true),lobby=MapOption("LobbyAllPets",false),follow=MapOption("LiloPet",false);
            auto toggle=[](const char* key,bool value){
                CVarSetInteger(key,!value);WarSave();
                if(!MapOption("PetsEnabled",true)) {
                    StopVoice(kVoiceCat);StopVoice(kVoiceBaby);StopVoice(kVoiceMaya);StopVoice(kVoicePhone);
                    StopVoice(kVoiceToy);StopVoice(kVoiceToySfx);gBabyCoos.clear();
                }
            };
            row(420,0,std::string("Pets   ")+(enabled?"On":"Off"),"Master switch for all pet companions",[enabled,toggle](int){toggle(ROYALE_CVAR("PetsEnabled"),enabled);});
            row(421,1,std::string("Lobby pets   ")+(lobby?"On":"Off"),"All three companions play together in the lobby",[lobby,toggle](int){toggle(ROYALE_CVAR("LobbyAllPets"),lobby);});
            row(422,2,std::string("Follow me   ")+(follow?"On":"Off"),"Bring the selected companion into gameplay",[follow,toggle](int){toggle(ROYALE_CVAR("LiloPet"),follow);});
            const char* names[]={"Lilo","Avriella","Maya"};
            row(423,3,std::string("Companion   ")+names[PetKind()],"Left/right selects the pet shown in the preview",[](int d){CVarSetInteger(ROYALE_CVAR("PetKind"),(PetKind()+(d<0?-1:1)+3)%3);WarSave();});
            WarAdd(424,{393,255,213,28},std::string("Animate preview   ")+(gWar.previewAnimate?"On":"Off"),"Toggle preview motion without changing gameplay or menu settings",[](int){gWar.previewAnimate=!gWar.previewAnimate;});
        } else if(gWar.section==7) {
            WarOptions({
                {"Original OoT sky",ROYALE_CVAR("SkyOot"),1},{"Stars",ROYALE_CVAR("SkyStars"),1},{"Sun and moon",ROYALE_CVAR("SkyBodies"),1},
                {"Puddles",ROYALE_CVAR("GroundPuddles"),1},{"Ground snow",ROYALE_CVAR("GroundSnow"),1},{"Ground frost",ROYALE_CVAR("GroundFrost"),1},
                {"Fallen leaves",ROYALE_CVAR("GroundLeaves"),1},{"Footprints and trails",ROYALE_CVAR("GroundTrails"),1},{"Merge ground patches",ROYALE_CVAR("GroundMerge"),1},
                {"Ground detail",ROYALE_CVAR("GroundAmount"),100,0,200,10},{"Ground deformation",ROYALE_CVAR("GroundDeform"),100,0,200,10}});
        } else if(gWar.section==8) {
            WarOptions({
                {"Water detail (0-2)",ROYALE_CVAR("WaterDetail"),1,0,2,1},{"Wave height",ROYALE_CVAR("WaterWaves"),100,0,200,10},{"Water clarity",ROYALE_CVAR("WaterClarity"),100,0,200,10},
                {"Splashes and bubbles",ROYALE_CVAR("WaterFx"),1},{"Water life",ROYALE_CVAR("WaterLife"),1},{"Swimmer wakes",ROYALE_CVAR("WaterWakes"),1},
                {"Rain rings",ROYALE_CVAR("WaterRain"),1},{"Sky reflections",ROYALE_CVAR("WaterSky"),1},{"Character reflections",ROYALE_CVAR("WaterBodies"),1},
                {"Underwater effects",ROYALE_CVAR("WaterUnder"),1},{"Water currents",ROYALE_CVAR("WaterCurrent"),1},{"Choppy crests",ROYALE_CVAR("WaterChop"),60,0,200,10},
                {"Sun glints",ROYALE_CVAR("WaterGlints"),1},{"Glint strength",ROYALE_CVAR("WaterGlintAmt"),100,0,200,10},{"Caustics",ROYALE_CVAR("WaterCaustics"),1},
                {"Caustic strength",ROYALE_CVAR("WaterCausticAmt"),100,0,200,10},{"Shore foam",ROYALE_CVAR("WaterFoamTex"),1},{"Foam strength",ROYALE_CVAR("WaterFoamAmt"),100,0,200,10},
                {"Ripple simulation",ROYALE_CVAR("WaterRipples"),1},{"Underwater strength",ROYALE_CVAR("WaterUnderAmt"),100,0,200,10}});
        } else if(gWar.section==9) {
            WarOptions({
                {"Hit particles",ROYALE_CVAR("HitParticles"),1},{"Scale hit effects",ROYALE_CVAR("HitScaled"),1},{"Effects on your Link",ROYALE_CVAR("HitOnSelf"),1},
                {"Hit reactions",ROYALE_CVAR("HitReact"),1},{"Hit effect strength",ROYALE_CVAR("HitAmount"),100,0,200,10},
                {"Other weapon glow",ROYALE_CVAR("HeldGlow"),1},{"Your weapon glow",ROYALE_CVAR("HeldGlowSelf"),0},
                {"Enemies on map",ROYALE_CVAR("MapEnemies"),1},{"Lilo encounter",ROYALE_CVAR("LiloCat"),1}});
        } else if(gWar.section==10) {
            const int pages=(kDebugCount+5)/6;gWar.optionPage=std::clamp(gWar.optionPage,0,pages-1);
            for(int row=0;row<6;++row){int id=gWar.optionPage*6+row;if(id<kDebugCount)WarDebugToggle(row,id);}
            WarOptionPager(pages);
        } else {
#ifdef __ANDROID__
            const int state=UpdaterInt("getState");
            WarRow(400,0,"CHECK FOR UPDATES","Look for a new standard game build",[](int){UpdaterCall("check");},state!=kUpdChecking&&state!=kUpdDownloading);
            WarRow(401,1,"DOWNLOAD UPDATE","Download the available build",[](int){UpdaterCall("download");},state==kUpdChecked&&UpdaterInt("getLatestBuild")>OwnBuildNumber());
            WarRow(402,2,"INSTALL UPDATE","Open Android's installer",[](int){UpdaterCall("install");},state==kUpdNeedPermission||state==kUpdInstalling);
#endif
        }
        break; }
    case WarPage::Lobby:
        gWar.rosterOffset = std::clamp(gWar.rosterOffset,0,h.roster.empty()?0:static_cast<int>((h.roster.size()-1)/5)*5);
        if(joined) {
            WarRow(60,0,h.isHost?(gPendingStart?"PREPARING...":"START MATCH"):(h.selfReady?"READY - toggle":"READY UP"),h.isHost?"Travel to the battlefield and begin the countdown":"Tell the host you are ready",[h](int){if(h.isHost)gPendingStart=true;else gSession.SetReady(!h.selfReady);},h.isHost?wt::CanStart(joined,h.isHost,h.state==royale::MatchState::Lobby,InGame()&&h.haveSelf,gPendingStart):h.state==royale::MatchState::Lobby);
            WarRow(61,1,std::string("Map   ")+royale::MapOf(h.mapId).name,"Host chooses the battlefield",[](int d){WarMap(d);},h.isHost&&h.state==royale::MatchState::Lobby&&!gPendingStart,true);
            WarRow(62,2,"MATCH RULES","Player count, bots, weather and automatic start",[](int){WarShow(WarPage::Settings);gWar.section=3;});
            WarRow(63,3,"EXPLORE LOBBY","Close the menu and walk around. Start brings it back.",[](int){WarHide();});
        }
        if(h.roster.size()>5) {
            WarAdd(65,{35,251,27,23},"<","Previous players",[](int){gWar.rosterOffset=std::max(0,gWar.rosterOffset-5);},gWar.rosterOffset>0);
            WarAdd(66,{178,251,27,23},">","More players",[](int){gWar.rosterOffset+=5;},gWar.rosterOffset+5<static_cast<int>(h.roster.size()));
        }
        WarRow(64,5,joined?"QUIT TO MAIN MENU":"CANCEL CONNECTION","Leave this session and return to the War Table",[](int){if(gSession.Joined())WarShow(WarPage::Quit);else WarQuit();});
        break;
    case WarPage::Pause:
        WarAdd(70,{239,147,361,43},"RESUME MATCH","Return to the battlefield",[](int){WarHide();});
        WarAdd(72,{239,201,175,39},"SETTINGS","Audio, graphics and comfort",[](int){WarShow(WarPage::Settings);});
        WarAdd(73,{425,201,175,39},"HOW TO PLAY","Battle Royale controls",[](int){WarShow(WarPage::Guide);});
        WarAdd(71,{239,253,175,39},gSession.Sandbox()?"PRACTICE TOOLS":"CHARACTER","Equipment and character options",[](int){WarShow(gSession.Sandbox()?WarPage::Tools:WarPage::Character);});
        WarAdd(74,{425,253,175,39},"LEAVE MATCH","Return to the main menu",[](int){WarShow(WarPage::Quit);});
        break;
    case WarPage::Death:
        WarAdd(120,{56,251,250,42},"SPECTATE","Watch the remaining players; B also spectates",[](int){WarSpectate();});
        WarAdd(121,{333,251,250,42},"LEAVE MATCH","Return to the main menu",[](int){WarShow(WarPage::Quit);});
        break;
    case WarPage::Quit:
        WarRow(80,2,"STAY IN GAME","Keep playing",[](int){WarOpen();});
        WarRow(81,4,"QUIT TO MAIN MENU",h.isHost?"You are hosting. Leaving closes this match for everyone.":"Leave this match and return to the main screen",[](int){WarQuit();});
        break;
    case WarPage::Results:
        WarRow(92,3,"FULL MATCH RECAP","Standings, score breakdown, replay and spectating",[](int){gEnd.open=true;gEnd.at=ImGui::GetTime()-kEndPanelDelay;WarHide();});
        WarRow(90,4,"NEW MATCH","Host starts another match with the same lobby",[](int){gSession.RequestPlayAgain();WarHide();},h.isHost);
        WarRow(91,5,"MAIN MENU","Return to the War Table",[](int){WarQuit();});
        break;
    case WarPage::Tools: {
        const bool arenaMap=royale::IsBossArena(h.mapId);
        const bool arenaMajor=h.mapId==royale::kMainBossArenaIndex;
        const char* sections[]={arenaMap?"Arena":"Courses","Equipment",arenaMap?"Boss encounter":"Bots & bosses","World"};
        for(int i=0;i<4;++i) WarAdd(100+i,{28,112.0f+i*34,184,29},sections[i],"Practice tools",[i](int){gWar.section=i;});
        if(!gSession.Sandbox()) break;
        if(arenaMap && gWar.section==0) {
            WarRow(110,0,royale::MapOf(h.mapId).name,"Clear flat ground with a solid enclosing rim",[](int){},false);
            WarRow(111,1,"RETURN TO SPAWN","Return to the safe start pad",[](int){SandboxDo(SandboxCmd::Go,0);WarHide();});
            WarRow(112,2,"VISIT ARMORY","Every weapon and item for testing",[](int){SandboxDo(SandboxCmd::Go,1);WarHide();});
            WarRow(113,3,"GO TO CENTER","Teleport to the fight center",[](int){SandboxDo(SandboxCmd::Go,2);WarHide();});
        } else if(arenaMap && gWar.section==2) {
            if(royale::IsMajorKind(static_cast<royale::BossKind>(gWar.boss))!=arenaMajor) gWar.boss=arenaMajor?static_cast<int>(royale::BossKind::DragonFire):0;
            WarRow(110,0,royale::BossOf(static_cast<royale::BossKind>(gWar.boss)).name,"Left / right selects a boss for this arena",[arenaMajor](int d){
                do {gWar.boss=(gWar.boss+(d<0?-1:1)+royale::kBossKindCount)%royale::kBossKindCount;}
                while(royale::IsMajorKind(static_cast<royale::BossKind>(gWar.boss))!=arenaMajor);
            },true,true);
            WarRow(111,1,"START ENCOUNTER","Replace the previous boss and start from full health",[](int){gSbx.lastBoss=gWar.boss;SandboxDo(SandboxCmd::Encounter,gWar.boss);WarHide();});
            WarRow(112,2,"RESET FIGHT","Clear attacks and drops, heal, and restart the current boss",[](int){SandboxDo(SandboxCmd::Encounter,gSbx.lastBoss);WarHide();},gSbx.lastBoss>=0);
            WarRow(113,3,"CLEAR ARENA","Remove the encounter and return to spawn",[](int){SandboxDo(SandboxCmd::ArenaClear);WarHide();});
            WarRow(114,4,"VISIT ARMORY","Choose weapons and counters for the next fight",[](int){SandboxDo(SandboxCmd::Go,1);WarHide();});
        } else if(gWar.section==0) {
            WarRow(110,0,royale::sandbox::kZones[gWar.zone].name,royale::sandbox::kZones[gWar.zone].blurb,[](int d){gWar.zone=(gWar.zone+(d<0?-1:1)+royale::sandbox::kZoneCount)%royale::sandbox::kZoneCount;},true,true);
            WarRow(111,1,"GO TO COURSE","Teleport to the selected course",[](int){SandboxDo(SandboxCmd::Go,gWar.zone);WarHide();});
            WarRow(112,2,"LAUNCH GLIDER","Launch above your current position",[](int){SandboxDo(SandboxCmd::Glide);WarHide();});
            WarRow(113,3,"SPAWN BUGGY","Place a buggy in front of you",[](int){SandboxDo(SandboxCmd::Cart);WarHide();});
            WarRow(114,4,"REMOVE BUGGIES","Clear the practice vehicles",[](int){SandboxDo(SandboxCmd::ClearCarts);});
        } else if(gWar.section==1) {
            WarRow(110,0,royale::kItems[gWar.item].name,"Left / right chooses an item",[](int d){gWar.item=(gWar.item+(d<0?-1:1)+royale::kItemCount)%royale::kItemCount;},true,true);
            WarRow(111,1,"GIVE ITEM","Receive the chosen item at legendary rarity",[](int){SandboxDo(SandboxCmd::Give,gWar.item,4);});
            WarRow(112,2,"HEAL AND REFILL","Restore health and resources",[](int){SandboxDo(SandboxCmd::Heal);});
            WarRow(113,3,std::string("Invincible   ")+(gSbx.god?"On":"Off"),"Toggle practice invulnerability",[](int){gSbx.god=!gSbx.god;SandboxDo(SandboxCmd::God,gSbx.god);},true,true);
            WarRow(114,4,"RESTOCK LOOT PLAZA","Restore the equipment in the loot plaza",[](int){SandboxDo(SandboxCmd::Restock);});
            WarRow(115,5,"REVIVE","Get back up after being defeated",[](int){SandboxDo(SandboxCmd::Revive);},!h.selfAlive);
        } else if(gWar.section==2) {
            WarRow(110,0,royale::BossOf(static_cast<royale::BossKind>(gWar.boss)).name,"Choose a boss to test",[](int d){gWar.boss=(gWar.boss+(d<0?-1:1)+royale::kBossKindCount)%royale::kBossKindCount;},true,true);
            WarRow(111,1,"SPAWN BOSS","Spawn the selected boss in front of you",[](int){SandboxDo(SandboxCmd::Boss,gWar.boss);WarHide();});
            WarRow(112,2,"SPAWN BOT","Add one opponent",[](int){SandboxDo(SandboxCmd::Bot,1);});
            WarRow(113,3,std::string("Freeze bots   ")+(gSbx.botsStill?"On":"Off"),"Use bots as stationary targets",[](int){gSbx.botsStill=!gSbx.botsStill;SandboxDo(SandboxCmd::FreezeBots,gSbx.botsStill);},true,true);
            WarRow(114,4,"CLEAR OPPONENTS","Remove bots and bosses",[](int){SandboxDo(SandboxCmd::ClearBots);SandboxDo(SandboxCmd::ClearBosses);});
        } else {
            WarRow(110,0,std::string("Storm   ")+(gSbx.stormRuns?"Running":"Frozen"),"Let the storm advance or hold it still",[](int){gSbx.stormRuns=!gSbx.stormRuns;SandboxDo(SandboxCmd::StormRuns,gSbx.stormRuns);},true,true);
            WarRow(111,1,"SUPPLY DROP","Create a nearby supply drop",[](int){SandboxDo(SandboxCmd::Supply);});
            WarRow(112,2,"Time of day   "+std::to_string(static_cast<int>(gSbx.day*100))+"%","Scrub from morning to night",[](int d){gSbx.day=std::clamp(gSbx.day+(d<0?-0.1f:0.1f),0.0f,1.0f);},true,true);
            WarRow(113,3,std::string("Sky   ")+royale::SkyName(static_cast<royale::Sky>(gSbx.sky)),"Change practice weather",[](int d){gSbx.sky=(gSbx.sky+(d<0?-1:1)+royale::kSkyCount)%royale::kSkyCount;SandboxDo(SandboxCmd::Weather,gSbx.season,gSbx.sky,gSbx.intensity);},true,true);
        }
        break; }
    case WarPage::Guide: break;
    case WarPage::Keyboard: break;
    }
}

