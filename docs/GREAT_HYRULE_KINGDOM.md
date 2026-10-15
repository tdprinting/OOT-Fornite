# Great Hyrule Kingdom

Battle Royale map id 14 and the default (main) map: `shared/map.h` `kGreatHyruleKingdomMapIndex`, `kDefaultMapIndex`. One big Hyrule on
Tee's fantasy landscape: a lagoon in the north, farmland and a central plateau, desert uplands west, snow peaks south west, Death Mountain
south east and a river down the east side. The older maps keep their ids and stay selectable (Moonfall Isle, Hyrule Kingdom and Skyloft
Isles in the release list, the rest in developer mode).

It plays in the Hyrule Field scene with swapped collision, like Moonfall and Skyloft (`shared/fortnite_map.h`, map 14). Nothing is
scattered over it: everything is baked by `tools/maps/great_hyrule_kingdom/`.

## Where it is

- Authoring: `tools/maps/great_hyrule_kingdom/` (`layout.py` places, water and pads; `prepare_landscape.py` and `landscape/` the source
  ground; `terrain.py`, `textures.py` (256 x 256), `pois.py`, `kit.py`, `routes.py`, `world.py`, `export.py`, `validate.py`, `blender_build.py`).
- Generated (never edit by hand): `shared/great_hyrule_kingdom_data.h`, `shared/great_hyrule_kingdom_model.h`, namespace
  `royale::great_hyrule_kingdom`.
- Hand-written: `shared/great_hyrule_kingdom_layout.h` (heights, water, placement, POI layout, boss kinds).
- Hooks (mirror Skyloft's): `RoyaleMod.cpp`, `Core.inc`, `EngineBridge.inc`, `GroundWeather.inc`, `Hud.inc` (release map), `Scenery.inc`,
  `Terrain.inc`, `Water.inc`, `war_table/Painter.inc`, `Menus.inc`/`Controls.inc` (default), `server/game_server.h`, `server/match.h`.
- Its textures are 256 x 256 RGBA16 (the other maps' are 128). The exporter writes 4096 texture units per repeat as for 128, so
  `Terrain.inc` loads a 256 texture with mask 8 and shift 15 (coordinates times two).

## Rebuild

```
python3 tools/maps/great_hyrule_kingdom/prepare_landscape.py
python3 tools/maps/great_hyrule_kingdom/export.py
python3 tools/maps/great_hyrule_kingdom/validate.py
```

CI runs `scripts/test_great_hyrule_kingdom_authoring.py` (validate.py) and `server/tests/great_hyrule_kingdom_tests.cpp`.

## The 24 places

In `kRegions` order (POI names follow Skyloft's, `PoiNameBase(14)` = 294). Seven hold a mini boss.

| # | Place | Boss |
|---|---|---|
| 0 | Castle Town (the hot drop) | none |
| 1 | Hyrule Castle | Stone |
| 2 | Lake Hylia | Tide |
| 3 | Hylia Docks | none |
| 4 | Ordon Village | none |
| 5 | Lon Lon Ranch | none |
| 6 | Kokiri Forest | Moss |
| 7 | Lost Woods | none |
| 8 | Great Hylia Bridge | none |
| 9 | Zora's River | none |
| 10 | Zora's Domain | none |
| 11 | Kakariko Village | none |
| 12 | Kakariko Graveyard | Shade |
| 13 | Hyrule Field Ruins | none |
| 14 | Gerudo Fortress | Dune |
| 15 | Haunted Wasteland | none |
| 16 | Desert Colossus | none |
| 17 | Temple of Time | none |
| 18 | Snowpeak Lodge | Frost |
| 19 | Snowpeak Summit | none |
| 20 | Lanayru Spring | none |
| 21 | Goron City | Lava |
| 22 | Death Mountain | none |
| 23 | Death Mountain Trail | none |

## Water

The sea and the northern lagoon at -227. Raised water is 8 boxes in `kPools`: Zora's Domain lake (380), Lanayru Spring (480) and the
river's 6 stepped pools falling north to the lagoon (waterfalls between them). Each is its own game water box (`EngineBridge.inc`), its own
level in `WaterHeightAt`/`GreatHyruleKingdomWaterY` and its own bot swim area (`game_server.h` `AddWater`); none of these has a pool limit.

## Status

Wired into game and server; ground-only export so far (no buildings, loot sites, doors or drawn models yet). Not yet run in the game.
