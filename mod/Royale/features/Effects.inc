// ---- the logo ---------------------------------------------------------------------------------------------------------------
// The picture in mod/Royale/logo_data.h (made from assets/logo.png by scripts/make_logo_assets.py) is turned into a texture the first time it is
// wanted, and drawn on the title screen, the file select screen, the match splash and the top of the menu.
// The game's own title screen (the Zelda logo that fades in when you press start) draws this picture instead of its own logo: patches/0010 calls here.
// Plain RGBA bytes, the same picture as everywhere else; returns null if there is none.
extern "C" const unsigned char* Royale_TitleLogoRGBA(int* width, int* height, int* destWidth, int* destHeight) {
    static std::vector<uint8_t> rgba;
    static bool built = false;
    if (!built) {
        built = true;
        if (royale::kLogoW > 0 && royale::kLogoH > 0 && royale::kLogoRuns > 0) {
            rgba.assign(static_cast<size_t>(royale::kLogoW) * royale::kLogoH * 4, 0);
            size_t px = 0;
            for (int r = 0; r < royale::kLogoRuns; r++) {
                const int count = royale::kLogoRle[r * 3], idx = royale::kLogoRle[r * 3 + 1], alpha = royale::kLogoRle[r * 3 + 2];
                for (int k = 0; k < count && px * 4 < rgba.size(); k++, px++) {
                    if (idx == 0) continue;
                    rgba[px * 4 + 0] = royale::kLogoPalette[(idx - 1) * 3 + 0];
                    rgba[px * 4 + 1] = royale::kLogoPalette[(idx - 1) * 3 + 1];
                    rgba[px * 4 + 2] = royale::kLogoPalette[(idx - 1) * 3 + 2];
                    rgba[px * 4 + 3] = static_cast<uint8_t>(alpha);
                }
            }
        }
    }
    if (rgba.empty()) return nullptr;
    *width = royale::kLogoW;
    *height = royale::kLogoH;
    *destHeight = 172;   // in the title's 320 x 240 picture
    *destWidth = static_cast<int>(172.0f * royale::kLogoW / royale::kLogoH + 0.5f);
    return rgba.data();
}

ImTextureID LogoTexture(ImVec2* size = nullptr) {
    static ImTextureID tex = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        std::vector<uint8_t> rgba(static_cast<size_t>(royale::kLogoW) * royale::kLogoH * 4, 0);
        size_t px = 0;
        for (int r = 0; r < royale::kLogoRuns; r++) {
            const int count = royale::kLogoRle[r * 3], idx = royale::kLogoRle[r * 3 + 1], alpha = royale::kLogoRle[r * 3 + 2];
            for (int k = 0; k < count && px * 4 < rgba.size(); k++, px++) {
                if (idx == 0) continue;
                rgba[px * 4 + 0] = royale::kLogoPalette[(idx - 1) * 3 + 0];
                rgba[px * 4 + 1] = royale::kLogoPalette[(idx - 1) * 3 + 1];
                rgba[px * 4 + 2] = royale::kLogoPalette[(idx - 1) * 3 + 2];
                rgba[px * 4 + 3] = static_cast<uint8_t>(alpha);
            }
        }
        GfxRenderingAPI* api = gfx_get_current_rendering_api();
        if (api != nullptr) {
            const int id = api->new_texture();
            api->select_texture(0, id);
            api->set_sampler_parameters(0, true, 0, 0);
            api->upload_texture(rgba.data(), royale::kLogoW, royale::kLogoH);
            tex = reinterpret_cast<ImTextureID>(static_cast<intptr_t>(id));   // (OpenGL: the texture id is the ImGui texture id)
        }
    }
    if (size) *size = ImVec2(static_cast<float>(royale::kLogoW), static_cast<float>(royale::kLogoH));
    return tex;
}

// The logo `width` wide with its top edge at `top`, centred on `cx`. Returns the height drawn (0 if there is no logo).
float DrawLogo(ImDrawList* dl, float cx, float top, float width, int alpha = 255, bool plaque = false) {
    ImVec2 sz;
    ImTextureID tex = LogoTexture(&sz);
    if (tex == nullptr || sz.x <= 0) return 0.0f;
    const float height = width * sz.y / sz.x;
    if (plaque) {   // the logo has black lettering: on a dark screen it sits on a pale rounded plaque
        const float pad = width * 0.04f;
        dl->AddRectFilled(ImVec2(cx - width * 0.5f - pad, top - pad), ImVec2(cx + width * 0.5f + pad, top + height + pad), IM_COL32(246, 241, 229, alpha * 235 / 255), pad * 1.6f);
    }
    dl->AddImage(tex, ImVec2(cx - width * 0.5f, top), ImVec2(cx + width * 0.5f, top + height), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, alpha));
    return height;
}

