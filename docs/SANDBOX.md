# The Sandbox test map

A flat test arena for trying every feature alone. Start it from the Battle Royale menu (**Sandbox test map**, under *Solo test*). It is a solo game: no
countdown, no drop, nobody else, the storm waits and nothing can hurt you (switch that off in the panel).

While it is running, open the Battle Royale menu again: the **SANDBOX** panel is at the top of the match page.

## The ground

`shared/sandbox_terrain.h` builds the heights, colours and ground cover. It uses the Fortnite Map's mesh and drawing code with different data
(`fortnite::UseTerrain(true)`), so everything that works on the island (swimming, collision, water level, flora cover) works here.

| Place | What it is for |
|---|---|
| Spawn Pad | start; smash rows of rocks and bushes just south of it |
| Loot Plaza | every item in the game in rows, plus a chest of each rarity and a Heart Container chest |
| Cart Lot | Lon Lon Buggies, a ring road, a kicker (jump) and a bumpy road |
| Boss Pad | flat and empty; bosses appear in front of you wherever you are |
| Glider Hill | a 400 tall hill. Walk up, then press **Glide from here** (3000 up, hold Z to dive) |
| Test Pond | swimming, water effects |
| Ramp Row | ramps of 17 and 27 degrees you can walk, and a 56 degree one you cannot; a plateau on top |
| Cliff Edge | a sheer 350 tall cliff with a 26 degree ramp up from the south |
| Block Course | stone steps 60, 120 and 180 tall, and gaps to jump |
| Cover Yard | boulders (every shape), pillars and a wall to hide behind |
| Grove | a few trees and tall grass |

`shared/sandbox_layout.h` places the props and names the zones. `docs/` previews: `/mnt/project-files/sandbox-map/sandbox-preview.png`.

## The panel

Go to a place; carts; bots (and "stand still" for a target dummy); every boss; give any item at any rarity; heal; the storm (on/off, jump to a circle);
supply drops; weather (season, sky, strength) and time of day; wind, tornado, cloth, grass; and the Debug switches (one per newer feature).

The server side is `Match::Sandbox*` in `server/match.h` and only works in a sandbox match. The panel never touches the server directly: it queues a
`SandboxCmd`, and `RunSandboxCommands` (RoyaleMod.cpp, run once per frame on the game thread) carries it out.

## Adding a feature to the sandbox

* **A feature with a Debug switch** (water, fog, sky, scenery...): nothing to do. The panel shows the whole Debug section, so the new switch is there.
* **A feature that needs a button** (a spawner, a test dummy, a ragdoll trigger): add a `SandboxCmd::Kind`, a case in `RunSandboxCommands`, and a button in
  `DrawSandboxPanel`. If it needs the server, add a `Match::Sandbox...` method next to the others and test it in `server/tests/tests.cpp` (`SandboxCommands`).
* **New terrain for a feature** (a ledge to ragdoll off, a shore for wakes): add it to `Height()` in `sandbox_terrain.h` (use `raise(...)`, and remember the ground
  is 64 x 64 squares about 235 units wide, so a feature smaller than that disappears), and a zone to `kZones` (the first twelve of the Sandbox's 24 place names).
* New per-frame code starts with `Feat("name")` so a crash note names it.
