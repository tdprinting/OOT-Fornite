// ---- motion: drifting motes, light shafts, sparkles and tap ripples ----
uint32_t WarFade(uint32_t rgba, float alpha) {
    return (rgba&0xFFFFFF00u)|static_cast<uint32_t>((rgba&255u)*std::clamp(alpha,0.0f,1.0f));
}
float WarEase(float t) { t=std::clamp(t,0.0f,1.0f); return 1-(1-t)*(1-t)*(1-t); }
// A four-point twinkle, the same shape as the sparkles on OoT's fairies and item gets.
void WarSparkle(WarPainter& p,float x,float y,float size,uint32_t colour) {
    p.Rect({x-size,y-0.75f,size*2,1.5f},colour);
    p.Rect({x-0.75f,y-size,1.5f,size*2},colour);
    p.Rect({x-size*0.45f,y-size*0.45f,size*0.9f,size*0.9f},WarFade(colour,0.55f));
}
// Soft teal light shafts falling through the dark, like the stylised Zelda forests and temples.
void WarShafts(WarPainter& p,float t) {
    for(int i=0;i<5;++i) {
        const float x=60+i*128+std::sin(t*0.23f+i*1.9f)*26, width=16+i%3*7;
        const float pulse=0.55f+0.45f*std::sin(t*0.4f+i*2.3f);
        for(int band=0;band<6;++band) {
            const float a=(0.075f-band*0.011f)*pulse;
            p.Rect({x+band*3.0f,band*40.0f,width,40},WarFade(0x6FD8D0FF,a));
        }
    }
}
// Fairy-like motes rising from the bottom; gold ones are bigger and twinkle.
void WarMotes(WarPainter& p,float t) {
    for(int i=0;i<30;++i) {
        const float speed=5+(i*7%11)*1.6f, life=380;
        const float y=360-std::fmod(t*speed+i*61.0f,life)+10;
        const float x=std::fmod(i*97.0f+31.0f,640.0f)+std::sin(t*0.6f+i*1.3f)*(8+i%4*4);
        const float edge=std::min(1.0f,std::min((360-y)/50,y/60));
        if(edge<=0)continue;
        const bool gold=i%4==0;
        const float twinkle=0.55f+0.45f*std::sin(t*(2.2f+i%3)+i);
        const uint32_t colour=gold?0xF7D774FF:(i%3==0?0x9EDCFFFF:0x7FE6D8FF);
        if(gold) {
            p.Rect({x-3,y-3,6,6},WarFade(colour,0.10f*edge*twinkle));
            WarSparkle(p,x,y,2.6f+twinkle*1.6f,WarFade(colour,0.85f*edge*twinkle));
        } else {
            const float size=1.2f+(i%3)*0.5f;
            p.Rect({x-size,y-size,size*2,size*2},WarFade(colour,0.55f*edge*twinkle));
        }
    }
}
// A star now and then crosses the corner of the screen.
void WarShootingStar(WarPainter& p,float t) {
    const float cycle=std::fmod(t,9.0f);
    if(cycle>0.9f)return;
    const float u=cycle/0.9f, x=470+u*190-95, y=20+u*70;
    for(int i=0;i<9;++i) {
        const float k=i/9.0f;
        p.Rect({x-i*7,y-i*3.5f,3.5f-k*2,2.2f-k*1.2f},WarFade(0xF4E9CAFF,(1-k)*0.9f*std::sin(u*3.14159f)));
    }
}
void WarRipples(WarPainter& p) {
    auto& list=gWar.ripples;
    list.erase(std::remove_if(list.begin(),list.end(),[](const WarState::Ripple& r){return gWar.clock-r.born>0.5f;}),list.end());
    for(const auto& r:list) {
        const float age=(gWar.clock-r.born)/0.5f, radius=5+WarEase(age)*34, fade=1-age;
        p.Border({r.x-radius,r.y-radius,radius*2,radius*2},WarFade(0xD8F3FFFF,0.7f*fade),1.5f);
        p.Border({r.x-radius*0.6f,r.y-radius*0.6f,radius*1.2f,radius*1.2f},WarFade(0xF7D774FF,0.5f*fade),1);
    }
}