// The game's title screen and file select screen (nothing of the game is running yet): our logo takes the place of the 3D title on the first, and
// sits at the top of the second.
// Which of the game's own screens is running is read from the game state itself: its title (the Nintendo 64 logo) or its file select. The moment the
// game moves on to anything else (the game itself, the intro cutscene) the logo is gone.
void DrawTitleLogo() {
    if (InGame() || gGameState == nullptr) return;
    const int mode = gGameState->init == Title_Init ? GAMEMODE_TITLE_SCREEN : gGameState->init == FileChoose_Init ? GAMEMODE_FILE_SELECT : -1;
    if (mode < 0) return;
    ImVec2 sz;
    if (LogoTexture(&sz) == nullptr) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float aspect = sz.x / sz.y;
    static int lastMode = -1;
    static double shownAt = 0.0;
    if (mode != lastMode) { lastMode = mode; shownAt = ImGui::GetTime(); }
    if (mode == GAMEMODE_TITLE_SCREEN) {
        // nothing: the game's own title screen now draws the logo (patches/0010), fading in with its own fade
    } else {
        const float width = std::min(ds.x * 0.30f, ds.y * 0.30f * aspect);
        DrawLogo(dl, ds.x * 0.5f, ds.y * 0.025f, width, 245, true);
    }
    // the build number, small, bottom right
    const std::string version = std::string("Version ") + ROYALE_BUILD_VERSION;
    const float vs = std::clamp(ds.y / 720.0f, 0.8f, 2.2f) * 18.0f;
    const ImVec2 vsz = ImGui::GetFont()->CalcTextSizeA(vs, FLT_MAX, 0.0f, version.c_str());
    const ImU32 vcol = mode == GAMEMODE_TITLE_SCREEN ? IM_COL32(60, 50, 40, 255) : IM_COL32(255, 255, 255, 235);
    dl->AddText(ImGui::GetFont(), vs, ImVec2(ds.x - vsz.x - 14.0f, ds.y - vsz.y - 10.0f), vcol, version.c_str());
}

void DrawSplash(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, float secondsIn) {
    const float fadeIn = std::clamp(secondsIn / 0.6f, 0.0f, 1.0f);
    const float fadeOut = std::clamp((kSplashSeconds - secondsIn) / 0.6f, 0.0f, 1.0f);
    const float a = std::min(fadeIn, fadeOut);
    if (a <= 0.0f) return;
    auto A = [&](int v) { return static_cast<int>(v * a); };
    const ImU32 gold = IM_COL32(255, 214, 90, A(255)), goldDark = IM_COL32(176, 118, 24, A(255)), shadow = IM_COL32(8, 14, 10, A(235)),
                cream = IM_COL32(236, 228, 190, A(255));

    // Night sky: top and bottom of a vertical gradient, with a vignette.
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ds, IM_COL32(6, 22, 38, A(255)), IM_COL32(6, 22, 38, A(255)), IM_COL32(14, 54, 46, A(255)), IM_COL32(14, 54, 46, A(255)));
    for (int i = 0; i < 40; i++) { // a few stars, fixed positions
        const float sx = std::fmod(i * 197.0f + 41.0f, 1000.0f) / 1000.0f * ds.x, sy = std::fmod(i * 331.0f + 17.0f, 1000.0f) / 1000.0f * ds.y * 0.6f;
        dl->AddCircleFilled(ImVec2(sx, sy), (1.0f + (i % 3)) * scale * 0.8f, IM_COL32(255, 255, 230, A(60 + (i * 37) % 140)));
    }

    // Double gold border with diamond corners.
    const float m = 26 * scale;
    dl->AddRect(ImVec2(m, m), ImVec2(ds.x - m, ds.y - m), gold, 0, 0, 3.0f * scale);
    dl->AddRect(ImVec2(m + 9 * scale, m + 9 * scale), ImVec2(ds.x - m - 9 * scale, ds.y - m - 9 * scale), goldDark, 0, 0, 1.5f * scale);
    const ImVec2 corners[4] = { ImVec2(m, m), ImVec2(ds.x - m, m), ImVec2(m, ds.y - m), ImVec2(ds.x - m, ds.y - m) };
    for (const ImVec2& c : corners) {
        const float r = 9 * scale;
        dl->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), gold);
    }

    auto lettered = [&](float y, float size, ImU32 col, const char* text) {
        ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
        const float x = (ds.x - sz.x) * 0.5f;
        const float o = std::max(2.0f, size * 0.05f);
        for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) {
            if (dx || dy) dl->AddText(font, size, ImVec2(x + dx * o, y + dy * o), shadow, text);
        }
        dl->AddText(font, size, ImVec2(x + o * 1.6f, y + o * 1.6f), shadow, text);
        dl->AddText(font, size, ImVec2(x, y), col, text);
        return sz.y;
    };

    float y;
    ImVec2 logoSize;
    if (LogoTexture(&logoSize) != nullptr) {   // the logo, floating gently
        const float bob = std::sin(secondsIn * 2.0f) * 4.0f * scale;
        const float width = std::min(ds.x * 0.5f, ds.y * 0.5f * logoSize.x / logoSize.y);
        const float top = ds.y * 0.06f + bob;
        y = top + DrawLogo(dl, ds.x * 0.5f, top, width, A(255), true) + 24 * scale;
    } else {
        // The Triforce, three gold triangles with a dark gap in the middle, floating gently.
        const float bob = std::sin(secondsIn * 2.0f) * 4.0f * scale;
        const ImVec2 c(ds.x * 0.5f, ds.y * 0.27f + bob);
        const float t = 62 * scale; // half the width of the whole mark
        auto tri = [&](ImVec2 top, ImVec2 left, ImVec2 right) {
            dl->AddTriangleFilled(ImVec2(top.x + 3 * scale, top.y + 3 * scale), ImVec2(left.x + 3 * scale, left.y + 3 * scale), ImVec2(right.x + 3 * scale, right.y + 3 * scale), shadow);
            dl->AddTriangleFilled(top, left, right, gold);
            dl->AddTriangle(top, left, right, goldDark, 2.0f * scale);
        };
        const float h = t * 1.732f;                    // height of the whole mark
        const float half = t * 0.5f, hh = h * 0.5f;
        const ImVec2 apex(c.x, c.y - hh), bl(c.x - t, c.y + hh), br(c.x + t, c.y + hh), midL(c.x - half, c.y), midR(c.x + half, c.y), midB(c.x, c.y + hh);
        tri(apex, midL, midR);
        tri(midL, bl, midB);
        tri(midR, midB, br);

        y = ds.y * 0.27f + hh + 28 * scale;
        y += lettered(y, 58 * scale, gold, "OOT ROYALE") + 10 * scale;

    }
    // A thin gold rule with a diamond in the middle under the title.
    dl->AddLine(ImVec2(ds.x * 0.34f, y), ImVec2(ds.x * 0.66f, y), goldDark, 2.0f * scale);
    dl->AddQuadFilled(ImVec2(ds.x * 0.5f, y - 6 * scale), ImVec2(ds.x * 0.5f + 6 * scale, y), ImVec2(ds.x * 0.5f, y + 6 * scale), ImVec2(ds.x * 0.5f - 6 * scale, y), gold);
    y += 22 * scale;
    y += lettered(y, 34 * scale, cream, "Made by Tevin Dahl") + 26 * scale;
    y += lettered(y, 21 * scale, cream, "Thank you to my wife Cynthia for supporting my wildest dreams and hobbies :)");
}


