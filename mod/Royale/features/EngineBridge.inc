// ---- the Fortnite map, the parts the game calls (patches/0013) ------------------------------------------------------------------

namespace {
// The island's collision in the game's own formats. Built once, then it stays: the game keeps pointers into it for as long as the scene is loaded.
std::vector<Vec3s> gFortniteVtx;
std::vector<CollisionPoly> gFortnitePoly;
SurfaceType gFortniteSurface[16];
CamData gFortniteCam[1];
WaterBox gFortniteWater[1];
CollisionHeader gFortniteHeader;
bool gFortniteBuilt = false;

bool gTerrainMapId8() { return royale::fortnite::gTerrainMapId == royale::kKingdomMapIndex; }

CollisionHeader* FortniteHeader() {
    namespace fn = royale::fortnite;
    if (gFortniteBuilt) return &gFortniteHeader;
    const fn::Mesh mesh = fn::BuildCollision();
    gFortniteVtx.resize(mesh.verts.size());
    for (size_t i = 0; i < mesh.verts.size(); i++) gFortniteVtx[i] = { mesh.verts[i].x, mesh.verts[i].y, mesh.verts[i].z };
    gFortnitePoly.resize(mesh.polys.size());
    for (size_t i = 0; i < mesh.polys.size(); i++) {
        const fn::Poly& p = mesh.polys[i];
        CollisionPoly& c = gFortnitePoly[i];
        c = {};
        c.type = p.surface;
        c.flags_vIA = p.a;
        c.flags_vIB = p.b;
        c.vIC = p.c;
        c.normal = { p.nx, p.ny, p.nz };
        c.dist = p.dist;
    }
    gFortniteSurface[0] = {};                       // plain ground: no exit, no damage, the first camera entry
    if (gTerrainMapId8()) {   // Hyrule Kingdom's table: footsteps, climbable ivy and cliffs (wall type 4), hook-shot wood
        constexpr int n = sizeof(royale::kingdom::kSurfaces) / sizeof(royale::kingdom::kSurfaces[0]);
        static_assert(n <= 16, "surface table");
        for (int i = 0; i < n; i++) {
            const auto& sf = royale::kingdom::kSurfaces[i];
            gFortniteSurface[i] = {};
            gFortniteSurface[i].data[0] = sf.climb ? (4u << 21) : 0u;
            gFortniteSurface[i].data[1] = static_cast<u32>(sf.sfx) | (sf.hook ? (1u << 17) : 0u);
        }
    }
    gFortniteCam[0] = {};
    gFortniteCam[0].cameraSType = CAM_SET_NORMAL0;
    gFortniteCam[0].numCameras = 0;
    gFortniteCam[0].camPosData = nullptr;
    gFortniteWater[0] = {};                         // one box over the whole island: every room (0x3F), the scene's first light setting
    gFortniteWater[0].xMin = static_cast<s16>(std::lround(-fn::kHalfX - 2.0f));
    gFortniteWater[0].zMin = static_cast<s16>(std::lround(-fn::kHalfZ - 2.0f));
    gFortniteWater[0].xLength = static_cast<s16>(std::lround(2.0f * fn::kHalfX + 4.0f));
    gFortniteWater[0].zLength = static_cast<s16>(std::lround(2.0f * fn::kHalfZ + 4.0f));
    gFortniteWater[0].ySurface = static_cast<s16>(fn::kWaterY);
    gFortniteWater[0].properties = 0x3Fu << 13;
    gFortniteHeader = {};
    gFortniteHeader.minBounds = { mesh.lo.x, mesh.lo.y, mesh.lo.z };
    gFortniteHeader.maxBounds = { mesh.hi.x, mesh.hi.y, mesh.hi.z };
    gFortniteHeader.numVertices = static_cast<u16>(gFortniteVtx.size());
    gFortniteHeader.vtxList = gFortniteVtx.data();
    gFortniteHeader.numPolygons = static_cast<u16>(gFortnitePoly.size());
    gFortniteHeader.polyList = gFortnitePoly.data();
    gFortniteHeader.surfaceTypeList = gFortniteSurface;
    gFortniteHeader.cameraDataList = gFortniteCam;
    gFortniteHeader.cameraDataListLen = 1;
    gFortniteHeader.numWaterBoxes = 1;
    gFortniteHeader.waterBoxes = gFortniteWater;
    gFortniteBuilt = true;
    return &gFortniteHeader;
}
} // namespace