void WarDraw(GameState* state) {
    if(!gWar.open||!state)return;
    DrawIdentity menuIdentity(&gWar, 0, 0);
    const auto h=gSession.Hud();
    const bool hero=gWar.page==WarPage::Play||gWar.page==WarPage::Character;
    if(hero)WarPortrait();
    const bool pets=gWar.page==WarPage::Settings&&gWar.section==6;
    const bool petImage=pets&&WarPetPreview();
    WarPainter p;
    p.Rect({-400,-400,1440,1160},0x060D20FF);
    p.Image(wt::art::background,{0,0,640,360},10,6);
    if(!gWar.reduced) {WarShafts(p,gWar.clock);WarMotes(p,gWar.clock);WarShootingStar(p,gWar.clock);}
    // the TDawgs logo takes the header's left corner: DrawMenuLogo (Effects.inc) paints it over the menu, in the box x 18..178, y 6..90 of this 640 x 360 layout
    p.Text(190,48,"H Y R U L E   W A R   T A B L E",9,wt::theme::Gold);
    p.Text(498,24,gSession.Joined()?(h.isHost?"HOSTING":"CONNECTED"):"OFFLINE",10,0x9EDCFFFF);
    p.Text(498,38,"VERSION " ROYALE_BUILD_VERSION,9,wt::theme::Gold);
    if(!gWar.bootError.empty()) {
        p.Panel({40,100,560,205});p.Text(60,120,"SAVE COULD NOT OPEN",18,wt::theme::Ink,true);
        p.Wrap(60,154,gWar.bootError,518,12,wt::theme::Ink,6);p.Text(60,266,"A / Enter: retry",12);p.End(state);return;
    }
    p.offset=(1-gWar.transition)*(1-gWar.transition)*18;
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
        if(gWar.page!=WarPage::Settings&&gWar.page!=WarPage::Tools&&gWar.page!=WarPage::Pause)p.Text(36,115,titles[static_cast<int>(gWar.page)],14,wt::theme::Ink,true,174);
        if(pets) {
            const char* names[]={"Lilo","Avriella","Maya"};
            p.Text(240,114,"PET PREVIEW",12,wt::theme::Gold,true);
            p.Border({239,139,143,143},wt::theme::Border);
            if(petImage)WarPetImage(p,{242,142,137,137});
            else p.Wrap(249,178,"Preview available once the game is ready.",120,11,wt::theme::Muted,4);
            p.Text(248,286,names[PetKind()],11,wt::theme::Ink,true);
        }
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
            char attack[48],defense[48],speed[48],left[48];
            std::snprintf(attack,sizeof(attack),"Attack   %.2f / hit",stats.attack);
            std::snprintf(defense,sizeof(defense),"Defense   %.0f%%",stats.defense);
            std::snprintf(speed,sizeof(speed),"Speed   x%.1f",h.speedMult);
            p.Text(36,112,"LINK",12,wt::theme::Ink,true);
            p.Text(36+p.TextWidth("LINK",12,true)+8,114,royale::MapOf(h.mapId).name,8,wt::theme::Muted,false,206-(36+p.TextWidth("LINK",12,true)+8));
            const int hearts=std::clamp(static_cast<int>(std::ceil(h.maxHealth)),1,10);
            for(int i=0;i<hearts;++i) {
                const float amount=std::clamp(h.selfHealth-i,0.0f,1.0f);
                p.Heart(36+(i%5)*14.0f,130+(i/5)*14.0f,13,amount<=0?0:std::clamp(static_cast<int>(std::ceil(amount*4)),1,4));
            }
            const float shieldFill=std::clamp(h.inv.shield/royale::kMaxShield,0.0f,1.0f), magicFill=std::clamp(h.magic/royale::kMaxMagic,0.0f,1.0f);
            p.Text(36,160,"SHIELD",8,wt::theme::Muted);p.Meter({36,169,150,11},shieldFill,0x55A5F5FF);
            p.Text(36,185,"MAGIC",8,wt::theme::Muted);p.Meter({36,194,150,11},magicFill,0x3CD255FF);
            const std::string sv=std::to_string(static_cast<int>(shieldFill*100)), mv=std::to_string(static_cast<int>(h.magic));
            p.Text(208-p.TextWidth(sv,9),170,sv,9,wt::theme::Ink);p.Text(208-p.TextWidth(mv,9),195,mv,9,wt::theme::Ink);
            const char* rows[]={attack,defense,speed};
            for(int i=0;i<3;++i)p.Text(36,216+i*15.0f,rows[i],11,wt::theme::Ink,false,171);
            p.Rect({39,269,4,4},0x6EF082FF);p.Rect({36,273,10,8},0x6EF082FF);p.Rect({39,281,4,4},0x6EF082FF);
            char rupees[16];std::snprintf(rupees,sizeof(rupees),"%04d",std::clamp(h.rupees,0,9999));
            p.Text(54,266,rupees,16,wt::theme::Gold,true);
            std::snprintf(left,sizeof(left),"%d left   %d KOs",h.alive,gMyStats.kills);
            p.Text(36,290,left,10,wt::theme::Muted,false,172);
            // Right panel: heading and stat chips.
            p.Text(238,110,"MATCH MENU",14,wt::theme::Ink,true);
            const bool finalZone=h.stormPhase>=royale::kStormPhaseCount;
            struct Chip { std::string text; uint32_t color; };
            const Chip chips[]={{finalZone?"FINAL ZONE":"ZONE "+ClockText(h.stormSecondsLeft),h.stormShrinking||h.stormSecondsLeft<=5?0xFF5A5AFFu:wt::theme::Gold},
                {"ALIVE "+std::to_string(h.alive),wt::theme::Ink},{"KOs "+std::to_string(gMyStats.kills),wt::theme::Ink}};
            float cx=606;
            for(int i=2;i>=0;--i){cx-=p.TextWidth(chips[i].text,10);p.Text(cx,113,chips[i].text,10,chips[i].color);cx-=14;}
            // Equipment row, OoT item-subscreen style with N64 button colours.
            const uint32_t slot=0x1B3A6AFF, kB=0x3CAF55FF, kC=0xF5CD28FF, kD=0x9A9AA6FF;
            p.Disc(262,162,24,slot);p.Ring(262,162,24,kB,2);
            p.ItemIcon(h.weapon,{247,147,30,30});
            p.Rect({240,186,44,3},p.RarityRgba(h.weaponRarity));
            p.Disc(244,142,7,kB);p.Text(241.5f,138.5f,"B",8,0x101D3DFF);
            const auto ammoKind=royale::AmmoUsedBy(h.weapon);
            p.Text(292,146,ShortName(h.weapon),11,wt::theme::Ink,false,92);
            p.Text(292,160,std::string(RarityName(h.weaponRarity))+(ammoKind==royale::AmmoKind::None?"  Melee":"  x"+std::to_string(h.ammo[static_cast<size_t>(ammoKind)])),9,wt::theme::Muted,false,92);
            for(int i=0;i<royale::kMaxReserveWeapons;++i) {
                const wt::Rect sq{388.0f+i*42,144,36,36};
                if(i<static_cast<int>(h.inv.reserve.size())) {
                    const auto& rw=h.inv.reserve[i];
                    p.Rect(sq,slot);p.Border(sq,0x527EBBFF);
                    p.ItemIcon(static_cast<royale::ItemId>(rw.item),{sq.x+5,sq.y+5,26,26});
                    p.Rect({sq.x,sq.y+37,36,3},p.RarityRgba(static_cast<royale::Rarity>(rw.rarity)));
                } else p.Border(sq,0x3A5A8AFF);
            }
            p.Disc(394,189,5,kD);p.Text(401,184,"D-pad L/R",8,wt::theme::Muted);
            p.Disc(494,162,18,slot);p.Ring(494,162,18,kC,2);
            if(h.hasShield)p.ItemIcon(h.shield,{481,149,26,26});else p.Disc(494,162,12,0x1B3A6A66);
            p.Disc(482,188,4,kC);p.Text(488,184,"Left",7,kC);
            p.Disc(538,162,18,slot);p.Ring(538,162,18,kD,2);
            if(h.inv.hasAbility)p.ItemIcon(static_cast<royale::ItemId>(h.inv.ability.item),{525,149,26,26});
            if(h.abilityReadyIn>0.05f)p.Text(530,184,ClockText(h.abilityReadyIn),8,wt::theme::Ink);else {p.Disc(529,188,4,kD);p.Text(535,184,"Up",7,kD);}
            const auto stacks=PotionStacks(h);
            for(int i=0;i<royale::kPotionSlots;++i) {
                const wt::Rect sq{563.0f+(i%3)*16,146.0f+(i/3)*16,14,14};
                if(i<static_cast<int>(stacks.size())) {
                    p.Rect(sq,slot);p.Border(sq,p.RarityRgba(stacks[i].rarity));
                    p.ItemIcon(stacks[i].item,{sq.x+2,sq.y+2,11,11});
                    if(stacks[i].count>1){const std::string n="x"+std::to_string(stacks[i].count);p.Text(sq.x+sq.w-p.TextWidth(n,7)-1,sq.y+sq.h-8,n,7,wt::theme::Ink);}
                } else p.Border(sq,0x3A5A8AFF);
            }
            p.Disc(571,188,4,kC);p.Text(577,184,"Down",7,kC);
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
            const bool classic=CVarGetInteger(ROYALE_CVAR("ControlScheme"),0)==1;
            const char* lines[]={"A   Open, take, hire, talk. With nothing else to do it JUMPS","B   Attack with the weapon in hand","D-pad L / R   Next / last weapon     D-pad Down   Heal","C-Left   Shield potion     C-Up   Ability     C-Right   Emote","Stick click   Sprint     Z   Lock on / dive     R   Shield / exit cart"};
            const char* classicLines[]={"A   Open, take, hire, talk","B   Attack with the weapon in hand","C-Up   JUMP     D-pad Up   Ability","D-pad L / R   Next / last weapon     D-pad Down   Heal","C-Left   Shield potion     C-Right   Emote     Stick click   Sprint"};
            p.Text(244,143,classic?"CLASSIC LAYOUT":"BATTLE ROYALE LAYOUT",10,wt::theme::Gold);
            for(int i=0;i<5;++i)p.Text(244,163+i*17,classic?classicLines[i]:lines[i],10,wt::theme::Ink,false,346);
            p.Text(244,252,"Change it in Settings > Comfort > Options. Menus: L / R pages, A select, B back.",9,wt::theme::Muted,false,346);
            p.Wrap(244,268,"Practice Tools lets you try equipment, bots, bosses, vehicles and gliding safely.",343,10,0x9EDCFFFF,2);
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
    if(gWar.page==WarPage::Settings&&gWar.section!=6&&gWar.section!=4&&gWar.section!=5) {
        p.Text(361,289,"OPTIONS "+std::to_string(gWar.optionPage+1)+" / "+std::to_string(gWar.optionPages),9,wt::theme::Muted);
    }
    if(hero&&InGame()) {
        const wt::Rect r=gWar.page==WarPage::Play?wt::Rect{38,133,108,146}:wt::Rect{44,146,145,128};
        gDPPipeSync(p.out++);gSPTexture(p.out++,0xffff,0xffff,0,G_TX_RENDERTILE,G_ON);
        gDPSetCombineMode(p.out++,G_CC_MODULATEIA_PRIM,G_CC_MODULATEIA_PRIM);p.Color(0xffffffff);
        gDPSetTileCustom(p.out++,G_IM_FMT_RGBA,G_IM_SIZ_16b,PAUSE_EQUIP_PLAYER_WIDTH,PAUSE_EQUIP_PLAYER_HEIGHT,0,G_TX_CLAMP,G_TX_CLAMP,G_TX_NOMASK,G_TX_NOMASK,G_TX_NOLOD,G_TX_NOLOD);
        // Fast3D normalizes UVs using loaded_texture, not just the render tile.
        // Load full portrait dimensions before binding the framebuffer texture.
        static std::array<uint16_t,PAUSE_EQUIP_PLAYER_WIDTH*PAUSE_EQUIP_PLAYER_HEIGHT> portraitTexture{};
        gDPSetTile(p.out++,G_IM_FMT_RGBA,G_IM_SIZ_16b,0,0,G_TX_LOADTILE,0,
                   G_TX_CLAMP,G_TX_NOMASK,G_TX_NOLOD,G_TX_CLAMP,G_TX_NOMASK,G_TX_NOLOD);
        gDPSetTextureImage(p.out++,G_IM_FMT_RGBA,G_IM_SIZ_16b,PAUSE_EQUIP_PLAYER_WIDTH,portraitTexture.data());
        gDPLoadSync(p.out++);
        gDPLoadTile(p.out++,G_TX_LOADTILE,0,0,(PAUSE_EQUIP_PLAYER_WIDTH-1)<<2,(PAUSE_EQUIP_PLAYER_HEIGHT-1)<<2);
        gDPSetTextureImageFB(p.out++,G_IM_FMT_RGBA,G_IM_SIZ_16b,PAUSE_EQUIP_PLAYER_WIDTH,gPauseLinkFrameBuffer);
        p.Quad(r,PAUSE_EQUIP_PLAYER_WIDTH,PAUSE_EQUIP_PLAYER_HEIGHT);
    }
    // Tab underline slides between pages.
    if(gWar.page!=WarPage::Keyboard&&gWar.page!=WarPage::Death) {
        p.Rect({gWar.tabX+6,95,132,2},wt::theme::Gold);
        p.Rect({gWar.tabX+6,95,132,2},WarFade(0xFFFFFFFF,gWar.reduced?0:0.18f+0.18f*std::sin(gWar.clock*4)));
    }
    int staggered=0;
    for(size_t i=0;i<gWar.controls.size();++i) {
        const auto& w=gWar.controls[i].view;const bool selected=static_cast<int>(i)==gWar.focus;
        // Buttons slide in one after another when a page opens.
        const bool chrome=w.id<5||w.touchOnly;
        float arrive=1;
        if(!chrome&&!gWar.reduced)arrive=WarEase((gWar.clock-gWar.pageChangedAt-0.04f*staggered++)/0.26f);
        wt::Rect r=w.rect;r.x+=(1-arrive)*22;
        const uint32_t bg=WarFade(!w.enabled?0x162644FF:selected?0x83C6EFFF:wt::theme::Button,arrive);
        p.Rect(r,bg);p.Border(r,WarFade(selected?0xD5F1FFFF:0x527EBBFF,arrive));
        if(selected&&!gWar.reduced) {
            // A glint sweeps across the selected button every couple of seconds.
            const float sweep=std::fmod(gWar.clock*0.55f,1.6f);
            if(sweep<1) {const float gx=r.x+(r.w+16)*sweep-14;const float x0=std::max(gx,r.x),x1=std::min(gx+10,r.x+r.w);
                if(x1>x0)p.Rect({x0,r.y+1,x1-x0,r.h-2},0xFFFFFF55);}
        }
        const float size=w.id>=1000?13:w.touchOnly?10:w.rect.w>300?12:w.rect.w>150?13:11;
        const uint32_t ink=WarFade(!w.enabled?0x8497B2FF:selected?0x101D3DFF:wt::theme::Ink,arrive);
        const float x=w.rect.w>300?r.x+10:r.x+std::max(5.0f,(r.w-p.TextWidth(w.label,size,true))/2);
        p.Text(x,r.y+(r.h-size)/2-1,w.label,size,ink,true,r.w-15);
    }
    // Sparkles burst from a button the moment it is chosen.
    if(!gWar.controls.empty()&&!gWar.reduced) {
        const float age=gWar.clock-gWar.focusChangedAt;
        if(age<0.45f) {
            const auto& f=gWar.focusRect;
            for(int i=0;i<7;++i) {
                const float k=age/0.45f, ang=i*0.9f+1;
                const float cx=f.x+f.w*((i%4)/3.0f), cy=i&1?f.y:f.y+f.h;
                WarSparkle(p,cx+std::cos(ang)*k*14,cy+(i&1?-1:1)*k*10,3.2f*(1-k)+0.8f,WarFade(i%2?0xF7D774FF:0xD8F3FFFF,1-k));
            }
        }
    }
    if(!gWar.controls.empty())p.Focus(gWar.focusRect);
    p.offset=0;WarRipples(p);
    p.offset=0;
    std::string hint=gWar.notice;
    if(hint.empty()&&!gWar.controls.empty())hint=gWar.controls[std::clamp(gWar.focus,0,static_cast<int>(gWar.controls.size())-1)].view.hint;
    p.Wrap(29,316,hint,582,10,0xC6E2F4FF,1);
    p.Text(30,339,gWar.page==WarPage::Death?"A  Select     B  Spectate":gWar.page==WarPage::Pause?"A  Select     B  Resume     L / R  Pages":"A  Select     B  Back     L / R  Pages",10,wt::theme::Ink);
    p.End(state);
}

