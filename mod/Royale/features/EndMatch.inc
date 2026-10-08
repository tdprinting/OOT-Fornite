// ---- dying, spectating and the end of the match -----------------------------------------------------------------------------------
// One flow, start to finish:
//   1. You are eliminated. Your Link is hidden and his body falls as a ragdoll (SpawnCorpse) with his cry and a thud at each bounce. The camera
//      stays on the body (OnPlayerUpdate), the edges of the screen go dark red and "ELIMINATED" says who got you and where you placed.
//   2. A moment later the death card comes up under it: your kills, damage and points so far, and two buttons, Spectate and Leave.
//   3. Spectating: a slim bar at the top says whom you watch; < and > (or D-pad Left/Right) go through your body and everyone still standing.
//   4. The match ends: "VICTORY ROYALE!" or your place, then the end screen: your numbers, how your points add up, every player and bot with their
//      place, kills, damage and score, the replay, and Keep spectating / New match / Leave. Keep spectating hides it; D-pad Up (or the Results
//      chip) brings it back.
// It is all drawn on ImGui's foreground list with the colours in kUi, so a new menu look only has to change kUi and the Draw* functions here.
// Controller, while the card or the end screen is up: D-pad Left/Right picks a button, A presses it, B is Keep spectating, D-pad Up/Down scroll
// the table. Touch: tap the buttons, drag the table.
// The look follows Fortnite's end of match: deep violet-blue over the game, a slanted orange banner, light blue labels, white numbers.
struct UiStyle {
    ImU32 bgTop, bgBottom, panel, edge, gold, white, grey, label, accent, red, green, selfRow, rowAlt, button, buttonHot, buttonOff, tile,
          banner, bannerLight, bannerLoss, bannerLossLight, bronze, silver, medalGold;
};
const UiStyle kUi = {
    HudColor(royale::wartable::theme::Panel,255),    // bgTop: the tint over the game behind the end screen, top...
    HudColor(royale::wartable::theme::Button,255),    // bgBottom: ...and bottom
    HudColor(royale::wartable::theme::Panel,237),     // panel (the death card, the spectating bar)
    HudColor(royale::wartable::theme::Border),  // edge
    HudColor(royale::wartable::theme::Gold),   // gold
    HudColor(royale::wartable::theme::Ink),  // white
    IM_COL32(160, 168, 200, 255),  // grey
    HudColor(royale::wartable::theme::Muted),  // label: the light blue of names and headings
    HudColor(royale::wartable::theme::Focus),   // accent: the ring, ticks, the chosen tab
    IM_COL32(255, 92, 84, 255),    // red
    IM_COL32(120, 255, 140, 255),  // green: you
    IM_COL32(60, 150, 255, 90),    // selfRow
    IM_COL32(255, 255, 255, 12),   // rowAlt
    HudColor(royale::wartable::theme::Button,237),     // button
    HudColor(royale::wartable::theme::Button,255),   // buttonHot
    IM_COL32(30, 30, 50, 190),     // buttonOff
    IM_COL32(255, 255, 255, 18),   // tile
    IM_COL32(240, 120, 30, 255),   // banner: a win
    IM_COL32(255, 160, 60, 255),   // bannerLight
    IM_COL32(70, 70, 170, 255),    // bannerLoss: any other place
    IM_COL32(100, 100, 210, 255),  // bannerLossLight
    IM_COL32(205, 127, 70, 255),   // bronze medal
    IM_COL32(200, 210, 225, 255),  // silver medal
    IM_COL32(255, 200, 50, 255),   // gold medal
};
constexpr double kDeathCardDelay = 1.8;   // seconds of watching your body fall before the card comes up
constexpr double kEndPanelDelay = 7.0;    // a visible victory lap before results take the controls

bool IsLive(const royale::HudState& h);   // with the match's health, below

// What you did this match, counted from the server's events (the exact numbers arrive with the results when the match ends).
struct MyMatchStats {
    int kills = 0, chests = 0, hits = 0;
    float damage = 0, taken = 0, distance = 0;   // hearts dealt, hearts taken, units walked, swum and ridden
    bool firstStrike = false, anyElim = false;   // you got the match's first elimination / somebody has been eliminated
    int streak = 0, bestStreak = 0;              // eliminations close together (each within kStreakSec of the last)
    double lastKillAt = -100, startAt = 0;       // startAt: the drop
    float lastX = 0, lastZ = 0;
    bool havePos = false;
};
constexpr double kStreakSec = 10.0;
MyMatchStats gMyStats;

struct DeathUi {
    bool active = false;        // you are out of this match
    double at = 0;              // when (ImGui time)
    uint16_t killer = royale::net::kNoPlayer16;
    std::string by;             // "by Saria", "by the storm" (empty until the elimination event says)
    int aliveAtDeath = 0, place = 0;
    bool placeSettled = false;
    bool card = true;           // the death card is up (until Spectate)
    int focus = 0;              // the card's button with the controller's focus: 0 Spectate, 1 Leave
    uint16_t body = 0;          // your body (a corpse id), 0 for none
    bool spectated = false;     // Spectate was chosen once (the card can come back with B, and then keeps whom you watch)
};
DeathUi gDeath;

struct EndUi {
    bool open = false;          // the end screen is up (Keep spectating hides it)
    double at = 0;              // when the match ended
    int focus = 1;              // 0 Keep spectating, 1 New match, 2 Leave
    float scroll = 0;           // the table, in pixels
    bool byScore = false;       // the table's order: by place, or by score
    bool dragging = false;
    int page = 0;               // 0 the summary (stats, points, medals), 1 the standings (everyone, the replay)
};
EndUi gEnd;

// The controller's presses for the card and the end screen, taken off the game in MatchUiInput and used up when they are drawn.
struct UiPad { bool a = false, b = false, left = false, right = false, up = false, down = false, page = false; };
UiPad gUiPad;

void UiSfx(u16 sfx) { Audio_PlaySoundGeneral(sfx, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb); }

void ResetMatchUi() { gMyStats = {}; gDeath = {}; gEnd = {}; gUiPad = {}; }

// You are out. Called by the elimination event and by OnPlayerUpdate, whichever sees it first.
void StartDeath(const royale::HudState& h) {
    if (gDeath.active) return;
    gDeath.active = true;
    gDeath.at = ImGui::GetTime();
    gDeath.aliveAtDeath = h.alive;
    gDeath.place = std::max(1, h.alive);
    gDeath.card = true;
    gDeath.focus = 0;
    gSpectateTarget = kSpectateSelf;   // the camera stays on your body until you choose to spectate
    UiSfx(NA_SE_IT_HAMMER_HIT);        // the blow lands: a heavy impact, then Link's cry from his body
    UiSfx(NA_SE_PL_BODY_HIT);
}

// The match has ended: the big title, then the end screen.
void StartEndScreen(const royale::HudState& h) {
    gEnd = {};
    gEnd.open = true;
    gEnd.at = ImGui::GetTime();
    gEnd.focus = h.isHost ? 1 : 0;
    if (h.winnerId != h.selfId && h.winnerId != royale::net::kNoPlayer16) gSpectateTarget = h.winnerId;
    if (h.winnerId == h.selfId && h.winnerId != royale::net::kNoPlayer16) Audio_PlayFanfare(NA_BGM_ITEM_GET);
}

bool DeathCardUp(const royale::HudState& h) { return gDeath.active && gDeath.card && IsLive(h) && ImGui::GetTime() - gDeath.at > kDeathCardDelay; }
bool EndScreenUp(const royale::HudState& h) { return h.state == royale::MatchState::Ending && gEnd.open && ImGui::GetTime() - gEnd.at > kEndPanelDelay; }

