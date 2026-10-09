// ---- things in flight --------------------------------------------------------------------------------------------------------
// Every arrow, seed, bomb, bombchu and boomerang anybody looses is drawn in flight: when a puppet starts its shooting or throwing pose (or when
// you press B with something to shoot) a projectile leaves from there, along the way they face. One stand-in actor near the player draws them
// all with our own models. The damage itself is the server's business, so this is only for the eyes (and ears).
struct Projectile {
    float x, y, z, vx, vy, vz, life, gravity, spin;
    uint32_t variant;
    bool explodes;
    royale::Rarity rarity = royale::Rarity::Common;
};
std::vector<Projectile> gProjectiles;
Actor* gProjectileActor = nullptr;

// What came out of a chest you can see being opened: the item's real model floats up out of the chest over a few seconds, turning and
// glittering, so you can tell what it was before it is gone (drawn by the same stand-in actor as the projectiles).
struct Reveal { float x, y, z; int gid; float age; royale::Rarity rarity; };
std::vector<Reveal> gReveals;
constexpr float kRevealSeconds = 3.2f;

void LaunchSelfFromShockwave(Player* player, float x, float z, royale::Rarity rarity);

void SpawnProjectileFrom(royale::ItemId weapon, float x, float y, float z, s16 yaw, royale::Rarity rarity) {
    using royale::ItemId;
    if (!InField() || gPlayState == nullptr) return;
    const royale::WeaponStats w = royale::WeaponOf(weapon);
    if (!w.ranged) return;
    uint32_t variant = 0;
    float speed = 1700.0f, gravity = 0.0f, spin = 0.0f;
    bool explodes = false;
    u16 sfx = NA_SE_IT_ARROW_SHOT;
    switch (weapon) {
        case ItemId::FireArrows: variant = 1; break;
        case ItemId::IceArrows: variant = 2; break;
        case ItemId::LightArrows: variant = 3; break;
        case ItemId::Slingshot: case ItemId::TripleSlingshot: variant = 4; speed = 1500.0f; sfx = NA_SE_IT_SLING_SHOT; break;
        case ItemId::Bombs: variant = 5; speed = 650.0f; gravity = 760.0f; explodes = true; spin = 6.0f; sfx = NA_SE_IT_BOMB_IGNIT; break;
        case ItemId::Bombchus: variant = 6; speed = 600.0f; explodes = true; sfx = NA_SE_IT_BOMB_IGNIT; break;
        case ItemId::HomingBombchus: variant = 7; speed = 700.0f; explodes = true; sfx = NA_SE_IT_BOMB_IGNIT; break;
        case ItemId::ShockwaveGrenade: variant = 10; speed = 650.0f; gravity = 760.0f; explodes = true; spin = 6.0f; sfx = NA_SE_IT_BOMB_IGNIT; break;
        case ItemId::DekuNuts: variant = 8; speed = 1000.0f; spin = 9.0f; sfx = NA_SE_IT_SLING_SHOT; break;
        case ItemId::Boomerang: variant = 9; speed = 900.0f; spin = 20.0f; sfx = NA_SE_IT_BOOMERANG_THROW; break;
        default: break;
    }
    const float base = yaw * (3.14159265f / 32768.0f);
    const int count = weapon == ItemId::TripleSlingshot ? 3 : 1;
    for (int i = 0; i < count; i++) {
        const float a = base + (count == 1 ? 0.0f : (i - 1) * 0.11f);
        Projectile p = { x, y, z, std::sin(a) * speed, gravity > 0 ? 330.0f : 0.0f, std::cos(a) * speed, std::clamp(std::min(w.range, 1500.0f) / speed, 0.28f, 1.2f), gravity, spin, variant, explodes, rarity };
        if (variant == 10) p.vy = 250.0f; // land near the end of its 520-unit throw
        if (gProjectiles.size() < 160) gProjectiles.push_back(p);
    }
    Vec3f at = { x, y, z };
    Audio_PlaySoundGeneral(sfx, &at, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

// Arrows, bombs, bombchus and boomerangs are drawn with the game's own models (the same display lists and skeleton the real arrow, bomb, bombchu and
// boomerang actors use), dressed the way the real actors draw them. The server still decides what they hit; these only fly for the eyes.
// Returns false for the kinds that keep our own little models (seeds and nuts).
bool DrawRealProjectile(PlayState* play, const Projectile& p) {
    const float horiz = std::hypot(p.vx, p.vz);
    const float yaw = std::atan2(p.vx, p.vz);
    const float pitchUp = std::atan2(p.vy, horiz);
    constexpr float kScale = 0.01f;   // the game's actors are drawn at a hundredth
    switch (p.variant) {
        case 0: case 1: case 2: case 3: {
            static SkelAnime arrow;
            static bool ready = false;
            if (!ready) {
                SkelAnime_Init(play, &arrow, (SkeletonHeader*)gArrowSkel, (AnimationHeader*)gArrow2Anim, nullptr, nullptr, 0);
                Animation_PlayOnce(&arrow, (AnimationHeader*)gArrow1Anim);
                SkelAnime_Update(&arrow);
                ready = true;
            }
            OPEN_DISPS(play->state.gfxCtx);
            Gfx_SetupDL_25Opa(play->state.gfxCtx);
            // Like the real arrow: yawed to its heading, then pitched about X by atan2(speed, -vy) (the model's long axis is Y).
            Matrix_Translate(p.x, p.y, p.z, MTXMODE_NEW);
            Matrix_RotateY(yaw, MTXMODE_APPLY);
            Matrix_RotateX(1.5707963f - pitchUp, MTXMODE_APPLY);
            Matrix_Scale(kScale, kScale, kScale, MTXMODE_APPLY);
            SkelAnime_DrawLod(play, arrow.skeleton, arrow.jointTable, nullptr, nullptr, nullptr, 0);
            CLOSE_DISPS(play->state.gfxCtx);
            if (p.variant != 0 && play->gameplayFrames % 2 == 0) {   // the elemental arrows leave a trail of sparks in their colour
                static const Color_RGBA8 prim[4] = { {255, 255, 255, 255}, {255, 170, 40, 255}, {170, 240, 255, 255}, {255, 255, 150, 255} };
                static const Color_RGBA8 env[4] = { {255, 255, 255, 255}, {255, 40, 0, 255}, {40, 120, 255, 255}, {255, 230, 80, 255} };
                Vec3f pos = { p.x, p.y, p.z }, vel = { 0.0f, 0.2f, 0.0f }, accel = { 0.0f, 0.0f, 0.0f };
                Color_RGBA8 pc = prim[p.variant], ec = env[p.variant];
                EffectSsKiraKira_SpawnDispersed(play, &pos, &vel, &accel, &pc, &ec, 120, 14);
            }
            return true;
        }
        case 5: {   // a bomb: the cap and the body, blinking, always facing the camera like the real one
            OPEN_DISPS(play->state.gfxCtx);
            Gfx_SetupDL_25Opa(play->state.gfxCtx);
            Matrix_Translate(p.x, p.y, p.z, MTXMODE_NEW);
            Matrix_ReplaceRotation(&play->billboardMtxF);
            Matrix_Scale(kScale, kScale, kScale, MTXMODE_APPLY);
            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gBombCapDL);
            Matrix_RotateZYX(0x4000, 0, 0, MTXMODE_APPLY);
            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            const int flash = static_cast<int>(120.0f + 120.0f * std::sin(p.spin * 3.0f));
            gDPPipeSync(POLY_OPA_DISP++);
            gDPSetEnvColor(POLY_OPA_DISP++, flash, 0, 40, 255);
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, flash, 0, 40, 255);
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gBombBodyDL);
            CLOSE_DISPS(play->state.gfxCtx);
            return true;
        }
        case 6: case 7: {   // a bombchu, blinking red like the real one
            OPEN_DISPS(play->state.gfxCtx);
            Gfx_SetupDL_25Opa(play->state.gfxCtx);
            const float blink = 0.5f + 0.5f * std::sin(p.spin * 4.0f);
            gDPSetEnvColor(POLY_OPA_DISP++, static_cast<int>(9.0f + blink * 209.0f), static_cast<int>(9.0f + blink * 34.0f), static_cast<int>(35.0f - blink * 35.0f), 255);
            Matrix_Translate(p.x, p.y, p.z, MTXMODE_NEW);
            Matrix_RotateY(yaw, MTXMODE_APPLY);
            Matrix_RotateX(-pitchUp, MTXMODE_APPLY);
            Matrix_Scale(kScale, kScale, kScale, MTXMODE_APPLY);
            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gBombchuDL);
            CLOSE_DISPS(play->state.gfxCtx);
            return true;
        }
        case 4: case 8: {   // a slingshot seed or a thrown deku nut: the game draws both as a spinning, pulsing glint (EnArrow_Draw)
            const bool seed = p.variant == 4;
            const u8 alpha = static_cast<u8>(Math_CosS(static_cast<s16>(p.spin * 5000.0f)) * 127.5f + 127.5f);
            const float scale = (seed ? 50.0f : 150.0f) * kScale;
            OPEN_DISPS(play->state.gfxCtx);
            Gfx_SetupDL_25Xlu2(play->state.gfxCtx);
            if (seed) {
                gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 255, 255, 255);
                gDPSetEnvColor(POLY_XLU_DISP++, 0, 255, 255, alpha);
            } else {
                gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 12, 0, 0, 255);
                gDPSetEnvColor(POLY_XLU_DISP++, 250, 250, 0, alpha);
            }
            Matrix_Translate(p.x, p.y, p.z, MTXMODE_NEW);
            Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
            Matrix_RotateZ(((play->gameplayFrames & 0xFF) * 4000) * (3.14159265f / 0x8000), MTXMODE_APPLY);
            Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gEffSparklesDL);
            CLOSE_DISPS(play->state.gfxCtx);
            return true;
        }
        case 9: {   // the boomerang, spinning flat as in the game
            OPEN_DISPS(play->state.gfxCtx);
            Gfx_SetupDL_25Opa(play->state.gfxCtx);
            Matrix_Translate(p.x, p.y, p.z, MTXMODE_NEW);
            Matrix_RotateY(yaw, MTXMODE_APPLY);
            Matrix_RotateZ(0x1F40 * (3.14159265f / 0x8000), MTXMODE_APPLY);
            Matrix_RotateY(p.spin * 5.0f, MTXMODE_APPLY);
            Matrix_Scale(kScale, kScale, kScale, MTXMODE_APPLY);
            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gBoomerangRefDL);
            CLOSE_DISPS(play->state.gfxCtx);
            return true;
        }
        default: return false;
    }
}

