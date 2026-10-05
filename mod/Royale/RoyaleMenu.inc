// OOT Royale: the game's menus, rebuilt around Battle Royale. Included at the end of RoyaleMod.cpp, inside its anonymous namespace, so it can use
// everything defined there.
//
// What the player sees:
//   - Boot: the title screen (with the OOT Royale logo). Start opens the Battle Royale main menu over it instead of the file select. There are no
//     save files to pick and no quest types: the mod keeps one Battle Royale save of its own and loads it behind the scenes (patches/0012).
//   - In the game: Start opens the same menu over the game (it does not pause; the match is online). It shows what fits the moment: the main
//     menu, the lobby, the match, the results.
//   - Everything is driven by the controller (D-pad or stick, A, B, Start) through the game's own input, and works by touch or mouse too.
//   - The look follows Ocarina of Time: the text-box black panels, gold lettering, the pause menu's pulsing corner cursor, the A "Decide" and
//     B "Return" buttons, item icons, and the game's own place-name cards for the maps. The game's art is read from the player's own ROM
//     at run time, like the rest of the mod.
//   - Each map has a preview picture taken in the game itself (a capture of the 3D view, without the HUD): the first time you are on a map, from the
//     sky while you skydive in, or all at once with "Take map pictures" in Options. They are saved as PNG files next to the game.
// The Ship of Harkinian menu keeps only what this menu doesn't do (graphics, controller mapping) and the developer tools.

// ---- small helpers ----------------------------------------------------------------------------------------------------------------

constexpr royale::ItemId kNoIcon = royale::ItemId::Count;   // a row without an icon

