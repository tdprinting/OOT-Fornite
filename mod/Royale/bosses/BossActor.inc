// ---- the actor that stands in for each boss --------------------------------------------------------------------------------------------

// The mode changed: the moments that deserve a sound or a burst of the game's own particles.
void BossModeStarted(PlayState* play, BossActor& b, BM was) {
    const BM mode = ModeOf(b);
    const float ground = GroundY(play, b.x, b.z, b.actor->world.pos.y);
    const Vec3f at = { b.x, ground, b.z };
    switch (KindOf(b)) {
        case BK::Stone:
            if (mode == BM::Stunned && b.aux == 3) {   // knocked to pieces: bones everywhere, and it laughs
                Vec3f p = at;
                EffectSsHahen_SpawnBurst(play, &p, 8.0f, 0, 10, 6, 10, -1, 10, nullptr);
                SparkBurst(play, b.x, ground + 40.0f, b.z, { 255, 60, 40, 255 }, 18, 4.0f);
                BossSound(b, NA_SE_EN_STAL_DEAD);
            } else if (was == BM::Stunned && b.shownAux == 3) {   // ...and back together
                SparkBurst(play, b.x, ground + 70.0f, b.z, { 255, 40, 30, 255 }, 26, 3.0f);
                BossSound(b, NA_SE_EN_STAL_REBORN);
            } else if (mode == BM::Leap && b.aux == 1) BossSound(b, NA_SE_EN_STAL_JUMP);
            break;
        case BK::Lava:
            if (mode == BM::Breath) BossSound(b, NA_SE_EN_DODO_J_FIRE);
            else if (mode == BM::Charge) BossSound(b, NA_SE_EN_DODO_K_ROLL);
            break;
        case BK::Frost:
            if (mode == BM::Summon) {   // the howl: a ring of frost rolls out from it
                BossSound(b, NA_SE_EN_WOLFOS_CRY);
                Vec3f p = { b.x, ground + 20.0f, b.z }, v = { 0, 0, 0 }, a = { 0, 0, 0 };
                Color_RGBA8 prim = { 200, 235, 255, 255 }, env = { 60, 140, 255, 255 };
                EffectSsBlast_SpawnShockwave(play, &p, &v, &a, &prim, &env, 14);
            } else if (mode == BM::Leap && b.aux == 1) BossSound(b, NA_SE_EN_WOLFOS_ATTACK);
            break;
        case BK::Moss:
            if (mode == BM::Summon) BossSound(b, NA_SE_EN_RIZA_CRY);
            else if (mode == BM::Leap) BossSound(b, NA_SE_EN_RIZA_JUMP);
            break;
        case BK::Tide:
            if (mode == BM::Charge) BossSound(b, NA_SE_EN_DAIOCTA_VOICE);
            else if (mode == BM::Stunned) BossSound(b, NA_SE_EN_DAIOCTA_MAHI);
            break;
        case BK::Shade:
            if (mode == BM::Summon) BossSound(b, NA_SE_EN_DEADHAND_LAUGH);
            else if (mode == BM::Hidden) { BossSound(b, NA_SE_EN_DEADHAND_HIDE); Vec3f p = at; EffectSsHahen_SpawnBurst(play, &p, 6.0f, 0, 8, 6, 8, -1, 10, nullptr); }
            else if (mode == BM::Emerge) { BossSound(b, NA_SE_EN_DEADHAND_BITE); Vec3f p = at; EffectSsHahen_SpawnBurst(play, &p, 9.0f, 0, 10, 8, 12, -1, 10, nullptr); }
            break;
        case BK::Dune:
            if (mode == BM::Slam) BossSound(b, NA_SE_EN_IRONNACK_SWING_AXE);
            break;
        case BK::DragonFire:
            if (mode == BM::Hidden) { BossSound(b, NA_SE_EN_VALVAISA_LAND); Vec3f p = at; EffectSsGMagma_Spawn(play, &p); }
            else if (mode == BM::Emerge) {   // it bursts up out of the ground in a spray of lava and rock
                BossSound(b, NA_SE_EN_VALVAISA_APPEAR);
                Vec3f p = at, v = { 0, 0, 0 }, a = { 0, 0, 0 };
                EffectSsBomb2_SpawnLayered(play, &p, &v, &a, 120, 18);
                EffectSsHahen_SpawnBurst(play, &p, 14.0f, 0, 14, 10, 16, -1, 10, nullptr);
                for (int i = 0; i < 4; i++) { Vec3f m = { b.x + (Rand_ZeroOne() - 0.5f) * 200.0f, ground, b.z + (Rand_ZeroOne() - 0.5f) * 200.0f }; EffectSsGMagma_Spawn(play, &m); }
            } else if (mode == BM::Breath) BossSound(b, NA_SE_EN_VALVAISA_FIRE);
            else if (mode == BM::Landed) BossSound(b, NA_SE_EN_VALVAISA_LAND2);
            break;
        case BK::DragonWater: {
            const float water = WaterTop(b.x, b.z, ground);
            if (mode == BM::Emerge) {
                BossSound(b, NA_SE_EN_MOFER_APPEAR);
                Vec3f p = { b.x, water, b.z };
                EffectSsGSplash_Spawn(play, &p, nullptr, nullptr, 0, 900);
                Ripple(play, b.x, water, b.z, 100, 900);
            } else if (mode == BM::Slam) BossSound(b, NA_SE_EN_MOFER_ATTACK);
            else if (mode == BM::Stunned) BossSound(b, NA_SE_EN_MOFER_CORE_LAND);
            else if (mode == BM::Hidden) { Vec3f p = { b.x, water, b.z }; EffectSsGSplash_Spawn(play, &p, nullptr, nullptr, 0, 500); }
            break;
        }
        case BK::DragonForest:
            if (mode == BM::Hidden || mode == BM::Emerge) {   // into his portal, or out of one: a crackle of purple lightning
                BossSound(b, mode == BM::Hidden ? NA_SE_EN_FANTOM_TRANSFORM : NA_SE_EN_FANTOM_LAUGH);
                Vec3f p = { b.x, b.actor->world.pos.y + 60.0f, b.z };
                Color_RGBA8 prim = { 255, 200, 255, 255 }, env = { 160, 60, 255, 255 };
                EffectSsLightning_Spawn(play, &p, &prim, &env, 180, Rand_ZeroOne() * 0xFFFF, 6, 4);
                SparkBurst(play, b.x, b.actor->world.pos.y + 80.0f, b.z, { 200, 120, 255, 255 }, 30, 6.0f);
            } else if (mode == BM::Beam) BossSound(b, NA_SE_EN_FANTOM_MASIC1);
            else if (mode == BM::Cast) BossSound(b, NA_SE_EN_FANTOM_THUNDER);
            else if (mode == BM::Charge) BossSound(b, NA_SE_EN_FANTOM_ATTACK);
            else if (mode == BM::Stunned) BossSound(b, NA_SE_EN_FANTOM_DAMAGE2);
            break;
        case BK::DragonShadow:
            if (mode == BM::Hidden) BossSound(b, NA_SE_EN_SHADEST_DISAPPEAR);
            else if (mode == BM::Slam) BossSound(b, b.aux == 2 ? NA_SE_EN_SHADEST_CLAP : NA_SE_EN_SHADEST_HAND_FLY);
            else if (mode == BM::Swoop) BossSound(b, NA_SE_EN_SHADEST_FLY_ATTACK);
            break;
        case BK::DragonSand:
            if (mode == BM::Beam) BossSound(b, b.aux == 1 ? NA_SE_EN_TWINROBA_SHOOT_FREEZE : NA_SE_EN_TWINROBA_SHOOT_FIRE);
            else if (mode == BM::Summon) BossSound(b, NA_SE_EN_TWINROBA_POWERUP);
            else if (mode == BM::Swoop || mode == BM::Charge) BossSound(b, NA_SE_EN_TWINROBA_FLY);
            else if (mode == BM::Stunned) BossSound(b, NA_SE_EN_TWINROBA_DAMAGE_VOICE);
            break;
        default: break;
    }
}

