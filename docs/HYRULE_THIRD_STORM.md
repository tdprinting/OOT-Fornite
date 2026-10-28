# Hyrule Before the Third Storm (map 15)

Battle Royale map id 15 (`shared/map.h` `kHyruleThirdStormMapIndex`), the biggest map: all of Hyrule built in Blender at full size, 1.8 km
across (36 game units a metre), in the release map list beside Great Hyrule Kingdom (which stays the default, map 14).

Twelve areas round Hyrule Field: Castle Town, Kakariko Village, Kakariko Graveyard, Death Mountain, Zora's Domain, Kokiri Forest, Lost
Woods, Lon Lon Ranch, Lake Hylia, Gerudo Valley, Gerudo Fortress, Desert Colossus, plus the Field's 12 loot pockets. Seven places hold a
mini boss: Death Mountain (Lava), Zora's Domain (Frost), Kokiri Forest (Moss), Lake Hylia (Tide), Gerudo Fortress (Dune), the Graveyard
(Shade), the Desert Colossus (Stone). Castle Town is the hot drop.

## Where it is

- Source: the map project, `C:\Users\mexic\Documents\Claude\zelda battle royale ultimate map\map` (`areas-compact/poi-00..12.blend`,
  `compact/README.md`). Not in this repo.
- Authoring (`tools/maps/hyrule_third_storm/`): `blender_extract.py` (in Blender: one area's meshes, simplified, plus its chests, floor
  loot, drop spots, water and prop boxes), `export.py` (writes the headers and the debug report), `layout.py` (frames, places, bosses, draw
  ranges), `validate.py` (CI).
- Generated (never edit): `shared/hyrule_third_storm_data.h` (3 MB: walk grid, minimap, places, loot, water; included by every server
  file through `fortnite_map.h`), `shared/hyrule_third_storm_collision.h` and `_model.h` (only the game and the map test include them). The
  big tables are base64 text in 16,000-character pieces: a compiler handles tens of MB of string literal, but ran out of memory on
  millions of numbers in braces. The walk grid is delta/varint coded, its flags run-length coded.
- Hand-written: `shared/hyrule_third_storm_ground.h` (decodes the walk grid and minimap once; floor, water, flags),
  `shared/hyrule_third_storm_draw.h` (decodes the drawn map and the collision; hooks the collision into `fortnite::BuildCollision`), `shared/hyrule_third_storm_layout.h` (placement, loot, colliders, bosses).
- Game: `DrawHyruleThirdStorm` in `Terrain.inc`; hooks in `Core.inc`, `EngineBridge.inc` (surfaces, water boxes, far clip), `Water.inc`,
  `GroundWeather.inc`, `Scenery.inc`, `Hud.inc` (release map, minimap extent), `war_table/Painter.inc`; server `game_server.h`, `match.h`.
- Engine: `patches/0040-royale-collision-plane-distance-32.patch` (below).

## Rebuild

```
blender -b <map>/areas-compact/poi-NN.blend --factory-startup --python tools/maps/hyrule_third_storm/blender_extract.py -- --out <dir> --detail 0.3
blender -b <map>/areas-compact/poi-NN.blend --factory-startup --python tools/maps/hyrule_third_storm/blender_extract.py -- --out <dir> --detail 0.06 --far
python tools/maps/hyrule_third_storm/export.py --src <dir>
python tools/maps/hyrule_third_storm/validate.py
```
Run the two Blender lines for every area 00..12 (about 2 minutes an area; one Blender at a time, they need several GB). `export.py` takes
about 2 minutes.

## How it is put together

