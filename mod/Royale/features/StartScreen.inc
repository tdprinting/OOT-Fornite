// ---- the start screen -------------------------------------------------------------------------------------------------------
// The first thing the game shows after the Nintendo 64 logo: the island at dusk with the logo landing in a burst and burning with the title
// flames (the whole picture is shared/title_scene.h; tools/title_scene/preview.cpp renders it without the game). It holds the boot
// (Royale_NativeMenuBoot in EngineBridge.inc) until Start, A, Enter, Space or a tap; a press before the logo lands skips to it. After the
// press the screen flashes and fades to black, then the save loads and the War Table opens on its main page. Once per launch.
struct StartScreen {
    bool done = false, pressed = false, tapped = false, keyDown = false;
    uint32_t shownAt = 0, pressedAt = 0;
    royale::title::Scene scene;
};
StartScreen gStart;

bool StartScreenHolding() { return !gStart.done; }

// The picture's coloured triangles go straight into ImGui's vertex buffer (one draw call), the logo is the shared logo texture.
struct ImTitleCanvas final : royale::title::Canvas {
    ImDrawList* dl;
    ImFont* font;
    ImTitleCanvas(ImDrawList* d, ImFont* f) : dl(d), font(f) {}
    static ImU32 Col(uint32_t c) { return IM_COL32(c >> 24, (c >> 16) & 255, (c >> 8) & 255, c & 255); }
    void Tri(float x0, float y0, uint32_t c0, float x1, float y1, uint32_t c1, float x2, float y2, uint32_t c2) override {
        if (((c0 | c1 | c2) & 255) == 0) return;
        const ImVec2 uv = dl->_Data->TexUvWhitePixel;
        dl->PrimReserve(3, 3);
        const ImDrawIdx base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
        dl->PrimWriteIdx(base);
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1));
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 2));
        dl->PrimWriteVtx(ImVec2(x0, y0), uv, Col(c0));
        dl->PrimWriteVtx(ImVec2(x1, y1), uv, Col(c1));
        dl->PrimWriteVtx(ImVec2(x2, y2), uv, Col(c2));
    }
    void Logo(float x0, float y0, float x1, float y1, float alpha) override {
        ImTextureID tex = LogoTexture();
        if (tex == nullptr || alpha <= 0.0f) return;
        dl->AddImage(tex, ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, static_cast<int>(255 * std::min(alpha, 1.0f))));
    }
    void Text(float cx, float y, float size, uint32_t rgba, const char* text) override {
        if ((rgba & 255) == 0) return;
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
        const ImVec2 pos(cx - sz.x * 0.5f, y);
        const float o = std::max(1.5f, size * 0.06f);
        const ImU32 outline = IM_COL32(30, 14, 4, (rgba & 255) * 220 / 255);
        for (int dx = -1; dx <= 1; dx++)
            for (int dy = -1; dy <= 1; dy++)
                if (dx || dy) dl->AddText(font, size, ImVec2(pos.x + dx * o, pos.y + dy * o), outline, text);
        dl->AddText(font, size, pos, Col(rgba), text);
    }
};

// Called every frame of the boot state, before anything loads. True while the start screen holds the boot.
bool StartScreenTick(GameState* state) {
    if (gStart.done) return false;
    const uint32_t now = std::max<uint32_t>(SDL_GetTicks(), 1);
    if (gStart.shownAt == 0) {
        gStart.shownAt = now;
        func_800F5E18(SEQ_PLAYER_BGM_MAIN, NA_BGM_TITLE, 0, 7, 1);   // the Ocarina of Time title theme
    }
    if (gStart.pressed) {
        if (now - gStart.pressedAt >= static_cast<uint32_t>(royale::title::kOutroSeconds * 1000)) {
            gStart.done = true;
            return false;
        }
        return true;
    }
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    const bool key = keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_KP_ENTER];
    const bool keyPress = key && !gStart.keyDown;
    gStart.keyDown = key;
    const bool press = (state->input[0].press.button & (BTN_START | BTN_A)) != 0 || keyPress || gStart.tapped;
    gStart.tapped = false;
    const uint32_t promptMs = static_cast<uint32_t>(royale::title::kPromptAt * 1000);
    if (press && now - gStart.shownAt > 250) {
        if (now - gStart.shownAt < promptMs) {
            gStart.shownAt = now - promptMs;   // skip the intro: the logo is there at once
        } else {
            gStart.pressed = true;
            gStart.pressedAt = now;
            Audio_PlaySoundGeneral(NA_SE_SY_PIECE_OF_HEART, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            Audio_QueueSeqCmd(NA_BGM_STOP | (SEQ_PLAYER_BGM_MAIN << 24) | (20 << 16));   // the theme fades out with the picture
        }
    }
    return true;
}

// Drawn from DrawOverlay (EndMatch.inc) over the boot frame. True while it covers the screen.
bool DrawStartScreen() {
    if (gStart.done || gStart.shownAt == 0 || InGame()) return false;
    if (ImGui::IsMouseClicked(0)) gStart.tapped = true;   // touch arrives as a mouse click
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const uint32_t now = std::max<uint32_t>(SDL_GetTicks(), 1);
    const float t = (now - gStart.shownAt) / 1000.0f;
    const float outro = gStart.pressed ? (now - gStart.pressedAt) / 1000.0f : -1.0f;
    ImVec2 sz(2, 1);
    LogoTexture(&sz);
    gStart.scene.SetHero(&sk::link_hd::kModel);   // the player body that ships with the mod (Skins.inc); the ROM's own Link is not loaded yet
    ImTitleCanvas canvas(dl, ImGui::GetFont());
    const bool reduced = CVarGetInteger(ROYALE_CVAR("ReducedMotion"), 0) != 0;
    gStart.scene.Draw(canvas, ds.x, ds.y, t, outro, reduced, sz.y > 0 ? sz.x / sz.y : 2.0f, std::string("VERSION ") + ROYALE_BUILD_VERSION);
    return true;
}
