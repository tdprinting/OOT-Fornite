// ---- the sign in the middle of the map -----------------------------------------------------------------------------------------
// A wooden sign stands at the centre of every map (on the nearest bit of open, walkable ground), and reads out its message when you walk up.
Actor* gSignActor = nullptr;
double gSignReadAt = -100.0;

void Sign_Update(Actor* actor, PlayState* play) {
    actor->focus.pos = actor->world.pos;
    actor->focus.pos.y += 120.0f;
    OfferTalk(actor, play, kTextSign, 220.0f);   // read it with A, in the game's wooden sign box
}
void Sign_Draw(Actor* actor, PlayState* play) {
    const GpuMesh* mesh = GpuMeshFor(royale::MeshKind::Sign, 0);
    if (mesh == nullptr || mesh->dl.empty()) return;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(actor->shape.rot.y * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    Matrix_Scale(1.35f, 1.35f, 1.35f, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(mesh->dl.data()));
    CLOSE_DISPS(play->state.gfxCtx);
}
void Sign_Destroy(Actor* actor, PlayState*) { if (gSignActor == actor) gSignActor = nullptr; }

// The spot for the sign: the walkable ground nearest the middle that is clear of walls, rocks and blocks.
bool FindSignSpot(const royale::Circle& map, royale::Vec2* out) {
    if (!gSession.Client()) return false;
    const auto& props = gSession.Client()->Props();
    auto clear = [&](royale::Vec2 p) {
        if (!WalkableAt(p)) return false;
        for (const royale::Prop& pr : props) {
            const float r = royale::PropRadius(pr.kind);
            if (r > 0.0f && royale::Distance(pr.pos, p) < r + 120.0f) return false;
        }
        return true;
    };
    for (float r = 0.0f; r <= 1100.0f; r += 70.0f) {
        const int n = r < 1.0f ? 1 : std::max(8, static_cast<int>(r / 28.0f));
        for (int i = 0; i < n; i++) {
            const float a = i * 6.2831853f / n;
            const royale::Vec2 p = { map.center.x + std::cos(a) * r, map.center.z + std::sin(a) * r };
            if (clear(p)) { *out = p; return true; }
        }
    }
    return false;
}

void ReconcileSign(const royale::HudState& hud) {
    const bool want = gSession.Joined() && InField() && hud.map.radius > 0 &&
                      (hud.state == royale::MatchState::Countdown || hud.state == royale::MatchState::Drop || hud.state == royale::MatchState::InMatch);
    if (!want) {
        if (gSignActor != nullptr) { Actor_Kill(gSignActor); gSignActor = nullptr; }
        gSignKnown = false;
        return;
    }
    if (gSignActor != nullptr) return;
    if (!gSignKnown) { if (!FindSignSpot(hud.map, &gSignPos)) return; gSignKnown = true; }
    float y = 0;
    if (!FloorAt(gSignPos.x, gSignPos.z, &y)) return;
    Actor* a = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ISHI, gSignPos.x, y, gSignPos.z, 0, 0x2000, 0, 0, false);
    if (a == nullptr) return;
    a->update = Sign_Update;
    a->draw = Sign_Draw;
    a->destroy = Sign_Destroy;
    a->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
    a->uncullZoneForward = 4000.0f; a->uncullZoneScale = 1500.0f; a->uncullZoneDownward = 1500.0f;
    a->shape.shadowScale = 0.0f;
    gSignActor = a;
}

bool SignNear() {
    if (gSignActor == nullptr || !InField()) return false;
    Player* pl = GET_PLAYER(gPlayState);
    return std::hypot(pl->actor.world.pos.x - gSignPos.x, pl->actor.world.pos.z - gSignPos.z) < 220.0f;
}

// A next to Lilo, Maya or the sign when the game did not already start the talk itself: open the same text box directly.
void PressedATalk() {
    Player* player = GET_PLAYER(gPlayState);
    Actor* companion = gMayaCompanion.actor;
    if (companion && std::fabs(player->linearVelocity) < 3.0f &&
        std::hypot(player->actor.world.pos.x-companion->world.pos.x, player->actor.world.pos.z-companion->world.pos.z) < 140.0f) {
        if (StartTalk(companion, static_cast<u16>(kTextMayaCompanion+gMayaCompanion.line))) gMayaCompanion.talking=true;
    }
    else if (MayaNear()) { if (StartTalk(gMayaActor, kTextMaya)) TalkToMaya(); }
    else if (LiloNear()) { if (StartTalk(gLiloActor, kTextLilo)) TalkToLilo(); }
    else if (SignNear()) StartTalk(gSignActor, kTextSign);
}

