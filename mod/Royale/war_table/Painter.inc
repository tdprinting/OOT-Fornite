// One of the game's own textures by OTR path (cached forever; nullptr if the archive has none).
const char* WarGameTexture(const char* name, const char* directory) {
    static std::unordered_map<std::string,std::string> cache;
    if(!name)return nullptr;
    const std::string key=std::string(directory)+"/"+name;
    auto it=cache.find(key);
    if(it==cache.end()) {
        std::string path="__OTR__textures/"+key;
        try { if(!Ship::Context::GetInstance()->GetResourceManager()->LoadResource(path,true))path.clear(); }
        catch(...) { path.clear(); }
        it=cache.emplace(key,path).first;
    }
    return it->second.empty()?nullptr:it->second.c_str();
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
        // guMtxIdent writes float bytes in this port; Fast3D expects a packed Mtx.
        MtxF identity;
        guMtxIdentF(identity.mf);
        Matrix_MtxFToMtx(&identity, &f.identity);
        f.viewport = {{{640,480,G_MAXZ/2,0},{640,480,G_MAXZ/2,0}}};
        gDPPipeSync(out++);
        gsSPResetFB(out++);
        gSPGrayscale(out++, false);
        gSPClearExtraGeometryMode(out++, G_EX_INVERT_CULLING);
        gSPSurfaceMap(out++,0); // World material effects must not leak into menu text or art.
        gSPViewport(out++,&f.viewport);
        gDPSetScissor(out++,G_SC_NON_INTERLACE,0,0,320,240);
        gSPMatrix(out++,&f.projection,G_MTX_PROJECTION|G_MTX_LOAD|G_MTX_NOPUSH);
        gSPMatrix(out++,&f.identity,G_MTX_MODELVIEW|G_MTX_LOAD|G_MTX_NOPUSH);
        gSPLoadGeometryMode(out++,G_SHADE|G_SHADING_SMOOTH);
        // Establish the complete 2D pipeline, including world alpha/depth state.
        gDPSetOtherMode(out++, G_AD_DISABLE | G_CD_DISABLE | G_CK_NONE | G_TC_FILT |
            G_TF_BILERP | G_TT_NONE | G_TL_TILE | G_TD_CLAMP | G_TP_NONE |
            G_CYC_1CYCLE | G_PM_NPRIMITIVE, G_AC_NONE | G_ZS_PIXEL |
            G_RM_XLU_SURF | G_RM_XLU_SURF2);
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
        Rect({r.x+2,r.y+3,r.w,r.h},0x060F24C0);Rect(r,wt::theme::Panel);
        Border(r,0x91B4D3CC);Border({r.x+3,r.y+3,r.w-6,r.h-6},0x426CA7AA);
        for(int corner=0;corner<4;++corner) {
            const float x=corner&1?r.x+r.w-7:r.x, y=corner&2?r.y+r.h-7:r.y;
            Rect({x,y,7,2},wt::theme::Gold);Rect({x,y,2,7},wt::theme::Gold);
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
    void Text(float x,float y,const std::string& s,float size=11,uint32_t color=wt::theme::Ink,bool heading=false,float maxWidth=1000) {
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
    void Wrap(float x,float y,const std::string& s,float width,float size=10,uint32_t color=wt::theme::Muted,int maxLines=3) {
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
    void Otr(const char* path,wt::Rect r,int w,int h,int fmt,int siz,uint32_t color=0xffffffff) {
        if(!Space())return;
        gDPPipeSync(out++);gSPTexture(out++,0xffff,0xffff,0,G_TX_RENDERTILE,G_ON);
        gDPSetCombineMode(out++,G_CC_MODULATEIA_PRIM,G_CC_MODULATEIA_PRIM);Color(color);
        // The load macro pastes the size token, so each size the HUD uses gets its own literal branch.
        if(siz==G_IM_SIZ_32b) {gDPLoadTextureBlock(out++,path,G_IM_FMT_RGBA,G_IM_SIZ_32b,w,h,0,G_TX_CLAMP,G_TX_CLAMP,G_TX_NOMASK,G_TX_NOMASK,G_TX_NOLOD,G_TX_NOLOD);}
        else if(siz==G_IM_SIZ_16b) {gDPLoadTextureBlock(out++,path,G_IM_FMT_RGBA,G_IM_SIZ_16b,w,h,0,G_TX_CLAMP,G_TX_CLAMP,G_TX_NOMASK,G_TX_NOMASK,G_TX_NOLOD,G_TX_NOLOD);}
        else {gDPLoadTextureBlock(out++,path,G_IM_FMT_IA,G_IM_SIZ_8b,w,h,0,G_TX_CLAMP,G_TX_CLAMP,G_TX_NOMASK,G_TX_NOMASK,G_TX_NOLOD,G_TX_NOLOD);}
        (void)fmt;
        Quad(r,w,h);
    }
    // OoT heart container: part 0 empty .. 4 full.
    void Heart(float x,float y,float size,int part) {
        static const char* names[]={"gHeartEmptyTex","gHeartQuarterTex","gHeartHalfTex","gHeartThreeQuarterTex","gHeartFullTex"};
        const uint32_t c=part>0?0xFF414BFF:0x782832FF;
        part=std::clamp(part,0,4);
        if(const char* t=WarGameTexture(names[part],"parameter_static")){Otr(t,{x,y,size,size},16,16,G_IM_FMT_IA,G_IM_SIZ_8b,c);return;}
        const float s=size/4;
        Rect({x+s*0.3f,y+s*0.4f,s*1.7f,s*1.6f},c);Rect({x+s*2,y+s*0.4f,s*1.7f,s*1.6f},c);Rect({x+s*0.8f,y+s*2,s*2.4f,s*1.6f},c);
    }
    // OoT magic-meter look.
    void Meter(wt::Rect r,float fill01,uint32_t tint) {
        Rect(r,0x14231AE6);Rect({r.x,r.y,r.w*std::clamp(fill01,0.0f,1.0f),r.h},tint);
        const char* cap=WarGameTexture("gMagicMeterEndTex","parameter_static");
        const char* mid=WarGameTexture("gMagicMeterMidTex","parameter_static");
        if(cap&&mid) {
            const float cw=r.h*0.5f;
            Otr(mid,r,24,16,G_IM_FMT_IA,G_IM_SIZ_8b);
            Otr(cap,{r.x-cw*0.5f,r.y,cw,r.h},8,16,G_IM_FMT_IA,G_IM_SIZ_8b);
            Otr(cap,{r.x+r.w-cw*0.5f,r.y,cw,r.h},8,16,G_IM_FMT_IA,G_IM_SIZ_8b);
        } else Border(r,0xE6E6DCFF);
    }
    // Round item buttons: five stacked Rects.
    void Disc(float cx,float cy,float r,uint32_t color) {
        static const float hw[]={0.6f,0.9f,1.0f,0.9f,0.6f};
        for(int i=0;i<5;++i)Rect({cx-hw[i]*r,cy-r+i*r*0.4f,hw[i]*r*2,r*0.4f},color);
    }
    void Ring(float cx,float cy,float r,uint32_t color,float width=2) {
        const float s=std::max(width,r*0.3f), d=r*0.68f;
        Rect({cx-r*0.45f,cy-r,r*0.9f,width},color);Rect({cx-r*0.45f,cy+r-width,r*0.9f,width},color);
        Rect({cx-r,cy-r*0.45f,width,r*0.9f},color);Rect({cx+r-width,cy-r*0.45f,width,r*0.9f},color);
        for(int i=0;i<4;++i)Rect({cx+(i&1?d:-d)-s/2,cy+(i&2?d:-d)-s/2,s,s},color);
    }
    static uint32_t RarityRgba(royale::Rarity q) {
        const auto& c=kRarityRgb[std::clamp(static_cast<int>(q),0,royale::kRarityCount-1)];
        return (static_cast<uint32_t>(c.r)<<24)|(static_cast<uint32_t>(c.g)<<16)|(static_cast<uint32_t>(c.b)<<8)|255u;
    }
    void ItemIcon(royale::ItemId id,wt::Rect r,uint32_t tint=0xffffffff) {
        const char* name=RealIconName(id);
        if(name) {
            const std::string n=name;
            const bool quest=n.rfind("gQuestIcon",0)==0;
            const char* dir=quest?"icon_item_24_static":(n.rfind("gHeart",0)==0||n.rfind("gRupee",0)==0)?"parameter_static":"icon_item_static";
            if(const char* t=WarGameTexture(name,dir)){const int s=quest?24:32;Otr(t,r,s,s,G_IM_FMT_RGBA,G_IM_SIZ_32b,tint);return;}
        }
        Rect(r,0x2D5F8AFF);
        const std::string s=ShortName(id).substr(0,1);
        Text(r.x+r.w/2-TextWidth(s,r.h*0.5f)/2,r.y+r.h*0.25f,s,r.h*0.5f,tint);
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
    if(map==royale::kRiftlandsMapIndex)colours=royale::riftlands::kColours;
    if(map==royale::kMoonfallMapIndex)colours=royale::moonfall::kColours;
    if(map==royale::kSkyloftMapIndex)colours=royale::skyloft::kColours;
    if(map==royale::kGreatHyruleKingdomMapIndex)colours=royale::great_hyrule_kingdom::kColours;
    if(map==royale::kHyruleThirdStormMapIndex)colours=royale::hyrule_third_storm::Ground().colours.data();
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
    if(!player->skelAnime.skeleton || !player->skelAnime.jointTable || player->actor.objBankIndex<0 || player->actor.objBankIndex>=OBJECT_EXCHANGE_BANK_MAX)return;
    void* keep=gPlayState->objectCtx.status[gPlayState->objectCtx.mainKeepIndex].segment;
    void* link=gPlayState->objectCtx.status[player->actor.objBankIndex].segment;
    if(!keep||!link)return;
    // Segment 6 belongs to the last world actor drawn, which need not be Link.
    const auto saved4=gSegments[4], saved6=gSegments[6];
    gSegments[4]=VIRTUAL_TO_PHYSICAL(keep);gSegments[6]=VIRTUAL_TO_PHYSICAL(link);
    Matrix_Push();
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
    Player_DrawPauseImpl(gPlayState,keep,link,
                        &player->skelAnime,&pos,&rot,LINK_IS_ADULT?0.047f:0.046f,
                        LINK_IS_ADULT?PLAYER_SWORD_MASTER:PLAYER_SWORD_KOKIRI,PLAYER_TUNIC_KOKIRI,PLAYER_SHIELD_HYLIAN,PLAYER_BOOTS_KOKIRI,
                        PAUSE_EQUIP_PLAYER_WIDTH,PAUSE_EQUIP_PLAYER_HEIGHT,&eye,&at,60,color.data(),depth.data());
    OPEN_DISPS(gPlayState->state.gfxCtx);
    gsSPResetFB(WORK_DISP++);
    CLOSE_DISPS(gPlayState->state.gfxCtx);
    Matrix_Pop();
    gSegments[4]=saved4;gSegments[6]=saved6;
    CVarSetInteger(CVAR_COSMETIC("Link.KokiriTunic.Changed"),changed);
    CVarSetColor24(CVAR_COSMETIC("Link.KokiriTunic.Value"),saved);
}


// Render the existing skinned pet rigs in a dedicated framebuffer. No actor,
// companion brain, speech or world geometry participates in this preview.
constexpr int kWarPetSize=192;
int gWarPetFramebuffer=-1;
bool WarPetPreview() {
    if(!InGame())return false;
    if(gWarPetFramebuffer<0)gWarPetFramebuffer=gfx_create_framebuffer(kWarPetSize,kWarPetSize,kWarPetSize,kWarPetSize,true);
    if(gWarPetFramebuffer<0)return false;
    DrawIdentity previewIdentity(&gWarPetFramebuffer,PetKind(),0);
    const float time=gWar.reduced||!gWar.previewAnimate?0:gWar.clock;
    const float yaw=.35f+(gWar.reduced||!gWar.previewAnimate?0:std::sin(time*.45f)*.22f);
    float minX=1e9f,maxX=-1e9f,minY=1e9f,maxY=-1e9f,depth=1;
    auto bounds=[&](const auto& pose,const auto* vertices,int count,auto skin,auto visible) {
        for(int i=0;i<count;++i) {
            if(!visible(vertices[i]))continue;
            float pos[3],normal[3];skin(pose,vertices[i],pos,normal);
            const float x=pos[0]*std::cos(yaw)+pos[2]*std::sin(yaw);
            minX=std::min(minX,x);maxX=std::max(maxX,x);minY=std::min(minY,pos[1]);maxY=std::max(maxY,pos[1]);
            depth=std::max(depth,std::fabs(pos[0])+std::fabs(pos[2]));
        }
    };
    royale::lilo::Pose lilo;royale::avriella::Pose baby;royale::maya::Pose maya;
    const int pet=PetKind();
    if(pet==0) {royale::lilo::SampleClip(royale::lilo::kIdle,time,lilo);bounds(lilo,royale::lilo::kVerts,royale::lilo::kVertCount,royale::lilo::SkinVertex,[](const auto&){return true;});}
    else if(pet==1) {royale::avriella::SampleClip(royale::avriella::kSit,time,baby);bounds(baby,royale::avriella::kVerts,royale::avriella::kVertCount,royale::avriella::SkinVertex,[](const auto&){return true;});}
    else {royale::maya::SampleClip(royale::maya::kIdle,time,maya);bounds(maya,royale::maya::kVerts,royale::maya::kVertCount,royale::maya::SkinVertex,[&maya](const auto& v){return royale::maya::PreviewVertexVisible(maya,v);});}
    struct Camera {Mtx projection,identity;Vp viewport;};static std::array<Camera,2> camera;
    auto& frame=camera[gPlayState->state.frames%2];
    const float half=std::max(1.0f,std::max(maxX-minX,maxY-minY)*.64f),cx=(minX+maxX)*.5f,cy=(minY+maxY)*.5f;
    guOrtho(&frame.projection,cx-half,cx+half,cy-half,cy+half,-depth*4,depth*4,1);
    MtxF identity;SkinMatrix_Clear(&identity);Matrix_MtxFToMtx(&identity,&frame.identity);
    frame.viewport={{{kWarPetSize*2,kWarPetSize*2,G_MAXZ/2,0},{kWarPetSize*2,kWarPetSize*2,G_MAXZ/2,0}}};
    static std::array<uint16_t,kWarPetSize*kWarPetSize> color{},zbuffer{};
    auto* play=gPlayState;Matrix_Push();
    OPEN_DISPS(play->state.gfxCtx);
    Gfx* opa=POLY_OPA_DISP++;Gfx* xlu=POLY_XLU_DISP++;
    gsSPSetFB(WORK_DISP++,gWarPetFramebuffer);
    gSPDisplayList(WORK_DISP++,POLY_OPA_DISP);gSPDisplayList(WORK_DISP++,POLY_XLU_DISP);
    gDPPipeSync(POLY_OPA_DISP++);gSPSurfaceMap(POLY_OPA_DISP++,0);
    gDPSetScissor(POLY_OPA_DISP++,G_SC_NON_INTERLACE,0,0,kWarPetSize,kWarPetSize);
    gSPGrayscale(POLY_OPA_DISP++,false);gSPClearExtraGeometryMode(POLY_OPA_DISP++,G_EX_INVERT_CULLING);
    gDPSetDepthImage(POLY_OPA_DISP++,zbuffer.data());
    gDPSetColorImage(POLY_OPA_DISP++,G_IM_FMT_RGBA,G_IM_SIZ_16b,kWarPetSize,zbuffer.data());
    gDPSetCycleType(POLY_OPA_DISP++,G_CYC_FILL);gDPSetRenderMode(POLY_OPA_DISP++,G_RM_NOOP,G_RM_NOOP2);
    gDPSetFillColor(POLY_OPA_DISP++,(GPACK_ZDZ(G_MAXFBZ,0)<<16)|GPACK_ZDZ(G_MAXFBZ,0));
    gDPFillRectangle(POLY_OPA_DISP++,0,0,kWarPetSize-1,kWarPetSize-1);gDPPipeSync(POLY_OPA_DISP++);
    gDPSetColorImage(POLY_OPA_DISP++,G_IM_FMT_RGBA,G_IM_SIZ_16b,kWarPetSize,color.data());
    gDPSetCycleType(POLY_OPA_DISP++,G_CYC_FILL);gDPSetRenderMode(POLY_OPA_DISP++,G_RM_NOOP,G_RM_NOOP2);
    gDPSetFillColor(POLY_OPA_DISP++,(GPACK_RGBA5551(22,56,113,1)<<16)|GPACK_RGBA5551(22,56,113,1));
    gDPFillRectangle(POLY_OPA_DISP++,0,0,kWarPetSize-1,kWarPetSize-1);
    gDPPipeSync(POLY_OPA_DISP++);
    // Mesh setup uses the world fog blend mode; neutralize inherited scene fog.
    gSPFogFactor(POLY_OPA_DISP++,0,0);gSPFogFactor(POLY_XLU_DISP++,0,0);
    gSPViewport(POLY_OPA_DISP++,&frame.viewport);gSPViewport(POLY_XLU_DISP++,&frame.viewport);
    gSPMatrix(POLY_OPA_DISP++,&frame.projection,G_MTX_PROJECTION|G_MTX_LOAD|G_MTX_NOPUSH);
    gSPMatrix(POLY_OPA_DISP++,&frame.identity,G_MTX_MODELVIEW|G_MTX_LOAD|G_MTX_NOPUSH);
    gSPMatrix(POLY_XLU_DISP++,&frame.projection,G_MTX_PROJECTION|G_MTX_LOAD|G_MTX_NOPUSH);
    gSPMatrix(POLY_XLU_DISP++,&frame.identity,G_MTX_MODELVIEW|G_MTX_LOAD|G_MTX_NOPUSH);
    if(pet==0)DrawLiloModel(play,0,0,0,yaw,1,lilo,0);
    else if(pet==1)DrawAvriellaModel(play,0,0,0,yaw,1,baby,0);
    else DrawMayaCompanionModel(play,0,0,0,yaw,1,maya,0);
    gSPEndDisplayList(POLY_OPA_DISP++);gSPEndDisplayList(POLY_XLU_DISP++);
    gSPBranchList(opa,POLY_OPA_DISP);gSPBranchList(xlu,POLY_XLU_DISP);
    gsSPResetFB(WORK_DISP++);
    CLOSE_DISPS(play->state.gfxCtx);
    Matrix_Pop();return true;
}
void WarPetImage(WarPainter& p,wt::Rect rect) {
    static std::array<uint16_t,kWarPetSize*kWarPetSize> pixels{};
    gDPPipeSync(p.out++);gSPTexture(p.out++,0xffff,0xffff,0,G_TX_RENDERTILE,G_ON);
    gDPSetCombineMode(p.out++,G_CC_MODULATEIA_PRIM,G_CC_MODULATEIA_PRIM);p.Color(0xffffffff);
    gDPSetTileCustom(p.out++,G_IM_FMT_RGBA,G_IM_SIZ_16b,kWarPetSize,kWarPetSize,0,G_TX_CLAMP,G_TX_CLAMP,G_TX_NOMASK,G_TX_NOMASK,G_TX_NOLOD,G_TX_NOLOD);
    gDPSetTile(p.out++,G_IM_FMT_RGBA,G_IM_SIZ_16b,0,0,G_TX_LOADTILE,0,G_TX_CLAMP,G_TX_NOMASK,G_TX_NOLOD,G_TX_CLAMP,G_TX_NOMASK,G_TX_NOLOD);
    gDPSetTextureImage(p.out++,G_IM_FMT_RGBA,G_IM_SIZ_16b,kWarPetSize,pixels.data());
    gDPLoadSync(p.out++);gDPLoadTile(p.out++,G_TX_LOADTILE,0,0,(kWarPetSize-1)<<2,(kWarPetSize-1)<<2);
    gDPSetTextureImageFB(p.out++,G_IM_FMT_RGBA,G_IM_SIZ_16b,kWarPetSize,gWarPetFramebuffer);
    p.Quad(rect,kWarPetSize,kWarPetSize);
}
