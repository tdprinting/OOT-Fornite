# Skyloft Isles

Battle Royale map id 13: one terraced island in the open sea, with islets round it joined by bridges. Layout inspired by Skyloft (Skyward
Sword); every shape is the project's own low-poly OoT-style geometry, built with Moonfall's kit (nothing from any game rip is in the repo).
Moonfall Isle stays the default; Skyloft Isles is picked in the map menu like the others.

It plays in the Hyrule Field scene with swapped collision, like Moonfall (`shared/fortnite_map.h`, map 13). Nothing is scattered over it:
every house, bridge, tree and rock is placed by `tools/maps/skyloft/`.

## Shape

About 11,000 x 9,700 units of land in the 14,800 x 15,400 grid, the sea at -227 all round. Five height levels, so the island reads from
afar: the plateau (100), the academy terrace (380) and shelf (520), the statue hill (560), the east cliffs (400) with the Waterfall Cave's
gorge cut to 40, the south headland (60). Two sea channels cut into the island: the academy channel (crossed by the Academy Bridge) and the
east gully (crossed by the East Span). Four islets: Beedle's Perch (west), the Lumpy Pumpkin (south east), Ember Rock (south) and the Loftwing
Roost crag (640, north east), each reached by a bridge.

## Places and bosses

24 named places. Seven hold a mini boss: Sparring Hall (Stone), Ember Rock (Lava), Loftwing Roost (Frost), Pumpkin Fields (Moss), Waterfall
Cave (Tide), Hollow Graveyard (Shade), Goddess Statue (Dune). The major boss is Morpha (Theme::Water).

| Place | Boss | What it is |
|---|---|---|
| Knight Academy | none | the whitewashed, red-roofed academy on its high lobe: hall, headmaster's tower, two dorms, courtyard, pond |
| Bazaar | none | the great striped tent in the middle of the plateau with four shops inside: the hot drop |
| Goddess Statue | Dune | the stone goddess on her hill over a shrine; the Goddess Stair winds up to her |
| Sparring Hall | Stone | the knights' training hall and yard on the academy terrace |
| Waterfall Cave | Tide | the gorge in the east cliffs: falls from the cliff-top reservoir into a pool, a rock-walled cave with ledges |
| Lumpy Pumpkin | none | the pumpkin-roofed inn on its islet over the Pumpkin Bridge |
| Light Tower | none | the lighthouse on the south headland: the tallest thing on the isle |
| Pumpkin Fields | Moss | fenced pumpkin patches, a barn and the east windmill |
| Westwind Homes | none | six pastel cottages, gardens and a lookout on the west lobe |
| Hollow Graveyard | Shade | headstones round the old crypt on the east lobe |
| Loftwing Roost | Frost | the bare crag with the great birds' nests, by a rope bridge from the statue hill |
| Ember Rock | Lava | the black islet with the scrap smithy, over the Ember Bridge |
| Statue Plaza | none | the paved plaza with the stone bird, fountain, lamps and benches |
| Academy Bridge, East Span, Pumpkin Bridge, Ember Bridge | none | the bridges (stone with parapets, or rope and plank) |
| Goddess Stair | none | the switchback stair road up the statue hill |
| Beedle's Perch, Fledge's Cove, Waterfall Pool, Windmill Hill, West / South Diving Deck | none | the small places: the merchant's tower, the boat cove, the pool's jetty, the north windmill, the launch decks over the sea |

Hireable NPCs wait at 7 stations, one per `AllyKind` in order (the server places kind i at station i): Kokiri at the fields' barn, Zora at
the Waterfall Pool jetty, Goron at the Ember Rock smithy, Gerudo at the top of the Goddess Stair, Jones at the Academy Bridge's terrace end,
Peter at the Lumpy Pumpkin's door, Master Chief at the Light Tower. Six cart stations stand at the road junctions. Chests: about 95
authored sites, 84 used per match, on floors, upper floors, roofs, tower tops, bridges, ledges and decks.

## Navigation

Every district has a landmark visible from far (the Light Tower south, the Goddess Statue north east, the headmaster's tower north west, the
two windmills, the Bazaar's striped tent in the middle) and its own colours (whitewash and red tile at the academy, striped canvas at the
Bazaar, pastel plaster and blue, green and teal tile at the homes, thatch and timber at the fields, dark brick at the graveyard and Ember Rock,
pumpkin orange at the inn). Dirt roads join every place; signposts stand at the junctions; bridges cross every gap; the cliffs round the
island are climbable nowhere but the ivy walls and the beaches at the Waterfall Pool and Fledge's Cove, so water is a swim back to a cove,
not a shortcut.

## How it is built

`tools/maps/skyloft/`: `layout.py` (places, lobes, channels, pads, roads, stations), `terrain.py` (heightmap and colours), `textures.py`
(Kingdom's textures plus the isle's own), `pois.py` (the place builders), `kit.py`, `routes.py`, `export.py`, `validate.py`,
`blender_build.py` (copies of Moonfall's, renamed). Run `python3 tools/maps/skyloft/export.py` after changing a builder (a few minutes); it
writes `shared/skyloft_data.h` and `shared/skyloft_model.h`. `python3 tools/maps/skyloft/validate.py` runs Moonfall's checks (scale, chests,
doors, floating solids, reachability from the lobby); `scripts/test_skyloft_authoring.py` runs them in CI and `server/tests/skyloft_tests.cpp`
checks the generated data in the game build.

Engine and server hooks mirror Moonfall's: `shared/map.h` (id 13, names, `kSkyloftMapIndex`), `shared/skyloft_layout.h`, `shared/fortnite_map.h`,
`Core.inc`, `EngineBridge.inc`, `Scenery.inc`, `Terrain.inc`, `Water.inc`, `war_table/Painter.inc` (map-select preview), `server/game_server.h`,
`server/match.h`. The HUD minimap and the war-table map come from the baked ground colours, as on Moonfall.

## Status

Build and tests pass; not yet run in the game.