void DrawQuestLabel() {
    if (gRoyaleQuestLabel <= 0) return;
    gRoyaleQuestLabel--;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float scale = std::clamp(ds.y / 720.0f, 0.8f, 2.2f);
    // The title screen is a 4:3 picture fitted to the window's height; its subtitle sits right of centre, a little below the middle.
    const float cx = ds.x * 0.5f + (246.0f / 320.0f - 0.5f) * ds.y * (4.0f / 3.0f), cy = ds.y * (196.0f / 240.0f);
    const char* text = "BATTLE ROYALE";
    const float size = 30.0f * scale;
    const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    const ImVec2 pos(cx - sz.x * 0.5f, cy - sz.y * 0.5f);
    for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) if (dx || dy) dl->AddText(font, size, ImVec2(pos.x + dx * 2.0f, pos.y + dy * 2.0f), IM_COL32(40, 20, 0, 255), text);
    dl->AddText(font, size, pos, IM_COL32(255, 214, 90, 255), text);
}

// ---- banners, chest-opening and boss effects --------------------------------------------------------------------------------
// A big line of text across the top for the moments that matter (a major boss arriving, what a chest gave you), and an item that floats up out of
// a chest you open. (The bosses' own effects are in RoyaleBosses.h.)
struct Banner { std::string text; ImU32 colour; double until; double start; };
std::vector<Banner> gBanners;
void ShowBanner(const std::string& text, ImU32 colour, float seconds = 2.6f) {
    const double now = ImGui::GetTime();
    gBanners.push_back({ text, colour, now + seconds, now });
    if (gBanners.size() > 2) gBanners.erase(gBanners.begin());
}

