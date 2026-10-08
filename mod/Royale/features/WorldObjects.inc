// ---- loot on the ground ---------------------------------------------------------------------------------------------------

struct LootActor {
    Actor* actor = nullptr;
    float baseY = 0;
    int pickupCooldown = 0;
    bool chest = false;       // a treasure chest (opened with A) rather than an item on the ground
    bool opened = false;
    bool killing = false;     // asked the game to remove it; it goes when the game next updates actors
    bool big = false;         // Epic and Legendary chests are the big kind
    bool supply = false;      // from a supply drop
    bool special = false;     // a Heart Container chest: pink
    royale::Rarity rarity = royale::Rarity::Common;
    int gid = -1;             // the game's own model for this item (GetItemDrawID), or -1 to keep the stand-in's
    ActorFunc origUpdate = nullptr, origDestroy = nullptr;
};
std::unordered_map<size_t, LootActor> gLoot;        // loot index -> its actor
std::unordered_map<const Actor*, size_t> gLootOf;   // actor -> loot index
constexpr float kLootSpawnRadius = 1500.0f;          // only draw what is near you
constexpr size_t kMaxLootActors = 56;
constexpr float kLootPickupRange = 55.0f;            // the server allows 75 for items on the ground
constexpr float kChestOpenRange = 90.0f;             // and 100 for chests (they are solid, so you stop a little way off)
bool gSpawningLoot = false;                          // true while we spawn an item actor, so the "no stray item drops" rule lets it through

// ---- turning a world position into a spot on the screen ---------------------------------------------------------------------

bool WorldToScreen(float x, float y, float z, ImVec2* out) {
    if (gPlayState == nullptr) return false;
    Vec3f p = { x, y, z }, clip;
    f32 w = 0.0f;
    SkinMatrix_Vec3fMtxFMultXYZW(&gPlayState->viewProjectionMtxF, &p, &clip, &w);
    if (w <= 1.0f) return false; // behind the camera
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    out->x = (clip.x / w * 0.5f + 0.5f) * ds.x;
    out->y = (0.5f - clip.y / w * 0.5f) * ds.y;
    return true;
}

bool LiveAndAlive(const royale::HudState& h) {
    return gSession.Joined() && (h.state == royale::MatchState::Drop || h.state == royale::MatchState::InMatch) && h.haveSelf && h.selfAlive;
}

bool VictoryWalk(const royale::HudState& h) {
    return gSession.Joined() && h.state == royale::MatchState::Ending &&
        h.haveSelf && h.selfAlive && h.winnerId == h.selfId;
}

void Sparkle(PlayState* play, const Vec3f& at, royale::Rarity rarity); // below, with the chests
void DrawSign(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale);   // below, with the sign
void PlayOneShot(int kind);
void DrawGliderAim(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale); // below, with the skydive
void DrawLilo(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale);
void DrawFartReactions(ImDrawList* dl, ImFont* font, float scale);   // below, with Lilo
void AddToxicCloud(float x, float y, float z, bool mine);   // below, with Lilo
bool LiloNear();
void TalkToLilo();
void DrawMaya(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale);
bool MayaNear();
void TalkToMaya();
bool SignNear();
bool MessageBoxUp();
void PressedATalk();   // A next to Lilo, Maya or the sign: the game's own text box (see "talking")
void DrawAllyLabels(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale, const royale::HudState& h);   // below, with the allies
int NearbyFreeAlly();
void AllyActionFx(const royale::ClientEvent& e, const royale::HudState& h);

void Loot_Update(Actor* actor, PlayState* play) {
    auto idx = gLootOf.find(actor);
    if (idx == gLootOf.end()) {
        Actor_Kill(actor);
        return;
    }
    LootActor& la = gLoot[idx->second];
    // Spin and bob so it reads as something to grab.
    actor->shape.rot.y += 0x300;
    actor->world.pos.y = la.baseY + 22.0f + 5.0f * std::sin(static_cast<float>(play->gameplayFrames) * 0.1f);
    if (la.pickupCooldown > 0) la.pickupCooldown--;
    Sparkle(play, actor->world.pos, la.rarity);

    Player* player = GET_PLAYER(play);
    royale::HudState h = gSession.Hud();
    if (player == nullptr || la.pickupCooldown > 0 || !LiveAndAlive(h)) return;
    float dx = player->actor.world.pos.x - actor->world.pos.x, dz = player->actor.world.pos.z - actor->world.pos.z;
    float dy = player->actor.world.pos.y - la.baseY;
    if (dx * dx + dz * dz <= kLootPickupRange * kLootPickupRange && std::fabs(dy) < 100.0f) {
        gSession.RequestPickup(static_cast<uint32_t>(idx->second), false); // the server only takes upgrades this way
        la.pickupCooldown = royale::kTickHz; // ask again in a second if the server said no
    }
}

