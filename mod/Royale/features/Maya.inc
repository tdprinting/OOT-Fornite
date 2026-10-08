// ---- Maya ---------------------------------------------------------------------------------------------------------------------
// A little Kokiri called Maya stands in a random spot on every map (the same spot for everyone in the match). Walk up and press A to talk to her.
Actor* gMayaActor = nullptr;
royale::Vec2 gMayaPos = {};
bool gMayaKnown = false;

void TalkToMaya();
void Maya_Update(Actor* actor, PlayState* play) {
    Player* pl = GET_PLAYER(play);
    const float dx = pl->actor.world.pos.x - actor->world.pos.x, dz = pl->actor.world.pos.z - actor->world.pos.z;
    if (dx * dx + dz * dz < 700.0f * 700.0f) {   // she turns to look at you as you come close
        const s16 want = static_cast<s16>(std::atan2(dx, dz) * (32768.0f / 3.14159265f));
        actor->shape.rot.y = static_cast<s16>(actor->shape.rot.y + static_cast<s16>(want - actor->shape.rot.y) * 0.12f);
    }
    actor->focus.pos = actor->world.pos;
    actor->focus.pos.y += 60.0f;
    if (OfferTalk(actor, play, kTextMaya, royale::kHireRange)) TalkToMaya();
}
void Maya_Draw(Actor* actor, PlayState* play) {
    const GpuMesh* mesh = GpuMeshFor(royale::MeshKind::Ally, 0);
    if (mesh == nullptr || mesh->dl.empty()) return;
    const float t = static_cast<float>(play->gameplayFrames) / royale::kTickHz;
    const bool talking = TalkingTo(actor);
    const float hop = talking ? std::fabs(std::sin(t * 9.0f)) * 16.0f : std::fabs(std::sin(t * 2.2f)) * 3.0f;   // she bounces when she talks
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y + hop, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(actor->shape.rot.y * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    Matrix_RotateZ(talking ? std::sin(t * 7.0f) * 0.1f : 0.0f, MTXMODE_APPLY);
    Matrix_Scale(0.78f, 0.78f, 0.78f, MTXMODE_APPLY);   // she is small
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(mesh->dl.data()));
    CLOSE_DISPS(play->state.gfxCtx);
}
void Maya_Destroy(Actor* actor, PlayState*) { if (gMayaActor == actor) gMayaActor = nullptr; }

// Her spot: random but the same for everybody in the same match (it comes from the map's own numbers), on open walkable ground, clear of walls and
// rocks and well away from the sign.
bool FindMayaSpot(const royale::Circle& map, royale::Vec2* out) {
    if (!gSession.Client()) return false;
    const auto& props = gSession.Client()->Props();
    uint64_t seed = gSession.Client()->Seed() ^ 0x4D617961ull;   // "Maya"
    royale::Rng rng(seed);
    auto clear = [&](royale::Vec2 p) {
        if (!WalkableAt(p)) return false;
        if (gSignKnown && royale::Distance(p, gSignPos) < 500.0f) return false;
        for (const royale::Prop& pr : props) {
            const float r = royale::PropRadius(pr.kind);
            if (r > 0.0f && royale::Distance(pr.pos, p) < r + 100.0f) return false;
        }
        return true;
    };
    for (int attempt = 0; attempt < 400; attempt++) {
        const float a = static_cast<float>(rng.Unit() * 6.2831853), d = map.radius * (0.2f + 0.5f * static_cast<float>(rng.Unit()));
        const royale::Vec2 p = { map.center.x + std::cos(a) * d, map.center.z + std::sin(a) * d };
        if (clear(p)) { *out = p; return true; }
    }
    return false;
}

void ReconcileMaya(const royale::HudState& hud) {
    const bool want = gSession.Joined() && InField() && hud.map.radius > 0 &&
                      (hud.state == royale::MatchState::Countdown || hud.state == royale::MatchState::Drop || hud.state == royale::MatchState::InMatch);
    if (!want) {
        if (gMayaActor != nullptr) { Actor_Kill(gMayaActor); gMayaActor = nullptr; }
        gMayaKnown = false;
        return;
    }
    if (gMayaActor != nullptr) return;
    if (!gMayaKnown) { if (!FindMayaSpot(hud.map, &gMayaPos)) return; gMayaKnown = true; }
    float y = 0;
    if (!FloorAt(gMayaPos.x, gMayaPos.z, &y)) return;
    Actor* a = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ISHI, gMayaPos.x, y, gMayaPos.z, 0, 0x4000, 0, 0, false);
    if (a == nullptr) return;
    a->update = Maya_Update;
    a->draw = Maya_Draw;
    a->destroy = Maya_Destroy;
    a->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
    a->uncullZoneForward = 4000.0f; a->uncullZoneScale = 1500.0f; a->uncullZoneDownward = 1500.0f;
    a->shape.shadowScale = 30.0f;
    gMayaActor = a;
}

bool MayaNear() {
    if (gMayaActor == nullptr || !InField()) return false;
    Player* pl = GET_PLAYER(gPlayState);
    return std::hypot(pl->actor.world.pos.x - gMayaPos.x, pl->actor.world.pos.z - gMayaPos.z) < royale::kHireRange;
}

// The talk itself is the game's text box; this is the sparkle that goes with it.
void TalkToMaya() {
    if (gPlayState != nullptr && gMayaActor != nullptr)
        SparkBurst(gPlayState, gMayaPos.x, gMayaActor->world.pos.y + 90.0f, gMayaPos.z, { 255, 190, 220, 255 }, 16, 3.0f);
}

// Her name over her head (what she says is in the game's own text box).
void DrawMaya(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale) {
    (void)ds;
    if (gMayaActor == nullptr || !InField()) return;
    Player* pl = GET_PLAYER(gPlayState);
    const float d = std::hypot(pl->actor.world.pos.x - gMayaPos.x, pl->actor.world.pos.z - gMayaPos.z);
    ImVec2 at;
    if (d < 1800.0f && WorldToScreen(gMayaPos.x, gMayaActor->world.pos.y + 190.0f, gMayaPos.z, &at)) {
        const float size = std::clamp(24.0f * scale * (1800.0f / (d + 900.0f)), 13.0f * scale, 28.0f * scale);
        const char* label = "Maya";
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label);
        dl->AddText(font, size, ImVec2(at.x - sz.x * 0.5f + 2, at.y + 2), IM_COL32(40, 10, 30, 230), label);
        dl->AddText(font, size, ImVec2(at.x - sz.x * 0.5f, at.y), IM_COL32(255, 170, 215, 255), label);
    }
}