// Runs before the game reads the controller (from OnEmoteWheelInput). While the death card or the end screen is up the controller is theirs (Start
// still opens the menu). Returns true when it took this frame's buttons.
bool MatchUiInput(Input& in, const royale::HudState& hud) {
    // From the moment you go down until you choose Spectate the pad is the death screen's, so a press can't make the hidden Link swing or roll.
    const bool card = gDeath.active && gDeath.card && IsLive(hud), end = EndScreenUp(hud);
    if (!card && !end) {
        gUiPad = {};
        if (gDeath.active && IsLive(hud) && (in.press.button & BTN_B)) {   // spectating: B brings the death card (and its Leave) back
            gDeath.card = true;
            gDeath.focus = 0;
            in.press.button &= ~BTN_B; in.cur.button &= ~BTN_B;
            UiSfx(NA_SE_SY_DECIDE);
            return true;
        }
        if (hud.state == royale::MatchState::Ending && !gEnd.open && (in.press.button & BTN_DUP)) {   // bring the results back
            gEnd.open = true;
            gEnd.at = ImGui::GetTime() - kEndPanelDelay;
            in.press.button &= ~BTN_DUP; in.cur.button &= ~BTN_DUP;
            UiSfx(NA_SE_SY_DECIDE);
        }
        return false;
    }
    const u16 p = (card && !DeathCardUp(hud)) ? 0 : in.press.button;   // presses during the fall itself do nothing
    gUiPad.a |= (p & BTN_A) != 0;
    gUiPad.b |= (p & BTN_B) != 0;
    gUiPad.left |= (p & BTN_DLEFT) != 0;
    gUiPad.right |= (p & BTN_DRIGHT) != 0;
    gUiPad.up |= (p & BTN_DUP) != 0;
    gUiPad.down |= (p & BTN_DDOWN) != 0;
    gUiPad.page |= (p & (BTN_R | BTN_Z)) != 0;
    in.cur.button &= BTN_START; in.press.button &= BTN_START; in.rel.button &= BTN_START;
    in.cur.stick_x = in.cur.stick_y = in.rel.stick_x = in.rel.stick_y = in.press.stick_x = in.press.stick_y = 0;
    return true;
}

bool UiTapIn(ImVec2 a, ImVec2 b) {
    ImGuiIO& io = ImGui::GetIO();
    return ImGui::IsMouseClicked(0) && !io.WantCaptureMouse && io.MousePos.x >= a.x && io.MousePos.x <= b.x && io.MousePos.y >= a.y && io.MousePos.y <= b.y;
}

void UiText(ImDrawList* dl, ImFont* font, float size, ImVec2 at, ImU32 col, const std::string& t, bool shadow = true) {
    if (shadow) dl->AddText(font, size, ImVec2(at.x + size * 0.06f, at.y + size * 0.06f), IM_COL32(0, 0, 0, (col >> 24) * 220 / 255), t.c_str());
    dl->AddText(font, size, at, col, t.c_str());
}
float UiTextW(ImFont* font, float size, const std::string& t) { return font->CalcTextSizeA(size, FLT_MAX, 0.0f, t.c_str()).x; }
// Text centred on x, shrunk to fit `maxW` if it has to.
void UiTextCentred(ImDrawList* dl, ImFont* font, float size, float cx, float y, ImU32 col, const std::string& t, float maxW = 1e9f) {
    const float w = UiTextW(font, size, t);
    if (w > maxW && w > 0) size *= maxW / w;
    UiText(dl, font, size, ImVec2(cx - UiTextW(font, size, t) * 0.5f, y), col, t);
}

ImU32 UiAlpha(ImU32 col, float a) { return (col & 0x00FFFFFF) | (static_cast<ImU32>(((col >> 24) & 0xFF) * std::clamp(a, 0.0f, 1.0f)) << 24); }

// A button. Returns true when it is tapped, or pressed with A while it has the controller's focus.
bool UiButton(ImDrawList* dl, ImFont* font, float scale, ImVec2 a, ImVec2 b, const std::string& label, bool focused, bool enabled, float alpha = 1.0f) {
    ImGuiIO& io = ImGui::GetIO();
    const bool hover = io.MousePos.x >= a.x && io.MousePos.x <= b.x && io.MousePos.y >= a.y && io.MousePos.y <= b.y && !io.WantCaptureMouse;
    const float r = 8.0f * scale;
    dl->AddRectFilled(a, b, UiAlpha(!enabled ? kUi.buttonOff : (focused || hover) ? kUi.buttonHot : kUi.button, alpha), r);
    dl->AddRect(a, b, UiAlpha(enabled ? kUi.edge : kUi.grey, alpha * (enabled ? 1.0f : 0.5f)), r, 0, (focused && enabled ? 3.0f : 1.5f) * scale);
    UiTextCentred(dl, font, 21.0f * scale, (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f - 11.0f * scale, UiAlpha(enabled ? kUi.white : kUi.grey, alpha), label, b.x - a.x - 12.0f * scale);
    if (!enabled || alpha < 0.9f) return false;
    if (UiTapIn(a, b)) { UiSfx(NA_SE_SY_DECIDE); return true; }
    if (focused && gUiPad.a) { gUiPad.a = false; UiSfx(NA_SE_SY_DECIDE); return true; }
    return false;
}

// A number with a label under it, on a faint tile.
void UiTile(ImDrawList* dl, ImFont* font, float scale, ImVec2 a, ImVec2 b, const std::string& value, const std::string& label, ImU32 col, float alpha = 1.0f) {
    dl->AddRectFilled(a, b, UiAlpha(kUi.tile, alpha), 8.0f * scale);
    const float cx = (a.x + b.x) * 0.5f, h = b.y - a.y;
    UiTextCentred(dl, font, h * 0.46f, cx, a.y + h * 0.1f, UiAlpha(col, alpha), value, b.x - a.x - 8.0f * scale);
    UiTextCentred(dl, font, h * 0.24f, cx, a.y + h * 0.62f, UiAlpha(kUi.grey, alpha), label, b.x - a.x - 8.0f * scale);
}

// Dark red closing in from the edges of the screen.
void UiVignette(ImDrawList* dl, ImVec2 ds, float strength) {
    if (strength <= 0.0f) return;
    const ImU32 edge = IM_COL32(40, 0, 0, static_cast<int>(200 * std::min(1.0f, strength))), clear = IM_COL32(40, 0, 0, 0);
    const float bandY = ds.y * 0.3f, bandX = ds.x * 0.22f;
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(ds.x, bandY), edge, edge, clear, clear);
    dl->AddRectFilledMultiColor(ImVec2(0, ds.y - bandY), ds, clear, clear, edge, edge);
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(bandX, ds.y), edge, clear, clear, edge);
    dl->AddRectFilledMultiColor(ImVec2(ds.x - bandX, 0), ds, clear, edge, edge, clear);
}

std::string UiThousands(int n) {
    std::string s = std::to_string(std::abs(n));
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<size_t>(i), ",");
    return n < 0 ? "-" + s : s;
}