void Loot_Destroy(Actor* actor, PlayState* play) {
    NameTag_RemoveAllForActor(actor);
    auto idx = gLootOf.find(actor);
    if (idx != gLootOf.end()) {
        auto la = gLoot.find(idx->second);
        if (la != gLoot.end() && la->second.actor == actor) gLoot.erase(la);
        gLootOf.erase(idx);
    }
}

// ---- treasure chests ------------------------------------------------------------------------------------------------------

// The game's own chest actor, kept shut and inert until the server says it was opened, then handed back to the game to play its normal
// opening animation and sounds. (Treasure flags are scene-wide; ours is cleared again after every spawn so chests don't start open.)
constexpr int kChestFlag = 19;

void ClearChestFlag() {
    gPlayState->actorCtx.flags.chest &= ~(1u << kChestFlag);
}

// The game's own glint effect: a spark of the item's rarity colour drifting up, more often the rarer it is. Makes chests and dropped items
// catch the eye (this is the nearest thing to shiny materials the mod can do; there are no custom shaders).
void Sparkle(PlayState* play, const Vec3f& at, royale::Rarity rarity) {
    static const int kEvery[] = { 30, 20, 12, 8, 4 }; // frames between glints, Common to Legendary
    if (play->gameplayFrames % kEvery[static_cast<int>(rarity)] != 0) return;
    Vec3f pos = at;
    pos.x += (Rand_ZeroOne() - 0.5f) * 40.0f;
    pos.z += (Rand_ZeroOne() - 0.5f) * 40.0f;
    pos.y += 12.0f + Rand_ZeroOne() * 48.0f;
    Vec3f vel = { 0.0f, 0.6f + Rand_ZeroOne() * 0.6f, 0.0f };
    Vec3f accel = { 0.0f, 0.0f, 0.0f };
    Color_RGBA8 prim = RarityColor(rarity);
    Color_RGBA8 env = { 255, 255, 255, 255 };
    EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 90, 26);
}

void Chest_Update(Actor* actor, PlayState* play) {
    auto idx = gLootOf.find(actor);
    if (idx == gLootOf.end()) { Actor_Kill(actor); return; }
    LootActor& la = gLoot[idx->second];
    EnBox* box = reinterpret_cast<EnBox*>(actor);
    if (!la.opened) {
        box->alpha = 255; // the game's own update normally fades it in
        Sparkle(play, actor->world.pos, la.rarity);
        if (la.special && play->gameplayFrames % 3 == 0) {   // a Heart Container chest: pink hearts of light drifting up
            Vec3f pos = { actor->world.pos.x + (Rand_ZeroOne() - 0.5f) * 40.0f, actor->world.pos.y + 20.0f + Rand_ZeroOne() * 60.0f, actor->world.pos.z + (Rand_ZeroOne() - 0.5f) * 40.0f };
            Vec3f vel = { 0.0f, 0.9f + Rand_ZeroOne() * 0.6f, 0.0f }, accel = { 0.0f, 0.0f, 0.0f };
            Color_RGBA8 prim = { 255, 120, 190, 255 }, env = { 255, 40, 110, 255 };
            EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 110, 30);
        }
        if (la.supply) {   // a supply crate: a column of red and gold light
            for (int i = 0; i < 3; i++) {
                Vec3f pos = { actor->world.pos.x + (Rand_ZeroOne() - 0.5f) * 30.0f, actor->world.pos.y + Rand_ZeroOne() * 420.0f, actor->world.pos.z + (Rand_ZeroOne() - 0.5f) * 30.0f };
                Vec3f vel = { 0.0f, 1.0f, 0.0f }, accel = { 0.0f, 0.0f, 0.0f };
                Color_RGBA8 prim = { 255, static_cast<u8>(120 + Rand_ZeroOne() * 110), 40, 255 }, env = { 255, 40, 20, 255 };
                EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &prim, &env, 40, 38);
            }
        }
        return;
    }
    la.origUpdate(actor, play);
}

