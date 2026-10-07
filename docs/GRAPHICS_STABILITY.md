# Cloud and rendering stability fix

The supplied IMG_9977.mov shows camera-following clouds and graphics instability during island movement and storm entry.

- Both cloud styles now use wind-driven world positions and fixed world heights. Tiled clouds fade before recycling. A fixed regional matrix keeps intermediate rendered frames from moving camera-relative cloud vertices with the interpolated camera.
- Plants and ground patches have stable interpolation paths based on mesh and position. Camera culling no longer changes which previous object a visible instance interpolates against.
- Sky, fog, light, storm and particle passes have separate interpolation identities. Sky matrix allocation checks the frame arena and records an explicit world matrix rather than applying the dummy actor's transform.
- Water-grid origins have identities based on the snapped grid coordinate. Crossing a cell does not blend the old origin with newly generated vertices.
- Negative waves remain above the opaque water-level terrain mesh, preventing the surface from alternating in front of and behind it.

The graphics regression executable checks world cloud anchoring under horizontal and vertical camera travel, wind motion, invisible wrap transitions and water separation. A full game compilation is also required; server tests alone do not validate engine integration.

Device verification: use the same island, sprint and rotate the camera with both sky styles, approach shores, enter/leave the storm, change weather, and compare 20 FPS with higher interpolation frame rates. Clouds should drift with wind while keeping their world height; foliage/ground must not stretch toward newly culled neighbours; water should remain visible across grid boundaries. Check dawn/night and skydiving too. Source fixes and successful compilation do not establish that every artifact in the recording is resolved on the device.

# Graphics layers (the flicker fix behind IMG_9991.mov)

Root cause: every effect wrote into the game's own display-list buffers, which were sized for the original game. The see-through buffer holds 4096 commands; the sky alone writes over 3000 on a clear night (the Milky Way, stars, dome and hills), and clouds, fog banks, the storm wall, weather specks and water come on top. When that buffer overflows the game drops the whole frame. The solid buffer (about 195 KB) is shared with every per-frame vertex and matrix; when it ran low, the old `FrameAlloc` refused memory and that effect skipped the frame (Maya's model alone needs 65 KB a frame). Both show as flicker in the sky, fog, foliage, storm wall and weather.

The fix (`RoyaleMod.cpp`, "graphics layers"; rules in `shared/graphics_layers.h`, tested by `server/tests/graphics_layers_tests.cpp`):

- Each effect draws as a `GfxLayer`: its commands, vertices and matrices go to a pool the mod owns (two halves, one per frame in flight). The game's buffers get one "draw this list" command per stream. The drawing code is unchanged: while a layer is open the game's buffer pointers point into the pool.
- Every layer starts and ends with the game's standard render setup, so no layer's fog, culling or blend mode leaks into another or into the game.
- Fixed order in `Projectile_Draw`: solid layers, then see-through ones from the ground up and far to near (ground patches, water, storm wall, fog, fireflies, tornado, weather, wind).
- The sky is its own pass behind the world (`DrawBackdrop`, called by `patches/0022` right after the game's skybox), drawn without a depth test like the game's skybox. Before, it was depth tested at 7000 units, where far hills and the dome fought over pixels.
- Fog banks, weather specks and wind streaks are drawn from a world anchor (a 2048-unit grid corner) instead of the camera, so the frames the game blends in between no longer drag them along with the camera.
- Memory: the pool starts at 512 KB per half, grows by half when a frame uses more than 70% (cap 6 MB), and each layer's see-through window is sized from its own use, never below a per-layer floor. A layer that does not fit is left out of one frame and gets more room the next. Graphics page, "Graphics memory (layers)", shows the numbers; "Left out" should stay at or near zero.

Adding an effect: draw it inside `{ GfxLayer layer(play, GfxLayerId::X); DrawX(play); }` in `Projectile_Draw` (add `X` to `GfxLayerId`, `kGfxLayerNames` and `kGfxLayerXluKB`), use `FrameAlloc` for per-frame vertices, and call `GfxHasRoom` before each piece in a big loop.
