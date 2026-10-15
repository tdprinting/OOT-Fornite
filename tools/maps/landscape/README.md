# Landscape map (Tee's fantasy landscape GLB)

Step 1 of the new main map: the uploaded `fantasylandscape3dmodel.glb` cleaned and scaled to game units. Not yet in the game: no map
index, buildings, places, water, bot navigation or engine data yet.

`clean.py` (run with Blender's `bpy`, e.g. `PYTHONPATH=<bpy dir> python3 tools/maps/landscape/clean.py`; the input path is set at the top):

- welds duplicate vertices and drops floating islands under 60 faces;
- decimates to 25% (381k to 95k triangles);
- scales to game units: 14830 wide (x), 15550 deep (z), up to 1393 high (y), centred on the origin;
- bakes a 256 x 256 height grid and colour grid (the base colour texture at each cell's face) for the next steps.

Output files (not committed, kept in the project folder): `landscape-map/landscape_clean.glb`, `.blend`, `height.npy`, `colour.npy`,
`meta.json`, `preview_top_shaded.png`.

Known: 20% of the grid has no ground (sea and the model's cut edges); 80% of cells hit the mesh.
