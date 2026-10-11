# Engine limits

The game engine (Ship of Harkinian and its renderer) was built around the N64's memory. These limits are raised for modern phones like the
AYN Odin 2 Portal by `patches/0029-royale-raise-engine-limits.patch` (game) and `patches/libultraship/0007-raise-renderer-limits.patch`
(renderer). The numbers code and exporters check against are in `shared/engine_limits.h`.

| Limit | Was | Now | Where |
|---|---|---|---|
| Collision vertices per mesh | 8191 (first two corners, 13 bits), 65535 (third) | 536,870,911 (29 bits, all corners) | `z64bgcheck.h` `COLPOLY_VTX_INDEX` |
| Collision triangles per mesh | 32767 (s16 polygon numbers) | 2,147,483,647 | `z64bgcheck.h` `SSNode`, `z_bgcheck.c` |
| Collision lookup nodes (our maps) | about 58,000 (0x23000 x 2 bytes; 65535 hard cap) | about 390,000 (0xC0000 x 4 = 3 MB; 32-bit node numbers) | `BgCheck_Allocate` |
| Collision lookup grid (our maps) | 32 x 2 x 32 cells | 64 x 4 x 64 cells | `BgCheck_Allocate` |
| Moving / solid-scenery collision (our maps) | 1024 triangles and vertices, 1000 nodes | 8192 triangles and vertices, 8000 nodes | `BgCheck_Allocate` |
| Collision memory, every other scene | memSize x 2 | memSize x 4 (nodes and triangles got bigger; every scene keeps at least its old room) | `BgCheck_Allocate` |
| Play heap (actors, collision, scene data) | 3.8 MB (0x1D4790 x 2) | 15.4 MB (0x1D4790 x 8) | `z_play.c` `Play_Init` |
| Solid display-list buffer (polyOpa) | 0x2FC0 commands (about 195 KB) | 0xBF00 commands (about 780 KB) | `z64.h` `GfxPool` |
| See-through buffer (polyXlu) | 0x1000 commands | 0x4000 commands | `z64.h` `GfxPool` |
| Overlay / work buffers | 0x800 / 0x100 commands | 0x2000 / 0x400 commands | `z64.h` `GfxPool` |
| Renderer vertex cache (one gSPVertex) | 64 (a bigger load wrote past the end) | 128, the most F3DEX2 can address; bigger loads are clamped | `gfx_pc.h` `MAX_VERTICES` |
| Triangles per draw call | 256 | 1024 | `gfx_pc.h` `MAX_BUFFERED` |
| Textures kept on the GPU | 500 | 2048 | `gfx_pc.cpp` `TEXTURE_CACHE_MAX_SIZE` |
| Mod graphics-layer pool | 6 MB | 32 MB (grows only when a frame needs it) | `shared/graphics_layers.h` `kPoolMax` |
| Solid scenery near you | 420 vertices, 420 triangles | 3000, 3000 | `Scenery.inc` `kSolidMaxVtx/Poly` |
| Fortnite Map climbing blocks baked in | 480 | 4000 | `shared/fortnite_map.h` `kMaxBlocks` |
| Loot actors at once | 56 | 128 | `WorldObjects.inc` `kMaxLootActors` |
| Prop actors at once | 170 | 400 | `WorldObjects.inc` `kMaxPropActors` |
| Props per match | 1200 | 4000 (16-bit on the wire: 65535 ceiling) | `shared/balance.h` `kMaxProps` |
| Imported OBJ model triangles | 40,000 | 500,000 | `shared/objmodel.h` `kObjMaxTriangles` |
| Map exporter checks | 8192 vertices (Convergence), 13-bit corner packing (Kingdom) | 29-bit; generated `Triangle` uses 32-bit corners on the next export | `tools/maps/` |

## What did not change, and why

- **World size.** Collision vertices are 16-bit positions in every OoT scene, so a map spans -32767 to 32767 units on each axis
  (`kWorldCoordMax`). Changing that means floating-point collision throughout the engine; scale a bigger map down instead.
- **Vertices per gSPVertex.** 128 is a hard limit of the F3DEX2 command format. Our exporters still batch 32 vertices; new exporters may use up to
  128 (`kRspMaxVertices`).
- **Textures.** 256 x 256 is the baseline. Load them with a tile load (`gDPLoadTextureTile`); a block load stops at 4096 texels.
- **Players, bosses, allies, network snapshots.** These are gameplay and bandwidth numbers, not memory limits.
- **Graphics page presets.** Low, Medium and High still choose how much is drawn. The bigger buffers only remove dropped frames and crashes; they cost
  memory (about 14 MB more up front: 11.6 MB play heap, 2.2 MB display lists), not speed. The layer pool grows only when needed.

ROM scenes are converted on load: their 16-bit vertex numbers (flags in bits 13 to 15) become 32-bit numbers with the flags in bits 29 to 31
(`COLPOLY_FROM_ROM` in `CollisionHeaderFactory.cpp`).
