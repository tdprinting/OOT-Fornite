#pragma once
#include <cstddef>
#include <cstdint>

// The engine's size limits as raised for modern phones (Odin 2 Portal and up) by patches/0029-royale-raise-engine-limits.patch,
// patches/libultraship/0007-raise-renderer-limits.patch and, for world-wide maps, patches/0039-royale-raise-engine-limits-2.patch and
// patches/libultraship/0013-raise-renderer-limits-2.patch (and the 32-bit plane distance of patches/0040). Map exporters and tests check against these. The old values are in the comments;
// docs/ENGINE_LIMITS.md has the full table.
namespace royale::engine {

// Collision (z64bgcheck.h). Vertex numbers are 32 bit with three flag bits on top; triangle and lookup-node numbers are 32 bit.
inline constexpr uint32_t kCollisionMaxVertices = (1u << 29) - 1;   // was 8191 in the first two corners (13 bits), 65535 in the third
inline constexpr uint32_t kCollisionMaxPolygons = 0x7FFFFFFF;       // was 32767 (polygon numbers were s16)
// Collision vertices are still 16-bit positions, as in every OoT scene: the world is -32767 to 32767 units on each axis.
inline constexpr int kWorldCoordMax = 32767;
// A collision triangle's plane distance (-(normal . corner)) is 32 bit since patches/0040 (it was s16, so a tilted wall more than about
// 32767 units from the origin overflowed: a world-wide map's corners reach |x| + |z| = 64800).
inline constexpr int64_t kCollisionMaxPlaneDist = 2147483647;
// Our maps' static collision (BgCheck_Allocate): the lookup grid and the room for its lists (8-byte nodes; a triangle is listed once per cell
// it touches). A map whose triangles touch more than kCollisionMaxListNodes cells in all does not fit.
inline constexpr int kCollisionGridX = 256, kCollisionGridY = 8, kCollisionGridZ = 256;   // was 64 x 4 x 64
// Each cell reaches kCollisionCellOverlap units past its sides (BGCHECK_SUBDIV_OVERLAP) and is at least kCollisionCellMin long. The room is
// 32 MB (0x800000 x 4, patches/0041) less the lookup grid (12 bytes a cell) and about 2 MB of moving-collision lists and per-triangle flags.
inline constexpr int kCollisionCellOverlap = 50;
inline constexpr float kCollisionCellMin = 150.0f;
inline constexpr size_t kCollisionLookupBytes = size_t(kCollisionGridX) * kCollisionGridY * kCollisionGridZ * 12;
inline constexpr size_t kCollisionMaxListNodes = (0x800000 * 4 - kCollisionLookupBytes - 2 * 1024 * 1024) / 8;   // about 3.1 million (was 1.09 million)
// Moving and solid-scenery collision on our maps (the dyna lists): polygons and vertices.
inline constexpr int kDynaMaxPolygons = 32768;                                            // was 8192 (1024 originally)
// Memory the game states may use (z_play.c, z64.h). Pages that are never touched are never backed by real memory.
inline constexpr size_t kPlayHeapBytes = size_t(160) << 20;                               // was 64 MB (3.8 MB originally)
inline constexpr size_t kSystemHeapBytes = size_t(256) << 20;                             // was 128 MB (4 MB originally)
// The farthest a map may ask the camera to draw (Royale_FarClip, patches/0039). A scene's own fog distance is s16, so 32767 at most.
inline constexpr float kFarClipMax = 64000.0f;

// Rendering (libultraship Fast3D). The F3DEX2 commands keep a vertex number in 7 bits, so 128 is the most one load can ever address.
inline constexpr int kRspMaxVertices = 128;        // was 64 (a gSPVertex of more than 64 wrote past the cache)
inline constexpr int kTrianglesPerDrawCall = 4096; // was 1024 (256 originally)
inline constexpr int kTextureCacheEntries = 4096;  // was 2048 (500 originally)
inline constexpr int kTextureMaxSize = 512;        // the largest map texture: 512 x 512 (Tee, 2026-10-26). The renderer takes up to 8192; the
                                                   // F3DEX2 tile commands address 1024; 512 RGBA16 is 512 KB a texture
inline constexpr int kTextureBaseline = 256;       // the usual size for models: 256 x 256 (load as tiles; a block load stops at 4096 texels)

// The game's per-frame display-list buffers (z64.h GfxPool), in commands: 16x the original for the solid and see-through ones, 8x for the
// overlay (all were 4x before patches/0039).
inline constexpr size_t kPolyOpaCommands = 0x2FC0 * 16;
inline constexpr size_t kPolyXluCommands = 0x1000 * 16;
inline constexpr size_t kOverlayCommands = 0x800 * 8;

}  // namespace royale::engine
