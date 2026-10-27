// ---- the always-on HUD ------------------------------------------------------------------------------------------------------

std::string ClockText(float seconds) {
    int s = static_cast<int>(std::ceil(std::max(0.0f, seconds)));
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

// Drawn straight onto the screen every frame, whether or not the menu is open: alive count, storm timer, a pointer to the
// safe zone, what you hold, and the big banners for countdown, elimination and results.
bool gSandboxGlide = false;   // the Sandbox's "glide from here" button: the skydive behaves as in the drop (glide, hold Z to dive) outside the drop
bool gSkydiving = false;   // falling in from the sky at the start of the match (see UpdateSkydive)
int gBusPhase = 0;         // while gSkydiving: 1 sat on the Moonfall Bus, 2 free fall from it, 0 the glider (BattleBus.inc)
void DrawBusOnMap(ImDrawList* dl, float scale, const std::function<ImVec2(float, float)>& toMap, const royale::HudState& h);
bool gDiveHeld = false;    // Z is held to dive. The game never sees that Z (see OnEmoteWheelInput), so it can't lock on and flatten the camera

// Sprinting, the Fortnite way: click the left stick while running and Link runs faster, draining a stamina bar under the magic meter.
// It stops when you let go of the stick, click again or run dry, and the bar refills after a short rest. The N64 pad has no stick
// click, so it is read from SDL directly. The game's own top run speed is raised (patches/0011), so Link's legs, footsteps and turning
// keep up with it; others see the Sprint pose (see ClassifyAnim). Purely local: the server trusts your position.
constexpr float kSprintMult = royale::kSprintMult;   // shared/balance.h: the bots sprint with the same numbers
constexpr float kSprintSeconds = royale::kSprintSeconds;
constexpr float kStaminaRefill = royale::kStaminaRefill;
constexpr float kStaminaRest = royale::kStaminaRest;
constexpr float kSprintMinStamina = royale::kSprintMinStamina;
float gStamina = 1.0f;
float gStaminaRestLeft = 0.0f;
bool gSprinting = false;
bool gSprintButtonWasDown = false;
double gStaminaFullAt = 0.0;                // when the bar last filled, so it can fade away

bool SprintButtonDown() {
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (!SDL_IsGameController(i)) continue;
        SDL_GameController* pad = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i));   // only pads the game already opened
        if (pad != nullptr && SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSTICK)) return true;
    }
    return false;
}

void UpdateSprint(Player* player, const royale::HudState& hud) {
    const float dt = 1.0f / royale::kTickHz;   // the game updates the player 20 times a second
    const bool down = SprintButtonDown();
    const bool clicked = down && !gSprintButtonWasDown;
    gSprintButtonWasDown = down;

    if (!LiveAndAlive(hud)) {   // a fresh bar for every match
        gStamina = 1.0f; gSprinting = false;
        return;
    }
    const Input& in = gPlayState->state.input[0];
    const bool stickHeld = std::hypot(static_cast<float>(in.cur.stick_x), static_cast<float>(in.cur.stick_y)) > 30.0f;
    const bool canSprint = InField() && !gSkydiving && hud.stunLeft <= 0 && (player->actor.bgCheckFlags & 1) &&
                           !(player->stateFlags1 & (PLAYER_STATE1_IN_WATER | PLAYER_STATE1_ON_HORSE | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_SHIELDING |
                                                    PLAYER_STATE1_CARRYING_ACTOR | PLAYER_STATE1_HANGING_OFF_LEDGE | PLAYER_STATE1_CLIMBING_LADDER));
    if (clicked) gSprinting = !gSprinting && canSprint && stickHeld && gStamina >= kSprintMinStamina;
    // Jumping off a ledge mid-sprint keeps it going; only the conditions that really end a run stop it.
    if (!stickHeld || gStamina <= 0.0f || hud.stunLeft > 0 || (player->stateFlags1 & (PLAYER_STATE1_IN_WATER | PLAYER_STATE1_ON_HORSE | PLAYER_STATE1_IN_CUTSCENE |
                                                                                          PLAYER_STATE1_HANGING_OFF_LEDGE | PLAYER_STATE1_CLIMBING_LADDER)))
        gSprinting = false;

    if (gSprinting) {
        if (std::fabs(player->linearVelocity) > 2.0f) gStamina = std::max(0.0f, gStamina - dt / kSprintSeconds);   // only running costs stamina
        gStaminaRestLeft = kStaminaRest;
    } else if (gStaminaRestLeft > 0.0f) {
        gStaminaRestLeft -= dt;
    } else if (gStamina < 1.0f) {
        gStamina = std::min(1.0f, gStamina + dt / kStaminaRefill);
        if (gStamina >= 1.0f) gStaminaFullAt = ImGui::GetTime();
    }
    gRoyaleRunSpeedScale = gSprinting ? kSprintMult : 1.0f;
    if (gSprinting && (player->actor.bgCheckFlags & 1) && std::fabs(player->linearVelocity) > 4.0f) SprintDust(gPlayState, &player->actor, gPlayState->gameplayFrames);
}

// Where the shield meter starts (below the heart rows): DrawOotVitals draws the shield and magic meters from here, the stamina bar sits under them.
float HudMeterTop(float scale, const royale::HudState& h) {
    const int count = std::clamp(static_cast<int>(std::ceil(h.maxHealth)), 1, 32);
    return 20.0f * scale + ((count + 9) / 10) * 22.0f * scale + 5.0f * scale;
}

// A thin bar under the magic meter, the width of the meters above it. No text. It only shows while you are using
// stamina and fades out a moment after it fills, so it stays off the screen the rest of the time.
void DrawStaminaBar(ImDrawList* dl, ImVec2 ds, const royale::HudState& h) {
    if (!LiveAndAlive(h) || !InField()) return;
    const double sinceFull = gStamina >= 1.0f ? ImGui::GetTime() - gStaminaFullAt : 0.0;
    const float alpha = static_cast<float>(std::clamp(1.0 - (sinceFull - 0.8) / 0.4, 0.0, 1.0));
    if (alpha <= 0.0f) return;
    auto a = [&](int v) { return static_cast<int>(v * alpha); };
    const float scale = std::clamp(ds.y / 720.0f, 0.8f, 2.2f);   // as DrawOverlay
    const float bx = 22.0f * scale, by = HudMeterTop(scale, h) + 21.0f * scale + 16.0f * scale + 5.0f * scale, bw = 150.0f * scale, bh = 4.0f * scale;
    const float fill = std::clamp(gStamina, 0.0f, 1.0f);
    const bool winded = !gSprinting && fill < kSprintMinStamina;
    dl->AddRectFilled(ImVec2(bx - 2, by - 2), ImVec2(bx + bw + 2, by + bh + 2), IM_COL32(0, 0, 0, a(170)), 3.0f);
    dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), HudColor(royale::wartable::theme::Button,a(200)), 2.0f);   // dark leather brown
    if (fill > 0.0f) {
        // Hylian hair gold (#F0DF57) from the art guide; Hylian crest red (#AD3725) while too winded to sprint.
        dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw * fill, by + bh), winded ? IM_COL32(173, 55, 37, a(255)) : HudColor(royale::wartable::theme::Gold,a(255)), 2.0f);
        dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw * fill, by + bh * 0.45f), IM_COL32(255, 248, 200, a(150)), 2.0f);
    }
}

struct SupplyMark { float x, z; double until; };
std::vector<SupplyMark> gSupplyMarks;   // announced crates, shown on the minimap until they have landed and been taken


// Spectating after you are eliminated: you can watch your own body (where it fell) or any other player who is still alive, and nobody else.
// D-pad Left/Right (or the < > buttons) switch between them; if the one you watch is eliminated the view moves on to the next living player.
constexpr uint16_t kSpectateSelf = 0xFFFF;
uint16_t gSpectateTarget = kSpectateSelf;

std::vector<uint16_t> SpectatableIds() {
    std::vector<uint16_t> ids = { kSpectateSelf };
    for (const auto& st : gSession.Puppets()) if (st.alive) ids.push_back(st.id);
    std::sort(ids.begin() + 1, ids.end());
    return ids;
}

void CycleSpectate(int dir) {
    const std::vector<uint16_t> ids = SpectatableIds();
    size_t at = 0;
    for (size_t i = 0; i < ids.size(); i++) if (ids[i] == gSpectateTarget) at = i;
    gSpectateTarget = ids[(at + ids.size() + (dir < 0 ? ids.size() - 1 : 1)) % ids.size()];
}

const royale::PuppetState* SpectateTarget() {
    if (gSpectateTarget == kSpectateSelf) return nullptr;
    for (const auto& st : gSession.Puppets()) if (st.id == gSpectateTarget && st.alive) { static royale::PuppetState keep; keep = st; return &keep; }
    return nullptr;
}

std::string SpectateName() {
    if (const royale::PuppetState* t = SpectateTarget()) return t->name.empty() ? "Link" : t->name;
    return "Your body";
}

// Is something covering the game right now (its own pause screen or the port's menu)? Then the storm and weather tints stay off the screen.
bool ScreenCovered() {
    if (WarMenuOpen()) return true;
    if (gPlayState != nullptr && gPlayState->pauseCtx.state != 0) return true;
    return SohGui::mSohMenu && SohGui::mSohMenu->IsVisible();
}

// The storm. Its edge is a glowing wall standing in the world (DrawStormWall), so you can see it from across the map. Standing outside the safe
// zone you are in the storm itself: the sky goes dark (DriveTimeOfDay), the game's own rain, lightning and a violet fog close in
// (DriveRealWeather), and this adds only a faint violet tint on top. Nothing of it is drawn while you are dead or a menu covers the game.
void DrawStorm(ImDrawList* dl, ImVec2 ds, float scale, const royale::HudState& h) {
    (void)scale;
    const float w = gStormWeather;
    if (w <= 0.0f || !InField() || !h.selfAlive || ScreenCovered()) return;
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ds, IM_COL32(40, 16, 70, static_cast<int>(60 * w)), IM_COL32(40, 16, 70, static_cast<int>(60 * w)),
                                IM_COL32(70, 40, 110, static_cast<int>(35 * w)), IM_COL32(70, 40, 110, static_cast<int>(35 * w)));
}

// ---- seasons and weather ----------------------------------------------------------------------------------------------------
// What the sky is doing (the server's spell of weather). Rain, snow, lightning, fog and the desert sandstorm are the game's own effects, in the
// world (DriveRealWeather); ash and blowing sand are particles in the world too (DrawWeatherParticles). All that is left on the screen is a faint
// seasonal tint and the flash of a bolt landing next to you. Everything fades in and out as spells change.

float WeatherAmount() { return gWeatherBlend * gWeatherShown.Strength(); }

void DrawWeather(ImDrawList* dl, ImVec2 ds, float scale, const royale::HudState& h) {
    (void)scale;
    if (InRegularGame()) return;   // the regular game's weather is its own (DriveRegularWeather)
    const bool live = InField() && gSession.Joined() && (h.state == royale::MatchState::Drop || h.state == royale::MatchState::InMatch);
    const bool active = live && h.weather.sky != royale::Sky::Clear && h.weather.intensity > 0;
    if (active) gWeatherShown = h.weather;
    gWeatherBlend = std::clamp(gWeatherBlend + (active ? 0.012f : -0.012f), 0.0f, 1.0f);
    if (live) gWeatherShown.season = h.weather.season;
    if (!live || ScreenCovered()) return;
    // The season colours the whole picture a little.
    static const ImU32 kSeasonTint[4] = { IM_COL32(150, 230, 140, 16), IM_COL32(255, 214, 120, 18), IM_COL32(255, 150, 60, 26), IM_COL32(190, 215, 255, 30) };
    dl->AddRectFilled(ImVec2(0, 0), ds, kSeasonTint[static_cast<int>(h.weather.season) & 3]);
    // A lightning bolt landing nearby lights up the whole screen.
    const double t = ImGui::GetTime();
    if (t < gBoltFlashUntil) dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(225, 232, 255, static_cast<int>(std::min(1.0, (gBoltFlashUntil - t) / 0.35) * 150.0)));
}

// Town names hanging over each point of interest, big enough to read from the sky while you skydive. On foot a name shows only for a place
// near you that you are not already in ("Entering ..." says that), and only while nothing solid stands between the camera and it, fading in and out.
void DrawPoiLabels(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const royale::HudState& h) {
    if (!gSession.Client() || !InField()) return;
    (void)h;
    Player* pl = GET_PLAYER(gPlayState);
    const float reach = gSkydiving ? 14000.0f : 3200.0f;
    static float shown[64] = {};   // how visible each place's name is now, 0 to 1, eased so a name does not blink as trunks pass in front
    Vec3f eye = gPlayState->view.eye;
    const auto& pois = gSession.Client()->Pois();
    for (size_t i = 0; i < pois.size(); i++) {
        const royale::Poi& p = pois[i];
        float& vis = shown[i & 63];
        const float dx = p.center.x - pl->actor.world.pos.x, dz = p.center.z - pl->actor.world.pos.z;
        const float d = std::sqrt(dx * dx + dz * dz);
        bool want = d <= reach && (gSkydiving || d > p.radius * 0.6f);
        if (!want && vis <= 0.0f) continue;   // out of reach and already faded: no floor or sight test for it
        // The ground under a place doesn't move: measured once per place (FloorAt walks every climbing block), not every frame.
        static float groundX[64], groundZ[64], groundY[64];
        static bool groundKnown[64] = {};
        const size_t slot = i & 63;
        float y = pl->actor.world.pos.y;
        if (groundKnown[slot] && groundX[slot] == p.center.x && groundZ[slot] == p.center.z) y = groundY[slot];
        else if (FloorAt(p.center.x, p.center.z, &y)) { groundKnown[slot] = true; groundX[slot] = p.center.x; groundZ[slot] = p.center.z; groundY[slot] = y; }
        if (want && !gSkydiving) {   // the way from the camera to the name must be open (looked at every other frame: the fade hides the gap)
            static bool clear[64] = {};
            if (((static_cast<size_t>(gPlayState->gameplayFrames) + i) & 1) == 0) {
                Vec3f to = { p.center.x, y + 650.0f, p.center.z }, hit;
                CollisionPoly* poly = nullptr;
                s32 bgId = 0;
                clear[slot] = !BgCheck_CameraLineTest1(&gPlayState->colCtx, &eye, &to, &hit, &poly, true, true, true, true, &bgId);
            }
            want = clear[slot];
        }
        vis = std::clamp(vis + (want ? 0.08f : -0.08f), 0.0f, 1.0f);
        if (vis <= 0.0f) continue;
        ImVec2 at;
        if (!WorldToScreen(p.center.x, y + 650.0f, p.center.z, &at)) continue;
        const int alpha = static_cast<int>(255.0f * vis * std::clamp((reach - d) / (reach * 0.4f), 0.0f, 1.0f));
        if (alpha <= 0) continue;
        const float size = std::clamp(34.0f * scale * (2400.0f / (d + 1200.0f)), 15.0f * scale, 38.0f * scale);
        const char* name = royale::kPoiNames[p.name];
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, name);
        const ImVec2 pos(at.x - sz.x * 0.5f, at.y - sz.y * 0.5f);
        // A name never runs into the compass strip at the top centre (DrawCompass: 440 wide, down to 42, its markers under it, then DrawStormBar): it fades out
        // as it climbs towards it.
        const float compassHalf = 220.0f * scale, compassBottom = 92.0f * scale;   // and DrawStormBar's bar and words, down to 92
        int a = alpha;
        if (pos.x < ds.x * 0.5f + compassHalf && pos.x + sz.x > ds.x * 0.5f - compassHalf)
            a = static_cast<int>(alpha * std::clamp((pos.y - compassBottom) / (24.0f * scale), 0.0f, 1.0f));
        if (a <= 0) continue;
        dl->AddText(font, size, ImVec2(pos.x + 2.0f, pos.y + 2.0f), IM_COL32(10, 14, 8, a), name);
        dl->AddText(font, size, pos, IM_COL32(255, 222, 110, a), name);
    }
}

