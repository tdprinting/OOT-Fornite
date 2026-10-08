# Hyrule Riftlands — map concept

An evolution of the existing Hyrule Kingdom and Convergence plans. Zelda supplies the places, materials and atmosphere; Fortnite Zero Build supplies readable landmarks, landing choices, loot pressure and rotations. This is a proposal, not an implemented map or measured balance result.

## Layout

Eight primary POIs around a traversable central field. Preserve familiar places as smaller landmarks rather than making all 24 places compete for attention. One continuous scene, furnished buildings, authored scenery and loot. The map diagram is schematic: positions and proportions are proposed, not coordinates from current map data.

| POI | Visual identity | Fight and rotation design | Encounter idea |
|---|---|---|---|
| Crownfall Castle | Pale stone, teal roofs, banners, waterfall terrace | Busy drop; courtyard, ramparts and rear gardens; three ground exits; every powerful perch needs a walkable flank | Stalfos |
| Death Mountain Quarry | Red basalt, forge awnings, lava glow | Terraced combat bowl; keep lava in readable side pockets; two ramps and a bypass road | Magma Dodongo |
| Kakariko Windmill | Plaster houses, orange roofs, windmill silhouette | Medium drop; alley, roof and orchard routes; graveyard sits outside the main traffic lane | Dead Hand in graveyard annex |
| Deku Hollow | Huge tree silhouette, emerald canopy, timber homes | Quieter drop; broad forest lanes and roots as cover; canopy openings keep gliding readable | Moss Lizalfos |
| Lake Lantern | Turquoise bay, stilt homes, warm dock lanterns | Shore, boardwalk and water routes; two separated crossings plus dry shoreline detour; no required swim to leave | Big Octo |
| Lon Lon Crossroads | Golden paddocks, red barns, wind-bent grass | Broad buggy access; fences, hay and stone walls break firing lanes; ramps into lofts | Suggested ally station, no mandatory boss |
| Spirit Bazaar | Sandstone arches, woven awnings, small oasis | Courtyard flanks, low dune cover and open colonnades; dunes blend into grass | Iron Knuckle |
| Frostwatch Lodge | Snow-tipped pines, blue shadows, amber windows | Compact lodge fight; two outdoor slopes and accessible indoor upper floor | White Wolfos |

Temple of Time ruins sit off-center in the central field. Add Fairy Spring, Zora's Falls, Goron Lookout, a lakeside lab and the graveyard as secondary landmarks. Clock Town and Ordon influences can be optional variants; they are broader Zelda references, not Ocarina of Time locations.

## Feature fit

The chosen checkout's feature index and Kingdom/Convergence documentation describe gliding, swimming, climbing, buggies, weather, allies, rarity loot, supply drops, minibosses and a major boss. These inform the concept. Older PLAY/DESIGN status statements conflict with newer documentation; this concept does not establish runtime readiness.

- Gliders: distinct silhouettes and roofs with landing space. Avoid roofs that are strong positions with no ordinary way to reach them.
- Buggies: a continuous perimeter road, radial connections and a dry inner bypass. Check bridge width, turn radius and camera clearance against the actual vehicle.
- Bots: ground ramps and authored walkways to every essential loot area. Optional climb shortcuts supplement a normal route. Swimming and upper-layer connectivity need verification in the target checkout.
- Loot: start from Kingdom's documented 84 authored sites, redistributing rather than automatically increasing them. Proposed split: 48 across eight POIs, 24 at smaller landmarks/route cover, 12 at exposed or upper-floor sites. Sites are placement candidates; active counts and rarity require balance testing. Keep starter gear accessible without fighting a boss.
- Bosses: seven themed optional arenas beside POIs, each with two escape paths and a bypass. Retain existing leash, rewards and match timing. Phantom Ganon is a proposed major-boss choice inherited from Convergence, not a confirmed Kingdom rule.
- Allies: proposed recruitment shelters at ranch, forest, bazaar and lake. Use existing hire rules; avoid putting services inside a boss arena.
- Storm: a broad dry interior with dispersed cover offers varied late circles. Do not funnel every match onto the temple. Validate storm placement against actual safe ground, accessible exits and cliffs.
- Weather: existing effects enrich biome identity; landmark silhouettes and route edges must remain legible under fog, ash and rain.
- Interiors: at least two usable entrances, clear central aisles, camera-friendly ceiling height, ramps to upper floors and no scene transitions.

## Spatial targets to test

Use traversal time before committing to map dimensions. Initial targets, not measurements: 20–35 seconds on foot between neighboring cover clusters/secondary destinations, 45–70 seconds between major POIs, and 15–25 seconds to reach an alternative exit from a courtyard. Adjust to real run speed and intended match length. Large fields need multiple small cover clusters within those journeys; do not interpret the destination timing as an uncovered sprint.

Each primary POI gets three ways out, two independent flanks and a quieter outer landing pocket. Every river bottleneck gets an alternate crossing or a practical land detour. Raise castle and quarry enough for silhouette, while retaining low approaches that do not force uphill duels. Terrain ridges interrupt cross-island sightlines.

## Art direction

Warm painted fantasy: sculpted foliage, worn stone, readable roof colors, turquoise water and small warm light accents. Keep saturation strongest at landmarks and loot. Bridge biomes through foothills, pine belts, damp grass and sandy scrub rather than abrupt seams. Zelda architecture remains dominant; the battle royale influence is spatial clarity and encounter pacing.

The generated images are aspirational concept renders. Their geometry, water, foliage density and lighting are not a claim about what the current renderer supports. For the Odin, prioritize strong silhouettes, shared material kits, spatially culled batches, modest interiors and expensive water effects concentrated near shore. No FPS claim is made.

## Build and review sequence

1. Greybox the route network, terrain shelves, river and POI bounds. Test on-foot and buggy travel.
2. Author collision and navigation with terrain. Check roofs, stairs, doors, bridges, water edges and boss escape routes before detailed decoration.
3. Place loot and encounters; simulate varied seeds and small/full lobbies. Measure landing distribution, first contact, storm deaths and boss interruption.
4. Build the shared house/ruin/tree kit, then finish castle, ranch and lake as the first visual slice.
5. Profile a full match on Odin: frame times, water views, actor load and sustained memory. Device testing determines density and effects.

## Deliverables and handoff

`concept-board.html` contains two editable SVG diagrams: island rotations and a sample Kakariko fight layout. Generated render files and exact prompts are saved alongside it when available. No gameplay code or preexisting map assets were changed. Review is documentary and visual; no map has been exported or device-tested.

Sources: `../../mod-feature-split-worktree/docs/HYRULE_KINGDOM.md`, `HYRULE_CONVERGENCE.md`, `PLACEMENT.md`, `MAP_WATER_PERFORMANCE.md`, and `mod/Royale/FEATURES.md` in that checkout.