// Steps 1 to 3: the moment you go down, the death card, and the spectating bar.
void DrawDeathScreen(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const royale::HudState& h) {
    Feat("death screen");
    const bool spectatingEnd = h.state == royale::MatchState::Ending && (!gEnd.open || !EndScreenUp(h)) && h.haveSelf && !h.selfAlive;
    if (!(gDeath.active && IsLive(h)) && !spectatingEnd) return;
    const double now = ImGui::GetTime(), t = now - gDeath.at;
    // Your place: the player count may or may not have taken you off yet when the elimination arrives, so look again a moment later.
    if (gDeath.active && !gDeath.placeSettled && t > 0.8) {
        gDeath.place = h.alive >= gDeath.aliveAtDeath ? gDeath.aliveAtDeath + 1 : gDeath.aliveAtDeath;
        if (IsLive(h)) gDeath.place = std::max(2, gDeath.place);
        gDeath.placeSettled = true;
    }
    const float cx = ds.x * 0.5f;

    if (gDeath.active && gDeath.card && IsLive(h)) {
        // 1. Going down: a red flash, the edges darken, the big word.
        if (t < 0.12) dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(255, 255, 255, static_cast<int>(170 * (1.0 - t / 0.12))));   // the hit
        else if (t < 0.6) dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(150, 0, 0, static_cast<int>(120 * (1.0 - (t - 0.12) / 0.48))));
        UiVignette(dl, ds, static_cast<float>(std::min(1.0, t / 0.6)));
        {   // cinema bars slide in while you watch yourself fall
            const float bar = ds.y * 0.085f * static_cast<float>(std::min(1.0, t / 0.45));
            dl->AddRectFilled(ImVec2(0, 0), ImVec2(ds.x, bar), IM_COL32(0, 0, 0, 235));
            dl->AddRectFilled(ImVec2(0, ds.y - bar), ds, IM_COL32(0, 0, 0, 235));
        }
        const float in = static_cast<float>(std::min(1.0, t / 0.35));
        const float titleSize = (58.0f + 18.0f * (1.0f - in)) * scale;
        UiTextCentred(dl, font, titleSize, cx, ds.y * 0.13f, UiAlpha(kUi.red, in), "ELIMINATED");
        const float sub = static_cast<float>(std::clamp((t - 0.35) / 0.4, 0.0, 1.0));
        UiTextCentred(dl, font, 24.0f * scale, cx, ds.y * 0.13f + 70.0f * scale, UiAlpha(kUi.white, sub), gDeath.by, ds.x * 0.9f);
        UiTextCentred(dl, font, 30.0f * scale, cx, ds.y * 0.13f + 104.0f * scale, UiAlpha(kUi.gold, sub),
                      "#" + std::to_string(gDeath.place) + "  of " + std::to_string(std::max(gDeath.place, h.playerLimit)));

        // After the fall, WarGameUpdate opens the native death menu.
        // Its controller/touch actions and statistics share the War Table painter.
        return;
    }

    // 3. Spectating: whom you watch, and the arrows to switch.
    UiVignette(dl, ds, 0.35f);
    const float bw = std::min(ds.x - 32.0f * scale, 460.0f * scale), bh = 58.0f * scale;
    const ImVec2 a(cx - bw * 0.5f, 14.0f * scale), b(a.x + bw, a.y + bh);
    dl->AddRectFilled(a, b, kUi.panel, 10.0f * scale);
    dl->AddRect(a, b, kUi.edge, 10.0f * scale, 0, 2.0f * scale);
    UiTextCentred(dl, font, 14.0f * scale, cx, a.y + 6.0f * scale, kUi.grey, "SPECTATING");
    UiTextCentred(dl, font, 24.0f * scale, cx, a.y + 24.0f * scale, kUi.white, SpectateName(), bw - 140.0f * scale);
    for (int side = 0; side < 2; side++) {
        const ImVec2 ba(side == 0 ? a.x + 8.0f * scale : b.x - 58.0f * scale, a.y + 8.0f * scale), bb(ba.x + 50.0f * scale, b.y - 8.0f * scale);
        if (UiButton(dl, font, scale, ba, bb, side == 0 ? "<" : ">", false, true)) CycleSpectate(side == 0 ? -1 : 1);
    }
    std::string under = IsLive(h) ? std::to_string(h.alive) + " still standing   |   you placed #" + std::to_string(gDeath.place) : "The match is over";
    UiTextCentred(dl, font, 15.0f * scale, cx, b.y + 6.0f * scale, kUi.grey, under + "   |   D-pad Left / Right to switch", ds.x - 32.0f * scale);
    if (IsLive(h)) {   // the card again: your numbers, and Leave
        const std::string label = "Options (B)";
        const float w = UiTextW(font, 18.0f * scale, label) + 28.0f * scale;
        const ImVec2 oa(cx - w * 0.5f, b.y + 30.0f * scale), ob(oa.x + w, oa.y + 38.0f * scale);
        if (UiButton(dl, font, scale, oa, ob, label, false, true)) { gDeath.card = true; gDeath.focus = 0; }
    }
}

// Step 4: the end screen, after Fortnite's. A slanted banner with your place (and a crown for a win), then two pages: the summary (your match
// stats on the left, how your points add up round a ring in the middle, the medals you earned on the right) and the standings (every player and
// bot, and the replay). R or Z (or the tabs) switch pages.

// Text with a dark outline, for the banner and the big numbers.
void UiTextOutlined(ImDrawList* dl, ImFont* font, float size, ImVec2 at, ImU32 col, const std::string& t) {
    const float o = std::max(1.5f, size * 0.05f);
    const ImU32 dark = IM_COL32(20, 10, 40, (col >> 24) & 0xFF);
    for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) if (dx || dy) dl->AddText(font, size, ImVec2(at.x + dx * o, at.y + dy * o), dark, t.c_str());
    dl->AddText(font, size, ImVec2(at.x + o * 1.4f, at.y + o * 2.2f), dark, t.c_str());
    dl->AddText(font, size, at, col, t.c_str());
}

void UiCrown(ImDrawList* dl, float cx, float base, float s, ImU32 col) {   // s: the width of one of its three points
    dl->AddRectFilled(ImVec2(cx - s * 1.5f, base - s * 0.55f), ImVec2(cx + s * 1.5f, base), col, s * 0.12f);
    for (int k = 0; k < 3; k++) {
        const float x = cx - s * 1.5f + k * s;
        dl->AddTriangleFilled(ImVec2(x, base - s * 0.5f), ImVec2(x + s, base - s * 0.5f), ImVec2(x + s * 0.5f, base - s * (k == 1 ? 1.9f : 1.5f)), col);
        dl->AddCircleFilled(ImVec2(x + s * 0.5f, base - s * (k == 1 ? 1.95f : 1.55f)), s * 0.16f, col, 10);
    }
}

// The banner: "#N" on the left, the words on a slanted, tilted ribbon. `cx, y` is its centre; `k` grows it (1 is normal).
void DrawEndBanner(ImDrawList* dl, ImFont* font, float scale, float cx, float y, int place, bool won, float alpha, float k) {
    const std::string words = won ? "VICTORY ROYALE!" : place > 0 ? "WELL PLAYED" : "MATCH OVER";
    const float ts = 40.0f * scale * k, bs = 72.0f * scale * k;
    const std::string badge = place > 0 ? "#" + std::to_string(place) : "";
    const float tw = UiTextW(font, ts, words), bw = badge.empty() ? 0.0f : UiTextW(font, bs, badge);
    const float h = 66.0f * scale * k, skew = 16.0f * scale * k, tilt = 7.0f * scale * k;
    const float w = tw + 70.0f * scale * k, x0 = cx - (w - bw * 0.45f) * 0.5f, x1 = x0 + w, top = y - h * 0.5f;
    // the ribbon, a little higher on the right, with a lighter top half
    const ImU32 dark = UiAlpha(won ? kUi.banner : kUi.bannerLoss, alpha), light = UiAlpha(won ? kUi.bannerLight : kUi.bannerLossLight, alpha);
    const ImVec2 a(x0 + skew, top + tilt), b(x1 + skew, top - tilt), c(x1 - skew, top + h - tilt), d(x0 - skew, top + h + tilt);
    dl->AddQuadFilled(ImVec2(a.x + 6 * scale, a.y + 8 * scale), ImVec2(b.x + 6 * scale, b.y + 8 * scale), ImVec2(c.x + 6 * scale, c.y + 8 * scale), ImVec2(d.x + 6 * scale, d.y + 8 * scale), IM_COL32(10, 0, 40, static_cast<int>(120 * alpha)));
    dl->AddQuadFilled(a, b, c, d, dark);
    dl->AddQuadFilled(a, b, ImVec2((b.x + c.x) * 0.5f, (b.y + c.y) * 0.5f), ImVec2((a.x + d.x) * 0.5f, (a.y + d.y) * 0.5f), light);
    dl->AddQuad(a, b, c, d, UiAlpha(IM_COL32(255, 255, 255, 120), alpha), 2.0f * scale);
    UiTextOutlined(dl, font, ts, ImVec2(cx - tw * 0.5f + bw * 0.22f, y - ts * 0.55f), UiAlpha(kUi.white, alpha), words);
    if (!badge.empty()) UiTextOutlined(dl, font, bs, ImVec2(x0 - bw * 0.62f, y - bs * 0.62f), UiAlpha(kUi.gold, alpha), badge);
    if (won) UiCrown(dl, cx + bw * 0.22f, top - 6.0f * scale * k, 18.0f * scale * k, UiAlpha(kUi.gold, alpha));
}

