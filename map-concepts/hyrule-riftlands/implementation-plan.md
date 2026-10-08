# Approved implementation plan

The user approved visual-target.png and requested GPT-6.1 Sol to build this map in Blender, integrate all existing game elements, complete their earlier fix list, and merge after verification. Preserve the image's small details, not just biome placement. Read design.md for gameplay intent.

## Visual acceptance checklist

- Castle: teal pointed spires, pale crenellated walls, gate arch, courtyard, gardens, terrace retaining walls, visible cliff strata, connected ramps and a waterfall beneath the plateau.
- River: stepped pools and waterfalls from castle through Kakariko to the lake; banks, rocks, turquoise shallows and foam accents. Waterfalls need deliberate geometry/effects compatible with the engine.
- Bridges: multiple pale stone arched spans, parapets, piers and bridge-end towers where shown. Deck collision, rail clearance and ordinary foot/buggy access matter as much as appearance.
- Kakariko: clustered orange/ochre roof houses, pale plaster, timber trim, village lanes, a tall windmill, gardens, stairs, fences and river overlook. Houses have furnished accessible interiors.
- Deku Hollow: immense sculpted trunk, roots and spreading canopy; tiered timber homes, porches, rope/wood footbridges and lantern accents. Provide grounded ramps/paths; no essential route depends on a new traversal feature.
- Ranch: fenced golden paddocks, barns, farmhouse, orchard and hay. Fence breaks and lanes allow vehicles and flanking.
- Spirit Bazaar: substantial sandstone ruins, tall gateway, columns, stepped terraces, shade awnings, palms, pots and small oasis. Preserve readable open aisles.
- Frostwatch: snowy mountain silhouette, layered pines, warm timber lodge, stone foundation, terraces and graded snow transition.
- Volcano: readable cone, glowing fissures and crater, basalt terraces, forge/quarry structures and side-pocket hazards rather than mandatory lava routes.
- Center: rolling green plateau, broken Temple of Time silhouette, distributed arches, walls, cobble remnants, boulders, flower/grass patches and small groves; retain multiple sightline breaks and routes.
- Lake: turquoise bay, rocks, shoreline, connected stilt village with huts, pitched roofs, boardwalks, posts, railings, dock furnishings and shore exits. Keep underwater/shore collision consistent with rendered water.
- Entire map: coherent worn materials, layered terrain rather than flat biome discs, authored prop clusters, biome transitions and scenic vistas. Fine details are instanced/shared or culled; distant silhouette meshes can carry expensive scenery.

## Work phases and completion gates

1. Inspect the current integration branch and attached worktrees, identify the user fix list and ensure unrelated dirty work is preserved. Use an isolated managed worktree for implementation when appropriate; baseline from current integration state deliberately.
2. Inspect existing Kingdom Blender/Python kit and export pipeline. Reuse engine contracts and tested features; author a separate map without replacing existing IDs/assets. Choose dimensions from actual movement/storm data.
3. Greybox terrain, routes, islands, bridges and POIs. Export collision/navigation early. Every important POI and loot site needs reachable paths, usable entrances and escape routes.
4. Model all visual checklist details in Blender through reproducible authoring scripts. Save a packed .blend and portable .glb, plus aerial and ground-level renders. Compare to visual-target.png with visual inspection; fix flat terrain, missing props, poor transitions and inaccessible structures.
5. Integrate stable map ID, selectors, minimap/name regions, water, terrain collision, static/proxy obstacles, ground/upper navigation, chest candidates, glider/drop/storm configuration, buggy routes, allies and optional themed bosses. Reuse weather, supply drop and major-boss rules; add map-specific configuration only where needed.
6. Add meaningful geometry/gameplay validation for route connectivity, collision limits, loot reachability, interiors, bridges, water and boss placements. Run required mod-layout/source checks, server suites and relevant compile/build validation. Verify packaged generated map assets.
7. Complete the earlier user fix list once located, distinguishing already completed items from pending ones. Do not invent its contents.
8. Review diff, create/attach PR if needed and merge to the verified integration branch after required checks. User has authorized merging. Preserve unrelated local changes. Never claim Odin device performance or collision feel from offline checks.

## Required reporting

Report saved design/model/render files, tests and build results, integration/PR/merge status, and any remaining device validation. Document practical renderer compromises against the reference. This plan is a target; completion must be evidenced by generated models, integrated code and passing checks.