bool MapOption(const char* name, bool fallback = true) {
    char key[64];
    std::snprintf(key, sizeof(key), CVAR_SETTING("Royale.%s"), name);
    return CVarGetInteger(key, fallback ? 1 : 0) != 0;
}
// Developer mode (Settings > Updates, off by default): shows test maps, debug switches, Regular game, pets, Practice/Sandbox and the
// Jones / Peter / Master Chief stations. Off, the War Table offers only the release game. Nothing is removed, only hidden.
bool DevMode() { return CVarGetInteger(CVAR_SETTING("Royale.DevMode"), 0) != 0; }
// Release battlefields: Great Hyrule Kingdom (the main map) and Moonfall Isle, with Hyrule Kingdom and Skyloft Isles as beta.
bool ReleaseMap(int id) { return id == royale::kHyruleThirdStormMapIndex || id == royale::kGreatHyruleKingdomMapIndex || id == royale::kMoonfallMapIndex || id == royale::kKingdomMapIndex || id == royale::kSkyloftMapIndex; }
std::string MapLabel(int id) {
    std::string s = royale::MapOf(id).name;
    if (!DevMode() && (id == royale::kKingdomMapIndex || id == royale::kSkyloftMapIndex)) s += " (beta)";
    return s;
}

// The game's own overworld minimap for the scene (the light-blue map that sits in the corner in Ocarina of Time), read from the player's ROM
// like the rest of the game's art. Its 4-bit grey and alpha pixels are turned into white for ImGui and tinted when drawn; drawn smooth rather
// than in blocky pixels, since the minimap below shows it close up.
struct GameMinimap { bool tried = false, ok = false; int texId = -1; ImTextureID tex = nullptr; int w = 0, h = 0; };

const char* OverworldMinimapName(int index) {
    static const char* const kNames[] = { dgHyruleFieldMinimapTex, dgKakarikoVillageMinimapTex, dgGraveyardMinimapTex, dgZorasRiverMinimapTex,
                                          dgKokiriForestMinimapTex, dgSacredMeadowMinimapTex, dgLakeHyliaMinimapTex, dgZorasDomainMinimapTex,
                                          dgZorasFountainMinimapTex, dgGerudoValleyMinimapTex, dgHauntedWastelandMinimapTex, dgDesertColossusMinimapTex,
                                          dgGerudosFortessMinimapTex, dgLostWoodsMinimapTex, dgHyruleCastleAreaMinimapTex, dgDeathMountainTrailMinimapTex,
                                          dgDeathMountainCraterMinimapTex, dgGoronCityMinimapTex, dgLonLonRanchMinimapTex, dgOutsideGanonsCastleMinimapTex };
    return index >= 0 && index < static_cast<int>(sizeof(kNames) / sizeof(kNames[0])) ? kNames[index] : nullptr;
}

ImTextureID UploadRgba(const uint8_t* rgba, int w, int h, int* idInOut) {
    GfxRenderingAPI* api = gfx_get_current_rendering_api();
    if (api == nullptr) return nullptr;
    if (*idInOut < 0) *idInOut = static_cast<int>(api->new_texture());
    api->select_texture(0, *idInOut);
    api->set_sampler_parameters(0, true, 0, 0);
    api->upload_texture(rgba, w, h);
    return reinterpret_cast<ImTextureID>(static_cast<intptr_t>(*idInOut));
}

// The Fortnite map's picture for the minimap: the baked vertex colours of the island, upscaled smoothly by the graphics card.
ImTextureID FortniteMinimapTexture() {
    static int texId = -1;
    static ImTextureID tex = nullptr;
    static int triedFor = -1;   // which ground the picture was made from (the island's or the Sandbox's)
    if (triedFor != royale::fortnite::gTerrainMapId) {
        triedFor = royale::fortnite::gTerrainMapId;
        constexpr int n = royale::fortnite::kFine + 1;
        std::vector<uint8_t> rgba(static_cast<size_t>(n) * n * 4);
        for (int i = 0; i < n * n; i++) {
            rgba[i * 4 + 0] = royale::fortnite::gColourData[i * 3 + 0];
            rgba[i * 4 + 1] = royale::fortnite::gColourData[i * 3 + 1];
            rgba[i * 4 + 2] = royale::fortnite::gColourData[i * 3 + 2];
            rgba[i * 4 + 3] = 255;
        }
        tex = UploadRgba(rgba.data(), n, n, &texId);
    }
    return tex;
}

const GameMinimap* GameMinimapFor(int sceneNum) {
    static GameMinimap maps[20];
    const int index = sceneNum - SCENE_HYRULE_FIELD;
    const char* name = OverworldMinimapName(index);
    if (name == nullptr) return nullptr;
    GameMinimap& m = maps[index];
    if (!m.tried) {
        m.tried = true;
        try {
            auto res = std::static_pointer_cast<Fast::Texture>(Ship::Context::GetInstance()->GetResourceManager()->LoadResource(name, true));
            if (res != nullptr && res->ImageData != nullptr && res->Type == Fast::TextureType::GrayscaleAlpha4bpp && res->Width > 0 && res->Height > 0 &&
                res->ImageDataSize >= static_cast<uint32_t>(res->Width * res->Height / 2)) {
                std::vector<uint8_t> rgba(static_cast<size_t>(res->Width) * res->Height * 4);
                for (int i = 0; i < res->Width * res->Height; i++) {
                    const uint8_t nib = (i & 1) ? (res->ImageData[i / 2] & 0xF) : (res->ImageData[i / 2] >> 4);
                    const uint8_t grey = static_cast<uint8_t>(((nib >> 1) & 7) * 255 / 7);
                    rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = grey;
                    rgba[i * 4 + 3] = (nib & 1) ? 255 : 0;
                }
                m.tex = UploadRgba(rgba.data(), res->Width, res->Height, &m.texId);
                m.w = res->Width;
                m.h = res->Height;
                m.ok = m.tex != nullptr;
            }
        } catch (...) {}
    }
    return m.ok ? &m : nullptr;
}

// Top right: a minimap that follows you. It shows the area around you on the game's own minimap for the scene, north up, with you in the
// middle as the game's yellow arrow: the storm in purple, the safe zone's edge, supply drops, mini bosses and other players. While you skydive
// it zooms out to show the whole map, so you can pick where to land.
constexpr float kMinimapHalf = 86.0f;   // 720p units
ImVec2 MinimapCentre(ImVec2 ds, float scale) { return ImVec2(ds.x - (kMinimapHalf + 22.0f) * scale, (kMinimapHalf + 22.0f) * scale); }
void DrawMinimap(ImDrawList* dl, ImVec2 ds, float scale, const royale::HudState& h) {
    if (h.map.radius <= 0 || !InField()) return;
    Player* pl = GET_PLAYER(gPlayState);
    const float half = kMinimapHalf * scale;
    const ImVec2 c = MinimapCentre(ds, scale);
    const ImVec2 a(c.x - half, c.y - half), b(c.x + half, c.y + half);
    const float px = pl->actor.world.pos.x, pz = pl->actor.world.pos.z;

    // How much of the world fits across (half of it, in world units): close around you on foot, the whole map while skydiving.
    static float viewHalf = 4200.0f;
    const float want = gSkydiving ? std::max(4200.0f, h.map.radius * 1.15f) : 4200.0f;
    viewHalf += (want - viewHalf) * std::min(1.0f, ImGui::GetIO().DeltaTime * 3.0f);
    const float k = half / viewHalf;
    // North (the game's -Z) is up, as on the game's own minimap.
    auto toMap = [&](float x, float z) { return ImVec2(c.x + (x - px) * k, c.y + (z - pz) * k); };
    auto inside = [&](ImVec2 p) { return p.x > a.x && p.x < b.x && p.y > a.y && p.y < b.y; };
    auto pinned = [&](ImVec2 p) {   // something off the edge: pinned to the rim, on the line towards it
        const float m = 5.0f * scale, dx = p.x - c.x, dy = p.y - c.y, far = std::max(std::fabs(dx), std::fabs(dy));
        if (far <= half - m) return p;
        const float t = (half - m) / far;
        return ImVec2(c.x + dx * t, c.y + dy * t);
    };

    const float round = 12.0f * scale;
    dl->AddRectFilled(ImVec2(a.x + 3 * scale, a.y + 4 * scale), ImVec2(b.x + 3 * scale, b.y + 4 * scale), IM_COL32(0, 0, 0, 90), round);
    HudFrame(dl,a,b,scale);
    dl->PushClipRect(ImVec2(a.x + 2 * scale, a.y + 2 * scale), ImVec2(b.x - 2 * scale, b.y - 2 * scale), true);

    // The game's minimap, placed with the game's own numbers for where it sits and how world positions land on it (z_map_exp.c's compass
    // icons): pixel u = (offsetX + x / scaleX) / 10 + 160 - minimapX, pixel v = 120 - (offsetY - z / scaleY) / 10 - minimapY.
    const int owIndex = gPlayState->sceneNum - SCENE_HYRULE_FIELD;
    const GameMinimap* gm = gFortniteScene ? nullptr : GameMinimapFor(gPlayState->sceneNum);
    if (gFortniteScene) {   // the island's own picture (its texture), laid over the whole map
        ImTextureID tex = FortniteMinimapTexture();
        if (tex != nullptr) dl->AddImage(tex, toMap(-royale::fortnite::MapHalfX(), -royale::fortnite::MapHalfZ()), toMap(royale::fortnite::MapHalfX(), royale::fortnite::MapHalfZ()), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 210));
    } else if (gm != nullptr && gMapData != nullptr && owIndex >= 0 && owIndex < 20) {
        const float sx = gMapData->owCompassInfo[owIndex][0], sz = gMapData->owCompassInfo[owIndex][1];
        const float ox = gMapData->owCompassInfo[owIndex][2], oy = gMapData->owCompassInfo[owIndex][3];
        const float mx = gMapData->owMinimapPosX[owIndex], my = gMapData->owMinimapPosY[owIndex];
        if (sx > 0 && sz > 0) {
            const float x0 = ((mx - 160.0f) * 10.0f - ox) * sx, x1 = x0 + gm->w * 10.0f * sx;
            const float z0 = (oy - (120.0f - my) * 10.0f) * sz, z1 = z0 + gm->h * 10.0f * sz;
            dl->AddImage(gm->tex, toMap(x0, z0), toMap(x1, z1), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(0, 255, 255, 190));   // the game's minimap colour
        }
    }

    // The storm: everything outside the safe zone, as a thick purple ring around it (clipped to the map), then the zone's white edge.
    const ImVec2 zc = toMap(h.safeZone.center.x, h.safeZone.center.z);
    const float zr = h.safeZone.radius * k;
    const float reach = std::sqrt((std::fabs(zc.x - c.x) + half) * (std::fabs(zc.x - c.x) + half) + (std::fabs(zc.y - c.y) + half) * (std::fabs(zc.y - c.y) + half));
    if (reach > zr) {
        const float thick = reach - zr + 4.0f * scale;
        dl->AddCircle(zc, zr + thick * 0.5f, IM_COL32(120, 50, 190, 120), 96, thick);
    }
    const bool hasNext = h.nextZone.radius > 0.0f && (std::fabs(h.nextZone.radius - h.safeZone.radius) > 1.0f ||
                                                       royale::Distance(h.nextZone.center, h.safeZone.center) > 1.0f);
    // While a smaller circle is coming, the storm's edge is lilac and the next safe zone is the white ring, as in Fortnite.
    dl->AddCircle(zc, zr, hasNext ? IM_COL32(205, 160, 255, 235) : IM_COL32(255, 255, 255, 235), 96, 2.0f * scale);
    if (hasNext) {
        const ImVec2 nc = toMap(h.nextZone.center.x, h.nextZone.center.z);
        const float nr = h.nextZone.radius * k;
        dl->AddCircle(nc, nr, IM_COL32(0, 0, 0, 150), 96, 3.5f * scale);
        dl->AddCircle(nc, nr, IM_COL32(255, 255, 255, 245), 96, 2.0f * scale);
        // Outside it: a dotted gold path from you to its nearest edge.
        const float dx = nc.x - c.x, dy = nc.y - c.y, d = std::sqrt(dx * dx + dy * dy);
        if (d > nr + 3.0f * scale) {
            const float ux = dx / d, uy = dy / d, len = d - nr, step = 7.0f * scale;
            for (float t = 6.0f * scale; t < len; t += step)
                dl->AddCircleFilled(ImVec2(c.x + ux * t, c.y + uy * t), 1.6f * scale, IM_COL32(255, 222, 110, 230), 8);
        }
    }

    const double now = ImGui::GetTime();
    {   // supply drops and helpers to hire: a pulsing star, pinned to the rim when off the map
        const float pulse = 0.75f + 0.25f * static_cast<float>(std::sin(now * 5.0));
        auto star = [&](float wx, float wz, ImU32 col) {
            const ImVec2 p = pinned(toMap(wx, wz));
            const float u = 7.0f * scale * pulse;
            dl->AddQuadFilled(ImVec2(p.x, p.y - u), ImVec2(p.x + u * 0.4f, p.y), ImVec2(p.x, p.y + u), ImVec2(p.x - u * 0.4f, p.y), col);
            dl->AddQuadFilled(ImVec2(p.x - u, p.y), ImVec2(p.x, p.y - u * 0.4f), ImVec2(p.x + u, p.y), ImVec2(p.x, p.y + u * 0.4f), col);
        };
        gSupplyMarks.erase(std::remove_if(gSupplyMarks.begin(), gSupplyMarks.end(), [&](const SupplyMark& m) { return now > m.until; }), gSupplyMarks.end());
        for (const SupplyMark& m : gSupplyMarks) star(m.x, m.z, IM_COL32(255, 160, 60, 255));
        for (const auto& [aid, al] : gAllies) if (al.owner == h.selfId) star(al.x, al.z, IM_COL32(120, 255, 150, 255));   // only your own helpers
        if (gSession.Client()) for (const auto& l : gSession.Client()->Loot()) if (l.supply && l.chest && !l.taken) star(l.x, l.z, IM_COL32(255, 220, 90, 255));
    }
    if (gSession.Client() && MapOption("MapEnemies")) {
        for (const auto& bn : gSession.Client()->Bosses()) {   // mini bosses: a purple diamond
            const ImVec2 p = toMap(bn.x, bn.z);
            if (!inside(p)) continue;
            const float u = 5.0f * scale;
            dl->AddQuadFilled(ImVec2(p.x, p.y - u), ImVec2(p.x + u, p.y), ImVec2(p.x, p.y + u), ImVec2(p.x - u, p.y), IM_COL32(190, 60, 255, 255));
            dl->AddQuad(ImVec2(p.x, p.y - u), ImVec2(p.x + u, p.y), ImVec2(p.x, p.y + u), ImVec2(p.x - u, p.y), IM_COL32(255, 255, 255, 230), 1.5f);
        }
    }
    DrawCartsOnMap(dl, scale, toMap, inside);
    DrawBusOnMap(dl, scale, toMap, h);
    DrawPadsOnMinimap(dl, scale, toMap);
    DrawPingsOnMinimap(dl, scale, toMap);
    DrawTeamOnMinimap(dl, scale, toMap);
    for (const auto& st : gSession.Puppets()) {
        if (!st.alive || !MapOption(st.isBot ? "MapBots" : "MapPlayers")) continue;
        if (st.busSeat >= 0 || (gSkydiving && royale::IsAirborne(st.anim))) continue;   // on the bus or in the sky: the bus's moon stands for them
        const ImVec2 p = toMap(st.x, st.z);
        if (!inside(p)) continue;
        dl->AddCircleFilled(p, 3.4f * scale, IM_COL32(0, 0, 0, 200));
        dl->AddCircleFilled(p, 2.6f * scale, st.isBot ? IM_COL32(255, 100, 100, 255) : IM_COL32(255, 170, 60, 255));
    }
    dl->PopClipRect();

    // You, in the middle: the game's own minimap arrow (yellow-green, as Minimap_DrawCompassIcons colours it), pointing the way Link faces.
    const float th = pl->actor.shape.rot.y * (3.14159265f / 32768.0f);
    const ImVec2 fwd(std::sin(th), std::cos(th)), side(-fwd.y, fwd.x);
    const float u = 7.0f * scale;
    const ImVec2 tip(c.x + fwd.x * u * 1.3f, c.y + fwd.y * u * 1.3f), l(c.x - fwd.x * u + side.x * u * 0.8f, c.y - fwd.y * u + side.y * u * 0.8f),
                 r(c.x - fwd.x * u - side.x * u * 0.8f, c.y - fwd.y * u - side.y * u * 0.8f);
    dl->AddTriangleFilled(tip, l, r, IM_COL32(200, 255, 0, 255));
    dl->AddTriangle(tip, l, r, IM_COL32(0, 0, 0, 220), 1.5f * scale);

    // The frame: the menus' gold rule, and N at the top.
    dl->AddRect(a,b,HudColor(royale::wartable::theme::Border),0,0,1.0f*scale);
    dl->AddRect(ImVec2(a.x + 4 * scale, a.y + 4 * scale), ImVec2(b.x - 4 * scale, b.y - 4 * scale), IM_COL32(255, 214, 90, 120), round * 0.7f, 0, 1.0f * scale);
    const ImVec2 nsz = ImGui::GetFont()->CalcTextSizeA(14.0f * scale, FLT_MAX, 0.0f, "N");
    const ImVec2 np(c.x - nsz.x * 0.5f, a.y - nsz.y * 0.5f);
    dl->AddCircleFilled(ImVec2(c.x, a.y), nsz.y * 0.7f, IM_COL32(16, 12, 8, 230));
    dl->AddText(ImGui::GetFont(), 14.0f * scale, np, IM_COL32(255, 222, 110, 255), "N");
}

