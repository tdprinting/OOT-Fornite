// ---- the Fortnite map, the parts the game calls (patches/0013) ------------------------------------------------------------------

namespace {
// The island's collision in the game's own formats. Built once, then it stays: the game keeps pointers into it for as long as the scene is loaded.
std::vector<Vec3s> gFortniteVtx;
std::vector<CollisionPoly> gFortnitePoly;
SurfaceType gFortniteSurface[16];
CamData gFortniteCam[1];
constexpr size_t kRiftlandsPoolCount = sizeof(royale::riftlands::kPools)/sizeof(royale::riftlands::kPools[0]);
constexpr size_t kMoonfallPoolCount = sizeof(royale::moonfall::kPools)/sizeof(royale::moonfall::kPools[0]);
constexpr size_t kSkyloftPoolCount = sizeof(royale::skyloft::kPools)/sizeof(royale::skyloft::kPools[0]);
constexpr size_t kGreatHyruleKingdomPoolCount = sizeof(royale::great_hyrule_kingdom::kPools)/sizeof(royale::great_hyrule_kingdom::kPools[0]);
constexpr size_t kHyruleThirdStormPoolCount = royale::hyrule_third_storm::kPoolCount;
constexpr size_t kMaxPoolCount = std::max({kRiftlandsPoolCount, kMoonfallPoolCount, kSkyloftPoolCount, kGreatHyruleKingdomPoolCount, kHyruleThirdStormPoolCount});   // the most raised pools any map has
WaterBox gFortniteWater[1+kMaxPoolCount];
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
    if (fn::gTerrainMapId == royale::kRiftlandsMapIndex) {   // collision loading must not depend on the previous live PlayState
        constexpr int n = sizeof(royale::riftlands::kSurfaces) / sizeof(royale::riftlands::kSurfaces[0]);
        static_assert(n <= 16, "surface table");
        for (int i = 0; i < n; i++) {
            const auto& sf = royale::riftlands::kSurfaces[i];
            gFortniteSurface[i] = {};
            gFortniteSurface[i].data[0] = sf.climb ? (4u << 21) : 0u;
            gFortniteSurface[i].data[1] = static_cast<u32>(sf.sfx) | (sf.hook ? (1u << 17) : 0u);
        }
    }
    if (fn::gTerrainMapId == royale::kMoonfallMapIndex) {   // collision loading must not depend on the previous live PlayState
        constexpr int n = sizeof(royale::moonfall::kSurfaces) / sizeof(royale::moonfall::kSurfaces[0]);
        static_assert(n <= 16, "surface table");
        for (int i = 0; i < n; i++) {
            const auto& sf = royale::moonfall::kSurfaces[i];
            gFortniteSurface[i] = {};
            gFortniteSurface[i].data[0] = sf.climb ? (4u << 21) : 0u;
            gFortniteSurface[i].data[1] = static_cast<u32>(sf.sfx) | (sf.hook ? (1u << 17) : 0u);
        }
    }
    if (fn::gTerrainMapId == royale::kSkyloftMapIndex) {
        constexpr int n = sizeof(royale::skyloft::kSurfaces) / sizeof(royale::skyloft::kSurfaces[0]);
        static_assert(n <= 16, "surface table");
        for (int i = 0; i < n; i++) {
            const auto& sf = royale::skyloft::kSurfaces[i];
            gFortniteSurface[i] = {};
            gFortniteSurface[i].data[0] = sf.climb ? (4u << 21) : 0u;
            gFortniteSurface[i].data[1] = static_cast<u32>(sf.sfx) | (sf.hook ? (1u << 17) : 0u);
        }
    }
    if (fn::gTerrainMapId == royale::kGreatHyruleKingdomMapIndex) {
        constexpr int n = sizeof(royale::great_hyrule_kingdom::kSurfaces) / sizeof(royale::great_hyrule_kingdom::kSurfaces[0]);
        static_assert(n <= 16, "surface table");
        for (int i = 0; i < n; i++) {
            const auto& sf = royale::great_hyrule_kingdom::kSurfaces[i];
            gFortniteSurface[i] = {};
            gFortniteSurface[i].data[0] = sf.climb ? (4u << 21) : 0u;
            gFortniteSurface[i].data[1] = static_cast<u32>(sf.sfx) | (sf.hook ? (1u << 17) : 0u);
        }
    }
    if (fn::gTerrainMapId == royale::kHyruleThirdStormMapIndex) {
        constexpr int n = sizeof(royale::hyrule_third_storm::kSurfaces) / sizeof(royale::hyrule_third_storm::kSurfaces[0]);
        static_assert(n <= 16, "surface table");
        for (int i = 0; i < n; i++) {
            const auto& sf = royale::hyrule_third_storm::kSurfaces[i];
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
    gFortniteHeader.numVertices = static_cast<u32>(gFortniteVtx.size());
    gFortniteHeader.vtxList = gFortniteVtx.data();
    gFortniteHeader.numPolygons = static_cast<u32>(gFortnitePoly.size());
    gFortniteHeader.polyList = gFortnitePoly.data();
    gFortniteHeader.surfaceTypeList = gFortniteSurface;
    gFortniteHeader.cameraDataList = gFortniteCam;
    gFortniteHeader.cameraDataListLen = 1;
    gFortniteHeader.numWaterBoxes = 1;
    if(fn::gTerrainMapId == royale::kRiftlandsMapIndex) {
        const WaterBox ocean=gFortniteWater[0];
        int count=0;
        for(const auto& pool:royale::riftlands::kPools) {
            auto& b=gFortniteWater[count++]; b={};
            b.xMin=static_cast<s16>(pool.x0); b.zMin=static_cast<s16>(pool.z0);
            b.xLength=static_cast<s16>(pool.x1-pool.x0); b.zLength=static_cast<s16>(pool.z1-pool.z0);
            b.ySurface=static_cast<s16>(pool.y); b.properties=0x3Fu<<13;
        }
        gFortniteWater[count++]=ocean;gFortniteHeader.numWaterBoxes=count;
    }
    if(fn::gTerrainMapId == royale::kMoonfallMapIndex) {
        const WaterBox ocean=gFortniteWater[0];
        int count=0;
        for(const auto& pool:royale::moonfall::kPools) {
            auto& b=gFortniteWater[count++]; b={};
            b.xMin=static_cast<s16>(pool.x0); b.zMin=static_cast<s16>(pool.z0);
            b.xLength=static_cast<s16>(pool.x1-pool.x0); b.zLength=static_cast<s16>(pool.z1-pool.z0);
            b.ySurface=static_cast<s16>(pool.y); b.properties=0x3Fu<<13;
        }
        gFortniteWater[count++]=ocean;gFortniteHeader.numWaterBoxes=count;
    }
    if(fn::gTerrainMapId == royale::kSkyloftMapIndex) {
        const WaterBox ocean=gFortniteWater[0];
        int count=0;
        for(const auto& pool:royale::skyloft::kPools) {
            auto& b=gFortniteWater[count++]; b={};
            b.xMin=static_cast<s16>(pool.x0); b.zMin=static_cast<s16>(pool.z0);
            b.xLength=static_cast<s16>(pool.x1-pool.x0); b.zLength=static_cast<s16>(pool.z1-pool.z0);
            b.ySurface=static_cast<s16>(pool.y); b.properties=0x3Fu<<13;
        }
        gFortniteWater[count++]=ocean;gFortniteHeader.numWaterBoxes=count;
    }
    if(fn::gTerrainMapId == royale::kGreatHyruleKingdomMapIndex) {   // two lakes and the river's stepped pools
        const WaterBox ocean=gFortniteWater[0];
        int count=0;
        for(const auto& pool:royale::great_hyrule_kingdom::kPools) {
            auto& b=gFortniteWater[count++]; b={};
            b.xMin=static_cast<s16>(pool.x0); b.zMin=static_cast<s16>(pool.z0);
            b.xLength=static_cast<s16>(pool.x1-pool.x0); b.zLength=static_cast<s16>(pool.z1-pool.z0);
            b.ySurface=static_cast<s16>(pool.y); b.properties=0x3Fu<<13;
        }
        gFortniteWater[count++]=ocean;gFortniteHeader.numWaterBoxes=count;
    }
    if(fn::gTerrainMapId == royale::kHyruleThirdStormMapIndex) {   // no sea: only its lakes, rivers and pools. (A box over the whole map
        int count=0;                                                // would not fit: a water box's size is s16 and the map is 64800 across.)
        for(const auto& pool:royale::hyrule_third_storm::kPools) {
            auto& b=gFortniteWater[count++]; b={};
            b.xMin=static_cast<s16>(pool.x0); b.zMin=static_cast<s16>(pool.z0);
            b.xLength=static_cast<s16>(std::min(32767.0f,pool.x1-pool.x0)); b.zLength=static_cast<s16>(std::min(32767.0f,pool.z1-pool.z0));
            b.ySurface=static_cast<s16>(pool.y); b.properties=0x3Fu<<13;
        }
        gFortniteHeader.numWaterBoxes=count;
    }
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
    const bool rebuild = !gFortniteBuilt;
    CollisionHeader* header = FortniteHeader();
    Crumb("collision: map %d (%s) island collision %s, %d vertices %d polygons %d water boxes", gMapId, royale::MapOf(gMapId).name,
          rebuild ? "built" : "reused", static_cast<int>(header->numVertices), static_cast<int>(header->numPolygons), static_cast<int>(header->numWaterBoxes));
    return header;
}
extern "C" s32 Royale_IsCustomCollision(CollisionHeader* header) { return gFortniteBuilt && header == &gFortniteHeader; }
extern "C" s32 Royale_HideRooms(void) { return gFortniteScene && gPlayState != nullptr && gPlayState->sceneNum == SCENE_HYRULE_FIELD; }

// The camera's far clip (patches/0039, Play_Draw). A map wider than the scene's fog distance sets gMapFarClip (game units, at most
// engine::kFarClipMax); 0, as on every map so far, keeps the scene's own value.
float gMapFarClip = 0.0f;
extern "C" f32 Royale_FarClip(s16 sceneFar) {
    // Hyrule Before the Third Storm is 64800 units across: its far meshes are drawn out to kFarClip.
    const float wanted = gFortniteScene && royale::fortnite::gTerrainMapId == royale::kHyruleThirdStormMapIndex ? royale::hyrule_third_storm::kFarClip : gMapFarClip;
    return std::max(static_cast<float>(sceneFar), std::min(wanted, royale::engine::kFarClipMax));
}

namespace {
#include "RoyaleWarTable.h"
}

// The recap uses the gameplay overlay, not WarDraw. Isolating it would clear the
// world and call a painter that deliberately draws nothing when the menu is shut.
extern "C" int Royale_NativeMenuIsOpen(void) { return WarMenuOpen(); }
extern "C" int Royale_NativeMenuHideHud(void) { return WarMenuOpen() || gSession.Joined(); }
extern "C" int Royale_NativeMenuAllowSfx(u16 id) {
    if (gMuteChestSfx && (id == NA_SE_EV_TBOX_UNLOCK || id == NA_SE_EV_TBOX_OPEN || id == NA_SE_EV_TRE_BOX_APPEAR)) return false;
    if (!gMenuAudioMuted.load(std::memory_order_relaxed)) return true;
    return id == NA_SE_SY_FSEL_CURSOR || id == NA_SE_SY_FSEL_DECIDE_L ||
           id == NA_SE_SY_FSEL_CLOSE || id == NA_SE_SY_FSEL_ERROR || id == NA_SE_SY_CURSOR || id == NA_SE_SY_DECIDE;
}
extern "C" void Royale_NativeMenuDraw(GameState* state) {
    Gfx_SetupFrame(state->gfxCtx, 8, 16, 36);
    WarDraw(state);
}

extern "C" void Royale_NativeMenuBoot(GameState* state) {
    gBootInput = &state->input[0]; gBootInputAt = SDL_GetTicks();
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