// A label over the sign from a distance; up close, A reads it in the game's own text box.
void DrawSign(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale) {
    (void)ds;
    if (gSignActor == nullptr || !InField()) return;
    Player* pl = GET_PLAYER(gPlayState);
    const float d = std::hypot(pl->actor.world.pos.x - gSignPos.x, pl->actor.world.pos.z - gSignPos.z);
    ImVec2 at;
    if (d >= 260.0f && d < 1500.0f && WorldToScreen(gSignPos.x, gSignActor->world.pos.y + 240.0f, gSignPos.z, &at)) {   // up close, the prompt says to read it
        const char* label = "Sign";
        const float size = std::clamp(26.0f * scale * (1800.0f / (d + 900.0f)), 14.0f * scale, 30.0f * scale);
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label);
        dl->AddText(font, size, ImVec2(at.x - sz.x * 0.5f + 2, at.y + 2), IM_COL32(20, 12, 4, 230), label);
        dl->AddText(font, size, ImVec2(at.x - sz.x * 0.5f, at.y), IM_COL32(255, 232, 160, 255), label);
    }
}

// ---- waiting-room reef aquarium ------------------------------------------------------------------------------------------------
#include "RoyaleLobbyFish.h"

// ---- match music ----------------------------------------------------------------------------------------------------------------
// "Match music" in the lobby menu: 0 the game's own music as usual, 1 a random song from the music folder (with the game's music turned down),
// 2 silence. The game's music volume is put back to the player's setting whenever a match is not on.
bool gBgmMuted = false;
void SetGameBgmVolume(bool muted) {
    if (muted == gBgmMuted) return;
    gBgmMuted = muted;
    gOot.lastVolume = -1;   // a converted song on the second music player keeps its own volume (set again on the next update)
    const float main = muted ? 0.0f : static_cast<float>(CVarGetInteger(CVAR_SETTING("Volume.MainMusic"), 100)) / 100.0f;
    const float sub = muted ? 0.0f : static_cast<float>(CVarGetInteger(CVAR_SETTING("Volume.SubMusic"), 100)) / 100.0f;
    Audio_SetGameVolume(SEQ_PLAYER_BGM_MAIN, main);
    Audio_SetGameVolume(SEQ_PLAYER_BGM_SUB, sub);
}
bool DriveMatchMusic(const royale::HudState& hud, bool joined) {
    const bool live = joined && InField() && (hud.state == royale::MatchState::Countdown || hud.state == royale::MatchState::Drop || hud.state == royale::MatchState::InMatch);
    const bool random = live && gMusicMode == 1;
    if (random && !gLobbyMusic.scanned) ScanMusicFolder();
    const bool haveSongs = !gLobbyMusic.tracks.empty();
    const bool lobbySongs = joined && hud.state == royale::MatchState::Lobby && MapOption("LobbyMusic", true);
    if (lobbySongs && !gLobbyMusic.scanned) ScanMusicFolder();
    SetGameBgmVolume((live && (gMusicMode == 2 || (gMusicMode == 1 && haveSongs))) || (lobbySongs && !gLobbyMusic.tracks.empty() && !gLobbyMusic.failed));
    return random && haveSongs;
}

// ---- the pause menu shows what you carry -----------------------------------------------------------------------------------------
// While a match is on, the game's own pause-menu inventory (items, equipment, quest items and ammo) is rebuilt from what the server says you carry, so
// everything you pick up shows up there and everything you lose goes. Your own save is put back exactly as it was when the match ends or you leave.
// The C-button items are cleared for the match too (the hotbar does their job, and nothing real should fire from them).
struct SavedInventory {
    bool have = false;
    decltype(gSaveContext.inventory) inventory;
    decltype(gSaveContext.equips) equips;
};
SavedInventory gSavedInv;
uint64_t gKitHash = 0;