int GidFor(royale::ItemId id);
float GidScale(int gid);
constexpr int kGidGilded = 1001;    // the Gilded Sword's own model (shared/gilded_sword_model.h), see DrawItemModel
constexpr int kGidGrenade = 1000;   // not one of the game's models: the Shockwave Grenade's own (shared/meshes.h), see DrawItemModel

// An item's model with the current matrix: the game's own (GetItem_Draw), or one of ours. Ours are built standing on y 0, so they are
// lifted to be centred like the game's.
void DrawItemModel(PlayState* play, int gid) {
    if (gid == kGidGilded) {   // the sword on the ground: blade up and to the right, turning slowly, as the game's own swords lie
        const GpuMesh* sword = GpuMeshFor(royale::MeshKind::GildedSword, 0);
        if (sword == nullptr || sword->dl.empty()) return;
        Matrix_Push();
        Matrix_RotateZ(0.9f, MTXMODE_APPLY);
        Matrix_Scale(0.0052f, 0.0052f, 0.0052f, MTXMODE_APPLY);   // limb units (a hundredth of a game unit) down to the size of the game's own loose swords
        Matrix_Translate(-2200.0f, 0.0f, 0.0f, MTXMODE_APPLY);    // about the middle of the sword
        OPEN_DISPS(play->state.gfxCtx);
        Gfx_SetupDL_25Opa(play->state.gfxCtx);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
        gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
        gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(sword->dl.data()));
        CLOSE_DISPS(play->state.gfxCtx);
        Matrix_Pop();
        return;
    }
    if (gid != kGidGrenade) { GetItem_Draw(play, static_cast<s16>(gid)); return; }
    const GpuMesh* mesh = GpuMeshFor(royale::MeshKind::Grenade, 0);
    if (mesh == nullptr || mesh->dl.empty()) return;
    Matrix_Push();
    Matrix_Translate(0.0f, -15.5f, 0.0f, MTXMODE_APPLY);
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);   // colours are baked into the vertices (the purple bands unshaded, so they glow)
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(mesh->dl.data()));
    CLOSE_DISPS(play->state.gfxCtx);
    Matrix_Pop();
}
void Sparkle(PlayState* play, const Vec3f& at, royale::Rarity rarity);

