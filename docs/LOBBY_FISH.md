# Waiting-room reef aquarium

The Temple of Time waiting lobby has a Hylian stone aquarium, with three original low-poly clownfish, three cleaner wrasse and three hermit crabs. Clownfish have warm orange bodies, cream bands with charcoal edges and chunky fins. Wrasse have narrow blue bodies and a dark side stripe. Crabs have faceted spiral-marked shells, orange walking legs, pincers and eyestalks. Flat shading and small polygon silhouettes match the N64/Ocarina of Time direction.

One clownfish approaches the front when Link visits; the small shy one shelters by the pink anemone, and the third weaves playful laps. Cleaner wrasse patrol faster and take turns visiting clownfish with slower cleaning-like motions. Bodies flex, tails sweep, pectoral fins paddle and dorsal fins flutter. Movement is smooth and separated, with fish-sized clearance inside the glass.

The tank is half the original version's world size, with thinner rim/posts and smaller fish. Explicit translucent blending is required: the engine's `Gfx_SetupDL_25Xlu` still selects the opaque SETUPDL_25 render mode. The glass now overrides that mode and uses very low alpha so the animals are visible through it.

The aquarium uses a closed dynamic collision mesh, including walls and a top, so Link and the camera cannot pass through it. Visual and collision geometry use the same half-scale and rotation. Its collision registration is removed on destruction; if no collision slot is available, the aquarium is not displayed without collision.

Animals remain local decoration with no combat or network effects. The tank appears only while joined to a lobby in the Temple of Time waiting room. Turning it off, leaving, starting the countdown or changing scenes removes it. The actor is placed on a flat, unobstructed patch ahead of the lobby arrival point; if none is available it retries once a second. It does not alter Temple of Time scene files or the original quest.

Fish use avoidance followed by a constraint solver with spheres enclosing their animated bodies and fins. Cleaning visits do not reduce clearance. Pairs pinned to the top/bottom slide sideways instead of becoming stuck in an overlapping state. Hermit crabs walk across the sand, pause to sift it, and avoid each other. Their leg and pincer movements are animated independently of the shell.

Toggle **Graphics → Lobby aquarium**, or **Debug → Lobby reef aquarium**. Enabled by default. Clients can choose independently; no protocol change is needed.

## Source and authoring

- `tools/fish/make_fish.py`: source geometry; regenerate `shared/lobby_fish_model.h` with Python, no dependencies.
- `shared/lobby_fish.h`: local swimming, visitor reactions, cleaning visits and vertex deformation.
- `shared/lobby_reef_geometry.h`: closed collision box with outward triangle winding.
- `mod/Royale/RoyaleLobbyFish.h`: registered cosmetic actor, placement, opaque fish and translucent aquarium rendering.
- `tools/fish/build_fish.py`: builds editable Blender source and an animated GLB from the same geometry. Run `blender --background --python tools/fish/build_fish.py`; add `-- --render` for a preview.
- `assets/fish/lobby_reef.blend` and `.glb`: original models with looping swim shape keys; gameplay also adds individual procedural behavior.

The game uses generated vertex-color meshes, not a GLB loader. Regenerate the header when editing the mesh generator. Blender edits alone do not change the game geometry. Keep the runtime deformation and the preview's `pose()` equivalent.

## Validation

`royale_fish_tests` exercises fifteen minutes of movement and reactions, per-frame fish/crab separation, animated mesh dimensions, deliberately coincident fish at tank corners, and collision winding/closed edges. Full engine compilation runs in the Android game-build workflow with its unchanged signing key and version counter. On the Odin 2 Portal, verify the smaller tank, visibility through front/side/top glass, Link walking/rolling against every side, camera obstruction, crab movement, Graphics toggle, leaving/rejoining and countdown removal (including collision cleanup). Rendered asset previews are not in-game screenshots.
