# Hyrule Riftlands — scenery prop design

Design by Astra, 2026-10-08. Handoff to Sol for Blender authoring, texturing and map integration. This is an asset specification, not a report of completed models. Read with `../visual-target.png`, `../design.md` and `../implementation-plan.md`. The approved image controls composition and density; this document supplies the close-range objects that make those places feel inhabited.

## Visual rule

**A beautiful old kingdom kept alive by small, practical daily rituals.** Make the recognizable silhouette first, then show use: a worn handrail, an awning repaired with lighter cloth, a rope-darkened post, flowers in a broken stone basin. Three readable layers per cluster: one shaped hero prop, two or three useful companion objects, and one small ground treatment. Detail should accumulate around places where people would actually work or rest.

Ocarina of Time supplies the architectural/material foundation and quiet rural craft. Majora's Mask contributes timekeeping, festival color and slightly curious handmade objects. Wind Waker contributes broad curved forms, confident painted patterns and weathered maritime craft. Fortnite contributes clear silhouettes, legible cover heights and arranged object clusters. Do not add four separate visual identities to each place. No modern crates, shipping containers, vending machines, logos, copied character faces or giant unrelated masks. Keep existing game loot visually dominant.

The reference has golden cream stone, cool cliff shadows, layered vegetation, orange roofs and turquoise water. Preserve that warmth. Most scenery is muted; each POI gets one modest accent. Avoid black contour lines, metallic PBR sheen and microtexture noise. Paint edge wear as broken broad strokes, not bright outlines around every polygon.

## Scale and authoring contract

`H` means the current standing playable Link collision height, measured by the implementer. All dimensions below are width × depth × height in H and are targets. The JSON catalog uses the same order. Reconcile against the actual avatar, camera and buggy dimensions before exporting; do not assume a meters-to-engine-unit conversion.

- Full cover: a substantial opaque body approximately 1.1–1.35H tall. Low cover: 0.50–0.70H. Confirm against current standing/avatar and aiming behavior; do not assume crouching is available. These are design targets, not gameplay measurements.
- Sol reports Link approximately 56 game units high; retain established 160-unit door width, 215-unit door height and 340-unit buggy route width. These project clearances override proportions inferred from Link. Check camera and actual vehicle clearance before export.
- Model bevels, taper, roof curves and major chips. Paint grain, mortar, stitching, shallow relief and most fastenings. Ground litter has no collision.
- Origin at grounded footprint center; local forward −Y, up +Z in Blender. Exporter performs its established axis conversion. Multi-part assembly names use `rf_<area>_<family>_<part>`; decorative and collision objects must be independently identifiable.
- Keep collision simpler than visuals. Register every cover body using existing static collision/obstacle pathways. Do not allocate a nearby dynamic proxy for each pot or lantern. Thin ropes, foliage and cloth are nonblocking. Open passages stay open in collision.
- Decorative wheels do not rotate, mill/quarry machinery does not simulate, lanterns do not require real lights, and scenic boats are stationary. Added animation is optional later work.
- Sol reports common world cover sizes around 150–300 units high and 100–220 units wide. Treat those as current kit constraints rather than automatically scaling every prop that tall. The H dimensions below retain human-scale craft; enlarge major cover masses selectively and verify against the actual camera/avatar before declaring them useful cover. A cover prop is not automatically climbable or a loot spawn. Any approved elevated route needs the existing ground/upper navigation and walkable collision contracts. Only use existing assigned loot candidates.

## Eight POIs: three hero families each

Sol confirmed spatial culling, native 128px material tiles and static kit-box cover collision. Allocate one focal family per POI (cistern, forge, well, cradle, skiff, wagon, sun-ring screen, woodcrib) a richer silhouette. All other primary families target 24–60 triangles: use broad planes, painted relief and merged masses instead of literal reconstruction of every small part. Ranges below are revised production targets excluding invisible collision. Reuse meshes with modest rotation/scale and material variants.

### Crownfall Castle — pale stone, peacock teal, old brass

