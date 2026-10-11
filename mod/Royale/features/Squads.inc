// ---- Squads: teams, knocked down, revives, pings and Deku launch pads ------------------------------------------------------------------
// The rules live on the server (server/match.h); this is what the game side does with them. Declared in Core.inc, called from:
//   SquadInput  - OnEmoteWheelInput (Vehicles.inc): L pings, A revives or launches, and a knocked-down player's pad is stripped to Start and L
//   UpdateSquad - OnPlayerUpdate (Terrain.inc): the crawl speed, whether you stand on a pad
//   Draw*       - the HUD (DrawSquadHud, DrawPings, and the three minimap helpers) and Projectile_Draw (DrawLaunchPads)

namespace squads {
constexpr float kReviveReachClient = 105.0f;   // the server's kReviveRange is 120; a little less so a press that looks in range is in range
constexpr float kUnitsPerMetre = 40.0f;
constexpr float kSquadListTop = 128.0f;        // the teammate list starts under the vitals (scaled)
constexpr ImU32 kMateColours[3] = { IM_COL32(80, 200, 255, 255), IM_COL32(130, 235, 110, 255), IM_COL32(255, 150, 230, 255) };
}   // namespace squads

uint16_t gReviveTarget = royale::net::kNoPlayer16;   // the teammate we told the server we are reviving
double gReviveSentAt = 0;
double gPingAt = -10;
bool gOnPad = false;                                 // standing on a Deku launch pad (A launches)

ImU32 PingColour(royale::PingKind k, int alpha = 255) {
    switch (k) {
        case royale::PingKind::Enemy: return IM_COL32(255, 70, 60, alpha);
        case royale::PingKind::Loot: return IM_COL32(255, 205, 60, alpha);
        default: return IM_COL32(60, 220, 200, alpha);
    }
}

std::string PuppetNameOf(uint16_t id) {
    for (const auto& st : gSession.Puppets()) if (st.id == id) return st.name;
    return "A teammate";
}

// The nearest knocked teammate close enough to revive from where you stand.
const royale::TeammateRow* KnockedMateInReach(const royale::HudState& h, float px, float pz) {
    const royale::TeammateRow* best = nullptr;
    float bestD = 1e9f;
    for (const royale::TeammateRow& r : h.squad) {
        if (!r.alive || !r.knocked) continue;
        const float d = std::hypot(r.x - px, r.z - pz);
        if (d <= squads::kReviveReachClient && d < bestD) { best = &r; bestD = d; }
    }
    return best;
}

int PadUnderfoot(const royale::HudState& h, float px, float pz) {
    for (size_t i = 0; i < h.launchPads.size(); i++)
        if (std::hypot(h.launchPads[i].first - px, h.launchPads[i].second - pz) <= royale::kLaunchPadRadius) return static_cast<int>(i);
    return -1;
}