// Small "+5 Rupees" lines that float up on the right when something drops out of a rock or bush.
// The kill feed, top right: who got whom.
struct FeedLine { std::string text; ImU32 colour; double at; };
std::vector<FeedLine> gFeed;
void AddFeed(const std::string& text, ImU32 colour) {
    gFeed.push_back({ text, colour, ImGui::GetTime() });
    if (gFeed.size() > 4) gFeed.erase(gFeed.begin());
}
void DrawFeed(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale) {
    const double now = ImGui::GetTime();
    float y = 14.0f * scale;
    for (const FeedLine& f : gFeed) {
        const double age = now - f.at;
        if (age > 5.0) continue;
        const float a = static_cast<float>(std::min(1.0, (5.0 - age) / 1.0));
        const float size = 17.0f * scale;
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, f.text.c_str());
        const ImVec2 pos(ds.x - sz.x - 18.0f * scale, y);
        dl->AddRectFilled(ImVec2(pos.x - 8 * scale, pos.y - 2 * scale), ImVec2(pos.x + sz.x + 8 * scale, pos.y + sz.y + 2 * scale), IM_COL32(6, 12, 18, static_cast<int>(150 * a)), 5.0f * scale);
        dl->AddText(font, size, pos, (f.colour & 0x00FFFFFF) | (static_cast<ImU32>(255 * a) << 24), f.text.c_str());
        y += sz.y + 7.0f * scale;
    }
    gFeed.erase(std::remove_if(gFeed.begin(), gFeed.end(), [&](const FeedLine& f) { return now - f.at > 5.0; }), gFeed.end());
}

struct Gain { std::string text; ImU32 colour; double at; };
std::vector<Gain> gGains;
void ShowGain(const std::string& text, ImU32 colour) {
    gGains.push_back({ text, colour, ImGui::GetTime() });
    if (gGains.size() > 5) gGains.erase(gGains.begin());
}
void DrawGains(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale) {
    const double now = ImGui::GetTime();
    float y = ds.y * 0.42f;
    for (const Gain& g : gGains) {
        const double age = now - g.at;
        if (age > 1.8) continue;
        const float a = static_cast<float>(std::min(1.0, (1.8 - age) / 0.5));
        const float size = 22.0f * scale;
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, g.text.c_str());
        const ImVec2 pos(ds.x - sz.x - 24.0f * scale, y - static_cast<float>(age) * 26.0f * scale);
        dl->AddText(font, size, ImVec2(pos.x + 1.5f, pos.y + 1.5f), IM_COL32(0, 0, 0, static_cast<int>(220 * a)), g.text.c_str());
        dl->AddText(font, size, pos, (g.colour & 0x00FFFFFF) | (static_cast<ImU32>(255 * a) << 24), g.text.c_str());
        y += 28.0f * scale;
    }
    gGains.erase(std::remove_if(gGains.begin(), gGains.end(), [&](const Gain& g) { return now - g.at > 1.8; }), gGains.end());
}

struct PickupFx { royale::ItemId item; royale::Rarity rarity; float x, y, z; double at; };
std::vector<PickupFx> gPickupFx;


void DrawBanners(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale) {
    const double now = ImGui::GetTime();
    float y = ds.y * 0.27f;
    for (const Banner& b : gBanners) {
        const double age = now - b.start, left = b.until - now;
        if (left <= 0) continue;
        const float a = static_cast<float>(std::min(1.0, std::min(age / 0.2, left / 0.5)));
        const float size = (30.0f + 6.0f * static_cast<float>(std::max(0.0, 1.0 - age * 4.0))) * scale;
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, b.text.c_str());
        const ImVec2 pos((ds.x - sz.x) * 0.5f, y);
        const ImU32 col = (b.colour & 0x00FFFFFF) | (static_cast<ImU32>(255 * a) << 24);
        dl->AddRectFilled(ImVec2(pos.x - 20 * scale, pos.y - 6 * scale), ImVec2(pos.x + sz.x + 20 * scale, pos.y + sz.y + 6 * scale), IM_COL32(6, 12, 18, static_cast<int>(170 * a)), 8.0f * scale);
        dl->AddText(font, size, ImVec2(pos.x + 2, pos.y + 2), IM_COL32(0, 0, 0, static_cast<int>(230 * a)), b.text.c_str());
        dl->AddText(font, size, pos, col, b.text.c_str());
        y += sz.y + 16.0f * scale;
    }
    gBanners.erase(std::remove_if(gBanners.begin(), gBanners.end(), [&](const Banner& b) { return b.until <= now; }), gBanners.end());
}

