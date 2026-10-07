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

# Water you can see (follow-up in the same PR)

Why the water was barely visible: on the island the sea was an opaque bed drawn flat at the water level with a faint (13 to 40 percent) tinted sheet over it, so there was nothing under the sheet to see, and the "stay above the opaque base" fix clamped every trough to 0.75 units over it, which flattened every dip and ripple. On top of that the sheet's squares are 140 to 360 units wide, while a swimmer's dip is about 30 units across, so displacement fell between the grid points.

- The island's sea bed is now drawn 40 units below the sheet (`kSeabedDrop`, following the real ground where it is shallower). The sheet is stronger (about 27 to 75 percent), the shallows still show the bed, and troughs and dips can go well under the level (`WaterSurfaceOffset`). Lakes and rivers keep the game's own water under ours, so there the sheet rides 5 units over it.
- Squares a swimmer, bot, cart or ring touches, and the ones round the player, are drawn again as a fine 8 x 8 grid (up to 16 squares, about 1300 vertices). The displacement, light and dark on the slopes, and the foam live there; it fades to nothing at the border with a coarse square, so there are no cracks. Rings last 4.5 seconds and carry a pale crest.
- More water life from the game's own sprites (`EffectSsDtBubble`, `EffectSsSibuki`, `EffectSsGSplash`, `EffectSsGRipple`): bubble bursts and spray going in and coming out, a steady string of bubbles for anyone under water (the local player too), foam bubbles in a swimmer's wake, spray off the bow of something fast, sea-floor vents that breathe strings of bubbles and pop in rings at the surface, and now and then something leaping out of the water. A running estimate of live bubbles keeps them under the game's small effect table. Graphics page, Water: "More bubbles, spray, sea-floor vents and leaping fish" (CVar `Royale.WaterLife`).
