#pragma once
#include "hyrule_third_storm_ground.h"
// Hyrule Before the Third Storm's drawn map, decoded once from the generated base64 tables (hyrule_third_storm_model.h) on first use.
// The tables (54 MB of text) are compiled only in mod/Royale/RoyaleThirdStormTables.cpp, a file of their own: inside RoyaleMod.cpp the
// compiler ran out of memory. Include this header for the types; link that file for Draw() and LoadCollision().
// Drawn by DrawHyruleThirdStorm in mod/Royale/features/Terrain.inc: one batch is one texture in one 4096-unit square at one level of detail;
// its vertices go to the renderer in windows (one vertex load each), the triangles' corners numbered within their window.
namespace royale { namespace hyrule_third_storm {
struct DrawVertex { int16_t x, y, z, s, t; uint8_t r, g, b; };
struct Window { uint8_t vertices, triangles; };
struct Batch { uint32_t firstVertex, firstWindow, firstIndex; uint16_t windows, texture; int16_t x, y, z; uint16_t radius, cell; uint8_t lod; };
struct DrawTables {
    std::vector<DrawVertex> verts;
    std::vector<Window> windows;
    std::vector<uint8_t> index;
    std::vector<Batch> batches;
    std::vector<uint16_t> textures;   // kTextureCount x 256 x 256 RGBA16
    const uint16_t* Texture(int i) const { return &textures[static_cast<size_t>(i) * kTextureSize * kTextureSize]; }
    bool ok = false;
};
// Decoded on first use.
const DrawTables& Draw();
// The collision (hyrule_third_storm_collision.h), for fortnite::BuildCollision: RoyaleThirdStormTables.cpp sets gLoadCollision to this
// when the program starts.
void LoadCollision(std::vector<Vertex>& verts, std::vector<Triangle>& tris);
}}  // namespace royale::hyrule_third_storm