// What each other player last picked up, so it can be held up over their head while they show it off, as Link does.
struct HeldFind { int gid; double at; };
std::unordered_map<uint16_t, HeldFind> gLastFind;
constexpr double kHeldFindSeconds = 4.0;   // a find is only shown if the pose comes this soon after taking it

void DrawHeldFinds(PlayState* play) {
    const double now = ImGui::GetTime();
    for (const auto& [id, actor] : gActorOf) {
        auto f = gLastFind.find(id);
        auto st = gState.find(id);
        if (f == gLastFind.end() || st == gState.end() || actor == nullptr || f->second.gid < 0) continue;
        if (st->second.anim != static_cast<uint8_t>(royale::Anim::ItemGet) || now - f->second.at > kHeldFindSeconds) continue;
        const float size = actor->scale.y / 0.01f;   // grown players hold it higher
        const float k = GidScale(f->second.gid) * 0.032f * size;   // the size the chest reveal settles at
        OPEN_DISPS(play->state.gfxCtx);
        Matrix_Translate(actor->world.pos.x, actor->world.pos.y + 62.0f * size, actor->world.pos.z, MTXMODE_NEW);
        Matrix_RotateY(BINANG_TO_RAD(actor->shape.rot.y), MTXMODE_APPLY);
        Matrix_Scale(k, k, k, MTXMODE_APPLY);
        DrawItemModel(play, f->second.gid);
        CLOSE_DISPS(play->state.gfxCtx);
    }
}

void DrawWater(PlayState* play);   // the water section, further down

