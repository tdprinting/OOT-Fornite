# Waiting-room reef aquarium

The Temple of Time waiting lobby has a decorative Hylian stone aquarium, with three original low-poly clownfish and three cleaner wrasse. Clownfish have warm orange bodies, cream bands with charcoal edges and chunky fins. Wrasse have narrow blue bodies and a dark side stripe. Flat shading and small polygon silhouettes match the N64/Ocarina of Time direction.

One clownfish approaches the front when Link visits; the small shy one shelters by the pink anemone, and the third weaves playful laps. Cleaner wrasse patrol faster and take turns visiting clownfish with slower cleaning-like motions. Bodies flex, tails sweep, pectoral fins paddle and dorsal fins flutter. Movement is smooth and separated, with fish-sized clearance inside the glass.

The aquarium is local decoration and has no collision or gameplay effects. It appears only while joined to a lobby in the Temple of Time waiting room. Turning it off, leaving, starting the countdown or changing scenes removes it. The actor is placed on a flat, unobstructed patch ahead of the lobby arrival point; if none is available it retries once a second. It does not alter Temple of Time scene files or the original quest.

Toggle **Graphics → Lobby aquarium**, or **Debug → Lobby reef aquarium**. Enabled by default. Clients can choose independently; no protocol change is needed.

## Source and authoring

- `tools/fish/make_fish.py`: source geometry; regenerate `shared/lobby_fish_model.h` with Python, no dependencies.
- `shared/lobby_fish.h`: local swimming, visitor reactions, cleaning visits and vertex deformation.
- `mod/Royale/RoyaleLobbyFish.h`: registered cosmetic actor, placement, opaque fish and translucent aquarium rendering.
- `tools/fish/build_fish.py`: builds editable Blender source and an animated GLB from the same geometry. Run `blender --background --python tools/fish/build_fish.py`; add `-- --render` for a preview.
- `assets/fish/lobby_reef.blend` and `.glb`: original models with looping swim shape keys; gameplay also adds individual procedural behavior.

The game uses generated vertex-color meshes, not a GLB loader. Regenerate the header when editing the mesh generator. Blender edits alone do not change the game geometry. Keep the runtime deformation and the preview's `pose()` equivalent.

## Validation

`royale_fish_tests` exercises long-running movement, visitor/cleaner reactions, reset, bounds and animated mesh dimensions. Full engine compilation runs in the Android game-build workflow; Codex branches now trigger that existing workflow with its unchanged signing key and version counter. On the Odin 2 Portal, verify arrival placement, visibility through the glass, controller/camera viewing, Graphics toggle, leaving/rejoining and countdown removal. Rendered asset previews are not in-game screenshots.
