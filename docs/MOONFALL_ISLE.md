# Moonfall Isle

A release Battle Royale map (map id 12; Great Hyrule Kingdom, id 14, is now the default). Other maps keep their ids and stay selectable in the map menu. It is one island that mixes Ocarina of Time (the Temple of Time, Hyrule Castle, Kakariko, Lost Woods, Deku Tree), Majora's Mask (Clock Town, Snowhead, Great Bay, Ikana, Romani Ranch, the falling Moon) and Fortnite's structure (named places, a hot drop in the middle, bridges and roads between them, loot under roofs and on high ground).

It plays in the Hyrule Field scene with swapped collision, like the other authored maps. Nothing is scattered over it: every tree, rock, house and prop is placed by the map's own builders.

## Scale

Everything is sized from adult Link, 68 units tall (1 m = 40 units). Doors are 110 x 150, a storey is 210, a table is 32 high, stairs climb at most 0.55, steps are at most 20. The kit lives in `tools/maps/moonfall/kit.py` (a rescaled copy of the Kingdom kit, which was too big). A room needs at least about 360 x 360 for a walkable staircase; the builder raises an error if it is smaller.

## Places and bosses

24 named places. Seven hold a mini boss at the place's home: Hyrule Castle (Stone), Snowhead Lodge (Frost), Spirit Colossus (Dune), Goron Forge (Lava), Ikana Graveyard (Shade), Great Bay Stilts (Tide), Deku Tree Village (Moss).

| Place | Boss | What it is |
|---|---|---|
| Hyrule Castle | Stone | the white castle on its plateau: walls, gatehouse, towers, a keep and waterfalls off the cliff |
| Clock Town | none | the walled town round the clock tower: rooftops, stalls and the Milk Bar |
| Temple of Time | none | the broken temple in the middle of the meadow: the hot drop |
| Snowhead Lodge | Frost | a timber lodge and cabins on the snowy shelf under the peaks |
| Spirit Colossus | Dune | sandstone ruins round the seated stone giant |
| Goron Forge | Lava | the red-stone fort on the volcano's shelf, over the lava, with its lookout tower |
| Kakariko Windmill | none | red-roofed houses round the windmill at the mountain's foot |
| Ikana Graveyard | Shade | headstones and the crypt in Kakariko's north yard |
| Romani Ranch | none | a fenced ranch: barn, silo, house and paddocks |
| Great Bay Stilts | Tide | a fishing village on stilts in the lagoon, joined by boardwalks |
| Deku Tree Village | Moss | the hollow giant tree with decks and huts in its branches |
| Lost Woods | none | a dark maze of great oaks on the way to the Deku Tree |
| Astral Observatory | none | the stargazer's tower on its knoll |
| Zora Cape | none | the lighthouse, piers and boat sheds facing the lagoon |
| Castle Falls | none | the pool at the foot of the castle's waterfall |
| Moonfall Span | none | the high stone bridge from the castle's west road to the Snowhead shelf |
| Milk Road | none | the ranch road's bridge over the west river |
| Fairy Fountain | none | a hidden fountain ring in the pines |
| Termina Bridge | none | the stone bridge over the east river's mouth |
| Lantern Bridge | none | the lantern-lit bridge from the meadow to Clock Town's gate |
| Moon Crater | none | the cracked crater where the moon fell: its chests are the richest on the isle |
| Frozen Pond | none | an iced pool between the peaks with a fishing hut |
| Spirit Oasis | none | palms and a pavilion round a pond at the desert's edge |
| Fisherman's Hut | none | a hut on its own little island in the lagoon |

Hireable NPCs wait at 8 stations (two Kokiri, two Zora, two Goron, two Gerudo, each beside the place that suits them). Six cart stations stand along the roads. Chests: 125 authored sites, all used per match (plus floor weapons beside them; Rare+ chests spill a second item), spread over rooftops, upper floors, towers, ledges, piers, a crater floor and cliff feet, each in a reachable spot.

## How it is built

`tools/maps/moonfall/`: `layout.py` (places, roads, rivers, pads, ally and cart spots), `terrain.py` (heightmap, colours), `kit.py` (houses, towers), `pois.py` (the place builders), `routes.py` (upper floors for bots, found from the baked collision), `export.py` (bakes `shared/moonfall_data.h` and `shared/moonfall_model.h`), `validate.py` (automated checks), `blender_build.py` (the `.blend` and the preview renders). Shared pieces (geometry, textures, surface maps) come from `tools/maps/kingdom/`. Run `python3 tools/maps/moonfall/export.py` after changing a builder (about a minute).

Collision is one baked table (terrain plus every solid). It has to stay under the engine's vertex limit (patch 0025b), so the exporter merges deep sea, picks the high-numbered vertices so that no triangle has two, and keeps small decoration non-solid.

## Checks

`python3 tools/maps/moonfall/validate.py` measures the baked collision and the bots' walking rules, not the intent:

- scale: door, storey, stair slope and step sizes against Link;
- every chest has a floor under it, headroom, no wall through it, and a side to stand on;
- every building's registered floor matches the collision, with a ceiling Link fits under;
- every door has floor on both sides, an open frame and a high enough lintel;
- no solid floats, and no building or chest is under water;
- a flood fill from the lobby with the bots' step, drop, swim and climb rules must reach every place, ally and cart station, chest and all but a few dozen cells of land.

Current result: 100.0% of the land reachable, 0 failures. The warnings are slope faces between 0.55 and 0.88 on rocks and roofs, and one broken arch on the old gate. `scripts/test_moonfall_authoring.py` runs the same checks in CI. `server/tests/moonfall_tests.cpp` checks the generated data in the game build.

## Previews

`python3 tools/maps/moonfall/blender_build.py --renders all --out <dir>` (needs `bpy`) writes `moonfall_isle.blend` and one render per place.