void GiveToSave(royale::ItemId id, int& bottles, int& tunics) {
    using royale::ItemId;
    auto put = [&](int item) { gSaveContext.inventory.items[SLOT(item)] = static_cast<u8>(item); };
    auto equip = [&](int type, int value) { gSaveContext.inventory.equipment |= OWNED_EQUIP_FLAG(type, value); };
    auto quest = [&](int q) { gSaveContext.inventory.questItems |= gBitFlags[q]; };
    auto bottle = [&](int item) { if (bottles < 4) gSaveContext.inventory.items[SLOT_BOTTLE_1 + bottles++] = static_cast<u8>(item); };
    (void)tunics;
    switch (id) {
        case ItemId::DekuStick: put(ITEM_STICK); break;
        case ItemId::KokiriSword: case ItemId::BasicSword: equip(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_KOKIRI); break;
        case ItemId::MasterSword: case ItemId::GildedSword: equip(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_MASTER); break;
        case ItemId::BiggoronSword: equip(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_BIGGORON); break;
        case ItemId::MegatonHammer: case ItemId::GiantsHammer: put(ITEM_HAMMER); break;
        case ItemId::Slingshot: case ItemId::TripleSlingshot: put(ITEM_SLINGSHOT); break;
        case ItemId::FairyBow: put(ITEM_BOW); break;
        case ItemId::Boomerang: put(ITEM_BOOMERANG); break;
        case ItemId::Bombs: put(ITEM_BOMB); break;
        case ItemId::Bombchus: case ItemId::HomingBombchus: put(ITEM_BOMBCHU); break;
        case ItemId::DekuNuts: put(ITEM_NUT); break;
        case ItemId::FireArrows: put(ITEM_BOW_ARROW_FIRE); break;
        case ItemId::IceArrows: put(ITEM_BOW_ARROW_ICE); break;
        case ItemId::LightArrows: put(ITEM_BOW_ARROW_LIGHT); break;
        case ItemId::DekuShield: equip(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_DEKU); break;
        case ItemId::HylianShield: equip(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_HYLIAN); break;
        case ItemId::MirrorShield: equip(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_MIRROR); break;
        case ItemId::GreenPotion: bottle(ITEM_POTION_GREEN); break;
        case ItemId::RedPotion: bottle(ITEM_POTION_RED); break;
        case ItemId::BluePotion: bottle(ITEM_POTION_BLUE); break;
        case ItemId::Fairy: bottle(ITEM_FAIRY); break;
        case ItemId::Milk: bottle(ITEM_MILK_BOTTLE); break;
        case ItemId::Fish: bottle(ITEM_FISH); break;
        case ItemId::BlueFire: bottle(ITEM_BLUE_FIRE); break;
        case ItemId::Bug: bottle(ITEM_BUG); break;
        case ItemId::Poe: bottle(ITEM_POE); break;
        case ItemId::SmallShieldPotion: case ItemId::LargeShieldPotion: bottle(ITEM_BOTTLE); break;
        case ItemId::DinsFire: put(ITEM_DINS_FIRE); break;
        case ItemId::FaroresWind: put(ITEM_FARORES_WIND); break;
        case ItemId::NayrusLove: put(ITEM_NAYRUS_LOVE); break;
        case ItemId::Hookshot: put(ITEM_HOOKSHOT); break;
        case ItemId::Longshot: put(ITEM_LONGSHOT); break;
        case ItemId::LensOfTruth: put(ITEM_LENS); break;
        case ItemId::MagicBeans: put(ITEM_BEAN); break;
        case ItemId::FairyOcarina: put(ITEM_OCARINA_FAIRY); break;
        case ItemId::OcarinaOfTime: put(ITEM_OCARINA_TIME); break;
        case ItemId::ZeldasLullaby: quest(QUEST_SONG_LULLABY); break;
        case ItemId::EponasSong: quest(QUEST_SONG_EPONA); break;
        case ItemId::SariasSong: quest(QUEST_SONG_SARIA); break;
        case ItemId::SunsSong: quest(QUEST_SONG_SUN); break;
        case ItemId::SongOfTime: quest(QUEST_SONG_TIME); break;
        case ItemId::SongOfStorms: quest(QUEST_SONG_STORMS); break;
        case ItemId::MinuetOfForest: quest(QUEST_SONG_MINUET); break;
        case ItemId::BoleroOfFire: quest(QUEST_SONG_BOLERO); break;
        case ItemId::SerenadeOfWater: quest(QUEST_SONG_SERENADE); break;
        case ItemId::NocturneOfShadow: quest(QUEST_SONG_NOCTURNE); break;
        case ItemId::RequiemOfSpirit: quest(QUEST_SONG_REQUIEM); break;
        case ItemId::PreludeOfLight: quest(QUEST_SONG_PRELUDE); break;
        case ItemId::KokiriTunic: equip(EQUIP_TYPE_TUNIC, EQUIP_VALUE_TUNIC_KOKIRI); break;
        case ItemId::GoronTunic: equip(EQUIP_TYPE_TUNIC, EQUIP_VALUE_TUNIC_GORON); break;
        case ItemId::ZoraTunic: equip(EQUIP_TYPE_TUNIC, EQUIP_VALUE_TUNIC_ZORA); break;
        case ItemId::KokiriBoots: equip(EQUIP_TYPE_BOOTS, EQUIP_VALUE_BOOTS_KOKIRI); break;
        case ItemId::IronBoots: equip(EQUIP_TYPE_BOOTS, EQUIP_VALUE_BOOTS_IRON); break;
        case ItemId::HoverBoots: equip(EQUIP_TYPE_BOOTS, EQUIP_VALUE_BOOTS_HOVER); break;
        case ItemId::GoronBracelet: Inventory_ChangeUpgrade(UPG_STRENGTH, 1); break;
        case ItemId::SilverGauntlets: Inventory_ChangeUpgrade(UPG_STRENGTH, 2); break;
        case ItemId::GoldenGauntlets: Inventory_ChangeUpgrade(UPG_STRENGTH, 3); break;
        case ItemId::SilverScale: Inventory_ChangeUpgrade(UPG_SCALE, 1); break;
        case ItemId::GoldenScale: Inventory_ChangeUpgrade(UPG_SCALE, 2); break;
        case ItemId::BigQuiver: Inventory_ChangeUpgrade(UPG_QUIVER, 3); break;
        case ItemId::BulletBag: Inventory_ChangeUpgrade(UPG_BULLET_BAG, 3); break;
        case ItemId::BombBag: Inventory_ChangeUpgrade(UPG_BOMB_BAG, 3); break;
        case ItemId::KeatonMask: gSaveContext.inventory.items[SLOT_TRADE_CHILD] = ITEM_MASK_KEATON; break;
        case ItemId::SkullMask: gSaveContext.inventory.items[SLOT_TRADE_CHILD] = ITEM_MASK_SKULL; break;
        case ItemId::SpookyMask: gSaveContext.inventory.items[SLOT_TRADE_CHILD] = ITEM_MASK_SPOOKY; break;
        case ItemId::BunnyHood: gSaveContext.inventory.items[SLOT_TRADE_CHILD] = ITEM_MASK_BUNNY; break;
        case ItemId::GoronMask: gSaveContext.inventory.items[SLOT_TRADE_CHILD] = ITEM_MASK_GORON; break;
        case ItemId::ZoraMask: gSaveContext.inventory.items[SLOT_TRADE_CHILD] = ITEM_MASK_ZORA; break;
        case ItemId::GerudoMask: gSaveContext.inventory.items[SLOT_TRADE_CHILD] = ITEM_MASK_GERUDO; break;
        case ItemId::MaskOfTruth: gSaveContext.inventory.items[SLOT_TRADE_CHILD] = ITEM_MASK_TRUTH; break;
        case ItemId::ForestMedallion: quest(QUEST_MEDALLION_FOREST); break;
        case ItemId::FireMedallion: quest(QUEST_MEDALLION_FIRE); break;
        case ItemId::WaterMedallion: quest(QUEST_MEDALLION_WATER); break;
        case ItemId::SpiritMedallion: quest(QUEST_MEDALLION_SPIRIT); break;
        case ItemId::ShadowMedallion: quest(QUEST_MEDALLION_SHADOW); break;
        case ItemId::LightMedallion: quest(QUEST_MEDALLION_LIGHT); break;
        case ItemId::KokiriEmerald: quest(QUEST_KOKIRI_EMERALD); break;
        case ItemId::GoronRuby: quest(QUEST_GORON_RUBY); break;
        case ItemId::ZoraSapphire: quest(QUEST_ZORA_SAPPHIRE); break;
        default: break;   // hearts, magic, rupees, grenades and the like have no place in the pause menu
    }
}