- **Frame.** Compact metres (south-west origin, +z up) to game units: x = (x - 900) * 36, y = z * 36, z = -(y - 900) * 36 (north is -z).
  The map spans -32400..32400: inside the 16-bit world. Scenery past +-32760 (the Field's rim) is left out.
- **Collision** is the drawn ground and solid meshes themselves (what you stand on is what you see): about 325,000 triangles, a surface
  each (footsteps, climbable steep ground, rock and ivy, hook-shot wood). Props get boxes (trees: trunk only).
- **Engine limit raised for it.** A collision triangle stores its plane as a normal and a distance `-(normal . corner)`. That distance was
  16 bit; a tilted wall near a corner of this map needs about 45,800. `patches/0040` makes it 32 bit (ROM scenes are sign-extended on load),
  and `fortnite_map.h` `Poly::dist` is 32 bit to match. About 6,500 of the map's triangles need it.
- **The walk grid** (1025 x 1025 points, 64 units apart) is the floor the server, the bots and the placement rules use: at every point the
  lowest floor with head room that is not inside rock and has no wall standing on it. The same pass finds the map's problems.
- **Drawing.** 4096-unit squares. A square within 7000 units of the camera draws its near mesh; the rest draw a far mesh (the ground as a
  512-unit heightfield plus the tallest structures, 40,000 triangles at most) out to the far clip (46,000). Vertices go to the renderer
  in windows of up to 60 shared corners. Textures: 16 at 256 x 256 (the map project's own stone, timber, cloth, rock, sand plus painted
  grass, dirt, snow), the material's colour and the baked sun in the vertex colour.
- **Match.** The circle (45,900) reaches the square's corners. 236 chests from the Blender markers (tier from the name), floor weapons
  beside them, 7 guarded places, 7 ally stations, carts at the Field's road joins, the lobby on open Field.

## The map debug view (any island map; most useful here)

Turn on **Debug > MapDebug** in the menu (off by default). In a match:
- The engine's real collision is drawn over the map near Link: floors green, walls blue, ceilings red, climbable yellow, hook-shot orange.
  Map you can see with no colour on it has no collision: you would fall through or walk through there.
- A panel: position (game units and map metres), the engine's floor under Link against the walk grid's (a red line when they disagree:
  bots and chests would float or sink there), water/steep flags, the draw counts (near/far triangles, batches), the frame time, the
  nearest place.
- Magenta posts: the exporter's problem spots (holes in the ground; chests with no floor).
- Hold **L**: D-pad **Right/Left** teleports to the next/previous place or problem spot, **Up** changes what is drawn (all, floors, walls,
  nothing), **Down** lifts Link 1500 units (look from above, then fall).

Offline: `export.py` writes `build/hyrule_third_storm/walk.png` (the floor seen from above: height shading, blue water, black no floor,
red too steep, yellow holes, magenta circles chests with no floor, cyan the lobby, labels the places), `minimap.png`, `budget.png`
(triangles in reach from each spot), `report.json` and `problems.json` (hole clusters with coordinates, chests without floor, sliver
triangles by object).

## Checks

- `server/tests/hyrule_third_storm_tests.cpp` (CTest `royale_hyrule_third_storm_tests`): registration, collision corners and planes, the
  engine's lookup-node room, the drawing tables, floors under chests, the bots' grid (places and chests reachable from the lobby, path
  times) and two 6-minute matches with 29 bots (they spread out, loot, never leave the map, server tick time).
- `scripts/test_hyrule_third_storm_authoring.py` (CI): `validate.py` on the generated headers.
- Headless game (`docs/PLAYTEST.md`): `solo-15`, `match-15`, `perf-15`, `tour-15` (every place with the debug view on, a screenshot each),
  `look-15`.

## Status (2026-10-27)

Last export: 325,000 collision triangles; 712,000 near and 70,000 far drawn triangles; about 112,000 triangles in reach on average, 172,000
at worst (Castle Town), of which the game draws what is in front (about half). 236 chests (220 on a floor within a step), 156 floor-loot
spots, 202 water boxes. Server test: bots reach 23 of 24 places and 204 of 236 chests from the lobby; a tick takes about 1 ms (worst 22 ms);
laying the map out (the bots' grid) takes 3 to 4 seconds on a desktop.

Known problems (the debug view and `walk.png` show them):
- Gerudo Fortress sits on a plateau the bots cannot reach from the Field (cliff edges all round in the walk grid); players can climb.
- About 16 chests have no floor within a step (most are on upper floors or in Death Mountain's and Kakariko's rock-cut rooms).
- The map's own known join problems (`map/notes/world.md`): steps of up to 1 m at some road ends, edge lips up to 17.7 m (Kakariko).
- Not yet run on the Odin: frame rate, the look of the baked colours and textures, and the time to lay the map out on the phone.

Headless playtest (Linux, software rendering, 2026-10-27): solo-15, match-15 (31 bots, 22 drawn), perf-15 and tour-15 (24 stops) all pass.
perf-15 ran 17.5 game updates a second of 20, but 40% of the time is Mesa's software rasteriser (no GPU in WSL), so it says nothing about the
phone. What it took:
- The engine's collision list overflowed and crashed (`StaticLookup_AddPolyToSSList`): `patches/0041` gives it 32 MB (room for 3.1 million
  nodes, 1.44 million needed) and makes a full list drop a poly instead of writing past the table. `server/tests` checks the budget.
- The generated tables compile in their own file, `mod/Royale/RoyaleThirdStormTables.cpp` (one translation unit with them ran out of memory).
- The bus opened its doors over the void: the map circle reaches the square's corners, so most of the bus line inside it lies past the square's
  edges (solo-15 landed at x 36574). The jump window is now also clipped to +-`kBusJumpWorldMax` (30000) on x and z (`shared/bus.h`).

Still to look at: tour stop 17 (River Bridge Camp, 3840 x 10816) left the camera under the river bank; tour stop 2 (Kakariko Graveyard)
shows stacked red roof planes, not collision (no ceilings there): check both on the Odin with the debug view.