// Top centre: Fortnite's compass strip in the War Table frame. Link's heading in a gold box in the middle, the letters and ticks sliding past
// (fading towards the edges, 120 degrees in view), a ring for the middle of the safe zone (red once you are outside it) and an orange star for
// each supply drop. North is the game's -Z, as on the minimap.
void DrawCompass(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const royale::HudState& h) {
    if (!InField() || !h.selfAlive) return;
    Player* pl = GET_PLAYER(gPlayState);
    const float px = pl->actor.world.pos.x, pz = pl->actor.world.pos.z;
    const float th = pl->actor.shape.rot.y * (3.14159265f / 32768.0f);
    const float heading = 180.0f - th * (180.0f / 3.14159265f);   // Link faces (sin th, cos th); bearing 0 is -Z, 90 is +X
    auto wrap = [](float d) { d = std::fmod(d + 180.0f, 360.0f); if (d < 0) d += 360.0f; return d - 180.0f; };
    auto bearing = [&](float x, float z) { return std::atan2(x - px, -(z - pz)) * (180.0f / 3.14159265f); };
    constexpr float kSpan = 60.0f;   // degrees either side of the middle

    const float cw = 440.0f * scale, ch = 30.0f * scale, x0 = (ds.x - cw) * 0.5f, y0 = 12.0f * scale, x1 = x0 + cw, y1 = y0 + ch;
    const float reach = cw * 0.5f - 14.0f * scale;
    auto at = [&](float rel) { return ds.x * 0.5f + rel / kSpan * reach; };
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(6, 15, 36, 150), 6.0f * scale);
    const ImU32 gold = HudColor(royale::wartable::theme::Gold);
    for (int i = 0; i < 4; ++i) {   // gold corner brackets, as HudFrame
        const ImVec2 c(i & 1 ? x1 : x0, i & 2 ? y1 : y0);
        dl->AddLine(c, ImVec2(c.x + (i & 1 ? -9.0f : 9.0f) * scale, c.y), gold, 2.0f * scale);
        dl->AddLine(c, ImVec2(c.x, c.y + (i & 2 ? -9.0f : 9.0f) * scale), gold, 2.0f * scale);
    }
    static const char* const names[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    for (int deg = 0; deg < 360; deg += 15) {
        const float rel = wrap(static_cast<float>(deg) - heading);
        if (std::fabs(rel) > kSpan) continue;
        const int a = static_cast<int>(255.0f * std::pow(1.0f - std::fabs(rel) / kSpan, 0.6f));
        const float x = at(rel);
        if (deg % 45 == 0) {
            const char* n = names[deg / 45];
            const float size = (n[1] ? 11.0f : 15.0f) * scale;
            const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, n);
            const ImVec2 p(x - sz.x * 0.5f, (y0 + y1) * 0.5f - sz.y * 0.5f);
            dl->AddText(font, size, ImVec2(p.x + 1.0f, p.y + 1.0f), IM_COL32(0, 0, 0, a * 220 / 255), n);
            dl->AddText(font, size, p, HudColor(royale::wartable::theme::Ink, a), n);
        } else {
            dl->AddLine(ImVec2(x, y1 - 8.0f * scale), ImVec2(x, y1 - 3.0f * scale), HudColor(royale::wartable::theme::Muted, a), 1.5f * scale);
        }
    }
    {   // the heading, in a gold-rimmed box in the middle
        char num[8];
        std::snprintf(num, sizeof(num), "%03d", (static_cast<int>(std::lround(heading)) % 360 + 360) % 360);
        const ImVec2 a(ds.x * 0.5f - 26.0f * scale, y0 - 4.0f * scale), b(ds.x * 0.5f + 26.0f * scale, y1 + 4.0f * scale);
        dl->AddRectFilled(a, b, HudColor(royale::wartable::theme::Panel, 245), 5.0f * scale);
        dl->AddRect(a, b, gold, 5.0f * scale, 0, 1.5f * scale);
        const float size = 15.0f * scale;
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, num);
        const ImVec2 p(ds.x * 0.5f - sz.x * 0.5f, (a.y + b.y) * 0.5f - sz.y * 0.5f);
        dl->AddText(font, size, ImVec2(p.x + 1.0f, p.y + 1.0f), IM_COL32(0, 0, 0, 220), num);
        dl->AddText(font, size, p, HudColor(royale::wartable::theme::Ink), num);
    }

    // Markers under the strip.
    const float my = y1 + 12.0f * scale;
    {   // supply drops: the same orange star as on the minimap
        const double now = ImGui::GetTime();
        const float u = 6.0f * scale;
        auto star = [&](float wx, float wz, ImU32 col) {
            const float rel = wrap(bearing(wx, wz) - heading);
            if (std::fabs(rel) > kSpan) return;
            const float x = at(rel);
            dl->AddQuadFilled(ImVec2(x, my - u), ImVec2(x + u * 0.4f, my), ImVec2(x, my + u), ImVec2(x - u * 0.4f, my), col);
            dl->AddQuadFilled(ImVec2(x - u, my), ImVec2(x, my - u * 0.4f), ImVec2(x + u, my), ImVec2(x, my + u * 0.4f), col);
        };
        for (const SupplyMark& m : gSupplyMarks) if (now <= m.until) star(m.x, m.z, IM_COL32(255, 160, 60, 255));
        if (gSession.Client()) for (const auto& l : gSession.Client()->Loot()) if (l.supply && l.chest && !l.taken) star(l.x, l.z, IM_COL32(255, 160, 60, 255));
    }
    {   // the safe zone's middle: a white ring, red when you stand outside the zone; pinned to the end of the strip when it is off to the side
        const float dx = h.safeZone.center.x - px, dz = h.safeZone.center.z - pz;
        const bool outside = std::sqrt(dx * dx + dz * dz) > h.safeZone.radius;
        const float rel = std::clamp(wrap(bearing(h.safeZone.center.x, h.safeZone.center.z) - heading), -kSpan, kSpan);
        const float x = at(rel);
        const ImU32 col = outside ? IM_COL32(255, 90, 90, 245) : IM_COL32(255, 255, 255, 240);
        dl->AddTriangleFilled(ImVec2(x, y1 + 1.0f * scale), ImVec2(x - 4.0f * scale, y1 + 6.0f * scale), ImVec2(x + 4.0f * scale, y1 + 6.0f * scale), col);
        dl->AddCircle(ImVec2(x, y1 + 12.0f * scale), 5.0f * scale, IM_COL32(0, 0, 0, 200), 16, 3.5f * scale);
        dl->AddCircle(ImVec2(x, y1 + 12.0f * scale), 5.0f * scale, col, 16, 2.0f * scale);
    }
}

// Under the compass: how far through the storm's hold or shrink we are, and how far you are from the next safe zone.
// Gold while the storm holds, purple while it closes; the distance is red when you stand outside the next circle.
void DrawStormBar(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const royale::HudState& h) {
    if (!InField() || !h.selfAlive || h.state != royale::MatchState::InMatch || h.nextZone.radius <= 0.0f) return;
    Player* pl = GET_PLAYER(gPlayState);
    const float w = 260.0f * scale, bh = 7.0f * scale, x0 = (ds.x - w) * 0.5f, y0 = 66.0f * scale;
    const ImU32 gold = HudColor(royale::wartable::theme::Gold), purple = IM_COL32(190, 110, 255, 255), red = IM_COL32(255, 90, 90, 255);
    dl->AddRectFilled(ImVec2(x0 - 2.0f * scale, y0 - 2.0f * scale), ImVec2(x0 + w + 2.0f * scale, y0 + bh + 2.0f * scale), IM_COL32(6, 15, 36, 170), 4.0f * scale);
    if (h.stormPhaseFrac > 0.0f)
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + w * h.stormPhaseFrac, y0 + bh), h.stormShrinking ? purple : gold, 3.0f * scale);
    dl->AddRect(ImVec2(x0 - 2.0f * scale, y0 - 2.0f * scale), ImVec2(x0 + w + 2.0f * scale, y0 + bh + 2.0f * scale), IM_COL32(255, 222, 110, 140), 4.0f * scale, 0, 1.0f * scale);

    const float d = royale::Distance({ pl->actor.world.pos.x, pl->actor.world.pos.z }, h.nextZone.center) - h.nextZone.radius;
    const std::string left = std::string(h.stormShrinking ? "Storm closing " : "Storm closes in ") + ClockText(h.stormSecondsLeft);
    const std::string right = d > 0.0f ? "Safe zone " + std::to_string(static_cast<int>(d / 40.0f)) + " m" : "In the next circle";   // 40 units = 1 m, as squads::kUnitsPerMetre
    const float size = 14.0f * scale, ty = y0 + bh + 4.0f * scale;
    auto text = [&](float x, ImU32 col, const std::string& t) {
        dl->AddText(font, size, ImVec2(x + 1.5f, ty + 1.5f), IM_COL32(0, 0, 0, 230), t.c_str());
        dl->AddText(font, size, ImVec2(x, ty), col, t.c_str());
    };
    text(x0, h.stormShrinking ? purple : HudColor(royale::wartable::theme::Ink), left);
    text(x0 + w - font->CalcTextSizeA(size, FLT_MAX, 0.0f, right.c_str()).x, d > 0.0f ? red : IM_COL32(110, 230, 130, 255), right);
}

// Under the minimap: a framed row with the storm timer, players left and your eliminations, then what is affecting you, right-aligned.
void DrawMatchStats(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const royale::HudState& h, int kills) {
    const ImVec2 mc = MinimapCentre(ds, scale);
    const float half = kMinimapHalf * scale;
    const ImVec2 a(mc.x - half, mc.y + half + 10.0f * scale), b(mc.x + half, a.y + 28.0f * scale);
    const ImU32 ink = HudColor(royale::wartable::theme::Ink), gold = HudColor(royale::wartable::theme::Gold),
                red = IM_COL32(255, 90, 90, 255), green = IM_COL32(110, 230, 130, 255);
    auto shadowed = [&](ImVec2 p, float size, ImU32 col, const char* t) {
        dl->AddText(font, size, ImVec2(p.x + 1.5f, p.y + 1.5f), IM_COL32(0, 0, 0, 230), t);
        dl->AddText(font, size, p, col, t);
    };
    HudFrame(dl, a, b, scale);
    const bool isFinal = h.stormPhase >= royale::kStormPhaseCount;
    const std::string storm = isFinal ? "FINAL" : ClockText(h.stormSecondsLeft);
    const ImU32 stormCol = isFinal || h.stormShrinking || h.stormSecondsLeft <= 5.0f ? red : h.stormSecondsLeft <= 15.0f ? gold : ink;
    const std::string alive = std::to_string(h.alive), ko = std::to_string(kills);
    const float ts = 15.0f * scale, icon = 15.0f * scale, ig = 4.0f * scale, cy = (a.y + b.y) * 0.5f;
    const float w0 = icon + ig + font->CalcTextSizeA(ts, FLT_MAX, 0.0f, storm.c_str()).x, w1 = icon + ig + font->CalcTextSizeA(ts, FLT_MAX, 0.0f, alive.c_str()).x,
                w2 = icon + ig + font->CalcTextSizeA(ts, FLT_MAX, 0.0f, ko.c_str()).x;
    const float pad = 9.0f * scale, space = std::max(3.0f * scale, ((b.x - a.x) - 2 * pad - w0 - w1 - w2) * 0.5f);
    float x = a.x + pad;
    auto label = [&](float xx, const std::string& t, ImU32 col) { shadowed(ImVec2(xx + icon + ig, cy - ts * 0.5f - 1.0f * scale), ts, col, t.c_str()); };
    {   // storm: a purple swirl
        const ImVec2 c(x + icon * 0.5f, cy);
        dl->AddCircle(c, 7.0f * scale, IM_COL32(200, 120, 255, 255), 16, 2.5f * scale);
        dl->AddCircleFilled(c, 2.8f * scale, IM_COL32(200, 120, 255, 255), 10);
        label(x, storm, stormCol);
        x += w0 + space;
    }
    {   // players left: a head and shoulders
        const ImVec2 c(x + icon * 0.5f, cy);
        dl->AddCircleFilled(ImVec2(c.x, c.y - 4.5f * scale), 3.6f * scale, ink, 12);
        dl->AddRectFilled(ImVec2(c.x - 6.5f * scale, c.y + 0.5f * scale), ImVec2(c.x + 6.5f * scale, c.y + 7.5f * scale), ink, 3.0f * scale);
        label(x, alive, ink);
        x += w1 + space;
    }
    {   // eliminations: a sword
        const ImVec2 c(x + icon * 0.5f, cy);
        dl->AddLine(ImVec2(c.x - 6.0f * scale, c.y + 6.0f * scale), ImVec2(c.x + 6.0f * scale, c.y - 6.0f * scale), IM_COL32(220, 230, 240, 255), 3.0f * scale);
        dl->AddLine(ImVec2(c.x - 5.5f * scale, c.y + 0.5f * scale), ImVec2(c.x - 0.5f * scale, c.y + 5.5f * scale), gold, 3.0f * scale);
        label(x, ko, ink);
    }

    // Only what is happening to you right now, short. Gear is shown as icons at the bottom left, the weather is on the screen itself.
    if (!h.selfAlive) return;
    float y = b.y + 8.0f * scale;
    auto line = [&](ImU32 col, const std::string& t) {
        const float size = 16.0f * scale;
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, t.c_str());
        shadowed(ImVec2(b.x - sz.x, y), size, col, t.c_str());
        y += size + 5.0f * scale;
    };
    if (h.adultLeft > 0.0f) line(gold, "Adult " + ClockText(h.adultLeft));
    if (h.invulnLeft > 0) line(gold, "Invulnerable " + ClockText(h.invulnLeft));
    if (h.speedLeft > 0) line(green, "Speed " + ClockText(h.speedLeft));
    if (h.revealLeft > 0) line(green, "Reveal " + ClockText(h.revealLeft));
    if (h.shieldLeft > 0) line(green, "Guard " + ClockText(h.shieldLeft));
    if (h.burnLeft > 0) line(red, "Burning");
    if (h.stunLeft > 0) line(red, "Stunned");
    if (h.inv.hasMark) line(green, "Farore's Wind set");
}

