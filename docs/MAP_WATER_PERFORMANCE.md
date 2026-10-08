# Kingdom refinement and water stability

The supplied IMG_0006/0007/0008/0010 recordings show the Kingdom map, water seams, and steep building sites. The detailed water draw loop passed the coarse vertex buffer to the fine-grid draw routine. At Low detail the coarse buffer has 225 vertices, while 16 detailed patches need 1,296. The corrected pass uses its own buffer, and a source-extracted regression executes the real loop with checked vertex loads. The old source fails this test; the fixed source passes.

Water depth and caustic caches now distinguish map IDs. Map switches reset wake tracking and ripple heights. Caustic flow coordinates wrap by complete texture tiles before reaching their signed 16-bit limit. Wave directions, amplitudes, time phases and steepness are prepared once per surface frame. Independent reference samples check that wave heights and horizontal displacement stay unchanged.

Kingdom retains its places, textured buildings, interiors and foliage. The authored Clock Town, Kakariko, Snowpeak, lookout and docks shelves now contain more of their buildings. The ice hut has its own shelf. Entrance ramps find their landing on the actual collision terrain and register as bot walkways. Crowded village roofs use rotation-aware spacing, keeping the same buildings in their neighbourhoods. Short rooms use two interior stair flights and a landing instead of a ramp crossing the exterior wall. Tall pillars are baked into permanent collision instead of consuming nearby proxy slots. Vertex packing includes a conflict-checked independent set of terrain vertices when architectural vertices alone cannot fit the 13-bit first/second corner limit.

The castle spire stands behind the keep with its own clear doorway and a connected rear terrace, rather than intersecting the hall. The windmill reserves its footprint before village houses are spaced.

The terrain and buildings now draw in the mod’s growable frame pool instead of filling the original fixed game command buffer. Batch command guards prevent an opaque-buffer overrun.

The renderer builds vertices and display lists per visible batch, with normal/bump and baked forms allocated only when used. Unseen batches expire after 180 game frames. Every batch has a conservative bounding sphere; only batches entirely behind the camera or beyond their existing draw distance are omitted. Texture resolution, foliage geometry, nearby surface relief, waves, foam and sparkle are retained.

Validation commands:

- `python scripts/test_water_draw_buffers.py` (uses C++; Windows: invoke in a VS developer shell with `cl` as the compiler argument).
- `cmake -S server -B build`, `cmake --build build`, `ctest --test-dir build --output-on-failure`. Kingdom and Convergence suites are now registered with CTest.
- `python tools/maps/kingdom/export.py` regenerates all edited map data.

Protocol 30 requires all multiplayer peers to update together.

Device checks still required: walk into both entrances of slope-side houses, climb interiors and land on roofs, swim along shores in all water detail settings, rotate the camera near water and walls, switch island maps, and compare frame time/memory during a long match. Offline geometry and CPU tests do not establish an Odin frame-rate improvement or eliminate every visual issue in the recordings.

Desktop wave microbenchmark (400 frames × 2,048 samples, MSVC /O2): original 159.2 ms; prepared 63.1 ms, about 2.52× sample throughput. This measures the wave sampler, not game FPS or Android performance.