void SyncPauseInventory(const royale::HudState& hud) {
    const bool active = gSession.Joined() && IsLive(hud) && hud.haveSelf;
    if (!active) {
        if (gSavedInv.have) {   // the match is over: your own inventory comes back untouched
            gSaveContext.inventory = gSavedInv.inventory;
            gSaveContext.equips = gSavedInv.equips;
            gSavedInv.have = false;
            gKitHash = 0;
            if (InGame()) Player_SetEquipmentData(gPlayState, GET_PLAYER(gPlayState));   // and Link wears your own shield again
        }
        return;
    }
    if (!gSavedInv.have) {
        gSavedInv.inventory = gSaveContext.inventory;
        gSavedInv.equips = gSaveContext.equips;
        gSavedInv.have = true;
        gKitHash = 0;
    }
    // Cheap change detection: the whole kit folded into one number.
    uint64_t hsh = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { hsh = (hsh ^ v) * 1099511628211ull; };
    mix(static_cast<uint64_t>(hud.weapon)); mix(hud.hasShield ? static_cast<uint64_t>(hud.shield) + 1 : 0);
    for (const auto& r : hud.inv.reserve) mix(100 + r.item);
    for (const auto& pt : hud.inv.potions) mix(200 + pt.item);
    if (hud.inv.hasAbility) mix(300 + hud.inv.ability.item);
    mix(hud.inv.gearMask);
    for (int sl = 0; sl < royale::kGearSlots; sl++) if (hud.inv.gearMask & (1 << sl)) mix(400 + hud.inv.gear[sl].item);
    for (int a = 0; a < 5; a++) mix(500 + static_cast<uint64_t>(std::min(127, std::max(0, static_cast<int>(hud.ammo[a])))));
    if (hsh == gKitHash) { for (int i = 1; i <= 3; i++) gSaveContext.equips.buttonItems[i] = ITEM_NONE; return; }
    gKitHash = hsh;

    auto& inv = gSaveContext.inventory;
    for (auto& it : inv.items) it = ITEM_NONE;
    for (auto& am : inv.ammo) am = 0;
    inv.equipment = 0;
    inv.upgrades &= ~(gUpgradeMasks[UPG_QUIVER] | gUpgradeMasks[UPG_BOMB_BAG] | gUpgradeMasks[UPG_STRENGTH] | gUpgradeMasks[UPG_BULLET_BAG] | gUpgradeMasks[UPG_SCALE]);
    inv.questItems &= ~(gBitFlags[QUEST_MEDALLION_FOREST] | gBitFlags[QUEST_MEDALLION_FIRE] | gBitFlags[QUEST_MEDALLION_WATER] | gBitFlags[QUEST_MEDALLION_SPIRIT] |
                        gBitFlags[QUEST_MEDALLION_SHADOW] | gBitFlags[QUEST_MEDALLION_LIGHT] | gBitFlags[QUEST_KOKIRI_EMERALD] | gBitFlags[QUEST_GORON_RUBY] | gBitFlags[QUEST_ZORA_SAPPHIRE] |
                        gBitFlags[QUEST_SONG_MINUET] | gBitFlags[QUEST_SONG_BOLERO] | gBitFlags[QUEST_SONG_SERENADE] | gBitFlags[QUEST_SONG_REQUIEM] | gBitFlags[QUEST_SONG_NOCTURNE] |
                        gBitFlags[QUEST_SONG_PRELUDE] | gBitFlags[QUEST_SONG_LULLABY] | gBitFlags[QUEST_SONG_EPONA] | gBitFlags[QUEST_SONG_SARIA] | gBitFlags[QUEST_SONG_SUN] |
                        gBitFlags[QUEST_SONG_TIME] | gBitFlags[QUEST_SONG_STORMS]);
    int bottles = 0, tunics = 0;
    GiveToSave(hud.weapon, bottles, tunics);
    if (hud.hasShield) GiveToSave(hud.shield, bottles, tunics);
    for (const auto& r : hud.inv.reserve) GiveToSave(static_cast<royale::ItemId>(r.item), bottles, tunics);
    for (const auto& pt : hud.inv.potions) GiveToSave(static_cast<royale::ItemId>(pt.item), bottles, tunics);
    if (hud.inv.hasAbility) GiveToSave(static_cast<royale::ItemId>(hud.inv.ability.item), bottles, tunics);
    for (int sl = 0; sl < royale::kGearSlots; sl++) if (hud.inv.gearMask & (1 << sl)) GiveToSave(static_cast<royale::ItemId>(hud.inv.gear[sl].item), bottles, tunics);
    // Ammo: the counts the server keeps, shown against the weapon that uses them (a bag big enough for them is given too).
    auto ammo = [&](royale::AmmoKind k, int item, int upgrade) {
        const int n = std::min(127, std::max(0, static_cast<int>(hud.ammo[static_cast<size_t>(k)])));
        if (n > 0 && inv.items[SLOT(item)] == item) {
            inv.ammo[SLOT(item)] = static_cast<s8>(n);
            if (upgrade >= 0 && CUR_UPG_VALUE(upgrade) == 0) Inventory_ChangeUpgrade(upgrade, n > 30 ? 3 : n > 20 ? 2 : 1);
        }
    };
    ammo(royale::AmmoKind::Arrows, ITEM_BOW, UPG_QUIVER);
    ammo(royale::AmmoKind::Seeds, ITEM_SLINGSHOT, UPG_BULLET_BAG);
    ammo(royale::AmmoKind::Bombs, ITEM_BOMB, UPG_BOMB_BAG);
    ammo(royale::AmmoKind::Bombchus, ITEM_BOMBCHU, -1);
    ammo(royale::AmmoKind::Nuts, ITEM_NUT, -1);
    if (inv.items[SLOT(ITEM_BOW)] == ITEM_BOW && CUR_UPG_VALUE(UPG_QUIVER) == 0) Inventory_ChangeUpgrade(UPG_QUIVER, 1);
    if (inv.items[SLOT(ITEM_SLINGSHOT)] == ITEM_SLINGSHOT && CUR_UPG_VALUE(UPG_BULLET_BAG) == 0) Inventory_ChangeUpgrade(UPG_BULLET_BAG, 1);
    if (inv.items[SLOT(ITEM_BOMB)] == ITEM_BOMB && CUR_UPG_VALUE(UPG_BOMB_BAG) == 0) Inventory_ChangeUpgrade(UPG_BOMB_BAG, 1);
    for (int i = 1; i <= 3; i++) gSaveContext.equips.buttonItems[i] = ITEM_NONE;
}

