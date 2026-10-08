// Included by RoyaleBosses.h after BossActor and the shared helpers.
void ChuChu_Draw(Actor* actor, PlayState* play, BossActor& b) {
    namespace chu = royale::chu;
    chu::Clip clip = b.moved > 0.6f ? chu::Wobble : chu::Idle;
    const BM mode = ModeOf(b);
    bool loop = true;
    float age = BossTime(play);
    float duration = 1.0f;
    if (mode == BM::Hidden) { clip = chu::Puddle; loop = false; }
    else if (mode == BM::Emerge) { clip = chu::Emerge; loop = false; age = b.modeAge; duration = 0.6f; }
    else if (mode == BM::Leap || b.smashAge < 0.55f) { clip = chu::Lunge; loop = false; age = mode == BM::Leap ? b.modeAge : b.smashAge; duration = 0.55f; }
    else if (mode == BM::Summon) { clip = chu::Discharge; age = b.modeAge; }
    else if (mode == BM::Stunned) { clip = KindOf(b) == BK::ChuDark && b.aux == 4 ? chu::Petrify : chu::Hurt; age = b.modeAge; }
    else if (b.hurtAge < 0.45f) { clip = chu::Hurt; loop = false; age = b.hurtAge; duration = 0.45f; }
    const float phase = loop ? std::fmod(age / duration, 1.0f) : std::clamp(age / duration, 0.0f, 1.0f);
    const float jellyTime = BossTime(play);
    const float frame = phase * (chu::kFrames - 1);
    const int f0 = static_cast<int>(frame), f1 = std::min(f0 + 1, chu::kFrames - 1);
    const float blend = frame - f0;
    constexpr size_t count = sizeof(chu::kCorners) / sizeof(chu::kCorners[0]);
    Vtx* vertices = static_cast<Vtx*>(Graph_Alloc(play->state.gfxCtx, count * sizeof(Vtx)));
    if (!vertices) return;
    for (size_t i = 0; i < count; i++) {
        const auto& c = chu::kCorners[i];
        const auto& a = chu::kPoses[clip][f0][c.point]; const auto& z = chu::kPoses[clip][f1][c.point];
        Vtx& v = vertices[i];
        v.v.ob[0] = static_cast<s16>(a.x + (z.x-a.x)*blend);
        v.v.ob[1] = static_cast<s16>(a.y + (z.y-a.y)*blend);
        v.v.ob[2] = static_cast<s16>(a.z + (z.z-a.z)*blend);
        // Small surface ripples keep the painted eyes readable while the jelly flows.
        const float body = std::clamp((150.0f-a.y)/80.0f,0.0f,1.0f);
        const float ripple = std::sin(a.y*.055f + jellyTime*3.0f + a.x*.018f);
        v.v.flag=0;
        v.v.tc[0]=static_cast<s16>(c.s + body*48.0f*ripple);
        v.v.tc[1]=static_cast<s16>(c.t + body*32.0f*std::sin(a.x*.04f-jellyTime*2.2f));
        const float glint = std::pow(std::max(0.0f,std::sin(a.x*.025f+a.z*.018f+a.y*.012f-jellyTime*1.8f)),8.0f);
        const u8 shade = static_cast<u8>(std::clamp(195.0f+a.y*.18f+body*12.0f*ripple+32.0f*glint,175.0f,255.0f));
        v.v.cn[0]=v.v.cn[1]=v.v.cn[2]=shade; v.v.cn[3]=255;
    }
    const bool stone = clip == chu::Petrify;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(b.rot * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    const float scale=royale::BossOf(KindOf(b)).scale;
    Matrix_Scale(scale*(KindOf(b)==BK::ChuDark?1.18f:1.0f),scale*(KindOf(b)==BK::ChuDark?0.78f:1.0f),scale,MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gDPSetCycleType(POLY_OPA_DISP++,G_CYC_1CYCLE);
    gDPSetTextureLUT(POLY_OPA_DISP++,G_TT_NONE);
    gDPSetTexturePersp(POLY_OPA_DISP++,G_TP_PERSP);
    if (stone) {
        gDPSetPrimColor(POLY_OPA_DISP++,0,0,145,145,160,255);
        gDPSetCombineMode(POLY_OPA_DISP++,G_CC_PRIMITIVE,G_CC_PRIMITIVE);
    } else {
        gDPSetCombineMode(POLY_OPA_DISP++,G_CC_MODULATEIA,G_CC_MODULATEIA);
        gSPTexture(POLY_OPA_DISP++,0xFFFF,0xFFFF,0,G_TX_RENDERTILE,G_ON);
        const int variant=b.kind-static_cast<int>(BK::ChuRed);
        // The port's LoadTile path supports a full high resolution image; LoadBlock truncates large uploads.
        gDPSetTextureFilter(POLY_OPA_DISP++,G_TF_BILERP);
        gDPLoadTextureTile(POLY_OPA_DISP++,const_cast<uint8_t*>(chu::kTextures[variant]),G_IM_FMT_RGBA,G_IM_SIZ_16b,
                           chu::kTextureWidth,chu::kTextureHeight,0,0,chu::kTextureWidth-1,chu::kTextureHeight-1,
                           0,G_TX_WRAP,G_TX_CLAMP,G_TX_NOMASK,G_TX_NOMASK,G_TX_NOLOD,G_TX_NOLOD);
    }
    for (size_t first=0;first<count;first+=30) {
        const int n=static_cast<int>(std::min<size_t>(30,count-first));
        gSPVertex(POLY_OPA_DISP++,reinterpret_cast<uintptr_t>(vertices+first),n,0);
        for(int i=0;i<n;i+=3) gSP1Triangle(POLY_OPA_DISP++,i,i+1,i+2,0);
    }
    gSPTexture(POLY_OPA_DISP++,0,0,0,G_TX_RENDERTILE,G_OFF);
    CLOSE_DISPS(play->state.gfxCtx);
}
