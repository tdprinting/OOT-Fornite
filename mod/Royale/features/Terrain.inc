// ---- the Fortnite map ---------------------------------------------------------------------------------------------------------
// The island (shared/fortnite_map.h) is played inside Hyrule Field's scene. patches/0013 lets us do three things there:
//   * swap the scene's collision for the island's triangles when the scene loads (Royale_CustomCollision, at the end of this file), so everything
//     the game does with the ground (walking, rolling, arrows, bombs, the camera, chests, spawns, the skydive's landing) works as on any map;
//   * not draw the field's rooms (Royale_HideRooms), and kill the scene's own actors (grass, rocks, trees: they stand on the old field's ground);
//   * and this actor draws the island in their place, with the texture baked into vertex colours (the game draws our meshes that way).
// Water is one big water box at the water level: lakes, rivers and the sea are the ground below it, and Link swims there.
struct FortniteGpu {
    static constexpr int kChunk = 4, kChunks = royale::fortnite::kCells / kChunk;   // blocks per chunk side; chunks per map side
    std::vector<Vtx> vtx[royale::fortnite::kLods];                            // [lod]: every block's vertices at that level of detail
    std::vector<Gfx> dl[royale::fortnite::kLods][kChunks * kChunks];          // one display list per chunk and level of detail
    float lo[kChunks * kChunks] = {}, hi[kChunks * kChunks] = {};             // each chunk's lowest and highest point, for culling
    bool built = false;
};
FortniteGpu gFortniteGpu;
uint8_t gFortniteLod[FortniteGpu::kChunks * FortniteGpu::kChunks] = {};   // the level of detail each chunk was last drawn at
Actor* gFortniteActor = nullptr;
bool gFortniteArrived = false;   // the local player has been put on the island since this scene loaded

void BuildFortniteGpu() {
    namespace fn = royale::fortnite;
    constexpr int kChunk = FortniteGpu::kChunk, kChunks = FortniteGpu::kChunks;
    FortniteGpu& g = gFortniteGpu;
    for (int lod = 0; lod < fn::kLods; lod++) g.vtx[lod].assign(static_cast<size_t>(fn::kCells) * fn::kCells * fn::LodVerts(lod), Vtx{});   // never resized again: the lists point into them
    std::fill(std::begin(g.lo), std::end(g.lo), 1.0e9f);
    std::fill(std::begin(g.hi), std::end(g.hi), -1.0e9f);
    std::vector<fn::DrawVert> tmp;
    for (int bj = 0; bj < fn::kCells; bj++) {
        for (int bi = 0; bi < fn::kCells; bi++) {
            for (int lod = 0; lod < fn::kLods; lod++) {
                tmp.clear();
                fn::BlockVertices(bi, bj, lod, tmp);
                Vtx* out = &g.vtx[lod][(static_cast<size_t>(bj) * fn::kCells + bi) * fn::LodVerts(lod)];
                for (size_t k = 0; k < tmp.size(); k++) {
                    out[k].v.ob[0] = tmp[k].x; out[k].v.ob[1] = tmp[k].y; out[k].v.ob[2] = tmp[k].z;
                    out[k].v.flag = 0;
                    out[k].v.tc[0] = out[k].v.tc[1] = 0;
                    out[k].v.cn[0] = tmp[k].r; out[k].v.cn[1] = tmp[k].g; out[k].v.cn[2] = tmp[k].b; out[k].v.cn[3] = 255;
                }
                if (lod == 0) {
                    const int c = (bj / kChunk) * kChunks + bi / kChunk;
                    for (const fn::DrawVert& v : tmp) { g.lo[c] = std::min<float>(g.lo[c], v.y); g.hi[c] = std::max<float>(g.hi[c], v.y); }
                }
            }
        }
    }
    for (int cj = 0; cj < kChunks; cj++) {
        for (int ci = 0; ci < kChunks; ci++) {
            for (int lod = 0; lod < fn::kLods; lod++) {
                std::vector<Gfx>& dl = g.dl[lod][cj * kChunks + ci];
                dl.assign(kChunk * kChunk * fn::kSub * (1 + fn::kSub * 2) + 1, Gfx{});   // a block loads its vertices a strip (two rows) at a time: the chip takes 32 at once
                Gfx* p = dl.data();
                for (int bj = cj * kChunk; bj < (cj + 1) * kChunk; bj++) {
                    for (int bi = ci * kChunk; bi < (ci + 1) * kChunk; bi++) {
                        const int use = fn::BlockIsOpenWater(bi, bj) ? fn::kLods - 1 : lod;   // open water is a flat square at any distance
                        const int n = fn::LodSquares(use), row = n + 1;
                        const Vtx* base = &g.vtx[use][(static_cast<size_t>(bj) * fn::kCells + bi) * fn::LodVerts(use)];
                        for (int b = 0; b < n; b++) {
                            gSPVertex(p++, reinterpret_cast<uintptr_t>(base + b * row), 2 * row, 0);   // rows b and b + 1
                            for (int a = 0; a < n; a++) {
                                gSP1Triangle(p++, a, a + 1, a + row + 1, 0);
                                gSP1Triangle(p++, a, a + row + 1, a + row, 0);
                            }
                        }
                    }
                }
                gSPEndDisplayList(p++);
                dl.resize(static_cast<size_t>(p - dl.data()));
            }
        }
    }
    g.built = true;
}

void FortniteTerrain_Init(Actor* actor, PlayState*) {
    Actor_SetScale(actor, 1.0f);
    actor->world.pos = actor->home.pos = { 0, 0, 0 };
}
void FortniteTerrain_Update(Actor*, PlayState*) {
    if (OnAuthoredTerrain()) EnsureSolidScenery();
}
void FortniteTerrain_Destroy(Actor* actor, PlayState*) { if (gFortniteActor == actor) gFortniteActor = nullptr; }