// The sky pass. patches/0022 calls this from the game's Play_Draw right after its own skybox and before the scene, so the sky is drawn behind
// everything, like the game's skybox: without a depth test, and the world drawn over it afterwards. (Drawn with the world it had to be depth tested
// at its 7000-unit radius, where the depth buffer is coarse: far hills and the dome fought over the same pixels and the horizon shimmered, and
// anything further than the dome was painted over.) The same frame's Projectile_Draw then knows the sky is done.
}   // namespace
extern "C" void (*gRoyaleBackdropDraw)(PlayState* play);   // patches/0022 (z_play.c)
namespace {
uint32_t gBackdropFrame = UINT32_MAX;
void DrawBackdrop(PlayState* play) {
    if (play == nullptr || play != gPlayState || !InField() || !DebugOn(kDbgSky)) return;
    Feat("draw: sky");
    GfxLayer layer(play, GfxLayerId::Sky, true);
    if (!layer.Open()) return;   // no room in the pool this frame: Projectile_Draw draws the sky with the world instead
    gBackdropFrame = play->state.frames;
    gGfxBackdropOpen = true;
    DrawSky(play);
    DrawSkyLight(play, true, false);
    gGfxBackdropOpen = false;
}

// Every effect in the world, each as its own layer (see "graphics layers"), always in this order. Solid things first; then the see-through ones from
// the ground up and from far to near, since see-through surfaces do not hide each other: whatever is drawn later lies on top.
void Projectile_Draw(Actor*, PlayState* play) {
    const float dt = std::min(0.05f, ImGui::GetIO().DeltaTime);
    if (DebugOn(kDbgSky) && gBackdropFrame != play->state.frames) {   // no sky pass this frame (a game without patches/0022): drawn with the world
        Feat("draw: sky");
        GfxLayer layer(play, GfxLayerId::Sky);
        DrawSky(play);
        DrawSkyLight(play, true, false);
    }
    if (DebugOn(kDbgFoliage)) { Feat("draw: foliage"); GfxLayer layer(play, GfxLayerId::Foliage); DrawFlora(play); }
    if (DebugOn(kDbgScenery)) { Feat("draw: island scenery"); GfxLayer layer(play, GfxLayerId::Scenery); DrawIslandScenery(play); }
    if (DebugOn(kDbgGroundPatches)) { Feat("draw: ground patches"); GfxLayer layer(play, GfxLayerId::Ground); DrawGroundPatches(play); }
    { GfxLayer layer(play, GfxLayerId::Shadows); DrawShadowsAndLights(play); }   // (switched off, it still gives back the game's round shadows)
    if (DebugOn(kDbgWater)) { GfxLayer layer(play, GfxLayerId::Water); DrawWater(play); }
    else DrawWater(play);   // (switched off: it only forgets its state)
    if (DebugOn(kDbgStormWall)) { Feat("draw: storm wall"); GfxLayer layer(play, GfxLayerId::Storm); DrawStormWall(play); }
    if (DebugOn(kDbgFog)) { Feat("draw: fog banks"); GfxLayer layer(play, GfxLayerId::Fog); DrawFogBanks(play); }
    if (DebugOn(kDbgSky)) { Feat("draw: fireflies"); GfxLayer layer(play, GfxLayerId::Fireflies); DrawSkyLight(play, false, true); }
    if (DebugOn(kDbgTornado)) { Feat("draw: tornado"); GfxLayer layer(play, GfxLayerId::Tornado); DrawTornado(play); }
    if (DebugOn(kDbgWeather)) { Feat("draw: weather particles"); GfxLayer layer(play, GfxLayerId::Weather); DrawWeatherParticles(play); }
    if (DebugOn(kDbgWind)) { Feat("draw: wind streaks"); GfxLayer layer(play, GfxLayerId::Wind); DrawWindParticles(play); }
    {   // how much room our layers left in the game's own buffers (the Graphics page shows it)
        const GraphicsContext* gfx = play->state.gfxCtx;
        gGfxPool.gameOpaLeft = gfx->polyOpa.d > gfx->polyOpa.p ? static_cast<size_t>(gfx->polyOpa.d - gfx->polyOpa.p) * sizeof(Gfx) : 0;
        gGfxPool.gameXluLeft = gfx->polyXlu.d > gfx->polyXlu.p ? static_cast<size_t>(gfx->polyXlu.d - gfx->polyXlu.p) * sizeof(Gfx) : 0;
    }
    Feat("draw: held finds");
    DrawHeldFinds(play);
    Feat("draw: chest reveals and projectiles");
    if (!DebugOn(kDbgProjectiles)) return;
    for (size_t i = 0; i < gReveals.size();) {
        Reveal& r = gReveals[i];
        r.age += dt;
        if (r.age >= kRevealSeconds) { gReveals.erase(gReveals.begin() + static_cast<long>(i)); continue; }
        const float t = r.age / kRevealSeconds;
        const float ease = 1.0f - (1.0f - t) * (1.0f - t);          // quick at first, then settling
        const float grow = std::min(1.0f, r.age / 0.6f);
        const Vec3f at = { r.x, r.y + 10.0f + ease * 70.0f, r.z };
        Sparkle(play, at, r.rarity);
        const float k = GidScale(r.gid) * 0.0016f * (0.4f + 0.6f * grow) * 1.5f;
        OPEN_DISPS(play->state.gfxCtx);
        Matrix_Translate(at.x, at.y, at.z, MTXMODE_NEW);
        Matrix_RotateY(r.age * 2.4f, MTXMODE_APPLY);
        Matrix_Scale(k * 20.0f, k * 20.0f, k * 20.0f, MTXMODE_APPLY);
        DrawItemModel(play, r.gid);
        CLOSE_DISPS(play->state.gfxCtx);
        i++;
    }
    for (size_t i = 0; i < gProjectiles.size();) {
        Projectile& p = gProjectiles[i];
        p.x += p.vx * dt; p.y += p.vy * dt; p.z += p.vz * dt;
        p.vy -= p.gravity * dt;
        p.life -= dt;
        p.spin += dt * 14.0f;
        float floorY = p.y - 1.0f;
        const bool landed = RawFloorAt(p.x, p.z, &floorY) && p.y <= floorY + 4.0f;
        if (p.life <= 0.0f || landed) {
            if (p.explodes && gPlayState != nullptr) {
                Vec3f pos = { p.x, std::max(p.y, floorY) + 10.0f, p.z }, vel = { 0, 0, 0 }, accel = { 0, 0, 0 };
                if (p.variant == 10) {
                    PowerFx(play, royale::ItemId::ShockwaveGrenade, pos, false, nullptr, royale::net::kNoPlayer16);
                    LaunchSelfFromShockwave(GET_PLAYER(play), pos.x, pos.z, p.rarity);
                    AddLightFlash(pos.x, pos.y + 20.0f, pos.z, 190, 100, 255, 380.0f, 0.7f, 1);
                } else {
                    EffectSsBomb2_SpawnLayered(play, &pos, &vel, &accel, 40, 10);
                }
                if (p.variant != 10) AddLightFlash(pos.x, pos.y + 20.0f, pos.z, 255, 170, 80, 380.0f, 0.7f, 1);
                Audio_PlaySoundGeneral(NA_SE_IT_BOMB_EXPLOSION, &pos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            }
            gProjectiles.erase(gProjectiles.begin() + static_cast<long>(i));
            continue;
        }
        if (DrawRealProjectile(play, p)) { i++; continue; }
        if (p.variant == 10) {
            Matrix_Push();
            Matrix_Translate(p.x, p.y, p.z, MTXMODE_NEW);
            Matrix_RotateY(std::atan2(p.vx, p.vz), MTXMODE_APPLY);
            Matrix_RotateZ(p.spin, MTXMODE_APPLY);
            Matrix_Scale(1.25f, 1.25f, 1.25f, MTXMODE_APPLY);
            DrawItemModel(play, kGidGrenade);
            Matrix_Pop();
            i++;
            continue;
        }
        const GpuMesh* mesh = GpuMeshFor(royale::MeshKind::Projectile, p.variant);
        if (mesh != nullptr && !mesh->dl.empty()) {
            const float horiz = std::hypot(p.vx, p.vz);
            OPEN_DISPS(play->state.gfxCtx);
            Gfx_SetupDL_25Opa(play->state.gfxCtx);
            Matrix_Translate(p.x, p.y, p.z, MTXMODE_NEW);
            Matrix_RotateY(std::atan2(p.vx, p.vz), MTXMODE_APPLY);
            Matrix_RotateX(-std::atan2(p.vy, horiz), MTXMODE_APPLY);
            if (p.variant == 4 || p.variant == 5 || p.variant == 8 || p.variant == 9) Matrix_RotateZ(p.spin, MTXMODE_APPLY);
            Matrix_Scale(1.25f, 1.25f, 1.25f, MTXMODE_APPLY);
            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
            gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
            gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(mesh->dl.data()));
            CLOSE_DISPS(play->state.gfxCtx);
        }
        i++;
    }
}
void Projectile_Update(Actor* actor, PlayState* play) {
    Player* p = GET_PLAYER(play);
    actor->world.pos = p->actor.world.pos;   // stay near the player so the actor is never skipped
    actor->focus.pos = actor->world.pos;
}
void Projectile_Destroy(Actor* actor, PlayState*) { if (gProjectileActor == actor) gProjectileActor = nullptr; }

void ReconcileProjectileActor() {
    if (InField() && gProjectileActor == nullptr && gPlayState != nullptr) {
        Player* p = GET_PLAYER(gPlayState);
        Actor* a = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ISHI, p->actor.world.pos.x, p->actor.world.pos.y, p->actor.world.pos.z, 0, 0, 0, 0, false);
        if (a == nullptr) return;
        a->update = Projectile_Update;
        a->draw = Projectile_Draw;
        a->destroy = Projectile_Destroy;
        a->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
        a->uncullZoneForward = 9000.0f; a->uncullZoneScale = 9000.0f; a->uncullZoneDownward = 9000.0f;
        a->shape.shadowScale = 0.0f;
        gProjectileActor = a;
    } else if (!InField() && gProjectileActor != nullptr) {
        gProjectileActor = nullptr;   // the scene is changing: the actor goes with it
        gProjectiles.clear();
    }
}