1. **Water-keeper cistern** (`crown_cistern`, 2.6 × 1.8 × 1.25H, 450–650 triangles). An octagonal cream-stone basin with a thick rolled lip, one cracked corner and a short fluted water spout shaped like a leaf. Its rear has a taller arched slab; the front rim is low. Paint mineral streaks beneath the outlet, moss only at the damp base, and shallow three-leaf relief on two faces. Use a flat turquoise inset for water; no extra simulation. Rear slab is full cover, basin is low cover. Collision follows the basin body without a hidden tall box across its open top.
2. **Teal pennant standard** (`crown_standard`, 0.85 × 0.85 × 2.8H, 24–36 triangles). A squat chamfered stone plinth, tapered wood shaft, little brass finial and one broad forked cloth pennant. Cloth hangs in two sculpted folds, bearing an abstract cream wing/leaf mark. Frayed bottom is a painted edge, not dozens of holes. Only the base blocks movement. This is a wayfinding accent, not combat cover.
3. **Royal terrace planter** (`crown_planter`, 2.5 × 0.75 × 0.68H, 40–60 triangles). A long stone trough with gently bowed end caps, raised corner pads and a chipped carved braid band. Three distinct clipped shrub mounds sit above dark earth, with a few cream flowers. Solid trough is low cover; foliage never conceals a taller collision box. Make one damaged empty variant using the same footprint.

**Arrange:** Pair standards asymmetrically at the gate, leaving the approach open. Place the cistern against the courtyard's garden edge, with an open circulation gap behind the rear slab; never center it on the gate axis. Stagger planters along terrace turns, leaving intentional firing gaps. A bench, bucket and two fallen petals belong beside one cistern only. Repeat the teal pennant at the foot of the plateau ramp to connect cliff and castle visually. Preserve the waterfall view corridor.

### Death Mountain Quarry — warm charcoal, iron red, ember orange

1. **Ore sled and basalt load** (`quarry_sled`, 2.2 × 1.25 × 1.15H, 40–60 triangles). A heavy wooden skid with upturned front runners, two bent iron straps and three angular red-black rock masses rising unevenly above waist height. One copper vein runs through a single large stone. No gold-like yellow sparkle that could read as loot. The rock mass forms full cover; runners do not snag feet. Use a merged solid collision hull at the load, not each pebble.
2. **Bellows forge canopy** (`quarry_forge`, 2.8 × 1.8 × 2.0H, 550–800 triangles). A rounded soot-dark stone furnace beside a broad wedge-shaped leather bellows, under a sloping rust-red cloth roof on two crooked timber posts. Large hexagonal mouth with a painted coal inset; warm vertex color/emissive appearance only if supported. Rear furnace is full cover, the working front remains accessible, and the canopy has generous headroom. No new damage hazard.
3. **Survey gong frame** (`quarry_gong`, 1.8 × 0.65 × 2.4H, 32–56 triangles). Two tapering basalt uprights, a heavy timber crossbar and a broad oxidized bronze disc with three shallow concentric rings. A small red cloth tie gives movement in the silhouette without animation. Posts are narrow obstacles; space beneath/around the gong is not a hidden solid wall. Use it as a quarry entrance marker.

**Arrange:** Put forge and sled on separate sides of a terrace work yard, forming two cover options rather than a single enclosed camp. Gong marks the dry bypass; orange forge marks the work yard. Repeat two sleds with different load orientations on the lower terrace. Add a low ore sorting tray, a cold charcoal spill and one tool rack near the forge. All props sit outside lava pockets and both escape ramps.

### Kakariko Windmill — butter plaster, honey wood, terracotta, faded indigo

1. **Clockmaker's well** (`kakariko_well`, 1.7 × 1.7 × 2.2H, 480–650 triangles). An irregular cream-stone well ring, timber windlass, broad slightly curved orange shingle canopy, and a small weathered twelve-tick sun dial fixed beneath the gable. No moon face. A rope runs to a half-visible wooden bucket; water is dark and shallow-looking. Ring is low cover. Put solid safe collision over the well opening so it is scenery, not an accidental pit; canopy posts have honest narrow collision.
2. **Orchard handcart** (`kakariko_cart`, 1.9 × 1.05 × 1.2H, 40–60 triangles). Two broad six-spoke wooden wheels, a bowed plank bed, two long resting handles and stacked woven baskets beneath a short indigo cloth drape. Apples are three clustered low-poly masses with a few distinct fruit silhouettes. Basket/cart body provides full cover where it reaches 1.1H; handles are nonblocking. Do not stack onto roof access lanes.
3. **Festival notice shelter** (`kakariko_notice`, 1.8 × 0.5 × 1.9H, 32–56 triangles). A small arched wooden board under a crooked tiled cap, three cream paper patches and a strand of muted triangular fabric tabs. Use tiny abstract ink marks, not readable quests or invented UI instructions. One attached low stone seat at the side anchors it to the ground. Board can be shot-blocking if collision matches, but it is too narrow to count as a cover cluster alone.

