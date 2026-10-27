// Hyrule Before the Third Storm's generated tables (map 15) and their decoding, in a file of their own so the compiler handles the
// 54 MB of base64 apart from RoyaleMod.cpp (together they ran out of memory). Declared in shared/hyrule_third_storm_draw.h; the server's
// map test compiles this file too.
#include "hyrule_third_storm_draw.h"
#include "hyrule_third_storm_collision.h"
#include "hyrule_third_storm_model.h"

namespace royale { namespace hyrule_third_storm {
const DrawTables& Draw() {
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
void LoadCollision(std::vector<Vertex>& verts, std::vector<Triangle>& tris) {
    auto u16 = [](const uint8_t* p) { return static_cast<int16_t>(p[0] | (p[1] << 8)); };
    auto u32 = [](const uint8_t* p) { return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24)); };
    const auto v = DecodeBase64(kCollisionVerticesB64, kCollisionVerticesB64Bytes);
    verts.resize(v.size() / 6);
    for (size_t i = 0; i < verts.size(); ++i) verts[i] = {u16(&v[i * 6]), u16(&v[i * 6 + 2]), u16(&v[i * 6 + 4])};
    const auto c = DecodeBase64(kCollisionTrianglesB64, kCollisionTrianglesB64Bytes);
    tris.resize(c.size() / 23);
    for (size_t i = 0; i < tris.size(); ++i) {
        const uint8_t* p = &c[i * 23];
        tris[i] = {u32(p), u32(p + 4), u32(p + 8), u16(p + 12), u16(p + 14), u16(p + 16), static_cast<int32_t>(u32(p + 18)), p[22]};
        if (tris[i].a >= verts.size() || tris[i].b >= verts.size() || tris[i].c >= verts.size()) { tris.clear(); break; }   // a damaged table: none
    }
}
namespace { const bool kCollisionHooked = (gLoadCollision = &LoadCollision, true); }
}}  // namespace royale::hyrule_third_storm