void Boss_Update(Actor* actor, PlayState* play) {
    auto of = gBossOf.find(actor);
    if (of == gBossOf.end()) { Actor_Kill(actor); return; }
    BossActor& b = gBosses[of->second];
    const float dt = 1.0f / royale::kTickHz;
    if (!b.initialised) { b.x = b.tx; b.z = b.tz; b.rot = b.trot; b.initialised = true; }
    const float nx = b.x + (b.tx - b.x) * 0.4f, nz = b.z + (b.tz - b.z) * 0.4f;
    b.moved = b.moved * 0.8f + std::hypot(nx - b.x, nz - b.z);
    b.x = nx; b.z = nz;
    const s16 diff = static_cast<s16>(b.trot - b.rot);
    b.rot = static_cast<s16>(b.rot + diff * 0.35f);
    b.smashAge += dt;
    b.modeAge += dt;
    b.flashAge += dt;
    b.recoilAge += dt;
    b.hurtAge += royale::IsDragonKind(KindOf(b)) ? dt : 0.0f;   // the mini bosses count their own (MiniBoss_Update)
    if (royale::IsDragonKind(KindOf(b))) { if (b.hp < b.lastHp - 0.01f) b.hurtAge = 0.0f; b.lastHp = b.hp; }
    b.modeFresh = b.mode != b.shownMode || b.aux != b.shownAux;
    if (b.modeFresh) {
        const BM was = static_cast<BM>(b.shownMode < 0 ? b.mode : b.shownMode);
        if (b.shownMode >= 0) BossModeStarted(play, b, was);
        b.shownMode = b.mode;
        b.shownAux = b.aux;
        b.modeAge = 0.0f;
    }
    actor->world.pos.x = b.x;
    actor->world.pos.z = b.z;
    b.alt += (b.talt - b.alt) * 0.25f;
    const float ground = GroundY(play, b.x, b.z, royale::IsDragonKind(KindOf(b)) ? GET_PLAYER(play)->actor.world.pos.y : actor->world.pos.y);
    actor->world.pos.y = ground + b.alt;
    actor->shape.rot.y = b.rot;
    actor->world.rot.y = b.rot;
    // The Iron Knuckle's armour comes off at half health: it bursts off in pieces.
    if (KindOf(b) == BK::Dune && !b.armourOff && b.hp < 0.5f) {
        b.armourOff = true;
        Vec3f p = { b.x, ground + 80.0f, b.z };
        EffectSsHahen_SpawnBurst(play, &p, 10.0f, 0, 12, 8, 14, -1, 10, nullptr);
        SparkBurst(play, b.x, ground + 90.0f, b.z, { 255, 210, 90, 255 }, 30, 6.0f);
        BossSound(b, NA_SE_EN_IRONNACK_ARMOR_OFF_DEMO);
    }
    // How much of it shows.
    float want = 1.0f;
    const BM mode = ModeOf(b);
    if (mode == BM::Hidden) want = 0.0f;
    if (KindOf(b) == BK::Moss && (mode == BM::Chase || mode == BM::Patrol) && b.smashAge > 0.8f && b.hurtAge > 0.8f) {   // the Lizalfos melts into the grass...
        const Player* me = GET_PLAYER(play);
        const float d = std::hypot(me->actor.world.pos.x - b.x, me->actor.world.pos.z - b.z);
        want = d < 160.0f ? 0.6f : 0.12f;   // ...a shimmer you can just make out, until it strikes
    }
    b.fade += (want - b.fade) * (want > b.fade ? 0.35f : 0.12f);
    // Z-targeting, like any enemy: aim at the middle of the body, from as far off as the game allows for a big one. Not while it is hidden
    // (underground, under water, in shadow) or melted into the grass: nothing can hit it then, so there is nothing to lock on to.
    {
        const bool major = royale::IsDragonKind(KindOf(b));
        const float body = (major ? 90.0f : 60.0f) * royale::kBossDefs[b.kind].scale;
        actor->focus.pos = actor->world.pos;
        actor->focus.pos.y += body;
        actor->targetMode = major ? 5 : 4;   // the game's own ranges: 1000 and 700 units
        if (b.fade > 0.5f && mode != BM::Hidden) actor->flags |= ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE;
        else actor->flags &= ~(ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE);
    }
    // Spins and rolls.
    if ((KindOf(b) == BK::Tide || KindOf(b) == BK::Lava) && mode == BM::Charge) b.spin += KindOf(b) == BK::Tide ? 0.9f : 0.6f;
    else b.spin = 0.0f;
    switch (KindOf(b)) {
        case BK::DragonFire: Volvagia_Update(play, b); break;
        case BK::DragonWater: b.skReady = true; break;   // Morpha is drawn from pieces, not a skeleton
        case BK::DragonForest: PhantomGanon_Update(play, b); break;
        case BK::DragonShadow: Bongo_Update(play, b); break;
        case BK::DragonSand: Twinrova_Update(play, b); break;
        default:
            if (royale::IsChuKind(KindOf(b))) {
                b.hurtAge += dt;
                if (b.hp < b.lastHp - 0.001f) b.hurtAge = 0;
                b.lastHp = b.hp;
            } else MiniBoss_Update(actor, play, b);
            break;
    }
}