**Arrange:** Build a crooked village triangle: well at the lane widening, cart at an orchard gate, notice shelter against a house flank. Preserve straight passage through the lane and the approach to the windmill. Blue laundry appears once above an alley at camera-safe height. Low flower pots, a bench and stacked split wood stay against walls. Paint a compact worn cobble patch around the well rather than scattering identical stones everywhere.

### Deku Hollow — chestnut bark, moss jade, warm straw, tiny amber lights

1. **Root cradle shelter** (`deku_cradle`, 3.0 × 1.6 × 1.65H, 480–700 triangles). Three curved branching roots arch around a woven reed seat-platform with a small overlapping leaf roof. The thickest side root becomes full cover; the front remains open and the roof curves away from the camera. A few large carved spiral cuts echo growth rings. Keep the floor at ground height or on a navigable shallow ramp. Avoid narrow hollow tubes requiring complicated internal collision.
2. **Acorn granary** (`deku_granary`, 1.55 × 1.55 × 2.0H, 48–60 triangles). A stout faceted wooden barrel-house with a deeply fluted bark body and broad acorn-cap thatch roof; raised on three short splayed feet. One small dark hatch and a rope band imply storage, not an enterable room. Lower body is solid full cover. Close off the tiny foot gaps visually and in collision to avoid false crawl passages.
3. **Firefly lantern crook** (`deku_lantern`, 0.8 × 0.7 × 2.1H, 24–40 triangles). A bent sapling pole with a hanging seed-pod lantern, four wood ribs and warm amber inner faces. One broad leaf at the bend and a wrapped rope join. Use a baked bright center with dark rim, no transparent glass or point light requirement. Decorative except for the short root base.

**Arrange:** Cradle shelters sit at broad root-path junctions, not in the middle of a footbridge. Two granaries flank a small village storage court with an offset gap and a clear rear exit. Lantern crooks trace two grounded paths to the main tree; do not line every edge like a runway. Add fern clumps, a reed mat and a basket only near a porch. Use alternating canopy openings and darker root pockets so the floor still reads from above.

### Lake Lantern — sunbleached wood, turquoise, sea-green paint, ochre sailcloth

1. **Reef skiff on cradle** (`lake_skiff`, 2.8 × 1.2 × 1.05H, 450–650 triangles). A broad crescent hull with a turned-up prow, simple carved leaf curl and two pale painted stripe bands. Rest it visibly on two low shore blocks, with a bundled ochre sail inside. The hull side is low/full transitional cover; use its actual opaque height. This stationary shore prop is not a usable vehicle. Keep playable collision out of the thin pointed prow.
2. **Net mender's rack** (`lake_netrack`, 2.2 × 0.85 × 1.9H, 36–60 triangles). Two angled posts and a horizontal spar hold a sagging folded teal net; add three large wooden floats and a low plank workbench. Paint net diamond marks onto opaque folded cloth strips; no expensive transparent net lattice. Bench and bundled fishing baskets provide low cover; hanging net is visually thin and nonblocking, so never label this full cover.
3. **Lantern mooring bollard** (`lake_bollard`, 0.8 × 0.8 × 1.6H, 24–40 triangles). Three uneven weathered dock timbers bound by thick rope bands; a crooked arm holds a squat amber lantern under a little teal cap. Rounded rope coil on the deck uses few broad segments. Timbers are narrow physical posts, lantern and coil decorative.

**Arrange:** Shore skiff and net rack form a small work beach beside the first boardwalk junction. Mooring lanterns mark turns and shore exits, with darker gaps between lights. Put fishing baskets at pier widens, never along both edges of a narrow bridge. One empty skiff variant at a secondary shore tells the same story without extra landmark competition. Keep all dry escape routes and the two lake crossings clear.

### Lon Lon Crossroads — red ochre, cream linen, wheat gold, worn green

1. **Hay wagon** (`ranch_wagon`, 2.5 × 1.5 × 1.4H, 420–600 triangles). A broad red-painted plank chassis on four heavy wheels carries two uneven rounded hay bundles under crossed rope. Exposed hay ends use broad scalloped geometry and painted stalk strokes. Main stack provides full cover; a low loose bale gives a separate 0.6H option. No collision on hitch shafts. Wagon does not imply driveable behavior.
2. **Milk stand shelter** (`ranch_milkstand`, 2.1 × 1.3 × 1.85H, 40–60 triangles). A small cream scalloped awning on split timber posts over a worn green counter, with three squat ceramic milk churns and a folded cloth. Broad hand-painted leaf/cowbell symbol only, no readable shop UI. Counter is low cover. Leave the back open for ally/service placement if an existing service is assigned here.
3. **Orchard trough and hitch** (`ranch_trough`, 2.3 × 0.7 × 1.25H, 36–60 triangles). A hollowed timber trough with fat rounded end grain sits below an asymmetric hitching beam and a hanging small brass bell. Water is a flat dark green inset. Trough is low cover; upright ends can block movement, the upper open space remains open. One moss-dark patch marks drips.

