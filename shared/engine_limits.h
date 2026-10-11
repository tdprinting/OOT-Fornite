#pragma once
#include <cstddef>
#include <cstdint>

// The engine's size limits as raised for modern phones (Odin 2 Portal and up) by patches/0029-royale-raise-engine-limits.patch and
// patches/libultraship/0007-raise-renderer-limits.patch. Map exporters and tests check against these. The old values are in the comments;
// docs/ENGINE_LIMITS.md has the full table.
namespace royale::engine {

// Collision (z64bgcheck.h). Vertex numbers are 32 bit with three flag bits on top; triangle and lookup-node numbers are 32 bit.
inline constexpr uint32_t kCollisionMaxVertices = (1u << 29) - 1;   // was 8191 in the first two corners (13 bits), 65535 in the third
inline constexpr uint32_t kCollisionMaxPolygons = 0x7FFFFFFF;       // was 32767 (polygon numbers were s16)
// Collision vertices are still 16-bit positions, as in every OoT scene: the world is -32767 to 32767 units on each axis.
inline constexpr int kWorldCoordMax = 32767;

// Rendering (libultraship Fast3D). The F3DEX2 commands keep a vertex number in 7 bits, so 128 is the most one load can ever address.
inline constexpr int kRspMaxVertices = 128;        // was 64 (a gSPVertex of more than 64 wrote past the cache)
inline constexpr int kTrianglesPerDrawCall = 1024; // was 256
inline constexpr int kTextureCacheEntries = 2048;  // was 500
inline constexpr int kTextureBaseline = 256;       // Tee's baseline for new textures: 256 x 256 (load them as tiles; a block load stops at 4096 texels)

// The game's per-frame display-list buffers (z64.h GfxPool), in commands. All four are 4x the original.
inline constexpr size_t kPolyOpaCommands = 0x2FC0 * 4;
inline constexpr size_t kPolyXluCommands = 0x1000 * 4;
inline constexpr size_t kOverlayCommands = 0x800 * 4;

}  // namespace royale::engine
