# Maps: what the ROM says

These numbers come from the game's own collision data, read with `tools/rom-extractor.html` ("Analyse ALL maps"). They are the ground truth the
five battle royale maps are fitted to.

| Map | Scene | Collision bounds (x x z) | Floor centre (x, z) | Radius holding 95% of the floor | Loading zones / exit surfaces | Water boxes | Actors |
|---|---|---|---|---|---|---|---|
| Hyrule Field | 0x51 | -11048..6732 x -1811..16529 | -1269, 6635 | 7421 | 0 / 9 | 6 | 73 |
| Lake Hylia | 0x57 | -5094..3103 x -1671..9290 | -1072, 5314 | 3557 | 2 / 5 | 4 | 56 |
| Kakariko Village | 0x52 | -4501..2640 x -4766..1866 | 50, 276 | 2500 | 9 / 14 | 1 | 57 |
| Death Mountain Crater | 0x61 | -3692..3487 x -3692..4063 | -136, -5 | 2045 | 1 / 4 | 0 | 40 |
| Desert Colossus | 0x5C | -4669..11320 x -8347..8680 | 2648, 85 | 4684 | 0 / 5 | 1 | 39 |

What changed because of it:

* **The measuring window.** The game used to probe the floor only inside x, z = -9000..9000, which cut Hyrule Field (x -11048..6732, z -1811..16529)
  and put its centre in the wrong place. It now probes the scene's own collision bounds, with a step that grows with the area.
* **Fallback circles.** Before a match starts the lobby world is built on a guess of the map's size. The guesses were all centred on (0, 0),
  which is nowhere near the ground on most maps (Hyrule Field's floor is centred at -1269, 6635; Lake Hylia's at -1072, 5314). They now use the
  numbers above. The host's game still measures the live scene when a match starts, then rebuilds the world once.
* **Exits.** Kakariko has nine loading zones and fourteen exit surfaces, the field has nine exit surfaces: more than anything else, these decide
  whether players get shoved back by the exit seal. Spawns, chests and storm centres are now kept off exit floor and 380 units away from every
  loading zone.
* **Real landmarks.** Each scene's actor list (grass, rocks, pots, gossip stones, doors) is in the analysis file; it is the place to look when
  placing points of interest on real buildings instead of random spots.

To refresh the numbers, drag a ROM onto `tools/rom-extractor.html`, press "Analyse ALL maps" and keep the JSON it saves.