// Authored structures use the same geometry as the Blender source and scene collision.
// Persistent buffers keep display-list pointers valid; spatial batches limit draw cost.
void DrawConvergenceStructures(PlayState* play) {
    if (!OnConvergenceTerrain()) return;
    namespace cv = royale::convergence;
    static std::vector<Vtx> vertices;
    static std::vector<std::vector<Gfx>> lists;
    if (vertices.empty()) {
        vertices.resize(sizeof(cv::kDrawVertices)/sizeof(cv::kDrawVertices[0]));
        for (size_t i=0;i<vertices.size();++i) {
            const auto& v=cv::kDrawVertices[i];auto& dst=vertices[i].v;
            dst.ob[0]=v.x;dst.ob[1]=v.y;dst.ob[2]=v.z;dst.flag=0;
            dst.tc[0]=v.s;dst.tc[1]=v.t;
            dst.cn[0]=v.r;dst.cn[1]=v.g;dst.cn[2]=v.b;dst.cn[3]=255;
        }
        lists.resize(sizeof(cv::kBatches)/sizeof(cv::kBatches[0]));
        for (size_t i=0;i<lists.size();++i) {
            const auto& batch=cv::kBatches[i];auto& dl=lists[i];
            dl.resize(batch.count/3 + batch.count/30 + 3);Gfx* p=dl.data();
            for (uint32_t first=0;first<batch.count;first+=30) {
                const int n=static_cast<int>(std::min<uint32_t>(30,batch.count-first));
                gSPVertex(p++,reinterpret_cast<uintptr_t>(&vertices[batch.first+first]),n,0);
                for (int t=0;t<n;t+=3) gSP1Triangle(p++,t,t+1,t+2,0);
            }
            gSPEndDisplayList(p++);dl.resize(static_cast<size_t>(p-dl.data()));
        }
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(0,0,0,MTXMODE_NEW);
    gSPMatrix(POLY_OPA_DISP++,MATRIX_NEWMTX(play->state.gfxCtx),G_MTX_NOPUSH|G_MTX_LOAD|G_MTX_MODELVIEW);
    gSPClearGeometryMode(POLY_OPA_DISP++,G_LIGHTING|G_CULL_BACK);
    gSPTexture(POLY_OPA_DISP++,0xFFFF,0xFFFF,0,G_TX_RENDERTILE,G_ON);
    gDPSetCombineMode(POLY_OPA_DISP++,G_CC_MODULATEIDECALA,G_CC_PASS2);
    int loaded=-1;
    for (size_t i=0;i<lists.size();++i) {
        const auto& batch=cv::kBatches[i];
        if (std::hypot(play->view.eye.x-batch.x,play->view.eye.z-batch.z)>6500.0f) continue;
        if (loaded!=batch.texture) {
            gDPLoadTextureBlock(POLY_OPA_DISP++,cv::kTextures[batch.texture],G_IM_FMT_RGBA,G_IM_SIZ_16b,32,32,0,
                G_TX_WRAP|G_TX_NOMIRROR,G_TX_WRAP|G_TX_NOMIRROR,5,5,G_TX_NOLOD,G_TX_NOLOD);
            loaded=batch.texture;
        }
        gSPDisplayList(POLY_OPA_DISP++,lists[i].data());
    }
    gSPTexture(POLY_OPA_DISP++,0,0,0,G_TX_RENDERTILE,G_OFF);
    CLOSE_DISPS(play->state.gfxCtx);
}

// Hyrule Kingdom's buildings, towers, bridges and trees (shared/kingdom_model.h): the same idea as Convergence's, with 128 x 128 textures and a
// reach per batch (landmarks are drawn from far away, foliage only near). The texture is a 128-wide tile: gDPLoadTextureBlock is limited to 4095 texels
// (64 x 64 at 16 bits), the tile load is not, and its coordinates are S10.5 so one repeat is 4096.
//
// The outsides of stone, wood, roofs, thatch, cobbles, plaster and bark are also drawn lit, with the game's surface light system (docs/ITEM_SURFACE_MAPS.md):
// each triangle carries its face normal, a fixed sun-and-sky light matching the baked light gives the base shading, and the normal and bump maps of
// shared/kingdom_surface_maps.h add relief that catches that light. The sliders of Graphics > surface detail (normals, bumps) control it; at zero these
// batches are drawn with their baked colours like everything else. Rooms stay baked, so their dim light is kept.
void DrawKingdomStructures(PlayState* play) {
    if (!OnKingdomTerrain()) return;
    namespace km = royale::kingdom;
    struct BatchCache {
        std::vector<Vtx> baked, lit;
        std::vector<Gfx> bakedList, litList;
        uint32_t seen = 0;
    };
    constexpr size_t kBatchCount = sizeof(km::kBatches) / sizeof(km::kBatches[0]);
    static std::vector<BatchCache> cache(kBatchCount);
    const uint32_t frame = play->state.frames;
    // Free geometry after 180 unseen game frames; never touch the two frames the renderer may still use.
    for (auto& c : cache) if ((!c.baked.empty() || !c.lit.empty()) && frame - c.seen > 180) c = BatchCache{};
    auto batchList = [&](size_t i, bool lighting) -> Gfx* {
        auto& c = cache[i]; c.seen = frame;
        auto& vertices = lighting ? c.lit : c.baked;
        auto& list = lighting ? c.litList : c.bakedList;
        const auto& batch = km::kBatches[i];
        if (vertices.empty()) {
            vertices.resize(batch.count);
            for (size_t k = 0; k < batch.count; ++k) {
                const auto& v = km::kDrawVertices[batch.first + k];
                auto& dst = vertices[k].v;
                dst.ob[0] = v.x; dst.ob[1] = v.y; dst.ob[2] = v.z; dst.flag = 0;
                dst.tc[0] = v.s; dst.tc[1] = v.t;
                if (lighting) {
                    vertices[k].n.n[0] = v.nx; vertices[k].n.n[1] = v.ny; vertices[k].n.n[2] = v.nz;
                    vertices[k].n.a = 255;
                } else { dst.cn[0] = v.r; dst.cn[1] = v.g; dst.cn[2] = v.b; dst.cn[3] = 255; }
            }
            list.resize(batch.count / 3 + batch.count / 30 + 3);
            Gfx* p = list.data();
            for (uint32_t first = 0; first < batch.count; first += 30) {
                const int n = static_cast<int>(std::min<uint32_t>(30, batch.count - first));
                gSPVertex(p++, reinterpret_cast<uintptr_t>(&vertices[first]), n, 0);
                for (int t = 0; t < n; t += 3) gSP1Triangle(p++, t, t + 1, t + 2, 0);
            }
            gSPEndDisplayList(p++);
            list.resize(static_cast<size_t>(p - list.data()));
        }
        return list.data();
    };
    const int normalPercent = std::clamp(CVarGetInteger(CVAR_SETTING("Royale.ItemNormals"), 100), 0, 200);
    const int bumpPercent = std::clamp(CVarGetInteger(CVAR_SETTING("Royale.ItemBumps"), 50), 0, 200);
    struct Rig { Lights1 lights; GfxSurfaceMap maps[km::kSurfaceClasses]; };
    Rig* rig = (normalPercent > 0 || bumpPercent > 0) ? static_cast<Rig*>(FrameAlloc(play, sizeof(Rig))) : nullptr;
    const bool relief = rig != nullptr;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(0, 0, 0, MTXMODE_NEW);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
    gSPTexture(POLY_OPA_DISP++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIDECALA, G_CC_PASS2);
    int loaded = -1;
    auto loadTexture = [&](int texture) {
        if (loaded == texture) return;
        gDPLoadTextureTile(POLY_OPA_DISP++, km::kTextures[texture], G_IM_FMT_RGBA, G_IM_SIZ_16b, km::kTextureSize, km::kTextureSize, 0, 0, km::kTextureSize - 1, km::kTextureSize - 1, 0,
            G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, 7, 7, G_TX_NOLOD, G_TX_NOLOD);
        loaded = texture;
    };
    // a chunk is 1853 x 1926 units across: test its far corner, not its middle
    auto inReach = [&](const km::Batch& batch) {
        const Vec3f eye = play->view.eye;
        const float dx = batch.x - eye.x, dy = batch.y - eye.y, dz = batch.z - eye.z;
        if (dx*dx + dz*dz > (batch.reach + batch.radius)*(batch.reach + batch.radius)) return false;
        const float fx = play->view.lookAt.x - eye.x, fy = play->view.lookAt.y - eye.y, fz = play->view.lookAt.z - eye.z;
        const float length = std::sqrt(fx*fx + fy*fy + fz*fz);
        return length < 1.0f || dx*fx + dy*fy + dz*fz >= -batch.radius * length;
    };
    for (size_t i = 0; i < kBatchCount; ++i) {   // everything drawn with its baked light
        const auto& batch = km::kBatches[i];
        if ((batch.surface && relief) || !inReach(batch)) continue;
        if (!GfxHasRoom(play, 20)) break;
        loadTexture(batch.texture);
        gSPDisplayList(POLY_OPA_DISP++, batchList(i, false));
    }
    if (relief) {   // the lit, relief-mapped faces, one surface class at a time
        {
            // The baked light, as a game light: a sky ambient and a sun from the south east late in the morning (tools/maps/kingdom/geom.py SUN).
            // With "the sun lights the buildings" on (Graphics > Shadows and lights) it is the sun or the moon of this moment instead.
            float sunDir[3] = { -57.0f / 127.0f, 99.0f / 127.0f, -55.0f / 127.0f }, sunCol[3] = { 107.0f / 255.0f, 104.0f / 255.0f, 98.0f / 255.0f };
            float ambCol[3] = { 148.0f / 255.0f, 148.0f / 255.0f, 148.0f / 255.0f };
            if (BuildingSunOn()) BuildingSunNow(sunDir, sunCol, ambCol);
            auto c8 = [](float v) { return static_cast<u8>(std::clamp(v * 255.0f, 0.0f, 255.0f)); };
            auto d8 = [](float v) { return static_cast<s8>(std::clamp(v * 127.0f, -127.0f, 127.0f)); };
            const Lights1 base = gdSPDefLights1(c8(ambCol[0]), c8(ambCol[1]), c8(ambCol[2]), c8(sunCol[0]), c8(sunCol[1]), c8(sunCol[2]), d8(sunDir[0]), d8(sunDir[1]), d8(sunDir[2]));
            rig->lights = base;
            for (int c = 0; c < km::kSurfaceClasses; ++c) {
                GfxSurfaceMap& m = rig->maps[c];
                m = {};
                m.normal = km::kSurfaceNormal[c];
                m.height = km::kSurfaceBump[c];
                m.width = m.heightPixels = km::kSurfaceSize;
                m.normalStrength = normalPercent / 100.0f;
                m.bumpStrength = bumpPercent / 100.0f;
                m.uvScale = 1.0f / km::kSurfaceUnits[c];
                for (int k = 0; k < 3; ++k) { m.sunDirection[k] = sunDir[k]; m.sunColor[k] = sunCol[k]; m.ambientColor[k] = ambCol[k]; }
            }
            gSPSetGeometryMode(POLY_OPA_DISP++, G_LIGHTING);
            gSPSetLights1(POLY_OPA_DISP++, rig->lights);
            for (int c = 1; c <= km::kSurfaceClasses; ++c) {
                bool started = false;
                for (size_t i = 0; i < kBatchCount; ++i) {
                    const auto& batch = km::kBatches[i];
                    if (batch.surface != c || !inReach(batch)) continue;
                    if (!GfxHasRoom(play, 20)) break;
                    if (!started) { gSPSurfaceMap(POLY_OPA_DISP++, reinterpret_cast<uintptr_t>(&rig->maps[c - 1])); started = true; }
                    loadTexture(batch.texture);
                    gSPDisplayList(POLY_OPA_DISP++, batchList(i, true));
                }
                if (started) gSPSurfaceMap(POLY_OPA_DISP++, 0);
            }
            gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING);
        }
    }
    gSPTexture(POLY_OPA_DISP++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    CLOSE_DISPS(play->state.gfxCtx);
}

void DrawRiftlandsStructures(PlayState* play) {
    if (!OnRiftlandsTerrain()) return;
    namespace km = royale::riftlands;
    struct BatchCache {
        std::vector<Vtx> baked, lit;
        std::vector<Gfx> bakedList, litList;
        uint32_t seen = 0;
    };
    constexpr size_t kBatchCount = sizeof(km::kBatches) / sizeof(km::kBatches[0]);
    static std::vector<BatchCache> cache(kBatchCount);
    const uint32_t frame = play->state.frames;
    // Free geometry after 180 unseen game frames; never touch the two frames the renderer may still use.
    for (auto& c : cache) if ((!c.baked.empty() || !c.lit.empty()) && frame - c.seen > 180) c = BatchCache{};
    auto batchList = [&](size_t i, bool lighting) -> Gfx* {
        auto& c = cache[i]; c.seen = frame;
        auto& vertices = lighting ? c.lit : c.baked;
        auto& list = lighting ? c.litList : c.bakedList;
        const auto& batch = km::kBatches[i];
        if (vertices.empty()) {
            vertices.resize(batch.count);
            for (size_t k = 0; k < batch.count; ++k) {
                const auto& v = km::kDrawVertices[batch.first + k];
                auto& dst = vertices[k].v;
                dst.ob[0] = v.x; dst.ob[1] = v.y; dst.ob[2] = v.z; dst.flag = 0;
                dst.tc[0] = v.s; dst.tc[1] = v.t;
                if (lighting) {
                    vertices[k].n.n[0] = v.nx; vertices[k].n.n[1] = v.ny; vertices[k].n.n[2] = v.nz;
                    vertices[k].n.a = 255;
                } else { dst.cn[0] = v.r; dst.cn[1] = v.g; dst.cn[2] = v.b; dst.cn[3] = 255; }
            }
            list.resize(batch.count / 3 + batch.count / 30 + 3);
            Gfx* p = list.data();
            for (uint32_t first = 0; first < batch.count; first += 30) {
                const int n = static_cast<int>(std::min<uint32_t>(30, batch.count - first));
                gSPVertex(p++, reinterpret_cast<uintptr_t>(&vertices[first]), n, 0);
                for (int t = 0; t < n; t += 3) gSP1Triangle(p++, t, t + 1, t + 2, 0);
            }
            gSPEndDisplayList(p++);
            list.resize(static_cast<size_t>(p - list.data()));
        }
        return list.data();
    };
    const int normalPercent = std::clamp(CVarGetInteger(CVAR_SETTING("Royale.ItemNormals"), 100), 0, 200);
    const int bumpPercent = std::clamp(CVarGetInteger(CVAR_SETTING("Royale.ItemBumps"), 50), 0, 200);
    struct Rig { Lights1 lights; GfxSurfaceMap maps[km::kSurfaceClasses]; };
    Rig* rig = (normalPercent > 0 || bumpPercent > 0) ? static_cast<Rig*>(FrameAlloc(play, sizeof(Rig))) : nullptr;
    const bool relief = rig != nullptr;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(0, 0, 0, MTXMODE_NEW);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);
    gSPTexture(POLY_OPA_DISP++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIDECALA, G_CC_PASS2);
    int loaded = -1;
    auto loadTexture = [&](int texture) {
        if (loaded == texture) return;
        gDPLoadTextureTile(POLY_OPA_DISP++, km::kTextures[texture], G_IM_FMT_RGBA, G_IM_SIZ_16b, km::kTextureSize, km::kTextureSize, 0, 0, km::kTextureSize - 1, km::kTextureSize - 1, 0,
            G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, 7, 7, G_TX_NOLOD, G_TX_NOLOD);
        loaded = texture;
    };
    // a chunk is 1853 x 1926 units across: test its far corner, not its middle
    auto inReach = [&](const km::Batch& batch) {
        const Vec3f eye = play->view.eye;
        const float dx = batch.x - eye.x, dy = batch.y - eye.y, dz = batch.z - eye.z;
        if (dx*dx + dz*dz > (batch.reach + batch.radius)*(batch.reach + batch.radius)) return false;
        const float fx = play->view.lookAt.x - eye.x, fy = play->view.lookAt.y - eye.y, fz = play->view.lookAt.z - eye.z;
        const float length = std::sqrt(fx*fx + fy*fy + fz*fz);
        return length < 1.0f || dx*fx + dy*fy + dz*fz >= -batch.radius * length;
    };
    for (size_t i = 0; i < kBatchCount; ++i) {   // everything drawn with its baked light
        const auto& batch = km::kBatches[i];
        if ((batch.surface && relief) || !inReach(batch)) continue;
        if (!GfxHasRoom(play, 20)) break;
        loadTexture(batch.texture);
        gSPDisplayList(POLY_OPA_DISP++, batchList(i, false));
    }
    if (relief) {   // the lit, relief-mapped faces, one surface class at a time
        {
            // The baked light, as a game light: a sky ambient and a sun from the south east late in the morning (tools/maps/kingdom/geom.py SUN).
            // With "the sun lights the buildings" on (Graphics > Shadows and lights) it is the sun or the moon of this moment instead.
            float sunDir[3] = { -57.0f / 127.0f, 99.0f / 127.0f, -55.0f / 127.0f }, sunCol[3] = { 107.0f / 255.0f, 104.0f / 255.0f, 98.0f / 255.0f };
            float ambCol[3] = { 148.0f / 255.0f, 148.0f / 255.0f, 148.0f / 255.0f };
            if (BuildingSunOn()) BuildingSunNow(sunDir, sunCol, ambCol);
            auto c8 = [](float v) { return static_cast<u8>(std::clamp(v * 255.0f, 0.0f, 255.0f)); };
            auto d8 = [](float v) { return static_cast<s8>(std::clamp(v * 127.0f, -127.0f, 127.0f)); };
            const Lights1 base = gdSPDefLights1(c8(ambCol[0]), c8(ambCol[1]), c8(ambCol[2]), c8(sunCol[0]), c8(sunCol[1]), c8(sunCol[2]), d8(sunDir[0]), d8(sunDir[1]), d8(sunDir[2]));
            rig->lights = base;
            for (int c = 0; c < km::kSurfaceClasses; ++c) {
                GfxSurfaceMap& m = rig->maps[c];
                m = {};
                m.normal = km::kSurfaceNormal[c];
                m.height = km::kSurfaceBump[c];
                m.width = m.heightPixels = km::kSurfaceSize;
                m.normalStrength = normalPercent / 100.0f;
                m.bumpStrength = bumpPercent / 100.0f;
                m.uvScale = 1.0f / km::kSurfaceUnits[c];
                for (int k = 0; k < 3; ++k) { m.sunDirection[k] = sunDir[k]; m.sunColor[k] = sunCol[k]; m.ambientColor[k] = ambCol[k]; }
            }
            gSPSetGeometryMode(POLY_OPA_DISP++, G_LIGHTING);
            gSPSetLights1(POLY_OPA_DISP++, rig->lights);
            for (int c = 1; c <= km::kSurfaceClasses; ++c) {
                bool started = false;
                for (size_t i = 0; i < kBatchCount; ++i) {
                    const auto& batch = km::kBatches[i];
                    if (batch.surface != c || !inReach(batch)) continue;
                    if (!GfxHasRoom(play, 20)) break;
                    if (!started) { gSPSurfaceMap(POLY_OPA_DISP++, reinterpret_cast<uintptr_t>(&rig->maps[c - 1])); started = true; }
                    loadTexture(batch.texture);
                    gSPDisplayList(POLY_OPA_DISP++, batchList(i, true));
                }
                if (started) gSPSurfaceMap(POLY_OPA_DISP++, 0);
            }
            gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING);
        }
    }
    gSPTexture(POLY_OPA_DISP++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    CLOSE_DISPS(play->state.gfxCtx);
}

// Draws only the chunks in front of the camera, each in the detail its distance calls for: full up close, half a little further, two triangles
// per square far away. Open sea costs two triangles a square whatever the distance.
void FortniteTerrain_Draw(Actor*, PlayState* play) {
    GfxLayer layer(play, GfxLayerId::Terrain);
    if (!layer.Open()) return;
    namespace fn = royale::fortnite;
    if (!gFortniteGpu.built) BuildFortniteGpu();
    constexpr int kChunk = FortniteGpu::kChunk, kChunks = FortniteGpu::kChunks;
    const Vec3f eye = play->view.eye, at = play->view.lookAt;
    float fx = at.x - eye.x, fy = at.y - eye.y, fz = at.z - eye.z;
    const float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (fl > 1.0f) { fx /= fl; fy /= fl; fz /= fl; } else { fx = 0; fy = -1; fz = 0; }
    const float flat = std::hypot(fx, fz);
    const float halfX = 0.5f * kChunk * fn::kCellX, halfZ = 0.5f * kChunk * fn::kCellZ;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(0.0f, 0.0f, 0.0f, MTXMODE_NEW);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK);   // the colours are baked in; draw both sides
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    for (int cj = 0; cj < kChunks; cj++) {
        for (int ci = 0; ci < kChunks; ci++) {
            const int c = cj * kChunks + ci;
            const float cx = -fn::kHalfX + (ci + 0.5f) * kChunk * fn::kCellX, cz = -fn::kHalfZ + (cj + 0.5f) * kChunk * fn::kCellZ;
            const float cy = 0.5f * (gFortniteGpu.lo[c] + gFortniteGpu.hi[c]), halfY = 0.5f * (gFortniteGpu.hi[c] - gFortniteGpu.lo[c]);
            const float dx = cx - eye.x, dy = cy - eye.y, dz = cz - eye.z;
            const float r = std::sqrt(halfX * halfX + halfZ * halfZ + halfY * halfY) + 60.0f;   // the chunk's bounding sphere, with a margin
            const float along = dx * fx + dy * fy + dz * fz;
            if (along < -r) continue;                                                           // wholly behind the camera
            if (flat > 0.6f) {                                                                  // looking out rather than down: also skip what is far off to the side
                const float hx = fx / flat, hz = fz / flat, ahead = dx * hx + dz * hz, side = std::fabs(dx * hz - dz * hx);
                if (side - r > std::max(0.0f, ahead + r) * 2.4f) continue;
            }
            // A chunk keeps its level of detail until the camera is clearly past the limit, so it does not flip back and forth at the edge while you
            // move or the camera swings.
            const float dist = std::hypot(dx, dz);
            uint8_t& held = gFortniteLod[c];
            constexpr float kNear = 2300.0f, kFar = 5000.0f, kSlack = 220.0f;
            int lod = dist < kNear ? 0 : dist < kFar ? 1 : 2;
            if (lod != held) {
                const float edge = std::min(lod, static_cast<int>(held)) == 0 ? kNear : kFar;
                if (std::fabs(dist - edge) < kSlack && std::abs(lod - static_cast<int>(held)) == 1) lod = held;
            }
            held = static_cast<uint8_t>(lod);
            gSPDisplayList(POLY_OPA_DISP++, const_cast<Gfx*>(gFortniteGpu.dl[lod][c].data()));
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);
    DrawConvergenceStructures(play);
    DrawKingdomStructures(play);
    DrawRiftlandsStructures(play);
}