// The game's own item art (the 32x32 icons from its resource archive) is used wherever the game has one for the item; the hand-drawn
// icons below are only the fallback for things the original never had an icon for (songs, loot like rupee piles, medallions).
const char* RealIconName(royale::ItemId id) {
    using royale::ItemId;
    switch (id) {
        case ItemId::DekuStick: return "gItemIconDekuStickTex";
        case ItemId::BasicSword: case ItemId::KokiriSword: return "gItemIconSwordKokiriTex";
        case ItemId::MasterSword: return "gItemIconSwordMasterTex";
        case ItemId::BiggoronSword: return "gItemIconSwordBiggoronTex";
        case ItemId::MegatonHammer: case ItemId::GiantsHammer: return "gItemIconHammerTex";
        case ItemId::Slingshot: case ItemId::TripleSlingshot: return "gItemIconSlingshotTex";
        case ItemId::FairyBow: return "gItemIconBowTex";
        case ItemId::Boomerang: return "gItemIconBoomerangTex";
        case ItemId::Bombs: case ItemId::BombAmmo: return "gItemIconBombTex";
        case ItemId::Bombchus: case ItemId::HomingBombchus: case ItemId::BombchuAmmo: return "gItemIconBombchuTex";
        case ItemId::DekuNuts: case ItemId::NutAmmo: return "gItemIconDekuNutTex";
        case ItemId::FireArrows: return "gItemIconArrowFireTex";
        case ItemId::IceArrows: return "gItemIconArrowIceTex";
        case ItemId::LightArrows: return "gItemIconArrowLightTex";
        case ItemId::ArrowAmmo: return "gItemIconBowTex";
        case ItemId::SeedAmmo: return "gItemIconDekuSeedsTex";
        case ItemId::DekuShield: return "gItemIconShieldDekuTex";
        case ItemId::HylianShield: return "gItemIconShieldHylianTex";
        case ItemId::MirrorShield: return "gItemIconShieldMirrorTex";
        case ItemId::GreenPotion: return "gItemIconBottlePotionGreenTex";
        case ItemId::RedPotion: return "gItemIconBottlePotionRedTex";
        case ItemId::BluePotion: return "gItemIconBottlePotionBlueTex";
        case ItemId::Fairy: return "gItemIconBottleFairyTex";
        case ItemId::Milk: return "gItemIconBottleMilkFullTex";
        case ItemId::Fish: return "gItemIconBottleFishTex";
        case ItemId::BlueFire: return "gItemIconBottleBlueFireTex";
        case ItemId::Bug: return "gItemIconBottleBugTex";
        case ItemId::Poe: return "gItemIconBottlePoeTex";
        case ItemId::SmallShieldPotion: case ItemId::LargeShieldPotion: return "gItemIconBottleEmptyTex";
        case ItemId::DinsFire: return "gItemIconDinsFireTex";
        case ItemId::FaroresWind: return "gItemIconFaroresWindTex";
        case ItemId::NayrusLove: return "gItemIconNayrusLoveTex";
        case ItemId::Hookshot: return "gItemIconHookshotTex";
        case ItemId::Longshot: return "gItemIconLongshotTex";
        case ItemId::LensOfTruth: return "gItemIconLensOfTruthTex";
        case ItemId::MagicBeans: return "gItemIconMagicBeanTex";
        case ItemId::FairyOcarina: return "gItemIconOcarinaFairyTex";
        case ItemId::OcarinaOfTime: return "gItemIconOcarinaOfTimeTex";
        case ItemId::KokiriTunic: return "gItemIconTunicKokiriTex";
        case ItemId::GoronTunic: return "gItemIconTunicGoronTex";
        case ItemId::ZoraTunic: return "gItemIconTunicZoraTex";
        case ItemId::KokiriBoots: return "gItemIconBootsKokiriTex";
        case ItemId::IronBoots: return "gItemIconBootsIronTex";
        case ItemId::HoverBoots: return "gItemIconBootsHoverTex";
        case ItemId::GoronBracelet: return "gItemIconGoronsBraceletTex";
        case ItemId::SilverGauntlets: return "gItemIconSilverGauntletsTex";
        case ItemId::GoldenGauntlets: return "gItemIconGoldenGauntletsTex";
        case ItemId::KeatonMask: return "gItemIconMaskKeatonTex";
        case ItemId::SkullMask: return "gItemIconMaskSkullTex";
        case ItemId::SpookyMask: return "gItemIconMaskSpookyTex";
        case ItemId::BunnyHood: return "gItemIconMaskBunnyHoodTex";
        case ItemId::GoronMask: return "gItemIconMaskGoronTex";
        case ItemId::ZoraMask: return "gItemIconMaskZoraTex";
        case ItemId::GerudoMask: return "gItemIconMaskGerudoTex";
        case ItemId::MaskOfTruth: return "gItemIconMaskTruthTex";
        case ItemId::SilverScale: return "gItemIconScaleSilverTex";
        case ItemId::GoldenScale: return "gItemIconScaleGoldenTex";
        case ItemId::BigQuiver: return "gItemIconQuiver50Tex";
        case ItemId::BulletBag: return "gItemIconBulletBag50Tex";
        case ItemId::BombBag: return "gItemIconBombBag40Tex";
        case ItemId::RecoveryHeart: return "gHeartFullTex";
        case ItemId::HeartPiece: return "gQuestIconHeartPieceTex";
        case ItemId::HeartContainer: return "gQuestIconHeartContainerTex";
        case ItemId::MagicJar: return "gQuestIconMagicJarSmallTex";
        case ItemId::Rupees: return "gRupeeGreenTex";
        case ItemId::ForestMedallion: return "gQuestIconMedallionForestTex";
        case ItemId::FireMedallion: return "gQuestIconMedallionFireTex";
        case ItemId::WaterMedallion: return "gQuestIconMedallionWaterTex";
        case ItemId::SpiritMedallion: return "gQuestIconMedallionSpiritTex";
        case ItemId::ShadowMedallion: return "gQuestIconMedallionShadowTex";
        case ItemId::LightMedallion: return "gQuestIconMedallionLightTex";
        case ItemId::KokiriEmerald: return "gQuestIconKokiriEmeraldTex";
        case ItemId::GoronRuby: return "gQuestIconGoronRubyTex";
        case ItemId::ZoraSapphire: return "gQuestIconZoraSapphireTex";
        case ItemId::ZeldasLullaby: case ItemId::EponasSong: case ItemId::SariasSong: case ItemId::SunsSong: case ItemId::SongOfTime: case ItemId::SongOfStorms:
        case ItemId::MinuetOfForest: case ItemId::BoleroOfFire: case ItemId::SerenadeOfWater: case ItemId::NocturneOfShadow: case ItemId::RequiemOfSpirit: case ItemId::PreludeOfLight:
            return "gSongNoteTex";   // the game's music note, tinted in each song's colour (see RealIconTint)
        default: return nullptr;
    }
}

// Songs share the game's note icon; each is tinted in the colour of its song.
ImU32 RealIconTint(royale::ItemId id) {
    using royale::ItemId;
    switch (id) {
        case ItemId::ZeldasLullaby: return IM_COL32(220, 150, 255, 255);
        case ItemId::EponasSong: return IM_COL32(230, 130, 90, 255);
        case ItemId::SariasSong: return IM_COL32(110, 240, 130, 255);
        case ItemId::SunsSong: return IM_COL32(255, 235, 110, 255);
        case ItemId::SongOfTime: return IM_COL32(120, 170, 255, 255);
        case ItemId::SongOfStorms: return IM_COL32(190, 200, 215, 255);
        case ItemId::MinuetOfForest: return IM_COL32(90, 220, 90, 255);
        case ItemId::BoleroOfFire: return IM_COL32(255, 90, 60, 255);
        case ItemId::SerenadeOfWater: return IM_COL32(80, 150, 255, 255);
        case ItemId::NocturneOfShadow: return IM_COL32(180, 90, 230, 255);
        case ItemId::RequiemOfSpirit: return IM_COL32(255, 160, 60, 255);
        case ItemId::PreludeOfLight: return IM_COL32(255, 245, 160, 255);
        default: return IM_COL32(255, 255, 255, 255);
    }
}

// Loads an icon on first use. nullptr (no icon, or not loadable) means "draw the fallback".
void* HudAssetTexture(const char* name, const char* directory) {
    static std::unordered_map<std::string, bool> loaded;
    if (name == nullptr) return nullptr;
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    auto it = loaded.find(name);
    if (it == loaded.end()) {
        bool ok = false;
        try {
            const std::string n = name;
            const std::string path = "__OTR__textures/" + std::string(directory) + "/" + n;
            auto res = Ship::Context::GetInstance()->GetResourceManager()->LoadResource(path, true);
            if (res != nullptr) {
                gui->LoadGuiTexture(std::string("royale:") + name, path, ImVec4(1, 1, 1, 1));
                ok = true;
            }
        } catch (...) {}
        it = loaded.emplace(name, ok).first;
    }
    return it->second ? (void*)gui->GetTextureByName(std::string("royale:") + name) : nullptr;
}

void* RealIcon(royale::ItemId id) {
    const char* name = RealIconName(id);
    if (name == nullptr) return nullptr;
    const std::string n = name;
    const char* directory = n.rfind("gQuestIcon", 0) == 0 ? "icon_item_24_static" :
        (n.rfind("gHeart", 0) == 0 || n.rfind("gRupee", 0) == 0) ? "parameter_static" : "icon_item_static";
    return HudAssetTexture(name, directory);
}

// ---- item icons ------------------------------------------------------------------------------------------------------------
// The game's own item icons live in its resource archive and aren't reachable from here, so every item gets a small hand-drawn
// icon built from lines and shapes, tinted by what the item is. `tier` is the rarity colour, used as the accent.
// The Gilded Sword's icon is the reference picture of the sword (mod/Royale/gilded_sword_icon.h, made by scripts/make_gilded_sword_icon.py).
ImTextureID GildedIconTexture() {
    static ImTextureID tex = nullptr;
    static int id = -1;
    static bool tried = false;
    if (!tried) {
        tried = true;
        tex = UploadRgba(royale::kGildedIconRgba, royale::kGildedIconSize, royale::kGildedIconSize, &id);
    }
    return tex;
}

