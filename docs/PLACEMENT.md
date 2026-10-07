# Where chests, boulders and scenery go

Everything is laid out by `shared/placement.h` (`PlaceMap`) from the match seed, the map's circle and the two ground probes the host measures
(`valid`: is there floor, `height`: how high). Nothing is random on its own any more: each piece is tied to the ground or to another piece.

| Piece | Rule |
| --- | --- |
| Towns | as before (`poi.h`), plus a **camp** outside each: a chest in a ring of rocks and bushes, open towards the town (`AddCamps`). |
| Loose scenery | `GenerateProps` makes clusters: groves of bushes, rock fields with a standing stone, and lines of boulders along the **foot of cliffs** (`FindTerrainFeatures`). Never on a bank steeper than 0.65, never overlapping another piece or a chest. |
| Formations, outposts, climbs, hideaways | `GenerateWilds`, now only on ground that is not steep (0.6). |
| Lookouts | the summits and plateau edges (ground that drops away on every side): a chest between two standing stones, always Epic or better. |
| Scattered chests | `GenerateAnchoredLoot`: one per anchor, picked at random: boulder nooks, groves, stone clusters, cliff feet and, on the Fortnite Map, the oaks and cliff slabs of its own scenery (`island_anchors.h`). On Convergence, its authored buildings and walls. |

A chest spot (`ChestSpotOk`) must be level (slope 0.3 or less), have floor right round it, be clear of every solid piece and the map's edge, and not be on a town's streets (the
towns have their own chests indoors). Scattered chests are 70% of the requested count and keep `max(450, 8% of the radius)` apart; the towns keep one chest per 320 units. Boulders only come in groups of two or more. At most a fifth of the requested count is ever put in the
open, so a map with little scenery has fewer chests instead of chests with nothing round them.

The sandbox is laid out by hand (`sandbox_layout.h`) and is not touched.

## Seeing it

```
g++ -std=c++17 -O2 -Iserver -Ishared -Iclient -Imod/Royale server/tools/placement_preview.cpp -o preview   # or the royale_placement_preview target
./preview 5 1 dump.txt                       # map 0-7, seed; prints how many chests are beside scenery, in the open, on slopes or inside a solid
python3 tools/maps/placement_preview.py out.png dump.txt [older-dump.txt]
```

Gold diamonds are chests with something beside them, red ones are out in the open. The Fortnite Map, the sandbox and Convergence use their real ground. The five Ocarina of
Time scenes have a floor only inside the game, so the preview gives them a stand-in (a ridge, a plateau and rolling ground) inside the scene's own circle:
it shows the rules, not the real scene.