int FortniteActorId() {
    static int id = -1;
    if (id < 0) {
        ActorDBInit init;
        init.name = "Royale_Terrain";
        init.desc = "Battle royale island";
        init.category = ACTORCAT_BG;
        init.flags = ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
        init.objectId = OBJECT_GAMEPLAY_KEEP;
        init.instanceSize = sizeof(Actor);
        init.init = FortniteTerrain_Init;
        init.destroy = FortniteTerrain_Destroy;
        init.update = FortniteTerrain_Update;
        init.draw = FortniteTerrain_Draw;
        id = ActorDB::Instance->AddEntry(init).entry.id;
    }
    return id;
}

// The climbing blocks of this match as part of the island's own collision (shared/fortnite_map.h, Block): the same footprint, foot and top the moving
// collision used to give them, but static ground. Standing on moving-object collision made Link hover and slide (it is rebuilt as the nearest scenery
// changes), so on the island maps the blocks live in the scene's collision, which only changes when the scene loads. Convergence has its own authored props.
std::vector<royale::fortnite::Block> WantedBlocks() {
    namespace fn = royale::fortnite;
    std::vector<fn::Block> out;
    if (!gSession.Client() || !royale::IsIslandMap(gMapId) || royale::IsAuthoredMap(gMapId)) return out;
    const float half = royale::kPlatformHalf;
    for (const royale::Prop& p : gSession.Client()->Props()) {
        if (!royale::IsPlatform(p.kind) || static_cast<int>(out.size()) >= fn::kMaxBlocks) continue;
        float mid = 0, low = 0;
        if (!fn::GroundHeight(p.pos.x, p.pos.z, &mid)) continue;
        low = mid;
        bool ok = true;
        for (int k = 0; k < 4 && ok; k++) {   // the lowest ground under the footprint, so a block on a slope reaches down to the low side
            float y = 0;
            ok = fn::GroundHeight(p.pos.x + (k & 1 ? 1 : -1) * half, p.pos.z + (k & 2 ? 1 : -1) * half, &y);
            low = std::min(low, y);
        }
        if (!ok) continue;
        out.push_back({ p.pos.x, p.pos.z, half, low - 20.0f, mid + royale::PlatformHeight(p.kind) });
    }
    return out;
}
// True on an island map once its blocks are in the collision: the moving collision and the old step-up code leave them alone.
bool BlocksAreBaked() { return gFortniteScene && !royale::fortnite::gBlocks.empty(); }

// Called every frame: the island is drawn while we are in a scene loaded with its collision; the player is put on it once on arrival (the scene
// puts Link at the field's door, which is somewhere inside or under the island); and a lobby that changes between the field and the island
// reloads the scene, since the collision is chosen when the scene loads.
void DriveFortnite(Player* player, const royale::HudState& hud) {
    if (gPlayState == nullptr || !InGame()) return;
    const bool onIsland = gFortniteScene && gPlayState->sceneNum == SCENE_HYRULE_FIELD;
    if (onIsland && gFortniteActor == nullptr) {
        gFortniteActor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, static_cast<s16>(FortniteActorId()), 0, 0, 0, 0, 0, 0, 0, false);
    } else if (!onIsland && gFortniteActor != nullptr) {
        Actor_Kill(gFortniteActor);
        gFortniteActor = nullptr;
    }
    if (!gSession.Joined()) return;
    if (gPlayState->sceneNum == SCENE_HYRULE_FIELD && (gFortniteScene != royale::IsIslandMap(gMapId) || (gFortniteScene && royale::fortnite::gTerrainMapId != gMapId))) {
        GoToField();   // the host changed the map: load the scene again with the right ground
        return;
    }
    if (!onIsland) { gFortniteArrived = false; return; }
    // The match's blocks are known only once the host has laid the map out; the collision is chosen as the scene loads, so load it again once (before the drop)
    // if they are not in it yet. `tried` stops a second reload for the same set.
    if (gPlayState->transitionTrigger == TRANS_TRIGGER_OFF && !gSkydiving && (hud.state == royale::MatchState::Lobby || hud.state == royale::MatchState::Countdown)) {
        static std::vector<royale::fortnite::Block> tried;
        const std::vector<royale::fortnite::Block> want = WantedBlocks();
        if (!(want == royale::fortnite::gBlocks) && !(want == tried)) { tried = want; Trace("blocks: loading the map again with the climbing blocks in it"); GoToField(); return; }
    }
    if (gFortniteArrived || gPlayState->transitionTrigger != TRANS_TRIGGER_OFF) return;
    gFortniteArrived = true;
    // A little scatter, so a lobby's players don't all stand in one spot. Only in the lobby: once the match is on, the skydive places everyone.
    if (hud.state != royale::MatchState::Lobby || gSkydiving) return;
    namespace fn = royale::fortnite;
    const float a = static_cast<float>(std::rand() % 628) / 100.0f, r = static_cast<float>(std::rand() % 220);
    const float x = fn::gSpawnX + std::sin(a) * r, z = fn::gSpawnZ + std::cos(a) * r;
    float y = 0;
    if (!fn::GroundHeight(x, z, &y)) return;
    player->actor.world.pos = { x, y + 12.0f, z };
    player->actor.prevPos = player->actor.world.pos;
    player->actor.velocity = { 0, 0, 0 };
    player->actor.speedXZ = 0.0f;
    player->fallDistance = 0;
}

void ApplyPlatforms(Player* player) {
    if (!InField() || SolidActive() || BlocksAreBaked()) return;
    RefreshPlatforms();
    if (gPlatformIdx.empty()) return;
    const auto& props = gSession.Client()->Props();
    const float px = player->actor.world.pos.x, pz = player->actor.world.pos.z;
    float py = player->actor.world.pos.y;
    // Walking: a grounded player steps up onto a block up to 64 tall without jumping (the real game's own step-up is much smaller), so the blocks
    // behave like stairs and terrain you can walk over; anything taller needs a jump.
    const float reach = ((player->actor.bgCheckFlags & 1) || player->actor.floorHeight >= py - 2.0f) ? 64.0f : 28.0f;
    for (size_t i : gPlatformIdx) {
        const royale::Prop& p = props[i];
        const float dx = px - p.pos.x, dz = pz - p.pos.z;
        if (std::fabs(dx) > royale::kPlatformHalf + 40.0f || std::fabs(dz) > royale::kPlatformHalf + 40.0f) continue;
        const float base = PlatformBase(i);
        if (base < -1.0e8f) continue;
        const float top = base + royale::PlatformHeight(p.kind);
        const bool onTop = std::fabs(dx) <= royale::kPlatformHalf && std::fabs(dz) <= royale::kPlatformHalf;
        if (onTop && py >= top - reach && player->actor.velocity.y <= 0.5f) {         // standing on it
            player->actor.world.pos.y = py = top;
            player->actor.velocity.y = 0.0f;
            player->actor.bgCheckFlags |= 1;
            player->actor.bgCheckFlags &= ~(2 | 4 | 8);   // not airborne, not against a wall: so no falling pose and no snagging on the block's edge
            player->actor.floorHeight = top;
            player->actor.gravity = player->actor.gravity > -0.1f ? player->actor.gravity : -1.0f;
        } else if (py < top - reach && std::fabs(dx) <= royale::kPlatformHalf + 16.0f && std::fabs(dz) <= royale::kPlatformHalf + 16.0f) {   // against its side: pushed out
            const float penX = royale::kPlatformHalf + 16.0f - std::fabs(dx), penZ = royale::kPlatformHalf + 16.0f - std::fabs(dz);
            if (penX < penZ) player->actor.world.pos.x += (dx >= 0 ? penX : -penX);
            else player->actor.world.pos.z += (dz >= 0 ? penZ : -penZ);
        }
    }
}

// Rocks, boulders and standing stones are solid in the mod's own terms too (the game's collision for the stand-in rock is smaller than the model you see,
// so you walked into the stone). You are pushed out of their sides, and can walk up onto the low ones (a rock, a boulder) the same way as onto the
// climbing blocks: step up by walking into them, or jump. Tall stones (the posts) are only walls.
void ApplyRocks(Player* player) {
    if (!InField() || !gSession.Client() || SolidActive()) return;
    const auto& props = gSession.Client()->Props();
    const float px = player->actor.world.pos.x, pz = player->actor.world.pos.z;
    const float py = player->actor.world.pos.y;
    const bool grounded = (player->actor.bgCheckFlags & 1) != 0;
    const float reach = grounded ? 64.0f : 28.0f;
    for (size_t i = 0; i < props.size(); i++) {
        const royale::Prop& p = props[i];
        if (p.kind != royale::PropKind::Rock && p.kind != royale::PropKind::Boulder && p.kind != royale::PropKind::Pillar) continue;
        const float dx = px - p.pos.x, dz = pz - p.pos.z;
        const bool boulder = p.kind == royale::PropKind::Boulder;
        const float radius = royale::PropRadius(p.kind) * (boulder ? royale::BoulderScale(p.rot) : 1.0f);
        if (std::fabs(dx) > radius + 40.0f || std::fabs(dz) > radius + 40.0f) continue;
        const float d = std::hypot(dx, dz);
        const float base = PlatformBase(i);
        if (base < -1.0e8f) continue;
        const float height = p.kind == royale::PropKind::Rock ? 24.0f : boulder ? royale::BoulderTop(royale::BoulderShape(p.rot)) * royale::BoulderScale(p.rot) : 200.0f;
        const float top = base + height;
        const float standR = radius * 0.62f;
        if (p.kind != royale::PropKind::Pillar && d <= standR && py >= top - reach && player->actor.velocity.y <= 0.5f) {   // on top
            player->actor.world.pos.y = top;
            player->actor.velocity.y = 0.0f;
            player->actor.bgCheckFlags |= 1;
            player->actor.floorHeight = top;
        } else if (d < radius && py < top - 4.0f && d > 0.01f) {   // against its side: out you go (and slide round it)
            const float push = radius - d;
            player->actor.world.pos.x += dx / d * push;
            player->actor.world.pos.z += dz / d * push;
        }
    }
}

// What a swing from Link would land on: the nearest other player or mini boss within reach and roughly in front of him.
// Returns 0 with *outDist left at 1e9 when there is none.
static uint16_t PickStrikeTarget(Player* player, float range, float* outDist) {
    uint16_t best = 0;
    float bestDist = 1e9f;
    // Z-targeting something (a player, a mini boss, a major boss) is choosing it: the blow goes to that one while it is in reach, whichever way the
    // sticks point (the game turns Link to face it, so the usual cone is not needed).
    if (const Actor* locked = player->focusActor) {
        uint16_t id = 0; float tx = 0, tz = 0, edge = 0;
        if (auto p = gPuppetOf.find(locked); p != gPuppetOf.end()) { id = p->second; auto st = gState.find(id); if (st != gState.end() && st->second.alive) { tx = st->second.x; tz = st->second.z; } else id = 0; }
        else if (auto b = gBossOf.find(locked); b != gBossOf.end() && b->second <= 0xFFFFu) {
            id = static_cast<uint16_t>(b->second); edge = royale::kBossBodyRadius;
            if (!KnownPosition(id, &tx, &tz)) id = 0;
        }
        if (id != 0) {
            const float d = std::hypot(tx - player->actor.world.pos.x, tz - player->actor.world.pos.z) - edge;
            if (d <= range * 1.05f) { *outDist = (std::max)(0.0f, d); return id; }
        }
    }
    for (const auto& [id, actor] : gActorOf) {
        auto st = gState.find(id);
        if (st == gState.end() || !st->second.alive) continue;
        float dx = st->second.x - player->actor.world.pos.x, dz = st->second.z - player->actor.world.pos.z;
        float d = std::sqrt(dx * dx + dz * dz);
        if (d > range * 1.05f) continue;
        // Within a 90 degree cone in front of Link (binary angle 0x2000 = 45 degrees).
        s16 toTarget = static_cast<s16>(std::atan2(dx, dz) * (32768.0f / 3.14159265f));
        s16 off = static_cast<s16>(toTarget - player->actor.shape.rot.y);
        if (std::abs(static_cast<int>(off)) > 0x2000 && d > 60.0f) continue;
        if (d < bestDist) { bestDist = d; best = id; }
    }
    // The mini bosses are big: they count from their edge, not their middle.
    if (gSession.Client()) {
        for (const auto& bn : gSession.Client()->Bosses()) {
            const float dx = bn.x - player->actor.world.pos.x, dz = bn.z - player->actor.world.pos.z;
            const float d = std::sqrt(dx * dx + dz * dz) - royale::kBossBodyRadius;
            if (d > range * 1.05f) continue;
            s16 toTarget = static_cast<s16>(std::atan2(dx, dz) * (32768.0f / 3.14159265f));
            s16 off = static_cast<s16>(toTarget - player->actor.shape.rot.y);
            if (std::abs(static_cast<int>(off)) > 0x2800 && d > 40.0f) continue;
            if (d < bestDist) { bestDist = d; best = static_cast<uint16_t>(bn.Id()); }
        }
    }
    // Carts: from their sides too (a cart is about a Link and a half wide and two long). People go first: a cart only counts when it is clearly nearer.
    for (const CartTarget& ct : CartTargets()) {
        const float dx = ct.x - player->actor.world.pos.x, dz = ct.z - player->actor.world.pos.z;
        const float d = std::sqrt(dx * dx + dz * dz) - royale::kCartHitRadius * 0.6f;
        if (d > range * 1.05f) continue;
        s16 toTarget = static_cast<s16>(std::atan2(dx, dz) * (32768.0f / 3.14159265f));
        s16 off = static_cast<s16>(toTarget - player->actor.shape.rot.y);
        if (std::abs(static_cast<int>(off)) > 0x2800 && d > 40.0f) continue;
        if (d + 30.0f < bestDist) { bestDist = std::max(0.0f, d); best = ct.id; }
    }
    *outDist = bestDist;
    return best;
}