// Straight up and into the glider: the same path as the Sandbox's "glide from here" (Lobby.inc).
void LaunchFromPad(Player* player) {
    gSkydiving = true;
    gSandboxGlide = true;
    player->actor.world.pos.y += 2200.0f;
    player->actor.prevPos = player->actor.world.pos;
    Audio_PlaySoundGeneral(NA_SE_EN_NUTS_UP, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

// L: ping what you are looking at (an enemy or loot in a narrow cone), else the ground 600-1500 units ahead.
void SendLocalPing(Player* player, const royale::HudState& hud) {
    const float yaw = player->actor.shape.rot.y * (3.14159265f / 32768.0f);
    const float fx = std::sin(yaw), fz = std::cos(yaw), px = player->actor.world.pos.x, pz = player->actor.world.pos.z;
    const float cone = std::cos(royale::kPingConeDegrees * 3.14159265f / 180.0f);
    auto inCone = [&](float x, float z, float* dist) {
        const float dx = x - px, dz = z - pz, d = std::hypot(dx, dz);
        if (d < 80.0f || d > royale::kPingReach || (dx * fx + dz * fz) / d < cone) return false;
        *dist = d;
        return true;
    };
    float bestEnemy = 1e9f, bestLoot = 1e9f, d = 0;
    float ex = 0, ez = 0, lx = 0, lz = 0;
    uint16_t enemy = royale::net::kNoPlayer16, loot = royale::net::kNoPlayer16;
    for (const auto& st : gSession.Puppets()) {
        if (!st.alive || st.teammate || st.id == hud.selfId || st.scene != gPlayState->sceneNum) continue;
        if (inCone(st.x, st.z, &d) && d < bestEnemy) { bestEnemy = d; enemy = st.id; ex = st.x; ez = st.z; }
    }
    for (const auto& kv : gLoot) {
        const LootActor& la = kv.second;
        if (la.actor == nullptr || la.killing || la.opened) continue;
        if (inCone(la.actor->world.pos.x, la.actor->world.pos.z, &d) && d < bestLoot) {
            bestLoot = d; loot = static_cast<uint16_t>(kv.first); lx = la.actor->world.pos.x; lz = la.actor->world.pos.z;
        }
    }
    if (enemy != royale::net::kNoPlayer16) gSession.SendPing(royale::PingKind::Enemy, ex, ez, enemy);
    else if (loot != royale::net::kNoPlayer16) gSession.SendPing(royale::PingKind::Loot, lx, lz, loot);
    else {
        const float ahead = 1000.0f;
        gSession.SendPing(royale::PingKind::Place, px + fx * ahead, pz + fz * ahead);
    }
}

void StripButtons(Input& in, uint16_t mask) {
    in.cur.button &= ~mask; in.press.button &= ~mask; in.rel.button &= ~mask;
}

bool LocalSeated();   // in a cart (Vehicles.inc): A is the throttle there, not a revive or a pad launch

void StopRevive() {
    if (gReviveTarget != royale::net::kNoPlayer16) gSession.HoldRevive(gReviveTarget, false);
    gReviveTarget = royale::net::kNoPlayer16;
}

// Before the game reads the pad. Returns true when the controller is used up (you are knocked down).
bool SquadInput(Input& in, const royale::HudState& hud) {
    if (!LiveAndAlive(hud) || !InField()) { StopRevive(); return false; }
    Player* player = GET_PLAYER(gPlayState);
    const double now = ImGui::GetTime();
    if ((in.press.button & BTN_L) && now - gPingAt > 0.35 && !MessageBoxUp()) { gPingAt = now; SendLocalPing(player, hud); }
    if (hud.selfKnocked) {   // crawling: nothing but Start (the menu) and L (ping) is yours
        StopRevive();
        gTouchJump = false;
        const uint16_t keep = BTN_START | BTN_L;
        StripButtons(in, static_cast<uint16_t>(~keep));
        return false;
    }
    if (LocalSeated()) { StopRevive(); return false; }   // driving past a downed teammate must not take the throttle (the server refuses riders' revives)
    // Hold A beside a knocked teammate: the revive. The press is kept from the game so Link does not jump or take something instead.
    const float px = player->actor.world.pos.x, pz = player->actor.world.pos.z;
    const royale::TeammateRow* mate = gSkydiving ? nullptr : KnockedMateInReach(hud, px, pz);
    if (mate != nullptr && (in.cur.button & BTN_A)) {
        StripButtons(in, BTN_A);
        gTouchJump = false;
        if (gReviveTarget != mate->id || now - gReviveSentAt > 0.12) {
            if (gReviveTarget != mate->id) StopRevive();
            gSession.HoldRevive(mate->id, true);
            gReviveTarget = mate->id;
            gReviveSentAt = now;
        }
        return false;
    }
    StopRevive();
    // Standing on a Deku launch pad: A (or the JUMP button) launches.
    if (!gSkydiving && PadUnderfoot(hud, px, pz) >= 0 && (player->actor.bgCheckFlags & 1) && ((in.press.button & BTN_A) || gTouchJump)) {
        StripButtons(in, BTN_A);
        gTouchJump = false;
        LaunchFromPad(player);
    }
    return false;
}

// Each frame in the match, after the speed buffs.
void UpdateSquad(Player* player, const royale::HudState& hud) {
    gSelfKnocked = hud.selfKnocked && LiveAndAlive(hud) && InField();
    if (gSelfKnocked) gRoyaleRunSpeedScale = royale::kKnockedSpeedScale;
    gOnPad = LiveAndAlive(hud) && InField() && !gSkydiving && !gSelfKnocked && PadUnderfoot(hud, player->actor.world.pos.x, player->actor.world.pos.z) >= 0;
}

// ---- drawing: pads in the world ---------------------------------------------------------------------------------------------------

void DrawLaunchPads(PlayState* play) {
    royale::GameClient* client = gSession.Client();
    if (client == nullptr || !InField()) return;
    const auto& pads = client->LaunchPads();
    if (pads.empty()) return;
    const GpuMesh* mesh = GpuMeshFor(royale::MeshKind::DekuFlower, 0);
    if (mesh == nullptr || mesh->dl.empty()) return;
    const Vec3f me = GET_PLAYER(play)->actor.world.pos;
    const float t = static_cast<float>(ImGui::GetTime());
    for (const auto& pad : pads) {
        if (std::hypot(pad.first - me.x, pad.second - me.z) > 4500.0f) continue;
        const float y = GroundY(play, pad.first, pad.second, me.y);
        const float pulse = 1.0f + 0.05f * std::sin(t * 3.0f);
        OPEN_DISPS(play->state.gfxCtx);
        Gfx_SetupDL_25Opa(play->state.gfxCtx);
        Matrix_Translate(pad.first, y + 1.0f, pad.second, MTXMODE_NEW);
        Matrix_RotateY(t * 0.25f, MTXMODE_APPLY);
        Matrix_Scale(pulse, pulse, pulse, MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
        gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
        gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(mesh->dl.data()));
        CLOSE_DISPS(play->state.gfxCtx);
    }
}

// ---- drawing: the HUD -------------------------------------------------------------------------------------------------------------

void DrawRing(ImDrawList* dl, ImVec2 c, float r, float frac, ImU32 back, ImU32 fill, float th) {
    dl->AddCircle(c, r, back, 32, th);
    if (frac <= 0.0f) return;
    const float a0 = -1.5707963f;
    dl->PathArcTo(c, r, a0, a0 + 6.2831853f * std::min(frac, 1.0f), 32);
    dl->PathStroke(fill, 0, th);
}

void DrawSquadHud(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const royale::HudState& h) {
    if (!(h.state == royale::MatchState::Drop || h.state == royale::MatchState::InMatch)) return;
    const ImU32 white = IM_COL32(255, 255, 255, 255), shadow = IM_COL32(0, 0, 0, 200);
    auto text = [&](ImVec2 at, float size, ImU32 col, const std::string& s) {
        dl->AddText(font, size, ImVec2(at.x + 1.5f * scale, at.y + 1.5f * scale), shadow, s.c_str());
        dl->AddText(font, size, at, col, s.c_str());
    };
    auto centred = [&](float cx, float y, float size, ImU32 col, const std::string& s) {
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, s.c_str());
        text(ImVec2(cx - sz.x * 0.5f, y), size, col, s);
    };
    const bool live = LiveAndAlive(h);
    // The teammate list, left, under the vitals.
    if (h.teamMode != royale::TeamMode::Solo) {
        float y = std::max(squads::kSquadListTop * scale, HudMeterTop(scale, h) + 66.0f * scale);   // under the stamina bar, even with two rows of hearts
        const float x = 14.0f * scale, w = 118.0f * scale, bar = 7.0f * scale, row = 34.0f * scale;
        const double t = ImGui::GetTime();
        for (const royale::TeammateRow& r : h.squad) {
            const ImU32 mc = squads::kMateColours[r.slot % 3];
            const bool out = !r.alive;
            const int a = out ? 110 : 255;
            dl->AddCircleFilled(ImVec2(x + 5.0f * scale, y + 8.0f * scale), 4.0f * scale, out ? IM_COL32(120, 120, 120, 150) : mc);
            text(ImVec2(x + 14.0f * scale, y), 15.0f * scale, out ? IM_COL32(150, 150, 150, 170) : white, r.name);
            const ImVec2 b0(x, y + 19.0f * scale);
            dl->AddRectFilled(b0, ImVec2(b0.x + w, b0.y + bar), IM_COL32(0, 0, 0, 170), 2.0f * scale);
            if (r.alive && !r.knocked) {
                const float f = std::clamp(r.health / royale::kMaxHealth, 0.0f, 1.0f);
                dl->AddRectFilled(b0, ImVec2(b0.x + w * f, b0.y + bar), f > 0.34f ? IM_COL32(90, 220, 90, a) : IM_COL32(235, 70, 60, a), 2.0f * scale);
            } else if (r.alive) {   // knocked: a red cross by the name and the bleed-out bar (cyan when someone is reviving them)
                const ImVec2 k(x + w + 10.0f * scale, y + 8.0f * scale);
                const float s = 4.5f * scale, pulse = 0.6f + 0.4f * static_cast<float>(std::sin(t * 6.0));
                dl->AddLine(ImVec2(k.x - s, k.y - s), ImVec2(k.x + s, k.y + s), IM_COL32(255, 60, 50, static_cast<int>(255 * pulse)), 2.5f * scale);
                dl->AddLine(ImVec2(k.x - s, k.y + s), ImVec2(k.x + s, k.y - s), IM_COL32(255, 60, 50, static_cast<int>(255 * pulse)), 2.5f * scale);
                dl->AddRectFilled(b0, ImVec2(b0.x + w * std::clamp(r.knockLeft, 0.0f, 1.0f), b0.y + bar), IM_COL32(255, 150, 40, 255), 2.0f * scale);
                if (r.reviveFrac > 0.0f)
                    dl->AddRectFilled(b0, ImVec2(b0.x + w * std::clamp(r.reviveFrac, 0.0f, 1.0f), b0.y + bar * 0.45f), IM_COL32(90, 230, 255, 255), 2.0f * scale);
            }
            dl->AddRect(b0, ImVec2(b0.x + w, b0.y + bar), IM_COL32(255, 255, 255, out ? 60 : 150), 2.0f * scale);
            y += row;
        }
    }
    if (!live) return;
    // You are down.
    if (h.selfKnocked) {
        const float w = 300.0f * scale, hgt = 12.0f * scale, cx = ds.x * 0.5f, y = ds.y * 0.74f;
        centred(cx, y - 34.0f * scale, 28.0f * scale, IM_COL32(255, 90, 70, 255), "KNOCKED - hold on!");
        const ImVec2 b0(cx - w * 0.5f, y), b1(cx + w * 0.5f, y + hgt);
        dl->AddRectFilled(b0, b1, IM_COL32(0, 0, 0, 190), 3.0f * scale);
        dl->AddRectFilled(b0, ImVec2(b0.x + w * std::clamp(h.selfKnockLeft, 0.0f, 1.0f), b1.y), IM_COL32(235, 70, 55, 255), 3.0f * scale);
        if (h.selfReviveFrac > 0.0f) {
            dl->AddRectFilled(b0, ImVec2(b0.x + w * std::clamp(h.selfReviveFrac, 0.0f, 1.0f), b0.y + hgt * 0.45f), IM_COL32(90, 230, 255, 255), 3.0f * scale);
            centred(cx, y + hgt + 6.0f * scale, 17.0f * scale, IM_COL32(150, 240, 255, 255), "A teammate is reviving you");
        } else {
            centred(cx, y + hgt + 6.0f * scale, 16.0f * scale, IM_COL32(230, 230, 230, 220), "Crawl to cover - a teammate can revive you");
        }
        dl->AddRect(b0, b1, white, 3.0f * scale, 0, 1.5f * scale);
        return;
    }
    if (gPlayState == nullptr || !InField()) return;
    const Vec3f me = GET_PLAYER(gPlayState)->actor.world.pos;
    // A knocked teammate in reach: Hold A.
    if (const royale::TeammateRow* mate = LocalSeated() ? nullptr : KnockedMateInReach(h, me.x, me.z)) {
        const ImVec2 c(ds.x * 0.5f, ds.y * 0.66f);
        const float r = 24.0f * scale;
        dl->AddCircleFilled(c, r + 4.0f * scale, IM_COL32(0, 0, 0, 150));
        DrawPadGlyph(dl, font, c, r * 0.72f, royale::controls::Glyph::A);
        DrawRing(dl, c, r, mate->reviveFrac, IM_COL32(255, 255, 255, 120), IM_COL32(90, 230, 255, 255), 4.0f * scale);
        centred(c.x, c.y + r + 8.0f * scale, 19.0f * scale, white, "Hold A: Revive " + mate->name);
    } else if (gOnPad) {   // on a Deku launch pad
        const ImVec2 c(ds.x * 0.5f, ds.y * 0.66f);
        const float r = 22.0f * scale;
        dl->AddCircleFilled(c, r + 4.0f * scale, IM_COL32(0, 0, 0, 150));
        DrawPadGlyph(dl, font, c, r * 0.72f, royale::controls::Glyph::A);
        centred(c.x, c.y + r + 8.0f * scale, 19.0f * scale, IM_COL32(255, 160, 215, 255), "Launch");
    }
}

// A ping in the world: a Navi-like glowing diamond that bobs, with the distance under it. Red for an enemy, gold for loot, teal for a place.
void DrawPings(ImDrawList* dl, ImFont* font, ImVec2, float scale) {
    if (gPlayState == nullptr || !InField()) return;
    const royale::HudState h = gSession.Hud();
    if (h.pings.empty()) return;
    const Vec3f me = GET_PLAYER(gPlayState)->actor.world.pos;
    const double t = ImGui::GetTime();
    for (const royale::PingRow& p : h.pings) {
        const float ground = GroundY(gPlayState, p.x, p.z, me.y);
        ImVec2 s;
        if (!WorldToScreen(p.x, ground + 70.0f + 6.0f * static_cast<float>(std::sin(t * 3.0 + p.owner)), p.z, &s)) continue;
        const int fade = static_cast<int>(255.0f * std::clamp(p.secondsLeft, 0.0f, 1.0f));
        const float u = 11.0f * scale, glow = u * (1.9f + 0.25f * static_cast<float>(std::sin(t * 5.0)));
        dl->AddCircleFilled(s, glow, PingColour(p.kind, fade / 7), 24);
        dl->AddCircleFilled(s, glow * 0.65f, PingColour(p.kind, fade / 5), 24);
        const ImVec2 top(s.x, s.y - u * 1.5f), right(s.x + u, s.y), bottom(s.x, s.y + u * 1.5f), left(s.x - u, s.y);
        dl->AddQuadFilled(top, right, bottom, left, PingColour(p.kind, fade));
        dl->AddQuad(top, right, bottom, left, IM_COL32(255, 255, 255, fade), 2.0f * scale);
        const std::string m = std::to_string(static_cast<int>(std::hypot(p.x - me.x, p.z - me.z) / squads::kUnitsPerMetre)) + " m";
        const float size = 16.0f * scale;
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, m.c_str()), at(s.x - sz.x * 0.5f, s.y + u * 1.8f);
        dl->AddText(font, size, ImVec2(at.x + 1.5f * scale, at.y + 1.5f * scale), IM_COL32(0, 0, 0, fade), m.c_str());
        dl->AddText(font, size, at, IM_COL32(255, 255, 255, fade), m.c_str());
    }
}