void Chest_Destroy(Actor* actor, PlayState* play) {
    NameTag_RemoveAllForActor(actor);
    ActorFunc orig = nullptr;
    auto idx = gLootOf.find(actor);
    if (idx != gLootOf.end()) {
        auto la = gLoot.find(idx->second);
        if (la != gLoot.end()) { orig = la->second.origDestroy; if (la->second.actor == actor) gLoot.erase(la); }
        gLootOf.erase(idx);
    }
    if (orig) orig(actor, play); // removes the chest's collision
}

void OpenChest(LootActor& la) {
    if (la.opened) return;
    la.opened = true;
    EnBox* box = reinterpret_cast<EnBox*>(la.actor);
    box->unk_1F4 = la.big ? 1 : -1; // the game's "Link opened this" marker: 1 = the slow opening with golden light, -1 = quick
    NameTag_RemoveAllForActor(la.actor);
}

void SpawnChest(size_t index, const royale::net::LootNet& l, float groundY) {
    const royale::Rarity rarity = static_cast<royale::Rarity>(l.rarity);
    const bool big = rarity >= royale::Rarity::Epic;
    const s16 params = static_cast<s16>(((big ? ENBOX_TYPE_BIG_DEFAULT : ENBOX_TYPE_SMALL) << 12) | (GI_RUPEE_GREEN << 5) | kChestFlag);
    if (l.taken) Flags_SetTreasure(gPlayState, kChestFlag); else ClearChestFlag(); // opened chests are spawned already open
    const s16 rot = static_cast<s16>((index * 2654435761u) >> 16);
    gSpawningLoot = true;
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_BOX, l.x, groundY, l.z, 0, rot, 0, params, false);
    gSpawningLoot = false;
    ClearChestFlag();
    if (actor == nullptr) return;
    LootActor la;
    la.actor = actor; la.baseY = groundY; la.chest = true; la.opened = l.taken; la.big = big; la.rarity = rarity; la.supply = l.supply; la.special = l.special;
    la.origUpdate = actor->update;
    la.origDestroy = actor->destroy;
    gLoot[index] = la;
    gLootOf[actor] = index;
    actor->update = Chest_Update;
    actor->destroy = Chest_Destroy;
    if (!l.taken) {
        std::string label = l.special ? std::string("Heart Container Chest") : std::string(RarityName(rarity)) + " Chest";
        Color_RGBA8 labelColour = RarityColor(rarity);
        if (l.special) labelColour = { 255, 130, 190, 255 };
        NameTag_RegisterForActorWithOptions(actor, label.c_str(), NameTagOptions{ "royale-loot", static_cast<int16_t>(big ? 50 : 34), labelColour });
    }
}

// ---- scenery -----------------------------------------------------------------------------------------------------------------

// ---- our own models, drawn instead of the game's rocks -----------------------------------------------------------------------

// A mesh from shared/meshes.h turned into the game's vertex and display-list format. The vectors never grow after building, so the
// pointers inside the display list stay valid.
struct GpuMesh {
    std::vector<Vtx> vtx;
    std::vector<Gfx> dl;
    int sub = 1;   // corner positions are stored in 1/sub units (the ground patches use quarter units, so a puddle's 0.3-unit height and a snow lump survive)
};
GpuMesh gGpuMeshes[static_cast<int>(royale::MeshKind::Count)][royale::kMeshVariantSlots];
bool gGpuBuilt[static_cast<int>(royale::MeshKind::Count)][royale::kMeshVariantSlots] = {};

// Turns a triangle list into vertices and a display list. The vectors must not move afterwards: the display list points into them.
bool BuildGpuMesh(const royale::MeshData& data, GpuMesh& m, int sub = 1) {
    if (data.v.empty()) return false;
    m.sub = sub;
    m.vtx.resize(data.v.size());
    for (size_t i = 0; i < data.v.size(); i++) {
        Vtx& v = m.vtx[i];
        v.v.ob[0] = static_cast<s16>(std::lround(data.v[i].x * static_cast<float>(sub)));
        v.v.ob[1] = static_cast<s16>(std::lround(data.v[i].y * static_cast<float>(sub)));
        v.v.ob[2] = static_cast<s16>(std::lround(data.v[i].z * static_cast<float>(sub)));
        v.v.flag = 0;
        v.v.tc[0] = v.v.tc[1] = 0;
        v.v.cn[0] = data.v[i].r;
        v.v.cn[1] = data.v[i].g;
        v.v.cn[2] = data.v[i].b;
        v.v.cn[3] = data.v[i].a;
    }
    // The graphics chip takes up to 32 vertices at a time; each triangle has its own three, so ten triangles per batch.
    const size_t batches = (data.v.size() / 3 + 9) / 10;
    m.dl.assign(data.v.size() / 3 + batches + 1, Gfx{});
    Gfx* g = m.dl.data();
    for (size_t first = 0; first < data.v.size(); first += 30) {
        const size_t count = std::min<size_t>(30, data.v.size() - first);
        gSPVertex(g++, reinterpret_cast<uintptr_t>(&m.vtx[first]), static_cast<int>(count), 0);
        for (size_t t = 0; t + 2 < count; t += 3) gSP1Triangle(g++, static_cast<int>(t), static_cast<int>(t + 1), static_cast<int>(t + 2), 0);
    }
    gSPEndDisplayList(g++);
    m.dl.resize(static_cast<size_t>(g - m.dl.data())); // exactly what was written (the size only shrinks, so nothing moves)
    return true;
}