void OnGameFrameUpdate() {
    EnsureHudWindow();
    // Game logic runs at 20 Hz, the same rate as the server tick, so one call is one step.
    Feat("session update"); gSession.Update(1.0f / royale::kTickHz);
    if (gTravelCooldown > 0) gTravelCooldown--;
    Feat("song melody"); if (DebugOn(kDbgMusic)) UpdateSongMelody();

    royale::HudState hud = gSession.Hud();
    bool joined = gSession.Joined();
    if (joined) gMapId = royale::ClampMap(hud.mapId);

    if (gHealthOverridden && !(joined && IsLive(hud))) RestoreHealth();
    Feat("platforms, rocks and trees"); if (DebugOn(kDbgTerrain) && joined && InField() && gPlayState != nullptr) { ApplyPlatforms(GET_PLAYER(gPlayState)); ApplyRocks(GET_PLAYER(gPlayState)); ApplyTrees(GET_PLAYER(gPlayState)); }
    Feat("tornado"); if (DebugOn(kDbgTornado) && gTornadoOn && InField() && gPlayState != nullptr) UpdateTornado(GET_PLAYER(gPlayState)); else gTornado.active = false;
    Feat("island scenery collision"); if (DebugOn(kDbgScenery) && joined && InField() && gPlayState != nullptr) ApplyScenery(GET_PLAYER(gPlayState));
    Feat("storm"); DriveStorm(hud);
    Feat("weather"); if (DebugOn(kDbgWeather)) DriveRealWeather();   // also before the player's own update, so it never sees itself as airborne
    Feat("dynamic lights"); UpdateDynamicLights(gPlayState);   // before the actors are drawn, so they are lit by this frame's lights
    Feat("tunic colour"); ApplyLocalTunic(joined && hud.state != royale::MatchState::Lobby && InField(), LocalWearTunic(hud));
    Feat("pause inventory"); SyncPauseInventory(hud);
    Feat("chicken music"); if (DebugOn(kDbgMusic)) UpdateChickenMusic();
    Feat("music scan"); if (gScanRequested) { gScanRequested = false; ScanMusicFolder(); }   // asked for by the menu (which draws on another thread)
    Feat("lobby and match music");
    if (WarMenuOpen()) UpdateLobbyMusic(false, false);
    else if (DebugOn(kDbgMusic)) UpdateLobbyMusic(joined && hud.state == royale::MatchState::Lobby, DriveMatchMusic(hud, joined));
    Feat("lobby timer"); DriveLobbyTimer(hud);
    Feat("time of day"); if (DebugOn(kDbgTimeOfDay)) DriveTimeOfDay(hud);
    Feat("boss effects"); if (DebugOn(kDbgBossFx)) UpdateBossWorldFx();
    Feat("sign"); ReconcileSign(hud);
    Feat("messages"); RegisterRoyaleMessages();
    Feat("Maya"); if (DebugOn(kDbgAllies)) ReconcileMaya(hud);
    Feat("Lilo"); if (DebugOn(kDbgAllies)) ReconcileLilo(hud);
    if (LobbyPetsActive()) {
        const auto& p=GET_PLAYER(gPlayState)->actor.world.pos;
        gLobbyPetGroup.Step({p.x,p.z},1.0f/royale::kTickHz);
    } else gLobbyPetGroup=royale::lobby::Group{};
    Feat("cat pet"); ReconcileCatPet(hud);
    Feat("Maya companion"); ReconcileMayaCompanion(hud);
    Feat("baby pet"); ReconcileBabyPet(hud);
    Feat("lobby reef aquarium"); ReconcileLobbyReef();
    Feat("Lilo effects"); if (DebugOn(kDbgAllies)) { UpdateLiloFx(); UpdateToxicClouds(hud); }
    Feat("allies"); if (DebugOn(kDbgAllies)) ReconcileAllies(hud);
    Feat("carts"); if (DebugOn(kDbgCarts)) ReconcileCarts(hud);
    RunSandboxCommands(hud);
    { static unsigned frames = 0; if (++frames % 100 == 0) ForgetOldHats(); }
    Feat("projectiles"); ReconcileProjectileActor();
    Feat("storm alerts"); DriveStormAlerts(hud);
    gStateNow = hud.state;
    Feat("sealed exits"); SealExits(hud);
    Feat("minimap switch"); if (DebugOn(kDbgMinimap)) DriveMinimapSwitch(joined && IsLive(hud) && InGame() && InField());

    // Just joined a lobby: head for the waiting room if the player wants that.
    if (joined && !gWasJoined) {
        Trace("lobby: joined");
        gLastEpoch = gSession.Client()->Epoch();
        WantsWaitingRoom = CVarGetInteger(CVAR_SETTING("Royale.WaitingRoom"), 1) != 0;
        Say(hud.isHost ? "Lobby open. Share your address from the Battle Royale menu" : "Joined the lobby");
    }
    if (!joined && gWasJoined) {
        WantsWaitingRoom = false;
        gLastCountdownShown = -1;
    }
    gWasJoined = joined;

    if (joined && InGame()) {
        if (hud.state == royale::MatchState::Lobby) {
            if (WantsWaitingRoom) {
                if (InSelectedWaitingRoom()) WantsWaitingRoom = false;
                else GoToWaitingRoom();
            }
        } else if (MustBeInField(hud.state) && !InField()) {
            GoToField(); // the match is on; everyone fights in Hyrule Field
        }
    }

    if (hud.state != gLastState) {
        if (hud.state == royale::MatchState::Lobby || hud.state == royale::MatchState::Countdown) ResetMatchUi();   // a new match: nobody is dead yet
        if (hud.state == royale::MatchState::Ending && joined) StartEndScreen(hud);
        if (hud.state == royale::MatchState::Drop) gMyStats.startAt = ImGui::GetTime();
        if (hud.state == royale::MatchState::Countdown && joined) {
            KillAllEnemies();
            WantsWaitingRoom = false;
        }
        gLastState = hud.state;
        gLastCountdownShown = -1;
    }

    // Count down out loud so nobody has to keep the menu open.
    if (joined && hud.state == royale::MatchState::Countdown) {
        int left = static_cast<int>(std::ceil(hud.countdownLeft));
        if (left != gLastCountdownShown && (left == 10 || left == 5 || (left >= 1 && left <= 3))) Say("Match starts in " + std::to_string(left));
        gLastCountdownShown = left;
    }

    Feat("match start"); DriveStart(hud);
    Feat("match events"); ReportEvents(hud);
    Feat("other players"); ReconcilePuppets(hud.state);
    Feat("loot"); if (DebugOn(kDbgLoot)) ReconcileLoot(hud);
    Feat("props"); if (DebugOn(kDbgProps)) ReconcileProps(hud);
    Feat("bosses"); ReconcileBosses(hud); ReconcileBokoblins(hud);
    Feat("between updates");
    WarGameUpdate();
}