// The game's own big sword moves (a jump slash: A while Z-targeting; a spin attack: hold and release B) count for their
// real worth: a jump slash hits harder and a spin catches everyone around you. Reported once, as each one starts.
static uint8_t gLastStrikeAnim = 0;
static void ReportStrikeMoves(Player* player, const royale::HudState& hud, uint8_t anim) {
    const uint8_t last = gLastStrikeAnim;
    gLastStrikeAnim = anim;
    if (anim == last) return;
    const bool jump = anim == static_cast<uint8_t>(royale::Anim::JumpSlash), spin = anim == static_cast<uint8_t>(royale::Anim::SpinAttack);
    if (!jump && !spin) return;
    const royale::WeaponStats w = royale::WeaponOf(hud.weapon);
    if (w.damage <= 0 || w.ranged) return;
    float dist = 1e9f;
    const uint16_t target = PickStrikeTarget(player, w.range, &dist);
    if (dist < 1e8f) gSession.ReportAttack(target, true, static_cast<uint8_t>(jump ? royale::AttackStyle::JumpSlash : royale::AttackStyle::Spin));
    else { HitNpcInFront(player, w); SmashPropInFront(player, w); }
    gAttackCooldown = std::max(gAttackCooldown, static_cast<int>(std::ceil(w.cooldown * royale::kTickHz)));
}