// Which props are part of a place (inside a town's radius): they are spawned ahead of loose scenery and from further away.
std::vector<uint8_t> gPropInTown;
const royale::Prop* gTownSrc = nullptr;
size_t gTownSrcCount = 0, gTownPoiCount = 0;
void RefreshPropTowns(const std::vector<royale::Prop>& props) {
    const auto& pois = gSession.Client()->Pois();
    if (props.data() == gTownSrc && props.size() == gTownSrcCount && pois.size() == gTownPoiCount && gPropInTown.size() == props.size()) return;
    gTownSrc = props.data(); gTownSrcCount = props.size(); gTownPoiCount = pois.size();
    gPropInTown.assign(props.size(), 0);
    for (size_t i = 0; i < props.size(); i++) {
        if (royale::IsPlatform(props[i].kind) || props[i].kind == royale::PropKind::Roof) { gPropInTown[i] = 1; continue; }
        for (const royale::Poi& poi : pois)
            if (std::hypot(props[i].pos.x - poi.center.x, props[i].pos.z - poi.center.z) < poi.radius + 160.0f) { gPropInTown[i] = 1; break; }
    }
}

// Keep the nearest scenery alive around the player, the same for everyone because the list comes from the host. The game can only hold so
// many of our actors (kMaxPropActors), so they go to the props that matter most: the pieces of a town count as much nearer than they are, so a
// place is always whole (walls, roofs, climbs) before the loose rocks and bushes round it, and when the budget is full the farthest loose
// prop is put away to make room for a nearer one. (Before, props were taken in list order, and on the smaller maps the loose scenery used up
// the whole budget, so the towns never appeared at all.)
void ReconcileProps(const royale::HudState& hud) {
    const bool show = gSession.Joined() && InField() && hud.state != royale::MatchState::Lobby;
    if (!show) { ClearProps(); return; }
    const auto& props = gSession.Client()->Props();
    Player* player = GET_PLAYER(gPlayState);
    const float px = player->actor.world.pos.x, pz = player->actor.world.pos.z;
    if (CustomSceneryOn() != gSceneryWasCustom) { // the player flipped the setting: respawn everything the other way
        gSceneryWasCustom = CustomSceneryOn();
        ClearProps();
        return;
    }
    for (size_t broken : gSession.Client()->BrokenProps()) { // somebody else broke these: they are gone for everyone
        gBrokenProps.insert(broken);
        auto it = gProps.find(broken);
        if (it != gProps.end() && gCulledProps.insert(broken).second) Actor_Kill(it->second.actor);
    }
    RefreshPropTowns(props);
    // Rank everything in reach: distance squared, a town's pieces counted at about half their distance.
    struct Want { float key; size_t i; };
    static std::vector<Want> wants;
    static std::vector<uint8_t> wanted;
    wants.clear();
    wanted.assign(props.size(), 0);
    for (size_t i = 0; i < props.size(); i++) {
        if (gBrokenProps.count(i)) continue;
        const float dx = props[i].pos.x - px, dz = props[i].pos.z - pz, d2 = dx * dx + dz * dz;
        const bool town = gPropInTown[i] != 0;
        const float reach = town ? kTownSpawnRadius : kPropSpawnRadius;
        if (d2 > reach * reach) continue;
        wants.push_back({town ? d2 * 0.25f : d2, i});
    }
    const bool full = wants.size() > kMaxPropActors;
    if (full) {
        std::nth_element(wants.begin(), wants.begin() + kMaxPropActors, wants.end(), [](const Want& a, const Want& b) { return a.key < b.key; });
        wants.resize(kMaxPropActors);
    }
    for (const Want& w : wants) wanted[w.i] = 1;
    // Put away what is out of reach, and, when there are more wanted than the budget holds, whatever didn't make the cut.
    for (auto& [i, pa] : gProps) {
        if (i < wanted.size() && wanted[i]) continue;
        const float dx = i < props.size() ? props[i].pos.x - px : 1e9f, dz = i < props.size() ? props[i].pos.z - pz : 1e9f;
        const float reach = (i < gPropInTown.size() && gPropInTown[i]) ? kTownSpawnRadius : kPropSpawnRadius;
        if ((full || dx * dx + dz * dz > reach * reach * 1.4f) && gCulledProps.insert(i).second) Actor_Kill(pa.actor);
    }
    // Spawn the missing ones, nearest (by rank) first, a few a frame.
    std::sort(wants.begin(), wants.end(), [](const Want& a, const Want& b) { return a.key < b.key; });
    int spawned = 0;
    for (const Want& w : wants) {
        if (spawned >= 6 || gProps.size() >= kMaxPropActors) break;
        if (gProps.find(w.i) != gProps.end()) continue;
        float y;
        if (!RawFloorAt(props[w.i].pos.x, props[w.i].pos.z, &y)) continue;
        SpawnProp(w.i, props[w.i], y);
        spawned++;
    }
}