void OnSceneInit(int16_t) {
    Feat("scene init");
    Trace("scene: init");
    gOurTravel = false;
    ForgetDynamicLights();   // the scene's light table starts empty again
    ForgetShadows();
    gGfxPool.want = royale::gfxlayers::kPoolMin; gGfxPool.peak = 0; gGfxPool.started = false;
    gGfxPool.trim[0] = gGfxPool.trim[1] = true;
    // Scene change destroys every puppet actor, so forget them all.
    gPuppetOf.clear();
    StopMayaVoice();
    StopVoice(kVoiceBokoblin);
    gBokoblins.clear(); gBokoblinOf.clear();
    gMayaCompanion = MayaCompanionState{};
    gLobbyPetGroup=royale::lobby::Group{};
    gActorOf.clear();
    gPlaying.clear();
    gMotion.clear();
    gLastFind.clear();
    gPlate.clear();
    gEmote = EmoteState{};   // the old Link is gone with the scene
    CloseEmoteWheel();
    gCorpses.clear();
    gCorpseOf.clear();
    gLastSeen.clear();
    gSpawningPuppet = 0;
    gLoot.clear();
    gLootOf.clear();
    gProps.clear();
    gPropOf.clear();
    gCulledProps.clear();
    gInFieldFrames = 0;
    gAngry.clear();
    gSolidActor = nullptr; gSolidBgId = -1; gSolidFailed = false; gSolidSet.clear();   // the scene's collision (and our actor with it) is gone
    gFortniteActor = nullptr; gFortniteArrived = false;
    ForgetLobbyReef();
    ForgetCarts();   // the cart actors went with the scene
}

