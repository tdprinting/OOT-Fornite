#pragma once
#include "fortnite_cover_data.h"
#include "fortnite_map_data.h"
#include "sandbox_terrain.h"
#include "boss_arena.h"
#include "convergence_data.h"
#include "kingdom_data.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace royale {
namespace fortnite {

// The "Fortnite Map": an island made from a heightmap and a texture (assets/fortnite, built into fortnite_map_data.h by scripts/make_fortnite_map.py).
// It is played inside Hyrule Field's scene: the mod swaps the scene's collision for the mesh below and draws the island instead of the field
// (RoyaleMod.cpp, "the Fortnite map"). Nothing here knows about the game's types, so the unit tests can check it.
//
// The ground is a grid of kCells x kCells squares, two triangles each, split from the low corner to the far corner. The same triangles are the
// collision and the drawn ground, so what you stand on is what you see. (The drawn mesh is finer, to carry the texture's colours, but every
// point of it lies on those triangles.) Water is a single level: lakes, rivers and the sea are the ground below it.
using namespace fortnite_data;

inline constexpr int kMapId = 5;                  // the place's number in kMaps (shared/map.h)
inline constexpr int kVerts = kCells + 1;         // collision vertices per side
inline constexpr int kFine = kCells * kSub;       // colour squares per side
inline constexpr int kBlockVerts = (kSub + 1) * (kSub + 1);
inline constexpr float kCellX = 2.0f * kHalfX / kCells;
inline constexpr float kCellZ = 2.0f * kHalfZ / kCells;

inline float VertexX(int i) { return -kHalfX + kCellX * i; }
inline float VertexZ(int j) { return -kHalfZ + kCellZ * j; }
// Which ground is live: the island's (the default) or the Sandbox test map's (shared/sandbox_terrain.h), which has the same size and squares.
// The pointers are switched when a scene is loaded (UseTerrain), never while it is being played.
inline const int16_t* gHeightData = kHeights;
inline const uint8_t* gColourData = kColours;
inline const char* gCoverData = kCover;
inline float gSpawnX = kSpawnX, gSpawnZ = kSpawnZ;   // flat inland ground for the lobby
inline bool gSandboxTerrain = false;
inline int gTerrainMapId = kMapId;
inline void UseTerrain(bool sandbox) {
    gTerrainMapId = sandbox ? 6 : kMapId;
    gSandboxTerrain = sandbox;
    if (sandbox) {
        const sandbox::Data& t = sandbox::Terrain();
        gHeightData = t.heights.data(); gColourData = t.colours.data(); gCoverData = t.cover.data();
        gSpawnX = sandbox::kSpawnX; gSpawnZ = sandbox::kSpawnZ;
    } else {
        gHeightData = kHeights; gColourData = kColours; gCoverData = kCover;
        gSpawnX = kSpawnX; gSpawnZ = kSpawnZ;
    }
}
inline void UseTerrainForMap(int mapId) {
    UseTerrain(mapId == 6);
    gTerrainMapId = mapId;
    if(mapId==9 || mapId==10) {
        const auto& t=arena::Terrain(mapId==10);
        gHeightData=t.heights.data();gColourData=t.colours.data();gCoverData=t.cover.data();
        gSpawnX=arena::kSpawn.x;gSpawnZ=arena::kSpawn.z;gSandboxTerrain=true;
    }
    if (mapId == 7) {
        gHeightData = convergence::kHeights; gColourData = convergence::kColours; gCoverData = convergence::kCover;
        gSpawnX = 0.0f; gSpawnZ = -600.0f;
    }
    if (mapId == 8) {   // Hyrule Kingdom (map.h kKingdomMapIndex)
        gHeightData = kingdom::kHeights; gColourData = kingdom::kColours; gCoverData = kingdom::kCover;
        gSpawnX = 2000.0f; gSpawnZ = 1100.0f;
    }
}
inline int VertexHeight(int i, int j) { return gHeightData[std::clamp(j, 0, kCells) * kVerts + std::clamp(i, 0, kCells)]; }

// Ground height under (x, z), exactly on the collision's triangles. False outside the map (there is nothing there).
inline bool GroundHeight(float x, float z, float* y) {
    if (x <= -kHalfX || x >= kHalfX || z <= -kHalfZ || z >= kHalfZ) return false;
    const float fx = (x + kHalfX) / kCellX, fz = (z + kHalfZ) / kCellZ;
    const int i = std::min(kCells - 1, static_cast<int>(fx)), j = std::min(kCells - 1, static_cast<int>(fz));
    const float u = fx - i, v = fz - j;
    const float h00 = static_cast<float>(VertexHeight(i, j)), h10 = static_cast<float>(VertexHeight(i + 1, j));
    const float h01 = static_cast<float>(VertexHeight(i, j + 1)), h11 = static_cast<float>(VertexHeight(i + 1, j + 1));
    if (y) *y = u >= v ? h00 + u * (h10 - h00) + v * (h11 - h10) : h00 + u * (h11 - h01) + v * (h01 - h00);
    return true;
}
inline bool IsWaterAt(float x, float z) {
    float y;
    return GroundHeight(x, z, &y) && y < static_cast<float>(kWaterY);
}
// How steep the ground is at (x, z): the up part of its triangle's normal (1 is flat). 0 outside the map.
inline float GroundUp(float x, float z) {
    if (x <= -kHalfX || x >= kHalfX || z <= -kHalfZ || z >= kHalfZ) return 0.0f;
    const float fx = (x + kHalfX) / kCellX, fz = (z + kHalfZ) / kCellZ;
    const int i = std::min(kCells - 1, static_cast<int>(fx)), j = std::min(kCells - 1, static_cast<int>(fz));
    const float h00 = static_cast<float>(VertexHeight(i, j)), h10 = static_cast<float>(VertexHeight(i + 1, j));
    const float h01 = static_cast<float>(VertexHeight(i, j + 1)), h11 = static_cast<float>(VertexHeight(i + 1, j + 1));
    const bool lower = fx - i >= fz - j;
    const float sx = (lower ? h10 - h00 : h11 - h01) / kCellX, sz = (lower ? h11 - h10 : h01 - h00) / kCellZ;   // the slope along x and along z
    return 1.0f / std::sqrt(1.0f + sx * sx + sz * sz);
}

// ---- ground cover -------------------------------------------------------------------------------------------------------------------
// What the texture shows on the ground (scripts/make_fortnite_map.py reads it from the colours): the mod grows trees in the painted woods,
// grass on the meadows and nothing on the roads, the towns' paving or the bare rock.
enum class Cover : uint8_t { Water, Meadow, Woods, Paving, Dirt };
inline Cover CoverAt(float x, float z) {
    if (x <= -kHalfX || x >= kHalfX || z <= -kHalfZ || z >= kHalfZ) return Cover::Water;
    const int i = std::min(kCoverCells - 1, static_cast<int>((x + kHalfX) / (2.0f * kHalfX) * kCoverCells));
    const int j = std::min(kCoverCells - 1, static_cast<int>((z + kHalfZ) / (2.0f * kHalfZ) * kCoverCells));
    return static_cast<Cover>(gCoverData[j * kCoverCells + i] - '0');
}

// ---- collision ------------------------------------------------------------------------------------------------------------------------

struct Vert { int16_t x, y, z; };
// One triangle the way the game stores it: three vertex numbers, the unit normal times 32767 (always pointing up) and the plane distance.
struct Poly { uint16_t a, b, c; int16_t nx, ny, nz, dist; uint8_t surface = 0; };   // surface: an index into the map's table (0 plain ground)
struct Mesh {
    std::vector<Vert> verts;
    std::vector<Poly> polys;
    Vert lo{}, hi{};
};

inline int16_t Round16(float v) { return static_cast<int16_t>(std::lround(std::clamp(v, -32000.0f, 32000.0f))); }

// Stone blocks built into the collision itself, so they are ground like the rest of the map (Link stands, walks and climbs on them, with nothing that
// can be rebuilt under his feet). The climbing blocks of a match go here; the host's game fills it in before the scene loads its collision.
struct Block {
    float x, z, half, bottom, top;   // a square footprint, `half` out from the middle, from `bottom` up to the flat `top`
    bool operator==(const Block& o) const { return x == o.x && z == o.z && half == o.half && bottom == o.bottom && top == o.top; }
};
inline std::vector<Block> gBlocks;
constexpr int kMaxBlocks = 480;   // the game's 13-bit vertex numbers (8191) leave room for this many 8-corner boxes after the 4225 of the ground

// The collision triangles. The game keeps vertex numbers in 13 bits (8191), and 65 x 65 vertices is 4225, so they all fit.
inline Mesh BuildCollision() {
    Mesh m;
    if (gTerrainMapId == 7) {
        for (const auto& v : convergence::kCollisionVertices) m.verts.push_back({v.x,v.y,v.z});
        for (const auto& p : convergence::kCollisionTriangles) m.polys.push_back({p.a,p.b,p.c,p.nx,p.ny,p.nz,p.dist});
        m.lo = m.hi = m.verts.front();
        for (const auto& v : m.verts) {
            m.lo = {std::min(m.lo.x,v.x),std::min(m.lo.y,v.y),std::min(m.lo.z,v.z)};
            m.hi = {std::max(m.hi.x,v.x),std::max(m.hi.y,v.y),std::max(m.hi.z,v.z)};
        }
        return m;
    }
    if (gTerrainMapId == 8) {   // Hyrule Kingdom: the ground and every building in one baked mesh, with a surface per triangle
        for (const auto& v : kingdom::kCollisionVertices) m.verts.push_back({v.x,v.y,v.z});
        for (const auto& p : kingdom::kCollisionTriangles) m.polys.push_back({p.a,p.b,p.c,p.nx,p.ny,p.nz,p.dist,p.surface});
        m.lo = m.hi = m.verts.front();
        for (const auto& v : m.verts) {
            m.lo = {std::min(m.lo.x,v.x),std::min(m.lo.y,v.y),std::min(m.lo.z,v.z)};
            m.hi = {std::max(m.hi.x,v.x),std::max(m.hi.y,v.y),std::max(m.hi.z,v.z)};
        }
        return m;
    }
    m.verts.reserve(kVerts * kVerts);
    for (int j = 0; j < kVerts; j++)
        for (int i = 0; i < kVerts; i++) m.verts.push_back({ Round16(VertexX(i)), Round16(static_cast<float>(VertexHeight(i, j))), Round16(VertexZ(j)) });
    m.lo = m.hi = m.verts[0];
    for (const Vert& v : m.verts) {
        m.lo = { std::min(m.lo.x, v.x), std::min(m.lo.y, v.y), std::min(m.lo.z, v.z) };
        m.hi = { std::max(m.hi.x, v.x), std::max(m.hi.y, v.y), std::max(m.hi.z, v.z) };
    }
    m.polys.reserve(kCells * kCells * 2);
    auto add = [&](int a, int b, int c) {
        const Vert &A = m.verts[a], &B = m.verts[b], &C = m.verts[c];
        const double ux = B.x - A.x, uy = B.y - A.y, uz = B.z - A.z, vx = C.x - A.x, vy = C.y - A.y, vz = C.z - A.z;
        double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        if (ny < 0) { std::swap(b, c); nx = -nx; ny = -ny; nz = -nz; }   // the game takes the normal from the winding: always up
        const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
        nx /= len; ny /= len; nz /= len;
        const Vert& P = m.verts[a];
        Poly p;
        p.a = static_cast<uint16_t>(a); p.b = static_cast<uint16_t>(b); p.c = static_cast<uint16_t>(c);
        p.nx = static_cast<int16_t>(std::lround(nx * 32767.0)); p.ny = static_cast<int16_t>(std::lround(ny * 32767.0)); p.nz = static_cast<int16_t>(std::lround(nz * 32767.0));
        p.dist = static_cast<int16_t>(std::lround(-(nx * P.x + ny * P.y + nz * P.z)));
        m.polys.push_back(p);
    };
    for (int j = 0; j < kCells; j++)
        for (int i = 0; i < kCells; i++) {
            const int v00 = j * kVerts + i, v10 = v00 + 1, v01 = v00 + kVerts, v11 = v01 + 1;
            add(v00, v10, v11);
            add(v00, v11, v01);
        }
    // The blocks: a flat top and four vertical sides each (no underside: the ground is there). Normals are given, not worked out from the winding.
    auto addFace = [&](int a, int b, int c, double nx, double ny, double nz) {
        const Vert& P = m.verts[a];
        Poly p;
        p.a = static_cast<uint16_t>(a); p.b = static_cast<uint16_t>(b); p.c = static_cast<uint16_t>(c);
        p.nx = static_cast<int16_t>(std::lround(nx * 32767.0)); p.ny = static_cast<int16_t>(std::lround(ny * 32767.0)); p.nz = static_cast<int16_t>(std::lround(nz * 32767.0));
        p.dist = static_cast<int16_t>(std::lround(-(nx * P.x + ny * P.y + nz * P.z)));
        m.polys.push_back(p);
    };
    int made = 0;
    for (const Block& bl : gBlocks) {
        if (made++ >= kMaxBlocks) break;
        const int first = static_cast<int>(m.verts.size());
        for (int k = 0; k < 8; k++) {   // bit 0: +x, bit 1: +z, bit 2: top
            const Vert v = { Round16(bl.x + ((k & 1) ? bl.half : -bl.half)), Round16((k & 4) ? bl.top : bl.bottom), Round16(bl.z + ((k & 2) ? bl.half : -bl.half)) };
            m.verts.push_back(v);
            m.lo = { std::min(m.lo.x, v.x), std::min(m.lo.y, v.y), std::min(m.lo.z, v.z) };
            m.hi = { std::max(m.hi.x, v.x), std::max(m.hi.y, v.y), std::max(m.hi.z, v.z) };
        }
        auto V = [&](int dx, int dz, int up) { return first + dx + 2 * dz + 4 * up; };
        addFace(V(0, 0, 1), V(1, 0, 1), V(1, 1, 1), 0, 1, 0);          // top
        addFace(V(0, 0, 1), V(1, 1, 1), V(0, 1, 1), 0, 1, 0);
        addFace(V(1, 0, 0), V(1, 1, 0), V(1, 1, 1), 1, 0, 0);          // +x side
        addFace(V(1, 0, 0), V(1, 1, 1), V(1, 0, 1), 1, 0, 0);
        addFace(V(0, 1, 0), V(0, 0, 0), V(0, 0, 1), -1, 0, 0);         // -x side
        addFace(V(0, 1, 0), V(0, 0, 1), V(0, 1, 1), -1, 0, 0);
        addFace(V(1, 1, 0), V(0, 1, 0), V(0, 1, 1), 0, 0, 1);          // +z side
        addFace(V(1, 1, 0), V(0, 1, 1), V(1, 1, 1), 0, 0, 1);
        addFace(V(0, 0, 0), V(1, 0, 0), V(1, 0, 1), 0, 0, -1);         // -z side
        addFace(V(0, 0, 0), V(1, 0, 1), V(0, 0, 1), 0, 0, -1);
    }
    return m;
}

// ---- the drawn island -----------------------------------------------------------------------------------------------------------------
// The island is drawn in blocks, one per collision square. Up close a block has kSub x kSub squares of vertex colour (a baked texture), a little
// further off half as many, and far away it is just its own two triangles. Water is a sheet at the water level (the mod draws it) over a flat sea bed kSeabedDrop below, with the sea bed's
// colour, so swimming looks right; a block that is all open water is only ever its two triangles, whatever the distance (there is nothing to
// see in it but the sheet).

// The sea bed is drawn this far under the water sheet (and follows the real ground where that is shallower). The sheet used to lie on a bed drawn
// flat at the water level, so it had no depth to show: ripples and wakes had nowhere to dip to, and a faint tint on an opaque bed read as nothing.
inline constexpr int kSeabedDrop = 40;
inline constexpr int kSeabedY = kWaterY - kSeabedDrop;

struct DrawVert { int16_t x, y, z; uint8_t r, g, b; };

inline DrawVert FineVertex(int fi, int fj) {   // fine vertex (fi, fj), 0..kFine
    const int ci = std::min(fi / kSub, kCells - 1), cj = std::min(fj / kSub, kCells - 1);
    const float u = static_cast<float>(fi - ci * kSub) / kSub, v = static_cast<float>(fj - cj * kSub) / kSub;
    const float h00 = static_cast<float>(VertexHeight(ci, cj)), h10 = static_cast<float>(VertexHeight(ci + 1, cj));
    const float h01 = static_cast<float>(VertexHeight(ci, cj + 1)), h11 = static_cast<float>(VertexHeight(ci + 1, cj + 1));
    float y = u >= v ? h00 + u * (h10 - h00) + v * (h11 - h10) : h00 + u * (h11 - h01) + v * (h01 - h00);
    y = std::max(y, static_cast<float>(kSeabedY));
    const uint8_t* c = &gColourData[(static_cast<size_t>(fj) * (kFine + 1) + fi) * 3];
    return { Round16(-kHalfX + kCellX * fi / kSub), Round16(y), Round16(-kHalfZ + kCellZ * fj / kSub), c[0], c[1], c[2] };
}

// How many squares per side a block is drawn with at each level of detail: kSub up close, half that further off, one far away.
inline constexpr int kLods = 3;
inline constexpr int LodSquares(int lod) { return lod <= 0 ? kSub : lod == 1 ? kSub / 2 : 1; }
inline constexpr int LodVerts(int lod) { return (LodSquares(lod) + 1) * (LodSquares(lod) + 1); }

// A coarser vertex takes the average colour of the fine ones around it (within `radius` fine squares) instead of the one colour it happens to sit
// on. The texture is busy (streams, paths, single trees), so a lone sample made far ground speckle with blue, orange and dark blotches that
// shimmered while the camera moved and jumped whenever a chunk changed level of detail. Averaged, the far ground fades to its plain colour. The
// result depends only on the fine position, so neighbouring blocks agree along their shared edge.
inline DrawVert FilteredVertex(int fi, int fj, int radius) {
    DrawVert v = FineVertex(fi, fj);
    if (radius <= 0) return v;
    int r = 0, g = 0, b = 0, n = 0;
    for (int j = std::max(0, fj - radius); j <= std::min(kFine, fj + radius); j++)
        for (int i = std::max(0, fi - radius); i <= std::min(kFine, fi + radius); i++) {
            const uint8_t* c = &gColourData[(static_cast<size_t>(j) * (kFine + 1) + i) * 3];
            r += c[0]; g += c[1]; b += c[2]; n++;
        }
    v.r = static_cast<uint8_t>(r / n); v.g = static_cast<uint8_t>(g / n); v.b = static_cast<uint8_t>(b / n);
    return v;
}

// Block (bi, bj) at level `lod` as a list of vertices, row by row: (LodSquares + 1)^2 of them, every one a fine vertex (its colour averaged over
// the squares it stands for, at the coarser levels).
inline void BlockVertices(int bi, int bj, int lod, std::vector<DrawVert>& out) {
    const int n = LodSquares(lod), step = kSub / n;
    const int radius = lod <= 0 ? 0 : step;
    for (int b = 0; b <= n; b++)
        for (int a = 0; a <= n; a++) out.push_back(FilteredVertex(bi * kSub + a * step, bj * kSub + b * step, radius));
}
inline void BlockVertices(int bi, int bj, bool fine, std::vector<DrawVert>& out) { BlockVertices(bi, bj, fine ? 0 : kLods - 1, out); }

// A block of open water: all four corners are under the water sheet, so every fine vertex of it is on the sheet and its finer versions would draw
// the same flat square.
inline bool BlockIsOpenWater(int bi, int bj) {
    return VertexHeight(bi, bj) < kWaterY && VertexHeight(bi + 1, bj) < kWaterY && VertexHeight(bi, bj + 1) < kWaterY && VertexHeight(bi + 1, bj + 1) < kWaterY;
}

} // namespace fortnite
} // namespace royale