void MenuSfx(u16 sfx) {
    Audio_PlaySoundGeneral(sfx, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

// One of the game's own textures (from the player's ROM), loaded once for drawing with ImGui. nullptr if it can't be loaded.
ImTextureID OotTexture(const char* path, ImVec2* size = nullptr) {
    struct Entry { bool ok; ImVec2 size; };
    static std::unordered_map<std::string, Entry> cache;
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    const std::string key = std::string("royale-menu:") + path;
    auto it = cache.find(path);
    if (it == cache.end()) {
        Entry e{ false, ImVec2(0, 0) };
        try {
            if (Ship::Context::GetInstance()->GetResourceManager()->LoadResource(path, true) != nullptr) {
                gui->LoadGuiTexture(key, path, ImVec4(1, 1, 1, 1));
                e.ok = gui->HasTextureByName(key);
                if (e.ok) e.size = gui->GetTextureSize(key);
            }
        } catch (...) {}
        it = cache.emplace(path, e).first;
    }
    if (!it->second.ok) return nullptr;
    if (size) *size = it->second.size;
    return gui->GetTextureByName(key);
}

// The place-name card the game shows when you enter each map (white lettering on clear; drawn tinted).
const char* MapTitleCard(int mapId) {
    switch (royale::ClampMap(mapId)) {
        case 1: return dgLakeHyliaTitleCardENGTex;
        case 2: return dgKakarikoVillageTitleCardENGTex;
        case 3: return dgDeathMountainCraterTitleCardENGTex;
        case 4: return dgDesertColossusTitleCardENGTex;
        default: return dgHyruleFieldTitleCardENGTex;
    }
}

// Each map's colours from the art style guide (region palettes), for its card when there is no picture yet.
struct MapColours { ImU32 top, bottom; };
MapColours MapPalette(int mapId) {
    switch (royale::ClampMap(mapId)) {
        case 1: return { IM_COL32(46, 139, 122, 255), IM_COL32(65, 90, 105, 255) };    // Lake Hylia: teal and slate
        case 2: return { IM_COL32(54, 43, 51, 255), IM_COL32(23, 49, 38, 255) };      // Kakariko: dusk purple over deep green
        case 3: return { IM_COL32(172, 32, 17, 255), IM_COL32(31, 23, 23, 255) };     // the crater: blood red over near-black
        case 4: return { IM_COL32(247, 214, 34, 255), IM_COL32(140, 92, 23, 255) };   // the desert: sand gold over sienna
        default: return { IM_COL32(196, 222, 148, 255), IM_COL32(62, 142, 58, 255) }; // Hyrule Field: sunlit grass over Kokiri green
    }
}

// The game's filename letters (the PAL table, as Boss Rush uses): 0-9, A-Z, a-z, then space.
u8 FilenameChar(char c) {
    if (c >= '0' && c <= '9') return static_cast<u8>(c - '0');
    if (c >= 'A' && c <= 'Z') return static_cast<u8>(10 + c - 'A');
    if (c >= 'a' && c <= 'z') return static_cast<u8>(36 + c - 'a');
    return 0x3E;
}

// ---- map preview pictures --------------------------------------------------------------------------------------------------------
// A picture is the game's 3D view (the HUD is drawn after it, so it isn't in the picture), scaled to 320 x 240 by the same code the Pictograph
// Box uses, read back to memory, and saved as royale-previews/mapN.png next to the game. The picture is taken once the scene has settled.

constexpr int kPreviewW = 320, kPreviewH = 240;

struct Preview {
    bool tried = false;     // looked for the file on disk
    bool have = false;
    bool aerial = false;    // taken from the sky during the skydive (these win over pictures taken on the ground)
    ImTextureID tex = nullptr;
    int texId = -1;
};
Preview gPreviews[royale::kMapCount];

std::filesystem::path PreviewDir() { return Ship::Context::GetPathRelativeToAppDirectory("royale-previews"); }
std::filesystem::path PreviewPath(int mapId) { return PreviewDir() / ("map" + std::to_string(mapId) + ".png"); }

ImTextureID UploadRgba(const uint8_t* rgba, int w, int h, int* idInOut) {
    GfxRenderingAPI* api = gfx_get_current_rendering_api();
    if (api == nullptr) return nullptr;
    if (*idInOut < 0) *idInOut = static_cast<int>(api->new_texture());
    api->select_texture(0, *idInOut);
    api->set_sampler_parameters(0, true, 0, 0);
    api->upload_texture(rgba, w, h);
    return reinterpret_cast<ImTextureID>(static_cast<intptr_t>(*idInOut));
}

// A plain PNG writer: 8-bit RGBA, no filtering, the image data in uncompressed ("stored") deflate blocks. Any viewer opens it.
uint32_t Crc32(const uint8_t* data, size_t n, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool built = false;
    if (!built) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        built = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

bool WritePng(const std::filesystem::path& path, const uint8_t* rgba, int w, int h) {
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(h) * (w * 4 + 1));
    for (int y = 0; y < h; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba + static_cast<size_t>(y) * w * 4, rgba + static_cast<size_t>(y + 1) * w * 4);
    }
    std::vector<uint8_t> z = { 0x78, 0x01 };
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
    for (size_t off = 0; off < raw.size() || off == 0; off += 65535) {
        const size_t len = std::min<size_t>(65535, raw.size() - off);
        z.push_back(off + len >= raw.size() ? 1 : 0);
        z.push_back(len & 0xFF); z.push_back((len >> 8) & 0xFF);
        z.push_back(~len & 0xFF); z.push_back((~len >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + off, raw.begin() + off + len);
        if (raw.empty()) break;
    }
    const uint32_t adler = (b << 16) | a;
    for (int s = 24; s >= 0; s -= 8) z.push_back((adler >> s) & 0xFF);

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    auto u32 = [&](uint32_t v) { const char c[4] = { char(v >> 24), char(v >> 16), char(v >> 8), char(v) }; out.write(c, 4); };
    auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        u32(static_cast<uint32_t>(data.size()));
        std::vector<uint8_t> td(type, type + 4);
        td.insert(td.end(), data.begin(), data.end());
        out.write(reinterpret_cast<const char*>(td.data()), td.size());
        u32(Crc32(td.data(), td.size()));
    };
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    out.write(reinterpret_cast<const char*>(sig), 8);
    std::vector<uint8_t> ihdr = { uint8_t(w >> 24), uint8_t(w >> 16), uint8_t(w >> 8), uint8_t(w), uint8_t(h >> 24), uint8_t(h >> 16), uint8_t(h >> 8), uint8_t(h), 8, 6, 0, 0, 0 };
    chunk("IHDR", ihdr);
    chunk("IDAT", z);
    chunk("IEND", {});
    return static_cast<bool>(out);
}

std::string ReadFileBytes(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The picture for a map, loaded from disk the first time it's wanted (any PNG works, so a picture can also be swapped by hand).
Preview& PreviewFor(int mapId) {
    Preview& p = gPreviews[royale::ClampMap(mapId)];
    if (!p.tried) {
        p.tried = true;
        p.aerial = CVarGetInteger((std::string(CVAR_SETTING("Royale.PreviewAerial")) + std::to_string(mapId)).c_str(), 0) != 0;
        std::error_code ec;
        const std::filesystem::path path = PreviewPath(mapId);
        if (std::filesystem::exists(path, ec)) {
            const std::string bytes = ReadFileBytes(path);
            int w = 0, h = 0, comp = 0;
            unsigned char* px = bytes.empty() ? nullptr : stbi_load_from_memory(reinterpret_cast<const unsigned char*>(bytes.data()), static_cast<int>(bytes.size()), &w, &h, &comp, 4);
            if (px != nullptr) {
                p.tex = UploadRgba(px, w, h, &p.texId);
                p.have = p.tex != nullptr;
                stbi_image_free(px);
            }
        }
    }
    return p;
}

// Capture: armed by the frame update, the copy is queued in the game's display list at the end of the 3D drawing (OnPlayDrawEnd), and the
// pixels are there a frame later.
enum class CapState { Idle, Armed, Queued };
CapState gCapState = CapState::Idle;
int gCapMap = 0;
bool gCapAerial = false;
int gCapWait = 0;
std::vector<uint16_t> gCapBuf(kPreviewW * kPreviewH);

void ArmCapture(int mapId, bool aerial) {
    if (gCapState != CapState::Idle) return;
    gCapState = CapState::Armed;
    gCapMap = royale::ClampMap(mapId);
    gCapAerial = aerial;
}

void QueueCapture() {   // OnPlayDrawEnd
    if (gCapState != CapState::Armed || gPlayState == nullptr) return;
    OPEN_DISPS(gPlayState->state.gfxCtx);
    Gfx* gfx = OVERLAY_DISP;
    FB_WriteFramebufferSliceToCPU(&gfx, gCapBuf.data(), false);
    OVERLAY_DISP = gfx;
    CLOSE_DISPS(gPlayState->state.gfxCtx);
    gCapState = CapState::Queued;
    gCapWait = 2;
}

void FinishCapture() {   // a frame or two after QueueCapture
    if (gCapState != CapState::Queued || --gCapWait > 0) return;
    gCapState = CapState::Idle;
    std::vector<uint8_t> rgba(static_cast<size_t>(kPreviewW) * kPreviewH * 4);
    bool any = false;
    for (int i = 0; i < kPreviewW * kPreviewH; i++) {
        const uint16_t v = gCapBuf[i];   // RGBA 5551, as the renderer wrote it
        rgba[i * 4 + 0] = static_cast<uint8_t>(((v >> 11) & 31) * 255 / 31);
        rgba[i * 4 + 1] = static_cast<uint8_t>(((v >> 6) & 31) * 255 / 31);
        rgba[i * 4 + 2] = static_cast<uint8_t>(((v >> 1) & 31) * 255 / 31);
        rgba[i * 4 + 3] = 255;
        any |= v > 1;
    }
    if (!any) return;   // nothing came back (a black frame, or the renderer couldn't read it): try again later
    Preview& p = gPreviews[gCapMap];
    p.tried = true;
    p.have = (p.tex = UploadRgba(rgba.data(), kPreviewW, kPreviewH, &p.texId)) != nullptr;
    p.aerial = gCapAerial;
    std::error_code ec;
    std::filesystem::create_directories(PreviewDir(), ec);
    WritePng(PreviewPath(gCapMap), rgba.data(), kPreviewW, kPreviewH);
    CVarSetInteger((std::string(CVAR_SETTING("Royale.PreviewAerial")) + std::to_string(gCapMap)).c_str(), gCapAerial ? 1 : 0);
    Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

bool AutoPictures() { return CVarGetInteger(CVAR_SETTING("Royale.AutoMapPictures"), 1) != 0; }

// "Take map pictures": visit every map in turn and take its picture, then come back to the Temple of Time. Only outside a lobby.
struct Tour { bool on = false; int map = 0; int frames = 0; bool travelled = false; };
Tour gTour;

void StartTour() {
    if (!InGame() || gSession.Joined()) return;
    gTour = Tour{ true, 0, 0, false };
}

int gSettledFrames = 0;     // frames in the same scene with nothing going on
int gSettledScene = -1;
int gAerialFrames = 0;

void DrivePictures() {
    FinishCapture();
    if (!InGame()) { gSettledFrames = 0; return; }
    const bool calm = gPlayState->transitionTrigger == TRANS_TRIGGER_OFF && gPlayState->msgCtx.msgMode == MSGMODE_NONE && gPlayState->pauseCtx.state == 0;
    if (gPlayState->sceneNum != gSettledScene || !calm) { gSettledScene = gPlayState->sceneNum; gSettledFrames = 0; }
    else gSettledFrames++;
    int here = -1;
    for (int i = 0; i < royale::kMapCount; i++) if (gPlayState->sceneNum == royale::kMaps[i].scene) here = i;

    if (gTour.on) {
        if (gSession.Joined()) { gTour.on = false; return; }
        if (gTour.map >= royale::kMapCount) {   // done: back to the Temple of Time
            if (GoToWaitingRoom()) { gTour.on = false; Say("Map pictures taken"); }
            return;
        }
        if (++gTour.frames > 25 * royale::kTickHz) { gTour.map++; gTour.frames = 0; gTour.travelled = false; return; }   // couldn't get there: skip it
        if (here != gTour.map) {
            if (!gTour.travelled || gPlayState->transitionTrigger == TRANS_TRIGGER_OFF) {
                gSaveContext.showTitleCard = false;   // no place-name card in the picture
                if (TravelTo(EntranceFor(gTour.map))) gTour.travelled = true;
            }
            return;
        }
        if (gSettledFrames == 3 * royale::kTickHz) ArmCapture(gTour.map, false);
        if (gSettledFrames > 3 * royale::kTickHz + 4 && gCapState == CapState::Idle) { gTour.map++; gTour.frames = 0; gTour.travelled = false; }
        return;
    }

    if (here < 0 || !AutoPictures()) { gAerialFrames = 0; return; }
    const royale::HudState hud = gSession.Hud();
    const bool aerial = gSession.Joined() && hud.state == royale::MatchState::Drop && gSkydiving;
    gAerialFrames = aerial ? gAerialFrames + 1 : 0;
    Preview& p = PreviewFor(here);
    if (aerial && gAerialFrames == royale::kTickHz * 3 / 2 && !p.aerial) ArmCapture(here, true);
    else if (!p.have && gSettledFrames > 0 && gSettledFrames % (5 * royale::kTickHz) == 0) ArmCapture(here, false);   // (tried again if it came back empty)
}

// ---- menu state ----------------------------------------------------------------------------------------------------------------

enum class Page { Main, Play, Setup, Join, Maps, Locker, Options, Controls, Lobby, Invite, Match, Results, Keyboard, ConfirmLeave };
enum class Launch { None, Solo, Host, Join };

struct PageState { Page page; int cursor = 0; float scroll = 0.0f; };

struct MenuState {
    bool open = false;
    std::vector<PageState> stack;
    double openedAt = 0.0;
    // input, gathered by the game's frame hook and spent by the drawing
    bool up = false, down = false, left = false, right = false, a = false, b = false, start = false;
    int swallow = 0;               // frames of the game's input to throw away after closing (so the closing press doesn't reach Link)
    bool passStart = false;        // hand one Start press to the game (opens its own pause screen)
    // what to do once the Battle Royale save has loaded
    Launch launch = Launch::None;
    bool launching = false;        // the title screen is fading out to load the save
    bool booted = false;           // this load came from our boot (the entrance gets overridden)
    bool justBooted = false;       // ...and the game hasn't settled in yet (then the chosen action runs, or the main menu opens)
    int launchWait = 0;
    int soloWait = 0;              // solo: waiting for the lobby to exist before pressing Start
    bool setupIsSolo = false;      // the Setup page was opened for "Play solo" (its button starts the match)
    // feedback
    std::string note;
    double noteUntil = 0.0;
    ImU32 noteColour = 0;
    // the on-screen keyboard
    char* kbBuf = nullptr;
    size_t kbSize = 0;
    std::string kbTitle;
    std::string kbText;
    bool kbDigitsOnly = false;
    std::function<void(const std::string&)> kbDone;
    int kbCol = 0, kbRow = 0;
    // last frame's rows, for touch and for scrolling
    struct Hit { ImVec2 a, b; int index; };
    std::vector<Hit> hits;
    float selTop = 0.0f, selBottom = 0.0f;
    bool mouseMoved = false;
    ImVec2 lastMouse = ImVec2(-1, -1);
    bool touchBack = false;
};
MenuState gMenu;

void ShowNote(const std::string& text, ImU32 colour = IM_COL32(255, 120, 100, 255), float seconds = 4.0f) {
    gMenu.note = text;
    gMenu.noteColour = colour;
    gMenu.noteUntil = ImGui::GetTime() + seconds;
}

bool OnTitleScreen() {
    return gGameState != nullptr && gGameState->init == Play_Init && gPlayState != nullptr && gSaveContext.gameMode == GAMEMODE_TITLE_SCREEN;
}

Page HomePage() {
    if (!gSession.Joined()) return Page::Main;
    const royale::HudState h = gSession.Hud();
    if (!h.connected) return Page::Lobby;
    switch (h.state) {
        case royale::MatchState::Lobby: return Page::Lobby;
        case royale::MatchState::Ending: return Page::Results;
        default: return Page::Match;
    }
}

void MenuOpen(Page page) {
    if (!gMenu.open) { gMenu.openedAt = ImGui::GetTime(); MenuSfx(NA_SE_SY_WIN_OPEN); }
    gMenu.open = true;
    gMenu.stack.clear();
    gMenu.stack.push_back({ page });
}
void MenuOpenHome() { MenuOpen(HomePage()); }

void MenuClose() {
    if (!gMenu.open) return;
    gMenu.open = false;
    gMenu.stack.clear();
    gMenu.swallow = 3;
    MenuSfx(NA_SE_SY_WIN_CLOSE);
}

void Push(Page page) { gMenu.stack.push_back({ page }); MenuSfx(NA_SE_SY_DECIDE); }
void Pop() {
    MenuSfx(NA_SE_SY_CANCEL);
    if (gMenu.stack.size() > 1) gMenu.stack.pop_back();
    else MenuClose();
}
PageState& Top() { return gMenu.stack.back(); }

void OpenKeyboard(const std::string& title, char* buf, size_t size, bool digitsOnly, std::function<void(const std::string&)> done) {
    gMenu.kbBuf = buf;
    gMenu.kbSize = size;
    gMenu.kbTitle = title;
    gMenu.kbText = buf;
    gMenu.kbDigitsOnly = digitsOnly;
    gMenu.kbDone = std::move(done);
    gMenu.kbCol = gMenu.kbRow = 0;
    Push(Page::Keyboard);
}

// ---- input --------------------------------------------------------------------------------------------------------------------
// Read from the game's own controller input (so the player's button mapping applies), before the game state runs. While the menu is open the
// input is then cleared, so Link and the game's own menus don't react to it.

struct Repeat { int held = 0; };
Repeat gRepUp, gRepDown, gRepLeft, gRepRight;

bool Repeating(Repeat& r, bool on) {
    if (!on) { r.held = 0; return false; }
    r.held++;
    return r.held == 1 || (r.held > 7 && (r.held - 7) % 2 == 0);
}

void ClearInput(Input& in) {
    in.cur.button = in.press.button = in.rel.button = 0;
    in.cur.stick_x = in.cur.stick_y = in.rel.stick_x = in.rel.stick_y = 0;
    in.press.stick_x = in.press.stick_y = 0;
}

void LaunchFromTitle(Launch what) {
    if (!OnTitleScreen() || gPlayState->transitionTrigger != TRANS_TRIGGER_OFF) return;
    gMenu.launch = what;
    gMenu.launching = true;
    Audio_SetCutsceneFlag(0);
    MenuSfx(NA_SE_SY_FSEL_DECIDE_L);
    gSaveContext.gameMode = GAMEMODE_FILE_SELECT;   // the file select loads our save at once (patches/0012)
    gPlayState->transitionTrigger = TRANS_TRIGGER_START;
    gPlayState->transitionType = TRANS_TYPE_FADE_BLACK;
    gMenu.open = false;
    gMenu.stack.clear();
}

void OnMenuMainStart() {
    if (gGameState == nullptr) return;
    Input& in = gGameState->input[0];
    if (gGameState->init != Play_Init) { gMenu.launching = false; if (gMenu.open) MenuClose(); return; }

    if (gMenu.passStart) {   // "Item screen": let one Start through so the game opens its own pause screen
        gMenu.passStart = false;
        in.press.button |= BTN_START;
        in.cur.button |= BTN_START;
        return;
    }

    const bool title = OnTitleScreen();
    if (!title && !InGame() && gMenu.open) MenuClose();
    if (title && gMenu.launching) { ClearInput(in); return; }

    if (!gMenu.open) {
        const u16 p = in.press.button;
        if (title && gSaveContext.fileNum != 0xFEDC && (p & (BTN_START | BTN_A))) {
            MenuOpen(Page::Main);
            ClearInput(in);
            return;
        }
        if (InGame() && (p & BTN_START) && gPlayState->pauseCtx.state == 0 && gPlayState->transitionTrigger == TRANS_TRIGGER_OFF &&
            gPlayState->msgCtx.msgMode == MSGMODE_NONE) {
            MenuOpenHome();
            ClearInput(in);
            return;
        }
        if (gMenu.swallow > 0) { gMenu.swallow--; ClearInput(in); }
        return;
    }

    const u16 cur = in.cur.button, press = in.press.button;
    const bool stickUp = in.cur.stick_y > 45, stickDown = in.cur.stick_y < -45, stickLeft = in.cur.stick_x < -45, stickRight = in.cur.stick_x > 45;
    gMenu.up |= Repeating(gRepUp, (cur & BTN_DUP) || stickUp);
    gMenu.down |= Repeating(gRepDown, (cur & BTN_DDOWN) || stickDown);
    gMenu.left |= Repeating(gRepLeft, (cur & BTN_DLEFT) || stickLeft);
    gMenu.right |= Repeating(gRepRight, (cur & BTN_DRIGHT) || stickRight);
    gMenu.a |= (press & BTN_A) != 0;
    gMenu.b |= (press & BTN_B) != 0;
    gMenu.start |= (press & BTN_START) != 0;
    ClearInput(in);
}

// The Battle Royale save: one of the three save slots, made the first time it's needed. Called by the file select (patches/0012) on every
// frame it runs; returns the slot to load straight away, or -1 to show the normal file select.
int ChooseRoyaleSlot(bool* made) {
    *made = false;
    char key[48];
    for (int i = 0; i < 3; i++) {
        std::snprintf(key, sizeof(key), CVAR_SETTING("Royale.BRFile%d"), i);
        if (CVarGetInteger(key, 0) != 0 && Save_Exist(i)) return i;
    }
    for (int i = 0; i < 3; i++) {
        if (Save_Exist(i)) continue;
        // A fresh save, made the way the game makes a new file, then set up for Battle Royale: a child in the Temple of Time.
        gSaveContext.fileNum = i;
        Save_InitFile(false);
        gSaveContext.ship.quest.id = QUEST_BATTLEROYALE;
        gSaveContext.linkAge = 1;
        gSaveContext.dayTime = 0x6AAB;
        gSaveContext.cutsceneIndex = 0;
        gSaveContext.entranceIndex = ENTR_TEMPLE_OF_TIME_ENTRANCE;
        gSaveContext.savedSceneNum = SCENE_TEMPLE_OF_TIME;
        gSaveContext.ship.filenameLanguage = NAME_LANGUAGE_PAL;
        const char* name = CleanName(Ui().name);
        for (int c = 0; c < 8; c++) gSaveContext.playerName[c] = c < static_cast<int>(std::strlen(name)) ? FilenameChar(name[c]) : 0x3E;
        Save_SaveFile();
        SaveManager_ThreadPoolWait();
        std::snprintf(key, sizeof(key), CVAR_SETTING("Royale.BRFile%d"), i);
        CVarSetInteger(key, 1);
        CVarSave();
        *made = true;
        return i;
    }
    return -1;
}

} // namespace (the two hooks below are called from the game's C code)

extern "C" int Royale_BootFileSlot(void) {
    if (CVarGetInteger(CVAR_SETTING("Royale.ClassicFileSelect"), 0) != 0) return -1;
    static int lastFrameAnswered = -1;
    bool made = false;
    const int slot = ChooseRoyaleSlot(&made);
    if (slot < 0) {
        if (lastFrameAnswered != -2) ShowNote("All three save slots hold story saves. Erase one (or start a Battle Royale file) to play.", IM_COL32(255, 200, 90, 255), 12.0f);
        lastFrameAnswered = -2;
        return -1;
    }
    lastFrameAnswered = slot;
    gMenu.booted = true;
    return slot;
}

extern "C" int Royale_TitleMenuOpen(void) {
    return gMenu.open && OnTitleScreen() ? 1 : 0;
}

namespace {

// After our boot loads the save: start in the Temple of Time (the waiting room), not in Link's house.
void OnMenuLoadGame(int32_t) {
    if (!gMenu.booted) return;
    gMenu.booted = false;
    gSaveContext.entranceIndex = ENTR_TEMPLE_OF_TIME_ENTRANCE;
    gSaveContext.cutsceneIndex = 0;
    gMenu.launchWait = 0;
    gMenu.justBooted = true;
}

// What "Play solo", "Host" and "Join" do, once the game is running.
void DoLaunch(Launch what) {
    UiState& ui = Ui();
    ui.error.clear();
    ui.port = std::clamp(ui.port, 1024, 65535);
    SaveUi(ui);
    gSession.ClearLastEnded();
    if (what == Launch::Join) {
        if (!gSession.Join(ui.address, static_cast<uint16_t>(ui.port), CleanName(ui.name), &ui.error)) {
            ShowNote("Could not join: " + ui.error);
            MenuOpen(Page::Join);
        } else {
            MenuOpen(Page::Lobby);   // "Connecting...", then the lobby
        }
        return;
    }
    if (!gSession.Host(static_cast<uint16_t>(ui.port), CleanName(ui.name), &ui.error)) {
        ShowNote("Could not start: " + ui.error);
        MenuOpen(Page::Play);
        return;
    }
    RefreshLocalAddresses(ui, true);
    if (what == Launch::Solo) {
        gMenu.soloWait = 10 * royale::kTickHz;
        MenuClose();
    } else {
        MenuOpen(Page::Lobby);
        Top().cursor = 0;
    }
}

void Choose(Launch what) {
    if (OnTitleScreen()) LaunchFromTitle(what);
    else if (InGame()) DoLaunch(what);
}

bool gMenuWasJoined = false;
int gInGameFrames = 0;

void OnMenuFrame() {
    DrivePictures();
    const bool in = InGame();
    gInGameFrames = in ? gInGameFrames + 1 : 0;
    if (!in) return;

    // Just loaded into the game from our boot: carry out what was chosen on the title screen, or show the main menu.
    if (gMenu.justBooted && gInGameFrames > royale::kTickHz && gPlayState->transitionTrigger == TRANS_TRIGGER_OFF) {
        gMenu.justBooted = false;
        gMenu.launching = false;
        const Launch what = gMenu.launch;
        gMenu.launch = Launch::None;
        if (what != Launch::None && !gSession.Joined()) DoLaunch(what);
        else if (!gSession.Joined() && !gMenu.open) MenuOpen(Page::Main);
    }

    // Solo: the lobby needs a moment to exist, then the match starts by itself.
    if (gMenu.soloWait > 0) {
        gMenu.soloWait--;
        const royale::HudState h = gSession.Hud();
        if (gSession.Joined() && h.connected && h.isHost && h.state == royale::MatchState::Lobby) {
            gPendingStart = true;
            gMenu.soloWait = 0;
        }
    }

    // Left a lobby or a match: back to the main menu.
    const bool joined = gSession.Joined();
    if (gMenuWasJoined && !joined && !gMenu.open) MenuOpen(Page::Main);
    gMenuWasJoined = joined;
    // The page on screen no longer fits (the match started, ended, or you left): show the one that does.
    if (gMenu.open && gMenu.stack.size() == 1) {
        const Page now = Top().page, home = HomePage();
        const bool lobbyPages = now == Page::Lobby || now == Page::Match || now == Page::Results || now == Page::Main;
        if (lobbyPages && now != home) { gMenu.stack.clear(); gMenu.stack.push_back({ home }); }
    }
}

// ---- drawing: the Ocarina of Time look ------------------------------------------------------------------------------------------

const ImU32 kMenuGold = IM_COL32(255, 214, 90, 255), kMenuGoldDark = IM_COL32(176, 118, 24, 255), kMenuCream = IM_COL32(236, 228, 190, 255),
            kMenuWhite = IM_COL32(255, 255, 255, 255), kMenuGrey = IM_COL32(170, 162, 150, 255), kMenuGreen = IM_COL32(110, 220, 110, 255),
            kMenuRed = IM_COL32(236, 90, 70, 255), kMenuSelText = IM_COL32(255, 255, 150, 255);

struct Ctx {
    ImDrawList* dl;
    ImFont* font;
    ImVec2 ds;
    float s;           // scale: 1 at 720 lines
    float x0, x1;      // the panel's inside
    float y;           // where the next row goes
    float clipTop, clipBottom;
    int index = 0;     // rows so far this frame
    int cursor;
    int tapped = -1;   // the row a touch landed on (becomes the cursor)
    float t;           // seconds since the menu opened
    bool tap = false;  // a click or touch landed this frame
    ImVec2 mouse;
};

ImU32 Alpha(ImU32 c, float a) { return (c & 0x00FFFFFF) | (static_cast<ImU32>(((c >> 24) & 0xFF) * std::clamp(a, 0.0f, 1.0f)) << 24); }

void Lettered(ImDrawList* dl, ImFont* font, float size, ImVec2 at, ImU32 col, const char* text, float outline = 0.0f) {
    const float o = outline > 0 ? outline : std::max(1.5f, size * 0.06f);
    const ImU32 shadow = Alpha(IM_COL32(20, 10, 0, 235), ((col >> 24) & 0xFF) / 255.0f);
    for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) if (dx || dy) dl->AddText(font, size, ImVec2(at.x + dx * o, at.y + dy * o), shadow, text);
    dl->AddText(font, size, at, col, text);
}

// A text-box panel: the game's dark message-box black, rounded, with the gold double rule and diamond corners of the match splash.
void OotPanelBox(ImDrawList* dl, ImVec2 a, ImVec2 b, float s, int alpha = 215) {
    dl->AddRectFilled(ImVec2(a.x + 5 * s, a.y + 6 * s), ImVec2(b.x + 5 * s, b.y + 6 * s), IM_COL32(0, 0, 0, alpha / 3), 14 * s);
    dl->AddRectFilled(a, b, OotPanel(alpha), 14 * s);
    dl->AddRect(a, b, kMenuGoldDark, 14 * s, 0, 2.0f * s);
    dl->AddRect(ImVec2(a.x + 5 * s, a.y + 5 * s), ImVec2(b.x - 5 * s, b.y - 5 * s), Alpha(kMenuGold, 0.55f), 10 * s, 0, 1.0f * s);
    const ImVec2 corners[4] = { a, ImVec2(b.x, a.y), ImVec2(a.x, b.y), b };
    for (const ImVec2& c : corners) {
        const float r = 6 * s;
        const ImVec2 cc(c.x + (c.x == a.x ? 6 * s : -6 * s), c.y + (c.y == a.y ? 6 * s : -6 * s));
        dl->AddQuadFilled(ImVec2(cc.x, cc.y - r), ImVec2(cc.x + r, cc.y), ImVec2(cc.x, cc.y + r), ImVec2(cc.x - r, cc.y), kMenuGold);
    }
}

// The pause menu's cursor: four gold corner brackets that pulse in and out around the chosen thing.
void OotCursor(ImDrawList* dl, ImVec2 a, ImVec2 b, float s, float t) {
    const float pulse = 0.5f + 0.5f * std::sin(t * 6.0f);
    const float out = (2.0f + 3.0f * pulse) * s, len = 12 * s, w = 3.0f * s;
    const ImU32 col = Alpha(IM_COL32(255, 236, 120, 255), 0.75f + 0.25f * pulse);
    a = ImVec2(a.x - out, a.y - out);
    b = ImVec2(b.x + out, b.y + out);
    dl->AddLine(a, ImVec2(a.x + len, a.y), col, w); dl->AddLine(a, ImVec2(a.x, a.y + len), col, w);
    dl->AddLine(ImVec2(b.x, a.y), ImVec2(b.x - len, a.y), col, w); dl->AddLine(ImVec2(b.x, a.y), ImVec2(b.x, a.y + len), col, w);
    dl->AddLine(ImVec2(a.x, b.y), ImVec2(a.x + len, b.y), col, w); dl->AddLine(ImVec2(a.x, b.y), ImVec2(a.x, b.y - len), col, w);
    dl->AddLine(b, ImVec2(b.x - len, b.y), col, w); dl->AddLine(b, ImVec2(b.x, b.y - len), col, w);
}

// A button prompt in the corner, as the game's HUD draws them: the round button in its colour, and the game's own action word beside it.
float ButtonPrompt(ImDrawList* dl, ImFont* font, float x, float y, float s, const char* letter, ImU32 colour, const char* wordTex, const char* fallback) {
    const float r = 17 * s;
    const ImVec2 c(x + r, y);
    dl->AddCircleFilled(ImVec2(c.x + 2 * s, c.y + 3 * s), r, IM_COL32(0, 0, 0, 140));
    dl->AddCircleFilled(c, r, colour);
    dl->AddCircle(c, r, IM_COL32(255, 255, 255, 120), 0, 1.5f * s);
    const ImVec2 lsz = font->CalcTextSizeA(20 * s, FLT_MAX, 0, letter);
    dl->AddText(font, 20 * s, ImVec2(c.x - lsz.x * 0.5f, c.y - lsz.y * 0.5f), kMenuWhite, letter);
    float nx = c.x + r + 8 * s;
    ImVec2 tsz;
    if (ImTextureID tex = wordTex ? OotTexture(wordTex, &tsz) : nullptr) {
        const float h = 22 * s, w = h * tsz.x / std::max(1.0f, tsz.y);
        dl->AddImage(tex, ImVec2(nx + 1.5f * s, y - h * 0.5f + 1.5f * s), ImVec2(nx + w + 1.5f * s, y + h * 0.5f + 1.5f * s), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(0, 0, 0, 200));
        dl->AddImage(tex, ImVec2(nx, y - h * 0.5f), ImVec2(nx + w, y + h * 0.5f));
        return nx + w + 22 * s;
    }
    const ImVec2 fsz = font->CalcTextSizeA(20 * s, FLT_MAX, 0, fallback);
    Lettered(dl, font, 20 * s, ImVec2(nx, y - fsz.y * 0.5f), kMenuWhite, fallback);
    return nx + fsz.x + 22 * s;
}

// ---- rows -----------------------------------------------------------------------------------------------------------------------
// Immediate mode: each page lists its rows every frame; the chosen one is the page's cursor. Each row returns true when it was used.

float RowHeight(const Ctx& c) { return 46 * c.s; }

struct RowResult { bool selected; bool activated; int nudge; ImVec2 a, b; bool visible; };

RowResult BeginRow(Ctx& c, float height) {
    RowResult r{};
    const int idx = c.index++;
    r.selected = idx == c.cursor;
    r.a = ImVec2(c.x0, c.y);
    r.b = ImVec2(c.x1, c.y + height);
    r.visible = r.b.y > c.clipTop && r.a.y < c.clipBottom;
    if (r.selected) { gMenu.selTop = r.a.y; gMenu.selBottom = r.b.y; }
    if (r.visible) gMenu.hits.push_back({ r.a, r.b, idx });
    if (r.selected) {
        if (gMenu.a) r.activated = true;
        if (gMenu.left) r.nudge = -1;
        if (gMenu.right) r.nudge = 1;
    }
    // touch: the row under the finger
    if (c.tap && r.visible && c.mouse.x >= r.a.x && c.mouse.x <= r.b.x && c.mouse.y >= r.a.y && c.mouse.y <= r.b.y && c.mouse.y >= c.clipTop && c.mouse.y <= c.clipBottom) {
        c.tapped = idx;
        r.selected = true;
        r.nudge = 0;   // (sliders and choices read which side was touched, see TouchSide)
        r.activated = true;
    }
    if (r.visible && r.selected) {
        c.dl->AddRectFilled(ImVec2(r.a.x - 4 * c.s, r.a.y + 2 * c.s), ImVec2(r.b.x + 4 * c.s, r.b.y - 2 * c.s), IM_COL32(255, 220, 120, 34), 6 * c.s);
        OotCursor(c.dl, ImVec2(r.a.x - 4 * c.s, r.a.y + 2 * c.s), ImVec2(r.b.x + 4 * c.s, r.b.y - 2 * c.s), c.s, c.t);
    }
    c.y += height;
    return r;
}

void RowLabel(Ctx& c, const RowResult& r, const char* label, royale::ItemId icon, ImU32 colour = 0) {
    if (!r.visible) return;
    float x = r.a.x + 10 * c.s;
    const float midY = (r.a.y + r.b.y) * 0.5f;
    if (icon != kNoIcon) {
        const float is = 34 * c.s;
        DrawItemIcon(c.dl, icon, ImVec2(x + is * 0.5f, midY), is, IM_COL32(255, 214, 90, 255));
        x += is + 12 * c.s;
    }
    const float size = 23 * c.s;
    const ImVec2 sz = c.font->CalcTextSizeA(size, FLT_MAX, 0, label);
    Lettered(c.dl, c.font, size, ImVec2(x, midY - sz.y * 0.5f), colour ? colour : (r.selected ? kMenuSelText : kMenuWhite), label);
}

void RowValue(Ctx& c, const RowResult& r, const std::string& value, ImU32 colour, bool arrows) {
    if (!r.visible) return;
    const float size = 21 * c.s, midY = (r.a.y + r.b.y) * 0.5f;
    const ImVec2 sz = c.font->CalcTextSizeA(size, FLT_MAX, 0, value.c_str());
    float right = r.b.x - 12 * c.s;
    if (arrows) {
        const float aw = 8 * c.s, pulse = r.selected ? 2.0f * c.s * std::sin(c.t * 8.0f) : 0.0f;
        const ImU32 ac = r.selected ? kMenuGold : Alpha(kMenuGold, 0.4f);
        c.dl->AddTriangleFilled(ImVec2(right + pulse, midY), ImVec2(right - aw + pulse, midY - aw), ImVec2(right - aw + pulse, midY + aw), ac);
        right -= aw + 10 * c.s;
        const float lx = right - sz.x - 10 * c.s;
        c.dl->AddTriangleFilled(ImVec2(lx - aw - pulse, midY), ImVec2(lx - pulse, midY - aw), ImVec2(lx - pulse, midY + aw), ac);
    }
    Lettered(c.dl, c.font, size, ImVec2(right - sz.x, midY - sz.y * 0.5f), colour, value.c_str());
}

bool Button(Ctx& c, const char* label, royale::ItemId icon = kNoIcon, const char* value = nullptr, bool enabled = true) {
    RowResult r = BeginRow(c, RowHeight(c));
    RowLabel(c, r, label, icon, enabled ? 0 : kMenuGrey);
    if (value) RowValue(c, r, value, kMenuCream, false);
    if (r.activated && !enabled) { MenuSfx(NA_SE_SY_ERROR); return false; }
    return r.activated && enabled;
}

// Touch on a slider or a choice: the right half steps up, the left half steps down.
int TouchSide(const Ctx& c, const RowResult& r) {
    if (!(c.tap && c.mouse.x >= r.a.x && c.mouse.x <= r.b.x && c.mouse.y >= r.a.y && c.mouse.y <= r.b.y)) return 0;
    return c.mouse.x > (r.a.x + r.b.x) * 0.5f ? 1 : -1;
}

bool Toggle(Ctx& c, const char* label, bool* v, royale::ItemId icon = kNoIcon) {
    RowResult r = BeginRow(c, RowHeight(c));
    RowLabel(c, r, label, icon);
    bool changed = false;
    if (r.activated || r.nudge == -1 || r.nudge == 1) { *v = r.nudge == 1 ? true : r.nudge == -1 ? false : !*v; changed = true; }
    if (r.visible) {   // a little lever: green and ON, or dark and OFF
        const float w = 64 * c.s, h = 26 * c.s, midY = (r.a.y + r.b.y) * 0.5f, x1 = r.b.x - 12 * c.s, x0 = x1 - w;
        c.dl->AddRectFilled(ImVec2(x0, midY - h * 0.5f), ImVec2(x1, midY + h * 0.5f), *v ? IM_COL32(52, 128, 50, 255) : IM_COL32(60, 50, 42, 255), h * 0.5f);
        c.dl->AddRect(ImVec2(x0, midY - h * 0.5f), ImVec2(x1, midY + h * 0.5f), kMenuGoldDark, h * 0.5f, 0, 1.5f * c.s);
        const float kx = *v ? x1 - h * 0.5f : x0 + h * 0.5f;
        c.dl->AddCircleFilled(ImVec2(kx, midY), h * 0.42f, *v ? kMenuGold : kMenuGrey);
        const char* word = *v ? "ON" : "OFF";
        const ImVec2 sz = c.font->CalcTextSizeA(15 * c.s, FLT_MAX, 0, word);
        c.dl->AddText(c.font, 15 * c.s, ImVec2(*v ? x0 + 9 * c.s : x1 - sz.x - 9 * c.s, midY - sz.y * 0.5f), kMenuWhite, word);
    }
    if (changed) MenuSfx(NA_SE_SY_CURSOR);
    return changed;
}

bool Slider(Ctx& c, const char* label, int* v, int lo, int hi, int step, const char* suffix = "") {
    RowResult r = BeginRow(c, RowHeight(c));
    RowLabel(c, r, label, kNoIcon);
    int dir = r.nudge;
    if (const int side = TouchSide(c, r)) dir = side;
    const int before = *v;
    if (dir) *v = std::clamp(*v + dir * step, lo, hi);
    if (r.visible) {
        const float w = 170 * c.s, h = 9 * c.s, midY = (r.a.y + r.b.y) * 0.5f, x1 = r.b.x - 70 * c.s, x0 = x1 - w;
        const float f = hi > lo ? static_cast<float>(*v - lo) / static_cast<float>(hi - lo) : 0.0f;
        c.dl->AddRectFilled(ImVec2(x0, midY - h * 0.5f), ImVec2(x1, midY + h * 0.5f), IM_COL32(40, 32, 24, 255), h * 0.5f);
        c.dl->AddRectFilled(ImVec2(x0, midY - h * 0.5f), ImVec2(x0 + w * f, midY + h * 0.5f), IM_COL32(62, 142, 58, 255), h * 0.5f);
        c.dl->AddRect(ImVec2(x0, midY - h * 0.5f), ImVec2(x1, midY + h * 0.5f), kMenuGoldDark, h * 0.5f, 0, 1.2f * c.s);
        c.dl->AddCircleFilled(ImVec2(x0 + w * f, midY), h, r.selected ? kMenuGold : kMenuCream);
        const std::string text = std::to_string(*v) + suffix;
        const ImVec2 sz = c.font->CalcTextSizeA(20 * c.s, FLT_MAX, 0, text.c_str());
        Lettered(c.dl, c.font, 20 * c.s, ImVec2(r.b.x - 12 * c.s - sz.x, midY - sz.y * 0.5f), kMenuCream, text.c_str());
    }
    if (*v != before) MenuSfx(NA_SE_SY_CURSOR);
    return *v != before;
}

bool Choice(Ctx& c, const char* label, int* v, const char* const* names, int count, royale::ItemId icon = kNoIcon) {
    RowResult r = BeginRow(c, RowHeight(c));
    RowLabel(c, r, label, icon);
    int dir = r.nudge;
    if (const int side = TouchSide(c, r)) dir = side;
    else if (r.activated && dir == 0) dir = 1;
    const int before = *v;
    if (dir) *v = (*v + dir + count) % count;
    RowValue(c, r, names[std::clamp(*v, 0, count - 1)], kMenuCream, true);
    if (*v != before) MenuSfx(NA_SE_SY_CURSOR);
    return *v != before;
}

// A line of text that can be selected (so long pages scroll with the cursor), and does nothing.
void Info(Ctx& c, const char* label, const char* value = nullptr, ImU32 colour = 0) {
    RowResult r = BeginRow(c, 36 * c.s);
    if (!r.visible) return;
    const float size = 19 * c.s, midY = (r.a.y + r.b.y) * 0.5f;
    const ImVec2 sz = c.font->CalcTextSizeA(size, FLT_MAX, 0, label);
    Lettered(c.dl, c.font, size, ImVec2(r.a.x + 12 * c.s, midY - sz.y * 0.5f), colour ? colour : kMenuCream, label);
    if (value) {
        const ImVec2 vs = c.font->CalcTextSizeA(size, FLT_MAX, 0, value);
        Lettered(c.dl, c.font, size, ImVec2(r.b.x - 12 * c.s - vs.x, midY - vs.y * 0.5f), kMenuGold, value);
    }
}

// A label and a value that can't be selected (match facts at the top of a page).
void Line(Ctx& c, const char* label, const char* value = nullptr, ImU32 colour = 0) {
    const float size = 21 * c.s, h = 38 * c.s, midY = c.y + h * 0.5f;
    if (c.y + h > c.clipTop && c.y < c.clipBottom) {
        const ImVec2 sz = c.font->CalcTextSizeA(size, FLT_MAX, 0, label);
        Lettered(c.dl, c.font, size, ImVec2(c.x0 + 12 * c.s, midY - sz.y * 0.5f), colour ? colour : kMenuCream, label);
        if (value) {
            const ImVec2 vs = c.font->CalcTextSizeA(size, FLT_MAX, 0, value);
            Lettered(c.dl, c.font, size, ImVec2(c.x1 - 12 * c.s - vs.x, midY - vs.y * 0.5f), kMenuGold, value);
        }
    }
    c.y += h;
}

// Text that is not a row: notes under a row, wrapped to the panel.
void Note(Ctx& c, const std::string& text, ImU32 colour = 0) {
    const float size = 17 * c.s, wrap = c.x1 - c.x0 - 24 * c.s;
    const ImVec2 sz = c.font->CalcTextSizeA(size, FLT_MAX, wrap, text.c_str());
    if (c.y + sz.y > c.clipTop && c.y < c.clipBottom) c.dl->AddText(c.font, size, ImVec2(c.x0 + 12 * c.s, c.y + 3 * c.s), colour ? colour : kMenuGrey, text.c_str(), nullptr, wrap);
    c.y += sz.y + 10 * c.s;
}

void Header(Ctx& c, const char* text) {
    c.y += 8 * c.s;
    const float size = 18 * c.s;
    if (c.y > c.clipTop - 30 * c.s && c.y < c.clipBottom) {
        const ImVec2 sz = c.font->CalcTextSizeA(size, FLT_MAX, 0, text);
        Lettered(c.dl, c.font, size, ImVec2(c.x0 + 10 * c.s, c.y), kMenuGold, text);
        const float ly = c.y + sz.y * 0.5f;
        c.dl->AddLine(ImVec2(c.x0 + 20 * c.s + sz.x, ly), ImVec2(c.x1 - 10 * c.s, ly), Alpha(kMenuGoldDark, 0.8f), 1.5f * c.s);
    }
    c.y += 28 * c.s;
}

void Saved() { SaveUi(Ui()); }

// ---- the right-hand card: a map's picture and name ---------------------------------------------------------------------------

void DrawMapCard(const Ctx& c, int mapId, ImVec2 a, ImVec2 b, const char* caption = nullptr) {
    ImDrawList* dl = c.dl;
    const float s = c.s;
    OotPanelBox(dl, a, b, s, 230);
    const ImVec2 ia(a.x + 14 * s, a.y + 14 * s);
    const float iw = b.x - a.x - 28 * s, ih = std::min(iw * 0.75f, b.y - a.y - 120 * s);
    const ImVec2 ib(ia.x + iw, ia.y + ih);
    Preview& p = PreviewFor(mapId);
    if (p.have) {
        dl->AddImageRounded(p.tex, ia, ib, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255), 8 * s);
    } else {   // no picture yet: the region's colours, and how to get one
        const MapColours mc = MapPalette(mapId);
        dl->AddRectFilledMultiColor(ia, ib, mc.top, mc.top, mc.bottom, mc.bottom);
        const char* hint = "No picture yet: it is taken the first time you are here";
        const ImVec2 hs = c.font->CalcTextSizeA(17 * s, FLT_MAX, iw - 30 * s, hint);
        dl->AddText(c.font, 17 * s, ImVec2(ia.x + (iw - hs.x) * 0.5f, ia.y + ih * 0.5f - hs.y * 0.5f), IM_COL32(255, 255, 255, 220), hint, nullptr, iw - 30 * s);
    }
    dl->AddRect(ia, ib, kMenuGoldDark, 8 * s, 0, 2.0f * s);
    // the game's own place-name card across the bottom of the picture, as when you arrive there
    ImVec2 tsz;
    if (ImTextureID card = OotTexture(MapTitleCard(mapId), &tsz)) {
        const float h = 34 * s, w = h * tsz.x / std::max(1.0f, tsz.y);
        const ImVec2 ca(ia.x + (iw - w) * 0.5f, ib.y - h - 10 * s);
        dl->AddRectFilled(ImVec2(ia.x, ca.y - 6 * s), ImVec2(ib.x, ib.y), IM_COL32(0, 0, 0, 110), 8 * s, ImDrawFlags_RoundCornersBottom);
        dl->AddImage(card, ImVec2(ca.x + 2 * s, ca.y + 2 * s), ImVec2(ca.x + w + 2 * s, ca.y + h + 2 * s), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(0, 0, 0, 220));
        dl->AddImage(card, ca, ImVec2(ca.x + w, ca.y + h));
    } else {
        const char* name = royale::MapOf(mapId).name;
        const ImVec2 ns = c.font->CalcTextSizeA(28 * s, FLT_MAX, 0, name);
        Lettered(dl, c.font, 28 * s, ImVec2(ia.x + (iw - ns.x) * 0.5f, ib.y - ns.y - 12 * s), kMenuWhite, name);
    }
    const float wrap = iw;
    const char* blurb = royale::MapOf(mapId).blurb;
    dl->AddText(c.font, 18 * s, ImVec2(ia.x, ib.y + 12 * s), kMenuCream, blurb, nullptr, wrap);
    if (caption) {
        const ImVec2 cs = c.font->CalcTextSizeA(16 * s, FLT_MAX, wrap, caption);
        dl->AddText(c.font, 16 * s, ImVec2(ia.x, b.y - cs.y - 14 * s), kMenuGrey, caption, nullptr, wrap);
    }
}

void DrawRosterCard(const Ctx& c, const royale::HudState& h, ImVec2 a, ImVec2 b) {
    OotPanelBox(c.dl, a, b, c.s, 230);
    const float s = c.s;
    float y = a.y + 16 * s;
    Lettered(c.dl, c.font, 20 * s, ImVec2(a.x + 18 * s, y), kMenuGold, ("Players  " + std::to_string(h.humanCount) + " / " + std::to_string(h.playerLimit)).c_str());
    y += 32 * s;
    std::vector<const royale::RosterRow*> rows;
    for (const auto& r : h.roster) rows.push_back(&r);
    std::stable_sort(rows.begin(), rows.end(), [](auto* x, auto* y2) { return x->host && !y2->host; });
    for (const auto* r : rows) {
        if (y > b.y - 50 * s) break;
        const std::string name = r->name + (r->self ? "  (you)" : "");
        c.dl->AddText(c.font, 19 * s, ImVec2(a.x + 18 * s, y), r->self ? kMenuSelText : kMenuWhite, name.c_str());
        const char* tag = r->host ? "HOST" : r->ready ? "READY" : "waiting";
        const ImVec2 ts = c.font->CalcTextSizeA(17 * s, FLT_MAX, 0, tag);
        c.dl->AddText(c.font, 17 * s, ImVec2(b.x - 18 * s - ts.x, y + 1 * s), r->host ? kMenuGold : r->ready ? kMenuGreen : kMenuGrey, tag);
        y += 27 * s;
    }
    if (h.botSlots > 0) {
        const std::string bots = "+ " + std::to_string(h.botSlots) + " bots when the match starts";
        c.dl->AddText(c.font, 17 * s, ImVec2(a.x + 18 * s, b.y - 34 * s), kMenuGrey, bots.c_str());
    }
}

void DrawResultsCard(const Ctx& c, const royale::HudState& h, ImVec2 a, ImVec2 b) {
    OotPanelBox(c.dl, a, b, c.s, 230);
    const float s = c.s;
    float y = a.y + 16 * s;
    const float cols[4] = { a.x + 18 * s, b.x - 200 * s, b.x - 130 * s, b.x - 60 * s };
    const char* heads[4] = { "Player", "Kills", "Dmg", "Pts" };
    for (int i = 0; i < 4; i++) c.dl->AddText(c.font, 17 * s, ImVec2(cols[i], y), kMenuGold, heads[i]);
    y += 28 * s;
    int rank = 1;
    for (const auto& r : h.results) {
        if (y > b.y - 30 * s) break;
        const ImU32 col = r.self ? kMenuSelText : r.placement == 1 ? kMenuGold : kMenuWhite;
        char dmg[16];
        std::snprintf(dmg, sizeof(dmg), "%.1f", r.damage);
        c.dl->AddText(c.font, 18 * s, ImVec2(cols[0], y), col, (std::to_string(rank++) + ". " + r.name).c_str());
        c.dl->AddText(c.font, 18 * s, ImVec2(cols[1], y), col, std::to_string(r.kills).c_str());
        c.dl->AddText(c.font, 18 * s, ImVec2(cols[2], y), col, dmg);
        c.dl->AddText(c.font, 18 * s, ImVec2(cols[3], y), col, std::to_string(r.score).c_str());
        y += 25 * s;
    }
}

// ---- the pages ------------------------------------------------------------------------------------------------------------------

void ApplyHostSettings(const UiState& ui) {
    gSession.SelectMap(ui.mapId);
    gSession.SetMajorBoss(ui.majorBoss);
    gSession.SetPlayerLimit(ui.playerLimit);
    gSession.SetBotDifficulty(static_cast<royale::BotDifficulty>(ui.botDifficulty));
    gSession.SetAutoStart(ui.autoStart ? royale::kLobbyAutoStartSec : 0.0f);
    gSession.SetWeatherOptions({ static_cast<uint8_t>(ui.weatherSeason), static_cast<uint8_t>(ui.weatherIntensity), static_cast<uint8_t>(ui.weatherChange) });
}

const char* PageTitle(Page p) {
    switch (p) {
        case Page::Main: return "MAIN MENU";
        case Page::Play: return "PLAY";
        case Page::Setup: return "MATCH SETTINGS";
        case Page::Join: return "JOIN A MATCH";
        case Page::Maps: return "CHOOSE THE MAP";
        case Page::Locker: return "LOCKER";
        case Page::Options: return "OPTIONS";
        case Page::Controls: return "CONTROLS";
        case Page::Lobby: return "LOBBY";
        case Page::Invite: return "INVITE FRIENDS";
        case Page::Match: return "MATCH";
        case Page::Results: return "MATCH OVER";
        case Page::Keyboard: return "ENTER TEXT";
        case Page::ConfirmLeave: return "LEAVE?";
    }
    return "";
}

void PageMain(Ctx& c) {
    if (Button(c, "Play", royale::ItemId::MasterSword)) Push(Page::Play);
    Note(c, "Solo against bots, host a match for friends, or join one.");
    if (Button(c, "Locker", royale::ItemId::KokiriTunic)) Push(Page::Locker);
    if (Button(c, "Options", royale::ItemId::OcarinaOfTime)) Push(Page::Options);
    if (Button(c, "Controls", royale::ItemId::HylianShield)) Push(Page::Controls);
    if (InGame()) {
        if (Button(c, "Walk around the Temple", royale::ItemId::KokiriBoots)) MenuClose();
    } else {
        if (Button(c, "Back to the title", kNoIcon)) MenuClose();
    }
}

void PagePlay(Ctx& c) {
    UiState& ui = Ui();
    if (Button(c, "Play solo", royale::ItemId::KokiriSword)) { gMenu.setupIsSolo = true; Push(Page::Setup); }
    Note(c, "You against bots. Pick the map and the rules, then go.");
    if (Button(c, "Host a match", royale::ItemId::HylianShield)) { gMenu.setupIsSolo = false; Push(Page::Setup); }
    Note(c, "Open a lobby that friends join. Bots fill the empty spots.");
    if (Button(c, "Join a match", royale::ItemId::Hookshot)) Push(Page::Join);
    Note(c, ui.address[0] ? std::string("Last address: ") + ui.address : std::string("Join a friend's lobby with the address they give you."));
}

void PageSetup(Ctx& c, bool inLobby) {
    UiState& ui = Ui();
    const royale::HudState h = gSession.Hud();
    bool changed = false;
    if (!inLobby) {
        if (Button(c, gMenu.setupIsSolo ? "Start the match" : "Open the lobby", royale::ItemId::MasterSword)) {
            ApplyHostSettings(ui);
            Saved();
            Choose(gMenu.setupIsSolo ? Launch::Solo : Launch::Host);
            return;
        }
    }
    static const char* kNames[royale::kMapCount] = {};
    for (int i = 0; i < royale::kMapCount; i++) kNames[i] = royale::kMaps[i].name;
    {   // the map: left and right step through them, A (or a touch in the middle) opens the chooser with the pictures
        RowResult r = BeginRow(c, RowHeight(c));
        RowLabel(c, r, "Map", kNoIcon);
        RowValue(c, r, kNames[royale::ClampMap(ui.mapId)], kMenuCream, true);
        int dir = r.nudge;
        if (c.tap && r.activated) {
            const float rel = (c.mouse.x - r.a.x) / std::max(1.0f, r.b.x - r.a.x);
            dir = rel > 0.85f ? 1 : rel > 0.5f && rel < 0.62f ? -1 : 0;
        }
        if (dir) { ui.mapId = (ui.mapId + dir + royale::kMapCount) % royale::kMapCount; changed = true; MenuSfx(NA_SE_SY_CURSOR); }
        else if (r.activated) { Push(Page::Maps); Top().cursor = ui.mapId; gMenu.kbRow = ui.mapId; return; }
    }
    int lo = inLobby ? std::max(royale::kMinPlayers, h.humanCount) : royale::kMinPlayers;
    changed |= Slider(c, "Players (bots fill the rest)", &ui.playerLimit, lo, royale::kMaxPlayers, 1);
    static const char* kLevels[] = { "Easy", "Normal", "Hard" };
    changed |= Choice(c, "Bot difficulty", &ui.botDifficulty, kLevels, 3);
    changed |= Toggle(c, "The map's major boss halfway through", &ui.majorBoss);
    static const char* kSeasons[] = { "Spring", "Summer", "Autumn", "Winter", "Random" };
    changed |= Choice(c, "Season", &ui.weatherSeason, kSeasons, 5);
    changed |= Slider(c, "Weather strength", &ui.weatherIntensity, 0, 100, 10, "%");
    changed |= Slider(c, "Weather changes", &ui.weatherChange, 0, 100, 10, "%");
    if (!gMenu.setupIsSolo || inLobby) changed |= Toggle(c, "Start by itself after 2 minutes", &ui.autoStart);
    if (!inLobby && !gMenu.setupIsSolo) {
        char port[16];
        std::snprintf(port, sizeof(port), "%d", ui.port);
        if (Button(c, "Port", kNoIcon, port)) {
            static char buf[8];
            std::snprintf(buf, sizeof(buf), "%d", ui.port);
            OpenKeyboard("Port (friends use the same number)", buf, sizeof(buf), true, [](const std::string& t) { Ui().port = std::clamp(std::atoi(t.c_str()), 1024, 65535); Saved(); });
        }
    }
    Note(c, "Smaller matches get fewer towns and mini bosses. Each place has its own weather; fog and sand hide you from bots, rain puts out fire.");
    if (changed) {
        ui.mapId = royale::ClampMap(ui.mapId);
        if (inLobby || gSession.Joined()) ApplyHostSettings(ui);
        Saved();
    }
}

void PageMaps(Ctx& c) {
    UiState& ui = Ui();
    for (int i = 0; i < royale::kMapCount; i++) {
        RowResult r = BeginRow(c, RowHeight(c));
        RowLabel(c, r, royale::kMaps[i].name, kNoIcon, i == ui.mapId ? kMenuGold : 0);
        if (r.visible && i == ui.mapId) RowValue(c, r, "chosen", kMenuGreen, false);
        if (r.selected) gMenu.kbRow = i;   // (the card on the right shows the map under the cursor)
        if (r.activated) {
            ui.mapId = i;
            if (gSession.Joined()) gSession.SelectMap(i);
            Saved();
            Pop();
            return;
        }
    }
    Note(c, "Pictures are taken in the game: the first time you are on a map, from the sky as you skydive in, or all at once with Take map pictures in Options.");
}

void PageJoin(Ctx& c) {
    UiState& ui = Ui();
    if (Button(c, "Address", kNoIcon, ui.address[0] ? ui.address : "(none)")) {
        OpenKeyboard("Host address (like 192.168.1.23)", ui.address, sizeof(ui.address), false, [](const std::string&) { Saved(); });
    }
    char port[16];
    std::snprintf(port, sizeof(port), "%d", ui.port);
    if (Button(c, "Port", kNoIcon, port)) {
        static char buf[8];
        std::snprintf(buf, sizeof(buf), "%d", ui.port);
        OpenKeyboard("Port", buf, sizeof(buf), true, [](const std::string& t) { Ui().port = std::clamp(std::atoi(t.c_str()), 1024, 65535); Saved(); });
    }
    if (Button(c, "Join", royale::ItemId::Hookshot, nullptr, ui.address[0] != '\0')) Choose(Launch::Join);
    Note(c, "Same Wi-Fi: use the address on the host's Invite screen. Over the internet: a VPN such as Tailscale, or forward UDP port " +
            std::to_string(royale::net::kDefaultPort) + ". Everyone must run the same version (" + ROYALE_BUILD_VERSION + ").");
}

void PageLocker(Ctx& c) {
    UiState& ui = Ui();
    if (Button(c, "Name", kNoIcon, CleanName(ui.name))) OpenKeyboard("Your name", ui.name, sizeof(ui.name), false, [](const std::string&) { Saved(); });
    static std::vector<const char*> names;
    if (names.empty()) { for (int i = 0; i < royale::kSkinCount; i++) names.push_back(royale::kSkins[i].name); names.push_back("Custom colour"); }
    bool changed = Choice(c, "Tunic", &ui.skin, names.data(), static_cast<int>(names.size()), royale::ItemId::KokiriTunic);
    if (ui.skin == royale::kCustomSkin) {
        int rgb[3] = { static_cast<int>(ui.customTunic[0] * 255 + 0.5f), static_cast<int>(ui.customTunic[1] * 255 + 0.5f), static_cast<int>(ui.customTunic[2] * 255 + 0.5f) };
        const char* labels[3] = { "Red", "Green", "Blue" };
        for (int i = 0; i < 3; i++) if (Slider(c, labels[i], &rgb[i], 0, 255, 15)) { ui.customTunic[i] = rgb[i] / 255.0f; changed = true; }
    }
    if (changed) {
        gLocalTunic = SelectedTunic(ui);
        gSession.SetTunic(gLocalTunic);
        Saved();
    }
    Note(c, gSession.Joined() ? "Other players see a new colour from the next lobby you join." : "Everyone sees your tunic in this colour.");
}

void OptionToggle(Ctx& c, const char* key, const char* label, bool fallback) {
    bool on = MapOption(key, fallback);
    if (Toggle(c, label, &on)) {
        char k[64];
        std::snprintf(k, sizeof(k), CVAR_SETTING("Royale.%s"), key);
        CVarSetInteger(k, on ? 1 : 0);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
}

void PageOptions(Ctx& c) {
    UiState& ui = Ui();
    Header(c, "GRAPHICS");
    if (Slider(c, "Weather effects", &ui.weatherDensity, 0, 200, 10, "%")) { gWeatherDensity = ui.weatherDensity / 100.0f; Saved(); }
    if (Slider(c, "Grass and trees", &ui.foliage, 0, 200, 10, "%")) { gFoliage = ui.foliage / 100.0f; Saved(); }
    if (Toggle(c, "Cloth physics (caps and gliders)", &ui.clothOn)) { gClothScale = ui.clothOn ? ui.clothPhysics / 100.0f : 0.0f; Saved(); }
    if (ui.clothOn && Slider(c, "Cloth strength", &ui.clothPhysics, 0, 200, 10, "%")) { gClothScale = ui.clothPhysics / 100.0f; Saved(); }
    {
        bool custom = CustomSceneryOn();
        if (Toggle(c, "Custom rocks and buildings", &custom)) {
            CVarSetInteger(CVAR_SETTING("Royale.CustomScenery"), custom ? 1 : 0);
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
    }
    Header(c, "SOUND");
    static const char* kMusic[] = { "The game's own", "Music folder", "None" };
    if (Choice(c, "Match music", &ui.musicMode, kMusic, 3)) { gMusicMode = ui.musicMode; Saved(); }
    OptionToggle(c, "LobbyMusic", "Lobby songs from the music folder", true);
    if (Button(c, "Look for new songs")) ScanMusicFolder();
    if (gLobbyMusic.status.empty()) ScanMusicFolder();
    Note(c, gLobbyMusic.status + " (16-bit PCM .wav files)");
    OptionToggle(c, "OotInstruments", "Play them with OoT's own instruments", true);
    if (OotInstrumentsOn()) Note(c, OotStatus());
    Header(c, "MINIMAP");
    OptionToggle(c, "MapPlayers", "Other players", true);
    OptionToggle(c, "MapBots", "Bots", true);
    OptionToggle(c, "MapEnemies", "Mini bosses", true);
    OptionToggle(c, "MapChests", "Chests", true);
    Header(c, "EFFECTS");
    OptionToggle(c, "HeldGlow", "Glow on other players' weapons", true);
    OptionToggle(c, "HeldGlowSelf", "Glow on your own weapon", false);
    Header(c, "EXTRAS");
    if (Toggle(c, "Wait in the Temple of Time in lobbies", &ui.waitingRoom)) Saved();
    OptionToggle(c, "LiloCat", "Lilo the cat hides on the map", true);
    OptionToggle(c, "LiloPet", "Lilo follows me as a pet", false);
    if (Button(c, "Reload the custom dragon (replaces Volvagia)")) LoadCustomDragon();
    if (!gDragonModel.tried) LoadCustomDragon();
    Note(c, gDragonModel.status);
    Header(c, "MAP PICTURES");
    {
        bool autoPics = AutoPictures();
        if (Toggle(c, "Take map pictures by themselves", &autoPics)) {
            CVarSetInteger(CVAR_SETTING("Royale.AutoMapPictures"), autoPics ? 1 : 0);
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
        const bool canTour = InGame() && !gSession.Joined() && !gTour.on;
        if (Button(c, gTour.on ? "Taking pictures..." : "Take map pictures now", royale::ItemId::LensOfTruth, nullptr, canTour)) { StartTour(); MenuClose(); }
        Note(c, canTour ? "Visits every map and takes its picture for the map chooser, then brings you back." : "Available outside a lobby, once the game has started.");
    }
    Header(c, "MORE");
    if (Button(c, "Graphics, sound and controller settings")) {
        MenuClose();
        CVarSetString(CVAR_SETTING("Menu.ActiveHeader"), "Settings");
        if (SohGui::mSohMenu && !SohGui::mSohMenu->IsVisible()) SohGui::mSohMenu->ToggleVisibility();
    }
    Note(c, "Opens the Ship of Harkinian settings (resolution, button mapping and the like).");
}

void PageControls(Ctx& c) {
    Header(c, "FIGHTING");
    Info(c, "Attack with what is in your hand", "B");
    Info(c, "Lock on to a player (hold)", "Z");
    Info(c, "Next / previous weapon", "D-pad Right / Left");
    Header(c, "STAYING ALIVE");
    Info(c, "Drink a health potion", "D-pad Down");
    Info(c, "Drink a shield potion", "C-Left");
    Info(c, "Use your ability (song, spell, hookshot)", "D-pad Up");
    Header(c, "MOVING");
    Info(c, "Move", "Stick");
    Info(c, "Jump (jump at a ledge to climb it)", "C-Up");
    Info(c, "Sidestep", "Z + move");
    Info(c, "Dive faster while skydiving", "Z");
    Info(c, "Open a chest, take an item, hire, talk", "A");
    Info(c, "Emote", "C-Right");
    Header(c, "MENUS");
    Info(c, "This menu", "Start");
    Info(c, "Choose / back", "A / B");
    Info(c, "Watch the next player when out", "D-pad Left / Right");
}

void PageLobby(Ctx& c) {
    const royale::HudState h = gSession.Hud();
    if (!h.connected) {
        Line(c, "Connecting to the host...");
        if (Button(c, "Cancel", kNoIcon)) gSession.Leave();
        return;
    }
    if (h.isHost) {
        if (Button(c, gPendingStart ? "Preparing..." : "Start the match", royale::ItemId::MasterSword, nullptr, InGame() && !gPendingStart)) { gPendingStart = true; MenuClose(); }
        int others = 0, readyOthers = 0;
        for (const auto& r : h.roster) if (!r.host) { others++; readyOthers += r.ready; }
        Note(c, others == 0 ? "Nobody else has joined yet. Start now to play against bots." : std::to_string(readyOthers) + " of " + std::to_string(others) + " players ready. Starting anyway is allowed.");
        if (Button(c, "Match settings", royale::ItemId::LensOfTruth)) { gMenu.setupIsSolo = false; Push(Page::Setup); }
        if (Button(c, "Invite friends", royale::ItemId::FairyOcarina)) { RefreshLocalAddresses(Ui(), true); Push(Page::Invite); }
    } else {
        bool ready = h.selfReady;
        if (Toggle(c, "I'm ready", &ready, royale::ItemId::MasterSword)) gSession.SetReady(ready);
        Note(c, "Waiting for the host to start the match.");
    }
    if (h.lobbyLeft >= 0) Line(c, "Starts by itself in", ClockText(h.lobbyLeft).c_str(), kMenuGold);
    if (InGame()) {
        if (!InWaitingRoom() && Button(c, "Go to the waiting room", royale::ItemId::SongOfTime)) { WantsWaitingRoom = true; MenuClose(); }
        if (!InField() && Button(c, (std::string("Look around ") + CurrentMap().name).c_str(), royale::ItemId::KokiriBoots)) { WantsWaitingRoom = false; GoToField(); MenuClose(); }
    }
    if (Button(c, "Locker", royale::ItemId::KokiriTunic)) Push(Page::Locker);
    if (Button(c, "Options", royale::ItemId::OcarinaOfTime)) Push(Page::Options);
    if (Button(c, "Controls", royale::ItemId::HylianShield)) Push(Page::Controls);
    if (Button(c, "Leave the lobby", kNoIcon)) Push(Page::ConfirmLeave);
}

void PageInvite(Ctx& c) {
    const royale::HudState h = gSession.Hud();
    UiState& ui = Ui();
    RefreshLocalAddresses(ui, false);
    if (ui.localAddresses.empty()) Line(c, "No network address found. Connect to Wi-Fi, or check your VPN.", nullptr, kMenuRed);
    for (size_t i = 0; i < ui.localAddresses.size() && i < 3; i++) {
        if (Button(c, ui.localAddresses[i].c_str(), kNoIcon, "copy")) {
            ImGui::SetClipboardText(ui.localAddresses[i].c_str());
            ShowNote("Copied " + ui.localAddresses[i], kMenuGreen, 2.5f);
        }
    }
    Line(c, "Port", std::to_string(h.hostPort).c_str());
    Note(c, "Friends choose Play, then Join a match, and type the address above (the port is " + std::to_string(h.hostPort) + " unless you changed it).");
}

void PageMatch(Ctx& c) {
    const royale::HudState h = gSession.Hud();
    if (h.state == royale::MatchState::Countdown) Line(c, "The match starts in", std::to_string(static_cast<int>(std::ceil(h.countdownLeft))).c_str(), kMenuGold);
    else Line(c, "Players left", (std::to_string(h.alive) + " / " + std::to_string(h.playerLimit)).c_str(), kMenuGold);
    if (Button(c, "Back to the match", royale::ItemId::MasterSword)) MenuClose();
    if (InField() && Button(c, "Item screen", royale::ItemId::FairyBow)) { MenuClose(); gMenu.swallow = 0; gMenu.passStart = true; }
    if (Button(c, "Options", royale::ItemId::OcarinaOfTime)) Push(Page::Options);
    if (Button(c, "Controls", royale::ItemId::HylianShield)) Push(Page::Controls);
    if (Button(c, "Leave the match", kNoIcon)) Push(Page::ConfirmLeave);
    Note(c, "The match keeps going while this menu is open.");
    if (!gPickupLog.empty()) {
        Header(c, "RECENT PICKUPS");
        for (const auto& n : gPickupLog) Line(c, n.text.c_str(), nullptr, RarityU32(n.rarity));
    }
}

void PageResults(Ctx& c) {
    const royale::HudState h = gSession.Hud();
    if (h.winnerId == h.selfId && h.winnerId != royale::net::kNoPlayer16) Line(c, "VICTORY ROYALE! You won!", nullptr, kMenuGold);
    else if (!h.winnerName.empty()) Line(c, "Winner", h.winnerName.c_str(), kMenuGold);
    else Line(c, "Nobody survived.");
    if (h.isHost) {
        if (Button(c, "Play again", royale::ItemId::MasterSword)) { gSession.RequestPlayAgain(); MenuClose(); }
        Note(c, "Starts a new match right away with everyone still connected.");
    } else {
        Note(c, "Waiting for the host to play again.");
    }
    if (Button(c, "Back to the main menu", kNoIcon)) gSession.Leave();
    Note(c, "Points: " + std::to_string(royale::kPointsPerHeartOfDamage) + " per heart of damage, " + std::to_string(royale::kPointsPerKill) + " per kill, " +
            std::to_string(royale::kPointsPerChest) + " per chest, more for lasting longer, " + std::to_string(royale::kPointsForWinning) + " for winning.");
}

void PageConfirmLeave(Ctx& c) {
    if (Button(c, "No, stay", kNoIcon)) { Pop(); return; }
    if (Button(c, "Yes, leave", kNoIcon)) { gSession.Leave(); gPendingStart = false; MenuOpen(Page::Main); }
}

// The on-screen keyboard, laid out like the game's name entry: a grid of letters, then Space, Back and End.
void PageKeyboard(Ctx& c) {
    static const char* kRows[] = { "ABCDEFGHIJKLM", "NOPQRSTUVWXYZ", "abcdefghijklm", "nopqrstuvwxyz", "0123456789.-:" };
    static const char* kDigitRows[] = { "12345", "67890" };
    const char* const* rows = gMenu.kbDigitsOnly ? kDigitRows : kRows;
    const int rowCount = gMenu.kbDigitsOnly ? 2 : 5;
    const int cols = gMenu.kbDigitsOnly ? 5 : 13;
    const int lastRow = rowCount;   // the Space / Back / End row
    std::string& text = gMenu.kbText;

    // typing on a real keyboard works too (and then the same frame's buttons are ignored: keys can also be mapped to the controller)
    ImGuiIO& io = ImGui::GetIO();
    if (io.InputQueueCharacters.Size > 0) gMenu.a = gMenu.b = gMenu.up = gMenu.down = gMenu.left = gMenu.right = false;
    for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
        const ImWchar ch = io.InputQueueCharacters[i];
        if (ch >= 32 && ch < 127 && text.size() + 1 < gMenu.kbSize && (!gMenu.kbDigitsOnly || (ch >= '0' && ch <= '9'))) text.push_back(static_cast<char>(ch));
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !text.empty()) text.pop_back();
    bool done = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) || gMenu.start;

    // the text so far, in the name-entry box
    const float s = c.s;
    const ImVec2 ba(c.x0, c.y), bb(c.x1, c.y + 56 * s);
    c.dl->AddRectFilled(ba, bb, IM_COL32(40, 60, 120, 200), 8 * s);
    c.dl->AddRect(ba, bb, kMenuGold, 8 * s, 0, 2 * s);
    const std::string shown = text + ((static_cast<int>(c.t * 2.5f) & 1) ? "_" : " ");
    Lettered(c.dl, c.font, 30 * s, ImVec2(ba.x + 16 * s, ba.y + 12 * s), kMenuWhite, shown.c_str());
    c.y += 70 * s;

    // the grid
    if (gMenu.up) gMenu.kbRow = (gMenu.kbRow + lastRow) % (lastRow + 1);
    if (gMenu.down) gMenu.kbRow = (gMenu.kbRow + 1) % (lastRow + 1);
    const int rowLen = gMenu.kbRow < rowCount ? static_cast<int>(std::strlen(rows[gMenu.kbRow])) : 3;
    if (gMenu.left) gMenu.kbCol = (gMenu.kbCol + rowLen - 1) % rowLen;
    if (gMenu.right) gMenu.kbCol = (gMenu.kbCol + 1) % rowLen;
    gMenu.kbCol = std::min(gMenu.kbCol, rowLen - 1);
    if (gMenu.up || gMenu.down || gMenu.left || gMenu.right) MenuSfx(NA_SE_SY_FSEL_CURSOR);

    const float cell = std::min(44 * s, (c.x1 - c.x0) / cols);
    auto press = [&](int row, int col) {
        if (row < rowCount) {
            if (text.size() + 1 < gMenu.kbSize) { text.push_back(rows[row][col]); MenuSfx(NA_SE_SY_FSEL_DECIDE_L); }
            else MenuSfx(NA_SE_SY_ERROR);
        } else if (col == 0) {
            if (!gMenu.kbDigitsOnly && text.size() + 1 < gMenu.kbSize) text.push_back(' ');
        } else if (col == 1) {
            if (!text.empty()) text.pop_back();
            MenuSfx(NA_SE_SY_CANCEL);
        } else {
            done = true;
        }
    };
    for (int r = 0; r <= lastRow; r++) {
        const int n = r < rowCount ? static_cast<int>(std::strlen(rows[r])) : 3;
        for (int k = 0; k < n; k++) {
            ImVec2 a, b;
            const char* label;
            char one[2] = { 0, 0 };
            if (r < rowCount) {
                a = ImVec2(c.x0 + k * cell, c.y + r * cell);
                b = ImVec2(a.x + cell - 4 * s, a.y + cell - 4 * s);
                one[0] = rows[r][k];
                label = one;
            } else {
                const float w = (c.x1 - c.x0) / 3.0f;
                a = ImVec2(c.x0 + k * w, c.y + r * cell + 8 * s);
                b = ImVec2(a.x + w - 8 * s, a.y + cell);
                label = k == 0 ? "SPACE" : k == 1 ? "BACK" : "END";
            }
            const bool sel = gMenu.kbRow == r && gMenu.kbCol == k;
            c.dl->AddRectFilled(a, b, sel ? IM_COL32(120, 160, 255, 150) : IM_COL32(255, 255, 255, 22), 5 * s);
            const float size = (r < rowCount ? 24 : 19) * s;
            const ImVec2 ls = c.font->CalcTextSizeA(size, FLT_MAX, 0, label);
            Lettered(c.dl, c.font, size, ImVec2((a.x + b.x - ls.x) * 0.5f, (a.y + b.y - ls.y) * 0.5f), sel ? kMenuSelText : kMenuWhite, label);
            if (sel) OotCursor(c.dl, a, b, s, c.t);
            if (c.tap && c.mouse.x >= a.x && c.mouse.x <= b.x && c.mouse.y >= a.y && c.mouse.y <= b.y) { gMenu.kbRow = r; gMenu.kbCol = k; press(r, k); }
        }
    }
    if (gMenu.a) press(gMenu.kbRow, gMenu.kbCol);
    c.y += (lastRow + 1) * cell + 16 * s;
    Note(c, "A types, B deletes, Start or END finishes.");

    if (gMenu.b) {   // B deletes; B on an empty box leaves without changing anything
        gMenu.b = false;
        if (!text.empty()) { text.pop_back(); MenuSfx(NA_SE_SY_CANCEL); }
        else { Pop(); return; }
    }
    if (done) {
        if (gMenu.kbBuf) std::snprintf(gMenu.kbBuf, gMenu.kbSize, "%s", text.c_str());
        auto cb = gMenu.kbDone;
        Pop();
        if (cb) cb(text);
    }
}

// ---- the frame --------------------------------------------------------------------------------------------------------------

void DrawMenu() {
    // things that show without the menu: the note about save slots on the file select, the picture tour
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float s = std::clamp(ds.y / 720.0f, 0.8f, 2.2f);
    const double now = ImGui::GetTime();

    if (gTour.on) {
        const std::string t = "Taking map pictures: " + std::to_string(std::min(gTour.map + 1, royale::kMapCount)) + " of " + std::to_string(royale::kMapCount);
        const ImVec2 sz = font->CalcTextSizeA(24 * s, FLT_MAX, 0, t.c_str());
        Lettered(dl, font, 24 * s, ImVec2((ds.x - sz.x) * 0.5f, ds.y * 0.08f), kMenuGold, t.c_str());
    }
    if (!gMenu.open) {
        if (now < gMenu.noteUntil) {
            const ImVec2 sz = font->CalcTextSizeA(22 * s, FLT_MAX, ds.x * 0.8f, gMenu.note.c_str());
            Lettered(dl, font, 22 * s, ImVec2((ds.x - sz.x) * 0.5f, ds.y * 0.9f - sz.y), gMenu.noteColour, gMenu.note.c_str());
        }
        gMenu.up = gMenu.down = gMenu.left = gMenu.right = gMenu.a = gMenu.b = gMenu.start = false;
        return;
    }
    if (gMenu.stack.empty()) gMenu.stack.push_back({ HomePage() });

    Ctx c;
    c.dl = dl;
    c.font = font;
    c.ds = ds;
    c.s = s;
    c.t = static_cast<float>(now - gMenu.openedAt);
    const float fade = std::clamp(c.t / 0.18f, 0.0f, 1.0f);
    ImGuiIO& io = ImGui::GetIO();
    c.mouse = io.MousePos;
    c.tap = ImGui::IsMouseClicked(0) && !(SohGui::mSohMenu && SohGui::mSohMenu->IsVisible());

    // The world behind dims (lighter on the title screen, so its flight over Hyrule Field shows through).
    const bool title = OnTitleScreen();
    const int dim = title ? 70 : 140;
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, static_cast<int>(dim * fade)), IM_COL32(0, 0, 0, static_cast<int>(dim * 0.4f * fade)),
                                IM_COL32(0, 0, 0, static_cast<int>(dim * 0.4f * fade)), IM_COL32(0, 0, 0, static_cast<int>(dim * fade)));

    const Page page = Top().page;
    const bool wide = ds.x / ds.y > 1.45f;

    // Top left: the logo, and the page's name in gold with a diamond rule (as on the match splash).
    float top = 26 * s;
    const float left = std::max(30 * s, ds.x * 0.05f);
    if (page == Page::Main) {
        ImVec2 lsz;
        if (LogoTexture(&lsz) != nullptr) top += DrawLogo(dl, left + 130 * s, top, 260 * s, static_cast<int>(255 * fade), true) + 18 * s;
    }
    {
        const char* name = PageTitle(page);
        const float size = 38 * s;
        Lettered(dl, font, size, ImVec2(left, top), Alpha(kMenuGold, fade), name, 2.5f * s);
        const ImVec2 nsz = font->CalcTextSizeA(size, FLT_MAX, 0, name);
        const float ry = top + nsz.y + 8 * s;
        dl->AddLine(ImVec2(left, ry), ImVec2(left + std::max(nsz.x, 220 * s), ry), Alpha(kMenuGoldDark, fade), 2 * s);
        const float dx = left + std::max(nsz.x, 220 * s) + 8 * s;
        dl->AddQuadFilled(ImVec2(dx, ry - 6 * s), ImVec2(dx + 6 * s, ry), ImVec2(dx, ry + 6 * s), ImVec2(dx - 6 * s, ry), Alpha(kMenuGold, fade));
        top = ry + 20 * s;
    }

    // The panel with the rows.
    const float panelW = std::min(wide ? ds.x * 0.46f : ds.x - 2 * left, 640 * s);
    const ImVec2 pa(left, top), pb(left + panelW, ds.y - 86 * s);
    OotPanelBox(dl, pa, pb, s, static_cast<int>(215 * fade));
    c.x0 = pa.x + 18 * s;
    c.x1 = pb.x - 18 * s;
    c.clipTop = pa.y + 14 * s;
    c.clipBottom = pb.y - 14 * s;
    c.cursor = Top().cursor;
    c.y = c.clipTop + 4 * s - Top().scroll;

    // Touch: a tap on the dimmed area outside the panels is "back".
    gMenu.hits.clear();
    dl->PushClipRect(ImVec2(pa.x, c.clipTop), ImVec2(pb.x, c.clipBottom), true);
    const size_t depth = gMenu.stack.size();
    if (page == Page::Keyboard) {
        // the keyboard reads the stick and buttons itself
        PageKeyboard(c);
    } else {
        switch (page) {
            case Page::Main: PageMain(c); break;
            case Page::Play: PagePlay(c); break;
            case Page::Setup: PageSetup(c, gSession.Joined()); break;
            case Page::Join: PageJoin(c); break;
            case Page::Maps: PageMaps(c); break;
            case Page::Locker: PageLocker(c); break;
            case Page::Options: PageOptions(c); break;
            case Page::Controls: PageControls(c); break;
            case Page::Lobby: PageLobby(c); break;
            case Page::Invite: PageInvite(c); break;
            case Page::Match: PageMatch(c); break;
            case Page::Results: PageResults(c); break;
            case Page::ConfirmLeave: PageConfirmLeave(c); break;
            case Page::Keyboard: break;
        }
    }
    dl->PopClipRect();

    // The page may have changed (a row opened another page or closed the menu): the rest is only for the page that was drawn.
    const bool samePage = gMenu.open && gMenu.stack.size() == depth && Top().page == page;
    if (samePage && page != Page::Keyboard) {
        PageState& ps = Top();
        if (c.tapped >= 0) ps.cursor = c.tapped;
        const int count = c.index;
        if (count > 0) {
            if (gMenu.up) { ps.cursor = (ps.cursor + count - 1) % count; MenuSfx(NA_SE_SY_CURSOR); }
            if (gMenu.down) { ps.cursor = (ps.cursor + 1) % count; MenuSfx(NA_SE_SY_CURSOR); }
            ps.cursor = std::clamp(ps.cursor, 0, count - 1);
            // keep the chosen row in view
            const float viewH = c.clipBottom - c.clipTop;
            const float rowTop = gMenu.selTop + ps.scroll - c.clipTop, rowBottom = gMenu.selBottom + ps.scroll - c.clipTop;
            if (rowTop < ps.scroll + 10 * s) ps.scroll = std::max(0.0f, rowTop - 10 * s);
            if (rowBottom > ps.scroll + viewH - 40 * s) ps.scroll = rowBottom - viewH + 40 * s;
            const float contentH = c.y + ps.scroll - c.clipTop;
            ps.scroll = std::clamp(ps.scroll, 0.0f, std::max(0.0f, contentH - viewH + 20 * s));
            if (ps.cursor == 0) ps.scroll = 0.0f;
        }
        if (gMenu.b) Pop();
        else if (gMenu.start && gMenu.stack.size() >= 1 && !title) MenuClose();
    }

    // The card on the right: what this page is about.
    if (gMenu.open && wide && !gMenu.stack.empty()) {
        const ImVec2 ca(pb.x + 30 * s, top), cb(std::min(ds.x - left, pb.x + 30 * s + 560 * s), pb.y);
        const royale::HudState h = gSession.Hud();
        const Page p = Top().page;
        if (p == Page::Setup || (p == Page::Lobby && h.connected) || p == Page::Maps) {
            const int mapId = p == Page::Maps ? gMenu.kbRow : (gSession.Joined() ? h.mapId : Ui().mapId);
            if (p == Page::Lobby) {
                const float split = ca.y + (cb.y - ca.y) * 0.58f;
                DrawMapCard(c, mapId, ca, ImVec2(cb.x, split - 10 * s));
                DrawRosterCard(c, h, ImVec2(ca.x, split + 6 * s), cb);
            } else {
                DrawMapCard(c, mapId, ca, cb, PreviewFor(mapId).have ? (PreviewFor(mapId).aerial ? "Picture taken from the sky in your game." : "Picture taken in your game.") : nullptr);
            }
        } else if (p == Page::Results) {
            DrawResultsCard(c, h, ca, cb);
        } else if (p == Page::Locker) {
            OotPanelBox(dl, ca, ImVec2(cb.x, ca.y + 260 * s), s, 230);
            const uint32_t rgb = SelectedTunic(Ui());
            const ImVec2 mid((ca.x + cb.x) * 0.5f, ca.y + 120 * s);
            dl->AddCircleFilled(mid, 78 * s, IM_COL32(royale::RgbR(rgb), royale::RgbG(rgb), royale::RgbB(rgb), 255));
            dl->AddCircle(mid, 78 * s, kMenuGold, 0, 3 * s);
            DrawItemIcon(dl, royale::ItemId::KokiriTunic, mid, 70 * s, kMenuGold);
            const char* nm = CleanName(Ui().name);
            const ImVec2 ns = font->CalcTextSizeA(26 * s, FLT_MAX, 0, nm);
            Lettered(dl, font, 26 * s, ImVec2(mid.x - ns.x * 0.5f, ca.y + 212 * s), kMenuWhite, nm);
        }
    }

    // A message (an error, "copied"), over the bottom of the panel.
    if (now < gMenu.noteUntil) {
        const ImVec2 sz = font->CalcTextSizeA(20 * s, FLT_MAX, panelW - 40 * s, gMenu.note.c_str());
        const ImVec2 na(pa.x + 10 * s, pb.y - sz.y - 30 * s);
        dl->AddRectFilled(na, ImVec2(pb.x - 10 * s, pb.y - 10 * s), IM_COL32(20, 8, 4, 235), 8 * s);
        dl->AddText(font, 20 * s, ImVec2(na.x + 10 * s, na.y + 10 * s), gMenu.noteColour, gMenu.note.c_str(), nullptr, panelW - 40 * s);
    }

    // Bottom: the button prompts, in the game's own words. (Tapping "Return" is back, for touch screens.)
    {
        const float y = ds.y - 44 * s;
        float x = left;
        const float aEnd = ButtonPrompt(dl, font, x, y, s, "A", IM_COL32(60, 110, 255, 255), dgDecideDoActionENGTex, "Decide");
        const float bStart = aEnd;
        const float bEnd = ButtonPrompt(dl, font, bStart, y, s, "B", IM_COL32(40, 170, 70, 255), dgReturnDoActionENGTex, "Return");
        if (c.tap && c.mouse.y > y - 24 * s && c.mouse.x > bStart && c.mouse.x < bEnd && !gMenu.stack.empty()) Pop();
        const char* ver = "Version " ROYALE_BUILD_VERSION;
        const ImVec2 vs = font->CalcTextSizeA(16 * s, FLT_MAX, 0, ver);
        dl->AddText(font, 16 * s, ImVec2(ds.x - vs.x - 18 * s, y - vs.y * 0.5f), Alpha(kMenuCream, 0.7f), ver);
    }

    // Mouse: hovering a row moves the cursor to it (only when the mouse actually moves, so it doesn't fight the D-pad).
    gMenu.mouseMoved = io.MousePos.x != gMenu.lastMouse.x || io.MousePos.y != gMenu.lastMouse.y;
    gMenu.lastMouse = io.MousePos;
    if (samePage && gMenu.open && gMenu.mouseMoved && !gMenu.stack.empty() && Top().page == page && page != Page::Keyboard) {
        for (const auto& hit : gMenu.hits) {
            if (io.MousePos.x >= hit.a.x && io.MousePos.x <= hit.b.x && io.MousePos.y >= hit.a.y && io.MousePos.y <= hit.b.y) Top().cursor = hit.index;
        }
    }
    gMenu.up = gMenu.down = gMenu.left = gMenu.right = gMenu.a = gMenu.b = gMenu.start = false;
}

class RoyaleMenuWindow : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void InitElement() override {}
    void UpdateElement() override {}
    void DrawElement() override {}
    void Draw() override { DrawMenu(); }
};

void EnsureMenuWindow() {
    static bool done = false;
    if (done) return;
    done = true;
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    gui->AddGuiWindow(std::make_shared<RoyaleMenuWindow>(CVAR_WINDOW("RoyaleMenu"), true, "Royale Menu", ImVec2(0, 0)));
}

void RegisterRoyaleMenuHooks() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameStateMainStart>([]() { EnsureHudWindow(); EnsureMenuWindow(); OnMenuMainStart(); });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(OnMenuFrame);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayDrawEnd>(QueueCapture);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnLoadGame>(OnMenuLoadGame);
}

static RegisterShipInitFunc royaleMenuHooksInit(RegisterRoyaleMenuHooks);