// The game's own 3D model for each item, so what lies on the ground is the real thing (a bow, a hookshot, a Zora tunic...) and not a
// stand-in gem. -1 means the game has no model for it and the stand-in stays.
int GidFor(royale::ItemId id) {
    using royale::ItemId;
    switch (id) {
        case ItemId::DekuStick: return GID_STICK;
        case ItemId::KokiriSword: case ItemId::MasterSword: case ItemId::BasicSword: return GID_SWORD_KOKIRI;
        case ItemId::BiggoronSword: return GID_SWORD_BGS;
        case ItemId::MegatonHammer: case ItemId::GiantsHammer: return GID_HAMMER;
        case ItemId::Slingshot: case ItemId::TripleSlingshot: return GID_SLINGSHOT;
        case ItemId::FairyBow: return GID_BOW;
        case ItemId::Boomerang: return GID_BOOMERANG;
        case ItemId::Bombs: case ItemId::BombAmmo: return GID_BOMB;
        case ItemId::ShockwaveGrenade: return kGidGrenade;
        case ItemId::GildedSword: return kGidGilded;
        case ItemId::Bombchus: case ItemId::HomingBombchus: case ItemId::BombchuAmmo: return GID_BOMBCHU;
        case ItemId::DekuNuts: case ItemId::NutAmmo: return GID_NUTS;
        case ItemId::FireArrows: return GID_ARROW_FIRE;
        case ItemId::IceArrows: return GID_ARROW_ICE;
        case ItemId::LightArrows: return GID_ARROW_LIGHT;
        case ItemId::ArrowAmmo: return GID_ARROWS_MEDIUM;
        case ItemId::SeedAmmo: return GID_SEEDS;
        case ItemId::DekuShield: return GID_SHIELD_DEKU;
        case ItemId::HylianShield: return GID_SHIELD_HYLIAN;
        case ItemId::MirrorShield: return GID_SHIELD_MIRROR;
        case ItemId::GreenPotion: case ItemId::LargeShieldPotion: return GID_POTION_GREEN;
        case ItemId::RedPotion: return GID_POTION_RED;
        case ItemId::BluePotion: case ItemId::SmallShieldPotion: return GID_POTION_BLUE;
        case ItemId::Fairy: return GID_FAIRY;
        case ItemId::Milk: return GID_MILK;
        case ItemId::Fish: return GID_FISH;
        case ItemId::BlueFire: return GID_BLUE_FIRE;
        case ItemId::Bug: return GID_BUG;
        case ItemId::Poe: return GID_POE;
        case ItemId::RecoveryHeart: return GID_HEART;
        case ItemId::HeartPiece: return GID_HEART_PIECE;
        case ItemId::HeartContainer: return GID_HEART_CONTAINER;
        case ItemId::MagicJar: return GID_MAGIC_LARGE;
        case ItemId::DinsFire: return GID_DINS_FIRE;
        case ItemId::FaroresWind: return GID_FARORES_WIND;
        case ItemId::NayrusLove: return GID_NAYRUS_LOVE;
        case ItemId::Hookshot: return GID_HOOKSHOT;
        case ItemId::Longshot: return GID_LONGSHOT;
        case ItemId::LensOfTruth: return GID_LENS;
        case ItemId::MagicBeans: return GID_BEAN;
        case ItemId::FairyOcarina: return GID_OCARINA_FAIRY;
        case ItemId::OcarinaOfTime: return GID_OCARINA_TIME;
        case ItemId::ZeldasLullaby: return GID_SONG_ZELDA;
        case ItemId::EponasSong: return GID_SONG_EPONA;
        case ItemId::SariasSong: return GID_SONG_SARIA;
        case ItemId::SunsSong: return GID_SONG_SUN;
        case ItemId::SongOfTime: return GID_SONG_TIME;
        case ItemId::SongOfStorms: return GID_SONG_STORM;
        case ItemId::MinuetOfForest: return GID_SONG_MINUET;
        case ItemId::BoleroOfFire: return GID_SONG_BOLERO;
        case ItemId::SerenadeOfWater: return GID_SONG_SERENADE;
        case ItemId::NocturneOfShadow: return GID_SONG_NOCTURNE;
        case ItemId::RequiemOfSpirit: return GID_SONG_REQUIEM;
        case ItemId::PreludeOfLight: return GID_SONG_PRELUDE;
        case ItemId::KokiriTunic: case ItemId::GoronTunic: return GID_TUNIC_GORON;
        case ItemId::ZoraTunic: return GID_TUNIC_ZORA;
        case ItemId::KokiriBoots: case ItemId::IronBoots: return GID_BOOTS_IRON;
        case ItemId::HoverBoots: return GID_BOOTS_HOVER;
        case ItemId::GoronBracelet: return GID_BRACELET;
        case ItemId::SilverGauntlets: return GID_GAUNTLETS_SILVER;
        case ItemId::GoldenGauntlets: return GID_GAUNTLETS_GOLD;
        case ItemId::KeatonMask: return GID_MASK_KEATON;
        case ItemId::SkullMask: return GID_MASK_SKULL;
        case ItemId::SpookyMask: return GID_MASK_SPOOKY;
        case ItemId::BunnyHood: return GID_MASK_BUNNY;
        case ItemId::GoronMask: return GID_MASK_GORON;
        case ItemId::ZoraMask: return GID_MASK_ZORA;
        case ItemId::GerudoMask: return GID_MASK_GERUDO;
        case ItemId::MaskOfTruth: return GID_MASK_TRUTH;
        case ItemId::SilverScale: return GID_SCALE_SILVER;
        case ItemId::GoldenScale: return GID_SCALE_GOLDEN;
        case ItemId::BigQuiver: return GID_QUIVER_50;
        case ItemId::BulletBag: return GID_BULLET_BAG_50;
        case ItemId::BombBag: return GID_BOMB_BAG_40;
        case ItemId::ForestMedallion: return GID_MEDALLION_FOREST;
        case ItemId::FireMedallion: return GID_MEDALLION_FIRE;
        case ItemId::WaterMedallion: return GID_MEDALLION_WATER;
        case ItemId::SpiritMedallion: return GID_MEDALLION_SPIRIT;
        case ItemId::ShadowMedallion: return GID_MEDALLION_SHADOW;
        case ItemId::LightMedallion: return GID_MEDALLION_LIGHT;
        case ItemId::KokiriEmerald: return GID_KOKIRI_EMERALD;
        case ItemId::GoronRuby: return GID_GORON_RUBY;
        case ItemId::ZoraSapphire: return GID_ZORA_SAPPHIRE;
        default: return -1;   // rupees keep the game's own rupee drawing
    }
}

