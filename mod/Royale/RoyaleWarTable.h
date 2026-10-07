// Included once, inside RoyaleMod.cpp's integration namespace, after gameplay services.
// This UI emits native Fast3D display lists. It has no ImGui widgets or draw calls.
namespace wt = royale::wartable;
using WarPage = wt::Page;
struct WarControl { wt::Widget view; std::function<void(int)> act; };
struct WarState {
    bool open = true, booted = false, music = false, reduced = false;
    bool lastJoined = false, mouseDown = false, wasTouch = false, homeTravel = false;
    WarPage page = WarPage::Play, returnPage = WarPage::Play;
    int focus = 0, section = 0, zone = 0, item = 1, boss = 0, rosterOffset = 0;
    int keyboardTarget = 0, pendingZone = -1;
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
    static float pitch[] = {1.12f, 1.0f, 0.88f, 0.9f, 1.30f};
    static float volume = 0.75f;
    static const u16 sounds[] = {NA_SE_SY_FSEL_CURSOR, NA_SE_SY_FSEL_DECIDE_L, NA_SE_SY_FSEL_CLOSE,
                                 NA_SE_SY_FSEL_ERROR, NA_SE_SY_CURSOR};
    volume = std::clamp(CVarGetInteger(ROYALE_CVAR("MenuSfx"), 75), 0, 100) / 100.0f;
    kind = std::clamp(kind, 0, 4);
    if (volume > 0) Audio_PlaySoundGeneral(sounds[kind], &gSfxDefaultPos, 4, &pitch[kind], &volume, &gSfxDefaultReverb);
}
void WarShow(WarPage page) {
    gWar.open = true;
    gWar.page = page;
    gWar.focus = 4;
    gWar.section = 0;
    gWar.transition = gWar.reduced ? 1.0f : 0.0f;
    gWar.enteredAt = SDL_GetTicks();
    gWar.repeat = {};
    CloseEmoteWheel();
    Ship::Context::GetInstance()->GetWindow()->SetMouseCapture(false);
}
void WarHide() {
    gWar.open = false;
    gWar.repeat = {};
    if (Ship::Context::GetInstance()->GetWindow()->IsFullscreen())
        Ship::Context::GetInstance()->GetWindow()->SetMouseCapture(true);
}
void WarOpen() {
    const auto h = gSession.Hud();
    WarShow(!gSession.Joined() ? WarPage::Play : h.state == royale::MatchState::Lobby ? WarPage::Lobby : h.state == royale::MatchState::Ending ? WarPage::Results : WarPage::Pause);
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
void WarBack() {
    if (gWar.page == WarPage::Keyboard) { WarShow(gWar.returnPage); return; }
    if (gWar.page == WarPage::Quit) { WarOpen(); return; }
    if (gWar.page == WarPage::Play) return; // Home always remains available after launch.
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
void WarHost(bool solo, bool sandbox) {
    if (!InGame() || gSession.GetMode() != royale::RoyaleSession::Mode::Idle) return;
    auto& ui = Ui();
    ui.port = std::clamp(ui.port, 1024, 65535);
    WarSave();
    ui.error.clear();
    gSession.ClearLastEnded();
    if (!gSession.Host(static_cast<uint16_t>(ui.port), CleanName(ui.name), &ui.error, solo, sandbox)) {
        gWar.notice = "Could not open the lobby: " + ui.error;
        WarSound(3);
        return;
    }
    gWar.homeTravel = false;
    gWar.rosterOffset = 0;
    RefreshLocalAddresses(ui, true);
    gSoloStartWanted = solo;
    gWar.pendingZone = sandbox ? gWar.zone : -1;
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
void WarSetting(int row, const char* label, const char* key, int fallback, int low, int high, int step, const char* hint) {
    int n = std::clamp(CVarGetInteger(key, fallback), low, high);
    const bool toggle = low == 0 && high == 1;
    std::string text = std::string(label) + "   " + (toggle ? (n ? "On" : "Off") : std::to_string(n));
    WarRow(400+row,row,text,hint,[key,low,high,step,n](int direction) {
        int value = low == 0 && high == 1 ? !n : std::clamp(n+(direction < 0 ? -step : step),low,high);
        CVarSetInteger(key,value);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        if (std::strcmp(key,CVAR_SETTING("Volume.SFX")) == 0) Audio_SetGameVolume(SEQ_PLAYER_SFX,value/100.0f);
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
    const char* tabs[] = {"PLAY","CHARACTER","SETTINGS","GUIDE"};
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
        WarRow(32,3,std::string("Battlefield   ")+royale::MapOf(ui.mapId).name,"Left / right chooses a battlefield for exploration",[](int d){WarMap(d);},true,true);
        WarRow(33,4,"EXPLORE BATTLEFIELD","Solo match with no bots. Loot, bosses and storm are active.",[](int){WarHost(true,false);},idle&&InGame());
        break;
    case WarPage::Character:
        WarRow(40,0,"Name   "+std::string(ui.name),"Edit your name for the next lobby",[](int){WarEdit(0);},idle);
        WarRow(41,1,std::string("Tunic   ")+(ui.skin<royale::kSkinCount?royale::kSkins[ui.skin].name:"Custom"),"Left / right changes your tunic",[](int d){Ui().skin=(Ui().skin+(d<0?-1:1)+royale::kSkinCount)%royale::kSkinCount;WarSave();},idle,true);
        { int pet=MapOption("LiloPet",false)?PetKind()+1:0; const char* names[]={"None","Lilo","Avriella","Maya"};
          WarRow(42,2,std::string("Companion   ")+names[std::clamp(pet,0,3)],"Your companion follows you in the world",[pet](int d){int n=(pet+(d<0?-1:1)+4)%4;CVarSetInteger(ROYALE_CVAR("LiloPet"),n!=0);CVarSetInteger(ROYALE_CVAR("PetKind"),std::max(0,n-1));WarSave();},true,true); }
        WarRow(43,4,"RETURN","Back to the game",[](int){WarOpen();});
        break;
    case WarPage::Settings: {
        const char* sections[]={"Audio","Graphics","Comfort","Match rules","Updates"};
        for(int i=0;i<5;++i) WarAdd(50+i,{28,108.0f+i*34,184,29},sections[i],"Choose a settings category",[i](int){gWar.section=i;});
        if(gWar.section==0) {
            WarSetting(0,"Menu music",ROYALE_CVAR("MenuMusic"),70,0,100,5,"Original Ocarina of Time file-select music");
            WarSetting(1,"Menu sounds",ROYALE_CVAR("MenuSfx"),75,0,100,5,"Navigation, confirm, back and error sounds");
            WarSetting(2,"Game music",CVAR_SETTING("Volume.MainMusic"),100,0,100,5,"Music volume during gameplay");
            WarSetting(3,"Game effects",CVAR_SETTING("Volume.SFX"),100,0,100,5,"Gameplay sound effects");
            WarRow(404,4,std::string("Match music   ")+(ui.musicMode==0?"Original":ui.musicMode==1?"Music folder":"Off"),"Choose original music, your music folder or silence",[](int d){Ui().musicMode=(Ui().musicMode+(d<0?-1:1)+3)%3;WarSave();},true,true);
        } else if(gWar.section==1) {
            WarRow(400,0,"Weather detail   "+std::to_string(ui.weatherDensity)+"%","Particle density on this device",[](int d){Ui().weatherDensity=std::clamp(Ui().weatherDensity+(d<0?-25:25),0,200);WarSave();},true,true);
            WarRow(401,1,"Foliage   "+std::to_string(ui.foliage)+"%","Grass and foliage density",[](int d){Ui().foliage=std::clamp(Ui().foliage+(d<0?-25:25),0,200);WarSave();},true,true);
            WarRow(402,2,std::string("Cloth motion   ")+(ui.clothOn?"On":"Off"),"Animate cloth on hats and gliders",[](int){Ui().clothOn=!Ui().clothOn;WarSave();},true,true);
            WarRow(403,3,std::string("Wind   ")+(ui.windOn?"On":"Off"),"Wind movement in the world",[](int){Ui().windOn=!Ui().windOn;WarSave();},true,true);
            WarSetting(4,"Frame interpolation",CVAR_SETTING("InterpolationFPS"),60,20,120,10,"Higher values may need more GPU power");
        } else if(gWar.section==2) {
            WarSetting(0,"Reduced menu motion",ROYALE_CVAR("ReducedMotion"),0,0,1,1,"Remove slides and selection pulses");
            WarSetting(1,"Show players on map",ROYALE_CVAR("MapPlayers"),1,0,1,1,"Show nearby known players on the minimap");
            WarSetting(2,"Show bots on map",ROYALE_CVAR("MapBots"),1,0,1,1,"Show nearby bots on the minimap");
            WarSetting(3,"Hit flashes",ROYALE_CVAR("HitFlash"),1,0,1,1,"Flash feedback when attacks connect");
            WarSetting(4,"Blood effects",ROYALE_CVAR("HitBlood"),1,0,1,1,"Optional combat blood effects");
        } else if(gWar.section==3) {
            const bool can=idle||(h.isHost&&h.state==royale::MatchState::Lobby&&!gPendingStart);
            WarRow(400,0,"Players   "+std::to_string(ui.playerLimit),"Empty places are filled with bots",[](int d){Ui().playerLimit=std::clamp(Ui().playerLimit+(d<0?-1:1),royale::kMinPlayers,royale::kMaxPlayers);WarSave();},can,true);
            const char* difficulty[]={"Easy","Normal","Hard"};
            WarRow(401,1,std::string("Bots   ")+difficulty[ui.botDifficulty],"Bot difficulty for hosted games",[](int d){Ui().botDifficulty=(Ui().botDifficulty+(d<0?-1:1)+3)%3;WarSave();},can,true);
            WarRow(402,2,std::string("Major boss   ")+(ui.majorBoss?"On":"Off"),"Allow the map's major boss",[](int){Ui().majorBoss=!Ui().majorBoss;WarSave();},can,true);
            WarRow(403,3,std::string("Automatic start   ")+(ui.autoStart?"On":"Off"),"Start after the lobby timer. Host can always start manually.",[](int){Ui().autoStart=!Ui().autoStart;WarSave();},can,true);
            WarRow(404,4,"Weather   "+std::to_string(ui.weatherIntensity)+"%","Weather strength for this match",[](int d){Ui().weatherIntensity=std::clamp(Ui().weatherIntensity+(d<0?-10:10),0,100);WarSave();},can,true);
            WarRow(405,5,"Host port   "+std::to_string(ui.port),"Change the port before creating a lobby",[](int){WarEdit(2);},idle);
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
        WarRow(70,0,"RESUME", "Return to the battlefield",[](int){WarHide();});
        if(gSession.Sandbox()) WarRow(71,1,"PRACTICE TOOLS","Items, bots, bosses, courses and world controls",[](int){WarShow(WarPage::Tools);});
        WarRow(72,2,"SETTINGS","Audio, graphics and comfort settings",[](int){WarShow(WarPage::Settings);});
        WarRow(73,3,"HOW TO PLAY","Battle Royale controls and objectives",[](int){WarShow(WarPage::Guide);});
        WarRow(74,5,"QUIT TO MAIN MENU","Leave the match and return to the War Table",[](int){WarShow(WarPage::Quit);});
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
        const char* sections[]={"Courses","Equipment","Bots & bosses","World"};
        for(int i=0;i<4;++i) WarAdd(100+i,{28,112.0f+i*34,184,29},sections[i],"Practice tools",[i](int){gWar.section=i;});
        if(!gSession.Sandbox()) break;
        if(gWar.section==0) {
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

// Native Fast3D painter. Arrays live for two game frames, just like the engine's
// graphics buffers. No pointers into temporary strings/vertices reach the renderer.
struct WarPainter {
    struct Frame {
        std::array<Gfx,32768> commands;
        std::array<Vtx,16384> vertices;
        Mtx projection, identity;
        Vp viewport;
    };
    static Frame& Buffer() {
        static auto buffers = std::make_unique<std::array<Frame,2>>();
        static unsigned index = 0;
        return (*buffers)[++index%2];
    }
    Frame& f;
    Gfx* out;
    size_t count = 0;
    float offset = 0;
    WarPainter() : f(Buffer()), out(f.commands.data()) {
        const float aspect = std::max(0.1f,OTRGetAspectRatio());
        const float scale = std::min(1.0f,aspect/(16.0f/9.0f));
        // Fast3D already corrects triangle X by 4:3 / output aspect.
        guOrtho(&f.projection,-240/scale,240/scale,-180/scale,180/scale,-10,10,1);
        guMtxIdent(&f.identity);
        f.viewport = {{{640,480,G_MAXZ/2,0},{640,480,G_MAXZ/2,0}}};
        gDPPipeSync(out++);
        gSPViewport(out++,&f.viewport);
        gDPSetScissor(out++,G_SC_NON_INTERLACE,0,0,320,240);
        gSPMatrix(out++,&f.projection,G_MTX_PROJECTION|G_MTX_LOAD|G_MTX_NOPUSH);
        gSPMatrix(out++,&f.identity,G_MTX_MODELVIEW|G_MTX_LOAD|G_MTX_NOPUSH);
        gSPLoadGeometryMode(out++,G_SHADE|G_SHADING_SMOOTH);
        gDPSetCycleType(out++,G_CYC_1CYCLE);
        gDPSetRenderMode(out++,G_RM_XLU_SURF,G_RM_XLU_SURF2);
        gDPSetTexturePersp(out++,G_TP_NONE);
        gDPSetTextureLUT(out++,G_TT_NONE);
        gDPSetTextureFilter(out++,G_TF_BILERP);
        gDPSetAlphaCompare(out++,G_AC_NONE);
    }
    bool Space() const { return count+4<f.vertices.size() && out+48<f.commands.data()+f.commands.size(); }
    void Quad(wt::Rect r, int tw=1, int th=1) {
        if(!Space())return;
        Vtx* v=&f.vertices[count]; count+=4;
        const float xs[]={r.x,r.x+r.w,r.x+r.w,r.x};
        const float ys[]={r.y,r.y,r.y+r.h,r.y+r.h};
        for(int i=0;i<4;++i) {
            v[i]={}; v[i].v.ob[0]=static_cast<s16>(std::lround(xs[i]-320));
            v[i].v.ob[1]=static_cast<s16>(std::lround(180-ys[i]-offset));
            v[i].v.tc[0]=static_cast<s16>((i==1||i==2)?tw*32:0);
            v[i].v.tc[1]=static_cast<s16>(i>=2?th*32:0);
            v[i].v.cn[0]=v[i].v.cn[1]=v[i].v.cn[2]=v[i].v.cn[3]=255;
        }
        gSPVertex(out++,reinterpret_cast<uintptr_t>(v),4,0);
        gSP2Triangles(out++,0,1,2,0,0,2,3,0);
    }
    void Color(uint32_t rgba) { gDPSetPrimColor(out++,0,0,rgba>>24,(rgba>>16)&255,(rgba>>8)&255,rgba&255); }
    void Rect(wt::Rect r,uint32_t rgba) {
        if(!Space())return;
        gDPPipeSync(out++);gSPTexture(out++,0xffff,0xffff,0,G_TX_RENDERTILE,G_OFF);
        gDPSetCombineMode(out++,G_CC_PRIMITIVE,G_CC_PRIMITIVE);Color(rgba);Quad(r);
    }
    void Border(wt::Rect r,uint32_t color,float width=1) {
        Rect({r.x,r.y,r.w,width},color);Rect({r.x,r.y+r.h-width,r.w,width},color);
        Rect({r.x,r.y,width,r.h},color);Rect({r.x+r.w-width,r.y,width,r.h},color);
    }
    void Panel(wt::Rect r) {
        Rect({r.x+2,r.y+3,r.w,r.h},0x060F24C0);Rect(r,0x163871ED);
        Border(r,0x91B4D3CC);Border({r.x+3,r.y+3,r.w-6,r.h-6},0x426CA7AA);
        for(int corner=0;corner<4;++corner) {
            const float x=corner&1?r.x+r.w-7:r.x, y=corner&2?r.y+r.h-7:r.y;
            Rect({x,y,7,2},0xC5A45DFF);Rect({x,y,2,7},0xC5A45DFF);
        }
    }
    void Tile(const unsigned char* data,wt::Rect r,int w,int h,bool font=false,uint32_t color=0xffffffff) {
        if(!Space())return;
        gDPPipeSync(out++);gSPTexture(out++,0xffff,0xffff,0,G_TX_RENDERTILE,G_ON);
        gDPSetCombineMode(out++,G_CC_MODULATEIA_PRIM,G_CC_MODULATEIA_PRIM);Color(color);
        if(font) {gDPLoadTextureBlock(out++,data,G_IM_FMT_IA,G_IM_SIZ_8b,w,h,0,G_TX_CLAMP,G_TX_CLAMP,G_TX_NOMASK,G_TX_NOMASK,G_TX_NOLOD,G_TX_NOLOD);}
        else {gDPLoadTextureBlock(out++,data,G_IM_FMT_RGBA,G_IM_SIZ_16b,w,h,0,G_TX_CLAMP,G_TX_CLAMP,G_TX_NOMASK,G_TX_NOMASK,G_TX_NOLOD,G_TX_NOLOD);}
        Quad(r,w,h);
    }
    void Image(const unsigned char* data,wt::Rect r,int cols,int rows,uint32_t color=0xffffffff) {
        for(int y=0;y<rows;++y)for(int x=0;x<cols;++x)
            Tile(data+(y*cols+x)*2048,{r.x+r.w*x/cols,r.y+r.h*y/rows,r.w/cols,r.h/rows},32,32,false,color);
    }
    float TextWidth(const std::string& s,float size,bool heading=false) {
        const auto* widths=heading?wt::art::headingWidths:wt::art::bodyWidths;
        float w=0;for(unsigned char c:s)w+=widths[(c>=32&&c<128)?c-32:31]*(size/(heading?21.0f:18.0f));return w;
    }
    void Text(float x,float y,const std::string& s,float size=11,uint32_t color=0xF4E9CAFF,bool heading=false,float maxWidth=1000) {
        const auto* atlas=heading?wt::art::heading:wt::art::body;
        const auto* widths=heading?wt::art::headingWidths:wt::art::bodyWidths;
        float k=size/(heading?21.0f:18.0f), origin=x;
        for(unsigned char c:s) {
            if(c=='\n'){x=origin;y+=size+4;continue;}
            const int index=(c>=32&&c<128)?c-32:31;
            if(x+widths[index]*k>origin+maxWidth)break;
            if(c!=' ')Tile(atlas+index*24*32,{x,y,24*k,32*k},24,32,true,color);
            x+=widths[index]*k;
        }
    }
    void Wrap(float x,float y,const std::string& s,float width,float size=10,uint32_t color=0xBCD7EEFF,int maxLines=3) {
        std::istringstream words(s);std::string word,line;int lines=0;
        while(words>>word) {
            std::string next=line.empty()?word:line+" "+word;
            if(!line.empty()&&TextWidth(next,size)>width) {
                Text(x,y,line,size,color,false,width);y+=size+5;line=word;
                if(++lines>=maxLines)return;
            } else line=next;
        }
        if(lines<maxLines)Text(x,y,line,size,color,false,width);
    }
    void Focus(wt::Rect r) {
        float a=gWar.reduced?1.0f:0.78f+0.22f*std::sin(gWar.clock*3.5f);
        const uint32_t c=0xD8F3FF00|static_cast<uint32_t>(255*a);
        for(int i=0;i<4;++i) {
            const float x=i&1?r.x+r.w-9:r.x-2, y=i&2?r.y+r.h-1:r.y-2;
            Rect({x,y,11,3},c);Rect({i&1?r.x+r.w-1:r.x-2,i&2?r.y+r.h-9:r.y-2,3,11},c);
        }
    }
    void End(GameState* state) {
        gDPPipeSync(out++);gSPEndDisplayList(out++);
        OPEN_DISPS(state->gfxCtx);
        gSPDisplayList(OVERLAY_DISP++,f.commands.data());
        CLOSE_DISPS(state->gfxCtx);
    }
};

// Each preview has its own immutable cache address. Browsing maps never switches
// the live terrain or invalidates collision under a running match.
const unsigned char* WarMapPixels(int map) {
    static std::array<std::vector<unsigned char>,royale::kMapCount> cache;
    auto& pixels=cache[royale::ClampMap(map)];
    if(!pixels.empty())return pixels.data();
    constexpr int width=192,height=128;
    std::vector<unsigned char> rgba(width*height*3,0);
    const unsigned char* colours=nullptr;
    if(map==royale::kFortniteMapIndex)colours=royale::fortnite_data::kColours;
    if(map==royale::kConvergenceMapIndex)colours=royale::convergence::kColours;
    if(map==royale::kKingdomMapIndex)colours=royale::kingdom::kColours;
    if(map==royale::kSandboxMapIndex)colours=royale::sandbox::Terrain().colours.data();
    std::shared_ptr<Fast::Texture> original;
    if(!colours) {
        try { original=std::dynamic_pointer_cast<Fast::Texture>(Ship::Context::GetInstance()->GetResourceManager()->LoadResource(OverworldMinimapName(royale::MapOf(map).scene-SCENE_HYRULE_FIELD),true)); }
        catch(...) {}
    }
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        int r=169,g=193,b=207;
        if(colours) {
            constexpr int n=royale::fortnite::kFine+1;
            const int i=((y*(n-1)/(height-1))*n+x*(n-1)/(width-1))*3;
            // Blue parchment wash; terrain colours and coastline remain recognizable.
            r=(colours[i]*3+148)/4;g=(colours[i+1]*3+175)/4;b=(colours[i+2]*3+206)/4;
        } else if(original && original->ImageData && original->Type==Fast::TextureType::GrayscaleAlpha4bpp &&
                  original->ImageDataSize>=original->Width*original->Height/2 && original->Width&&original->Height) {
            const int ix=x*original->Width/width,iy=y*original->Height/height;
            const int i=iy*original->Width+ix;
            const uint8_t packed=original->ImageData[i/2],nib=(i&1)?packed&15:packed>>4;
            if(nib&1){r=47+(nib>>1)*8;g=83+(nib>>1)*9;b=125+(nib>>1)*9;}
        }
        const int grain=((x*17+y*29)%9)-4;
        const int i=(y*width+x)*3;rgba[i]=std::clamp(r+grain,0,255);rgba[i+1]=std::clamp(g+grain,0,255);rgba[i+2]=std::clamp(b+grain,0,255);
    }
    pixels.reserve(width*height*2);
    for(int ty=0;ty<height;ty+=32)for(int tx=0;tx<width;tx+=32)for(int y=0;y<32;++y)for(int x=0;x<32;++x) {
        const int i=((ty+y)*width+tx+x)*3;
        const uint16_t p=((rgba[i]>>3)<<11)|((rgba[i+1]>>3)<<6)|((rgba[i+2]>>3)<<1)|1;
        pixels.push_back(p>>8);pixels.push_back(p&255);
    }
    return pixels.data();
}

void WarPortrait() {
    if(!InGame())return;
    Player* player=GET_PLAYER(gPlayState);
    if(!player->skelAnime.skeleton || !player->skelAnime.jointTable)return;
    Vec3f pos={0,LINK_IS_ADULT?-180.0f:-130.0f,-40};
    Vec3s rot={0,static_cast<s16>(31000+(gWar.reduced?0:std::sin(gWar.clock*0.6f)*1100)),0};
    Vec3f eye={0,0,-400},at={0,0,0};
    static std::array<uint16_t,PAUSE_EQUIP_PLAYER_WIDTH*PAUSE_EQUIP_PLAYER_HEIGHT> color{},depth{};
    const bool changed=CVarGetInteger(CVAR_COSMETIC("Link.KokiriTunic.Changed"),0)!=0;
    const auto saved=CVarGetColor24(CVAR_COSMETIC("Link.KokiriTunic.Value"),Color_RGB8{30,105,27});
    CVarSetInteger(CVAR_COSMETIC("Link.KokiriTunic.Changed"),1);
    CVarSetColor24(CVAR_COSMETIC("Link.KokiriTunic.Value"),Color_RGB8{royale::RgbR(gLocalTunic),royale::RgbG(gLocalTunic),royale::RgbB(gLocalTunic)});
    OPEN_DISPS(gPlayState->state.gfxCtx);
    gsSPSetFB(WORK_DISP++,gPauseLinkFrameBuffer);
    CLOSE_DISPS(gPlayState->state.gfxCtx);
    Player_DrawPauseImpl(gPlayState,reinterpret_cast<void*>(gSegments[4]),reinterpret_cast<void*>(gSegments[6]),
                        &player->skelAnime,&pos,&rot,LINK_IS_ADULT?0.047f:0.046f,
                        LINK_IS_ADULT?PLAYER_SWORD_MASTER:PLAYER_SWORD_KOKIRI,PLAYER_TUNIC_KOKIRI,PLAYER_SHIELD_HYLIAN,PLAYER_BOOTS_KOKIRI,
                        PAUSE_EQUIP_PLAYER_WIDTH,PAUSE_EQUIP_PLAYER_HEIGHT,&eye,&at,60,color.data(),depth.data());
    OPEN_DISPS(gPlayState->state.gfxCtx);
    gsSPResetFB(WORK_DISP++);
    CLOSE_DISPS(gPlayState->state.gfxCtx);
    CVarSetInteger(CVAR_COSMETIC("Link.KokiriTunic.Changed"),changed);
    CVarSetColor24(CVAR_COSMETIC("Link.KokiriTunic.Value"),saved);
}

void WarDraw(GameState* state) {
    if(!gWar.open||!state)return;
    const auto h=gSession.Hud();
    const bool hero=gWar.page==WarPage::Play||gWar.page==WarPage::Character||gWar.page==WarPage::Pause;
    if(hero)WarPortrait();
    WarPainter p;
    p.Rect({-400,-400,1440,1160},0x060D20FF);
    p.Image(wt::art::background,{0,0,640,360},10,6);
    p.Text(26,20,"BATTLE ROYALE",26,0xF4E9CAFF,true);
    p.Text(28,48,"H Y R U L E   W A R   T A B L E",9,0xC5A45DFF);
    p.Text(498,28,gSession.Joined()?(h.isHost?"HOSTING":"CONNECTED"):"OFFLINE",10,0x9EDCFFFF);
    if(!gWar.bootError.empty()) {
        p.Panel({40,100,560,205});p.Text(60,120,"SAVE COULD NOT OPEN",18,0xF4E9CAFF,true);
        p.Wrap(60,154,gWar.bootError,518,12,0xF4E9CAFF,6);p.Text(60,266,"A / Enter: retry",12);p.End(state);return;
    }
    p.offset=(1-gWar.transition)*12;
    if(gWar.page==WarPage::Play) {
        p.Panel({24,102,136,207});p.Panel({169,102,273,207});p.Panel({449,102,168,207});
        p.Text(36,113,"ADVENTURER",12,0xF4E9CAFF,true);
        const float reveal=gWar.reduced?1:std::min(1.0f,(SDL_GetTicks()-gWar.mapChangedAt)/180.0f);
        p.Image(WarMapPixels(Ui().mapId),{177+(1-reveal)*8,112,257,153},6,4,0xFFFFFF00|static_cast<uint32_t>(255*reveal));
        p.Text(212,282,royale::MapOf(Ui().mapId).name,11,0xF4E9CAFF,true,190);
        p.Text(40,288,Ui().name,11,0x9EDCFFFF,false,112);
    } else if(gWar.page==WarPage::Keyboard) {
        p.Panel({82,100,476,209});p.Text(104,112,gWar.edit+"_",13,0xF4E9CAFF,false,428);
    } else {
        p.Panel({24,102,194,207});p.Panel({226,102,391,207});
        const char* titles[]={"PLAY","CHARACTER","SETTINGS","HOW TO PLAY","JOIN FRIENDS","PRACTICE","LOBBY","MATCH MENU","PRACTICE TOOLS","RESULTS","KEYBOARD","LEAVE MATCH?"};
        if(gWar.page!=WarPage::Settings&&gWar.page!=WarPage::Tools)p.Text(36,115,titles[static_cast<int>(gWar.page)],14,0xF4E9CAFF,true,174);
        if(gWar.page==WarPage::Join) p.Wrap(38,151,"Enter the host address shown in their lobby. On the same Wi-Fi, use their local address. Internet play requires a VPN or UDP forwarding.",155,11,0xBCD7EEFF,9);
        if(gWar.page==WarPage::Practice) {
            p.Image(WarMapPixels(royale::kSandboxMapIndex),{35,145,172,111},6,4);
            p.Wrap(38,265,"Twelve courses. Every item. Your own test ground.",161,10,0xBCD7EEFF,3);
        }
        if(gWar.page==WarPage::Quit) p.Wrap(38,153,h.isHost?"Leaving will close the hosted match for every player. Return to the main screen?":"Leave this match and return to the main screen?",159,12,0xF4E9CAFF,8);
        if(gWar.page==WarPage::Pause) {p.Text(38,283,"MATCH CONTINUES",10,0xC5A45DFF,true);}
        if(gWar.page==WarPage::Lobby) {
            p.Text(38,139,std::to_string(h.humanCount)+" players / "+std::to_string(h.playerLimit)+" slots",10);
            int y=160;for(size_t i=gWar.rosterOffset;i<h.roster.size()&&i<static_cast<size_t>(gWar.rosterOffset+5);++i){
                p.Text(38,y,(h.roster[i].host?"* ":h.roster[i].ready?"+ ":"  ")+h.roster[i].name,11,0xF4E9CAFF,false,160);y+=18;
            }
            if(h.isHost) {
                p.Text(68,257,"PORT "+std::to_string(h.hostPort),9,0xC5A45DFF);
                p.Text(38,279,Ui().localAddresses.empty()?"No LAN address":Ui().localAddresses.front(),10,0x9EDCFFFF,false,170);
            }
        }
        if(gWar.page==WarPage::Guide) {
            p.Wrap(38,150,"Drop into Hyrule, gather gear, stay inside the safe zone and be the last survivor.",154,12,0xBCD7EEFF,8);
            p.Text(244,121,"BATTLE ROYALE CONTROLS",15,0xF4E9CAFF,true);
            p.Wrap(244,155,"Move with the stick. Use your equipped weapon with B. Interact with A. Your current control prompts appear during play. Open this menu with Start. L / R changes menu pages; A selects, B goes back.",346,12,0xF4E9CAFF,8);
            p.Wrap(244,260,"Practice Tools lets you try equipment, bots, bosses, vehicles and gliding safely.",343,10,0x9EDCFFFF,2);
        }
        if(gWar.page==WarPage::Results) {
            p.Wrap(38,151,h.winnerName.empty()?"Match complete":h.winnerName+" wins!",155,16,0xC5A45DFF,3);
            int y=116;for(size_t i=0;i<h.results.size()&&i<5;++i){const auto& r=h.results[i];p.Text(244,y,std::to_string(r.placement)+"  "+r.name+"   "+std::to_string(r.kills)+" KOs",12,0xF4E9CAFF,false,351);y+=22;}
        }
        if(gWar.page==WarPage::Settings&&gWar.section==4) {
            p.Text(246,231,"VERSION " ROYALE_BUILD_VERSION,11,0xC5A45DFF);
#ifdef __ANDROID__
            p.Wrap(246,252,UpdaterString("getMessage"),340,10,0xBCD7EEFF,3);
#else
            p.Wrap(246,131,"Install a newer desktop build to update the game.",340,12,0xBCD7EEFF,3);
#endif
        }
    }
    if(hero&&InGame()) {
        const wt::Rect r=gWar.page==WarPage::Play?wt::Rect{38,133,108,146}:wt::Rect{44,146,145,128};
        gDPPipeSync(p.out++);gSPTexture(p.out++,0xffff,0xffff,0,G_TX_RENDERTILE,G_ON);
        gDPSetCombineMode(p.out++,G_CC_MODULATEIA_PRIM,G_CC_MODULATEIA_PRIM);p.Color(0xffffffff);
        gDPSetTileCustom(p.out++,G_IM_FMT_RGBA,G_IM_SIZ_16b,PAUSE_EQUIP_PLAYER_WIDTH,PAUSE_EQUIP_PLAYER_HEIGHT,0,G_TX_CLAMP,G_TX_CLAMP,G_TX_NOMASK,G_TX_NOMASK,G_TX_NOLOD,G_TX_NOLOD);
        gDPSetTextureImageFB(p.out++,G_IM_FMT_RGBA,G_IM_SIZ_16b,PAUSE_EQUIP_PLAYER_WIDTH,gPauseLinkFrameBuffer);
        p.Quad(r,PAUSE_EQUIP_PLAYER_WIDTH,PAUSE_EQUIP_PLAYER_HEIGHT);
    }
    for(size_t i=0;i<gWar.controls.size();++i) {
        const auto& w=gWar.controls[i].view;const bool selected=static_cast<int>(i)==gWar.focus;
        const uint32_t bg=!w.enabled?0x162644FF:selected?0x83C6EFFF:0x244F9EFF;
        p.Rect(w.rect,bg);p.Border(w.rect,selected?0xD5F1FFFF:0x527EBBFF);
        const float size=w.id>=1000?13:w.rect.w>300?12:w.rect.w>150?13:11;
        const uint32_t ink=!w.enabled?0x8497B2FF:selected?0x101D3DFF:0xF4E9CAFF;
        const float x=w.rect.w>300?w.rect.x+10:w.rect.x+std::max(5.0f,(w.rect.w-p.TextWidth(w.label,size,true))/2);
        p.Text(x,w.rect.y+(w.rect.h-size)/2-1,w.label,size,ink,true,w.rect.w-15);
    }
    if(!gWar.controls.empty())p.Focus(gWar.focusRect);
    p.offset=0;
    std::string hint=gWar.notice;
    if(hint.empty()&&!gWar.controls.empty())hint=gWar.controls[std::clamp(gWar.focus,0,static_cast<int>(gWar.controls.size())-1)].view.hint;
    p.Wrap(29,316,hint,582,10,0xC6E2F4FF,1);
    p.Text(30,339,"A  Select     B  Back     L / R  Pages     Start  Resume",10,0xF4E9CAFF);
    p.End(state);
}

void WarMusic() {
    if(!InGame())return;
    const bool want=gWar.open;
    if(want) {
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
    if(!joined&&gWar.lastJoined) {
        const std::string reason=gSession.LastEnded();
        WarQuit();gWar.notice=reason;
    }
    if(joined&&!gWar.lastJoined) { gWar.notice.clear(); if(!gSession.SoloTest())WarShow(WarPage::Lobby); }
    gWar.lastJoined=joined;
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
        if(gWar.open&&(gWar.page==WarPage::Pause||gWar.page==WarPage::Lobby))WarHide();
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

bool WarPrepareSave() {
    try {
        SaveManager* saves=SaveManager::Instance;
        if(!saves){gWar.bootError="The save service is not ready. Press A to retry.";return false;}
        saves->ThreadPoolWait();
        const auto folder=std::filesystem::path(Ship::Context::GetPathRelativeToAppDirectory("Save"));
        std::filesystem::create_directories(folder);
        const auto path=folder/"battle-royale.sav";
        if(std::filesystem::exists(path)) {
            std::ifstream input(path);
            const auto json=nlohmann::json::parse(input);
            if(json.value("version",0)!=1 || !json.contains("sections") || !json["sections"].contains("base") ||
               !json["sections"]["base"].contains("data") || !json["sections"]["base"]["data"].is_object()) {
                gWar.bootError="The Battle Royale save is not a supported save file. It has been preserved. Restore a backup of Save/battle-royale.sav, then retry.";
                return false;
            }
            const int version=json["sections"]["base"].value("version",0);
            if(version<1||version>4){gWar.bootError="This Battle Royale save needs a different game version. The file has been preserved.";return false;}
        }
        saves->UseBattleRoyaleFile();
        if(!std::filesystem::exists(path)) {
            saves->InitFile(false);
            gSaveContext.fileNum=0;
            gSaveContext.ship.quest.id=QUEST_BATTLEROYALE;
            gSaveContext.linkAge=LINK_AGE_ADULT;
            gSaveContext.entranceIndex=ENTR_TEMPLE_OF_TIME_ENTRANCE;
            gSaveContext.savedSceneNum=SCENE_TEMPLE_OF_TIME;
            gSaveContext.cutsceneIndex=0;
            gSaveContext.healthCapacity=gSaveContext.health=7*16;
            gSaveContext.equips.buttonItems[0]=ITEM_SWORD_MASTER;
            gSaveContext.inventory.equipment|=OWNED_EQUIP_FLAG(EQUIP_TYPE_SWORD,EQUIP_INV_SWORD_MASTER);
            gSaveContext.equips.equipment=(gSaveContext.equips.equipment&~0xf)|EQUIP_VALUE_SWORD_MASTER;
            saves->SaveFile(0);saves->ThreadPoolWait();
            if(!std::filesystem::exists(path)){gWar.bootError="Could not write Save/battle-royale.sav. Check free space and folder access, then retry.";return false;}
        }
        // Exercise the engine section loaders before committing to the play-state transition.
        // The native-save patch propagates errors and preserves the original file.
        saves->LoadFile(0);
        CVarSetInteger(CVAR_DEVELOPER_TOOLS("DebugEnabled"),0);
        CVarSetInteger(ROYALE_CVAR("BRFile0"),1);
        gWar.bootError.clear();return true;
    } catch(const std::exception& e) {
        gWar.bootError=std::string("The Battle Royale save could not be opened. Existing saves were preserved. ")+e.what();
        return false;
    }
}