void Boss_Draw(Actor* actor, PlayState* play) {
    auto of = gBossOf.find(actor);
    if (of == gBossOf.end()) return;
    BossActor& b = gBosses[of->second];
    if (royale::IsChuKind(KindOf(b))) { ChuChu_Draw(actor, play, b); return; }
    switch (KindOf(b)) {
        case BK::DragonFire: Volvagia_Draw(actor, play, b); return;
        case BK::DragonWater: Morpha_Draw(actor, play, b); return;
        case BK::DragonForest: PhantomGanon_Draw(actor, play, b); return;
        case BK::DragonShadow: Bongo_Draw(actor, play, b, of->second); return;
        case BK::DragonSand: Twinrova_Draw(actor, play, b); return;
        default: break;
    }
    if (b.skReady) { MiniBoss_Draw(actor, play, b, of->second); return; }
    const GpuMesh* mesh = GpuMeshFor(royale::MeshKind::Golem, static_cast<uint32_t>(b.kind));   // the stand-in golem, until the model is ready
    if (mesh == nullptr || mesh->dl.empty()) return;
    const float t = static_cast<float>(play->gameplayFrames) / royale::kTickHz;
    const float bob = b.moved > 0.5f ? std::fabs(std::sin(t * 6.0f)) * 9.0f : std::sin(t * 1.5f) * 2.0f;
    const float lean = b.smashAge < 0.45f ? 0.55f * std::sin(b.smashAge / 0.45f * 3.14159f) : 0.0f; // a swing: it pitches forward
    const float scale = royale::kBossDefs[b.kind].scale;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y + bob, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(actor->shape.rot.y * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    Matrix_RotateX(lean, MTXMODE_APPLY);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(mesh->dl.data()));
    CLOSE_DISPS(play->state.gfxCtx);
}