// The item floats up out of the chest on a halo of its rarity colour, spinning its glow, then fades.
void DrawPickupFx(ImDrawList* dl, ImVec2 ds, float scale) {
    const double now = ImGui::GetTime();
    for (const PickupFx& f : gPickupFx) {
        const double age = now - f.at;
        if (age > 1.7) continue;
        ImVec2 at;
        if (!WorldToScreen(f.x, f.y + 50.0f + static_cast<float>(std::min(age, 1.0)) * 90.0f, f.z, &at)) continue;
        const float a = static_cast<float>(std::min(1.0, (1.7 - age) / 0.5));
        const float pop = static_cast<float>(std::min(1.0, age / 0.25)) * (1.0f + 0.12f * std::sin(static_cast<float>(age) * 14.0f));
        const Rgb& c = kRarityRgb[static_cast<int>(f.rarity)];
        const float R = 44.0f * scale * pop;
        dl->AddCircleFilled(at, R * 1.5f, IM_COL32(c.r, c.g, c.b, static_cast<int>(60 * a)), 28);
        for (int i = 0; i < 10; i++) { // rays turning slowly
            const float ang = i * 0.62832f + static_cast<float>(age) * 1.6f;
            dl->AddLine(ImVec2(at.x + std::cos(ang) * R * 0.9f, at.y + std::sin(ang) * R * 0.9f), ImVec2(at.x + std::cos(ang) * R * 1.8f, at.y + std::sin(ang) * R * 1.8f),
                        IM_COL32(c.r, c.g, c.b, static_cast<int>(150 * a)), 3.0f * scale);
        }
        dl->AddCircleFilled(at, R, OotPanel(static_cast<int>(210 * a)), 28);
        dl->AddCircle(at, R, IM_COL32(c.r, c.g, c.b, static_cast<int>(255 * a)), 28, 3.0f * scale);
        DrawItemIcon(dl, f.item, at, R * 1.25f, IM_COL32(c.r, c.g, c.b, 255));
    }
    gPickupFx.erase(std::remove_if(gPickupFx.begin(), gPickupFx.end(), [&](const PickupFx& f) { return now - f.at > 1.7; }), gPickupFx.end());
}

// ---- hit effects -------------------------------------------------------------------------------------------------------------
// When you hurt someone: a white X hit marker at the crosshair (red when it is a kill) and the damage floating up from them.
// When you are hurt: the screen edges flash red, a red arrow on a ring around the middle points at who hit you, and the damage shows.
struct FloatingNumber { float x, y, z, amount; double at; bool mine; };
std::vector<FloatingNumber> gFloatingNumbers;
struct IncomingHit { uint16_t from; double at; };
std::vector<IncomingHit> gIncomingHits;
double gHurtAt = -100.0, gHitMarkerAt = -100.0;
bool gHitMarkerKill = false;
float gHurtAmount = 0.0f;

bool KnownPosition(uint16_t id, float* x, float* z) {
    if (gSession.Client() && royale::IsHelperId(id)) for (const auto& h:gSession.Client()->Helpers()) if (h.Id()==id && h.hp>0) { *x=h.x;*z=h.z;return true; }
    for (const auto& st : gSession.Puppets()) if (st.id == id) { *x = st.x; *z = st.z; return true; }
    if (gSession.Client() && royale::IsBossId(id)) for (const auto& bn : gSession.Client()->Bosses()) if (royale::kBossIdBase + bn.index == id) { *x = bn.x; *z = bn.z; return true; }
    if (CartTargetAt(id, x, z)) return true;
    return false;
}

// ---- hit feedback ------------------------------------------------------------------------------------------------------------------
// Every blow that lands makes the target flash and throw particles from the point of impact, the way the game's own enemies do (the red or
// white flash of Actor_SetColorFilter, a hit mark, blood drops from CollisionCheck_SpawnRedBlood and friends), but scaled by how hard the hit was
// and coloured for what was hit: red drops for living things, bone dust for skeletons, sparks for armour, ice for the frost, embers for fire.
// It all runs through the game's own effect system (no display lists of ours), so the per-layer pool of "graphics layers" has nothing to carry.
enum class HitKind { Flesh, Bone, Metal, Ice, Fire, Moss, Water, Shadow, Magic };

HitKind HitKindOfBoss(const BossActor& b) {
    switch (KindOf(b)) {
        case BK::Stone: return HitKind::Bone;
        case BK::Lava: case BK::DragonFire: return HitKind::Fire;
        case BK::Frost: return HitKind::Ice;
        case BK::Moss: return HitKind::Moss;
        case BK::Tide: case BK::DragonWater: return HitKind::Water;
        case BK::Shade: case BK::DragonShadow: return HitKind::Shadow;
        case BK::Dune: return b.armourOff ? HitKind::Flesh : HitKind::Metal;
        default: return HitKind::Magic;
    }
}

// Where the blow lands on a target at `pos` (height `height` above its feet), nudged to the side the attacker is on.
Vec3f HitPoint(const Vec3f& pos, float height, float fromX, float fromZ, bool haveFrom, float reach) {
    Vec3f at = { pos.x, pos.y + height, pos.z };
    if (haveFrom) {
        const float dx = fromX - pos.x, dz = fromZ - pos.z, d = std::hypot(dx, dz);
        if (d > 1.0f) { at.x += dx / d * reach; at.z += dz / d * reach; }
    }
    return at;
}