// A medal: what it is for, and its tier (0 bronze, 1 silver, 2 gold).
struct Medal { std::string name, what; int tier; };
std::vector<Medal> EarnedMedals(int place, int players, int kills, float damage, int chests) {
    std::vector<Medal> m;
    char buf[64];
    if (place == 1) m.push_back({ "VICTORY ROYALE", "The last one standing", 2 });
    if (gMyStats.firstStrike) m.push_back({ "FIRST STRIKE", "First elimination of the match", 2 });
    if (kills >= 10) m.push_back({ "DOUBLE DIGITS", std::to_string(kills) + " eliminations in a match", 2 });
    else if (kills > 0) m.push_back({ "BATTLE MEDAL", std::to_string(kills) + (kills == 1 ? " elimination" : " eliminations"), kills >= 5 ? 2 : kills >= 3 ? 1 : 0 });
    if (gMyStats.bestStreak >= 3) m.push_back({ "TRIPLE ELIM", "Three elims in quick succession", 2 });
    else if (gMyStats.bestStreak == 2) m.push_back({ "DOUBLE ELIM", "Two elims in quick succession", 1 });
    if (place > 1 && place <= 5) m.push_back({ "SURVIVOR", "Made the top 5", 2 });
    else if (place > 1 && place <= 10) m.push_back({ "SURVIVOR", "Made the top 10", 1 });
    else if (place > 1 && place <= std::max(2, players / 2)) m.push_back({ "SURVIVOR", "Outlasted half the players", 0 });
    if (damage >= 5.0f) { std::snprintf(buf, sizeof(buf), "%.1f hearts of damage dealt", damage); m.push_back({ "HEAVY HITTER", buf, damage >= 20.0f ? 2 : damage >= 10.0f ? 1 : 0 }); }
    if (chests > 0) m.push_back({ "SCAVENGER", "Searched " + std::to_string(chests) + (chests == 1 ? " chest" : " chests"), chests >= 8 ? 2 : chests >= 4 ? 1 : 0 });
    if (place == 1 && gMyStats.taken < 5.0f) { std::snprintf(buf, sizeof(buf), "Won taking only %.1f hearts", gMyStats.taken); m.push_back({ "UNTOUCHABLE", buf, 2 }); }
    if (gMyStats.hits >= 25) m.push_back({ "RELENTLESS", std::to_string(gMyStats.hits) + " hits landed", gMyStats.hits >= 60 ? 2 : 1 });
    if (gMyStats.distance >= 40000.0f) { std::snprintf(buf, sizeof(buf), "Travelled %.1f km", gMyStats.distance / 40000.0f); m.push_back({ "EXPLORER", buf, 1 }); }
    if (m.empty()) m.push_back({ "INTO THE FRAY", "Dropped in and fought", 0 });
    std::stable_sort(m.begin(), m.end(), [](const Medal& p, const Medal& q) { return p.tier > q.tier; });
    return m;
}

void DrawMedalIcon(ImDrawList* dl, ImVec2 c, float r, int tier, float alpha) {
    const ImU32 col = UiAlpha(tier == 2 ? kUi.medalGold : tier == 1 ? kUi.silver : kUi.bronze, alpha);
    dl->AddTriangleFilled(ImVec2(c.x - r * 0.75f, c.y - r * 0.2f), ImVec2(c.x - r * 0.1f, c.y), ImVec2(c.x - r * 0.55f, c.y + r * 1.25f), UiAlpha(IM_COL32(60, 120, 230, 255), alpha));   // the ribbon
    dl->AddTriangleFilled(ImVec2(c.x + r * 0.75f, c.y - r * 0.2f), ImVec2(c.x + r * 0.1f, c.y), ImVec2(c.x + r * 0.55f, c.y + r * 1.25f), UiAlpha(IM_COL32(60, 120, 230, 255), alpha));
    dl->AddCircleFilled(c, r, col, 24);
    dl->AddCircle(c, r * 0.78f, UiAlpha(IM_COL32(255, 255, 255, 140), alpha), 24, 1.5f);
    ImVec2 star[10];   // a five-pointed star in the middle
    for (int i = 0; i < 10; i++) {
        const float ang = -1.5707963f + i * 0.6283185f, rr = (i % 2 ? 0.25f : 0.58f) * r;
        star[i] = ImVec2(c.x + std::cos(ang) * rr, c.y + std::sin(ang) * rr);
    }
    for (int i = 0; i < 10; i += 2) dl->AddTriangleFilled(c, star[i], star[(i + 1) % 10], UiAlpha(IM_COL32(255, 255, 255, 230), alpha)),
                                    dl->AddTriangleFilled(c, star[(i + 9) % 10], star[i], UiAlpha(IM_COL32(255, 255, 255, 230), alpha));
}

void UiTick(ImDrawList* dl, ImVec2 at, float s, ImU32 col) {
    dl->AddLine(ImVec2(at.x, at.y + s * 0.5f), ImVec2(at.x + s * 0.38f, at.y + s * 0.9f), col, s * 0.22f);
    dl->AddLine(ImVec2(at.x + s * 0.38f, at.y + s * 0.9f), ImVec2(at.x + s, at.y), col, s * 0.22f);
}