void Boss_Destroy(Actor* actor, PlayState* play) {
    ActorFunc orig = nullptr;
    auto of = gBossOf.find(actor);
    if (of != gBossOf.end()) {
        auto b = gBosses.find(of->second);
        if (b != gBosses.end()) {
            orig = b->second.origDestroy;
            BossActor& ba = b->second;
            if (ba.skReady && KindOf(ba) != BK::DragonWater) SkelAnime_Free(&ba.sk, play);
            if (ba.handsReady) {
                SkelAnime_Free(&ba.hand[0], play);
                if (KindOf(ba) == BK::DragonShadow) SkelAnime_Free(&ba.hand[1], play);
            }
            gBosses.erase(b);
        }
        gBossOf.erase(of);
    }
    if (orig) orig(actor, play);
}

void ReconcileBosses(const royale::HudState& hud) {
    const bool show = gSession.Joined() && InField() && gSession.Client() &&
                      (hud.state == royale::MatchState::Drop || hud.state == royale::MatchState::InMatch || hud.state == royale::MatchState::Ending);
    std::unordered_map<uint32_t, bool> wanted;
    if (show) {
        for (const royale::net::BossNet& n : gSession.Client()->Bosses()) {
            const uint32_t id = n.Id();
            wanted[id] = true;
            gBossKindSeen[id] = n.kind;
            auto it = gBosses.find(id);
            if (it == gBosses.end()) {
                float y = 0;
                if (!FloorAt(n.x, n.z, &y)) {
                    if (!royale::IsDragonKind(static_cast<royale::BossKind>(n.kind))) continue;
                    y = GET_PLAYER(gPlayState)->actor.world.pos.y; // it flies: over a gap or the lava there may be no floor
                }
                Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ISHI, n.x, y, n.z, 0, n.rot, 0, 0, false);
                if (actor == nullptr) continue;
                BossActor b;
                b.actor = actor; b.origDestroy = actor->destroy; b.kind = n.kind; b.alt = b.talt = n.y; b.mode = n.mode; b.aux = n.aux;
                b.shownMode = n.mode; b.shownAux = n.aux;
                b.tx = n.x; b.tz = n.z; b.trot = n.rot; b.rot = n.rot; b.x = n.x; b.z = n.z; b.initialised = true;
                b.hp = b.lastHp = n.hp / 255.0f;
                b.armourOff = n.kind == static_cast<uint8_t>(BK::Dune) && b.hp < 0.5f;
                gBosses[id] = b;
                gBossOf[actor] = id;
                actor->update = Boss_Update;
                actor->draw = Boss_Draw;
                actor->destroy = Boss_Destroy;
                actor->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED;
                actor->uncullZoneForward = 5000.0f; actor->uncullZoneScale = 1500.0f; actor->uncullZoneDownward = 1500.0f;
                if (royale::IsDragonKind(static_cast<royale::BossKind>(n.kind))) {
                    actor->flags |= ACTOR_FLAG_DRAW_CULLING_DISABLED;
                    actor->uncullZoneForward = 12000.0f; actor->uncullZoneScale = 4000.0f; actor->uncullZoneDownward = 4000.0f;
                    actor->shape.shadowScale = 0.0f;
                } else {
                    actor->shape.shadowScale = 70.0f * royale::kBossDefs[n.kind].scale;
                }
            } else {
                BossActor& b = it->second;
                b.tx = n.x; b.tz = n.z; b.trot = n.rot; b.hp = n.hp / 255.0f; b.talt = n.y; b.mode = n.mode; b.aux = n.aux;
                if (n.smashing && b.smashAge > 0.3f) b.smashAge = 0.0f;
                if (b.actor) b.actor->shape.shadowScale = royale::BossHidden(static_cast<BM>(n.mode)) || b.fade < 0.5f ? 0.0f : (royale::IsDragonKind(KindOf(b)) ? 0.0f : 70.0f * royale::kBossDefs[n.kind].scale);
            }
        }
    }
    for (auto& [id, b] : gBosses) if (!wanted.count(id) && b.actor) Actor_Kill(b.actor);
}