// How big a model is drawn (the game's own item drops use 25; long things are drawn smaller so they do not tower over the player).
float GidScale(int gid) {
    switch (gid) {
        case GID_SWORD_KOKIRI: case GID_SWORD_BGS: case GID_HAMMER: case GID_BOW: case GID_HOOKSHOT: case GID_LONGSHOT: return 17.0f;
        case GID_TUNIC_GORON: case GID_TUNIC_ZORA: case GID_SHIELD_DEKU: case GID_SHIELD_HYLIAN: case GID_SHIELD_MIRROR: return 20.0f;
        default: return 25.0f;
    }
}

void Loot_Draw(Actor* actor, PlayState* play) {
    auto idx = gLootOf.find(actor);
    if (idx == gLootOf.end()) return;
    const LootActor& la = gLoot[idx->second];
    if (la.gid < 0) return;
    func_8002EBCC(actor, play, 0);   // the same highlights the game's own 3D drops get
    func_8002ED80(actor, play, 0);
    const float k = GidScale(la.gid);
    Matrix_Scale(k, k, k, MTXMODE_APPLY);
    DrawItemModel(play, la.gid);
}

void SpawnLoot(size_t index, const royale::net::LootNet& l, float groundY) {
    if (l.chest) { SpawnChest(index, l, groundY); return; } // generated loot is in chests; only dropped items lie on the ground
    royale::Rarity rarity = static_cast<royale::Rarity>(l.rarity);
    gSpawningLoot = true;
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ITEM00, l.x, groundY + 22.0f, l.z, 0, 0, 0, DropTypeFor(static_cast<royale::ItemId>(l.item), rarity, l.amount), false);
    gSpawningLoot = false;
    if (actor == nullptr) return;
    // Keep the game's rupee model and drawing, but replace its behaviour: no vanilla pickup, our own spin and server-checked grab.
    actor->update = Loot_Update;
    actor->destroy = Loot_Destroy;
    gLoot[index] = { actor, groundY, 0 };
    gLoot[index].rarity = rarity;
    gLoot[index].gid = GidFor(static_cast<royale::ItemId>(l.item));
    if (gLoot[index].gid >= 0) {   // the item's real model instead of the stand-in
        actor->draw = Loot_Draw;
        Actor_SetScale(actor, 0.03f);
    }
    gLootOf[actor] = index;
    std::string label = LootLabel(l);
    NameTag_RegisterForActorWithOptions(actor, label.c_str(), NameTagOptions{ "royale-loot", 26, RarityColor(rarity) });
}

