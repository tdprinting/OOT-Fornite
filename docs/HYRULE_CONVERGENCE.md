# Hyrule Convergence

An additional Battle Royale map, selectable in the main solo-test map chooser and the host lobby's map chooser. Existing map IDs are preserved: Fortnite 5, Sandbox 6, Hyrule Convergence 7. Protocol 25 prevents older clients from joining a world whose collision they cannot render.

The world uses the existing Hyrule Field custom-scene hooks, with its own height grid, minimap colours, textured native geometry and full scene collision. All buildings and their furnished interiors belong to the same scene. Their two open entrances have 220-unit width and 280-unit height. There are no door actors, room transitions or exit surfaces. Furniture sits beside a clear central aisle; beds, hearths, tables, shelves, pottery, books, benches and storage provide lived-in interiors. Roofs, walls and major furniture have collision. Small ornaments are decorative.

## Places and bosses

| Place | Encounter |
|---|---|
| Triforce Market | Four furnished houses, central fountain and loot |
| Lon Lon Crossroads | Ranch homes, hay, milk churns and buggy routes |
| Lostwood Sanctuary | Moss Lizalfos, timber outposts and wooded cover |
| Ember Quarry | Magma Dodongo, basalt buildings and forging stations |
| Frostwatch Lodge | White Wolfos, snowy hills and timber lodges |
| Spirit Caravanserai | Iron Knuckle, sandstone buildings and colonnades |
| Lake Lantern | Big Octo, waterside homes, a jetty and swimming pool |
| Whispering Graveyard | Dead Hand, mossy buildings and headstones |
| Royal Ruins | Stalfos, broken battlements and stone outposts |
| Royal Pavilion | Furnished royal hall, turquoise spires and climbing steps |

When minibosses are enabled, all seven appear at their authored homes, including in small matches. Switching minibosses off still suppresses them. Boss types are matched by their home position after the spawn list is shuffled, so match seeds cannot put a volcanic boss in the snow. The usual boss leash, attacks, rewards and major-boss arrival remain in force. Phantom Ganon is this map's major boss.

The map has a dry central area for late storm phases, radial and perimeter buggy routes, 32 authored loot sites, and seven open combat areas. Server placement and navigation reject water, steep slopes and authored wall/furniture footprints. Indoor height lookups use the actual raised building floors so bots and loot do not sink through them. Random towns and wild scenery are suppressed for this map. Authored trees replace procedural island scenery, keeping entrances clear. Existing storm, supply drops, allies, bots, gliding, inventory and vehicle rules remain active.

## Authoring and export

Editable source: `assets/maps/hyrule_convergence/hyrule_convergence.blend`; portable textured export: `hyrule_convergence.glb`. Images are packed in the Blender source and embedded in GLB. The art adapts the supplied warm fantasy reference with rounded foliage, worn masonry, timber interiors, castle spires and golden-hour presentation. The game uses 32-pixel RGBA16 material textures to fit the existing renderer; area filtering preserves brick mortar and roof seams. Blender uses 256-pixel textures, a painted ground atlas and relief shading. Blender presentation lighting, including warm interior fill, is not an engine lighting replacement.

Rebuild from the repository root:

```powershell
& 'C:/Program Files/Blender Foundation/Blender 5.1/blender.exe' -b -t 4 --python tools/maps/build_convergence.py
```

The authoring script writes `shared/convergence_data.h` (terrain, collision, navigation footprints and placement data) and `shared/convergence_model.h` (textured draw batches). Source geometry is converted from Blender XYZ to engine `(100X,100Z,-100Y)`. The export checks the engine's 8191 maximum collision vertex index and 16-bit polygon count. Terrain triangles use the same diagonal and heights as the game lookup grid. Spatial draw batches are culled by distance. Buildings are rendered from persistent native vertex/display-list buffers, with no GLB parsing at runtime.

## Validation and handoff

`server/tests/convergence_tests.cpp` checks map selection, stable IDs, collision limits/indices, native draw batches, dry lobby spawning, swimming terrain, every building's open aisle, and all seven boss themes over eight match seeds. It also checks loot placement and returning to Sandbox/Fortnite terrain. It is included in CMake/CI. `tools/maps/run_map_tests.cmd` runs this suite and the existing game-logic/network suites on Windows with Visual Studio 2022.

Final controller and device checks: select the map in both menus; switch among all three custom terrains; walk through every building and inspect furnishings; land on roofs; fight each miniboss; swim at Lake Lantern; drive the radial and ring routes; finish a storm match; leave and rejoin without stale geometry. A headless test or APK build cannot verify camera clearance, frame rate or controller feel.