// Name and health bar over each boss (not over one that is out of sight).
void DrawBossBars(ImDrawList* dl, ImFont* font, float scale) {
    if (!InField()) return;
    Player* pl = GET_PLAYER(gPlayState);
    for (const auto& [id, b] : gBosses) {
        if (!b.actor || b.fade < 0.3f) continue;
        const float dx = b.x - pl->actor.world.pos.x, dz = b.z - pl->actor.world.pos.z;
        const float d = std::sqrt(dx * dx + dz * dz);
        const bool major = royale::IsDragonKind(static_cast<royale::BossKind>(b.kind));
        if (d > (major ? 9000.0f : 3800.0f)) continue;
        ImVec2 at;
        if (!WorldToScreen(b.x, b.actor->world.pos.y + (major ? 300.0f : 330.0f * royale::kBossDefs[b.kind].scale), b.z, &at)) continue;
        const float w = 130.0f * scale * std::clamp(1500.0f / (d + 700.0f), 0.55f, 1.4f), h = 11.0f * scale;
        const char* name = royale::kBossDefs[b.kind].name;
        const float ts = 17.0f * scale * std::clamp(1500.0f / (d + 700.0f), 0.7f, 1.3f);
        const ImVec2 sz = font->CalcTextSizeA(ts, FLT_MAX, 0.0f, name);
        dl->AddText(font, ts, ImVec2(at.x - sz.x * 0.5f + 1.5f, at.y - h - sz.y + 1.5f), IM_COL32(0, 0, 0, 230), name);
        dl->AddText(font, ts, ImVec2(at.x - sz.x * 0.5f, at.y - h - sz.y), IM_COL32(255, 120, 90, 255), name);
        dl->AddRectFilled(ImVec2(at.x - w * 0.5f - 2, at.y - h - 2), ImVec2(at.x + w * 0.5f + 2, at.y + 2), IM_COL32(0, 0, 0, 200));
        const ImU32 col = b.hp > 0.5f ? IM_COL32(120, 220, 90, 255) : b.hp > 0.25f ? IM_COL32(240, 200, 60, 255) : IM_COL32(230, 70, 60, 255);
        dl->AddRectFilled(ImVec2(at.x - w * 0.5f, at.y - h), ImVec2(at.x - w * 0.5f + w * std::clamp(b.hp, 0.0f, 1.0f), at.y), col);
    }
}

