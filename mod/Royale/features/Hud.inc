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

// A thin bar under the magic meter, the width of the hearts, like the shield and magic bars above it. No text. It only shows while you are using
// stamina and fades out a moment after it fills, so it stays off the screen the rest of the time.
void DrawStaminaBar(ImDrawList* dl, ImVec2 ds, const royale::HudState& h) {
    if (!LiveAndAlive(h) || !InField()) return;
    const double sinceFull = gStamina >= 1.0f ? ImGui::GetTime() - gStaminaFullAt : 0.0;
    const float alpha = static_cast<float>(std::clamp(1.0 - (sinceFull - 0.8) / 0.4, 0.0, 1.0));
    if (alpha <= 0.0f) return;
    auto a = [&](int v) { return static_cast<int>(v * alpha); };
    const float unit = ds.y / 240.0f;   // the game's own HUD is laid out on a 240 high screen
    const float bx = 34.0f * unit / 3.0f, by = 146.0f * unit / 3.0f, bw = 260.0f * unit / 3.0f, bh = 4.0f * unit;
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

// Town names hanging over each point of interest, big enough to read from the sky while you skydive.
void DrawPoiLabels(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const royale::HudState& h) {
    if (!gSession.Client() || !InField()) return;
    (void)h;
    Player* pl = GET_PLAYER(gPlayState);
    const float reach = gSkydiving ? 14000.0f : 5000.0f;
    for (const royale::Poi& p : gSession.Client()->Pois()) {
        const float dx = p.center.x - pl->actor.world.pos.x, dz = p.center.z - pl->actor.world.pos.z;
        const float d = std::sqrt(dx * dx + dz * dz);
        if (d > reach) continue;
        float y = pl->actor.world.pos.y;
        FloorAt(p.center.x, p.center.z, &y);
        ImVec2 at;
        if (!WorldToScreen(p.center.x, y + 650.0f, p.center.z, &at)) continue;
        const int alpha = static_cast<int>(255.0f * std::min(1.0f, (reach - d) / (reach * 0.4f)));
        const float size = std::clamp(34.0f * scale * (2400.0f / (d + 1200.0f)), 15.0f * scale, 38.0f * scale);
        const char* name = royale::kPoiNames[p.name];
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, name);
        const ImVec2 pos(at.x - sz.x * 0.5f, at.y - sz.y * 0.5f);
        dl->AddText(font, size, ImVec2(pos.x + 2.0f, pos.y + 2.0f), IM_COL32(10, 14, 8, alpha), name);
        dl->AddText(font, size, pos, IM_COL32(255, 222, 110, alpha), name);
    }
}