**Arrange:** Wagon sits at a bend just outside the barn, oblique to the road so the load interrupts the long field sightline. Milk stand faces the crossroads from a widened pull-off. Trough rests at a fence break with enough room to drive past. Fence sections, loose bales and orchard trunks build two staggered cover routes through golden paddocks; preserve large deliberate gates. Add a stool and feed sack at the stand, not random sacks across fields.

### Spirit Bazaar — honey sandstone, dusty rose, oxidized turquoise, indigo

1. **Sunshade merchant stall** (`spirit_stall`, 2.6 × 1.7 × 2.0H, 48–60 triangles). Two unequal sandstone footings support curved wood poles and a sloping indigo canopy with one faded rose repair panel. A low stepped display plinth holds three nested baskets and two large turquoise-glazed vessels. The plinth is low cover; one closed storage jar group reaches full cover only if visibly opaque and large enough. Open front/back permit circulation. Cloth stitches and diamond banding are texture work.
2. **Broken sun-ring screen** (`spirit_screen`, 2.7 × 0.75 × 1.55H, 420–600 triangles). A substantial low sandstone wall rises to a partial circular disc at one end; two broad chipped sun-ray notches break the upper silhouette. A darker recess and turquoise pigment fragments imply an old relief. Tall end is full cover, low end low cover. Do not create apparent arrow slits that collision closes. This is a ruin fragment, not a repeated arch from the building kit.
3. **Oasis water urn** (`spirit_urn`, 1.25 × 1.25 × 1.5H, 36–60 triangles). A generous pear-shaped clay vessel on a stepped square pedestal, with thick rolled rim, short pouring lip and two stubby loop handles. Matte ochre body carries a broad worn teal wave band. A small shallow catch bowl and darkened drip patch sit at one side. Urn body is full cover; handles and bowl decorative. Do not make it destructible unless the existing gameplay system supports it deliberately.

**Arrange:** Two stalls turn inward around an open lane, with their backs accessible; offset the sun-ring screen to split the courtyard into two flanks. Place urns at the oasis and at the cool side of the main gateway, connecting water and commerce. Keep the gateway axis broad. Palms cast implied painted shade near seats; carpets lie flush and never obscure steps or loot. Small pot groups use at most three silhouettes.

### Frostwatch Lodge — blue-gray stone, dark cedar, snow cream, amber and brick red

1. **Cedar firewood crib** (`frost_woodcrib`, 2.3 × 1.15 × 1.35H, 350–500 triangles). A sturdy A-frame rack with split log ends, angled brace boards and one steep cedar-shingle roof carrying an asymmetrical snow cap. Main log mass is full cover. Model six large visible log ends; use a painted dark end-grain panel deeper inside instead of dozens of cylinders. Snow cap overhangs minimally and has no separate collision.
2. **Trail bell cairn** (`frost_bell`, 1.4 × 1.0 × 2.2H, 36–60 triangles). A chunky stacked-stone base supports two short carved timber uprights, a deep red crosspiece and a broad tarnished bell beneath a tiny snow-covered roof. One faded triangular cloth route pennant hangs below the roof line. Stone base is low cover; upper open frame remains open. No new interactable or audio requirement.
3. **Rescue sled cache** (`frost_sled`, 2.2 × 1.1 × 0.85H, 40–60 triangles). Long curved runners carry two lashed travel trunks, a rolled cream bed blanket with a red stripe and a folded wooden snow shovel. Broad sled body provides low cover. Curved runner tips and rope tails are nonblocking. This is a mundane supply cache, visually distinct from gameplay supply drops/chests.

**Arrange:** Woodcrib sits outside the warm lodge wall, leaving both lodge exits free. Sled is parked at the lower terrace landing with a rear walk-around route. Bell cairns mark the two outdoor slopes, with a spare broken cairn in the pine belt. Use snow-dusted stones, pine needles and one hanging coat-shaped blanket near the porch. Snow sits on upper faces and recedes around footpaths; avoid frosting every face white.