void HitParticles(PlayState* play, HitKind kind, const Vec3f& at, float size, int count, float fromX, float fromZ, bool haveFrom, bool kill) {
    // Particles fly away from the attacker: the hit is a spray out of the far side as well as a puff at the near one.
    float dirX = 0, dirZ = 0;
    if (haveFrom) { const float dx = at.x - fromX, dz = at.z - fromZ, d = std::hypot(dx, dz); if (d > 1.0f) { dirX = dx / d; dirZ = dz / d; } }
    auto spray = [&](Color_RGBA8 prim, Color_RGBA8 env, int n, float speed, s16 scale, s32 life) {
        for (int i = 0; i < n; i++) {
            Vec3f pos = { at.x + Rand_CenteredFloat(10.0f), at.y + Rand_CenteredFloat(10.0f), at.z + Rand_CenteredFloat(10.0f) };
            const float sp = speed * (0.5f + Rand_ZeroOne());
            Vec3f vel = { dirX * sp * 0.8f + Rand_CenteredFloat(speed), speed * (0.3f + Rand_ZeroOne() * 0.9f), dirZ * sp * 0.8f + Rand_CenteredFloat(speed) };
            Vec3f accel = { 0.0f, -0.4f, 0.0f };
            EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, scale, life);
        }
    };
    auto puff = [&](Color_RGBA8 prim, Color_RGBA8 env, s16 scale) {   // a soft puff of dust at the point of impact
        Vec3f pos = { at.x, at.y, at.z }, vel = { dirX * 1.2f, 0.8f, dirZ * 1.2f }, accel = { 0.0f, 0.15f, 0.0f };
        EffectSsDust_Spawn(play, 4, &pos, &vel, &accel, &prim, &env, scale, 6, 12, 0);
    };
    Vec3f mark = at;
    const s16 markScale = static_cast<s16>(220.0f * size);
    switch (kind) {
        case HitKind::Flesh:
            if (gHitBlood) {
                EffectSsHitMark_SpawnCustomScale(play, EFFECT_HITMARK_RED, markScale, &mark);
                CollisionCheck_SpawnRedBlood(play, &mark);   // the game's red drops
                spray({ 200, 20, 30, 255 }, { 110, 0, 20, 255 }, count, 3.0f * size, 40, 22);
            } else {
                EffectSsHitMark_SpawnCustomScale(play, EFFECT_HITMARK_WHITE, markScale, &mark);
                spray({ 255, 255, 230, 255 }, { 255, 230, 120, 255 }, count, 3.0f * size, 40, 20);
            }
            break;
        case HitKind::Bone:
            EffectSsHitMark_SpawnCustomScale(play, EFFECT_HITMARK_DUST, markScale, &mark);
            puff({ 235, 225, 195, 255 }, { 150, 135, 100, 255 }, static_cast<s16>(260 * size));
            spray({ 240, 230, 200, 255 }, { 170, 150, 110, 255 }, count, 2.6f * size, 34, 22);
            break;
        case HitKind::Metal:
            EffectSsHitMark_SpawnCustomScale(play, EFFECT_HITMARK_METAL, markScale, &mark);
            CollisionCheck_SpawnShieldParticlesMetal(play, &mark);   // the shower of sparks off steel
            spray({ 255, 240, 170, 255 }, { 255, 160, 40, 255 }, count, 3.6f * size, 30, 16);
            break;
        case HitKind::Ice:
            EffectSsHitMark_SpawnCustomScale(play, EFFECT_HITMARK_WHITE, markScale, &mark);
            spray({ 225, 245, 255, 255 }, { 110, 180, 255, 255 }, count, 2.8f * size, 40, 24);
            if (kill || size > 1.5f) EffectSsIcePiece_SpawnBurst(play, &mark, 0.6f * size);   // shards flying off
            break;
        case HitKind::Fire:
            EffectSsHitMark_SpawnCustomScale(play, EFFECT_HITMARK_RED, markScale, &mark);
            spray({ 255, 190, 40, 255 }, { 255, 60, 0, 255 }, count, 3.0f * size, 44, 26);
            puff({ 90, 60, 40, 200 }, { 30, 20, 10, 0 }, static_cast<s16>(200 * size));
            break;
        case HitKind::Moss:
            EffectSsHitMark_SpawnCustomScale(play, EFFECT_HITMARK_DUST, markScale, &mark);
            spray({ 170, 240, 90, 255 }, { 40, 120, 20, 255 }, count, 2.8f * size, 38, 22);
            break;
        case HitKind::Water:
            EffectSsHitMark_SpawnCustomScale(play, EFFECT_HITMARK_WHITE, markScale, &mark);
            CollisionCheck_SpawnWaterDroplets(play, &mark);   // the game's water droplets
            spray({ 200, 235, 255, 255 }, { 60, 130, 230, 255 }, count, 2.8f * size, 36, 22);
            break;
        case HitKind::Shadow:
            EffectSsHitMark_SpawnCustomScale(play, EFFECT_HITMARK_WHITE, markScale, &mark);
            puff({ 90, 40, 140, 220 }, { 20, 0, 40, 0 }, static_cast<s16>(280 * size));
            spray({ 190, 120, 255, 255 }, { 70, 20, 130, 255 }, count, 2.8f * size, 40, 24);
            break;
        case HitKind::Magic:
            EffectSsHitMark_SpawnCustomScale(play, EFFECT_HITMARK_WHITE, markScale, &mark);
            spray({ 255, 255, 190, 255 }, { 120, 200, 255, 255 }, count, 3.4f * size, 44, 24);
            break;
    }
}

