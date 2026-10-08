# Hyrule Riftlands

Riftlands is a separate authored map (ID 11). Existing maps and IDs remain available. The approved image and Astra prop brief are in `map-concepts/hyrule-riftlands/`; the executable source is `tools/maps/riftlands/`. The scene uses the existing Kingdom material, open-interior, collision, navigation and native rendering contracts.

Eight primary places surround an inner field: Crownfall Castle, Death Mountain Quarry, Kakariko Windmill, Deku Hollow, Lake Lantern, Lon Lon Crossroads, Spirit Bazaar and Frostwatch Lodge. Temple ruins, the graveyard, fairy spring, lab, river overlooks and recruitment shelters remain secondary places. Curved perimeter/radial roads, graded shelves, three wide stone spans and independent shore ramps connect the island. Buildings use shared 160-unit door openings and 215-unit lintels; adult visual scaling remains the game's existing 1.35.

The approved rendering is adapted to the game's low-poly, tiled-material renderer. The model preserves its teal castle spires, pale walls, terraces and gardens; snowy lodge peaks; volcanic crater/quarry; plaster village/windmill; branching root homes; golden fenced paddocks; substantial sandstone ruins/oasis; central broken masonry; stepped cascades and turquoise stilt village. Shared textures and spatial batches carry the detail. Grass/flower patches, worn stone, shore rocks and named daily-craft prop families support the landmarks. The packed Blender file remains editable; the GLB includes portable scenery and embedded textures, while the game consumes generated native headers.

Water levels are explicit rectangular engine boxes with meandering rendered banks inside them. The four raised pools and ocean agree across swimming, raw floor validation, navigation and the engine collision header. Waterfalls use opaque textured ribbons and foam accents. Blender's ocean tint is a presentation gradient; the game uses its existing depth, foam, wave and weather controls. These are deliberate renderer adaptations, not a claim that a concept image is a runtime screenshot.

The 84 authored chest candidates comprise 48 starter sites, 24 secondary/route sites and 12 upper-room sites. Riftlands does not add the old random chest pass on top. Existing rarity rolls, inventory, heart pieces, boss rewards and supply drops remain active. Seven optional side arenas retain existing themes and the intentional ChuChu substitution rule. `BossKind::DragonForest` is the existing Phantom Ganon major boss. Four shelters use existing ally hiring; four dry stations use the existing buggy system. Existing gliding, climbing, combat, storms, weather, companions and player rules apply through the normal match flow.

## Rebuild

Run each authoring script in its own Python/Blender process; it selects Riftlands modules before loading the shared kit.

```text
python tools/maps/riftlands/export.py
blender --background --python-exit-code 1 --python tools/maps/riftlands/blender_build.py
blender --background --python-exit-code 1 --python tools/maps/riftlands/prop_sheet.py
```

The exporter requires NumPy. Blender 5.1 provides its bundled NumPy. Generated data/model headers are outputs; edit the authoring files and regenerate them. Materials are listed per named prop instance in `assets/maps/hyrule_riftlands/placement.json`, alongside roads, water boxes, encounters, recruitment and vehicle stations. The export/model reports record measured geometry and packed assets. The Blender source includes hidden editable collision; collision is excluded from the scenery GLB.

## Verification

```text
python scripts/check_mod_layout.py
python scripts/test_kingdom_authoring.py
python scripts/test_riftlands_integration.py
cmake -S server -B build/tests -A x64
cmake --build build/tests --config Release
ctest --test-dir build/tests -C Release --output-on-failure
```

The Riftlands suite checks stable IDs/POI names, collision packing/normals, render batch bounds, water levels, ramp slopes, dry routes, doors, chest reachability, themed encounters, storm centers and terrain switching. The source/package guard protects first-load collision initialization from a stale `gPlayState`, resets elevated water boxes on map switching, and checks the prop manifest and GLB.

Offline checks do not establish Odin visuals, camera behavior, collision feel, sustained frame time or memory. Final device testing must cover every primary route and interior, both shore exits, glider landing, buggy crossings, swimming through each raised pool, weather visibility and a full match under actor load.