## Center and secondary landmarks

Use these as small extensions of the same kit, not six new POIs.

- **Temple of Time: fallen rose-window segment** (`temple_window`, 3.0 × 1.0 × 1.35H, 550–800 triangles). Three thick cream-stone radial ribs lie on a fractured semicircular plinth; one blue-green inset shard is opaque dull stone/glass color, not glowing loot. Tall broken side is full cover; center depression stays open only if visually and physically passable. Set two at different rotations outside the main ruin, with a stone bench and sparse white flowers. Preserve several exits across the central field.
- **Temple/roadways: pilgrim waystone** (`waystone`, 0.85 × 0.65 × 1.4H, 24–40 triangles). Tapered rounded stone slab with a shallow leaf-and-path relief, one moss shoulder and a faded teal inlay. Use at route forks with a low adjacent boulder, never as a dense wall. Pair it with existing ruin walls/arches to build dispersed cover clusters.
- **Fairy Spring: petal basin** (`fairy_basin`, 2.0 × 2.0 × 0.75H, 40–60 triangles). Five broad stone petal lobes around a small turquoise inset pool; moss and cream flowers gather only on the rear side. Low cover at the perimeter, safe ground below. Reuse the castle stone and basin water materials. Ring with three existing rocks and a partial fern crescent; leave an open approach and rear exit.
- **Zora's Falls: scalloped spill marker** (`falls_marker`, 1.5 × 0.7 × 1.7H, 48–60 triangles). A pale bluish stone fin curves upward from a thick base, with three scalloped ridges and one blue wave inlay. Place a pair at an overlook set back from the waterfall edge, with a low bench/rock; do not block the falls themselves or introduce a slippery route.
- **Goron Lookout:** reuse the quarry gong and ore sled with one broad stone sitting block. A low semicircle of three existing basalt boulders frames the distant volcano; keep the middle clear and both approach paths open.
- **Lakeside lab:** reuse net rack and bollard; add a **tide gauge** (`lab_gauge`, 0.65 × 0.55 × 2.0H, 24–32 triangles): sea-green plank staff with broad cream tick marks, a copper ring and little roof cap. Ground it at the accessible shore, not in required deep water. A shelf of three opaque colored sample jars can be part of the rack material/mesh variant.
- **Graveyard annex:** a **leaning bellflower memorial** (`grave_memorial`, 1.1 × 0.6 × 1.5H, 32–48 triangles), a rounded stone tablet under a shallow hood, shallow carved bellflower, moss base and a single faded ribbon. No text, skeleton pile or copied mask face. Three staggered tablets and two low slabs frame the annex path, preserving its bypass. Add full cover only through nearby genuine wall/boulder mass.

## Shared detail kit and density

Build once: three rock silhouettes; two shrub mounds; broad fern clump; low flower patch; ceramic pot in two profiles; reed basket; wood bucket; folded cloth; stool/bench; short split-wood pile; one rope coil; simple fence segments including broken end; broad flat soil/cobble/scuff patches. Existing equivalents should be reused before creating new assets.

Near a hero cluster: 1 hero + 1–2 companion props + 2–4 small details + 1 grounded material patch. At distance: retain hero and one companion; discard the tiny details. Start with 3–5 deliberately composed clusters per POI, adjusted to actual POI size and existing buildings. Do not place every family at every doorway. A second hero instance should differ by orientation, load variant or palette accent, not arbitrary giant scale.

Use the approved image's stepped terrain and buildings to carry richness at island scale. Props enhance those compositions; hundreds of little loose items cannot substitute for cliff strata, terrace walls, connected bridges, roof variety or tree canopy mass.

## Materials, texture packing and renderer handoff

The current Kingdom documentation describes hand-painted 128 × 128 material textures and lit exterior stone/wood/roof/thatch/cobble/plaster/bark with optional surface detail. Author to that established pipeline first. Confirm the target checkout's exporter before adding material slots; this design does not assume Blender shaders survive export.

