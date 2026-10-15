# Great Hyrule Kingdom (map 14, the default main map)

Built from Tee's fantasy landscape model (`fantasylandscape3dmodel.glb`). Distinct from Hyrule Kingdom (map 8): use
`great_hyrule_kingdom` / `GreatHyruleKingdom` in code and file names. Game-side notes: `docs/GREAT_HYRULE_KINGDOM.md`.

## Rebuild

1. `clean.py` (Blender's `bpy`): welds, drops floating bits, decimates, scales the model to game units and bakes `height.npy` and
   `colour.npy` into `/mnt/project-files/landscape-map/` (not committed).
2. `python3 -I prepare_landscape.py` (from this folder, numpy only): removes the trees fused into the model's surface (a morphological
   opening), and writes `landscape/ground.npy`, `colour.npy`, `trees.npy`, `biome.npy` (committed, small).
3. `python3 tools/maps/great_hyrule_kingdom/export.py`: writes `shared/great_hyrule_kingdom_data.h` and `_model.h`.
4. `python3 tools/maps/great_hyrule_kingdom/validate.py` (CI runs it as `scripts/test_great_hyrule_kingdom_authoring.py`): must say 0 failures.
5. Previews: `python3 tools/maps/great_hyrule_kingdom/blender_build.py --renders overview,top,castle_town` (needs `bpy`).

## Files

- `layout.py`: the 24 places, roads, river, pools, mountains, the Death Mountain pass, pads, ally and cart stations, the lobby.
- `terrain.py`: the ground. The cleaned landscape, the shore shelf, the added mountains (after the shelf, so Death Mountain rises out of
  the sea), pads, the pass, the stepped river (one water box per step, levees off the channel) and the pool dishes. `simplify()` lays
  nearly-flat 4 x 4 and 2 x 2 blocks of ground exactly flat so the exporter can merge them: the game keeps a collision vertex number in
  13 bits for two corners of a triangle (patches/0025), and the map must stay near 11,500 collision vertices.
- `pois.py`: every place, built from `kit.py` (Skyloft's kit, sized from adult Link). Zero build: cover comes from walls, ruins, rocks,
  hedges, trees, upper floors and roof terraces.
- `textures.py`: the Kingdom set plus Skyloft's extras, kept at 256 x 256 (Tee's texture rule).
- `export.py`, `validate.py`, `routes.py`, `world.py`, `blender_build.py`: as for Skyloft Isles.

## Budgets (last export)

| | |
|---|---|
| Collision vertices | 11,444 (3,254 in the high range) |
| Drawn vertices | 329,871 in 735 batches |
| Chests | 158 |
