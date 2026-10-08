// Original OoT textures, with server-owned values. No adventure HUD duplication.
void DrawOotVitals(ImDrawList* dl, float scale, const royale::HudState& h) {
    const float x = 22 * scale, y = 20 * scale, heart = 20 * scale, step = 22 * scale;
    const int count = std::clamp(static_cast<int>(std::ceil(h.maxHealth)), 1, 32);
    const char* names[] = {"gHeartEmptyTex", "gHeartQuarterTex", "gHeartHalfTex", "gHeartThreeQuarterTex", "gHeartFullTex"};
    for (int i = 0; i < count; ++i) {
        const float amount = std::clamp(h.selfHealth - i, 0.0f, 1.0f);
        const int part = amount <= 0 ? 0 : std::clamp(static_cast<int>(std::ceil(amount * 4)), 1, 4);
        const ImVec2 a(x + (i % 10) * step, y + (i / 10) * step), b(a.x + heart, a.y + heart);
        if (void* tex = HudAssetTexture(names[part], "parameter_static"))
            dl->AddImage(tex, a, b, {0,0}, {1,1}, part ? IM_COL32(255,65,75,255) : IM_COL32(120,40,50,255));
        else {
            // Faithful silhouette fallback when the user's resource pack lacks a heart.
            const ImVec2 c(a.x + heart/2, a.y + heart/3);
            const ImU32 color = part ? IM_COL32(230,40,55,255) : IM_COL32(70,20,30,255);
            dl->AddCircleFilled({c.x-heart/5,c.y},heart/4,color,12);
            dl->AddCircleFilled({c.x+heart/5,c.y},heart/4,color,12);
            dl->AddTriangleFilled({a.x+heart/20,c.y},{b.x-heart/20,c.y},{c.x,b.y},color);
        }
    }
    const float meterY = y + ((count+9)/10) * step + 5 * scale;
    auto meter = [&](float top, float value, float maximum, ImU32 tint) {
        const float width = 150 * scale, height = 16 * scale, cap = 8 * scale;
        const ImVec2 a(x, top), b(x+width, top+height);
        dl->AddRectFilled({a.x+cap/2,a.y+3*scale},{b.x-cap/2,b.y-3*scale},IM_COL32(20,35,25,230));
        const float fill = std::clamp(value / std::max(0.01f,maximum),0.0f,1.0f);
        if (fill > 0) {
            const ImVec2 fa(a.x+cap/2,a.y+3*scale), fb(fa.x+(width-cap)*fill,b.y-3*scale);
            if (void* tex=HudAssetTexture("gMagicMeterFillTex","parameter_static")) dl->AddImage(tex,fa,fb,{0,0},{1,1},tint);
            else dl->AddRectFilled(fa,fb,tint);
        }
        void* end=HudAssetTexture("gMagicMeterEndTex","parameter_static");
        void* mid=HudAssetTexture("gMagicMeterMidTex","parameter_static");
        if (end && mid) {
            dl->AddImage(end,a,{a.x+cap,b.y});
            dl->AddImage(mid,{a.x+cap,a.y},{b.x-cap,b.y});
            dl->AddImage(end,{b.x-cap,a.y},b,{1,0},{0,1});
        } else dl->AddRect(a,b,IM_COL32(230,230,220,255),3*scale,0,2*scale);
    };
    // OoT had no shield-energy meter; reuse its magic frame in Hylian blue.
    meter(meterY,h.inv.shield,royale::kMaxShield,IM_COL32(85,165,245,255));
    meter(meterY+21*scale,h.magic,royale::kMaxMagic,IM_COL32(60,210,85,255));
}