void HandleCombatInput(Player* player, const royale::HudState& hud) {
    if (gAttackCooldown > 0) gAttackCooldown--;
    if (!LiveAndAlive(hud) || !InField()) return;
    const Input& in = gPlayState->state.input[0];

    // C-Up: jump. In the air, if a ledge about knee to chest high is right in front of you, you are hauled up onto it.
    if ((in.press.button & BTN_CUP) && (player->actor.bgCheckFlags & 1) && !(player->stateFlags1 & (PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_GETTING_ITEM))) {
        player->actor.velocity.y = 11.5f;
        player->actor.bgCheckFlags &= ~1;
        gJumpAssistFrames = 14;
        Audio_PlaySoundGeneral(NA_SE_PL_JUMP, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }
    if (gJumpAssistFrames > 0) {
        gJumpAssistFrames--;
        const float yaw = player->actor.shape.rot.y * (3.14159265f / 32768.0f);
        const float fx = std::sin(yaw), fz = std::cos(yaw);
        float ledge = 0;
        float ground = GroundY(gPlayState, player->actor.world.pos.x, player->actor.world.pos.z, player->actor.world.pos.y - 1.0f);
        float standingOn = 0;
        if (PlatformTopAt(player->actor.world.pos.x, player->actor.world.pos.z, &standingOn) && player->actor.world.pos.y >= standingOn - 30.0f) ground = std::max(ground, standingOn);   // the step you are on counts as the ground
        for (float reach : { 28.0f, 48.0f }) {
            float y;
            if (FloorAt(player->actor.world.pos.x + fx * reach, player->actor.world.pos.z + fz * reach, &y) && y > ledge) ledge = y;
        }
        const float rise = ledge - player->actor.world.pos.y;
        if (ledge > 0 && rise > 6.0f && ledge - ground > 24.0f && ledge - ground < 105.0f && rise < 70.0f && player->actor.velocity.y < 6.5f) {
            player->actor.world.pos.x += fx * 5.0f;                     // up and over the edge
            player->actor.world.pos.z += fz * 5.0f;
            player->actor.velocity.y = std::max(player->actor.velocity.y, 7.0f);
            if (rise < 14.0f) player->actor.world.pos.y = ledge + 1.0f;  // close enough: on top
        }
    }

    if (in.press.button & BTN_DDOWN) {
        if (hud.potions > 0) { gSession.RequestUsePotion(); StartAction(royale::Anim::Drink, kDrinkSeconds, BottleToDrink(hud, false)); DrinkSounds(player); }
        else Say("No potions");
    }

    // C-Left: drink a shield potion (the bar under your hearts).
    if (in.press.button & BTN_CLEFT) {
        bool has = false;
        for (const auto& pot : hud.inv.potions) has |= royale::PotionOf(static_cast<royale::ItemId>(pot.item)).shield > 0;
        if (!has) Say("No shield potion");
        else if (hud.inv.shield >= royale::kMaxShield - 0.05f) Say("Your shield is full");
        else { gSession.UseShield(); StartAction(royale::Anim::Drink, kDrinkSeconds, BottleToDrink(hud, true)); DrinkSounds(player); }
    }

    // A: open the chest in front of you, take or swap the item on the ground, hire an ally or talk. Walking over an upgrade picks it up on its own.
    // While a text box is up, A belongs to it.
    if ((in.press.button & BTN_A) && !MessageBoxUp()) {
        const size_t target = NearestLootIndex();
        if (target != kNoLoot) gSession.RequestPickup(static_cast<uint32_t>(target), true);
        else {   // nothing to open or take: maybe somebody to hire
            const int ally = NearbyFreeAlly();
            if (ally >= 0) {
                const royale::AllyDef& def = royale::kAllyDefs[ally];
                if (hud.inv.rupees < def.price) Say(std::string("The ") + def.name + " wants " + std::to_string(def.price) + " rupees (you have " + std::to_string(hud.inv.rupees) + ")");
                else gSession.HireAlly(ally);
            } else {
                PressedATalk();
            }
        }
    }

    // D-pad Right: your next weapon; D-pad Left: back to the one before (like the bumpers on a hotbar). With spares B and C, the hand goes A, B, C, A...
    if (in.press.button & (BTN_DLEFT | BTN_DRIGHT)) {
        const int spares = static_cast<int>(hud.inv.reserve.size());
        if (spares == 0) {
            Say("No other weapon: open chests to find more");
        } else if (in.press.button & BTN_DRIGHT) {
            gNextWeaponSlot = gNextWeaponSlot > spares ? 1 : gNextWeaponSlot;
            gSession.SelectWeapon(gNextWeaponSlot);
            gLastWeaponSlot = gNextWeaponSlot;
            gNextWeaponSlot = gNextWeaponSlot >= spares ? 1 : gNextWeaponSlot + 1;
        } else {   // back: swap again with the slot we last swapped, which puts the earlier weapon back in hand
            const int back = gLastWeaponSlot >= 1 && gLastWeaponSlot <= spares ? gLastWeaponSlot : spares;
            gSession.SelectWeapon(back);
            gNextWeaponSlot = back;
        }
    }

    if (in.press.button & BTN_DUP) {
        if (!hud.inv.hasAbility) Say("No ability");
        else if (hud.abilityReadyIn > 0.05f) Say("Ability recharging: " + ClockText(hud.abilityReadyIn));
        else if (hud.magic + 0.001f < royale::AbilityMagic(static_cast<royale::ItemId>(hud.inv.ability.item))) Say("Not enough magic (" + std::to_string(static_cast<int>(hud.magic)) + " of " + std::to_string(static_cast<int>(royale::AbilityMagic(static_cast<royale::ItemId>(hud.inv.ability.item)))) + ")");
        else {
            gSession.UseAbility();
            const royale::ItemId ab = static_cast<royale::ItemId>(hud.inv.ability.item);
            StartAction(royale::IsSong(ab) || ab == royale::ItemId::FairyOcarina || ab == royale::ItemId::OcarinaOfTime ? royale::Anim::Play
                        : ab == royale::ItemId::ShockwaveGrenade || ab == royale::ItemId::Hookshot || ab == royale::ItemId::Longshot ? royale::Anim::Throw : royale::Anim::Cast,
                        royale::IsSong(ab) ? 1.5f : 0.9f, ab);
            PowerFx(gPlayState, ab, player->actor.world.pos, true, &player->actor, hud.selfId);
            Audio_PlaySoundGeneral(royale::IsSong(ab) || ab == royale::ItemId::FairyOcarina || ab == royale::ItemId::OcarinaOfTime ? NA_SE_PL_MAGIC_SOUL_BALL : AbilitySfx(ab),
                                   &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            if (const u16 v = AbilityVoice(ab)) Player_PlaySfx(&player->actor, static_cast<u16>(v + player->ageProperties->unk_92));
        }
    }

    if (!(in.press.button & BTN_B) || gAttackCooldown > 0) return;
    const royale::AmmoKind ammoKind = royale::AmmoUsedBy(hud.weapon);
    const bool hasAmmo = ammoKind == royale::AmmoKind::None || hud.ammo[static_cast<size_t>(ammoKind)] > 0;
    royale::WeaponStats w = royale::ActiveWeapon(hud.weapon, hasAmmo);   // no ammo: the weapon is only bashed with
    if (w.damage <= 0) return;
    if (!hasAmmo && ammoKind != royale::AmmoKind::None && gAttackCooldown <= 0) Say(std::string("Out of ") + royale::AmmoName(ammoKind) + ": bash them with it, or find more");
    gAttackCooldown = std::max(1, static_cast<int>(std::ceil(w.cooldown * royale::kTickHz)));

    float bestDist = 1e9f;
    const uint16_t best = PickStrikeTarget(player, w.range, &bestDist);
    // Show the swing or the shot: face what you are hitting, strike the pose, and loose the arrow or throw the bomb.
    s16 aim = player->actor.shape.rot.y;
    if (bestDist < 1e8f) {
        float tx = 0, tz = 0;
        if (KnownPosition(best, &tx, &tz)) aim = static_cast<s16>(std::atan2(tx - player->actor.world.pos.x, tz - player->actor.world.pos.z) * (32768.0f / 3.14159265f));
        player->actor.shape.rot.y = player->actor.world.rot.y = aim;
    }
    StartAction(hasAmmo ? PoseForWeapon(hud.weapon) : royale::Anim::Attack, 0.45f);
    // The swing, shot or throw (its model, sound and flight) is the game's own item code now, run by the item on the B button.
    // The hookshot's chain and the grenade's blast are the mod's own (the game's real hookshot would fly Link to a wall, a real bomb would hurt him).
    if (hud.weapon == royale::ItemId::Hookshot || hud.weapon == royale::ItemId::ShockwaveGrenade) {
        Audio_PlaySoundGeneral(AbilitySfx(hud.weapon), &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        float tx = 0, tz = 0;
        if (hud.weapon == royale::ItemId::ShockwaveGrenade && bestDist < 1e8f && KnownPosition(best, &tx, &tz)) {
            const Vec3f at = { tx, player->actor.world.pos.y, tz };
            PowerFx(gPlayState, hud.weapon, at, false, &player->actor, hud.selfId);
        }
    }
    if (bestDist < 1e8f) { gSession.ReportAttack(best, true); return; }
    // Nothing in reach: the arrow, seed, bomb or bombchu still flies, so the server still spends it (else the count under the hotbar never drops).
    if (hasAmmo && ammoKind != royale::AmmoKind::None) gSession.ReportAttack(royale::net::kNoPlayer16, false);
    HitNpcInFront(player, w);
    SmashPropInFront(player, w);
}

// The match-start skydive, as in Fortnite: you spawn high above your spawn point, hang there during the countdown, then fall
// during the drop. The stick steers, holding Z dives faster, and the ground ends it (anyone still airborne when the drop ends
// plummets). The server only knows x and z, and protects everyone for the whole drop, so this is all done on the client.
constexpr float kSkyHeight = royale::kSkyHeight;   // shared/balance.h: the bots skydive with the same numbers
constexpr float kGlideSpeed = royale::kGlideSpeed;
constexpr float kDiveSpeed = royale::kDiveSpeed;
constexpr float kAirSpeed = royale::kAirSpeed;
// Where you may touch down: floor that is not water, lava, a door, a cliff face or a bottomless drop, inside the circle the storm has left.
bool SafeLanding(float x, float z, const royale::HudState& hud) {
    float y;
    if (!FloorAt(x, z, &y) || std::fabs(y - gMedianFloorY) > 1200.0f || UnderWater(x, z, y) || OnExitFloor(x, z) || NearLoadingZone(x, z, 200.0f) || HazardFloorAt(x, z)) return false;
    if (hud.safeZone.radius > 0.0f && royale::Distance({ x, z }, hud.safeZone.center) > hud.safeZone.radius * 0.97f) return false;
    return true;
}

// The skydive is steered by you, but it never lets you land in the lava, the lake or a doorway: low enough to matter, the glider eases toward the nearest
// ground that is safe, and if you somehow end up over nothing at all you are put back above the nearest safe ground.
royale::Vec2 gLandTarget = {};
bool gHaveLandTarget = false;
int gLandCheckAge = 0;
bool FindSafeLanding(float x, float z, const royale::HudState& hud, royale::Vec2* out) {
    for (float r = 120.0f; r <= 3600.0f; r += 120.0f) {   // rings outward from where you are: the nearest safe spot wins
        const int n = std::max(8, static_cast<int>(r / 60.0f));
        for (int i = 0; i < n; i++) {
            const float a = 6.2831853f * i / n;
            const float tx = x + std::sin(a) * r, tz = z + std::cos(a) * r;
            if (SafeLanding(tx, tz, hud)) { *out = { tx, tz }; return true; }
        }
    }
    return false;
}
void SteerToSafeLanding(Player* player, const royale::HudState& hud, float dt) {
    const float px = player->actor.world.pos.x, pz = player->actor.world.pos.z;
    float ground = 0;
    const bool haveGround = FloorAt(px, pz, &ground);
    const float height = haveGround ? player->actor.world.pos.y - ground : 1.0e5f;
    if (haveGround && height > 1500.0f) { gHaveLandTarget = false; return; }   // high up: steer wherever you like
    if (++gLandCheckAge >= 6 || (haveGround ? false : !gHaveLandTarget)) {       // a few times a second
        gLandCheckAge = 0;
        gHaveLandTarget = !SafeLanding(px, pz, hud) && FindSafeLanding(px, pz, hud, &gLandTarget);
    }
    if (!haveGround && player->actor.world.pos.y < gMedianFloorY - 400.0f) {   // fell past the bottom of the map: back above the closest safe ground
        royale::Vec2 safe;
        if (FindSafeLanding(px, pz, hud, &safe) || FindSafeLanding(hud.safeZone.center.x, hud.safeZone.center.z, hud, &safe)) {
            player->actor.world.pos.x = safe.x; player->actor.world.pos.z = safe.z;
            player->actor.world.pos.y = gMedianFloorY + 500.0f;
        }
        return;
    }
    if (!gHaveLandTarget) return;
    const float dx = gLandTarget.x - px, dz = gLandTarget.z - pz;
    const float d = std::hypot(dx, dz);
    if (d < 20.0f) { gHaveLandTarget = false; return; }
    const float pull = std::min(d, kAirSpeed * 1.6f * dt);   // a firm drift: faster than you can steer away from it once you are low
    player->actor.world.pos.x += dx / d * pull;
    player->actor.world.pos.z += dz / d * pull;
}

void UpdateSkydive(Player* player, const royale::HudState& hud) {
    if (!gSkydiving) return;
    const bool phaseOk = hud.state == royale::MatchState::Countdown || hud.state == royale::MatchState::Drop || hud.state == royale::MatchState::InMatch;
    if (!phaseOk || !InField() || (hud.haveSelf && !hud.selfAlive)) { gSkydiving = false; gSandboxGlide = false; return; }
    const bool dropLike = hud.state == royale::MatchState::Drop || gSandboxGlide;   // gliding freely, as in the drop
    constexpr float dt = 1.0f / royale::kTickHz;
    const Input& in = gPlayState->state.input[0];

    // Steer relative to the camera, the way Link's stick normally works.
    const float sx = in.cur.stick_x, sy = in.cur.stick_y;
    const float mag = std::min(1.0f, std::sqrt(sx * sx + sy * sy) / 60.0f);
    if (mag > 0.1f) {
        const float yaw = static_cast<float>(Camera_GetInputDirYaw(GET_ACTIVE_CAM(gPlayState))) * (3.14159265f / 32768.0f);
        const float a = yaw + std::atan2(-sx, sy);
        player->actor.world.pos.x += std::sin(a) * kAirSpeed * mag * dt;
        player->actor.world.pos.z += std::cos(a) * kAirSpeed * mag * dt;
        player->actor.shape.rot.y = static_cast<s16>(a * (32768.0f / 3.14159265f));
        player->actor.world.rot.y = player->actor.shape.rot.y;
    }

    gGliderRoll += ((mag > 0.1f ? -sx / 80.0f * 0.5f : 0.0f) - gGliderRoll) * 0.12f;                  // bank into the turn
    gGliderDiving = dropLike && gDiveHeld;

    float fall = 0.0f;                                                          // hold in the sky during the countdown
    if (dropLike) fall = gDiveHeld ? kDiveSpeed : kGlideSpeed;
    else if (hud.state == royale::MatchState::InMatch) fall = royale::kLateFallSpeed;           // the drop is over: land now
    SteerToSafeLanding(player, hud, dt);
    const float ground = GroundY(gPlayState, player->actor.world.pos.x, player->actor.world.pos.z, -1.0e6f);
    float y = player->actor.world.pos.y - fall * dt;
    if (ground > -1.0e5f && y <= ground + 8.0f) {
        y = ground + 8.0f;
        gSkydiving = false;                                                       // touched down; normal gravity takes over
        gSandboxGlide = false;
    }
    player->actor.world.pos.y = y;
    player->actor.prevPos = player->actor.world.pos;
    player->actor.velocity.y = 0.0f;
    player->actor.speedXZ = 0.0f;
    player->fallDistance = 0;                                                     // no landing damage or hard-landing stun
}

// The Shockwave Grenade, as in Fortnite: a blast under your feet throws you high into the air, and you take no fall damage (no hard landing,
// no stagger) until you land and for a moment after. The server only knows x and z, so this is all done here. Link is in his own jump the
// whole way, so the stick steers him in the air as in any jump, and walls and ledges work as they always do.
constexpr float kLaunchGraceSeconds = 1.0f;   // still safe this long after touching down (a bounce off a ledge, a slope into a drop)
int gLaunchFrames = 0;                        // frames since the launch; 0 = no launch going
bool gLaunchAirborne = false;                 // has left the ground
double gLaunchSafeUntil = 0;                  // set when you touch down

void LaunchSelf(Player* player, royale::Rarity rarity) {
    if (gSkydiving || player->stateFlags1 & (PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_DEAD)) return;
    const float height = 260.0f * royale::RarityScale(rarity);           // Uncommon about 300 units up (six Links), Legendary about 455
    const float g = player->actor.gravity < -0.3f ? -player->actor.gravity : 1.0f;
    player->actor.velocity.y = std::sqrt(2.0f * g * height);
    player->actor.world.pos.y += 2.0f;                                   // off the floor, so the game puts him in his jump next frame
    player->actor.bgCheckFlags &= ~1;
    player->fallStartHeight = static_cast<s16>(player->actor.world.pos.y);
    gLaunchFrames = 1;
    gLaunchAirborne = false;
    gLaunchSafeUntil = 0;
}

void UpdateLaunch(Player* player) {
    if (gLaunchFrames == 0) return;
    gLaunchFrames++;
    const bool grounded = (player->actor.bgCheckFlags & 1) || (player->stateFlags1 & PLAYER_STATE1_IN_WATER);
    if (!grounded) gLaunchAirborne = true;
    // The game measures a fall from the last height it saw you standing at. Moving that up with you means every landing is a short hop.
    player->fallStartHeight = static_cast<s16>(player->actor.world.pos.y);
    player->fallDistance = 0;
    const double now = ImGui::GetTime();
    if (!gLaunchAirborne) {
        if (gLaunchFrames > 10) gLaunchFrames = 0;                       // never got off the ground (hanging on a ledge, say): nothing to protect
        return;
    }
    if (!grounded && gLaunchSafeUntil == 0 && (gLaunchFrames & 1)) {     // a purple trail behind you on the way up and down
        Vec3f pos = { player->actor.world.pos.x, player->actor.world.pos.y + 10.0f, player->actor.world.pos.z }, vel = { 0, -0.5f, 0 }, accel = { 0, 0, 0 };
        Color_RGBA8 prim = { 235, 190, 255, 255 }, env = { 160, 60, 255, 255 };
        EffectSsKiraKira_SpawnDispersed(gPlayState, &pos, &vel, &accel, &prim, &env, 90, 20);
    }
    if (grounded && gLaunchSafeUntil == 0) gLaunchSafeUntil = now + kLaunchGraceSeconds;
    if ((gLaunchSafeUntil > 0 && now > gLaunchSafeUntil) || gLaunchFrames > 20 * 30) gLaunchFrames = 0;   // done (or something held you in the air for ages)
}

// Where you will land if you keep doing what you are doing: a ring on the ground at that spot (and a faint one straight below you), with how
// high you are. Steering moves the ring, holding Z pulls it closer, so you can pick a landing spot before you get there.
void DrawGliderAim(ImDrawList* dl, ImFont* font, ImVec2 ds, float scale) {
    if (gPlayState == nullptr || !InField()) return;
    Player* player = GET_PLAYER(gPlayState);
    const Input& in = gPlayState->state.input[0];
    const float px = player->actor.world.pos.x, pz = player->actor.world.pos.z, py = player->actor.world.pos.y;
    const float below = GroundY(gPlayState, px, pz, -1.0e6f);
    if (below < -1.0e5f) return;
    const float sx = in.cur.stick_x, sy = in.cur.stick_y;
    const float mag = std::min(1.0f, std::sqrt(sx * sx + sy * sy) / 60.0f);
    const bool dropping = gStateNow == royale::MatchState::Drop;
    const float fall = dropping ? (gDiveHeld ? kDiveSpeed : kGlideSpeed) : royale::kLateFallSpeed;
    const float seconds = std::max(0.0f, (py - below)) / fall;
    float lx = px, lz = pz;
    if (mag > 0.1f) {   // the same steering the skydive uses, held for the rest of the fall
        const float yaw = static_cast<float>(Camera_GetInputDirYaw(GET_ACTIVE_CAM(gPlayState))) * (3.14159265f / 32768.0f);
        const float a = yaw + std::atan2(-sx, sy);
        lx += std::sin(a) * kAirSpeed * mag * seconds;
        lz += std::cos(a) * kAirSpeed * mag * seconds;
    }
    const float ly = GroundY(gPlayState, lx, lz, py);
    const ImU32 gold = IM_COL32(255, 236, 120, 255);
    ImVec2 at, under;
    const bool haveUnder = WorldToScreen(px, below + 2.0f, pz, &under);
    if (WorldToScreen(lx, (ly > -1.0e5f ? ly : below) + 2.0f, lz, &at) && at.x > 0 && at.x < ds.x && at.y > 0 && at.y < ds.y) {
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 6.0f);
        const float r = (16.0f + 3.0f * pulse) * scale;
        dl->AddCircle(at, r + 2 * scale, IM_COL32(0, 0, 0, 160), 24, 5.0f * scale);
        dl->AddCircle(at, r, gold, 24, 3.0f * scale);
        dl->AddCircleFilled(at, 3.5f * scale, gold);
        if (haveUnder && mag > 0.1f) dl->AddLine(under, at, IM_COL32(255, 236, 120, 90), 2.0f * scale);
        char text[48];
        std::snprintf(text, sizeof(text), "%d m", static_cast<int>((py - below) * 0.1f));
        const ImVec2 sz = font->CalcTextSizeA(15.0f * scale, FLT_MAX, 0, text);
        dl->AddText(font, 15.0f * scale, ImVec2(at.x - sz.x * 0.5f + scale, at.y + r + 5 * scale + scale), IM_COL32(0, 0, 0, 220), text);
        dl->AddText(font, 15.0f * scale, ImVec2(at.x - sz.x * 0.5f, at.y + r + 5 * scale), IM_COL32(255, 255, 255, 255), text);
    }
}

// Boots, Epona's Song and friends: the server allows a faster run and the game applies it by stretching the step Link just took.
// (Teleports and the drop to spawn move far more than a step, so those are left alone.)
royale::Vec2 gLastPos = {};
bool gHaveLastPos = false;
void ApplySpeedBuffs(Player* player, const royale::HudState& hud) {
    const bool active = LiveAndAlive(hud) && InField();
    const float mult = hud.speedMult;
    if (active && gHaveLastPos && std::fabs(mult - 1.0f) > 0.01f) {
        const float dx = player->actor.world.pos.x - gLastPos.x, dz = player->actor.world.pos.z - gLastPos.z;
        if (dx * dx + dz * dz < 40.0f * 40.0f) {
            player->actor.world.pos.x += dx * (mult - 1.0f);
            player->actor.world.pos.z += dz * (mult - 1.0f);
        }
    }
    gLastPos = { player->actor.world.pos.x, player->actor.world.pos.z };
    gHaveLastPos = active;
}

// Link holds what the server says he holds, using the game's own item code. The matching real item (Deku Stick, bow, bombs...) sits on the B
// button, so the game itself draws it, puts it in his hand and runs its swing, shot or throw; before this the item was forced into his hand with
// B empty, and the game put it away again a moment later. The save's own B item is put back when the match is over.
royale::ItemId gLocalWeaponShown = static_cast<royale::ItemId>(255);
bool gLocalLookApplied = false;
u8 gSavedButtonItem0 = ITEM_NONE;
u8 RealItemFor(royale::ItemId w) {
    using royale::ItemId;
    switch (w) {
        case ItemId::BasicSword: case ItemId::KokiriSword: return ITEM_SWORD_KOKIRI;
        case ItemId::MasterSword: case ItemId::GildedSword: return ITEM_SWORD_MASTER;
        case ItemId::BiggoronSword: return ITEM_SWORD_BGS;
        case ItemId::MegatonHammer: case ItemId::GiantsHammer: return ITEM_HAMMER;
        case ItemId::DekuStick: return ITEM_STICK;
        case ItemId::FairyBow: return ITEM_BOW;
        case ItemId::FireArrows: return ITEM_BOW_ARROW_FIRE;
        case ItemId::IceArrows: return ITEM_BOW_ARROW_ICE;
        case ItemId::LightArrows: return ITEM_BOW_ARROW_LIGHT;
        case ItemId::Slingshot: case ItemId::TripleSlingshot: return ITEM_SLINGSHOT;
        case ItemId::Boomerang: return ITEM_BOOMERANG;
        case ItemId::Bombs: return ITEM_BOMB;
        case ItemId::Bombchus: case ItemId::HomingBombchus: return ITEM_BOMBCHU;
        case ItemId::DekuNuts: return ITEM_NUT;
        default: return ITEM_NONE;
    }
}
// The game's own enhancements that let any Link hold and shoot any item (child with the bow, adult with the slingshot); on only during a match.
const char* const kItemCvars[3] = { CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), CVAR_ENHANCEMENT("BowSlingshotAmmoFix"), CVAR_CHEAT("TimelessEquipment") };
int gSavedItemCvars[3] = {};
bool gItemCvarsOn = false;
void ApplyItemCvars(bool on) {
    if (on == gItemCvarsOn) return;
    gItemCvarsOn = on;
    for (int i = 0; i < 3; i++) {
        if (on) { gSavedItemCvars[i] = CVarGetInteger(kItemCvars[i], 0); CVarSetInteger(kItemCvars[i], 1); }
        else CVarSetInteger(kItemCvars[i], gSavedItemCvars[i]);
    }
}
// The game's use of an item checks its ammo and (for the elemental arrows) magic, so give it the server's counts; the server stays the authority.
void SyncRealAmmo(const royale::HudState& hud) {
    auto set = [&](int slot, royale::AmmoKind k) { gSaveContext.inventory.ammo[slot] = static_cast<s8>(std::min(99, static_cast<int>(hud.ammo[static_cast<size_t>(k)]))); };
    set(SLOT_BOW, royale::AmmoKind::Arrows);
    set(SLOT_SLINGSHOT, royale::AmmoKind::Seeds);
    set(SLOT_BOMB, royale::AmmoKind::Bombs);
    set(SLOT_BOMBCHU, royale::AmmoKind::Bombchus);
    set(SLOT_NUT, royale::AmmoKind::Nuts);
    gSaveContext.inventory.ammo[SLOT_STICK] = 9;   // sticks never run out
    if (hud.weapon == royale::ItemId::FireArrows || hud.weapon == royale::ItemId::IceArrows || hud.weapon == royale::ItemId::LightArrows) {
        gSaveContext.isMagicAcquired = true;
        gSaveContext.magicLevel = 1;
        gSaveContext.magicCapacity = 0x30;
        gSaveContext.magic = 0x30;
    }
}
// What your own Link wears in a match, as the game's values: the boots and mask are only put on him while he is drawn (the game's own iron boots,
// hover boots and bunny hood would also change how he moves, and the server decides that), the shield for real (it is what he raises).
struct LocalDress {
    bool on = false;
    s8 boots = PLAYER_BOOTS_KOKIRI;
    u8 mask = PLAYER_MASK_NONE;
    royale::ItemId weapon = royale::ItemId::BasicSword;
};
LocalDress gLocalDress;
bool LocalDressOn() { return gLocalDress.on; }
royale::ItemId WornGear(const royale::HudState& hud, royale::GearSlot slot) {
    const int i = static_cast<int>(slot);
    return (hud.inv.gearMask & (1 << i)) ? static_cast<royale::ItemId>(hud.inv.gear[i].item) : royale::ItemId::Count;
}
void SyncLocalDress(Player* player, const royale::HudState& hud) {
    gLocalDress.on = gSession.Joined() && IsLive(hud) && hud.selfAlive && InField();
    gSelfInvulnLeft = hud.invulnLeft;
    if (!gLocalDress.on) return;
    gLocalDress.boots = PlayerBootsFor(WornGear(hud, royale::GearSlot::Boots));
    gLocalDress.mask = PlayerMaskFor(WornGear(hud, royale::GearSlot::Mask));
    gLocalDress.weapon = hud.weapon;
    const s8 shield = hud.hasShield ? PlayerShieldFor(hud.shield) : PLAYER_SHIELD_NONE;
    if (player->currentShield != shield && !(player->stateFlags1 & PLAYER_STATE1_SHIELDING)) {
        Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, static_cast<u16>(shield));   // the pause screen and the game's own checks agree (put back after the match)
        player->currentShield = shield;
        Player_SetModelGroup(player, Player_ActionToModelGroup(player, player->heldItemAction));
    }
    if (player->actor.draw == Player_Draw) player->actor.draw = LocalLink_Draw;
}
// Draws your own Link dressed (see LocalDress). While you drink, play a song or use a gadget, the bottle, the ocarina or the hookshot is in his
// hand as in the game, and if you are standing still he goes through the game's motions for it (the bottle raised, the ocarina played, the
// spell cast). Swings and shots need none of this: they are the game's own item code (SyncLocalWeapon).
void DrawLocalDressed(Player* player, PlayState* play, bool mayPose) {
    if (!gLocalDress.on) { Player_Draw(&player->actor, play); return; }
    const s8 boots = player->currentBoots;
    const u8 mask = player->currentMask;
    player->currentBoots = gLocalDress.boots;
    player->currentMask = gLocalDress.mask;
    const bool using_ = mayPose && gActionFrames > 0 && gActionItem != royale::ItemId::Count && player->meleeWeaponState == 0 &&
                        !(player->stateFlags1 & (PLAYER_STATE1_SHIELDING | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_DEAD));
    const s32 group = player->modelGroup;
    const s8 itemAction = player->itemAction, held = player->heldItemAction;
    const u8 button = gSaveContext.equips.buttonItems[0];
    bool swapped = false;
    if (using_) {
        const float t = static_cast<float>(ImGui::GetTime() - gActionStart);
        const Look look = ActionLook(static_cast<uint8_t>(gActionAnim), gLocalDress.weapon, gActionItem, t, gActionItem);
        if (look.modelGroup != group || look.itemAction != held) {
            gSaveContext.equips.buttonItems[0] = look.buttonItem;
            player->itemAction = player->heldItemAction = look.itemAction;
            Player_SetModelGroup(player, look.modelGroup);
            ShowStickLength(player);
            swapped = true;
        }
        if (std::fabs(player->linearVelocity) < 1.0f && (player->actor.bgCheckFlags & 1))
            PoseSeq(player, SeqFor(static_cast<uint8_t>(gActionAnim), gLocalDress.weapon, 0, gActionItem, player), t);
    }
    gGildedPlayer = gLocalDress.weapon == royale::ItemId::GildedSword ? player : nullptr;
    Player_Draw(&player->actor, play);
    gGildedPlayer = nullptr;
    if (swapped) {
        gSaveContext.equips.buttonItems[0] = button;
        player->itemAction = itemAction;
        player->heldItemAction = held;
        Player_SetModelGroup(player, group);
    }
    player->currentBoots = boots;
    player->currentMask = mask;
}

