# Hyrule Kingdom

The main and default Battle Royale map (map id 8, after Convergence at 7; no other map's id or collision changed). It is a Fortnite-style island built from Zelda places: a castle on a plateau over Zora's River, a clock-tower market town, a stilt village on a lake, a desert ruin, a snowy lodge, a volcano, a forest village, a ranch, docks, a waterfall and more, joined by open field, a great stone bridge and the castle viaduct. It plays in the Hyrule Field scene (like Fortnite, Sandbox and Convergence), so its field music plays automatically, and its collision is swapped in by the existing hook (patch 0013).

Nothing is scattered over it. Trees, rocks, ruins, fences, chests' homes and props are all modelled into the map; the random boulder and prop generation is off here.

## Places and bosses

Twenty-four named places: Hyrule Castle, Clock Town, Lake Hylia Stilts, Lakeside Lab, Gerudo Ruins, Snowpeak Lodge, Death Mountain, Kakariko Village, Kokiri Forest, Lon Lon Ranch, Temple Ruins, Ordon Docks, Zora's Falls, Great Hylia Bridge, Castle Bridge, Hyrule Field, Windmill Hill, Kakariko Graveyard, Deku Tree Hollow, Blossom Oasis, Frozen Pond, Goron Lookout, Fairy Fountain and Lighthouse Point.

Seven of them hold a mini boss at their authored home, matched by place so the kind suits the scenery: Hyrule Castle, Lake Hylia Stilts, Gerudo Ruins, Snowpeak Lodge, Death Mountain, Kakariko Village and Kokiri Forest. The major boss is unchanged.

## How it is built

`tools/maps/kingdom/` holds the Python and Blender sources: `terrain.py` (heightmap, colours, water), `textures.py` (37 hand-painted 128 x 128 textures), `kit.py` (houses with interiors, towers, bridges, walls, trees), `pois.py` (the 17 place builders), `layout.py`/`world.py`, `blender_build.py` (renders and previews). `export.py` bakes the result into `shared/kingdom_data.h` (terrain, collision, obstacles, buildings, walkways, chest sites, regions) and `shared/kingdom_model.h` (drawing data). `surface_maps.py` makes the bump and normal maps. Run `python3 tools/maps/kingdom/export.py` after changing a builder (about a minute).

- Collision is baked terrain plus every solid. Polygon indices go past the 13-bit limit of the original collision loader, so patch 0025 widens the one place that truncated them.
- Buildings have furnished interiors, doorways and no loading zones. Upper floors are reached by ramps inside, and roofs by ramps or ivy.
- Chests sit at 84 authored sites (`kLootSites`, with a height each), balanced by the placement rules in `docs/PLACEMENT.md`.
- Exterior stone, rock, wood, roof, thatch, cobble, plaster and bark triangles are drawn lit with a fixed sun and use the surface bump and normal maps of the Graphics page ("Surface detail"). Interiors keep their baked light.

## Bots

The bots' grid (`server/nav.h`) is built from the baked ground, building floors and walkways (`kWalkways`: decks, bridges and boardwalks), with doorways open and walls closed. The grid is one height per cell, so chests upstairs and on roofs (about half of the sites) are marked as out of reach for bots (`NavGrid::MarkUpper`, `BuildRegions`/`Connected`): they are for players. Bots reach every ground-level place connected by land, the viaduct and the great bridge. The lake stilts, the lab island and some cliff ledges need swimming, so bots skip those chests. A route graph with stairs and ramps as portals, so bots can use upper floors, is the next step.

## Tests

`server/tests/kingdom_tests.cpp` (also in `tools/maps/run_map_tests.cmd`) checks the data, collision indices, layout, bosses, chests, and that the main places are joined for bots.
