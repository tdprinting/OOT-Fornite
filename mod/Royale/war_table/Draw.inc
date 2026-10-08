void WarDraw(GameState* state) {
    if(!gWar.open||!state)return;
    DrawIdentity menuIdentity(&gWar, 0, 0);
    const auto h=gSession.Hud();
    const bool hero=gWar.page==WarPage::Play||gWar.page==WarPage::Character;
    if(hero)WarPortrait();
    WarPainter p;
    p.Rect({-400,-400,1440,1160},0x060D20FF);
    p.Image(wt::art::background,{0,0,640,360},10,6);
    p.Text(26,20,"BATTLE ROYALE",26,wt::theme::Ink,true);
    p.Text(28,48,"H Y R U L E   W A R   T A B L E",9,wt::theme::Gold);
    p.Text(498,28,gSession.Joined()?(h.isHost?"HOSTING":"CONNECTED"):"OFFLINE",10,0x9EDCFFFF);
    if(!gWar.bootError.empty()) {
        p.Panel({40,100,560,205});p.Text(60,120,"SAVE COULD NOT OPEN",18,wt::theme::Ink,true);
        p.Wrap(60,154,gWar.bootError,518,12,wt::theme::Ink,6);p.Text(60,266,"A / Enter: retry",12);p.End(state);return;
    }
    p.offset=(1-gWar.transition)*12;
    if(gWar.page==WarPage::Play) {
        p.Panel({24,102,136,207});p.Panel({169,102,273,207});p.Panel({449,102,168,207});
        p.Text(36,113,"ADVENTURER",12,wt::theme::Ink,true);
        const float reveal=gWar.reduced?1:std::min(1.0f,(SDL_GetTicks()-gWar.mapChangedAt)/180.0f);
        p.Image(WarMapPixels(Ui().mapId),{177+(1-reveal)*8,112,257,153},6,4,0xFFFFFF00|static_cast<uint32_t>(255*reveal));
        p.Text(212,282,royale::MapOf(Ui().mapId).name,11,wt::theme::Ink,true,190);
        p.Text(40,288,Ui().name,11,0x9EDCFFFF,false,112);
    } else if(gWar.page==WarPage::Death) {
        p.Panel({40,103,560,204});
        p.Text(59,114,"ELIMINATED",24,wt::theme::Ink,true);
        p.Text(471,118,"#"+std::to_string(gDeath.place),24,wt::theme::Gold,true,110);
        p.Text(61,149,gDeath.by.empty()?"Your battle is over. The match continues.":gDeath.by,11,wt::theme::Muted,false,516);
        char damage[20];std::snprintf(damage,sizeof(damage),"%.1f",gMyStats.damage);
        const std::string values[]={std::to_string(gMyStats.kills),damage,UiThousands(royale::ScorePoints(gMyStats.damage,gMyStats.kills,gMyStats.chests,gDeath.place))};
        const char* labels[]={"ELIMINATIONS","DAMAGE DEALT","MATCH POINTS"};
        for(int i=0;i<3;++i) {
            const float x=58+i*180.0f;
            p.Rect({x,177,164,60},wt::theme::Button);p.Border({x,177,164,60},0x527EBBFF);
            p.Text(x+12,184,values[i],22,i==2?wt::theme::Gold:wt::theme::Ink,true,140);
            p.Text(x+12,215,labels[i],9,wt::theme::Muted,true,140);
        }
    } else if(gWar.page==WarPage::Keyboard) {
        p.Panel({82,100,476,209});p.Text(104,112,gWar.edit+"_",13,wt::theme::Ink,false,428);
    } else {
        p.Panel({24,102,194,207});p.Panel({226,102,391,207});
        const char* titles[]={"PLAY","CHARACTER","SETTINGS","HOW TO PLAY","JOIN FRIENDS","PRACTICE","LOBBY","MATCH MENU","PRACTICE TOOLS","RESULTS","KEYBOARD","LEAVE MATCH?","ELIMINATED"};
        if(gWar.page!=WarPage::Settings&&gWar.page!=WarPage::Tools)p.Text(36,115,titles[static_cast<int>(gWar.page)],14,wt::theme::Ink,true,174);
        if(gWar.page==WarPage::Join) p.Wrap(38,151,"Enter the host address shown in their lobby. On the same Wi-Fi, use their local address. Internet play requires a VPN or UDP forwarding.",155,11,wt::theme::Muted,9);
        if(gWar.page==WarPage::Practice) {
            p.Image(WarMapPixels(royale::kSandboxMapIndex),{35,145,172,111},6,4);
            p.Wrap(38,265,"Twelve courses. Every item. Your own test ground.",161,10,wt::theme::Muted,3);
        }
        if(gWar.page==WarPage::Quit) p.Wrap(38,153,h.isHost?"Leaving will close the hosted match for every player. Return to the main screen?":"Leave this match and return to the main screen?",159,12,wt::theme::Ink,8);
        if(gWar.page==WarPage::Pause) {
            royale::PlayerState loadout;
            loadout.gearMask=h.inv.gearMask;
            for(int i=0;i<royale::kGearSlots;++i) {
                loadout.gear[i].item=static_cast<royale::ItemId>(h.inv.gear[i].item);
                loadout.gear[i].rarity=static_cast<royale::Rarity>(h.inv.gear[i].rarity);
            }
            const auto gear=royale::TotalsOf(loadout);
            const auto ammo=royale::AmmoUsedBy(h.weapon);
            const bool hasAmmo=ammo==royale::AmmoKind::None || h.ammo[static_cast<size_t>(ammo)]>0;
            const auto stats=wt::Loadout(h.weapon,h.weaponRarity,hasAmmo,gear.melee,gear.ranged,gear.damageTaken,
                h.hasShield?royale::ShieldReduction(h.shield,h.shieldRarity):0,h.adultLeft>0);
            char health[48],shield[48],attack[48],defense[48],magic[48];
            std::snprintf(health,sizeof(health),"Health   %.1f / %.1f",h.selfHealth,h.maxHealth);
            std::snprintf(shield,sizeof(shield),"Shield   %.1f / %.1f",h.inv.shield,royale::kMaxShield);
            std::snprintf(attack,sizeof(attack),"Attack   %.2f / hit",stats.attack);
            std::snprintf(defense,sizeof(defense),"Defense   %.0f%%",stats.defense);
            std::snprintf(magic,sizeof(magic),"Magic   %.0f / %.0f",h.magic,royale::kMaxMagic);
            const char* rows[]={health,shield,attack,defense,magic};
            for(int i=0;i<5;++i)p.Text(37,142+i*23.0f,rows[i],11,wt::theme::Ink,false,171);
            p.Text(37,259,std::to_string(h.alive)+" left / "+std::to_string(gMyStats.kills)+" KOs",10,wt::theme::Muted,false,172);
            p.Text(38,286,"MATCH CONTINUES",10,wt::theme::Gold,true);
            p.Text(239,117,"BATTLE ROYALE",16,wt::theme::Ink,true);
            p.Text(239,135,royale::MapOf(h.mapId).name,8,wt::theme::Muted,false,352);
        }
        if(gWar.page==WarPage::Lobby) {
            p.Text(38,139,std::to_string(h.humanCount)+" players / "+std::to_string(h.playerLimit)+" slots",10);
            int y=160;for(size_t i=gWar.rosterOffset;i<h.roster.size()&&i<static_cast<size_t>(gWar.rosterOffset+5);++i){
                p.Text(38,y,(h.roster[i].host?"* ":h.roster[i].ready?"+ ":"  ")+h.roster[i].name,11,wt::theme::Ink,false,160);y+=18;
            }
            if(h.isHost) {
                p.Text(68,257,"PORT "+std::to_string(h.hostPort),9,wt::theme::Gold);
                p.Text(38,279,Ui().localAddresses.empty()?"No LAN address":Ui().localAddresses.front(),10,0x9EDCFFFF,false,170);
            }
        }
        if(gWar.page==WarPage::Guide) {
            p.Wrap(38,150,"Drop into Hyrule, gather gear, stay inside the safe zone and be the last survivor.",154,12,wt::theme::Muted,8);
            p.Text(244,121,"BATTLE ROYALE CONTROLS",15,wt::theme::Ink,true);
            p.Wrap(244,155,"Move with the stick. Use your equipped weapon with B. Interact with A. Your current control prompts appear during play. Open this menu with Start. L / R changes menu pages; A selects, B goes back.",346,12,wt::theme::Ink,8);
            p.Wrap(244,260,"Practice Tools lets you try equipment, bots, bosses, vehicles and gliding safely.",343,10,0x9EDCFFFF,2);
        }
        if(gWar.page==WarPage::Results) {
            p.Wrap(38,151,h.winnerName.empty()?"Match complete":h.winnerName+" wins!",155,16,wt::theme::Gold,3);
            int y=116;for(size_t i=0;i<h.results.size()&&i<5;++i){const auto& r=h.results[i];p.Text(244,y,std::to_string(r.placement)+"  "+r.name+"   "+std::to_string(r.kills)+" KOs",12,wt::theme::Ink,false,351);y+=22;}
        }
        if(gWar.page==WarPage::Settings&&gWar.section==5) {
            p.Text(246,231,"VERSION " ROYALE_BUILD_VERSION,11,wt::theme::Gold);
#ifdef __ANDROID__
            p.Wrap(246,252,UpdaterString("getMessage"),340,10,wt::theme::Muted,3);
#else
            p.Wrap(246,131,"Install a newer desktop build to update the game.",340,12,wt::theme::Muted,3);
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
        const uint32_t bg=!w.enabled?0x162644FF:selected?0x83C6EFFF:wt::theme::Button;
        p.Rect(w.rect,bg);p.Border(w.rect,selected?0xD5F1FFFF:0x527EBBFF);
        const float size=w.id>=1000?13:w.rect.w>300?12:w.rect.w>150?13:11;
        const uint32_t ink=!w.enabled?0x8497B2FF:selected?0x101D3DFF:wt::theme::Ink;
        const float x=w.rect.w>300?w.rect.x+10:w.rect.x+std::max(5.0f,(w.rect.w-p.TextWidth(w.label,size,true))/2);
        p.Text(x,w.rect.y+(w.rect.h-size)/2-1,w.label,size,ink,true,w.rect.w-15);
    }
    if(!gWar.controls.empty())p.Focus(gWar.focusRect);
    p.offset=0;
    std::string hint=gWar.notice;
    if(hint.empty()&&!gWar.controls.empty())hint=gWar.controls[std::clamp(gWar.focus,0,static_cast<int>(gWar.controls.size())-1)].view.hint;
    p.Wrap(29,316,hint,582,10,0xC6E2F4FF,1);
    p.Text(30,339,gWar.page==WarPage::Death?"A  Select     B  Spectate     L / R  Pages":"A  Select     B  Back     L / R  Pages     Start  Menu",10,wt::theme::Ink);
    p.End(state);
}