void DrawItemIcon(ImDrawList* dl, royale::ItemId id, ImVec2 c, float s, ImU32 tier) {
    using royale::ItemId;
    if (id == ItemId::GildedSword) {
        if (ImTextureID tex = GildedIconTexture()) {
            const float h = s * 0.5f;
            dl->AddImage(tex, ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h));
            return;
        }
    }
    if (void* real = RealIcon(id)) {
        const float h = s * 0.5f;
        dl->AddImage(real, ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), ImVec2(0, 0), ImVec2(1, 1), RealIconTint(id));
        return;
    }
    const float u = s * 0.5f; // half the icon box
    auto P = [&](float x, float y) { return ImVec2(c.x + x * u, c.y + y * u); };
    // Colours from the official art: chrome steel with a cool tint, Triforce gold, Kokiri green, Hylian-shield blue, crest red, warm leather.
    const ImU32 steel = IM_COL32(200, 218, 232, 255), dark = IM_COL32(43, 32, 30, 255), wood = IM_COL32(134, 92, 45, 255), gold = IM_COL32(247, 214, 34, 255),
                red = IM_COL32(205, 55, 37, 255), green = IM_COL32(72, 166, 64, 255), blue = IM_COL32(70, 96, 200, 255), white = IM_COL32(240, 236, 222, 255),
                purple = IM_COL32(150, 70, 190, 255), orange = IM_COL32(238, 130, 40, 255), cyan = IM_COL32(110, 200, 220, 255);
    const float th = std::max(1.5f, s * 0.07f);
    auto sword = [&](ImU32 blade, ImU32 hilt, float len, float width) {
        dl->AddLine(P(-0.62f, 0.62f), P(0.62f - (1.0f - len) * 0.6f, -0.62f + (1.0f - len) * 0.6f), blade, th * width);
        dl->AddLine(P(-0.55f, 0.15f), P(-0.15f, 0.55f), hilt, th * 1.2f);   // crossguard
        dl->AddLine(P(-0.45f, 0.45f), P(-0.78f, 0.78f), hilt, th * 1.5f);   // grip
    };
    auto bottle = [&](ImU32 fill, bool big) {
        const float r = big ? 0.62f : 0.5f;
        dl->AddCircleFilled(P(0, 0.22f), u * r, fill, 20);
        dl->AddCircle(P(0, 0.22f), u * r, white, 20, th * 0.7f);
        dl->AddRectFilled(P(-0.17f, -0.62f), P(0.17f, -0.2f), steel);
        dl->AddRectFilled(P(-0.22f, -0.74f), P(0.22f, -0.58f), wood);
    };
    auto shieldShape = [&](ImU32 body, ImU32 trim) {
        const ImVec2 pts[5] = { P(-0.62f, -0.62f), P(0.62f, -0.62f), P(0.62f, 0.1f), P(0.0f, 0.82f), P(-0.62f, 0.1f) };
        dl->AddConvexPolyFilled(pts, 5, body);
        dl->AddPolyline(pts, 5, trim, ImDrawFlags_Closed, th);
    };
    auto heart = [&](ImU32 col, float k) {
        dl->AddCircleFilled(P(-0.3f * k, -0.2f * k), u * 0.42f * k, col, 16);
        dl->AddCircleFilled(P(0.3f * k, -0.2f * k), u * 0.42f * k, col, 16);
        dl->AddTriangleFilled(P(-0.68f * k, -0.02f * k), P(0.68f * k, -0.02f * k), P(0, 0.75f * k), col);
    };
    auto note = [&](ImU32 col) {
        dl->AddCircleFilled(P(-0.25f, 0.5f), u * 0.3f, col, 14);
        dl->AddLine(P(0.02f, 0.45f), P(0.02f, -0.6f), col, th * 1.2f);
        dl->AddLine(P(0.02f, -0.6f), P(0.5f, -0.35f), col, th * 1.6f);
    };
    auto orb = [&](ImU32 col) {
        dl->AddCircleFilled(c, u * 0.62f, col, 22);
        dl->AddCircle(c, u * 0.62f, white, 22, th * 0.8f);
        dl->AddCircleFilled(P(-0.2f, -0.2f), u * 0.18f, IM_COL32(255, 255, 255, 190), 12);
    };
    auto medal = [&](ImU32 col) {
        dl->AddCircleFilled(c, u * 0.62f, col, 22);
        dl->AddCircle(c, u * 0.62f, gold, 22, th * 1.4f);
        dl->AddCircleFilled(c, u * 0.2f, white, 12);
    };
    auto gem = [&](ImU32 col) {
        const ImVec2 pts[4] = { P(0, -0.75f), P(0.55f, 0), P(0, 0.75f), P(-0.55f, 0) };
        dl->AddConvexPolyFilled(pts, 4, col);
        dl->AddPolyline(pts, 4, white, ImDrawFlags_Closed, th * 0.8f);
    };
    auto mask = [&](ImU32 col) {
        dl->AddCircleFilled(c, u * 0.62f, col, 20);
        dl->AddCircleFilled(P(-0.24f, -0.12f), u * 0.13f, dark, 10);
        dl->AddCircleFilled(P(0.24f, -0.12f), u * 0.13f, dark, 10);
        dl->AddLine(P(-0.2f, 0.28f), P(0.2f, 0.28f), dark, th);
    };
    auto boot = [&](ImU32 col) {
        dl->AddRectFilled(P(-0.3f, -0.7f), P(0.2f, 0.3f), col);
        dl->AddRectFilled(P(-0.3f, 0.2f), P(0.7f, 0.7f), col, 4.0f);
        dl->AddRectFilled(P(-0.3f, 0.6f), P(0.7f, 0.75f), dark);
    };
    auto tunic = [&](ImU32 col) {
        const ImVec2 pts[8] = { P(-0.35f, -0.7f), P(0.35f, -0.7f), P(0.8f, -0.35f), P(0.55f, 0.0f), P(0.38f, -0.15f), P(0.38f, 0.7f), P(-0.38f, 0.7f), P(-0.38f, -0.15f) };
        dl->AddConvexPolyFilled(pts, 8, col);
        dl->AddTriangleFilled(P(-0.38f, -0.15f), P(-0.55f, 0.0f), P(-0.8f, -0.35f), col);
        dl->AddTriangleFilled(P(-0.35f, -0.7f), P(-0.8f, -0.35f), P(-0.38f, -0.15f), col);
        dl->AddLine(P(-0.38f, 0.3f), P(0.38f, 0.3f), gold, th);
    };
    auto arrow = [&](ImU32 tip) {
        dl->AddLine(P(-0.7f, 0.7f), P(0.6f, -0.6f), wood, th);
        dl->AddTriangleFilled(P(0.8f, -0.8f), P(0.28f, -0.5f), P(0.5f, -0.28f), tip);
        dl->AddLine(P(-0.7f, 0.7f), P(-0.4f, 0.7f), white, th);
        dl->AddLine(P(-0.7f, 0.7f), P(-0.7f, 0.4f), white, th);
    };

    switch (id) {
        case ItemId::DekuStick: dl->AddLine(P(-0.5f, 0.7f), P(0.5f, -0.7f), wood, th * 2.2f); dl->AddCircleFilled(P(0.55f, -0.75f), u * 0.18f, orange, 8); break;
        case ItemId::BasicSword: sword(IM_COL32(170, 178, 190, 255), wood, 0.7f, 1.2f); break;
        case ItemId::KokiriSword: sword(steel, green, 0.8f, 1.5f); break;
        case ItemId::MasterSword: sword(cyan, blue, 1.0f, 1.8f); dl->AddCircleFilled(P(-0.35f, 0.35f), u * 0.12f, gold, 8); break;
        case ItemId::BiggoronSword: sword(white, red, 1.0f, 2.8f); break;
        case ItemId::GildedSword: {   // silver blade with gold diamonds down it, a red grip and a curled guard
            sword(IM_COL32(214, 222, 238, 255), IM_COL32(210, 216, 232, 255), 1.0f, 2.6f);
            for (float t : { 0.30f, 0.55f, 0.80f }) {
                const float cx = -0.62f + 1.24f * t, cy = 0.62f - 1.24f * t;
                dl->AddQuadFilled(P(cx - 0.17f, cy + 0.17f), P(cx + 0.07f, cy + 0.07f), P(cx + 0.17f, cy - 0.17f), P(cx - 0.07f, cy - 0.07f), gold);
            }
            dl->AddCircle(P(-0.52f, 0.02f), u * 0.13f, IM_COL32(210, 216, 232, 255), 10, th * 0.6f);   // the curls of the guard
            dl->AddCircle(P(-0.02f, 0.52f), u * 0.13f, IM_COL32(210, 216, 232, 255), 10, th * 0.6f);
            dl->AddLine(P(-0.5f, 0.5f), P(-0.7f, 0.7f), red, th * 1.5f);
            break;
        }
        case ItemId::MegatonHammer:
            dl->AddLine(P(-0.6f, 0.7f), P(0.35f, -0.35f), wood, th * 1.8f);
            dl->AddRectFilled(P(0.0f, -0.8f), P(0.78f, -0.15f), IM_COL32(120, 125, 140, 255), 3.0f);
            dl->AddRect(P(0.0f, -0.8f), P(0.78f, -0.15f), steel, 3.0f, 0, th * 0.7f);
            break;
        case ItemId::Slingshot:
            dl->AddLine(P(0, 0.75f), P(0, 0.1f), wood, th * 1.8f);
            dl->AddLine(P(0, 0.1f), P(-0.55f, -0.65f), wood, th * 1.8f);
            dl->AddLine(P(0, 0.1f), P(0.55f, -0.65f), wood, th * 1.8f);
            dl->AddLine(P(-0.55f, -0.65f), P(0.55f, -0.65f), red, th * 0.8f);
            break;
        case ItemId::TripleSlingshot:
            dl->AddLine(P(0, 0.75f), P(0, 0.1f), wood, th * 1.8f);
            dl->AddLine(P(0, 0.1f), P(-0.6f, -0.55f), wood, th * 1.8f);
            dl->AddLine(P(0, 0.1f), P(0.6f, -0.55f), wood, th * 1.8f);
            for (int i = -1; i <= 1; i++) dl->AddCircleFilled(P(i * 0.3f, -0.62f - (i == 0 ? 0.15f : 0.0f)), u * 0.14f, orange, 8);
            break;
        case ItemId::GiantsHammer:
            dl->AddLine(P(-0.7f, 0.8f), P(0.3f, -0.3f), wood, th * 2.2f);
            dl->AddRectFilled(P(-0.15f, -0.95f), P(0.95f, -0.05f), gold, 3.0f);
            dl->AddRect(P(-0.15f, -0.95f), P(0.95f, -0.05f), IM_COL32(160, 110, 30, 255), 3.0f, 0, th);
            break;
        case ItemId::HomingBombchus:
            dl->AddRectFilled(P(-0.3f, -0.55f), P(0.3f, 0.5f), purple, u * 0.3f);
            dl->AddCircleFilled(P(-0.35f, -0.55f), u * 0.22f, IM_COL32(255, 200, 255, 255), 10);
            dl->AddCircleFilled(P(0.35f, -0.55f), u * 0.22f, IM_COL32(255, 200, 255, 255), 10);
            dl->AddLine(P(0, 0.5f), P(0.3f, 0.8f), orange, th);
            dl->AddCircle(c, u * 0.9f, IM_COL32(220, 150, 255, 200), 20, th * 0.8f);   // the ring says it seeks
            break;
        case ItemId::Rupees: gem(green); break;
        case ItemId::ArrowAmmo: for (int i = -1; i <= 1; i++) { dl->AddLine(P(-0.5f + i * 0.2f, 0.7f), P(0.4f + i * 0.2f, -0.6f), wood, th); dl->AddTriangleFilled(P(0.5f + i * 0.2f, -0.8f), P(0.2f + i * 0.2f, -0.55f), P(0.45f + i * 0.2f, -0.4f), steel); } break;
        case ItemId::SeedAmmo: dl->AddCircleFilled(P(-0.3f, 0.15f), u * 0.3f, IM_COL32(190, 150, 90, 255), 12); dl->AddCircleFilled(P(0.3f, -0.1f), u * 0.3f, IM_COL32(150, 190, 90, 255), 12); dl->AddCircleFilled(P(0.0f, 0.5f), u * 0.3f, orange, 12); break;
        case ItemId::BombAmmo:
            dl->AddCircleFilled(P(0, 0.2f), u * 0.58f, IM_COL32(35, 40, 55, 255), 20);
            dl->AddCircle(P(0, 0.2f), u * 0.58f, steel, 20, th * 0.7f);
            dl->AddLine(P(0.3f, -0.25f), P(0.55f, -0.55f), wood, th * 1.2f);
            dl->AddCircleFilled(P(0.6f, -0.62f), u * 0.14f, orange, 8);
            break;
        case ItemId::BombchuAmmo:
            dl->AddRectFilled(P(-0.3f, -0.55f), P(0.3f, 0.5f), red, u * 0.3f);
            dl->AddCircleFilled(P(-0.35f, -0.55f), u * 0.22f, white, 10);
            dl->AddCircleFilled(P(0.35f, -0.55f), u * 0.22f, white, 10);
            break;
        case ItemId::NutAmmo: dl->AddCircleFilled(c, u * 0.55f, wood, 18); dl->AddCircle(c, u * 0.55f, IM_COL32(95, 60, 30, 255), 18, th); dl->AddLine(P(0, -0.55f), P(0, -0.8f), green, th * 1.4f); break;
        case ItemId::FairyBow:
            dl->AddBezierQuadratic(P(-0.2f, -0.8f), P(0.95f, 0.0f), P(-0.2f, 0.8f), wood, th * 1.8f);
            dl->AddLine(P(-0.2f, -0.8f), P(-0.2f, 0.8f), white, th * 0.6f);
            dl->AddLine(P(-0.5f, 0.0f), P(0.7f, 0.0f), steel, th * 0.9f);
            break;
        case ItemId::FireArrows: arrow(IM_COL32(255, 120, 40, 255)); break;
        case ItemId::IceArrows: arrow(cyan); break;
        case ItemId::LightArrows: arrow(IM_COL32(255, 245, 150, 255)); break;
        case ItemId::Boomerang:
            dl->AddLine(P(-0.65f, -0.5f), P(0.0f, 0.6f), orange, th * 2.2f);
            dl->AddLine(P(0.0f, 0.6f), P(0.65f, -0.5f), orange, th * 2.2f);
            break;
        case ItemId::Bombs:
            dl->AddCircleFilled(P(0, 0.2f), u * 0.58f, IM_COL32(35, 40, 55, 255), 20);
            dl->AddCircle(P(0, 0.2f), u * 0.58f, steel, 20, th * 0.7f);
            dl->AddLine(P(0.3f, -0.25f), P(0.55f, -0.55f), wood, th * 1.2f);
            dl->AddCircleFilled(P(0.6f, -0.62f), u * 0.14f, orange, 8);
            break;
        case ItemId::Bombchus:
            dl->AddRectFilled(P(-0.3f, -0.55f), P(0.3f, 0.5f), red, u * 0.3f);
            dl->AddCircleFilled(P(-0.35f, -0.55f), u * 0.22f, white, 10);
            dl->AddCircleFilled(P(0.35f, -0.55f), u * 0.22f, white, 10);
            dl->AddLine(P(0, 0.5f), P(0.3f, 0.8f), orange, th);
            break;
        case ItemId::DekuNuts: dl->AddCircleFilled(c, u * 0.55f, wood, 18); dl->AddCircle(c, u * 0.55f, IM_COL32(95, 60, 30, 255), 18, th); dl->AddLine(P(0, -0.55f), P(0, -0.8f), green, th * 1.4f); break;
        case ItemId::DekuShield: shieldShape(wood, IM_COL32(95, 60, 30, 255)); break;
        case ItemId::HylianShield: shieldShape(blue, steel); dl->AddTriangleFilled(P(0, -0.3f), P(-0.25f, 0.2f), P(0.25f, 0.2f), gold); break;
        case ItemId::MirrorShield: shieldShape(steel, gold); dl->AddCircleFilled(P(0, -0.05f), u * 0.25f, red, 12); break;
        case ItemId::GreenPotion: bottle(green, false); break;
        case ItemId::RedPotion: bottle(red, false); break;
        case ItemId::BluePotion: bottle(blue, false); break;
        case ItemId::SmallShieldPotion: bottle(cyan, false); break;
        case ItemId::LargeShieldPotion: bottle(blue, true); dl->AddCircle(P(0, 0.22f), u * 0.34f, white, 16, th * 0.8f); break;
        case ItemId::Milk: bottle(white, false); break;
        case ItemId::Fish: bottle(orange, false); break;
        case ItemId::BlueFire: bottle(IM_COL32(90, 170, 255, 255), false); break;
        case ItemId::Bug: bottle(IM_COL32(140, 190, 60, 255), false); break;
        case ItemId::Poe: bottle(purple, false); break;
        case ItemId::Fairy:
            dl->AddCircleFilled(c, u * 0.22f, white, 12);
            dl->AddCircle(c, u * 0.34f, IM_COL32(255, 200, 255, 255), 14, th * 0.8f);
            dl->AddTriangleFilled(P(-0.1f, -0.1f), P(-0.8f, -0.55f), P(-0.45f, 0.2f), IM_COL32(190, 230, 255, 230));
            dl->AddTriangleFilled(P(0.1f, -0.1f), P(0.8f, -0.55f), P(0.45f, 0.2f), IM_COL32(190, 230, 255, 230));
            break;
        case ItemId::DekuLaunchFlower:   // a Deku Flower seen from above: a ring of pink petals round a green bud
            for (int k = 0; k < 6; k++) {
                const float ang = k * 1.0471976f;
                dl->AddCircleFilled(P(std::cos(ang) * 0.48f, std::sin(ang) * 0.48f), u * 0.3f, IM_COL32(236, 120, 170, 255), 10);
            }
            dl->AddCircleFilled(c, u * 0.3f, IM_COL32(110, 180, 70, 255), 12);
            dl->AddCircleFilled(c, u * 0.14f, gold, 8);
            break;
        case ItemId::RecoveryHeart: heart(red, 1.0f); break;
        case ItemId::HeartPiece: heart(IM_COL32(240, 120, 150, 255), 0.85f); dl->AddLine(P(0, -0.5f), P(0, 0.7f), white, th * 0.7f); break;
        case ItemId::HeartContainer: heart(IM_COL32(255, 70, 110, 255), 1.1f); dl->AddCircle(c, u * 0.9f, gold, 22, th); break;
        case ItemId::MagicJar:
            dl->AddRectFilled(P(-0.42f, -0.35f), P(0.42f, 0.7f), green, 6.0f);
            dl->AddRectFilled(P(-0.5f, -0.6f), P(0.5f, -0.3f), IM_COL32(120, 120, 130, 255), 3.0f);
            break;
        case ItemId::DinsFire: orb(red); break;
        case ItemId::FaroresWind: orb(green); break;
        case ItemId::NayrusLove: orb(blue); break;
        case ItemId::Hookshot: case ItemId::Longshot:
            dl->AddLine(P(-0.7f, 0.7f), P(0.2f, -0.2f), id == ItemId::Longshot ? gold : steel, th * 1.6f);
            dl->AddTriangleFilled(P(0.8f, -0.8f), P(0.25f, -0.45f), P(0.45f, -0.25f), steel);
            dl->AddLine(P(-0.5f, 0.9f), P(-0.9f, 0.5f), wood, th * 1.6f);
            break;
        case ItemId::LensOfTruth:
            dl->AddCircleFilled(c, u * 0.7f, IM_COL32(60, 40, 120, 255), 22);
            dl->AddCircleFilled(c, u * 0.4f, white, 18);
            dl->AddCircleFilled(c, u * 0.2f, dark, 12);
            dl->AddCircle(c, u * 0.7f, gold, 22, th);
            break;
        case ItemId::MagicBeans: dl->AddCircleFilled(P(-0.2f, 0.1f), u * 0.34f, IM_COL32(190, 150, 90, 255), 14); dl->AddCircleFilled(P(0.25f, -0.15f), u * 0.3f, IM_COL32(150, 190, 90, 255), 14); break;
        case ItemId::FairyOcarina: case ItemId::OcarinaOfTime:
            dl->AddCircleFilled(c, u * 0.55f, id == ItemId::OcarinaOfTime ? blue : IM_COL32(110, 160, 220, 255), 20);
            dl->AddRectFilled(P(0.2f, -0.65f), P(0.75f, -0.1f), id == ItemId::OcarinaOfTime ? blue : IM_COL32(110, 160, 220, 255), 3.0f);
            for (int i = 0; i < 3; i++) dl->AddCircleFilled(P(-0.25f + i * 0.25f, 0.05f + (i == 1 ? -0.15f : 0.0f)), u * 0.09f, dark, 8);
            if (id == ItemId::OcarinaOfTime) dl->AddCircle(c, u * 0.55f, gold, 20, th * 0.8f);
            break;
        case ItemId::ZeldasLullaby: note(IM_COL32(240, 150, 200, 255)); break;
        case ItemId::EponasSong: note(orange); break;
        case ItemId::SariasSong: note(green); break;
        case ItemId::SunsSong: note(gold); break;
        case ItemId::SongOfTime: note(cyan); break;
        case ItemId::SongOfStorms: note(IM_COL32(150, 160, 190, 255)); break;
        case ItemId::MinuetOfForest: note(green); break;
        case ItemId::BoleroOfFire: note(red); break;
        case ItemId::SerenadeOfWater: note(blue); break;
        case ItemId::NocturneOfShadow: note(purple); break;
        case ItemId::RequiemOfSpirit: note(orange); break;
        case ItemId::PreludeOfLight: note(IM_COL32(255, 245, 150, 255)); break;
        case ItemId::ShockwaveGrenade: {   // like its model: a silver ball, a faceted purple crystal in a socket, glowing studs round it
            const ImU32 halo = IM_COL32(170, 80, 255, 70), metal = IM_COL32(196, 200, 214, 255), edge = IM_COL32(70, 66, 96, 255);
            dl->AddCircleFilled(c, u * 0.84f, halo, 24);
            for (int k = 0; k < 6; k++) {   // the studs, peeking out round the rim
                const float a = (k + 0.5f) * 1.0471976f;
                const ImVec2 stud = P(std::cos(a) * 0.62f, std::sin(a) * 0.62f);
                dl->AddCircleFilled(stud, u * 0.15f, IM_COL32(170, 172, 190, 255), 10);
                dl->AddCircleFilled(stud, u * 0.075f, IM_COL32(215, 150, 255, 255), 8);
            }
            dl->AddCircleFilled(c, u * 0.6f, metal, 24);
            dl->AddCircleFilled(P(-0.2f, -0.24f), u * 0.2f, IM_COL32(232, 236, 245, 255), 14);       // the shine
            dl->AddCircleFilled(c, u * 0.4f, IM_COL32(150, 152, 170, 255), 16);                        // the socket
            ImVec2 gem[8];
            for (int k = 0; k < 8; k++) { const float a = (k + 0.5f) * 0.7853982f; gem[k] = P(std::cos(a) * 0.31f, std::sin(a) * 0.31f); }
            dl->AddConvexPolyFilled(gem, 8, IM_COL32(124, 64, 226, 255));                               // the crystal
            dl->AddTriangleFilled(gem[4], gem[5], c, IM_COL32(214, 170, 255, 255));                    // its lit facets
            dl->AddTriangleFilled(gem[5], gem[6], c, IM_COL32(176, 108, 255, 255));
            dl->AddCircle(c, u * 0.6f, edge, 24, th * 0.8f);
            break;
        }
        case ItemId::KokiriTunic: tunic(green); break;
        case ItemId::GoronTunic: tunic(red); break;
        case ItemId::ZoraTunic: tunic(blue); break;
        case ItemId::KokiriBoots: boot(wood); break;
        case ItemId::IronBoots: boot(IM_COL32(110, 115, 130, 255)); break;
        case ItemId::HoverBoots: boot(IM_COL32(150, 110, 70, 255)); dl->AddLine(P(-0.3f, 0.85f), P(0.7f, 0.85f), cyan, th * 1.4f); break;
        case ItemId::GoronBracelet: dl->AddCircle(c, u * 0.55f, IM_COL32(180, 130, 70, 255), 20, th * 2.4f); break;
        case ItemId::SilverGauntlets: dl->AddRectFilled(P(-0.55f, -0.45f), P(0.55f, 0.65f), steel, 6.0f); dl->AddRectFilled(P(-0.55f, -0.65f), P(0.55f, -0.35f), IM_COL32(150, 160, 175, 255), 3.0f); break;
        case ItemId::GoldenGauntlets: dl->AddRectFilled(P(-0.55f, -0.45f), P(0.55f, 0.65f), gold, 6.0f); dl->AddRectFilled(P(-0.55f, -0.65f), P(0.55f, -0.35f), IM_COL32(200, 150, 40, 255), 3.0f); break;
        case ItemId::KeatonMask: mask(IM_COL32(230, 170, 70, 255)); break;
        case ItemId::SkullMask: mask(white); break;
        case ItemId::SpookyMask: mask(IM_COL32(210, 90, 60, 255)); break;
        case ItemId::BunnyHood: mask(IM_COL32(245, 235, 240, 255)); dl->AddRectFilled(P(-0.4f, -0.95f), P(-0.2f, -0.4f), white, 4.0f); dl->AddRectFilled(P(0.2f, -0.95f), P(0.4f, -0.4f), white, 4.0f); break;
        case ItemId::GoronMask: mask(IM_COL32(190, 110, 60, 255)); break;
        case ItemId::ZoraMask: mask(IM_COL32(90, 150, 230, 255)); break;
        case ItemId::GerudoMask: mask(IM_COL32(230, 200, 90, 255)); break;
        case ItemId::MaskOfTruth: mask(IM_COL32(120, 90, 190, 255)); dl->AddCircle(P(0, -0.12f), u * 0.18f, gold, 10, th); break;
        case ItemId::SilverScale: dl->AddCircleFilled(c, u * 0.58f, steel, 20); dl->AddCircle(c, u * 0.58f, white, 20, th); break;
        case ItemId::GoldenScale: dl->AddCircleFilled(c, u * 0.58f, gold, 20); dl->AddCircle(c, u * 0.58f, white, 20, th); break;
        case ItemId::BigQuiver: dl->AddRectFilled(P(-0.3f, -0.1f), P(0.3f, 0.8f), wood, 4.0f); dl->AddLine(P(-0.12f, -0.1f), P(-0.12f, -0.8f), steel, th); dl->AddLine(P(0.12f, -0.1f), P(0.12f, -0.8f), steel, th); break;
        case ItemId::BulletBag: case ItemId::BombBag:
            dl->AddCircleFilled(P(0, 0.2f), u * 0.58f, id == ItemId::BombBag ? IM_COL32(60, 80, 140, 255) : IM_COL32(150, 110, 60, 255), 18);
            dl->AddRectFilled(P(-0.25f, -0.55f), P(0.25f, -0.2f), IM_COL32(190, 150, 90, 255), 3.0f);
            break;
        case ItemId::ForestMedallion: medal(green); break;
        case ItemId::FireMedallion: medal(red); break;
        case ItemId::WaterMedallion: medal(blue); break;
        case ItemId::SpiritMedallion: medal(orange); break;
        case ItemId::ShadowMedallion: medal(purple); break;
        case ItemId::LightMedallion: medal(IM_COL32(255, 240, 140, 255)); break;
        case ItemId::KokiriEmerald: gem(green); break;
        case ItemId::GoronRuby: gem(red); break;
        case ItemId::ZoraSapphire: gem(blue); break;
        default: dl->AddCircleFilled(c, u * 0.5f, tier, 16); break;
    }
}