const GpuMesh* GpuMeshFor(royale::MeshKind kind, uint32_t variant) {
    const int k = static_cast<int>(kind);
    variant %= royale::kMeshVariantSlots;
    GpuMesh& m = gGpuMeshes[k][variant];
    if (gGpuBuilt[k][variant]) return &m;
    if (!BuildGpuMesh(royale::BuildMesh(kind, variant), m, kind == royale::MeshKind::Ground ? 4 : 1)) return nullptr;
    gGpuBuilt[k][variant] = true;
    return &m;
}

struct PropActor {
    Actor* actor = nullptr;
    ActorFunc origDestroy = nullptr;
    int meshKind = -1;     // royale::MeshKind drawn in place of the game's model, or -1 to leave the game's own
    uint32_t variant = 0;
    float scale = 1.0f;    // boulders come in sizes (royale::BoulderScale)
};
std::unordered_map<size_t, PropActor> gProps;        // prop index -> its actor
std::unordered_map<const Actor*, size_t> gPropOf;
std::unordered_set<size_t> gBrokenProps;              // rocks and bushes players smashed; they stay gone
std::unordered_set<size_t> gCulledProps;              // props we removed ourselves (far away), as opposed to smashed ones
constexpr float kPropSpawnRadius = 2200.0f;
constexpr float kTownSpawnRadius = 3400.0f;   // the pieces of a town (and the climbing blocks) show from further off, so you see a place before you reach it
constexpr size_t kMaxPropActors = 170;   // a town is a lot of wall pieces

void Prop_NoUpdate(Actor*, PlayState*) {} // the roof has no collision: never let the game's rock logic run on its stand-in

void Prop_DrawCustom(Actor* actor, PlayState* play) {
    auto idx = gPropOf.find(actor);
    if (idx == gPropOf.end()) return;
    auto pa = gProps.find(idx->second);
    if (pa == gProps.end() || pa->second.meshKind < 0) return;
    const GpuMesh* mesh = GpuMeshFor(static_cast<royale::MeshKind>(pa->second.meshKind), pa->second.variant);
    if (mesh == nullptr || mesh->dl.empty()) return;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(actor->shape.rot.y * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    if (pa->second.scale != 1.0f) Matrix_Scale(pa->second.scale, pa->second.scale, pa->second.scale, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK); // colours are baked into the vertices; draw both sides of every face
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(mesh->dl.data()));
    CLOSE_DISPS(play->state.gfxCtx);
}

bool CustomSceneryOn() {
    return CVarGetInteger(CVAR_SETTING("Royale.CustomScenery"), 1) != 0;
}
bool gSceneryWasCustom = true; // the setting the current props were spawned with

void Prop_Destroy(Actor* actor, PlayState* play) {
    ActorFunc orig = nullptr;
    auto idx = gPropOf.find(actor);
    if (idx != gPropOf.end()) {
        const size_t i = idx->second;
        auto pa = gProps.find(i);
        if (pa != gProps.end()) { orig = pa->second.origDestroy; gProps.erase(pa); }
        if (gCulledProps.erase(i) == 0) {                      // broken, not just put away because it is far off
            gBrokenProps.insert(i);
            if (gSession.Client() && !gSession.Client()->BrokenProps().count(i)) gSession.ReportPropSmashed(i);   // the server decides what was inside
        }
        gPropOf.erase(idx);
    }
    if (orig) orig(actor, play);
}

