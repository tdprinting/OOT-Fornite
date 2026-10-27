#pragma once
#include "hyrule_third_storm_ground.h"
#include "hyrule_third_storm_model.h"
// Hyrule Before the Third Storm's drawn map, decoded once from the generated base64 tables (hyrule_third_storm_model.h) on first use.
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
inline const DrawTables& Draw() {
    static const DrawTables t = [] {
        DrawTables d;
        auto u16 = [](const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); };
        auto u32 = [](const uint8_t* p) { return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24)); };
        const auto v = DecodeBase64(kDrawVerticesB64, kDrawVerticesB64Bytes);
        d.verts.resize(v.size() / 13);
        for (size_t i = 0; i < d.verts.size(); ++i) {
            const uint8_t* p = &v[i * 13];
            d.verts[i] = {static_cast<int16_t>(u16(p)), static_cast<int16_t>(u16(p + 2)), static_cast<int16_t>(u16(p + 4)), static_cast<int16_t>(u16(p + 6)),
                          static_cast<int16_t>(u16(p + 8)), p[10], p[11], p[12]};
        }
        const auto w = DecodeBase64(kWindowsB64, kWindowsB64Bytes);
        d.windows.resize(w.size() / 2);
        for (size_t i = 0; i < d.windows.size(); ++i) d.windows[i] = {w[i * 2], w[i * 2 + 1]};
        d.index = DecodeBase64(kIndicesB64, kIndicesB64Bytes);
        const auto b = DecodeBase64(kBatchesB64, kBatchesB64Bytes);
        d.batches.resize(b.size() / 27);
        for (size_t i = 0; i < d.batches.size(); ++i) {
            const uint8_t* p = &b[i * 27];
            d.batches[i] = {u32(p), u32(p + 4), u32(p + 8), u16(p + 12), u16(p + 14), static_cast<int16_t>(u16(p + 16)), static_cast<int16_t>(u16(p + 18)),
                            static_cast<int16_t>(u16(p + 20)), u16(p + 22), u16(p + 24), p[26]};
        }
        const auto x = DecodeBase64(kTexturesB64, kTexturesB64Bytes);
        d.textures.resize(x.size() / 2);
        for (size_t i = 0; i < d.textures.size(); ++i) d.textures[i] = u16(&x[i * 2]);
        d.ok = v.size() == kDrawVerticesB64Bytes && b.size() == kBatchesB64Bytes && d.textures.size() == static_cast<size_t>(kTextureCount) * kTextureSize * kTextureSize;
        if (!d.ok) d.batches.clear();   // draw nothing rather than read past a short table
        return d;
    }();
    return t;
}
}}  // namespace royale::hyrule_third_storm