// Flash and particles for a blow of `hearts` on `id` (a player, a bot or a boss). `attacker` may be kNoPlayer16 (the storm, a fall): no spray then.
void HitFeedback(uint16_t id, uint16_t attacker, float hearts, bool kill, bool self) {
    if (!DebugOn(kDbgHitFx) || gPlayState == nullptr) return;
    if (self && !gHitOnSelf) return;
    const float size = gHitScaled ? std::clamp(0.75f + hearts * 0.3f, 0.8f, 2.0f) * (kill ? 1.3f : 1.0f) : 1.0f;
    float fromX = 0, fromZ = 0;
    const bool haveFrom = attacker != royale::net::kNoPlayer16 && KnownPosition(attacker, &fromX, &fromZ);
    Vec3f pos = { 0, 0, 0 };
    float height = 36.0f;
    HitKind kind = HitKind::Flesh;
    BossActor* boss = nullptr;
    Actor* actor = nullptr;
    if (self) {
        Player* me = GET_PLAYER(gPlayState);
        pos = me->actor.world.pos;
    } else if (royale::IsHelperId(id)) {
        auto it=gBokoblins.find(id); if (it==gBokoblins.end() || !it->second.actor) return;
        actor=it->second.actor;pos=actor->world.pos;height=35.0f;
    } else if (royale::IsBossId(id)) {
        auto it = gBosses.find(id);
        if (it == gBosses.end() || it->second.actor == nullptr || it->second.fade < 0.2f) return;   // out of sight: nothing to hit
        boss = &it->second;
        pos = boss->actor->world.pos;
        kind = HitKindOfBoss(*boss);
        height = royale::IsDragonKind(KindOf(*boss)) ? 70.0f : 50.0f * royale::kBossDefs[boss->kind].scale;
    } else {
        auto a = gActorOf.find(id);
        if (a == gActorOf.end()) return;
        actor = a->second;
        pos = actor->world.pos;
    }
    if (!self && std::hypot(pos.x - GET_PLAYER(gPlayState)->actor.world.pos.x, pos.z - GET_PLAYER(gPlayState)->actor.world.pos.z) > 2400.0f) return;   // too far to see
    const Vec3f at = HitPoint(pos, height, fromX, fromZ, haveFrom, boss != nullptr ? 22.0f * royale::kBossDefs[boss->kind].scale : 14.0f);
    if (gHitReact && (boss == nullptr || !royale::IsDragonKind(KindOf(*boss)))) {   // rock away from the attacker, harder for a harder hit
        float dx = 0, dz = 0;
        if (haveFrom) { dx = pos.x - fromX; dz = pos.z - fromZ; const float d = std::hypot(dx, dz); if (d > 1.0f) { dx /= d; dz /= d; } else dx = dz = 0.0f; }
        const float amp = std::clamp(0.10f + hearts * 0.05f, 0.12f, 0.3f) * (boss != nullptr ? 0.7f : 1.0f);
        if (boss != nullptr) { boss->recoilAge = 0.0f; boss->recoilAmp = amp; boss->recoilDx = dx; boss->recoilDz = dz; }
        else { HitRecoil& r = gHitRecoil[id]; r.amp = amp; r.age = 0.0f; r.dx = dx; r.dz = dz; }
    }
    if (gHitFlash && !self) {
        const int frames = std::clamp(static_cast<int>(8 + 5 * size), 8, 20);
        if (boss != nullptr) {
            boss->flashAge = 0.0f; boss->flashLen = frames / royale::kTickHz;
            const bool white = kind == HitKind::Bone || kind == HitKind::Metal || kind == HitKind::Magic;
            const bool blue = kind == HitKind::Ice || kind == HitKind::Water;
            boss->flashR = white ? 255 : blue ? 90 : 255;
            boss->flashG = white ? 255 : blue ? 170 : kind == HitKind::Moss ? 160 : 40;
            boss->flashB = white ? 255 : blue ? 255 : kind == HitKind::Shadow ? 200 : 20;
        } else if (actor != nullptr) {
            Actor_SetColorFilter(actor, 0x4000, 0xFF, 0, frames);   // the game's red enemy flash
        }
    }
    if (gHitParticles && gHitAmount > 0.01f) {
        const int count = std::clamp(static_cast<int>((self ? 3.0f : 5.0f) * size * gHitAmount + 0.5f), 0, 16);
        HitParticles(gPlayState, kind, at, self ? std::min(size, 1.2f) : size, count, fromX, fromZ, haveFrom, kill);
    }
}