void SpawnProp(size_t index, const royale::Prop& p, float groundY) {
    int16_t id = ACTOR_EN_ISHI;
    int16_t params = 0;
    switch (p.kind) {
        case royale::PropKind::Rock: id = ACTOR_EN_ISHI; params = 0; break;
        case royale::PropKind::Boulder: id = ACTOR_EN_ISHI; params = 0x3CC1; break; // large rock; switch flag 0x3F so it is never "already smashed"
        case royale::PropKind::Pillar: id = ACTOR_EN_ISHI; params = 0x3CC1; break;
        case royale::PropKind::Bush: id = ACTOR_EN_KUSA; params = 0; break;
        case royale::PropKind::Roof: id = ACTOR_EN_ISHI; params = 0; break; // a stand-in actor to hang our roof model on
        case royale::PropKind::PlatformLow: case royale::PropKind::PlatformMid: case royale::PropKind::PlatformHigh: id = ACTOR_EN_ISHI; params = 0; break; // likewise for the climbing blocks
        default: return;
    }
    int meshKind = -1;
    if (royale::IsPlatform(p.kind)) meshKind = static_cast<int>(royale::MeshKind::Platform);   // the blocks only exist as our own model, whatever the scenery setting
    if (CustomSceneryOn()) {
        switch (p.kind) {
            case royale::PropKind::Rock: meshKind = static_cast<int>(royale::MeshKind::Rock); break;
            case royale::PropKind::Boulder: meshKind = static_cast<int>(royale::MeshKind::Boulder); break;
            case royale::PropKind::Pillar: meshKind = static_cast<int>(royale::MeshKind::Pillar); break;
            case royale::PropKind::Roof: meshKind = static_cast<int>(royale::MeshKind::Roof); break;
            default: break;
        }
    }
    if (p.kind == royale::PropKind::Roof && meshKind < 0) return; // a roof only exists as our own model
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, id, p.pos.x, groundY, p.pos.z, 0, static_cast<s16>(p.rot), 0, params, false);
    if (actor == nullptr) return; // the scene didn't have the object loaded for this one; skip it
    if (p.kind == royale::PropKind::Pillar && meshKind < 0) actor->scale.y *= 2.2f; // tall standing stones (the game's own rock, stretched)
    PropActor pa;
    pa.actor = actor;
    pa.origDestroy = actor->destroy;
    pa.meshKind = meshKind;
    pa.variant = royale::IsPlatform(p.kind) ? static_cast<uint32_t>(p.kind) - static_cast<uint32_t>(royale::PropKind::PlatformLow) : static_cast<uint32_t>(p.rot >> 4);
    if (royale::IsPlatform(p.kind)) pa.variant += 3u * static_cast<uint32_t>(CurrentMap().theme);   // blocks, posts and roofs in this map's stone and colours too
    if (p.kind == royale::PropKind::Pillar || p.kind == royale::PropKind::Roof) pa.variant = static_cast<uint32_t>(CurrentMap().theme);
    if (p.kind == royale::PropKind::Rock || p.kind == royale::PropKind::Boulder) {   // its shape, in this map's stone (meshes.h, StoneLook)
        pa.variant = static_cast<uint32_t>(royale::BoulderShape(p.rot) + royale::kBoulderShapes * static_cast<int>(CurrentMap().theme));
        if (p.kind == royale::PropKind::Boulder) pa.scale = royale::BoulderScale(p.rot);
    }
    gProps[index] = pa;
    gPropOf[actor] = index;
    actor->destroy = Prop_Destroy;
    if (meshKind >= 0) actor->draw = Prop_DrawCustom; // the game's rock stays as the solid part, unseen; our model is what you see
    switch (p.kind) { // a blob shadow under each, sized to the model
        case royale::PropKind::Rock: actor->shape.shadowScale = 26.0f; break;
        case royale::PropKind::Boulder: actor->shape.shadowScale = 75.0f * pa.scale; break;
        case royale::PropKind::Pillar: actor->shape.shadowScale = 40.0f; break;
        case royale::PropKind::Roof: actor->shape.shadowScale = 0.0f; break;
        default: if (royale::IsPlatform(p.kind)) actor->shape.shadowScale = 0.0f; break;
    }
    if (p.kind == royale::PropKind::Roof || royale::IsPlatform(p.kind)) {
        actor->update = Prop_NoUpdate;                // floating stand-in: no collision, no breaking
        actor->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED;
        actor->uncullZoneForward = 3000.0f; actor->uncullZoneScale = 1200.0f; actor->uncullZoneDownward = 1200.0f;
    }
}

void ClearProps() {
    for (auto& [idx, pa] : gProps) { gCulledProps.insert(idx); Actor_Kill(pa.actor); }
}

#include "RoyaleBosses.h"   // the bosses: mini bosses, the major boss of each map, their effects

