# Hyrule Riftlands

Riftlands is a separate authored map (ID 11). Existing maps and IDs remain available. The approved image and Astra prop brief are in `map-concepts/hyrule-riftlands/`; the executable source is `tools/maps/riftlands/`. The scene uses the existing Kingdom material, open-interior, collision, navigation and native rendering contracts.

Eight primary places surround an inner field: Crownfall Castle, Death Mountain Quarry, Kakariko Windmill, Deku Hollow, Lake Lantern, Lon Lon Crossroads, Spirit Bazaar and Frostwatch Lodge. Temple ruins, the graveyard, fairy spring, lab, river overlooks and recruitment shelters remain secondary places. Curved perimeter/radial roads, graded shelves, three wide stone spans and independent shore ramps connect the island. Riftlands buildings retain 240-unit door widths for rotated bot-grid and camera clearance. Ordinary floors are 170 units high with 140-unit lintels; the castle keep and great lodge retain monumental dimensions. Four loft buildings use 160-unit stairs and larger stairwells. Shared Kingdom defaults remain unchanged.

## Adult Link scale correction

Normal player actor scale is 0.01. The 1.35 multiplier belongs to temporary Adult Power, not ordinary adulthood. The engine adult age properties specify 56-unit ceiling checks and an 18-unit wall radius. These are verified gameplay clearances, not the full animated mesh/hat bounding box. The player's actual collision cylinder is derived from animated foot/head positions in the engine; no ROM model is bundled here.

Ordinary houses previously used 270-unit storeys and 215-unit lintels. Interior tables were 80 units high, stools 42 and beds 196 units long including their headboard. They are now 170/140 for houses, 32 for tables, 16.8 for stools, and 78.4 for bed length. Shelves are 68 high. Furniture drawing, static collision and nearby collision proxies are scaled together around each item's floor anchor; building ground floors, doors, roads, bridges and terrain positions remain fixed. Lower ordinary roof rises and smaller furnishings reduce the oversized impression and open up interiors without crowding routes. Landmark trees, castle spires, public hall and battle-cover props retain their intentional larger silhouettes.

Run `python tools/maps/riftlands/scale_audit.py` to measure the generated furniture and verify that shared defaults are restored. Its `scale-audit.json` and `scale-comparison.svg` show engine-unit proportions. The playable diameter is 13,000 units (about 232 adult ceiling-clearance heights), so an aerial image alone is not a player-scale view. Final rendered-avatar and camera confirmation still requires an Odin playtest. Protocol 33 prevents peers using different scale/collision data from sharing a match.

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

The Riftlands suite checks stable IDs/POI names, collision packing/normals, render batch bounds, water levels, ramp slopes, dry routes, doors, 72 ground sites and 12 optional upper/climbing sites, themed encounters, storm centers and terrain switching. The source/package guard protects first-load collision initialization from a stale `gPlayState`, resets elevated water boxes on map switching, and checks the prop manifest and GLB.

Offline checks do not establish Odin visuals, camera behavior, collision feel, sustained frame time or memory. Final device testing must cover every primary route and interior, both shore exits, glider landing, buggy crossings, swimming through each raised pool, weather visibility and a full match under actor load.

A bounded two-seed offline sample used `royale_balance 2 normal 11 8` and `royale_balance 2 normal 11 32`. The initial 32-player sample averaged 376 seconds, first elimination 24 seconds and 47 opened chests; the same two Kingdom runs averaged 431 seconds and first elimination 24 seconds. Eight-player Riftlands averaged 427 seconds with the first elimination after 275 seconds and one player elimination per match. Every sampled match retained a survivor. Small lobbies were quiet; two seeds and simulated bots establish a flow smoke check, not validated balance or device performance. A final 32-player sample is recorded after the final geometry rebuild.

Final validation: all 19 CTest suites passed after fixing procedural POI name offsets to use `PoiNameBase`. Release compilation, native mod syntax/patch checks, mod layout, Kingdom authoring, water-buffer bounds and Riftlands source/packed-asset checks passed. Final two-seed 32-player simulations averaged 419 seconds, first player elimination at 27 seconds, 28 survivors at one minute and 48 opened chests. Both matches had a survivor. Ten storm eliminations per match in this small sample remain a device/playtest balance concern rather than a claim of final tuning.

Saved artifacts: `assets/maps/hyrule_riftlands/hyrule_riftlands.blend`, `hyrule_riftlands.glb`, `overview.png`, `gallery.jpg`, `prop-sheet.png`, textures and placement/export/model manifests. The approved concept was generated with built-in ImageGen; its exact prompt is saved in `map-concepts/hyrule-riftlands/render-prompts.txt`. Geometry is a deliberate low-poly adaptation. Some close-up preview framing and interior exposure remain imperfect; judge runtime visuals and camera behavior on the Odin.