void DrawHitEffects(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale) {
    const double now = ImGui::GetTime();
    if (gPlayState == nullptr || !InField()) { gFloatingNumbers.clear(); gIncomingHits.clear(); return; }
    Player* pl = GET_PLAYER(gPlayState);
    // red edges
    const double hurtAge = now - gHurtAt;
    if (hurtAge < 0.7) {
        const int a = static_cast<int>((1.0 - hurtAge / 0.7) * (150 + std::min(90.0f, gHurtAmount * 80.0f)));
        const ImU32 solid = IM_COL32(220, 20, 20, a), clear = IM_COL32(220, 20, 20, 0);
        const float e = std::min(ds.x, ds.y) * 0.22f;
        dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(ds.x, e), solid, solid, clear, clear);
        dl->AddRectFilledMultiColor(ImVec2(0, ds.y - e), ImVec2(ds.x, ds.y), clear, clear, solid, solid);
        dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(e, ds.y), solid, clear, clear, solid);
        dl->AddRectFilledMultiColor(ImVec2(ds.x - e, 0), ImVec2(ds.x, ds.y), clear, solid, solid, clear);
    }
    // arrows towards whoever hit you
    const ImVec2 centre(ds.x * 0.5f, ds.y * 0.5f);
    const float yaw = static_cast<float>(Camera_GetInputDirYaw(GET_ACTIVE_CAM(gPlayState))) * (3.14159265f / 32768.0f);
    for (const IncomingHit& hit : gIncomingHits) {
        const double age = now - hit.at;
        if (age > 1.6) continue;
        float ax, az;
        if (!KnownPosition(hit.from, &ax, &az)) continue;
        const float rel = std::atan2(ax - pl->actor.world.pos.x, az - pl->actor.world.pos.z) - yaw;
        const ImVec2 dir(std::sin(rel), -std::cos(rel)), side(-dir.y, dir.x);
        const float R = 150.0f * scale, w = 20.0f * scale, len = 34.0f * scale;
        const ImVec2 base(centre.x + dir.x * R, centre.y + dir.y * R);
        const int a = static_cast<int>(255.0 * (1.0 - age / 1.6));
        dl->AddTriangleFilled(ImVec2(base.x + dir.x * len, base.y + dir.y * len), ImVec2(base.x + side.x * w, base.y + side.y * w), ImVec2(base.x - side.x * w, base.y - side.y * w), IM_COL32(235, 30, 30, a));
    }
    gIncomingHits.erase(std::remove_if(gIncomingHits.begin(), gIncomingHits.end(), [&](const IncomingHit& h) { return now - h.at > 1.6; }), gIncomingHits.end());
    // hit marker
    const double markAge = now - gHitMarkerAt;
    if (markAge < 0.3) {
        const float k = static_cast<float>(1.0 - markAge / 0.3);
        const float in = (6.0f + (1.0f - k) * 5.0f) * scale, out = in + 11.0f * scale;
        const ImU32 col = gHitMarkerKill ? IM_COL32(255, 60, 60, static_cast<int>(255 * k)) : IM_COL32(255, 255, 255, static_cast<int>(255 * k));
        for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2) {
            dl->AddLine(ImVec2(centre.x + sx * in, centre.y + sy * in), ImVec2(centre.x + sx * out, centre.y + sy * out), IM_COL32(0, 0, 0, static_cast<int>(200 * k)), 5.0f * scale);
            dl->AddLine(ImVec2(centre.x + sx * in, centre.y + sy * in), ImVec2(centre.x + sx * out, centre.y + sy * out), col, 2.6f * scale);
        }
    }
    // floating numbers
    for (const FloatingNumber& f : gFloatingNumbers) {
        const double age = now - f.at;
        if (age > 1.0) continue;
        ImVec2 at;
        if (f.mine) { // taken from you: rises beside the crosshair
            at = ImVec2(centre.x + 60.0f * scale, centre.y + 20.0f * scale - static_cast<float>(age) * 50.0f * scale);
        } else if (!WorldToScreen(f.x, f.y + 70.0f + static_cast<float>(age) * 60.0f, f.z, &at)) {
            continue;
        }
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.1f", f.amount);
        const int a = static_cast<int>(255.0 * (1.0 - age));
        const float size = (f.mine ? 26.0f : 24.0f + std::min(14.0f, f.amount * 8.0f)) * scale;
        dl->AddText(font, size, ImVec2(at.x + 1.5f, at.y + 1.5f), IM_COL32(0, 0, 0, a), buf);
        dl->AddText(font, size, at, f.mine ? IM_COL32(255, 80, 80, a) : IM_COL32(255, 230, 90, a), buf);
    }
    gFloatingNumbers.erase(std::remove_if(gFloatingNumbers.begin(), gFloatingNumbers.end(), [&](const FloatingNumber& f) { return now - f.at > 1.0; }), gFloatingNumbers.end());
}