std::string UiClock(double secs) {
    const int s = std::max(0, static_cast<int>(secs));
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

// The summary page inside (a, b).
void DrawEndSummary(ImDrawList* dl, ImFont* font, float scale, ImVec2 a, ImVec2 b, const royale::HudState& h, const royale::ResultsRow* me, int place,
                    float alpha, double t) {
    const float w = b.x - a.x, colW = w / 3.0f, pad = 14.0f * scale;
    const int kills = me ? me->kills : gMyStats.kills, chests = me ? me->chests : gMyStats.chests;
    const float damage = me ? me->damage : gMyStats.damage;
    const int score = me ? me->score : royale::ScorePoints(damage, kills, chests, place);
    const int players = std::max<int>(static_cast<int>(h.results.size()), h.playerLimit);
    char buf[32];

    // Left: match stats.
    {
        const float x0 = a.x + pad, x1 = a.x + colW - pad;
        UiText(dl, font, 18.0f * scale, ImVec2(x0, a.y), UiAlpha(kUi.label, alpha), "MATCH STATS");
        const double alive = (gDeath.active ? gDeath.at : gEnd.at) - gMyStats.startAt;
        std::vector<std::pair<std::string, std::string>> rows;
        rows.push_back({ "Place", place > 0 ? "#" + std::to_string(place) + " of " + std::to_string(players) : "-" });
        rows.push_back({ "Eliminations", std::to_string(kills) });
        std::snprintf(buf, sizeof(buf), "%.1f", damage); rows.push_back({ "Damage to players", buf });
        rows.push_back({ "Hits", std::to_string(gMyStats.hits) });
        std::snprintf(buf, sizeof(buf), "%.1f", gMyStats.taken); rows.push_back({ "Damage taken", buf });
        rows.push_back({ "Chests searched", std::to_string(chests) });
        if (gMyStats.startAt > 0 && alive > 0) rows.push_back({ "Time survived", UiClock(alive) });
        if (gMyStats.distance >= 40000.0f) std::snprintf(buf, sizeof(buf), "%.1f km", gMyStats.distance / 40000.0f);
        else std::snprintf(buf, sizeof(buf), "%d m", static_cast<int>(gMyStats.distance / 40.0f));
        rows.push_back({ "Distance travelled", buf });
        float y = a.y + 34.0f * scale;
        const float rowH = std::min(34.0f * scale, (b.y - y) / static_cast<float>(rows.size()));
        for (size_t i = 0; i < rows.size(); i++) {
            const float k = static_cast<float>(std::clamp((t - 0.08 * i) / 0.25, 0.0, 1.0));   // one after another
            const float size = std::min(21.0f * scale, rowH * 0.7f);
            UiText(dl, font, size, ImVec2(x0 + (1.0f - k) * 20.0f * scale, y), UiAlpha(kUi.label, alpha * k), rows[i].first);
            const float vw = UiTextW(font, size, rows[i].second);
            UiText(dl, font, size, ImVec2(x1 - vw, y), UiAlpha(kUi.white, alpha * k), rows[i].second);
            y += rowH;
        }
    }

    // Middle: the points, round a ring that fills, and what they came from, ticked off one by one.
    {
        const float cx = a.x + colW * 1.5f;
        const float r = std::min(colW * 0.32f, (b.y - a.y) * 0.24f);
        const ImVec2 c(cx, a.y + r + 14.0f * scale);
        const float fill = static_cast<float>(std::min(1.0, t / 1.1));
        const float ease = 1.0f - (1.0f - fill) * (1.0f - fill);
        dl->AddCircleFilled(c, r, UiAlpha(IM_COL32(10, 20, 70, 200), alpha), 64);
        dl->AddCircle(c, r, UiAlpha(IM_COL32(90, 225, 255, 60), alpha), 64, 10.0f * scale);
        dl->PathArcTo(c, r, -1.5707963f, -1.5707963f + 6.2831853f * ease, 64);
        dl->PathStroke(UiAlpha(kUi.accent, alpha), 0, 10.0f * scale);
        const std::string total = UiThousands(static_cast<int>(score * ease + 0.5f));
        UiTextCentred(dl, font, r * 0.62f, cx, c.y - r * 0.42f, UiAlpha(kUi.white, alpha), total, r * 1.7f);
        UiTextCentred(dl, font, r * 0.22f, cx, c.y + r * 0.28f, UiAlpha(kUi.label, alpha), "POINTS");

        const int pDamage = static_cast<int>(damage * royale::kPointsPerHeartOfDamage + 0.5f), pKills = kills * royale::kPointsPerKill;
        const int pChests = chests * royale::kPointsPerChest;
        const int pLasted = place > 0 ? (royale::kMaxPlayers - place) * royale::kPointsPerPlacementStep : 0, pWin = place == 1 ? royale::kPointsForWinning : 0;
        const int pOther = score - (pDamage + pKills + pChests + pLasted + pWin);
        std::vector<std::pair<int, std::string>> parts = { { pDamage + pKills, "Combat" }, { pChests, "Scavenging" }, { pLasted + pWin, "Survival" } };
        if (pOther > 0) parts.push_back({ pOther, "Mini bosses" });
        float y = c.y + r + 22.0f * scale;
        const float rowH = std::min(30.0f * scale, (b.y - y) / static_cast<float>(parts.size()));
        for (size_t i = 0; i < parts.size(); i++) {
            const float k = static_cast<float>(std::clamp((t - 0.6 - 0.25 * i) / 0.25, 0.0, 1.0));
            if (k <= 0.0f) break;
            const float size = std::min(22.0f * scale, rowH * 0.75f);
            const std::string n = UiThousands(parts[i].first);
            const float nw = UiTextW(font, size, n);
            UiTick(dl, ImVec2(cx - nw - 34.0f * scale, y + size * 0.15f), size * 0.7f, UiAlpha(kUi.accent, alpha * k));
            UiText(dl, font, size, ImVec2(cx - nw - 6.0f * scale, y), UiAlpha(kUi.white, alpha * k), n);
            UiText(dl, font, size * 0.8f, ImVec2(cx + 6.0f * scale, y + size * 0.15f), UiAlpha(kUi.label, alpha * k), parts[i].second);
            y += rowH;
        }
    }

    // Right: medals and accolades.
    {
        const float x0 = a.x + colW * 2.0f + pad, x1 = b.x - pad;
        UiText(dl, font, 18.0f * scale, ImVec2(x0, a.y), UiAlpha(kUi.label, alpha), "MEDALS & ACCOLADES");
        const std::vector<Medal> medals = EarnedMedals(place, players, kills, damage, chests);
        const float rowH = 54.0f * scale, r = 17.0f * scale;
        float y = a.y + 36.0f * scale;
        dl->PushClipRect(ImVec2(x0 - 4.0f * scale, y), ImVec2(x1 + 4.0f * scale, b.y), true);
        for (size_t i = 0; i < medals.size() && y < b.y; i++) {
            const float k = static_cast<float>(std::clamp((t - 0.3 - 0.15 * i) / 0.3, 0.0, 1.0));
            const float pop = 1.0f + 0.35f * (1.0f - k);
            DrawMedalIcon(dl, ImVec2(x0 + r, y + r + 2.0f * scale), r * pop, medals[i].tier, alpha * k);
            const float tx = x0 + r * 2.0f + 12.0f * scale, room = x1 - tx;
            UiTextCentred(dl, font, 17.0f * scale, tx + std::min(room, UiTextW(font, 17.0f * scale, medals[i].name)) * 0.5f, y, UiAlpha(kUi.white, alpha * k), medals[i].name, room);
            UiTextCentred(dl, font, 14.0f * scale, tx + std::min(room, UiTextW(font, 14.0f * scale, medals[i].what)) * 0.5f, y + 21.0f * scale, UiAlpha(kUi.label, alpha * k), medals[i].what, room);
            y += rowH;
        }
        dl->PopClipRect();
    }
}

// The standings page inside (a, b): everyone, and the replay beside them when there is room.
void DrawEndStandings(ImDrawList* dl, ImFont* font, float scale, ImVec2 a, ImVec2 b, const royale::HudState& h, float alpha) {
    float tableRight = b.x;
    royale::GameClient* client = gSession.Client();
    if (client != nullptr && client->ReplayComplete() && b.x - a.x > 760.0f * scale) {
        const float side = std::min(b.y - a.y - 56.0f * scale, (b.x - a.x) * 0.3f);
        if (side > 120.0f * scale) {
            tableRight = b.x - side - 20.0f * scale;
            if (alpha > 0.99f) DrawReplay(dl, font, ImVec2(b.x - side, a.y + 28.0f * scale), side, scale, h);
        }
    }
    const float x0 = a.x, x1 = tableRight, rowH = 32.0f * scale, headH = 28.0f * scale, bodyTop = a.y, bodyBottom = b.y;
    const float cRank = x0 + 10.0f * scale, cName = x0 + 58.0f * scale, cKills = x1 - 250.0f * scale, cDmg = x1 - 165.0f * scale, cScore = x1 - 80.0f * scale;
    const ImU32 head = UiAlpha(kUi.label, alpha);
    UiText(dl, font, 15.0f * scale, ImVec2(cRank, bodyTop), head, gEnd.byScore ? "#" : "# v", false);
    UiText(dl, font, 15.0f * scale, ImVec2(cName, bodyTop), head, "Player", false);
    UiText(dl, font, 15.0f * scale, ImVec2(cKills, bodyTop), head, "Kills", false);
    UiText(dl, font, 15.0f * scale, ImVec2(cDmg, bodyTop), head, "Damage", false);
    UiText(dl, font, 15.0f * scale, ImVec2(cScore, bodyTop), head, gEnd.byScore ? "Score v" : "Score", false);
    if (UiTapIn(ImVec2(x0, bodyTop - 4.0f * scale), ImVec2(x1, bodyTop + headH)) && alpha > 0.99f) { gEnd.byScore = !gEnd.byScore; gEnd.scroll = 0; UiSfx(NA_SE_SY_CURSOR); }
    dl->AddLine(ImVec2(x0, bodyTop + headH - 4.0f * scale), ImVec2(x1, bodyTop + headH - 4.0f * scale), UiAlpha(kUi.edge, alpha * 0.5f), 1.0f * scale);

    const ImVec2 la(x0, bodyTop + headH), lb(x1, bodyBottom);
    const float viewH = lb.y - la.y;
    std::vector<const royale::ResultsRow*> rows;
    for (const auto& r : h.results) rows.push_back(&r);
    if (!gEnd.byScore) std::stable_sort(rows.begin(), rows.end(), [](const royale::ResultsRow* p, const royale::ResultsRow* q) {
        return (p->placement > 0 ? p->placement : 999) < (q->placement > 0 ? q->placement : 999);
    });
    const float maxScroll = std::max(0.0f, static_cast<float>(rows.size()) * rowH - viewH);
    // scrolling: D-pad Up/Down, the mouse wheel, or dragging the list
    ImGuiIO& io = ImGui::GetIO();
    if (gUiPad.up) gEnd.scroll -= rowH * 3.0f;
    if (gUiPad.down) gEnd.scroll += rowH * 3.0f;
    const bool overList = io.MousePos.x >= la.x && io.MousePos.x <= lb.x && io.MousePos.y >= la.y && io.MousePos.y <= lb.y && !io.WantCaptureMouse;
    if (overList) gEnd.scroll -= io.MouseWheel * rowH * 2.0f;
    if (ImGui::IsMouseClicked(0) && overList) gEnd.dragging = true;
    if (!ImGui::IsMouseDown(0)) gEnd.dragging = false;
    if (gEnd.dragging) gEnd.scroll -= io.MouseDelta.y;
    gEnd.scroll = std::clamp(gEnd.scroll, 0.0f, maxScroll);

    dl->PushClipRect(la, lb, true);
    float ry = la.y - gEnd.scroll;
    int index = 0;
    for (const royale::ResultsRow* r : rows) {
        if (ry + rowH >= la.y && ry <= lb.y) {
            const ImVec2 ra(x0, ry), rb(x1, ry + rowH - 2.0f * scale);
            if (r->self) dl->AddRectFilled(ra, rb, UiAlpha(kUi.selfRow, alpha), 6.0f * scale);
            else if (index % 2) dl->AddRectFilled(ra, rb, UiAlpha(kUi.rowAlt, alpha), 6.0f * scale);
            const bool winner = r->placement == 1;
            const ImU32 col = UiAlpha(r->self ? kUi.green : winner ? kUi.gold : kUi.white, alpha);
            const float ty = ry + (rowH - 20.0f * scale) * 0.5f - 1.0f * scale;
            const int shownRank = gEnd.byScore ? index + 1 : r->placement;
            UiText(dl, font, 20.0f * scale, ImVec2(cRank, ty), col, shownRank > 0 ? std::to_string(shownRank) : "-", false);
            std::string name = r->name.empty() ? "Link" : r->name;
            if (r->self) name += " (you)";
            const float nameMax = cKills - cName - 70.0f * scale;
            float nameSize = 20.0f * scale;
            const float nw = UiTextW(font, nameSize, name);
            if (nw > nameMax && nw > 0) nameSize *= std::max(0.6f, nameMax / nw);
            UiText(dl, font, nameSize, ImVec2(cName, ty), col, name, false);
            float tagX = cName + UiTextW(font, nameSize, name) + 10.0f * scale;
            if (winner) { UiCrown(dl, tagX + 10.0f * scale, ry + rowH * 0.5f + 6.0f * scale, 7.0f * scale, UiAlpha(kUi.gold, alpha)); tagX += 30.0f * scale; }
            if (r->isBot) UiText(dl, font, 13.0f * scale, ImVec2(tagX, ty + 4.0f * scale), UiAlpha(kUi.grey, alpha), "BOT", false);
            UiText(dl, font, 20.0f * scale, ImVec2(cKills, ty), col, std::to_string(r->kills), false);
            char dmg[16];
            std::snprintf(dmg, sizeof(dmg), "%.1f", r->damage);
            UiText(dl, font, 20.0f * scale, ImVec2(cDmg, ty), col, dmg, false);
            UiText(dl, font, 20.0f * scale, ImVec2(cScore, ty), col, UiThousands(r->score), false);
        }
        ry += rowH;
        index++;
    }
    if (rows.empty()) UiTextCentred(dl, font, 18.0f * scale, (x0 + x1) * 0.5f, la.y + 20.0f * scale, UiAlpha(kUi.grey, alpha), "Waiting for the results...");
    dl->PopClipRect();
    if (maxScroll > 0) {   // a thin scroll bar
        const float barH = std::max(24.0f * scale, viewH * viewH / (viewH + maxScroll));
        const float barY = la.y + (viewH - barH) * (gEnd.scroll / maxScroll);
        dl->AddRectFilled(ImVec2(x1 + 4.0f * scale, barY), ImVec2(x1 + 8.0f * scale, barY + barH), UiAlpha(kUi.edge, alpha * 0.6f), 2.0f * scale);
    }
}

void DrawEndScreen(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const royale::HudState& h) {
    Feat("end screen");
    if (h.state != royale::MatchState::Ending) return;
    const double now = ImGui::GetTime(), t = now - gEnd.at;
    const float cx = ds.x * 0.5f;
    const bool won = h.winnerId == h.selfId && h.winnerId != royale::net::kNoPlayer16;
    const royale::ResultsRow* me = nullptr;
    for (const auto& r : h.results) if (r.self) me = &r;
    const int place = me != nullptr && me->placement > 0 ? me->placement : won ? 1 : gDeath.place;
    const std::string winnerLine = won ? "You are the last one standing" : !h.winnerName.empty() ? "Winner: " + h.winnerName : "Nobody survived";
    if (gEnd.open && t < kEndPanelDelay) {
        // Keep the winner, their crown and their emote visible. The recap must
        // not cover the world or capture movement during this celebration.
        DrawEndBanner(dl, font, scale, cx, 65.0f * scale, place, won, 1.0f, 0.8f);
        UiTextCentred(dl, font, 18.0f * scale, cx, 115.0f * scale, kUi.gold,
            won ? "Take a victory lap! Hold C-Right to emote" : winnerLine, ds.x * 0.9f);
        return;
    }

    if (!gEnd.open) {   // kept spectating: a chip to bring the results back
        const std::string label = "Results (D-pad Up)";
        const float w = UiTextW(font, 18.0f * scale, label) + 28.0f * scale;
        const ImVec2 a(ds.x - w - 16.0f * scale, 16.0f * scale), b(ds.x - 16.0f * scale, 16.0f * scale + 40.0f * scale);
        if (UiButton(dl, font, scale, a, b, label, false, true)) { gEnd.open = true; gEnd.at = now - kEndPanelDelay; }
        return;
    }

    // The violet tint over the game, and the banner: big in the middle first, then up to the top.
    const float tint = static_cast<float>(std::min(1.0, t / 0.6));
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ds, UiAlpha(kUi.bgTop, tint), UiAlpha(kUi.bgTop, tint), UiAlpha(kUi.bgBottom, tint), UiAlpha(kUi.bgBottom, tint));
    const float pop = static_cast<float>(std::min(1.0, t / 0.3));
    const float rise = static_cast<float>(std::clamp((t - kEndPanelDelay + 0.4) / 0.4, 0.0, 1.0));
    const float bannerY = ds.y * 0.4f + (60.0f * scale - ds.y * 0.4f) * (1.0f - (1.0f - rise) * (1.0f - rise));
    const float bannerK = (1.45f - 0.45f * rise) * (0.7f + 0.3f * pop);
    DrawEndBanner(dl, font, scale, cx, bannerY, place, won, pop, bannerK);
    if (t < kEndPanelDelay) {
        UiTextCentred(dl, font, 26.0f * scale, cx, bannerY + 70.0f * scale, UiAlpha(won ? kUi.white : kUi.gold, pop), winnerLine, ds.x * 0.9f);
        gUiPad = {};
        return;
    }
    const float a = static_cast<float>(std::min(1.0, (t - kEndPanelDelay) / 0.3));
    const double pt = t - kEndPanelDelay;   // the page's own clock, for its count-ups

    const float cw = std::min(ds.x - 40.0f * scale, 1120.0f * scale);
    const ImVec2 ca(cx - cw * 0.5f, 114.0f * scale);
    // Under the banner: who won and where, and the two tabs.
    UiTextCentred(dl, font, 17.0f * scale, cx, ca.y, UiAlpha(kUi.gold, a), winnerLine + "   |   " + CurrentMap().name, cw);
    if (gUiPad.page) { gEnd.page = 1 - gEnd.page; gEnd.scroll = 0; UiSfx(NA_SE_SY_CURSOR); }
    const char* tabs[2] = { "SUMMARY", "STANDINGS" };
    const float tabW = 150.0f * scale, tabY = ca.y + 28.0f * scale;
    for (int i = 0; i < 2; i++) {
        const ImVec2 ta(cx - tabW + i * tabW, tabY), tb(ta.x + tabW, tabY + 30.0f * scale);
        const bool on = gEnd.page == i;
        UiTextCentred(dl, font, 18.0f * scale, (ta.x + tb.x) * 0.5f, ta.y + 4.0f * scale, UiAlpha(on ? kUi.white : kUi.grey, a), tabs[i]);
        if (on) dl->AddRectFilled(ImVec2(ta.x + 20.0f * scale, tb.y - 3.0f * scale), ImVec2(tb.x - 20.0f * scale, tb.y), UiAlpha(kUi.accent, a), 2.0f * scale);
        if (!on && a > 0.99f && UiTapIn(ta, tb)) { gEnd.page = i; gEnd.scroll = 0; UiSfx(NA_SE_SY_CURSOR); }
    }

    // The buttons and the hint at the bottom; the page between.
    const float bh = 50.0f * scale, hintH = 22.0f * scale, by = ds.y - 16.0f * scale - hintH - bh;
    const ImVec2 pageA(ca.x, tabY + 46.0f * scale), pageB(ca.x + cw, by - 16.0f * scale);
    if (gEnd.page == 0) DrawEndSummary(dl, font, scale, pageA, pageB, h, me, place, a, pt);
    else DrawEndStandings(dl, font, scale, pageA, pageB, h, a);

    // Keep spectating / New match / Leave.
    const bool canNew = h.isHost;
    if (gUiPad.left || gUiPad.right) {
        const int dir = gUiPad.left ? -1 : 1;
        do { gEnd.focus = (gEnd.focus + 3 + dir) % 3; } while (gEnd.focus == 1 && !canNew);
        UiSfx(NA_SE_SY_CURSOR);
    }
    if (gUiPad.b) { gEnd.open = false; UiSfx(NA_SE_SY_FSEL_CLOSE); }
    gUiPad.left = gUiPad.right = gUiPad.up = gUiPad.down = gUiPad.b = gUiPad.page = false;
    if (!canNew && gEnd.focus == 1) gEnd.focus = 0;
    const float bw = std::min(260.0f * scale, (cw - 24.0f * scale) / 3.0f), gap = 12.0f * scale, bx = cx - bw * 1.5f - gap;
    if (UiButton(dl, font, scale, ImVec2(bx, by), ImVec2(bx + bw, by + bh), "Keep spectating", gEnd.focus == 0, true, a)) gEnd.open = false;
    if (UiButton(dl, font, scale, ImVec2(bx + bw + gap, by), ImVec2(bx + bw * 2 + gap, by + bh), "New match", gEnd.focus == 1, canNew, a))
        gSession.RequestPlayAgain();
    if (UiButton(dl, font, scale, ImVec2(bx + (bw + gap) * 2, by), ImVec2(bx + bw * 3 + gap * 2, by + bh), "Leave", gEnd.focus == 2, true, a)) { gSession.Leave(); return; }
    gUiPad.a = false;
    const std::string hint = std::string(canNew ? "D-pad Left / Right and A to choose" : "Only the host can start a new match") +
                             "   |   R: " + (gEnd.page == 0 ? "standings" : "summary") + "   |   B: keep spectating" + (gEnd.page == 1 ? "   |   D-pad Up / Down: scroll" : "");
    UiTextCentred(dl, font, 14.0f * scale, cx, ds.y - 16.0f * scale - hintH + 4.0f * scale, UiAlpha(kUi.grey, a), hint, cw);
}

