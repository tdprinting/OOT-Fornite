void DrawVictoryCrown(PlayState* play, const Player* player, uint16_t id) {
    if (gStateNow != royale::MatchState::Ending || !gSession.Client() || gSession.Client()->Winner() != id) return;
    const Vec3f& head = player->bodyPartsPos[PLAYER_BODYPART_HEAD];
    if (!std::isfinite(head.x+head.y+head.z)) return;
    const GpuMesh* mesh = GpuMeshFor(royale::MeshKind::VictoryCrown, 0);
    if (mesh == nullptr) return;
    const float size = player->actor.scale.y / .01f;
    Matrix_Push();
    Matrix_Translate(head.x,head.y+7*size,head.z,MTXMODE_NEW);
    Matrix_RotateY(player->actor.shape.rot.y*(3.14159265f/32768),MTXMODE_APPLY);
    Matrix_Scale(size,size,size,MTXMODE_APPLY);
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++,MATRIX_NEWMTX(play->state.gfxCtx),G_MTX_NOPUSH|G_MTX_LOAD|G_MTX_MODELVIEW);
    gSPClearGeometryMode(POLY_OPA_DISP++,G_LIGHTING|G_CULL_BACK);
    gDPSetCombineMode(POLY_OPA_DISP++,G_CC_SHADE,G_CC_SHADE);
    gSPSurfaceMap(POLY_OPA_DISP++,0);
    gSPDisplayList(POLY_OPA_DISP++,const_cast<Gfx*>(mesh->dl.data()));
    CLOSE_DISPS(play->state.gfxCtx);
    Matrix_Pop();
}