bool MapOption(const char* name, bool fallback = true) {
    char key[64];
    std::snprintf(key, sizeof(key), CVAR_SETTING("Royale.%s"), name);
    return CVarGetInteger(key, fallback ? 1 : 0) != 0;
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

// Bottom left: a minimap that follows you. It shows the area around you on the game's own minimap for the scene, north up, with you in the
// middle as the game's yellow arrow: the storm in purple, the safe zone's edge, supply drops, mini bosses and other players. While you skydive
// it zooms out to show the whole map, so you can pick where to land.
void DrawMinimap(ImDrawList* dl, ImVec2 ds, float scale, const royale::HudState& h) {
    if (h.map.radius <= 0 || !InField()) return;
    Player* pl = GET_PLAYER(gPlayState);
    const float half = 86.0f * scale;
    const ImVec2 c(half + 22.0f * scale, ds.y - half - 30.0f * scale);
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
        if (tex != nullptr) dl->AddImage(tex, toMap(-royale::fortnite::kHalfX, -royale::fortnite::kHalfZ), toMap(royale::fortnite::kHalfX, royale::fortnite::kHalfZ), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 210));
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
    dl->AddCircle(zc, zr, IM_COL32(255, 255, 255, 235), 96, 2.0f * scale);

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
    for (const auto& st : gSession.Puppets()) {
        if (!st.alive || !MapOption(st.isBot ? "MapBots" : "MapPlayers")) continue;
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
        case ItemId::ShockwaveGrenade: {   // a grey ball with glowing purple bands, like its model
            const ImU32 glow = IM_COL32(200, 120, 255, 255), halo = IM_COL32(170, 80, 255, 70);
            dl->AddCircleFilled(c, u * 0.78f, halo, 24);
            dl->AddCircleFilled(c, u * 0.62f, IM_COL32(150, 152, 162, 255), 24);
            dl->AddCircleFilled(P(-0.18f, -0.2f), u * 0.24f, IM_COL32(190, 192, 200, 255), 14);       // the shine
            dl->AddLine(P(-0.62f, 0), P(0.62f, 0), glow, th * 1.6f);                                    // the band round the middle
            dl->AddBezierQuadratic(P(0, -0.62f), P(0.42f, 0), P(0, 0.62f), glow, th * 1.4f, 12);       // and the ones over the top
            dl->AddBezierQuadratic(P(0, -0.62f), P(-0.42f, 0), P(0, 0.62f), glow, th * 1.4f, 12);
            dl->AddCircleFilled(P(0, -0.62f), u * 0.12f, IM_COL32(70, 72, 82, 255), 10);              // the cap
            dl->AddCircle(c, u * 0.62f, IM_COL32(60, 62, 72, 255), 24, th * 0.8f);
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

// The item bar, like Fortnite's: weapons (the one in hand highlighted), shield, potions and your ability. D-pad Left cycles weapons;
// on a touch screen you can tap a slot.
void DrawHotbar(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const royale::HudState& h) {
    struct Slot { std::string title, sub; ImU32 border; bool filled, selected; float cooldown; int action; royale::ItemId icon; int ammo = -1; };
    const ImU32 grey = HudColor(royale::wartable::theme::Muted,150);
    const royale::ItemId none = royale::ItemId::DekuStick;
    std::vector<Slot> slots;
    auto ammoOf = [&](royale::ItemId id) { const royale::AmmoKind k = royale::AmmoUsedBy(id); return k == royale::AmmoKind::None ? -1 : h.ammo[static_cast<size_t>(k)]; };
    slots.push_back({ ShortName(h.weapon), "", RarityU32(h.weaponRarity), true, true, 0.0f, 0, h.weapon, ammoOf(h.weapon) });
    for (int i = 0; i < royale::kMaxReserveWeapons; i++) {
        if (i < static_cast<int>(h.inv.reserve.size())) {
            const auto& r = h.inv.reserve[i];
            const royale::ItemId id = static_cast<royale::ItemId>(r.item);
            slots.push_back({ "", "", RarityU32(static_cast<royale::Rarity>(r.rarity)), true, false, 0.0f, i + 1, id, ammoOf(id) });
        } else {
            slots.push_back({ "", "", grey, false, false, 0.0f, 0, none });
        }
    }
    if (h.hasShield) slots.push_back({ "", "", RarityU32(h.shieldRarity), true, false, 0.0f, 0, h.shield });
    else slots.push_back({ "", "", grey, false, false, 0.0f, 0, none });
    if (!h.inv.potions.empty()) {
        const auto& p = h.inv.potions.front();
        const royale::ItemId id = static_cast<royale::ItemId>(p.item);
        slots.push_back({ "", "x" + std::to_string(h.inv.potions.size()), RarityU32(static_cast<royale::Rarity>(p.rarity)), true, false, 0.0f, 10, id });
    } else {
        slots.push_back({ "", "", grey, false, false, 0.0f, 10, none });
    }
    if (h.inv.hasAbility) {
        const royale::ItemId id = static_cast<royale::ItemId>(h.inv.ability.item);
        const float cd = royale::AbilityOf(id).cooldown;
        const bool lowMagic = h.magic + 0.001f < royale::AbilityMagic(id);
        slots.push_back({ "", h.abilityReadyIn > 0.05f ? ClockText(h.abilityReadyIn) : lowMagic ? "NO MAGIC" : "", RarityU32(static_cast<royale::Rarity>(h.inv.ability.rarity)), true, false,
                          cd > 0 ? std::min(1.0f, h.abilityReadyIn / cd) : 0.0f, 11, id });
    } else {
        slots.push_back({ "", "", grey, false, false, 0.0f, 11, none });
    }

    const float w = 84.0f * scale, hgt = 76.0f * scale, gap = 8.0f * scale;
    const float total = slots.size() * w + (slots.size() - 1) * gap;
    float x = (ds.x - total) * 0.5f;
    const float y = ds.y - hgt - 20.0f * scale;
    ImGuiIO& io = ImGui::GetIO();
    const bool tap = ImGui::IsMouseClicked(0) && !io.WantCaptureMouse;
    for (size_t i = 0; i < slots.size(); i++) {
        const Slot& sl = slots[i];
        const ImVec2 a(x, y), b(x + w, y + hgt);
        HudFrame(dl,a,b,scale,sl.selected);
        if (sl.filled) DrawItemIcon(dl, sl.icon, ImVec2((a.x + b.x) * 0.5f, a.y + hgt * 0.4f), hgt * 0.5f, sl.border);
        if (sl.cooldown > 0) dl->AddRectFilled(a, ImVec2(b.x, a.y + hgt * sl.cooldown), IM_COL32(0, 0, 0, 150), 6.0f * scale);
        dl->AddRectFilled(ImVec2(a.x+4*scale,b.y-4*scale),ImVec2(b.x-4*scale,b.y-2*scale),sl.border);
        if (sl.filled) {   // which button uses the slot
            const int nres = royale::kMaxReserveWeapons;
            const char* hint = i == 0 ? "B" : static_cast<int>(i) <= nres ? "D-pad L/R" : static_cast<int>(i) == nres + 1 ? "C-Left" : static_cast<int>(i) == nres + 2 ? "D-pad Dn" : "D-pad Up";
            const float hs = 12.5f * scale;
            const ImVec2 hsz = font->CalcTextSizeA(hs, FLT_MAX, 0.0f, hint);
            dl->AddText(font, hs, ImVec2((a.x + b.x - hsz.x) * 0.5f + 1, b.y + 2 * scale + 1), IM_COL32(0, 0, 0, 220), hint);
            const ImU32 hintCol = i == 0 ? IM_COL32(100, 230, 110, 255) : static_cast<int>(i) == nres + 1 ? IM_COL32(255, 220, 40, 255) : IM_COL32(210, 205, 190, 235); // B green, C yellow, as on the N64 pad
            dl->AddText(font, hs, ImVec2((a.x + b.x - hsz.x) * 0.5f, b.y + 2 * scale), hintCol, hint);
        }
        const float ts = 11.5f * scale;
        dl->AddText(font, ts, ImVec2(a.x + 5 * scale, b.y - 17 * scale), HudColor(royale::wartable::theme::Ink,sl.filled?255:120), sl.title.c_str());
        dl->AddText(font, ts * 0.9f, ImVec2(a.x + 5 * scale, a.y + 3 * scale), sl.filled ? sl.border : grey, sl.sub.c_str());
        if (sl.filled && sl.ammo >= 0) { // how many shots are left
            const std::string n = "x" + std::to_string(sl.ammo);
            const ImVec2 nsz = font->CalcTextSizeA(ts * 1.15f, FLT_MAX, 0.0f, n.c_str());
            dl->AddText(font, ts * 1.15f, ImVec2(b.x - nsz.x - 5 * scale + 1, a.y + 4 * scale + 1), IM_COL32(0, 0, 0, 220), n.c_str());
            dl->AddText(font, ts * 1.15f, ImVec2(b.x - nsz.x - 5 * scale, a.y + 4 * scale), sl.ammo > 0 ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 90, 90, 255), n.c_str());
        }
        if (tap && io.MousePos.x >= a.x && io.MousePos.x <= b.x && io.MousePos.y >= a.y && io.MousePos.y <= b.y) {
            if (sl.action >= 1 && sl.action <= royale::kMaxReserveWeapons) gSession.SelectWeapon(sl.action);
            else if (sl.action == 10 && !h.inv.potions.empty()) gSession.RequestUsePotion();
            else if (sl.action == 11 && h.inv.hasAbility && h.abilityReadyIn <= 0.05f && h.magic + 0.001f >= royale::AbilityMagic(static_cast<royale::ItemId>(h.inv.ability.item))) gSession.UseAbility();
        }
        x += w + gap;
    }

    // Money, at the right end of the bar.
    {
        const std::string n = std::to_string(h.rupees);
        const float ts = 20.0f * scale;
        const ImVec2 nsz = font->CalcTextSizeA(ts, FLT_MAX, 0.0f, n.c_str());
        const float rx = (ds.x + total) * 0.5f, ry = y - 34.0f * scale;
        DrawItemIcon(dl, royale::ItemId::Rupees, ImVec2(rx - nsz.x - 22.0f * scale, ry + 12.0f * scale), 26.0f * scale, IM_COL32(110, 240, 130, 255));
        dl->AddText(font, ts, ImVec2(rx - nsz.x + 1, ry + 1), IM_COL32(0, 0, 0, 220), n.c_str());
        dl->AddText(font, ts, ImVec2(rx - nsz.x, ry), IM_COL32(150, 255, 160, 255), n.c_str());
    }

    // What you wear: one small icon per gear slot, just above the bar (tunic, boots, gauntlets, mask, scale, pack, charm).
    float gx = (ds.x - total) * 0.5f;
    const float gs = 30.0f * scale, gy = y - gs - 10.0f * scale;
    for (int i = 0; i < royale::kGearSlots; i++) {
        if (!(h.inv.gearMask & (1 << i))) continue;
        const royale::ItemId id = static_cast<royale::ItemId>(h.inv.gear[i].item);
        const ImU32 col = RarityU32(static_cast<royale::Rarity>(h.inv.gear[i].rarity));
        const ImVec2 a(gx, gy), b(gx + gs, gy + gs);
        HudFrame(dl,a,b,scale);
        DrawItemIcon(dl, id, ImVec2(gx + gs * 0.5f, gy + gs * 0.5f), gs * 0.72f, col);
        dl->AddRectFilled(ImVec2(a.x+3*scale,b.y-3*scale),ImVec2(b.x-3*scale,b.y-scale),col);
        gx += gs + 5.0f * scale;
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

    // The EMOTE button, bottom right.
    {
        const float w = 128.0f * scale, hgt = 40.0f * scale;
        const ImVec2 a(ds.x - w - 18.0f * scale, ds.y - hgt - 24.0f * scale), b(a.x + w, a.y + hgt);
        dl->AddRectFilled(a, b, OotPanel(200), 6.0f * scale);
        dl->AddRect(a, b, gWheel.open ? lit : goldDark, 6.0f * scale, 0, 2.5f * scale);
        WheelText(dl, font, 16.0f * scale, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), IM_COL32(255, 255, 255, 255), "EMOTE");
        if (tap && io.MousePos.x >= a.x && io.MousePos.x <= b.x && io.MousePos.y >= a.y && io.MousePos.y <= b.y) {
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