// A bomb or bombchu in Link's hands burns down like a thrown one (a bomb in 70 frames, a bombchu in 120) and blows up in his hands. Here you carry your
// weapon until you throw it, so the fuse is held where it started for as long as he holds it; it starts the moment he lets go.
void HoldFuse(Player* player) {
    Actor* held = player->heldActor;
    if (held == nullptr || held->parent != &player->actor) return;
    if (held->id == ACTOR_EN_BOM) reinterpret_cast<EnBom*>(held)->timer = 70;
    else if (held->id == ACTOR_EN_BOM_CHU) reinterpret_cast<EnBomChu*>(held)->timer = 120;
}
int gUseRetry = 0;
void SyncLocalWeapon(Player* player, const royale::HudState& hud) {
    ApplyItemCvars(gSession.Joined() && IsLive(hud));
    SyncLocalDress(player, hud);
    const bool on = LiveAndAlive(hud) && InField() && !gSkydiving && gEmote.id < 0;
    if (!on) {
        if (gLocalLookApplied && !(gSession.Joined() && IsLive(hud))) { gSaveContext.equips.buttonItems[0] = gSavedButtonItem0; gLocalLookApplied = false; gLocalWeaponShown = static_cast<royale::ItemId>(255); }
        return;
    }
    const Look look = LookFor(hud.weapon);
    const u8 item = RealItemFor(hud.weapon);
    if (!gLocalLookApplied) { gSavedButtonItem0 = gSaveContext.equips.buttonItems[0]; gLocalLookApplied = true; gLocalWeaponShown = static_cast<royale::ItemId>(255); }
    SyncRealAmmo(hud);
    HoldFuse(player);
    gSaveContext.equips.buttonItems[0] = item;
    // The interface puts the bow, slingshot and bombchu back from this value when it refreshes the B button, so it must name the same item.
    gSaveContext.buttonStatus[0] = item;
    if (item == ITEM_NONE) return;
    const bool free = !(player->stateFlags1 & (PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_GETTING_ITEM | PLAYER_STATE1_START_CHANGING_HELD_ITEM | PLAYER_STATE1_SHIELDING)) &&
                      player->stateFlags2 == (player->stateFlags2 & ~PLAYER_STATE2_OCARINA_PLAYING);
    const s8 want = Player_ItemToItemAction(item);
    if (hud.weapon != gLocalWeaponShown && player->heldItemAction == want) gLocalWeaponShown = hud.weapon;   // same real item (slingshot and triple slingshot, bombchus and homing ones): using it again would fire it
    if (hud.weapon != gLocalWeaponShown || player->heldItemAction != want) {
        if (!free) return;
        if (gUseRetry > 0) { gUseRetry--; return; }
        gLocalWeaponShown = hud.weapon;
        gUseRetry = 10;
        Player_UseItem(gPlayState, player, item);   // the game's own take-out: the change animation, the sound, the item in his hand
        if (player->heldItemAction != want) {       // refused (nothing to shoot, say): still show it in the hand
            player->itemAction = player->heldItemAction = look.itemAction;
            Player_SetModelGroup(player, look.modelGroup);
            ShowStickLength(player);
        }
    }
}

// In a match only the server eliminates anyone. If the game's own death started anyway (health hit 0 inside the player's update, before
// this hook could put it back), undo it before the game-over screen, its music or its camera take over: the player would otherwise be
// stuck in the dying animation while spectating, then thrown to the game-over menu.
void CancelGameDeath(Player* player) {
    if (!(player->stateFlags1 & PLAYER_STATE1_DEAD) && gPlayState->gameOverCtx.state == GAMEOVER_INACTIVE) {
        gMatchSeqId = gSaveContext.seqId;
        gMatchAmbienceId = gSaveContext.natureAmbienceId;
        return;
    }
    gPlayState->gameOverCtx.state = GAMEOVER_INACTIVE;
    player->stateFlags1 &= ~PLAYER_STATE1_DEAD;
    gPlayState->func_11D54(player, gPlayState);   // back to standing still
    OnePointCutscene_EndCutscene(gPlayState, SUBCAM_ACTIVE);   // the death's close-up camera
    Audio_QueueSeqCmd(0x100000FF | (SEQ_PLAYER_FANFARE << 24));   // stop the game-over tune
    func_800F47FC();                                               // the death muted the music; bring it back up
    gSaveContext.seqId = gMatchSeqId;
    gSaveContext.natureAmbienceId = gMatchAmbienceId;
    if (gMatchSeqId != 0xFF) Audio_QueueSeqCmd((SEQ_PLAYER_BGM_MAIN << 24) | gMatchSeqId);
}

void OnPlayerUpdate() {
    Feat("player update");
    gRoyaleRunSpeedScale = 1.0f;   // normal speed unless UpdateSprint below says otherwise
    gRoyaleNoAimView = 0;
    gRoyaleNoUseOnTakeOut = 0;
    if (!gSession.Joined() || !InGame()) { gCamLiftNow = 0.0f; gRoyaleCamLift = 0.0f; return; }
    Player* player = GET_PLAYER(gPlayState);
    royale::GameClient* client = gSession.Client();
    royale::HudState hud = gSession.Hud();
    ApplySwimPhysics(player);
    {   // the rock of a blow on your own Link, tilted about the feet on top of whatever he is doing (not on the cart saddle: that sets its own tilt)
        static bool tilted = false;
        auto rc = gHitRecoil.find(hud.selfId);
        if (rc != gHitRecoil.end() && player->actor.draw != LocalRide_Draw) {
            rc->second.age += 1.0f / royale::kTickHz;
            float pitch = 0, roll = 0;
            RecoilTilt(rc->second, player->actor.shape.rot.y * (3.14159265f / 32768.0f), &pitch, &roll);
            player->actor.shape.rot.x = static_cast<s16>(pitch * (32768.0f / 3.14159265f));
            player->actor.shape.rot.z = static_cast<s16>(roll * (32768.0f / 3.14159265f));
            tilted = true;
            if (rc->second.age > 0.5f) gHitRecoil.erase(rc);
        } else if (tilted) {
            if (player->actor.draw != LocalRide_Draw) player->actor.shape.rot.x = player->actor.shape.rot.z = 0;
            tilted = false;
        }
    }

    // The server moves everyone to spawn points when the match starts. It only knows x and z, so drop from above. This only
    // happens once the player has actually arrived in the field (they may still be loading in from the waiting room).
    if (InField() && client->Epoch() != gLastEpoch) {
        gLastEpoch = client->Epoch();
        if (const royale::net::PlayerNet* self = client->Self()) {
            player->actor.world.pos.x = self->x;
            player->actor.world.pos.z = self->z;
            const float ground = GroundY(gPlayState, self->x, self->z, 1500.0f);
            // At the start of a match you spawn high in the sky and fall in; any other teleport (Hookshot, songs) is a plain move.
            gSkydiving = hud.state == royale::MatchState::Countdown || hud.state == royale::MatchState::Drop;
            player->actor.world.pos.y = gSkydiving ? ground + kSkyHeight : ground + 20.0f;
            player->actor.prevPos = player->actor.world.pos;
            player->actor.home.pos = player->actor.world.pos;
        }
    }

    // Only report a position from a scene the server expects. Otherwise a player still loading in from the waiting room
    // would drag their spawn point toward their old coordinates.
    bool allowed = hud.state == royale::MatchState::Lobby ? InPlayableScene(hud.state) : InField();
    if (allowed) {
        const uint8_t anim = ClassifyAnim(player);
        if (hud.state == royale::MatchState::InMatch) ReportStrikeMoves(player, hud, anim);
        gSession.SendLocalPose(player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z,
                               player->actor.shape.rot.y, anim, static_cast<uint8_t>(gPlayState->sceneNum));
    }

    {   // Adult Power: you grow, and shrink back when it runs out
        const float target = 0.01f * (hud.adultLeft > 0.0f ? royale::kAdultScale : 1.0f);
        const float k = player->actor.scale.x + (target - player->actor.scale.x) * 0.15f;
        player->actor.scale.x = player->actor.scale.y = player->actor.scale.z = k;
    }
    UpdateSkydive(player, hud);
    UpdateLaunch(player);
    ReconcileLocalGlider(gSkydiving);
    if (gSkydiving) {   // hang from the glider's bar with both hands, like the puppets do
        LinkAnimationHeader* hang = RA(normal_jump_climb_wait);
        if (player->skelAnime.animation != (void*)hang) LinkAnimation_PlayLoop(gPlayState, &player->skelAnime, hang);
    }
    UpdateEmote(player, hud);
    // Safety net: a living player in a live match is never invisible (the flag is set only for emotes and eliminated spectators).
    if (gEmote.id < 0 && InField() && LiveAndAlive(hud)) player->stateFlags2 &= ~PLAYER_STATE2_DISABLE_DRAW;
    if (gSession.Joined() && IsLive(hud) && hud.selfAlive && InField()) HeldGlow(gPlayState, player, hud.weaponRarity, true);
    NoticePoi(player, hud);
    if (gActionFrames > 0) gActionFrames--;
    // Shots are aimed by where Link faces, with a target or without: with nothing to Z-target the game would otherwise swing the camera into the
    // first-person aiming view (a mode this match never uses), so ready and fire in place as it does when locked on.
    gRoyaleNoAimView = gSession.Joined() && IsLive(hud) ? 1 : 0;
    gRoyaleNoUseOnTakeOut = gRoyaleNoAimView;
    SyncLocalWeapon(player, hud);
    DriveFortnite(player, hud);
    EnsureSolidScenery();
    gAngryPlayerAlive = !(hud.haveSelf && !hud.selfAlive);
    ApplyPlatforms(player);
    ApplyRocks(player);
    HandleCombatInput(player, hud);
    UpdateSprint(player, hud);
    ApplySpeedBuffs(player, hud);
    UpdateLocalRide(player, hud);
    UpdateRideCamera(player);

    // The server owns health once the match is on. Overwrite the local value every frame so enemies, falls and the
    // game's own damage can't change it, and let a server-side elimination kill Link.
    bool dead = hud.haveSelf && !hud.selfAlive && (IsLive(hud) || hud.state == royale::MatchState::Ending);
    if (IsLive(hud) && hud.haveSelf) {
        if (!gHealthOverridden) {
            // Remember the player's real values so they can be put back afterwards (see RestoreHealth).
            gSavedCapacity = gSaveContext.healthCapacity;
            gSavedHealth = gSaveContext.health;
            gHealthOverridden = true;
        }
        gSaveContext.healthCapacity = static_cast<s16>(std::lround(hud.maxHealth * 16.0f));
        // An eliminated player does not die in the game (that would end in the game-over screen). They become an invisible,
        // invulnerable spectator who can still walk around and watch the rest of the match.
        // Never 0 while alive, even for a sliver of health or the tick where the server has zeroed health but not yet sent the
        // elimination: the game starts its own death and game-over screen the moment the save's health reads 0.
        gMatchHealth = hud.selfAlive ? static_cast<s16>(std::max(1L, std::lround(hud.selfHealth * 16.0f))) : gSaveContext.healthCapacity;
        gSaveContext.health = gMatchHealth;
        CancelGameDeath(player);
    }
    static bool wasDead = false;
    if (dead && !wasDead && IsLive(hud)) StartDeath(hud);
    if (LiveAndAlive(hud) && InField()) {   // how far you went, for the end screen (a jump of more than a few steps is a teleport, not travel)
        const float x = player->actor.world.pos.x, z = player->actor.world.pos.z;
        const float step = gMyStats.havePos ? std::hypot(x - gMyStats.lastX, z - gMyStats.lastZ) : 0.0f;
        if (step < 120.0f) gMyStats.distance += step;
        gMyStats.lastX = x; gMyStats.lastZ = z; gMyStats.havePos = true;
    }
    if (dead && !wasDead && InField()) {   // your body falls where you stood, thrown away from whoever got you (or backwards)
        royale::PuppetState me;
        me.x = player->actor.world.pos.x; me.y = player->actor.world.pos.y; me.z = player->actor.world.pos.z;
        me.id = hud.selfId; me.rot = player->actor.shape.rot.y; me.weapon = hud.weapon; me.tunic = gLocalTunic; me.scene = static_cast<uint8_t>(gPlayState->sceneNum);
        const float a = me.rot * (3.14159265f / 32768.0f);
        float px = -std::sin(a), pz = -std::cos(a);
        auto killer = gState.find(gDeath.killer);
        if (killer != gState.end() && std::hypot(me.x - killer->second.x, me.z - killer->second.z) > 1.0f) { px = me.x - killer->second.x; pz = me.z - killer->second.z; }
        gDeath.body = SpawnCorpse(me, px, pz);
    }
    if (dead && !wasDead) gSpectateTarget = kSpectateSelf;
    wasDead = dead;
    if (dead && gHealthOverridden) {
        player->stateFlags2 |= PLAYER_STATE2_DISABLE_DRAW;
        player->invincibilityTimer = 20;
        gSpectating = true;
        // The camera follows the invisible player, so watching someone means standing (unseen) on top of them.
        const u16 pressed = gPlayState->state.input[0].press.button;
        if (pressed & BTN_DLEFT) CycleSpectate(-1);
        if (pressed & BTN_DRIGHT) CycleSpectate(1);
        if (gSpectateTarget != kSpectateSelf && SpectateTarget() == nullptr) CycleSpectate(1); // who you watched is out
        if (const royale::PuppetState* t = SpectateTarget()) {
            player->actor.world.pos.x = t->x;
            player->actor.world.pos.z = t->z;
            player->actor.world.pos.y = t->isBot ? BotY(gPlayState, t->x, t->z, t->y) : t->y;
            player->actor.prevPos = player->actor.world.pos;
            player->actor.velocity.y = 0.0f;
            player->actor.shape.rot.y = t->rot;
        } else if (gDeath.body != 0) {   // watching your own body: stay (unseen) with it as it tumbles, so the camera keeps it in view
            auto body = gCorpses.find(gDeath.body);
            if (body != gCorpses.end() && !body->second.dying && body->second.actor != nullptr) {
                player->actor.world.pos = body->second.actor->world.pos;
                player->actor.prevPos = player->actor.world.pos;
                player->actor.velocity.y = 0.0f;
                player->actor.speedXZ = 0.0f;
            }
        }
    } else if (gSpectating) {
        gSpectating = false; // the flag is per-frame, so it clears itself
    }
}