// The bag's stacks, one per kind and tier in the order they were picked up (the server sends one entry per potion, Fortnite-style stacks are built here).
struct PotionStack { royale::ItemId item; royale::Rarity rarity; int count; };
std::vector<PotionStack> PotionStacks(const royale::HudState& h) {
    std::vector<PotionStack> out;
    for (const auto& p : h.inv.potions) {
        const royale::ItemId id = static_cast<royale::ItemId>(p.item);
        const royale::Rarity r = static_cast<royale::Rarity>(p.rarity);
        bool found = false;
        for (auto& st : out) if (st.item == id && st.rarity == r) { st.count++; found = true; break; }
        if (!found) out.push_back({ id, r, 1 });
    }
    return out;
}
int gHandStack = -1;               // the stack held in hand (B drinks one), or -1 for the weapon
royale::ItemId gHandItem{};        // what the stack in hand holds: the bag's order shifts when an earlier slot of the same kind is drunk from
royale::Rarity gHandRarity{};      // (the server takes the first potion of a kind), so the hand follows the kind and tier, not the slot number
void TakeStackInHand(const std::vector<PotionStack>& stacks, int i) {
    gHandStack = i >= 0 && i < static_cast<int>(stacks.size()) ? i : -1;
    if (gHandStack >= 0) { gHandItem = stacks[static_cast<size_t>(i)].item; gHandRarity = stacks[static_cast<size_t>(i)].rarity; }
}
void FollowHandStack(const std::vector<PotionStack>& stacks) {
    if (gHandStack < 0) return;
    for (size_t i = 0; i < stacks.size(); i++)
        if (stacks[i].item == gHandItem && stacks[i].rarity == gHandRarity) { gHandStack = static_cast<int>(i); return; }
    gHandStack = -1;   // that stack is used up
}
bool gStackBPressed = false;       // B was pressed this frame with a stack in hand (taken off the controller so Link doesn't swing)
double gStackBHold = -1.0;         // when C-Down went down: a tap picks the next hotbar slot, a long hold drops the selected one

// The five-slot hotbar (royale::kHotbarSlots), Fortnite style: weapons (the one in hand and the backups) and potion stacks share it; the starter
// sword is the fallback in hand and takes no slot, worn gear and the ability have their own places. The server keeps "in hand + backups" and the
// bag; the slot order is kept here so a slot doesn't jump around when a backup weapon is swapped into hand.
struct HotbarSlot { bool stack; royale::ItemId item; royale::Rarity rarity; int count; };
std::vector<HotbarSlot> gHotbarOrder;
bool IsStarterWeapon(royale::ItemId id, royale::Rarity r) { return id == royale::ItemId::BasicSword && r == royale::Rarity::Common; }
const std::vector<HotbarSlot>& HotbarSlots(const royale::HudState& h) {
    std::vector<HotbarSlot> now;
    if (!IsStarterWeapon(h.weapon, h.weaponRarity)) now.push_back({ false, h.weapon, h.weaponRarity, 1 });
    for (const auto& r : h.inv.reserve) {
        const royale::ItemId id = static_cast<royale::ItemId>(r.item);
        const royale::Rarity ra = static_cast<royale::Rarity>(r.rarity);
        if (!IsStarterWeapon(id, ra)) now.push_back({ false, id, ra, 1 });
    }
    for (const PotionStack& st : PotionStacks(h)) now.push_back({ true, st.item, st.rarity, st.count });
    std::vector<HotbarSlot> kept;
    for (const HotbarSlot& old : gHotbarOrder) {   // keep what is still carried, where it was
        for (size_t i = 0; i < now.size(); i++)
            if (now[i].stack == old.stack && now[i].item == old.item && now[i].rarity == old.rarity) { kept.push_back(now[i]); now.erase(now.begin() + static_cast<long>(i)); break; }
    }
    for (const HotbarSlot& n : now) kept.push_back(n);   // new things go in the first free slots
    if (kept.size() > static_cast<size_t>(royale::kHotbarSlots)) kept.resize(static_cast<size_t>(royale::kHotbarSlots));
    gHotbarOrder = kept;
    return gHotbarOrder;
}
// The selected slot: the stack in hand, else the weapon in hand (-1: only the starter sword).
int HotbarSelected(const royale::HudState& h, const std::vector<HotbarSlot>& slots) {
    for (size_t i = 0; i < slots.size(); i++) {
        if (gHandStack >= 0 ? (slots[i].stack && slots[i].item == gHandItem && slots[i].rarity == gHandRarity)
                            : (!slots[i].stack && slots[i].item == h.weapon && slots[i].rarity == h.weaponRarity)) return static_cast<int>(i);
    }
    return -1;
}
// Select slot i: a stack is taken in hand (B drinks), a weapon is swapped into hand by the server.
void EndDrink();                 // MusicDiagnostics.inc (the local action pose)
float ShieldDrinkProgress();
royale::ItemId ShieldDrinkItem();
void SelectHotbarSlot(const royale::HudState& h, const std::vector<HotbarSlot>& slots, int i) {
    if (i < 0 || i >= static_cast<int>(slots.size())) return;
    const HotbarSlot& sl = slots[static_cast<size_t>(i)];
    if (sl.stack) {
        const std::vector<PotionStack> stacks = PotionStacks(h);
        for (size_t k = 0; k < stacks.size(); k++) if (stacks[k].item == sl.item && stacks[k].rarity == sl.rarity) TakeStackInHand(stacks, static_cast<int>(k));
        return;
    }
    gHandStack = -1;
    if (sl.item == h.weapon && sl.rarity == h.weaponRarity) return;   // already in hand
    EndDrink();
    for (size_t k = 0; k < h.inv.reserve.size(); k++)
        if (static_cast<royale::ItemId>(h.inv.reserve[k].item) == sl.item && static_cast<royale::Rarity>(h.inv.reserve[k].rarity) == sl.rarity) { gSession.SelectWeapon(static_cast<int>(k) + 1); return; }
}
// What a pickup with a full hotbar swaps out: the selected stack, or the weapon in hand.
void HotbarReplaceTarget(uint8_t* item, uint8_t* rarity) {
    *item = gHandStack >= 0 ? static_cast<uint8_t>(gHandItem) : royale::kHandSlot;
    *rarity = gHandStack >= 0 ? static_cast<uint8_t>(gHandRarity) : 0;
}