// Minimap helpers. Call inside the map's clip rect, after the other markers.
void DrawTeamOnMinimap(ImDrawList* dl, float scale, const std::function<ImVec2(float, float)>& toMap) {
    const royale::HudState h = gSession.Hud();
    for (const royale::TeammateRow& r : h.squad) {
        if (!r.alive) continue;
        const ImVec2 p = toMap(r.x, r.z);
        dl->AddCircleFilled(p, 4.6f * scale, IM_COL32(0, 0, 0, 220));
        dl->AddCircleFilled(p, 3.5f * scale, squads::kMateColours[r.slot % 3]);
        if (r.knocked) dl->AddCircle(p, 6.0f * scale, IM_COL32(255, 70, 60, 255), 16, 1.8f * scale);
    }
}

void DrawPingsOnMinimap(ImDrawList* dl, float scale, const std::function<ImVec2(float, float)>& toMap) {
    const royale::HudState h = gSession.Hud();
    for (const royale::PingRow& p : h.pings) {
        const ImVec2 c = toMap(p.x, p.z);
        const float u = 5.0f * scale;
        dl->AddQuadFilled(ImVec2(c.x, c.y - u * 1.4f), ImVec2(c.x + u, c.y), ImVec2(c.x, c.y + u * 1.4f), ImVec2(c.x - u, c.y), PingColour(p.kind));
        dl->AddQuad(ImVec2(c.x, c.y - u * 1.4f), ImVec2(c.x + u, c.y), ImVec2(c.x, c.y + u * 1.4f), ImVec2(c.x - u, c.y), IM_COL32(255, 255, 255, 230), 1.4f * scale);
    }
}