- Existing compatible materials confirmed by Sol: bark, planks, timber, castle_stone, moss_stone, town_stone, sandstone, iron, roof_red, roof_blue, roof_green, thatch, cloth, gold, hay, leaves, window_lit, water, foam, lava_glow. Prefer these names and vertex tints. Shared base palette: cream stone `#C8B88F`, shadow stone `#6E7C79`, honey sandstone `#C89658`, basalt `#544847`, cedar `#765138`, pale wood `#B79464`, terracotta `#B86B42`, teal `#347D79`, indigo `#4B6079`, moss `#68854B`, amber `#DCA44D`, snow `#D9E4E2`.
- Reserve high saturation and bright warm values for a few cloth/lantern accents. Keep backdrop props below loot brightness and avoid rarity-colored outlines.
- Reuse existing stone, wood, bark, thatch and roof tiles. Proposed additions only when existing tiles cannot serve: painted cloth, woven goods, aged bronze/iron, glazed pottery. Each remains a small opaque tile compatible with the current texture lookup, not a new PBR material graph.
- Author a logical 2 × 2 palette sheet for cloth/weave/metal/pottery if useful in Blender, but export as separate compatible 128px tiles unless atlas rectangles are already supported. A large atlas is optional packaging, not an integration prerequisite. Provide a tile/material mapping manifest so engine output and Blender preview match.
- Paint grooves and motif bands at readable scale. A cloth patch should occupy a meaningful UV area; tiny elaborate runes will disappear. If textures use nearest sampling, avoid diagonal one-pixel embroidery. Include padding where exporter filtering needs it.
- Bake broad local occlusion into vertex colors: darker under roofs, between bundles, at stone feet. Preserve enough midtone range for the runtime light. Use material tint/vertex colors for snow and biome variants instead of multiplying new texture identities.
- Lantern/coal centers are opaque warm surfaces. Avoid alpha-blended glass, mesh net transparency, multilayer glow cards, dynamic point lights and shader-only wind. A render-only light may help presentation, but its absence in runtime must be reviewed explicitly.

## Geometry, LOD and placement budget

The eight focal families total roughly 3.6–5.1k unique triangles; the sixteen primary support families add approximately 0.6–1.0k. Optional landmark families add approximately 0.7–1.05k. Catalog totals are 4,894–7,078 unique triangles before shared detail kit geometry and instantiated expansion. These are design estimates; measure final exported counts. Instance count and combined visible geometry matter more than unique mesh count. The catalog is a menu of bounded families, not permission to exceed current packed index, collision, obstacle or render limits. Measure exporter output before choosing final density.

- Medium mesh: about 45–60% of near triangles; merge small baskets/logs, simplify wheels and folds, remove ropes/handles and hidden undersides. Far: about 15–25%, retaining distinctive outer shape and broad color blocks.
- If runtime mesh LOD is unavailable, author near silhouettes cheaply and use existing spatial culling/batching. Do not add a new engine LOD system just to meet this brief. Keep tiny-detail groups separately cullable through supported pathways.
- Decorative triangle detail never changes the physical cover height. LOD/culling cannot remove a still-visible gameplay-relevant cover body while leaving its collision invisible.
- Repeated near meshes should share mesh/material data in Blender. If the exporter bakes all instances, record the expanded count and reduce placement density as needed.
- Place on the actual terrain normal/height; bury stone feet slightly, keep sled/boat supports touching ground and avoid floating grass. Limit tilt on engineered assemblies; stones and memorials can lean deliberately.
- Cluster bounding boxes are not collision boxes. Open stalls, cradles, pergolas and well canopies need individual solid members or simplified honest contours, with clear walkable gaps.

## Implementation order and review

First visual slice: cistern + planter; hay wagon + milk stand; skiff + net rack + bollard. These test stone/wood/cloth, full/low cover, terrain grounding and the castle–ranch–lake vistas in the approved image. Then finish Kakariko/Deku, quarry/bazaar/frost, and center/secondary accents. Existing building work continues independently.

For each family provide reproducible authoring source, named mesh/material mapping, dimensions, origin and a deliberately chosen collision policy. Keep source editable in Blender and export through the existing map pipeline. An attractive Blender render alone does not verify runtime materials or collision.

Review one eye-level composition and one aerial view for every POI. Check: recognizable hero silhouette; three layers of detail; grounded objects; clear doorways; both flanks/three exits maintained; no new loot-like false cues; no opaque decorative net pretending to be open cover; no hidden wall in an open frame. Check collider overlay and bot/vehicle clearance on changed routes. Report generated render/export counts and required tests. Runtime visual matching and sustained performance still require Odin validation.

Companion file: `prop-catalog.json` supplies IDs, target dimensions, near triangle ranges, cover class and basic collision policy for implementation bookkeeping. This brief controls appearance and placement when a terse catalog field omits nuance.

Visual companion: prop-silhouette-plate.svg shows the eight focal shape/color designs. It is a stylized design diagram, not a Blender or runtime render.