// A round OoT item button (like the C buttons of the real game): drop shadow, panel fill, a coloured rim and a soft highlight.
void HudRoundButton(ImDrawList* dl, ImVec2 c, float r, ImU32 rim, bool lit, float scale) {
    using namespace royale::wartable::theme;
    dl->AddCircleFilled(ImVec2(c.x + 2 * scale, c.y + 3 * scale), r, IM_COL32(6, 15, 36, 192), 32);
    dl->AddCircleFilled(c, r, HudColor(lit ? Button : Panel, 237), 32);
    dl->AddCircle(c, r, rim, 32, (lit ? 3.0f : 2.0f) * scale);
    dl->PathArcTo(c, r - 5 * scale, 3.14159265f * 1.05f, 3.14159265f * 1.45f, 14);
    dl->PathStroke(IM_COL32(255, 255, 255, 45), 0, 2 * scale);
}

// The right-hand column of the HUD, bottom up: the weapon button, its name and ammo count, the other ammo kinds one per row, and the touch
// EMOTE and JUMP buttons stacked above that. All in 720p units times scale, measured from the bottom of the screen.
int HudAmmoRows(const royale::HudState& h) {
    int n = 0;
    for (int k = 0; k < royale::kAmmoKinds; k++) if (h.ammo[static_cast<size_t>(k)] > 0) n++;
    return n;
}
float HudAmmoRowY(ImVec2 ds, float scale, int row) { return ds.y - 172.0f * scale - row * 28.0f * scale; }   // centre of the row (0 is the lowest)
// Centre of a touch button on the right edge (0 EMOTE, 1 JUMP), kept clear of the ammo column; `radius` gets the button's size.
ImVec2 HudTouchButton(ImVec2 ds, float scale, const royale::HudState& h, int index, float* radius) {
    const int n = HudAmmoRows(h);
    const float r = 28.0f * scale, top = n > 0 ? HudAmmoRowY(ds, scale, n - 1) - 16.0f * scale : ds.y - 158.0f * scale;
    if (radius) *radius = r;
    return ImVec2(ds.x - 20.0f * scale - r, top - 8.0f * scale - r - index * (2 * r + 8.0f * scale));
}

// The item bar, in Zelda's round buttons and Fortnite's order: bottom right, right to left. The weapon in hand is the big B button with its
// ammo count and name above it, then the worn shield, the five usable hotbar slots (weapons and potion stacks; D-pad Left/Right selects, B
// uses, hold C-Down drops) and the ability on its own button (D-pad Up, or C-Up in the Battle Royale layout), then the rupee counter. Gear icons you wear are at the bottom left. On a touch screen tap a slot.
void DrawHotbar(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const royale::HudState& h) {
    using royale::wartable::theme::Muted;
    using G = royale::controls::Glyph;
    const float S = scale, bottom = ds.y - 20.0f * S, right = ds.x - 20.0f * S;
    const ImU32 grey = HudColor(Muted, 150), ink = IM_COL32(244, 233, 202, 255);
    const ImU32 nB = IM_COL32(60, 175, 85, 255), nC = IM_COL32(245, 205, 40, 255), nD = IM_COL32(150, 150, 160, 255);
    const royale::controls::Prompt pr = royale::controls::PromptsFor(ControlScheme());
    auto ammoOf = [&](royale::ItemId id) { const royale::AmmoKind k = royale::AmmoUsedBy(id); return k == royale::AmmoKind::None ? -1 : h.ammo[static_cast<size_t>(k)]; };
    const std::vector<PotionStack> stacks = PotionStacks(h);
    FollowHandStack(stacks);
    const bool stackInHand = gHandStack >= 0;
    ImGuiIO& io = ImGui::GetIO();
    const bool tap = ImGui::IsMouseClicked(0) && !io.WantCaptureMouse;
    auto hitRound = [&](ImVec2 c, float r) { return tap && std::hypot(io.MousePos.x - c.x, io.MousePos.y - c.y) <= r; };
    auto hitRect = [&](ImVec2 a, ImVec2 b) { return tap && io.MousePos.x >= a.x && io.MousePos.x <= b.x && io.MousePos.y >= a.y && io.MousePos.y <= b.y; };
    // Drinking a shield potion: a ring fills above Link in the shield bar's Hylian blue, like a bottle being emptied.
    if (const float drink = ShieldDrinkProgress(); drink >= 0.0f && h.selfAlive) {
        const ImVec2 c(ds.x * 0.5f, ds.y * 0.34f);   // above Link's head, clear of him and of the crosshair
        const float r = 26.0f * S, a0 = -IM_PI * 0.5f;
        dl->AddCircleFilled(c, r + 5.0f * S, IM_COL32(12, 20, 40, 170), 40);
        dl->AddCircle(c, r, IM_COL32(85, 165, 245, 70), 40, 5.0f * S);
        dl->PathArcTo(c, r, a0, a0 + IM_PI * 2.0f * drink, 40);
        dl->PathStroke(IM_COL32(85, 165, 245, 255), ImDrawFlags_None, 5.0f * S);
        const char* label = ShieldDrinkItem() == royale::ItemId::LargeShieldPotion ? "Large Shield Potion" : "Small Shield Potion";
        const ImVec2 ts = font->CalcTextSizeA(15.0f * S, FLT_MAX, 0.0f, label);
        dl->AddText(font, 15.0f * S, ImVec2(c.x - ts.x * 0.5f + 1.5f * S, c.y + r + 9.0f * S + 1.5f * S), IM_COL32(0, 0, 0, 220), label);
        dl->AddText(font, 15.0f * S, ImVec2(c.x - ts.x * 0.5f, c.y + r + 9.0f * S), IM_COL32(200, 228, 255, 255), label);
    }
    auto shadowed = [&](ImVec2 p, float size, ImU32 col, const char* t) {
        dl->AddText(font, size, ImVec2(p.x + 1.5f, p.y + 1.5f), IM_COL32(0, 0, 0, 230), t);
        dl->AddText(font, size, p, col, t);
    };
    auto rightText = [&](float xr, float y, float size, ImU32 col, const char* t) {
        shadowed(ImVec2(xr - font->CalcTextSizeA(size, FLT_MAX, 0.0f, t).x, y), size, col, t);
    };
    auto centreText = [&](ImVec2 c, float size, ImU32 col, const char* t) {
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, t);
        shadowed(ImVec2(c.x - sz.x * 0.5f, c.y - sz.y * 0.5f), size, col, t);
    };
    auto rarityArc = [&](ImVec2 c, float r, ImU32 col) {   // along the bottom of a round button
        dl->PathArcTo(c, r - 3 * S, 3.14159265f * 0.17f, 3.14159265f * 0.83f, 16);
        dl->PathStroke(col, 0, 4 * S);
    };
    auto rarityBar = [&](ImVec2 a, ImVec2 b, ImU32 col) { dl->AddRectFilled(ImVec2(a.x + 4 * S, b.y - 4 * S), ImVec2(b.x - 4 * S, b.y - 2 * S), col); };

    // The weapon in hand: big, with the B button on its rim.
    const float wr = 38.0f * S;
    const ImVec2 wc(right - wr, bottom - wr);
    HudRoundButton(dl, wc, wr, stackInHand ? IM_COL32(60, 175, 85, 110) : nB, !stackInHand, S);
    DrawItemIcon(dl, h.weapon, wc, wr * 1.2f, RarityU32(h.weaponRarity));
    rarityArc(wc, wr, RarityU32(h.weaponRarity));
    DrawPadGlyph(dl, font, ImVec2(wc.x - wr * 0.72f, wc.y + wr * 0.72f), 8.0f * S, pr.weapon);
    if (hitRound(wc, wr) && stackInHand) gHandStack = -1;   // tap the weapon to put the potion away
    {   // ammo in the weapon and its name above it, both right-aligned; the other ammo above those, one row per kind
        const float nameY = wc.y - wr - 30.0f * S;
        std::string name = ShortName(h.weapon);
        for (char& ch : name) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        rightText(right, nameY, 14.0f * S, RarityU32(h.weaponRarity), name.c_str());
        if (const int n = ammoOf(h.weapon); n >= 0) rightText(right, nameY - 28.0f * S, 24.0f * S, n > 0 ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 90, 90, 255), std::to_string(n).c_str());
        int row = 0;
        for (int k = 0; k < royale::kAmmoKinds; k++) {
            const int n = h.ammo[static_cast<size_t>(k)];
            if (n <= 0) continue;
            const float y = HudAmmoRowY(ds, S, row++);
            const std::string t = "x " + std::to_string(n);
            rightText(right, y - 8.5f * S, 16.0f * S, ink, t.c_str());
            DrawItemIcon(dl, royale::AmmoItem(static_cast<royale::AmmoKind>(k)), ImVec2(right - 46.0f * S, y), 24.0f * S, IM_COL32(220, 230, 240, 255));
        }
    }

    // The shield you wear: a round button, display only (Z raises it). It is an upgrade, not a hotbar slot.
    const float r2 = 28.0f * S, gap = 6.0f * S;
    float x = wc.x - wr - gap;
    {
        const ImVec2 c(x - r2, bottom - r2);
        const ImU32 col = h.hasShield ? RarityU32(h.shieldRarity) : grey;
        HudRoundButton(dl, c, r2, h.hasShield ? nC : IM_COL32(245, 205, 40, 110), false, S);
        if (h.hasShield) {
            DrawItemIcon(dl, h.shield, c, r2 * 1.3f, col);
            rarityArc(c, r2, col);
            if (pr.shield != G::None) DrawPadGlyph(dl, font, ImVec2(c.x - r2 * 0.72f, c.y + r2 * 0.72f), 7.0f * S, pr.shield);
        }
        x = c.x - r2 - gap;
    }

    // The five usable slots (royale::kHotbarSlots), right to left in pickup order: weapons and potion stacks alike. The selected one is lit and
    // shows B; D-pad Left/Right (glyph above the row) moves the selection, holding C-Down drops it. Tap a slot to select it, tap again to drink.
    {
        const std::vector<HotbarSlot>& slots = HotbarSlots(h);
        const int sel = HotbarSelected(h, slots);
        const float ps = 50.0f * S, pgap = 5.0f * S, rowRight = x;
        for (int i = 0; i < royale::kHotbarSlots; i++) {
            const ImVec2 a(x - ps, bottom - ps), b(x, bottom);
            const bool filled = i < static_cast<int>(slots.size());
            HudFrame(dl, a, b, S, sel == i);
            if (filled) {
                const HotbarSlot& sl = slots[static_cast<size_t>(i)];
                const ImU32 col = RarityU32(sl.rarity);
                DrawItemIcon(dl, sl.item, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f + 1 * S), ps * 0.72f, col);
                rarityBar(a, b, col);
                if (sl.stack) {
                    const std::string cnt = "x" + std::to_string(sl.count);
                    rightText(b.x - 4 * S, a.y + 3 * S, 12.0f * S, ink, cnt.c_str());
                } else if (const int n = ammoOf(sl.item); n >= 0) {
                    rightText(b.x - 4 * S, a.y + 3 * S, 12.0f * S, n > 0 ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 90, 90, 255), std::to_string(n).c_str());
                }
                if (sel == i) DrawPadGlyph(dl, font, ImVec2(a.x + 8 * S, a.y + 8 * S), 6.5f * S, pr.weapon);
                if (hitRect(a, b)) {
                    if (sel == i && sl.stack) gSession.UseStack(static_cast<uint8_t>(sl.item), static_cast<uint8_t>(sl.rarity));
                    else SelectHotbarSlot(h, slots, i);
                }
            } else rarityBar(a, b, grey);
            x = a.x - pgap;
        }
        if (!slots.empty()) DrawPadGlyph(dl, font, ImVec2((x + pgap + rowRight) * 0.5f, bottom - ps - 10.0f * S), 7.0f * S, pr.swap);
    }

    // The ability: a round D-pad Up button with a pie cooldown.
    x -= 2 * S;
    {
        const ImVec2 c(x - r2, bottom - r2);
        const bool has = h.inv.hasAbility;
        const royale::ItemId id = static_cast<royale::ItemId>(h.inv.ability.item);
        const ImU32 col = has ? RarityU32(static_cast<royale::Rarity>(h.inv.ability.rarity)) : grey;
        HudRoundButton(dl, c, r2, has ? nD : IM_COL32(150, 150, 160, 110), false, S);
        if (has) {
            const float cd = royale::AbilityOf(id).cooldown;
            const bool lowMagic = h.magic + 0.001f < royale::AbilityMagic(id);
            DrawItemIcon(dl, id, c, r2 * 1.3f, col);
            rarityArc(c, r2, col);
            const float left = cd > 0 ? std::min(1.0f, h.abilityReadyIn / cd) : 0.0f;
            if (left > 0.0f) {   // a dark pie, clockwise from the top, for the time still to wait
                const int steps = std::max(2, static_cast<int>(40 * left));
                for (int s = 0; s < steps; s++) {
                    const float t0 = -1.5707963f + 6.2831853f * left * s / steps, t1 = -1.5707963f + 6.2831853f * left * (s + 1) / steps;
                    dl->AddTriangleFilled(c, ImVec2(c.x + std::cos(t0) * r2, c.y + std::sin(t0) * r2), ImVec2(c.x + std::cos(t1) * r2, c.y + std::sin(t1) * r2), IM_COL32(0, 0, 0, 150));
                }
            }
            if (h.abilityReadyIn > 0.05f) centreText(ImVec2(c.x, c.y - r2 - 9 * S), 12.0f * S, ink, ClockText(h.abilityReadyIn).c_str());
            else if (lowMagic) centreText(ImVec2(c.x, c.y - r2 - 9 * S), 12.0f * S, IM_COL32(255, 120, 110, 255), "NO MAGIC");
            DrawPadGlyph(dl, font, ImVec2(c.x - r2 * 0.72f, c.y + r2 * 0.72f), 7.0f * S, pr.ability);
            if (hitRound(c, r2) && h.abilityReadyIn <= 0.05f && !lowMagic) gSession.UseAbility();
        }
        x = c.x - r2 - 12 * S;
    }

    // Money, Wind Waker style: a rupee and four digits (the wallet holds 1000).
    {
        char n[8];
        std::snprintf(n, sizeof(n), "%04d", std::clamp(h.rupees, 0, 9999));
        const float ts = 24.0f * S;
        const ImVec2 nsz = font->CalcTextSizeA(ts, FLT_MAX, 0.0f, n);
        const float cy = bottom - 22.0f * S;
        shadowed(ImVec2(x - nsz.x, cy - nsz.y * 0.5f), ts, IM_COL32(250, 215, 90, 255), n);
        DrawItemIcon(dl, royale::ItemId::Rupees, ImVec2(x - nsz.x - 16.0f * S, cy), 28.0f * S, IM_COL32(110, 240, 130, 255));
    }

    // What you wear: one small icon per gear slot at the bottom left (tunic, boots, gauntlets, mask, scale, pack, charm).
    float gx = 18.0f * S;
    const float gs = 28.0f * S, gy = ds.y - gs - 20.0f * S;
    for (int i = 0; i < royale::kGearSlots; i++) {
        if (!(h.inv.gearMask & (1 << i))) continue;
        const royale::ItemId id = static_cast<royale::ItemId>(h.inv.gear[i].item);
        const ImU32 col = RarityU32(static_cast<royale::Rarity>(h.inv.gear[i].rarity));
        const ImVec2 a(gx, gy), b(gx + gs, gy + gs);
        HudFrame(dl, a, b, S);
        DrawItemIcon(dl, id, ImVec2(gx + gs * 0.5f, gy + gs * 0.5f), gs * 0.72f, col);
        rarityBar(a, b, col);
        gx += gs + 4.0f * S;
    }
}