void DrawUnderwaterOverlay(ImDrawList* dl, ImVec2 ds);   // the water section
void DrawOverlay() {
    if (WarMenuOpen()) return;
    DrawTitleLogo();
    DrawQuestLabel();
    if (!gSession.Joined()) return;
    royale::HudState h = gSession.Hud();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    DrawUnderwaterOverlay(dl, ds);
    const float scale = std::clamp(ds.y / 720.0f, 0.8f, 2.2f);
    const ImU32 white = HudColor(royale::wartable::theme::Ink), gold = HudColor(royale::wartable::theme::Gold), red = IM_COL32(255, 90, 90, 255),
                green = IM_COL32(110, 230, 130, 255);
    auto text = [&](float x, float y, ImU32 col, float size, const std::string& t) {
        dl->AddText(font, size, ImVec2(x + 1.5f, y + 1.5f), IM_COL32(0, 0, 0, 230), t.c_str());
        dl->AddText(font, size, ImVec2(x, y), col, t.c_str());
    };
    auto centered = [&](float y, ImU32 col, float size, const std::string& t) {
        ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, t.c_str());
        text((ds.x - sz.x) * 0.5f, y, col, size, t);
    };

    bool live = h.state == royale::MatchState::Drop || h.state == royale::MatchState::InMatch;

    const bool splashing = h.state == royale::MatchState::Countdown && h.countdownLeft > royale::kCountdownSec - kSplashSeconds;
    if (splashing) {
        DrawSplash(dl, font, ds, scale, royale::kCountdownSec - h.countdownLeft);
    } else if (h.state == royale::MatchState::Countdown) {
        centered(ds.y * 0.16f, gold, 46 * scale, "MATCH STARTS IN " + std::to_string(static_cast<int>(std::ceil(h.countdownLeft))));
        if (!InField()) centered(ds.y * 0.16f + 56 * scale, white, 24 * scale, "Heading to " + std::string(CurrentMap().name) + "...");
    }
    if (live && (h.state == royale::MatchState::Drop || gSandboxGlide) && gSkydiving) centered(ds.y * 0.2f, green, 22 * scale, "Hold Z to dive   Stick to steer");
    if (live && gSkydiving) DrawGliderAim(dl, font, ds, scale);

    DrawPickupFx(dl, ds, scale);
    DrawFeed(dl, font, ds, scale);
    DrawGains(dl, font, ds, scale);
    DrawHitEffects(dl, font, ds, scale);
    DrawBanners(dl, font, ds, scale);
    DrawDeathScreen(dl, font, ds, scale, h);   // going down, the death card, spectating
    DrawEndScreen(dl, font, ds, scale, h);     // the match is over

    if (!live || !h.haveSelf) return;

    // What is at your feet: chests say how rare they are (not what is inside); items on the ground say what they are.
    // One line: a green A button, then what pressing it does.
    auto prompt = [&](ImU32 col, const std::string& t) {
        const float size = 24 * scale, btn = 13 * scale, gap = 8 * scale, y = ds.y * 0.66f;
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, t.c_str());
        const float x = (ds.x - sz.x - btn * 2 - gap) * 0.5f;
        const ImVec2 c(x + btn, y + sz.y * 0.5f);
        dl->AddCircleFilled(ImVec2(c.x + 1.5f, c.y + 1.5f), btn, IM_COL32(0, 0, 0, 200), 20);
        dl->AddCircleFilled(c, btn, IM_COL32(70, 200, 90, 255), 20);
        const ImVec2 asz = font->CalcTextSizeA(btn * 1.4f, FLT_MAX, 0.0f, "A");
        dl->AddText(font, btn * 1.4f, ImVec2(c.x - asz.x * 0.5f, c.y - asz.y * 0.5f), white, "A");
        text(x + btn * 2 + gap, y, col, size, t);
    };
    if (InField() && h.selfAlive && gSession.Client()) {
        const size_t near = NearestLootIndex();
        const auto& loot = gSession.Client()->Loot();
        if (near != kNoLoot && near < loot.size()) {
            const royale::Rarity r = static_cast<royale::Rarity>(loot[near].rarity);
            if (loot[near].chest) prompt(loot[near].special ? IM_COL32(255, 130, 190, 255) : RarityU32(r), loot[near].special ? std::string("Heart Container Chest") : std::string(RarityName(r)) + " Chest");
            else prompt(RarityU32(r), ItemLabel(static_cast<royale::ItemId>(loot[near].item), r));
        }
        else if (const std::string cart = CartPromptText(); !cart.empty()) {
            prompt(IM_COL32(255, 210, 140, 255), cart);
        } else if (const int ally = NearbyFreeAlly(); ally >= 0) {
            const royale::AllyDef& def = royale::kAllyDefs[ally];
            const bool afford = h.rupees >= def.price;
            prompt(afford ? IM_COL32(255, 222, 110, 255) : IM_COL32(255, 130, 120, 255), "Hire " + std::string(def.name) + " (" + std::to_string(def.price) + " rupees)");
        } else if (MessageBoxUp()) {
            // reading: the text box has the screen
        } else if (MayaNear()) {
            prompt(IM_COL32(255, 170, 215, 255), "Talk to Maya");
        } else if (LiloNear()) {
            prompt(IM_COL32(235, 235, 230, 255), "Talk to Lilo");
        } else if (SignNear()) {
            prompt(IM_COL32(255, 232, 160, 255), "Read the sign");
        }
    }
    if (ImGui::GetTime() < gBannerUntil) {
        const float fade = static_cast<float>(std::min(1.0, gBannerUntil - ImGui::GetTime()));
        ImU32 col = RarityU32(gBannerRarity);
        col = (col & 0x00FFFFFF) | (static_cast<ImU32>(255 * fade) << 24);
        centered(ds.y * 0.58f, col, 30 * scale, gBannerText);
    }
    DrawWeather(dl, ds, scale, h);
    DrawStorm(dl, ds, scale, h);
    DrawBossBars(dl, font, scale);
    DrawPoiLabels(dl, font, ds, scale, h);
    DrawSign(dl, font, ds, scale);
    DrawMaya(dl, font, ds, scale);
    DrawLilo(dl, font, ds, scale);
    DrawFartReactions(dl, font, scale);
    DrawAllyLabels(dl, font, ds, scale, h);
    DrawCartHud(dl, font, ds, scale);
    DrawMinimap(dl, ds, scale, h);
    DrawHotbar(dl, font, ds, scale, h);
    DrawEmotes(dl, font, ds, scale, h);

    DrawOotVitals(dl, scale, h);
    DrawStaminaBar(dl, ds, h);

    // Top right: the match at a glance (where the game's C buttons used to be; the hotbar at the bottom does their job now). Right-aligned,
    // under the safe-zone compass: how many are left, the zone timer and anything that is affecting you.
    {
        const float rx = ds.x - 16 * scale, line = 24 * scale;
        float y = 150 * scale;
        auto textR = [&](ImU32 col, float size, const std::string& t) {
            const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, t.c_str());
            text(rx - sz.x, y, col, size, t);
            y += size + 4 * scale;
        };
        textR(gold, 24 * scale, "ALIVE " + std::to_string(h.alive));
        if (h.stormPhase >= royale::kStormPhaseCount) textR(red, 20 * scale, "Final zone");
        else textR(h.stormShrinking || h.stormSecondsLeft <= 5.0f ? red : h.stormSecondsLeft <= 15.0f ? gold : white, 20 * scale, (h.stormShrinking ? "Closing " : "Zone ") + ClockText(h.stormSecondsLeft));
        // Only what is happening to you right now, short. Gear is already shown as icons above the item bar, the weather is on the screen itself.
        if (h.selfAlive) {
            if (h.adultLeft > 0.0f) textR(gold, 18 * scale, "Adult " + ClockText(h.adultLeft));
            if (h.invulnLeft > 0) textR(gold, 18 * scale, "Invulnerable " + ClockText(h.invulnLeft));
            if (h.speedLeft > 0) textR(green, 18 * scale, "Speed " + ClockText(h.speedLeft));
            if (h.revealLeft > 0) textR(green, 18 * scale, "Reveal " + ClockText(h.revealLeft));
            if (h.shieldLeft > 0) textR(green, 18 * scale, "Guard " + ClockText(h.shieldLeft));
            if (h.burnLeft > 0) textR(red, 18 * scale, "Burning");
            if (h.stunLeft > 0) textR(red, 18 * scale, "Stunned");
            if (h.inv.hasMark) textR(green, 16 * scale, "Farore's Wind set");
        }
    }

    // Top right: which way is the safe zone, relative to the way Link is facing.
    if (InField() && h.selfAlive) {
        Player* p = GET_PLAYER(gPlayState);
        float dx = h.safeZone.center.x - p->actor.world.pos.x, dz = h.safeZone.center.z - p->actor.world.pos.z;
        bool outside = std::sqrt(dx * dx + dz * dz) > h.safeZone.radius;
        float rel = std::atan2(dx, dz) - p->actor.shape.rot.y * (3.14159265f / 32768.0f);
        ImVec2 c(ds.x - 74 * scale, 84 * scale);
        float r = 34 * scale;
        auto pt = [&](float angle, float len) { return ImVec2(c.x + std::sin(angle) * len, c.y - std::cos(angle) * len); };
        dl->AddCircleFilled(c, r + 10 * scale, IM_COL32(0, 0, 0, 120));
        dl->AddTriangleFilled(pt(rel, r), pt(rel + 2.5f, r * 0.75f), pt(rel - 2.5f, r * 0.75f), outside ? red : green);
        if (outside) {
            ImVec2 sz = font->CalcTextSizeA(16 * scale, FLT_MAX, 0.0f, "SAFE ZONE");
            text(c.x - sz.x * 0.5f, c.y + r + 14 * scale, red, 16 * scale, "SAFE ZONE");
        }
    }
}

class RoyaleHudWindow : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void InitElement() override {}
    void UpdateElement() override {}
    void DrawElement() override {}
    void Draw() override {
        if (IsVisible()) DrawOverlay();
    }
};

// The overlay is a window with no frame of its own: it registers once, the first time the game loop runs (the GUI exists by then).
void EnsureHudWindow() {
    static bool done = false;
    if (done) return;
    done = true;
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    gui->AddGuiWindow(std::make_shared<RoyaleHudWindow>(CVAR_WINDOW("RoyaleHud"), true, "Royale HUD", ImVec2(0, 0)));
}

