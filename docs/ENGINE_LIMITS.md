# Engine limits

The game engine (Ship of Harkinian and its renderer) was built around the N64's memory. These limits are raised for modern phones like the
AYN Odin 2 Portal by `patches/0029-royale-raise-engine-limits.patch` (game) and `patches/libultraship/0007-raise-renderer-limits.patch`
(renderer), and raised again for maps that fill the whole world (Hyrule Before the Third Storm, Tee 2026-10-26) by
`patches/0039-royale-raise-engine-limits-2.patch` and `patches/libultraship/0013-raise-renderer-limits-2.patch`. The numbers code and
exporters check against are in `shared/engine_limits.h`. "Now" below is after round 2; the middle column is the original value, then round 1.

| Limit | Was | Now | Where |
|---|---|---|---|
| Collision vertices per mesh | 8191 (first two corners, 13 bits), 65535 (third) | 536,870,911 (29 bits, all corners) | `z64bgcheck.h` `COLPOLY_VTX_INDEX` |
| Collision plane distance | s16 (a tilted wall past about 32767 units from the origin overflowed) | s32 (`patches/0040`; ROM scenes are sign-extended on load) | `z64bgcheck.h` `CollisionPoly.dist` |
| Collision triangles per mesh | 32767 (s16 polygon numbers) | 2,147,483,647 | `z64bgcheck.h` `SSNode`, `z_bgcheck.c` |
| Collision lookup nodes (our maps) | about 58,000 (0x23000 x 2 bytes; 65535 hard cap); round 1 about 390,000 | about 2.1 million (0x400000 x 4 = 16 MB; 32-bit node numbers) | `BgCheck_Allocate` |
| Collision lookup grid (our maps) | 32 x 2 x 32 cells; round 1 64 x 4 x 64 | 256 x 8 x 256 cells (about 250 units across on a world-wide map; 6 MB) | `BgCheck_Allocate` |
| Moving / solid-scenery collision (our maps) | 1024 triangles and vertices, 1000 nodes; round 1 8192 | 32768 triangles and vertices, 34816 nodes | `BgCheck_Allocate` |
| Collision memory, every other scene | memSize x 2 | memSize x 4 (nodes and triangles got bigger; every scene keeps at least its old room) | `BgCheck_Allocate` |
| Play heap (actors, collision, scene data) | 3.8 MB (0x1D4790 x 2); round 1 64 MB | 160 MB | `z_play.c` `Play_Init` |
| System heap (every game state's heap) | 4 MB; round 1 128 MB | 256 MB | `z64.h` `SYSTEM_HEAP_SIZE` |
| Solid display-list buffer (polyOpa) | 0x2FC0 commands (about 195 KB); round 1 x4 | x16: 0x2FC00 commands (about 1.5 MB) | `z64.h` `GfxPool` |
| See-through buffer (polyXlu) | 0x1000 commands; round 1 x4 | x16: 0x10000 commands | `z64.h` `GfxPool` |
| Overlay / work buffers | 0x800 / 0x100 commands; round 1 x4 | x8: 0x4000 / 0x800 commands | `z64.h` `GfxPool` |
| Camera far clip | the scene's fog distance (s16, at most 32767; Hyrule Field 12800) | a map may ask for up to 64000 (`gMapFarClip`, `Royale_FarClip`); every map so far keeps its scene's value | `z_play.c` `Play_Draw`, `EngineBridge.inc` |
| Renderer vertex cache (one gSPVertex) | 64 (a bigger load wrote past the end) | 128, the most F3DEX2 can address; bigger loads are clamped | `gfx_pc.h` `MAX_VERTICES` |
| Triangles per draw call | 256; round 1 1024 | 4096 | `gfx_pc.h` `MAX_BUFFERED` |
| Textures kept on the GPU | 500; round 1 2048 | 4096 | `gfx_pc.cpp` `TEXTURE_CACHE_MAX_SIZE` |
| Largest map texture | 128; Great Hyrule Kingdom 256 | 512 x 512 RGBA16 (wrap mask 9, shift 14; `engine::kTextureMaxSize`) | `Terrain.inc` `DrawAuthoredStructures` |
| Mod graphics-layer pool | 6 MB; round 1 32 MB | 128 MB (grows only when a frame needs it) | `shared/graphics_layers.h` `kPoolMax` |
| Solid scenery near you | 420 vertices, 420 triangles; round 1 3000 | 12000, 12000 | `Scenery.inc` `kSolidMaxVtx/Poly` |
| Fortnite Map climbing blocks baked in | 480 | 4000 | `shared/fortnite_map.h` `kMaxBlocks` |
| Loot actors at once | 56; round 1 128 | 256 | `WorldObjects.inc` `kMaxLootActors` |
| Prop actors at once | 170; round 1 400 | 1024 | `WorldObjects.inc` `kMaxPropActors` |
| Props per match | 1200; round 1 4000 | 16000 (16-bit on the wire: 65535 ceiling) | `shared/balance.h` `kMaxProps` |
| Imported OBJ model triangles | 40,000; round 1 500,000 | 2,000,000 | `shared/objmodel.h` `kObjMaxTriangles` |
| Map exporter checks | 8192 vertices (Convergence), 13-bit corner packing (Kingdom) | 29-bit; generated `Triangle` uses 32-bit corners on the next export | `tools/maps/` |

## What did not change, and why

- **World size.** Collision vertices are 16-bit positions in every OoT scene, so a map spans -32767 to 32767 units on each axis
  (`kWorldCoordMax`). Changing that means floating-point collision throughout the engine; scale a bigger map down instead.
- **Vertices per gSPVertex.** 128 is a hard limit of the F3DEX2 command format. Our exporters still batch 32 vertices; new exporters may use up to
  128 (`kRspMaxVertices`).
- **Textures.** Up to 512 x 512 for maps (256 x 256 stays the usual size for models). Load them with a tile load (`gDPLoadTextureTile`); a
  block load stops at 4096 texels. The renderer itself takes textures up to 8192 (its upload buffer), and the F3DEX2 tile commands address
  1024, so 512 is a choice of memory, not a hard limit: a 512 RGBA16 texture is 512 KB in the game and 1 MB on the GPU.
- **Players, bosses, allies, network snapshots.** These are gameplay and bandwidth numbers, not memory limits.
- **Graphics page presets.** Low, Medium and High still choose how much is drawn. The bigger buffers only remove dropped frames and crashes; they cost
  memory, not speed: round 2 reserves about 6 MB more of display lists up front (two frames' pools); the heaps are reserved address space, and
  only the pages a map really uses are backed by memory. The layer pool grows only when needed. None of this has been measured on the Odin yet.

ROM scenes are converted on load: their 16-bit vertex numbers (flags in bits 13 to 15) become 32-bit numbers with the flags in bits 29 to 31
(`COLPOLY_FROM_ROM` in `CollisionHeaderFactory.cpp`).