// Announce the moments players care about without needing the menu open.
void ReportEvents(const royale::HudState& hud) {
    for (const royale::ClientEvent& e : gSession.DrainEvents()) {
        auto nameOf = [&](uint16_t id) -> std::string {
            for (const auto& r : hud.roster) if (r.id == id) return r.name;
            if (royale::IsBossId(id)) return std::string("a mini boss");
            if (royale::IsVehicleId(id)) return std::string("a cart");
            return royale::BotName(id);
        };
        switch (e.type) {
            case royale::ClientEvent::Type::PlayerJoined:
                if (e.id != hud.selfId) {
                    // The roster entry arrives with the event, but this frame's HudState may predate it.
                    Say(nameOf(e.id) + " joined the lobby");
                }
                break;
            case royale::ClientEvent::Type::PlayerLeft:
                Say("A player left the lobby");
                break;
            case royale::ClientEvent::Type::Damaged: {
                if (e.other == hud.selfId && e.id != hud.selfId && IsLive(hud)) { gMyStats.damage += e.amount; gMyStats.hits++; }   // for the death and end screens
                if (e.id == hud.selfId && IsLive(hud)) gMyStats.taken += e.amount;
                if (!InGame()) break;
                Player* me = GET_PLAYER(gPlayState);
                const double now = ImGui::GetTime();
                auto swing = [&](uint16_t who) { if (who != hud.selfId && !royale::IsBossId(who)) gSwingFrames[who] = 10; };
                auto soundAt = [&](uint16_t who, u16 sfx) {
                    auto a = gActorOf.find(who);
                    if (a != gActorOf.end()) Audio_PlaySoundGeneral(sfx, &a->second->projectedPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                };
                {   // Homing bombchus leave a purple streak from the thrower to what they hit.
                    royale::ItemId weapon = royale::ItemId::BasicSword;
                    float fx = 0, fz = 0, tx2 = 0, tz2 = 0, fy = me->actor.world.pos.y;
                    bool haveFrom = false, haveTo = false;
                    if (e.other == hud.selfId) { weapon = hud.weapon; fx = me->actor.world.pos.x; fz = me->actor.world.pos.z; haveFrom = true; }
                    else if (gState.count(e.other)) { weapon = gState[e.other].weapon; fx = gState[e.other].x; fz = gState[e.other].z; haveFrom = true; }
                    if (e.id == hud.selfId) { tx2 = me->actor.world.pos.x; tz2 = me->actor.world.pos.z; haveTo = true; }
                    else if (gState.count(e.id)) { tx2 = gState[e.id].x; tz2 = gState[e.id].z; haveTo = true; }
                    if (haveTo) {   // fire, ice, light and nuts leave the game's own mark on whoever they hit
                        float ty0 = me->actor.world.pos.y;
                        if (e.id != hud.selfId) { auto tgt = gActorOf.find(e.id); if (tgt != gActorOf.end()) ty0 = tgt->second->world.pos.y; }
                        Vec3f hit = { tx2, ty0 + 35.0f, tz2 };
                        using royale::ItemId;
                        if (weapon == ItemId::FireArrows) {
                            for (int i = 0; i < 4; i++) { Vec3f p2 = { hit.x + (Rand_ZeroOne() - 0.5f) * 30.0f, hit.y + (Rand_ZeroOne() - 0.5f) * 40.0f, hit.z + (Rand_ZeroOne() - 0.5f) * 30.0f };
                                                          Color_RGBA8 pc = { 255, 200, 0, 255 }, ec = { 255, 0, 0, 255 }; Vec3f v = { 0, 1.5f, 0 }, ac = { 0, 0.05f, 0 };
                                                          EffectSsKiraKira_SpawnDispersed(gPlayState, &p2, &v, &ac, &pc, &ec, 110, 24); }
                            SparkBurst(gPlayState, hit.x, hit.y, hit.z, { 255, 120, 20, 255 }, 10, 3.0f);
                        } else if (weapon == ItemId::IceArrows) {
                            EffectSsIcePiece_SpawnBurst(gPlayState, &hit, 1.0f);
                        } else if (weapon == ItemId::LightArrows) {
                            EffectSsHitMark_SpawnFixedScale(gPlayState, 0, &hit);
                            SparkBurst(gPlayState, hit.x, hit.y, hit.z, { 255, 255, 170, 255 }, 14, 4.0f);
                        } else if (weapon == ItemId::DekuNuts) {
                            EffectSsHitMark_SpawnFixedScale(gPlayState, 0, &hit);
                        }
                    }
                    if (haveFrom && haveTo && weapon == royale::ItemId::HomingBombchus) {
                        for (int i = 0; i <= 14; i++) {
                            const float k = i / 14.0f;
                            SparkBurst(gPlayState, fx + (tx2 - fx) * k + (Rand_ZeroOne() - 0.5f) * 30.0f, fy + 25.0f + std::sin(k * 3.14159f) * 40.0f, fz + (tz2 - fz) * k + (Rand_ZeroOne() - 0.5f) * 30.0f, { 200, 100, 255, 255 }, 1, 0.8f);
                        }
                    }
                }
                // Whether the blow landed on a raised shield facing it: the server took most of it, and it rings off the shield.
                auto onShield = [&](bool shieldUp, int16_t rot, float tx, float tz) {
                    if (!shieldUp || e.other == royale::net::kNoPlayer16) return false;
                    float ax = 0, az = 0;
                    if (e.other == hud.selfId) { ax = me->actor.world.pos.x; az = me->actor.world.pos.z; }
                    else if (!KnownPosition(e.other, &ax, &az)) return false;
                    const s16 off = static_cast<s16>(static_cast<s16>(std::atan2(ax - tx, az - tz) * (32768.0f / 3.14159265f)) - rot);
                    return std::abs(static_cast<int>(off)) * (3.14159265f / 32768.0f) <= royale::kGuardHalfAngle;
                };
                if (e.id == hud.selfId && onShield((me->stateFlags1 & PLAYER_STATE1_SHIELDING) != 0, me->actor.shape.rot.y, me->actor.world.pos.x, me->actor.world.pos.z)) {
                    Audio_PlaySoundGeneral(NA_SE_IT_SHIELD_REFLECT_SW, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    {   // the spark the game shows when a blow rings off a shield
                        Vec3f at = { me->actor.world.pos.x + Math_SinS(me->actor.shape.rot.y) * 20.0f, me->actor.world.pos.y + 35.0f, me->actor.world.pos.z + Math_CosS(me->actor.shape.rot.y) * 20.0f };
                        EffectSsHitMark_SpawnFixedScale(gPlayState, EFFECT_HITMARK_METAL, &at);
                    }
                    gFloatingNumbers.push_back({ 0, 0, 0, e.amount, now, true });
                    if (e.other != royale::net::kNoPlayer16) { gIncomingHits.push_back({ e.other, now }); swing(e.other); }
                } else if (e.id != hud.selfId && gState.count(e.id) && gActorOf.count(e.id) &&
                           onShield(gState[e.id].anim == static_cast<uint8_t>(royale::Anim::Guard), gState[e.id].rot, gActorOf[e.id]->world.pos.x, gActorOf[e.id]->world.pos.z)) {
                    Actor* t = gActorOf[e.id];
                    if (e.other == hud.selfId) { gHitMarkerAt = now; gHitMarkerKill = e.health <= 0.001f; gFloatingNumbers.push_back({ t->world.pos.x, t->world.pos.y, t->world.pos.z, e.amount, now, false }); }
                    else swing(e.other);
                    if (std::hypot(t->world.pos.x - me->actor.world.pos.x, t->world.pos.z - me->actor.world.pos.z) < 1200.0f) soundAt(e.id, NA_SE_IT_SHIELD_REFLECT_SW);
                    Vec3f at = { t->world.pos.x + Math_SinS(t->shape.rot.y) * 20.0f, t->world.pos.y + 35.0f, t->world.pos.z + Math_CosS(t->shape.rot.y) * 20.0f };
                    EffectSsHitMark_SpawnFixedScale(gPlayState, EFFECT_HITMARK_METAL, &at);
                } else if (e.id == hud.selfId) {
                    // you were hit: flash, shake, a grunt, the sound of the blow, and an arrow towards the attacker
                    Actor_SetColorFilter(&me->actor, 0x4000, 0xFF, 0, 12);
                    Player_PlaySfx(&me->actor, NA_SE_VO_LI_DAMAGE_S);
                    Audio_PlaySoundGeneral(NA_SE_PL_BODY_HIT, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    gHurtAt = now; gHurtAmount = e.amount;
                    HitFeedback(e.id, e.other, e.amount, e.health <= 0.001f, true);
                    gFloatingNumbers.push_back({ 0, 0, 0, e.amount, now, true });
                    if (e.other != royale::net::kNoPlayer16) {
                        gIncomingHits.push_back({ e.other, now });
                        swing(e.other);
                        soundAt(e.other, NA_SE_IT_SWORD_SWING_HARD);
                    }
                } else {
                    float tx = 0, tz = 0;
                    const bool known = KnownPosition(e.id, &tx, &tz);
                    float ty = me->actor.world.pos.y;
                    auto target = gActorOf.find(e.id);
                    if (target != gActorOf.end()) { ty = target->second->world.pos.y; tx = target->second->world.pos.x; tz = target->second->world.pos.z; }
                    if (e.other == hud.selfId) {
                        // you hit someone: hit marker, their flash, the damage over their head, a clang
                        gHitMarkerAt = now;
                        gHitMarkerKill = e.health <= 0.001f;
                        if (known || target != gActorOf.end()) gFloatingNumbers.push_back({ tx, ty, tz, e.amount, now, false });
                        if (!DebugOn(kDbgHitFx) && target != gActorOf.end()) Actor_SetColorFilter(target->second, 0x4000, 0xFF, 0, 14);
                        HitFeedback(e.id, e.other, e.amount, e.health <= 0.001f, false);
                        if (target != gActorOf.end() && e.health > 0.001f) gFlinchFrames[e.id] = 8;   // they reel from it
                        Audio_PlaySoundGeneral(NA_SE_IT_SWORD_STRIKE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    } else {
                        // two others fighting nearby: you can see the slash and hear the blow
                        swing(e.other);
                        HitFeedback(e.id, e.other, e.amount, e.health <= 0.001f, false);
                        if (target != gActorOf.end()) {
                            if (!DebugOn(kDbgHitFx)) Actor_SetColorFilter(target->second, 0x4000, 0xFF, 0, 8);
                            if (e.health > 0.001f) gFlinchFrames[e.id] = 8;
                            if (std::hypot(tx - me->actor.world.pos.x, tz - me->actor.world.pos.z) < 1200.0f) soundAt(e.id, NA_SE_IT_SWORD_STRIKE);
                        }
                    }
                }
                break;
            }
            case royale::ClientEvent::Type::LootTaken:
                if (e.id == hud.selfId && gSession.Client() && e.index < gSession.Client()->Loot().size() && gSession.Client()->Loot()[e.index].chest) gMyStats.chests++;
                if (e.id != hud.selfId && gSession.Client() && e.index < gSession.Client()->Loot().size())
                    gLastFind[e.id] = { GidFor(static_cast<royale::ItemId>(gSession.Client()->Loot()[e.index].item)), ImGui::GetTime() };
                if (e.id == hud.selfId && gSession.Client() && e.index < gSession.Client()->Loot().size()) {
                    const auto& l = gSession.Client()->Loot()[e.index];
                    const royale::Rarity got = static_cast<royale::Rarity>(l.rarity);
                    const std::string label = LootLabel(l);
                    NotePickup(label, got, l.chest);
                    const royale::ItemId itemId = static_cast<royale::ItemId>(l.item);
                    float py = 0;
                    if (!FloorAt(l.x, l.z, &py)) py = GET_PLAYER(gPlayState)->actor.world.pos.y;
                    gPickupFx.push_back({ itemId, got, l.x, py, l.z, ImGui::GetTime() });
                    SparkBurst(gPlayState, l.x, py + 40.0f, l.z, RarityColor(got), 8 + 6 * static_cast<int>(got), 3.5f + 0.8f * static_cast<int>(got));
                    if (l.chest) {
                        StartAction(royale::Anim::ItemGet, 0.9f);   // everyone else sees you hold it up
                        Vec3f at = { l.x, py + 30.0f, l.z };
                        Audio_PlaySoundGeneral(NA_SE_EV_TBOX_OPEN, &at, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    }
                    Audio_PlaySoundGeneral(NA_SE_SY_GET_ITEM, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    if (itemId == royale::ItemId::HeartContainer) Audio_PlayFanfare(NA_BGM_HEART_GET);
                    else if (got >= royale::Rarity::Epic) Audio_PlayFanfare(NA_BGM_ITEM_GET);
                    else if (got == royale::Rarity::Rare) Audio_PlayFanfare(NA_BGM_SMALL_ITEM_GET);
                }
                break;
            case royale::ClientEvent::Type::BossDown: {
                const int kind = gBossKindSeen.count(e.id) ? gBossKindSeen[e.id] : 0;
                const std::string killer = e.other == hud.selfId ? std::string("You") : nameOf(e.other);
                AddFeed(killer + " defeated the " + royale::kBossDefs[kind].name, IM_COL32(255, 200, 120, 255));
                break;
            }
            case royale::ClientEvent::Type::SupplyDrop: {
                ShowBanner("Supply drop incoming", IM_COL32(255, 150, 60, 255), 3.0f);
                gSupplyMarks.push_back({ e.x, e.z, ImGui::GetTime() + 60.0 });
                Audio_PlaySoundGeneral(NA_SE_EV_FIRE_PILLAR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                break;
            }
            case royale::ClientEvent::Type::PropBroken: {
                if (e.id != hud.selfId && InGame() && gSession.Client() && e.index < gSession.Client()->Props().size()) {
                    // somebody else cut a bush or broke a rock near you: you see and hear it go
                    const auto& pr = gSession.Client()->Props()[e.index];
                    Player* me = GET_PLAYER(gPlayState);
                    float py = me->actor.world.pos.y;
                    if (std::hypot(pr.pos.x - me->actor.world.pos.x, pr.pos.z - me->actor.world.pos.z) < kPuppetHearing && FloorAt(pr.pos.x, pr.pos.z, &py)) {
                        const bool bush = pr.kind == royale::PropKind::Bush;
                        SparkBurst(gPlayState, pr.pos.x, py + 20.0f, pr.pos.z, bush ? Color_RGBA8{ 90, 220, 90, 255 } : Color_RGBA8{ 190, 190, 180, 255 }, 8, 3.0f);
                        Vec3f at = { pr.pos.x, py + 20.0f, pr.pos.z };
                        Audio_PlaySoundGeneral(bush ? NA_SE_EV_PLANT_BROKEN : NA_SE_EV_ROCK_BROKEN, &at, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    }
                }
                if (e.id != hud.selfId || !InGame() || !gSession.Client()) break;
                const auto& props = gSession.Client()->Props();
                float py = GET_PLAYER(gPlayState)->actor.world.pos.y, px = GET_PLAYER(gPlayState)->actor.world.pos.x, pz = GET_PLAYER(gPlayState)->actor.world.pos.z;
                if (e.index < props.size()) { px = props[e.index].pos.x; pz = props[e.index].pos.z; FloorAt(px, pz, &py); }
                if (e.item == 255) { SparkBurst(gPlayState, px, py + 25.0f, pz, { 150, 150, 150, 255 }, 4, 2.0f); break; } // nothing inside
                const royale::ItemId item = static_cast<royale::ItemId>(e.item);
                const bool money = item == royale::ItemId::Rupees;
                const std::string what = std::string("+") + std::to_string(e.count) + " " + ItemName(item);
                ShowGain(what, money ? (e.count >= 20 ? IM_COL32(255, 90, 90, 255) : e.count >= 5 ? IM_COL32(110, 170, 255, 255) : IM_COL32(110, 240, 130, 255)) : IM_COL32(255, 220, 120, 255));
                SparkBurst(gPlayState, px, py + 25.0f, pz, money ? Color_RGBA8{ 120, 255, 140, 255 } : Color_RGBA8{ 255, 220, 120, 255 }, 7, 3.0f);
                Audio_PlaySoundGeneral(money ? NA_SE_SY_GET_RUPY : NA_SE_SY_GET_ITEM, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                break;
            }
            case royale::ClientEvent::Type::BossSpawned: {
                const char* name = royale::kBossDefs[std::min<int>(e.item, royale::kBossKindCount - 1)].name;
                ShowBanner(std::string(name) + " has arrived!", IM_COL32(255, 120, 80, 255), 3.4f);
                Audio_PlaySoundGeneral(BossArrivalSound(e.item), &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                break;
            }
            case royale::ClientEvent::Type::FartCloud:   // somebody else's Lilo: mine was drawn the moment she did it
                if (e.id != hud.selfId && InGame()) {
                    float y = 0;
                    Player* me = GET_PLAYER(gPlayState);
                    AddToxicCloud(e.x, FloorAt(e.x, e.z, &y) ? y : me->actor.world.pos.y, e.z, false);
                }
                break;
            case royale::ClientEvent::Type::Strike:
                AddStrikeFx(e.x, e.z, e.amount, e.health, e.id == royale::net::kNoPlayer16 ? static_cast<uint8_t>(royale::StrikeStyle::Bolt) : e.item, e.id);
                break;
            case royale::ClientEvent::Type::AllyChanged: {
                const char* name = royale::kAllyDefs[std::min<int>(e.index, royale::kAllyCount - 1)].name;
                if (e.item == 0) {
                    if (e.id == hud.selfId) ShowBanner(std::string("The ") + name + " joins you!", IM_COL32(130, 255, 150, 255), 2.4f);
                } else if (e.item != 1) {
                    auto it = gAllies.find(static_cast<uint8_t>(e.index));
                    if (it != gAllies.end() && it->second.actor != nullptr && gPlayState != nullptr)
                        SparkBurst(gPlayState, it->second.x, it->second.actor->world.pos.y + 60.0f, it->second.z, { 220, 220, 255, 255 }, 24, 5.0f);
                }
                break;
            }
            case royale::ClientEvent::Type::AllyAction:
                AllyActionFx(e, hud);
                break;
            case royale::ClientEvent::Type::WeatherChanged: {
                const auto sky = static_cast<royale::Sky>(e.item);
                if (!gSeasonAnnounced) {
                    gSeasonAnnounced = true;
                    const auto season = static_cast<royale::Season>(e.id & 3);
                    ShowBanner(std::string(royale::SeasonName(season)) + ": " + royale::SeasonBlurb(season), IM_COL32(255, 235, 170, 255), 3.4f);
                } else if (sky != royale::Sky::Clear) {
                    ShowBanner(std::string(royale::SkyName(sky)) + " rolls in", IM_COL32(190, 210, 255, 255), 2.8f);
                } else {
                    ShowBanner("The sky clears", IM_COL32(255, 235, 170, 255), 2.4f);
                }
                break;
            }
            case royale::ClientEvent::Type::MapChanged:
                gBrokenProps.clear();   // a new match (or a rematch): all the scenery is back
                gSeasonAnnounced = false;
                gPickupLog.clear();
                break;
            case royale::ClientEvent::Type::AbilityUsed:
                if (e.item < royale::kItemCount) AbilityFx(e.id, static_cast<royale::ItemId>(e.item), e.id == hud.selfId, e.x, e.z);
                if (e.id == hud.selfId && e.item == static_cast<uint8_t>(royale::ItemId::ShockwaveGrenade) && InField())
                    LaunchSelf(GET_PLAYER(gPlayState), static_cast<royale::Rarity>(hud.inv.ability.rarity));
                if (e.item == royale::net::kRevivedItem) {
                    if (e.id == hud.selfId) ShowBanner("A Fairy saved you!", IM_COL32(255, 170, 215, 255), 2.4f);
                }
                break;
            case royale::ClientEvent::Type::Eliminated: {
                const bool me = e.id == hud.selfId, mine = e.other == hud.selfId;
                if (mine && !me) {   // for the death and end screens (and their medals)
                    gMyStats.kills++;
                    if (!gMyStats.anyElim) gMyStats.firstStrike = true;
                    const double now = ImGui::GetTime();
                    gMyStats.streak = now - gMyStats.lastKillAt < kStreakSec ? gMyStats.streak + 1 : 1;
                    gMyStats.bestStreak = std::max(gMyStats.bestStreak, gMyStats.streak);
                    gMyStats.lastKillAt = now;
                }
                if (!royale::IsBossId(e.id)) gMyStats.anyElim = true;
                // (Link's cry as he goes down comes from his body, see Corpse_Update.)
                // The server stops sending eliminated players, so their puppet just vanishes: leave their body here instead, thrown away
                // from whoever got them (or backwards, for the storm). Your own body is made in OnPlayerUpdate.
                // (A player who was only just seen, or just went out of range, is found in gLastSeen: any kind of death leaves a body.)
                const royale::PuppetState* known = gState.count(e.id) ? &gState[e.id] : gLastSeen.count(e.id) ? &gLastSeen[e.id] : nullptr;
                if (!me && InField() && !royale::IsBossId(e.id) && known != nullptr && known->scene == gPlayState->sceneNum) {
                    royale::PuppetState body = *known;
                    auto actor = gActorOf.find(e.id);
                    if (actor != gActorOf.end() && actor->second != nullptr) {   // where it is drawn right now, not the last network sample
                        body.x = actor->second->world.pos.x; body.y = actor->second->world.pos.y; body.z = actor->second->world.pos.z;
                    }
                    const float a = body.rot * (3.14159265f / 32768.0f);
                    float px = -std::sin(a), pz = -std::cos(a);
                    const Player* self = GET_PLAYER(gPlayState);
                    float kx = 0, kz = 0;
                    bool haveKiller = false;
                    if (mine) { kx = self->actor.world.pos.x; kz = self->actor.world.pos.z; haveKiller = true; }
                    else if (gState.count(e.other)) { kx = gState[e.other].x; kz = gState[e.other].z; haveKiller = true; }
                    if (haveKiller && std::hypot(body.x - kx, body.z - kz) > 1.0f) { px = body.x - kx; pz = body.z - kz; }
                    SpawnCorpse(body, px, pz);
                }
                const std::string victim = me ? std::string("You") : nameOf(e.id);
                std::string line;
                if (e.other == royale::net::kNoPlayer16) line = victim + (me ? " were" : " was") + " caught by the storm";
                else if (royale::IsBossId(e.other)) line = victim + (me ? " were" : " was") + " defeated by the " + royale::kBossDefs[gBossKindSeen.count(e.other) ? gBossKindSeen[e.other] : 0].name;
                else line = (mine ? std::string("You") : nameOf(e.other)) + " eliminated " + victim;
                AddFeed(line, me ? IM_COL32(255, 110, 110, 255) : mine ? IM_COL32(255, 220, 90, 255) : IM_COL32(230, 230, 235, 255));
                if (me) {   // the death screen takes it from here (DrawDeathScreen)
                    gDeath.killer = e.other;
                    if (e.other == royale::net::kNoPlayer16) gDeath.by = "by the storm";
                    else if (royale::IsBossId(e.other)) gDeath.by = "by the " + std::string(royale::kBossDefs[gBossKindSeen.count(e.other) ? gBossKindSeen[e.other] : 0].name);
                    else if (e.other == hud.selfId) gDeath.by = "by your own attack";
                    else gDeath.by = "by " + nameOf(e.other);
                    StartDeath(hud);
                }
                break;
            }
            case royale::ClientEvent::Type::StateChanged:
                break;   // the big centre text already says what happened (see DrawOverlay)
            default:
                break;
        }
    }
}

// The host's Start does three things in order: get to Hyrule Field, measure the real playable area and rebuild the world on it
// (loot, spawns and storm on ground that exists), then begin.
void DriveStart(const royale::HudState& hud) {
    if (gSoloStartWanted) {   // the solo test needs no one else: press Start for the host as soon as they are in the lobby
        if (!gSession.Joined() || !gSession.SoloTest()) gSoloStartWanted = false;
        else if (hud.isHost && hud.state == royale::MatchState::Lobby && hud.haveSelf && InGame() && !gPendingStart) { gPendingStart = true; gSoloStartWanted = false; }
    }
    if (!gPendingStart) return;
    if (!gSession.Joined() || !hud.isHost || hud.state != royale::MatchState::Lobby) { gPendingStart = false; return; }
    if (!InGame()) return;
    if (!InField()) { Trace("start: going to the field"); WantsWaitingRoom = false; GoToField(); gInFieldFrames = 0; return; }
    if (gPlayState->transitionTrigger != TRANS_TRIGGER_OFF || ++gInFieldFrames < royale::kTickHz) return; // let the scene settle
    Trace("start: measuring the field");
    royale::Circle measured;
    if (MeasureField(&measured)) {
        Trace("start: configuring the map");
        gSession.ConfigureMap(measured, WalkableAt, [](royale::Vec2 p, float* y) { return RawFloorAt(p.x, p.z, y); });   // the bots learn the ledges and cliffs
    }
    Trace("start: starting the match");
    gSession.StartMatch();
    Trace("start: match started");
    gPendingStart = false;
}