// ---- the emote wheel ---------------------------------------------------------------------------------------------------------------
// Hold C-Right and the wheel comes up in the middle of the screen: point the stick at an emote and let go of C-Right to do it (or press A).
// A quick tap of C-Right does the last one again; B closes it. By touch or mouse, tap EMOTE (bottom right), then tap an emote.
// While it is up the stick picks an emote instead of moving Link. It is drawn in the look of the game's pause menu: the text-box black,
// gold rules and the pulsing corner cursor.
struct EmoteWheel {
    bool open = false;
    bool byTouch = false;   // opened with the EMOTE button: stays up until an emote (or anywhere else) is tapped
    double openedAt = 0;
    int hover = -1;         // the emote pointed at, -1 for none yet
};
EmoteWheel gWheel;
int gLastEmote = 0;         // what a quick tap of C-Right does again
void StartEmote(int index, const royale::HudState& hud); // below, with the emote logic
bool CanEmote(const royale::HudState& hud);

void OpenEmoteWheel(bool byTouch) {
    gWheel = EmoteWheel{};
    gWheel.open = true;
    gWheel.byTouch = byTouch;
    gWheel.openedAt = ImGui::GetTime();
    Audio_PlaySoundGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}
void CloseEmoteWheel() { gWheel = EmoteWheel{}; }

// Which emote a direction points at: x to the right, y up. The emotes go clockwise from the top.
int WheelSliceAt(float x, float y) {
    float turns = std::atan2(x, y) / 6.2831853f;
    if (turns < 0.0f) turns += 1.0f;
    return static_cast<int>(turns * royale::kEmoteCount + 0.5f) % royale::kEmoteCount;
}

void WheelText(ImDrawList* dl, ImFont* font, float size, ImVec2 centre, ImU32 col, const char* text) {
    const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0, text);
    const ImVec2 at(centre.x - sz.x * 0.5f, centre.y - sz.y * 0.5f);
    const float o = std::max(1.5f, size * 0.06f);
    for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) if (dx || dy) dl->AddText(font, size, ImVec2(at.x + dx * o, at.y + dy * o), IM_COL32(20, 10, 0, 235), text);
    dl->AddText(font, size, at, col, text);
}

void DrawEmotes(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const royale::HudState& h) {
    if (!h.selfAlive || !InField()) { if (gWheel.open) CloseEmoteWheel(); return; }
    ImGuiIO& io = ImGui::GetIO();
    const bool tap = ImGui::IsMouseClicked(0) && !io.WantCaptureMouse;
    const ImU32 gold = IM_COL32(255, 214, 90, 255), goldDark = IM_COL32(176, 118, 24, 255);
    const ImU32 lit = IM_COL32(255, 236, 120, 255);
    bool used = false;   // a tap the wheel has dealt with

    // The EMOTE button: a round C-Right button on the right edge, above the ammo column (JUMP is stacked above it).
    {
        float rad;
        const ImVec2 c = HudTouchButton(ds, scale, h, 0, &rad);
        const bool over = std::hypot(io.MousePos.x - c.x, io.MousePos.y - c.y) <= rad;
        const bool down = over && ImGui::IsMouseDown(0) && !io.WantCaptureMouse;
        HudRoundButton(dl, c, rad, gWheel.open ? lit : goldDark, gWheel.open || down, scale);
        DrawPadGlyph(dl, font, ImVec2(c.x, c.y - 7.0f * scale), 12.0f * scale, royale::controls::PromptsFor(ControlScheme()).emote);
        WheelText(dl, font, 11.0f * scale, ImVec2(c.x, c.y + 14.0f * scale), IM_COL32(255, 255, 255, 255), "EMOTE");
        if (tap && over) {
            used = true;
            if (gWheel.open) CloseEmoteWheel();
            else if (CanEmote(h)) OpenEmoteWheel(true);
        }
    }
    if (!gWheel.open) return;

    const ImVec2 c(ds.x * 0.5f, ds.y * 0.5f);
    const float outer = 168.0f * scale, inner = 58.0f * scale, mid = (outer + inner) * 0.5f;
    const int n = royale::kEmoteCount;
    const float t = static_cast<float>(ImGui::GetTime() - gWheel.openedAt);
    const float grow = std::min(1.0f, t / 0.12f);   // pops open
    const float R = outer * (0.85f + 0.15f * grow), r = inner;

    // The mouse or a finger points at an emote too.
    const float mx = io.MousePos.x - c.x, my = io.MousePos.y - c.y, md = std::sqrt(mx * mx + my * my);
    if (gWheel.byTouch && md > r && md < R + 30.0f * scale) gWheel.hover = WheelSliceAt(mx, -my);

    // The ring: the text-box black, a gold rule round each edge, and spokes between the emotes.
    dl->AddCircleFilled(ImVec2(c.x + 5 * scale, c.y + 6 * scale), R, IM_COL32(0, 0, 0, 70), 64);
    for (int i = 0; i < n; i++) {
        const float a0 = (i - 0.5f) / n * 6.2831853f - 1.5707963f, a1 = (i + 0.5f) / n * 6.2831853f - 1.5707963f;
        dl->PathClear();
        dl->PathArcTo(c, R, a0, a1, 24);
        dl->PathArcTo(c, r, a1, a0, 12);
        dl->PathFillConvex(i == gWheel.hover ? IM_COL32(70, 52, 20, 235) : OotPanel(215));
    }
    dl->AddCircle(c, R, goldDark, 64, 2.0f * scale);
    dl->AddCircle(c, R - 5 * scale, (gold & 0x00FFFFFF) | 0x8C000000, 64, 1.0f * scale);
    dl->AddCircle(c, r, goldDark, 48, 2.0f * scale);
    for (int i = 0; i < n; i++) {
        const float a = (i + 0.5f) / n * 6.2831853f - 1.5707963f;
        dl->AddLine(ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r), ImVec2(c.x + std::cos(a) * (R - 5 * scale), c.y + std::sin(a) * (R - 5 * scale)),
                    (goldDark & 0x00FFFFFF) | 0xB4000000, 1.5f * scale);
    }
    // The names, and the pulsing corner cursor round the one pointed at.
    for (int i = 0; i < n; i++) {
        const float a = static_cast<float>(i) / n * 6.2831853f - 1.5707963f;
        const ImVec2 p(c.x + std::cos(a) * mid, c.y + std::sin(a) * mid);
        const bool sel = i == gWheel.hover;
        // Two short lines at most: the names are split at the first space when they are long.
        std::string name = royale::kEmoteNames[i];
        const size_t space = name.find(' ');
        const float size = 15.0f * scale;
        if (name.size() > 9 && space != std::string::npos) {
            WheelText(dl, font, size, ImVec2(p.x, p.y - size * 0.55f), sel ? IM_COL32(255, 255, 150, 255) : IM_COL32(236, 228, 190, 255), name.substr(0, space).c_str());
            WheelText(dl, font, size, ImVec2(p.x, p.y + size * 0.55f), sel ? IM_COL32(255, 255, 150, 255) : IM_COL32(236, 228, 190, 255), name.substr(space + 1).c_str());
        } else {
            WheelText(dl, font, size, p, sel ? IM_COL32(255, 255, 150, 255) : IM_COL32(236, 228, 190, 255), name.c_str());
        }
        if (sel) {
            const float pulse = 0.5f + 0.5f * std::sin(t * 6.0f);
            const float hw = 44.0f * scale + 3.0f * scale * pulse, hh = 24.0f * scale + 3.0f * scale * pulse, len = 10.0f * scale, w = 3.0f * scale;
            const ImU32 col = (lit & 0x00FFFFFF) | (static_cast<ImU32>(255 * (0.75f + 0.25f * pulse)) << 24);
            const ImVec2 q0(p.x - hw, p.y - hh), q1(p.x + hw, p.y + hh);
            dl->AddLine(q0, ImVec2(q0.x + len, q0.y), col, w); dl->AddLine(q0, ImVec2(q0.x, q0.y + len), col, w);
            dl->AddLine(ImVec2(q1.x, q0.y), ImVec2(q1.x - len, q0.y), col, w); dl->AddLine(ImVec2(q1.x, q0.y), ImVec2(q1.x, q0.y + len), col, w);
            dl->AddLine(ImVec2(q0.x, q1.y), ImVec2(q0.x + len, q1.y), col, w); dl->AddLine(ImVec2(q0.x, q1.y), ImVec2(q0.x, q1.y - len), col, w);
            dl->AddLine(q1, ImVec2(q1.x - len, q1.y), col, w); dl->AddLine(q1, ImVec2(q1.x, q1.y - len), col, w);
        }
    }
    // The middle: what will happen, and how.
    dl->AddCircleFilled(c, r - 3 * scale, OotPanel(230), 48);
    WheelText(dl, font, 17.0f * scale, ImVec2(c.x, c.y - 9 * scale), gold, "EMOTE");
    WheelText(dl, font, 12.0f * scale, ImVec2(c.x, c.y + 11 * scale), IM_COL32(236, 228, 190, 255), gWheel.byTouch ? "tap one" : "B: close");

    if (tap && !used) {
        if (md > r && md < R + 30.0f * scale) {
            const int pick = WheelSliceAt(mx, -my);
            CloseEmoteWheel();
            StartEmote(pick, h);
        } else {
            CloseEmoteWheel();   // a tap anywhere else puts it away
        }
    }
}

// The replay of the match that just ended, as a top-down map at high speed (on the end screen, see DrawEndScreen): the storm closing in, everybody's
// dots, trails for you and the winner, red rings where somebody was eliminated. It loops. `a` is the top left of the square map, `side` its size;
// the title goes just above it and the clock just below.
void DrawReplay(ImDrawList* dl, ImFont* font, ImVec2 a, float side, float scale, const royale::HudState& h) {
    royale::GameClient* client = gSession.Client();
    if (client == nullptr || !client->ReplayComplete() || h.map.radius <= 0 || side < 60.0f * scale) return;
    const royale::Replay& rp = client->GetReplay();
    if (rp.frames.empty()) return;
    const ImVec2 b(a.x + side, a.y + side);
    dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 90), 6.0f * scale);
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    const float k = side * 0.5f / h.map.radius * 0.97f;
    auto toPanel = [&](float x, float z) { return ImVec2(c.x + (x - h.map.center.x) * k, c.y - (z - h.map.center.z) * k); };
    const double cycle = rp.frames.size() * 0.12 + 2.5;
    const double t = std::fmod(ImGui::GetTime(), cycle);
    const float fpos = static_cast<float>(std::min(t / 0.12, static_cast<double>(rp.frames.size() - 1)));
    const int f0 = static_cast<int>(fpos);
    const int f1 = std::min<int>(f0 + 1, static_cast<int>(rp.frames.size()) - 1);
    const float mix = fpos - f0;
    dl->AddText(font, 20.0f * scale, ImVec2(a.x, a.y - 27 * scale), IM_COL32(255, 210, 70, 255), "REPLAY");
    // the map, the storm (purple outside the safe circle) and the safe circle
    dl->AddCircleFilled(c, h.map.radius * k, IM_COL32(110, 50, 170, 150), 72);
    if (const royale::Storm* storm = client->GetStorm()) {
        const royale::Circle safe = storm->SafeZoneAt(f0 * royale::kReplayStepSec);
        dl->AddCircleFilled(toPanel(safe.center.x, safe.center.z), safe.radius * k, IM_COL32(26, 52, 40, 255), 64);
        dl->AddCircle(toPanel(safe.center.x, safe.center.z), safe.radius * k, IM_COL32(255, 255, 255, 230), 64, 1.5f * scale);
    }
    dl->AddCircle(c, h.map.radius * k, IM_COL32(200, 160, 255, 255), 72, 1.5f * scale);
    int alive = 0;
    for (size_t i = 0; i < rp.ids.size(); i++) {
        const int16_t x0 = rp.frames[static_cast<size_t>(f0)][i * 2], x1 = rp.frames[static_cast<size_t>(f1)][i * 2];
        if (x0 == royale::kReplayGone) continue;
        alive++;
        const float z0 = rp.frames[static_cast<size_t>(f0)][i * 2 + 1], z1 = rp.frames[static_cast<size_t>(f1)][i * 2 + 1];
        const float px = x1 == royale::kReplayGone ? x0 : x0 + (x1 - x0) * mix, pz = x1 == royale::kReplayGone ? z0 : z0 + (z1 - z0) * mix;
        const bool self = rp.ids[i] == h.selfId, winner = rp.ids[i] == h.winnerId;
        if (self || winner) {   // a trail behind you and the winner
            ImVec2 prev = toPanel(px, pz);
            for (int back = 1; back <= 10 && f0 - back >= 0; back++) {
                const auto& fr = rp.frames[static_cast<size_t>(f0 - back)];
                if (fr[i * 2] == royale::kReplayGone) break;
                const ImVec2 q = toPanel(fr[i * 2], fr[i * 2 + 1]);
                dl->AddLine(prev, q, self ? IM_COL32(255, 220, 90, 190 - back * 15) : IM_COL32(120, 255, 150, 190 - back * 15), 2.0f * scale);
                prev = q;
            }
        }
        const ImU32 col = self ? IM_COL32(255, 220, 90, 255) : winner ? IM_COL32(120, 255, 150, 255) : rp.ids[i] >= 1000 ? IM_COL32(190, 190, 205, 255) : IM_COL32(255, 255, 255, 255);
        dl->AddCircleFilled(toPanel(px, pz), (self || winner ? 4.5f : 3.0f) * scale, col, 10);
    }
    // eliminations: a red ring that grows and fades where the player was last seen
    for (const royale::ReplayKill& kill : rp.kills) {
        const float age = fpos - kill.frame;
        if (age < 0.0f || age > 6.0f) continue;
        size_t col = rp.ids.size();
        for (size_t i = 0; i < rp.ids.size(); i++) if (rp.ids[i] == kill.victim) col = i;
        if (col == rp.ids.size()) continue;
        const int fr = std::max(0, static_cast<int>(kill.frame) - 1);
        const ImVec2 at = toPanel(rp.frames[static_cast<size_t>(fr)][col * 2], rp.frames[static_cast<size_t>(fr)][col * 2 + 1]);
        dl->AddCircle(at, (5.0f + age * 3.5f) * scale, IM_COL32(255, 70, 60, static_cast<int>(255 * (1.0f - age / 6.0f))), 16, 2.0f * scale);
    }
    char caption[48];
    const int secs = static_cast<int>(f0 * royale::kReplayStepSec);
    std::snprintf(caption, sizeof(caption), "%d:%02d    %d alive", secs / 60, secs % 60, alive);
    dl->AddText(font, 17.0f * scale, ImVec2(a.x, b.y + 5 * scale), IM_COL32(235, 235, 240, 255), caption);
}

// The splash shown at the start of every match's countdown, in the look of OoT's title and file-select screens: deep blue-green
// night, gold lettering with a dark drop shadow, a gold Triforce and a double gold border. It covers the screen for the first
// kSplashSeconds of the countdown (fading in and out), which is also when everyone is being moved to Hyrule Field.
constexpr float kSplashSeconds = 5.0f;