void RegisterRoyaleMod() {
    InstallCrashReporter();
    gRoyaleBackdropDraw = DrawBackdrop;   // the sky pass, behind the world (see DrawBackdrop)
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(OnGameFrameUpdate);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerUpdate>(OnPlayerUpdate);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>(OnSceneInit);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameStateMainStart>([]() { WarInput(); OnEmoteWheelInput(); });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnZTitleInit>([](void*) { EnsureHudWindow(); });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameStateMainStart>([]() { EnsureHudWindow(); });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerHatLimb>(OnPlayerHatLimb);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerClothLimb>(OnPlayerClothLimb);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerCustomSword>(OnPlayerCustomSword);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerItemMaterial>(OnPlayerItemMaterial);
    // The server owns health during a match, so whatever the game itself does to it (a long fall, lava, a void out) is undone on the spot.
    // Otherwise a hit that takes it to 0 starts the game's own death and game-over screen before OnPlayerUpdate can put it back.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerHealthChange>([](int16_t) {
        if (gHealthOverridden) gSaveContext.health = gMatchHealth;
    });
    // The game's C-button icons (top right) and D-pad item icons are hidden during a match: the hotbar does their job.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnInterfaceUpdate>([]() {
        if (gPlayState == nullptr || !gSession.Joined() || !IsLive(gSession.Hud())) return;
        InterfaceContext& ic = gPlayState->interfaceCtx;
        ic.cLeftAlpha = ic.cDownAlpha = ic.cRightAlpha = 0;
        ic.dpadUpAlpha = ic.dpadDownAlpha = ic.dpadLeftAlpha = ic.dpadRightAlpha = 0;
    });

    // Turn the Player actor we spawn for a remote player into a puppet *before* its init runs. Requires patches/0001.
    GameInteractor::Instance->RegisterGameHookForID<GameInteractor::ShouldActorInit>(
        ACTOR_PLAYER, [](void* actorRef, bool* should) {
            if (gSpawningPuppet == 0) return; // the real player: leave alone
            Actor* actor = (Actor*)actorRef;
            gPuppetOf[actor] = gSpawningPuppet;
            // The actor was already added to the player list; move it so GET_PLAYER still finds the real Link.
            Actor_ChangeCategory(gPlayState, &gPlayState->actorCtx, actor, ACTORCAT_NPC);
            actor->id = ACTOR_EN_OE2;
            actor->category = ACTORCAT_NPC;
            actor->init = Puppet_Init;
            actor->update = Puppet_Update;
            actor->draw = Puppet_Draw;
            actor->destroy = Puppet_Destroy;
            if (gSpawningPuppet >= kCorpseIdBase) { actor->update = Corpse_Update; actor->draw = Corpse_Draw; } // a body, not a live player
        });

    // The Fortnite map has no use for the field's own scenery (grass, rocks, trees, cows): it stands on the old field's ground, not the island's.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneSpawnActors>([]() {
        if (!gFortniteScene || gPlayState == nullptr || gPlayState->sceneNum != SCENE_HYRULE_FIELD) return;
        for (int cat = 0; cat < ACTORCAT_MAX; cat++) {
            if (cat == ACTORCAT_PLAYER) continue;
            for (Actor* a = gPlayState->actorCtx.actorLists[cat].head; a != nullptr; a = a->next) Actor_Kill(a);
        }
    });

    // The kill above runs before the field's actors have started up, and killing only stops an actor's update and draw: its init still ran
    // afterwards (the drawbridge spawned its chains, and registered its collision, after being "killed"). So on the island the field's own
    // actors are refused their init altogether: scenery, doors/grottos, switches, NPCs, horses, spawners and the like never come to life.
    // Our own actors (ids from the actor database, above ACTOR_ID_MAX), the player, and the few vanilla ones the mod spawns pass.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::ShouldActorInit>([](void* actorRef, bool* should) {
        if (!OnIsland()) return;
        Actor* a = (Actor*)actorRef;
        // The field's own rocks, bushes, chests and items start up while the scene loads (its first frames); ours are spawned later. Left alone they
        // sat at the old field's spots, unseen but still solid and still smashable, so they are refused too.
        if ((a->id == ACTOR_EN_ISHI || a->id == ACTOR_EN_KUSA || a->id == ACTOR_EN_BOX || a->id == ACTOR_EN_ITEM00) && gPlayState->gameplayFrames < 3 && gSpawningPuppet == 0 && !gSpawningLoot) { *should = false; return; }
        if (a->id >= ACTOR_ID_MAX || a->id == ACTOR_PLAYER || a->id == ACTOR_EN_OE2 || a->id == ACTOR_EN_ISHI || a->id == ACTOR_EN_KUSA ||
            a->id == ACTOR_EN_ITEM00 || a->id == ACTOR_EN_BOX || a->id == ACTOR_OBJECT_KANKYO) return;
        switch (a->category) {
            case ACTORCAT_SWITCH: case ACTORCAT_BG: case ACTORCAT_NPC: case ACTORCAT_PROP: case ACTORCAT_BOSS: case ACTORCAT_DOOR: case ACTORCAT_CHEST:
                *should = false; return;
            default: break;
        }
        switch (a->id) {   // the field's spawners and ambience that sit in the item-action / misc lists
            case ACTOR_OBJ_MURE: case ACTOR_OBJ_MURE2: case ACTOR_OBJ_MURE3: case ACTOR_OBJ_HANA: case ACTOR_EN_WONDER_ITEM:
            case ACTOR_EN_ENCOUNT1: case ACTOR_DEMO_KANKYO: case ACTOR_EN_VIEWER: case ACTOR_EN_EX_RUPPY: case ACTOR_EN_BUTTE: case ACTOR_EN_INSECT:
                *should = false; break;
            default: break;
        }
    });
    // ...and no "Hyrule Field" place-name card on arrival at the island.
    REGISTER_VB_SHOULD(VB_SHOW_TITLE_CARD, {
        if (OnIsland()) *should = false;
    });

    // No enemies spawn while in a lobby or match.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::ShouldActorInit>([](void* actorRef, bool* should) {
        Actor* actor = (Actor*)actorRef;
        if (!gSession.Joined()) return;
        if (actor->category == ACTORCAT_ENEMY) *should = false;
        // Smashed rocks and cut bushes would drop the game's own rupees and hearts; the only items in a match come from the server.
        if (actor->id == ACTOR_EN_ITEM00 && !gSpawningLoot) *should = false;
    });
}

static RegisterShipInitFunc royaleInit(RegisterRoyaleMod);