void ClearLoot() {
    for (auto& [idx, la] : gLoot) Actor_Kill(la.actor);
}

// Keep a handful of actors alive for the loot near the player, and none for loot that is taken or far away.
void ReconcileLoot(const royale::HudState& hud) {
    bool show = gSession.Joined() && InField() &&
                (hud.state == royale::MatchState::Countdown || hud.state == royale::MatchState::Drop || hud.state == royale::MatchState::InMatch);
    if (!show) {
        ClearLoot();
        return;
    }
    const auto& loot = gSession.Client()->Loot();
    Player* player = GET_PLAYER(gPlayState);
    const float px = player->actor.world.pos.x, pz = player->actor.world.pos.z;

    for (auto it = gLoot.begin(); it != gLoot.end();) {
        const size_t idx = it->first;
        LootActor& la = it->second;
        const bool known = idx < loot.size();
        const float dx = known ? loot[idx].x - px : 1e9f, dz = known ? loot[idx].z - pz : 1e9f;
        const bool far = dx * dx + dz * dz > kLootSpawnRadius * kLootSpawnRadius * 1.5f;
        if (la.chest) {
            // Chests clean up their own entry when the game destroys them (they have collision to remove first).
            if (known && loot[idx].taken && !la.opened) {
                OpenChest(la);
                const int gid = GidFor(static_cast<royale::ItemId>(loot[idx].item));
                if (gid >= 0 && dx * dx + dz * dz < 1800.0f * 1800.0f && gReveals.size() < 8) gReveals.push_back({ la.actor->world.pos.x, la.actor->world.pos.y + 25.0f, la.actor->world.pos.z, gid, 0.0f, la.rarity });
            }
            if ((!known || far) && !la.killing) { la.killing = true; Actor_Kill(la.actor); }
            ++it;
        } else if (!known || loot[idx].taken || far) {
            Actor_Kill(la.actor);
            gLootOf.erase(la.actor);
            it = gLoot.erase(it);
        } else {
            ++it;
        }
    }

    if (gLoot.size() >= kMaxLootActors) return;
    int spawnedThisFrame = 0;
    for (size_t i = 0; i < loot.size() && spawnedThisFrame < 4 && gLoot.size() < kMaxLootActors; i++) {
        if ((loot[i].taken && !loot[i].chest) || gLoot.find(i) != gLoot.end()) continue; // opened chests stay, standing open
        float dx = loot[i].x - px, dz = loot[i].z - pz;
        if (dx * dx + dz * dz > kLootSpawnRadius * kLootSpawnRadius) continue;
        float y;
        if (!FloorAt(loot[i].x, loot[i].z, &y)) continue; // nothing to stand on there
        SpawnLoot(i, loot[i], y);
        spawnedThisFrame++;
    }
}

constexpr size_t kNoLoot = SIZE_MAX;

// The closest thing the player could take or open right now (an unopened chest within reach, or an item on the ground).
size_t NearestLootIndex() {
    if (!InField() || gSession.Client() == nullptr) return kNoLoot;
    const auto& loot = gSession.Client()->Loot();
    Player* pl = GET_PLAYER(gPlayState);
    size_t best = kNoLoot;
    float bestD = 1e18f;
    for (const auto& [idx, la] : gLoot) {
        if (idx >= loot.size() || loot[idx].taken || la.opened) continue;
        const float reach = la.chest ? kChestOpenRange : kLootPickupRange;
        const float dx = la.actor->world.pos.x - pl->actor.world.pos.x, dz = la.actor->world.pos.z - pl->actor.world.pos.z;
        const float d2 = dx * dx + dz * dz;
        if (d2 <= reach * reach && d2 < bestD && std::fabs(pl->actor.world.pos.y - la.baseY) < 120.0f) { bestD = d2; best = idx; }
    }
    return best;
}

struct PickupNote {
    std::string text;
    royale::Rarity rarity;
};
std::deque<PickupNote> gPickupLog;                    // newest first, shown (colour coded) in the menu
std::string gBannerText;                              // the big "you got" line above the hotbar
std::string gBannerDescription;
royale::Rarity gBannerRarity = royale::Rarity::Common;
double gBannerUntil = 0;

void NotePickup(royale::ItemId item, const std::string& label, royale::Rarity rarity, bool fromChest) {
    gPickupLog.push_front({ (fromChest ? "Chest: " : "") + label, rarity });
    while (gPickupLog.size() > 10) gPickupLog.pop_back();
    gBannerText = label;
    gBannerDescription = royale::DefOf(item).effect;
    gBannerRarity = rarity;
    gBannerUntil = ImGui::GetTime() + 4.0;
}

std::string ShortName(royale::ItemId id) {
    std::string n = ItemName(id);
    return n.size() > 15 ? n.substr(0, 14) + "." : n;
}