// Called as every scene loads its collision: the island's, if this is Hyrule Field and the lobby's map is the Fortnite map; otherwise null (the scene's own).
extern "C" CollisionHeader* Royale_CustomCollision(PlayState* play) {
    gFortniteScene = false;
    if (play == nullptr || play->sceneNum != SCENE_HYRULE_FIELD || !gSession.Joined() || !royale::IsIslandMap(gMapId)) return nullptr;
    gFortniteScene = true;
    const bool sandbox = royale::IsTestMap(gMapId);
    if (gMapId != royale::fortnite::gTerrainMapId) {   // rebuild both collision and drawing on every custom-map change
        royale::fortnite::UseTerrainForMap(gMapId);
        gFortniteSandbox = sandbox;
        gFortniteBuilt = false;
        gFortniteGpu.built = false;
    }
    {   // the match's climbing blocks go into the ground itself
        const std::vector<royale::fortnite::Block> want = WantedBlocks();
        if (!(want == royale::fortnite::gBlocks)) { royale::fortnite::gBlocks = want; gFortniteBuilt = false; }
    }
    return FortniteHeader();
}
extern "C" s32 Royale_IsCustomCollision(CollisionHeader* header) { return gFortniteBuilt && header == &gFortniteHeader; }
extern "C" s32 Royale_HideRooms(void) { return gFortniteScene && gPlayState != nullptr && gPlayState->sceneNum == SCENE_HYRULE_FIELD; }

namespace {
#include "RoyaleWarTable.h"
}

extern "C" int Royale_NativeMenuIsOpen(void) { return WarMenuOpen() || EndScreenUp(gSession.Hud()); }
extern "C" int Royale_NativeMenuHideHud(void) { return WarMenuOpen() || gSession.Joined(); }
extern "C" int Royale_NativeMenuAllowSfx(u16 id) {
    if (!gMenuAudioMuted.load(std::memory_order_relaxed)) return true;
    return id == NA_SE_SY_FSEL_CURSOR || id == NA_SE_SY_FSEL_DECIDE_L ||
           id == NA_SE_SY_FSEL_CLOSE || id == NA_SE_SY_FSEL_ERROR || id == NA_SE_SY_CURSOR || id == NA_SE_SY_DECIDE;
}
extern "C" void Royale_NativeMenuDraw(GameState* state) {
    Gfx_SetupFrame(state->gfxCtx, 8, 16, 36);
    WarDraw(state);
}

extern "C" void Royale_NativeMenuBoot(GameState* state) {
    Gfx_SetupFrame(state->gfxCtx, 8, 16, 36);
    const bool retry = state->input[0].press.button & BTN_A;
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    if (gWar.bootError.empty() || retry || keys[SDL_SCANCODE_RETURN]) {
        if (WarPrepareSave()) {
            auto* choose = reinterpret_cast<FileChooseContext*>(state);
            choose->buttonIndex = 0;
            FileChoose_LoadGame(state);
            gSaveContext.entranceIndex = ENTR_TEMPLE_OF_TIME_ENTRANCE;
            gSaveContext.savedSceneNum = SCENE_TEMPLE_OF_TIME;
            gSaveContext.cutsceneIndex = 0;
            // A new adult adventure otherwise starts Sheik's story cutscene in our waiting room.
            Flags_SetEventChkInf(EVENTCHKINF_SHEIK_SPAWNED_AT_MASTER_SWORD_PEDESTAL);
            gSaveContext.showTitleCard = false;
            gSaveContext.ship.quest.id = QUEST_BATTLEROYALE;
            gWar.booted = true;
            WarOpen();
        }
    }
    WarDraw(state);
}