// A small pink flower for each Deku launch pad.
void DrawPadsOnMinimap(ImDrawList* dl, float scale, const std::function<ImVec2(float, float)>& toMap) {
    royale::GameClient* client = gSession.Client();
    if (client == nullptr) return;
    for (const auto& pad : client->LaunchPads()) {
        const ImVec2 c = toMap(pad.first, pad.second);
        const float r = 2.3f * scale;
        for (int i = 0; i < 5; i++) {
            const float a = 6.2831853f * static_cast<float>(i) / 5.0f;
            dl->AddCircleFilled(ImVec2(c.x + std::cos(a) * r * 1.2f, c.y + std::sin(a) * r * 1.2f), r, IM_COL32(255, 120, 190, 255));
        }
        dl->AddCircleFilled(c, r * 0.8f, IM_COL32(255, 220, 90, 255));
    }
}

// The feed and banner lines for the squad events (called from the ClientEvent switch in Terrain.inc).
void SquadEvent(const royale::ClientEvent& e, const royale::HudState& hud) {
    using T = royale::ClientEvent::Type;
    const bool me = e.id == hud.selfId;
    auto isMate = [&](uint16_t id) { for (const auto& r : hud.squad) if (r.id == id) return true; return false; };
    switch (e.type) {
        case T::Knocked:
            if (me) ShowBanner("You are down!", IM_COL32(255, 90, 70, 255), 2.2f);
            else if (isMate(e.id)) AddFeed(PuppetNameOf(e.id) + " is down!", IM_COL32(255, 170, 70, 255));
            else if (e.other == hud.selfId) ShowGain("Knocked " + PuppetNameOf(e.id), IM_COL32(255, 200, 90, 255));
            break;
        case T::TeamRevived:
            if (me) ShowBanner("Revived by " + PuppetNameOf(e.other), IM_COL32(140, 245, 160, 255), 2.2f);
            else if (isMate(e.id)) AddFeed(PuppetNameOf(e.id) + " was revived", IM_COL32(140, 245, 160, 255));
            if (e.other == hud.selfId && !me)
                Audio_PlaySoundGeneral(NA_SE_SY_HP_RECOVER, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            break;
        case T::Pinged:
            Audio_PlaySoundGeneral(NA_SE_SY_CAMERA_ZOOM_UP, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            break;
        default: break;
    }
}
